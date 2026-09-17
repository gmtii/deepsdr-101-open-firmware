#include <string.h>
#include <math.h>
#include "hfdl_framer.h"

void hfdl_framer_init(hfdl_framer_t *f, const float32_t *ref_bits, uint32_t ref_len,
		uint32_t n_blocks, float32_t on_thresh, float32_t off_thresh)
{
	if (ref_len > HFDL_FRAMER_MAX_REF_LEN) {
		ref_len = HFDL_FRAMER_MAX_REF_LEN; /* see header - a clamp here means a config bug upstream */
	}
	if (n_blocks < 1u) {
		n_blocks = 1u; /* see header - n_blocks==1 is the original single-coherent-block behavior */
	}
	if (n_blocks > ref_len) {
		n_blocks = ref_len; /* degenerate but well-defined: every block is exactly 1 symbol long */
	}
	f->ref_bits = ref_bits;
	f->ref_len = ref_len;
	f->n_blocks = n_blocks;
	f->on_thresh = on_thresh;
	f->off_thresh = off_thresh;
	hfdl_framer_reset(f);
}

void hfdl_framer_reset(hfdl_framer_t *f)
{
	memset(f->ring, 0, sizeof(f->ring));
	f->ring_head = 0u;
	f->n_pushed = 0u;
	f->state = HFDL_FRAMER_SEARCHING;
	f->last_score = 0.0f;
	f->symbols_since_reset = 0u;
}

/* Correlates exactly `len` consecutive ring[] entries starting at
 * ring index `ring_start` against `ref` (ref_bits + block_offset,
 * i.e. the matching slice of the full reference for this block),
 * RMS-normalized (see hfdl_framer_process_symbol()'s round-2 comment
 * on why), resolving the local +-ref sign ambiguity. Internal helper
 * - both the original single-block path AND the new (16/08/2026)
 * multi-block non-coherent combining call this, so the RMS
 * normalization and sign resolution logic lives in exactly one place
 * instead of two near-duplicate copies. Returns 0.0f for a
 * near-silent block (see the rms guard below) rather than blowing up
 * dividing by ~0. */
static float32_t framer_correlate_block(const hfdl_framer_t *f, uint32_t ring_start,
		const float32_t *ref, uint32_t len)
{
	/* HARD-DECISION bit-agreement correlation (21/08/2026, ring
	 * storage simplified to match same day). Matches dumphfdl's real
	 * algorithm exactly (src/hfdl.c: corr = 2*bsequence_correlate(
	 * ref_bits, c->bits)/A_LEN - 1, i.e. hard-slice each recovered
	 * symbol to a bit FIRST, then score by simple agreement fraction -
	 * see bsequence_correlate() in liquid-dsp). Root-caused against a
	 * real captured HFDL burst (host harness + dumphfdl cross-check,
	 * 21/08/2026): the soft dot-product version's true-signal peak
	 * (~0.43) was BELOW its own noise-floor peaks (~0.69) - no
	 * threshold could separate them, causing spurious LOCKED events on
	 * noise before the real preamble was ever reached. The hard-
	 * decision score on the exact same recovered symbols reached 0.953
	 * at the true burst against a 0.07-0.21 noise floor - clean
	 * separation, no further tuning needed. Mathematically:
	 * sum(sign(sample)*ref[k])/len == (matches-mismatches)/len ==
	 * 2*match/len - 1, i.e. exactly dumphfdl's formula, since ref[k]
	 * is already +-1.0f.
	 *
	 * ring[] itself now stores that sign directly (int8_t, +1/-1 -
	 * see hfdl_framer.h's own history comment on why), so there's
	 * nothing left to hard-slice here - just read it straight. The
	 * old RMS "is this window silence" guard is gone too: it read
	 * ring[]*ring[] to estimate amplitude, but a ring of pure +-1
	 * values always gives rms==1.0 by construction, so that guard
	 * could never fire once the switch to sign-only storage was
	 * made - removing it isn't a behavior change, just deleting code
	 * that had already become unreachable. */
	float32_t dot = 0.0f;
	for (uint32_t k = 0; k < len; k++) {
		uint32_t idx = (ring_start + k) % HFDL_FRAMER_MAX_REF_LEN;
		dot += (float32_t)f->ring[idx] * ref[k];
	}
	return dot / (float32_t)len;
}

/* Correlates the last f->ref_len ring entries (oldest-to-newest, i.e.
 * matching ref_bits[0..ref_len-1] in chronological order) against
 * f->ref_bits. Internal - not part of the public API, no need for a
 * NaN/zero-length guard beyond what hfdl_framer_process_symbol()
 * already ensures (n_pushed >= ref_len before this is ever called,
 * and ref_len > 0 is the only sane way hfdl_framer_init() gets used).
 *
 * AMPLITUDE NORMALIZATION (16/08/2026, round 2 of the soft-decision
 * change - see hfdl_framer_process_symbol()'s comment for round 1) -
 * real hardware capture showed scores collapsing to ~0.10-0.16 even
 * on the strongest, longest bursts in the whole project so far (ratio
 * 10-12x, one burst 67356 symbols long, still never got a single A1
 * hit against the unchanged 0.30 threshold) - worse than before the
 * soft-decision switch, not better. Root cause: switching ring[] from
 * hard +-1.0f to the raw analog Costas-derotated value (see that
 * comment) meant the dot product is scaled by whatever amplitude AGC
 * actually settles on, which apparently sits well under 1.0 in
 * practice - so score's old "1.0 at a perfect match" property
 * silently became "whatever AGC's amplitude times 1.0 at a perfect
 * match", quietly invalidating HFDL_CORR_THRESHOLD_A1/A2/M1 (still
 * 0.30f, tuned for the OLD hard-decision scale) without changing
 * them. FIX: measure the RMS amplitude of the same window being
 * correlated (framer_correlate_block(), above) and divide by that
 * instead of just the window length - restores "ref_bits are exactly
 * +-1, so a perfect match's dot product should read 1.0" REGARDLESS
 * of whatever gain the front end/AGC happens to be running.
 *
 * NON-COHERENT BLOCK COMBINING (16/08/2026, round 5 - see this file's
 * top-of-header comment for the full reasoning): with f->n_blocks==1
 * this is EXACTLY the original behavior (one coherent block spanning
 * the whole window, signed score, +-ref sign resolved once for the
 * whole thing). With f->n_blocks>1, ref_len is split into n_blocks
 * equal sub-blocks of block_len = ref_len/n_blocks symbols each
 * (ref_len % n_blocks leftover trailing symbols in the window are
 * simply not used by any block - see header, 127 is prime so this is
 * unavoidable for any n_blocks>1 that isn't 127 itself), each
 * correlated independently (own local sign resolution - a block is
 * free to match +ref_block OR -ref_block, whichever fits its own
 * local carrier phase, since nothing here assumes phase consistency
 * ACROSS blocks anymore), and the block magnitudes averaged. */
static float32_t framer_correlate(const hfdl_framer_t *f)
{
	/* Oldest sample among the last ref_len pushed sits at
	 * ring_head - ref_len (mod HFDL_FRAMER_MAX_REF_LEN, careful
	 * with unsigned wraparound). */
	uint32_t start = (f->ring_head + HFDL_FRAMER_MAX_REF_LEN - f->ref_len) % HFDL_FRAMER_MAX_REF_LEN;

	if (f->n_blocks <= 1u) {
		/* Original single-coherent-block path, byte-for-byte the
		 * pre-16/08/2026-round-5 behavior: SIGNED score (caller
		 * checks |score| against on_thresh), sign tells which phase
		 * (+ref vs -ref) matched - see header. */
		float32_t plus = framer_correlate_block(f, start, f->ref_bits, f->ref_len);
		return plus; /* framer_correlate_block() already picks whichever of the ring's actual
		               * values best fits +ref via its own dot product sign - no separate
		               * against-negated-ref pass needed, the dot product itself is already
		               * signed and framer_correlate_block() doesn't fabs() it. */
	}

	uint32_t block_len = f->ref_len / f->n_blocks;
	float32_t mag_sum = 0.0f;
	for (uint32_t b = 0; b < f->n_blocks; b++) {
		uint32_t block_ring_start = (start + b * block_len) % HFDL_FRAMER_MAX_REF_LEN;
		float32_t block_score = framer_correlate_block(f, block_ring_start,
				f->ref_bits + (b * block_len), block_len);
		mag_sum += fabsf(block_score);
	}
	return mag_sum / (float32_t)f->n_blocks; /* always >= 0.0f - see struct's last_score comment */
}

uint8_t hfdl_framer_process_symbol(hfdl_framer_t *f, float32_t i_sym, float32_t q_sym)
{
	(void)q_sym; /* accepted for API symmetry only - see header. Still not used even after the
	               * 16/08/2026 soft-decision change: HFDL's preamble is BPSK, and this module's
	               * whole 180-degree-ambiguity handling (framer_correlate() checking both +ref
	               * and -ref) assumes the information lives entirely in i_sym once Costas has
	               * derotated onto the real axis - q_sym close to 0 at a good lock is the
	               * EXPECTED/desired case, not a source of extra correlation signal to exploit,
	               * so there's still no reason to fold it in here. */

	/* CHANGED (16/08/2026, A2-score-ceiling investigation - see this
	 * file's top comment and hfdl_costas.h's coarse-frequency-
	 * estimator comment for the full mechanism this fixes): push the
	 * SOFT (analog, Costas-derotated) i_sym straight into the ring,
	 * not a hard sign(i_sym) decision. [...]
	 *
	 * CHANGED BACK (21/08/2026): framer_correlate_block() itself
	 * moved to hard-decision agreement scoring the same day (see its
	 * own comment) after that turned out to be the real fix needed
	 * against actual captured hardware data - once the correlator
	 * hard-slices at READ time regardless, storing the full analog
	 * value here was pure waste (ring[] is now int8_t, not
	 * float32_t - see hfdl_framer.h's own history comment). */
	f->ring[f->ring_head] = (i_sym >= 0.0f) ? (int8_t)1 : (int8_t)-1;
	f->ring_head = (f->ring_head + 1u) % HFDL_FRAMER_MAX_REF_LEN;
	if (f->n_pushed < HFDL_FRAMER_MAX_REF_LEN) {
		f->n_pushed++;
	}
	f->symbols_since_reset++;

	if (f->ref_len == 0u || f->n_pushed < f->ref_len) {
		return 0u; /* not enough history yet to correlate a full reference length */
	}

	float32_t score = framer_correlate(f);
	f->last_score = score;

	uint8_t abs_over_on = (score >= f->on_thresh) || (-score >= f->on_thresh);

	uint8_t just_found = 0u;
	if (abs_over_on && (f->state == HFDL_FRAMER_SEARCHING)) {
		f->state = HFDL_FRAMER_PREAMBLE_FOUND;
		just_found = 1u;
	}
	return just_found;
}

void hfdl_framer_push_only(hfdl_framer_t *f, float32_t i_sym, float32_t q_sym)
{
	(void)q_sym; /* see hfdl_framer_process_symbol()'s comment - still unused post-16/08/2026 */
	f->ring[f->ring_head] = (i_sym >= 0.0f) ? (int8_t)1 : (int8_t)-1; /* CHANGED (21/08/2026) - hard sign, see hfdl_framer.h's history comment */
	f->ring_head = (f->ring_head + 1u) % HFDL_FRAMER_MAX_REF_LEN;
	if (f->n_pushed < HFDL_FRAMER_MAX_REF_LEN) {
		f->n_pushed++;
	}
	f->symbols_since_reset++;
}

void hfdl_framer_rearm(hfdl_framer_t *f)
{
	f->state = HFDL_FRAMER_SEARCHING;
}

void hfdl_framer_confirm_lock(hfdl_framer_t *f)
{
	if (f->state == HFDL_FRAMER_PREAMBLE_FOUND) {
		f->state = HFDL_FRAMER_LOCKED;
	}
}

hfdl_framer_state_t hfdl_framer_get_state(const hfdl_framer_t *f)
{
	return f->state;
}

float32_t hfdl_framer_get_last_score(const hfdl_framer_t *f)
{
	return f->last_score;
}

float32_t hfdl_framer_get_global_polarity(const hfdl_framer_t *f)
{
	/* Same "oldest sample among the last ref_len pushed" index math
	 * framer_correlate() itself uses - see that function's comment. */
	uint32_t start = (f->ring_head + HFDL_FRAMER_MAX_REF_LEN - f->ref_len) % HFDL_FRAMER_MAX_REF_LEN;
	return framer_correlate_block(f, start, f->ref_bits, f->ref_len); /* n_blocks=1 style single
	                                                                      coherent block, see this
	                                                                      function's header comment */
}

/* DIAGNOSTIC (15/08/2026, A2 peak-location investigation): computes
 * the SAME correlation framer_correlate() does, over whatever's
 * currently in the ring, WITHOUT any of process_symbol()'s side
 * effects (no state transition, doesn't touch last_score). Safe to
 * call every symbol regardless of push_only()/process_symbol() being
 * used that same symbol - purely a read. Returns 0.0f if there isn't
 * a full ref_len window yet (same guard as process_symbol()). */
float32_t hfdl_framer_peek_score(const hfdl_framer_t *f)
{
	if (f->ref_len == 0u || f->n_pushed < f->ref_len) {
		return 0.0f;
	}
	return framer_correlate(f);
}

/* ==========================================================================
 * Self-test - SYNTHETIC pattern only, see hfdl_framer.h's header for why.
 * ========================================================================== */

#define SELFTEST_REF_LEN 63u /* arbitrary, well under HFDL_FRAMER_MAX_REF_LEN - NOT HFDL's real A2/M1 length */

/* Simple deterministic PRNG (xorshift32) so the self-test is
 * reproducible across runs/platforms without relying on libc rand()
 * - same reasoning hfdl_symsync.c's own self-test documents for why
 * it avoids depending on a specific PRNG implementation's sequence. */
static uint32_t xorshift32(uint32_t *state)
{
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

uint8_t hfdl_framer_selftest(void)
{
	/* Deterministic synthetic "reference sequence" - explicitly NOT
	 * HFDL's real A2/M1 (see this file's header). Built from the
	 * PRNG itself so it's not accidentally a trivial/periodic
	 * pattern that could correlate against noise by chance. */
	static float32_t ref[SELFTEST_REF_LEN];
	uint32_t seed = 0xACE1u;
	for (uint32_t k = 0; k < SELFTEST_REF_LEN; k++) {
		ref[k] = (xorshift32(&seed) & 1u) ? 1.0f : -1.0f;
	}

	hfdl_framer_t f;
	hfdl_framer_init(&f, ref, SELFTEST_REF_LEN, 1u, 0.8f, 0.6f); /* n_blocks=1 - original single-coherent-block mode, see header */

	/* Build a stream: NOISE_PRE random bits, then the reference
	 * pattern (positive polarity), then more noise, then the
	 * reference pattern again NEGATED (tests ambiguity-resolving
	 * max/sign handling). */
	#define NOISE_PRE  200u
	#define NOISE_MID  150u
	uint32_t stream_seed = 0x1234ABCDu;

	uint8_t found_positive = 0u;
	uint32_t found_positive_at = 0u;
	uint8_t false_trigger_in_noise = 0u;

	uint32_t sym = 0u;
	for (uint32_t k = 0; k < NOISE_PRE; k++, sym++) {
		float32_t b = (xorshift32(&stream_seed) & 1u) ? 1.0f : -1.0f;
		uint8_t trig = hfdl_framer_process_symbol(&f, b, 0.0f);
		if (trig) {
			false_trigger_in_noise = 1u;
		}
	}
	for (uint32_t k = 0; k < SELFTEST_REF_LEN; k++, sym++) {
		uint8_t trig = hfdl_framer_process_symbol(&f, ref[k], 0.0f);
		if (trig) {
			found_positive = 1u;
			found_positive_at = sym;
		}
	}
	if (!found_positive) {
		return 0u; /* positive-polarity match never fired */
	}
	/* A real correlator match should land exactly at the last
	 * symbol of the embedded pattern (that's when the sliding
	 * window first fully aligns with it). */
	if (found_positive_at != (NOISE_PRE + SELFTEST_REF_LEN - 1u)) {
		return 0u;
	}

	/* Reset and repeat with the NEGATED pattern, to check the
	 * ambiguity-resolving max(|+ref|, |-ref|) path. */
	hfdl_framer_reset(&f);
	stream_seed = 0x5678EF01u;
	uint8_t found_negative = 0u;
	sym = 0u;
	for (uint32_t k = 0; k < NOISE_MID; k++, sym++) {
		float32_t b = (xorshift32(&stream_seed) & 1u) ? 1.0f : -1.0f;
		uint8_t trig = hfdl_framer_process_symbol(&f, b, 0.0f);
		if (trig) {
			false_trigger_in_noise = 1u;
		}
	}
	for (uint32_t k = 0; k < SELFTEST_REF_LEN; k++, sym++) {
		uint8_t trig = hfdl_framer_process_symbol(&f, -ref[k], 0.0f); /* NEGATED */
		if (trig) {
			found_negative = 1u;
		}
	}
	if (!found_negative) {
		return 0u; /* negated-polarity (phase-ambiguous) match never fired */
	}
	if (false_trigger_in_noise) {
		return 0u; /* random noise should not cross on_thresh with these thresholds/lengths */
	}

	/* State/getter sanity. */
	if (hfdl_framer_get_state(&f) != HFDL_FRAMER_PREAMBLE_FOUND) {
		return 0u;
	}
	hfdl_framer_confirm_lock(&f);
	if (hfdl_framer_get_state(&f) != HFDL_FRAMER_LOCKED) {
		return 0u;
	}
	hfdl_framer_reset(&f);
	if (hfdl_framer_get_state(&f) != HFDL_FRAMER_SEARCHING) {
		return 0u;
	}

	return 1u;
}

uint8_t hfdl_framer_selftest_block_combining(void)
{
	static float32_t ref[SELFTEST_REF_LEN];
	uint32_t seed = 0xBEEF1234u;
	for (uint32_t k = 0; k < SELFTEST_REF_LEN; k++) {
		ref[k] = (xorshift32(&seed) & 1u) ? 1.0f : -1.0f;
	}

	/* Phase ramp rate (16/08/2026) - chosen empirically (by literally
	 * sweeping this rate 0.005..0.06 rad/symbol while writing this
	 * test and printing both scores at each step, same "check before
	 * trusting" spirit as everything else this round) to land where
	 * the difference between n_blocks=1 and n_blocks=4 is large and
	 * unambiguous: single-block collapses to ~0.18 (a clean miss
	 * against any reasonable threshold) while multi-block still reads
	 * ~0.84 (a clean, solid match) - too small a ramp and both modes
	 * pass easily (no evidence of an advantage); too large and both
	 * degrade together (also no evidence, and noisier/less
	 * reproducible - the raw sweep showed some ramp rates near 0.03
	 * landing in an unstable crossover purely from this particular
	 * PRNG realization, so 0.045 was picked specifically for margin,
	 * not just "the first rate that happened to pass once"). */
	#define BLOCK_TEST_RAMP_RAD_PER_SYM 0.045f

	hfdl_framer_t f_single, f_multi;
	hfdl_framer_init(&f_single, ref, SELFTEST_REF_LEN, 1u, 0.30f, 0.30f); /* n_blocks=1 */
	hfdl_framer_init(&f_multi, ref, SELFTEST_REF_LEN, 4u, 0.30f, 0.30f); /* n_blocks=4 */

	for (uint32_t k = 0; k < SELFTEST_REF_LEN; k++) {
		float32_t theta = BLOCK_TEST_RAMP_RAD_PER_SYM * (float32_t)k;
		float32_t corrupted = ref[k] * cosf(theta); /* phase-drift-attenuated symbol, see comment above */
		hfdl_framer_process_symbol(&f_single, corrupted, 0.0f);
		hfdl_framer_process_symbol(&f_multi, corrupted, 0.0f);
	}

	float32_t single_score = fabsf(hfdl_framer_get_last_score(&f_single));
	float32_t multi_score = fabsf(hfdl_framer_get_last_score(&f_multi));

	/* The actual claim this round's change makes: multi-block should
	 * score MEANINGFULLY higher than single-block on a phase-drift-
	 * corrupted stream (the whole point), AND should clear a
	 * reasonable detection threshold where single-block visibly
	 * struggles - both checked explicitly rather than just eyeballing
	 * printed numbers, so this function's return value alone is
	 * trustworthy evidence. */
	if (multi_score <= single_score) {
		return 0u; /* no advantage demonstrated - the whole premise of this round's change would be unverified */
	}
	if (multi_score < 0.5f) {
		return 0u; /* multi-block should recover a solid match here, not just "less bad" than single-block */
	}

	return 1u;
}
