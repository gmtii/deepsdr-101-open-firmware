/* Comprobacion de presupuesto: compila gfx2 + las seis fuentes + el colormap
 * para el GD32F450 real y mide flash y RAM. Referencia las tablas para que
 * el enlazador no las descarte. */
#include "gfx2.h"
#include "palette.h"
#include "wf_cmap.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include "font_ui_24b.h"
#include "font_num_44.h"
#include "font_num_20.h"

static const gfx2_font_t *const k_fonts[6] = {
    &font_ui_14, &font_ui_14b, &font_ui_18b,
    &font_ui_24b, &font_num_44, &font_num_20
};

/* buffer de historial del waterfall, ahora en indices de 8 bits */
#define WF_ROWS 84
#define WF_W   740
static uint8_t s_wf[WF_ROWS][WF_W];

volatile uint32_t sink;

static void draw(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
    gfx2_rrect(s, 10, 10, 120, 40, 8, gfx2_rgb(PAL_SURF_2));
    gfx2_rrect_outline(s, 10, 10, 120, 40, 8, 1, gfx2_rgb(PAL_LINE));
    gfx2_vgrad(s, 0, 0, GFX2_W, 72, gfx2_rgb(0x1B2027), gfx2_rgb(PAL_SURF_1));
    gfx2_text(s, 18, 8, "7.200,00", k_fonts[4], gfx2_rgb(PAL_INK));
    gfx2_text(s, 18, 60, "Recepción · Atenuador · Señal", k_fonts[0],
              gfx2_rgb(PAL_INK_DIM));
    gfx2_text_in(s, 0, 100, GFX2_W, "Ajustes", k_fonts[3],
                 gfx2_rgb(PAL_ACCENT), GFX2_ALIGN_C);
}

int main(void)
{
    unsigned i;
    for (i = 0; i < 6; i++) sink += k_fonts[i]->line_h;
    sink += wf_cmap[128];
    s_wf[0][0] = (uint8_t)sink;
    gfx2_render_screen(draw, 0);
    gfx2_wf_blit(10, 332, WF_W, WF_ROWS, &s_wf[0][0], WF_W, 0, wf_cmap);
    for (;;) { }
}
