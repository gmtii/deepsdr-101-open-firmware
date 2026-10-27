/*
 * EL VALOR DE LA CELDA DE INFORMACION, MEDIDO - revision 26/10/2026.
 *
 * sim/pie_ancho.c mide el PIE (el tercer renglon). El renglon del VALOR
 * (l2) no lo medía nadie, y los dos cambios del 26/10 le añaden texto:
 *   - INFO_XAUDIO: un tercer numero ("vac").
 *   - INFO_XREPARTO: la rama de WFM, "fuga: mide N, quita M".
 *
 * La fuente del valor depende de si la fila tiene pie (ui_grid.c:157):
 * con pie es font_ui_14, sin pie es font_ui_14b.
 */
#include "gfx2.h"
#include "gfx2_font.h"
#include "ui_grid.h"
#include <stdio.h>
#include <string.h>

extern const gfx2_font_t font_ui_14;
extern const gfx2_font_t font_ui_14b;

static int malos;

static void mide(const char *que, const char *txt, const gfx2_font_t *f)
{
    int16_t w = gfx2_text_w(txt, f);
    int     n = (int)strlen(txt) + 1;

    printf("  %-26s %-36s %3d px  %2d B", que, txt, w, n);
    if (w > UIG_CELL_W || n > 40) {
        printf("   *** %s%s ***\n",
               (w > UIG_CELL_W) ? "NO CABE EN LA CELDA " : "",
               (n > 40) ? "NO CABE EN s_info_val[40]" : "");
        if (strstr(que, "antes") == 0 && strstr(que, "sin topar") == 0) { malos++; }
        else { printf("    (es la version vieja, esta aqui para comparar)\n"); }
    } else {
        printf("   ok\n");
    }
}

int main(void)
{
    printf("\nEL VALOR DE LA CELDA (l2): celda %d px, buffer s_info_val[][40]\n\n",
           UIG_CELL_W);

    printf(" -- INFO_XAUDIO (tiene pie -> font_ui_14) --\n");
    mide("antes del cambio",      "0 sal, 0 rf",                    &font_ui_14);
    mide("ahora, recien puesto",  "0 vac 0 sal 0 rf",               &font_ui_14);
    mide("ahora, 4 cifras",       "1234 vac 1234 sal 1234 rf",      &font_ui_14);
    mide("ahora, PEOR CASO",      "999+ vac 999+ sal 999+ rf" ,   &font_ui_14);
    mide("sin topar (lo de antes)","4294967295 vac, 4294967295 sal, 4294967295 rf",
                                                                    &font_ui_14);

    printf("\n -- INFO_XREPARTO, rama WFM (sin pie -> font_ui_14b) --\n");
    mide("es, normal",   "fuga 2840/2048",                    &font_ui_14b);
    mide("es, PEOR CASO","fuga 9999+/9999+",                  &font_ui_14b);
    mide("en, normal",   "leak 2840/2048",                    &font_ui_14b);
    mide("en, PEOR CASO","leak 9999+/9999+",                  &font_ui_14b);
    mide("lo de antes",  "fuga: mide 2840, quita 2048",       &font_ui_14b);

    printf("\n -- INFO_XISR, para comparar (sin pie -> font_ui_14b) --\n");
    mide("es", "37 %, 8 rui", &font_ui_14b);

    printf("\n%s (%d malos)\n\n", malos ? "HAY FILAS QUE NO CABEN" : "TODO CORRECTO",
           malos);
    return malos ? 1 : 0;
}
