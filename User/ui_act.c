#include "ui_act.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"

static int16_t btn_x(uint8_t i)
{
    return (int16_t)(UI_ACT_MARGIN + (int16_t)i * (UI_ACT_BTN_W + UI_ACT_GAP));
}

int8_t ui_act_hit(uint16_t x, uint16_t y)
{
    uint8_t i;

    /*
     * Vertical: la franja entera, incluidos los 2 px de separacion de arriba
     * y los 4 de abajo. Horizontal: tambien la mitad de cada hueco entre
     * botones. Es decir, no hay zona muerta: cualquier punto de la franja
     * pertenece a algun boton, al mas cercano.
     *
     * Con un tactil que ya pide tocar con ganas, un toque que cae en el hueco
     * de 10 px entre dos botones y no hace nada se vive como "no ha
     * funcionado", y lleva a tocar mas fuerte en vez de mas centrado.
     */
    /* Vertical: desde donde acaba el waterfall hasta abajo del todo, o sea
     * tambien los 7 px de aire de arriba y los 3 de abajo. */
    if (y < UI_ACT_Y - 7 || y >= GFX2_H) {
        return -1;
    }
    for (i = 0U; i < UI_ACT_N; i++) {
        /* Los de los extremos se llevan tambien su margen exterior, para que
         * el borde mismo de la pantalla sea zona util. */
        int16_t x0 = (i == 0U) ? 0 : (int16_t)(btn_x(i) - UI_ACT_GAP / 2);
        int16_t x1 = (i == UI_ACT_N - 1U) ? GFX2_W
                                          : (int16_t)(btn_x(i) + UI_ACT_BTN_W + UI_ACT_GAP / 2);
        if ((int16_t)x >= x0 && (int16_t)x < x1) {
            return (int8_t)i;
        }
    }
    return -1;
}

static void draw_btn(gfx2_surf_t *s, const ui_act_state_t *st, uint8_t i)
{
    int16_t x = btn_x(i);
    int16_t y = UI_ACT_Y;
    uint8_t press = (uint8_t)(st->pressed == (int8_t)i);
    gfx2_rgba_t fondo, borde, tinta, tinta2;

    /*
     * Tres estados y tres aspectos distintos, sin depender solo del color:
     *
     *   pulsado  - relleno de acento solido y texto oscuro. Salta a la vista
     *              aunque el dedo tape medio boton.
     *   activo   - relleno tenue de acento y contorno de acento.
     *   reposo   - relleno de superficie y contorno de linea.
     */
    if (press) {
        fondo  = gfx2_rgb(PAL_ACCENT);
        borde  = gfx2_rgb(PAL_ACCENT);
        tinta  = gfx2_rgb(PAL_SURF_0);
        tinta2 = gfx2_rgba(PAL_SURF_0, 200);
    } else if (st->active[i]) {
        fondo  = gfx2_rgba(PAL_ACCENT, 44);
        borde  = gfx2_rgba(PAL_ACCENT, 170);
        tinta  = gfx2_rgb(PAL_ACCENT);
        tinta2 = gfx2_rgb(PAL_INK);
    } else {
        fondo  = gfx2_rgb(PAL_SURF_2);
        borde  = gfx2_rgb(PAL_LINE);
        tinta  = gfx2_rgb(PAL_INK);
        tinta2 = gfx2_rgb(PAL_INK_MUTE);
    }

    gfx2_rrect(s, x, y, UI_ACT_BTN_W, UI_ACT_H, 8, fondo);
    gfx2_rrect_outline(s, x, y, UI_ACT_BTN_W, UI_ACT_H, 8, 1, borde);

    if (st->value[i]) {
        /* Dos lineas: nombre arriba, valor abajo. El nombre en negrita y el
         * valor en normal - el nombre es lo que se busca con la vista, el
         * valor lo que se lee una vez encontrado. */
        gfx2_text_in(s, x, (int16_t)(y + 7), UI_ACT_BTN_W, st->name[i],
                     &font_ui_14b, tinta, GFX2_ALIGN_C);
        gfx2_text_in(s, x, (int16_t)(y + 26), UI_ACT_BTN_W, st->value[i],
                     &font_ui_14, tinta2, GFX2_ALIGN_C);
    } else {
        gfx2_text_in(s, x, (int16_t)(y + 17), UI_ACT_BTN_W, st->name[i],
                     &font_ui_14b, tinta, GFX2_ALIGN_C);
    }
}

static void draw_bar(gfx2_surf_t *s, void *ctx)
{
    const ui_act_state_t *st = (const ui_act_state_t *)ctx;
    uint8_t i;

    if (!gfx2_band_hits(s, UI_ACT_Y - 5, (int16_t)(GFX2_H - UI_ACT_Y + 5))) { return; }

    /* Se limpia desde 5 px por encima de los botones hasta el borde de abajo:
     * ese aire es parte de la barra y tiene que quedar del color del panel,
     * no con lo que hubiera antes debajo del waterfall. */
    gfx2_fill(s, 0, (int16_t)(UI_ACT_Y - 5), GFX2_W,
              (int16_t)(GFX2_H - UI_ACT_Y + 5), gfx2_rgb(PAL_SURF_0));
    for (i = 0U; i < UI_ACT_N; i++) {
        draw_btn(s, st, i);
    }
}

/* Contexto para repintar uno solo: hace falta pasar el indice ademas del
 * estado, y gfx2_render solo lleva un puntero. */
typedef struct { const ui_act_state_t *st; uint8_t i; } one_ctx_t;

static void draw_one(gfx2_surf_t *s, void *ctx)
{
    const one_ctx_t *o = (const one_ctx_t *)ctx;

    if (!gfx2_band_hits(s, UI_ACT_Y, UI_ACT_H)) { return; }
    gfx2_fill(s, btn_x(o->i), UI_ACT_Y, UI_ACT_BTN_W, UI_ACT_H,
              gfx2_rgb(PAL_SURF_0));
    draw_btn(s, o->st, o->i);
}

void ui_act_draw(const ui_act_state_t *st)
{
    gfx2_render(0, (int16_t)(UI_ACT_Y - 5), GFX2_W,
                (int16_t)(GFX2_H - UI_ACT_Y + 5), draw_bar, (void *)st);
}

void ui_act_draw_one(const ui_act_state_t *st, uint8_t i)
{
    one_ctx_t o;

    if (i >= UI_ACT_N) { return; }
    o.st = st;
    o.i  = i;
    gfx2_render(btn_x(i), UI_ACT_Y, UI_ACT_BTN_W, UI_ACT_H, draw_one, &o);
}
