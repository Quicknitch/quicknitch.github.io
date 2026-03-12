/**
 * face_recog.c
 * User Face Enrollment and Recognition
 *
 * Model:  MobileFaceNet INT8
 *         Input:  112×112 or 96×96 grayscale INT8 (crop from detected bbox)
 *         Output: 128-dim embedding (INT8, dequantized to float32 for L2)
 *         Size:   ~90KB INT8 TFLite flatbuffer
 *
 * Enrollment: capture 10 frames, compute embedding each, store mean in flash.
 * Recognition: compute embedding → L2 distance to stored embedding.
 *              L2 < 0.75 → recognized owner; else → unknown.
 * Runs at 2fps during follow mode.
 */

#include "inference_engines.h"
#include "hal.h"
#include <math.h>

extern const uint8_t  g_face_model[];
extern const uint32_t g_face_model_size;

#define FACE_INPUT_W   96U
#define FACE_INPUT_H   96U
#define FACE_INPUT_SZ  (FACE_INPUT_W * FACE_INPUT_H)

#define FACE_L2_THRESHOLD    0.75f
#define FACE_EMBED_SCALE     (1.0f / 128.0f)

/* Working buffers */
static int8_t  s_face_input[FACE_INPUT_SZ]    __attribute__((aligned(16)));
static int8_t  s_face_output[FACE_EMBED_DIM]  __attribute__((aligned(4)));

/* Enrollment accumulation */
static float   s_enroll_sum[FACE_EMBED_DIM];
static uint8_t s_enroll_count = 0;

/* Stored owner embedding (in SRAM, loaded from flash at init) */
static float   s_owner_embed[FACE_EMBED_DIM];
static bool    s_owner_enrolled = false;

/* Flash storage for mean embedding (128 floats = 512 bytes) */
#define FACE_FLASH_MAGIC    0xFACEBEEFUL
typedef struct {
    uint32_t magic;
    float    embed[FACE_EMBED_DIM];
    uint32_t crc32;
} face_flash_record_t;

/* ─────────────────────────────────────────────
   CRC-32 for face record integrity
   ───────────────────────────────────────────── */
static uint32_t crc32_compute(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }
    return ~crc;
}

/* ─────────────────────────────────────────────
   Face crop and resize from full frame + bbox
   ───────────────────────────────────────────── */
static void crop_and_resize_face(
        const uint8_t  *src320x240,
        float           bbox_x, float bbox_y,
        float           bbox_w, float bbox_h,
        int8_t         *dst96x96) {

    /* Convert normalised bbox to pixel coords */
    int32_t px = (int32_t)(bbox_x * CAM_WIDTH);
    int32_t py = (int32_t)(bbox_y * CAM_HEIGHT);
    int32_t pw = (int32_t)(bbox_w * CAM_WIDTH);
    int32_t ph = (int32_t)(bbox_h * CAM_HEIGHT);

    /* Add 10% padding to bbox */
    int32_t pad_x = pw / 10;
    int32_t pad_y = ph / 10;
    px = CLAMP(px - pad_x, 0, (int32_t)CAM_WIDTH  - 1);
    py = CLAMP(py - pad_y, 0, (int32_t)CAM_HEIGHT - 1);
    pw = CLAMP(pw + 2*pad_x, 1, (int32_t)CAM_WIDTH  - px);
    ph = CLAMP(ph + 2*pad_y, 1, (int32_t)CAM_HEIGHT - py);

    float x_scale = (float)pw / (float)FACE_INPUT_W;
    float y_scale = (float)ph / (float)FACE_INPUT_H;

    for (uint32_t dy = 0; dy < FACE_INPUT_H; dy++) {
        int32_t sy = py + (int32_t)(dy * y_scale);
        sy = CLAMP(sy, 0, (int32_t)CAM_HEIGHT - 1);
        for (uint32_t dx = 0; dx < FACE_INPUT_W; dx++) {
            int32_t sx = px + (int32_t)(dx * x_scale);
            sx = CLAMP(sx, 0, (int32_t)CAM_WIDTH - 1);
            uint8_t pix = src320x240[sy * CAM_WIDTH + sx];
            dst96x96[dy * FACE_INPUT_W + dx] = (int8_t)((int32_t)pix - 128);
        }
    }
}

/* ─────────────────────────────────────────────
   Embedding computation
   ───────────────────────────────────────────── */
static bool compute_embedding(
        const uint8_t *frame,
        float bbox_x, float bbox_y, float bbox_w, float bbox_h,
        float *embed_out) {

    crop_and_resize_face(frame, bbox_x, bbox_y, bbox_w, bbox_h, s_face_input);

    bool ok = hal_accai_run(
        g_face_model, g_face_model_size,
        s_face_input, sizeof(s_face_input),
        s_face_output, sizeof(s_face_output));

    if (!ok) return false;

    /* Dequantize INT8 → float, then L2-normalize embedding */
    float norm_sq = 0.0f;
    for (uint32_t i = 0; i < FACE_EMBED_DIM; i++) {
        embed_out[i] = (float)s_face_output[i] * FACE_EMBED_SCALE;
        norm_sq += embed_out[i] * embed_out[i];
    }
    float inv_norm = 1.0f / (sqrtf(norm_sq) + 1e-8f);
    for (uint32_t i = 0; i < FACE_EMBED_DIM; i++) {
        embed_out[i] *= inv_norm;
    }
    return true;
}

/* ─────────────────────────────────────────────
   L2 distance between two unit-norm embeddings
   ───────────────────────────────────────────── */
static float l2_distance(const float *a, const float *b) {
    float sum = 0.0f;
    for (uint32_t i = 0; i < FACE_EMBED_DIM; i++) {
        float d = a[i] - b[i];
        sum += d * d;
    }
    return sqrtf(sum);
}

/* ─────────────────────────────────────────────
   Flash persistence
   ───────────────────────────────────────────── */
static bool load_enrollment_from_flash(void) {
    face_flash_record_t rec;
    hal_flash_read(FLASH_FACE_EMBED_BASE, (uint8_t *)&rec, sizeof(rec));

    if (rec.magic != FACE_FLASH_MAGIC) return false;

    uint32_t crc = crc32_compute((uint8_t *)&rec,
                                  sizeof(rec) - sizeof(rec.crc32));
    if (crc != rec.crc32) return false;

    memcpy(s_owner_embed, rec.embed, sizeof(s_owner_embed));
    s_owner_enrolled = true;
    return true;
}

static bool save_enrollment_to_flash(const float *embed) {
    face_flash_record_t rec;
    rec.magic = FACE_FLASH_MAGIC;
    memcpy(rec.embed, embed, sizeof(rec.embed));
    rec.crc32 = crc32_compute((uint8_t *)&rec,
                               sizeof(rec) - sizeof(rec.crc32));

    hal_flash_erase_sector(FLASH_FACE_EMBED_BASE);
    return hal_flash_write(FLASH_FACE_EMBED_BASE,
                           (uint8_t *)&rec, sizeof(rec));
}

/* ─────────────────────────────────────────────
   Public API
   ───────────────────────────────────────────── */
void face_recog_init(void) {
    memset(s_enroll_sum, 0, sizeof(s_enroll_sum));
    s_enroll_count = 0;
    s_owner_enrolled = false;
    load_enrollment_from_flash();
}

bool face_enroll_frame(const uint8_t *frame, uint8_t frame_idx) {
    if (frame_idx >= FACE_ENROLL_FRAMES) return false;

    float embed[FACE_EMBED_DIM];
    /* Use full-frame bbox during enrollment (assumes user is centered) */
    bool ok = compute_embedding(frame, 0.1f, 0.0f, 0.8f, 1.0f, embed);
    if (!ok) return false;

    for (uint32_t i = 0; i < FACE_EMBED_DIM; i++) {
        s_enroll_sum[i] += embed[i];
    }
    s_enroll_count++;
    return true;
}

bool face_enroll_finalize(void) {
    if (s_enroll_count == 0) return false;

    float mean_embed[FACE_EMBED_DIM];
    float inv_n = 1.0f / (float)s_enroll_count;
    float norm_sq = 0.0f;
    for (uint32_t i = 0; i < FACE_EMBED_DIM; i++) {
        mean_embed[i] = s_enroll_sum[i] * inv_n;
        norm_sq += mean_embed[i] * mean_embed[i];
    }
    /* Re-normalize the mean embedding */
    float inv_norm = 1.0f / (sqrtf(norm_sq) + 1e-8f);
    for (uint32_t i = 0; i < FACE_EMBED_DIM; i++) {
        mean_embed[i] *= inv_norm;
    }

    if (!save_enrollment_to_flash(mean_embed)) return false;

    memcpy(s_owner_embed, mean_embed, sizeof(s_owner_embed));
    s_owner_enrolled = true;

    /* Reset accumulators */
    memset(s_enroll_sum, 0, sizeof(s_enroll_sum));
    s_enroll_count = 0;
    return true;
}

/* called with person bbox from person_detect */
bool face_recog_run(const uint8_t *frame, face_result_t *result) {
    result->match         = false;
    result->confidence_pct = 0;
    result->l2_distance   = 9.99f;

    if (!s_owner_enrolled) return false;

    /* Use the bounding box from latest person detection
     * (passed via global or as parameter — simplified: use centre crop) */
    float embed[FACE_EMBED_DIM];
    bool ok = compute_embedding(frame, 0.1f, 0.0f, 0.8f, 1.0f, embed);
    if (!ok) return false;

    float dist = l2_distance(embed, s_owner_embed);
    result->l2_distance    = dist;
    result->match          = (dist < FACE_L2_THRESHOLD);

    /* Map distance to pseudo-confidence:
     * dist=0 → 100%, dist=THRESHOLD → 50%, dist>1.5*THRESHOLD → 0% */
    float conf = 1.0f - (dist / (1.5f * FACE_L2_THRESHOLD));
    result->confidence_pct = (uint8_t)(CLAMP(conf * 100.0f, 0.0f, 100.0f));
    return true;
}

void face_recog_build_i2c_msg(
        const face_result_t *result,
        qn_i2c_msg_t *out) {

    out->header   = I2C_MSG_HEADER;
    out->msg_type = result->match ? MSG_FACE_RECOGNIZED : MSG_FACE_UNKNOWN;
    memset(out->payload, 0, I2C_PAYLOAD_SIZE);

    qn_payload_face_t *p = (qn_payload_face_t *)out->payload;
    p->match          = result->match ? 1 : 0;
    /* L2 as Q0.8: scale 0..2.0 range → 0..255 */
    p->l2_dist_q8     = (uint8_t)CLAMP(result->l2_distance * 128.0f, 0.0f, 255.0f);
    p->confidence_pct = result->confidence_pct;
    p->enrolled_slot  = 0;   /* single-user device */

    out->crc16 = hal_crc16_ccitt((uint8_t *)out, I2C_MSG_SIZE - 2);
}
