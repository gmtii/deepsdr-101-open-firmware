#ifndef RTTY_H
#define RTTY_H

#include <stdint.h>

/*
 * RTTY (radioteletype) decoder - added 08/08/2026, per the project
 * owner. Same "decoupled from the radio pipeline" shape as nr_ss.h:
 * this module knows nothing about demod modes or decimation, it just
 * processes whatever RTTY_BLOCK_SAMPLES-sample float AUDIO block it's
 * handed (READ-ONLY - unlike nr_ss_process(), this doesn't modify the
 * audio, it's a passive listener). demod_am.c owns deciding WHEN to
 * call it (USB/LSB only - RTTY is received as two audio tones inside
 * an SSB passband, there's no meaning to "RTTY in AM/NFM/WFM") and
 * WHERE the audio comes from - see demod_am.c's RTTY INTEGRATION
 * comment, right where s_ssb_dec (the SSB chain's own already-12kHz
 * buffer) gets reused for this, same "don't decimate twice" reasoning
 * already established for USB/LSB's own inline NR path.
 *
 * *** VALIDATED 08/08/2026 *** - a real "PORTAPACK" + alphabet +
 * digits test message decoded cleanly once mark/space were tuned
 * against rtty_scope.c's tuning scope and USB/LSB inversion was
 * handled correctly (see menu_mode_preset_callback()'s RTTY-L/RTTY-U
 * handling). Bit-sync timing and the Baudot table both hold up in
 * practice, not just in theory - CONFIG_RTTY_DIAG_ENABLED in config.h
 * has the verbose per-character raw-bit dump that got this confirmed,
 * left in place (default off) for next time a decode-quality issue
 * needs that level of detail. See rtty.c's top-of-file comment for
 * anything that still might need tuning for a DIFFERENT setup
 * (mark/space frequencies are inherently receiver/convention-specific
 * - see config.h's own warning on those).
 */

/*
 * Block size this module expects, matching demod_am.c's
 * DEC_BLOCK_SAMPLES (32 samples @ 12kHz) - same value, same
 * "not #include-d from demod_am.h, checked via _Static_assert()
 * instead" decoupling reasoning as nr_ss.h's NR_SS_BLOCK_SAMPLES.
 */
#define RTTY_BLOCK_SAMPLES 32U

/* Precomputes the Goertzel coefficients from config.h's
 * CONFIG_RTTY_MARK_HZ/SPACE_HZ/BAUD and resets all decoder state
 * (bit-sync state machine, output ring buffer). Call once at startup,
 * same as nr_ss_init(). */
void rtty_init(void);

/*
 * Processes exactly RTTY_BLOCK_SAMPLES of audio (READ-ONLY - audio[]
 * is not modified). `n` must equal RTTY_BLOCK_SAMPLES every call,
 * same contract as nr_ss_process(). Any successfully decoded
 * characters go into the output ring buffer - drain it with
 * rtty_get_char().
 */
void rtty_process(const float *audio, uint32_t n);

/*
 * Pulls one decoded character (already through the Baudot->ASCII/
 * FIGS-LTRS translation - see rtty.c's k_baudot_letters/k_baudot_figs
 * tables). Returns 1 and writes *out if a character was pending,
 * 0 (leaves *out untouched) if the output ring buffer is empty.
 * Call this from the main loop (NOT the ISR) to drain decoded text,
 * same "ISR sets state, main loop drains/acts on it" split as every
 * other cross-context field in this codebase.
 */
uint8_t rtty_get_char(char *out);

/*
 * Master on/off switch - same "toggle separate from any tuning value"
 * shape as nr_ss_set_enabled(). Defaults to CONFIG_RTTY_ENABLED_DEFAULT
 * (see config.h) at rtty_init() - 0 (off) by default; set from
 * menu_mode_preset_callback() in main.c when RTTY-L/RTTY-U is
 * selected from the mode picker, and back to 0 when any other mode is
 * picked.
 */
void rtty_set_enabled(uint8_t on);
uint8_t rtty_get_enabled(void);

/*
 * Diagnostics for tuning against a real signal - see rtty.c's
 * RTTY_DIAG_ENABLED. Raw per-block tone decision (1=mark, 0=space) and
 * the two Goertzel magnitudes it was decided from, all from the LAST
 * processed block - poll from the main loop for a live "is it even
 * seeing two distinct tones" check before worrying about whether
 * characters are decoding correctly.
 */
uint8_t rtty_get_last_bit(void);
float   rtty_get_last_mark_mag(void);
float   rtty_get_last_space_mag(void);

/*
 * Live mark/space Hz adjustment - see rtty.c's comment on
 * rtty_set_mark_space_hz() for the full story (short version: the
 * config.h defaults are a starting guess, not a guarantee - this lets
 * you nudge them against the real tuning scope without recompiling).
 * Recomputes the Goertzel coefficients immediately.
 */
void rtty_set_mark_space_hz(float mark_hz, float space_hz);
float rtty_get_mark_hz(void);
float rtty_get_space_hz(void);

/*
 * Shift (Hz separation between mark/space) as an independent
 * parameter - see rtty_set_shift_hz()'s comment in rtty.c. Preserves
 * the center frequency and current mark/space polarity; only changes
 * the distance between them. Recomputes the Goertzel coefficients
 * immediately, same as rtty_set_mark_space_hz().
 */
void rtty_set_shift_hz(float shift_hz);
float rtty_get_shift_hz(void);

/*
 * Baud rate (bit period) as a runtime-adjustable parameter - added
 * 09/08/2026, per the project owner, alongside the DIG page's BAUD
 * tile (see main.c's menu_tile_rtty_baud_callback()). Previously
 * fixed at compile time via config.h's CONFIG_RTTY_BAUD; this cycles
 * through the common ham/commercial rates (45.45/50/75/100) without a
 * rebuild, same "config.h default is a starting guess, not a
 * guarantee" reasoning as rtty_set_mark_space_hz(). Recomputes
 * s_bit_period_samples immediately and resyncs the bit-sync state
 * machine from a clean slate (same as rtty_set_enabled()'s toggle -
 * changing the bit period mid-character would otherwise corrupt
 * whatever frame was in flight).
 */
void rtty_set_baud(float baud);
float rtty_get_baud(void);

/*
 * Station NORMAL/REVERSE convention - added 09/08/2026, per the
 * project owner, after a field finding with a real DDK9 transmission:
 * this is a DIFFERENT axis from the USB/LSB sideband mirror that
 * RTTY_VARIANT_NORMAL/INVERTED (see main.c's k_demod_modes[]) already
 * handles - that one exists because USB and LSB are frequency mirror
 * images of each other for the same pair of RF tones, a receiver-side
 * fact. This one is the TRANSMITTING station's own choice of which
 * tone means mark vs space, a convention some stations run "reversed"
 * from the near-universal default independently of which sideband
 * you're listening on. rtty_set_station_inverted() swaps the CURRENT
 * mark_hz/space_hz live (preserving center + shift, same as
 * rtty_set_shift_hz()) and remembers the flag so a later mode switch
 * (which sets a fresh absolute mark/space pair for the new sideband)
 * can reapply it - see rtty_reapply_station_inversion().
 */
void rtty_set_station_inverted(uint8_t inverted);
uint8_t rtty_get_station_inverted(void);

/*
 * Re-applies the CURRENT station inversion flag (see
 * rtty_set_station_inverted() above) on top of whatever mark/space
 * pair was just set - called from main.c's menu_mode_preset_callback()
 * right after it sets the RTTY_VARIANT_NORMAL/INVERTED base pair for a
 * newly-selected mode, so switching e.g. RTTY-L -> RTTY-U doesn't
 * silently drop an active REVERSE setting back to NORMAL. Does
 * nothing to the flag itself, only to mark_hz/space_hz.
 */
void rtty_reapply_station_inversion(void);

/*
 * LA INVERSION SE DETECTA SOLA. Ver el comentario de rtty_sombra_t en
 * rtty.c: mientras se decodifica, se lleva la cuenta de cuantos marcos
 * cerrarian bien con la polaridad CONTRARIA, y si la de ahora va mal y la
 * otra la triplica, la radio se da la vuelta ella misma. No hay que tocar
 * nada ni darle al boton dos veces.
 *
 * Esto devuelve cuantas veces lo ha hecho desde que se encendio el modo:
 * cero y texto limpio es lo normal; uno y texto limpio es que acertó.
 */
uint16_t rtty_auto_inv_veces(void);

/*
 * Salud del decodificador: que % de los caracteres de la ultima ventana
 * cerraron con su bit de parada donde tocaba. 100 es texto limpio; por
 * debajo de 85 algo esta mal puesto. Es lo que mira rtty_auto.c antes de
 * cambiar nada, para no tocarle los ajustes a una radio que va bien.
 */
uint8_t rtty_salud_pc(void);

/* Marcos cerrados desde que arranco el modo (cuenta corrida). */
uint16_t rtty_marcos_n(void);

/* Ventanas de salud cerradas: para saber si rtty_salud_pc() es nuevo. */
uint16_t rtty_salud_n(void);

#endif /* RTTY_H */
