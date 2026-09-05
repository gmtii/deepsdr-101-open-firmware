#include "rtty_scope.h"
#include "rtty.h" /* RTTY_BLOCK_SAMPLES - the feed chunk size, see rtty_scope_feed() */

/*
 * See rtty_scope.h for the full "why a separate FFT module" story.
 * Radix-2 DIT FFT, same algorithm/structure as fft.c, deliberately
 * duplicated rather than shared (different size, different input
 * type, different consumer - see the header comment).
 *
 * Moved to TCM RAM 01/09/2026 - see fft.c's own comment on its
 * identically-shaped buffers for the full reasoning (freed main-RAM
 * headroom for the widened waterfall; none of these are DMA targets,
 * s_accum included - "ISR context" just means CPU code executing
 * inside an interrupt handler, still plain core loads/stores, not a
 * DMA descriptor).
 */
#define TCMRAM_BSS __attribute__((section(".tcmram")))

static float s_re[RTTY_SCOPE_FFT_SIZE] TCMRAM_BSS;
static float s_im[RTTY_SCOPE_FFT_SIZE] TCMRAM_BSS;
static float s_twiddle_cos[RTTY_SCOPE_FFT_SIZE / 2U] TCMRAM_BSS;
static float s_twiddle_sin[RTTY_SCOPE_FFT_SIZE / 2U] TCMRAM_BSS;
static float s_hann[RTTY_SCOPE_FFT_SIZE] TCMRAM_BSS;
static uint16_t s_bitrev[RTTY_SCOPE_FFT_SIZE] TCMRAM_BSS;

static float s_accum[RTTY_SCOPE_FFT_SIZE] TCMRAM_BSS; /* filled by rtty_scope_feed() (ISR context) */
static uint32_t s_accum_count;             /* samples currently in s_accum, ISR-owned */
static volatile uint8_t s_pending;         /* 1 = s_accum has a full window ready for rtty_scope_poll() */

static float s_mag[RTTY_SCOPE_BINS];       /* main-loop-owned output, written by rtty_scope_poll() */
static uint8_t s_frame_ready;

/* --- multi-window averaging accumulator, see RTTY_SCOPE_AVG_FRAMES's
 * comment in rtty_scope.h --- */
static float    s_mag_sum[RTTY_SCOPE_BINS];
static uint32_t s_avg_count; /* raw windows accumulated into s_mag_sum so far, 0..RTTY_SCOPE_AVG_FRAMES-1 between flushes */

/* Same Bhaskara-I approximation as fft.c/gd32_i2s.c, reimplemented
 * locally on purpose - see this module's "decoupled" reasoning in
 * rtty_scope.h. */
static float sinf_approx_local(float x)
{
    float pi = 3.14159265358979f;
    float x2;
    int negate = 0;
    while (x > pi) { x -= 2.0f * pi; }
    while (x < -pi) { x += 2.0f * pi; }
    if (x < 0.0f) { x = -x; negate = 1; }
    x2 = (16.0f * x * (pi - x)) / (5.0f * pi * pi - 4.0f * x * (pi - x));
    return negate ? -x2 : x2;
}

static float cosf_approx_local(float x)
{
    return sinf_approx_local(x + (3.14159265358979f / 2.0f));
}

void rtty_scope_init(void)
{
    uint32_t n;
    uint32_t bits = 0U;
    uint32_t size = RTTY_SCOPE_FFT_SIZE;

    while (size > 1U) { size >>= 1U; bits++; }

    for (n = 0; n < RTTY_SCOPE_FFT_SIZE / 2U; n++) {
        float angle = -2.0f * 3.14159265358979f * (float)n / (float)RTTY_SCOPE_FFT_SIZE;
        s_twiddle_cos[n] = cosf_approx_local(angle);
        s_twiddle_sin[n] = sinf_approx_local(angle);
    }

    for (n = 0; n < RTTY_SCOPE_FFT_SIZE; n++) {
        s_hann[n] = 0.5f - 0.5f * cosf_approx_local(
            2.0f * 3.14159265358979f * (float)n / (float)(RTTY_SCOPE_FFT_SIZE - 1U));
    }

    for (n = 0; n < RTTY_SCOPE_FFT_SIZE; n++) {
        uint32_t v = n;
        uint32_t r = 0U;
        uint32_t b;
        for (b = 0; b < bits; b++) {
            r = (r << 1U) | (v & 1U);
            v >>= 1U;
        }
        s_bitrev[n] = (uint16_t)r;
    }

    s_accum_count = 0U;
    s_pending = 0U;
    s_frame_ready = 0U;
    s_avg_count = 0U;
}

void rtty_scope_feed(const float *audio, uint32_t n)
{
    uint32_t i;

    if (s_pending) {
        return; /* previous window not yet consumed by rtty_scope_poll() - drop this
                  * one rather than overwrite s_accum out from under a possible
                  * in-progress read; the next window picks back up normally. */
    }
    for (i = 0; i < n && s_accum_count < RTTY_SCOPE_FFT_SIZE; i++) {
        s_accum[s_accum_count] = audio[i];
        s_accum_count++;
    }
    if (s_accum_count >= RTTY_SCOPE_FFT_SIZE) {
        s_pending = 1U;
        s_accum_count = 0U; /* next feed starts the NEXT window fresh */
    }
}

/* Same iterative radix-2 DIT butterfly pass as fft.c's fft_run() -
 * see that file for the annotated version, this is unchanged logic
 * just re-sized. */
static void scope_fft_run(void)
{
    uint32_t i, j;
    uint32_t half_size, step;

    for (half_size = 1U, step = RTTY_SCOPE_FFT_SIZE / 2U; half_size < RTTY_SCOPE_FFT_SIZE;
         half_size <<= 1U, step >>= 1U) {
        for (i = 0; i < RTTY_SCOPE_FFT_SIZE; i += (half_size << 1U)) {
            for (j = 0; j < half_size; j++) {
                uint32_t tw_idx = j * step;
                float tre = s_twiddle_cos[tw_idx];
                float tim = s_twiddle_sin[tw_idx];
                uint32_t a = i + j;
                uint32_t b = a + half_size;
                float br = s_re[b] * tre - s_im[b] * tim;
                float bi = s_re[b] * tim + s_im[b] * tre;
                s_re[b] = s_re[a] - br;
                s_im[b] = s_im[a] - bi;
                s_re[a] = s_re[a] + br;
                s_im[a] = s_im[a] + bi;
            }
        }
    }
}

void rtty_scope_poll(void)
{
    uint32_t n;
    float peak;

    if (!s_pending) {
        return;
    }

    for (n = 0; n < RTTY_SCOPE_FFT_SIZE; n++) {
        uint16_t src = s_bitrev[n];
        s_re[n] = s_accum[src] * s_hann[src];
        s_im[n] = 0.0f;
    }

    scope_fft_run();

    peak = 1.0e-12f; /* avoid a divide-by-zero if the block was silence */
    for (n = 0; n < RTTY_SCOPE_BINS; n++) {
        float power = s_re[n] * s_re[n] + s_im[n] * s_im[n];
        s_mag[n] = power;
        if (power > peak) {
            peak = power;
        }
    }
    /* Normalize to THIS WINDOW's own peak - see rtty_scope_get_frame()'s
     * comment: a tuning aid, not a calibrated strength readout, so it
     * should always use the full display range regardless of actual
     * signal level. Normalizing PER WINDOW (before averaging, not
     * after) keeps that same "always full range" property even once
     * several windows get blended below - a quiet window and a loud
     * window contribute comparably-shaped peaks to the average,
     * instead of the loud one silently dominating it. */
    for (n = 0; n < RTTY_SCOPE_BINS; n++) {
        s_mag[n] /= peak;
    }

    s_pending = 0U; /* rtty_scope_feed() can accept the next window again */

    /* --- multi-window averaging, see RTTY_SCOPE_AVG_FRAMES's comment
     * in rtty_scope.h --- */
    if (s_avg_count == 0U) {
        for (n = 0; n < RTTY_SCOPE_BINS; n++) {
            s_mag_sum[n] = s_mag[n];
        }
    } else {
        for (n = 0; n < RTTY_SCOPE_BINS; n++) {
            s_mag_sum[n] += s_mag[n];
        }
    }
    s_avg_count++;

    if (s_avg_count >= RTTY_SCOPE_AVG_FRAMES) {
        for (n = 0; n < RTTY_SCOPE_BINS; n++) {
            s_mag[n] = s_mag_sum[n] / (float)RTTY_SCOPE_AVG_FRAMES;
        }
        s_avg_count = 0U;
        s_frame_ready = 1U;
    }
    /* else: not enough raw windows yet - s_frame_ready stays as it
     * was (0, unless a previous ready frame is still waiting to be
     * collected by rtty_scope_get_frame() - see its own comment) and
     * the caller just sees "nothing new this poll", same as the
     * original !s_pending early-return above. */
}

uint8_t rtty_scope_frame_ready(void)
{
    return s_frame_ready;
}

const float *rtty_scope_get_frame(void)
{
    s_frame_ready = 0U;
    return s_mag;
}

float rtty_scope_hz_per_bin(void)
{
    return 12000.0f / (float)RTTY_SCOPE_FFT_SIZE;
}

void rtty_scope_avg_reset(void)
{
    s_avg_count = 0U;
}
