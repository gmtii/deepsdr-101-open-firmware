#include "settings.h"
#include "touch.h"
#include "spi_flash.h"
#include "debug_uart.h"
#include "ms5351.h"
#include "backlight.h"  /* backlight_pct - read/applied directly, see settings.h's header comment */
#include "spectrum.h"   /* spectrum_style - read/applied directly, see settings.h's header comment */

extern volatile uint32_t g_msticks; /* same free-running ms counter touch.c/touch_calib.c/spi_flash.c already use */

#define CONFIG_FILE_NAME8 "CONFIG  "
#define CONFIG_FILE_EXT3  "CSV"

/* Wait this long after the last settings_mark_dirty() before actually
 * writing - see settings.h's comment. 3s comfortably outlasts a single
 * encoder detent's worth of tuning without feeling laggy if the user
 * powers off shortly after their last change (worst case: that last
 * change is lost, same trade-off every debounced autosave makes). */
#define SETTINGS_SAVE_DEBOUNCE_MS 3000UL

static uint8_t s_dirty = 0U;
static uint32_t s_dirty_since_ms = 0U;

/* Buffer for the CSV being saved via the async path - must stay
 * valid/unchanged for the WHOLE async operation (many poll() calls
 * across many main loop iterations), since
 * spi_flash_async_save_start() only references it, doesn't copy it -
 * see spi_flash.h's comment. A stack-local buffer would be gone the
 * instant settings_poll() returns, so this has to be static. */
static uint8_t s_async_csv_buf[512]; /* 01/09/2026: bumped from 256 to 384 - see settings_load()'s own buffer comment for the worst-case math that made 256 too tight once nonwfm_use_48k was added. 07/09/2026: bumped again to 512 for the same reason, now that pga_gain_db_x2/spectrum_smooth_pct/speaker_enabled/backlight_pct/spectrum_style add roughly another 100 bytes worst-case. */
static uint8_t s_async_save_in_progress = 0U;

/* --- manual CSV building/parsing - no sprintf/strtol, same policy as
 * the rest of this project (see e.g. main.c's own itoa comment). --- */

static uint32_t append_str(uint8_t *buf, uint32_t pos, uint32_t buf_size, const char *s)
{
    while ((*s != '\0') && (pos < buf_size)) {
        buf[pos++] = (uint8_t)(*s++);
    }
    return pos;
}

static uint32_t append_u32(uint8_t *buf, uint32_t pos, uint32_t buf_size, uint32_t v)
{
    char tmp[10];
    uint8_t n = 0U;

    if (v == 0U) {
        tmp[n++] = '0';
    } else {
        while ((v > 0U) && (n < 10U)) {
            tmp[n++] = (char)('0' + (v % 10U));
            v /= 10U;
        }
    }
    while ((n > 0U) && (pos < buf_size)) {
        buf[pos++] = (uint8_t)tmp[--n];
    }
    return pos;
}

static uint32_t append_i32(uint8_t *buf, uint32_t pos, uint32_t buf_size, int32_t v)
{
    if (v < 0) {
        if (pos < buf_size) {
            buf[pos++] = '-';
        }
        v = -v;
    }
    return append_u32(buf, pos, buf_size, (uint32_t)v);
}

static const char *mode_to_str(demod_mode_t mode)
{
    switch (mode) {
    case DEMOD_MODE_AM:  return "AM";
    case DEMOD_MODE_USB: return "USB";
    case DEMOD_MODE_LSB: return "LSB";
    case DEMOD_MODE_NFM: return "NFM";
    case DEMOD_MODE_WFM: return "WFM";
    case DEMOD_MODE_SAM: return "SAM"; /* 26/08/2026 fix - was missing, so saving while in SAM silently persisted "AM" instead */
    default:             return "AM"; /* unreachable in practice, but never emit garbage into the file */
    }
}

static const char *audio_bw_to_str(audio_bw_t bw)
{
    switch (bw) {
    case AUDIO_BW_4K0: return "4K0";
    case AUDIO_BW_2K3: return "2K3";
    case AUDIO_BW_1K8: return "1K8";
    default:           return "4K0"; /* unreachable in practice, same policy as mode_to_str() */
    }
}

static const char *spectrum_style_to_str(spectrum_style_t style)
{
    switch (style) {
    case SPECTRUM_STYLE_LINE:    return "LINE";
    case SPECTRUM_STYLE_OUTLINE: return "OUTLINE";
    case SPECTRUM_STYLE_HEATMAP:
    default:                     return "HEATMAP"; /* unreachable in practice, same policy as mode_to_str() */
    }
}

static const char *spectrum_palette_to_str(spectrum_palette_t palette)
{
    switch (palette) {
    case SPECTRUM_PALETTE_FIRE:          return "FIRE";
    case SPECTRUM_PALETTE_VIRIDIS:       return "VIRIDIS";
    case SPECTRUM_PALETTE_GRAYSCALE:     return "GRAYSCALE";
    case SPECTRUM_PALETTE_TURBO:         return "TURBO";
    case SPECTRUM_PALETTE_INFERNO:       return "INFERNO";
    case SPECTRUM_PALETTE_MAGMA:         return "MAGMA";
    case SPECTRUM_PALETTE_PLASMA:        return "PLASMA";
    case SPECTRUM_PALETTE_GQRX:          return "GQRX";
    case SPECTRUM_PALETTE_ELECTRIC:      return "ELECTRIC";
    case SPECTRUM_PALETTE_CLASSIC_GREEN: return "CLASSIC_GREEN";
    case SPECTRUM_PALETTE_SMOKE:         return "SMOKE";
    case SPECTRUM_PALETTE_TEMPER_COLORS: return "TEMPER_COLORS";
    case SPECTRUM_PALETTE_VIVID:         return "VIVID";
    case SPECTRUM_PALETTE_WEBSDR:        return "WEBSDR";
    case SPECTRUM_PALETTE_CLASSIC:
    default:                             return "CLASSIC"; /* unreachable in practice, same policy as mode_to_str() */
    }
}

/* Returns the byte length written (does NOT null-terminate - this is
 * flash file content, not a C string). */
static uint32_t build_csv(uint8_t *buf, uint32_t buf_size,
                           const touch_calibration_t *cal, uint32_t vfo_hz, demod_mode_t mode,
                           uint32_t tune_step_hz, audio_bw_t audio_bw, int16_t volume_db_x2,
                           uint8_t nonwfm_use_48k, int16_t pga_gain_db_x2,
                           uint8_t spectrum_smooth_pct, uint8_t speaker_enabled,
                           uint8_t att_rin_level)
{
    uint32_t p = 0U;

    p = append_str(buf, p, buf_size, "key,value\n");
    p = append_str(buf, p, buf_size, "touch_raw_x_min,"); p = append_u32(buf, p, buf_size, cal->raw_x_min); p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "touch_raw_x_max,"); p = append_u32(buf, p, buf_size, cal->raw_x_max); p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "touch_raw_y_min,"); p = append_u32(buf, p, buf_size, cal->raw_y_min); p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "touch_raw_y_max,"); p = append_u32(buf, p, buf_size, cal->raw_y_max); p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "touch_swap_xy,");   p = append_u32(buf, p, buf_size, cal->swap_xy);   p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "touch_invert_x,");  p = append_u32(buf, p, buf_size, cal->invert_x);  p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "touch_invert_y,");  p = append_u32(buf, p, buf_size, cal->invert_y);  p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "vfo_hz,");          p = append_u32(buf, p, buf_size, vfo_hz);         p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "mode,");            p = append_str(buf, p, buf_size, mode_to_str(mode)); p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "tune_step_hz,");    p = append_u32(buf, p, buf_size, tune_step_hz);   p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "audio_bw,");        p = append_str(buf, p, buf_size, audio_bw_to_str(audio_bw)); p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "volume_db_x2,");    p = append_i32(buf, p, buf_size, volume_db_x2);   p = append_str(buf, p, buf_size, "\n");
    /* AM/USB/LSB/NFM sample rate (01/09/2026) - plain 0/1, same shape
     * as the touch_* boolean flags above (0=96kHz, the firmware
     * default; 1=48kHz). WFM is unaffected either way (always
     * 192kHz) - see s_nonwfm_use_48k's own declaration comment in
     * main.c. */
    p = append_str(buf, p, buf_size, "nonwfm_use_48k,");  p = append_u32(buf, p, buf_size, nonwfm_use_48k); p = append_str(buf, p, buf_size, "\n");
    /* MS5351 crystal reference (26/08/2026) - read directly from
     * ms5351.c, same "not threaded through every settings_poll()/
     * settings_save_now() call site" pattern as touch_get_calibration()
     * just above - see ms5351_get_xtal_hz()'s comment. */
    p = append_str(buf, p, buf_size, "ms5351_xtal_hz,");  p = append_u32(buf, p, buf_size, ms5351_get_xtal_hz()); p = append_str(buf, p, buf_size, "\n");
    /* PGA analog input gain ceiling (07/09/2026) - 0.5dB units, same
     * shape as volume_db_x2, but unsigned (0-95, no cut direction) -
     * see main.c's s_pga_gain_db_x2 declaration comment. main.c-owned,
     * so passed in like volume_db_x2/nonwfm_use_48k rather than read
     * via a getter. */
    p = append_str(buf, p, buf_size, "pga_gain_db_x2,");  p = append_i32(buf, p, buf_size, pga_gain_db_x2); p = append_str(buf, p, buf_size, "\n");
    /* Spectrum temporal smoothing (07/09/2026) - stored as the same
     * 0-95% "history weight" the UI already shows (see main.c's
     * s_spectrum_smooth_alpha comment), not the raw 0.0-0.95 float -
     * the caller converts on both ends so this file never needs to
     * know main.c's float representation. main.c-owned, passed in. */
    p = append_str(buf, p, buf_size, "spectrum_smooth_pct,"); p = append_u32(buf, p, buf_size, spectrum_smooth_pct); p = append_str(buf, p, buf_size, "\n");
    /* Speaker PA enable/mute (07/09/2026) - plain 0/1, same shape as
     * the touch_* / nonwfm_use_48k boolean flags. main.c-owned
     * (s_speaker_pa_enabled), passed in. */
    p = append_str(buf, p, buf_size, "speaker_enabled,"); p = append_u32(buf, p, buf_size, speaker_enabled); p = append_str(buf, p, buf_size, "\n");
    /* Backlight brightness (07/09/2026) - 0-100%, read straight from
     * backlight.c's own getter (backlight_get_percent()), same
     * "no threading through every call site" pattern as
     * ms5351_get_xtal_hz()/touch_get_calibration() above - see
     * settings.h's header comment for why this one is NOT a
     * build_csv() parameter despite main.c owning s_pga_gain_db_x2/
     * s_spectrum_smooth_alpha/s_speaker_pa_enabled above. */
    p = append_str(buf, p, buf_size, "backlight_pct,"); p = append_u32(buf, p, buf_size, backlight_get_percent()); p = append_str(buf, p, buf_size, "\n");
    /* Spectrum trace style (07/09/2026) - HEATMAP/LINE/OUTLINE, read
     * straight from spectrum.c's own getter, same reasoning as
     * backlight_pct just above. */
    p = append_str(buf, p, buf_size, "spectrum_style,"); p = append_str(buf, p, buf_size, spectrum_style_to_str(spectrum_get_style())); p = append_str(buf, p, buf_size, "\n");
    /* Spectrum/waterfall color palette (08/09/2026) - read straight
     * from spectrum.c's own getter, same "no ordering hazard, no
     * threading through call sites" reasoning as spectrum_style just
     * above (spectrum_init() only rebuilds the LUT from whatever
     * palette is ALREADY set - it doesn't reset the palette choice
     * itself - so applying this directly from settings_load(), before
     * spectrum_init() runs, is safe; see settings.h's header comment). */
    p = append_str(buf, p, buf_size, "spectrum_palette,"); p = append_str(buf, p, buf_size, spectrum_palette_to_str(spectrum_get_palette())); p = append_str(buf, p, buf_size, "\n");
    /* HEATMAP trace white/color-matched toggle (08/09/2026) - read
     * straight from spectrum.c's own getter, same "no ordering
     * hazard, no threading through call sites" reasoning as
     * spectrum_style/spectrum_palette above (spectrum_init() doesn't
     * touch this flag either). */
    p = append_str(buf, p, buf_size, "spec_trace_white,"); p = append_u32(buf, p, buf_size, spectrum_get_heatmap_trace_white()); p = append_str(buf, p, buf_size, "\n");
    /* ATT / front-end input impedance level (08/09/2026) - 0=10k/
     * 1=20k/2=40k (aic3204_rin_t), shared by the manual ATT tile and
     * the RF-level auto-AGC's own Rin escalation - see main.c's
     * s_rf_agc_rin_level declaration comment. main.c-owned, no
     * getter, so passed in like pga_gain_db_x2/spectrum_smooth_pct/
     * speaker_enabled above rather than read directly here. */
    p = append_str(buf, p, buf_size, "att_rin_level,"); p = append_u32(buf, p, buf_size, att_rin_level); p = append_str(buf, p, buf_size, "\n");
    return p;
}

static uint32_t manual_atou32(const uint8_t *s, uint32_t len)
{
    uint32_t v = 0U;
    uint32_t i;

    for (i = 0U; i < len; i++) {
        if ((s[i] < '0') || (s[i] > '9')) {
            break;
        }
        v = (v * 10U) + (uint32_t)(s[i] - '0');
    }
    return v;
}

/* Same as manual_atou32() but handles a leading '-' - needed for
 * volume_db_x2 (negative most of the time - 0dB is already unity, so
 * anything below that is a negative value in these 0.5dB-native
 * units). */
static int32_t manual_atoi32(const uint8_t *s, uint32_t len)
{
    uint8_t neg = 0U;
    uint32_t i = 0U;

    if ((len > 0U) && (s[0] == '-')) {
        neg = 1U;
        i = 1U;
    }
    {
        int32_t v = (int32_t)manual_atou32(&s[i], len - i);
        return neg ? -v : v;
    }
}

static uint8_t mem_eq(const uint8_t *a, const uint8_t *b, uint32_t len)
{
    uint32_t i;

    for (i = 0U; i < len; i++) {
        if (a[i] != b[i]) {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t key_is(const uint8_t *key, uint32_t key_len, const char *literal)
{
    uint32_t lit_len = 0U;

    while (literal[lit_len] != '\0') {
        lit_len++;
    }
    return ((key_len == lit_len) && mem_eq(key, (const uint8_t *)literal, lit_len)) ? 1U : 0U;
}

uint8_t settings_load(settings_loaded_t *out)
{
    uint8_t buf[512]; /* 01/09/2026: bumped from 256 to 384 - the REALISTIC worst case (every touch corner at 4095, vfo_hz at TUNE_MAX_HZ=180000000, tune_step_hz at 1000000, ms5351_xtal_hz near 26000000, mode="USB"/"WFM", audio_bw="1K8", volume_db_x2 negative) came out to 246 bytes even BEFORE nonwfm_use_48k existed - only 10 bytes of headroom, and adding that one new short line ("nonwfm_use_48k,1\n", 18 bytes) already pushed it OVER 256. append_str()/append_u32()/append_i32() all respect buf_size and would have silently truncated rather than corrupted memory, but a truncated CONFIG.CSV losing its last field(s) is still a real, if rare, data-loss bug worth avoiding outright rather than accepting - 384 gives comfortable headroom for this field and future ones. 07/09/2026: bumped again to 512 - pga_gain_db_x2/spectrum_smooth_pct/speaker_enabled/backlight_pct/spectrum_style ("spectrum_style,HEATMAP\n" alone is 23 bytes) add roughly another 100 bytes worst-case, and 384 no longer leaves the same comfortable margin. */
    uint32_t n;
    uint32_t pos = 0U;
    uint8_t got_any = 0U;
    touch_calibration_t cal;
    uint8_t have_field[7] = { 0U, 0U, 0U, 0U, 0U, 0U, 0U }; /* only apply the calibration if ALL 7 showed up - see this function's comment */

    out->have_vfo_hz = 0U;
    out->have_mode = 0U;
    out->have_tune_step_hz = 0U;
    out->have_audio_bw = 0U;
    out->have_volume_db_x2 = 0U;
    out->have_nonwfm_use_48k = 0U;
    out->have_pga_gain_db_x2 = 0U;
    out->have_spectrum_smooth_pct = 0U;
    out->have_speaker_enabled = 0U;
    out->have_backlight_pct = 0U;
    out->have_att_rin_level = 0U;

    n = spi_flash_read_file_by_name(CONFIG_FILE_NAME8, CONFIG_FILE_EXT3, buf, sizeof(buf));
    if (n == 0U) {
        debug_print("settings_load: CONFIG.CSV not found or empty - using firmware defaults\n");
        return 0U;
    }

    while (pos < n) {
        uint32_t line_start = pos;
        uint32_t line_len;
        uint32_t comma;
        uint8_t found_comma = 0U;
        uint32_t k;

        while ((pos < n) && (buf[pos] != '\n')) {
            pos++;
        }
        line_len = pos - line_start;
        if (pos < n) {
            pos++; /* skip the '\n' itself */
        }

        comma = 0U;
        for (k = 0U; k < line_len; k++) {
            if (buf[line_start + k] == ',') {
                comma = k;
                found_comma = 1U;
                break;
            }
        }
        if (!found_comma) {
            continue; /* header line ("key,value") also lands here and gets skipped the same way if it somehow had no comma - harmless either way since "key" isn't a recognized key below */
        }

        {
            const uint8_t *key = &buf[line_start];
            uint32_t key_len = comma;
            const uint8_t *val = &buf[line_start + comma + 1U];
            uint32_t val_len = line_len - comma - 1U;

            if (key_is(key, key_len, "touch_raw_x_min")) { cal.raw_x_min = (uint16_t)manual_atou32(val, val_len); have_field[0] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "touch_raw_x_max")) { cal.raw_x_max = (uint16_t)manual_atou32(val, val_len); have_field[1] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "touch_raw_y_min")) { cal.raw_y_min = (uint16_t)manual_atou32(val, val_len); have_field[2] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "touch_raw_y_max")) { cal.raw_y_max = (uint16_t)manual_atou32(val, val_len); have_field[3] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "touch_swap_xy"))   { cal.swap_xy   = (uint8_t)manual_atou32(val, val_len);  have_field[4] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "touch_invert_x"))  { cal.invert_x  = (uint8_t)manual_atou32(val, val_len);  have_field[5] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "touch_invert_y"))  { cal.invert_y  = (uint8_t)manual_atou32(val, val_len);  have_field[6] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "vfo_hz")) { out->vfo_hz = manual_atou32(val, val_len); out->have_vfo_hz = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "tune_step_hz")) { out->tune_step_hz = manual_atou32(val, val_len); out->have_tune_step_hz = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "volume_db_x2")) { out->volume_db_x2 = (int16_t)manual_atoi32(val, val_len); out->have_volume_db_x2 = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "nonwfm_use_48k")) { out->nonwfm_use_48k = (uint8_t)manual_atou32(val, val_len); out->have_nonwfm_use_48k = 1U; got_any = 1U; }
            /* pga_gain_db_x2/spectrum_smooth_pct/speaker_enabled
             * (07/09/2026): main.c-owned, so - same as vfo_hz/mode/
             * volume_db_x2/nonwfm_use_48k above - just stashed into
             * *out with their own have_* flag for the CALLER to apply
             * once boot ordering allows (see settings.h's header
             * comment). No range clamping here - that is main.c's job
             * when it applies these, same as it already does for
             * volume_db_x2 (VOLUME_MIN_X2/MAX_X2) rather than settings.c
             * guessing at limits that live in main.c. */
            else if (key_is(key, key_len, "pga_gain_db_x2")) { out->pga_gain_db_x2 = (int16_t)manual_atoi32(val, val_len); out->have_pga_gain_db_x2 = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "spectrum_smooth_pct")) { out->spectrum_smooth_pct = (uint8_t)manual_atou32(val, val_len); out->have_spectrum_smooth_pct = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "speaker_enabled")) { out->speaker_enabled = (uint8_t)manual_atou32(val, val_len); out->have_speaker_enabled = 1U; got_any = 1U; }
            /* backlight_pct (07/09/2026): backlight.c-owned WITH a
             * getter, but still stashed into *out rather than applied
             * directly here - backlight_init() runs AFTER
             * settings_load() in main()'s boot sequence and would
             * just overwrite a direct apply with its own compiled-in
             * default (see settings.h's header comment). No clamping
             * needed on the caller's side either - backlight_set_percent()
             * already clamps both ends itself. */
            else if (key_is(key, key_len, "backlight_pct")) { out->backlight_pct = (uint8_t)manual_atou32(val, val_len); out->have_backlight_pct = 1U; got_any = 1U; }
            /* att_rin_level (08/09/2026): main.c-owned
             * (s_rf_agc_rin_level), no getter, so - same as
             * pga_gain_db_x2/spectrum_smooth_pct/speaker_enabled above
             * - just stashed into *out with its own have_* flag for
             * the caller to apply once the codec is up (needs
             * aic3204_set_input_impedance(), same boot-ordering
             * requirement as pga_gain_db_x2's rf_agc_apply_pga() call
             * - see settings.h's header comment). No clamping here -
             * that's the caller's job, same reasoning as the other
             * three main.c-owned fields. */
            else if (key_is(key, key_len, "att_rin_level")) { out->att_rin_level = (uint8_t)manual_atou32(val, val_len); out->have_att_rin_level = 1U; got_any = 1U; }
            /* spectrum_style (07/09/2026): applied DIRECTLY here, no
             * have_ flag/value pair at all - unlike backlight_pct just
             * above, spectrum_init() (also called after
             * settings_load()) never touches spectrum.c's style state,
             * so there is no ordering hazard to defer around - same
             * reasoning as touch_set_calibration()/ms5351_set_xtal_hz()
             * below. Unrecognized value: silently ignored, leaves
             * whatever spectrum_init()/the s_style initializer already
             * set (SPECTRUM_STYLE_HEATMAP). */
            else if (key_is(key, key_len, "spectrum_style")) {
                if      ((val_len >= 4U) && mem_eq(val, (const uint8_t *)"LINE", 4U))    { spectrum_set_style(SPECTRUM_STYLE_LINE); got_any = 1U; }
                else if ((val_len >= 7U) && mem_eq(val, (const uint8_t *)"OUTLINE", 7U)) { spectrum_set_style(SPECTRUM_STYLE_OUTLINE); got_any = 1U; }
                else if ((val_len >= 7U) && mem_eq(val, (const uint8_t *)"HEATMAP", 7U)) { spectrum_set_style(SPECTRUM_STYLE_HEATMAP); got_any = 1U; }
                /* else: unrecognized value - leave the current style alone */
            }
            /* spectrum_palette (08/09/2026): applied DIRECTLY here,
             * same shape/reasoning as spectrum_style just above - see
             * this file's build_csv() comment for why there's no
             * ordering hazard. Unrecognized value: silently ignored,
             * leaves whatever build_lut()/the s_palette initializer
             * already set (SPECTRUM_PALETTE_CLASSIC). Checked longest-
             * name-first - REQUIRED for CLASSIC_GREEN/CLASSIC (the
             * former genuinely starts with the latter) and
             * TEMPER_COLORS (same length as CLASSIC_GREEN, no actual
             * collision with anything, kept in the same length-order
             * position for consistency). Every other name is checked
             * longest-first too, purely as a defensive habit - none
             * of the rest actually collide. */
            else if (key_is(key, key_len, "spectrum_palette")) {
                if      ((val_len >= 13U) && mem_eq(val, (const uint8_t *)"CLASSIC_GREEN", 13U)) { spectrum_set_palette(SPECTRUM_PALETTE_CLASSIC_GREEN); got_any = 1U; }
                else if ((val_len >= 13U) && mem_eq(val, (const uint8_t *)"TEMPER_COLORS", 13U)) { spectrum_set_palette(SPECTRUM_PALETTE_TEMPER_COLORS); got_any = 1U; }
                else if ((val_len >= 9U) && mem_eq(val, (const uint8_t *)"GRAYSCALE", 9U))        { spectrum_set_palette(SPECTRUM_PALETTE_GRAYSCALE); got_any = 1U; }
                else if ((val_len >= 8U) && mem_eq(val, (const uint8_t *)"ELECTRIC", 8U))         { spectrum_set_palette(SPECTRUM_PALETTE_ELECTRIC); got_any = 1U; }
                else if ((val_len >= 7U) && mem_eq(val, (const uint8_t *)"VIRIDIS", 7U))          { spectrum_set_palette(SPECTRUM_PALETTE_VIRIDIS); got_any = 1U; }
                else if ((val_len >= 7U) && mem_eq(val, (const uint8_t *)"INFERNO", 7U))          { spectrum_set_palette(SPECTRUM_PALETTE_INFERNO); got_any = 1U; }
                else if ((val_len >= 7U) && mem_eq(val, (const uint8_t *)"CLASSIC", 7U))          { spectrum_set_palette(SPECTRUM_PALETTE_CLASSIC); got_any = 1U; }
                else if ((val_len >= 6U) && mem_eq(val, (const uint8_t *)"PLASMA", 6U))           { spectrum_set_palette(SPECTRUM_PALETTE_PLASMA); got_any = 1U; }
                else if ((val_len >= 6U) && mem_eq(val, (const uint8_t *)"WEBSDR", 6U))           { spectrum_set_palette(SPECTRUM_PALETTE_WEBSDR); got_any = 1U; }
                else if ((val_len >= 5U) && mem_eq(val, (const uint8_t *)"MAGMA", 5U))            { spectrum_set_palette(SPECTRUM_PALETTE_MAGMA); got_any = 1U; }
                else if ((val_len >= 5U) && mem_eq(val, (const uint8_t *)"TURBO", 5U))            { spectrum_set_palette(SPECTRUM_PALETTE_TURBO); got_any = 1U; }
                else if ((val_len >= 5U) && mem_eq(val, (const uint8_t *)"SMOKE", 5U))            { spectrum_set_palette(SPECTRUM_PALETTE_SMOKE); got_any = 1U; }
                else if ((val_len >= 5U) && mem_eq(val, (const uint8_t *)"VIVID", 5U))            { spectrum_set_palette(SPECTRUM_PALETTE_VIVID); got_any = 1U; }
                else if ((val_len >= 4U) && mem_eq(val, (const uint8_t *)"FIRE", 4U))             { spectrum_set_palette(SPECTRUM_PALETTE_FIRE); got_any = 1U; }
                else if ((val_len >= 4U) && mem_eq(val, (const uint8_t *)"GQRX", 4U))             { spectrum_set_palette(SPECTRUM_PALETTE_GQRX); got_any = 1U; }
                /* else: unrecognized value - leave the current palette alone */
            }
            /* spec_trace_white (08/09/2026): applied DIRECTLY here,
             * same shape/reasoning as spectrum_style/spectrum_palette
             * above - no ordering hazard. */
            else if (key_is(key, key_len, "spec_trace_white")) { spectrum_set_heatmap_trace_white((uint8_t)manual_atou32(val, val_len)); got_any = 1U; }
            else if (key_is(key, key_len, "ms5351_xtal_hz")) {
                /* Applied directly, same as touch_set_calibration()
                 * just below - no ordering dependency on the rest of
                 * main()'s boot sequence (ms5351_init()/
                 * ms5351_tune_captured() don't care what s_xtal_hz is
                 * until the first real ms5351_set_lo_freq() call,
                 * which always happens after settings_load() in
                 * main()). */
                ms5351_set_xtal_hz(manual_atou32(val, val_len));
                got_any = 1U;
            }
            else if (key_is(key, key_len, "mode")) {
                if      ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"USB", 3U)) { out->mode = DEMOD_MODE_USB; }
                else if ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"LSB", 3U)) { out->mode = DEMOD_MODE_LSB; }
                else if ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"NFM", 3U)) { out->mode = DEMOD_MODE_NFM; }
                else if ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"WFM", 3U)) { out->mode = DEMOD_MODE_WFM; }
                else if ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"SAM", 3U)) { out->mode = DEMOD_MODE_SAM; } /* 26/08/2026 fix, see mode_to_str()'s comment */
                else if ((val_len >= 2U) && mem_eq(val, (const uint8_t *)"AM", 2U))  { out->mode = DEMOD_MODE_AM; }
                else { continue; } /* unrecognized value - don't set have_mode, leave the caller's default alone */
                out->have_mode = 1U;
                got_any = 1U;
            }
            else if (key_is(key, key_len, "audio_bw")) {
                if      ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"4K0", 3U)) { out->audio_bw = AUDIO_BW_4K0; }
                else if ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"2K3", 3U)) { out->audio_bw = AUDIO_BW_2K3; }
                else if ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"1K8", 3U)) { out->audio_bw = AUDIO_BW_1K8; }
                else { continue; } /* unrecognized value - leave the caller's default alone */
                out->have_audio_bw = 1U;
                got_any = 1U;
            }
            /* any other unrecognized key: silently ignored - see settings.h's forward-compatibility comment */
        }
    }

    if (have_field[0] && have_field[1] && have_field[2] && have_field[3] && have_field[4] && have_field[5] && have_field[6]) {
        touch_set_calibration(&cal);
        debug_print("settings_load: touch calibration applied from CONFIG.CSV\n");
    } else if (have_field[0] || have_field[1] || have_field[2] || have_field[3] || have_field[4] || have_field[5] || have_field[6]) {
        debug_print("settings_load: CONFIG.CSV has SOME but not all touch_* fields - ignoring calibration entirely rather than applying a half-built one\n");
    }

    debug_print("settings_load: done\n");
    return got_any;
}

void settings_mark_dirty(void)
{
    s_dirty = 1U;
    s_dirty_since_ms = g_msticks;
}

void settings_save_now(uint32_t vfo_hz, demod_mode_t mode, uint32_t tune_step_hz, audio_bw_t audio_bw, int16_t volume_db_x2, uint8_t nonwfm_use_48k,
                        int16_t pga_gain_db_x2, uint8_t spectrum_smooth_pct, uint8_t speaker_enabled, uint8_t att_rin_level)
{
    touch_calibration_t cal;
    uint8_t csv[512]; /* see settings_load()'s buffer comment for why 256, then 384, stopped being safe */
    uint32_t len;

    touch_get_calibration(&cal);
    len = build_csv(csv, sizeof(csv), &cal, vfo_hz, mode, tune_step_hz, audio_bw, volume_db_x2, nonwfm_use_48k,
                     pga_gain_db_x2, spectrum_smooth_pct, speaker_enabled, att_rin_level);

    if (spi_flash_write_or_update_file(CONFIG_FILE_NAME8, CONFIG_FILE_EXT3, csv, len)) {
        debug_print("settings_save_now: CONFIG.CSV saved\n");
    } else {
        debug_print("settings_save_now: *** CONFIG.CSV save FAILED - see spi_flash error above ***\n");
    }
    s_dirty = 0U;
}

void settings_poll(uint32_t vfo_hz, demod_mode_t mode, uint32_t tune_step_hz, audio_bw_t audio_bw, int16_t volume_db_x2, uint8_t nonwfm_use_48k,
                    int16_t pga_gain_db_x2, uint8_t spectrum_smooth_pct, uint8_t speaker_enabled, uint8_t att_rin_level)
{
    if (s_async_save_in_progress) {
        spi_flash_async_status_t st = spi_flash_async_save_poll();

        if (st == SPI_FLASH_ASYNC_DONE) {
            debug_print("settings_poll: async CONFIG.CSV save done\n");
            s_async_save_in_progress = 0U;
        } else if (st == SPI_FLASH_ASYNC_ERROR) {
            debug_print("settings_poll: *** async CONFIG.CSV save FAILED - see spi_flash error above ***\n");
            s_async_save_in_progress = 0U;
        }
        /* SPI_FLASH_ASYNC_BUSY: nothing else to do this tick - don't
         * re-check dirty/debounce or start a second save while one is
         * still in flight. */
        return;
    }

    if (!s_dirty) {
        return;
    }
    if ((g_msticks - s_dirty_since_ms) < SETTINGS_SAVE_DEBOUNCE_MS) {
        return;
    }

    /*
     * Non-blocking path - see spi_flash.h's comment on why this
     * matters (a blocking save here would freeze the spectrum/
     * waterfall, which share the main loop with this call, for
     * however long the flash chip's own erase/program circuitry
     * takes - noticed by the project owner 18/08/2026 while tuning).
     * spi_flash_async_save_start() only succeeds if CONFIG.CSV
     * already exists AND the new content needs the exact same
     * cluster count as before - true on essentially every save in
     * practice, since this schema always fits in one 512-byte
     * cluster. If it can't (the very first save ever, before
     * CONFIG.CSV exists yet), fall back to the old blocking
     * settings_save_now() just this once - correct either way, just
     * not smooth that one time.
     */
    {
        touch_calibration_t cal;
        uint32_t len;

        touch_get_calibration(&cal);
        len = build_csv(s_async_csv_buf, sizeof(s_async_csv_buf), &cal, vfo_hz, mode, tune_step_hz, audio_bw, volume_db_x2, nonwfm_use_48k,
                         pga_gain_db_x2, spectrum_smooth_pct, speaker_enabled, att_rin_level);

        if (spi_flash_async_save_start(CONFIG_FILE_NAME8, CONFIG_FILE_EXT3, s_async_csv_buf, len)) {
            s_async_save_in_progress = 1U;
            s_dirty = 0U;
        } else {
            debug_print("settings_poll: async fast path unavailable (first save?) - falling back to a blocking save\n");
            settings_save_now(vfo_hz, mode, tune_step_hz, audio_bw, volume_db_x2, nonwfm_use_48k,
                               pga_gain_db_x2, spectrum_smooth_pct, speaker_enabled, att_rin_level);
        }
    }
}
