In memory of Carlos EB4DYL

# DEEPSDR 101 / HTOOL/ BAJEI SDR V5 GD32F450 Open Source Firmware

This project is a collaboration between EA8DGL Esteban, UA6YKK Alexandr and 
EA7GIB Blas, aiming to create open firmware for the DEEPSDR 101 and BAJEI SDR V5 
clone with the GD32F450 MCU. 

It is currently in the development phase, and programming is being carried 
out primarily using AI. The current version is functional and supports most 
DEEPSDR radio features.

We would welcome any collaboration or assistance with its development. 
Regards.

## Disclaimer

This firmware is provided **"as is"**, without warranty of any kind,
express or implied, including but not limited to warranties of
merchantability, fitness for a particular purpose, and
non-infringement.

This is a hobbyist, experimental project. Flashing this firmware onto
your hardware, and any hardware modifications you make in order to
use it (wiring, GPIO changes, RF front-end changes, etc.), are done
**entirely at your own risk**. The author assumes no responsibility
and accepts no liability for any damage, malfunction, data loss, or
other harm to your equipment - or to any other equipment, property,
or person - resulting from downloading, building, flashing, modifying,
or otherwise using this firmware, whether used as-is or altered by you
or any third party.

You are solely responsible for:
- Verifying that this firmware is suitable for and compatible with
  your specific hardware before flashing it (check that your mcu is
  the GD32F450VET6/VGT6!!!).
- Complying with all applicable radio spectrum, transmission, and
  equipment regulations in your country/region. (This firmware
  targets a *receiver* - it is not designed or intended to transmit -
  but it is still your responsibility to ensure your use of it, and
  of the underlying RF hardware, is fully compliant with local law.)
- Any consequences of modifying, adapting, or redistributing this
  firmware, including modifications made by you or by anyone else who
  obtains it from you.

No support, maintenance, or fitness for any particular use case is
guaranteed. Use of this project constitutes acceptance of this
disclaimer.

## Overview

This document covers three things deliberately kept separate from the
hardware/clock-tree description: what the project is, how to build
and flash it (both the ST-Link/OpenOCD workflow and the vendor
bootloader's `update4.bin` workflow), and exactly how the current menu
system and general UI behave. It reflects the UI/menu state as of
01/09/2026 — several tiles referenced here were added or moved during
that session; if the hardware doc still shows an older 3-page menu
with only a handful of tiles, this file supersedes it for anything
UI-related.

## 1. Project

This is a collaborative, hobbyist, bare-metal firmware project
targeting the GD32F450VET6 MCU used in the DEEPSDR 101 / BAJEI SDR V5
receiver boards — a direct-sampling QSD (Quadrature Sampling Detector)
SDR receiver with an 800x480 touchscreen. No RTOS; direct register
access via GigaDevice's standard peripheral library where it matters
(clocks, DMA, I2S, timers).

The project is in active development, programmed primarily with AI
assistance under the project owner's direction, with real-hardware
verification (oscilloscope, real reception tests) driving essentially
every design decision — nothing in the clock tree, RF chain, or DSP
path is trusted on paper alone; the commit/comment history throughout
the source consistently documents what was actually confirmed on the
bench versus what's still an assumption.

It currently supports AM, USB, LSB, NFM, and WFM reception, a
touch-driven panadapter/waterfall display with drag- and tap-to-tune
gestures, a paged settings menu covering RF/audio/display/digital-mode
options, RTTY decoding, a selectable AM/USB/LSB/NFM sample rate
(96kHz/48kHz — see 3.9), and a growing set of diagnostic and
quality-of-life tools (manual/auto AGC, selectable audio and channel
filter widths, spectrum auto-scaling, a calibrated S-meter dBm readout
(see 3.8), and a GD32-generated quadrature LO path for the lowest
tuning range where the board's MS5351 clock generator can't reliably
hold quadrature).

## 2. Building and Flashing

### 2.1 Prerequisites

```sh
sudo apt install gcc-arm-none-eabi openocd
```

### 2.2 Build

```sh
make            # build build/firmware.elf / .hex / .bin
make clean      # remove build artifacts
```

### 2.3 Flashing via ST-Link (OpenOCD)

This is the direct, debugger-based flashing path — used for
development, and for any board with an accessible SWD header.

```sh
make flash      # flash + verify + reset via OpenOCD
make erase      # mass-erase the chip via OpenOCD
```

Both targets use `openocd/gd32f450.cfg` — a **custom** target config,
not the stock `target/stm32f4x.cfg` that ships with OpenOCD. This
matters: the GD32F450's silicon ID makes OpenOCD misdetect it as a
dual-bank 2048KB STM32F42x/43x part, when the real part is a 512KB
single-bank device. Flashing with the wrong target config produces
`Error: checksum mismatch` after programming — if that happens, check
that the custom `.cfg` is actually the one being used, not a stock
STM32F4 profile.

Debugging in VS Code: with the Cortex-Debug extension installed
(suggested automatically via `.vscode/extensions.json`), pressing F5
launches the "Debug GD32F450 (OpenOCD + ST-Link)" configuration
already set up in `.vscode/launch.json`.

### 2.4 Flashing via `update4.bin` (vendor bootloader)

This board ships with a vendor bootloader that expects a specifically
padded and signed update file on external storage (SD card / flash),
rather than a raw `.bin` written at a fixed offset. This is the path
used for a normal user update — no SWD debugger required, useful for
distributing firmware to someone who only has the assembled radio.

```sh
make update4    # pad + sign build/firmware.bin as update4.bin
```

This target pads the built binary and prepends/appends the fixed
magic signature the vendor bootloader's update routine checks for
before it will accept a file — see the `Makefile` for the exact byte
layout and magic value it currently uses. The application itself is
linked to run from `0x08020000`, **not** `0x08000000` — the vendor
bootloader occupies the low part of flash and expects the application
image starting at that offset; `SCB->VTOR` is set explicitly in
`main()` at startup to match, so interrupts vector correctly regardless
of which flashing method was used to get the image onto the chip.

To use `update4.bin`: copy it to wherever the vendor bootloader expects
to find an update image (typically the root of an SD card, or a
specific USB-mass-storage path — this is board/bootloader-specific and
belongs in the hardware doc if not already documented there), then
power-cycle or otherwise trigger the bootloader's update sequence per
the board's own vendor documentation.

### 2.5 Which method to use

- **ST-Link/OpenOCD**: use during development, for any board with an
  accessible SWD header, or when something has gone wrong badly enough
  that the vendor bootloader itself might not be trustworthy (e.g.
  recovering from a bad flash).
- **`update4.bin`**: use for a normal end-user-style update on a board
  that's already running some firmware and boots into its vendor
  bootloader normally — no debugger needed.

## 3. User Interface and Menu System

### 3.1 Screen layout

Landscape 800x480 touchscreen, confirmed on real hardware. Reworked
01/09/2026 from an earlier 3-column layout (676px spectrum + a
vertical right-hand column) to a full-width layout — spectrum and
waterfall now span the whole screen, with S-meter/badges moved into a
horizontal strip under the top bar:

```
+--------------------------------------------------------------+
| TOP BAR (h=64): freq (big) | mode | step+vol | time | batt   |
+--------------------------------------------------------------+
| STATUS STRIP (h=40): S-meter | dBm | NR SPT AGC [profile] [BW] OVR RATE |
+--------------------------------------------------------------+
| SPECTRUM (796 wide, 240 tall)                                |
+--------------------------------------------------------------+
| WATERFALL (796 wide)                                         |
+--------------------------------------------------------------+
| BOTTOM BAR: 6 buttons (MODE VOL STEP NR BANDS MENU)          |
+--------------------------------------------------------------+
```

- **Frequency display** (top bar): tapping it opens a numeric keypad
  for direct frequency entry (see 3.5).
- **Spectrum panel gestures**:
  - **Drag** left/right tunes relatively, quantized to the currently
    selected tune STEP (100Hz-1MHz, see 3.4) rather than a fixed
    1kHz — dragging respects whatever step is dialed in, and changing
    STEP mid-drag takes effect on the very next movement.
  - **Tap** (as opposed to a drag — told apart by total finger travel
    since the press started, not by speed) tunes DIRECTLY to whatever
    frequency that point on the panadapter represents, using the
    exact same pixel-to-Hz mapping the panadapter's own tick labels
    use — so tapping precisely on a labeled tick tunes to precisely
    that frequency. Unlike the drag, a tap is not snapped to the
    current tune step, since the point is landing exactly where a
    signal's peak visually is.
- **Status strip** (below the top bar, above the spectrum): an
  S-meter (12-segment bar, driven by the same peak reading as the
  numeric readout beside it), a calibrated **dBm** numeric readout
  (see 3.8 for how it's calibrated and its real limitations — this
  replaced an earlier, uncalibrated SNR readout that used to live in
  the same spot), and a row of status badges (NR, SPT, AGC — plus two
  real tappable buttons showing the current AGC profile and audio
  filter width in the same row — OVR, and RATE showing 96K/48K/192K).
  Several of these are themselves tappable shortcuts to the same
  setting the matching menu tile controls.

### 3.2 Bottom bar

| Button | Action |
|---|---|
| MODE  | Opens the demod mode picker (AM/USB/LSB/NFM/WFM) |
| VOL   | Toggles the encoder between TUNE and VOLUME |
| STEP  | Opens the tune-step picker (100Hz-1MHz, 8 entries) |
| NR    | Cycles the AGC profile (OFF/SLW/MED/FST) - same action as tapping the AGC badge in the right column |
| BANDS | Opens the band preset picker |
| MENU  | Opens the settings menu (see 3.4) |

### 3.3 Rotary encoder

Same knob used for tuning doubles as the adjustment control for
whichever target is currently selected:

- **Rotate**: adjusts the current target — frequency by default, or
  whichever setting was last opened via a DETAIL view in the settings
  menu (VOLUME, SQUELCH, BACKLIGHT, SCALE, PGA, SMOOTH, SHIFT, ...).
- **Short press**: cycles the tune step when the target is TUNE,
  toggles which bound (LO/HI) is being adjusted when the target is
  SCALE; no effect for most other targets.
- **Long press**: unconditionally hands the knob back to TUNE and
  closes any open menu/picker screen — the universal "get me out of
  here" gesture, from anywhere.

Manually adjusting SCALE with the encoder automatically turns off
Spectrum AGC if it was on (see 3.4.3) — turning the knob always means
"I'm taking over," never "fight the auto-tracker."

### 3.4 Band / mode / step pickers

Reachable directly from the bottom bar, not nested inside the settings
menu:

- **BANDS** (12 presets, each one tap sets frequency + demod mode +
  tune step together): SW 49M, SW 41M, SW 31M, SW 19M, FM BCST,
  AIRBAND, 2M, VHF HI, 80M, 40M, 20M, 11M (CB).
- **MODE** (5): AM, USB, LSB, NFM, WFM.
- **STEP** (8): 100Hz, 1kHz, 5kHz, 10kHz, 12.5kHz, 25kHz, 100kHz, 1MHz.

### 3.5 Direct frequency entry (numeric keypad)

Tapping the frequency display in the top bar opens a 4x4 numeric
keypad:

```
 1   2   3   DEL
 4   5   6   CLR
 7   8   9
 .   0   KHZ MHZ
```

Type digits, optionally including a decimal point (`.`), then tap
`KHZ` or `MHZ` to apply. This lets a frequency be entered exactly the
way it's normally written — e.g. `14`, `.`, `2`, `0`, `0`, then `MHZ`
for 14.200MHz, or `0`, `.`, `6`, `2`, `1`, then `MHZ` for 621kHz — as
well as the older "plain integer count of the chosen unit" style
(e.g. `146520` then `KHZ` for 146.520MHz). `DEL` removes the last
character typed (including the point, if you backspace onto it);
`CLR` clears the whole entry. There is no bare "Hz" unit button —
entering a frequency out to single-Hz precision digit-by-digit had no
practical use once kHz/MHz entry with a decimal point covered every
realistic case.

### 3.6 Settings menu (MENU button)

A paged tile grid, confined to the panadapter area (the title bar,
S-meter/badges, and bottom bar all stay live and visible underneath):

- **Column 0** (all rows): a fixed page selector — **RADIO / UI / HW /
  DIG** — visually distinct from every option tile, so "switch page"
  reads differently at a glance from "adjust an option." The active
  page is filled solid; the others are outlined.
- **Columns 1-3**: the selected page's own option tiles, up to 9 slots
  (3x3) per page. Bottom-right is always **EXIT**, on every page,
  closing the whole menu.

Some tiles cycle or toggle directly on tap and stay on the grid (you
can tap several in a row without leaving the menu); others open a
full-screen DETAIL view where the encoder adjusts the value live and a
BACK tile returns to the grid.

#### RADIO page (8/8 slots full)

| Tile | Type | What it does |
|---|---|---|
| AGC | cycle | AGC profile: OFF / SLW / MED / FST. OFF genuinely bypasses the demod AGC's peak-tracking (fixed unity gain) rather than just being a very slow setting. |
| SQL | detail | Squelch threshold (AM + NFM) |
| VOL | detail | Volume |
| BW | detail | Audio filter width — **mode-dependent meaning**: 4K0/2K3/1K8 in AM/USB/LSB, but 15K/8K0/4K0 in WFM (same physical control and tile, different filter bank selected underneath depending on the current demod mode) |
| PGA | detail | Codec input gain, 0-47.5dB |
| NR | cycle | Spectral Subtraction noise-reduction strength (AM/USB/LSB only) |
| RFAGC | toggle | RF-level auto-attenuation: automatically steps the codec's input impedance (and backs off PGA) if the front end is overloading |
| ATT | cycle | Manual 3-way codec input attenuator: 0 / -6 / -12dB (10k/20k/40k input impedance) — independent of RFAGC, for deliberately picking a fixed attenuation |

#### UI page (6/6 slots full)

| Tile | Type | What it does |
|---|---|---|
| BL | detail | Backlight brightness |
| SCALE | detail | Spectrum/waterfall dB range (manual LO/HI bounds) |
| SPT | detail | Spatial smoothing passes (spectrum trace) |
| SMH | detail | Frame-to-frame smoothing |
| SPC | cycle | Spectrum trace style: heatmap vs line |
| ZOOM | cycle | Spectrum/waterfall zoom: 1x/2x/4x/8x |

#### HW page (7/8 slots used)

| Tile | Type | What it does |
|---|---|---|
| SPK | toggle | Speaker PA enable/mute |
| IFBW | toggle | WFM's **pre-discriminator** channel filter width: WIDE (96K, i.e. no filter — the full ±96kHz complex Nyquist bandwidth, unfiltered, the original/default behavior) vs NARROW (80K — a real channel filter ahead of the FM discriminator, for adjacent-channel/wideband-noise rejection on a crowded band or a weak station). This is a completely separate control from the BW tile above (which shapes the *demodulated audio*, after the discriminator) — IFBW filters the raw baseband I/Q *before* it. |
| SAGC | toggle | Spectrum/waterfall auto-scale: tracks the display's dB range from the actual incoming spectrum instead of only manual SCALE adjustment. On by default. |
| RATE | toggle | AM/USB/LSB/NFM sample rate: 96K (default) vs 48K. Added as a diagnostic/escape-hatch control for a birdie tied to a harmonic of the sample rate (see 3.9) — tapping it forces an immediate live reconfigure if a non-WFM mode is already active, not just on the next mode change. WFM is unaffected either way (always 192kHz). Persisted to CONFIG.CSV as of 01/09/2026, once 48kHz settled into being an everyday preference rather than just a bench A/B toggle. |
| *(slot 7 free)* | — | reserved |

#### DIG page (3/8 slots used)

| Tile | Type | What it does |
|---|---|---|
| SHIFT | detail | RTTY mark/space tone separation |
| BAUD | cycle | RTTY bit rate |
| INV | toggle | RTTY station NORMAL/REVERSE tone convention (independent of the USB/LSB sideband mirror, which RTTY-L/RTTY-U already handle separately) |
| *(slots 3-7 free)* | — | reserved for future digital modes (PSK31 and similar) |

### 3.7 Notes on tile placement

Several tiles ended up on pages that don't perfectly match their own
"category" (e.g. RF-related IFBW and RATE living on the HW page, or
ATT/RFAGC on RADIO alongside pure DSP settings) — this is a direct
consequence of the RADIO page filling up at 8/8 slots first; new
controls went wherever free slots existed rather than being sorted
by subject. If reorganizing the page layout in the future, be aware
several tiles rely on the shared underlying state (e.g. BW's
mode-dependent relabeling, IFBW/RATE's live-reconfigure-on-tap
behavior) — check each tile's own callback comment in `main.c` before
moving it, since a few have non-obvious side effects tied to exactly
when they run.

### 3.8 S-meter dBm calibration

The status-strip numeric readout next to the S-meter shows a
calibrated dBm figure (`smeter_dbm_update_and_draw()` in `main.c`),
derived from a real bench session against an external signal
generator with a known, calibrated dBm output:

- **Procedure**: AGC off (a genuine unity-gain bypass, not just a slow
  setting), ATT and PGA fixed at a known combination, a clean CW/AM
  tone fed at a known dBm across several frequencies and levels,
  reading the raw dBFS via a UART diagnostic
  (`smeter_dbfs_uart_report()`, 1Hz, unrounded — the 12-segment bar
  alone is far too coarse, 7dB/segment, for this).
- **Result**: a consistent `SMETER_CAL_OFFSET_DB = -38.2` dB offset
  held across 6 of 11 tested points (within about 1dB of each other),
  spanning both LO generation paths and several signal levels — good
  evidence it's genuinely representative, not a coincidence.
- **Dynamically compensated for ATT/PGA** — both are real, precisely
  known linear gain elements (ATT: the Rin selector, an exact 0/-6/
  -12dB signal drop per step; PGA: `aic3204_set_pga_gain_db()`, a real
  0.5dB-precision hardware stage), so the reading stays correct across
  ANY ATT/PGA combination, not just the one originally calibrated
  against — confirmed by direct testing on real hardware. The MAIN
  receive AGC still must be off for the reading to mean anything; that
  gain is genuinely dynamic, not a simple number that can be
  subtracted out the way ATT/PGA can.
- **Known gaps** — the reading will be wrong by a large, inconsistent
  amount (not just imprecise) in three situations, none of which this
  offset can paper over: (1) above roughly -50dBm input at this
  calibration's PGA setting, where the receiver visibly compresses;
  (2) the 37-60MHz RF low-pass filter range (`rf_lpf.c`), where both
  tested edges read about 30dB worse than the general cluster — a
  real, still-uninvestigated loss or possible relay/filter fault, not
  a calibration issue; (3) right at the low-band/high-band LO handoff
  (~4.8MHz), where the high-band side read about 20dB worse than the
  low-band side just below it, for a reason not yet tracked down.

### 3.9 Known RF quirks: internal-clock birdies

This board's own internal clock sources can and do produce birdies
(spurious tones from digital clock harmonics leaking into the RF
front end) — worth knowing about before chasing what looks like an
external interference source that's actually coming from inside the
receiver itself. Three separate clock domains are involved, each a
potential source of its own family of harmonics:

- **The MS5351/Si5351 LO generator's 26MHz reference crystal**
  (`ms5351.c`) — this drives the receiver's own local oscillator via
  PLLA/PLLB, so any of its own harmonics or PLL artifacts land
  wherever the current tuning happens to put them, moving with the
  VFO rather than sitting at one fixed spot.
- **The audio codec's 12.288MHz crystal** (`gd32_i2s.c`/`aic3204.c`) —
  the reference for MCLK and, downstream, the codec's own internal
  PLL (CODEC_CLKIN, currently 86.016MHz — see `aic3204.c`'s clock-
  chain comment) that ultimately produces the I2S bit clock and
  sample rate.
- **The I2S/sample-rate clock itself** — and this is the one with a
  **confirmed, on-the-bench** birdie: at 96kHz (AM/USB/LSB/NFM's
  previous fixed rate), the 287th harmonic of the sample rate
  (287 × 96kHz = 27.552MHz) lands squarely in the 11m/CB band,
  reproduced on both a modified and an unmodified board (ruling out a
  power-rail coupling issue specific to one unit). Since this harmonic
  number scales with whatever the sample rate actually is, switching
  rate moves the whole comb of harmonics to different frequencies —
  which is exactly why the **RATE tile** (HW page, 96K/48K) exists:
  moving Fs relocates this class of birdie to a different, hopefully
  less troublesome, spot rather than eliminating it outright. The
  same reasoning applies to WFM's own fixed 192kHz rate, which has its
  own comb of N×192kHz harmonics somewhere — not separately confirmed
  on the bench the way the 96kHz case was, but expected by the same
  mechanism, and not user-selectable the way AM/USB/LSB/NFM's rate is
  (WFM is always 192kHz).

None of these birdies are a firmware bug in the sense of something
this project can filter or calibrate away — they're consequence of
real clock energy on the same board as a sensitive front end. The
practical mitigations available today are: retuning slightly (the LO
harmonics move with the VFO), or trying the other sample rate via the
RATE tile (the I2S-clock harmonics move with Fs). If a stronger fix
(shielding, decoupling, a cleaner reference clock) is ever pursued,
it belongs in the hardware document rather than here.
