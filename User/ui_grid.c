#include "ui_grid.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"

static int16_t cel_x(uint8_t i) { return (int16_t)(UIG_MARGIN + (i % UIG_COLS) * (UIG_CELL_W + UIG_GAP)); }
static int16_t cel_y(uint8_t i) { return (int16_t)(UIG_GRID_Y + (i / UIG_COLS) * (UIG_CELL_H + UIG_GAP)); }

/* Los tres botones de abajo, repartidos a lo ancho. */
/* Dos, no tres: "Cerrar" se va. Volver a la pantalla principal se hace
 * pulsando otra vez el boton de la barra de abajo por el que entraste, que
 * ademas aparece resaltado mientras la pantalla esta abierta - o sea que ya
 * se estaba anunciando como un interruptor. Un boton dentro que hace lo
 * mismo que otro fuera es una decision de mas cada vez que quieres salir. */
#define NAV_N 2
static int16_t nav_x(uint8_t i)
{
    int16_t w = (int16_t)((GFX2_W - 2 * UIG_MARGIN - (NAV_N - 1) * UIG_GAP) / NAV_N);
    return (int16_t)(UIG_MARGIN + i * (w + UIG_GAP));
}
static int16_t nav_w(void)
{
    return (int16_t)((GFX2_W - 2 * UIG_MARGIN - (NAV_N - 1) * UIG_GAP) / NAV_N);
}

int8_t ui_grid_hit(uint16_t x, uint16_t y)
{
    uint8_t i;

    /* Rejilla. Los huecos entre celdas se reparten entre las vecinas, igual
     * que en la barra de acciones: en un tactil que pide fuerza, un toque
     * que cae en la juntura y no hace nada se vive como que no ha entrado. */
    if (y >= UIG_GRID_Y - UIG_GAP / 2 &&
        y <  UIG_GRID_Y + UIG_ROWS * (UIG_CELL_H + UIG_GAP)) {
        for (i = 0U; i < UIG_CELLS; i++) {
            int16_t cx = cel_x(i), cy = cel_y(i);
            if ((int16_t)x >= cx - UIG_GAP / 2 && (int16_t)x < cx + UIG_CELL_W + UIG_GAP / 2 &&
                (int16_t)y >= cy - UIG_GAP / 2 && (int16_t)y < cy + UIG_CELL_H + UIG_GAP / 2) {
                return (int8_t)i;
            }
        }
        return UIG_HIT_NONE;
    }

    /* Fila de navegacion: se lleva todo lo que hay de la rejilla para abajo,
     * incluido el aire, para que no haya banda muerta. */
    if (y >= UIG_NAV_Y - 6 && y < UIG_Y + UIG_H) {
        for (i = 0U; i < NAV_N; i++) {
            if ((int16_t)x >= nav_x(i) - UIG_GAP / 2 &&
                (int16_t)x <  nav_x(i) + nav_w() + UIG_GAP / 2) {
                return (int8_t)(UIG_CELLS + i);
            }
        }
    }
    return UIG_HIT_NONE;
}

static void celda(gfx2_surf_t *s, const ui_grid_state_t *st, uint8_t i)
{
    int16_t x = cel_x(i), y = cel_y(i);
    const ui_grid_cell_t *c = &st->cel[i];
    uint8_t press  = (uint8_t)(st->pressed == (int8_t)i);
    uint8_t actual = (uint8_t)(st->marcada == i);
    uint8_t cursor = (uint8_t)(st->cursor == (int8_t)i);
    gfx2_rgba_t fondo, borde, tinta, tinta2;

    if (i >= st->n) {
        /* Hueco de una pagina incompleta: se deja el fondo, sin marco. Un
         * recuadro vacio invita a tocarlo. */
        return;
    }

    if (press) {
        fondo = gfx2_rgb(PAL_ACCENT);   borde = gfx2_rgb(PAL_ACCENT);
        tinta = gfx2_rgb(PAL_SURF_0);   tinta2 = gfx2_rgba(PAL_SURF_0, 200);
    } else if (actual) {
        /* La banda en la que estas AHORA. Sin esto, la pantalla no dice de
         * donde vienes y es facil volver a pulsar la misma. */
        fondo = gfx2_rgba(PAL_ACCENT, 44); borde = gfx2_rgba(PAL_ACCENT, 170);
        tinta = gfx2_rgb(PAL_ACCENT);      tinta2 = gfx2_rgb(PAL_INK);
    } else {
        fondo = gfx2_rgb(PAL_SURF_2);   borde = gfx2_rgb(PAL_LINE);
        tinta = gfx2_rgb(PAL_INK);      tinta2 = gfx2_rgb(PAL_INK_MUTE);
    }

    gfx2_rrect(s, x, y, UIG_CELL_W, UIG_CELL_H, 8, fondo);
    gfx2_rrect_outline(s, x, y, UIG_CELL_W, UIG_CELL_H, 8, 1, borde);
    if (cursor && !press) {
        /* Donde apunta el MANDO. Contorno grueso, no relleno: asi se
         * distingue de "esta es tu banda ahora" (relleno tenue) y las dos
         * cosas pueden verse a la vez en la misma celda, que es justo lo que
         * pasa al abrir la pantalla. */
        gfx2_rrect_outline(s, x, y, UIG_CELL_W, UIG_CELL_H, 8, 2,
                           gfx2_rgb(PAL_ACCENT));
    }

    if (c->l3) {
        /* Tres renglones: titulo, dato y dato secundario. */
        gfx2_text_in(s, x, (int16_t)(y + 8), UIG_CELL_W, c->l1,
                     &font_ui_18b, tinta, GFX2_ALIGN_C);
        gfx2_text_in(s, x, (int16_t)(y + 33), UIG_CELL_W, c->l2,
                     &font_ui_14, tinta2, GFX2_ALIGN_C);
        gfx2_text_in(s, x, (int16_t)(y + 53), UIG_CELL_W, c->l3,
                     &font_ui_14b, tinta2, GFX2_ALIGN_C);
    } else {
        /* Dos renglones: se centran en la altura de la celda en vez de
         * dejar el hueco del tercero abajo, que se veia descolgado. */
        gfx2_text_in(s, x, (int16_t)(y + 15), UIG_CELL_W, c->l1,
                     &font_ui_18b, tinta, GFX2_ALIGN_C);
        gfx2_text_in(s, x, (int16_t)(y + 42), UIG_CELL_W, c->l2,
                     &font_ui_14b, tinta2, GFX2_ALIGN_C);
    }
}

static void boton_nav(gfx2_surf_t *s, const ui_grid_state_t *st,
                      uint8_t i, const char *txt, uint8_t habilitado)
{
    int16_t x = nav_x(i), w = nav_w();
    uint8_t press = (uint8_t)(st->pressed == (int8_t)(UIG_CELLS + i));
    gfx2_rgba_t fondo, borde, tinta;

    if (!habilitado) {
        /* Deshabilitado: se dibuja, apagado y sin contorno, en vez de
         * desaparecer. Un boton que salta de sitio segun la pagina obliga a
         * volver a buscarlo cada vez. */
        fondo = gfx2_rgb(PAL_SURF_1); borde = fondo; tinta = gfx2_rgb(PAL_INK_MUTE);
    } else if (press) {
        fondo = gfx2_rgb(PAL_ACCENT); borde = fondo; tinta = gfx2_rgb(PAL_SURF_0);
    } else {
        fondo = gfx2_rgb(PAL_SURF_2); borde = gfx2_rgb(PAL_LINE); tinta = gfx2_rgb(PAL_INK);
    }

    gfx2_rrect(s, x, UIG_NAV_Y, w, UIG_NAV_H, 7, fondo);
    if (habilitado) {
        gfx2_rrect_outline(s, x, UIG_NAV_Y, w, UIG_NAV_H, 7, 1, borde);
    }
    gfx2_text_in(s, x, (int16_t)(UIG_NAV_Y + 8), w, txt, &font_ui_14b, tinta,
                 GFX2_ALIGN_C);
}

static void draw_all(gfx2_surf_t *s, void *ctx)
{
    const ui_grid_state_t *st = (const ui_grid_state_t *)ctx;
    char pg[12];
    uint8_t i;

    if (!gfx2_band_hits(s, UIG_Y, UIG_H)) { return; }

    gfx2_fill(s, 0, UIG_Y, GFX2_W, UIG_H, gfx2_rgb(PAL_SURF_0));

    /* Cabecera: familia a la izquierda, pagina a la derecha. */
    gfx2_text(s, UIG_MARGIN + 4, (int16_t)(UIG_Y + 6), st->titulo,
              &font_ui_18b, gfx2_rgb(PAL_INK));
    {
        uint8_t k = 0;
        pg[k++] = (char)('0' + (st->pagina + 1U));
        pg[k++] = ' '; pg[k++] = 'd'; pg[k++] = 'e'; pg[k++] = ' ';
        pg[k++] = (char)('0' + st->paginas);
        pg[k] = '\0';
    }
    gfx2_text_in(s, 0, (int16_t)(UIG_Y + 9), (int16_t)(GFX2_W - UIG_MARGIN - 4),
                 pg, &font_ui_14, gfx2_rgb(PAL_INK_MUTE), GFX2_ALIGN_R);
    gfx2_hline(s, UIG_MARGIN, (int16_t)(UIG_Y + UIG_HDR_H - 1),
               (int16_t)(GFX2_W - 2 * UIG_MARGIN), gfx2_rgb(PAL_LINE));

    for (i = 0U; i < UIG_CELLS; i++) {
        celda(s, st, i);
    }

    /* Con una sola pagina no hay pie: dos botones apagados que no llevan a
     * ninguna parte ocupan sitio y piden que los mires para descartarlos. */
    if (st->paginas > 1U) {
        for (i = 0U; i < NAV_N; i++) {
            boton_nav(s, st, i, st->nav[i], st->nav_on[i]);
        }
    }
}

typedef struct { const ui_grid_state_t *st; int8_t i; } uno_t;

static void draw_uno(gfx2_surf_t *s, void *ctx)
{
    const uno_t *o = (const uno_t *)ctx;

    if (o->i < UIG_CELLS) {
        gfx2_fill(s, cel_x((uint8_t)o->i), cel_y((uint8_t)o->i),
                  UIG_CELL_W, UIG_CELL_H, gfx2_rgb(PAL_SURF_0));
        celda(s, o->st, (uint8_t)o->i);
    } else {
        uint8_t k = (uint8_t)(o->i - UIG_CELLS);
        gfx2_fill(s, nav_x(k), UIG_NAV_Y, nav_w(), UIG_NAV_H, gfx2_rgb(PAL_SURF_0));
        boton_nav(s, o->st, k, o->st->nav[k], o->st->nav_on[k]);
    }
}

void ui_grid_draw(const ui_grid_state_t *st)
{
    gfx2_render(0, UIG_Y, GFX2_W, UIG_H, draw_all, (void *)st);
}

void ui_grid_draw_one(const ui_grid_state_t *st, int8_t i)
{
    uno_t o;

    if (i < 0 || i >= UIG_HIT_COUNT) { return; }
    o.st = st;
    o.i  = i;
    if (i < UIG_CELLS) {
        gfx2_render(cel_x((uint8_t)i), cel_y((uint8_t)i), UIG_CELL_W, UIG_CELL_H,
                    draw_uno, &o);
    } else {
        gfx2_render(nav_x((uint8_t)(i - UIG_CELLS)), UIG_NAV_Y, nav_w(), UIG_NAV_H,
                    draw_uno, &o);
    }
}
