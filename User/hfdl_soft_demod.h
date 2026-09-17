#ifndef HFDL_SOFT_DEMOD_H
#define HFDL_SOFT_DEMOD_H

#include "arm_math.h"
#include <stdint.h>
#include <stdbool.h>

/*
 * hfdl_soft_demod - per-symbol soft-bit demodulation for HFDL's 3 real
 * modulation schemes (BPSK/PSK4/PSK8), the piece decode_user_data()
 * needs between the equalizer and the deinterleaver (19/08/2026,
 * "integración completa" round).
 *
 * SOURCE OF TRUTH - dumphfdl calls liquid-dsp's modem_demodulate_soft()
 * directly (src/hfdl.c decode_user_data(), line ~1013) rather than
 * implementing its own soft-bit math, so THIS module ports the actual
 * liquid-dsp algorithm (jgaeddert/liquid-dsp, MIT license) it calls
 * into, confirmed by downloading and reading that repo directly this
 * session, not assumed from liquid's own public docs:
 *   - modem_bpsk.proto.c's MODEM(_demodulate_soft_bpsk)(): BPSK's own
 *     dedicated formula (real-axis-only, gamma=4.0 fixed constant).
 *   - modem_psk.proto.c's MODEM(_create_psk)(): confirms hfdl.c's
 *     M_PSK4/M_PSK8 use LIQUID_MODEM_PSK4/PSK8 (the GENERIC M-ary PSK
 *     type), NOT LIQUID_MODEM_QPSK (a DIFFERENT, 45-degree-rotated
 *     4-point constellation liquid-dsp also provides - easy to
 *     conflate, confirmed the actual modem_create() call site in
 *     hfdl.c line ~499 to be sure which one HFDL actually uses).
 *   - modem_common.proto.c's MODEM(_demodulate_soft)() dispatch table:
 *     PSK4 (m=2 bits/symbol) does NOT get a soft-bit lookup table
 *     built at all (MODEM(_demodsoft_gentab)() is only called when
 *     q->m >= 3, see modem_psk.proto.c's create function) - so PSK4
 *     falls through to the GENERIC fallback path, which is HARD
 *     decision only, soft bits just copied to LIQUID_SOFTBIT_0/1 (0
 *     or 255, no actual soft/LLR information at all). This is a real,
 *     easy-to-miss finding - naively assuming PSK4 gets "some" soft
 *     information (like BPSK/PSK8 do) would be wrong.
 *   - modem_common.proto.c's MODEM(_demodulate_soft_table)(): PSK8
 *     (m=3) DOES get real soft bits, via a p=2-nearest-neighbor
 *     Euclidean-distance LLR approximation (gamma=1.2*M). This
 *     module's PSK8 path computes the SAME result directly (the 2
 *     nearest neighbors of any point on an evenly-spaced M-PSK circle
 *     are ALWAYS its immediate phase-adjacent points by simple
 *     geometry - confirmed, not assumed, by reading
 *     MODEM(_demodsoft_gentab)()'s actual nearest-neighbor search;
 *     replicating its RESULT via direct adjacency is exact, not an
 *     approximation of the algorithm, just a closed-form shortcut
 *     for what its O(M^2) search would find for THIS specific,
 *     symmetric constellation).
 *   - modem_utilities.c's gray_encode()/gray_decode(): exact bit
 *     patterns HFDL's PSK4/PSK8 symbol-to-phase mapping depends on.
 *
 * VALIDATED (19/08/2026) against an independent Python re-
 * implementation of the exact same formulas above (not against this
 * module's own logic circularly) - hfdl_soft_demod_selftest() checks
 * against those Python-computed reference values, same "cross-check
 * against an independent implementation" discipline this project's
 * other Phase 2 modules (descrambler, deinterleaver) already used.
 *
 * WHAT THIS DOES NOT DO: BPSK/PSK4/PSK8 hard-decision demodulation for
 * the PREAMBLE (A1/A2/M1 correlation, symbol timing) - that's
 * hfdl_framer.c's own concern, unrelated and already built/validated.
 * This module is ONLY for post-LOCKED, post-equalizer DATA_1/DATA_2
 * symbols feeding the deinterleaver.
 */

/* Maximum soft bits any one HFDL symbol can produce (PSK8 = 3
 * bits/symbol, HFDL's largest real scheme) - sizes the caller's
 * output array, matches HFDL_MOD_PSK8 in hfdl_preamble_seq.h. */
#define HFDL_SOFT_DEMOD_MAX_BITS_PER_SYMBOL 3u

/* Demodulates one equalized, Costas-corrected complex data symbol
 * into `mod_arity` soft bits (HFDL_MOD_BPSK=1/PSK4=2/PSK8=3, see
 * hfdl_preamble_seq.h - same field hfdl_data_segment_t.mod_arity and
 * hfdl_rate_params_t.scheme_bits already track, deliberately NOT a
 * separate enum to avoid a 4th name for the same 3 values). Each
 * output bit is MSB-first within the symbol (soft_bits_out[0] is the
 * symbol's most-significant coded bit) - the SAME order
 * hfdl_deinterleaver_push() must receive them in, one push() call per
 * bit, in this order, immediately after this call (see
 * hfdl_payload_decode.c for the actual wiring). Each byte is
 * LIQUID_SOFTBIT_0(0)..LIQUID_SOFTBIT_1(255) - 0 means "confidently a
 * 0 bit", 255 means "confidently a 1 bit", matching hfdl_viterbi.h's
 * own documented convention exactly (no conversion needed between
 * this module's output and that one's input). i_in/q_in are expected
 * on the SAME roughly-unit-RMS scale hfdl_equalizer_execute() already
 * produces (T_seq-relative, ±1.0-ish) - this module does no
 * additional normalization of its own, same "normalize once, upstream"
 * discipline hfdl_equalizer.c's own history already established. */
void hfdl_soft_demod(uint32_t mod_arity, float32_t i_in, float32_t q_in, uint8_t *soft_bits_out);

/* Decision-directed Costas phase-error, matching liquid-dsp's own
 * modem_get_demodulator_phase_error() EXACTLY (confirmed from source,
 * 19/08/2026 - modem_common.proto.c: `return cimagf(_q->r*conjf(_q->x_hat));`)
 * - the SAME formula dumphfdl feeds into costas_cccf_adjust() on
 * EVERY symbol from EQ_TRAIN through the end of DATA_1/DATA_2
 * (src/hfdl.c's main per-symbol loop, confirmed this session by
 * downloading and reading dumphfdl's actual source - NOT scoped to
 * EQ_TRAIN only there, unlike this project's own
 * hfdl_costas_adjust() call site, which this project's own prior
 * session deliberately deferred general M-PSK decision-directed
 * correction on (see hfdl_costas.c's own comment at that call site)
 * pending exactly this kind of validated piece).
 *
 * `i_in`/`q_in`: the equalized symbol (same input hfdl_soft_demod()
 * takes). Returns Im(r * conj(x_hat)) where r=(i_in,q_in) and x_hat
 * is the nearest constellation point for `mod_arity` - i.e. re-runs
 * the SAME hard-decision this module's soft-bit path already
 * computes internally, remodulates it, and measures how far off in
 * phase the received sample is from that clean point. Intended to be
 * fed straight into hfdl_costas_adjust() (same units/sign convention
 * - both this project's and dumphfdl's own Costas adjust() just scale
 * this value by alpha/beta, no conversion needed). */
float32_t hfdl_soft_demod_costas_phase_error(uint32_t mod_arity, float32_t i_in, float32_t q_in);

/* Host-testable self-test, cross-checked against an independent
 * Python re-implementation of liquid-dsp's actual algorithm (see top
 * comment) - covers all 8 PSK8 constellation points, all 4 PSK4
 * points, 5 BPSK amplitudes, and a PSK8 "partway between two
 * symbols" sweep (confirms soft bits move smoothly through 127 at the
 * exact halfway point, not just correct at clean constellation
 * points - a implementation that only got the HARD decision right but
 * botched the neighbor-distance soft math could still pass a clean-
 * points-only test, this sweep specifically guards against that).
 * Returns true if every check matches its expected value exactly. */
bool hfdl_soft_demod_selftest(void);

#endif /* HFDL_SOFT_DEMOD_H */
