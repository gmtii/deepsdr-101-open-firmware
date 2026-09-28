#ifndef HFDL_MATCHED_FILTER_H
#define HFDL_MATCHED_FILTER_H

#include <stdint.h>
#include "arm_math.h"

/*
 * HFDL matched filter - second DSP block of the demod chain, feeding
 * from hfdl_iq_mixer.c's baseband-shifted I/Q output. Wraps CMSIS-DSP's
 * arm_fir_f32 (already linked into this project for the USB/LSB
 * Hilbert transformer - see demod_am.c - so this costs nothing extra
 * to link), applied independently to the I and Q channels.
 *
 * Coefficients: 19 taps, literal values extracted directly from
 * dumphfdl's src/hfdl.c (szpajder/dumphfdl, GPL-3.0-or-later) -
 * hfdl_matched_filter[] there. Confirmed symmetric (linear phase, as
 * a real matched filter should be) as a sanity check on the
 * transcription - see hfdl_matched_filter_selftest() below, which
 * checks this structurally rather than just trusting the literal
 * values were typed correctly.
 *
 * HFDL_MF_SYMBOL_DELAY (3 symbols) is dumphfdl's own stated group
 * delay for this filter - relevant later once symbol timing recovery
 * needs to know how many samples of latency this stage introduces,
 * not used internally by this module itself.
 */

#define HFDL_MF_TAPS_CNT 19
#define HFDL_MF_SYMBOL_DELAY 3 /* symbols of group delay this filter introduces - informational, see above */

/* Generous upper bound on the block size this will ever be called
 * with - current pipeline value (see demod_am.c's DEC_BLOCK_SAMPLES,
 * SDR_RX_BLOCK_SAMPLES(256)/DECIM_FACTOR(8) as of this writing) is
 * 32; this is doubled for headroom against that changing again (it
 * already has, more than once, per demod_am.c's own comments) without
 * this module needing a rewrite - just make sure block_size passed to
 * hfdl_matched_filter_process() never exceeds this. */
#define HFDL_MF_MAX_BLOCK_SIZE 64

extern const float32_t hfdl_matched_filter_coeffs[HFDL_MF_TAPS_CNT];

typedef struct {
	arm_fir_instance_f32 fir_i;
	arm_fir_instance_f32 fir_q;
	float32_t state_i[HFDL_MF_TAPS_CNT + HFDL_MF_MAX_BLOCK_SIZE - 1];
	float32_t state_q[HFDL_MF_TAPS_CNT + HFDL_MF_MAX_BLOCK_SIZE - 1];
} hfdl_matched_filter_t;

/* Initializes both I and Q FIR instances with hfdl_matched_filter_coeffs
 * and clears filter state (call once, or again to reset history e.g.
 * at the start of a new HFDL burst). */
void hfdl_matched_filter_init(hfdl_matched_filter_t *f);

/* Filters `block_size` samples of i_in/q_in (block_size must be <=
 * HFDL_MF_MAX_BLOCK_SIZE - not checked at runtime, matching
 * CMSIS-DSP's own convention of trusting the caller, see arm_fir_f32's
 * own documentation) into i_out/q_out. Safe to call with i_out==i_in
 * and q_out==q_in (arm_fir_f32 supports in-place operation). */
void hfdl_matched_filter_process(hfdl_matched_filter_t *f, const float32_t *i_in, const float32_t *q_in,
		float32_t *i_out, float32_t *q_out, uint32_t block_size);

/* Runs the module's standalone self-test:
 *   1) Structural check that hfdl_matched_filter_coeffs[] is exactly
 *      symmetric (linear phase) - catches a transcription typo that
 *      Part 2 alone might not (a typo could still pass an impulse
 *      response check against itself, since Part 2 just confirms the
 *      filter reproduces whatever coefficients are actually stored,
 *      not that those coefficients are individually correct).
 *   2) Feeds a unit impulse through the I channel (Q held at zero
 *      throughout) and checks the output reproduces
 *      hfdl_matched_filter_coeffs[] exactly (the standard FIR identity:
 *      the impulse response of an FIR filter equals its coefficients)
 *      - and that the Q channel, having never been fed anything but
 *      zero, stays exactly zero throughout (channels are independent).
 * Zero dependency on real hardware or a real HFDL signal - like
 * hfdl_iq_mixer_selftest(), this only proves the filter mechanics are
 * correct, not that these are definitely the right 19 coefficients
 * for real on-air HFDL (that traces back to dumphfdl's own source,
 * already cross-checked there - see this module's header comment). */
uint8_t hfdl_matched_filter_selftest(void);

#endif /* HFDL_MATCHED_FILTER_H */
