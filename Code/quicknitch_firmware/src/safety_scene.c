/**
 * safety_scene.c
 * 5-class Safety Scene Detection
 *
 * Model:  Lightweight custom CNN
 *         Input:  64×64 grayscale INT8
 *         Output: 5-class softmax [safe_indoor, near_glass, staircase, too_dark, outdoor]
 *         Size:   ~44KB INT8 TFLite flatbuffer
 *
 * Runs at 3fps. Staircase or near_glass → safety_alert to ESP32-S3.
 */

#include "inference_engines.h"
#include "hal.h"

extern const uint8_t  g_safety_model[];
extern const uint32_t g_safety_model_size;

#define SAFETY_INPUT_W  64U
#define SAFETY_INPUT_H  64U
#define SAFETY_INPUT_SZ (SAFETY_INPUT_W * SAFETY_INPUT_H)
#define N_SAFETY_CLASSES 5U

static int8_t s_safety_input[SAFETY_INPUT_SZ]   __attribute__((aligned(16)));
static int8_t s_safety_output[N_SAFETY_CLASSES]  __attribute__((aligned(4)));

/* Hysteresis: require 2 consecutive detections of alert class before firing */
static scene_class_t s_prev_scene  = SCENE_SAFE_INDOOR;
static uint8_t       s_alert_count = 0;
#define ALERT_HYSTERESIS  2

/* ─────────────────────────────────────────────
   Resize 320×240 → 64×64 (nearest-neighbour for speed at 3fps)
   ───────────────────────────────────────────── */
static void resize_to_64(const uint8_t *src, int8_t *dst) {
    const float x_scale = (float)CAM_WIDTH  / (float)SAFETY_INPUT_W;
    const float y_scale = (float)CAM_HEIGHT / (float)SAFETY_INPUT_H;
    for (uint32_t dy = 0; dy < SAFETY_INPUT_H; dy++) {
        uint32_t sy = (uint32_t)(dy * y_scale);
        for (uint32_t dx = 0; dx < SAFETY_INPUT_W; dx++) {
            uint32_t sx = (uint32_t)(dx * x_scale);
            dst[dy * SAFETY_INPUT_W + dx] =
                (int8_t)((int32_t)src[sy * CAM_WIDTH + sx] - 128);
        }
    }
}

void safety_init(void) {
    s_prev_scene  = SCENE_SAFE_INDOOR;
    s_alert_count = 0;
}

bool safety_run(const uint8_t *frame, safety_result_t *result) {
    result->scene          = SCENE_SAFE_INDOOR;
    result->confidence_pct = 0;
    result->alert_required = false;

    resize_to_64(frame, s_safety_input);

    bool ok = hal_accai_run(
        g_safety_model, g_safety_model_size,
        s_safety_input, sizeof(s_safety_input),
        s_safety_output, sizeof(s_safety_output));

    if (!ok) return false;

    /* Softmax decode */
    float scores[N_SAFETY_CLASSES];
    float max_score = -1.0f;
    int   max_class = 0;
    for (int i = 0; i < (int)N_SAFETY_CLASSES; i++) {
        scores[i] = ((float)s_safety_output[i] + 128.0f) / 255.0f;
        if (scores[i] > max_score) { max_score = scores[i]; max_class = i; }
    }

    result->scene          = (scene_class_t)max_class;
    result->confidence_pct = (uint8_t)(max_score * 100.0f);

    /* Hysteresis for alert classes */
    bool is_alert = (result->scene == SCENE_STAIRCASE ||
                     result->scene == SCENE_NEAR_GLASS);

    if (is_alert && result->scene == s_prev_scene) {
        s_alert_count++;
    } else {
        s_alert_count = 0;
    }
    s_prev_scene = result->scene;

    result->alert_required = (is_alert && s_alert_count >= ALERT_HYSTERESIS
                               && max_score > 0.70f);
    return true;
}

void safety_build_i2c_msg(
        const safety_result_t *result,
        qn_i2c_msg_t *out) {

    out->header   = I2C_MSG_HEADER;
    out->msg_type = MSG_SAFETY_ALERT;
    memset(out->payload, 0, I2C_PAYLOAD_SIZE);

    qn_payload_safety_t *p = (qn_payload_safety_t *)out->payload;
    p->alert_class    = (uint8_t)result->scene;
    p->confidence_pct = result->confidence_pct;
    p->flags          = 0;
    if (result->scene == SCENE_STAIRCASE) p->flags |= (1U << 0);
    if (result->scene == SCENE_NEAR_GLASS) p->flags |= (1U << 1);
    if (result->scene == SCENE_TOO_DARK)   p->flags |= (1U << 2);

    out->crc16 = hal_crc16_ccitt((uint8_t *)out, I2C_MSG_SIZE - 2);
}
