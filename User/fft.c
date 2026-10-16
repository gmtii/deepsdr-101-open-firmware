#include "fft.h"

/*
 * RAM BUDGET: re/im (256*4*2=2048B) + cos/sin twiddles (128*4*2=1024B)
 * + Hann window (256*4=1024B) + bit-reversal table (256*2=512B) =
 * ~4.6KB static (was ~2.3KB @ FFT_SIZE=128/48kHz, ~9.2KB @
 * FFT_SIZE=512/192kHz before that - see sdr_rx.h's
 * SDR_RX_BLOCK_SAMPLES comment).
 *
 * *** 01/09/2026: moved to TCM RAM *** - per the project owner,
 * widening the spectrum/waterfall panel to the full screen width
 * needed main-RAM headroom the waterfall's own enlarged buffer
 * couldn't spare (see waterfall.h's own comment). None of these six
 * buffers is ever touched by DMA - every one is written and read
 * purely by CPU-executed code within this file's own functions (the
 * FFT computation itself, and the one-time init that fills the
 * twiddle/Hann/bit-reversal tables) - so TCM (data-bus-only, no DMA/
 * EXMC access - see GD32F450VE_FLASH.ld's own comment) is a safe,
 * genuine free-lunch home for them: the 64KB TCM region sat
 * completely unused before this change (confirmed via `nm`,
 * `.tcmram` section size was 0 bytes), while these six buffers
 * together are a meaningful chunk of what widening the waterfall
 * needed freed from main RAM.
 */
#define TCMRAM_BSS __attribute__((section(".tcmram")))

static float s_re[FFT_SIZE] TCMRAM_BSS;
static float s_im[FFT_SIZE] TCMRAM_BSS;
static float s_twiddle_cos[FFT_SIZE / 2U] TCMRAM_BSS;
static float s_twiddle_sin[FFT_SIZE / 2U] TCMRAM_BSS;
static float s_hann[FFT_SIZE] TCMRAM_BSS;
static uint16_t s_bitrev[FFT_SIZE] TCMRAM_BSS;

/* Sine approximation with no libm dependency - same method (Bhaskara
 * I) as gd32_i2s.c, reimplemented locally to keep the two modules
 * decoupled. */
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

/* Approximate log2(x) via IEEE754 bit reinterpretation (the classic
 * "fast log2" trick): not exact (typically a few percent error), but
 * more than good enough for scaling color in a waterfall display, and
 * avoids libm entirely. */
static float log2_approx(float x)
{
    union { float f; uint32_t i; } vx;
    float y;
    if (x <= 0.0f) {
        x = 1.0e-9f; /* avoid log(0)/negative input - arbitrary low floor */
    }
    vx.f = x;
    y = (float)vx.i;
    y *= 1.1920929e-7f; /* 1 / 2^23 */
    return y - 126.94269504f;
}

void fft_init(void)
{
    uint32_t n;
    uint32_t bits = 0U;
    uint32_t size = FFT_SIZE;

    /* Exact log2(FFT_SIZE) by counting shifts (FFT_SIZE is guaranteed
     * to be a power of two per the header's contract) */
    while (size > 1U) { size >>= 1U; bits++; }

    for (n = 0; n < FFT_SIZE / 2U; n++) {
        float angle = -2.0f * 3.14159265358979f * (float)n / (float)FFT_SIZE;
        s_twiddle_cos[n] = cosf_approx_local(angle);
        s_twiddle_sin[n] = sinf_approx_local(angle);
    }

    for (n = 0; n < FFT_SIZE; n++) {
        /* Hann window: 0.5 - 0.5*cos(2*pi*n/(N-1)) */
        s_hann[n] = 0.5f - 0.5f * cosf_approx_local(
            2.0f * 3.14159265358979f * (float)n / (float)(FFT_SIZE - 1U));
    }

    /* Bit-reversal table for `bits` bits, precomputed once */
    for (n = 0; n < FFT_SIZE; n++) {
        uint32_t v = n;
        uint32_t r = 0U;
        uint32_t b;
        for (b = 0; b < bits; b++) {
            r = (r << 1U) | (v & 1U);
            v >>= 1U;
        }
        s_bitrev[n] = (uint16_t)r;
    }
}

/* The iterative radix-2 DIT butterfly pass, in-place over s_re/s_im.
 * Shared by the real and the complex (I/Q) entry points - the core
 * was always complex, only the load stage differs. */
static void fft_run(void)
{
    uint32_t i, j;
    uint32_t half_size, step;

    for (half_size = 1U, step = FFT_SIZE / 2U; half_size < FFT_SIZE;
         half_size <<= 1U, step >>= 1U) {
        for (i = 0; i < FFT_SIZE; i += (half_size << 1U)) {
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

void fft_compute_db(const int16_t *samples, float *db_out)
{
    uint32_t n;

    /* Window + load, with bit-reversal reordering applied on load */
    for (n = 0; n < FFT_SIZE; n++) {
        uint16_t src = s_bitrev[n];
        s_re[n] = (float)samples[src] * s_hann[src];
        s_im[n] = 0.0f;
    }

    fft_run();

    /* Power -> "dB" (10*log10(x) = 3.0103*log2(x)), only the useful
     * bins 0..N/2-1 (the rest is the mirror image of a real input) */
    for (n = 0; n < FFT_BINS_USEFUL; n++) {
        float power = s_re[n] * s_re[n] + s_im[n] * s_im[n];
        db_out[n] = 3.0103f * log2_approx(power);
    }
}

void fft_compute_db_iq(const int16_t *i_samples, const int16_t *q_samples,
                        float *db_out)
{
    uint32_t n;

    /* Complex load: I -> real, Q -> imaginary. With a complex input
     * the transform distinguishes +f from -f, which is the whole
     * point: signals above the LO land on the right half of the
     * display, below the LO on the left. */
    for (n = 0; n < FFT_SIZE; n++) {
        uint16_t src = s_bitrev[n];
        s_re[n] = (float)i_samples[src] * s_hann[src];
        s_im[n] = (float)q_samples[src] * s_hann[src];
    }

    fft_run();

    /* All FFT_SIZE bins are meaningful now. Output in fftshift order:
     * db_out[0] = -Fs/2 ... db_out[N/2] = DC (the VFO) ...
     * db_out[N-1] = +Fs/2 - 1 bin. Natural bin k maps to output index
     * (k + N/2) mod N. */
    for (n = 0; n < FFT_SIZE; n++) {
        uint32_t out = (n + FFT_SIZE / 2U) & (FFT_SIZE - 1U);
        float power = s_re[n] * s_re[n] + s_im[n] * s_im[n];
        db_out[out] = 3.0103f * log2_approx(power);
    }

#if FFT_IQ_DC_BLANK
    /* The QSD + codec chain has a DC offset that shows up as a tall
     * spike exactly at the center (on top of the VFO line), dwarfing
     * real signals visually. Replace DC and its two Hann-spread
     * neighbors with the average of the adjacent clean bins - purely
     * cosmetic, the underlying data is not used for demodulation. */
    {
        uint32_t c = FFT_SIZE / 2U;
        float fill = 0.5f * (db_out[c - 4U] + db_out[c + 4U]);
        /* +/-2 bins: Hann leakage from a strong DC component reaches
         * measurably beyond the immediate neighbors (verified with a
         * synthetic full-scale DC on the host test). */
        db_out[c - 2U] = fill;
        db_out[c - 1U] = fill;
        db_out[c]      = fill;
        db_out[c + 1U] = fill;
        db_out[c + 2U] = fill;
    }
#endif
}
