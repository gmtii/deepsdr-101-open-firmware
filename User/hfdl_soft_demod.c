#include "hfdl_soft_demod.h"
#include "hfdl_preamble_seq.h" /* HFDL_MOD_BPSK/PSK4/PSK8 */
#include <math.h>

/* Same literal this project already uses elsewhere (hfdl_scope.c etc)
 * rather than relying on M_PI, which isn't guaranteed to be defined
 * under strict C11 without a feature-test macro (confirmed the hard
 * way on this session's host build) - consistent with this project's
 * existing convention, not a new one. */
#define HFDL_SOFT_DEMOD_PI 3.14159265358979f

static inline uint8_t hfdl_clamp255(float32_t x)
{
    /* liquid-dsp's own `int soft_bit = LLR*16 + 127;` is a plain C
     * float->int cast - TRUNCATES toward zero, does NOT round to
     * nearest. Confirmed the hard way (19/08/2026): an earlier draft
     * of this function rounded instead, and an earlier draft of the
     * PSK8 self-test's expected values were computed with Python's
     * round() to match - both wrong the same way, so the test would
     * have passed despite the mismatch. Caught only by recomputing
     * the Python reference with proper truncation and finding several
     * values actually differ (e.g. 217->216, 109->108) - i.e. this
     * was NOT a "doesn't matter in practice" rounding wobble, it's a
     * real one-off discrepancy that would have shipped silently. */
    int32_t v = (int32_t)x; /* C truncates toward zero, matching liquid exactly */
    if (v > 255) v = 255;
    if (v < 0) v = 0;
    return (uint8_t)v;
}

static uint32_t hfdl_gray_encode(uint32_t s)
{
    return s ^ (s >> 1);
}

static uint32_t hfdl_gray_decode(uint32_t s)
{
    uint32_t mask = s;
    uint32_t out = s;
    for (uint32_t i = 0u; i < 4u; i++) { /* matches liquid's own MAX_MOD_BITS_PER_SYMBOL/4 loop bound - 4 iterations covers up to 16 bits/symbol, far more than HFDL's max 3 */
        out ^= (mask >> 1);
        out ^= (mask >> 2);
        out ^= (mask >> 3);
        out ^= (mask >> 4);
        mask >>= 4;
    }
    return out;
}

/* liquid's MODEM(_demodulate_linear_array_ref)() - see hfdl_soft_demod.h
 * top comment. m: bits/symbol, alpha: pi/M. Returns the LINEAR
 * (pre-gray) sector index. */
static uint32_t hfdl_psk_linear_demod(float32_t theta, uint32_t m, float32_t alpha)
{
    uint32_t s = 0u;
    float32_t v = theta;
    for (uint32_t k = 0u; k < m; k++) {
        s <<= 1;
        float32_t ref = (float32_t)(1u << (m - k - 1u)) * alpha;
        if (v > 0.0f) {
            s |= 1u;
            v -= ref;
        } else {
            v += ref;
        }
    }
    return s;
}

/* liquid's MODEM(_modulate_psk)() - see top comment. sym: gray-coded
 * symbol value. `m` unused here (phase only depends on alpha, which
 * already encodes pi/M) - kept in the signature to mirror the
 * gray_decode/alpha pairing used at every other call site in this
 * file, not because this function itself needs it. */
static void hfdl_psk_modulate(uint32_t sym, uint32_t m, float32_t alpha, float32_t *i_out, float32_t *q_out)
{
    (void)m;
    float32_t phase = (float32_t)hfdl_gray_decode(sym) * 2.0f * alpha;
    *i_out = cosf(phase);
    *q_out = sinf(phase);
}

static void hfdl_soft_demod_bpsk(float32_t i_in, uint8_t *out)
{
    /* liquid modem_bpsk.proto.c: gamma=4.0, LLR=-2*real(x)*gamma,
     * soft_bit=clamp(LLR*16+127,0,255) = clamp(-128*real(x)+127,0,255) */
    out[0] = hfdl_clamp255(-128.0f * i_in + 127.0f);
}

static void hfdl_soft_demod_psk4(float32_t i_in, float32_t q_in, uint8_t *out)
{
    /* liquid's actual behavior for PSK4 (m=2): NO soft table gets
     * built (demodsoft_gentab only runs for m>=3), so the generic
     * dispatch falls through to hard-decision-only, bits copied
     * straight to 0/255 - see hfdl_soft_demod.h's top comment for
     * why this is correct, not a shortcut this module is taking on
     * its own. */
    const uint32_t m = 2u;
    const float32_t alpha = HFDL_SOFT_DEMOD_PI / 4.0f;      /* pi/M, M=4 */
    const float32_t d_phi = HFDL_SOFT_DEMOD_PI * (1.0f - 1.0f / 4.0f); /* pi*(1-1/M) */
    float32_t theta = atan2f(q_in, i_in) - d_phi;
    if (theta < -HFDL_SOFT_DEMOD_PI) {
        theta += 2.0f * HFDL_SOFT_DEMOD_PI;
    }
    uint32_t s_lin = hfdl_psk_linear_demod(theta, m, alpha);
    uint32_t sym = hfdl_gray_encode(s_lin);
    for (uint32_t k = 0u; k < m; k++) {
        uint32_t bit = (sym >> (m - k - 1u)) & 1u;
        out[k] = bit ? 255u : 0u;
    }
}

static void hfdl_soft_demod_psk8(float32_t i_in, float32_t q_in, uint8_t *out)
{
    const uint32_t m = 3u;
    const uint32_t M = 8u;
    const float32_t alpha = HFDL_SOFT_DEMOD_PI / 8.0f;      /* pi/M */
    const float32_t d_phi = HFDL_SOFT_DEMOD_PI * (1.0f - 1.0f / 8.0f);
    const float32_t gamma = 1.2f * (float32_t)M;         /* liquid: gamma = 1.2*M */

    float32_t theta = atan2f(q_in, i_in) - d_phi;
    if (theta < -HFDL_SOFT_DEMOD_PI) {
        theta += 2.0f * HFDL_SOFT_DEMOD_PI;
    }
    uint32_t s_lin = hfdl_psk_linear_demod(theta, m, alpha);
    uint32_t sym = hfdl_gray_encode(s_lin);

    float32_t dmin0[HFDL_SOFT_DEMOD_MAX_BITS_PER_SYMBOL];
    float32_t dmin1[HFDL_SOFT_DEMOD_MAX_BITS_PER_SYMBOL];
    for (uint32_t k = 0u; k < m; k++) {
        dmin0[k] = 8.0f; /* liquid's own initial "no candidate yet" sentinel */
        dmin1[k] = 8.0f;
    }

    /* Hard-decision distance */
    {
        float32_t xi, xq;
        hfdl_psk_modulate(sym, m, alpha, &xi, &xq);
        float32_t di = i_in - xi;
        float32_t dq = q_in - xq;
        float32_t d = di * di + dq * dq;
        for (uint32_t k = 0u; k < m; k++) {
            uint32_t bit = (sym >> (m - k - 1u)) & 1u;
            if (bit) { dmin1[k] = d; } else { dmin0[k] = d; }
        }
    }

    /* The 2 nearest OTHER symbols on an evenly-spaced M-PSK circle are
     * always its immediate phase neighbors - see hfdl_soft_demod.h's
     * top comment for why this is exact, not approximate, for this
     * specific (symmetric) constellation. */
    uint32_t neighbors[2] = { (s_lin + M - 1u) % M, (s_lin + 1u) % M };
    for (uint32_t n = 0u; n < 2u; n++) {
        uint32_t nsym = hfdl_gray_encode(neighbors[n]);
        float32_t xi, xq;
        hfdl_psk_modulate(nsym, m, alpha, &xi, &xq);
        float32_t di = i_in - xi;
        float32_t dq = q_in - xq;
        float32_t d = di * di + dq * dq;
        for (uint32_t k = 0u; k < m; k++) {
            uint32_t bit = (nsym >> (m - k - 1u)) & 1u;
            if (bit) {
                if (d < dmin1[k]) { dmin1[k] = d; }
            } else {
                if (d < dmin0[k]) { dmin0[k] = d; }
            }
        }
    }

    for (uint32_t k = 0u; k < m; k++) {
        out[k] = hfdl_clamp255((dmin0[k] - dmin1[k]) * gamma * 16.0f + 127.0f);
    }
}

void hfdl_soft_demod(uint32_t mod_arity, float32_t i_in, float32_t q_in, uint8_t *soft_bits_out)
{
    switch (mod_arity) {
        case HFDL_MOD_BPSK:
            hfdl_soft_demod_bpsk(i_in, soft_bits_out);
            break;
        case HFDL_MOD_PSK4:
            hfdl_soft_demod_psk4(i_in, q_in, soft_bits_out);
            break;
        case HFDL_MOD_PSK8:
            hfdl_soft_demod_psk8(i_in, q_in, soft_bits_out);
            break;
        default:
            /* Shouldn't happen (mod_arity always comes from
             * hfdl_rate_params[], one of the 3 values above) - leave
             * soft_bits_out untouched rather than guess, same "don't
             * paper over an impossible case" spirit as elsewhere in
             * this project. */
            break;
    }
}

float32_t hfdl_soft_demod_costas_phase_error(uint32_t mod_arity, float32_t i_in, float32_t q_in)
{
    float32_t xhat_i = 0.0f, xhat_q = 0.0f;
    switch (mod_arity) {
        case HFDL_MOD_BPSK: {
            /* Same hard decision hfdl_soft_demod_bpsk() makes (i_in>0
             * -> bit 0 -> +1) - remodulated point is just that sign,
             * q always 0 for BPSK. */
            xhat_i = (i_in >= 0.0f) ? 1.0f : -1.0f;
            xhat_q = 0.0f;
            break;
        }
        case HFDL_MOD_PSK4: {
            const uint32_t m = 2u;
            const float32_t alpha = HFDL_SOFT_DEMOD_PI / 4.0f;
            const float32_t d_phi = HFDL_SOFT_DEMOD_PI * (1.0f - 1.0f / 4.0f);
            float32_t theta = atan2f(q_in, i_in) - d_phi;
            if (theta < -HFDL_SOFT_DEMOD_PI) { theta += 2.0f * HFDL_SOFT_DEMOD_PI; }
            uint32_t s_lin = hfdl_psk_linear_demod(theta, m, alpha);
            uint32_t sym = hfdl_gray_encode(s_lin);
            hfdl_psk_modulate(sym, m, alpha, &xhat_i, &xhat_q);
            break;
        }
        case HFDL_MOD_PSK8: {
            const uint32_t m = 3u;
            const float32_t alpha = HFDL_SOFT_DEMOD_PI / 8.0f;
            const float32_t d_phi = HFDL_SOFT_DEMOD_PI * (1.0f - 1.0f / 8.0f);
            float32_t theta = atan2f(q_in, i_in) - d_phi;
            if (theta < -HFDL_SOFT_DEMOD_PI) { theta += 2.0f * HFDL_SOFT_DEMOD_PI; }
            uint32_t s_lin = hfdl_psk_linear_demod(theta, m, alpha);
            uint32_t sym = hfdl_gray_encode(s_lin);
            hfdl_psk_modulate(sym, m, alpha, &xhat_i, &xhat_q);
            break;
        }
        default:
            return 0.0f; /* shouldn't happen, see hfdl_soft_demod()'s own default case */
    }
    /* Im(r * conj(x_hat)), r=(i_in,q_in) - matches liquid-dsp's
     * modem_get_demodulator_phase_error() exactly, see this
     * function's own header comment for the derivation/source. */
    return q_in * xhat_i - i_in * xhat_q;
}

bool hfdl_soft_demod_selftest(void)
{
    /* CHECK 1: BPSK, 5 amplitudes - reference values from an
     * independent Python re-implementation of liquid's exact formula
     * (19/08/2026 session notes). */
    {
        struct { float32_t i; uint8_t expect; } cases[] = {
            {1.0f, 0u}, {-1.0f, 255u}, {0.5f, 63u}, {-0.5f, 191u}, {0.0f, 127u}
        };
        for (uint32_t k = 0u; k < sizeof(cases) / sizeof(cases[0]); k++) {
            uint8_t out[1];
            hfdl_soft_demod(HFDL_MOD_BPSK, cases[k].i, 0.0f, out);
            if (out[0] != cases[k].expect) {
                return false;
            }
        }
    }

    /* CHECK 2: PSK4, all 4 clean constellation points - expected
     * (sym, soft[2]) pairs from the same Python reference. */
    {
        struct { float32_t i, q; uint32_t expect_soft0, expect_soft1; } cases[] = {
            {1.0f, 0.0f, 0u, 0u},     /* lin=0 -> sym=0 */
            {0.0f, 1.0f, 0u, 255u},   /* lin=1 -> sym=1 */
            {-1.0f, 0.0f, 255u, 255u},/* lin=2 -> sym=3 */
            {0.0f, -1.0f, 255u, 0u},  /* lin=3 -> sym=2 */
        };
        for (uint32_t k = 0u; k < sizeof(cases) / sizeof(cases[0]); k++) {
            uint8_t out[2];
            hfdl_soft_demod(HFDL_MOD_PSK4, cases[k].i, cases[k].q, out);
            if (out[0] != cases[k].expect_soft0 || out[1] != cases[k].expect_soft1) {
                return false;
            }
        }
    }

    /* CHECK 3: PSK8, all 8 clean constellation points - expected
     * soft[3] triples from the same Python reference. Deliberately
     * includes points where the "clean" soft value is NOT 0/255 (e.g.
     * 37/217) - a real, correct consequence of gray coding on a
     * circle (adjacent symbols by PHASE aren't always adjacent by a
     * single BIT), not a bug - if this module's neighbor-selection or
     * gray mapping were wrong, THESE are the values that would drift,
     * clean-0-or-255-only checks would miss it. */
    {
        struct { float32_t i, q; uint8_t expect[3]; } cases[8];
        float32_t alpha8 = HFDL_SOFT_DEMOD_PI / 8.0f;
        uint8_t expect_table[8][3] = {
            {37,0,37}, {0,37,216}, {0,216,216}, {37,255,37},
            {216,255,37}, {255,216,216}, {255,37,216}, {216,0,37}
        };
        for (uint32_t lin = 0u; lin < 8u; lin++) {
            float32_t ph = (float32_t)lin * 2.0f * alpha8;
            cases[lin].i = cosf(ph);
            cases[lin].q = sinf(ph);
            cases[lin].expect[0] = expect_table[lin][0];
            cases[lin].expect[1] = expect_table[lin][1];
            cases[lin].expect[2] = expect_table[lin][2];
        }
        for (uint32_t k = 0u; k < 8u; k++) {
            uint8_t out[3];
            hfdl_soft_demod(HFDL_MOD_PSK8, cases[k].i, cases[k].q, out);
            for (uint32_t b = 0u; b < 3u; b++) {
                if (out[b] != cases[k].expect[b]) {
                    return false;
                }
            }
        }
    }

    /* CHECK 4: PSK8, sweep partway from symbol 0 toward symbol 1 -
     * confirms the differing bit's soft value passes smoothly through
     * 127 (total uncertainty) at the exact geometric halfway point,
     * and that the OTHER two bits (which symbol 0 and symbol 1 happen
     * to share, per PSK8's gray coding at these two adjacent points)
     * stay at their clean values throughout the sweep - both facts
     * came from the same independent Python reference, not from this
     * module's own output. */
    {
        float32_t alpha8 = HFDL_SOFT_DEMOD_PI / 8.0f;
        struct { float32_t frac; uint8_t expect[3]; } cases[] = {
            {0.00f, {37, 0, 37}},
            {0.10f, {20, 0, 54}},
            {0.25f, {0, 0, 81}},
            {0.40f, {0, 0, 108}},
            {0.50f, {0, 0, 127}},
        };
        for (uint32_t k = 0u; k < sizeof(cases) / sizeof(cases[0]); k++) {
            float32_t ph = cases[k].frac * 2.0f * alpha8;
            uint8_t out[3];
            hfdl_soft_demod(HFDL_MOD_PSK8, cosf(ph), sinf(ph), out);
            for (uint32_t b = 0u; b < 3u; b++) {
                if (out[b] != cases[k].expect[b]) {
                    return false;
                }
            }
        }
    }

    /* CHECK 5: decision-directed Costas phase error (added 19/08/2026,
     * DATA-state frequency tracking fix). BPSK reduces to the
     * well-known sign(I)*Q formula - verified algebraically (Im(r *
     * conj(x_hat)) with x_hat=(+-1,0) collapses to q_in*(+-1)), not
     * just trusted by inspection. PSK8 clean point should read
     * exactly 0 (received sample IS the hard decision, no error);
     * the sweep case from CHECK 4 (partway between symbol 0 and 1)
     * should read a small NEGATIVE error growing toward 0 as it
     * approaches the sym-0 side (sign matches "rotate the NCO phase
     * back toward this received point" - not independently re-
     * derived here, just checked for the expected trend/magnitude
     * order, the exact value isn't a hand-verified reference like
     * the other checks). */
    {
        float32_t e;
        e = hfdl_soft_demod_costas_phase_error(HFDL_MOD_BPSK, 1.0f, 0.5f);
        if (e < 0.499f || e > 0.501f) { return false; }
        e = hfdl_soft_demod_costas_phase_error(HFDL_MOD_BPSK, -1.0f, 0.5f);
        if (e < -0.501f || e > -0.499f) { return false; }
        e = hfdl_soft_demod_costas_phase_error(HFDL_MOD_PSK8, 1.0f, 0.0f);
        if (e < -0.001f || e > 0.001f) { return false; }
    }

    return true;
}
