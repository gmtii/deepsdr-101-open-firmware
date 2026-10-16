/* Etapa 1: el reloj con gfx2 dentro de la barra superior que pinta gfx.c.
 * Comprueba que el relleno de fondo casa con el DARKGRAY del resto y que el
 * texto no invade ni el indicador de bateria ni lo que hay a su izquierda. */
#include "lcd_sim.h"
#include "gfx.h"
#include "gfx2.h"
#include "palette.h"
#include "font_num_20.h"
#include <stdio.h>

const palette_t *g_pal = &k_pal_oscura;

#define TIME_X 690
#define TIME_Y   8
#define TIME2_X 684
#define TIME2_Y   4
#define TIME2_W (GFX2_W - TIME2_X)
#define TIME2_H  30
#define TOPBAR_BG 0x424242u

static const char *s_time_text = "21:47";

static void time_draw_gfx2(gfx2_surf_t *s, void *ctx)
{
    int16_t w;
    (void)ctx;
    gfx2_fill(s, TIME2_X, TIME2_Y, TIME2_W, TIME2_H, gfx2_rgb(TOPBAR_BG));
    w = gfx2_text_w(s_time_text, &font_num_20);
    gfx2_text(s, (int16_t)(GFX2_W - 10 - w), (int16_t)(TIME2_Y + 3),
              s_time_text, &font_num_20, gfx2_rgb(0xFFFFFF));
}

int main(void)
{
    /* la barra superior tal cual la pinta el firmware hoy */
    lcd_sim_reset(GFX_COLOR_BLACK);
    gfx_fill_rect(0, 0, 800, 64, GFX_COLOR_DARKGRAY);
    gfx_text(6, 14, "7.200.00", GFX_COLOR_WHITE, GFX_COLOR_DARKGRAY, 6);
    gfx_text(300, 10, "AM", GFX_COLOR_YELLOW, GFX_COLOR_DARKGRAY, 3);
    gfx_text(300, 40, "STEP 1kHz", GFX_COLOR_WHITE, GFX_COLOR_DARKGRAY, 1);
    gfx_text(420, 40, "VOL 18", GFX_COLOR_WHITE, GFX_COLOR_DARKGRAY, 1);
    /* bateria, que vive mas abajo en la misma barra */
    gfx_rect(700, 40, 40, 16, GFX_COLOR_WHITE);
    gfx_fill_rect(702, 42, 26, 12, GFX_COLOR_GREEN);
    gfx_fill_rect(740, 45, 3, 6, GFX_COLOR_WHITE);
    gfx_text(750, 44, "3.9V", GFX_COLOR_WHITE, GFX_COLOR_DARKGRAY, 1);

    /* el reloj ANTIGUO, para comparar, a la izquierda de su sitio */
    gfx_text(500, TIME_Y, "21:47", GFX_COLOR_WHITE, GFX_COLOR_DARKGRAY, 3);

    /* el reloj NUEVO, en su sitio real */
    gfx2_render(TIME2_X, TIME2_Y, TIME2_W, TIME2_H, time_draw_gfx2, 0);

    lcd_sim_write_ppm("out/09_clock.ppm");
    printf("ok\n");
    return 0;
}
