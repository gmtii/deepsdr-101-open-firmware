#ifndef GFX2_H_INCLUDED
#define GFX2_H_INCLUDED

#include <stdint.h>
#include "gfx2_font.h"

/*
 * gfx2 - nucleo grafico con compositor por bandas para el DEEPSDR 101.
 *
 * QUE PROBLEMA RESUELVE
 * ---------------------
 * El gfx.c actual escribe directo a la GRAM del panel. Eso impide tres
 * cosas a la vez: antialiasing (no se puede leer el fondo para mezclar),
 * transparencias, y actualizacion sin parpadeo. Y ademas resulta lento
 * donde mas duele: gfx_char() abre una ventana del controlador POR CADA
 * PIXEL del glyph, 17 accesos al bus para pintar uno.
 *
 * gfx2 compone en RAM y vuelca. Un framebuffer completo no cabe
 * (800*480*2 = 750KB frente a 192KB de SRAM), asi que compone por BANDAS
 * horizontales: se reserva una banda de GFX2_BAND_H filas, el codigo de
 * dibujo se ejecuta una vez por banda con recorte vertical, y cada banda
 * sale al panel con UNA sola ventana.
 *
 * El coste de reejecutar el dibujo por banda es CPU, que sobra; lo que no
 * sobra son accesos al bus EXMC, que es justo lo que esto minimiza.
 *
 * PRESUPUESTO DE RAM
 * ------------------
 * La banda es 800 x GFX2_BAND_H x 2 bytes. Con BAND_H=24 son 37.5KB.
 * Eso solo cabe si el waterfall pasa a guardarse como indices de 8 bits
 * en vez de RGB565 (libera 56KB) - ver wf_cmap.h y el comentario de
 * gfx2_wf_blit().
 */

#define GFX2_W      800
#define GFX2_H      480
/*
 * Altura de la banda compositora, en filas. El gasto de RAM es 800 * n * 2
 * bytes, asi que durante el port por etapas conviene empezar pequeno:
 *
 *   ETAPA 1:          8 filas = 12,5 KB, que era lo que cabia con el
 *                     waterfall todavia guardado en RGB565.
 *   ETAPA 3 (ahora): 24 filas = 37,5 KB, ya con el waterfall en indices de
 *                     8 bits, que libero 56 KB.
 *
 * Mas filas significa menos bandas, y por tanto menos veces que se reejecuta
 * el codigo de dibujo por cada repintado. Se puede forzar desde la linea de
 * compilacion con -DGFX2_BAND_H=n.
 */
#ifndef GFX2_BAND_H
#define GFX2_BAND_H 24
#endif

/* --- Superficie de composicion ------------------------------------------
 * Un rectangulo de pixeles en RAM mas su posicion en pantalla. Las
 * primitivas reciben coordenadas de PANTALLA y recortan solas contra la
 * banda, asi que el codigo de dibujo se escribe una vez y no se entera de
 * que esta corriendo por bandas. */
typedef struct {
    uint16_t *px;    /* buffer de w*h pixeles RGB565 */
    int16_t   x, y;  /* esquina superior izquierda en coordenadas de pantalla */
    int16_t   w, h;
} gfx2_surf_t;

/* Color en formato de trabajo: RGB888 + alpha, para poder mezclar bien.
 * Se convierte a RGB565 solo al escribir en la banda. */
typedef struct { uint8_t r, g, b, a; } gfx2_rgba_t;

static inline gfx2_rgba_t gfx2_rgb(uint32_t hex)
{
    gfx2_rgba_t c;
    c.r = (uint8_t)((hex >> 16) & 0xFF);
    c.g = (uint8_t)((hex >> 8) & 0xFF);
    c.b = (uint8_t)(hex & 0xFF);
    c.a = 255;
    return c;
}

static inline gfx2_rgba_t gfx2_rgba(uint32_t hex, uint8_t a)
{
    gfx2_rgba_t c = gfx2_rgb(hex);
    c.a = a;
    return c;
}

static inline uint16_t gfx2_to565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8) << 8) |
                      ((uint16_t)(g & 0xFC) << 3) |
                      ((uint16_t)b >> 3));
}

/* Cierto si el rango vertical [y, y+h) toca la banda que se esta componiendo.
 * Las primitivas y el codigo de dibujo lo usan para salir cuanto antes: el
 * callback de dibujo se ejecuta una vez POR BANDA (20 para pantalla completa),
 * asi que todo lo que no se corte pronto se paga 20 veces. */
static inline int gfx2_band_hits(const gfx2_surf_t *s, int16_t y, int16_t h)
{
    return !(y >= (int16_t)(s->y + s->h) || (int16_t)(y + h) <= s->y);
}

/* --- Render por bandas --------------------------------------------------
 * Ejecuta draw() una vez por banda que interseque el rectangulo pedido y
 * vuelca cada banda al panel. Es el unico punto que habla con el driver. */
void gfx2_render(int16_t x, int16_t y, int16_t w, int16_t h,
                 void (*draw)(gfx2_surf_t *s, void *ctx), void *ctx);

/* Igual, pero para toda la pantalla. */
void gfx2_render_screen(void (*draw)(gfx2_surf_t *s, void *ctx), void *ctx);

/* Registra una funcion que se llama tras volcar cada banda. Pensada para
 * muestrear la entrada durante un repintado largo. 0 la desactiva. */
void gfx2_set_pump(void (*fn)(void));

/* --- Primitivas ---------------------------------------------------------
 * Todas toman coordenadas de PANTALLA y mezclan segun el alpha del color. */
void gfx2_fill(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
               gfx2_rgba_t c);

/* Degradado vertical de c0 (arriba) a c1 (abajo). Usa dithering ordenado
 * 4x4 para que la cuantizacion a RGB565 no produzca bandas visibles:
 * en un degradado de 64px con 32 niveles de verde, sin dither se ven los
 * escalones. */
void gfx2_vgrad(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                gfx2_rgba_t c0, gfx2_rgba_t c1);

/* Rectangulo de esquinas redondeadas, con bordes antialiasados. r=0 da un
 * rectangulo normal. */
void gfx2_rrect(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                int16_t r, gfx2_rgba_t c);

/* Solo el contorno, grosor t, tambien con esquinas antialiasadas. */
void gfx2_rrect_outline(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                        int16_t r, int16_t t, gfx2_rgba_t c);

void gfx2_hline(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, gfx2_rgba_t c);
void gfx2_vline(gfx2_surf_t *s, int16_t x, int16_t y, int16_t h, gfx2_rgba_t c);

/* --- Texto -------------------------------------------------------------- */
typedef enum { GFX2_ALIGN_L = 0, GFX2_ALIGN_C, GFX2_ALIGN_R } gfx2_align_t;

/* Dibuja str con la linea superior en y. Devuelve el ancho consumido. */
int16_t gfx2_text(gfx2_surf_t *s, int16_t x, int16_t y, const char *str,
                  const gfx2_font_t *f, gfx2_rgba_t c);

/* Igual, pero alineado dentro de [x, x+w). */
void gfx2_text_in(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w,
                  const char *str, const gfx2_font_t *f, gfx2_rgba_t c,
                  gfx2_align_t al);

/* Ancho de str en px, sin dibujar. Se puede llamar sin superficie. */
int16_t gfx2_text_w(const char *str, const gfx2_font_t *f);

/* --- Volcado del waterfall ----------------------------------------------
 * idx es el buffer de indices de 8 bits (no RGB565): el ahorro de 56KB que
 * paga la banda compositora. La conversion a color se hace aqui con la LUT,
 * lo que tiene un efecto secundario util: cambiar de colormap repinta todo
 * el historial, no solo las filas nuevas. */
void gfx2_wf_blit(int16_t x, int16_t y, int16_t w, int16_t rows,
                  const uint8_t *idx, int16_t stride, int16_t head,
                  const uint16_t *cmap);

#endif /* GFX2_H_INCLUDED */
