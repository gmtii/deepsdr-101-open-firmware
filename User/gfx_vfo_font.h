#ifndef GFX_VFO_FONT_H
#define GFX_VFO_FONT_H

#include <stdint.h>

/*
 * Standalone drawing code for the 16x22 VFO font (vfo_font16x22.h).
 *
 * This is intentionally separate from gfx_char()/gfx_text() in gfx.c:
 * the VFO font uses a different data layout (row-major, MSB-aligned)
 * than gfx_font5x7 (column-major, LSB-first), and it only covers a
 * small charset (digits + VFO symbols/units) that would misbehave if
 * routed through the general-purpose text function. Kept as its own
 * module so it can be used today without a refactor of gfx.c's font
 * handling; the two draw functions merge naturally once/if gfx.c grows
 * a real multi-font gfx_font_t abstraction.
 *
 * Same drawing model as gfx.c: writes straight to the GRAM via
 * gfx_fill_rect(), no framebuffer, one scale-sized block per font
 * pixel. Same scale semantics as gfx_char(): scale=1 -> 16x22 real
 * pixels, scale=2 -> 32x44, etc.
 */

/* Unsupported characters (outside vfo_font16x22_chars) draw as blank
 * (space). See vfo_font_glyph_for() in the .c file. */
void gfx_vfo_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);
void gfx_vfo_text(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t scale);
uint16_t gfx_vfo_text_width(const char *str, uint8_t scale);
uint16_t gfx_vfo_text_height(uint8_t scale);

#endif /* GFX_VFO_FONT_H */
