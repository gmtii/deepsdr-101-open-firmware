/*
 * ======================================================================
 * CUANTO BUS GASTA CADA ESTILO DE TRAZA.
 * ======================================================================
 *
 * EL PORQUE, con el audio del usuario medido delante. Con el espectro en
 * pantalla, el siseo sube hasta 10 dB respecto a tener el menu abierto, y
 * late a 374,8 Hz -que es 96.000/256, el ritmo del bloque de audio- y a
 * 42-44 Hz, el ritmo del cuadro. Con el menu abierto esas dos rayas NO
 * EXISTEN.
 *
 * El acoplo es electrico: el usuario midio en su dia que el ruido sube con
 * la ganancia de entrada -o sea que entra ANTES del PGA- y el 30/10/2026
 * comprobo que NO baja con el brillo. No es la retroiluminacion: es el
 * repintado.
 *
 * Y si es el repintado, lo que manda es CUANTOS PIXELES SE EMPUJAN POR EL
 * BUS. Eso es lo que esto cuenta, con el User/spectrum.c de verdad por
 * debajo, para los tres estilos de traza y para la cascada. Asi se puede
 * decir con un numero cual es el ajuste que menos ruido mete, en vez de
 * mandar al usuario a probar a ciegas.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

const palette_t *g_pal = &k_pal_oscura;

static int fallos;
static void di(int cond, const char *que, const char *como)
{
    printf("    %-52s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { printf("        %s\n", como); fallos++; }
}

#define BINS  1024
#define ANCHO 756
#define ALTO  208

static float g_db[BINS];

/*
 * LA BANDA QUIETA, PERO VIVA. 01/11/2026.
 *
 * El primer intento de medir "quieto contra sintonizando" repetia la
 * MISMA escena bit a bit, y eso no es ninguna radio: con la traza
 * congelada el camino parcial de spectrum.c no tiene nada que repintar y
 * el suelo sale regalado. La radio quieta tiene ruido, o sea una traza
 * que baila unos decibelios en todas las columnas aunque el dial no se
 * mueva.
 *
 * Asi que el suelo es ESTE: las mismas portadoras en el mismo sitio -el
 * dial no se ha movido- y el suelo de ruido distinto en cada cuadro.
 * Contra eso si vale comparar lo que cuesta sintonizar, que ademas de
 * mover el ruido MUEVE LAS PORTADORAS.
 */
static void panorama_ruido(int semilla)
{
    unsigned s = (unsigned)semilla * 1103515245u + 12345u;
    int i, k;

    for (i = 0; i < BINS; i++) {
        s = s * 1103515245u + 12345u;
        g_db[i] = -110.0f + 3.0f * sinf((float)i * 0.11f)
                  + 2.5f * ((float)((s >> 16) & 0xFFFFu) / 32768.0f - 1.0f);
    }
    for (k = 0; k < 9; k++) {
        int c = (k * 113 + 37) % BINS;
        int w;
        for (w = -3; w <= 3; w++) {
            int x = c + w;
            if (x < 0 || x >= BINS) { continue; }
            g_db[x] = -40.0f + 4.0f * (float)(w < 0 ? -w : w);
        }
    }
}

static void panorama(int corr)
{
    int i, k;
    for (i = 0; i < BINS; i++) {
        g_db[i] = -110.0f + 3.0f * sinf((float)(i + corr) * 0.11f);
    }
    for (k = 0; k < 9; k++) {
        int c = ((k * 113 + 37) + corr) % BINS;
        int w;
        for (w = -3; w <= 3; w++) {
            int x = c + w;
            if (x < 0 || x >= BINS) { continue; }
            g_db[x] = -40.0f + 4.0f * (float)(w < 0 ? -w : w);
        }
    }
}

/* Un puñado de cuadros seguidos, como en la radio, y lo que se ha empujado
 * por el bus de media por cuadro. */
static double por_cuadro(int estilo, int cuadros)
{
    int f;

    spectrum_set_style((spectrum_style_t)estilo);
    /* dos cuadros de calentamiento: el parcial necesita uno anterior */
    panorama(0);
    spectrum_draw(g_db, BINS, 40, 108, ANCHO, ALTO, -120.0f, -20.0f,
                  ANCHO / 2, 0U, 0, 0);
    panorama(1);
    spectrum_draw(g_db, BINS, 40, 108, ANCHO, ALTO, -120.0f, -20.0f,
                  ANCHO / 2, 0U, 0, 0);

    lcd_sim_cost_reset();
    for (f = 0; f < cuadros; f++) {
        panorama(2 + f);
        spectrum_draw(g_db, BINS, 40, 108, ANCHO, ALTO, -120.0f, -20.0f,
                      ANCHO / 2, 0U, 0, 0);
    }
    /* `pixels` del simulador: escrituras que acaban siendo un pixel
     * visible. Es exactamente lo que conmuta el bus del panel. */
    return (double)lcd_sim_cost().pixels / (double)cuadros;
}

/*
 * ======================================================================
 * Y CUANTA CPU, QUE ES LO QUE DE VERDAD CUESTA - 01/11/2026.
 * ======================================================================
 *
 * EL PORQUE, con la pantalla del usuario delante. Su fila "Fotograma"
 * dice `2+0+16(0)+7 ms`: dos de FFT, cero de marco, DIECISEIS de traza,
 * CERO de empujar pixeles, y siete de cascada. El parentesis es
 * s_push_us, y el propio main.c dice como leerlo: "si este numero es casi
 * toda la traza, el problema es el bus; si es una parte pequena, el
 * problema es el bucle que decide los pixeles, y el bus no tiene nada que
 * ver".
 *
 * Cero de dieciseis. Los pixeles los empuja el DMA y son gratis; lo que
 * cuesta es el bucle. Asi que contar pixeles -lo que hace la seccion de
 * arriba- mide el RUIDO que mete el panel, que para eso nacio, pero NO
 * mide el tiempo. Para el tiempo hace falta un cronometro.
 *
 * Y la pregunta concreta es esta: el usuario dice que el espectro se para
 * EN CUANTO MUEVE LA FRECUENCIA, y no con el volumen. La diferencia entre
 * las dos es que mover la frecuencia cambia TODAS las columnas a la vez,
 * y spectrum.c solo recalcula las bandas que cambian (s_parcial_hay y
 * s_tramo_techo). O sea que sintonizar deberia costar bastante mas que
 * estar quieto. Esto lo mide.
 */
static double cpu_por_cuadro(int estilo, int cuadros, int mover)
{
    struct timespec t0, t1;
    int f;

    spectrum_set_style((spectrum_style_t)estilo);
    /* Dos de calentamiento: el parcial necesita un cuadro anterior. */
    panorama(0);
    spectrum_draw(g_db, BINS, 40, 108, ANCHO, ALTO, -120.0f, -20.0f,
                  ANCHO / 2, 0U, 0, 0);
    if (mover) { panorama(1); } else { panorama_ruido(1); }
    spectrum_draw(g_db, BINS, 40, 108, ANCHO, ALTO, -120.0f, -20.0f,
                  ANCHO / 2, 0U, 0, 0);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (f = 0; f < cuadros; f++) {
        /* Quieto: la MISMA escena una y otra vez, que es la radio sin
         * tocar el mando -la traza se mueve sola con el ruido, pero aqui
         * se quiere el suelo-. Moviendo: una escena distinta cada cuadro,
         * que es el dial girando. */
        /* Quieto: el dial no se mueve, pero el ruido si -ver
         * panorama_ruido()-. Moviendo: ademas se desplaza la banda
         * entera, que es el dial girando. */
        if (mover) { panorama(2 + f); } else { panorama_ruido(2 + f); }
        spectrum_draw(g_db, BINS, 40, 108, ANCHO, ALTO, -120.0f, -20.0f,
                      ANCHO / 2, 0U, 0, 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    return ((double)(t1.tv_sec - t0.tv_sec) * 1e6
            + (double)(t1.tv_nsec - t0.tv_nsec) / 1e3) / (double)cuadros;
}

int main(void)
{
    double relleno, linea, contorno;
    char det[120];

    printf("\n== CUANTO BUS GASTA CADA ESTILO DE TRAZA ==\n\n");
    lcd_sim_reset(0x0000);

    relleno  = por_cuadro((int)SPECTRUM_STYLE_HEATMAP, 30);
    linea    = por_cuadro((int)SPECTRUM_STYLE_LINE, 30);
    contorno = por_cuadro((int)SPECTRUM_STYLE_OUTLINE, 30);

    printf("    el panel de la traza son %d x %d = %d pixeles\n\n",
           ANCHO, ALTO, ANCHO * ALTO);
    printf("      estilo        pixeles por cuadro    del panel\n");
    printf("      Relleno       %10.0f          %5.1f %%\n",
           relleno, 100.0 * relleno / (double)(ANCHO * ALTO));
    printf("      Linea         %10.0f          %5.1f %%\n",
           linea, 100.0 * linea / (double)(ANCHO * ALTO));
    printf("      Contorno      %10.0f          %5.1f %%\n",
           contorno, 100.0 * contorno / (double)(ANCHO * ALTO));
    printf("\n");
    printf("      a 45 cuadros por segundo eso son:\n");
    printf("      Relleno       %8.2f Mpixel/s\n", relleno * 45.0 / 1e6);
    printf("      Linea         %8.2f Mpixel/s\n", linea * 45.0 / 1e6);
    printf("      Contorno      %8.2f Mpixel/s\n", contorno * 45.0 / 1e6);
    printf("\n");

    /*
     * ==================================================================
     * LO QUE ESTE BANCO HA CONTESTADO, Y NO ERA LO QUE YO ESPERABA.
     * ==================================================================
     *
     * Fui a medirlo dando por hecho que "Relleno" empujaria mucho mas que
     * "Linea" -pinta una columna entera bajo la traza en vez de una raya-
     * y que bastaria con recomendar un estilo. LOS TRES EMPUJAN LO MISMO:
     * un 80 % del panel en cada cuadro.
     *
     * Y tiene explicacion, que esta en el propio spectrum.c: el panel no
     * se pinta pixel a pixel sino POR BANDAS de 16 filas, y de cada banda
     * se manda el tramo que va del PRIMER segmento que cambia al ULTIMO.
     * Con una traza que se mueve por todas partes -y una traza de ruido se
     * mueve por todas partes-, el primero esta cerca del borde izquierdo y
     * el ultimo cerca del derecho, asi que se manda la banda entera. El
     * estilo cambia QUE se pinta dentro de la banda, no CUANTA banda se
     * manda.
     *
     * CONSECUENCIA, y es la que importa: el estilo no es la palanca. La
     * unica palanca que mueve esto en un orden de magnitud son los CUADROS
     * POR SEGUNDO, porque lo que se empuja por segundo es pixeles por
     * cuadro x cuadros por segundo y el primer factor esta clavado en el
     * 80 % del panel.
     *
     * Por eso lo que se exige aqui no es que un estilo gane: es que el
     * numero siga siendo el que es. Si alguien consigue bajarlo de verdad
     * -pintando solo los segmentos que cambian en vez del tramo entero-
     * esto se pondra rojo, y ese rojo sera una buena noticia que habra que
     * leer antes de tocar el numero.
     */
    /* ---------------------------------------------------------------- */
    {
        double quieto, moviendo;
        int    e;
        static const struct { int v; const char *n; } es[3] = {
            { (int)SPECTRUM_STYLE_HEATMAP, "Relleno"  },
            { (int)SPECTRUM_STYLE_LINE,    "Linea"    },
            { (int)SPECTRUM_STYLE_OUTLINE, "Contorno" },
        };

        printf("== Y CUANTA CPU: QUIETO CONTRA SINTONIZANDO ==\n\n");
        printf("      estilo        quieto      sintonizando    veces\n");
        for (e = 0; e < 3; e++) {
            quieto   = cpu_por_cuadro(es[e].v, 60, 0);
            moviendo = cpu_por_cuadro(es[e].v, 60, 1);
            printf("      %-12s %8.0f us   %8.0f us    %5.2f\n",
                   es[e].n, quieto, moviendo,
                   (quieto > 0.0) ? (moviendo / quieto) : 0.0);
        }
        printf("\n");
    }

    snprintf(det, sizeof det, "relleno %.0f, linea %.0f, contorno %.0f",
             relleno, linea, contorno);
    di(relleno > 0.0 && linea > 0.0 && contorno > 0.0,
       "los tres estilos empujan algo", det);
    di(relleno <= (double)(ANCHO * ALTO) &&
       linea <= (double)(ANCHO * ALTO) &&
       contorno <= (double)(ANCHO * ALTO),
       "y ninguno empuja mas que el panel entero", det);
    /*
     * Y EL ROJO LLEGO EL MISMO DIA. Aqui ponia "y hoy los tres se van al
     * 80 % del panel", que era el numero de la primera medida. Mandando
     * cada RACHA de segmentos por separado en vez del tramo entero del
     * primero al ultimo -ver spectrum.c- ese 79,5 % se quedo en 33,2:
     *
     *     antes   124.939 pixeles por cuadro   5,62 Mpixel/s a 45 fps
     *     ahora    52.132 pixeles por cuadro   2,35 Mpixel/s
     *
     * 2,4 veces menos bus, y sim/espectro_oro.c dice que no cambia ni un
     * pixel de lo que se ve. Lo que se exige ahora es que no se vuelva a
     * subir: el tope es el 40 % del panel, con sitio para que la escena
     * del banco respire sin dejar pasar una vuelta atras.
     */
    di(relleno < (double)(ANCHO * ALTO) * 0.40 &&
       linea   < (double)(ANCHO * ALTO) * 0.40 &&
       contorno < (double)(ANCHO * ALTO) * 0.40,
       "y ninguno pasa del 40 % del panel por cuadro", det);

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
