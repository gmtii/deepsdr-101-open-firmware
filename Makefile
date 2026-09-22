TARGET     = firmware
BUILD_DIR  = build

# --- Toolchain ---
PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc -x assembler-with-cpp
LD      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

# --- MCU ---
CPU    = -mcpu=cortex-m4
FPU    = -mfpu=fpv4-sp-d16
FLOAT  = -mfloat-abi=hard
MCUFLAGS = $(CPU) -mthumb $(FPU) $(FLOAT)

# --- Defines ---
DEFS = -DGD32F450 -DUSE_STDPERIPH_DRIVER -DHXTAL_VALUE=12288000U
# Master switch for all debug_print*() UART logging (238 call sites,
# see User/debug_uart.h) - off by default. For a bring-up/debug
# session, build with `make DEBUG_UART_ENABLED=1` (no source edits
# needed).
DEBUG_UART_ENABLED ?= 0
DEFS += -DDEBUG_UART_ENABLED=$(DEBUG_UART_ENABLED)
# Codec input wiring (18/09/2026). 0 (default) = DIFFERENTIAL I/Q, the
# normal wiring for the RF front-end (QSD): IN2L(+)/IN2R(-) and
# IN3R(+)/IN3L(-). 1 = SINGLE-ENDED bench mode, for a line-level audio
# source wired straight to the codec with the front-end disconnected
# (see aic3204_set_input_single_ended_test() in User/aic3204.h). Pick it
# on the command line, e.g. `make clean && make AIC3204_SINGLE_ENDED_TEST=1`
# - do a `make clean` whenever you change it, the Makefile does not
# track defines.
AIC3204_SINGLE_ENDED_TEST ?= 0
DEFS += -DAIC3204_SINGLE_ENDED_TEST=$(AIC3204_SINGLE_ENDED_TEST)

# Display fix (20/09/2026). 1 (default): after a menu closes, the 2px gap between the spectrum and waterfall
# panels is blanked (a row of the menu's tiles used to leave a coloured stripe there). 0 = previous behaviour;
# use it to rebuild the binaries of earlier tests byte for byte, e.g. `make UI_PANEL_GAP_FIX=0`.
UI_PANEL_GAP_FIX ?= 1
DEFS += -DUI_PANEL_GAP_FIX=$(UI_PANEL_GAP_FIX)

# HFDL tuning knobs (18/09/2026). Every name below is a compile-time
# constant in the HFDL code that is wrapped in #ifndef, so it can be
# overridden from the command line for A/B experiments without editing
# any source, e.g.
#     make clean && make HFDL_QUANT_POWER_EMA_LAMBDA=0.05f HFDL_AGC_BW=0.02f
# Values are passed straight through as C literals (so floats need the
# trailing "f"). Unset knobs keep the source default, so a plain `make`
# builds exactly what it always did. Do a `make clean` whenever you change
# one - the Makefile does not track defines. NOTE: the comments inside the
# HFDL sources have long advertised `make HFDL_...=` overrides, but until
# this block existed nothing here forwarded them to the compiler, so they
# silently had no effect. See each knob's own comment for what it does:
#   HFDL_AGC_BW                     hfdl_agc.h       chain AGC loop bandwidth
#   HFDL_COSTAS_DEFAULT_ALPHA/BETA  hfdl_costas.h    carrier loop gains
#   HFDL_SYMSYNC_LOOP_B0/NEG_A1/RATE_ADJ  hfdl_symsync.h  symbol timing loop
#   HFDL_EQ_NLMS                    hfdl_equalizer.c 1 = normalized LMS like liquid-dsp/dumphfdl (mu then means what it does there, 0.1)
#   HFDL_EQ_MU_OVERRIDE             hfdl_equalizer.h equalizer LMS step size
#   HFDL_EQ_POWER_EMA_LAMBDA, HFDL_EQ_GAIN_LEAK_RATE, HFDL_EQ_DISABLE_GAIN_RENORM
#                                   hfdl_equalizer.c
#   HFDL_QUANT_POWER_EMA_LAMBDA     hfdl_payload_decode.c  soft-bit amplitude tracker
#   HFDL_MIXER_SIGN_OVERRIDE        hfdl_iq_mixer.h  subcarrier mixer sign
#   HFDL_BURST_ON_RATIO/OFF_RATIO   hfdl_scope.c     burst detector thresholds
#   HFDL_CHAIN_ALWAYS_ON            demod_am.c       1 = run the demod chain all the time, ignore the burst detector
#   HFDL_ABORT_BAD_TRAINING         demod_am.c       1 = give up on a frame after the initial training if its error >= HFDL_ABORT_MSE_THRESHOLD (default 1.0); off by default
#   HFDL_ICAO24_DB                  icao24_db.c      1 = show the aircraft model next to the ICAO, read from ICAO24.BIN on the flash volume (see scripts/make_icao24_compact.py); off by default
#   HFDL_AIRCRAFT_TABLE             hfdl_aircraft.c  1 = third HFDL screen: one row per aircraft (ICAO, model, flight, position, time, event) + ground station and preamble funnel; off by default
#   HFDL_M1_DIAG                    demod_am.c       1 = per-frame M1/A2 correlation strengths on the debug UART; off by default
#   HFDL_HOLD_MAX_DROPOUT_MS        demod_am.c       keep the chain running through burst-detector dropouts of up to this many ms once a frame is locked (0 = off)
HFDL_KNOBS := HFDL_AGC_BW HFDL_COSTAS_DEFAULT_ALPHA HFDL_COSTAS_DEFAULT_BETA \
    HFDL_SYMSYNC_LOOP_B0 HFDL_SYMSYNC_LOOP_NEG_A1 HFDL_SYMSYNC_RATE_ADJ \
    HFDL_EQ_NLMS HFDL_EQ_MU_OVERRIDE HFDL_EQ_POWER_EMA_LAMBDA HFDL_EQ_GAIN_LEAK_RATE HFDL_EQ_DISABLE_GAIN_RENORM \
    HFDL_QUANT_POWER_EMA_LAMBDA HFDL_MIXER_SIGN_OVERRIDE \
    HFDL_BURST_ON_RATIO HFDL_BURST_OFF_RATIO HFDL_CHAIN_ALWAYS_ON HFDL_HOLD_MAX_DROPOUT_MS \
    HFDL_ABORT_BAD_TRAINING HFDL_ABORT_MSE_THRESHOLD HFDL_M1_DIAG HFDL_ICAO24_DB HFDL_AIRCRAFT_TABLE
$(foreach k,$(HFDL_KNOBS),$(if $($(k)),$(eval DEFS += -D$(k)=$($(k)))))
# Escape hatch for anything not listed above: make HFDL_EXTRA_DEFS="-DFOO=1 -DBAR=2"
HFDL_EXTRA_DEFS ?=
DEFS += $(HFDL_EXTRA_DEFS)
# RTTY_ENABLED build flag REMOVED 08/08/2026: RTTY (User/rtty.c,
# rtty_scope.c) graduated from a debug-build-only tool to a real
# selectable mode (RTTY-L/RTTY-U in main.c's k_demod_modes[]) once
# validated against a real signal - both files are always compiled in
# and reachable from the normal mode picker now, no special build
# needed. For a debug session with the verbose per-character bit-level
# dump, flip CONFIG_RTTY_DIAG_ENABLED in User/config.h instead of a
# Makefile flag (still want `make DEBUG_UART_ENABLED=1` too, to
# actually see any of rtty.c's own debug_print*() output).
# ARM_MATH_DSP: the GD32F450 (Cortex-M4F) really does have the DSP
# extension, so let CMSIS-DSP call the real __QADD8/__SSAT/... it
# needs instead of pulling in its dsp/none.h software-emulation
# fallback (which would redefine the same intrinsics our CMSIS-Core
# headers already provide - see CMSIS/DSP/Include/cmsis_compiler.h).
DEFS += -DARM_MATH_DSP=1 -DARM_MATH_CM4

# --- Includes ---
INCLUDES  = -ICMSIS/Include
INCLUDES += -ICMSIS/GD/GD32F4xx/Include
INCLUDES += -IFirmware/Include
INCLUDES += -IUser
INCLUDES += -IFt8Lib
# CMSIS-DSP: PrivateInclude before Include isn't required, but keeping
# both on the path matches upstream's own build files.
INCLUDES += -ICMSIS/DSP/Include
INCLUDES += -ICMSIS/DSP/PrivateInclude
# USB CDC-ACM (GD32F4xx USB device library, device-mode subset only -
# see USB/README-ish comment in User/usb_serial.h for the TIMER2 vs
# TIMER5 note). usb_conf.h/usbd_conf.h live in USB/conf, adapted from
# GigaDevice's CDC_ACM demo to drop the GD32450i-EVAL BSP dependency.
INCLUDES += -IUSB/conf
INCLUDES += -IUSB/driver/Include
INCLUDES += -IUSB/device/core/Include
INCLUDES += -IUSB/device/class/cdc/Include
INCLUDES += -IUSB/ustd/class/cdc
INCLUDES += -IUSB/ustd/common

# --- Sources ---
C_SOURCES   = $(wildcard User/*.c)
C_SOURCES  += $(wildcard Ft8Lib/ft8/*.c)
C_SOURCES  += $(wildcard Firmware/Source/*.c)
C_SOURCES  += CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c
# CMSIS-DSP: only the specific functions demod_am.c uses (biquad
# cascade DF1 float32 + complex magnitude float32), not the whole
# library - keeps build times and flash usage down. Add more
# CMSIS/DSP/Source/*/arm_*.c files here if you use more of it.
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c
C_SOURCES  += CMSIS/DSP/Source/ComplexMathFunctions/arm_cmplx_mag_f32.c
# arm_fir_f32/_init_f32: the Hilbert transformer for SSB (USB/LSB).
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_fir_f32.c
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_fir_init_f32.c
# arm_fir_decimate/_interpolate: the SSB decimated architecture
# (192kHz -> 12kHz for the Hilbert stage, then back up for the DAC).
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_fir_decimate_f32.c
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_fir_decimate_init_f32.c
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_fir_interpolate_f32.c
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_fir_interpolate_init_f32.c
# USB CDC-ACM (device mode only - host-mode driver/interrupt sources
# are not built at all, see USB/driver/Source)
C_SOURCES  += USB/driver/Source/drv_usb_core.c
C_SOURCES  += USB/driver/Source/drv_usb_dev.c
C_SOURCES  += USB/driver/Source/drv_usbd_int.c
C_SOURCES  += USB/device/core/Source/usbd_core.c
C_SOURCES  += USB/device/core/Source/usbd_enum.c
C_SOURCES  += USB/device/core/Source/usbd_transc.c
C_SOURCES  += USB/device/class/cdc/Source/cdc_acm_core.c

ASM_SOURCES = CMSIS/GD/GD32F4xx/Source/GCC/startup_gd32f450_470.S

# --- Linker ---
LDSCRIPT = GD32F450VE_FLASH.ld
# -lm: needed as of 31/07/2026 for atan2f() (demod_am.c's WFM
# discriminator, see demod_am.h's WFM note on why libm instead of
# CMSIS-DSP's arm_atan2_f32()) - nano.specs/nosys.specs alone don't
# pull libm in, only libc; without this the link fails with
# "undefined reference to atan2f". Nothing else in the project uses
# libm, so this wasn't needed before.
LDFLAGS  = $(MCUFLAGS) -specs=nano.specs -specs=nosys.specs -T$(LDSCRIPT) \
           -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections -lm

# -O0 was costing far more than the AM filter itself: with the FPU
# enabled but nothing optimized, every per-sample float op in the
# demod/filter path re-spilled to memory instead of staying in FPU
# registers, run from the RX DMA ISR on top of it. -O2 -g3 keeps full
# debug symbols (just some locals become "optimized out" in gdb).
CFLAGS  = $(MCUFLAGS) $(DEFS) $(INCLUDES) -Wall -O2 -g3 -ffunction-sections -fdata-sections
ASFLAGS = $(MCUFLAGS) -g3

OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.S=.o)))

vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.S $(sort $(dir $(ASM_SOURCES)))

.PHONY: all clean flash erase

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin
	$(SIZE) $(BUILD_DIR)/$(TARGET).elf

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.S Makefile | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) $(LDSCRIPT)
	$(LD) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O binary -S $< $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

# --- Programming with ST-Link + OpenOCD ---
# We use our own target/gd32f450.cfg (in openocd/) instead of the
# stock target/stm32f4x.cfg: the GD32F450's silicon ID makes OpenOCD
# detect it as a dual-bank, 2048KB STM32F42x/43x, when the real VET6
# part is 512KB single-bank. See openocd/gd32f450.cfg.
#
# IMPORTANT: no address at the end of the program command. The .elf
# already carries the load address embedded in each section
# (0x08020000, set by the linker script). Passing an extra address
# here makes OpenOCD treat it as a "relocation offset" that gets ADDED
# to each section's address in the .elf (a feature meant for .bin
# files, which carry no address of their own) - that's what used to
# happen: it tried to write at 0x08020000+0x08020000 = 0x10040000,
# outside the flash range (0x08000000-0x08080000), silently failing to
# program anything.
flash: $(BUILD_DIR)/$(TARGET).elf
	openocd -f interface/stlink.cfg -f openocd/gd32f450.cfg \
		-c "program $(BUILD_DIR)/$(TARGET).elf verify reset exit"

erase:
	openocd -f interface/stlink.cfg -f openocd/gd32f450.cfg \
		-c "init; reset halt; stm32f2x mass_erase 0; reset; exit"

# --- Generate update4.bin for the real bootloader ---
# The bootloader requires a fixed 8-byte signature at offset 0x40000
# (256KB) of the file, or it hangs with "APP not Programmed". This
# signature is constant (the same across any valid vendor firmware,
# independent of content), extracted by analyzing the bootloader. This
# target pads our .bin to 256KB and appends it.
UPDATE4_MAGIC := 8f25c865599c5531
update4: $(BUILD_DIR)/$(TARGET).bin
	python3 -c "\
firmware = open('$(BUILD_DIR)/$(TARGET).bin','rb').read(); \
magic = bytes.fromhex('$(UPDATE4_MAGIC)'); \
padded = firmware + b'\x00' * (0x40000 - len(firmware)) + magic; \
open('update4.bin','wb').write(padded); \
print('update4.bin generated:', len(padded), 'bytes')"

