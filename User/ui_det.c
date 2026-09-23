#include "ui_det.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"

#include "font_num_44.h"

/* Hueco central entre los dos botones: donde va el numero. */
#define UID_VAL_X  (UID_BTN_X0 + UID_BTN_W + 10)
#define UID_VAL_W  (UID_BTN_X1 - 10 - UID_VAL_X)

static int16_t pie_x(uint8_t i, uint8_t n)
{
    int16_t w = (int16_t)((800 - 2 * 8 - (n - 1) * 8) / n);
    return (int16_t)(8 + i * (w + 8));
}
static int16_t pie_w(uint8_t n)
{
    return (int16_t)((800 - 2 * 8 - (n - 1) * 8) / n);
}

int8_t ui_det_hit(uint16_t x, uint16_t y)
{
    /* Los dos grandes se llevan tambien el aire de arriba y de abajo: entre
     * ellos solo esta el numero, que no es tocable, asi que ampliar no le
     * quita el toque a nadie. */
    if (y >= UID_BTN_Y - 20 && y < UID_BTN_Y + UID_BTN_H + 20) {
        if ((int16_t)x < UID_VAL_X)                  { return UID_HIT_MENOS; }
        if ((int16_t)x >= UID_BTN_X1 - 10)           { return UID_HIT_MAS; }
        return UID_HIT_NONE;   /* el numero */
    }
    if (y >= UID_PIE_Y - 8 && y < UIG_Y + UIG_H) {
        /* El pie tiene uno o dos botones segun haya "LO / HI". Se decide por
         * la mitad de la pantalla, que es donde esta el reparto cuando son
         * dos; con uno solo, cualquier x vale. */
        if ((int16_t)x < 400) { return UID_HIT_ALT; }
        return UID_HIT_VOLVER;
    }
    return UID_HIT_NONE;
}

/* El "-" y el "+" se dibujan como FORMAS, no como caracteres: el guion de
 * una fuente de cifras es un trazo corto y fino, y al lado de un "+" de la
 * misma fuente parecia otra cosa mas pequeña. Dos barras de 12 px dicen
 * "menos" y "mas" sin depender de como haya dibujado el tipografo el signo. */
static void signo(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                  uint8_t mas, gfx2_rgba_t c)
{
    const int16_t largo = 56, grueso = 12;
    int16_t cx = (int16_t)(x + w / 2), cy = (int16_t)(y + h / 2);

    gfx2_fill(s, (int16_t)(cx - largo / 2), (int16_t)(cy - grueso / 2),
              largo, grueso, c);
    if (mas) {
        gfx2_fill(s, (int16_t)(cx - grueso / 2), (int16_t)(cy - largo / 2),
                  grueso, largo, c);
    }
}

static void boton(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                  int16_t r, const char *txt, const gfx2_font_t *f,
                  uint8_t press, uint8_t destacado)
{
    gfx2_rgba_t fondo, borde, tinta;

    if (press) {
        fondo = gfx2_rgb(PAL_ACCENT); borde = fondo; tinta = gfx2_rgb(PAL_SURF_0);
    } else if (destacado) {
        fondo = gfx2_rgba(PAL_ACCENT, 44); borde = gfx2_rgba(PAL_ACCENT, 170);
        tinta = gfx2_rgb(PAL_ACCENT);
    } else {
        fondo = gfx2_rgb(PAL_SURF_2); borde = gfx2_rgb(PAL_LINE); tinta = gfx2_rgb(PAL_INK);
    }
    gfx2_rrect(s, x, y, w, h, r, fondo);
    gfx2_rrect_outline(s, x, y, w, h, r, 1, borde);
    if (txt == 0) {
        return;   /* el dibujante lo pone por su cuenta (ver signo()) */
    }
    gfx2_text_in(s, x, (int16_t)(y + (h - f->line_h) / 2), w, txt, f, tinta,
                 GFX2_ALIGN_C);
}

static void valor(gfx2_surf_t *s, const ui_det_state_t *st)
{
    gfx2_fill(s, UID_VAL_X, (int16_t)(UID_BTN_Y + 10), UID_VAL_W, 80,
              gfx2_rgb(PAL_SURF_0));
    gfx2_text_in(s, UID_VAL_X, (int16_t)(UID_BTN_Y + 16), UID_VAL_W, st->valor,
                 &font_num_44, gfx2_rgb(PAL_INK), GFX2_ALIGN_C);
    if (st->pie) {
        gfx2_text_in(s, UID_VAL_X, (int16_t)(UID_BTN_Y + 74), UID_VAL_W, st->pie,
                     &font_ui_14, gfx2_rgb(PAL_INK_MUTE), GFX2_ALIGN_C);
    }
}

static void draw_all(gfx2_surf_t *s, void *ctx)
{
    const ui_det_state_t *st = (const ui_det_state_t *)ctx;
    uint8_t n = (uint8_t)(st->alt ? 2U : 1U);

    if (!gfx2_band_hits(s, UIG_Y, UIG_H)) { return; }

    gfx2_fill(s, 0, UIG_Y, GFX2_W, UIG_H, gfx2_rgb(PAL_SURF_0));

    gfx2_text_in(s, 0, (int16_t)(UIG_Y + 14), GFX2_W, st->titulo,
                 &font_ui_18b, gfx2_rgb(PAL_INK), GFX2_ALIGN_C);

    {
        uint8_t pm = (uint8_t)(st->pressed == UID_HIT_MENOS);
        uint8_t pp = (uint8_t)(st->pressed == UID_HIT_MAS);
        boton(s, UID_BTN_X0, UID_BTN_Y, UID_BTN_W, UID_BTN_H, 10, 0, 0, pm, 0U);
        signo(s, UID_BTN_X0, UID_BTN_Y, UID_BTN_W, UID_BTN_H, 0U,
              pm ? gfx2_rgb(PAL_SURF_0) : gfx2_rgb(PAL_INK));
        boton(s, UID_BTN_X1, UID_BTN_Y, UID_BTN_W, UID_BTN_H, 10, 0, 0, pp, 0U);
        signo(s, UID_BTN_X1, UID_BTN_Y, UID_BTN_W, UID_BTN_H, 1U,
              pp ? gfx2_rgb(PAL_SURF_0) : gfx2_rgb(PAL_INK));
    }

    valor(s, st);

    if (st->alt) {
        boton(s, pie_x(0U, n), UID_PIE_Y, pie_w(n), UID_PIE_H, 7, st->alt,
              &font_ui_14b, (uint8_t)(st->pressed == UID_HIT_ALT), st->alt_on);
    }
    boton(s, pie_x((uint8_t)(n - 1U), n), UID_PIE_Y, pie_w(n), UID_PIE_H, 7,
          "Volver", &font_ui_14b, (uint8_t)(st->pressed == UID_HIT_VOLVER), 0U);
}

static void draw_val(gfx2_surf_t *s, void *ctx)
{
    if (!gfx2_band_hits(s, (int16_t)(UID_BTN_Y + 10), 80)) { return; }
    valor(s, (const ui_det_state_t *)ctx);
}

void ui_det_draw(const ui_det_state_t *st)
{
    gfx2_render(0, UIG_Y, GFX2_W, UIG_H, draw_all, (void *)st);
}

void ui_det_draw_valor(const ui_det_state_t *st)
{
    gfx2_render(UID_VAL_X, (int16_t)(UID_BTN_Y + 10), UID_VAL_W, 80,
                draw_val, (void *)st);
}

void ui_det_draw_one(const ui_det_state_t *st, int8_t i)
{
    /* Los botones son pocos y baratos: se repinta la pantalla entera menos el
     * numero, que va por su cuenta. Mantener cuatro rutas de repintado
     * parcial para ahorrar milisegundos en una pantalla donde no hay nada
     * moviendose no compensa. */
    (void)i;
    ui_det_draw(st);
}
