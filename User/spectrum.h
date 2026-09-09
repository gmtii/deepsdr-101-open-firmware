#ifndef SPECTRUM_H
#define SPECTRUM_H

#include <stdint.h>

/*
 * Spectrum trace rendering + shared dB->RGB565 colormap (the same
 * palette colors both the live spectrum and the waterfall rows, so
 * the scale reads consistently between them).
 *
 * PERFORMANCE MODEL (this is the hot path of the whole UI): the
 * original implementation called a float colormap function - with
 * divisions and branches - once per LIT PIXEL, up to ~80k calls per
 * frame, which dominated the frame time by an order of magnitude
 * over the actual EXMC pixel writes. This version moves ALL float
 * math out of the per-pixel loop:
 *
 *   - The palette is a 256-entry RGB565 lookup table built once by
 *     spectrum_init().
 *   - Per frame, dB values are folded to one value per screen COLUMN
 *     (averaging every FFT bin that lands in the column - cheap
 *     anti-aliasing, kills the nearest-neighbor shimmer), smoothed,
 *     and quantized to integer bar heights.
 *   - Per frame, one color per ROW is precomputed for the vertical
 *     gradient fill.
 *   - The per-pixel inner loop is then just an integer compare and a
 *     16-bit store into a row stripe that gfx_blit()s once per row.
 *
 * VISUALS:
 *   - Vertical gradient fill (each bar colored by height through the
 *     palette, classic "heat" look).
 *   - Asymmetric exponential smoothing per column: fast attack, slow
 *     decay - signals pop instantly, the floor calms down.
 *   - Optional decaying peak-hold markers.
 *   - Horizontal gridline every SPECTRUM_GRID_DB dB, drawn dim under
 *     the trace.
 *   - 1px bright trace on top of each bar.
 */

/* Grid spacing in dB (set 0 to disable gridlines). */
#define SPECTRUM_GRID_DB 20.0f

/* Smoothing factors (0..1): applied per displayed frame, not per FFT
 * block. Attack is how fast the trace RISES toward a stronger value,
 * decay how fast it FALLS. */
#define SPECTRUM_EMA_ATTACK 0.60f
#define SPECTRUM_EMA_DECAY  0.25f

/* Peak-hold: 0 disables. Decay is in dB per displayed frame. */
#define SPECTRUM_PEAK_HOLD  0
#define SPECTRUM_PEAK_DECAY 0.4f

/* Vertical marker at the horizontal center (the VFO, when fed the
 * fftshifted I/Q spectrum). Drawn UNDER signals, over the grid, so it
 * never obscures a real trace. 0 disables.
 *
 * When low-IF tuning is active (see demod_am.h's LOW-IF TUNING note),
 * the demodulated signal doesn't sit on the true LO/center bin
 * anymore - it's DEMOD_IF_OFFSET_HZ away from it. spectrum_draw()'s
 * center_mark_offset_px parameter shifts this line to track the
 * actual demod point instead of always drawing it dead-center. */
#define SPECTRUM_CENTER_MARK 1

/* Build the palette LUT. Call once at startup, before the first
 * spectrum_draw()/spectrum_colormap(). */
void spectrum_init(void);

/*
 * Three trace styles - HEATMAP and LINE added 31/07/2026, OUTLINE
 * added 07/08/2026, all per the project owner. HEATMAP is the
 * original palette-gradient bars; LINE is a plain continuous-line
 * look (flat fill + bright outline on a dark navy background, like a
 * classic panadapter); OUTLINE is LINE with the fill removed - just
 * the 1px contour on the dark navy background, so gridlines/band
 * tint/center mark show straight through underneath the trace
 * instead of being covered by a filled bar. Toggled live via
 * spectrum_set_style() - the NEXT spectrum_draw() call picks it up,
 * no re-init needed. Only affects the SPECTRUM panel's own
 * fill/background/trace colors - the waterfall keeps using
 * spectrum_colormap() (the palette LUT) regardless of this setting,
 * since a flat-color waterfall would be useless (it's the palette
 * gradient that makes a waterfall readable at a glance).
 */
typedef enum {
    SPECTRUM_STYLE_HEATMAP = 0, /* existing palette-gradient bars (default, unchanged) */
    SPECTRUM_STYLE_LINE    = 1, /* flat fill + bright outline on a dark navy background */
    SPECTRUM_STYLE_OUTLINE = 2  /* same navy background as LINE, but no fill - contour only */
} spectrum_style_t;

void spectrum_set_style(spectrum_style_t style);
spectrum_style_t spectrum_get_style(void);

/*
 * HEATMAP-only trace treatment - added 08/09/2026, per the project
 * owner, correcting this same feature's first pass: connecting the
 * bar-top trace across steep edges (see spectrum_draw()'s Pass 1.6)
 * initially kept using the fixed bright SPEC_COLOR_TRACE (white) for
 * the connector, which reads as a stark white contour hugging the
 * whole spectrum's silhouette - looks wrong, especially against
 * palettes where white doesn't already appear near the top of the
 * gradient. Default (0/OFF) now colors the trace - AND the connecting
 * bridge - with the SAME per-row palette color the bar fill already
 * uses at that height, so the top edge just reads as a clean,
 * connected, correctly-colored bar chart with no separate highlight
 * element at all. Set to 1/ON for the old explicit bright-white
 * contour instead, for anyone who prefers that visible highlight.
 * Only affects HEATMAP - LINE and OUTLINE keep their own fixed
 * SPEC_LINE_TRACE color unconditionally, since for THOSE two styles
 * the trace/contour line is the entire visual (OUTLINE draws nothing
 * else at all), not an optional highlight on top of a fill.
 */
void spectrum_set_heatmap_trace_white(uint8_t white);
uint8_t spectrum_get_heatmap_trace_white(void);

/*
 * Color palette for the shared dB->RGB565 colormap (spectrum_colormap()
 * below) - added 08/09/2026, per the project owner ("un tile para
 * cambiar las paletas... irnos a otras combinaciones de colores").
 * Unlike spectrum_style_t (which only affects the SPECTRUM panel's
 * own fill/outline look), this affects BOTH the spectrum's HEATMAP
 * style AND the waterfall, since both read colors through the same
 * LUT (see this header's own PERFORMANCE MODEL note above) - a
 * palette change is visible in both places at once, and in neither
 * when spectrum_style_t is LINE/OUTLINE (no gradient fill to color)
 * though the waterfall below it still shows the new palette either
 * way.
 *
 * CLASSIC is SDR++'s own actual "Classic" gradient now (see below) -
 * kept first/default so the DEFAULT visual barely changes for anyone
 * not using the new tile (SDR++'s Classic and this project's old
 * invented default are both dark-to-hot gradients).
 */
typedef enum {
    SPECTRUM_PALETTE_CLASSIC       = 0,  /* dark navy -> blues -> white -> yellow -> orange -> red -> dark red (SDR++'s own "Classic", exact) */
    SPECTRUM_PALETTE_FIRE          = 1,  /* black -> red -> orange -> yellow -> white (this project's own addition, not from SDR++) */
    SPECTRUM_PALETTE_VIRIDIS       = 2,  /* dark purple -> blue -> teal -> green -> yellow (exact, matplotlib/B.I.D.S.) */
    SPECTRUM_PALETTE_GRAYSCALE     = 3,  /* black -> white, no hue at all (exact, matches SDR++'s "Grey Scale") */
    SPECTRUM_PALETTE_TURBO         = 4,  /* dark blue -> blue -> green -> yellow -> dark red (exact, Google AI) */
    SPECTRUM_PALETTE_INFERNO       = 5,  /* near-black -> purple -> red -> orange -> pale yellow (exact, B.I.D.S.) */
    SPECTRUM_PALETTE_MAGMA         = 6,  /* near-black -> purple -> pink/red -> orange -> pale pink-white (exact, B.I.D.S.) */
    SPECTRUM_PALETTE_PLASMA        = 7,  /* deep blue-purple -> magenta -> orange -> pale yellow (exact, B.I.D.S.) */
    SPECTRUM_PALETTE_GQRX          = 8,  /* black -> blue -> cyan/green -> yellow -> red -> white (exact, csete) */
    SPECTRUM_PALETTE_ELECTRIC      = 9,  /* black -> blue -> cyan -> white (exact, Ryzerth) */
    SPECTRUM_PALETTE_CLASSIC_GREEN = 10, /* black -> blues -> pale green -> orange -> red -> dark red (exact, Paul PD0SWL) */
    SPECTRUM_PALETTE_SMOKE         = 11, /* white -> grays -> black, i.e. an INVERTED grayscale (exact, Yaroslav Andrianov) */
    SPECTRUM_PALETTE_TEMPER_COLORS = 12, /* black -> indigo -> violet -> slate blue -> dusty rose -> plum (exact, Yaroslav Andrianov) */
    SPECTRUM_PALETTE_VIVID         = 13, /* black -> purple -> viridis-like band -> yellow -> orange -> red (exact, Yaroslav Andrianov) */
    SPECTRUM_PALETTE_WEBSDR        = 14  /* black -> navy -> magenta -> pale yellow -> white (exact, Ryzerth) */
} spectrum_palette_t;

/*
 * *** 08/09/2026 UPDATE *** - CLASSIC/VIRIDIS/TURBO/INFERNO/MAGMA/
 * PLASMA/GQRX/ELECTRIC/CLASSIC_GREEN were all approximated from
 * memory or general knowledge in this palette feature's first pass;
 * the project owner then uploaded the ACTUAL JSON files from SDR++'s
 * own root/res/colormaps/ (AlexandreRouma/SDRPlusPlus), so every one
 * of those nine now uses the REAL stop colors from those files
 * (GQRX/INFERNO/MAGMA/PLASMA/TURBO/VIRIDIS happen to already ship as
 * full 256-entry maps, so those five plus GQRX are copied in
 * directly, one LUT entry per source entry, no interpolation at all -
 * see spectrum.c's k_lut_* tables). SMOKE/TEMPER_COLORS/VIVID/WEBSDR
 * are new additions from that same upload (also exact). FIRE remains
 * this project's own invention - it isn't in SDR++'s set.
 */
void spectrum_set_palette(spectrum_palette_t palette);
spectrum_palette_t spectrum_get_palette(void);

/*
 * Spatial line smoothing - added 01/08/2026, exposed live through the
 * repurposed NB tile/button/badge in main.c (see s_spec_smooth_passes'
 * comment there; the old NB noise-blanker flag never drove any real
 * DSP, so its UI slot was free). Runs a 3-tap (1,2,1)/4 box filter
 * across neighboring columns' bar heights, `passes` times, AFTER
 * quantization to pixels - it only softens the on-screen trace/fill
 * edge, it never touches the per-column EMA/peak-hold state that
 * spectrum_draw() keeps between frames (see its Pass 1 vs Pass 1.5
 * comments), so it can't accumulate or drift frame to frame the way
 * smoothing the EMA itself would.
 *
 * 0 disables it (bin-sharp edge, the original look). Each additional
 * pass costs one more ~w-iteration integer loop per frame (negligible
 * next to the w*h pixel loop) and rounds the trace a bit further;
 * 2-3 passes is the range the project owner found gives a visibly
 * smoother line without flattening real peaks. Clamped internally to
 * [0, SPECTRUM_LINE_SMOOTH_MAX].
 */
#define SPECTRUM_LINE_SMOOTH_MAX 5
void spectrum_set_line_smooth(uint8_t passes);
uint8_t spectrum_get_line_smooth(void);

/* dB -> RGB565 through the shared palette (LUT-backed; the float here
 * is fine for per-COLUMN use, e.g. coloring a waterfall line - just
 * never call it per pixel). */
uint16_t spectrum_colormap(float db, float db_min, float db_max);

/*
 * Draws `n_bins` dB values into the rectangle (x,y,w,h). Columns
 * average their bins; smoothing/peak state is kept internally per
 * column (it self-resets if w changes). db_min/max fix the vertical
 * scale and the palette range.
 *
 * center_mark_offset_px shifts the SPECTRUM_CENTER_MARK line from the
 * true horizontal center by this many pixels (positive = right, i.e.
 * higher frequency) - pass 0 to mark the true center (the LO) as
 * before. Clamped internally to stay inside the plot area.
 *
 * band_active/band_lo_offset_px/band_hi_offset_px - added 03/08/2026,
 * per the project owner: an optional tinted background region behind
 * the trace showing WHICH slice of the panadapter is actually being
 * demodulated right now (AM/USB/LSB's audio bandwidth, mapped back
 * onto the frequency axis) - so it's visually obvious what you're
 * listening to, not just where the LO/demod point sits. band_active=0
 * skips it entirely (draw exactly as before - used for NFM/WFM, which
 * don't have a caller-selectable audio bandwidth the way AM/SSB do).
 * When active, band_lo_offset_px/band_hi_offset_px are pixel offsets
 * from the panel's horizontal center, SAME origin and sign convention
 * as center_mark_offset_px (so the caller can build them directly on
 * top of that same value - see main.c's call site) - lo/hi order
 * doesn't matter, both get clamped and sorted internally. The tint
 * only ever replaces BACKGROUND pixels (gridline or plain fill,
 * whichever would've shown) - it never covers the trace, the bar
 * fill, the peak marker, or the center mark, all of which keep
 * drawing on top of it exactly as they would without it.
 */
void spectrum_draw(const float *db, uint32_t n_bins,
                    uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                    float db_min, float db_max,
                    int16_t center_mark_offset_px,
                    uint8_t band_active,
                    int16_t band_lo_offset_px, int16_t band_hi_offset_px);

#endif /* SPECTRUM_H */
