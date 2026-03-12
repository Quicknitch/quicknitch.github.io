/**
 * main.c
 * Quicknitch Edge AI Firmware — Top-Level Scheduler
 *
 * Bare-metal, interrupt-driven cooperative scheduler (no RTOS).
 * All inference runs from the main loop; ISRs only set flags and
 * fill buffers.
 *
 * Inference pipeline per task slot:
 *   1. Check period timer
 *   2. Grab frame / audio
 *   3. Pre-process
 *   4. hal_accai_run()  (blocks ~5–50ms, gates clock after)
 *   5. Post-process
 *   6. Build & send I2C message
 *
 * State machine drives which tasks are active:
 *
 *  IDLE        → wake word only, camera at 1fps, HW-AccAI gated
 *  PERSON_TRACK→ person detect 5fps + face recog 2fps + safety 3fps
 *  GESTURE_SCAN→ adds gesture 5fps (ToF < 1.2m)
 *  LOST_USER   → person detect 2fps, gesture off, face off
 *  ENROLLING   → 10-frame face enrollment, camera full-res
 *  OTA         → all inference suspended, I2C chunk reception only
 *  FAULT       → all stopped, await reset
 */

#include "quicknitch.h"
#include "hal.h"
#include "wake_word.h"
#include "inference_engines.h"
#include "ota.h"

/* ─────────────────────────────────────────────
   Global system state (extern in hal.h)
   ───────────────────────────────────────────── */
qn_system_t g_sys = {
    .state               = SYS_STATE_IDLE,
    .tick_ms             = 0,
    .person_present      = false,
    .person_last_seen_ms = 0,
    .gesture_mode_enabled= false,
    .ota_in_progress     = false,
    .active_model_bank   = 0,
};

/* ─────────────────────────────────────────────
   Per-task scheduling state
   ───────────────────────────────────────────── */
typedef struct {
    uint32_t last_run_ms;
    uint32_t period_ms;
} task_sched_t;

static task_sched_t s_sched_person  = {0, PERSON_DETECT_PERIOD_MS};
static task_sched_t s_sched_gesture = {0, GESTURE_PERIOD_MS};
static task_sched_t s_sched_face    = {0, FACE_RECOG_PERIOD_MS};
static task_sched_t s_sched_safety  = {0, SAFETY_PERIOD_MS};

#define TASK_DUE(sched) \
    ((hal_get_tick_ms() - (sched).last_run_ms) >= (sched).period_ms)
#define TASK_MARK_RUN(sched) \
    ((sched).last_run_ms = hal_get_tick_ms())

/* ─────────────────────────────────────────────
   Audio processing tick (10ms = HOP interval)
   ───────────────────────────────────────────── */
static uint32_t s_audio_last_ms = 0;

/* ─────────────────────────────────────────────
   Enrollment state machine
   ───────────────────────────────────────────── */
static uint8_t s_enroll_frame_idx = 0;

/* ─────────────────────────────────────────────
   I2C command dispatch from ESP32
   ───────────────────────────────────────────── */
#define ESP_CMD_START_ENROLL   0xE1U
#define ESP_CMD_ENABLE_GESTURE 0xE2U
#define ESP_CMD_GESTURE_DISABLE 0xE3U

static void dispatch_i2c_cmd(const uint8_t *buf, uint8_t len) {
    if (len == 0) return;

    /* OTA commands always take priority */
    if (buf[0] >= 0xA0U && buf[0] <= 0xA3U) {
        ota_process_i2c_cmd(buf, len);
        return;
    }

    switch (buf[0]) {
        case ESP_CMD_START_ENROLL:
            if (g_sys.state == SYS_STATE_PERSON_TRACK ||
                g_sys.state == SYS_STATE_IDLE) {
                g_sys.state = SYS_STATE_ENROLLING;
                s_enroll_frame_idx = 0;
                face_recog_init();
            }
            break;

        case ESP_CMD_ENABLE_GESTURE:
            g_sys.gesture_mode_enabled = true;
            break;

        case ESP_CMD_GESTURE_DISABLE:
            g_sys.gesture_mode_enabled = false;
            if (g_sys.state == SYS_STATE_GESTURE_SCAN) {
                g_sys.state = SYS_STATE_PERSON_TRACK;
            }
            break;

        default:
            break;
    }
}

/* ─────────────────────────────────────────────
   Task: Wake Word (runs every 10ms in main loop)
   ───────────────────────────────────────────── */
static void task_wake_word(void) {
    if ((hal_get_tick_ms() - s_audio_last_ms) < 10U) return;
    s_audio_last_ms = hal_get_tick_ms();

    ww_result_t ww = wake_word_process_audio();
    if (ww != WW_RESULT_NONE) {
        qn_i2c_msg_t msg;
        wake_word_build_i2c_msg(ww, 90, &msg);
        hal_i2c_send_msg(&msg);

        /* Wake word lifts us from IDLE */
        if (g_sys.state == SYS_STATE_IDLE ||
            g_sys.state == SYS_STATE_LOST_USER) {
            g_sys.state = SYS_STATE_PERSON_TRACK;
        }
    }
}

/* ─────────────────────────────────────────────
   Task: Person Detection
   ───────────────────────────────────────────── */
static void task_person_detect(void) {
    if (!TASK_DUE(s_sched_person)) return;
    if (!g_frame_new) return;
    TASK_MARK_RUN(s_sched_person);

    const uint8_t *frame = hal_camera_get_frame();
    person_result_t result;
    person_detect_status_t status = person_detect_run(frame, &result);

    qn_i2c_msg_t msg;
    if (status == PERSON_STATUS_DETECTED) {
        person_detect_build_i2c_msg(&result, false, &msg);
        hal_i2c_send_msg(&msg);

        if (g_sys.state == SYS_STATE_IDLE ||
            g_sys.state == SYS_STATE_LOST_USER) {
            g_sys.state = SYS_STATE_PERSON_TRACK;
        }
        /* If gesture enabled by ESP32, switch mode */
        if (g_sys.gesture_mode_enabled &&
            g_sys.state == SYS_STATE_PERSON_TRACK) {
            g_sys.state = SYS_STATE_GESTURE_SCAN;
        }
    } else if (status == PERSON_STATUS_LOST) {
        person_detect_build_i2c_msg(&result, true, &msg);
        hal_i2c_send_msg(&msg);
        /* State already set to LOST_USER by person_detect.c */
    }
}

/* ─────────────────────────────────────────────
   Task: Gesture Recognition
   ───────────────────────────────────────────── */
static const uint8_t *s_prev_gesture_frame = NULL;

static void task_gesture(void) {
    if (g_sys.state != SYS_STATE_GESTURE_SCAN) return;
    if (!TASK_DUE(s_sched_gesture)) return;
    if (!g_frame_new) return;
    TASK_MARK_RUN(s_sched_gesture);

    const uint8_t *frame = hal_camera_get_frame();

    /* Down-scale to 96×96 first (done inside gesture_run via prev frame) */
    gesture_result_t result;
    bool detected = gesture_run(frame, s_prev_gesture_frame, &result);
    s_prev_gesture_frame = frame;

    if (detected) {
        qn_i2c_msg_t msg;
        gesture_build_i2c_msg(&result, &msg);
        hal_i2c_send_msg(&msg);
    }
}

/* ─────────────────────────────────────────────
   Task: Face Recognition
   ───────────────────────────────────────────── */
static void task_face_recog(void) {
    if (g_sys.state != SYS_STATE_PERSON_TRACK &&
        g_sys.state != SYS_STATE_GESTURE_SCAN) return;
    if (!TASK_DUE(s_sched_face)) return;
    if (!g_frame_new) return;
    TASK_MARK_RUN(s_sched_face);

    const uint8_t *frame = hal_camera_get_frame();
    face_result_t result;
    bool ok = face_recog_run(frame, &result);

    if (ok) {
        qn_i2c_msg_t msg;
        face_recog_build_i2c_msg(&result, &msg);
        hal_i2c_send_msg(&msg);
    }
}

/* ─────────────────────────────────────────────
   Task: Safety Scene Detection
   ───────────────────────────────────────────── */
static void task_safety(void) {
    if (g_sys.state == SYS_STATE_IDLE ||
        g_sys.state == SYS_STATE_OTA) return;
    if (!TASK_DUE(s_sched_safety)) return;
    if (!g_frame_new) return;
    TASK_MARK_RUN(s_sched_safety);

    const uint8_t *frame = hal_camera_get_frame();
    safety_result_t result;
    bool ok = safety_run(frame, &result);

    if (ok && result.alert_required) {
        qn_i2c_msg_t msg;
        safety_build_i2c_msg(&result, &msg);
        hal_i2c_send_msg(&msg);
    }
}

/* ─────────────────────────────────────────────
   Task: Face Enrollment
   ───────────────────────────────────────────── */
static uint32_t s_enroll_last_ms = 0;
#define ENROLL_FRAME_PERIOD_MS  500U

static void task_enrollment(void) {
    if (g_sys.state != SYS_STATE_ENROLLING) return;
    if ((hal_get_tick_ms() - s_enroll_last_ms) < ENROLL_FRAME_PERIOD_MS) return;
    if (!g_frame_new) return;
    s_enroll_last_ms = hal_get_tick_ms();

    const uint8_t *frame = hal_camera_get_frame();
    bool ok = face_enroll_frame(frame, s_enroll_frame_idx);
    if (ok) {
        s_enroll_frame_idx++;
    }

    if (s_enroll_frame_idx >= FACE_ENROLL_FRAMES) {
        face_enroll_finalize();
        g_sys.state = SYS_STATE_PERSON_TRACK;
        s_enroll_frame_idx = 0;
    }
}

/* ─────────────────────────────────────────────
   Task: I2C receive (commands from ESP32)
   ───────────────────────────────────────────── */
static void task_i2c_receive(void) {
    uint8_t buf[64];
    uint8_t len = 0;
    if (hal_i2c_read_cmd(buf, &len)) {
        dispatch_i2c_cmd(buf, len);
    }
}

/* ─────────────────────────────────────────────
   Power management helper
   In IDLE: only PDM + wake word, camera 1fps,
            HW-AccAI gated. ARM enters WFI between tasks.
   ───────────────────────────────────────────── */
static void power_manage(void) {
    if (g_sys.state == SYS_STATE_IDLE) {
        /* Slow down camera to 1fps to save power */
        s_sched_person.period_ms = 1000U;
        hal_power_gate_camera(false);  /* Keep on but slow */
    } else {
        s_sched_person.period_ms = PERSON_DETECT_PERIOD_MS;
    }

    /* If all tasks idle for >2ms, sleep */
    bool all_idle = !g_frame_new &&
                    ((hal_get_tick_ms() - s_audio_last_ms) < 8U);
    if (all_idle) {
        hal_enter_sleep();  /* WFI — woken by SysTick, camera DMA, PDM */
    }
}

/* ─────────────────────────────────────────────
   Main entry point
   ───────────────────────────────────────────── */
int main(void) {
    /* Hardware init */
    hal_system_init();

    /* Inference engine init */
    wake_word_init();
    person_detect_init();
    gesture_init();
    face_recog_init();
    safety_init();
    ota_init();

    /* Start camera double-buffer capture */
    hal_camera_start_capture(g_frame_buf[g_frame_active]);
    hal_pdm_start();

    /* Initial power state: idle */
    g_sys.state = SYS_STATE_IDLE;
    hal_power_gate_accai(true);  /* Gate until first inference needed */

    /* ── Main loop ── */
    while (1) {
        /* OTA mode: only handle I2C, no inference */
        if (g_sys.state == SYS_STATE_OTA) {
            task_i2c_receive();
            continue;
        }

        /* Normal pipeline */
        task_i2c_receive();
        task_wake_word();
        task_person_detect();
        task_gesture();
        task_face_recog();
        task_safety();
        task_enrollment();

        power_manage();
    }

    return 0; /* unreachable */
}
