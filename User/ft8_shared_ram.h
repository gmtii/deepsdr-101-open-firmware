#ifndef FT8_SHARED_RAM_H
#define FT8_SHARED_RAM_H

#include <stdint.h>
#include "waterfall.h"       /* WATERFALL_WIDTH, WATERFALL_ROWS */
#include "ft8_fft1024.h"     /* FT8_FFT_SIZE, FT8_FFT_BINS_USEFUL */
#include "ft8_waterfall_adapter.h" /* FT8_ADAPTER_* */

/*
 * Real memory union between the main waterfall's own pixel buffer and
 * every large static buffer FT8 mode needs (09/2026) - added when
 * widening the FT8 search window to 1600Hz overflowed RAM by ~41.9KB
 * at link time. Up to that point, "FT8 mode reuses the waterfall's
 * memory footprint" had only ever been a budget ESTIMATE in comments
 * (see ft8_waterfall_adapter.h's own memory-budget history) - the
 * actual code just declared separate static arrays for everything,
 * which the linker allocates ADDITIONALLY on top of the waterfall's
 * own buffer, not instead of it. This union is what finally makes
 * that reuse real, the same way the HFDL decoder's own RAM management
 * already does for ITS waterfall-lending trick (see
 * /areas/deepsdr-hfdl-decoder.md's "RAM managed via a union lending
 * the waterfall buffer to HFDL demod when in HFDL mode").
 *
 * SAFETY: the waterfall's own buffer (waterfall.c's s_buf) is written
 * and read purely by CPU code (memset/memcpy/gfx_blit - confirmed by
 * reading waterfall.c directly, no DMA anywhere in that file), same
 * "CPU-only, safe to overlap" class of buffer as the TCM-relocated
 * ones (fft.c, spectrum.c, etc.) - and it is NEVER written while FT8
 * mode is active, because sdr_spectrum_waterfall_tick() (the only
 * thing that calls waterfall_push_line()) does not run while FT8 mode
 * has taken over that same screen region (see main.c's rtty_showing/
 * ft8_showing render gate). Conversely, none of FT8's own buffers are
 * touched while a normal (non-FT8, non-RTTY) mode is showing the real
 * waterfall - ft8_waterfall_reset()/feed_subblock() are simply never
 * called outside FT8 mode.
 *
 * SIZE: the waterfall's own buffer (WATERFALL_WIDTH*WATERFALL_ROWS*2 =
 * 114624 bytes) is comfortably the larger member - everything FT8
 * needs together (mag[] + the FFT-1024 tables + the cascade history +
 * the sliding analysis window + FFT scratch) totals ~74.2KB, ~40KB
 * less than the waterfall buffer alone - so this union costs exactly
 * what the waterfall buffer already cost by itself: NO net RAM
 * increase for adding all of FT8's needs.
 *
 * ONE instance, defined (not just declared extern) in waterfall.c -
 * see that file's own comment at the point it replaced its old
 * private `static uint16_t s_buf[...]` declaration with a reference
 * into this union.
 */
/*
 * ADAPTADO A ESTE ARBOL - 24/09/2026
 *
 * Dos diferencias con el original de la rama hfdl, las dos porque nuestra
 * cascada no es la suya:
 *
 *  1. uint8_t en vez de uint16_t. Nuestra cascada guarda INDICES DE PALETA,
 *     un byte por punto, no color RGB565. Eso ahorro 56 kB en su dia... y
 *     aqui juega en contra, porque el buffer con el que se comparte memoria
 *     es la mitad de grande: 54.432 bytes contra sus 114.624.
 *
 *  2. Fuera el miembro de HFDL: aqui no hay HFDL (todavia).
 *
 * LO QUE ESO OBLIGA. Con 256 bins (1600 Hz) el bloque de FT8 mide 121.856
 * bytes y a ellos les cabia casi entero dentro de su cascada. A nosotros no:
 * seria pedir 67 kB de mas y solo hay 19 libres. Con 64 bins (400 Hz) mide
 * 47.360, o sea MENOS que nuestra cascada, y entonces esta union no cuesta
 * absolutamente nada - ocupa lo que ya ocupaba el buffer de la cascada.
 *
 * Y 64 bins no es un recorte a la desesperada: es la configuracion que ELLOS
 * llegaron a publicar, con su A/B contra el corpus de ft8_lib detras (ver
 * ft8_waterfall_adapter.h). Ensancharla es cambiar FT8_ADAPTER_NUM_BINS.
 */
typedef union
{
    uint8_t waterfall_buf[WATERFALL_ROWS][WATERFALL_WIDTH];

    struct
    {
        uint8_t mag[FT8_ADAPTER_MAX_BLOCKS * FT8_ADAPTER_TIME_OSR * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS];
        /*
         * EL DESVIO. 25/09/2026, sustituye a la copia entera de mag[].
         *
         * EL PROBLEMA, que es real y no se va solo: capturar una ranura dura
         * 14,88 s y decodificarla se lleva lo suyo. Las dos cosas juntas no
         * caben en los 15 s que dura una ranura de FT8. O sea que al cerrar
         * una hay que empezar la siguiente YA, antes de decodificar - y
         * entonces la captura nueva escribe encima de lo que el decodificador
         * todavia esta leyendo.
         *
         * COMO LO RESOLVIA: copiando mag[] entero a un segundo buffer del
         * mismo tamano y decodificando de la copia. Funciona, y cuesta
         * 23.808 bytes, que con la ventana en 1.600 Hz es dinero de verdad:
         * es lo que obligo a bajar la banda de dibujo de gfx2 a 8 filas y a
         * que el espectro fuera algo mas lento en todos los demas modos.
         *
         * COMO LO RESUELVE AHORA: al reves. En vez de apartar lo VIEJO, se
         * aparta lo NUEVO. Mientras se decodifica, la captura escribe aqui,
         * en un buffer pequeno; cuando la decodificacion acaba, esto se
         * vuelca al principio de mag[] y la captura sigue como si nada.
         *
         * Y sale barato porque no hacen falta 93 bloques: solo los que la
         * captura alcance a escribir MIENTRAS dura la decodificacion, a 160
         * ms por bloque. Con 24 bloques hay 3,84 segundos de margen, de sobra
         * para una decodificacion que tarda menos de uno.
         *
         * Lo que se gana: 23.808 - 6.144 = 17.664 bytes, con los que la banda
         * de dibujo vuelve a sus 16 filas y el espectro a su velocidad.
         *
         * Y LO MEJOR: ft8_lib no se entera. Sigue leyendo mag[] tal cual,
         * sin saber que la captura esta desviada. La alternativa -que leyera
         * de dos sitios segun el bloque- era meterle mano por dentro al
         * decodificador, y eso se descarto por eso mismo.
         *
         * SI SE DESBORDA: si la decodificacion tardara mas de 24 bloques, lo
         * que no quepa se pierde. No en silencio: lo cuenta
         * ft8_waterfall_desbordes() y sale en la pantalla.
         */
        uint8_t lado[FT8_ADAPTER_LADO_BLOQUES * FT8_ADAPTER_TIME_OSR
                     * FT8_ADAPTER_FREQ_OSR * FT8_ADAPTER_NUM_BINS];
        float fft_re[FT8_FFT_SIZE];
        float fft_im[FT8_FFT_SIZE];
        float fft_twiddle_cos[FT8_FFT_SIZE / 2U];
        float fft_twiddle_sin[FT8_FFT_SIZE / 2U];
        float fft_hann[FT8_FFT_SIZE];
        uint16_t fft_bitrev[FT8_FFT_SIZE];
        /*
         * EL SELLO DE LAS TABLAS DE LA FFT - 25/09/2026.
         *
         * Las cuatro tablas de arriba (seno, coseno, Hann y el orden de bits
         * invertido) no se escriben solas: las construye ft8_fft1024_init().
         * Y viven AQUI DENTRO, o sea que cada vez que se sale de FT8 y la
         * cascada recupera su memoria, se borran. Hay que reconstruirlas en
         * CADA arranque de FT8, no una vez al encender la radio.
         *
         * Esto no salio de un razonamiento: salio de que la radio se colgaba
         * con un pitido continuo en cuanto se entraba en FT8. fft_bitrev se
         * usa como INDICE -samples[src], s_hann[src]- y sin construir
         * contiene lo que dejo la cascada, o sea cualquier numero hasta
         * 65535. Leer s_hann[65535] es 256 kB mas alla de una tabla de 4 kB:
         * fuera de la SRAM, fallo de bus, y a apagar la radio.
         *
         * El sello es la red: ft8_fft1024_init() lo escribe al terminar y
         * ft8_fft1024_compute_db() lo mira antes de usar las tablas. Y no
         * puede mentir, que es lo que lo hace util: vive en los MISMOS bytes
         * que las tablas, asi que el memset de waterfall_init() lo borra
         * exactamente cuando las borra a ellas. Un sello que sobreviviera a
         * los datos que avala seria peor que no tenerlo.
         */
        uint32_t fft_sello;
        int16_t window[FT8_ADAPTER_NFFT];
        float dbout[FT8_FFT_BINS_USEFUL];
    } ft8;
} ft8_shared_ram_t;

/*
 * QUIEN SOSTIENE EL SELLO. 25/09/2026.
 *
 * Aqui habia un _Static_assert que exigia que el miembro de la cascada fuera
 * el mas grande, porque waterfall_init() solo borraba ESE, y si FT8 se salia
 * por el otro extremo el sello podia sobrevivir a las tablas que avala.
 *
 * Era un aserto correcto sobre un diseno fragil: ataba el ancho de la ventana
 * de FT8 al tamano de la cascada, dos cosas que no tienen nada que ver. Salto
 * el primer dia que se quiso ensanchar la ventana, y con razon.
 *
 * Ahora waterfall_init() borra la UNION ENTERA (ver su comentario), asi que
 * el sello y las tablas se van siempre juntos, midan lo que midan los dos
 * miembros. La garantia ya no depende de una coincidencia de tamanos, y por
 * eso el aserto sobra: lo que vigilaba no puede pasar.
 */

extern ft8_shared_ram_t g_ft8_shared_ram;

#endif /* FT8_SHARED_RAM_H */
