#include <string.h>
#include "hfdl_preamble_sync.h"

/* Reference sequences, built ONCE (they're constant) and cached here
 * rather than in hfdl_preamble_sync_t itself - keeps the struct
 * smaller when a caller only needs one instance's worth of framer
 * state, since all instances would share identical reference data
 * anyway (no reason to duplicate 127+8*127 floats per instance). */
static float32_t s_a_ref[HFDL_A_LEN];
static float32_t s_m1_ref[HFDL_M_SHIFT_CNT][HFDL_M1_LEN];
static uint8_t s_refs_built = 0u;

static void build_refs_once(void)
{
	if (s_refs_built) {
		return;
	}
	hfdl_preamble_seq_build_a(s_a_ref);
	for (uint32_t k = 0; k < HFDL_M_SHIFT_CNT; k++) {
		hfdl_preamble_seq_build_m1_variant(k, s_m1_ref[k]);
	}
	s_refs_built = 1u;
}

void hfdl_preamble_sync_init(hfdl_preamble_sync_t *p)
{
	build_refs_once();

	/* NON-COHERENT BLOCK COMBINING (16/08/2026, round 5 - see
	 * hfdl_framer.h's top comment for the full reasoning): a single
	 * 127-symbol coherent correlation requires phase consistency
	 * across the whole window, and every seeding/loop-tuning approach
	 * tried against real captures so far capped scores around
	 * ~0.35-0.40 regardless. HFDL_FRAMER_N_BLOCKS splits that window
	 * into this many independently-correlated, non-coherently-combined
	 * sub-blocks instead - trades some coherent processing gain for
	 * tolerance to phase drift across the window.
	 *
	 * VALUE (16/08/2026, updated after the project's first real
	 * LOCKED): 4 was the original, unverified first guess - it DID
	 * produce that first LOCKED, but a single field data point can't
	 * tell "4 is good" apart from "4 happened to be good enough that
	 * one time". Properly tuned via host_test_harness/
	 * n_blocks_sweep.c - a Monte Carlo sweep across (n_blocks, phase
	 * drift rate, SNR), CRITICALLY including a false-alarm control
	 * (pure noise, no real signal) alongside detection probability.
	 * That control matters a lot: naive detection-probability alone
	 * kept climbing all the way to n_blocks=63, but false-alarm rate
	 * climbs even faster past n_blocks~12 (very short blocks - e.g.
	 * 2-symbol blocks at n_blocks=63 - can "match" a reference by pure
	 * chance, and this module's per-block |score| never goes negative,
	 * so noise alone gets systematically biased toward a high score as
	 * blocks shrink) - n_blocks=16 already false-alarms on PURE NOISE
	 * ~55-60% of the time, making its apparent "detection" gain
	 * meaningless. Within the range where false-alarm stays low
	 * (<=~6%, since a false HFDL_FRAMER_PREAMBLE_FOUND is only
	 * tentative - still needs A2 confirmation, so it's not free but
	 * isn't catastrophic either), n_blocks=8 gave the best detection
	 * probability of the candidates tested (1,2,4,6,8), at every phase
	 * drift rate and both SNR levels checked - e.g. at -9dB SNR,
	 * ramp=0.02 rad/symbol: n_blocks=4 detects 12% of the time,
	 * n_blocks=8 detects 28%, both with false-alarm well under 10%.
	 * Re-run that sweep tool (block_len = 127/n_blocks = 15 symbols at
	 * n_blocks=8) if real captures suggest a different operating point
	 * than what this synthetic model assumed - it does NOT know the
	 * true relationship between hfdl_scope's broadband signal/floor
	 * ratio and per-symbol coherent SNR, see the sweep tool's own
	 * top comment. */
#define HFDL_FRAMER_N_BLOCKS 8u

	hfdl_framer_init(&p->a_framer, s_a_ref, HFDL_A_LEN, HFDL_FRAMER_N_BLOCKS, HFDL_CORR_THRESHOLD_A1, HFDL_CORR_THRESHOLD_A1);
	for (uint32_t k = 0; k < HFDL_M_SHIFT_CNT; k++) {
		hfdl_framer_init(&p->m1_framer[k], s_m1_ref[k], HFDL_M1_LEN, HFDL_FRAMER_N_BLOCKS, HFDL_CORR_THRESHOLD_M1, HFDL_CORR_THRESHOLD_M1);
	}

	hfdl_preamble_sync_reset(p);
}

void hfdl_preamble_sync_reset(hfdl_preamble_sync_t *p)
{
	hfdl_framer_reset(&p->a_framer);
	p->a_framer.on_thresh = HFDL_CORR_THRESHOLD_A1; /* A2_SEARCH may have lowered this - restore for the next A1_SEARCH */
	for (uint32_t k = 0; k < HFDL_M_SHIFT_CNT; k++) {
		hfdl_framer_reset(&p->m1_framer[k]);
	}
	p->state = HFDL_PREAMBLE_A1_SEARCH;
	p->skip_remaining = 0u;
	p->search_retries = 0u;
	p->a1_polarity = 0;
	p->locked_m1_index = -1;
}

hfdl_preamble_state_t hfdl_preamble_sync_process_symbol(hfdl_preamble_sync_t *p, float32_t i_sym, float32_t q_sym)
{
	switch (p->state) {

	case HFDL_PREAMBLE_A1_SEARCH: {
		uint8_t hit = hfdl_framer_process_symbol(&p->a_framer, i_sym, q_sym);
		if (hit) {
			float32_t score = hfdl_framer_get_last_score(&p->a_framer);
			p->a1_polarity = (score >= 0.0f) ? 1 : -1;
			p->a_framer.on_thresh = HFDL_CORR_THRESHOLD_A2; /* looser threshold for the A2 confirmation check - see hfdl_preamble_seq.h */
			hfdl_framer_rearm(&p->a_framer); /* re-arm WITHOUT wiping the ring - same running bit history, look for a second A occurrence in it */
			p->state = HFDL_PREAMBLE_A2_SEARCH;
			p->skip_remaining = HFDL_A_LEN - 1u; /* dumphfdl: symbols_wanted = A_LEN right after the A1 hit */
			p->search_retries = 0u;
		}
		break;
	}

	case HFDL_PREAMBLE_A2_SEARCH: {
		if (p->skip_remaining > 0u) {
			hfdl_framer_push_only(&p->a_framer, i_sym, q_sym); /* keep the sliding register current while skipping ahead */
			p->skip_remaining--;
		} else {
			uint8_t hit = hfdl_framer_process_symbol(&p->a_framer, i_sym, q_sym);
			if (hit) {
				for (uint32_t k = 0; k < HFDL_M_SHIFT_CNT; k++) {
					hfdl_framer_reset(&p->m1_framer[k]); /* M1 stage starts a genuinely fresh window - dumphfdl's c->bits register is also effectively restarted here since it needs M1_LEN NEW bits before the M1 check is meaningful */
				}
				p->state = HFDL_PREAMBLE_M1_SEARCH;
				p->skip_remaining = HFDL_M1_LEN - 1u; /* dumphfdl: symbols_wanted = M1_LEN right after the A2 hit */
				p->search_retries = 0u;
			} else {
				p->search_retries++;
				if (p->search_retries >= HFDL_MAX_SEARCH_RETRIES) {
					hfdl_preamble_sync_reset(p); /* dumphfdl: framer_reset(c) - give up, back to A1_SEARCH */
				}
				/* else: skip_remaining is already 0, so every subsequent symbol re-checks immediately - matches dumphfdl's real post-first-miss behavior (see hfdl_preamble_sync.h's header) */
			}
		}
		break;
	}

	case HFDL_PREAMBLE_M1_SEARCH: {
		if (p->skip_remaining > 0u) {
			for (uint32_t k = 0; k < HFDL_M_SHIFT_CNT; k++) {
				hfdl_framer_push_only(&p->m1_framer[k], i_sym, q_sym);
			}
			p->skip_remaining--;
		} else {
			int32_t matched = -1;
			for (uint32_t k = 0; k < HFDL_M_SHIFT_CNT; k++) {
				uint8_t hit = hfdl_framer_process_symbol(&p->m1_framer[k], i_sym, q_sym);
				if (hit && matched < 0) {
					matched = (int32_t)k; /* first match wins - see hfdl_preamble_sync.h, real match_sequence() picks the best of the 8, this picks the first crossing threshold, a reasonable simplification at these thresholds */
				}
			}
			if (matched >= 0) {
				p->locked_m1_index = matched;
				p->state = HFDL_PREAMBLE_LOCKED;
			} else {
				p->search_retries++;
				if (p->search_retries >= HFDL_MAX_SEARCH_RETRIES) {
					hfdl_preamble_sync_reset(p); /* dumphfdl: framer_reset(c) */
				}
			}
		}
		break;
	}

	case HFDL_PREAMBLE_LOCKED:
	default:
		/* Nothing to do - caller drives the next hfdl_preamble_sync_reset() once this
		 * frame's preamble has been consumed (see hfdl_preamble_sync.h). */
		break;
	}

	return p->state;
}

hfdl_preamble_state_t hfdl_preamble_sync_get_state(const hfdl_preamble_sync_t *p)
{
	return p->state;
}

int32_t hfdl_preamble_sync_get_m1_index(const hfdl_preamble_sync_t *p)
{
	return (p->state == HFDL_PREAMBLE_LOCKED) ? p->locked_m1_index : -1;
}

const hfdl_frame_params_t *hfdl_preamble_sync_get_frame_params(const hfdl_preamble_sync_t *p)
{
	return &HFDL_FRAME_PARAMS[p->locked_m1_index];
}

float32_t hfdl_preamble_sync_get_a_score(const hfdl_preamble_sync_t *p)
{
	return hfdl_framer_get_last_score(&p->a_framer);
}

float32_t hfdl_preamble_sync_peek_a_score(const hfdl_preamble_sync_t *p)
{
	return hfdl_framer_peek_score(&p->a_framer);
}

/* ==========================================================================
 * Self-test - see hfdl_preamble_sync.h's header: real A/M1 reference
 * sequences, synthetic clean stream (no real DSP chain in front of it).
 * ========================================================================== */

static uint32_t xorshift32(uint32_t *state)
{
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

uint8_t hfdl_preamble_sync_selftest(void)
{
	build_refs_once();

	const uint32_t TEST_M1_INDEX = 3u; /* arbitrary pick (1800bps single slot) - exercises a non-zero index, not just index 0 */

	hfdl_preamble_sync_t p;
	hfdl_preamble_sync_init(&p);

	uint32_t seed = 0xDEADBEEFu;
	hfdl_preamble_state_t last_state = HFDL_PREAMBLE_A1_SEARCH;

	#define FEED_NOISE(n) \
		for (uint32_t i = 0; i < (n); i++) { \
			float32_t b = (xorshift32(&seed) & 1u) ? 1.0f : -1.0f; \
			last_state = hfdl_preamble_sync_process_symbol(&p, b, 0.0f); \
		}
	#define FEED_SEQ(arr, n) \
		for (uint32_t i = 0; i < (n); i++) { \
			last_state = hfdl_preamble_sync_process_symbol(&p, (arr)[i], 0.0f); \
		}

	FEED_NOISE(300u); /* pre-preamble noise/silence */
	if (last_state != HFDL_PREAMBLE_A1_SEARCH) {
		return 0u; /* must NOT have false-locked on noise */
	}

	FEED_SEQ(s_a_ref, HFDL_A_LEN); /* A1 occurrence: last symbol completes the window and triggers the hit */
	if (last_state != HFDL_PREAMBLE_A2_SEARCH) {
		return 0u;
	}

	/* A2's occurrence, fed as a single contiguous HFDL_A_LEN-symbol
	 * block, same as the real signal would present it: the first
	 * HFDL_A_LEN-1 of these are consumed by process_symbol()'s
	 * internal skip-ahead (pushed into the ring, not checked - see
	 * HFDL_PREAMBLE_A2_SEARCH's skip_remaining handling), and the
	 * LAST one is the symbol where skip_remaining reaches 0 and the
	 * correlation check actually runs - which is exactly when the
	 * ring's last HFDL_A_LEN entries equal the full A pattern. */
	FEED_SEQ(s_a_ref, HFDL_A_LEN);
	if (last_state != HFDL_PREAMBLE_M1_SEARCH) {
		return 0u; /* A2 should have been confirmed */
	}

	/* Same shape for the M1 occurrence - HFDL_M1_LEN symbols, first
	 * HFDL_M1_LEN-1 skipped-but-pushed, last one triggers the check. */
	FEED_SEQ(s_m1_ref[TEST_M1_INDEX], HFDL_M1_LEN);
	if (last_state != HFDL_PREAMBLE_LOCKED) {
		return 0u; /* M1 should have matched */
	}
	if (hfdl_preamble_sync_get_m1_index(&p) != (int32_t)TEST_M1_INDEX) {
		return 0u; /* must identify the CORRECT rotation, not just any */
	}
	const hfdl_frame_params_t *fp = hfdl_preamble_sync_get_frame_params(&p);
	if (fp->mod_arity != HFDL_FRAME_PARAMS[TEST_M1_INDEX].mod_arity) {
		return 0u;
	}

	/* Reset and confirm a fresh search doesn't spuriously "remember"
	 * the previous lock. */
	hfdl_preamble_sync_reset(&p);
	if (hfdl_preamble_sync_get_state(&p) != HFDL_PREAMBLE_A1_SEARCH) {
		return 0u;
	}
	if (hfdl_preamble_sync_get_m1_index(&p) != -1) {
		return 0u;
	}

	#undef FEED_NOISE
	#undef FEED_SEQ

	return 1u;
}
