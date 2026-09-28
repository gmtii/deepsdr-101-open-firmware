#ifndef HFDL_DEINTERLEAVER_H
#define HFDL_DEINTERLEAVER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * HFDL data-segment deinterleaver - step 4 of Phase 2's staged plan
 * (see hfdl_crc.h/hfdl_pdu.h/hfdl_descrambler.h for steps 1-3, and
 * the project's HFDL_fase2_arquitectura document for the full plan).
 * Zero dependency on the rest of the pipeline, same as those - a pure
 * table-shuffling algorithm, built/tested standalone first.
 *
 * Algorithm confirmed directly against dumphfdl's src/hfdl.c
 * (szpajder/dumphfdl, GPL-3.0-or-later) struct deinterleaver /
 * deinterleaver_create() / _push() / _pop() / _reset() - read
 * literally, not reconstructed from a description.
 *
 * IMPORTANT structural note, confirmed by reading dumphfdl's own
 * call sites (decode_user_data() in hfdl.c): push and pop share ONE
 * cursor (row, col) inside the same object - they are NOT independent
 * read/write pointers into a FIFO. The intended usage per data
 * segment is: push every soft bit of the whole segment first (this
 * writes every table cell exactly once, in a specific order), THEN
 * pop the same count (this reads every cell exactly once, in a
 * DIFFERENT order) - continuing from wherever the cursor was left by
 * the push phase, with NO reset in between. hfdl_deinterleaver_reset()
 * is only meant to be called once per new frame/channel session (per
 * dumphfdl's channel_reset()), not between the push phase and the pop
 * phase of one segment.
 *
 * This only produces a valid result (every cell written exactly once
 * on push, every cell read exactly once on pop, i.e. pop is a full
 * permutation of what was pushed) for the SPECIFIC (column_cnt,
 * push_column_shift) pairs HFDL's spec actually uses - arbitrary
 * small numbers picked for a "simple" test do NOT generally form this
 * bijection (verified with Python during development: column_cnt=3,
 * push_shift=1 leaves many cells unwritten). hfdl_deinterleaver_selftest()
 * below therefore tests with REAL HFDL parameters, not made-up ones -
 * see that function for why.
 *
 * RAM ownership: this module does NOT allocate the table itself (no
 * malloc in this bare-metal project, per the project's own
 * conventions) - the caller supplies a buffer via
 * hfdl_deinterleaver_configure(), sized for whichever rate's
 * column_cnt is needed. Per the project's Phase 2 architecture
 * decision ("un unico deinterleaver activo"), the intended usage is
 * ONE statically-allocated buffer sized for the worst case
 * (HFDL_DEINTERLEAVER_MAX_TABLE_SIZE, 1800bps double-slot = 15120
 * bytes) reused across whichever rate the preamble detects, not one
 * buffer per rate.
 */

#define HFDL_DEINTERLEAVER_ROW_CNT       40
#define HFDL_DEINTERLEAVER_POP_ROW_SHIFT 9

/* Worst case across all 8 rate/slot combinations (1800bps
 * double-slot): column_cnt=378, table size = 378*40 = 15120 bytes.
 * See hfdl_rate_params[] below for all 8 combinations' exact sizes. */
#define HFDL_DEINTERLEAVER_MAX_COLUMNS    378
#define HFDL_DEINTERLEAVER_MAX_TABLE_SIZE (HFDL_DEINTERLEAVER_MAX_COLUMNS * HFDL_DEINTERLEAVER_ROW_CNT)

typedef struct {
	uint8_t *table;          /* caller-owned buffer, >= column_cnt * HFDL_DEINTERLEAVER_ROW_CNT bytes */
	int32_t row;
	int32_t col;
	int32_t column_cnt;
	int32_t push_column_shift;
} hfdl_deinterleaver_t;

/* The 8 rate/slot combinations from dumphfdl's hfdl_frame_params[]
 * table (see the project's HFDL_phy_layer_params_dumphfdl document,
 * section 3) - kept here since deinterleaver sizing is the thing that
 * actually consumes them at runtime. `m1_index` is dumphfdl's own
 * index (0-7) for this combination, mainly useful if/when the framer
 * module (a later Phase 2 step) needs to report/log which one the
 * preamble detected. */
typedef struct {
	uint8_t  m1_index;
	uint16_t bitrate_bps;
	bool     double_slot;
	uint8_t  scheme_bits;       /* 1=BPSK, 2=QPSK, 3=8PSK */
	uint8_t  code_rate_denom;   /* 2 or 4 (4 means rate-1/4 repetition + rate-1/2 conv code) */
	int32_t  push_column_shift; /* 17 (single-slot) or 23 (double-slot) */
	uint16_t data_segment_cnt;  /* 72 (single-slot) or 168 (double-slot) */
} hfdl_rate_params_t;

extern const hfdl_rate_params_t hfdl_rate_params[8];

/* Computes column_cnt for a given rate/slot combination:
 * data_segment_cnt * DATA_FRAME_LEN(30) * scheme_bits / ROW_CNT(40).
 * This is always an exact integer division for all 8 real HFDL
 * combinations (verified during development) - HFDL's own frame
 * structure is designed so it divides evenly. */
int32_t hfdl_deinterleaver_column_cnt(const hfdl_rate_params_t *p);

/* Binds `d` to a specific rate/slot's parameters and a caller-owned
 * table buffer (`table_buf` must be at least
 * hfdl_deinterleaver_column_cnt(p) * HFDL_DEINTERLEAVER_ROW_CNT bytes
 * - HFDL_DEINTERLEAVER_MAX_TABLE_SIZE always covers the worst case).
 * Does NOT clear the buffer or reset the cursor - call
 * hfdl_deinterleaver_reset() explicitly afterwards for a clean start. */
void hfdl_deinterleaver_configure(hfdl_deinterleaver_t *d, uint8_t *table_buf, const hfdl_rate_params_t *p);

/* Resets the shared push/pop cursor to (0,0) - call once per new
 * frame/channel session, NOT between a segment's push phase and its
 * pop phase (see module header comment above for why). */
void hfdl_deinterleaver_reset(hfdl_deinterleaver_t *d);

/* Pushes one soft bit into the table at the current cursor position
 * and advances the cursor per HFDL's push pattern. Call exactly
 * column_cnt * HFDL_DEINTERLEAVER_ROW_CNT times per segment (that
 * count exactly fills every table cell once) before any
 * hfdl_deinterleaver_pop() call for that same segment. */
void hfdl_deinterleaver_push(hfdl_deinterleaver_t *d, uint8_t val);

/* Pops one soft bit from the table at the current cursor position and
 * advances the cursor per HFDL's (different) pop pattern - continuing
 * from wherever the preceding push phase left the cursor, NOT reset
 * first. Call exactly column_cnt * HFDL_DEINTERLEAVER_ROW_CNT times
 * to drain everything that was pushed for that segment. */
uint8_t hfdl_deinterleaver_pop(hfdl_deinterleaver_t *d);

/* Runs the module's standalone self-test: for the 300bps single-slot
 * rate (the smallest real HFDL config, table size 2160 bytes) it
 * pushes a full segment of distinct marker values, verifies every
 * cell was written exactly once (no push collisions), pops the same
 * count back, verifies every cell was read exactly once (no
 * unwritten-cell pops) and that the popped sequence is a full
 * permutation of what was pushed - then cross-checks the first 20 and
 * last 10 popped values against an independent Python
 * re-implementation of the identical algorithm run during
 * development. Also verifies the bijection property (push=write-once,
 * pop=read-once) holds for all 8 real rate/slot combinations, not
 * just the one used for the detailed ordering check. Zero dependency
 * on the rest of Phase 2. */
bool hfdl_deinterleaver_selftest(void);

#endif /* HFDL_DEINTERLEAVER_H */
