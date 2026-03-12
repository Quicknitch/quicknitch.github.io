/**
 * person_detect.h
 */
#ifndef PERSON_DETECT_H
#define PERSON_DETECT_H
#include "quicknitch.h"

typedef struct {
    bool    person_present;
    uint8_t confidence_pct;
    float   bbox_x, bbox_y, bbox_w, bbox_h;  /* normalised 0..1 */
} person_result_t;

typedef enum {
    PERSON_STATUS_DETECTED = 0,
    PERSON_STATUS_ABSENT   = 1,
    PERSON_STATUS_LOST     = 2,
    PERSON_STATUS_ERROR    = 3,
} person_detect_status_t;

void                   person_detect_init(void);
person_detect_status_t person_detect_run(const uint8_t *frame,
                                         person_result_t *result);
void                   person_detect_build_i2c_msg(const person_result_t *result,
                                                    bool person_lost,
                                                    qn_i2c_msg_t *out);
#endif /* PERSON_DETECT_H */

/* ──────────────────────────────────────────── */

/**
 * gesture.h
 */
#ifndef GESTURE_H
#define GESTURE_H
#include "quicknitch.h"

typedef enum {
    GESTURE_NONE        = 0xFF,
    GESTURE_WAVE_HELLO  = 0x00,
    GESTURE_POINT_FWD   = 0x01,
    GESTURE_THUMBS_UP   = 0x02,
    GESTURE_CROSS_ARMS  = 0x03,
} gesture_id_t;

typedef struct {
    gesture_id_t gesture;
    uint8_t      confidence_pct;
    uint16_t     frame_count;
} gesture_result_t;

void gesture_init(void);
bool gesture_run(const uint8_t *frame_curr,
                 const uint8_t *frame_prev,
                 gesture_result_t *result);
void gesture_build_i2c_msg(const gesture_result_t *result,
                            qn_i2c_msg_t *out);
#endif /* GESTURE_H */

/* ──────────────────────────────────────────── */

/**
 * face_recog.h
 */
#ifndef FACE_RECOG_H
#define FACE_RECOG_H
#include "quicknitch.h"

#define FACE_EMBED_DIM     128U
#define FACE_ENROLL_FRAMES 10U

typedef struct {
    bool    match;
    uint8_t confidence_pct;
    float   l2_distance;
} face_result_t;

void face_recog_init(void);
bool face_enroll_frame(const uint8_t *frame, uint8_t frame_idx);
bool face_enroll_finalize(void);          /* average embeds, write to flash */
bool face_recog_run(const uint8_t *frame, face_result_t *result);
void face_recog_build_i2c_msg(const face_result_t *result, qn_i2c_msg_t *out);
#endif /* FACE_RECOG_H */

/* ──────────────────────────────────────────── */

/**
 * safety_scene.h
 */
#ifndef SAFETY_SCENE_H
#define SAFETY_SCENE_H
#include "quicknitch.h"

typedef enum {
    SCENE_SAFE_INDOOR    = 0,
    SCENE_NEAR_GLASS     = 1,
    SCENE_STAIRCASE      = 2,
    SCENE_TOO_DARK       = 3,
    SCENE_OUTDOOR        = 4,
} scene_class_t;

typedef struct {
    scene_class_t scene;
    uint8_t       confidence_pct;
    bool          alert_required;
} safety_result_t;

void safety_init(void);
bool safety_run(const uint8_t *frame, safety_result_t *result);
void safety_build_i2c_msg(const safety_result_t *result, qn_i2c_msg_t *out);
#endif /* SAFETY_SCENE_H */
