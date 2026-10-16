#include "touch_calib.h"
#include "touch.h"
#include "gfx.h"
#include "debug_uart.h"

/* main.c's free-running millisecond counter, same extern touch.c
 * already uses for its own SPI-transaction timing. */
extern volatile uint32_t g_msticks;

/* px in from each screen edge for the crosshair targets - see this
 * file's header comment. Close enough to the real corner that the
 * measured raw range still covers nearly the whole panel, far enough
 * that the crosshair (and a fingertip pressing it) aren't sitting
 * right on the bezel edge, which resistive panels tend to read least
 * reliably.
 *
 * Lowered from 40 to 20, 18/08/2026: touch_read() treats the
 * MEASURED raw_x/y_min/max as corresponding to screen 0/max (see its
 * own comment) - i.e. it already stretches whatever was measured
 * CAL_MARGIN px in to cover the FULL screen, extrapolating linearly
 * past the measured points for the outer CAL_MARGIN px on each side.
 * The project owner found a ~3mm (~20px) band along each edge that
 * stayed pinned at a constant 479/799 (touch_debug_stream_poll()
 * showed raw still changing smoothly through that band, only the
 * CALIBRATED output was stuck) - i.e. clamp_u16() saturating because
 * the panel's last few mm aren't quite as linear as the extrapolation
 * assumes, so it "predicts" reaching the edge before the finger
 * actually gets there. A smaller CAL_MARGIN means less distance to
 * extrapolate over in the first place, which should shrink that dead
 * band proportionally - re-run the calibration wizard and check with
 * TOUCH_EDGE_DEBUG (see main.c) whether it did. Some small dead band
 * right at the physical bezel is normal for resistive panels in
 * general (most commercial devices have one too); the goal here is
 * "small enough to not matter", not necessarily zero. Still safely
 * above CAL_CROSS_HALF so the crosshair itself never gets clipped
 * against the screen edge (see draw_crosshair()'s call sites - this
 * relies on CAL_MARGIN > CAL_CROSS_HALF, unchanged). */
#define CAL_MARGIN        20U
#define CAL_CROSS_HALF    14U /* crosshair arm half-length, px */

/* A press must hold for this long before its raw reading counts -
 * filters the initial mechanical bounce of a resistive panel making
 * contact, same concern as touch.c's own TOUCH_SAMPLES median filter
 * in touch_read_raw(), just at the "did the finger actually land yet"
 * level instead of per-SPI-sample. */
#define CAL_DEBOUNCE_MS   120UL

/* How long the BAD-reading message stays up before automatically
 * restarting from point 0. */
#define CAL_BAD_MSG_MS    2000UL

/* Sanity thresholds for compute_calibration() - see its comment. */
#define CAL_MIN_RANGE        200U /* minimum acceptable |raw_max - raw_min| on either axis */
#define CAL_MIN_DOMINANCE    2    /* the winning raw channel must move at least this many times more than the other one during the TL->TR horizontal move, or swap_xy is too close to call */

typedef enum {
    CAL_STATE_IDLE = 0,
    CAL_STATE_ARM,             /* waiting for finger-up before arming the current step - avoids the tap that OPENED the wizard (or a lingering finger from the previous point/bad-reading message) being misread as the next press */
    CAL_STATE_WAIT_PRESS,      /* armed, waiting for a new press to start */
    CAL_STATE_DEBOUNCE,        /* press detected, waiting out CAL_DEBOUNCE_MS before sampling */
    CAL_STATE_WAIT_RELEASE,    /* point captured, waiting for finger-up before moving on */
    CAL_STATE_BAD_WAIT,        /* sanity check failed - error message up for CAL_BAD_MSG_MS, then restarts from point 0 */
    CAL_STATE_RESULT_ARM,      /* result screen shown, waiting for finger-up before arming the "tap anywhere to exit" */
    CAL_STATE_RESULT_WAIT_TAP, /* armed, any press now exits the wizard */
} cal_state_t;

typedef struct {
    uint16_t x, y;
    const char *label;
} cal_point_def_t;

#define CAL_NUM_POINTS 4U

/* Corner indices, fixed order - referenced by name throughout this
 * file instead of bare 0/1/2/3, since compute_calibration() groups
 * them by edge (top/bottom, left/right) in several places. */
#define CAL_PT_TL 0U
#define CAL_PT_TR 1U
#define CAL_PT_BR 2U
#define CAL_PT_BL 3U

/* TOP-LEFT -> TOP-RIGHT -> BOTTOM-RIGHT -> BOTTOM-LEFT, one
 * continuous clockwise sweep - see this file's header comment for why
 * all four corners are measured directly instead of reconstructing
 * the fourth by extrapolation from three. */
static const cal_point_def_t k_cal_points[CAL_NUM_POINTS] = {
    { CAL_MARGIN,                          CAL_MARGIN,                          "TOP-LEFT"     },
    { GFX_SCREEN_WIDTH  - 1U - CAL_MARGIN, CAL_MARGIN,                          "TOP-RIGHT"    },
    { GFX_SCREEN_WIDTH  - 1U - CAL_MARGIN, GFX_SCREEN_HEIGHT - 1U - CAL_MARGIN, "BOTTOM-RIGHT" },
    { CAL_MARGIN,                          GFX_SCREEN_HEIGHT - 1U - CAL_MARGIN, "BOTTOM-LEFT"  },
};

static cal_state_t s_state = CAL_STATE_IDLE;
static uint8_t s_point_idx = 0U;
static uint32_t s_press_start_ms = 0U;
static uint32_t s_bad_until_ms = 0U;
static uint16_t s_raw_x[CAL_NUM_POINTS], s_raw_y[CAL_NUM_POINTS]; /* raw reading captured at each corner, index-matched to k_cal_points[] */
static touch_calibration_t s_result_cal;
static touch_calib_done_cb_t s_on_done = NULL;

/* --- drawing helpers ---------------------------------------------- */

static void draw_crosshair(uint16_t x, uint16_t y, uint16_t color)
{
    gfx_hline((uint16_t)(x - CAL_CROSS_HALF), y, (uint16_t)(2U * CAL_CROSS_HALF + 1U), color);
    gfx_vline(x, (uint16_t)(y - CAL_CROSS_HALF), (uint16_t)(2U * CAL_CROSS_HALF + 1U), color);
}

/* Manual formatting, no sprintf - same policy as main.c's itoa (see
 * its calib_height_ruler_draw() comment) and debug_uart.c's
 * debug_print_dec(), neither of which this file can reuse directly
 * (the former is static to main.c, the latter compiles away entirely
 * when DEBUG_UART_ENABLED=0 - see debug_uart.h). Returns the number of
 * digits written to out; does NOT null-terminate, so callers can
 * append more before terminating themselves. */
static uint8_t u16_to_dec(uint16_t v, char *out)
{
    char tmp[6];
    uint8_t n = 0U, i = 0U;

    if (v == 0U) {
        tmp[n++] = '0';
    } else {
        while (v > 0U) {
            tmp[n++] = (char)('0' + (v % 10U));
            v = (uint16_t)(v / 10U);
        }
    }
    while (n > 0U) {
        out[i++] = tmp[--n];
    }
    return i;
}

static void draw_cal_field(uint16_t x, uint16_t y, const char *label, uint16_t value)
{
    char buf[24];
    uint8_t i = 0U;
    const char *p = label;

    /*
     * SIETE, NO DOS - 09/10/2026. La guarda reservaba sitio para UN
     * caracter mas y el cero final, pero detras del rotulo se escriben
     * siete bytes: el '=', hasta cinco cifras de u16_to_dec() y el cero.
     * Con los rotulos de hoy -el mas largo es "raw_x_min", 9- el peor caso
     * real son 16 de 24 y no llega a pasar nada; con un rotulo de 22 se
     * escribirian cinco bytes fuera de la pila. Una guarda que no cubre lo
     * que viene detras no es una guarda: es un numero que tranquiliza.
     */
    while (*p != '\0' && i < (uint8_t)(sizeof(buf) - 7U)) {
        buf[i++] = *p++;
    }
    buf[i++] = '=';
    i = (uint8_t)(i + u16_to_dec(value, &buf[i]));
    buf[i] = '\0';
    gfx_text(x, y, buf, GFX_COLOR_WHITE, GFX_COLOR_BLACK, 1);
}

static void build_point_caption(uint8_t idx, char *out, uint8_t out_size)
{
    const char *prefix = "POINT ";
    const char *mid = "/4 - ";
    const char *lbl = k_cal_points[idx].label;
    uint8_t i = 0U;

    while (*prefix != '\0' && i < (uint8_t)(out_size - 1U)) { out[i++] = *prefix++; }
    if (i < (uint8_t)(out_size - 1U)) { out[i++] = (char)('1' + idx); }
    while (*mid != '\0' && i < (uint8_t)(out_size - 1U)) { out[i++] = *mid++; }
    while (*lbl != '\0' && i < (uint8_t)(out_size - 1U)) { out[i++] = *lbl++; }
    out[i] = '\0';
}

static void draw_point_prompt(uint8_t idx)
{
    char caption[40];

    gfx_fill_screen(GFX_COLOR_BLACK);
    gfx_text(20, 20, "TOUCH CALIBRATION", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 2);
    build_point_caption(idx, caption, sizeof(caption));
    gfx_text(20, 60, caption, GFX_COLOR_YELLOW, GFX_COLOR_BLACK, 2);
    gfx_text(20, 90, "Touch and hold the crosshair below", GFX_COLOR_CYAN, GFX_COLOR_BLACK, 1);
    gfx_text(20, (uint16_t)(GFX_SCREEN_HEIGHT - 30), "Short-press the knob to cancel", GFX_COLOR_GRAY, GFX_COLOR_BLACK, 1);
    draw_crosshair(k_cal_points[idx].x, k_cal_points[idx].y, GFX_COLOR_YELLOW);
}

/* Partial redraw only (no gfx_fill_screen()) - same "don't repaint
 * more than actually changed" habit as the rest of the UI (see e.g.
 * ui.h's comment on ui_screen_touch() only redrawing the widget whose
 * state changed). Just the crosshair turning green + a short
 * confirmation line is enough feedback that the point registered. */
static void draw_point_captured(uint8_t idx)
{
    draw_crosshair(k_cal_points[idx].x, k_cal_points[idx].y, GFX_COLOR_GREEN);
    gfx_text(20, 90, "Captured - lift your finger            ", GFX_COLOR_GREEN, GFX_COLOR_BLACK, 1);
}

static void draw_bad_message(void)
{
    gfx_fill_screen(GFX_COLOR_BLACK);
    gfx_text(20, 20, "CALIBRATION FAILED", GFX_COLOR_RED, GFX_COLOR_BLACK, 2);
    gfx_text(20, 70, "Readings too close together, or an", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 1);
    gfx_text(20, 90, "ambiguous axis orientation.", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 1);
    gfx_text(20, 120, "Try touching each crosshair more precisely.", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 1);
    gfx_text(20, 160, "Restarting from point 1...", GFX_COLOR_YELLOW, GFX_COLOR_BLACK, 1);
}

static void print_calibration_uart(const touch_calibration_t *cal)
{
    /* Compiles away entirely when DEBUG_UART_ENABLED=0 (see
     * debug_uart.h) - harmless to call unconditionally. */
    debug_print("touch_calib: computed calibration - paste into touch_init()'s cal struct to make it survive a reboot:\n");
    debug_print_dec("  raw_x_min", cal->raw_x_min);
    debug_print_dec("  raw_x_max", cal->raw_x_max);
    debug_print_dec("  raw_y_min", cal->raw_y_min);
    debug_print_dec("  raw_y_max", cal->raw_y_max);
    debug_print_dec("  swap_xy", cal->swap_xy);
    debug_print_dec("  invert_x", cal->invert_x);
    debug_print_dec("  invert_y", cal->invert_y);
}

static void draw_result_screen(const touch_calibration_t *cal)
{
    gfx_fill_screen(GFX_COLOR_BLACK);
    gfx_text(20, 20, "CALIBRATION APPLIED", GFX_COLOR_GREEN, GFX_COLOR_BLACK, 2);
    gfx_text(20, 60, "Now in effect for this session:", GFX_COLOR_WHITE, GFX_COLOR_BLACK, 1);

    draw_cal_field(20, 90, "raw_x_min", cal->raw_x_min);
    draw_cal_field(20, 110, "raw_x_max", cal->raw_x_max);
    draw_cal_field(20, 130, "raw_y_min", cal->raw_y_min);
    draw_cal_field(20, 150, "raw_y_max", cal->raw_y_max);
    draw_cal_field(20, 170, "swap_xy", cal->swap_xy);
    draw_cal_field(20, 190, "invert_x", cal->invert_x);
    draw_cal_field(20, 210, "invert_y", cal->invert_y);

#if DEBUG_UART_ENABLED
    gfx_text(20, 250, "Also sent to the debug UART - paste it into", GFX_COLOR_CYAN, GFX_COLOR_BLACK, 1);
    gfx_text(20, 270, "touch_init()'s cal struct to keep it after reboot.", GFX_COLOR_CYAN, GFX_COLOR_BLACK, 1);
#else
    gfx_text(20, 250, "Build with -DDEBUG_UART_ENABLED=1 to also get", GFX_COLOR_GRAY, GFX_COLOR_BLACK, 1);
    gfx_text(20, 270, "this printed as a paste-able struct over UART.", GFX_COLOR_GRAY, GFX_COLOR_BLACK, 1);
#endif
    gfx_text(20, 300, "This is SESSION ONLY - copy the numbers above", GFX_COLOR_YELLOW, GFX_COLOR_BLACK, 1);
    gfx_text(20, 320, "into touch_init()'s cal struct to make it stick.", GFX_COLOR_YELLOW, GFX_COLOR_BLACK, 1);

    gfx_text(20, (uint16_t)(GFX_SCREEN_HEIGHT - 30), "Tap anywhere to continue", GFX_COLOR_CYAN, GFX_COLOR_BLACK, 1);
}

/*
 * Averages the two corner readings on each side of an edge grouping
 * and returns the resulting min/max/invert for one calibration field
 * (either the raw_x or the raw_y field of touch_calibration_t - see
 * compute_calibration()'s comment for which grouping each field uses
 * depending on swap_xy).
 *
 * horizontal_grouping=1 groups by LEFT edge (TL+BL) vs RIGHT edge
 * (TR+BR) - use this when the channel being calibrated is the one
 * that drives screen-horizontal movement.
 * horizontal_grouping=0 groups by TOP edge (TL+TR) vs BOTTOM edge
 * (BR+BL) - use this when it drives screen-vertical movement.
 *
 * "low" is always whichever side has the SMALLER target screen
 * coordinate (left for horizontal, top for vertical) - invert is 1 if
 * that side's raw average is actually the BIGGER one.
 */
static void axis_calib_from_group(const uint16_t *raw, uint8_t horizontal_grouping,
                                   uint16_t *out_min, uint16_t *out_max, uint8_t *out_invert)
{
    int32_t low_avg, high_avg;

    if (horizontal_grouping) {
        low_avg  = ((int32_t)raw[CAL_PT_TL] + (int32_t)raw[CAL_PT_BL]) / 2; /* left edge */
        high_avg = ((int32_t)raw[CAL_PT_TR] + (int32_t)raw[CAL_PT_BR]) / 2; /* right edge */
    } else {
        low_avg  = ((int32_t)raw[CAL_PT_TL] + (int32_t)raw[CAL_PT_TR]) / 2; /* top edge */
        high_avg = ((int32_t)raw[CAL_PT_BR] + (int32_t)raw[CAL_PT_BL]) / 2; /* bottom edge */
    }

    *out_min    = (uint16_t)((low_avg < high_avg) ? low_avg : high_avg);
    *out_max    = (uint16_t)((low_avg > high_avg) ? low_avg : high_avg);
    *out_invert = (low_avg > high_avg) ? 1U : 0U;
}

/* --- calibration math ----------------------------------------------
 *
 * See this file's header comment for the full reasoning. Summary:
 *
 *   1. swap_xy: compare how much each raw channel (X and Y) moved
 *      across BOTH horizontal edges - top (TL->TR) and bottom
 *      (BL->BR) - summed together for a steadier signal than a single
 *      edge would give. Whichever channel moved more overall is the
 *      one actually wired to the screen's horizontal axis.
 *
 *   2. Once swap_xy is known, each touch_calibration_t field gets
 *      calibrated from whichever pair of opposite edges its own
 *      hardware channel actually varies along:
 *        - raw_x field: horizontal grouping (left/right edges) if
 *          !swap_xy (raw_x drives screen X), otherwise vertical
 *          grouping (top/bottom edges) - see touch.c's touch_read()
 *          SWAP FIX comment for why the field always describes the
 *          HARDWARE channel, never "whatever ends up as screen x".
 *        - raw_y field: the opposite grouping of raw_x's.
 *      axis_calib_from_group() does the averaging + invert decision
 *      for whichever grouping it's told to use.
 * -------------------------------------------------------------- */
static uint8_t compute_calibration(touch_calibration_t *out)
{
    int32_t dx_rawX_top = (int32_t)s_raw_x[CAL_PT_TR] - (int32_t)s_raw_x[CAL_PT_TL];
    int32_t dx_rawY_top = (int32_t)s_raw_y[CAL_PT_TR] - (int32_t)s_raw_y[CAL_PT_TL];
    int32_t dx_rawX_bot = (int32_t)s_raw_x[CAL_PT_BR] - (int32_t)s_raw_x[CAL_PT_BL];
    int32_t dx_rawY_bot = (int32_t)s_raw_y[CAL_PT_BR] - (int32_t)s_raw_y[CAL_PT_BL];

    int32_t sum_adx_rawX = ((dx_rawX_top < 0) ? -dx_rawX_top : dx_rawX_top)
                          + ((dx_rawX_bot < 0) ? -dx_rawX_bot : dx_rawX_bot);
    int32_t sum_adx_rawY = ((dx_rawY_top < 0) ? -dx_rawY_top : dx_rawY_top)
                          + ((dx_rawY_bot < 0) ? -dx_rawY_bot : dx_rawY_bot);

    uint8_t swap_xy   = (sum_adx_rawY > sum_adx_rawX) ? 1U : 0U;
    int32_t dominant  = swap_xy ? sum_adx_rawY : sum_adx_rawX;
    int32_t other     = swap_xy ? sum_adx_rawX : sum_adx_rawY;

    uint16_t raw_x_min, raw_x_max, raw_y_min, raw_y_max;
    uint8_t  invert_x, invert_y;

    axis_calib_from_group(s_raw_x, (uint8_t)!swap_xy, &raw_x_min, &raw_x_max, &invert_x);
    axis_calib_from_group(s_raw_y, swap_xy,           &raw_y_min, &raw_y_max, &invert_y);

    /* Sanity checks - refuse to silently accept a calibration built
     * from readings too close together (fingers landing near-identical
     * spots between corners, or flaky SPI transactions) or an
     * ambiguous swap_xy call (the "losing" channel moved almost as
     * much as the winner) rather than trusting arithmetic on noise.
     * Matches this project's general "validate against real hardware
     * before trusting it" habit (see e.g. touch_read_raw()'s own
     * re-check after the SPI transaction, or the WFM AGC
     * investigation's insistence on re-measuring rather than
     * assuming). */
    if ((uint32_t)(raw_x_max - raw_x_min) < CAL_MIN_RANGE) { return 0U; }
    if ((uint32_t)(raw_y_max - raw_y_min) < CAL_MIN_RANGE) { return 0U; }
    if (dominant < (other * CAL_MIN_DOMINANCE)) { return 0U; }

    out->raw_x_min = raw_x_min;
    out->raw_x_max = raw_x_max;
    out->raw_y_min = raw_y_min;
    out->raw_y_max = raw_y_max;
    out->swap_xy  = swap_xy;
    out->invert_x = invert_x;
    out->invert_y = invert_y;
    return 1U;
}

/* --- public API ------------------------------------------------------ */

void touch_calib_start(touch_calib_done_cb_t on_done)
{
    s_on_done = on_done;
    s_point_idx = 0U;
    draw_point_prompt(0U);
    s_state = CAL_STATE_ARM;
}

uint8_t touch_calib_active(void)
{
    return (s_state != CAL_STATE_IDLE) ? 1U : 0U;
}

void touch_calib_cancel(void)
{
    /* Deliberately does NOT touch touch_set_calibration() - whatever
     * was active before touch_calib_start() stays in effect, and
     * s_on_done is NOT called (that callback means "a GOOD calibration
     * was applied", which didn't happen here). The caller is
     * responsible for repainting whatever screen was showing before
     * the wizard took over the panel, same as screen_wake() does for
     * screen_sleep_enter(). */
    s_state = CAL_STATE_IDLE;
}

void touch_calib_poll(void)
{
    switch (s_state) {

    case CAL_STATE_ARM:
        if (!touch_is_pressed()) {
            s_state = CAL_STATE_WAIT_PRESS;
        }
        break;

    case CAL_STATE_WAIT_PRESS:
        if (touch_is_pressed()) {
            s_press_start_ms = g_msticks;
            s_state = CAL_STATE_DEBOUNCE;
        }
        break;

    case CAL_STATE_DEBOUNCE:
        if (!touch_is_pressed()) {
            s_state = CAL_STATE_WAIT_PRESS; /* released before the debounce window elapsed - bounce, try again */
            break;
        }
        if ((g_msticks - s_press_start_ms) >= CAL_DEBOUNCE_MS) {
            uint16_t rx, ry;
            if (touch_read_raw(&rx, &ry)) {
                s_raw_x[s_point_idx] = rx;
                s_raw_y[s_point_idx] = ry;
                draw_point_captured(s_point_idx);
                s_state = CAL_STATE_WAIT_RELEASE;
            } else {
                /* Contact lifted at the exact instant of the SPI
                 * transaction (touch_read_raw() re-checks PENIRQ
                 * before returning) - just retry from a fresh press. */
                s_state = CAL_STATE_WAIT_PRESS;
            }
        }
        break;

    case CAL_STATE_WAIT_RELEASE:
        if (!touch_is_pressed()) {
            s_point_idx++;
            if (s_point_idx < CAL_NUM_POINTS) {
                draw_point_prompt(s_point_idx);
                s_state = CAL_STATE_ARM;
            } else if (compute_calibration(&s_result_cal)) {
                touch_set_calibration(&s_result_cal);
                print_calibration_uart(&s_result_cal);
                draw_result_screen(&s_result_cal);
                s_state = CAL_STATE_RESULT_ARM;
            } else {
                draw_bad_message();
                s_bad_until_ms = g_msticks + CAL_BAD_MSG_MS;
                s_state = CAL_STATE_BAD_WAIT;
            }
        }
        break;

    case CAL_STATE_BAD_WAIT:
        if (g_msticks >= s_bad_until_ms) {
            s_point_idx = 0U;
            draw_point_prompt(0U);
            s_state = CAL_STATE_ARM;
        }
        break;

    case CAL_STATE_RESULT_ARM:
        if (!touch_is_pressed()) {
            s_state = CAL_STATE_RESULT_WAIT_TAP;
        }
        break;

    case CAL_STATE_RESULT_WAIT_TAP:
        if (touch_is_pressed()) {
            touch_calib_done_cb_t cb = s_on_done;
            touch_calibration_t cal = s_result_cal;
            s_state = CAL_STATE_IDLE; /* set BEFORE the callback - see touch_calib_active()'s comment: the caller is allowed to redraw its own full screen from inside on_done() */
            if (cb != NULL) {
                cb(&cal);
            }
        }
        break;

    case CAL_STATE_IDLE:
    default:
        break;
    }
}
