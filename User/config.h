/*
 * config.h - centralized runtime-tunable parameters.
 *
 * Added 07/08/2026, per the project owner: this project's usual style
 * is "constant lives right next to what it configures, with a comment
 * explaining why" (see e.g. AM_SETTLE_MUTE_BLOCKS in demod_am.c, or
 * MENU_TILE_GAP in main.c) - good for understanding WHY a value is
 * what it is once you've found it, bad for finding it in the first
 * place across several thousand-line files if you don't already know
 * where to look. That style is UNCHANGED and stays the primary
 * source of truth for most things - screen geometry, colors, DSP
 * filter coefficients, and so on all stay exactly where they were.
 *
 * This file exists ONLY for the handful of values someone is actually
 * likely to want to tweak without reading a DSP comment first: what
 * the radio boots into, and how aggressively the RF-level auto-AGC
 * reacts. If you're looking for something NOT in this file, it's
 * still living next to its own explanation somewhere in main.c,
 * demod_am.c, or aic3204.c - grep for it, the comment there will
 * explain the tradeoffs a one-line #define here can't.
 *
 * Each #define below still only has ONE real owner (the .c file that
 * actually declares the variable/uses the value) - this file supplies
 * the NUMBER, the owning file supplies the explanation of what it
 * does and why that number, via a comment pointing back here or vice
 * versa. Search for the CONFIG_* name to find every place it's used.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "demod_am.h" /* for demod_mode_t - CONFIG_START_MODE below */

/* ===================== STARTUP DEFAULTS ================================
 * What the radio boots into, before you touch anything. See main.c's
 * s_tune_hz/s_tune_step_idx declarations and demod_am.c's s_mode
 * declaration for how each of these actually gets applied.
 */

/* Startup tuning frequency, Hz. 7.150.000 = 40m band - changed
 * 07/08/2026 from the old MS5351_CAPTURED_LO_HZ (90.8MHz), which made
 * no sense given CONFIG_START_MODE below is AM, not WFM. See
 * ms5351.h's MS5351_CAPTURED_LO_HZ comment - that constant is now
 * ONLY the hardware smoke-test replay frequency (ms5351_tune_captured()
 * in main(), byte-exact captured bytes, deliberately left alone), NOT
 * where the radio actually ends up - main() retunes to this value
 * right after that smoke test runs. */
#define CONFIG_TUNE_START_HZ  7150000UL

/* Startup tuning step. This is a raw INDEX into main.c's
 * k_tune_steps[]/BAND_STEP_* table, not a Hz value - deliberately not
 * a symbolic BAND_STEP_* reference here, to keep this file free of any
 * dependency on main.c's internals. 1 = BAND_STEP_1K (1kHz) as of
 * 07/08/2026 - sensible for HF voice tuning in the 40m/AM startup
 * band above. *** If you ever reorder/resize k_tune_steps[] in
 * main.c, re-check this index still points at what you think it does
 * - nothing enforces the two stay in sync. *** */
#define CONFIG_TUNE_START_STEP_IDX  1U

/* Startup demodulation mode - see demod_am.h's demod_mode_t. */
#define CONFIG_START_MODE  DEMOD_MODE_AM

/* Startup MIC_PGA ceiling (analog input gain BEFORE the ADC), in 0.5dB
 * units (40 = 20.0dB) - see main.c's s_pga_gain_db_x2 and
 * aic3204_set_pga_gain_db()'s 0-95 (0-47.5dB) field range. This is
 * the CEILING the RF-level auto-AGC backs off from (see below), not
 * necessarily what's actually applied to the codec at any given
 * moment. */
#define CONFIG_PGA_START_DB_X2  40

/* ===================== RF-LEVEL (ANALOG PGA) AUTO-AGC ==================
 * See main.c's s_rf_agc_enabled declaration comment for the full
 * design (off by default - toggled via the RFAGC menu tile), and
 * demod_am.c's rf_clip_scan() comment for the detection side. Same
 * general shape as the digital AGC's own profile choices: fast to
 * protect against clipping, slow to back off from that protection,
 * just several octaves coarser in both gain-step size and timing
 * because this drives real I2C writes to the codec, not per-sample
 * float math.
 */

/* Clipping detection (demod_am.c's rf_clip_scan()): how close to the
 * ADC's +/-32768 rail a raw sample has to get, and how many samples
 * in the same block need to be that close, before a block counts as
 * "clipped". See rf_clip_scan()'s comment for why both numbers matter
 * (a single near-full-scale sample can be a legitimate strong signal,
 * not clipping - it takes several to be confident). */
#define CONFIG_RF_CLIP_THRESHOLD   32000
#define CONFIG_RF_CLIP_MIN_COUNT   3U

/* PGA backoff step size (2.0dB, in 0.5dB units) and ceiling (30.0dB
 * max backoff below CONFIG_PGA_START_DB_X2, AT THE CURRENT Rin
 * level) - see main.c's rf_agc_poll(). */
#define CONFIG_RF_AGC_STEP_X2         4U
#define CONFIG_RF_AGC_BACKOFF_MAX_X2  60U

/* Rin (input impedance) escalation step - 6.0dB, matching the
 * datasheet's per-step attenuation for 10k->20k->40k. See
 * aic3204_set_input_impedance()'s *** IMPORTANT UNVERIFIED
 * ASSUMPTION *** comment before trusting the 20k/40k register values
 * this drives. */
#define CONFIG_RF_AGC_RIN_STEP_X2     12U

/* Ballistics: minimum gap between consecutive backoff/Rin steps
 * (attack), and how long the signal must have stayed clip-free before
 * easing back up one step (release) - see rf_agc_poll()'s comment for
 * why release is deliberately many seconds, not milliseconds (avoids
 * audibly pumping the gain in sync with a strong station's own
 * modulation). */
#define CONFIG_RF_AGC_ATTACK_COOLDOWN_MS   50UL
#define CONFIG_RF_AGC_RELEASE_COOLDOWN_MS  3000UL

/* ===================== RTTY DECODER ===================================
 * See rtty.h/rtty.c - first draft, added 08/08/2026, per the project
 * owner. USB/LSB only (RTTY is two tones inside an SSB passband, see
 * demod_am.c's RTTY INTEGRATION comment for the exact hookup).
 *
 * *** MARK/SPACE Hz ARE THE FIRST THING TO CHECK AGAINST A REAL SIGNAL
 * *** - these depend entirely on YOUR receive convention (LSB vs USB,
 * the station's shift polarity, your radio's audio passband) - there
 * is no universal correct value, only what actually lands where for
 * your setup. Defaults below are a reasonable common ham-radio
 * starting point (170Hz shift, mark below space), not a guarantee.
 * Use rtty_get_last_mark_mag()/rtty_get_last_space_mag() tuned to a
 * known RTTY signal to find where your two tones actually peak.
 */
#define CONFIG_RTTY_MARK_HZ    2525.0f
#define CONFIG_RTTY_SPACE_HZ   2075.0f  /* mark + 450Hz shift, the common ham convention */
#define CONFIG_RTTY_BAUD       50.0f   /* the near-universal HF ham RTTY rate */

/* Encoder step size while live-nudging mark/space against the tuning
 * scope (see rtty_set_mark_space_hz()'s comment in rtty.c and
 * tune_encoder_poll()'s RTTY branch in main.c). Was 100Hz - the
 * project owner couldn't center the marker lines precisely enough on
 * the real peaks with steps that coarse; 10Hz gives ~17 detents to
 * cross a 170Hz shift, fine enough to actually land on a peak rather
 * than always stepping past it. */
#define CONFIG_RTTY_ENCODER_STEP_HZ 10.0f

/* SHIFT tile (RADIO page, see main.c's ENCODER_TARGET_RTTY_SHIFT) -
 * step size and bounds for adjusting the mark/space SEPARATION
 * directly (as opposed to CONFIG_RTTY_ENCODER_STEP_HZ above, which
 * moves both together and leaves the separation alone). 5Hz steps
 * give fine control; the bounds cover the range of shifts actually in
 * use (170/200Hz common on HF, up to 850Hz on some VHF/UHF or
 * wide-shift systems) with some margin either side. */
#define CONFIG_RTTY_SHIFT_STEP_HZ 5.0f
#define CONFIG_RTTY_SHIFT_MIN_HZ  50.0f
#define CONFIG_RTTY_SHIFT_MAX_HZ  1000.0f

/* Verbose per-character raw-bit diagnostic (see rtty.c's
 * rtty_state_machine()) - prints the raw 5-bit Baudot code and
 * whether the stop bit landed where expected, for EVERY character
 * attempt, valid or not. Confirmed working (08/08/2026 - a real
 * "PORTAPACKABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890" decoded cleanly
 * once mark/space were tuned against the scope), so back to 0 for
 * normal use - flip to 1 again if a future decode-quality issue needs
 * this level of detail (one line per ~22ms character at 45.45 baud,
 * a lot of log traffic for something only useful while chasing a
 * bit-sync/framing bug specifically). */
#define CONFIG_RTTY_DIAG_ENABLED 0U

/* 1 by default now that RTTY graduated from a debug-build-only tool
 * to a real selectable mode (RTTY-L/RTTY-U in main.c's
 * k_demod_modes[]) - was tied to the (now-removed) RTTY_ENABLED build
 * flag. Starts OFF: selecting a plain AM/USB/LSB/NFM/WFM mode from
 * the mode picker turns it back off (see menu_mode_preset_callback()),
 * RTTY-L/RTTY-U turn it on - see rtty_get_enabled()'s callers. */
#define CONFIG_RTTY_ENABLED_DEFAULT 0U

/* ===================== DECODIFICADOR DE CW ============================
 *
 * Telegrafia Morse, 21/09/2026. Solo en USB/LSB, igual que el RTTY y por
 * la misma razon: el CW se oye como UN tono dentro del paso de banda de
 * una banda lateral. Ver demod_am.c, donde se engancha justo al lado del
 * RTTY y sobre el mismo audio ya diezmado a 12 kHz.
 *
 * El tono es el que se quiere OIR: el operador sintoniza hasta que el
 * pitido suena a esta frecuencia. 700 Hz es lo mas comun; a quien tenga
 * el oido cansado en agudos le ira mejor 600, y a quien tenga ruido de
 * red 800.
 *
 * La velocidad NO es un ajuste que mande: el decodificador la estima y
 * la persigue solo. Esto es solo por donde empieza a buscar, y sirve
 * para enganchar antes. Medido en el simulador: partiendo de 20 PPM
 * decodifica bien de 10 a 45 PPM sin que nadie le diga nada.
 */
/* ===================== IDENTIDAD DEL FIRMWARE =========================
 * Lo que sale en Ajustes -> Equipo -> Información. La fecha y la hora NO
 * se ponen aqui: salen de __DATE__ y __TIME__, o sea del momento de
 * compilar, y asi no hay forma de que digan una cosa y el binario sea
 * otra. Esto es lo unico que hay que subir a mano al sacar version. */
#define CONFIG_FW_VERSION  "Rediseño 11"

#define CONFIG_CW_PITCH_HZ      700.0f
#define CONFIG_CW_WPM_HINT       20.0f

#endif /* CONFIG_H */
