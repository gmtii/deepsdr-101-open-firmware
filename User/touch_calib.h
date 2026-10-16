#ifndef TOUCH_CALIB_H
#define TOUCH_CALIB_H

#include <stdint.h>
#include "touch.h"

/*
 * 4-point on-screen calibration wizard for the XPT2046 resistive
 * touch panel driven by touch.c.
 *
 * touch.c already supports a full linear calibration (touch_calibration_t:
 * raw_x_min/max, raw_y_min/max, swap_xy, invert_x, invert_y - see
 * touch.h), but until now those numbers had to be hand-measured with
 * touch_debug_raw() (touch each corner, note the raw values, guess at
 * swap_xy/invert_x/invert_y by trial and error) and hardcoded into
 * touch_init(). This module automates that: it walks the user through
 * touching four on-screen crosshairs, in order TOP-LEFT -> TOP-RIGHT ->
 * BOTTOM-RIGHT -> BOTTOM-LEFT (one continuous clockwise sweep), and
 * derives a full touch_calibration_t from the four raw readings.
 *
 * Three points (not collinear) would already be the mathematical
 * minimum to solve swap_xy/invert_x/invert_y/min/max - deliberately
 * using a FOURTH instead, one per corner, for two reasons:
 *   - Every corner gets DIRECTLY measured. With only 3 points one
 *     corner is always reconstructed by linear extrapolation from the
 *     other three instead of actually touched - exactly the corner
 *     where accuracy tends to matter most in practice, since that's
 *     usually where a real on-screen button (e.g. this project's
 *     bottom-right EXIT/MENU tiles) sits and a resistive panel is
 *     rarely perfectly linear all the way to its physical edges.
 *   - Each axis's min/max now comes from AVERAGING two independent
 *     edge measurements instead of a single pair of raw readings -
 *     e.g. the X range uses BOTH the top edge (TOP-LEFT/TOP-RIGHT)
 *     AND the bottom edge (BOTTOM-LEFT/BOTTOM-RIGHT), which also
 *     halves the effect of any single noisy sample.
 *
 * swap_xy itself is decided from BOTH horizontal edges (top and
 * bottom) together, same "which raw channel moved more" logic as
 * before, just summed across two independent horizontal moves instead
 * of one - see compute_calibration()'s comment in touch_calib.c for
 * the full derivation.
 *
 * Talks to the XPT2046 through touch_is_pressed()/touch_read_raw()
 * directly (NOT touch_read()) - the whole point is to measure raw ADC
 * counts independent of whatever calibration (or lack of one) is
 * currently active, same reasoning as touch_debug_raw().
 *
 * Non-blocking, same shape as the rest of the input handling in this
 * project: touch_calib_start() only arms the wizard and draws the
 * first target; touch_calib_poll() must be called once per main loop
 * iteration while touch_calib_active() is true (same pattern as
 * s_screen_asleep's gating in main.c's loop) to actually advance it.
 *
 * The result is applied live via touch_set_calibration() as soon as
 * it's computed (so the wizard's own "tap to confirm" screen already
 * exercises the new numbers), AND printed to the debug UART as a
 * ready-to-paste touch_calibration_t literal, the same way the
 * current hardcoded calibration in touch_init() was originally
 * derived by hand - copy that into touch_init() to make it survive a
 * reboot, since this project has no flash-backed settings storage yet
 * (session-only otherwise).
 */

typedef void (*touch_calib_done_cb_t)(const touch_calibration_t *cal);

/* Starts the wizard: draws the full-screen instructions + first
 * crosshair immediately (synchronous - safe to call right before
 * returning to the main loop). on_done may be NULL if the caller
 * doesn't need to know when a GOOD calibration lands (touch_calib.c
 * has already applied it via touch_set_calibration() by the time the
 * callback fires - it's for whatever ELSE the caller wants to do,
 * e.g. main.c uses it only to know it's safe to repaint the radio
 * screen once the wizard's own result screen is dismissed). */
void touch_calib_start(touch_calib_done_cb_t on_done);

/* 1 while the wizard owns the screen and touch input. The caller's
 * main loop MUST skip its own touch routing (demo_touch_poll()) and
 * any periodic redraw that would paint over the wizard while this is
 * true - same requirement s_screen_asleep already has on the loop. */
uint8_t touch_calib_active(void);

/* Advances the wizard's state machine by one step; cheap and
 * non-blocking (no busy-waiting on a touch), call once per main loop
 * iteration while touch_calib_active(). */
void touch_calib_poll(void);

/* Aborts the wizard immediately, discarding any points captured so
 * far - whatever touch_set_calibration() last had stays in effect
 * (this call never touches it). Intended for a short press on the
 * encoder while the wizard is active, the same "get me out of here"
 * role the encoder already has for screen_sleep_enter() - see main.c's
 * loop. Safe to call even if the wizard isn't active (no-op). */
void touch_calib_cancel(void);

#endif /* TOUCH_CALIB_H */
