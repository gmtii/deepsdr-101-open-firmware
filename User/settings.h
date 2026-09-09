#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include "demod_am.h"

/*
 * Persistent settings (CONFIG.CSV, alongside CHANNEL.CSV) - built on
 * top of spi_flash.c's confirmed-working FAT12 read/write (17/08/2026:
 * chip identified as Winbond W25Q16, filesystem confirmed FAT12 and
 * confirmed to be the SAME volume the bootloader's USB-MSC mode
 * exposes for update4.bin, write path confirmed both by our own
 * read-back AND by mounting it on a PC after a real write).
 *
 * Format: plain "key,value\n" lines, no quoting/escaping - same
 * spirit as CHANNEL.CSV, and dead simple to parse/generate on a
 * bare-metal MCU without a heap (see settings.c's manual
 * parser/serializer - no sprintf/strtol, same policy as the rest of
 * this project). Unknown keys are silently ignored on load (forward
 * compatible: an older CONFIG.CSV still loads fine after this schema
 * grows); a key missing entirely just leaves that setting at whatever
 * the caller's own default already was.
 *
 * Currently covers: touch calibration (the 7 touch_calibration_t
 * fields - see touch.h), VFO frequency, demod mode, tune step, the
 * AM/SSB audio filter width, speaker volume, the AM/USB/LSB/NFM
 * sample rate choice (96kHz/48kHz - see main.c's s_nonwfm_use_48k,
 * persisted 01/09/2026 once it became a settled everyday preference
 * rather than just a bench A/B toggle), and the MS5351 crystal
 * reference frequency (ms5351_xtal_hz - see ms5351_get_xtal_hz()'s
 * comment; applied directly from settings_load(), same "no boot-order
 * dependency" reasoning as touch calibration). Does NOT currently cover memory
 * channels - CHANNEL.CSV already exists for that on this volume (727
 * bytes, format not yet reverse-engineered from this project's code)
 * and deserves its own look before deciding whether to read/write it
 * or keep it separate; out of scope for this first pass.
 *
 * 07/09/2026: five more fields added, split across the two existing
 * patterns depending on who OWNS the underlying state:
 *   - pga_gain_db_x2 (main.c's s_pga_gain_db_x2, the manual PGA
 *     ceiling - see main.c's own comment), spectrum_smooth_pct
 *     (main.c's s_spectrum_smooth_alpha, presented/stored as the same
 *     0-95% "history weight" the UI already uses rather than the raw
 *     0.0-0.95 float) and speaker_enabled (main.c's
 *     s_speaker_pa_enabled) are all static state main.c itself owns
 *     with no getter - same shape as vfo_hz/mode/volume_db_x2/
 *     nonwfm_use_48k above: passed IN by the caller on save (see
 *     settings_poll()/settings_save_now()'s new parameters) and
 *     handed back via their own have_ flag/value pair on load, for the
 *     caller to apply once boot ordering allows (PGA needs the codec
 *     already up - same as volume_db_x2 - and speaker_enabled needs
 *     speaker_pa_gpio_init() to have already run, or it would just be
 *     overwritten by that function's own GPIO-level sync of the
 *     compiled-in default).
 *   - backlight_pct is backlight.c-owned state WITH a getter
 *     (backlight_get_percent()) but, like PGA/speaker above, still
 *     needs a have_ flag/value pair rather than being applied directly
 *     from settings_load() - backlight_init() runs AFTER
 *     settings_load() in main()'s boot sequence and unconditionally
 *     calls backlight_set_percent() with the compiled-in default, so
 *     applying here first would just be clobbered a few lines later.
 *   - spectrum_style is spectrum.c-owned state (spectrum_get_style()/
 *     spectrum_set_style()) with NO such ordering hazard -
 *     spectrum_init() (called after settings_load() too) only builds
 *     the palette LUT and never touches the style - so this one IS
 *     applied directly from settings_load(), same as touch
 *     calibration and ms5351_xtal_hz, and needs no struct field at
 *     all.
 *
 * Tune step is stored as its Hz VALUE ("tune_step_hz"), not a raw
 * index into main.c's k_tune_steps[] - main.c looks up which index
 * matches on load, so a future reordering/insertion in that array
 * doesn't silently point an old saved CONFIG.CSV at the wrong step.
 *
 * Persistence policy: NOR flash write cycles are finite (~100k per
 * sector is typical for a W25Q part) - saving on every encoder tick
 * while tuning would burn through that fast for no benefit. See
 * settings_mark_dirty()/settings_poll()'s comments for the debounce
 * this module uses instead.
 */

/* One flag+value pair per optional field - see settings_load()'s
 * comment. Grouped into a struct (rather than a growing list of
 * settings_load() out-parameters) so adding another persisted setting
 * later is one new pair of fields here, not a signature change
 * rippling through every caller. */
typedef struct {
    uint8_t      have_vfo_hz;
    uint32_t     vfo_hz;
    uint8_t      have_mode;
    demod_mode_t mode;
    uint8_t      have_tune_step_hz;
    uint32_t     tune_step_hz;
    uint8_t      have_audio_bw;
    audio_bw_t   audio_bw;
    uint8_t      have_volume_db_x2;
    int16_t      volume_db_x2;
    uint8_t      have_nonwfm_use_48k;
    uint8_t      nonwfm_use_48k;
    uint8_t      have_pga_gain_db_x2;
    int16_t      pga_gain_db_x2;      /* 0-95, 0.5dB units - see main.c's PGA_MIN_X2/PGA_MAX_X2 */
    uint8_t      have_spectrum_smooth_pct;
    uint8_t      spectrum_smooth_pct; /* 0-95 percent - see main.c's SPECTRUM_SMOOTH_MIN/MAX */
    uint8_t      have_speaker_enabled;
    uint8_t      speaker_enabled;     /* 0/1 */
    uint8_t      have_backlight_pct;
    uint8_t      backlight_pct;       /* 0-100, see backlight.h - backlight_set_percent() clamps up to its own floor */
    uint8_t      have_att_rin_level;
    uint8_t      att_rin_level;       /* 0=10k/1=20k/2=40k - aic3204_rin_t, see main.c's s_rf_agc_rin_level */
} settings_loaded_t;

/* Reads CONFIG.CSV (if present) and:
 *   - applies touch calibration directly via touch_set_calibration(),
 *     if and only if ALL 7 fields were present (a partially-written
 *     or corrupt file must not silently apply a half-built
 *     calibration - see settings.c's comment);
 *   - fills the rest of `*out` field-by-field, each with its own
 *     have_* flag, for the CALLER to apply (unlike touch calibration,
 *     these have ordering dependencies on the rest of main()'s boot
 *     sequence - demod_am_init() for mode/audio_bw, the real LO tune
 *     for vfo_hz - that settings.c has no business knowing about; see
 *     main()'s boot sequence for exactly where/why each one gets
 *     applied). A have_* flag left at 0 means that key wasn't present
 *     (first boot, or an older CONFIG.CSV from before that key
 *     existed) - the caller's own pre-set default should stand.
 * Returns 1 if CONFIG.CSV was found and at least one recognized key
 * was parsed from it, 0 otherwise. Call once at boot, after
 * spi_flash_init(). */
uint8_t settings_load(settings_loaded_t *out);

/* spectrum_style is applied directly against spectrum.c inside
 * settings_load() itself (see this file's header comment on why it
 * has no ordering hazard) - unlike everything else in
 * settings_loaded_t, there is no have_/value pair for it because the
 * caller never needs to touch it. */

/* Marks settings as changed - does NOT write to flash immediately,
 * see this file's header comment on write-cycle wear.
 * settings_poll() is what actually saves, debounced. Safe/cheap to
 * call from a hot path (e.g. every encoder tick during tuning) -
 * that's the whole point of debouncing here instead of at each call
 * site. */
void settings_mark_dirty(void);

/* Call once per main loop iteration (cheap - just a timestamp check
 * when nothing is dirty, or one small non-blocking step of an
 * in-progress save - see spi_flash.h's async-save comment). If
 * settings_mark_dirty() was called at least SETTINGS_SAVE_DEBOUNCE_MS
 * ago (see settings.c) and nothing has re-marked dirty since, starts
 * a save via spi_flash_async_save_start() - non-blocking, spread over
 * many subsequent settings_poll() calls instead of freezing the main
 * loop (and with it spectrum/waterfall drawing) for the flash chip's
 * own erase/program time. Same "wait for things to settle before
 * committing" idea as a text editor's autosave either way, so a
 * session of continuous tuning results in ONE save once you stop, not
 * one per encoder detent. Pass the CURRENT live value of everything
 * this module persists - it has no other way to know them.
 * pga_gain_db_x2/spectrum_smooth_pct/speaker_enabled/att_rin_level are
 * the four main.c-owned values (07-08/09/2026) with no getter -
 * backlight_pct and spectrum_style are NOT parameters here, since
 * settings.c reads those two straight from backlight_get_percent()/
 * spectrum_get_style() itself (see this file's header comment). */
void settings_poll(uint32_t vfo_hz, demod_mode_t mode, uint32_t tune_step_hz, audio_bw_t audio_bw, int16_t volume_db_x2, uint8_t nonwfm_use_48k,
                    int16_t pga_gain_db_x2, uint8_t spectrum_smooth_pct, uint8_t speaker_enabled, uint8_t att_rin_level);

/* Saves immediately, no debounce - for events that are already
 * naturally rare/deliberate (the touch calibration wizard finishing
 * is the current use, see main.c's touch_calib_done_callback()) where
 * waiting for the debounce window would just be a pointless delay
 * before the thing the user just did for its own sake gets persisted.
 * Same four trailing parameters as settings_poll() above. */
void settings_save_now(uint32_t vfo_hz, demod_mode_t mode, uint32_t tune_step_hz, audio_bw_t audio_bw, int16_t volume_db_x2, uint8_t nonwfm_use_48k,
                        int16_t pga_gain_db_x2, uint8_t spectrum_smooth_pct, uint8_t speaker_enabled, uint8_t att_rin_level);

#endif /* SETTINGS_H */
