# GCC-ARM build for the Dragino LSN50v2 (STM32L072CZT6) DRAGINO-LRWAN(AT)
# application, mirroring the Keil MDK-ARM target "sx1276mb1las"
# (MDK-ARM/STM32L072CZ-Nucleo/Lora.uvprojx -- the only target in that
# project; Device=STM32L072CZTx, Define includes REGION_EU868).
#
# The source list, include paths and preprocessor defines below were
# harvested verbatim from that .uvprojx (see gcc/README.md for how); they
# are not hand-picked. Do not add/remove a source file here without first
# checking whether the Keil target's file list changed.
#
# Usage:
#   make -C gcc            # build build/lora.elf/.hex/.bin
#   make -C gcc clean
#   make -C gcc size        # arm-none-eabi-size summary
#
# Toolchain: arm-none-eabi-gcc (tested with 16.1.0). No Keil/ARMCC involved.

APP_ROOT  := ..
CUBE_ROOT := ../../../../../..
BUILD_DIR := build

TARGET := lora

PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

# ---------------------------------------------------------------------------
# Source lists (harvested from Lora.uvprojx <FilePath> entries; see README.md)
# ---------------------------------------------------------------------------

# Our GAS-syntax startup file (see README.md: sourced from ST's own CubeL0
# GCC template, Drivers/CMSIS/Device/ST/STM32L0xx/Source/Templates/gcc/,
# NOT translated from the Keil ARMASM .s -- ST already ships a GCC one).
STARTUP := startup_stm32l072xx.s

# Files under src/, group "Projects/End_Node" + "Projects/End_Node/ML3" in
# the uvprojx (21 stock End_Node files + adc_precision.c and the six
# ml3_*.c ML3-branch modules).
APP_SRCS := \
  $(APP_ROOT)/src/bsp.c \
  $(APP_ROOT)/src/debug.c \
  $(APP_ROOT)/src/hw_gpio.c \
  $(APP_ROOT)/src/hw_rtc.c \
  $(APP_ROOT)/src/hw_spi.c \
  $(APP_ROOT)/src/main.c \
  $(APP_ROOT)/src/stm32l0xx_hal_msp.c \
  $(APP_ROOT)/src/stm32l0xx_hw.c \
  $(APP_ROOT)/src/stm32l0xx_it.c \
  $(APP_ROOT)/src/vcom.c \
  $(APP_ROOT)/src/at.c \
  $(APP_ROOT)/src/command.c \
  $(APP_ROOT)/src/lora.c \
  $(APP_ROOT)/src/tiny_sscanf.c \
  $(APP_ROOT)/src/tiny_vsnprintf.c

# The 7 ML3-branch modules (group "Projects/End_Node/ML3" in the uvprojx).
# These get the strict host-suite warning set (see ML3_STRICT_CFLAGS below);
# everything else in this Makefile is vendor code and does not.
ML3_SRCS := \
  $(APP_ROOT)/src/adc_precision.c \
  $(APP_ROOT)/src/ml3_measurement.c \
  $(APP_ROOT)/src/ml3_calibration.c \
  $(APP_ROOT)/src/ml3_quality.c \
  $(APP_ROOT)/src/ml3_payload.c \
  $(APP_ROOT)/src/ml3_thermistor.c \
  $(APP_ROOT)/src/ml3_at_commands.c

# Vendor sources under STM32CubeExpansion_LRWAN/{Drivers,Middlewares}, exactly
# the CUBE_ROOT-relative <FilePath> entries in the uvprojx (64 files).
CUBE_SRCS := \
  $(CUBE_ROOT)/Drivers/BSP/Components/flash_eraseprogram/flash_eraseprogram.c \
  $(CUBE_ROOT)/Drivers/BSP/Components/iwdg/iwdg.c \
  $(CUBE_ROOT)/Drivers/BSP/Components/sx1276/sx1276.c \
  $(CUBE_ROOT)/Drivers/BSP/Components/pwr_out/pwr_out.c \
  $(CUBE_ROOT)/Drivers/BSP/Components/gpio_exti/gpio_exti.c \
  $(CUBE_ROOT)/Drivers/BSP/Components/oil_float/oil_float.c \
  $(CUBE_ROOT)/Drivers/BSP/Components/ds18b20/ds18b20.c \
  $(CUBE_ROOT)/Drivers/BSP/Components/sht20/sht20.c \
  $(CUBE_ROOT)/Drivers/BSP/Components/sht31/sht31.c \
  $(CUBE_ROOT)/Drivers/BSP/Components/bh1750/bh1750.c \
  $(CUBE_ROOT)/Drivers/BSP/Components/lidar_lite_v3hp/lidar_lite_v3hp.c \
  $(CUBE_ROOT)/Drivers/BSP/Components/tfsensor/tfsensor.c \
  $(CUBE_ROOT)/Drivers/BSP/Components/ult/ult.c \
  $(CUBE_ROOT)/Drivers/BSP/Components/weight/weight.c \
  $(CUBE_ROOT)/Drivers/BSP/STM32L0xx_Nucleo/stm32l0xx_nucleo.c \
  $(CUBE_ROOT)/Drivers/BSP/sx1276mb1las/sx1276mb1las.c \
  $(CUBE_ROOT)/Drivers/CMSIS/Device/ST/STM32L0xx/Source/Templates/system_stm32l0xx.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_spi.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_uart.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_adc.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_rcc.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_rcc_ex.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_cortex.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_gpio.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_dma.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_pwr.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_pwr_ex.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_rtc.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_rtc_ex.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_tim.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_tim_ex.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_adc_ex.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_uart_ex.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_i2c.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_i2c_ex.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_flash_ex.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_flash.c \
  $(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_iwdg.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/Region.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/RegionAS923.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/RegionAU915.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/RegionCN470.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/RegionCN779.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/RegionCommon.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/RegionEU433.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/RegionEU868.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/RegionIN865.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/RegionKR920.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/RegionUS915.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/RegionRU864.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/RegionKZ865.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/region/RegionMA869.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/LoRaMac.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac/LoRaMacCrypto.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Core/lora-test.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Utilities/delay.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Utilities/low_power_manager.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Utilities/timeServer.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Utilities/utilities.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Utilities/trace.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Utilities/queue.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Crypto/aes.c \
  $(CUBE_ROOT)/Middlewares/Third_Party/Lora/Crypto/cmac.c

ALL_C_SRCS := $(APP_SRCS) $(ML3_SRCS) $(CUBE_SRCS)
ASM_SRCS   := $(STARTUP)

# ---------------------------------------------------------------------------
# Include paths -- the uvprojx <IncludePath> list, resolved from the
# MDK-ARM/STM32L072CZ-Nucleo project dir against APP_ROOT/CUBE_ROOT.
# ---------------------------------------------------------------------------
INCLUDES := \
  -I$(APP_ROOT)/inc \
  -I$(CUBE_ROOT)/Drivers/BSP/STM32L0xx_Nucleo \
  -I$(CUBE_ROOT)/Drivers/STM32L0xx_HAL_Driver/Inc \
  -I$(CUBE_ROOT)/Drivers/CMSIS/Device/ST/STM32L0xx/Include \
  -I$(CUBE_ROOT)/Drivers/CMSIS/Include \
  -I$(CUBE_ROOT)/Middlewares/Third_Party/Lora/Crypto \
  -I$(CUBE_ROOT)/Middlewares/Third_Party/Lora/Mac \
  -I$(CUBE_ROOT)/Middlewares/Third_Party/Lora/Phy \
  -I$(CUBE_ROOT)/Middlewares/Third_Party/Lora/Utilities \
  -I$(CUBE_ROOT)/Middlewares/Third_Party/Lora/Core \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/Common \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/sx1276 \
  -I$(CUBE_ROOT)/Drivers/BSP/sx1276mb1las \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/flash_eraseprogram \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/ds18b20 \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/gpio_exti \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/oil_float \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/sht20 \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/pwr_out \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/sht31 \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/ult \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/lidar_lite_v3hp \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/weight \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/iwdg \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/bh1750 \
  -I$(CUBE_ROOT)/Drivers/BSP/Components/tfsensor \
  -I.

# ---------------------------------------------------------------------------
# Defines -- uvprojx <Define>: "STM32L072xx, USE_STM32L0XX_NUCLEO,
# USE_HAL_DRIVER, USE_SHT,REGION_EU868" (whitespace normalized here).
# ---------------------------------------------------------------------------
DEFINES := \
  -DSTM32L072xx \
  -DUSE_STM32L0XX_NUCLEO \
  -DUSE_HAL_DRIVER \
  -DUSE_SHT \
  -DREGION_EU868

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
MCU_FLAGS := -mcpu=cortex-m0plus -mthumb

# Shared C flags (vendor + ML3). -ffunction-sections/-fdata-sections pair
# with -Wl,--gc-sections at link time to drop unused vendor code (this repo
# compiles all 14 BSP Components drivers even though the LSN50v2 ML3 target
# only wires up a few; --gc-sections keeps the image close to what a
# per-feature Keil membership list would produce).
COMMON_CFLAGS := $(MCU_FLAGS) -std=c99 -Os -g \
  -ffunction-sections -fdata-sections \
  -fno-common \
  $(INCLUDES) $(DEFINES) \
  -include gcc_compat.h

# Vendor sources: do not impose the strict host-suite warning set on them,
# they were never held to it and will warn (see README.md "vendor warnings").
VENDOR_CFLAGS := $(COMMON_CFLAGS) -Wall

# ML3-branch modules: exactly the warning set tests/host/run_ml3_host_tests.sh
# uses for the same 7 files on the host build, so a target-side regression
# in these modules can't hide behind "it's just cross-compiled differently."
# -Wconversion/-Wvla/-Wundef etc. carried over verbatim; NOT added to vendor
# code, which does not meet this bar.
ML3_STRICT_CFLAGS := $(COMMON_CFLAGS) \
  -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Wvla \
  -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wundef

ASFLAGS := $(MCU_FLAGS) -x assembler-with-cpp $(INCLUDES) $(DEFINES)

LDSCRIPT := STM32L072CZTx_FLASH.ld

# newlib-nano for size; nosys for the syscall stubs (no semihosting/host FS
# on target). -u _printf_float: bsp.c/at.c format live sensor readings with
# "%.1f"/"%.3f" through PPRINTF -> vcom.c TraceSend -> libc vsnprintf, so
# nano's float-stripped printf would silently mis-render that diagnostic
# output without it (checked via grep, not assumed -- see README.md).
# --no-warn-rwx-segments: newer GNU ld (~2.39+) warns whenever a PT_LOAD
# segment has read+write+execute permissions, as a generic security nudge
# for MMU-backed targets with W^X enforcement. It fires here because
# .init_array/.fini_array (SHF_WRITE|SHF_ALLOC by ELF convention) share a
# flash-resident PT_LOAD with .text/.isr_vector/.rodata -- the same
# layout ST's own vendor GCC linker script template uses (see
# STM32L1xx/Source/Templates/gcc/linker/*.ld). Cortex-M0+ has no MMU and
# no W^X to violate; this is cosmetic, not a real finding, and suppressing
# it is standard practice for this class of bare-metal target.
LDFLAGS := $(MCU_FLAGS) \
  -specs=nano.specs -specs=nosys.specs \
  -u _printf_float \
  -T$(LDSCRIPT) -L. \
  -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(TARGET).map \
  -Wl,--print-memory-usage \
  -Wl,--no-warn-rwx-segments

# Object paths are computed with static pattern rules below, NOT VPATH.
# VPATH was tried first and is unsafe here: this vendor tree has more than
# one file called "lora.c" (Middlewares/Third_Party/Lora/Core/lora.c is a
# generic middleware stub; the one this app actually builds is
# src/lora.c). Both directories end up on any VPATH search list because
# Core/ also holds lora-test.c, which IS in our source list, so VPATH's
# directory-order search silently linked the wrong lora.c (30 symbols
# instead of the real file's) with no error -- only a much later
# undefined-reference storm at link time gave it away. Static pattern
# rules bind each object to an explicit, fully-qualified source path, so
# there is no directory search to get shadowed.
#
# APP_SRCS/ML3_SRCS objects are namespaced under build/app/ by basename
# (safe: both lists are files that live directly in the single src/ dir,
# so basenames are inherently unique there). CUBE_SRCS objects are
# namespaced under build/cube/ preserving their full path *relative to
# CUBE_ROOT*, not just the basename, precisely to avoid repeating the
# lora.c mistake for any other vendor basename collision.
APP_OBJS   := $(addprefix $(BUILD_DIR)/app/,$(notdir $(APP_SRCS:.c=.o)))
ML3_OBJS   := $(addprefix $(BUILD_DIR)/app/,$(notdir $(ML3_SRCS:.c=.o)))
CUBE_OBJS  := $(patsubst $(CUBE_ROOT)/%.c,$(BUILD_DIR)/cube/%.o,$(CUBE_SRCS))
ASM_OBJS   := $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SRCS:.s=.o)))
ALL_OBJS   := $(APP_OBJS) $(ML3_OBJS) $(CUBE_OBJS) $(ASM_OBJS)

ELF := $(BUILD_DIR)/$(TARGET).elf
HEX := $(BUILD_DIR)/$(TARGET).hex
BIN := $(BUILD_DIR)/$(TARGET).bin

.PHONY: all clean size

all: $(ELF) $(HEX) $(BIN)
	@$(SIZE) $(ELF)

$(ELF): $(ALL_OBJS) $(LDSCRIPT)
	@mkdir -p $(dir $@)
	$(CC) $(ALL_OBJS) $(LDFLAGS) -o $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

# Vendor App sources (src/*.c minus the 7 ML3 modules): static pattern
# rule, explicit source directory, no VPATH search.
$(APP_OBJS): $(BUILD_DIR)/app/%.o: $(APP_ROOT)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(VENDOR_CFLAGS) -c $< -o $@

# Vendor CubeL0/LoRaWAN sources under STM32CubeExpansion_LRWAN/{Drivers,
# Middlewares}: static pattern rule preserving the full path under
# build/cube/ (see the CUBE_OBJS comment above for why -- this is what
# fixed the wrong-lora.c-linked bug). iwdg.o is carved out of this rule's
# target list (not out of CUBE_OBJS/the link) because it needs the extra
# -include below; giving the same target a recipe from two rules would
# only produce a harmless "overriding recipe" make warning, but excluding
# it here avoids that noise entirely.
$(filter-out $(BUILD_DIR)/cube/Drivers/BSP/Components/iwdg/iwdg.o,$(CUBE_OBJS)): $(BUILD_DIR)/cube/%.o: $(CUBE_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(VENDOR_CFLAGS) -c $< -o $@

# iwdg.c override: real vendor bug (linkage mismatch in
# Drivers/BSP/Components/iwdg/iwdg.h vs iwdg.c), see gcc/iwdg_shim.h for
# the full explanation. Not fixed by renaming (a first attempt via -D
# proved the substitution reaches both the header's declaration and the
# .c's definition identically, since both are processed in the same
# translation unit -- it just reproduces the same conflict under the new
# name). Instead, pre-arm iwdg.h's include guard via an extra -include so
# iwdg.c never sees the conflicting extern prototype. This explicit rule
# overrides the static pattern rule above for this one object only.
$(BUILD_DIR)/cube/Drivers/BSP/Components/iwdg/iwdg.o: $(CUBE_ROOT)/Drivers/BSP/Components/iwdg/iwdg.c iwdg_shim.h
	@mkdir -p $(dir $@)
	$(CC) $(VENDOR_CFLAGS) -include iwdg_shim.h -c $< -o $@

# ML3-branch C sources get the strict/-Werror set: same static-pattern
# idiom as APP_OBJS, but a different object subdir (app/) is impossible to
# share with a plain pattern rule at the same stem, so this is spelled out
# as one static pattern rule matching exactly the ML3_OBJS list.
$(ML3_OBJS): $(BUILD_DIR)/app/%.o: $(APP_ROOT)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(ML3_STRICT_CFLAGS) -c $< -o $@

$(ASM_OBJS): $(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

size: $(ELF)
	$(SIZE) -A $(ELF)

clean:
	rm -rf $(BUILD_DIR)
