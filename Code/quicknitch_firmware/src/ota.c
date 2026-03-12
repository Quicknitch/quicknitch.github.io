/**
 * ota.c
 * OTA Model Update — receive new .tflite model over I2C in 4KB chunks
 *
 * Protocol (master-initiated write transactions):
 *
 *  OTA_START  [0xA0][model_id 1B][total_size 4B][sha256 32B][pad 2B]  → 40B cmd
 *  OTA_CHUNK  [0xA1][chunk_idx 2B][chunk_size 2B][data 4096B]         → 4100B
 *  OTA_COMMIT [0xA2][model_id 1B][pad 6B]                             → 8B cmd
 *  OTA_ABORT  [0xA3]
 *
 * WE-I response (16-byte I2C message):
 *  MSG_OTA_ACK      → chunk_idx accepted
 *  MSG_OTA_ERROR    → error_code in payload[0]
 *  MSG_OTA_COMPLETE → reboot imminent
 *
 * Flash layout:
 *   Bank A: currently running models (FLASH_MODEL_BANK_A)
 *   Bank B: OTA staging   (FLASH_MODEL_BANK_B)
 * After SHA256 verify: copy B→A, reboot.
 */

#include "ota.h"
#include "hal.h"

/* ─────────────────────────────────────────────
   OTA command bytes (from ESP32 master)
   ───────────────────────────────────────────── */
#define OTA_CMD_START   0xA0U
#define OTA_CMD_CHUNK   0xA1U
#define OTA_CMD_COMMIT  0xA2U
#define OTA_CMD_ABORT   0xA3U
#define OTA_CHUNK_SIZE  4096U

/* ─────────────────────────────────────────────
   OTA error codes
   ───────────────────────────────────────────── */
typedef enum {
    OTA_ERR_NONE          = 0x00,
    OTA_ERR_SIZE_TOO_LARGE= 0x01,
    OTA_ERR_FLASH_ERASE   = 0x02,
    OTA_ERR_FLASH_WRITE   = 0x03,
    OTA_ERR_SHA256_FAIL   = 0x04,
    OTA_ERR_SEQ_ERROR     = 0x05,
    OTA_ERR_INVALID_CMD   = 0x06,
} ota_error_t;

/* Model ID → flash base address mapping */
typedef struct {
    uint8_t  model_id;
    uint32_t flash_base;
    uint32_t max_size;
} model_map_t;

/* Model IDs */
#define MODEL_ID_WAKE_WORD    0x01U
#define MODEL_ID_PERSON       0x02U
#define MODEL_ID_GESTURE      0x03U
#define MODEL_ID_FACE         0x04U
#define MODEL_ID_SAFETY       0x05U

static const model_map_t s_model_map[] = {
    {MODEL_ID_WAKE_WORD, FLASH_MODEL_BANK_B + 0x00000, WAKE_WORD_MODEL_MAX},
    {MODEL_ID_PERSON,    FLASH_MODEL_BANK_B + 0x0C800, PERSON_MODEL_MAX},
    {MODEL_ID_GESTURE,   FLASH_MODEL_BANK_B + 0x1C800, GESTURE_MODEL_MAX},
    {MODEL_ID_FACE,      FLASH_MODEL_BANK_B + 0x2C800, FACE_MODEL_MAX},
    {MODEL_ID_SAFETY,    FLASH_MODEL_BANK_B + 0x40800, SAFETY_MODEL_MAX},
};

/* OTA state */
typedef struct {
    bool     active;
    uint8_t  model_id;
    uint32_t total_size;
    uint8_t  expected_sha256[32];
    uint32_t flash_base;
    uint32_t max_size;
    uint16_t next_chunk;
    uint32_t bytes_written;
} ota_state_t;

static ota_state_t s_ota = {0};

/* ─────────────────────────────────────────────
   Helpers
   ───────────────────────────────────────────── */
static void ota_send_ack(uint16_t chunk_idx) {
    qn_i2c_msg_t msg;
    msg.header   = I2C_MSG_HEADER;
    msg.msg_type = MSG_OTA_ACK;
    memset(msg.payload, 0, I2C_PAYLOAD_SIZE);
    msg.payload[0] = (uint8_t)(chunk_idx & 0xFF);
    msg.payload[1] = (uint8_t)(chunk_idx >> 8);
    msg.crc16 = hal_crc16_ccitt((uint8_t *)&msg, I2C_MSG_SIZE - 2);
    hal_i2c_send_msg(&msg);
}

static void ota_send_error(ota_error_t err) {
    qn_i2c_msg_t msg;
    msg.header   = I2C_MSG_HEADER;
    msg.msg_type = MSG_OTA_ERROR;
    memset(msg.payload, 0, I2C_PAYLOAD_SIZE);
    msg.payload[0] = (uint8_t)err;
    msg.crc16 = hal_crc16_ccitt((uint8_t *)&msg, I2C_MSG_SIZE - 2);
    hal_i2c_send_msg(&msg);
    s_ota.active = false;
}

static void ota_send_complete(void) {
    qn_i2c_msg_t msg;
    msg.header   = I2C_MSG_HEADER;
    msg.msg_type = MSG_OTA_COMPLETE;
    memset(msg.payload, 0, I2C_PAYLOAD_SIZE);
    msg.payload[0] = s_ota.model_id;
    msg.crc16 = hal_crc16_ccitt((uint8_t *)&msg, I2C_MSG_SIZE - 2);
    hal_i2c_send_msg(&msg);
}

static const model_map_t *find_model(uint8_t model_id) {
    for (uint32_t i = 0; i < ARRAY_SIZE(s_model_map); i++) {
        if (s_model_map[i].model_id == model_id) return &s_model_map[i];
    }
    return NULL;
}

/* ─────────────────────────────────────────────
   OTA command handlers
   ───────────────────────────────────────────── */
static void handle_ota_start(const uint8_t *cmd, uint8_t len) {
    /* [0xA0][model_id][total_size 4B LE][sha256 32B] = 38 bytes */
    if (len < 38) { ota_send_error(OTA_ERR_INVALID_CMD); return; }

    uint8_t model_id = cmd[1];
    uint32_t total_size = (uint32_t)cmd[2]
                        | ((uint32_t)cmd[3] << 8)
                        | ((uint32_t)cmd[4] << 16)
                        | ((uint32_t)cmd[5] << 24);

    const model_map_t *mm = find_model(model_id);
    if (!mm) { ota_send_error(OTA_ERR_INVALID_CMD); return; }
    if (total_size > mm->max_size) { ota_send_error(OTA_ERR_SIZE_TOO_LARGE); return; }

    /* Erase OTA bank flash sectors for this model */
    uint32_t n_sectors = (total_size + 4095U) / 4096U;
    for (uint32_t s = 0; s < n_sectors; s++) {
        if (!hal_flash_erase_sector(mm->flash_base + s * 4096U)) {
            ota_send_error(OTA_ERR_FLASH_ERASE);
            return;
        }
    }

    s_ota.active        = true;
    s_ota.model_id      = model_id;
    s_ota.total_size    = total_size;
    s_ota.flash_base    = mm->flash_base;
    s_ota.max_size      = mm->max_size;
    s_ota.next_chunk    = 0;
    s_ota.bytes_written = 0;
    memcpy(s_ota.expected_sha256, &cmd[6], 32);

    g_sys.ota_in_progress = true;
    g_sys.state = SYS_STATE_OTA;

    ota_send_ack(0xFFFF);   /* 0xFFFF = START ack */
}

static void handle_ota_chunk(const uint8_t *cmd, uint8_t len) {
    /* [0xA1][chunk_idx 2B LE][chunk_size 2B LE][data up to 4096B] */
    if (!s_ota.active || len < 5) { ota_send_error(OTA_ERR_INVALID_CMD); return; }

    uint16_t chunk_idx  = (uint16_t)cmd[1] | ((uint16_t)cmd[2] << 8);
    uint16_t chunk_size = (uint16_t)cmd[3] | ((uint16_t)cmd[4] << 8);
    const uint8_t *data = &cmd[5];

    if (chunk_idx != s_ota.next_chunk) {
        ota_send_error(OTA_ERR_SEQ_ERROR);
        return;
    }
    if (chunk_size > OTA_CHUNK_SIZE) {
        ota_send_error(OTA_ERR_INVALID_CMD);
        return;
    }
    if ((s_ota.bytes_written + chunk_size) > s_ota.total_size) {
        ota_send_error(OTA_ERR_SIZE_TOO_LARGE);
        return;
    }

    uint32_t write_addr = s_ota.flash_base + s_ota.bytes_written;
    if (!hal_flash_write(write_addr, data, chunk_size)) {
        ota_send_error(OTA_ERR_FLASH_WRITE);
        return;
    }

    s_ota.bytes_written += chunk_size;
    s_ota.next_chunk++;
    ota_send_ack(chunk_idx);
}

static void handle_ota_commit(const uint8_t *cmd, uint8_t len) {
    (void)cmd; (void)len;

    if (!s_ota.active) { ota_send_error(OTA_ERR_INVALID_CMD); return; }

    /* Verify SHA256 over written data */
    if (!hal_flash_verify_sha256(s_ota.flash_base,
                                  s_ota.bytes_written,
                                  s_ota.expected_sha256)) {
        ota_send_error(OTA_ERR_SHA256_FAIL);
        return;
    }

    /* Promote Bank B → Bank A for this model:
     * Erase bank A region, copy from B. */
    uint32_t bank_a_base = FLASH_MODEL_BANK_A +
                           (s_ota.flash_base - FLASH_MODEL_BANK_B);
    uint32_t n_sectors = (s_ota.bytes_written + 4095U) / 4096U;
    static uint8_t s_copy_buf[512];

    for (uint32_t s = 0; s < n_sectors; s++) {
        uint32_t sa = bank_a_base + s * 4096U;
        uint32_t sb = s_ota.flash_base + s * 4096U;

        if (!hal_flash_erase_sector(sa)) {
            ota_send_error(OTA_ERR_FLASH_ERASE);
            return;
        }
        /* Copy 4KB in 512-byte sub-chunks */
        for (uint32_t off = 0; off < 4096U; off += 512U) {
            hal_flash_read(sb + off, s_copy_buf, 512U);
            if (!hal_flash_write(sa + off, s_copy_buf, 512U)) {
                ota_send_error(OTA_ERR_FLASH_WRITE);
                return;
            }
        }
    }

    ota_send_complete();
    hal_delay_ms(100);   /* Allow I2C TX to complete */
    hal_reset();         /* Reboot into new model */
}

static void handle_ota_abort(void) {
    s_ota.active = false;
    g_sys.ota_in_progress = false;
    g_sys.state = SYS_STATE_IDLE;
}

/* ─────────────────────────────────────────────
   Public API
   ───────────────────────────────────────────── */
void ota_init(void) {
    memset(&s_ota, 0, sizeof(s_ota));
}

void ota_process_i2c_cmd(const uint8_t *buf, uint8_t len) {
    if (len == 0) return;

    switch (buf[0]) {
        case OTA_CMD_START:  handle_ota_start(buf, len);  break;
        case OTA_CMD_CHUNK:  handle_ota_chunk(buf, len);  break;
        case OTA_CMD_COMMIT: handle_ota_commit(buf, len); break;
        case OTA_CMD_ABORT:  handle_ota_abort();          break;
        default:             ota_send_error(OTA_ERR_INVALID_CMD); break;
    }
}

bool ota_is_active(void) { return s_ota.active; }
