#ifndef HFDL_DEMOD_CHAIN_H
#define HFDL_DEMOD_CHAIN_H

#include <stdint.h>
#include "arm_math.h"
#include "hfdl_iq_mixer.h"
#include "hfdl_rational_resampler.h"
#include "hfdl_agc.h"
#include "hfdl_matched_filter.h"
#include "hfdl_symsync.h"
#include "hfdl_costas.h"

/*
 * HFDL demod chain wiring - ties together the DSP blocks built this
 * session, in the exact order confirmed against dumphfdl's real
 * per-sample processing loop (src/hfdl.c, szpajder/dumphfdl,
 * GPL-3.0-or-later - see that file's channel processing thread for
 * the reference: resampler -> AGC -> matched filter -> symsync ->
 * Costas -> equalizer):
 *
 *   hfdl_iq_mixer (ours, replaces dumphfdl's FFT channelizer/DDC -
 *                  we don't have one, so this shifts the 1440Hz
 *                  subcarrier to baseband instead)
 *     -> hfdl_rational_resampler (12000Hz -> 5400Hz, replaces
 *                  dumphfdl's arbitrary-rate msresamp - see the
 *                  project's session notes for why a FIXED 9:20
 *                  ratio resampler was chosen instead)
 *     -> hfdl_agc (11/08/2026 - normalizes amplitude before the
 *                  matched filter; NOT a verified port of liquid's
 *                  real agc_crcf, see hfdl_agc.h's own provenance
 *                  note before assuming it matches this chain's
 *                  other blocks' pedigree)
 *     -> hfdl_matched_filter (the real 19-tap HFDL matched filter)
 *     -> hfdl_symsync (timing recovery, ~1 output sample/symbol)
 *     -> hfdl_costas (BPSK carrier tracking, once per symbol)
 *     -> [equalizer - NOT BUILT THIS SESSION, see "MISSING PIECES" below]
 *     -> [bit decision / descrambler / deinterleaver / Viterbi -
 *          NOT WIRED YET - hfdl_descrambler.c and
 *          hfdl_deinterleaver.c exist and are self-tested
 *          standalone, but nothing here calls them yet; Viterbi
 *          itself hasn't been built at all]
 *
 * MISSING PIECES, called out explicitly so they aren't silently
 * forgotten:
 *   - AGC: BUILT 11/08/2026 (hfdl_agc.c, wired in between the
 *     resampler and the matched filter) - see hfdl_agc.h for what it
 *     is and, importantly, what it is NOT (not a verified port of
 *     liquid's real agc_crcf - own single-pole design, provenance
 *     documented in that module's header).
 *   - Equalizer: dumphfdl runs eqlms_cccf after Costas, trained
 *     during the preamble's dedicated T-sequence blocks (see the
 *     project's phy-layer document, section 7). Not built this
 *     session - the framer state machine that would drive "when to
 *     train" doesn't exist yet either.
 *   - Everything from bit decisions onward (descrambler ->
 *     deinterleaver -> Viterbi -> CRC -> PDU parse): the first three
 *     of those modules exist and are independently self-tested
 *     (hfdl_descrambler.c, hfdl_deinterleaver.c, hfdl_crc.c,
 *     hfdl_pdu.c) but NOTHING in this file calls them - this chain
 *     stops at Costas-corrected complex symbols, output via
 *     hfdl_demod_chain_process()'s symbols_out/n_symbols_out, for a
 *     future stage to pick up.
 *
 * BLOCK-SIZE MISMATCH: demod_am.c feeds DEC_BLOCK_SAMPLES (currently
 * 32, see hfdl_matched_filter.h's own note on this drifting over
 * time) samples per call, but hfdl_rational_resampler_process()
 * requires EXACTLY HFDL_RESAMP_DECIM (20) input samples per call -
 * 32 is not a multiple of 20. This module owns a small internal ring
 * buffer (resamp_buf_i/q) that accumulates mixed samples across
 * calls to hfdl_demod_chain_process() and only invokes the resampler
 * once 20 samples are available, carrying any remainder over to the
 * next call - so the caller does not need to know or care about this
 * mismatch.
 */

/* Worst case: every 12kHz input sample could (in principle) result in
 * resampler output once enough have accumulated - for a block of
 * HFDL_DEMOD_CHAIN_MAX_INPUT_BLOCK samples, the maximum possible
 * number of complete 20-sample resampler groups is
 * ceil(HFDL_DEMOD_CHAIN_MAX_INPUT_BLOCK / HFDL_RESAMP_DECIM) + 1 (for
 * a leftover carried in from the previous call) - each producing up
 * to HFDL_SYMSYNC_M symsync outputs, each yielding at most 1 Costas
 * symbol. Sized generously against demod_am.c's current 32-sample
 * blocks, with headroom - see hfdl_demod_chain_process()'s own bounds
 * check, which will not overflow this even if the caller's block size
 * changes, it will just produce fewer symbols per call. */
#define HFDL_DEMOD_CHAIN_MAX_INPUT_BLOCK 64
#define HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT 16

typedef struct {
	hfdl_iq_mixer_t mixer;
	hfdl_rational_resampler_t resampler;
	hfdl_agc_t agc;
	hfdl_matched_filter_t mf;
	hfdl_symsync_t symsync;
	hfdl_costas_t costas;

	/* Accumulates mixed 12kHz samples until HFDL_RESAMP_DECIM (20)
	 * are available for one resampler call - see module header's
	 * "BLOCK-SIZE MISMATCH" note. */
	float32_t resamp_buf_i[HFDL_RESAMP_DECIM];
	float32_t resamp_buf_q[HFDL_RESAMP_DECIM];
	uint32_t  resamp_buf_fill;
} hfdl_demod_chain_t;

/* Initializes every sub-block (mixer at HFDL_MIXER_SUBCARRIER_HZ /
 * 12000Hz, matched filter, symsync, Costas with its default
 * alpha/beta) and clears the resampler accumulation buffer. Call once
 * at startup, or again to fully reset state (e.g. at the start of a
 * new HFDL burst - mirrors calling reset on each sub-block). */
void hfdl_demod_chain_init(hfdl_demod_chain_t *c);

/* Processes `n_in` samples of real-valued s_i_dec/s_q_dec-style input
 * (n_in must be <= HFDL_DEMOD_CHAIN_MAX_INPUT_BLOCK) through the full
 * mixer -> resampler -> matched filter -> symsync -> Costas chain.
 * Writes up to HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT Costas-corrected
 * complex symbols to i_symbols_out/q_symbols_out and reports the
 * actual count via *n_symbols_out (can legitimately be 0 for a given
 * call, if fewer than 20 samples have accumulated yet, or several,
 * depending on timing). See module header for what's NOT included
 * (AGC, equalizer, anything past Costas).
 *
 * i_half_symbols_out/q_half_symbols_out (same indexing/capacity as
 * i_symbols_out/q_symbols_out) - the T/2 companion sample for each
 * symbol (see hfdl_symsync_step()'s own comment), added 25/08/2026
 * ("ecualizador T/2" round, pieza 1) and phase-corrected as of the
 * same round's pieza 3: Costas's phase-error computation and loop
 * filter update still run ONCE per symbol (unchanged, on the main T
 * sample only, via hfdl_costas_step_bpsk() exactly as before) - the
 * T/2 sample is derotated using that SAME loop's phase estimate as
 * carried over from the PREVIOUS symbol, via the pure
 * hfdl_costas_derotate_only() (no loop-filter update, no
 * coarse-acquisition feed) - see that function's own header comment
 * for why the T/2 sample can't go through the full decision-directed
 * step_bpsk() itself. Both outputs are therefore genuinely
 * Costas-corrected and safe to push directly into a fractionally-
 * spaced equalizer (see hfdl_equalizer.h). */
void hfdl_demod_chain_process(hfdl_demod_chain_t *c,
		const float32_t *i_in, const float32_t *q_in, uint32_t n_in,
		float32_t *i_symbols_out, float32_t *q_symbols_out,
		float32_t *i_half_symbols_out, float32_t *q_half_symbols_out,
		uint32_t *n_symbols_out);

/* Convenience accessors for the chain's internal AGC state (see
 * hfdl_agc.h) - added so a caller (e.g. demod_am.c's HFDL probe) can
 * report front-end signal level alongside symsync/Costas state
 * without reaching into hfdl_demod_chain_t's internals directly. */
float32_t hfdl_demod_chain_get_agc_power_avg(const hfdl_demod_chain_t *c);
float32_t hfdl_demod_chain_get_agc_gain(const hfdl_demod_chain_t *c);

/* Runs the module's standalone self-test: pushes a synthetic
 * BPSK-modulated-at-1440Hz-subcarrier signal (built from the same
 * real 19-tap matched filter and a fixed fractional sample-timing
 * offset used in hfdl_symsync.c's own self-test, further modulated
 * onto a 1440Hz subcarrier the way HFDL's USB signal actually
 * presents at hfdl_iq_mixer.c's input) through the ENTIRE chain in
 * caller-realistic block sizes, and checks: no NaN/divergence
 * anywhere in the chain, and that Costas-corrected symbols are
 * actually produced (not zero throughout the run). This is an
 * end-to-end PLUMBING and STABILITY check - it does NOT establish
 * that the chain achieves lock or acceptable BER against a real
 * signal (see hfdl_symsync.h/hfdl_costas.h's own self-test
 * limitations, which compound across this many cascaded blocks) -
 * that needs the sigidwiki.com samples mentioned in the project's
 * session notes, run on real hardware. */
uint8_t hfdl_demod_chain_selftest(void);

#endif /* HFDL_DEMOD_CHAIN_H */
