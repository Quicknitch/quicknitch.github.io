/**
 * quicknitch.h
 * Quicknitch Edge AI Firmware — Himax WE-I Plus
 * Master configuration and type definitions
 *
 * Hardware: ARMv7-M @ 400MHz + HW-AccAI CNN accelerator
 *           HM01B0 QVGA camera, PDM microphone, I2C slave to ESP32-S3
 * Power budget: 5mW active, 0.8mW idle
 */

#ifndef QUICKNITCH_H
#define QUICKNITCH_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ─────────────────────────────────────────────
   Build-time feature flags
   ───────────────────────────────────────────── */
#define QN_ENABLE_WAKE_WORD        1
#define QN_ENABLE_PERSON_DETECT    1
#define QN_ENABLE_GESTURE          1
#define QN_ENABLE_FACE_RECOG       1
#define QN_ENABLE_SAFETY_SCENE     1
#define QN_ENABLE_OTA              1

/* ─────────────────────────────────────────────
   Clock & timing
   ───────────────────────────────────────────── */
#define QN_CPU_FREQ_HZ             400000000UL
#define QN_SYSTICK_FREQ_HZ         1000U          /* 1ms tick */
#define QN_MS_TO_TICKS(ms)         ((ms) * (QN_SYSTICK_FREQ_HZ / 1000U))

/* ─────────────────────────────────────────────
   Camera: HM01B0
   ───────────────────────────────────────────── */
#define CAM_WIDTH                  320U
#define CAM_HEIGHT                 240U
#define CAM_SMALL_WIDTH            96U
#define CAM_SMALL_HEIGHT           96U
#define CAM_SAFETY_WIDTH           64U
#define CAM_SAFETY_HEIGHT          64U
#define CAM_BYTES_PER_FRAME        (CAM_WIDTH * CAM_HEIGHT)  /* mono8 */
#define CAM_SMALL_BYTES            (CAM_SMALL_WIDTH * CAM_SMALL_HEIGHT)
#define CAM_SAFETY_BYTES           (CAM_SAFETY_WIDTH * CAM_SAFETY_HEIGHT)

/* ─────────────────────────────────────────────
   Audio
   ───────────────────────────────────────────── */
#define AUDIO_SAMPLE_RATE_HZ       16000U
#define AUDIO_WINDOW_MS            1000U
#define AUDIO_WINDOW_SAMPLES       (AUDIO_SAMPLE_RATE_HZ * AUDIO_WINDOW_MS / 1000U)
#define MEL_FEATURES               40U
#define MEL_TIME_FRAMES            98U
#define PDM_DECIMATION_FACTOR      64U            /* PDM 1.024MHz → 16kHz */

/* ─────────────────────────────────────────────
   Inference rates
   ───────────────────────────────────────────── */
#define PERSON_DETECT_FPS          5U
#define PERSON_DETECT_PERIOD_MS    200U
#define GESTURE_FPS                5U
#define GESTURE_PERIOD_MS          200U
#define FACE_RECOG_FPS             2U
#define FACE_RECOG_PERIOD_MS       500U
#define SAFETY_FPS                 3U
#define SAFETY_PERIOD_MS           333U

/* ─────────────────────────────────────────────
   Model sizes (flash budgets, bytes)
   ───────────────────────────────────────────── */
#define WAKE_WORD_MODEL_MAX        (50U  * 1024U)
#define PERSON_MODEL_MAX           (80U  * 1024U)
#define GESTURE_MODEL_MAX          (64U  * 1024U)
#define FACE_MODEL_MAX             (96U  * 1024U)
#define SAFETY_MODEL_MAX           (48U  * 1024U)

/* ─────────────────────────────────────────────
   Flash layout (512KB total flash assumed)
   ───────────────────────────────────────────── */
#define FLASH_BASE                 0x08000000UL
#define FLASH_FIRMWARE_BANK_A      0x08000000UL   /* 128KB firmware */
#define FLASH_MODEL_BANK_A         0x08020000UL   /* 192KB models   */
#define FLASH_MODEL_BANK_B         0x08050000UL   /* 192KB OTA bank */
#define FLASH_CONFIG_BASE          0x08080000UL   /* 4KB config     */
#define FLASH_FACE_EMBED_BASE      0x08081000UL   /* 2KB face embed */

/* ─────────────────────────────────────────────
   I2C slave address & protocol
   ───────────────────────────────────────────── */
#define I2C_SLAVE_ADDR             0x42U
#define I2C_MSG_HEADER             0x51U
#define I2C_MSG_SIZE               16U            /* bytes total    */
#define I2C_PAYLOAD_SIZE           12U
#define I2C_CRC_SIZE               2U

/* Message type IDs */
typedef enum {
    MSG_WAKE_WORD_DETECTED   = 0x01,
    MSG_PERSON_DETECTED      = 0x02,
    MSG_PERSON_LOST          = 0x03,
    MSG_GESTURE_DETECTED     = 0x04,
    MSG_FACE_RECOGNIZED      = 0x05,
    MSG_FACE_UNKNOWN         = 0x06,
    MSG_SAFETY_ALERT         = 0x07,
    MSG_SYSTEM_STATUS        = 0x08,
    MSG_OTA_ACK              = 0x09,
    MSG_OTA_COMPLETE         = 0x0A,
    MSG_OTA_ERROR            = 0x0B,
} qn_msg_type_t;

/* ─────────────────────────────────────────────
   I2C message wire format (16 bytes)
   [0]    = 0x51  header magic
   [1]    = msg_type (qn_msg_type_t)
   [2-13] = payload (12 bytes, type-specific)
   [14-15]= CRC-16/CCITT over bytes [0..13]
   ───────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  header;               /* 0x51  */
    uint8_t  msg_type;
    uint8_t  payload[I2C_PAYLOAD_SIZE];
    uint16_t crc16;
} qn_i2c_msg_t;

/* ── Payload layouts ── */

/* MSG_WAKE_WORD_DETECTED */
typedef struct __attribute__((packed)) {
    uint8_t  word_id;              /* 0=hey_nitch, 1=quicknitch */
    uint8_t  confidence_pct;       /* 0-100 */
    uint8_t  noise_floor_dbfs;     /* signed, cast to int8_t */
    uint8_t  reserved[9];
} qn_payload_wake_word_t;

/* MSG_PERSON_DETECTED */
typedef struct __attribute__((packed)) {
    uint16_t bbox_x;               /* Q8.8 fixed-point, normalised 0..1 */
    uint16_t bbox_y;
    uint16_t bbox_w;
    uint16_t bbox_h;
    uint8_t  confidence_pct;
    uint8_t  tracking_id;
    uint8_t  reserved[2];
} qn_payload_person_t;

/* MSG_GESTURE_DETECTED */
typedef struct __attribute__((packed)) {
    uint8_t  gesture_id;           /* 0=wave, 1=point, 2=thumbs_up, 3=cross_arms */
    uint8_t  confidence_pct;
    uint16_t frame_count;          /* total frames processed */
    uint8_t  reserved[8];
} qn_payload_gesture_t;

/* MSG_FACE_RECOGNIZED / MSG_FACE_UNKNOWN */
typedef struct __attribute__((packed)) {
    uint8_t  match;                /* 1=recognized owner, 0=unknown */
    uint8_t  l2_dist_q8;           /* L2 distance as Q0.8 fixed-point */
    uint8_t  confidence_pct;
    uint8_t  enrolled_slot;        /* which enroll slot matched */
    uint8_t  reserved[8];
} qn_payload_face_t;

/* MSG_SAFETY_ALERT */
typedef struct __attribute__((packed)) {
    uint8_t  alert_class;          /* 0=safe,1=near_glass,2=staircase,3=dark,4=outdoor */
    uint8_t  confidence_pct;
    uint8_t  flags;                /* bit0=staircase, bit1=glass, bit2=dark */
    uint8_t  reserved[9];
} qn_payload_safety_t;

/* ─────────────────────────────────────────────
   System state machine
   ───────────────────────────────────────────── */
typedef enum {
    SYS_STATE_IDLE         = 0,    /* low power, wake word listening only */
    SYS_STATE_PERSON_TRACK = 1,    /* person detected, full pipeline       */
    SYS_STATE_GESTURE_SCAN = 2,    /* person close enough, gesture active  */
    SYS_STATE_LOST_USER    = 3,    /* person absent >10s                   */
    SYS_STATE_ENROLLING    = 4,    /* face enrollment mode                 */
    SYS_STATE_OTA          = 5,    /* receiving OTA model update           */
    SYS_STATE_FAULT        = 6,
} qn_sys_state_t;

/* ─────────────────────────────────────────────
   Global volatile state (set by ISRs, read by main)
   ───────────────────────────────────────────── */
typedef struct {
    volatile qn_sys_state_t  state;
    volatile uint32_t        tick_ms;
    volatile bool            person_present;
    volatile uint32_t        person_last_seen_ms;
    volatile bool            gesture_mode_enabled;  /* set by I2C from ESP32 */
    volatile bool            ota_in_progress;
    volatile uint8_t         active_model_bank;     /* 0=A, 1=B */
} qn_system_t;

extern qn_system_t g_sys;

/* ─────────────────────────────────────────────
   Utility macros
   ───────────────────────────────────────────── */
#define ARRAY_SIZE(x)   (sizeof(x) / sizeof((x)[0]))
#define CLAMP(v,lo,hi)  ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define Q8_8(f)         ((uint16_t)((f) * 256.0f + 0.5f))
#define MIN(a,b)        ((a) < (b) ? (a) : (b))
#define MAX(a,b)        ((a) > (b) ? (a) : (b))

/* Fixed-point sqrt (Newton-Raphson, 16-bit input) */
static inline uint32_t fp_sqrt_u32(uint32_t x) {
    if (x == 0) return 0;
    uint32_t r = x >> 1;
    for (int i = 0; i < 8; i++) r = (r + x / r) >> 1;
    return r;
}

#endif /* QUICKNITCH_H */
