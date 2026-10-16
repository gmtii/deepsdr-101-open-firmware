/* Render de la pantalla de un ajuste. */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_det.h"
#include <stdio.h>
#include <stdlib.h>
const palette_t *g_pal = &k_pal_oscura;
static void fondo(gfx2_surf_t *s, void *c){ (void)c; gfx2_fill(s,0,0,GFX2_W,GFX2_H,gfx2_rgb(PAL_SURF_0)); }
int main(int argc, char **argv)
{
    ui_det_state_t st = {0};
    int cual = (argc > 1) ? atoi(argv[1]) : 0;

    st.pressed = -1; st.alt = 0; st.alt_on = 0; st.pie = 0;
    if (cual == 1) {
        st.titulo = "Escala: límite bajo"; st.valor = "-110";
        st.alt = "Cambiar a LO / HI"; st.alt_on = 0;
    } else if (cual == 2) {
        st.titulo = "Ganancia de entrada"; st.valor = "-47,5 dB";
        st.pie = "PGA del códec, 0 a 47,5 dB";
        st.pressed = UID_HIT_MAS;
    } else {
        st.titulo = "Volumen"; st.valor = "-12,0 dB";
    }
    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);
    ui_det_draw(&st);
    lcd_sim_write_ppm("out/detalle.ppm");
    printf("detalle: botones %dx%d en y=%d, pie en y=%d\n",
           UID_BTN_W, UID_BTN_H, UID_BTN_Y, UID_PIE_Y);
    return 0;
}
