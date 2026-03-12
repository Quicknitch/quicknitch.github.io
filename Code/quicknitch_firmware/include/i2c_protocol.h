/**
 * i2c_protocol.h
 * ═══════════════════════════════════════════════════════════════
 * Quicknitch — WE-I Plus ↔ ESP32-S3 I2C Message Protocol
 * ═══════════════════════════════════════════════════════════════
 *
 * Physical layer:
 *   - I2C slave address:  0x42 (7-bit)
 *   - Speed:              400 kHz (Fast-mode)
 *   - Pull-ups:           4.7kΩ to 3.3V
 *   - SDA/SCL:            WE-I GPIO12/GPIO13 (BSP-specific)
 *
 * ───────────────────────────────────────────────────────────────
 * MESSAGE FORMAT (16 bytes, all transfers)
 * ───────────────────────────────────────────────────────────────
 *
 *  Byte  Field        Description
 *  ────  ──────────   ──────────────────────────────────────────
 *  [0]   header       Always 0x51 (magic; rejects bus noise)
 *  [1]   msg_type     Message type ID (see table below)
 *  [2-13] payload     12 bytes, layout depends on msg_type
 *  [14-15] crc16      CRC-16/CCITT-FALSE over bytes [0..13]
 *                     Poly=0x1021, Init=0xFFFF, RefIn=false
 *
 * ───────────────────────────────────────────────────────────────
 * MESSAGE TYPE TABLE
 * ───────────────────────────────────────────────────────────────
 *
 * ID    Name                  Direction   Description
 * 0x01  WAKE_WORD_DETECTED    WE-I→ESP    Wake word heard
 * 0x02  PERSON_DETECTED       WE-I→ESP    Person in frame + bbox
 * 0x03  PERSON_LOST           WE-I→ESP    Person gone >10s
 * 0x04  GESTURE_DETECTED      WE-I→ESP    Gesture classified
 * 0x05  FACE_RECOGNIZED       WE-I→ESP    Owner recognised
 * 0x06  FACE_UNKNOWN          WE-I→ESP    Unknown face seen
 * 0x07  SAFETY_ALERT          WE-I→ESP    Staircase / glass / dark
 * 0x08  SYSTEM_STATUS         WE-I→ESP    Periodic heartbeat
 * 0x09  OTA_ACK               WE-I→ESP    OTA chunk accepted
 * 0x0A  OTA_COMPLETE          WE-I→ESP    OTA done, rebooting
 * 0x0B  OTA_ERROR             WE-I→ESP    OTA error code
 * 0xA0  OTA_START (cmd)       ESP→WE-I    Begin OTA session
 * 0xA1  OTA_CHUNK (cmd)       ESP→WE-I    4KB data chunk
 * 0xA2  OTA_COMMIT (cmd)      ESP→WE-I    Finalise + verify
 * 0xA3  OTA_ABORT (cmd)       ESP→WE-I    Abort OTA
 * 0xE1  CMD_START_ENROLL      ESP→WE-I    Start face enrollment
 * 0xE2  CMD_ENABLE_GESTURE    ESP→WE-I    Activate gesture mode
 * 0xE3  CMD_DISABLE_GESTURE   ESP→WE-I    Deactivate gesture mode
 *
 * ───────────────────────────────────────────────────────────────
 * PAYLOAD LAYOUTS (bytes [2..13])
 * ───────────────────────────────────────────────────────────────
 *
 * 0x01  WAKE_WORD_DETECTED
 *   [2]  word_id          0=hey_nitch, 1=quicknitch
 *   [3]  confidence_pct   0-100
 *   [4]  noise_floor_dbfs Signed, cast to int8_t; typical -60..-40
 *   [5..13] reserved=0
 *
 * 0x02  PERSON_DETECTED
 *   [2-3]  bbox_x   Q8.8 fixed-point (uint16 LE), normalised 0..1
 *   [4-5]  bbox_y   Q8.8 fixed-point
 *   [6-7]  bbox_w   Q8.8 fixed-point
 *   [8-9]  bbox_h   Q8.8 fixed-point
 *   [10]   confidence_pct  0-100
 *   [11]   tracking_id     object ID (currently always 1)
 *   [12-13] reserved=0
 *
 *   Decode: x_f = bbox_x / 256.0f  (Q8.8 to float)
 *
 * 0x03  PERSON_LOST
 *   [2..13] all 0
 *
 * 0x04  GESTURE_DETECTED
 *   [2]    gesture_id      0=wave_hello, 1=point_fwd,
 *                          2=thumbs_up, 3=cross_arms_stop
 *   [3]    confidence_pct  0-100 (always ≥85 when sent)
 *   [4-5]  frame_count     uint16 LE, running inference counter
 *   [6..13] reserved=0
 *
 * 0x05/0x06  FACE_RECOGNIZED / FACE_UNKNOWN
 *   [2]    match           1=owner recognised, 0=unknown
 *   [3]    l2_dist_q8      L2 distance as uint8; decode: d = val/128.0f
 *   [4]    confidence_pct  0-100
 *   [5]    enrolled_slot   Always 0 (single-user device)
 *   [6..13] reserved=0
 *
 * 0x07  SAFETY_ALERT
 *   [2]    alert_class     0=safe,1=near_glass,2=staircase,
 *                          3=too_dark,4=outdoor
 *   [3]    confidence_pct  0-100
 *   [4]    flags           bit0=staircase, bit1=glass, bit2=dark
 *   [5..13] reserved=0
 *
 * 0x09  OTA_ACK
 *   [2-3]  chunk_idx  uint16 LE; 0xFFFF = START ack
 *   [4..13] reserved=0
 *
 * 0x0B  OTA_ERROR
 *   [2]    error_code  0x01=size_too_large, 0x02=flash_erase,
 *                      0x03=flash_write,    0x04=sha256_fail,
 *                      0x05=seq_error,      0x06=invalid_cmd
 *   [3..13] reserved=0
 *
 * ───────────────────────────────────────────────────────────────
 * OTA COMMAND PAYLOADS (ESP32-S3 writes; not 16-byte format)
 * These are raw I2C write transactions, variable length.
 * ───────────────────────────────────────────────────────────────
 *
 * 0xA0  OTA_START   Total 38 bytes:
 *   [0]     0xA0
 *   [1]     model_id  (0x01-0x05, see model IDs above)
 *   [2-5]   total_size  uint32 LE
 *   [6-37]  sha256[32]  expected SHA-256 of model data
 *
 * 0xA1  OTA_CHUNK   Variable, max 4101 bytes:
 *   [0]     0xA1
 *   [1-2]   chunk_idx   uint16 LE (0-based, sequential)
 *   [3-4]   chunk_size  uint16 LE (1-4096)
 *   [5..N]  data        raw model bytes
 *
 * 0xA2  OTA_COMMIT  8 bytes:
 *   [0]     0xA2
 *   [1]     model_id
 *   [2-7]   reserved=0
 *
 * 0xA3  OTA_ABORT   1 byte:
 *   [0]     0xA3
 *
 * ───────────────────────────────────────────────────────────────
 * TIMING CONSTRAINTS
 * ───────────────────────────────────────────────────────────────
 *
 * Max message rate (WE-I → ESP32):
 *   Person detect:   5 msg/s (200ms period)
 *   Gesture:         5 msg/s  — only when ToF < 1.2m
 *   Face recog:      2 msg/s (500ms period)
 *   Safety:          3 msg/s (333ms period) — alert only
 *   Wake word:       event-driven, typical <1/s
 *
 * OTA chunk timing:
 *   ESP32 should wait for OTA_ACK before sending next chunk.
 *   Timeout: 500ms per chunk. Retry up to 3× before abort.
 *   Full 80KB model @ 4KB chunks = 20 transactions ≈ 10s total.
 *
 * ───────────────────────────────────────────────────────────────
 * CRC COMPUTATION EXAMPLE (Python)
 * ───────────────────────────────────────────────────────────────
 *
 * import struct
 * def crc16_ccitt(data: bytes) -> int:
 *     crc = 0xFFFF
 *     for b in data:
 *         crc ^= b << 8
 *         for _ in range(8):
 *             crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
 *         crc &= 0xFFFF
 *     return crc
 *
 * # Build MSG_PERSON_DETECTED for bbox (0.3, 0.1, 0.2, 0.6), conf=87%
 * payload = bytearray(12)
 * struct.pack_into('<HHHH', payload, 0,
 *     int(0.3*256), int(0.1*256), int(0.2*256), int(0.6*256))
 * payload[8] = 87   # confidence
 * payload[9] = 1    # tracking_id
 * msg = bytes([0x51, 0x02]) + bytes(payload)
 * crc = crc16_ccitt(msg)  # appended as 2 LE bytes
 * full_msg = msg + struct.pack('<H', crc)
 * assert len(full_msg) == 16
 *
 * ───────────────────────────────────────────────────────────────
 * MODEL IDs (for OTA)
 * ───────────────────────────────────────────────────────────────
 * 0x01  Wake Word      (DS-CNN-S)
 * 0x02  Person Detect  (MobileNetV1-0.25)
 * 0x03  Gesture        (Temporal CNN)
 * 0x04  Face Recog     (MobileFaceNet)
 * 0x05  Safety Scene   (Safety CNN)
 */

#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H
/* This file is documentation only — see quicknitch.h for C type defs */
#include "quicknitch.h"
#endif /* I2C_PROTOCOL_H */
