#include "ft8_fft1024.h"
#include "ft8_shared_ram.h"

/* Real memory union with the main waterfall's own pixel buffer - see
 * ft8_shared_ram.h for the full "why" and the safety argument (these
 * arrays are never touched while the main waterfall is showing, and
 * vice versa). Storage itself is defined once, in waterfall.c -
 * these are just typed views into it via macros, so every line below
 * that already said s_re/s_im/etc. keeps working unchanged. */
#define s_re           (g_ft8_shared_ram.ft8.fft_re)
#define s_im           (g_ft8_shared_ram.ft8.fft_im)
#define s_twiddle_cos  (g_ft8_shared_ram.ft8.fft_twiddle_cos)
#define s_twiddle_sin  (g_ft8_shared_ram.ft8.fft_twiddle_sin)
#define s_hann         (g_ft8_shared_ram.ft8.fft_hann)
#define s_bitrev       (g_ft8_shared_ram.ft8.fft_bitrev)

/* Identical Bhaskara-I sine approximation to fft.c's own
 * sinf_approx_local() - reimplemented locally (not shared/exported
 * from fft.c) to keep this module fully independent, same reasoning
 * fft.c itself gives for not sharing with gd32_i2s.c's copy. */
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

static float log2_approx(float x)
{
    union { float f; uint32_t i; } vx;
    float y;
    if (x <= 0.0f) {
        x = 1.0e-9f;
    }
    vx.f = x;
    y = (float)vx.i;
    y *= 1.1920929e-7f;
    return y - 126.94269504f;
}

void ft8_fft1024_init(void)
{
    uint32_t n;
    uint32_t bits = 0U;
    uint32_t size = FT8_FFT_SIZE;

    while (size > 1U) { size >>= 1U; bits++; }

    for (n = 0; n < FT8_FFT_SIZE / 2U; n++) {
        float angle = -2.0f * 3.14159265358979f * (float)n / (float)FT8_FFT_SIZE;
        s_twiddle_cos[n] = cosf_approx_local(angle);
        s_twiddle_sin[n] = sinf_approx_local(angle);
    }

    for (n = 0; n < FT8_FFT_SIZE; n++) {
        s_hann[n] = 0.5f - 0.5f * cosf_approx_local(
            2.0f * 3.14159265358979f * (float)n / (float)(FT8_FFT_SIZE - 1U));
    }

    for (n = 0; n < FT8_FFT_SIZE; n++) {
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

static void fft_run(void)
{
    uint32_t i, j;
    uint32_t half_size, step;

    for (half_size = 1U, step = FT8_FFT_SIZE / 2U; half_size < FT8_FFT_SIZE;
         half_size <<= 1U, step >>= 1U) {
        for (i = 0; i < FT8_FFT_SIZE; i += (half_size << 1U)) {
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

void ft8_fft1024_compute_db(const int16_t *samples, float *db_out)
{
    uint32_t n;

    for (n = 0; n < FT8_FFT_SIZE; n++) {
        uint16_t src = s_bitrev[n];
        s_re[n] = (float)samples[src] * s_hann[src];
        s_im[n] = 0.0f;
    }

    fft_run();

    for (n = 0; n < FT8_FFT_BINS_USEFUL; n++) {
        float power = s_re[n] * s_re[n] + s_im[n] * s_im[n];
        db_out[n] = 3.0103f * log2_approx(power);
    }
}
