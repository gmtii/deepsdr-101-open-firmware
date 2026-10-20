/*
 * QUE EL CARTEL DE LA CAPTURA QUEPA ENTERO.
 *
 * El cartel dice por que no se ha podido guardar una captura, y la mitad
 * del texto no lo escribe main.c: lo escribe spi_flash_anade_porque_txt().
 * Pegados detras de "NO CABE: " y del tamano que hace falta, salen frases
 * largas.
 *
 * Y un cartel que no cabe NO DA ERROR: gfx2_text_in() lo recorta, asi que
 * el motivo se queda a medias justo cuando hace falta leerlo entero. Por
 * eso se mide aqui, con la fuente de verdad -font_ui_18b es proporcional,
 * no se puede estimar a tantos pixeles por letra- y con la caja de verdad.
 *
 * La lista sale del codigo (tools/gen_avisos.py), no de una copia a mano:
 * escribirla a mano era garantizar que el dia que alguien anada un motivo
 * nuevo, esto siga midiendo los de ayer.
 */
#include "gfx2.h"
#include "font_ui_18b.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "ui_grid.h"
#include "palette.h"
#include "avisos_tabla.h"
#include <stdio.h>

const palette_t *g_pal = &k_pal_oscura;

/* Lo mismo que capt_aviso_pinta() en main.c. */
#define CAJA_MAX   780
#define AIRE        48

int main(void)
{
    unsigned i;
    int fallos = 0;
    int peor = 0;
    const char *peor_t = "";

    printf("\nEL CARTEL DE LA CAPTURA TIENE QUE CABER\n\n");

    for (i = 0U; i < (sizeof k_avisos / sizeof k_avisos[0]); i++) {
        int w = gfx2_text_w(k_avisos[i], &font_ui_18b);
        if (w > peor) { peor = w; peor_t = k_avisos[i]; }
        if (w + AIRE > CAJA_MAX) {
            printf("    %-52s *** MAL ***  %d px\n", k_avisos[i], w);
            fallos++;
        }
    }

    printf("  %u textos de cartel; el mas ancho %d px de los %d que caben\n",
           (unsigned)(sizeof k_avisos / sizeof k_avisos[0]), peor,
           CAJA_MAX - AIRE);
    printf("    \"%s\"\n", peor_t);

    /*
     * Y los renglones de la fila "Sitio para fotos". Van en una celda de
     * rejilla, que mide 192 px: ocho veces menos que el cartel. Es donde
     * reviento primero -se vio en pantalla el valor saliendose
     * por los dos lados de la celda- y por eso se mide aparte y con SU
     * fuente, que tampoco es la misma.
     */
    peor = 0; peor_t = "";
    for (i = 0U; i < (sizeof k_celdas / sizeof k_celdas[0]); i++) {
        int w2 = gfx2_text_w(k_celdas[i], &font_ui_14);
        int w3 = gfx2_text_w(k_celdas[i], &font_ui_14b);
        int w = (w2 > w3) ? w2 : w3;
        if (w > peor) { peor = w; peor_t = k_celdas[i]; }
        if (w > UIG_CELL_W) {
            printf("    %-52s *** MAL ***  %d px\n", k_celdas[i], w);
            fallos++;
        }
    }
    printf("  %u renglones de celda; el mas ancho %d px de los %d que caben\n",
           (unsigned)(sizeof k_celdas / sizeof k_celdas[0]), peor,
           (int)UIG_CELL_W);
    printf("    \"%s\"\n", peor_t);
    /*
     * Y QUE "_IN" SEA VERDAD. Hasta hoy gfx2_text_in() colocaba el texto
     * dentro de un ancho y se olvidaba del ancho: si no cabia, centrado lo
     * sacaba por los DOS lados, encima del vecino. Aqui se le da un texto
     * que no cabe ni de lejos y se exige que no toque un solo pixel fuera
     * de su caja.
     */
    {
        static uint16_t banda[800 * 16];
        gfx2_surf_t sf;
        const int16_t X = 200, W = 192;
        int x, y, fuera = 0;
        uint16_t fondo;

        sf.px = banda; sf.x = 0; sf.y = 0; sf.w = 800; sf.h = 16;
        gfx2_fill(&sf, 0, 0, 800, 16, gfx2_rgb(0x000000));
        fondo = banda[0];
        gfx2_text_in(&sf, X, 0, W,
                     "un texto larguisimo que no cabe de ninguna manera aqui",
                     &font_ui_14, gfx2_rgb(0xFFFFFF), GFX2_ALIGN_C);
        for (y = 0; y < 16; y++) {
            for (x = 0; x < 800; x++) {
                if (banda[y * 800 + x] == fondo) { continue; }
                if (x < X || x >= X + W) { fuera++; }
            }
        }
        if (fuera != 0) {
            printf("    %-52s *** MAL ***  %d px\n",
                   "un texto que no cabe se queda dentro de su caja", fuera);
            fallos++;
        } else {
            printf("    %-52s ok\n",
                   "un texto que no cabe se queda dentro de su caja");
        }
    }

    printf("\n%s\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO: ninguno se recorta");
    return fallos ? 1 : 0;
}
