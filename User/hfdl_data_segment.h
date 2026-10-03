#ifndef HFDL_DATA_SEGMENT_H
#define HFDL_DATA_SEGMENT_H

#include "arm_math.h"
#include <stdint.h>

/*
 * hfdl_data_segment - post-LOCKED symbol bookkeeping (16/08/2026,
 * "paso 2" round 1 - see the project's own session history: this
 * exists because hfdl_preamble_sync.c's own SCOPE note says plainly
 * it confirms+identifies the preamble and does NOT run dumphfdl's
 * FRAMER_M2_SKIP-onward stages - this module is exactly that "onward"
 * part, ported from dumphfdl's real src/hfdl.c (the project's own
 * uploaded copy - see M2_LEN/T_LEN/DATA_FRAME_LEN/etc. below, all
 * literal constants from there, not guessed).
 *
 * SCOPE OF THIS ROUND: mirrors dumphfdl's FRAMER_M2_SKIP / EQ_TRAIN /
 * DATA_1 / DATA_2 state sequence and symbol-count bookkeeping
 * EXACTLY, so the very first thing this can prove is "does our
 * symbol counting through this whole sequence match dumphfdl's real
 * numbers, on a real captured frame". It deliberately does NOT yet:
 *   - run an LMS equalizer (dumphfdl's eqlms_cccf, trained against
 *     T_seq during every EQ_TRAIN window and applied to every sample,
 *     including data ones) - hfdl_equalizer.c doesn't exist yet.
 *   - resolve the GLOBAL 180-degree phase ambiguity dumphfdl tracks
 *     via c->bitmask (set once from A1's correlation sign, then
 *     XORed into every subsequent hard-decision bit and used to pick
 *     which T_seq[] variant to train against) - this project's A1/A2/
 *     M1 framer now resolves that ambiguity independently PER BLOCK
 *     (see hfdl_framer.h's non-coherent block combining), which
 *     doesn't hand back a single global answer the way dumphfdl's
 *     single-coherent-block correlator did, so this needs its own
 *     design work, not a direct port.
 *   - track/adjust Costas phase using a multi-arity demodulator's
 *     phase error (dumphfdl's modem_demodulate() +
 *     modem_get_demodulator_phase_error(), generalized across BPSK/
 *     QPSK/8PSK) - hfdl_costas.c's phase detector is BPSK-only today.
 *   - demodulate DATA_1/DATA_2 symbols into actual bits at all - this
 *     round only COUNTS and stores raw soft I/Q, it doesn't attempt
 *     to recover data content.
 * All four are real, separate pieces of follow-up work once this
 * round's counting is confirmed correct against real hardware.
 *
 * SOURCE OF TRUTH: dumphfdl src/hfdl.c, specifically:
 *   - #defines: M2_LEN=15, T_LEN=15, DATA_FRAME_LEN=30 (lines ~30-34)
 *   - the switch(c->fr_state) block for FRAMER_M2_SKIP/FRAMER_EQ_TRAIN/
 *     FRAMER_DATA_1/FRAMER_DATA_2 (~lines 844-891)
 *   - the c->symbols_wanted decrement-then-switch pattern (~line 745:
 *     "if(c->symbols_wanted > 1) { c->symbols_wanted--; continue; }")
 *     - same "act on the LAST symbol of the current window" shape
 *     this project's own hfdl_framer.c/hfdl_preamble_sync.c already
 *     use for skip_remaining, so this module fits the established
 *     local convention as well as matching dumphfdl.
 *
 * SEQUENCE (per real frame, once M1 has matched):
 *   M2_SKIP (15 symbols, discarded)
 *   -> EQ_TRAIN x9 (9 * 15 = 135 symbols, the initial equalizer
 *      training burst before any data)
 *   -> repeat data_segment_cnt times:
 *        DATA_1 (15) + DATA_2 (15) = one 30-symbol data frame
 *        EQ_TRAIN x1 (15 symbols, re-trains between every data frame)
 *   -> DONE (caller should hfdl_preamble_sync_reset() now and go back
 *      to hunting for the next frame's A1)
 *
 * Total symbols this module will walk through, for a given
 * data_segment_cnt: 15 + 135 + data_segment_cnt*(30+15)
 *   = 150 + data_segment_cnt*45
 * For data_segment_cnt=72 (single slot, seen in real captures):
 *   150 + 72*45 = 3390
 * For data_segment_cnt=168 (double slot, also seen in real captures):
 *   150 + 168*45 = 7710
 * hfdl_data_segment_selftest() checks both these totals exactly, not
 * just "some plausible number" - see hfdl_data_segment.c.
 */

typedef enum {
	HFDL_DSEG_M2_SKIP = 0,
	HFDL_DSEG_EQ_TRAIN = 1,
	HFDL_DSEG_DATA_1 = 2,
	HFDL_DSEG_DATA_2 = 3,
	HFDL_DSEG_DONE = 4 /* full data_segment_cnt consumed - caller's job to reset
	                      hfdl_preamble_sync_t and resume hunting, this module doesn't
	                      touch that struct at all (kept decoupled, same reasoning as
	                      every other module boundary in this project) */
} hfdl_data_segment_state_t;

/* Literal constants from dumphfdl's src/hfdl.c - see this file's top
 * comment for exact line references. HFDL_M2_LEN is intentionally a
 * SEPARATE constant from hfdl_preamble_seq.h even though both
 * ultimately come from dumphfdl's same M2_LEN=15 - this module has no
 * dependency on hfdl_preamble_seq.h (M2 is SKIPPED here, never
 * correlated against, so this module never needs the actual M2[]
 * bit-sequence hfdl_preamble_seq.h builds for the OTHER, still-
 * unresolved global-bitmask-ambiguity reason explained above). */
#define HFDL_M2_LEN 15u
#define HFDL_T_LEN 15u
#define HFDL_DATA_FRAME_LEN 30u

typedef struct {
	hfdl_data_segment_state_t state;
	int32_t symbols_wanted;   /* mirrors dumphfdl's c->symbols_wanted - counts down to 1, see top comment */
	int32_t eq_train_seq_cnt; /* mirrors dumphfdl's c->eq_train_seq_cnt - starts at 9, drops to 1 between data frames */
	int32_t data_segment_cnt; /* mirrors dumphfdl's c->data_segment_cnt - counts DOWN from hfdl_frame_params_t's value to 0 */
	uint32_t mod_arity;       /* HFDL_MOD_BPSK/PSK4/PSK8 (hfdl_preamble_seq.h) - recorded but NOT used for demod this round, see top comment */
	uint32_t t_idx;           /* mirrors dumphfdl's c->T_idx (16/08/2026, round 2) - indexes HFDL_T_SEQ[bitmask][t_idx]
	                              during EQ_TRAIN, 0..HFDL_T_LEN-1, resets to 0 every time a new EQ_TRAIN window starts
	                              (dumphfdl: c->T_idx = 0 at the same two transition points this module's state
	                              machine already mirrors - see hfdl_data_segment.c) */

	/* Per-category tallies (16/08/2026, round-1 validation purpose -
	 * see top comment) - not used by dumphfdl itself, added here
	 * specifically so hfdl_data_segment_get_*() below can report
	 * exactly how many symbols fell in each category, for comparing
	 * against this file's own worked-out totals on real captures. */
	uint32_t total_symbols_seen;
	uint32_t m2_skip_symbols_seen;
	uint32_t eq_train_symbols_seen;
	uint32_t data_symbols_seen;
} hfdl_data_segment_t;

/* Starts tracking a fresh data segment right after LOCKED - call
 * exactly once per real LOCKED event, with the SAME hfdl_frame_params_t
 * hfdl_preamble_sync_get_frame_params() just returned. Sets state to
 * HFDL_DSEG_M2_SKIP and symbols_wanted to HFDL_M2_LEN, matching
 * dumphfdl's own FRAMER_M1_SEARCH case body (the moment M1 matches,
 * BEFORE FRAMER_M2_SKIP's own switch-case body even runs once - see
 * this file's top comment, "the symbol M1 matched on" already counts
 * as symbols_wanted's first decrement in dumphfdl, this mirrors that
 * exactly by NOT counting the M1-match symbol itself here at all,
 * consistent with hfdl_preamble_sync.c handing control over only
 * AFTER that symbol is already consumed). */
void hfdl_data_segment_init(hfdl_data_segment_t *ds, uint32_t mod_arity, int32_t data_segment_cnt);

/* Feeds ONE post-LOCKED Costas-corrected complex symbol. Updates
 * state/tallies per dumphfdl's real switch(c->fr_state) logic (see
 * top comment) - does NOT do anything with i_sym/q_sym's actual
 * values yet beyond counting (no equalizer, no demod - see top
 * comment for why). Caller should stop calling this (and instead
 * read hfdl_data_segment_get_state() == HFDL_DSEG_DONE, then go handle
 * the completed segment / reset hfdl_preamble_sync_t) once DONE. */
void hfdl_data_segment_process_symbol(hfdl_data_segment_t *ds, float32_t i_sym, float32_t q_sym);

hfdl_data_segment_state_t hfdl_data_segment_get_state(const hfdl_data_segment_t *ds);
uint32_t hfdl_data_segment_get_total_symbols_seen(const hfdl_data_segment_t *ds);
uint32_t hfdl_data_segment_get_m2_skip_symbols_seen(const hfdl_data_segment_t *ds);
uint32_t hfdl_data_segment_get_eq_train_symbols_seen(const hfdl_data_segment_t *ds);
uint32_t hfdl_data_segment_get_data_symbols_seen(const hfdl_data_segment_t *ds);

/* mirrors dumphfdl's c->T_idx - see hfdl_data_segment_t's own field
 * comment. Only meaningful while hfdl_data_segment_get_state()
 * returns HFDL_DSEG_EQ_TRAIN - callers should index
 * HFDL_T_SEQ[bitmask][hfdl_data_segment_get_t_idx(ds)] (see
 * hfdl_preamble_seq.h) for the CURRENT symbol's training reference,
 * BEFORE calling hfdl_data_segment_process_symbol() for that same
 * symbol (t_idx only advances on the process_symbol() call itself -
 * same "read then feed" ordering as this project's other per-symbol
 * getters). */
uint32_t hfdl_data_segment_get_t_idx(const hfdl_data_segment_t *ds);

/* Host-testable self-test (same "check before trusting" shape as
 * every other module in this project) - runs hfdl_data_segment_init()
 * + hfdl_data_segment_process_symbol() through a full synthetic
 * segment for BOTH data_segment_cnt=72 and =168 (the two real values
 * seen in actual captures so far), and checks the EXACT expected
 * total/per-category symbol counts worked out in this file's top
 * comment - not just "some number came out", the precise number
 * dumphfdl's own state machine would produce. Returns 1 if both pass,
 * 0 otherwise. */
uint8_t hfdl_data_segment_selftest(void);

#endif /* HFDL_DATA_SEGMENT_H */
