#ifndef HFDL_PREAMBLE_SYNC_H
#define HFDL_PREAMBLE_SYNC_H

#include <stdint.h>
#include "arm_math.h"
#include "hfdl_framer.h"
#include "hfdl_preamble_seq.h"

/*
 * HFDL preamble state machine - A1_SEARCH -> A2_SEARCH -> M1_SEARCH,
 * a faithful port of dumphfdl's real framer_state handling in
 * hfdl_decoder_thread() (src/hfdl.c, szpajder/dumphfdl,
 * GPL-3.0-or-later - see hfdl_preamble_seq.h's header for the full
 * provenance note on the constants this drives). Built on top of
 * hfdl_framer.h's already self-tested generic correlator engine - one
 * instance for the A sequence (reused for both the A1 and A2 checks,
 * matching dumphfdl exactly), and 8 instances (one per M1 rotation)
 * for the M1 stage.
 *
 * SCOPE, stated explicitly: this module gets a receiver from "no
 * sync" to "confirmed A1+A2+M1 match, frame configuration known"
 * (mod arity, code rate, data segment count, deinterleaver shift -
 * see HFDL_FRAME_PARAMS[] in hfdl_preamble_seq.h). It does NOT
 * implement anything past that point - dumphfdl's FRAMER_M2_SKIP /
 * FRAMER_EQ_TRAIN / FRAMER_DATA_1 / FRAMER_DATA_2 states (M2 skip,
 * equalizer training against the T-sequences, and the actual data-
 * frame symbol demodulation) are NOT built here. Reaching
 * HFDL_PREAMBLE_LOCKED is "the preamble was found and identified",
 * not "a full frame was decoded" - see hfdl_demod_chain.h's own
 * "MISSING PIECES" note, which still applies to everything after this
 * module's LOCKED state.
 *
 * TIMING/SKIP-AHEAD behavior, ported faithfully (not simplified) from
 * dumphfdl's real `symbols_wanted` mechanism:
 *   - A1_SEARCH: correlate EVERY symbol against the A sequence
 *     (threshold HFDL_CORR_THRESHOLD_A1). On a hit, remember which
 *     phase matched (needed later for bit-level payload recovery,
 *     not used by this module itself yet) and skip ahead exactly
 *     HFDL_A_LEN-1 more symbols before the FIRST A2 check (dumphfdl:
 *     c->symbols_wanted = A_LEN right after the A1 hit).
 *   - A2_SEARCH: on that first check, correlate once. If it misses,
 *     dumphfdl's own symbols_wanted bookkeeping (see hfdl.c) ends up
 *     re-checking EVERY subsequent symbol (not skipping again) for up
 *     to HFDL_MAX_SEARCH_RETRIES misses before giving up and going
 *     back to A1_SEARCH - reproduced here the same way. A hit moves
 *     to M1_SEARCH and skips ahead HFDL_M1_LEN-1 symbols before the
 *     first M1 check, same shape as the A1->A2 transition.
 *   - M1_SEARCH: correlates all 8 rotations in parallel each checked
 *     symbol once the skip-ahead has elapsed; same
 *     HFDL_MAX_SEARCH_RETRIES-miss-then-reset-to-A1_SEARCH behavior
 *     as A2_SEARCH. A hit -> HFDL_PREAMBLE_LOCKED, with the matching
 *     rotation's index (and HFDL_FRAME_PARAMS[index]) available via
 *     hfdl_preamble_sync_get_m1_index()/get_frame_params().
 */

typedef enum {
	HFDL_PREAMBLE_A1_SEARCH = 0,
	HFDL_PREAMBLE_A2_SEARCH,
	HFDL_PREAMBLE_M1_SEARCH,
	HFDL_PREAMBLE_LOCKED
} hfdl_preamble_state_t;

typedef struct {
	hfdl_framer_t a_framer;                       /* reused for both A1 and A2 checks */
	hfdl_framer_t m1_framer[HFDL_M_SHIFT_CNT];     /* one per rotation, run in parallel during M1_SEARCH */

	hfdl_preamble_state_t state;
	uint32_t skip_remaining;   /* symbols left to skip before the next correlation check is even attempted (dumphfdl's symbols_wanted, offset by one - see .c file) */
	uint32_t search_retries;   /* misses since the last skip-ahead, within A2_SEARCH or M1_SEARCH - reset to A1_SEARCH once this hits HFDL_MAX_SEARCH_RETRIES */

	int8_t   a1_polarity;      /* +1 or -1: sign of the A1 correlation hit, i.e. which of the two BPSK phases matched - for a future bit-level stage's benefit, not used internally here */
	int32_t  locked_m1_index;  /* valid only once state == HFDL_PREAMBLE_LOCKED, index into HFDL_FRAME_PARAMS[]/HFDL_M_SHIFTS[] */
} hfdl_preamble_sync_t;

/* Configures all 9 internal hfdl_framer_t instances (1 for A, 8 for
 * M1's rotations) with the real HFDL reference sequences/thresholds
 * from hfdl_preamble_seq.h, and resets to HFDL_PREAMBLE_A1_SEARCH.
 * The reference sequence arrays themselves are built ONCE into the
 * static buffers owned by this module (see hfdl_preamble_sync.c) -
 * callers don't need to manage that. Call once at startup, or again
 * on a hard reset (e.g. hfdl_scope_burst_active()'s rising edge, same
 * spot hfdl_demod_chain_init() is already called from). */
void hfdl_preamble_sync_init(hfdl_preamble_sync_t *p);

/* Same as hfdl_preamble_sync_init() but does NOT rebuild the
 * reference sequences (already done once, they're constant) - just
 * clears state/counters and goes back to A1_SEARCH. This is the one
 * to call on every new burst; hfdl_preamble_sync_init() is really
 * only needed once at boot. */
void hfdl_preamble_sync_reset(hfdl_preamble_sync_t *p);

/* Feeds ONE Costas-corrected complex symbol (one call per symbol -
 * same rate note as hfdl_costas.h/hfdl_framer.h). Returns the state
 * AFTER processing this symbol, so a caller can simply watch for the
 * transition to HFDL_PREAMBLE_LOCKED without tracking edges itself
 * (comparing this call's return value against the previous one is
 * enough for a rising-edge check, same pattern already used in
 * demod_am.c's probe for hfdl_scope_burst_active()). */
hfdl_preamble_state_t hfdl_preamble_sync_process_symbol(hfdl_preamble_sync_t *p, float32_t i_sym, float32_t q_sym);

hfdl_preamble_state_t hfdl_preamble_sync_get_state(const hfdl_preamble_sync_t *p);

/* Valid only once get_state() == HFDL_PREAMBLE_LOCKED - index into
 * HFDL_FRAME_PARAMS[]/HFDL_M_SHIFTS[] (hfdl_preamble_seq.h). Returns
 * -1 if not currently locked. */
int32_t hfdl_preamble_sync_get_m1_index(const hfdl_preamble_sync_t *p);

/* Convenience: HFDL_FRAME_PARAMS[hfdl_preamble_sync_get_m1_index(p)],
 * only meaningful once locked - caller should check get_state() first
 * (this does NOT itself validate the locked state, just indexes the
 * table - an unlocked call reads locked_m1_index's last value, which
 * could be -1's wraparound into garbage, so DON'T call this before
 * checking get_state() == HFDL_PREAMBLE_LOCKED). */
const hfdl_frame_params_t *hfdl_preamble_sync_get_frame_params(const hfdl_preamble_sync_t *p);

/* DIAGNOSTIC (added 15/08/2026, for the real-signal A2_SEARCH miss
 * investigation): last signed correlation score computed by the
 * shared A-sequence framer (p->a_framer), i.e. hfdl_framer_get_last_
 * score() passed through. Only updates on an actual correlation
 * check (hfdl_framer_process_symbol()) - it holds its previous value
 * (from the A1 hit) during the HFDL_A_LEN-1-symbol skip-ahead, since
 * hfdl_framer_push_only() doesn't touch last_score. Meaningful to log
 * only once state == HFDL_PREAMBLE_A2_SEARCH AND skip_remaining has
 * reached 0 (i.e. right when a real A2 check just ran) - the caller
 * doesn't have skip_remaining visibility, so in practice: log this on
 * every symbol while state == A2_SEARCH and treat repeated identical
 * values as "still skipping", a changed value as "a check just ran". */
float32_t hfdl_preamble_sync_get_a_score(const hfdl_preamble_sync_t *p);

/* DIAGNOSTIC (15/08/2026) - see hfdl_framer_peek_score()'s comment.
 * Unlike get_a_score() above (which only updates on an actual
 * correlation check and holds stale during the skip-ahead), this
 * recomputes the score for the CURRENT ring content every call - safe
 * to log every symbol through the whole A2_SEARCH window, skip phase
 * included, to see exactly where the real correlation peak sits
 * relative to where process_symbol() actually checks. */
float32_t hfdl_preamble_sync_peek_a_score(const hfdl_preamble_sync_t *p);

/* Self-test: builds a SYNTHETIC symbol stream directly from the REAL
 * HFDL A/M1 reference sequences (hfdl_preamble_seq.h - these ARE the
 * real constants, unlike hfdl_framer_selftest()'s deliberately-fake
 * pattern) - noise, then the A sequence (=A1), then the A sequence
 * again right after (=A2, fed as one HFDL_A_LEN-symbol block -
 * process_symbol()'s internal skip-ahead consumes its first
 * HFDL_A_LEN-1 symbols without checking, exactly the way the real
 * skip window would be filled by the real A2 occurrence arriving over
 * the air, not by unrelated filler), then one specific M1 rotation
 * the same way. Checks the state machine reaches
 * HFDL_PREAMBLE_LOCKED with the CORRECT matching M1 index. This
 * exercises the real protocol
 * constants and the real skip-ahead/retry timing logic end-to-end,
 * but still only against a clean synthetic stream (no real matched
 * filter/AGC/noise/multipath) - it proves "the state machine and the
 * real reference sequences are wired together correctly", not that
 * this locks against an actual over-the-air HFDL signal (that needs
 * real hardware or the sigidwiki.com samples, same caveat as every
 * other self-test in this chain). */
uint8_t hfdl_preamble_sync_selftest(void);

#endif /* HFDL_PREAMBLE_SYNC_H */
