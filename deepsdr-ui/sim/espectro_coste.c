/*
 * DONDE SE VAN LOS MILISEGUNDOS DE LA TRAZA
 * =========================================
 *
 * En la radio, la fila "Fotograma" dice que el espectro se lleva 22 ms de
 * cada cuadro con el bus de la pantalla practicamente parado. O sea que no
 * es empujar pixeles: es DECIDIRLOS. Y ahi caben dos cosas muy distintas,
 * que es justo lo que no se sabia:
 *
 *   - lo que cuesta por COLUMNA: las pasadas 1, 1.5, 1.6 y la plantilla,
 *     que recorren las 756 columnas unas cuantas veces;
 *   - lo que cuesta por PIXEL: el montaje de las bandas, que son 756 x 208
 *     = 157.248 pixeles por cuadro.
 *
 * Las dos crecen con el tamaño del panel, pero no con el MISMO tamaño: una
 * va con w y la otra con w*h. Asi que se mide el dibujado entero con el
 * ancho y el alto cambiados por separado y se despeja cual es cual:
 *
 *      T(w,h) = fijo + por_columna*w + por_pixel*w*h
 *
 * No hace falta que el reloj del PC se parezca al de la radio: lo que se
 * busca es el REPARTO, y ese es el mismo codigo en los dos sitios. Si el
 * grueso esta en el termino de w*h, lo que hay que tocar es el montaje de
 * las bandas; si esta en el de w, son las pasadas por columna, y entonces
 * encoger el panel no arregla nada.
 *
 * Compila el User/spectrum.c DE VERDAD, no la copia de vendor/.
 */
#include "lcd_sim.h"
#include "gfx.h"
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

const palette_t *g_pal = &k_pal_oscura;

#define BINS 796
#define PX   40
#define PY   108
#define PW   756
#define PH   208

static float g_db[BINS];

/* La misma señal que la imagen patron: suelo, un pico estrecho, uno ancho,
 * una rampa y un escalon vertical. */
static void senal(int semilla)
{
    int i;
    unsigned s = (unsigned)semilla * 2654435761U + 12345U;

    for (i = 0; i < BINS; i++) {
        s = s * 1103515245U + 12345U;
        g_db[i] = -104.0f + (float)((s >> 16) % 7U);
    }
    for (i = 100; i < 104; i++) { g_db[i] = -30.0f; }
    for (i = 300; i < 340; i++) { g_db[i] = -45.0f; }
    for (i = 500; i < 520; i++) { g_db[i] = -95.0f + (float)((i - 500) * 3); }
    for (i = 600; i < 610; i++) { g_db[i] = -25.0f; }
}

static double ahora_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
}

/*
 * Un cuadro, con el repintado parcial DESARMADO a proposito: se mueve la
 * señal entre cuadro y cuadro para que todas las columnas cambien. Lo que
 * interesa medir es lo que cuesta un cuadro entero, que es el techo; con
 * la traza quieta el firmware se salta bandas y sale lo que sea.
 */
static double un_cuadro(uint16_t w, uint16_t h, int n, uint64_t *accesos)
{
    double t0, t1;
    int k;

    senal(1);
    spectrum_draw(g_db, BINS, PX, PY, w, h, -110.0f, -20.0f, 0, 0U, 0, 0);
    lcd_sim_cost_reset();
    t0 = ahora_us();
    for (k = 0; k < n; k++) {
        senal(k + 2);   /* señal distinta: ninguna banda se puede saltar */
        spectrum_draw(g_db, BINS, PX, PY, w, h, -110.0f, -20.0f, 0, 0U, 0, 0);
    }
    t1 = ahora_us();
    *accesos = lcd_sim_cost().bus_writes / (uint64_t)n;
    return (t1 - t0) / (double)n;
}

/* La mejor de varias corridas: en un PC con mas cosas corriendo, la media
 * mide el ruido y la minima mide el codigo. */
static double mejor(uint16_t w, uint16_t h, int n, uint64_t *accesos)
{
    double m = 1e30;
    int i;

    for (i = 0; i < 5; i++) {
        double t = un_cuadro(w, h, n, accesos);
        if (t < m) { m = t; }
    }
    return m;
}

int main(void)
{
    uint64_t acc;
    double t_alto, t_medio, por_pixel;
    const int N = 200;

    lcd_sim_reset(0x0000);
    spectrum_set_style(SPECTRUM_STYLE_HEATMAP);
    spectrum_set_line_smooth(0U);
    spectrum_set_bridge(1U);

    printf("\n== LO QUE CUESTA UN CUADRO ENTERO DE ESPECTRO ==\n\n");

    t_alto  = mejor(PW, PH, N, &acc);
    printf("  %3d x %3d  %8.1f us   %6llu accesos al bus\n",
           PW, PH, t_alto, (unsigned long long)acc);
    t_medio = mejor(PW, PH / 2U, N, &acc);
    printf("  %3d x %3d  %8.1f us   %6llu accesos al bus\n",
           PW, PH / 2, t_medio, (unsigned long long)acc);

    /*
     * Con el ancho igual y el alto a la mitad, lo que se va es la MITAD de
     * los pixeles y ni una columna: las pasadas 1, 1.5, 1.6 y la plantilla
     * recorren las mismas 756. Asi que la diferencia es medio termino de
     * pixel, y no hace falta ajustar nada ni inventarse un termino fijo.
     */
    por_pixel = 2.0 * (t_alto - t_medio);

    printf("\n== EL REPARTO ==\n\n");
    printf("  por pixel          %7.1f us   %4.1f %% del cuadro\n",
           por_pixel, 100.0 * por_pixel / t_alto);
    printf("  por columna y fijo %7.1f us   %4.1f %%\n",
           t_alto - por_pixel, 100.0 * (t_alto - por_pixel) / t_alto);

    printf("\n  LO QUE ESTO DICE Y LO QUE NO DICE.\n\n");
    printf("  DICE que el cuadro se lo come el termino de PIXEL, no el de\n");
    printf("  columna: encoger el panel a lo ancho no arregla nada, y lo que\n");
    printf("  hay que tocar es el montaje de las bandas.\n\n");
    printf("  NO DICE cuantos milisegundos son en la radio. Este PC tiene\n");
    printf("  cache y el microcontrolador no, y el relleno escribe en\n");
    printf("  vertical -una linea de cache por pixel-, asi que aqui ese\n");
    printf("  termino sale castigado de una forma que alli no pasa. Para la\n");
    printf("  radio el numero bueno es el del desensamblado de ARM, y son\n");
    printf("  CINCO instrucciones por pixel de relleno (eran siete).\n");
    printf("\n");
    return 0;
}
