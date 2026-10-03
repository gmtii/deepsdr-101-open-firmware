#ifndef GFX2_FONT_H
#define GFX2_FONT_H

#include <stdint.h>

/*
 * Formato de fuente del nuevo nucleo grafico.
 *
 * Frente a la fuente 5x7 de 1 bit del firmware actual, esta es:
 *   - proporcional (cada glyph tiene su ancho y su avance reales),
 *   - antialiasada a 4 bpp (16 niveles de cobertura),
 *   - con bearing vertical por glyph, asi que hay descendentes de verdad.
 *
 * El bitmap va empaquetado por filas, dos pixeles por byte, nibble alto =
 * pixel izquierdo. Una fila de ancho impar se rellena con medio byte.
 *
 * Nota sobre la cobertura de 4 bpp: 16 niveles son de sobra para texto de
 * UI. El ojo distingue escalones de alpha muy por encima de 1/16 solo en
 * degradados grandes y planos, no en bordes de letra de 1-2 px. Bajar de
 * 8 a 4 bpp ahorra la mitad de flash por glyph, que es lo que hace viable
 * tener seis fuentes en vez de tres.
 */

typedef struct {
    uint32_t off;   /* offset dentro del bitmap de la fuente */
    uint8_t  w;     /* ancho de la tinta, en pixeles */
    uint8_t  h;     /* alto de la tinta, en pixeles */
    int8_t   bx;    /* bearing X: desplazamiento desde el cursor */
    int8_t   by;    /* bearing Y: desde la LINEA SUPERIOR hasta el top de la tinta */
    uint8_t  adv;   /* avance del cursor tras dibujar */
} gfx2_glyph_t;

typedef struct {
    uint16_t lo;    /* primer codepoint del rango */
    uint16_t hi;    /* ultimo codepoint del rango */
    uint16_t first; /* indice en la tabla de glyphs que corresponde a lo */
} gfx2_range_t;

typedef struct {
    const uint8_t      *bitmap;
    const gfx2_glyph_t *glyphs;
    const gfx2_range_t *ranges;
    uint8_t             n_ranges;
    uint8_t             ascent;
    uint8_t             descent;
    uint8_t             line_h;
} gfx2_font_t;

#endif /* GFX2_FONT_H */
