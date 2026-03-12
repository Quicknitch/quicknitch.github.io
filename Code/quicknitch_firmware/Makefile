# Makefile — Quicknitch Himax WE-I Plus Firmware
# Toolchain: arm-none-eabi-gcc
# Target: ARMv7-M (Cortex-M4F equivalent)

TARGET   := quicknitch_we1
BUILD    := build
SRCS_DIR := src
INC_DIR  := include

# ── Toolchain ──────────────────────────────────
CC      := arm-none-eabi-gcc
LD      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
OBJDUMP := arm-none-eabi-objdump
SIZE    := arm-none-eabi-size

# ── Architecture flags ──────────────────────────
ARCH_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

# ── Compiler flags ──────────────────────────────
CFLAGS  := $(ARCH_FLAGS)
CFLAGS  += -O2 -ffunction-sections -fdata-sections
CFLAGS  += -Wall -Wextra -Wshadow -fno-strict-aliasing
CFLAGS  += -std=c11
CFLAGS  += -I$(INC_DIR)
CFLAGS  += -DTFLM_AVAILABLE
# TFLM include path (adjust to your TFLM SDK location)
CFLAGS  += -I$(TFLM_ROOT)/tensorflow/lite/micro
CFLAGS  += -I$(TFLM_ROOT)

# Math library (for sinf, cosf, sqrtf etc.)
LDFLAGS := $(ARCH_FLAGS) -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map
LIBS    := -lm -lc -lgcc -lnosys

# TFLM static library (pre-built for cortex-m4)
TFLM_LIB := $(TFLM_ROOT)/gen/cortex_m_generic/libtensorflow-microlite.a

# ── Linker script ───────────────────────────────
LDSCRIPT := linker/we1plus.ld
LDFLAGS  += -T$(LDSCRIPT)

# ── Sources ─────────────────────────────────────
C_SRCS := \
    $(SRCS_DIR)/main.c         \
    $(SRCS_DIR)/hal.c          \
    $(SRCS_DIR)/wake_word.c    \
    $(SRCS_DIR)/person_detect.c\
    $(SRCS_DIR)/gesture.c      \
    $(SRCS_DIR)/face_recog.c   \
    $(SRCS_DIR)/safety_scene.c \
    $(SRCS_DIR)/ota.c

# Auto-generated model C arrays (produced by build_models.sh)
C_SRCS += \
    models/wake_word_model.c   \
    models/person_model.c      \
    models/gesture_model.c     \
    models/face_model.c        \
    models/safety_model.c

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS))

# ── Build targets ────────────────────────────────
.PHONY: all clean flash size

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin

$(BUILD)/%.o: %.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(TFLM_LIB)
	$(LD) $(LDFLAGS) $(OBJS) $(TFLM_LIB) $(LIBS) -o $@
	$(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD):
	mkdir -p $@

size: $(BUILD)/$(TARGET).elf
	$(SIZE) --format=sysv $<

flash: $(BUILD)/$(TARGET).bin
	# Using Himax Flash Tool (adapt path/port as needed)
	himax_flash_tool --port $(PORT) --image $<

clean:
	rm -rf $(BUILD)
