#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include "rm68120_exmc.h"

/*
 * Primitivas graficas de bajo nivel para la UI del SDR.
 *
 * IMPORTANTE - modelo de dibujo: estas funciones NO usan un framebuffer
 * en RAM. Escriben directamente en la GRAM del panel a traves de
 * rm68120_set_window() + rm68120_write_data(), aprovechando que tras fijar
 * una ventana el controlador autoincrementa la direccion en la GRAM con
 * cada escritura (primero en X, luego en Y). Esto es lo correcto para la
 * UI general (botones, marcos, texto, barras esteticas) porque:
 *
 *   - 480x800x2 bytes = 750KB no caben en los 192KB de RAM disponibles,
 *     asi que un framebuffer completo esta descartado.
 *   - La UI cambia poco por segundo (botones, labels) comparado con el
 *     waterfall/espectro, que si necesita su propio buffer parcial en RAM
 *     para poder hacer scroll - ver waterfall.h para eso.
 *
 * Coste de una llamada: rm68120_set_window() manda 8 escrituras de
 * comando/dato (CASET x4 + RASET x4) + 1 de RAMWR antes de poder empezar
 * a volcar pixeles. Por eso gfx_hline/gfx_vline/gfx_fill_rect fijan la
 * ventana UNA sola vez y escriben todos los pixeles seguidos, en vez de
 * llamar a gfx_pixel() en un bucle (que reabriria ventana en cada pixel).
 */

/* --- Colores RGB565 --- */
#define GFX_COLOR_BLACK     0x0000
#define GFX_COLOR_WHITE     0xFFFF
#define GFX_COLOR_RED       0xF800
#define GFX_COLOR_GREEN     0x07E0
#define GFX_COLOR_BLUE      0x001F
#define GFX_COLOR_YELLOW    0xFFE0
#define GFX_COLOR_CYAN      0x07FF
#define GFX_COLOR_MAGENTA   0xF81F
#define GFX_COLOR_GRAY      0x8410
#define GFX_COLOR_DARKGRAY  0x4208
#define GFX_COLOR_ORANGE    0xFC00

static inline uint16_t gfx_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8) << 8) |
                       ((uint16_t)(g & 0xFC) << 3) |
                       ((uint16_t)(b >> 3)));
}

/* Dimensiones fisicas del panel. Confirmado con hardware real (foto):
 * con el MADCTL actual (0xA3, ver rm68120_exmc.h) el panel funciona en
 * horizontal - texto y trazos se ven sin rotar, pero el contenido pintado
 * asumiendo 480 de ancho solo ocupaba la franja izquierda de un panel mas
 * ancho. O sea: CASET (x, columnas) llega hasta 799, RASET (y, filas)
 * hasta 479 - lo contrario de lo que decia el README original (480x800
 * "vertical"). Si mas adelante cambia el MADCTL, revisar esto tambien. */
#define GFX_SCREEN_WIDTH   800
#define GFX_SCREEN_HEIGHT  480

/* --- Primitivas de relleno / lineas rectas (rapidas, ventana unica) --- */
/*
 * Banda superior reservada. Con rows > 0, TODAS las primitivas de este
 * fichero recortan lo que caiga en y < rows: la cabecera y la barra de
 * estado las dibuja ui_top.c con gfx2, que no pasa por aqui, y asi ningun
 * readout antiguo ni ningun widget de ui.c puede pintar encima. Ver el
 * comentario largo en gfx.c. 0 = desactivado (valor de arranque).
 */
void gfx_guard_top_set(uint16_t rows);
uint16_t gfx_guard_top(void);

void gfx_pixel(uint16_t x, uint16_t y, uint16_t color);
void gfx_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void gfx_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
void gfx_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color);
void gfx_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color); /* solo borde, 1px */
void gfx_fill_screen(uint16_t color); /* wrapper de rm68120_fill_screen, mismo formato de API */

/* Linea generica (Bresenham). Para pendientes arbitrarias hace falta
 * reabrir ventana pixel a pixel (mas lenta que gfx_hline/vline) - la usa
 * internamente cuando detecta que la linea no es horizontal ni vertical. */
void gfx_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

/* Volcado de un buffer de pixeles RGB565 ya en RAM a una ventana
 * rectangular de la GRAM. w*h debe coincidir con el numero de elementos
 * de pixels. Es la funcion que usa waterfall.c para pintar su buffer. */
void gfx_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels);

/* --- Texto (fuente 5x7 monoespaciada, ver gfx_font.h) --- */
/* scale multiplica cada pixel de la fuente (scale=1 -> letras de 5x7 reales,
 * scale=2 -> 10x14, etc). bg se usa como fondo de cada glyph; si fg==bg no
 * tiene sentido (letra invisible), es responsabilidad del llamante evitarlo. */
void gfx_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);
void gfx_text(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t scale);
uint16_t gfx_text_width(const char *str, uint8_t scale);
uint16_t gfx_text_height(uint8_t scale);

#endif /* GFX_H */
