#include "gfx.h"
#include "gfx_font.h"

/*
 * BANDA SUPERIOR RESERVADA (guard)
 * --------------------------------
 * A partir de la etapa 2, la franja de y=0 a y=GFX_GUARD_TOP-1 (cabecera +
 * barra de estado) la dibuja ui_top.c con gfx2, que escribe al panel por su
 * cuenta y NO pasa por este fichero. Todo lo demas de la interfaz -los
 * widgets de ui.c, los readouts antiguos, los badges- si pasa por aqui.
 *
 * El problema que resuelve esto: bastaba con que UNA llamada vieja siguiera
 * pintando ahi para que apareciera un recuadro plano encima de la cabecera
 * nueva (el caso que se vio en hardware: al cambiar el AGC desde el menu
 * salia un rectangulo cian durante un segundo, porque ui_button_draw()
 * rellenaba el rectangulo del boton con su color de fondo). Ir caso por caso
 * no cierra el problema: cualquier codigo futuro puede volver a abrirlo.
 *
 * Asi que se recorta aqui, en el unico sitio por el que pasan todas: cuatro
 * primitivas (pixel, fill_rect, blit, fill_screen) son el embudo de gfx_hline,
 * gfx_vline, gfx_rect, gfx_line, gfx_char y gfx_text. Con el guard armado, un
 * dibujo que cae entero en la banda no hace NINGUN acceso al bus, y uno que
 * la cruza se recorta a la parte de abajo.
 *
 * Arranca a 0 (desactivado) para que las pantallas que se apoderan de todo el
 * panel -el asistente de calibracion del tactil, el borrado al dormir, la
 * regla de calibracion de altura- sigan funcionando igual. main.c lo arma al
 * final de radio_screen_draw() y lo desarma antes de esas.
 */
static uint16_t s_guard_top = 0U;

void gfx_guard_top_set(uint16_t rows)
{
    s_guard_top = (rows > GFX_SCREEN_HEIGHT) ? GFX_SCREEN_HEIGHT : rows;
}

uint16_t gfx_guard_top(void)
{
    return s_guard_top;
}

void gfx_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (y < s_guard_top) {
        return;
    }
    rm68120_set_window(x, y, x, y);
    rm68120_write_data(color);
}

void gfx_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint32_t n;

    if (w == 0 || h == 0) {
        return;
    }

    if (y < s_guard_top) {
        uint16_t skip = (uint16_t)(s_guard_top - y);
        if (skip >= h) {
            return;          /* cae entero en la banda reservada */
        }
        y = s_guard_top;
        h = (uint16_t)(h - skip);
    }

    rm68120_set_window(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));

    n = (uint32_t)w * (uint32_t)h;
    while (n--) {
        rm68120_write_data(color);
    }
}

void gfx_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    gfx_fill_rect(x, y, w, 1, color);
}

void gfx_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
    gfx_fill_rect(x, y, 1, h, color);
}

void gfx_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0 || h == 0) {
        return;
    }
    gfx_hline(x, y, w, color);
    gfx_hline(x, (uint16_t)(y + h - 1), w, color);
    gfx_vline(x, y, h, color);
    gfx_vline((uint16_t)(x + w - 1), y, h, color);
}

void gfx_fill_screen(uint16_t color)
{
    if (s_guard_top != 0U) {
        /* Respeta la banda reservada: borra solo de ahi para abajo. Quien
         * quiera borrar el panel ENTERO (dormir, calibrar) desarma el guard
         * primero - ver gfx_guard_top_set(). */
        gfx_fill_rect(0, s_guard_top, GFX_SCREEN_WIDTH,
                      (uint16_t)(GFX_SCREEN_HEIGHT - s_guard_top), color);
        return;
    }
    rm68120_fill_screen(color);
}

void gfx_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    int16_t dx, dy, sx, sy, err, e2;

    if (y0 == y1) {
        uint16_t x_start = (uint16_t)((x0 < x1) ? x0 : x1);
        uint16_t w = (uint16_t)((x0 < x1) ? (x1 - x0 + 1) : (x0 - x1 + 1));
        gfx_hline(x_start, (uint16_t)y0, w, color);
        return;
    }
    if (x0 == x1) {
        uint16_t y_start = (uint16_t)((y0 < y1) ? y0 : y1);
        uint16_t h = (uint16_t)((y0 < y1) ? (y1 - y0 + 1) : (y0 - y1 + 1));
        gfx_vline((uint16_t)x0, y_start, h, color);
        return;
    }

    /* Bresenham generico. Reabre ventana en cada pixel (gfx_pixel), mas
     * lento que los casos H/V de arriba - normal para lineas diagonales
     * ocasionales (p.ej. traza del espectro), no pensado para volumenes
     * grandes de pixeles por segundo. */
    dx = (int16_t)((x1 > x0) ? (x1 - x0) : (x0 - x1));
    dy = (int16_t)((y1 > y0) ? (y0 - y1) : (y1 - y0)); /* negativo o cero */
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = (int16_t)(dx + dy);

    for (;;) {
        gfx_pixel((uint16_t)x0, (uint16_t)y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        e2 = (int16_t)(2 * err);
        if (e2 >= dy) {
            err = (int16_t)(err + dy);
            x0 = (int16_t)(x0 + sx);
        }
        if (e2 <= dx) {
            err = (int16_t)(err + dx);
            y0 = (int16_t)(y0 + sy);
        }
    }
}

void gfx_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels)
{
    uint32_t n, i;

    if (w == 0 || h == 0 || pixels == NULL) {
        return;
    }

    if (y < s_guard_top) {
        uint16_t skip = (uint16_t)(s_guard_top - y);
        if (skip >= h) {
            return;
        }
        pixels += (uint32_t)skip * (uint32_t)w;  /* salta las filas recortadas */
        y = s_guard_top;
        h = (uint16_t)(h - skip);
    }

    rm68120_set_window(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));

    n = (uint32_t)w * (uint32_t)h;
    for (i = 0; i < n; i++) {
        rm68120_write_data(pixels[i]);
    }
}

/* Devuelve las columnas (5 bytes) del glyph de c, o el de espacio si c
 * esta fuera de la tabla cubierta (ver gfx_font.h, 0x20-0x5A). */
static const uint8_t *gfx_glyph_for(char c)
{
    uint8_t code = (uint8_t)c;

    if (code < GFX_FONT_FIRST_CHAR || code > GFX_FONT_LAST_CHAR) {
        code = ' ';
    }
    return gfx_font5x7[code - GFX_FONT_FIRST_CHAR];
}

void gfx_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
    const uint8_t *glyph;
    uint8_t col, row;

    if (scale == 0) {
        scale = 1;
    }
    glyph = gfx_glyph_for(c);

    for (col = 0; col < GFX_FONT_WIDTH; col++) {
        uint8_t bits = glyph[col];
        for (row = 0; row < GFX_FONT_HEIGHT; row++) {
            uint16_t color = (bits & (1U << row)) ? fg : bg;
            uint16_t px = (uint16_t)(x + (uint16_t)col * scale);
            uint16_t py = (uint16_t)(y + (uint16_t)row * scale);
            gfx_fill_rect(px, py, scale, scale, color);
        }
    }
    /* columna de separacion entre caracteres (1 pixel logico * scale) */
    gfx_fill_rect((uint16_t)(x + (uint16_t)GFX_FONT_WIDTH * scale), y, scale,
                  (uint16_t)(GFX_FONT_HEIGHT * scale), bg);
}

void gfx_text(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint16_t cursor_x = x;
    uint8_t step = (uint8_t)((GFX_FONT_WIDTH + 1) * ((scale == 0) ? 1 : scale));

    while (*str != '\0') {
        gfx_char(cursor_x, y, *str, fg, bg, scale);
        cursor_x = (uint16_t)(cursor_x + step);
        str++;
    }
}

uint16_t gfx_text_width(const char *str, uint8_t scale)
{
    uint16_t len = 0;
    uint8_t step = (uint8_t)((GFX_FONT_WIDTH + 1) * ((scale == 0) ? 1 : scale));

    while (*str != '\0') {
        len = (uint16_t)(len + step);
        str++;
    }
    return len;
}

uint16_t gfx_text_height(uint8_t scale)
{
    return (uint16_t)(GFX_FONT_HEIGHT * ((scale == 0) ? 1 : scale));
}
