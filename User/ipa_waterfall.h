#ifndef IPA_WATERFALL_H
#define IPA_WATERFALL_H

#include <stdint.h>

/*
 * IPA (Image Processing Accelerator)-backed replacement for the per-pixel
 * CPU colormap lookup in main.c's waterfall row builder (see the comment
 * right above the `for (x = 0; x < WATERFALL_WIDTH; x++)` loop that used to
 * call spectrum_colormap() once per column).
 *
 * WHY: that loop only does a float->index quantization (bin selection,
 * normalization) plus a 256-entry LUT lookup and a 16-bit store - the LUT
 * lookup/store half is exactly what the GD32F450's IPA does natively in
 * "foreground memory to destination memory with pixel format convert" mode
 * (PFCM=01, see Table 11-1 in the GD32F4xx user manual) when the foreground
 * pixel format is L8 (8-bit LUT index) and the destination format is
 * RGB565. The float/index half (which the IPA can't do - it's not an
 * arithmetic engine) stays on the CPU; only the color lookup moves to
 * hardware.
 *
 * DIVISION OF LABOR:
 *   - Caller (main.c) still computes one 8-bit palette index per waterfall
 *     column exactly as before, into a plain byte array.
 *   - ipa_waterfall_colormap_line() hands that byte array to the IPA as an
 *     L8 foreground image, and gets back a row of RGB565 pixels - the same
 *     format waterfall_push_line() already expects, so nothing downstream
 *     of this call changes.
 *   - The 256-entry palette itself (spectrum.c's s_lut, already RGB565)
 *     needs a matching ARGB8888 copy for the IPA's LUT, since the IPA's LUT
 *     path only accepts ARGB8888 or RGB888 entries (IPA_LUT_PF_ARGB8888/
 *     IPA_LUT_PF_RGB888 - RGB565 isn't a supported LUT storage format).
 *     spectrum.c owns and derives that copy; this module only consumes it.
 */

/* One-time bring-up: enables the IPA's peripheral clock and puts the block
 * in a known (just-reset) state. Call once at startup, after spectrum_init()
 * (so a palette is already loaded) and before the first waterfall frame. */
void ipa_waterfall_init(void);

/* Loads a new 256-entry ARGB8888 palette into the IPA's foreground LUT.
 * Called from spectrum.c's build_lut() on every palette change (initial
 * load included) - NOT once per frame: the IPA keeps the LUT resident
 * across transfers until this is called again, so reloading it every line
 * would just be wasted AHB bandwidth. argb8888_256 must stay valid for the
 * lifetime of the program (it's spectrum.c's static table, so this holds).
 */
void ipa_waterfall_load_palette(const uint32_t *argb8888_256);

/*
 * Converts one waterfall row of 8-bit palette indices (idx_line[0..width-1],
 * one per screen column, already computed by the caller) into RGB565 pixels
 * (out_line[0..width-1]) by running them through the LUT most recently
 * loaded via ipa_waterfall_load_palette().
 *
 * Blocking: polls the IPA's "full transfer finish" flag until done. For a
 * single row (WATERFALL_WIDTH pixels, 1 line, no blending) this is a small,
 * fixed number of AHB bus cycles - far shorter than servicing an interrupt
 * would take, and it keeps this a drop-in replacement for the old CPU loop
 * without restructuring main.c's frame loop around an IRQ/DMA handoff. If
 * WATERFALL_WIDTH ever grows enough to make that overhead worth avoiding,
 * switch to IPA_INT_FTF + ipa_interrupt_flag_get() instead.
 *
 * idx_line and out_line must not overlap, and out_line must have room for
 * `width` uint16_t.
 */
void ipa_waterfall_colormap_line(const uint8_t *idx_line, uint16_t *out_line, uint16_t width);

/*
 * *** TEMPORARY BENCH TEST *** - see ipa_waterfall.c's own comment above
 * this function for the full explanation. Confirms (via UART, no need to
 * eyeball the waterfall's colors) whether the IPA's foreground LUT really
 * holds all 256 entries with fg_lut_num=255 (the size-1 convention this
 * file assumes) or silently truncates to 255. Call once, early - right
 * after ipa_waterfall_init(), before spectrum_init(). Returns 1 for PASS,
 * 0 for FAIL - the caller is expected to keep re-printing this
 * periodically (e.g. alongside an existing steady-state debug line), NOT
 * just once at boot: this project's UART capture has repeatedly missed
 * everything printed in the first moment or two after reset (USB CDC-ACM
 * enumeration delay), so a one-shot print here is not reliably seen.
 * Remove both the call and the periodic re-print once you've seen and
 * acted on the result.
 */
uint8_t ipa_waterfall_lut_size_selftest(void);

#endif /* IPA_WATERFALL_H */
