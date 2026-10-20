/*
 * EL EJE DE dB SE REPINTABA EN TODOS LOS FOTOGRAMAS - 05/10/2026.
 *
 * Esta era una de ellas, y es de las que no se ven leyendo ninguno de los
 * dos ficheros implicados, porque los dos son correctos por separado:
 *
 *   - spec_agc.c mueve db_min y db_max con una media exponencial de coma
 *     flotante. Correcto: es un suavizado.
 *   - spec_chrome.c preguntaba "¿ha cambiado el eje?" con != de floats.
 *     Correcto si lo que se compara fueran valores discretos.
 *
 * Juntos: la media exponencial cambia el ultimo bit en CADA fotograma, asi
 * que la respuesta era "si" siempre y el eje se repintaba 45 veces por
 * segundo para dejar la pantalla exactamente igual. Son 11.843 accesos al
 * bus por fotograma -medidos aqui mismo- y ademas por el camino de la CPU,
 * que no se solapa con nada.
 *
 * COMO SE COMPRUEBA: se corre la autoescala DE VERDAD sobre un espectro
 * sintetico que se mueve despacio, y se cuentan los repintados en 300
 * fotogramas. Con la comparacion vieja -que este banco lleva dentro, para
 * poder enseñar los dos numeros- salen 300. Con la nueva tienen que salir
 * unos pocos: los fotogramas en los que el numero IMPRESO cambia de verdad.
 *
 * Y la otra mitad, que importa igual: que siga diciendo "si" cuando cambia
 * algo que se ve. Un comparador que conteste siempre "no" ahorraria todavia
 * mas bus y dejaria el eje congelado con numeros viejos.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "spec_chrome.h"
#include "spectrum.h"
#include "spec_agc.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

static void di(const char *que, int bien, const char *detalle)
{
    printf("    %-52s %s%s%s\n", que, bien ? "ok" : "*** MAL ***",
           detalle ? "  " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

/* La comparacion VIEJA, para poder enseñar los dos numeros uno al lado del
 * otro. No se usa en el firmware: vive aqui y solo aqui. */
static int vieja(const spec_chrome_t *a, const spec_chrome_t *b)
{
    return (a->db_min != b->db_min || a->db_max != b->db_max
            || a->cmap != b->cmap);
}

#define NBINS 256

int main(void)
{
    spec_chrome_t ant, nue;
    float db[NBINS];
    float dbmin = 60.0f, dbmax = 120.0f;
    int n_vieja = 0, n_nueva = 0, f, i;
    unsigned long sem = 20261005UL;

    printf("\nEL EJE DE dB: CUANTAS VECES SE REPINTA DE VERDAD\n\n");

    spectrum_init();

    ant.db_min = dbmin; ant.db_max = dbmax;
    ant.cmap = spectrum_colormap_lut();
    ant.center_hz = 7200000; ant.span_hz = 96000;
    ant.demod_px = 0; ant.band = SPC_BAND_NONE; ant.band_hz = 0;
    nue = ant;

    /*
     * 600 fotogramas de un espectro que se mueve DESPACIO: un suelo de ruido
     * quieto y una señal que sube y baja medio decibelio. Es el caso normal
     * de una emisora recibida.
     *
     * Y SE CUENTAN SOLO LOS 300 ULTIMOS, a proposito. Los 300 primeros son
     * la autoescala viniendo desde su valor inicial hasta el de la señal:
     * ahi los numeros cambian DE VERDAD, decibelio a decibelio, y repintar
     * es lo correcto. Lo que se quiere medir es el regimen permanente -la
     * radio quieta sobre una emisora-, que es donde se pasaba el dia
     * repintando lo mismo.
     */
    for (f = 0; f < 600; f++) {
        /*
         * CON RUIDO DE VERDAD, Y ESTO NO ES UN ADORNO.
         *
         * La primera version de este banco ponia un suelo de ruido FIJO, el
         * mismo en todos los fotogramas. Con eso la media exponencial
         * converge del todo -la diferencia se queda por debajo del ultimo
         * bit del float- y hasta la comparacion vieja contestaba "no ha
         * cambiado": el banco daba 0 y 0 y no probaba nada.
         *
         * En la radio de verdad el suelo de ruido se mueve en cada
         * fotograma, asi que los percentiles que calcula la autoescala no
         * paran quietos y la media exponencial no llega a converger NUNCA.
         * Eso es lo que hacia que el != de floats contestara "si" siempre.
         * Sin ruido aqui, este banco no mide el fallo que existe.
         */
        for (i = 0; i < NBINS; i++) {
            sem = sem * 1103515245UL + 12345UL;
            db[i] = 70.0f + 0.5f * (float)((i * 37) % 7)
                    + 3.0f * ((float)((sem >> 16) & 0x7FFF) / 32767.0f - 0.5f);
        }
        /*
         * Y UNA EMISORA QUE SE DESVANECE, que es lo que de verdad mueve la
         * autoescala. Ocho decibelios arriba y abajo con un periodo de unos
         * cuatro segundos: eso cambia de casilla el percentil alto cada
         * pocos fotogramas, y CADA cambio de casilla arranca una cola de
         * convergencia de la media exponencial que dura ~200 fotogramas. En
         * esa cola el float cambia en TODOS los fotogramas y el numero
         * impreso cambia una o dos veces. Ahi esta la diferencia entre las
         * dos comparaciones, y por eso hace falta señal que se mueva: con
         * una escena quieta la media converge del todo, el float se para y
         * hasta la comparacion vieja dice que no.
         */
        db[128] = 112.0f + 8.0f * (float)sin((double)f * 0.035);

        nue.db_min = dbmin; nue.db_max = dbmax;
        spec_agc_step(db, NBINS, &dbmin, &dbmax);
        nue.db_min = dbmin; nue.db_max = dbmax;

        if (f >= 300) {
            if (vieja(&ant, &nue))                    { n_vieja++; }
            if (spec_chrome_axis_changed(&ant, &nue)) { n_nueva++; }
        }
        ant = nue;
    }

    printf("  con la autoescala corriendo, 300 fotogramas ya asentados\n");
    printf("      comparando floats con !=   : %3d repintados\n", n_vieja);
    printf("      comparando lo que se IMPRIME: %3d repintados\n", n_nueva);
    {
        char det[64];
        snprintf(det, sizeof det, "%d accesos al bus ahorrados",
                 (n_vieja - n_nueva) * 11843);
        di("el eje deja de repintarse en cada fotograma", n_nueva < 20, det);
    }
    di("y la comparacion vieja si repintaba siempre", n_vieja > 290, 0);

    /*
     * Y QUE NO SE HAYA QUEDADO MUDO. Un comparador que conteste siempre que
     * no ahorraria mas bus todavia y dejaria el eje con numeros viejos, que
     * es peor que repintar de mas.
     */
    printf("\n  y que sigue diciendo que si cuando cambia lo que se ve\n");
    {
        spec_chrome_t a = ant, b = ant;
        b.db_max = ant.db_max + 1.0f;
        di("un decibelio entero arriba si se nota",
           spec_chrome_axis_changed(&a, &b) != 0U, 0);
        b = ant; b.db_min = ant.db_min - 1.0f;
        di("un decibelio entero abajo tambien",
           spec_chrome_axis_changed(&a, &b) != 0U, 0);
        b = ant; b.cmap = 0;
        di("cambiar de paleta tambien",
           spec_chrome_axis_changed(&a, &b) != 0U, 0);
        b = ant; b.db_max = ant.db_max + 0.001f;
        di("y una milesima, que no se ve, NO",
           spec_chrome_axis_changed(&a, &b) == 0U, 0);
    }

    /*
     * LAS DIVISIONES Y SUS NUMEROS TIENEN QUE CAER EN EL MISMO SITIO.
     *
     * spectrum.c dibuja SPECTRUM_GRID_ROWS lineas en h*gi/(ROWS+1). La
     * canaleta ponia sus numeros con un 3 escrito a mano mientras el
     * espectro dibujaba 4 lineas: ni un solo numero caia sobre una linea.
     * El comentario de spec_chrome.c ya decia que si las dos cuentas se
     * separaban el eje mentiria; se habian separado.
     */
    printf("\n  los numeros caen sobre las lineas\n");
    {
        char det[64];
        snprintf(det, sizeof det, "%d divisiones", (int)SPECTRUM_GRID_ROWS);
        di("la canaleta usa las lineas que dibuja el espectro",
           spec_chrome_divisiones() == (int)SPECTRUM_GRID_ROWS, det);
    }

    /*
     * Y LO MISMO PARA LA REGLA DE FRECUENCIA - 19/10/2026.
     *
     * spec_chrome_ruler_changed() comparaba center_hz EN HERCIOS mientras la
     * regla imprime kHz: con el span de serie un pixel son 127 Hz, asi que
     * cualquier clic pequeño repintaba 20.834 accesos al bus para dejar la
     * pantalla igual. Es el mismo fallo que el eje tenia arreglado desde el
     * 05/10 y que aqui no se habia mirado.
     */
    printf("\n  la regla se repinta cuando cambia lo que se LEE\n");
    {
        spec_chrome_t a, b;

        memset(&a, 0, sizeof a);
        a.center_hz  = 7100000UL;
        a.span_hz    = 96000UL;
        a.demod_px   = 378;
        a.en_banda   = 1U;
        a.fam_banda  = 0U;
        a.eje_en_hz  = 0U;

        b = a; b.center_hz = a.center_hz + 1UL;
        di("un hercio, que no se lee, NO repinta",
           spec_chrome_ruler_changed(&a, &b) == 0U, 0);
        b = a; b.center_hz = a.center_hz + 100UL;
        di("cien hercios tampoco",
           spec_chrome_ruler_changed(&a, &b) == 0U, 0);
        b = a; b.center_hz = a.center_hz + 1000UL;
        di("un kilohercio SI, que ahi cambia el numero",
           spec_chrome_ruler_changed(&a, &b) != 0U, 0);
        b = a; b.span_hz = 48000UL;
        di("cambiar el span SI",
           spec_chrome_ruler_changed(&a, &b) != 0U, 0);
        b = a; b.demod_px = a.demod_px + 200;
        di("mover el punto de demodulacion SI",
           spec_chrome_ruler_changed(&a, &b) != 0U, 0);
        b = a; b.fam_banda = 2U;
        di("cambiar de clase de banda SI, que la raya es de otro color",
           spec_chrome_ruler_changed(&a, &b) != 0U, 0);
        b = a; b.eje_en_hz = 1U;
        di("y pasar el eje a hercios SI",
           spec_chrome_ruler_changed(&a, &b) != 0U, 0);
    }

    printf("\n  %s\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}
