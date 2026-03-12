/**
 * person_detect.c
 * Always-On Person Detection + Bounding Box
 *
 * Model:  MobileNetV1 0.25x width multiplier
 *         Input:  96×96 grayscale INT8
 *         Output: [person_prob, bbox_x, bbox_y, bbox_w, bbox_h] (INT8 dequant)
 *         Size:   ~74KB INT8 TFLite flatbuffer
 *
 * Runs at 5fps on HW-AccAI.
 * Person absent >10 s → SYS_STATE_LOST_USER.
 */

#include "person_detect.h"
#include "hal.h"

/* Flash address of person detection model (bank A or B via OTA) */
extern const uint8_t g_person_model[];
extern const uint32_t g_person_model_size;

/* Working memory for HW-AccAI output: 5 INT8 values */
#define PERSON_OUTPUT_SIZE  8U
static int8_t  s_output_buf[PERSON_OUTPUT_SIZE] __attribute__((aligned(4)));

/* Pre-processed 96×96 INT8 input */
static int8_t  s_input_buf[CAM_SMALL_WIDTH * CAM_SMALL_HEIGHT]
               __attribute__((aligned(16)));

/* Person-absent timer */
static uint32_t s_last_person_ms = 0;
#define PERSON_LOST_TIMEOUT_MS  10000U

/* Simple IIR tracking: exponentially smooth bbox */
typedef struct {
    float x, y, w, h;
    bool  valid;
} bbox_track_t;

static bbox_track_t s_track = {0};
#define BBOX_ALPHA   0.4f   /* IIR smoothing coefficient */

/* ─────────────────────────────────────────────
   Image pre-processing
   Resize 320×240 → 96×96 bilinear, normalize to INT8 [-128..127]
   ───────────────────────────────────────────── */
static void preprocess_frame(const uint8_t *src320x240, int8_t *dst96x96) {
    const float x_scale = (float)CAM_WIDTH  / (float)CAM_SMALL_WIDTH;
    const float y_scale = (float)CAM_HEIGHT / (float)CAM_SMALL_HEIGHT;

    for (uint32_t dy = 0; dy < CAM_SMALL_HEIGHT; dy++) {
        float fy = dy * y_scale;
        uint32_t sy0 = (uint32_t)fy;
        uint32_t sy1 = MIN(sy0 + 1, CAM_HEIGHT - 1);
        float wy1 = fy - sy0;
        float wy0 = 1.0f - wy1;

        for (uint32_t dx = 0; dx < CAM_SMALL_WIDTH; dx++) {
            float fx = dx * x_scale;
            uint32_t sx0 = (uint32_t)fx;
            uint32_t sx1 = MIN(sx0 + 1, CAM_WIDTH - 1);
            float wx1 = fx - sx0;
            float wx0 = 1.0f - wx1;

            /* Bilinear interpolation */
            float val =
                wy0 * (wx0 * src320x240[sy0*CAM_WIDTH + sx0] +
                       wx1 * src320x240[sy0*CAM_WIDTH + sx1]) +
                wy1 * (wx0 * src320x240[sy1*CAM_WIDTH + sx0] +
                       wx1 * src320x240[sy1*CAM_WIDTH + sx1]);

            /* Normalize uint8 [0,255] → int8 [-128,127] */
            dst96x96[dy * CAM_SMALL_WIDTH + dx] = (int8_t)((int32_t)val - 128);
        }
    }
}

/* ─────────────────────────────────────────────
   Output decoding
   Dequantize INT8 → float; scale=1/128, zero_point=0
   ───────────────────────────────────────────── */
static void decode_output(const int8_t *out,
                           person_result_t *result) {
    /* Output layout (5 values):
     * [0] = person_logit  (class score, not softmax)
     * [1] = bbox_cx  (centre x, normalised 0..1)
     * [2] = bbox_cy
     * [3] = bbox_w
     * [4] = bbox_h
     * Quantisation: scale=0.0078125 (1/128), zero_point=0 */
    const float scale = 1.0f / 128.0f;

    float person_logit = out[0] * scale;
    float prob = 1.0f / (1.0f + expf(-person_logit));  /* sigmoid */

    result->confidence_pct = (uint8_t)(prob * 100.0f);
    result->person_present = (prob > 0.55f);

    if (result->person_present) {
        float cx = out[1] * scale;  /* 0..1 */
        float cy = out[2] * scale;
        float bw = out[3] * scale;
        float bh = out[4] * scale;

        /* IIR smoothing */
        if (!s_track.valid) {
            s_track.x = cx - bw / 2.0f;
            s_track.y = cy - bh / 2.0f;
            s_track.w = bw;
            s_track.h = bh;
            s_track.valid = true;
        } else {
            s_track.x = BBOX_ALPHA * (cx - bw / 2.0f) + (1.0f - BBOX_ALPHA) * s_track.x;
            s_track.y = BBOX_ALPHA * (cy - bh / 2.0f) + (1.0f - BBOX_ALPHA) * s_track.y;
            s_track.w = BBOX_ALPHA * bw + (1.0f - BBOX_ALPHA) * s_track.w;
            s_track.h = BBOX_ALPHA * bh + (1.0f - BBOX_ALPHA) * s_track.h;
        }

        result->bbox_x = CLAMP(s_track.x, 0.0f, 1.0f);
        result->bbox_y = CLAMP(s_track.y, 0.0f, 1.0f);
        result->bbox_w = CLAMP(s_track.w, 0.0f, 1.0f);
        result->bbox_h = CLAMP(s_track.h, 0.0f, 1.0f);
    } else {
        s_track.valid = false;
        result->bbox_x = result->bbox_y = 0.0f;
        result->bbox_w = result->bbox_h = 0.0f;
    }
}

/* ─────────────────────────────────────────────
   Public API
   ───────────────────────────────────────────── */
void person_detect_init(void) {
    memset(s_input_buf,  0, sizeof(s_input_buf));
    memset(s_output_buf, 0, sizeof(s_output_buf));
    s_last_person_ms = hal_get_tick_ms();
}

person_detect_status_t person_detect_run(
        const uint8_t   *frame,     /* 320×240 mono8 */
        person_result_t *result) {

    if (!frame || !result) return PERSON_STATUS_ERROR;

    /* Pre-process */
    preprocess_frame(frame, s_input_buf);

    /* Run on HW-AccAI */
    bool ok = hal_accai_run(
        g_person_model,
        g_person_model_size,
        s_input_buf,
        sizeof(s_input_buf),
        s_output_buf,
        sizeof(s_output_buf));

    if (!ok) return PERSON_STATUS_ERROR;

    /* Decode output */
    decode_output(s_output_buf, result);

    /* State tracking */
    uint32_t now = hal_get_tick_ms();
    if (result->person_present) {
        s_last_person_ms = now;
        g_sys.person_present = true;
        g_sys.person_last_seen_ms = now;
        return PERSON_STATUS_DETECTED;
    } else {
        uint32_t absent_ms = now - s_last_person_ms;
        if (absent_ms > PERSON_LOST_TIMEOUT_MS) {
            g_sys.person_present = false;
            if (g_sys.state == SYS_STATE_PERSON_TRACK ||
                g_sys.state == SYS_STATE_GESTURE_SCAN) {
                g_sys.state = SYS_STATE_LOST_USER;
            }
            return PERSON_STATUS_LOST;
        }
        return PERSON_STATUS_ABSENT;
    }
}

void person_detect_build_i2c_msg(
        const person_result_t *result,
        bool person_lost,
        qn_i2c_msg_t *out) {

    out->header   = I2C_MSG_HEADER;
    out->msg_type = person_lost ? MSG_PERSON_LOST : MSG_PERSON_DETECTED;
    memset(out->payload, 0, I2C_PAYLOAD_SIZE);

    if (!person_lost) {
        qn_payload_person_t *p = (qn_payload_person_t *)out->payload;
        p->bbox_x          = Q8_8(result->bbox_x);
        p->bbox_y          = Q8_8(result->bbox_y);
        p->bbox_w          = Q8_8(result->bbox_w);
        p->bbox_h          = Q8_8(result->bbox_h);
        p->confidence_pct  = result->confidence_pct;
        p->tracking_id     = 1;   /* single-object tracker */
    }

    out->crc16 = hal_crc16_ccitt((uint8_t *)out, I2C_MSG_SIZE - 2);
}
