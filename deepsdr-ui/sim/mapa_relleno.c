/*
 * ======================================================================
 * LO QUE COSTARIA RELLENAR LA TIERRA - 13/10/2026
 * ======================================================================
 *
 * EL COLOR YA ESTA. MAPA_COL_TIERRA es el hueco 1 de los quince colores que
 * lleva CADA uno de los cien .map, esta en el enum y esta en
 * tools/mapa_pack.py... y no lo lee nadie. mapa.c rellena el panel entero
 * con t.mar y dibuja las costas encima. Por eso el catalogo dice "el estilo
 * HOY NO CAMBIA EL DIBUJO" y por eso 013, 014 y 015 son el mismo fichero.
 *
 * LO QUE FALTA ES RELLENAR, y los datos son TRAZOS, no poligonos: n puntos y
 * saltos de 1/32 de grado, sin anillo cerrado ni sentido de giro. O sea que
 * no se sabe que lado es tierra. Esto NO resuelve eso -eso es trabajo del
 * empaquetador-: esto mide LO QUE COSTARIA el relleno una vez resuelto, que
 * es lo que decide si se hace o no.
 *
 * COMO SE MIDE SIN INVENTARSE LA GEOMETRIA. Se intercepta gfx2_line() y se
 * guardan los segmentos que el mapa dibuja DE VERDAD en un fotograma, ya en
 * coordenadas de pantalla y al zoom que toque. Esos segmentos SON la costa:
 * el numero de cruces por linea de barrido es una propiedad de esa geometria
 * y no depende de que los anillos esten cerrados. Cerrarlos añade un
 * segmento por trazo, que se cuenta aparte y es de segundo orden.
 *
 * Y SE COMPARA CONTRA LO QUE YA CUESTA EL MAPA, no contra un numero
 * inventado: el dibujado de hoy ya cabe en el presupuesto de cuadro de
 * 22 ms, asi que la RAZON entre las dos es lo unico que se transporta del
 * PC al Cortex-M4. El valor absoluto de aqui no vale para nada.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "mapa.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

#define ALTO     288
#define MAX_SEG  40000

typedef struct { int16_t x0, y0, x1, y1; } seg_t;
static seg_t  g_seg[MAX_SEG];
static int    g_nseg;
static int    g_capturando;

void __real_gfx2_line(gfx2_surf_t *s, int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1, gfx2_rgba_t c);

/*
 * LAS COORDENADAS YA SON DE PANTALLA, y eso hay que saberlo o la captura
 * sale deformada: gfx2.h lo dice -"las primitivas reciben coordenadas de
 * PANTALLA y recortan solas contra la banda"-. La primera version de este
 * banco les sumaba el origen de la banda y no compilaba porque el campo que
 * buscaba no existe. Menos mal: sumarlo habria dado una costa estirada
 * dieciocho veces y un numero de cruces inventado.
 *
 * gfx2 compone en bandas de 16 filas y llama al dibujo una vez por banda,
 * pero la guarda de banda de mapa.c ya filtra: a cada banda solo le llegan
 * los segmentos que la cruzan. O sea que lo que se captura en un fotograma
 * es el conjunto que el mapa dibuja DE VERDAD, sin repetir.
 */
void __wrap_gfx2_line(gfx2_surf_t *s, int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1, gfx2_rgba_t c)
{
    if (g_capturando && g_nseg < MAX_SEG) {
        g_seg[g_nseg].x0 = x0; g_seg[g_nseg].y0 = y0;
        g_seg[g_nseg].x1 = x1; g_seg[g_nseg].y1 = y1;
        g_nseg++;
    }
    __real_gfx2_line(s, x0, y0, x1, y1, c);
}

static mapa_caja_t g_caja;
static void pinta(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
    mapa_dibuja(s, &g_caja);
}

static double ahora(void) { return (double)clock() / (double)CLOCKS_PER_SEC; }

/* --------------------------------------------------------------------
 * EL RELLENO: tabla de aristas activas.
 *
 * La version ingenua -por cada linea, mirar todas las aristas- es
 * 288 * N y no hace falta medirla para saber que no cabe: se cuenta
 * aparte, para tener el numero.
 *
 * La buena ordena las aristas por su y menor y mantiene una lista de las
 * que cruzan la linea actual. El trabajo total es N log N de la ordenacion
 * mas la suma de cruces de todas las lineas, que es lo que de verdad se
 * paga.
 * -------------------------------------------------------------------- */
typedef struct { int16_t ymin, ymax; float x, dxdy; } arista_t;
static arista_t g_ar[MAX_SEG];
static int      g_nar;
static int      g_orden[MAX_SEG];
static float    g_cruce[2048];

static int cmp_ymin(const void *a, const void *b)
{
    int ia = *(const int *)a, ib = *(const int *)b;
    return (int)g_ar[ia].ymin - (int)g_ar[ib].ymin;
}

static unsigned long g_trabajo;   /* cruces sumados en todas las lineas */
static unsigned long g_pix;       /* pixeles rellenados */

static void rellena(uint16_t *lienzo)
{
    int activas[4096];
    int n_act = 0, sig = 0, y, i;

    g_nar = 0;
    for (i = 0; i < g_nseg; i++) {
        int y0 = g_seg[i].y0, y1 = g_seg[i].y1;
        float x0 = (float)g_seg[i].x0, x1 = (float)g_seg[i].x1;

        if (y0 == y1) { continue; }            /* horizontal: no cruza */
        if (y0 > y1) { int t = y0; y0 = y1; y1 = t;
                       { float f = x0; x0 = x1; x1 = f; } }
        g_ar[g_nar].ymin = (int16_t)y0;
        g_ar[g_nar].ymax = (int16_t)y1;
        g_ar[g_nar].x    = x0;
        g_ar[g_nar].dxdy = (x1 - x0) / (float)(y1 - y0);
        g_nar++;
    }

    for (i = 0; i < g_nar; i++) { g_orden[i] = i; }
    qsort(g_orden, (size_t)g_nar, sizeof g_orden[0], cmp_ymin);

    for (y = 0; y < ALTO; y++) {
        int nc = 0;

        while (sig < g_nar && g_ar[g_orden[sig]].ymin <= y) {
            if (n_act < 4096) { activas[n_act++] = g_orden[sig]; }
            sig++;
        }
        for (i = 0; i < n_act; ) {
            if (g_ar[activas[i]].ymax <= y) { activas[i] = activas[--n_act]; }
            else { i++; }
        }
        for (i = 0; i < n_act && nc < 2048; i++) {
            const arista_t *a = &g_ar[activas[i]];
            g_cruce[nc++] = a->x + a->dxdy * (float)(y - a->ymin);
            g_trabajo++;
        }
        /* ordenar los cruces: insercion, que es lo que se usaria en la
         * radio porque nc es pequeño casi siempre */
        for (i = 1; i < nc; i++) {
            float v = g_cruce[i]; int j = i - 1;
            while (j >= 0 && g_cruce[j] > v) { g_cruce[j + 1] = g_cruce[j]; j--; }
            g_cruce[j + 1] = v;
        }
        for (i = 0; i + 1 < nc; i += 2) {
            int xa = (int)(g_cruce[i] + 0.5f), xb = (int)(g_cruce[i + 1] + 0.5f);
            int x;
            if (xa < 0) { xa = 0; }
            if (xb > GFX2_W) { xb = GFX2_W; }
            for (x = xa; x < xb; x++) { lienzo[y * GFX2_W + x] = 0x3B2A; g_pix++; }
        }
    }
}

int main(void)
{
    static const uint8_t escalas[] = { 1U, 2U, 4U };
    static uint16_t lienzo[GFX2_W * ALTO];
    unsigned e;
    char det[160];
    double peor_razon = 0.0;

    printf("\n== LO QUE COSTARIA RELLENAR LA TIERRA ==\n");
    printf("\n  un fotograma del hueco de FT8 (800x288), %d lineas\n\n", ALTO);

    for (e = 0; e < sizeof escalas / sizeof escalas[0]; e++) {
        double t_dib, t_rel, t0, razon;
        unsigned long ingenuo;
        int vueltas = 50, k;

        memset(&g_caja, 0, sizeof g_caja);
        mapa_pon(&g_caja, 0, 0, GFX2_W, ALTO);
        if (escalas[e] > 1U) {
            (void)mapa_zoom(&g_caja, escalas[e], GFX2_W / 2, ALTO / 2);
        }

        /* 1. capturar los segmentos de un fotograma */
        g_nseg = 0; g_capturando = 1;
        lcd_sim_reset(0x0000);
        gfx2_render(0, 0, GFX2_W, ALTO, pinta, 0);
        g_capturando = 0;

        /* 2. cronometrar el dibujado de hoy */
        t0 = ahora();
        for (k = 0; k < vueltas; k++) {
            lcd_sim_reset(0x0000);
            gfx2_render(0, 0, GFX2_W, ALTO, pinta, 0);
        }
        t_dib = (ahora() - t0) / vueltas;

        /* 3. cronometrar el relleno sobre esa misma geometria */
        g_trabajo = 0UL; g_pix = 0UL;
        t0 = ahora();
        for (k = 0; k < vueltas; k++) { rellena(lienzo); }
        t_rel = (ahora() - t0) / vueltas;

        ingenuo = (unsigned long)g_nseg * (unsigned long)ALTO;
        razon = (t_dib > 0.0) ? (t_rel / t_dib) : 0.0;
        if (razon > peor_razon) { peor_razon = razon; }

        printf("    escala %u\n", (unsigned)escalas[e]);
        printf("      segmentos de costa en el fotograma   %8d\n", g_nseg);
        printf("      cruces que paga la tabla de aristas  %8lu\n", g_trabajo / vueltas);
        printf("      lo que pagaria mirarlas todas        %8lu  (%.0fx mas)\n",
               ingenuo, g_trabajo ? (double)ingenuo / ((double)g_trabajo / vueltas) : 0.0);
        printf("      pixeles rellenados                   %8lu\n", g_pix / vueltas);
        printf("      dibujado de hoy  %7.2f ms   relleno  %7.2f ms   razon %.2f\n",
               t_dib * 1e3, t_rel * 1e3, razon);

        /*
         * Y UNA COMPROBACION CONTRA ALGO QUE NO ES ESTE BANCO: a escala 1 se
         * ve el mundo, y la tierra es el 29 % de la superficie del planeta.
         * El relleno cubre el 28,4 %. No es para presumir de precision -el
         * panel recorta por arriba y por abajo, asi que la Antartida entra a
         * medias, y los anillos NO estan cerrados-, pero si para saber que
         * lo que se esta rellenando es tierra y no una figura cualquiera.
         * Un 2 % o un 90 % querrian decir que la regla par-impar se esta
         * aplicando sobre basura, y entonces los milisegundos de arriba no
         * medirian lo que dicen medir.
         */
        if (escalas[e] == 1U) {
            double frac = 100.0 * (double)(g_pix / vueltas)
                        / (double)((unsigned long)GFX2_W * ALTO);

            printf("      cubre el %.1f %% del panel (la tierra es el 29 %%"
                   " del planeta)\n", frac);
            if (frac < 20.0 || frac > 40.0) {
                printf("      *** MAL: eso no es la tierra ***\n");
                fallos++;
            }
        }
        printf("\n");
    }

    snprintf(det, sizeof det, "(la peor es %.2f, el tope que me pongo es 1,00)",
             peor_razon);
    if (peor_razon < 1.00) {
        printf("    %-50s ok  %s\n", "el relleno cuesta menos que el dibujado de hoy", det);
    } else {
        printf("    %-50s *** MAL ***  %s\n", "el relleno cuesta menos que el dibujado de hoy", det);
        fallos++;
    }

    printf("\n  Esto es un PC: los milisegundos no se transportan. Lo que se\n");
    printf("  transporta es la RAZON contra el dibujado de hoy, que ya cabe en\n");
    printf("  los 22 ms de presupuesto de cuadro. Y falta lo que este banco NO\n");
    printf("  mide: cerrar los anillos y saber que lado es tierra, que es\n");
    printf("  trabajo de tools/mapa_pack.py y cambia el formato del .map.\n");

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
