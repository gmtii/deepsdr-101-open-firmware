#ifndef RTC_HW_H
#define RTC_HW_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Real-time clock bring-up on the GD32F450's on-chip RTC peripheral,
 * clocked from the board's real 32.768kHz crystal (LXTAL) - confirmed
 * present on this board, 09/2026. Nothing in Firmware/Source/gd32f4xx_rtc.c
 * was being called anywhere in User/ before this file: the vendor
 * driver was linked in but dormant.
 *
 * WHY LXTAL AND NOT IRC32K: IRC32K (the internal RC oscillator) drifts
 * on the order of 1% - at that rate the clock would walk off FT8's
 * +/-1s decode-window tolerance within minutes, requiring constant
 * ESP32 UART resyncs (undesirable - see rtc_hw.c's comment on why
 * resyncs should stay infrequent). A real 32.768kHz crystal drifts on
 * the order of a few tens of ppm, i.e. a few seconds per DAY - so a
 * resync every few hours (or once a day) keeps comfortable margin.
 *
 * PERSISTENCE ACROSS RESET: the RTC + its backup domain (including
 * RTC_BKP0, used here as an init marker) keep running across a warm
 * MCU reset as long as VDD stays up - only a full power loss (with no
 * VBAT coin cell backing the domain, which this board does not
 * appear to have per the schematic notes so far) resets it. rtc_hw_init()
 * checks the marker so a normal reset/reflash does NOT re-stomp a
 * calendar that's already ticking correctly.
 */

typedef struct
{
    uint16_t year;   /* full year, e.g. 2026 */
    uint8_t  month;  /* 1-12 */
    uint8_t  day;    /* 1-31 */
    uint8_t  hour;   /* 0-23 */
    uint8_t  minute; /* 0-59 */
    uint8_t  second; /* 0-59 */
} rtc_hw_datetime_t;

/* Enables the PMU/backup domain and LXTAL, selects LXTAL as the RTC
 * clock source, and brings the RTC peripheral up. If RTC_BKP0 does not
 * already hold this driver's init marker (i.e. this looks like a
 * cold/first boot, or a boot after a full power loss), the calendar
 * is reset to a default epoch (2026-01-01 00:00:00) and the marker is
 * written - the ESP32 is expected to correct this with a real NTP
 * time shortly after boot via rtc_hw_set(). If the marker IS already
 * present, the running calendar is left completely untouched (no
 * rtc_init() call at all) - only the clock-source/peripheral-enable
 * steps are (re)done, which are harmless no-ops on an already-running
 * RTC. Call once, early in main(), same place group as the other
 * *_init() calls. */
void rtc_hw_init(void);

/* True if LXTAL never confirmed stable during rtc_hw_init() - see its
 * own comment for the full "why". If this is true, every time/second
 * value this driver produces is running on an unconfirmed clock
 * source and cannot be trusted to be accurate - a real hardware
 * concern (marginal crystal/load caps/PCB), not something this
 * driver can fix, only surface. */
bool rtc_hw_lxtal_failed(void);

/* Reads the current calendar out of the RTC shadow registers. */
void rtc_hw_get(rtc_hw_datetime_t *out);

/* Full recalibration: reinitializes the RTC calendar to the given
 * date/time. This is a hard set (goes through rtc_init() again), so
 * it CAN produce a visible jump if the previous calendar was off by
 * more than a second or two - intended for the ESP32's very first
 * NTP sync right after boot (where the default epoch is wildly wrong
 * and a jump is expected/harmless), not for routine resyncs once the
 * clock is already disciplined. For routine drift correction once
 * the calendar is already close, use rtc_hw_apply_shift() instead. */
void rtc_hw_set(const rtc_hw_datetime_t *dt);

/* Routine drift correction without disturbing the running calendar,
 * wrapping rtc_second_adjust(). This is a SHIFT operation, not an
 * arbitrary +/-N seconds adjustment - per call you can add exactly one
 * whole second (add_one_second = true) and/or subtract a fraction of
 * a second (subsecond_fraction, 0 to the synchronous prescaler value
 * used in rtc_hw_init() - currently 255, so 0-255 represents 0 up to
 * just under 1.0s in ~3.9ms steps). Net range per call is therefore
 * about -1s to +1s. With a real LXTAL crystal this comfortably covers
 * a periodic resync's expected drift (low single-digit ppm -> a few
 * ms per hour). If a resync ever needs a correction bigger than that
 * (e.g. after a long power-off with the calendar reset to the default
 * epoch), the caller should use rtc_hw_set() instead. Returns false if
 * the hardware rejected the shift (e.g. shift-pending flag still set
 * from a previous call, or RTC not yet initialized). */
bool rtc_hw_apply_shift(bool add_one_second, uint16_t subsecond_fraction);

/* Returns true exactly once per rtc_hw_set() call (whether from the
 * ESP32 link, the on-screen keypad, or any future caller), then
 * clears back to false - a one-shot "the calendar just jumped"
 * signal. Added because time_display_draw()'s own periodic redraw in
 * main.c throttles by comparing uptime-minutes, which was a safe
 * proxy back when the displayed clock only ever advanced
 * monotonically with uptime - now that rtc_hw_set() can jump the
 * calendar to an arbitrary value at an arbitrary instant (an ESP32
 * FULL_SET landing mid-uptime-minute), that throttle can miss the
 * jump for up to a real minute, or indefinitely in any UI mode whose
 * periodic redraw pass doesn't run at all (e.g. RTTY mode skips
 * sdr_spectrum_waterfall_tick() entirely). Callers should OR this into
 * their own redraw-needed check, not use it as the ONLY condition -
 * see main.c's call site. */
bool rtc_hw_consume_dirty(void);

/* Monotonically incrementing counter, bumped once per rtc_hw_set()
 * call (never by rtc_hw_apply_shift()), never reset/cleared. Unlike
 * rtc_hw_consume_dirty() (a single global one-shot flag - see its own
 * comment: "Returns true exactly once... then clears back to false"),
 * this can have MULTIPLE INDEPENDENT CONSUMERS: each caller keeps its
 * own "last seq I saw" static variable and compares against the
 * current value returned here, so one consumer reading/clearing the
 * event can never cause a different consumer to miss it. Added
 * because status_bar_tick() was already consuming (and clearing) the
 * one-shot dirty flag for its own on-screen-clock redraw, which
 * silently swallowed the same event before any other subsystem (e.g.
 * FT8's RTC-seeded slot grid) ever got a chance to see it. */
uint32_t rtc_hw_get_set_seq(void);

#endif /* RTC_HW_H */
