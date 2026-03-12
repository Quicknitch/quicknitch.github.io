/**
 * gesture.c
 * Temporal CNN Gesture Recognition over 15-frame optical flow windows
 *
 * Model:  Temporal CNN
 *         Input:  15 frames × 48×48 optical flow (2 channels: dx, dy) INT8
 *         Output: 4-class softmax [wave, point, thumbs_up, cross_arms]
 *         Size:   ~58KB INT8 TFLite flatbuffer
 *
 * Optical flow: sparse Lucas-Kanade at 16×16 grid on 96×96 frames
 * Confidence threshold: 0.85 before emitting gesture event
 * Active only when person_present AND tof_distance < 1.2m
 */

#include "inference_engines.h"
#include "hal.h"

extern const uint8_t  g_gesture_model[];
extern const uint32_t g_gesture_model_size;

/* ─────────────────────────────────────────────
   Optical flow parameters
   ───────────────────────────────────────────── */
#define OF_GRID_COLS    6U
#define OF_GRID_ROWS    6U
#define OF_POINTS       (OF_GRID_COLS * OF_GRID_ROWS)   /* 36 points */
#define OF_PATCH_SIZE   7U                               /* search patch radius */
#define OF_MAX_DISP     8                                /* max displacement px */

/* Flow fields for temporal window */
#define GESTURE_FRAMES  15U
#define FLOW_INPUT_W    48U
#define FLOW_INPUT_H    48U
#define FLOW_CHANNELS   2U   /* dx, dy */

/* Circular buffer of flow frames */
static int8_t s_flow_ring[GESTURE_FRAMES][FLOW_INPUT_H * FLOW_INPUT_W * FLOW_CHANNELS]
              __attribute__((aligned(16)));
static uint8_t  s_flow_ring_idx = 0;
static uint16_t s_flow_frame_count = 0;

/* HW-AccAI output (4 classes) */
static int8_t s_gesture_output[4] __attribute__((aligned(4)));

/* ─────────────────────────────────────────────
   Sparse Lucas-Kanade optical flow
   Estimate flow at OF_POINTS grid positions between curr and prev 96×96 frames
   ───────────────────────────────────────────── */

/* Compute sum of absolute differences between two patches */
static int32_t sad_patch(
        const uint8_t *img,
        uint32_t       img_w,
        int32_t        cx,  int32_t cy,
        int32_t        ox,  int32_t oy,
        int32_t        half) {

    int32_t sad = 0;
    for (int32_t dy = -half; dy <= half; dy++) {
        for (int32_t dx = -half; dx <= half; dx++) {
            int32_t px0 = cx + dx;
            int32_t py0 = cy + dy;
            int32_t px1 = ox + dx;
            int32_t py1 = oy + dy;

            px0 = CLAMP(px0, 0, (int32_t)img_w - 1);
            py0 = CLAMP(py0, 0, (int32_t)CAM_SMALL_HEIGHT - 1);
            px1 = CLAMP(px1, 0, (int32_t)img_w - 1);
            py1 = CLAMP(py1, 0, (int32_t)CAM_SMALL_HEIGHT - 1);

            int32_t diff = (int32_t)img[py0 * img_w + px0] -
                           (int32_t)img[py1 * img_w + px1];
            sad += (diff < 0) ? -diff : diff;
        }
    }
    return sad;
}

/**
 * compute_optical_flow()
 * Brute-force block matching on OF_GRID sparse grid.
 * Outputs flow_dx[], flow_dy[] for OF_POINTS points.
 */
static void compute_optical_flow(
        const uint8_t *prev96,
        const uint8_t *curr96,
        float          flow_dx[OF_POINTS],
        float          flow_dy[OF_POINTS]) {

    const int32_t half = OF_PATCH_SIZE / 2;
    uint32_t pt = 0;

    for (uint32_t row = 0; row < OF_GRID_ROWS; row++) {
        for (uint32_t col = 0; col < OF_GRID_COLS; col++) {
            /* Grid centre in 96×96 space */
            int32_t cx = (int32_t)((col + 0.5f) * (float)CAM_SMALL_WIDTH  / OF_GRID_COLS);
            int32_t cy = (int32_t)((row + 0.5f) * (float)CAM_SMALL_HEIGHT / OF_GRID_ROWS);

            int32_t best_dx = 0, best_dy = 0;
            int32_t best_sad = INT32_MAX;

            for (int32_t dy = -OF_MAX_DISP; dy <= OF_MAX_DISP; dy++) {
                for (int32_t dx = -OF_MAX_DISP; dx <= OF_MAX_DISP; dx++) {
                    int32_t sad = sad_patch(prev96, CAM_SMALL_WIDTH,
                                            cx, cy,
                                            cx + dx, cy + dy, half);
                    if (sad < best_sad) {
                        best_sad = sad;
                        best_dx  = dx;
                        best_dy  = dy;
                    }
                }
            }

            flow_dx[pt] = (float)best_dx / (float)OF_MAX_DISP;  /* normalise -1..1 */
            flow_dy[pt] = (float)best_dy / (float)OF_MAX_DISP;
            pt++;
        }
    }
}

/**
 * Rasterize sparse flow into FLOW_INPUT_W × FLOW_INPUT_H dense map
 * and quantize to INT8.
 */
static void flow_to_dense_int8(
        const float flow_dx[OF_POINTS],
        const float flow_dy[OF_POINTS],
        int8_t *out_frame) {

    /* Bilinear splatting from OF_GRID → dense FLOW_INPUT map */
    float dense_x[FLOW_INPUT_H * FLOW_INPUT_W] = {0};
    float dense_y[FLOW_INPUT_H * FLOW_INPUT_W] = {0};

    /* Simple nearest-neighbour assign for speed */
    uint32_t pt = 0;
    for (uint32_t row = 0; row < OF_GRID_ROWS; row++) {
        for (uint32_t col = 0; col < OF_GRID_COLS; col++) {
            uint32_t r0 = row * (FLOW_INPUT_H / OF_GRID_ROWS);
            uint32_t r1 = MIN(r0 + (FLOW_INPUT_H / OF_GRID_ROWS), FLOW_INPUT_H);
            uint32_t c0 = col * (FLOW_INPUT_W / OF_GRID_COLS);
            uint32_t c1 = MIN(c0 + (FLOW_INPUT_W / OF_GRID_COLS), FLOW_INPUT_W);
            for (uint32_t r = r0; r < r1; r++) {
                for (uint32_t c = c0; c < c1; c++) {
                    dense_x[r * FLOW_INPUT_W + c] = flow_dx[pt];
                    dense_y[r * FLOW_INPUT_W + c] = flow_dy[pt];
                }
            }
            pt++;
        }
    }

    /* Quantize: float -1..1 → INT8 -128..127 */
    for (uint32_t i = 0; i < FLOW_INPUT_H * FLOW_INPUT_W; i++) {
        out_frame[i * 2 + 0] = (int8_t)(dense_x[i] * 127.0f);
        out_frame[i * 2 + 1] = (int8_t)(dense_y[i] * 127.0f);
    }
}

/* ─────────────────────────────────────────────
   Public API
   ───────────────────────────────────────────── */
static uint8_t s_prev_frame[CAM_SMALL_WIDTH * CAM_SMALL_HEIGHT];
static bool    s_prev_valid = false;

void gesture_init(void) {
    memset(s_flow_ring, 0, sizeof(s_flow_ring));
    s_flow_ring_idx   = 0;
    s_flow_frame_count = 0;
    s_prev_valid      = false;
}

bool gesture_run(
        const uint8_t   *frame_curr,
        const uint8_t   *frame_prev_in,
        gesture_result_t *result) {

    result->gesture        = GESTURE_NONE;
    result->confidence_pct = 0;

    /* Need at least 2 frames */
    const uint8_t *prev = s_prev_valid ? s_prev_frame : frame_prev_in;
    if (!prev) {
        memcpy(s_prev_frame, frame_curr, CAM_SMALL_BYTES);
        s_prev_valid = true;
        return false;
    }

    /* Compute flow */
    float flow_dx[OF_POINTS], flow_dy[OF_POINTS];
    compute_optical_flow(prev, frame_curr, flow_dx, flow_dy);

    /* Rasterize into ring buffer slot */
    int8_t *slot = s_flow_ring[s_flow_ring_idx % GESTURE_FRAMES];
    flow_to_dense_int8(flow_dx, flow_dy, slot);
    s_flow_ring_idx++;
    s_flow_frame_count++;

    /* Update previous frame */
    memcpy(s_prev_frame, frame_curr, CAM_SMALL_BYTES);

    /* Need full GESTURE_FRAMES window */
    if (s_flow_frame_count < GESTURE_FRAMES) return false;

    /* Assemble temporally ordered input for inference
     * Re-order ring buffer so oldest frame is first */
    static int8_t s_model_input[GESTURE_FRAMES * FLOW_INPUT_H * FLOW_INPUT_W * FLOW_CHANNELS]
                  __attribute__((aligned(16)));

    uint8_t start_idx = s_flow_ring_idx % GESTURE_FRAMES;
    for (uint32_t f = 0; f < GESTURE_FRAMES; f++) {
        uint8_t ring_pos = (start_idx + f) % GESTURE_FRAMES;
        memcpy(s_model_input + f * FLOW_INPUT_H * FLOW_INPUT_W * FLOW_CHANNELS,
               s_flow_ring[ring_pos],
               FLOW_INPUT_H * FLOW_INPUT_W * FLOW_CHANNELS);
    }

    /* Run inference */
    bool ok = hal_accai_run(
        g_gesture_model, g_gesture_model_size,
        s_model_input, sizeof(s_model_input),
        s_gesture_output, sizeof(s_gesture_output));

    if (!ok) return false;

    /* Decode softmax output */
    float scores[4];
    float max_score = -1.0f;
    int   max_class = 0;
    for (int i = 0; i < 4; i++) {
        scores[i] = ((float)s_gesture_output[i] + 128.0f) / 255.0f;
        if (scores[i] > max_score) { max_score = scores[i]; max_class = i; }
    }

    result->frame_count = s_flow_frame_count;

    if (max_score < 0.85f) return false;   /* confidence threshold */

    result->gesture        = (gesture_id_t)max_class;
    result->confidence_pct = (uint8_t)(max_score * 100.0f);
    return true;
}

void gesture_build_i2c_msg(
        const gesture_result_t *result,
        qn_i2c_msg_t *out) {

    out->header   = I2C_MSG_HEADER;
    out->msg_type = MSG_GESTURE_DETECTED;
    memset(out->payload, 0, I2C_PAYLOAD_SIZE);

    qn_payload_gesture_t *p = (qn_payload_gesture_t *)out->payload;
    p->gesture_id      = (uint8_t)result->gesture;
    p->confidence_pct  = result->confidence_pct;
    p->frame_count     = result->frame_count;

    out->crc16 = hal_crc16_ccitt((uint8_t *)out, I2C_MSG_SIZE - 2);
}
