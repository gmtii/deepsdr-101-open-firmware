#include "rtc_hw.h"
#include "gd32f4xx.h"
#include "debug_uart.h" /* debug_print() - see rtc_hw_init()'s LXTAL-failure surfacing */

/* Marker written to RTC_BKP0 once this driver has brought the RTC up
 * for the first time on this board. Arbitrary value, just needs to be
 * distinctive enough that it's implausible as a leftover BKP0 value
 * from anything else (there is nothing else in this project writing
 * to RTC backup registers). If this marker is missing, rtc_hw_init()
 * treats it as a first boot / power loss and reseeds the calendar. */
#define RTC_HW_INIT_MARKER 0x46543801U /* "FT8" + version nibble, arbitrary */

/* Async/sync prescalers for a 32.768kHz LXTAL -> exactly 1Hz:
 * ck_spre = ck_apre / (factor_syn+1), ck_apre = LXTAL / (factor_asyn+1).
 * 32768 / (127+1) = 256Hz apre; 256 / (255+1) = 1Hz spre. Standard
 * values for a 32.768kHz source - see gd32f4xx_rtc.c's own example
 * comments in the vendor driver. */
#define RTC_HW_FACTOR_ASYN 0x7FU  /* 127 */
#define RTC_HW_FACTOR_SYN  0xFFU  /* 255 - also rtc_hw_apply_shift()'s subsecond range */

/* Set by rtc_hw_set() every time it's called (ESP32 link, on-screen
 * keypad, or any future caller) - see rtc_hw_consume_dirty()'s own
 * comment in the header for why this exists. */
static volatile bool s_rtc_dirty;

/* See rtc_hw_get_set_seq()'s own comment in the header for the full
 * "why" this exists alongside s_rtc_dirty rather than replacing it:
 * s_rtc_dirty is a one-shot flag with a single implicit consumer
 * (whoever calls rtc_hw_consume_dirty() first each tick clears it for
 * everyone else); this counter lets any number of independent
 * consumers each track their own last-seen value with no risk of
 * stealing the event from one another. */
static volatile uint32_t s_rtc_set_seq;

/* Running total, in milliseconds, of every rtc_hw_apply_shift() call
 * that actually succeeded - see rtc_hw_get_cumulative_shift_ms()'s own
 * comment in the header for the full "why". Signed: add_one_second
 * moves the clock FORWARD (+1000ms), subsecond_fraction moves it
 * BACKWARD by a fraction of a second (the RTC hardware's own
 * subtract-only "shift" semantics - see rtc_second_adjust()'s vendor
 * documentation). Never reset - only ever accumulated - so any
 * consumer can diff against their own last-seen value, same pattern
 * as s_rtc_set_seq above but carrying a MAGNITUDE instead of just an
 * event marker. */
static volatile int32_t s_rtc_cumulative_shift_ms;

/* Set once in rtc_hw_init() - see its own comment for the full "why".
 * true means LXTAL never confirmed stable within the timeout, so the
 * RTC's clock source (and therefore every second/minute/hour reading
 * this driver produces) cannot be trusted to be accurate - exposed via
 * rtc_hw_lxtal_failed() so callers (main.c's status readout, etc.) can
 * surface this instead of silently trusting a possibly-wrong clock. */
static bool s_rtc_lxtal_failed;

bool rtc_hw_lxtal_failed(void)
{
    return s_rtc_lxtal_failed;
}

static uint8_t bin2bcd(uint8_t bin)
{
    return (uint8_t)(((bin / 10U) << 4) | (bin % 10U));
}

static uint8_t bcd2bin(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10U) + (bcd & 0x0FU));
}

/* RTC_JAN..RTC_DEC are defined as 0x01..0x12 (BCD-equivalent to binary
 * for 1-12, no letters involved) and RTC_MONDAY..RTC_SUNDAY as 0x01..
 * 0x07 - both ranges happen to be identical in BCD and binary, so no
 * conversion helper is needed for month; day_of_week isn't tracked by
 * rtc_hw_datetime_t at all (FT8/HFDL scheduling only cares about
 * time-of-day and date, not weekday) and is set to a fixed placeholder. */

static void rtc_hw_write_calendar(const rtc_hw_datetime_t *dt)
{
    rtc_parameter_struct init_struct;

    init_struct.year          = bin2bcd((uint8_t)(dt->year % 100U)); /* RTC only stores a 2-digit year */
    init_struct.month         = bin2bcd(dt->month);
    init_struct.date          = bin2bcd(dt->day);
    init_struct.day_of_week   = RTC_MONDAY; /* placeholder - not tracked, not used by FT8/HFDL timing */
    init_struct.hour          = bin2bcd(dt->hour);
    init_struct.minute        = bin2bcd(dt->minute);
    init_struct.second        = bin2bcd(dt->second);
    init_struct.factor_asyn   = RTC_HW_FACTOR_ASYN;
    init_struct.factor_syn    = RTC_HW_FACTOR_SYN;
    init_struct.am_pm         = RTC_AM;
    init_struct.display_format = RTC_24HOUR;

    rtc_init(&init_struct);

    /*
     * REAL BUG FIX (09/2026), per the project owner's report: FT8
     * would decode fine for exactly one capture window right after
     * manually setting the clock, then never again until the clock
     * was set again. rtc_current_time_get() (what rtc_hw_get() wraps)
     * reads the RTC's SHADOW registers, not the live counter directly
     * - after rtc_init() rewrites the calendar, those shadow registers
     * need to be explicitly resynchronized (RTC_STAT_RSYNF, via
     * rtc_register_sync_wait()) before they reliably reflect the
     * counter ticking forward again. Without this call, rtc_hw_get()
     * could keep returning the exact value just written, FROZEN,
     * indefinitely - which would make main.c's ":00/:15/:30/:45
     * boundary" check permanently true right after a manual set,
     * causing it to re-arm a new FT8 capture immediately after every
     * previous one finishes (completely unaligned to real UTC time)
     * instead of genuinely waiting ~15 minutes for the next real
     * boundary - exactly the free-running behavior the RTC-alignment
     * feature was built to prevent, silently reintroduced by a frozen
     * clock read rather than a scheduling bug. This affects EVERY
     * caller of rtc_hw_set() (this driver's own default-epoch seed at
     * first boot, the on-screen keypad, and previously the ESP32/UART
     * link, now USB) - fixing it here, once, covers all of them.
     */
    rtc_register_sync_wait();
}

void rtc_hw_init(void)
{
    ErrStatus lxtal_ready;

    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable(); /* unlock the backup domain (BDCTL) before touching LXTAL/RTCSRC */

    rcu_osci_on(RCU_LXTAL);
    /*
     * REAL BUG FIX (09/2026), per the project owner's own careful
     * measurement work: with the audio pipeline PROVEN exact (see
     * ft8_decimator_get_raw_sample_count()/demod_am_get_isr_call_count()'s
     * own comments - measured 12000.0Hz input, 375.0Hz ISR rate,
     * bang-on), FT8 slot captures STILL drifted by ~1 real second per
     * 15-second round - a ~6.67% error that can only be the RTC
     * itself running wrong, since everything audio-side checks out
     * exactly. rcu_osci_stab_wait() CAN fail (LXTAL_STARTUP_TIMEOUT,
     * gd32f4xx_rcu.c) - its return value was never checked here
     * before this fix, despite this file's own OLD comment claiming
     * it "blocks until the crystal is confirmed running" (it doesn't
     * - it can give up and return ERROR). If LXTAL never actually
     * stabilized (marginal crystal, load capacitors, PCB layout - a
     * real hardware possibility, not something firmware can fix
     * outright), rcu_rtc_clock_config(RCU_RTCSRC_LXTAL) below would
     * silently configure the RTC onto a clock source that was never
     * confirmed running cleanly - exactly the kind of thing that
     * could produce a real, consistent rate error like the one
     * measured, with zero visibility into WHY. This fix doesn't (and
     * firmware can't) force a marginal crystal to behave - what it
     * DOES do is stop hiding the failure: check the real result and
     * surface it via s_rtc_lxtal_failed/rtc_hw_lxtal_failed(), instead
     * of silently proceeding as if LXTAL were known-good when it
     * might not be.
     */
    lxtal_ready = rcu_osci_stab_wait(RCU_LXTAL);
    s_rtc_lxtal_failed = (lxtal_ready != SUCCESS);
    if (s_rtc_lxtal_failed)
    {
        debug_print("rtc_hw: *** LXTAL FAILED TO STABILIZE - RTC clock source may be running on an unconfirmed/unstable oscillator, timing WILL be unreliable ***\n");
    }

    rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);
    rcu_periph_clock_enable(RCU_RTC);

    if (RTC_BKP0 == RTC_HW_INIT_MARKER)
    {
        /* Calendar already running from a previous boot (warm reset,
         * reflash, etc.) - do NOT call rtc_init() again, it would
         * reset the running time. Nothing further to do here; the
         * clock-source/enable calls above are harmless on an
         * already-configured RTC. Still resync the shadow registers
         * (see rtc_hw_write_calendar()'s comment on
         * rtc_register_sync_wait() for the full "why") - re-enabling
         * RCU_RTC's APB interface after a reset is exactly the kind
         * of event that can leave a stale shadow-register read
         * sitting there until explicitly resynced. */
        rtc_register_sync_wait();
        return;
    }

    /* First boot (or the backup domain lost power) - reseed with a
     * placeholder epoch. The ESP32 is expected to overwrite this with
     * a real NTP time via rtc_hw_set() shortly after boot. */
    rtc_hw_datetime_t default_epoch = { 2026U, 1U, 1U, 0U, 0U, 0U };
    rtc_hw_write_calendar(&default_epoch);

    RTC_BKP0 = RTC_HW_INIT_MARKER;
}

void rtc_hw_get(rtc_hw_datetime_t *out)
{
    rtc_parameter_struct s;

    rtc_current_time_get(&s);

    out->year   = (uint16_t)(2000U + bcd2bin(s.year)); /* RTC only stores 2 digits - this board will not see year 2100 */
    out->month  = bcd2bin(s.month);
    out->day    = bcd2bin(s.date);
    out->hour   = bcd2bin(s.hour);
    out->minute = bcd2bin(s.minute);
    out->second = bcd2bin(s.second);
}

void rtc_hw_set(const rtc_hw_datetime_t *dt)
{
    rtc_hw_write_calendar(dt);
    RTC_BKP0 = RTC_HW_INIT_MARKER; /* in case this is also serving as the very first sync after a cold boot */
    s_rtc_dirty = true;
    s_rtc_set_seq++; /* see rtc_hw_get_set_seq()'s comment - independent of s_rtc_dirty above */
}

bool rtc_hw_consume_dirty(void)
{
    bool was_dirty = s_rtc_dirty;
    s_rtc_dirty = false;
    return was_dirty;
}

uint32_t rtc_hw_get_set_seq(void)
{
    return s_rtc_set_seq;
}

bool rtc_hw_apply_shift(bool add_one_second, uint16_t subsecond_fraction)
{
    uint32_t add   = add_one_second ? RTC_SHIFT_ADD1S_SET : RTC_SHIFT_ADD1S_RESET;
    uint32_t minus = (uint32_t)subsecond_fraction & RTC_SHIFTCTL_SFS; /* clamp to the 15-bit SFS field, though callers should stay <= RTC_HW_FACTOR_SYN (255) per the .h comment */
    bool ok = (rtc_second_adjust(add, minus) == SUCCESS);

    if (ok) {
        /* See rtc_hw_get_cumulative_shift_ms()'s own comment (header)
         * for the full "why" this is tracked here. RTC_HW_FACTOR_SYN+1
         * (256) subsecond steps span exactly 1.0s, per rtc_hw_init()'s
         * own prescaler derivation - so each step here is 1000/256 ms. */
        int32_t delta_ms = add_one_second ? 1000 : 0;
        delta_ms -= (int32_t)((subsecond_fraction * 1000U) / (RTC_HW_FACTOR_SYN + 1U));
        s_rtc_cumulative_shift_ms += delta_ms;
    }

    return ok;
}

int32_t rtc_hw_get_cumulative_shift_ms(void)
{
    return s_rtc_cumulative_shift_ms;
}
