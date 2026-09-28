#include <stddef.h>
#include "hfdl_deinterleaver.h"

/* See hfdl_deinterleaver.h for the derivation and dumphfdl source
 * reference. DATA_FRAME_LEN=30 is fixed across all 8 combinations
 * (confirmed in the project's HFDL_phy_layer_params_dumphfdl document,
 * section 3) - not repeated per-row here since it's a shared constant,
 * folded directly into hfdl_deinterleaver_column_cnt() below. */
#define HFDL_DATA_FRAME_LEN 30

const hfdl_rate_params_t hfdl_rate_params[8] = {
	/* m1_index, bitrate_bps, double_slot, scheme_bits, code_rate_denom, push_column_shift, data_segment_cnt */
	{ 0,  300, false, 1, 4, 17,  72 },
	{ 1,  600, false, 1, 2, 17,  72 },
	{ 2, 1200, false, 2, 2, 17,  72 },
	{ 3, 1800, false, 3, 2, 17,  72 },
	{ 4,  300, true,  1, 4, 23, 168 },
	{ 5,  600, true,  1, 2, 23, 168 },
	{ 6, 1200, true,  2, 2, 23, 168 },
	{ 7, 1800, true,  3, 2, 23, 168 },
};

int32_t hfdl_deinterleaver_column_cnt(const hfdl_rate_params_t *p)
{
	return ((int32_t)p->data_segment_cnt * HFDL_DATA_FRAME_LEN * (int32_t)p->scheme_bits)
		/ HFDL_DEINTERLEAVER_ROW_CNT;
}

void hfdl_deinterleaver_configure(hfdl_deinterleaver_t *d, uint8_t *table_buf, const hfdl_rate_params_t *p)
{
	d->table = table_buf;
	d->column_cnt = hfdl_deinterleaver_column_cnt(p);
	d->push_column_shift = p->push_column_shift;
	d->row = 0;
	d->col = 0;
}

void hfdl_deinterleaver_reset(hfdl_deinterleaver_t *d)
{
	d->row = 0;
	d->col = 0;
}

void hfdl_deinterleaver_push(hfdl_deinterleaver_t *d, uint8_t val)
{
	d->table[(d->row * d->column_cnt) + d->col] = val;
	d->row++;
	if (d->row == HFDL_DEINTERLEAVER_ROW_CNT) {
		d->row = 0;
		d->col++;
	}
	d->col -= d->push_column_shift;
	if (d->col < 0) {
		d->col += d->column_cnt;
	}
}

uint8_t hfdl_deinterleaver_pop(hfdl_deinterleaver_t *d)
{
	uint8_t ret = d->table[(d->row * d->column_cnt) + d->col];
	d->row = (d->row + HFDL_DEINTERLEAVER_POP_ROW_SHIFT) % HFDL_DEINTERLEAVER_ROW_CNT;
	if (d->row == 0) {
		d->col++;
	}
	return ret;
}

/* ---- self-test ---- */

/* Static, not stack-local: at 15120 bytes (worst case) this is
 * comfortably within the project's free-RAM budget (see the
 * architecture document) but not something to put on a call stack,
 * especially inside a self-test that might run from a shallow-stack
 * boot-time context. */
static uint8_t s_selftest_table[HFDL_DEINTERLEAVER_MAX_TABLE_SIZE];

/* Coverage bitmap, bit-packed (1 bit per cell instead of 1 byte) -
 * (15120 + 7) / 8 = 1890 bytes, reused across all 8 rate checks in
 * Part 1 AND for both the push-coverage and pop-coverage passes
 * within each rate (cleared between). An earlier version of this
 * self-test used 2 full byte-per-cell arrays instead (30240 bytes
 * combined) plus an oversized third one - ~50KB total, 89% of this
 * project's free RAM margin, just for test scaffolding. That was
 * real but clearly not something to be casual about even for a
 * TEMP validation build, so it got reworked into this bit-packed
 * version instead (~1.9KB) before being considered done. */
#define SELFTEST_BITMAP_BYTES ((HFDL_DEINTERLEAVER_MAX_TABLE_SIZE + 7) / 8)
static uint8_t s_selftest_bitmap[SELFTEST_BITMAP_BYTES];

static void bitmap_clear(uint8_t *bitmap, int32_t nbits)
{
	int32_t nbytes = (nbits + 7) / 8;
	for (int32_t i = 0; i < nbytes; i++) {
		bitmap[i] = 0u;
	}
}

/* Sets bit `idx` and returns true if it was ALREADY set (i.e. this is
 * a second touch of the same cell - a collision on push, or a
 * double-read on pop). */
static bool bitmap_test_and_set(uint8_t *bitmap, int32_t idx)
{
	int32_t byte_idx = idx >> 3;
	uint8_t bit_mask = (uint8_t)(1u << (idx & 7));
	bool was_set = (bitmap[byte_idx] & bit_mask) != 0u;
	bitmap[byte_idx] |= bit_mask;
	return was_set;
}

static bool selftest_bijection_for_rate(const hfdl_rate_params_t *p)
{
	int32_t column_cnt = hfdl_deinterleaver_column_cnt(p);
	int32_t table_size = column_cnt * HFDL_DEINTERLEAVER_ROW_CNT;

	if (table_size > HFDL_DEINTERLEAVER_MAX_TABLE_SIZE) {
		return false; /* would not fit - shouldn't happen for any real HFDL rate */
	}

	hfdl_deinterleaver_t d;
	hfdl_deinterleaver_configure(&d, s_selftest_table, p);
	hfdl_deinterleaver_reset(&d);

	/* Push table_size times, tracking which cells got touched. Since
	 * exactly table_size pushes happen into a table_size-cell table,
	 * "every cell touched at least once" and "no cell touched more
	 * than once" are the SAME condition here (pigeonhole principle) -
	 * so a single "was this already set?" check per push is enough to
	 * catch a collision, and a full pass at the end confirms no cell
	 * was left untouched. */
	bitmap_clear(s_selftest_bitmap, table_size);
	for (int32_t i = 0; i < table_size; i++) {
		int32_t idx = (d.row * d.column_cnt) + d.col; /* cell about to be written by this push */
		if (idx < 0 || idx >= table_size) {
			return false; /* cursor escaped the table - structural bug */
		}
		if (bitmap_test_and_set(s_selftest_bitmap, idx)) {
			return false; /* push collision */
		}
		hfdl_deinterleaver_push(&d, (uint8_t)((uint32_t)i & 0xFFu));
	}
	/* (no separate "all covered" pass needed - the pigeonhole argument
	 * above means the collision check during the loop already proves
	 * full coverage too, given exactly table_size pushes happened) */

	/* Pop table_size times (cursor continues from where push left it,
	 * per the module's documented usage - no reset here), same
	 * pigeonhole argument for the pop side. */
	bitmap_clear(s_selftest_bitmap, table_size);
	for (int32_t i = 0; i < table_size; i++) {
		int32_t idx = (d.row * d.column_cnt) + d.col; /* cell about to be read by this pop */
		if (idx < 0 || idx >= table_size) {
			return false;
		}
		if (bitmap_test_and_set(s_selftest_bitmap, idx)) {
			return false; /* this cell already popped once this segment */
		}
		hfdl_deinterleaver_pop(&d);
	}

	return true;
}

bool hfdl_deinterleaver_selftest(void)
{
	/* --- Part 1: bijection (push=write-once, pop=read-once) holds
	 * for all 8 real HFDL rate/slot combinations, not just one. --- */
	for (int32_t i = 0; i < 8; i++) {
		if (!selftest_bijection_for_rate(&hfdl_rate_params[i])) {
			return false;
		}
	}

	/* --- Part 2: exact ordering cross-check against an independent
	 * Python re-implementation of the identical push/pop algorithm,
	 * run during development for the 300bps single-slot rate
	 * (hfdl_rate_params[0]: column_cnt=54, push_shift=17, table
	 * size=2160). This catches transcription bugs (off-by-one, wrong
	 * shift direction, row/col swapped, etc.) that the bijection
	 * check in Part 1 could theoretically miss if such a bug happened
	 * to still produce SOME valid permutation, just a different one
	 * than HFDL actually uses on the air. --- */
	{
		static const uint16_t expected_first_20[20] = {
			0, 369, 738, 1107, 1476, 2125, 334, 703, 1072, 1721,
			2090, 299, 668, 1037, 1686, 2055, 264, 633, 1282, 1651
		};
		static const uint16_t expected_last_10[10] = {
			70, 439, 1088, 1457, 1826, 35, 684, 1053, 1422, 1791
		};

		hfdl_deinterleaver_t d;
		hfdl_deinterleaver_configure(&d, s_selftest_table, &hfdl_rate_params[0]);
		hfdl_deinterleaver_reset(&d);

		int32_t column_cnt = hfdl_deinterleaver_column_cnt(&hfdl_rate_params[0]);
		int32_t n = column_cnt * HFDL_DEINTERLEAVER_ROW_CNT; /* 2160 */

		for (int32_t i = 0; i < n; i++) {
			hfdl_deinterleaver_push(&d, (uint8_t)((uint32_t)i & 0xFFu));
		}

		/* Need 16-bit markers to disambiguate values >= 256 (n=2160
		 * wraps a uint8_t marker many times over) - push a second,
		 * wider marker table in parallel via the SAME cursor sequence
		 * by re-deriving each popped value's ORIGINAL push index from
		 * its table position instead of trusting the (wrapped) byte
		 * value. Simplest correct approach: redo the push with the low
		 * byte AND track full 16-bit index via a separate array
		 * indexed the same way the table is. */
		static uint16_t s_index_table[2160];
		hfdl_deinterleaver_configure(&d, s_selftest_table, &hfdl_rate_params[0]);
		hfdl_deinterleaver_reset(&d);
		{
			hfdl_deinterleaver_t d2 = d; /* mirror cursor to also drive s_index_table identically */
			for (int32_t i = 0; i < n; i++) {
				s_index_table[(d2.row * d2.column_cnt) + d2.col] = (uint16_t)i;
				hfdl_deinterleaver_push(&d2, (uint8_t)((uint32_t)i & 0xFFu));
			}
		}

		uint16_t popped_indices[2160];
		for (int32_t i = 0; i < n; i++) {
			int32_t idx = (d.row * d.column_cnt) + d.col;
			popped_indices[i] = s_index_table[idx];
			hfdl_deinterleaver_pop(&d);
		}

		for (int32_t i = 0; i < 20; i++) {
			if (popped_indices[i] != expected_first_20[i]) {
				return false;
			}
		}
		for (int32_t i = 0; i < 10; i++) {
			if (popped_indices[n - 10 + i] != expected_last_10[i]) {
				return false;
			}
		}
	}

	return true;
}
