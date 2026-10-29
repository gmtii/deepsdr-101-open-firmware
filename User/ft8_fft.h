#ifndef FT8_FFT_H
#define FT8_FFT_H

#include <stdint.h>

/*
 * *** 2.048 PUNTOS DESDE EL 28/10/2026, no 1.024: al pasar el
 * remuestreador a 6.400 Hz la ventana de analisis sigue midiendo dos
 * simbolos -320 ms- y eso son 2.048 muestras. Lo de abajo explica por que
 * este modulo existe aparte de fft.c, que no ha cambiado; los tamanos que
 * cita son los de entonces. El tamano se cambia en FT8_FFT_SIZE y de el
 * sale todo lo demas, FT8_FFT_BITS incluido. ***
 *
 * Dedicated 1024-point real-input FFT for FT8's wider search window
 * (09/2026) - a full independent copy of fft.c's design (same radix-2
 * Cooley-Tukey, same Hann window, same libm-free fast log2 "dB"), NOT
 * a parameterized version of fft.c and NOT sharing its buffers.
 *
 * WHY A SEPARATE MODULE INSTEAD OF JUST BUMPING fft.h's FFT_SIZE:
 * fft.c's own FFT_SIZE is locked in step with the main spectrum's
 * SDR_RX_BLOCK_SAMPLES (currently 256, tuned for the live panadapter's
 * own real-time ISR-driven use) - fft.h's own history comment notes
 * it used to be 512 and was deliberately reduced, implying that size
 * mattered for the main spectrum's performance. Bumping it back up
 * just for FT8 risks that same regression for everyone, all the time,
 * not just in FT8 mode. A separate 1024-point instance, used only
 * while FT8 mode is active, isolates the cost entirely.
 *
 * RAM: this does NOT use fft.c's TCMRAM_BSS placement (TCM has no
 * room left, see the FT8 memory-budget history in
 * ft8_waterfall_adapter.h) - these buffers live in ordinary SRAM,
 * sized into the same "reuse the waterfall's own memory footprint
 * while FT8 mode is active" budget already established for
 * mag[]/the cascade. Approx size: re+im (1024*4*2=8192B) + twiddles
 * (512*4*2=4096B) + Hann (1024*4=4096B) + bit-reversal (1024*2=2048B)
 * = ~18.4KB.
 *
 * Y DE ESAS CUATRO CIFRAS SOLO QUEDA LA PRIMERA: las tablas de giro
 * viven en flash y son un cuarto de onda (2.052 bytes, ver k_ft8_cuarto),
 * la ventana de Hann se calcula (ft8_fft_hann()) y el orden de bits
 * invertido tambien (ft8_bitrev(), una instruccion `rbit`). En RAM solo
 * estan re+im, que con 2.048 puntos son 16.384 bytes.
 */
#define FT8_FFT_SIZE        2048U
#define FT8_FFT_BINS_USEFUL (FT8_FFT_SIZE / 2U) /* 512 - real input: bins 0..Nyquist, the rest mirror these */

/* Must be called once at startup (precomputes the twiddle/Hann/
 * bit-reversal tables) - alongside fft_init(), not instead of it. */
/*
 * LAS TABLAS, EN FLASH. Las genera tools/gen_ft8_tablas.py en
 * User/ft8_tablas.c; antes se construian en RAM en cada entrada a FT8 y
 * costaban 10.240 bytes de la memoria compartida con la cascada. Ver el
 * comentario del generador para por que eso importaba tanto.
 */
/*
 * UN CUARTO DE ONDA PARA LOS DOS GIROS - 28/10/2026.
 *
 * *** POR QUE: al pasar la FFT a 2.048 puntos las dos tablas de media
 * onda -coseno y seno, N/2 floats cada una- pasaban de 4.096 a 8.192
 * bytes, y la flash interna se habia acabado: el enlace se pasaba por
 * 1.360 bytes. ***
 *
 * Un coseno tabulado de 0 a pi/2 da los dos giros enteros, con
 * reflexiones exactas (no aproximadas: son identidades del coseno):
 *
 *     cos(2*pi*j/N) =  C[j]          si j <= N/4
 *                   = -C[N/2 - j]    si no
 *     sen(2*pi*j/N) =  C[N/4 - j]    si j <= N/4
 *                   =  C[j - N/4]    si no
 *
 * 513 floats -2.052 bytes- en vez de 8.192: 6.140 menos.
 *
 * LO QUE SI CAMBIA, y se dice: los valores que salen de una reflexion ya
 * no son EXACTAMENTE los que daba la aproximacion de Bhaskara-I evaluada
 * en su propio angulo. La aproximacion no es simetrica, y en este arbol
 * eso ya estaba medido y escrito: "el seno no sale del coseno -152 de
 * 512-". Lo que cuesta en decodificacion lo dicen los dos bancos de FT8,
 * y es cero medible: la misma rejilla de medio bin, la misma
 * sensibilidad y la misma S/R que con las dos tablas.
 *
 * El giro de la FFT es e^(-i*2*pi*j/N), o sea coseno con el seno
 * CAMBIADO DE SIGNO. Por eso ft8_sen_giro() devuelve el negativo.
 */
extern const float    k_ft8_cuarto[FT8_FFT_SIZE / 4U + 1U];

static inline float ft8_cos_giro(uint32_t j)   /* parte real  de e^(-i2pi j/N) */
{
    return (j <= (FT8_FFT_SIZE / 4U))
         ?  k_ft8_cuarto[j]
         : -k_ft8_cuarto[(FT8_FFT_SIZE / 2U) - j];
}

static inline float ft8_sen_giro(uint32_t j)   /* parte imaginaria, ya con su signo */
{
    return (j <= (FT8_FFT_SIZE / 4U))
         ? -k_ft8_cuarto[(FT8_FFT_SIZE / 4U) - j]
         : -k_ft8_cuarto[j - (FT8_FFT_SIZE / 4U)];
}
/* La ventana de Hann ya no es una tabla: se calcula. Ver el comentario
 * largo de ft8_fft_hann() en el .c -4.096 bytes de flash a cambio de
 * 4,75 us por transformada, y bit a bit los mismos numeros-. Se publica
 * para que el banco pueda comprobarla. */
float ft8_fft_hann(uint32_t n);

/*
 * Y LA CUARTA SE CALCULA - 26/10/2026, haciendo hueco en la flash interna.
 *
 * k_ft8_bitrev[] eran 1.024 enteros de 16 bits -2.048 bytes- y lo que
 * guardaban era EXACTAMENTE invertir diez bits: se comprobo valor a valor,
 * 0 diferencias en los 1.024. En un Cortex-M4 eso es la instruccion `rbit`,
 * un ciclo, o sea menos de lo que cuesta leer la tabla.
 *
 * Es la unica de las cuatro que se podia quitar sin cambiar un bit del
 * resultado. Las otras tres son de coma flotante y salen de una
 * aproximacion, asi que deducir una de otra SI cambia valores: medido, la
 * ventana de Hann no es simetrica bit a bit -349 de 512 pares difieren- y
 * el seno no sale del coseno -152 de 512-.
 *
 * Se usa una sola vez por transformada, en el bucle que reordena las
 * muestras de entrada.
 */
/*
 * Y LOS BITS LOS DICE EL TAMAÑO, NO UN NUMERO ESCRITO A MANO - 28/10/2026.
 *
 * Esto invertia DIEZ bits porque la FFT media 1.024 puntos, y lo vigilaba
 * un aserto que decia "solo vale para 1024". Al pasar a 2.048 ese aserto
 * hizo su trabajo: no compilo. Pero volver a escribir "11" a mano deja la
 * misma trampa puesta para la proxima vez.
 *
 * FT8_FFT_BITS sale de FT8_FFT_SIZE, asi que cambiar el tamaño cambia el
 * desplazamiento solo. El aserto de abajo sigue estando: lo que vigila
 * ahora es que el tamaño sea potencia de dos, que es de lo que depende
 * TODO el radix-2.
 */
#if   FT8_FFT_SIZE == 256U
#define FT8_FFT_BITS 8U
#elif FT8_FFT_SIZE == 512U
#define FT8_FFT_BITS 9U
#elif FT8_FFT_SIZE == 1024U
#define FT8_FFT_BITS 10U
#elif FT8_FFT_SIZE == 2048U
#define FT8_FFT_BITS 11U
#elif FT8_FFT_SIZE == 4096U
#define FT8_FFT_BITS 12U
#else
#error "FT8_FFT_SIZE tiene que ser una potencia de dos entre 256 y 4096"
#endif

static inline uint16_t ft8_bitrev(uint32_t n)
{
#if defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_7M__)
    uint32_t r;
    __asm__ ("rbit %0, %1" : "=r" (r) : "r" (n));
    return (uint16_t)(r >> (32U - FT8_FFT_BITS)); /* de 32 bits a los de abajo */
#else
    uint32_t r = 0U, b;              /* el simulador, que no es un Cortex */

    for (b = 0U; b < FT8_FFT_BITS; b++) {
        r = (r << 1U) | (n & 1U);
        n >>= 1U;
    }
    return (uint16_t)r;
#endif
}

void ft8_fft_init(void);

/*
 * Input: `samples` (FT8_FFT_SIZE real samples). Output: `db_out`
 * (FT8_FFT_BINS_USEFUL values, uncalibrated relative "dB" - same
 * formula/scale as fft.c's fft_compute_db(), see
 * ft8_waterfall_adapter.h's FT8_ADAPTER_DB_OFFSET comment for why that
 * scale needs its own calibration offset regardless). Applies a Hann
 * window internally before transforming.
 */
void ft8_fft_compute_db(const int16_t *samples, float *db_out);

/* La misma, pero guardando solo `n_out` bins empezando en `base` y de
 * `paso` en `paso`. Ver su comentario en el .c: con la FFT en 2.048 el
 * adaptador lee uno de cada dos, y guardar los otros costaba 4.096 bytes
 * de SRAM y la mitad de los logaritmos. */
void ft8_fft_compute_db_rango(const int16_t *samples, float *db_out,
                              uint32_t base, uint32_t paso, uint32_t n_out);

#endif /* FT8_FFT_H */
