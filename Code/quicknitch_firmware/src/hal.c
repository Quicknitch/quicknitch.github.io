/**
 * hal.c
 * Hardware Abstraction Layer implementation — Himax WE-I Plus
 *
 * Implements camera, PDM, I2C slave, HW-AccAI, flash, power management.
 * All register writes assume a BSP that matches Himax SDK memory map.
 */

#include "hal.h"
#include <stddef.h>

/* ─────────────────────────────────────────────
   Frame buffers (double-buffered) in SRAM
   32-byte aligned for DMA
   ───────────────────────────────────────────── */
uint8_t  g_frame_buf[2][CAM_BYTES_PER_FRAME] __attribute__((aligned(32)));
volatile uint8_t  g_frame_active = 0;
volatile uint8_t  g_frame_ready  = 1;
volatile bool     g_frame_new    = false;

/* Audio ring buffer */
int16_t  g_audio_ring[AUDIO_RING_SAMPLES] __attribute__((aligned(4)));
volatile uint32_t g_audio_write_idx = 0;

/* SysTick counter */
static volatile uint32_t s_tick_ms = 0;

/* I2C TX double buffer */
static uint8_t  s_i2c_tx_buf[I2C_MSG_SIZE];
static volatile bool s_i2c_tx_busy = false;

/* ─────────────────────────────────────────────
   CRC-16/CCITT (poly 0x1021, init 0xFFFF)
   ───────────────────────────────────────────── */
uint16_t hal_crc16_ccitt(const uint8_t *data, uint32_t len) {
    uint16_t crc = 0xFFFFU;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000U) ? ((crc << 1) ^ 0x1021U) : (crc << 1);
        }
    }
    return crc;
}

/* ─────────────────────────────────────────────
   SysTick ISR
   ───────────────────────────────────────────── */
void SysTick_Handler(void) {
    s_tick_ms++;
}

uint32_t hal_get_tick_ms(void) { return s_tick_ms; }

void hal_delay_ms(uint32_t ms) {
    uint32_t start = s_tick_ms;
    while ((s_tick_ms - start) < ms) { __asm volatile("nop"); }
}

/* ─────────────────────────────────────────────
   System initialisation
   ───────────────────────────────────────────── */
void hal_systick_init(void) {
    /* Configure SysTick for 1ms at 400MHz */
    volatile uint32_t *SYST_RVR = (volatile uint32_t *)0xE000E014UL;
    volatile uint32_t *SYST_CVR = (volatile uint32_t *)0xE000E018UL;
    volatile uint32_t *SYST_CSR = (volatile uint32_t *)0xE000E010UL;
    *SYST_RVR = (QN_CPU_FREQ_HZ / QN_SYSTICK_FREQ_HZ) - 1U;
    *SYST_CVR = 0;
    *SYST_CSR = 0x07U;   /* CLKSOURCE=core, TICKINT=1, ENABLE=1 */
}

void hal_system_init(void) {
    hal_systick_init();

    /* Enable clocks for all peripherals during init */
    PMU->CLK_ENABLE = PMU_CLK_CAMERA | PMU_CLK_ACCAI |
                      PMU_CLK_PDM    | PMU_CLK_DMA;

    hal_camera_init();
    hal_pdm_init();
    hal_i2c_slave_init(I2C_SLAVE_ADDR);
    hal_accai_init();
}

void hal_reset(void) {
    /* ARM AIRCR reset */
    volatile uint32_t *AIRCR = (volatile uint32_t *)0xE000ED0CUL;
    *AIRCR = (0x05FA0000UL | (1U << 2));
    while (1) {}
}

void __attribute__((noreturn)) hal_fault(const char *msg) {
    (void)msg;
    /* In production: write msg to a fault log in retained RAM, then reset */
    hal_reset();
    while (1) {}
}

/* ─────────────────────────────────────────────
   Power gating
   ───────────────────────────────────────────── */
void hal_power_gate_accai(bool gate) {
    if (gate) {
        HW_ACCAI->CLK_GATE = 1U;
        PMU->CLK_DISABLE = PMU_CLK_ACCAI;
    } else {
        PMU->CLK_ENABLE  = PMU_CLK_ACCAI;
        HW_ACCAI->CLK_GATE = 0U;
        /* Warm-up: wait ~10 cycles */
        for (volatile int i = 0; i < 10; i++) { __asm volatile("nop"); }
    }
}

void hal_power_gate_camera(bool gate) {
    if (gate) PMU->CLK_DISABLE = PMU_CLK_CAMERA;
    else      PMU->CLK_ENABLE  = PMU_CLK_CAMERA;
}

void hal_power_gate_pdm(bool gate) {
    if (gate) PMU->CLK_DISABLE = PMU_CLK_PDM;
    else      PMU->CLK_ENABLE  = PMU_CLK_PDM;
}

void hal_enter_sleep(void) {
    __asm volatile("wfi");   /* Wait For Interrupt — ARM sleep */
}

/* ─────────────────────────────────────────────
   Camera — HM01B0 init & capture
   ───────────────────────────────────────────── */

/* HM01B0 I2C address (programming interface) */
#define HM01B0_I2C_ADDR   0x24U

/* Minimal register set for QVGA mono8, 30fps, auto-exposure */
static const uint16_t s_hm01b0_init[][2] = {
    {0x0103, 0x00},  /* SW reset release */
    {0x0350, 0x00},  /* No test pattern  */
    {0x1000, 0x43},  /* AE enabled, frame rate 30fps */
    {0x1001, 0x00},
    {0x2000, 0x07},  /* Mono output      */
    {0x0383, 0x01},  /* No x-skip        */
    {0x0387, 0x01},  /* No y-skip        */
    {0x0101, 0x00},  /* No flip/mirror   */
    {0xFFFF, 0xFF},  /* End marker       */
};

static void hm01b0_write_reg(uint16_t reg, uint8_t val) {
    /* In real BSP this goes through the camera-side I2C master port.
     * Stub: assume Himax SDK hx_drv_sensor_set_reg() equivalent. */
    (void)reg; (void)val;
}

void hal_camera_init(void) {
    /* Assert reset, wait, release */
    volatile uint32_t *cam_ctrl = (volatile uint32_t *)(CAMERA_IF_BASE + 0x00);
    *cam_ctrl = 0;
    hal_delay_ms(5);
    *cam_ctrl = 1;
    hal_delay_ms(5);

    for (int i = 0; s_hm01b0_init[i][0] != 0xFFFF; i++) {
        hm01b0_write_reg(s_hm01b0_init[i][0],
                         (uint8_t)s_hm01b0_init[i][1]);
    }
}

void hal_camera_start_capture(uint8_t *buf) {
    dma_chan_t *ch = DMA_CHAN(DMA_CAMERA_CHAN);
    ch->SRC_ADDR = CAMERA_IF_BASE + 0x10;   /* camera pixel FIFO */
    ch->DST_ADDR = (uint32_t)buf;
    ch->COUNT    = CAM_BYTES_PER_FRAME;
    ch->CTRL     = 0x03U;   /* enable + done IRQ */
}

void hal_camera_stop(void) {
    dma_chan_t *ch = DMA_CHAN(DMA_CAMERA_CHAN);
    ch->CTRL = 0;
}

/* Camera frame-done DMA ISR */
void Camera_DMA_IRQHandler(void) {
    dma_chan_t *ch = DMA_CHAN(DMA_CAMERA_CHAN);
    if (ch->STATUS & 0x02U) {          /* done flag */
        ch->STATUS = 0x02U;            /* clear */
        g_frame_ready = g_frame_active;
        g_frame_active ^= 1U;
        g_frame_new = true;
        /* Restart capture into new active buffer */
        hal_camera_start_capture(g_frame_buf[g_frame_active]);
    }
}

const uint8_t *hal_camera_get_frame(void) {
    g_frame_new = false;
    return g_frame_buf[g_frame_ready];
}

/* ─────────────────────────────────────────────
   PDM Microphone
   Software decimation: PDM bit-stream @ 1.024 MHz → 16kHz PCM INT16
   CIC filter (order 5, decimation 64) + compensation FIR
   ───────────────────────────────────────────── */

/* 5th-order CIC integrator state */
static int32_t s_cic_int[5]  = {0};
static int32_t s_cic_comb[5] = {0};
static int32_t s_cic_prev[5] = {0};
static uint32_t s_cic_count  = 0;

/* Compensation FIR (15-tap, designed for CIC droop correction) */
static const int16_t s_comp_fir[15] = {
    -18, -42, -80, -110, -98, 12, 256, 512, 256, 12, -98, -110, -80, -42, -18
};
static int16_t s_fir_buf[15] = {0};
static uint8_t s_fir_idx     = 0;

static int16_t pdm_process_bit(uint8_t bit) {
    /* CIC integrate */
    int32_t x = bit ? 32 : -32;
    for (int k = 0; k < 5; k++) {
        s_cic_int[k] += (k == 0) ? x : s_cic_int[k-1];
    }

    s_cic_count++;
    if (s_cic_count < PDM_DECIMATION_FACTOR) return 0x7FFF; /* sentinel: no sample yet */

    s_cic_count = 0;

    /* CIC comb */
    int32_t out = s_cic_int[4];
    for (int k = 0; k < 5; k++) {
        int32_t diff = out - s_cic_prev[k];
        s_cic_prev[k] = out;
        out = diff;
    }

    /* Gain scaling: CIC gain = (D*M)^N = 64^5 / normalise to 16-bit */
    out >>= 16;

    /* Compensation FIR */
    s_fir_buf[s_fir_idx] = (int16_t)CLAMP(out, -32768, 32767);
    s_fir_idx = (s_fir_idx + 1) % 15;
    int32_t acc = 0;
    for (int k = 0; k < 15; k++) {
        int idx = (s_fir_idx + k) % 15;
        acc += (int32_t)s_fir_buf[idx] * s_comp_fir[k];
    }
    acc >>= 10;
    return (int16_t)CLAMP(acc, -32768, 32767);
}

/* PDM ISR — called per byte received from PDM FIFO */
void PDM_IRQHandler(void) {
    volatile uint32_t *pdm_dr = (volatile uint32_t *)(PDM_BASE + 0x04);
    uint8_t pdm_byte = (uint8_t)(*pdm_dr & 0xFF);
    uint8_t mask = 0x80U;
    while (mask) {
        int16_t sample = pdm_process_bit((pdm_byte & mask) ? 1 : 0);
        if (sample != (int16_t)0x7FFF) {
            uint32_t wi = g_audio_write_idx & (AUDIO_RING_SAMPLES - 1U);
            g_audio_ring[wi] = sample;
            g_audio_write_idx++;
        }
        mask >>= 1;
    }
}

void hal_pdm_init(void) {
    volatile uint32_t *pdm_cr  = (volatile uint32_t *)(PDM_BASE + 0x00);
    volatile uint32_t *pdm_clk = (volatile uint32_t *)(PDM_BASE + 0x08);
    /* PDM clock = CPU_FREQ / (2 * 195) ≈ 1.026 MHz for 16kHz * 64 */
    *pdm_clk = 195U;
    *pdm_cr  = 0x03U;  /* enable + IRQ on each byte */
}

void hal_pdm_start(void) {
    volatile uint32_t *pdm_cr = (volatile uint32_t *)(PDM_BASE + 0x00);
    *pdm_cr |= (1U << 0);
}

void hal_pdm_stop(void) {
    volatile uint32_t *pdm_cr = (volatile uint32_t *)(PDM_BASE + 0x00);
    *pdm_cr &= ~(1U << 0);
}

void hal_audio_get_window(int16_t *dst) {
    /* Copy most recent AUDIO_WINDOW_SAMPLES from ring buffer */
    uint32_t wi    = g_audio_write_idx;
    uint32_t start = (wi - AUDIO_WINDOW_SAMPLES) & (AUDIO_RING_SAMPLES - 1U);
    for (uint32_t i = 0; i < AUDIO_WINDOW_SAMPLES; i++) {
        dst[i] = g_audio_ring[(start + i) & (AUDIO_RING_SAMPLES - 1U)];
    }
}

/* ─────────────────────────────────────────────
   HW-AccAI
   ───────────────────────────────────────────── */
void hal_accai_init(void) {
    HW_ACCAI->CLK_GATE = 0U;   /* ungated initially */
}

bool hal_accai_run(
        const uint8_t *model_addr,
        uint32_t       model_size,
        const int8_t  *input,
        uint32_t       input_bytes,
        int8_t        *output,
        uint32_t       output_bytes) {

    (void)input_bytes; (void)output_bytes;

    /* Ungate accelerator clock */
    hal_power_gate_accai(false);

    HW_ACCAI->MODEL_ADDR  = (uint32_t)model_addr;
    HW_ACCAI->MODEL_SIZE  = model_size;
    HW_ACCAI->INPUT_ADDR  = (uint32_t)input;
    HW_ACCAI->OUTPUT_ADDR = (uint32_t)output;

    /* Start inference */
    HW_ACCAI->CTRL = ACCAI_CTRL_START;

    /* Poll until done (typically <20ms at 5fps budget) */
    uint32_t timeout = hal_get_tick_ms() + 200U;
    while (!(HW_ACCAI->STATUS & ACCAI_STATUS_DONE)) {
        if (hal_get_tick_ms() > timeout) {
            hal_power_gate_accai(true);
            return false;
        }
    }

    bool ok = !(HW_ACCAI->STATUS & ACCAI_STATUS_ERROR);

    /* Gate clock to save power */
    hal_power_gate_accai(true);
    return ok;
}

/* ─────────────────────────────────────────────
   I2C Slave
   ───────────────────────────────────────────── */
#define I2C_RX_BUF_SIZE  64U
static uint8_t  s_i2c_rx_buf[I2C_RX_BUF_SIZE];
static uint8_t  s_i2c_rx_len = 0;
static volatile bool s_i2c_rx_ready = false;
static uint8_t  s_i2c_tx_idx  = 0;
static uint8_t  s_i2c_tx_len  = 0;

void hal_i2c_slave_init(uint8_t addr) {
    I2C_SLAVE->OAR1 = (uint32_t)(addr << 1) | (1U << 14); /* 7-bit mode */
    I2C_SLAVE->CR2  = 0x24U;  /* IRQ on ADDR, RXNE, TXE, STOPF */
    I2C_SLAVE->CR1  = 0x01U;  /* PE=1, enable peripheral */
}

bool hal_i2c_send_msg(const qn_i2c_msg_t *msg) {
    if (s_i2c_tx_busy) return false;
    memcpy(s_i2c_tx_buf, msg, I2C_MSG_SIZE);
    s_i2c_tx_idx = 0;
    s_i2c_tx_len = I2C_MSG_SIZE;
    s_i2c_tx_busy = true;
    /* Data will be clocked out on master-read in I2C ISR */
    return true;
}

bool hal_i2c_read_cmd(uint8_t *buf, uint8_t *len) {
    if (!s_i2c_rx_ready) return false;
    *len = s_i2c_rx_len;
    memcpy(buf, s_i2c_rx_buf, s_i2c_rx_len);
    s_i2c_rx_ready = false;
    return true;
}

void I2C_Slave_IRQHandler(void) {
    uint32_t sr1 = I2C_SLAVE->SR1;

    if (sr1 & I2C_SR1_ADDR) {
        /* Clear ADDR by reading SR1 then SR2 */
        (void)I2C_SLAVE->SR2;
        s_i2c_rx_len = 0;
        return;
    }
    if (sr1 & I2C_SR1_RXNE) {
        uint8_t byte = (uint8_t)(I2C_SLAVE->DR & 0xFF);
        if (s_i2c_rx_len < I2C_RX_BUF_SIZE) {
            s_i2c_rx_buf[s_i2c_rx_len++] = byte;
        }
        return;
    }
    if (sr1 & I2C_SR1_TXE) {
        if (s_i2c_tx_busy && s_i2c_tx_idx < s_i2c_tx_len) {
            I2C_SLAVE->DR = s_i2c_tx_buf[s_i2c_tx_idx++];
        } else {
            I2C_SLAVE->DR = 0xFF;  /* NACK filler */
            s_i2c_tx_busy = false;
        }
        return;
    }
    if (sr1 & I2C_SR1_STOPF) {
        /* Clear STOPF */
        (void)I2C_SLAVE->SR1;
        I2C_SLAVE->CR1 |= 1U;
        if (s_i2c_rx_len > 0) {
            s_i2c_rx_ready = true;
        }
    }
}

/* ─────────────────────────────────────────────
   Flash
   ───────────────────────────────────────────── */
#define FLASH_KEY1  0x45670123UL
#define FLASH_KEY2  0xCDEF89ABUL

typedef struct {
    volatile uint32_t ACR;
    volatile uint32_t KEYR;
    volatile uint32_t SR;
    volatile uint32_t CR;
    volatile uint32_t AR;
    volatile uint32_t RESERVED;
    volatile uint32_t OBR;
    volatile uint32_t WRPR;
} flash_ctrl_regs_t;

#define FLASH_CTRL ((flash_ctrl_regs_t *)FLASH_CTRL_BASE)

static void flash_unlock(void) {
    FLASH_CTRL->KEYR = FLASH_KEY1;
    FLASH_CTRL->KEYR = FLASH_KEY2;
}
static void flash_lock(void) { FLASH_CTRL->CR |= (1U << 7); }
static void flash_wait_busy(void) {
    while (FLASH_CTRL->SR & 0x01U) {}
}

bool hal_flash_erase_sector(uint32_t addr) {
    flash_unlock();
    flash_wait_busy();
    FLASH_CTRL->CR = (1U << 1);   /* PER = page erase */
    FLASH_CTRL->AR = addr;
    FLASH_CTRL->CR |= (1U << 6);  /* STRT */
    flash_wait_busy();
    bool ok = !(FLASH_CTRL->SR & 0x14U);
    flash_lock();
    return ok;
}

bool hal_flash_write(uint32_t addr, const uint8_t *data, uint32_t len) {
    flash_unlock();
    FLASH_CTRL->CR = (1U << 0);   /* PG = programming */
    volatile uint16_t *ptr = (volatile uint16_t *)addr;
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t hw = (uint16_t)data[i] | ((i + 1 < len) ? ((uint16_t)data[i+1] << 8) : 0xFF00U);
        *ptr++ = hw;
        flash_wait_busy();
    }
    flash_lock();
    return true;
}

void hal_flash_read(uint32_t addr, uint8_t *data, uint32_t len) {
    memcpy(data, (const void *)addr, len);
}

/* SHA-256 verification stub — replace with actual SW SHA-256 */
bool hal_flash_verify_sha256(uint32_t addr, uint32_t len,
                              const uint8_t expected_hash[32]) {
    /* Production: run sw_sha256 over [addr..addr+len-1], compare to expected */
    (void)addr; (void)len; (void)expected_hash;
    return true; /* STUB */
}
