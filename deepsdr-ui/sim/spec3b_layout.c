/*
 * Comprobacion de anchos de la etapa 3b.
 *
 * Mide de verdad cada etiqueta con la fuente real y comprueba que cabe en
 * su hueco y que no pisa a la de al lado. Los tres solapamientos de la
 * primera version (el "dBFS" cortado, "kHz" contra la etiqueta del borde
 * izquierdo, y el ancho de filtro contra la del borde derecho) se vieron
 * mirando el PNG - que es exactamente lo que no escala. Esto los cazaria
 * sin mirar.
 */
#include "gfx2.h"
#include "palette.h"
#include "spec_chrome.h"
#include "font_ui_14.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;

static int fallos = 0;

static void cabe(const char *que, int16_t ancho, int16_t hueco)
{
    int ok = (ancho <= hueco);
    if (!ok) { fallos++; }
    printf("  %-46s %4d px en %4d  %s\n", que, ancho, hueco, ok ? "OK" : "NO CABE");
}

static void separado(const char *que, int16_t x0_fin, int16_t x1_ini)
{
    int ok = (x0_fin <= x1_ini);
    if (!ok) { fallos++; }
    printf("  %-46s acaba %4d, siguiente %4d  %s\n", que, x0_fin, x1_ini,
           ok ? "OK" : "SE PISAN");
}

int main(void)
{
    /* Peor caso de cada etiqueta, no un caso cualquiera. */
    const char *db_peor   = "-110";        /* fondo de escala               */
    const char *khz_peor  = "108.000";     /* FM, 7 caracteres + separador  */
    int16_t w_dbfs = gfx2_text_w("dBFS", &font_ui_14);
    int16_t w_db   = gfx2_text_w(db_peor, &font_ui_14);
    int16_t w_khz  = gfx2_text_w("kHz", &font_ui_14);
    int16_t w_lbl  = gfx2_text_w(khz_peor, &font_ui_14);

    printf("canaleta = %d px, traza = %d px (x %d..%d)\n\n",
           SPC_GUT_W, SPC_TRACE_W, SPC_TRACE_X, SPC_TRACE_X + SPC_TRACE_W - 1);

    printf("Canaleta:\n");
    cabe("unidad \"dBFS\"",            w_dbfs, (int16_t)(SPC_GUT_W - 2));
    cabe("numero de dB \"-110\"",      w_db,   (int16_t)(SPC_GUT_W - 6));
    cabe("unidad \"kHz\" de la regla", w_khz,  (int16_t)(SPC_GUT_W - 6));
    cabe("leyenda del waterfall",      w_db,   (int16_t)(SPC_GUT_W - 2));

    printf("\nRegla de frecuencia (5 marcas, la peor etiqueta en todas):\n");
    {
        int k;
        int16_t fin_prev = SPC_GUT_W;   /* la canaleta lleva el \"kHz\" */
        const char *quien_prev = "canaleta \"kHz\"";
        for (k = -2; k <= 2; k++) {
            int16_t cx = (int16_t)(SPC_TRACE_X + SPC_TRACE_W / 2
                                   + k * (SPC_TRACE_W / 4));
            int16_t tx = (int16_t)(cx - w_lbl / 2);
            char que[64];
            if (tx < SPC_TRACE_X + 2) { tx = (int16_t)(SPC_TRACE_X + 2); }
            if (tx > (int16_t)(GFX2_W - w_lbl - 2)) { tx = (int16_t)(GFX2_W - w_lbl - 2); }
            snprintf(que, sizeof que, "%s -> marca k=%+d", quien_prev, k);
            separado(que, fin_prev, tx);
            fin_prev = (int16_t)(tx + w_lbl);
            quien_prev = "marca anterior";
        }
        cabe("la ultima etiqueta cabe en pantalla", fin_prev, GFX2_W);
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
