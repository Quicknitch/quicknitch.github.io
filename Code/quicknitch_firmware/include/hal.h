/**
 * hal.h
 * Hardware Abstraction Layer — Himax WE-I Plus peripherals
 *
 * Covers: HM01B0 camera, PDM mic, I2C slave, HW-AccAI, flash, power gating
 */

#ifndef QN_HAL_H
#define QN_HAL_H

#include "quicknitch.h"

/* ═══════════════════════════════════════════════
   Register base addresses (Himax WE-I Plus)
   Adjust to match actual SoC memory map / BSP.
   ═══════════════════════════════════════════════ */
#define HW_ACC_AI_BASE      0x40010000UL   /* CNN hardware accelerator      */
#define I2C_SLAVE_BASE      0x40005400UL   /* I2C slave peripheral          */
#define CAMERA_IF_BASE      0x40007000UL   /* Parallel camera interface     */
#define PDM_BASE            0x40006000UL   /* PDM microphone interface      */
#define FLASH_CTRL_BASE     0x40022000UL   /* Flash controller              */
#define PMU_BASE            0x40001000UL   /* Power Management Unit         */
#define DMA_BASE            0x40020000UL   /* DMA controller                */
#define NVIC_BASE           0xE000E100UL   /* ARM NVIC                      */

/* ─────────────────────────────────────────────
   HW-AccAI registers
   ───────────────────────────────────────────── */
typedef struct {
    volatile uint32_t CTRL;        /* [0]=start, [1]=done IRQ en, [4]=clk_gate */
    volatile uint32_t STATUS;      /* [0]=busy, [1]=done, [2]=error            */
    volatile uint32_t INPUT_ADDR;  /* DMA src: int8 tensor base address         */
    volatile uint32_t OUTPUT_ADDR; /* DMA dst: int8 result base address         */
    volatile uint32_t MODEL_ADDR;  /* Flash/SRAM address of .tflite flatbuffer  */
    volatile uint32_t MODEL_SIZE;  /* bytes                                     */
    volatile uint32_t CYCLES_HI;   /* 64-bit cycle counter                      */
    volatile uint32_t CYCLES_LO;
    volatile uint32_t CLK_GATE;    /* Write 1 to gate; 0 to ungate              */
    volatile uint32_t RESERVED[7];
} hw_accai_regs_t;

#define HW_ACCAI  ((hw_accai_regs_t *)HW_ACC_AI_BASE)

#define ACCAI_CTRL_START        (1U << 0)
#define ACCAI_CTRL_DONE_IRQ_EN  (1U << 1)
#define ACCAI_STATUS_BUSY       (1U << 0)
#define ACCAI_STATUS_DONE       (1U << 1)
#define ACCAI_STATUS_ERROR      (1U << 2)

/* ─────────────────────────────────────────────
   I2C Slave registers
   ───────────────────────────────────────────── */
typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t OAR1;        /* own address */
    volatile uint32_t DR;          /* data register */
    volatile uint32_t SR1;
    volatile uint32_t SR2;
    volatile uint32_t CCR;
    volatile uint32_t TRISE;
} i2c_regs_t;

#define I2C_SLAVE  ((i2c_regs_t *)I2C_SLAVE_BASE)

#define I2C_SR1_RXNE    (1U << 6)
#define I2C_SR1_TXE     (1U << 7)
#define I2C_SR1_BTF     (1U << 2)
#define I2C_SR1_ADDR    (1U << 1)
#define I2C_SR1_STOPF   (1U << 4)

/* ─────────────────────────────────────────────
   PMU — power gating
   ───────────────────────────────────────────── */
typedef struct {
    volatile uint32_t CLK_ENABLE;
    volatile uint32_t CLK_DISABLE;
    volatile uint32_t SLEEP_CTRL;
    volatile uint32_t WAKEUP_SRC;
} pmu_regs_t;

#define PMU ((pmu_regs_t *)PMU_BASE)

#define PMU_CLK_CAMERA   (1U << 0)
#define PMU_CLK_ACCAI    (1U << 1)
#define PMU_CLK_PDM      (1U << 2)
#define PMU_CLK_DMA      (1U << 3)

/* ─────────────────────────────────────────────
   DMA channel descriptor
   ───────────────────────────────────────────── */
typedef struct {
    volatile uint32_t SRC_ADDR;
    volatile uint32_t DST_ADDR;
    volatile uint32_t COUNT;       /* bytes */
    volatile uint32_t CTRL;        /* [0]=en,[1]=done IRQ,[4:5]=width 0=8b */
    volatile uint32_t STATUS;      /* [0]=busy,[1]=done */
} dma_chan_t;

#define DMA_CHAN(n)  ((dma_chan_t *)(DMA_BASE + (n) * 0x20UL))
#define DMA_CAMERA_CHAN   0
#define DMA_ACCAI_CHAN    1

/* ─────────────────────────────────────────────
   Camera frame buffer (double-buffered, in SRAM)
   ───────────────────────────────────────────── */
extern uint8_t g_frame_buf[2][CAM_BYTES_PER_FRAME];
extern volatile uint8_t g_frame_active;    /* index being captured */
extern volatile uint8_t g_frame_ready;     /* index ready for inference */
extern volatile bool    g_frame_new;       /* set by camera ISR */

/* ─────────────────────────────────────────────
   Audio ring buffer
   ───────────────────────────────────────────── */
#define AUDIO_RING_SAMPLES   (AUDIO_WINDOW_SAMPLES * 2U)
extern int16_t  g_audio_ring[AUDIO_RING_SAMPLES];
extern volatile uint32_t g_audio_write_idx;

/* ─────────────────────────────────────────────
   HAL API
   ───────────────────────────────────────────── */

/* System */
void hal_system_init(void);
void hal_systick_init(void);
uint32_t hal_get_tick_ms(void);
void hal_delay_ms(uint32_t ms);
void hal_reset(void);
void __attribute__((noreturn)) hal_fault(const char *msg);

/* Power */
void hal_power_gate_accai(bool gate);
void hal_power_gate_camera(bool gate);
void hal_power_gate_pdm(bool gate);
void hal_enter_sleep(void);

/* Camera */
void hal_camera_init(void);
void hal_camera_start_capture(uint8_t *buf);
void hal_camera_stop(void);
/* Returns pointer to completed frame (double-buffer swap) */
const uint8_t *hal_camera_get_frame(void);

/* PDM / Audio */
void hal_pdm_init(void);
void hal_pdm_start(void);
void hal_pdm_stop(void);
/* Copy AUDIO_WINDOW_SAMPLES into dst from ring buffer (most recent window) */
void hal_audio_get_window(int16_t *dst);

/* HW-AccAI */
void hal_accai_init(void);
/* Blocking: load model from flash, run inference on input, write to output */
bool hal_accai_run(
    const uint8_t *model_addr,
    uint32_t       model_size,
    const int8_t  *input,
    uint32_t       input_bytes,
    int8_t        *output,
    uint32_t       output_bytes);

/* I2C slave */
void hal_i2c_slave_init(uint8_t addr);
bool hal_i2c_send_msg(const qn_i2c_msg_t *msg);
/* Called from ISR — returns true if a write from master was received */
bool hal_i2c_read_cmd(uint8_t *buf, uint8_t *len);

/* Flash */
bool hal_flash_erase_sector(uint32_t addr);
bool hal_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);
void hal_flash_read(uint32_t addr, uint8_t *data, uint32_t len);
bool hal_flash_verify_sha256(uint32_t addr, uint32_t len,
                              const uint8_t expected_hash[32]);

/* CRC */
uint16_t hal_crc16_ccitt(const uint8_t *data, uint32_t len);

#endif /* QN_HAL_H */
