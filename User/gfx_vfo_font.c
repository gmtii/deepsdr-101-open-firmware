#include <string.h>
#include "gfx_vfo_font.h"
#include "vfo_font.h"
#include "gfx.h"   /* for gfx_fill_rect() */

/* Looks up the glyph row for c in vfo_font16x22_chars by linear scan
 * (charset is tiny, ~20 entries - not worth a lookup table). Falls
 * back to the space glyph for anything not covered. */
static const uint16_t *vfo_font_glyph_for(char c)
{
    const char *pos = strchr(vfo_font16x22_chars, c);

    if (pos == NULL) {
        pos = strchr(vfo_font16x22_chars, ' ');
    }
    return vfo_font16x22_bits[pos - vfo_font16x22_chars];
}

void gfx_vfo_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
    const uint16_t *glyph;
    uint8_t row, col;

    if (scale == 0) {
        scale = 1;
    }
    glyph = vfo_font_glyph_for(c);

    for (row = 0; row < VFO_FONT_HEIGHT; row++) {
        uint16_t bits = glyph[row];
        for (col = 0; col < VFO_FONT_WIDTH; col++) {
            uint16_t color = (bits & (0x8000U >> col)) ? fg : bg;
            uint16_t px = (uint16_t)(x + (uint16_t)col * scale);
            uint16_t py = (uint16_t)(y + (uint16_t)row * scale);
            gfx_fill_rect(px, py, scale, scale, color);
        }
    }
    /* separator column between characters, same convention as gfx_char() */
    gfx_fill_rect((uint16_t)(x + (uint16_t)VFO_FONT_WIDTH * scale), y, scale,
                  (uint16_t)(VFO_FONT_HEIGHT * scale), bg);
}

void gfx_vfo_text(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint16_t cursor_x = x;
    uint8_t step = (uint8_t)((VFO_FONT_WIDTH + 1) * ((scale == 0) ? 1 : scale));

    while (*str != '\0') {
        gfx_vfo_char(cursor_x, y, *str, fg, bg, scale);
        cursor_x = (uint16_t)(cursor_x + step);
        str++;
    }
}

uint16_t gfx_vfo_text_width(const char *str, uint8_t scale)
{
    uint16_t len = 0;
    uint8_t step = (uint8_t)((VFO_FONT_WIDTH + 1) * ((scale == 0) ? 1 : scale));

    while (*str != '\0') {
        len = (uint16_t)(len + step);
        str++;
    }
    return len;
}

uint16_t gfx_vfo_text_height(uint8_t scale)
{
    return (uint16_t)(VFO_FONT_HEIGHT * ((scale == 0) ? 1 : scale));
}
