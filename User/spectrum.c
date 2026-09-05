#include "spectrum.h"
#include "gfx.h"

/* --- palette LUT ---------------------------------------------------- */

static uint16_t s_lut[256];
static uint8_t  s_lut_ready = 0;

/* The original 4-segment palette (black->blue->cyan->green/yellow->red),
 * evaluated once per LUT entry at init instead of once per pixel. */
static uint16_t palette_eval(float t)
{
    uint8_t r, g, b;

    if (t < 0.0f) { t = 0.0f; }
    if (t > 1.0f) { t = 1.0f; }

    if (t < 0.25f) {
        float u = t / 0.25f;
        r = 0U; g = 0U; b = (uint8_t)(u * 255.0f);
    } else if (t < 0.5f) {
        float u = (t - 0.25f) / 0.25f;
        r = 0U; g = (uint8_t)(u * 255.0f); b = 255U;
    } else if (t < 0.75f) {
        float u = (t - 0.5f) / 0.25f;
        r = (uint8_t)(u * 255.0f); g = 255U; b = (uint8_t)((1.0f - u) * 255.0f);
    } else {
        float u = (t - 0.75f) / 0.25f;
        r = 255U; g = (uint8_t)((1.0f - u) * 255.0f); b = 0U;
    }
    return gfx_rgb565(r, g, b);
}

void spectrum_init(void)
{
    uint16_t i;
    for (i = 0; i < 256U; i++) {
        s_lut[i] = palette_eval((float)i * (1.0f / 255.0f));
    }
    s_lut_ready = 1;
}

static spectrum_style_t s_style = SPECTRUM_STYLE_HEATMAP;

void spectrum_set_style(spectrum_style_t style)
{
    s_style = style;
}

spectrum_style_t spectrum_get_style(void)
{
    return s_style;
}

uint16_t spectrum_colormap(float db, float db_min, float db_max)
{
    float t;
    int32_t idx;

    if (db_max <= db_min) {
        return GFX_COLOR_BLACK;
    }
    if (!s_lut_ready) {
        spectrum_init(); /* safety net if someone draws before init */
    }
    t = (db - db_min) / (db_max - db_min);
    idx = (int32_t)(t * 255.0f);
    if (idx < 0)   { idx = 0; }
    if (idx > 255) { idx = 255; }
    return s_lut[idx];
}

/* --- spectrum rendering --------------------------------------------- */

#define SPEC_MAX_W 800
#define SPEC_MAX_H 280 /* raised from 160 (30/07/2026) for the redesigned UI's
                          taller spectrum panel - per-row arrays only, cheap */

/* Extra colors not taken from the palette. */
#define SPEC_COLOR_TRACE  GFX_COLOR_WHITE
#define SPEC_COLOR_PEAK   GFX_COLOR_ORANGE
#define SPEC_COLOR_GRID   0x2104 /* very dark gray, under everything */
#define SPEC_COLOR_CENTER 0x7800 /* dim red: VFO center line */

/* SPECTRUM_STYLE_LINE's palette - see spectrum_set_style()'s comment
 * in spectrum.h. Computed offline for a dark-navy-background,
 * medium-blue-fill, light-blue-trace look (RGB565): background
 * (6,10,26), fill (28,70,150), trace (110,180,255), gridline
 * (20,26,46) - a lighter navy than the background so the gridlines
 * stay faintly visible against it, same role SPEC_COLOR_GRID plays
 * against black for the HEATMAP style. */
#define SPEC_LINE_BG    0x0043
#define SPEC_LINE_FILL  0x1A32
#define SPEC_LINE_TRACE 0x6D9F
#define SPEC_LINE_GRID  0x10C5

/* Demodulated-bandwidth background tint - see spectrum_draw()'s
 * comment in spectrum.h. Deliberately a hue the rest of each style's
 * palette never uses (dim PURPLE) - the HEATMAP palette's own low end
 * ramps black->BLUE->cyan->green/yellow->red, and the LINE style is
 * all blues (background/fill/trace/grid), so a weak real signal could
 * never accidentally read as "this is the tint", or vice versa.
 * HEATMAP: RGB (40,0,60). LINE: RGB (34,0,52), slightly dimmer since
 * SPEC_LINE_BG is already non-black. */
#define SPEC_COLOR_BAND_TINT      0x2807
#define SPEC_LINE_BAND_TINT       0x2006

/* *** 01/09/2026: moved to TCM RAM *** - pure spectrum-rendering
 * working buffers, never DMA targets (only this file's own drawing
 * functions touch them) - see fft.c's fuller TCM comment for the
 * "why" (freed main-RAM headroom for the widened waterfall/spectrum
 * panel - SPEC_MAX_W was already 800, comfortably covering the new
 * width, so no size change was needed here, just relocating where
 * these already-existing buffers live). */
#define TCMRAM_BSS __attribute__((section(".tcmram")))

static float    s_col_ema[SPEC_MAX_W] TCMRAM_BSS;   /* smoothed dB per column      */
#if SPECTRUM_PEAK_HOLD
static float    s_col_peak[SPEC_MAX_W] TCMRAM_BSS;  /* peak-hold dB per column     */
#endif
static uint16_t s_bar_h[SPEC_MAX_W] TCMRAM_BSS;     /* bar height, px from bottom  */
static uint16_t s_bar_h_smooth[SPEC_MAX_W] TCMRAM_BSS; /* scratch buf for spatial smoothing */
static uint16_t s_peak_h[SPEC_MAX_W] TCMRAM_BSS;    /* peak marker height          */
static uint16_t s_bar_lo[SPEC_MAX_W] TCMRAM_BSS;    /* OUTLINE only: vertical trace bridge, low end  */
static uint16_t s_bar_hi[SPEC_MAX_W] TCMRAM_BSS;    /* OUTLINE only: vertical trace bridge, high end */
static uint16_t s_row_color[SPEC_MAX_H] TCMRAM_BSS; /* gradient fill color per row */
static uint8_t  s_row_grid[SPEC_MAX_H] TCMRAM_BSS;  /* 1 = gridline on this row    */
static uint16_t s_row_buf[SPEC_MAX_W] TCMRAM_BSS;   /* stripe assembled in RAM     */
static uint16_t s_prev_w = 0;            /* detect geometry change      */

/* See spectrum_set_line_smooth()'s comment in spectrum.h. Defaults to
 * 0 (disabled, original bin-sharp look) - main.c's repurposed NB tile
 * sets its own starting value on the first menu draw, this is just
 * the safe fallback if spectrum_draw() ever runs before that. */
static uint8_t s_line_smooth_passes = 3U;

void spectrum_set_line_smooth(uint8_t passes)
{
    if (passes > SPECTRUM_LINE_SMOOTH_MAX) {
        passes = SPECTRUM_LINE_SMOOTH_MAX;
    }
    s_line_smooth_passes = passes;
}

uint8_t spectrum_get_line_smooth(void)
{
    return s_line_smooth_passes;
}

void spectrum_draw(const float *db, uint32_t n_bins,
                    uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                    float db_min, float db_max,
                    int16_t center_mark_offset_px,
                    uint8_t band_active,
                    int16_t band_lo_offset_px, int16_t band_hi_offset_px)
{
    uint16_t col, row;
    float scale_t;
    uint16_t center_mark_col;
    uint16_t band_col_lo = 0, band_col_hi = 0; /* only meaningful when band_active */

    if (db_max <= db_min || w == 0U || h == 0U ||
        w > SPEC_MAX_W || h > SPEC_MAX_H || n_bins == 0U) {
        return;
    }
    if (!s_lut_ready) {
        spectrum_init();
    }

    /* Clamp the (possibly offset) center mark into [0, w-1] - a
     * caller passing an offset close to the panel's edge shouldn't be
     * able to walk the column index out of s_row_buf's bounds. */
    {
        int32_t c = (int32_t)(w / 2U) + (int32_t)center_mark_offset_px;
        if (c < 0) { c = 0; }
        if (c > (int32_t)(w - 1U)) { c = (int32_t)(w - 1U); }
        center_mark_col = (uint16_t)c;
    }

    /* Same clamp, applied to both band edges independently, then
     * sorted - the caller (main.c) builds these from
     * center_mark_offset_px +/- a bandwidth in pixels, so which one
     * ends up smaller depends on the demod mode (AM straddles both
     * ways, USB/LSB only extend one way - see the call site), not
     * worth requiring the caller to pre-sort them here too. */
    if (band_active) {
        int32_t lo = (int32_t)(w / 2U) + (int32_t)band_lo_offset_px;
        int32_t hi = (int32_t)(w / 2U) + (int32_t)band_hi_offset_px;
        int32_t tmp;

        if (lo > hi) { tmp = lo; lo = hi; hi = tmp; }
        if (lo < 0) { lo = 0; }
        if (hi > (int32_t)(w - 1U)) { hi = (int32_t)(w - 1U); }
        if (lo > (int32_t)(w - 1U)) { lo = (int32_t)(w - 1U); }
        if (hi < 0) { hi = 0; }
        band_col_lo = (uint16_t)lo;
        band_col_hi = (uint16_t)hi;
    }

    /* Reset smoothing state when the geometry changes (first call
     * included): stale EMA values from a different column mapping
     * would be meaningless. */
    if (w != s_prev_w) {
        for (col = 0; col < w; col++) {
            s_col_ema[col] = db_min;
#if SPECTRUM_PEAK_HOLD
            s_col_peak[col] = db_min;
#endif
        }
        s_prev_w = w;
    }

    scale_t = 1.0f / (db_max - db_min);

    /*
     * Pass 1 (per column, float - only ~w iterations): fold bins into
     * the column by AVERAGING every bin whose center lands in it
     * (when n_bins >= w), or nearest bin (when zoomed in, n_bins < w).
     * Then asymmetric EMA and peak-hold, and quantize to pixels.
     */
    for (col = 0; col < w; col++) {
        uint32_t b0 = ((uint32_t)col * n_bins) / w;
        uint32_t b1 = ((uint32_t)(col + 1U) * n_bins) / w;
        float v;
        float t;
        uint32_t bi;

        if (b1 <= b0) {
            b1 = b0 + 1U; /* zoomed in: at least one bin */
        }
        v = 0.0f;
        for (bi = b0; bi < b1; bi++) {
            v += db[bi];
        }
        v = v / (float)(b1 - b0);

        /* Fast attack, slow decay. */
        if (v > s_col_ema[col]) {
            s_col_ema[col] += SPECTRUM_EMA_ATTACK * (v - s_col_ema[col]);
        } else {
            s_col_ema[col] += SPECTRUM_EMA_DECAY * (v - s_col_ema[col]);
        }

#if SPECTRUM_PEAK_HOLD
        if (s_col_ema[col] > s_col_peak[col]) {
            s_col_peak[col] = s_col_ema[col];
        } else {
            s_col_peak[col] -= SPECTRUM_PEAK_DECAY;
            if (s_col_peak[col] < db_min) {
                s_col_peak[col] = db_min;
            }
        }
        t = (s_col_peak[col] - db_min) * scale_t;
        if (t < 0.0f) { t = 0.0f; }
        if (t > 1.0f) { t = 1.0f; }
        s_peak_h[col] = (uint16_t)(t * (float)h);
#else
        s_peak_h[col] = 0;
#endif

        t = (s_col_ema[col] - db_min) * scale_t;
        if (t < 0.0f) { t = 0.0f; }
        if (t > 1.0f) { t = 1.0f; }
        s_bar_h[col] = (uint16_t)(t * (float)h);
    }

    /*
     * Pass 1.5 (per column, integer - only ~w iterations per pass):
     * light spatial smoothing across neighboring columns so the
     * trace/fill edge isn't jagged bin-to-bin. 3-tap (1,2,1)/4
     * average, edges left untouched (clamped), run
     * s_line_smooth_passes times back-to-back (see
     * spectrum_set_line_smooth()'s comment in spectrum.h - repeating
     * the same narrow kernel approximates a wider one without the
     * cost/complexity of a real 5-/7-tap filter). Applied
     * post-quantization, on top of s_bar_h only, so it never touches
     * the persistent s_col_ema / s_col_peak temporal state - it's a
     * purely visual pass, redone from scratch every frame.
     */
    if (w >= 3U) {
        uint8_t pass;
        for (pass = 0; pass < s_line_smooth_passes; pass++) {
            s_bar_h_smooth[0] = s_bar_h[0];
            for (col = 1; col < w - 1U; col++) {
                s_bar_h_smooth[col] = (uint16_t)(((uint32_t)s_bar_h[col - 1U] +
                                                   2U * (uint32_t)s_bar_h[col] +
                                                   (uint32_t)s_bar_h[col + 1U]) / 4U);
            }
            s_bar_h_smooth[w - 1U] = s_bar_h[w - 1U];
            for (col = 0; col < w; col++) {
                s_bar_h[col] = s_bar_h_smooth[col];
            }
        }
    }

    /*
     * Pass 1.6 (per column, integer - only ~w iterations, OUTLINE
     * only): the single bright trace pixel per column only lands on
     * ONE row (bh == level_from_bottom). Where a signal edge is
     * steep, adjacent columns' bar heights can differ by more than
     * 1px, so their trace pixels don't share a row and the contour
     * reads as scattered dots instead of a line. HEATMAP/LINE don't
     * have this problem - their fill occupies the vertical gap
     * between differing column heights, so the top edge still reads
     * as continuous even though the bright trace pixel itself is
     * exactly as sparse. OUTLINE has no fill to hide the gap, so it
     * needs an explicit vertical bridge: for each column, connect its
     * height to its LEFT neighbor's with trace color, drawn in the
     * current column's pixel stripe (a stepped/staircase join, same
     * idea as a polyline plot). col 0 has no left neighbor, so its
     * bridge collapses to the single dot as before.
     */
    if (s_style == SPECTRUM_STYLE_OUTLINE) {
        for (col = 0; col < w; col++) {
            uint16_t left = (col == 0U) ? s_bar_h[col] : s_bar_h[col - 1U];
            uint16_t cur  = s_bar_h[col];
            if (left < cur) {
                s_bar_lo[col] = left;
                s_bar_hi[col] = cur;
            } else {
                s_bar_lo[col] = cur;
                s_bar_hi[col] = left;
            }
        }
    }

    /*
     * Pass 2 (per row, float - only ~h iterations): the vertical
     * gradient color of each row (HEATMAP), or a single flat fill
     * color for every row (LINE - see spectrum_set_style()'s comment
     * in spectrum.h), and whether a dB gridline lands on it. Row
     * `row` on screen corresponds to level (h - row) px from the
     * bottom.
     */
    for (row = 0; row < h; row++) {
        if (s_style == SPECTRUM_STYLE_HEATMAP) {
            uint32_t level = (uint32_t)(h - row);
            int32_t idx = (int32_t)((level * 255U) / h);
            s_row_color[row] = s_lut[idx];
        } else {
            /* LINE reads this as its flat fill color. OUTLINE never
             * reads it (fill_enabled below is 0 for that style) - left
             * assigned anyway so the two styles share this branch. */
            s_row_color[row] = SPEC_LINE_FILL;
        }
        s_row_grid[row] = 0;
    }
#if defined(SPECTRUM_GRID_DB)
    if (SPECTRUM_GRID_DB > 0.0f) {
        float g;
        for (g = db_min; g <= db_max; g += SPECTRUM_GRID_DB) {
            float t = (g - db_min) * scale_t;
            uint32_t level = (uint32_t)(t * (float)h);
            if (level >= 1U && level <= h) {
                s_row_grid[h - level] = 1;
            }
        }
    }
#endif

    /*
     * Pass 3 (per pixel, INTEGER ONLY): assemble each row stripe in
     * RAM and push it with one gfx_blit per row. Priority per pixel:
     * trace > peak marker > bar fill > gridline > background.
     * bg_color/grid_color/trace_color picked ONCE here (not per
     * pixel) based on the current style - see spectrum_set_style()'s
     * comment in spectrum.h. Peak-hold and center-mark colors stay
     * the same in both styles (informational overlays, not part of
     * the base heat-vs-line aesthetic).
     */
    {
        /* HEATMAP and LINE fill the bar interior; OUTLINE shares
         * LINE's dark-navy palette but leaves fill_enabled clear -
         * see the loop below, where bh > level_from_bottom then just
         * falls through to peak/center/band/background instead of
         * being painted solid. That's the whole "no fill, only the
         * contour" difference; everything else about OUTLINE (trace
         * color, background, gridlines, band tint) is identical to
         * LINE. */
        uint8_t  fill_enabled = (s_style == SPECTRUM_STYLE_OUTLINE) ? 0U : 1U;
        uint16_t bg_color    = (s_style == SPECTRUM_STYLE_HEATMAP) ? GFX_COLOR_BLACK : SPEC_LINE_BG;
        uint16_t grid_color  = (s_style == SPECTRUM_STYLE_HEATMAP) ? SPEC_COLOR_GRID : SPEC_LINE_GRID;
        uint16_t trace_color = (s_style == SPECTRUM_STYLE_HEATMAP) ? SPEC_COLOR_TRACE : SPEC_LINE_TRACE;
        uint16_t band_color  = (s_style == SPECTRUM_STYLE_HEATMAP) ? SPEC_COLOR_BAND_TINT : SPEC_LINE_BAND_TINT;

        for (row = 0; row < h; row++) {
            uint16_t level_from_bottom = (uint16_t)(h - row);
            uint16_t fill  = s_row_color[row];
            uint16_t empty = s_row_grid[row] ? grid_color : bg_color;

            for (col = 0; col < w; col++) {
                uint16_t bh = s_bar_h[col];
                uint16_t px;
                uint8_t  is_trace;

                /* OUTLINE: lit if this row falls anywhere in the
                 * bridge to the left neighbor (see Pass 1.6) - not
                 * just the exact bh row - so the contour has no gaps
                 * on steep edges. HEATMAP/LINE: unchanged, exact-row
                 * dot (the fill below it is what makes it read as
                 * continuous). */
                if (fill_enabled) {
                    is_trace = (bh == level_from_bottom) ? 1U : 0U;
                } else {
                    is_trace = (level_from_bottom >= s_bar_lo[col] &&
                                level_from_bottom <= s_bar_hi[col]) ? 1U : 0U;
                }

                if (is_trace) {
                    px = trace_color;                /* bar top: bright trace */
                } else if (fill_enabled && bh > level_from_bottom) {
                    px = fill;                        /* inside the bar (HEATMAP/LINE only) */
#if SPECTRUM_PEAK_HOLD
                } else if (s_peak_h[col] == level_from_bottom) {
                    px = SPEC_COLOR_PEAK;             /* floating peak dot */
#endif
#if SPECTRUM_CENTER_MARK
                } else if (col == center_mark_col) {
                    px = SPEC_COLOR_CENTER;           /* demod point marker, under signals */
#endif
                } else if (band_active && col >= band_col_lo && col <= band_col_hi) {
                    px = band_color;                  /* demodulated-bandwidth tint, under everything else */
                } else {
                    px = empty;
                }
                s_row_buf[col] = px;
            }
            gfx_blit(x, (uint16_t)(y + row), w, 1, s_row_buf);
        }
    }
}