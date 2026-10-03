#include "hfdl_viterbi.h"
#include <string.h>

/* BRANCH METRIC TABLE - PRECOMPUTED (19/08/2026) at Python/build time
 * from libfec's own set_viterbi27_polynomial() (viterbi27_port.c):
 *   Branchtab27[b].c[state] = parity((2*state) & poly[b]) ? 255 : 0
 * for state 0..31, b in {0,1}, poly[0]=HFDL_VITERBI_POLYA=0x6d,
 * poly[1]=HFDL_VITERBI_POLYB=0x4f (both positive - libfec's `(polys[b]<0)`
 * sign-flip term is a no-op for this project's polynomials, confirmed
 * from fec.h's #define values, so it's correctly omitted here rather
 * than carried as dead code). Kept as `static const` (FLASH/.rodata,
 * NOT .bss) rather than computed at runtime the way libfec's own
 * lazy-Init pattern does - this project has zero RAM headroom (see
 * hfdl_viterbi.h's top comment) and these 64 bytes are fully
 * deterministic compile-time constants, no reason to burn either RAM
 * or a runtime init step on them. Verified by direct calculation, not
 * transcribed by eye - see this session's own working notes if the
 * polynomials ever change and this needs regenerating. */
static const uint8_t HFDL_VITERBI_BRANCHTAB_0[32] = {
    0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0,
    255, 255, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255, 255
};
static const uint8_t HFDL_VITERBI_BRANCHTAB_1[32] = {
    0, 255, 255, 0, 255, 0, 0, 255, 0, 255, 255, 0, 255, 0, 0, 255,
    0, 255, 255, 0, 255, 0, 0, 255, 0, 255, 255, 0, 255, 0, 0, 255
};

bool hfdl_viterbi_init(hfdl_viterbi_t *v,
        hfdl_viterbi_metric_t *metrics_a, hfdl_viterbi_metric_t *metrics_b,
        hfdl_viterbi_decision_t *decisions, uint32_t decisions_capacity,
        uint32_t starting_state)
{
    if (decisions_capacity < 1u) {
        return false; /* can't hold even one decoded bit's decision */
    }
    v->metrics_a = metrics_a;
    v->metrics_b = metrics_b;
    v->decisions = decisions;
    v->decisions_capacity = decisions_capacity;
    v->decisions_used = 0u;

    /* ZERO THE DECISIONS BUFFER (19/08/2026 - found via host testing,
     * NOT obvious from reading libfec's source alone). chainback()
     * below is a direct port of libfec's own chainback_viterbi27(),
     * which reads decisions[used_range_end .. used_range_end+5] - SIX
     * slots PAST what update_block() actually writes (see
     * hfdl_viterbi_chainback()'s own comment for the "+6 look past
     * tail" derivation). libfec's create_viterbi27() gets this for
     * free from calloc() (guaranteed-zero memory) - reading zero
     * there is not a happy accident, it's how a PROPERLY TERMINATED
     * convolutional code's known all-zero tail gets decoded correctly
     * without needing 6 extra real trellis steps. This project's
     * caller-provided buffers (see top comment's ZERO NEW PERSISTENT
     * RAM section - no malloc/calloc anywhere) do NOT get that
     * guarantee for free - a plain stack or borrowed-scratch buffer
     * can contain anything. Confirmed via host testing: WITHOUT this
     * memset, a 64-bit self-test message passed by pure chance
     * (stack happened to be zero there), but a 200-bit message with
     * the exact same code showed 3-6 mismatches that changed between
     * IDENTICAL repeated runs - a classic uninitialized-memory
     * symptom, not a logic bug in the butterfly/traceback math
     * itself. Zeroing here matches calloc()'s guarantee exactly and
     * costs decisions_capacity*sizeof(hfdl_viterbi_decision_t) bytes
     * of write time once per segment (negligible - worst real case
     * ~60KB, microseconds on this MCU) in exchange for removing this
     * entire bug class regardless of what the caller's buffer
     * (stack, borrowed waterfall scratch, whatever) happened to
     * contain before. */
    memset(decisions, 0, (size_t)decisions_capacity * sizeof(hfdl_viterbi_decision_t));

    /* Matches libfec's init_viterbi27(): all-states-equally-unlikely
     * (63, an arbitrary-but-large starting metric, same value libfec
     * uses - NOT 0, so the KNOWN starting_state's metric of 0 below
     * is a genuine, meaningful advantage over every other state right
     * from the first butterfly) except the known starting state,
     * biased to 0 (the "this path is the only one we KNOW is real"
     * anchor the whole decode's correctness depends on). */
    for (uint32_t i = 0u; i < HFDL_VITERBI_NUM_STATES; i++) {
        metrics_a->w[i] = 63u;
    }
    v->old_metrics = metrics_a;
    v->new_metrics = metrics_b;
    v->old_metrics->w[starting_state % HFDL_VITERBI_NUM_STATES] = 0u;
    return true;
}

/* Single butterfly update, one of 32 per decoded bit - direct port of
 * viterbi27_port.c's BFLY() macro (same variable names/shape
 * deliberately kept close to the original for line-by-line
 * comparison against the source of truth, not renamed to this
 * project's usual naming conventions - correctness-by-visible-
 * similarity matters more than style consistency for this one
 * function). `i` is the butterfly index 0..31, `d` is this bit's
 * decision_t slot, sym0/sym1 are this bit's two soft-decision input
 * bytes. */
static inline void hfdl_viterbi_bfly(hfdl_viterbi_t *v, uint32_t i,
        hfdl_viterbi_decision_t *d, uint8_t sym0, uint8_t sym1)
{
    uint32_t metric = (uint32_t)(HFDL_VITERBI_BRANCHTAB_0[i] ^ sym0)
                     + (uint32_t)(HFDL_VITERBI_BRANCHTAB_1[i] ^ sym1);
    uint32_t m0 = v->old_metrics->w[i] + metric;
    uint32_t m1 = v->old_metrics->w[i + 32u] + (510u - metric);
    uint32_t decision = (uint32_t)((int32_t)(m0 - m1) > 0);
    v->new_metrics->w[2u * i] = decision ? m1 : m0;
    d->w[i / 16u] |= decision << ((2u * i) & 31u);

    m0 -= (metric + metric - 510u);
    m1 += (metric + metric - 510u);
    decision = (uint32_t)((int32_t)(m0 - m1) > 0);
    v->new_metrics->w[2u * i + 1u] = decision ? m1 : m0;
    d->w[i / 16u] |= decision << ((2u * i + 1u) & 31u);
}

bool hfdl_viterbi_update_block(hfdl_viterbi_t *v, const uint8_t *syms, uint32_t nbits)
{
    if (nbits > v->decisions_capacity - v->decisions_used) {
        /* Would overrun the caller's buffer - refuse cleanly, touch
         * NOTHING (no partial write). See hfdl_viterbi.h's top
         * comment, M1=7/PSK8-double-slot case - this is exactly the
         * guard that turns that combination into a clean "can't
         * decode this one yet" rather than silent memory corruption. */
        return false;
    }
    hfdl_viterbi_decision_t *d = &v->decisions[v->decisions_used];
    for (uint32_t bit = 0u; bit < nbits; bit++) {
        d->w[0] = 0u;
        d->w[1] = 0u;
        uint8_t sym0 = syms[2u * bit];
        uint8_t sym1 = syms[2u * bit + 1u];

        for (uint32_t i = 0u; i < 32u; i++) {
            hfdl_viterbi_bfly(v, i, d, sym0, sym1);
        }

        d++;
        hfdl_viterbi_metric_t *tmp = v->old_metrics;
        v->old_metrics = v->new_metrics;
        v->new_metrics = tmp;
    }
    v->decisions_used += nbits;
    return true;
}

void hfdl_viterbi_chainback(const hfdl_viterbi_t *v, uint8_t *data_out, uint32_t nbits, uint32_t endstate)
{
    /* Direct port of viterbi27_port.c's chainback_viterbi27() - same
     * "walk backwards through the last nbits decisions, using the
     * known end state to pick which decision bit was actually taken
     * at each step" algorithm, same bit-packing order into data_out
     * (MSB-first per byte - see hfdl_viterbi.h's own note on why this
     * has to match exactly for hfdl_pdu.h's REVERSE_BYTE convention
     * downstream). Assumes nbits <= v->decisions_used (caller's
     * responsibility - this mirrors libfec's own lack of a bounds
     * check here, since by construction it only ever chainbacks
     * exactly what update_block() just wrote). */
    endstate = endstate % HFDL_VITERBI_NUM_STATES;
    endstate <<= 2u;

    /* decisions_used - nbits: chainback walks the LAST nbits
     * decisions written, not necessarily starting from index 0 (see
     * hfdl_viterbi.h's note that update_block() may be called more
     * than once) - libfec's own d+=6 "look past tail" offset is
     * relative to the START of the block being chained back, so it's
     * applied from THIS starting point, not from v->decisions[0]. */
    const hfdl_viterbi_decision_t *d = &v->decisions[v->decisions_used - nbits];
    d += 6u; /* look past tail, same margin HFDL_VITERBI_DECISION_BYTES() reserves */

    uint32_t n = nbits;
    while (n-- != 0u) {
        uint32_t k = (d[n].w[(endstate >> 2u) / 32u] >> ((endstate >> 2u) % 32u)) & 1u;
        data_out[n >> 3u] = (uint8_t)(endstate = (endstate >> 1u) | (k << 7u));
    }
}

/* Final path metric for a given trellis state, after the last
 * hfdl_viterbi_update_block() call - diagnostic only (added
 * 19/08/2026, see hfdl_payload_decode.c's own comment on why: several
 * real captures all showed crc_ok=0 with no way to tell "genuinely
 * noisy channel" from "clean decode, still somehow wrong" apart).
 * Divided by the number of bits decoded, this is a rough per-bit
 * confidence indicator - low means the decoder found a clean,
 * low-ambiguity path (pointing AWAY from noise as the explanation),
 * high means it didn't (pointing TOWARD noise, or a real tracking
 * problem upstream). Not a probability or a calibrated LLR sum, just
 * a relative signal to compare across attempts. Reads
 * v->old_metrics - see hfdl_viterbi_update_block()'s own swap logic:
 * after every completed bit (any parity), the most-recently-computed
 * metrics end up there, not in new_metrics. */
uint32_t hfdl_viterbi_get_final_metric(const hfdl_viterbi_t *v, uint32_t state)
{
    return v->old_metrics->w[state % HFDL_VITERBI_NUM_STATES];
}

/* ---- self-test ---- */

/* Tiny reference K=7 r=1/2 convolutional ENCODER, self-test-only (see
 * hfdl_viterbi.h - this decoder never needs to encode for real). Same
 * polynomials/shift-register convention as the decoder above:
 * output0 = parity(reg & POLYA), output1 = parity(reg & POLYB), where
 * `reg` is the 7-bit window (current input bit in bit 6, five
 * previous bits below it, matching the decoder's `2*state` alignment
 * - see HFDL_VITERBI_BRANCHTAB_0/1's own derivation comment). Encoder
 * state (6 bits) starts at 0 and is NOT flushed to 0 at the end by
 * this function itself - the self-test appends HFDL_VITERBI_K-1
 * explicit zero tail bits to the input before encoding, the standard
 * way a real terminated convolutional code is built, matching
 * dumphfdl's own init_viterbi27(v,0)/chainback(...,0) known-start/
 * known-end assumption. */
static uint32_t hfdl_viterbi_test_parity32(uint32_t x)
{
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1u;
}

static void hfdl_viterbi_test_encode(const uint8_t *bits_in, uint32_t nbits_in, uint8_t *soft_out)
{
    uint32_t reg = 0u; /* 6-bit shift register, LSB = most recent bit */
    for (uint32_t i = 0u; i < nbits_in; i++) {
        uint32_t reg7 = (reg << 1) | bits_in[i]; /* align with decoder's "2*state | input" convention */
        uint32_t out0 = hfdl_viterbi_test_parity32(reg7 & HFDL_VITERBI_POLYA);
        uint32_t out1 = hfdl_viterbi_test_parity32(reg7 & HFDL_VITERBI_POLYB);
        soft_out[2u * i]      = out0 ? 255u : 0u;
        soft_out[2u * i + 1u] = out1 ? 255u : 0u;
        reg = reg7 & 0x3Fu; /* keep 6 bits for next state */
    }
}

static uint32_t xorshift32_vt(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return x;
}

bool hfdl_viterbi_selftest(void)
{
    #define VT_TEST_MSG_BITS 64u
    #define VT_TEST_TOTAL_BITS (VT_TEST_MSG_BITS + (HFDL_VITERBI_K - 1u)) /* + zero-flush tail */

    uint8_t bits_in[VT_TEST_TOTAL_BITS];
    uint32_t rng = 0xFEEDFACEu;
    for (uint32_t i = 0u; i < VT_TEST_MSG_BITS; i++) {
        bits_in[i] = (uint8_t)(xorshift32_vt(&rng) & 1u);
    }
    for (uint32_t i = VT_TEST_MSG_BITS; i < VT_TEST_TOTAL_BITS; i++) {
        bits_in[i] = 0u; /* flush to state 0, matching dumphfdl's known-endstate=0 assumption */
    }

    uint8_t soft[2u * VT_TEST_TOTAL_BITS];
    hfdl_viterbi_test_encode(bits_in, VT_TEST_TOTAL_BITS, soft);

    hfdl_viterbi_metric_t ma, mb;
    hfdl_viterbi_decision_t decisions[VT_TEST_TOTAL_BITS + 6u];
    uint8_t data_out[(VT_TEST_TOTAL_BITS + 7u) / 8u];

    /* CHECK 1: clean channel, no errors - decoder must reproduce the
     * exact input bits (including the zero tail). */
    hfdl_viterbi_t v;
    if (!hfdl_viterbi_init(&v, &ma, &mb, decisions, VT_TEST_TOTAL_BITS + 6u, 0u)) {
        return false;
    }
    if (!hfdl_viterbi_update_block(&v, soft, VT_TEST_TOTAL_BITS)) {
        return false;
    }
    hfdl_viterbi_chainback(&v, data_out, VT_TEST_TOTAL_BITS, 0u);
    for (uint32_t i = 0u; i < VT_TEST_TOTAL_BITS; i++) {
        uint32_t got = (data_out[i >> 3u] >> (7u - (i & 7u))) & 1u;
        if (got != bits_in[i]) {
            return false; /* clean-channel mismatch - decoder itself is broken */
        }
    }

    /* CHECK 2: same message, but with real bit errors injected -
     * confirms this is actually CORRECTING errors, not just passing
     * clean data through unchanged (a bug that inverts nothing could
     * still pass CHECK 1 by accident if the test message happened to
     * be its own fixed point - vanishingly unlikely at 64 bits, but
     * this check removes the doubt entirely). Flip a handful of soft
     * bits hard (0<->255) and nudge a few more only partway (still
     * technically "wrong" but less confidently so) - K=7 r=1/2 should
     * comfortably correct a handful of errors spread across 71 coded
     * bits. */
    uint8_t soft_noisy[2u * VT_TEST_TOTAL_BITS];
    memcpy(soft_noisy, soft, sizeof(soft_noisy));
    uint32_t err_positions[] = {3u, 17u, 40u, 90u, 130u};
    for (uint32_t k = 0u; k < sizeof(err_positions) / sizeof(err_positions[0]); k++) {
        uint32_t p = err_positions[k] % (2u * VT_TEST_TOTAL_BITS);
        soft_noisy[p] = (uint8_t)(255u - soft_noisy[p]); /* hard flip */
    }
    soft_noisy[7] = 140u;  /* partial/ambiguous soft value, not a hard flip */
    soft_noisy[55] = 120u;

    hfdl_viterbi_metric_t ma2, mb2;
    hfdl_viterbi_decision_t decisions2[VT_TEST_TOTAL_BITS + 6u];
    uint8_t data_out2[(VT_TEST_TOTAL_BITS + 7u) / 8u];
    hfdl_viterbi_t v2;
    if (!hfdl_viterbi_init(&v2, &ma2, &mb2, decisions2, VT_TEST_TOTAL_BITS + 6u, 0u)) {
        return false;
    }
    if (!hfdl_viterbi_update_block(&v2, soft_noisy, VT_TEST_TOTAL_BITS)) {
        return false;
    }
    hfdl_viterbi_chainback(&v2, data_out2, VT_TEST_TOTAL_BITS, 0u);
    for (uint32_t i = 0u; i < VT_TEST_TOTAL_BITS; i++) {
        uint32_t got = (data_out2[i >> 3u] >> (7u - (i & 7u))) & 1u;
        if (got != bits_in[i]) {
            return false; /* failed to correct a realistic error pattern */
        }
    }

    /* CHECK 3: capacity guard - a block that's deliberately too big
     * for the buffer must be refused cleanly (see hfdl_viterbi.h's
     * M1=7/PSK8-double-slot case), not silently overrun it. */
    hfdl_viterbi_metric_t ma3, mb3;
    hfdl_viterbi_decision_t decisions3[4]; /* deliberately tiny */
    hfdl_viterbi_t v3;
    if (!hfdl_viterbi_init(&v3, &ma3, &mb3, decisions3, 4u, 0u)) {
        return false;
    }
    if (hfdl_viterbi_update_block(&v3, soft, VT_TEST_TOTAL_BITS)) {
        return false; /* should have been refused - VT_TEST_TOTAL_BITS > 4 */
    }

    return true;

    #undef VT_TEST_MSG_BITS
    #undef VT_TEST_TOTAL_BITS
}
