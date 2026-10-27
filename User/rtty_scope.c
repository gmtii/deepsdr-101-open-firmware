#include "rtty_scope.h"
#include "rtty.h" /* RTTY_BLOCK_SAMPLES - the feed chunk size, see rtty_scope_feed() */
#include "fft.h"  /* fft_potencias(): la transformada, que ya no es de aqui */

/*
 * LA FFT YA NO ES DE ESTE FICHERO - 24/10/2026.
 *
 * *** Preguntado por el usuario: "que mas cosas hay en memoria mientras no
 * se estan usando?". ***
 *
 * Aqui habia una FFT ENTERA: s_re, s_im, las dos tablas de giro, la
 * ventana de Hann y la tabla de inversion de bits, 9.216 bytes de TCM, mas
 * su propia mariposa y sus propias aproximaciones de seno y coseno.
 *
 * La cabecera de rtty_scope.h lo justificaba con tres razones -"different
 * size, different input type, different consumer"- y las tres se han
 * caido:
 *
 *   - EL TAMAÑO. El de aqui eran 512 y el de fft.c 256. Hoy los dos son
 *     512, y no por hacerle sitio a este: porque a 192 kHz el panadaptador
 *     necesita 512 para no enseñar las emisoras con la punta plana.
 *   - EL TIPO DE ENTRADA. Float aqui, int16 alli. Eso es una funcion de
 *     carga, no una transformada.
 *   - EL CONSUMIDOR. Potencia aqui, decibelios alli. Otra linea.
 *
 * O sea que lo distinto de verdad eran la carga y la salida, y eso es
 * fft_potencias(). Lo que se gana: 9.216 bytes de TCM -que es lo que hacia
 * posible subir la FFT del panadaptador a 512- y, de regalo, la mariposa
 * BUENA: la de aqui calculaba el giro dentro del bucle mas interno, que es
 * justo lo que fft.c arreglo el 19/10/2026 por "cuatro veces menos".
 *
 * LO QUE SE QUEDA es s_accum, y se queda porque es de otra clase: lo llena
 * rtty_scope_feed() DESDE LA INTERRUPCION, ventana a ventana, y tiene que
 * sobrevivir entre una y otra. Los buffers de fft.c no sobreviven a nada:
 * se llenan, se transforman y se vacian dentro de la misma llamada, que es
 * exactamente por lo que se pueden compartir.
 *
 * Moved to TCM RAM 01/09/2026 - "ISR context" just means CPU code
 * executing inside an interrupt handler, still plain core loads/stores,
 * not a DMA descriptor.
 */
#define TCMRAM_BSS __attribute__((section(".tcmram")))

/* Lo que la FFT de fft.c espera y lo que este modulo cree: si alguien
 * cambia uno de los dos, que no compile. */
_Static_assert(RTTY_SCOPE_FFT_SIZE == FFT_SIZE,
               "el osciloscopio y fft.c tienen que usar el mismo tamaño");
_Static_assert(RTTY_SCOPE_BINS == FFT_BINS_USEFUL,
               "los bins del osciloscopio y los de fft.c se han separado");

static float s_accum[RTTY_SCOPE_FFT_SIZE] TCMRAM_BSS; /* filled by rtty_scope_feed() (ISR context) */
static uint32_t s_accum_count;             /* samples currently in s_accum, ISR-owned */
static volatile uint8_t s_pending;         /* 1 = s_accum has a full window ready for rtty_scope_poll() */

static float s_mag[RTTY_SCOPE_BINS];       /* main-loop-owned output, written by rtty_scope_poll() */
static uint8_t s_frame_ready;
static uint32_t s_frame_n;     /* cuadros promediados desde el arranque */

/* --- multi-window averaging accumulator, see RTTY_SCOPE_AVG_FRAMES's
 * comment in rtty_scope.h ---
 *
 * DETRAS DE UN #if DESDE EL 10/10/2026. RTTY_SCOPE_AVG_FRAMES vale 1, y
 * con 1 el promediado era: copiar los 256 huecos a este acumulador, y
 * acto seguido copiarlos de vuelta dividiendo por 1,0. Mil veinticuatro
 * bytes de una RAM que va al 90 % y 512 operaciones de coma flotante por
 * cuadro para dejar los numeros exactamente como estaban.
 *
 * No se borra el promediado: se apaga. Quien suba la constante lo
 * recupera entero, con su acumulador y su reparto, sin reescribir nada.
 * Lo que no se paga es tenerlo encendido en 1. */
#if RTTY_SCOPE_AVG_FRAMES > 1U
static float    s_mag_sum[RTTY_SCOPE_BINS];
static uint32_t s_avg_count; /* raw windows accumulated into s_mag_sum so far, 0..RTTY_SCOPE_AVG_FRAMES-1 between flushes */
#endif

void rtty_scope_init(void)
{
    /*
     * Y SE LLAMA A fft_init() DESDE AQUI, no se da por hecho.
     *
     * La primera version decia "las tablas las construye fft_init(), que
     * main() llama antes que esto". Era verdad en la radio y FALSO en los
     * bancos: sim/rtty_solo.c solo llama a rtty_scope_init(), asi que la
     * transformada corrio con las tablas a cero y el ajuste automatico de
     * RTTY dejo de converger. El banco lo caso a la primera.
     *
     * Dar por hecho un orden de arranque es pedir que algun dia no se
     * cumpla. fft_init() solo rellena tablas a partir de constantes:
     * llamarla dos veces deja exactamente lo mismo, asi que no hay nada
     * que proteger y si una trampa que quitar.
     */
    fft_init();

    s_accum_count = 0U;
    s_pending = 0U;
    s_frame_ready = 0U;
#if RTTY_SCOPE_AVG_FRAMES > 1U
    s_avg_count = 0U;
#endif
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

void rtty_scope_poll(void)
{
    uint32_t n;
    float peak;

    if (!s_pending) {
        return;
    }

    /* La transformada, la ventana y la inversion de bits las hace fft.c.
     * Ver el bloque del principio de este fichero. */
    fft_potencias(s_accum, s_mag);

    peak = 1.0e-12f; /* avoid a divide-by-zero if the block was silence */
    for (n = 0; n < RTTY_SCOPE_BINS; n++) {
        if (s_mag[n] > peak) {
            peak = s_mag[n];
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
    /* Un reciproco y 256 multiplicaciones en vez de 256 divisiones:
     * `peak` no cambia dentro del bucle, asi que dividir 256 veces por el
     * mismo numero es pagar 256 VDIV.F32 por uno. 10/10/2026. */
    {
        float inv = 1.0f / peak;

        for (n = 0; n < RTTY_SCOPE_BINS; n++) {
            s_mag[n] *= inv;
        }
    }

    s_pending = 0U; /* rtty_scope_feed() can accept the next window again */

    /* --- multi-window averaging, see RTTY_SCOPE_AVG_FRAMES's comment
     * in rtty_scope.h. Apagado mientras valga 1: ver s_mag_sum. --- */
#if RTTY_SCOPE_AVG_FRAMES > 1U
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
        s_frame_n++;
    }
    /* else: not enough raw windows yet - s_frame_ready stays as it
     * was (0, unless a previous ready frame is still waiting to be
     * collected by rtty_scope_get_frame() - see its own comment) and
     * the caller just sees "nothing new this poll", same as the
     * original !s_pending early-return above. */
#else
    /* Con una sola ventana no hay nada que promediar: s_mag ya es el
     * cuadro, normalizado a su propio pico unas lineas mas arriba. */
    s_frame_ready = 1U;
    s_frame_n++;
#endif
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

/*
 * MIRAR EL CUADRO SIN QUITARSELO A NADIE - 06/10/2026.
 *
 * rtty_scope_get_frame() CONSUME: baja la bandera de "hay cuadro nuevo".
 * Eso esta bien cuando el que mira es uno solo -el dibujo del
 * osciloscopio- y mal en cuanto hay dos. Y hay dos desde que el ajuste
 * automatico promedia el espectro para encontrar la PAREJA de tonos.
 *
 * Lo que pasaba: el dibujo corre antes en la vuelta, coge el cuadro y baja
 * la bandera; despues rtty_auto llamaba otra vez y se llevaba EL MISMO
 * cuadro, vuelta tras vuelta. Su media exponencial de ocho cuadros estaba
 * promediando ocho copias de uno, o sea que no promediaba nada - y el
 * sentido de esa media es justo que en 43 ms solo suena UNO de los dos
 * tonos y hacen falta varios cuadros para que suenen los dos. Encontraba
 * un tono, no una pareja, y sin pareja no hay desplazamiento que medir.
 * Es el mismo fallo que el modulo documenta en su propia cabecera, pero
 * por el otro lado.
 *
 * Asi que quien solo quiere MIRAR usa esto, y cuenta los cuadros el mismo
 * con rtty_scope_frame_n() para no promediar dos veces el mismo.
 */
const float *rtty_scope_frame_peek(void)
{
    return s_mag;
}

uint32_t rtty_scope_frame_n(void)
{
    return s_frame_n;
}

float rtty_scope_hz_per_bin(void)
{
    return 12000.0f / (float)RTTY_SCOPE_FFT_SIZE;
}

void rtty_scope_avg_reset(void)
{
#if RTTY_SCOPE_AVG_FRAMES > 1U
    s_avg_count = 0U;
#endif
    /* Con RTTY_SCOPE_AVG_FRAMES == 1 no hay acumulador que reiniciar, pero
     * la funcion se queda: sus llamantes no tienen por que saber si el
     * promediado esta encendido. Ver s_mag_sum. */
}
