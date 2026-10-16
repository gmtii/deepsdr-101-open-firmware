/*
 * LOS CARTELES Y LA TRAZA, EN EL MISMO DIBUJO - 15/10/2026
 *
 * Pedido por el usuario.
 *
 * Los tres son el mismo sitio: la franja de carteles y la traza se
 * pintaban por separado. Ahora spectrum_draw() tiene un gancho
 * (spectrum_encima) y los pinta en el mismo buffer. ESTO es lo unico que
 * mide esa union, y lo hace compilando el User/spectrum.c DE VERDAD.
 *
 * LO QUE COMPRUEBA, y lo tercero es el que no se puede ver con numeros:
 *
 *   1. Que los carteles llegan al buffer de la traza: si el gancho no se
 *      llamara, esta franja saldria sin una sola letra.
 *   2. Que el sombreado del paso de banda NO sube hasta el techo. En la
 *      franja de los carteles, esas columnas tienen que ser fondo o
 *      rejilla, no color de banda.
 *   3. Y deja un PPM para mirarlo, que con algo que se juzga por como
 *      queda es la unica prueba que vale.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "lcd_sim.h"
#include "gfx.h"
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include "spec_rotulos.h"

const palette_t *g_pal = &k_pal_oscura;
static int fallos;
static void di(int cond, const char *que, const char *como)
{
    printf("    %-56s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { printf("        %s\n", como); fallos++; }
}

#define X0     40
#define Y0     108
#define ANCHO  756
/* La geometria DE LA RADIO: SPC_TRACE_H son 208 y las cuatro rayas de
 * la rejilla lo parten en cinco, asi que la primera cae en la 41. Si
 * aqui se pusiera otro alto, este banco mediria otra pantalla. */
#define ALTO   208
#define ROT_H  1   /* el alto lo decide la rejilla: ver spectrum_rejilla_fila */

static spec_rotulos_t g_rr;
static spec_rot_txt_t g_txt;
static uint32_t       g_centro = 9600000UL, g_span = 96000UL;

static void carteles(gfx2_surf_t *s)
{
    (void)spec_rotulos_draw(s, &g_rr, g_txt, g_centro, g_span,
                            X0, ANCHO, (int16_t)Y0,
                            (int16_t)(spectrum_rejilla_fila(1) + 1), 0,
                            gfx2_rgb(0xD8E6F2UL), gfx2_rgb(0x7E9ABAUL));
}

static void pinta(float *db, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        /* ruido de fondo y una portadora en el centro */
        db[i] = 70.0f + 3.0f * (float)((i * 37) % 11) / 11.0f;
    }
    db[n / 2] = 118.0f;
    db[n / 2 - 1] = 112.0f;
    db[n / 2 + 1] = 112.0f;
}

static void guarda(const char *ruta)
{
    const uint16_t *fb = lcd_sim_fb();
    FILE *f = fopen(ruta, "wb");
    int x, y;
    if (f == NULL) { return; }
    fprintf(f, "P6\n%d %d\n255\n", ANCHO + 2 * X0, Y0 + ALTO);
    for (y = 0; y < Y0 + ALTO; y++) {
        for (x = 0; x < ANCHO + 2 * X0; x++) {
            uint16_t v = fb[(size_t)y * 800U + (size_t)x];
            unsigned char p[3];
            p[0] = (unsigned char)(((v >> 11) & 0x1FU) * 255U / 31U);
            p[1] = (unsigned char)(((v >> 5)  & 0x3FU) * 255U / 63U);
            p[2] = (unsigned char)(( v        & 0x1FU) * 255U / 31U);
            fwrite(p, 1, 3, f);
        }
    }
    fclose(f);
}

/* Cuantos pixeles de una columna, en un rango de filas, NO son del color
 * que mas se repite fuera de la banda. Sirve para ver si la banda de paso
 * ha teñido esas filas. */
static int distintos(int col, int y0, int y1, uint16_t ref)
{
    const uint16_t *fb = lcd_sim_fb();
    int y, n = 0;
    for (y = y0; y < y1; y++) {
        if (fb[(size_t)y * 800U + (size_t)col] != ref) { n++; }
    }
    return n;
}

int main(void)
{
    static float db[1024];
    int n = 512;
    const uint16_t *fb;
    uint16_t ref_arriba;
    int col_banda = X0 + ANCHO / 2 - 20;   /* dentro del sombreado */
    int col_fuera = X0 + 40;               /* lejos de la banda */

    printf("\n== LOS CARTELES Y LA TRAZA, EN EL MISMO DIBUJO ==\n\n");

    spectrum_init();
    lcd_sim_reset(0x0000);

    memset(&g_rr, 0, sizeof g_rr);
    g_rr.base_hz = g_centro - g_span / 2UL;
    {
        static const char *k_n[] = { "R. Romania", "BBC World Svc",
                                     "R. Exterior", "NHK Japan" };
        static const uint32_t k_hz[] = { 9560000UL, 9586000UL,
                                         9600000UL, 9627000UL };
        uint8_t q;
        for (q = 0U; q < 4U; q++) {
            spec_rotulos_pon(&g_rr, q, k_hz[q], q);
            spec_rotulos_recorta(g_txt[q], k_n[q]);
        }
    }
    g_rr.n = 4U;

    pinta(db, n);
    spectrum_encima(carteles);
    /* banda de paso de +-60 px alrededor del centro */
    spectrum_draw(db, (uint32_t)n, X0, Y0, ANCHO, ALTO, 60.0f, 125.0f,
                  0, 1U, (int16_t)-60, (int16_t)60);
    spectrum_encima(0);

    fb = lcd_sim_fb();
    guarda("out/rotulos_banda.ppm");

    printf("  1. LAS LETRAS LLEGAN AL BUFFER DE LA TRAZA\n");
    {
        /* en la franja de carteles tiene que haber pixeles claros */
        int claros = 0, x, y;
        for (y = Y0 + 2; y < Y0 + 40; y++) {
            for (x = X0; x < X0 + ANCHO; x++) {
                uint16_t v = fb[(size_t)y * 800U + (size_t)x];
                if (((v >> 11) & 0x1FU) > 20U) { claros++; }
            }
        }
        printf("     %d pixeles claros en la franja\n", claros);
        di(claros > 200, "el gancho ha pintado dentro del dibujo de la traza",
           "spectrum_draw() no esta llamando al gancho");
    }

    printf("\n  2. Y EL SOMBREADO DEL BW NO SUBE HASTA EL TECHO\n");
    {
        int dentro_arriba, dentro_abajo;
        /* La referencia: una columna lejos de la banda, en la franja. */
        ref_arriba = fb[(size_t)(Y0 + 1) * 800U + (size_t)col_fuera];
        dentro_arriba = distintos(col_banda, Y0, Y0 + 40, ref_arriba);
        dentro_abajo  = distintos(col_banda, Y0 + 46, Y0 + 86, ref_arriba);
        printf("     columna de la banda: %d pixeles teñidos arriba, %d debajo\n",
               dentro_arriba, dentro_abajo);
        di(dentro_abajo > 20,
           "debajo de los carteles la banda SI se ve",
           "si no se ve, es que esta prueba no esta mirando la banda");
        /* Arriba solo pueden quedar las filas de rejilla, que son pocas. */
        di(dentro_arriba <= 4,
           "y en la franja de los carteles no",
           "las barras del bw siguen atravesando hasta arriba");
        /*
         * Y EMPIEZA JUSTO DEBAJO DE LA PRIMERA RAYA DE LA REJILLA.
         * Pedido por el usuario.
         * La raya se busca mirando el framebuffer: la primera fila, por
         * debajo de los carteles, en la que una columna LEJOS de la banda
         * deja de ser fondo.
         */
        {
            int y, raya = -1, arranque = -1;
            uint16_t fondo = fb[(size_t)(Y0 + 36) * 800U + (size_t)col_fuera];
            for (y = Y0 + 36; y < Y0 + ALTO; y++) {
                if (raya < 0
                    && fb[(size_t)y * 800U + (size_t)col_fuera] != fondo) { raya = y; }
                if (arranque < 0
                    && fb[(size_t)y * 800U + (size_t)col_banda] != fondo
                    && fb[(size_t)y * 800U + (size_t)col_banda]
                       != fb[(size_t)y * 800U + (size_t)col_fuera]) { arranque = y; }
            }
            printf("     primera raya en y=%d, la banda arranca en y=%d\n",
                   raya, arranque);
            di(raya >= 0 && arranque == raya + 1,
               "y arranca en la fila siguiente a la raya",
               "queda un escalon entre la raya y el sombreado");
        }
    }

    printf("\n  (imagen en out/rotulos_banda.ppm - hay que MIRARLA)\n");
    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
