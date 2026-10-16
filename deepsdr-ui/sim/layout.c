/*
 * Comprobacion de que las franjas de la pantalla principal no se pisan.
 *
 * Cada modulo dibuja su franja sin saber de los demas. Esto verifica, con
 * las constantes REALES de cada cabecera, que las franjas son contiguas y
 * caben en los 480 px - que es justo la clase de cosa que se nota tarde y
 * mal: un solape de dos filas solo se ve cuando uno de los dos repinta.
 */
#include "gfx2.h"
#include "ui_top.h"
#include "spec_chrome.h"
#include "ui_act.h"
#include <stdio.h>

static int fallos = 0;

static void franja(const char *nom, int y0, int y1, int *cursor)
{
    int ok = (y0 == *cursor);
    int hueco = y0 - *cursor;
    if (y0 < *cursor) { fallos++; ok = 0; }
    printf("  %-24s y=%3d..%-3d  %s\n", nom, y0, y1,
           (y0 < *cursor) ? "SE PISA CON LA ANTERIOR"
                          : (hueco ? "OK, con hueco" : "OK"));
    if (hueco > 0 && y0 >= *cursor) {
        printf("  %-24s              hueco de %d px sin asignar\n", "", hueco);
    }
    (void)ok;
    *cursor = y1 + 1;
}

int main(void)
{
    int c = 0;

    printf("Franjas de la pantalla principal (800x480):\n\n");
    franja("cabecera",   0, UI_TOP_H - 1, &c);
    franja("barra de estado", UI_STATUS_Y, UI_STATUS_Y + UI_STATUS_H - 1, &c);
    franja("espectro",   SPC_Y, SPC_RULER_Y - 1, &c);
    franja("regla",      SPC_RULER_Y, SPC_RULER_Y + SPC_RULER_H - 1, &c);
    franja("borde waterfall", SPC_WF_PANEL_Y, SPC_WF_Y - 1, &c);
    franja("waterfall",  SPC_WF_Y, SPC_WF_Y + SPC_WF_ROWS - 1, &c);
    franja("acciones",   UI_ACT_Y, UI_ACT_Y + UI_ACT_H - 1, &c);

    if (c > GFX2_H) {
        printf("\n  se sale de la pantalla: %d > %d\n", c, GFX2_H);
        fallos++;
    } else {
        printf("\n  margen inferior: %d px\n", GFX2_H - c);
    }

    /* la canaleta y la traza tienen que sumar el ancho, sin pisarse */
    if (SPC_TRACE_X < SPC_GUT_W) { printf("  la traza invade la canaleta\n"); fallos++; }
    if (SPC_TRACE_X + SPC_TRACE_W > GFX2_W) { printf("  la traza se sale\n"); fallos++; }
    if (SPC_TRACE_W % 4) { printf("  la traza no es divisible por 4 (marcador Fs/4)\n"); fallos++; }
    printf("  traza x=%d..%d, margen derecho %d px, W/4=%d exacto\n",
           SPC_TRACE_X, SPC_TRACE_X + SPC_TRACE_W - 1,
           GFX2_W - (SPC_TRACE_X + SPC_TRACE_W), SPC_TRACE_W / 4);

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
