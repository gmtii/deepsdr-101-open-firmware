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

/* Quien quiera los bins complejos. Ver fft_set_bins_hook() en fft.h. */
static void (*s_bins_hook)(const float *re, const float *im, uint32_t n);

void fft_set_bins_hook(void (*f)(const float *re, const float *im, uint32_t n))
{
    s_bins_hook = f;
}

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

    /*
     * EL GIRO SE SACA UNA VEZ POR COLUMNA, NO UNA POR MARIPOSA - 19/10/2026.
     *
     * Pedido por el usuario: que la radio vaya mas rapido. Y esta
     * transformada corre en CADA fotograma del espectro, unas 45 veces por
     * segundo.
     *
     * Los dos bucles de dentro estaban al reves: el de `j` -que es el que
     * elige el giro- era el interior, asi que `tre` y `tim` se volvian a
     * calcular en cada mariposa. Son ocho etapas por 128 mariposas = 1.024
     * giros por transformada, cada uno con una multiplicacion para el
     * indice y dos lecturas de tabla.
     *
     * Dandole la vuelta a los dos bucles el giro se calcula una vez por
     * columna: 1 + 2 + 4 + ... + 128 = 255. CUATRO VECES menos. Y la
     * primera etapa no lleva giro -solo existe j = 0, o sea w = 1-, asi
     * que se saca aparte y se queda en una suma y una resta sin las cuatro
     * multiplicaciones: son 128 mariposas de las 1.024.
     *
     * Las mariposas son las mismas y en el mismo orden dentro de cada una,
     * asi que el resultado es el mismo bit a bit. Lo comprueba
     * sim/ffttest.c, y el espectro entero lo comprueba sim/espectro_oro.c
     * contra una imagen patron.
     *
     * El mismo cambio, por el mismo motivo, esta en la transformada de
     * stanag_det.c.
     */
    for (i = 0; i < FFT_SIZE; i += 2U) {
        float br = s_re[i + 1U], bi = s_im[i + 1U];
        s_re[i + 1U] = s_re[i] - br;
        s_im[i + 1U] = s_im[i] - bi;
        s_re[i]      = s_re[i] + br;
        s_im[i]      = s_im[i] + bi;
    }

    for (half_size = 2U, step = FFT_SIZE / 4U; half_size < FFT_SIZE;
         half_size <<= 1U, step >>= 1U) {
        uint32_t tw_idx = 0U;

        for (j = 0; j < half_size; j++, tw_idx += step) {
            float tre = s_twiddle_cos[tw_idx];
            float tim = s_twiddle_sin[tw_idx];

            for (i = 0; i < FFT_SIZE; i += (half_size << 1U)) {
                uint32_t a = i + j;
                uint32_t b = a + half_size;
                float br = s_re[b] * tre - s_im[b] * tim;
                float bi = s_re[b] * tim + s_im[b] * tre;
                /* ar/ai se guardan ANTES de escribir en [b]. El compilador no
                 * puede demostrar que a != b, asi que sin esto vuelve a leer
                 * s_re[a] y s_im[a] de memoria despues de cada escritura:
                 * medido, dos cargas de mas por mariposa (25 -> 23
                 * instrucciones). nr_ss.c ya lo hacia asi. Bit a bit igual:
                 * son los mismos valores, leidos del registro. */
                float ar = s_re[a];
                float ai = s_im[a];
                s_re[b] = ar - br;
                s_im[b] = ai - bi;
                s_re[a] = ar + br;
                s_im[a] = ai + bi;
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

    /*
     * EL GANCHO DE LOS BINS COMPLEJOS - 19/10/2026. Ver fft_set_bins_hook().
     *
     * Este es el unico sitio del firmware donde existe el espectro COMPLEJO:
     * dos lineas mas abajo se pasa a dB y la fase se tira. Quien necesite la
     * fase -hoy, la calibracion del corrector de imagen- se engancha aqui en
     * vez de hacer una segunda FFT solo para el.
     *
     * Es un PUNTERO y no una llamada directa a proposito, igual que
     * gfx2_set_pump(): fft.c es una transformada y no tiene por que saber
     * que existe un corrector de I/Q. El primer intento llamaba a
     * iqbal_calibra() por su nombre, y con eso media docena de bancos que
     * enlazan fft.c a solas dejaron de enlazar.
     */
    if (s_bins_hook != 0) { s_bins_hook(s_re, s_im, (uint32_t)FFT_SIZE); }

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
