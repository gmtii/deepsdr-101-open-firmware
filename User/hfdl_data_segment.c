#include "hfdl_data_segment.h"

void hfdl_data_segment_init(hfdl_data_segment_t *ds, uint32_t mod_arity, int32_t data_segment_cnt)
{
	ds->state = HFDL_DSEG_M2_SKIP;
	ds->symbols_wanted = (int32_t)HFDL_M2_LEN;
	ds->eq_train_seq_cnt = 9; /* dumphfdl: c->eq_train_seq_cnt = 9, set in the FRAMER_M1_SEARCH case body */
	ds->data_segment_cnt = data_segment_cnt;
	ds->mod_arity = mod_arity;
	ds->t_idx = 0u; /* dumphfdl: c->T_idx = 0 at framer_reset() - see hfdl_data_segment_t's field comment */
	ds->total_symbols_seen = 0u;
	ds->m2_skip_symbols_seen = 0u;
	ds->eq_train_symbols_seen = 0u;
	ds->data_symbols_seen = 0u;
}

void hfdl_data_segment_process_symbol(hfdl_data_segment_t *ds, float32_t i_sym, float32_t q_sym)
{
	(void)i_sym; /* not used yet - see header's top comment (no equalizer/demod this round) */
	(void)q_sym;

	if (ds->state == HFDL_DSEG_DONE) {
		return; /* caller should have stopped calling by now - see header, harmless no-op guard */
	}

	ds->total_symbols_seen++;
	switch (ds->state) {
	case HFDL_DSEG_M2_SKIP:
		ds->m2_skip_symbols_seen++;
		break;
	case HFDL_DSEG_EQ_TRAIN:
		ds->eq_train_symbols_seen++;
		ds->t_idx++; /* dumphfdl: c->T_idx++ right after the eqlms_cccf_step() call - see
		                hfdl_data_segment_t's field comment on the "read before feeding this
		                symbol, THEN this increments" ordering callers must follow */
		break;
	case HFDL_DSEG_DATA_1:
	case HFDL_DSEG_DATA_2:
		ds->data_symbols_seen++;
		break;
	default:
		break;
	}

	/* Same "act on the LAST symbol of the current window" shape as
	 * dumphfdl's own `if(c->symbols_wanted > 1) { c->symbols_wanted--; continue; }`
	 * - see header's top comment. */
	if (ds->symbols_wanted > 1) {
		ds->symbols_wanted--;
		return;
	}

	/* This IS the last symbol of the current window - advance the
	 * state machine, literal transcription of dumphfdl's
	 * switch(c->fr_state) body for these four cases (see header's
	 * top comment for exact source line reference). */
	switch (ds->state) {
	case HFDL_DSEG_M2_SKIP:
		ds->symbols_wanted = (int32_t)HFDL_T_LEN;
		ds->eq_train_seq_cnt = 9;
		/* dumphfdl does NOT explicitly reset T_idx in this exact case
		 * body - it relies on T_idx already being 0 from the last
		 * framer_reset() (never touched since). This module's
		 * hfdl_data_segment_init() already sets t_idx=0 up front for
		 * the same reason, so the RESULT is identical without needing
		 * a redundant reset here too - see hfdl_data_segment_init(). */
		ds->state = HFDL_DSEG_EQ_TRAIN;
		break;

	case HFDL_DSEG_EQ_TRAIN:
		if (ds->eq_train_seq_cnt > 1) {
			/* next frame is ANOTHER training sequence - dumphfdl DOES
			 * explicitly reset T_idx=0 in this exact branch (each of
			 * the 9 initial training sub-windows gets its own fresh
			 * 0..14 index, not one continuously-incrementing 0..134 -
			 * confirmed from src/hfdl.c's real FRAMER_EQ_TRAIN case
			 * body). */
			ds->eq_train_seq_cnt--;
			ds->symbols_wanted = (int32_t)HFDL_T_LEN;
			ds->t_idx = 0u;
		} else if (ds->data_segment_cnt > 0) {
			/* next frame is a data frame */
			ds->symbols_wanted = (int32_t)(HFDL_DATA_FRAME_LEN / 2u);
			ds->state = HFDL_DSEG_DATA_1;
		} else {
			/* end of frame - dumphfdl calls decode_user_data() + framer_reset()
			 * here; this module stops at DONE and hands control back to the
			 * caller instead, see header's top comment */
			ds->state = HFDL_DSEG_DONE;
		}
		break;

	case HFDL_DSEG_DATA_1:
		ds->symbols_wanted = (int32_t)(HFDL_DATA_FRAME_LEN / 2u);
		ds->state = HFDL_DSEG_DATA_2;
		break;

	case HFDL_DSEG_DATA_2:
		ds->data_segment_cnt--;
		ds->state = HFDL_DSEG_EQ_TRAIN;
		ds->eq_train_seq_cnt = 1;
		ds->symbols_wanted = (int32_t)HFDL_T_LEN;
		ds->t_idx = 0u; /* dumphfdl: c->T_idx = 0, same FRAMER_DATA_2 case body */
		break;

	case HFDL_DSEG_DONE:
	default:
		break;
	}
}

hfdl_data_segment_state_t hfdl_data_segment_get_state(const hfdl_data_segment_t *ds)
{
	return ds->state;
}

uint32_t hfdl_data_segment_get_total_symbols_seen(const hfdl_data_segment_t *ds)
{
	return ds->total_symbols_seen;
}

uint32_t hfdl_data_segment_get_m2_skip_symbols_seen(const hfdl_data_segment_t *ds)
{
	return ds->m2_skip_symbols_seen;
}

uint32_t hfdl_data_segment_get_eq_train_symbols_seen(const hfdl_data_segment_t *ds)
{
	return ds->eq_train_symbols_seen;
}

uint32_t hfdl_data_segment_get_data_symbols_seen(const hfdl_data_segment_t *ds)
{
	return ds->data_symbols_seen;
}

uint32_t hfdl_data_segment_get_t_idx(const hfdl_data_segment_t *ds)
{
	return ds->t_idx;
}

#define HFDL_MOD_BPSK_FOR_SELFTEST 1u /* HFDL_MOD_BPSK from hfdl_preamble_seq.h - not included here to
                                         keep this module's only real dependency being its own header,
                                         see hfdl_data_segment.h's top comment on why M2/bitmask
                                         resolution is deliberately NOT wired to hfdl_preamble_seq.h yet */

static uint8_t selftest_one(int32_t data_segment_cnt)
{
	hfdl_data_segment_t ds;
	hfdl_data_segment_init(&ds, HFDL_MOD_BPSK_FOR_SELFTEST, data_segment_cnt);

	/* Expected totals, worked out in the header's top comment -
	 * checked EXACTLY, not just "some plausible number". */
	uint32_t expected_m2 = HFDL_M2_LEN;
	uint32_t expected_eq_train = 9u * HFDL_T_LEN + (uint32_t)data_segment_cnt * HFDL_T_LEN;
	uint32_t expected_data = (uint32_t)data_segment_cnt * HFDL_DATA_FRAME_LEN;
	uint32_t expected_total = expected_m2 + expected_eq_train + expected_data;

	/* Feed symbols one at a time until DONE, with a hard safety cap
	 * so a bug that never reaches DONE fails this test instead of
	 * looping forever (host test context, but good practice
	 * regardless - same "never trust an unbounded loop" instinct
	 * this project applies on the embedded side too). */
	uint32_t safety_cap = expected_total * 2u + 100u;
	uint32_t iterations = 0u;
	while (hfdl_data_segment_get_state(&ds) != HFDL_DSEG_DONE) {
		hfdl_data_segment_process_symbol(&ds, 0.0f, 0.0f); /* values irrelevant - see header, no demod this round */
		iterations++;
		if (iterations > safety_cap) {
			return 0u; /* never reached DONE - real bug, not just a slow test */
		}
		/* t_idx bounds check (16/08/2026, round 2) - see
		 * hfdl_data_segment_t's field comment: t_idx must stay
		 * 0..HFDL_T_LEN-1 while in EQ_TRAIN (it's an index into
		 * HFDL_T_SEQ[bitmask][t_idx], HFDL_T_LEN=15 entries) - if this
		 * ever fires, either a reset point was missed or t_idx is
		 * incrementing somewhere it shouldn't. */
		if (hfdl_data_segment_get_state(&ds) == HFDL_DSEG_EQ_TRAIN &&
				hfdl_data_segment_get_t_idx(&ds) >= HFDL_T_LEN) {
			return 0u;
		}
	}

	if (hfdl_data_segment_get_total_symbols_seen(&ds) != expected_total) {
		return 0u;
	}
	if (hfdl_data_segment_get_m2_skip_symbols_seen(&ds) != expected_m2) {
		return 0u;
	}
	if (hfdl_data_segment_get_eq_train_symbols_seen(&ds) != expected_eq_train) {
		return 0u;
	}
	if (hfdl_data_segment_get_data_symbols_seen(&ds) != expected_data) {
		return 0u;
	}
	if (ds.data_segment_cnt != 0) {
		return 0u; /* should have counted all the way down, same as dumphfdl's own c->data_segment_cnt */
	}

	return 1u;
}

#define HFDL_MOD_BPSK_FOR_SELFTEST 1u /* HFDL_MOD_BPSK from hfdl_preamble_seq.h - not included here to
                                         keep this module's only real dependency being its own header,
                                         see hfdl_data_segment.h's top comment on why M2/bitmask
                                         resolution is deliberately NOT wired to hfdl_preamble_seq.h yet */

uint8_t hfdl_data_segment_selftest(void)
{
	if (!selftest_one(72)) {  /* single-slot, real value seen in project captures */
		return 0u;
	}
	if (!selftest_one(168)) { /* double-slot, real value seen in project captures */
		return 0u;
	}
	return 1u;
}
