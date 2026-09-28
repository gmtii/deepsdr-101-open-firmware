#ifndef HFDL_PREAMBLE_SEQ_H
#define HFDL_PREAMBLE_SEQ_H

#include <stdint.h>
#include "arm_math.h"

/*
 * HFDL preamble reference sequences and thresholds - CONFIRMED DIRECTLY
 * against dumphfdl's own source (szpajder/dumphfdl, GPL-3.0-or-later,
 * src/hfdl.c, project version uploaded by the project owner 13/08/2026
 * after this project's earlier attempt to fetch it from GitHub was
 * blocked in this environment - see hfdl_framer.h's header for that
 * history). Every constant below is a literal transcription of
 * dumphfdl's own arrays/`#define`s, not reconstructed or guessed -
 * same provenance standard as hfdl_descrambler.h's LFSR
 * genpoly/init/seq_len and hfdl_deinterleaver.h's
 * column_cnt/push_column_shift, both taken from this same source file.
 *
 * PROTOCOL STRUCTURE this module's constants describe (dumphfdl's
 * hfdl_decoder_thread(), FRAMER_A1_SEARCH -> FRAMER_A2_SEARCH ->
 * FRAMER_M1_SEARCH states):
 *   - A sequence (127 bits, "HFDL_A_LEN"): a single fixed BPSK bit
 *     pattern, checked TWICE in a row (once as "A1", once as "A2",
 *     immediately after skipping ahead HFDL_A_LEN symbols from the A1
 *     hit) - NOT two different sequences, despite the A1/A2 naming.
 *     Finding it twice back-to-back is what confirms "this is really
 *     an HFDL preamble", per dumphfdl's own stats doc
 *     (doc/STATSD_METRICS.md): "A2 is a pseudo-random bit sequence in
 *     the preamble ... occurs twice ... finding a second occurrence
 *     ... is a good indication that a HFDL frame has been found."
 *   - M1 sequence (127 bits, "HFDL_M1_LEN"): checked once, right after
 *     A2 - one of HFDL_M_SHIFT_CNT (8) cyclic rotations of the SAME
 *     base 127-bit array (HFDL_M1_BASE_BITS below), each rotation
 *     identifying one of the 8 (rate, single/double-slot) frame
 *     configurations in hfdl_frame_params[] below. Whichever rotation
 *     correlates above threshold tells the receiver the data phase's
 *     modulation order, code rate, segment count, and deinterleaver
 *     shift for the rest of THIS frame.
 *
 * Bit ordering / A_octets->bit unpacking: dumphfdl builds its
 * reference bsequence via liquid-dsp's bsequence_init(bs, v), which
 * unpacks each byte of v[] MSB-first (bit 7 down to bit 0) and pushes
 * each resulting bit via bsequence_push() (oldest bit ends up at the
 * "front" of the register, matching chronological/transmission
 * order) - confirmed by reading liquid-dsp's own
 * src/sequence/src/bsequence.c (jgaeddert/liquid-dsp, MIT license,
 * fetched from the liquid-dsp PyPI sdist in this environment, version
 * 1.8.2) directly, not assumed from the public API docs. This
 * project's hfdl_preamble_seq_init() (see hfdl_preamble_seq.c)
 * replicates that exact same MSB-first unpacking so
 * HFDL_A_OCTETS/HFDL_M1_BASE_BITS below produce bit-identical
 * sequences to dumphfdl's own A_bs/M1[]/M2[] - same values, just
 * expressed as +-1.0f floats (for direct use with
 * hfdl_framer_t's dot-product correlator, see hfdl_framer.h) instead
 * of a packed bsequence object.
 */

/* --- lengths (dumphfdl: #define A_LEN 127 / M1_LEN 127 / M2_LEN 15 / M_SHIFT_CNT 8) --- */
#define HFDL_A_LEN        127u
#define HFDL_M1_LEN       127u
#define HFDL_M2_LEN       15u
#define HFDL_M_SHIFT_CNT  8u
#define HFDL_MAX_SEARCH_RETRIES 3u /* dumphfdl: #define MAX_SEARCH_RETRIES 3 */

/* --- correlation thresholds (dumphfdl's #define CORR_THRESHOLD_*) ---
 * Note A1 and A2 use the SAME reference sequence (HFDL_A_OCTETS) but
 * DIFFERENT thresholds - A1's is stricter (first acquisition), A2's
 * looser (already roughly locked, just confirming). */
#define HFDL_CORR_THRESHOLD_A1  0.36f
#define HFDL_CORR_THRESHOLD_A2  0.30f
#define HFDL_CORR_THRESHOLD_M1  0.30f

/* Raw A sequence bytes, MSB-first, 128 bits packed but only the first
 * HFDL_A_LEN (127) are used (dumphfdl's bsequence_create(A_LEN) drops
 * the very last bit of the 16th byte - see hfdl_preamble_seq.c's
 * unpack function). Literal copy of dumphfdl's hfdl_init_globals()
 * A_octets[] array (src/hfdl.c). */
extern const uint8_t HFDL_A_OCTETS[16];

/* Base 127-bit M1 pattern (dumphfdl's hfdl_init_globals() M1_bits[]),
 * as plain 0/1 values (not bit-packed - dumphfdl declares it that way
 * too, as uint32_t M1_bits[M1_LEN]). The 8 real M1/M2 reference
 * sequences used for correlation are each a CYCLIC ROTATION of this
 * same array by HFDL_M_SHIFTS[shift] positions (see
 * hfdl_preamble_seq_build_m1_variant() in the .c file) - dumphfdl
 * does not store 8 separate base arrays, and neither does this
 * module. */
extern const uint8_t HFDL_M1_BASE_BITS[HFDL_M1_LEN];

/* Cyclic rotation offsets for the 8 M1 variants (dumphfdl's
 * M_shifts[M_SHIFT_CNT]). Index into this array is the SAME index
 * used into HFDL_FRAME_PARAMS[] below - e.g. HFDL_M_SHIFTS[2]/
 * HFDL_FRAME_PARAMS[2] both describe the "1200 bps, single slot"
 * configuration. */
extern const uint16_t HFDL_M_SHIFTS[HFDL_M_SHIFT_CNT];

/* Modulation arity values - dumphfdl's mod_arity enum (src/hfdl.c),
 * values are bits-per-symbol, kept as plain #defines here since this
 * project doesn't need the full enum (M_PSK4/M_PSK8 demodulation
 * isn't implemented in this project yet - see hfdl_preamble_sync.h). */
#define HFDL_MOD_BPSK 1u
#define HFDL_MOD_PSK4 2u
#define HFDL_MOD_PSK8 3u

/* One entry per M1 index (0-7) - literal transcription of dumphfdl's
 * hfdl_frame_params[M_SHIFT_CNT] (src/hfdl.c). Tells the receiver,
 * once a specific M1 rotation has been matched, what the rest of
 * THIS frame looks like. code_rate/deinterleaver_push_column_shift
 * are exactly the numbers this project's hfdl_deinterleaver.h/
 * hfdl_crc.h already document needing (17 for single-slot, 23 for
 * double-slot) - consistent with what was already sourced for those
 * modules earlier in the project. */
typedef struct {
	uint8_t  mod_arity;       /* HFDL_MOD_BPSK/PSK4/PSK8 */
	int32_t  data_segment_cnt;
	int32_t  code_rate;
	int32_t  deinterleaver_push_column_shift;
} hfdl_frame_params_t;

extern const hfdl_frame_params_t HFDL_FRAME_PARAMS[HFDL_M_SHIFT_CNT];

/* Fills `out[HFDL_A_LEN]` with the A sequence as +-1.0f floats, MSB-
 * first unpacked from HFDL_A_OCTETS exactly like liquid-dsp's
 * bsequence_init() (see this file's header). Call once at startup -
 * result is deterministic/constant, callers may cache it (e.g. as a
 * module-static array) rather than re-unpacking per use. */
void hfdl_preamble_seq_build_a(float32_t out[HFDL_A_LEN]);

/* Fills `out[HFDL_M1_LEN]` with M1 variant `shift_idx` (0..7, indexes
 * both HFDL_M_SHIFTS[] and HFDL_FRAME_PARAMS[]) as +-1.0f floats: the
 * cyclic rotation HFDL_M1_BASE_BITS[(HFDL_M_SHIFTS[shift_idx]+j) %
 * HFDL_M1_LEN] for j=0..HFDL_M1_LEN-1, matching dumphfdl's
 * hfdl_init_globals() M1[shift] construction exactly. */
void hfdl_preamble_seq_build_m1_variant(uint32_t shift_idx, float32_t out[HFDL_M1_LEN]);

/* T_seq (16/08/2026, "paso 2" round 2 - equalizer training reference)
 * - LITERAL transcription of dumphfdl's src/hfdl.c static float
 * complex T_seq[2][T_LEN] table (T_LEN=15 - see hfdl_data_segment.h's
 * HFDL_T_LEN, same constant). Unlike A1/A2/M1 (which are GENERATED
 * from a bit sequence + rotation - see hfdl_preamble_seq_build_a()/
 * _build_m1_variant() above), T_seq is a small enough fixed literal
 * table in dumphfdl itself that there's no generation logic to port -
 * just the two 15-symbol +-1.0f patterns, index [0] and its exact
 * negation index [1] (dumphfdl: T_seq[1][k] == -T_seq[0][k] for every
 * k, confirms these are meant to be the same training pattern under
 * BPSK's 180-degree ambiguity, not two independently-designed
 * patterns). Real part only (T_seq is real-valued in dumphfdl despite
 * being declared complex - every element's imaginary part is 0). Pick
 * HFDL_T_SEQ[bitmask] where bitmask is 0 or 1 - see
 * hfdl_framer_get_global_polarity()'s comment for how this project
 * derives that bit (dumphfdl: c->bitmask & 1, from c->bitmask being
 * set to 0 or ~0 based on A1's correlation sign - see
 * hfdl_equalizer.h's top comment's NOT YET DONE section, since
 * updated). */
extern const float32_t HFDL_T_SEQ[2][15]; /* 15 = HFDL_T_LEN, from hfdl_data_segment.h - literal
                                              here (not included) to keep this header self-
                                              contained, same as HFDL_A_LEN/HFDL_M1_LEN above */

#endif /* HFDL_PREAMBLE_SEQ_H */
