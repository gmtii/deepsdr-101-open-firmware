#include "ui_cfg.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"

/* Geometria segun el reparto. Se guarda el ultimo estado dibujado para que
 * ui_cfg_hit() sepa cual esta en pantalla sin que el llamante tenga que
 * pasarselo: lo que se toca es, por construccion, lo ultimo que se pinto. */
static uint8_t s_denso = 0U;
static uint8_t s_cats  = UIC_CATS;

static int16_t cel_w(void) { return s_denso ? UIC_DCELL_W : UIC_CELL_W; }
static int16_t cel_h(void) { return s_denso ? UIC_DCELL_H : UIC_CELL_H; }
static int16_t cel_g(void) { return s_denso ? UIC_DGAP    : UIC_GAP; }
static uint8_t cel_cols(void) { return s_denso ? UIC_DCOLS : UIC_COLS; }
static uint8_t cel_n(void) { return (uint8_t)(cel_cols() * (s_denso ? UIC_DROWS : UIC_ROWS)); }

static int16_t cel_x(uint8_t i) { return (int16_t)(UIC_GRID_X + (i % cel_cols()) * (cel_w() + cel_g())); }
static int16_t cel_y(uint8_t i) { return (int16_t)(UIC_GRID_Y + (i / cel_cols()) * (cel_h() + cel_g())); }

/* Las categorias se reparten el alto disponible: con tres salen mas altas que
 * con cuatro, en vez de dejar un hueco abajo. */
static int16_t cat_h(void) { return (int16_t)((UIG_H - 12 - (s_cats - 1) * UIC_CAT_GAP) / s_cats); }
static int16_t cat_y(uint8_t i) { return (int16_t)(UIC_CAT_Y + i * (cat_h() + UIC_CAT_GAP)); }

int8_t ui_cfg_hit(uint16_t x, uint16_t y)
{
    uint8_t i;

    if (y < UIG_Y || y >= UIG_Y + UIG_H) { return UIC_HIT_NONE; }

    /* Columna de la izquierda. Se lleva todo el ancho hasta donde empieza la
     * rejilla, incluido el margen: no hay franja muerta entre las dos zonas. */
    if ((int16_t)x < UIC_GRID_X - UIC_GAP / 2) {
        for (i = 0U; i < s_cats; i++) {
            if ((int16_t)y >= cat_y(i) - UIC_CAT_GAP / 2 &&
                (int16_t)y <  cat_y(i) + cat_h() + UIC_CAT_GAP / 2) {
                return (int8_t)(UIC_HIT_CAT0 + i);
            }
        }
        return UIC_HIT_NONE;
    }

    for (i = 0U; i < cel_n(); i++) {
        int16_t cx = cel_x(i), cy = cel_y(i);
        if ((int16_t)x >= cx - cel_g() / 2 && (int16_t)x < cx + cel_w() + cel_g() / 2 &&
            (int16_t)y >= cy - cel_g() / 2 && (int16_t)y < cy + cel_h() + cel_g() / 2) {
            return (int8_t)i;
        }
    }
    return UIC_HIT_NONE;
}

static void categoria(gfx2_surf_t *s, const ui_cfg_state_t *st, uint8_t i)
{
    int16_t y = cat_y(i), h = cat_h();
    uint8_t sel   = (uint8_t)(st->cat_sel == i);
    uint8_t press = (uint8_t)(st->pressed == (int8_t)(UIC_HIT_CAT0 + i));
    gfx2_rgba_t fondo, borde, tinta;

    if (press) {
        fondo = gfx2_rgb(PAL_ACCENT); borde = fondo; tinta = gfx2_rgb(PAL_SURF_0);
    } else if (sel) {
        fondo = gfx2_rgb(PAL_SURF_2); borde = gfx2_rgb(PAL_ACCENT); tinta = gfx2_rgb(PAL_INK);
    } else {
        fondo = gfx2_rgb(PAL_SURF_0); borde = gfx2_rgb(PAL_LINE); tinta = gfx2_rgb(PAL_INK_DIM);
    }

    gfx2_rrect(s, UIC_SIDE_X, y, UIC_SIDE_W, h, 9, fondo);
    gfx2_rrect_outline(s, UIC_SIDE_X, y, UIC_SIDE_W, h, 9, 1, borde);
    if (sel && !press) {
        /* Barra de acento: la categoria activa no depende SOLO del color, que
         * es lo que la haria indistinguible en deuteranopia. */
        gfx2_rrect(s, (int16_t)(UIC_SIDE_X + 8), (int16_t)(y + h / 2 - 16),
                   5, 32, 2, gfx2_rgb(PAL_ACCENT));
    }
    gfx2_text_in(s, (int16_t)(UIC_SIDE_X + 18), (int16_t)(y + h / 2 - 10),
                 (int16_t)(UIC_SIDE_W - 26), st->cat[i], &font_ui_18b, tinta,
                 GFX2_ALIGN_C);
}


static void celda(gfx2_surf_t *s, const ui_cfg_state_t *st, uint8_t i)
{
    int16_t x = cel_x(i), y = cel_y(i), w = cel_w(), h = cel_h();
    uint8_t press  = (uint8_t)(st->pressed == (int8_t)i);
    uint8_t cursor = (uint8_t)(st->cursor == (int8_t)i);
    uint8_t marca  = (uint8_t)(st->marcada == i);
    gfx2_rgba_t fondo, borde, tinta, tinta2;

    if (i >= st->n) { return; }   /* hueco: fondo y nada mas */

    if (press) {
        fondo = gfx2_rgb(PAL_ACCENT); borde = fondo;
        tinta = gfx2_rgb(PAL_SURF_0); tinta2 = gfx2_rgba(PAL_SURF_0, 200);
    } else if (marca) {
        /* "aqui estas ahora": relleno tenue, distinto del contorno grueso del
         * cursor del mando, para que las dos cosas puedan verse a la vez. */
        fondo = gfx2_rgba(PAL_ACCENT, 44); borde = gfx2_rgba(PAL_ACCENT, 170);
        tinta = gfx2_rgb(PAL_INK_MUTE); tinta2 = gfx2_rgb(PAL_ACCENT);
    } else {
        fondo = gfx2_rgb(PAL_SURF_2); borde = gfx2_rgb(PAL_LINE);
        tinta = gfx2_rgb(PAL_INK_MUTE); tinta2 = gfx2_rgb(PAL_INK);
    }

    gfx2_rrect(s, x, y, w, h, 10, fondo);
    gfx2_rrect_outline(s, x, y, w, h, 10,
                       (cursor && !press) ? 2 : 1,
                       (cursor && !press) ? gfx2_rgb(PAL_ACCENT) : borde);

    if (st->cel[i].extra) {
        /* Tres renglones (bandas): nombre destacado arriba, rango y modo
         * debajo. Aqui el nombre SI es lo que se busca. */
        gfx2_text_in(s, x, (int16_t)(y + 6), w, st->cel[i].nombre,
                     &font_ui_18b, press ? tinta : gfx2_rgb(PAL_INK), GFX2_ALIGN_C);
        gfx2_text_in(s, x, (int16_t)(y + 30), w, st->cel[i].valor,
                     &font_ui_14, tinta, GFX2_ALIGN_C);
        gfx2_text_in(s, x, (int16_t)(y + 49), w, st->cel[i].extra,
                     &font_ui_14b, tinta2, GFX2_ALIGN_C);
    } else {
        /* Dos renglones (ajustes): nombre arriba, pequeño; valor abajo,
         * grande. Ver el comentario de ui_cfg.h sobre por que en ese orden. */
        gfx2_text(s, (int16_t)(x + 14), (int16_t)(y + 12), st->cel[i].nombre,
                  &font_ui_14, tinta);
        gfx2_text(s, (int16_t)(x + 14), (int16_t)(y + h - 42),
                  st->cel[i].valor, &font_ui_18b,
                  press ? tinta2 : (cursor ? gfx2_rgb(PAL_ACCENT) : tinta2));
    }
}

static void draw_all(gfx2_surf_t *s, void *ctx)
{
    const ui_cfg_state_t *st = (const ui_cfg_state_t *)ctx;
    uint8_t i;

    if (!gfx2_band_hits(s, UIG_Y, UIG_H)) { return; }

    gfx2_fill(s, 0, UIG_Y, GFX2_W, UIG_H, gfx2_rgb(PAL_SURF_0));
    for (i = 0U; i < s_cats; i++) { categoria(s, st, i); }
    for (i = 0U; i < cel_n(); i++) { celda(s, st, i); }
}

typedef struct { const ui_cfg_state_t *st; int8_t i; } uno_t;

static void draw_uno(gfx2_surf_t *s, void *ctx)
{
    const uno_t *o = (const uno_t *)ctx;

    if (o->i < (int8_t)cel_n()) {
        gfx2_fill(s, cel_x((uint8_t)o->i), cel_y((uint8_t)o->i),
                  cel_w(), cel_h(), gfx2_rgb(PAL_SURF_0));
        celda(s, o->st, (uint8_t)o->i);
    } else if (o->i >= UIC_HIT_CAT0) {
        uint8_t k = (uint8_t)(o->i - UIC_HIT_CAT0);
        gfx2_fill(s, UIC_SIDE_X, cat_y(k), UIC_SIDE_W, cat_h(), gfx2_rgb(PAL_SURF_0));
        categoria(s, o->st, k);
    }
    /* Entre cel_n() y UIC_HIT_CAT0 no hay nada cuando el reparto no es denso:
     * ui_cfg_hit() no devuelve esos indices, pero no restar a ciegas evita
     * que un llamante equivocado se lleve una categoria negativa. */
}

void ui_cfg_draw(const ui_cfg_state_t *st)
{
    s_denso = st->denso;
    s_cats  = (st->cats == 0U || st->cats > UIC_CATS) ? UIC_CATS : st->cats;
    gfx2_render(0, UIG_Y, GFX2_W, UIG_H, draw_all, (void *)st);
}

void ui_cfg_draw_one(const ui_cfg_state_t *st, int8_t i)
{
    uno_t o;

    if (i < 0 || i >= UIC_HIT_COUNT) { return; }
    s_denso = st->denso;
    s_cats  = (st->cats == 0U || st->cats > UIC_CATS) ? UIC_CATS : st->cats;
    o.st = st; o.i = i;
    if (i < (int8_t)cel_n()) {
        gfx2_render(cel_x((uint8_t)i), cel_y((uint8_t)i), cel_w(), cel_h(),
                    draw_uno, &o);
    } else {
        gfx2_render(UIC_SIDE_X, cat_y((uint8_t)(i - UIC_HIT_CAT0)),
                    UIC_SIDE_W, cat_h(), draw_uno, &o);
    }
}
