/* Reproduce EXACTAMENTE la barra de acciones de la tarjeta de prueba, para
 * ver el solape del rotulo de paleta antes y despues de arreglarlo. */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include <stdio.h>

const palette_t *g_pal = &k_pal_oscura;

#define ACT_Y 418
#define ACT_H  62

static int g_fixed = 0;

static void bar(gfx2_surf_t *s, void *ctx)
{
    static const char *lab[6] = { "Modo", "Bandas", "Filtros", "AGC", "Ruido", "Ajustes" };
    static const char *sub[6] = { "AM", "41 m", "4,0 kHz", "Media", "no", 0 };
    int16_t i;
    (void)ctx;

    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
    gfx2_vgrad(s, 0, ACT_Y, GFX2_W, ACT_H, gfx2_rgb(PAL_SURF_1), gfx2_rgb(PAL_SURF_0));
    gfx2_hline(s, 0, ACT_Y, GFX2_W, gfx2_rgb(PAL_LINE));

    if (!g_fixed) {
        /* VERSION CON EL FALLO: el rotulo se pinta en la propia barra, y los
         * botones (que empiezan 7 px mas abajo) solo tapan parte de el. */
        gfx2_text(s, 10, (int16_t)(ACT_Y + 1), "paleta:", &font_ui_14,
                  gfx2_rgb(PAL_INK_MUTE));
        gfx2_text(s, 58, (int16_t)(ACT_Y + 1), g_pal->name, &font_ui_14b,
                  gfx2_rgb(PAL_ACCENT));
        gfx2_text(s, 150, (int16_t)(ACT_Y + 1),
                  "— pulsación larga para alternar", &font_ui_14,
                  gfx2_rgb(PAL_INK_MUTE));
    }

    for (i = 0; i < 6; i++) {
        int16_t bx = (int16_t)(10 + i * 132);
        int16_t by = (int16_t)(ACT_Y + 7);
        gfx2_rrect(s, bx, by, 126, 48, 8, gfx2_rgb(PAL_SURF_2));
        gfx2_rrect_outline(s, bx, by, 126, 48, 8, 1, gfx2_rgb(PAL_LINE));
        if (sub[i]) {
            gfx2_text_in(s, bx, (int16_t)(by + 6), 126, lab[i], &font_ui_14b,
                         gfx2_rgb(PAL_INK), GFX2_ALIGN_C);
            gfx2_text_in(s, bx, (int16_t)(by + 25), 126, sub[i], &font_ui_14,
                         gfx2_rgb(PAL_INK_DIM), GFX2_ALIGN_C);
        } else {
            gfx2_text_in(s, bx, (int16_t)(by + 14), 126, lab[i], &font_ui_18b,
                         gfx2_rgb(PAL_INK), GFX2_ALIGN_C);
        }
    }
}

int main(void)
{
    lcd_sim_reset(0);
    g_fixed = 0;
    gfx2_render_screen(bar, 0);
    lcd_sim_write_ppm("out/05_bar_bug.ppm");
    lcd_sim_reset(0);
    g_fixed = 1;
    gfx2_render_screen(bar, 0);
    lcd_sim_write_ppm("out/06_bar_fixed.ppm");
    printf("ok\n");
    return 0;
}
