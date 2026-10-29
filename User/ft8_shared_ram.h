#ifndef FT8_SHARED_RAM_H
#define FT8_SHARED_RAM_H

#include <stdint.h>
#include "waterfall.h"       /* WATERFALL_WIDTH, WATERFALL_ROWS */
#include "ft8_fft.h"     /* FT8_FFT_SIZE, FT8_FFT_BINS_USEFUL */
#include "ft8_waterfall_adapter.h" /* FT8_ADAPTER_* */
#include "hfdl_ram.h"       /* HFDL_RAM_TOTAL: el tercer inquilino */

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
 * SIZE: this paragraph is the ORIGINAL from the hfdl branch and its
 * numbers do NOT apply here - see "ADAPTADO A ESTE ARBOL" below, which
 * corrects them. Left in place because the reasoning is still the right
 * reasoning; only the figures changed. It said: the waterfall's own
 * buffer (WATERFALL_WIDTH*WATERFALL_ROWS*2 = 114624 bytes) is comfortably
 * the larger member, everything FT8 needs totals ~74.2KB, so the union is
 * free.
 *
 * In THIS tree the waterfall buffer is 54.432 bytes (756 x 72 x 1) and
 * FT8's member is 52.484, so the union is NOT free by a mile: only 1.948
 * bytes of slack. Anyone sizing something new against "~40KB less" would
 * be working from the wrong tree's numbers.
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
 *  2. Fuera el miembro de HFDL... y eso DEJO DE SER VERDAD. Ver mas abajo.
 *
 * DOS COSAS DE AQUI SE QUEDARON VIEJAS, y se corrigen el 30/09/2026 en vez
 * de borrarse, porque las dos se leian como razones para no tocar nada:
 *
 *  - "aqui no hay HFDL (todavia)". Lo hay desde el 25/09, y es el mayor
 *    inquilino de esta misma union: hfdl_ram.c reparte g_ft8_shared_ram con
 *    HFDL_RAM_CABECERA, y con el entraron WSPR, AIS, ALE y JTTY. Son SEIS
 *    modos repartiendose estos bytes, no uno.
 *
 *  - las cifras de "121.856 con 256 bins" y "47.360 con 64" son de cuando
 *    TIME_OSR y FREQ_OSR valian 2. Hoy valen 1, este arbol usa 256 bins
 *    (1600 Hz, ver ft8_waterfall_adapter.h) y el miembro de FT8 mide 52.484
 *    bytes: medido compilando una sonda contra esta cabecera, no calculado.
 *    Contra los 54.432 del buffer de la cascada eso deja 1.948 bytes de
 *    holgura, o sea que la union SI cuesta algo -poco- y no "absolutamente
 *    nada" como decia aqui.
 *
 * El parrafo de arriba (el de los 52.484 y los 1.948 de holgura) es el que
 * esta al dia; este bloque decia "corregir" aquellos numeros y llevaba una
 * semana corrigiendolos hacia atras.
 */
typedef union
{
    uint8_t waterfall_buf[WATERFALL_ROWS][WATERFALL_WIDTH];

    /*
     * EL TERCER INQUILINO, QUE AHORA ES EL QUE MANDA - 11/10/2026.
     *
     * HFDL llevaba desde el 25/09 usando esta union SIN miembro propio:
     * hfdl_ram.c cogia `sizeof g_ft8_shared_ram` y se repartia lo que
     * hubiera. Funcionaba porque le sobraba... hasta que se vio que NO le
     * sobraba: dos de los ocho giros no caben, y uno de ellos si cabria
     * con 5.872 bytes mas.
     *
     * Ahora lo pide por su nombre. Y eso cambia quien manda en el tamano
     * de la union: era la cascada con 54.432 y ahora es esto con 60.304.
     *
     * Que lo pida EXPLICITAMENTE no es cosmetica. Mientras era implicito,
     * la capacidad de HFDL dependia de un buffer de PIXELES: ensanchar la
     * cascada le habria dado memoria de regalo y estrecharla se la habria
     * quitado en silencio, sin que ningun numero de HFDL se moviera. Dos
     * cosas que no tienen nada que ver atadas por una coincidencia de
     * tamanos - que es el mismo fallo que ya cazo el comentario de las
     * cuatro tablas, mas abajo.
     *
     * El porque de los 60.304, y por que el giro 7 no entra ni pagando,
     * estan en hfdl_ram.h.
     */
    uint8_t hfdl_buf[HFDL_RAM_TOTAL];


    struct
    {
        /* Bytes, no celdas: desde el 28/10/2026 cada celda ocupa medio
         * byte. Ver FT8_ADAPTER_MAG_BITS en ft8_waterfall_adapter.h. */
        uint8_t mag[FT8_ADAPTER_MAG_BYTES];
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
        uint8_t lado[FT8_ADAPTER_LADO_BYTES];
        float fft_re[FT8_FFT_SIZE];
        float fft_im[FT8_FFT_SIZE];
        /*
         * *** AQUI ESTABAN LAS CUATRO TABLAS, Y EL SELLO - 10/10/2026. ***
         *
         * fft_twiddle_cos, fft_twiddle_sin, fft_hann y fft_bitrev sumaban
         * 10.240 bytes -y el ultimo ya ni existe: lo calcula `rbit` en un
         * ciclo, ver ft8_bitrev()-, y ft8_fft_init() las RECONSTRUIA
         * en cada entrada
         * a FT8 porque cada salida las borraba. Son funcion pura de
         * FT8_FFT_SIZE, o sea unos numeros que no cambian nunca ocupando la
         * memoria mas disputada de la radio.
         *
         * Ahora viven en flash (User/ft8_tablas.c, generado por
         * tools/gen_ft8_tablas.py), con los MISMOS valores bit a bit.
         *
         * Y con ellas se fue fft_sello, que existia solo porque vivian aqui:
         * sin construir, fft_bitrev contenia indices de paleta y se usaba
         * COMO INDICE, asi que la radio se colgaba al entrar en FT8. Ese
         * cuelgue ya no existe, en vez de estar tapado por un sello.
         *
         * LO QUE SE HIZO CON EL HUECO, que es lo que de verdad importa: la
         * union mide lo que mida su miembro MAYOR -la cascada, 54.432 bytes-
         * asi que quitar 10.240 de aqui no bajaba la RAM ni un byte. Lo que
         * daba era SITIO, y ahi han entrado los seis buffers que FT8 tenia
         * FUERA de la union y que por tanto se sumaban a la cascada en vez
         * de compartirla. Ver `fuera` aqui abajo.
         */
        int16_t window[FT8_ADAPTER_NFFT];
        /* Solo los bins que el adaptador lee: ver FT8_ADAPTER_DB_BINS.
         * Con la FFT en 2.048 eran 1.024 floats y se miraba uno de cada
         * dos. */
        float dbout[FT8_ADAPTER_DB_BINS];

        /*
         * ==============================================================
         * LOS SEIS QUE ESTABAN FUERA. 10/10/2026.
         * ==============================================================
         *
         * Son de FT8, solo viven mientras FT8 esta puesto, y estaban
         * declarados cada uno en su fichero como `static`. O sea que el
         * enlazador los ponia ADEMAS de la cascada, no en vez de ella:
         * 10.748 bytes de SRAM que se sumaban a los 54.432 en vez de
         * compartirlos. Es exactamente el fallo que esta union nacio para
         * arreglar -ver el parrafo de arriba del todo- y que estos seis se
         * habian librado de pagar.
         *
         * No cabian hasta hoy: el miembro de FT8 medía 52.484 de los 54.432
         * y solo quedaban 1.948 de hueco. Sacando las cuatro tablas de la
         * FFT a flash quedan 12.188, y estos caben con 1.440 de sobra.
         *
         * POR QUE ESTAN AQUI COMO BYTES Y NO CON SU TIPO: dos de ellos son
         * arrays de tipos que viven en ft8_decoder.h y en las cabeceras de
         * Ft8Lib, y meter eso aqui haria que CUALQUIER fichero que incluya
         * esta cabecera -waterfall.c, splash_screen.c, hfdl_ram.c- arrastre
         * el decodificador entero. Cada dueño pone su tipo con un cast y un
         * _Static_assert que comprueba que cabe: si alguien agranda su
         * estructura, no compila.
         */
        float   ldpc_tov[174][3];    /* FTX_LDPC_N x 3  - ldpc.c  */
        float   ldpc_toc[83][7];     /* FTX_LDPC_M x 7  - ldpc.c  */
        int16_t deci_out[2][FT8_ADAPTER_SUBBLOCK_SIZE];  /* ft8_decimator.c */
        uint8_t cola[3520];          /* s_queue       - ft8_decoder.c (40 x 88) */
        uint8_t cands[1120];         /* candidate_list- ft8_decoder.c */
        char    lineas[48][88];      /* s_lin         - ft8_modo.c */
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
