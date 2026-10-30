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
