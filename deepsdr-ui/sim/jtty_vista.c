/*
 * COMO QUEDA EL PANEL DE JTTY - 28/09/2026.
 *
 * Pedido por el usuario.
 *
 * sim/jtty_cols.c comprueba que los anchos cuadran, que es una pregunta
 * distinta de si SE LEE. Esto pinta el panel con renglones de verdad -de
 * las fotos de 14.090- y saca un PPM para mirarlo, que es lo unico que
 * contesta a "queda bien" sin tener la radio delante.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_digi.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;

static void fondo(gfx2_surf_t *s, void *c)
{
    (void)c;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

int main(void)
{
    ui_digi_state_t st;
    /* Renglones de verdad, de las fotos de 14.090 del 28/09. El pais es el
     * que saca dxcc.c del ultimo indicativo de cada mensaje. */
    static const char *k[] = {
        "17:29\t-265 Hz\t78\tAP2HA DE DL ... DL4MFF PSE K\tAlemania",
        "17:30\t46 Hz\t75\tLZ1QZ ... JUO F4JUO...\tFrancia",
        "17:30\t-78 Hz\t48\t5DNZ...\t",
        "17:30\t62 Hz\t54\tCQ LZ1QZ CQ\tBulgaria",
        "17:30\t-78 Hz\t64\tQ NAME LUIGI K\t",
        "17:31\t46 Hz\t91\tLZ1QZ DE F4JUO UR RST 599 009 OP JACQUE\tFrancia",
        "17:31\t46 Hz\t54\tIK3CHK\tItalia",
        "17:31\t-125 Hz\t56\tLZ1QZ...\tBulgaria",
        "17:31\t46 Hz\t95\tW5CHA DE IK3CHK  IK3CHK\tItalia",
        "17:31\t46 Hz\t60\tLZ1QZ 599 011\tBulgaria",
        "17:31\t125 Hz\t53\t7M5OAP TU\tJapon",
        /* Y el peor caso de las dos columnas que se han tocado: el pais mas
         * ancho de la tabla y un mensaje que no cabe. */
        "23:58\t-500 Hz\t100\tCQ ZL1ABC CQ MY NAME IS A VERY LONG ONE AND"
            " IT DOES NOT FIT AT ALL\tNueva Zelanda",
        NULL
    };
    unsigned i;

    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);

    memset(&st, 0, sizeof st);
    st.y       = 104;
    st.h       = 318;
    st.filas   = 15U;
    st.btn     = "Borrar";
    st.btn4    = "14.090";
    st.btn5    = "Mapa";
    st.chip    = "3724";
    st.cebra   = 1U;            /* lo que se acaba de poner */
    st.cols    = ui_digi_cols_jtty;
    st.ncols   = UI_DIGI_COLS_JTTY_N;
    st.titulos = "Hora\tDesvío\tCal.\tMensaje\tPaís";

    for (i = 0U; k[i] != NULL && i < (unsigned)UDG_ROWS_MAX; i++) {
        st.fila[i] = k[i];
    }
    ui_digi_draw_texto(&st);

    lcd_sim_write_ppm("out/jtty_vista.ppm");
    printf("  out/jtty_vista.ppm  (%u renglones, %u columnas, cebra %s)\n",
           i, (unsigned)UI_DIGI_COLS_JTTY_N, st.cebra ? "si" : "no");
    return 0;
}
