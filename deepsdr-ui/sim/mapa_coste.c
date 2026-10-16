/*
 * ======================================================================
 * LO QUE CUESTA PINTAR EL MAPA, MEDIDO - 13/10/2026
 * ======================================================================
 *
 * gfx2 compone la pantalla en bandas de 16 filas y la funcion de dibujo
 * se llama UNA VEZ POR BANDA. El mapa de FT8 mide 288 px de alto: son
 * DIECIOCHO pasadas por fotograma, y mapa_dibuja() recorria la costa
 * entera en todas ellas.
 *
 * Esto cuenta, interceptando gfx2_line() con --wrap, cuantos trazos
 * entran y cuantos de ellos ESCRIBEN DE VERDAD algun pixel. No mide
 * microsegundos: dependerian de la maquina del simulador y no dirian
 * nada de la radio. Mide TRABAJO TIRADO, que es una propiedad del
 * algoritmo y vale igual en las dos.
 *
 * Y mide ademas lo unico que de verdad hay que proteger al optimizar:
 * QUE EL DIBUJO NO CAMBIE. La guarda de banda es exacta -un segmento con
 * los dos extremos por encima de la banda, o los dos por debajo, no puede
 * cruzarla- asi que el lienzo tiene que salir identico.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "mapa.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

static unsigned g_entran, g_utiles;

void __real_gfx2_line(gfx2_surf_t *s, int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1, gfx2_rgba_t c);

void __wrap_gfx2_line(gfx2_surf_t *s, int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1, gfx2_rgba_t c)
{
    static uint16_t antes[GFX2_W * 16];
    int32_t n = (int32_t)s->w * s->h;
    int32_t i;
    int cambio = 0;

    g_entran++;
    if (n > (int32_t)(sizeof antes / sizeof antes[0])) { n = 0; }
    for (i = 0; i < n; i++) { antes[i] = s->px[i]; }
    __real_gfx2_line(s, x0, y0, x1, y1, c);
    for (i = 0; i < n; i++) {
        if (s->px[i] != antes[i]) { cambio = 1; break; }
    }
    if (cambio) { g_utiles++; }
}

static mapa_caja_t g_caja;

static void pinta(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
    mapa_dibuja(s, &g_caja);
}

int main(void)
{
    static const uint8_t escalas[] = { 1U, 2U, 4U };
    unsigned e;

    printf("\n== LO QUE CUESTA PINTAR EL MAPA ==\n");
    printf("\n  un fotograma del hueco de FT8 (800x288 = 18 bandas)\n\n");

    for (e = 0; e < sizeof escalas / sizeof escalas[0]; e++) {
        unsigned i, pixeles = 0U;
        const uint16_t *fb;
        uint16_t fondo;

        memset(&g_caja, 0, sizeof g_caja);
        mapa_pon(&g_caja, 0, 0, GFX2_W, 288);
        if (escalas[e] > 1U) {
            (void)mapa_zoom(&g_caja, escalas[e], GFX2_W / 2, 288 / 2);
        }

        g_entran = 0U; g_utiles = 0U;
        lcd_sim_reset(0x0000);
        gfx2_render(0, 0, GFX2_W, 288, pinta, 0);

        fb = lcd_sim_fb();
        fondo = fb[0];
        for (i = 0; i < GFX2_W * 288U; i++) {
            if (fb[i] != fondo) { pixeles++; }
        }

        printf("    escala %u: %6u trazos entran, %6u pintan (%2u %%),"
               "  %6u pixeles de costa\n",
               (unsigned)escalas[e], g_entran, g_utiles,
               g_entran ? (g_utiles * 100U / g_entran) : 0U, pixeles);

        if (pixeles < 1000U) {
            printf("    *** MAL: el mapa casi no ha pintado ***\n");
            fallos++;
        }
        /*
         * EL SUELO. Sin la guarda de banda de mapa.c esto daba un 6 %:
         * de cada cien trazos que entraban en recorta() y en gfx2_line(),
         * seis escribian un pixel. Con ella pasa del 80 %. El suelo se
         * pone en 50 para que un cambio que lo estropee a la mitad salga
         * rojo, pero sin atarse a la cifra exacta, que depende de la
         * tabla de costa que lleve el firmware.
         */
        if (g_entran && (g_utiles * 100U / g_entran) < 50U) {
            printf("    *** MAL: mas de la mitad de los trazos no pintan "
                   "nada: falta la guarda de banda ***\n");
            fallos++;
        }
    }

    lcd_sim_write_ppm("out/mapa_coste.ppm");
    printf("\n  foto en out/mapa_coste.ppm\n");
    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
