#include "time_sync.h"

#include "gd32f4xx.h"
#include "rtc_hw.h"
#include "usb_serial.h"
#include "ft8_decoder.h" /* ft8_decoder_set_own_grid() - MSG_SET_GRID, see its own comment below */
#include "settings.h"    /* settings_mark_dirty() - persists the grid to CONFIG.CSV once set via MSG_SET_GRID */

/*
 * Frame format (little-endian multi-byte fields), sent by the ESP32 -
 * byte-for-byte identical to the old time_sync_uart.c protocol, just
 * arriving over the USB CDC-ACM port (usb_serial_read()) instead of
 * USART0's RX interrupt:
 *
 *   [0]      0xA5              start marker
 *   [1]      length            payload length in bytes (MSG_*_LEN below)
 *   [2]      msg_type          MSG_FULL_SET, MSG_SHIFT, or MSG_SET_GRID
 *   [3..]    payload           see per-type layout below
 *   [3+len]  crc8              CRC8-CCITT (poly 0x07, init 0x00) over
 *                               msg_type + payload only (not the
 *                               marker/length/end-marker bytes)
 *   [4+len]  0x5A              end marker
 *
 * MSG_FULL_SET (0x01), payload = 7 bytes: year_lo, year_hi, month, day,
 * hour, minute, second. Used for the ESP32's first sync right after
 * its own boot/NTP fetch, where the GD32's calendar may be off by
 * anything (default epoch) - goes through rtc_hw_set(), a hard set.
 *
 * MSG_SHIFT (0x02), payload = 3 bytes: add_one_second (0/1),
 * subsecond_fraction_lo, subsecond_fraction_hi. Used for routine
 * resyncs once the calendar is already close - goes through
 * rtc_hw_apply_shift(), which does NOT disturb the running calendar.
 * See rtc_hw.h for the +/-1s range this covers per call.
 *
 * MSG_SET_GRID (0x03, 09/2026) - payload = 4 or 6 bytes: the Maidenhead
 * grid locator itself as ASCII, NOT NUL-terminated (length comes from
 * the frame's own [1] byte, same as every other message type here) -
 * e.g. "IL18" or "IL18vl". Added per the project owner's request to
 * set/update FT8's distance-to-grid own-QTH square from the same PC-
 * side tool used for time sync (time_sync_sdr101.py), rather than
 * needing a recompile or a separate on-screen keypad for a value that
 * only ever changes if the operator physically moves - see
 * ft8_decoder_set_own_grid()'s own comment for the validation this
 * goes through (a malformed grid is rejected there, not here) and
 * grid_to_latlon()'s own explicit "RR73" guard. Persisted immediately
 * via settings_mark_dirty() (debounced, same as every other CONFIG.CSV
 * field) rather than waiting for some unrelated setting to also
 * change and drag this one along as a side effect.
 */
#define FRAME_START   0xA5U
#define FRAME_END     0x5AU
#define MSG_FULL_SET  0x01U
#define MSG_SHIFT     0x02U
#define MSG_SET_GRID  0x03U
#define MSG_FULL_SET_LEN 7U
#define MSG_SHIFT_LEN    3U
#define MSG_SET_GRID_LEN_4 4U
#define MSG_SET_GRID_LEN_6 6U
#define MSG_PAYLOAD_MAX  MSG_FULL_SET_LEN /* largest of all three (7 >= 6 >= 4 >= 3) */

/* Small RX ring buffer, filled from usb_serial_read() at the top of
 * time_sync_poll() instead of an ISR - USB reception is already
 * buffered upstream by usb_serial.c's own ring (see its header), so
 * there's no byte-loss risk in only draining it once per main loop
 * pass. Frames are short (at most 5+MSG_PAYLOAD_MAX = 12 bytes) and
 * infrequent (see time_sync.h's header comment on why traffic is kept
 * low), so 64 bytes is ample even if a couple of frames land
 * back-to-back. */
#define RX_RING_SIZE 64U
static uint8_t  s_rx_ring[RX_RING_SIZE];
static uint16_t s_rx_head;
static uint16_t s_rx_tail;

static uint32_t s_error_count;

static uint8_t crc8_ccitt(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0x00U;

    for (uint16_t i = 0U; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            crc = (uint8_t)((crc & 0x80U) ? ((crc << 1) ^ 0x07U) : (crc << 1));
        }
    }

    return crc;
}

static uint16_t rx_available(void)
{
    return (uint16_t)((s_rx_head + RX_RING_SIZE - s_rx_tail) % RX_RING_SIZE);
}

static uint8_t rx_peek(uint16_t offset)
{
    return s_rx_ring[(s_rx_tail + offset) % RX_RING_SIZE];
}

static void rx_drop(uint16_t n)
{
    s_rx_tail = (uint16_t)((s_rx_tail + n) % RX_RING_SIZE);
}

/* Pulls whatever usb_serial.c already has buffered into this module's
 * own ring, one byte at a time so the "ring full" case degrades the
 * same way the old ISR-fed version did: drop the newest byte and let
 * the CRC check downstream reject whatever frame that corrupts, then
 * resync on the next start marker. */
static void rx_fill_from_usb(void)
{
    uint8_t byte;

    while (1U == usb_serial_read(&byte, 1U))
    {
        uint16_t next_head = (uint16_t)((s_rx_head + 1U) % RX_RING_SIZE);

        if (next_head == s_rx_tail)
        {
            break; /* ring full - leave the byte in usb_serial's own buffer momentarily lost */
        }

        s_rx_ring[s_rx_head] = byte;
        s_rx_head = next_head;
    }
}

static void handle_full_set(const uint8_t *payload)
{
    rtc_hw_datetime_t dt;

    dt.year   = (uint16_t)(payload[0] | ((uint16_t)payload[1] << 8));
    dt.month  = payload[2];
    dt.day    = payload[3];
    dt.hour   = payload[4];
    dt.minute = payload[5];
    dt.second = payload[6];

    rtc_hw_set(&dt);

    /* Visual bring-up confirmation: PA8 (led_gpio_init() in main.c)
     * exists but nothing else in this project ever drives it - toggle
     * it here so a synced ESP32 link is visible without needing
     * debug_print(). Toggling (not just setting) means you'll see it
     * flip once per FULL_SET even across repeated resyncs, not just
     * once ever. */
    gpio_bit_write(GPIOA, GPIO_PIN_8, (bit_status)!gpio_output_bit_get(GPIOA, GPIO_PIN_8));
}

static void handle_shift(const uint8_t *payload)
{
    bool add_one_second = (payload[0] != 0U);
    uint16_t subsecond_fraction = (uint16_t)(payload[1] | ((uint16_t)payload[2] << 8));

    (void)rtc_hw_apply_shift(add_one_second, subsecond_fraction); /* "since last sync" tracking now lives in rtc_hw.c itself (rtc_hw_has_ever_synced()/rtc_hw_get_seconds_since_sync()) - see rtc_hw_set()'s and rtc_hw_apply_shift()'s own comments there for why that's the right place for it (persists across a firmware reset that doesn't also take VBAT down, unlike anything tracked here in RAM) */
}

/* See this file's own header comment on MSG_SET_GRID for the full
 * "why". Deliberately does not touch anything RTC-related - setting
 * the grid says nothing about whether the CLOCK is currently
 * trustworthy. */
static void handle_set_grid(const uint8_t *payload, uint8_t len)
{
    ft8_decoder_set_own_grid((const char *)payload, (int)len);
    settings_mark_dirty(); /* persist to CONFIG.CSV - debounced, same as every other settings field */
}

void time_sync_init(void)
{
    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_error_count = 0U;
}

void time_sync_poll(void)
{
    rx_fill_from_usb();

    /* Look for a start marker; drop anything before it (resync after
     * noise/framing loss, or a stray byte from something else the
     * host application might send down the same COM port later). */
    while (rx_available() > 0U && rx_peek(0) != FRAME_START)
    {
        rx_drop(1U);
        s_error_count++;
    }

    if (rx_available() < 3U) /* need at least start+length+type to know how much more to wait for */
    {
        return;
    }

    uint8_t len  = rx_peek(1);
    uint8_t type = rx_peek(2);
    uint16_t frame_total = (uint16_t)(3U + len + 1U + 1U); /* start+len+type + payload + crc + end */

    if (len > MSG_PAYLOAD_MAX || frame_total > RX_RING_SIZE)
    {
        rx_drop(1U); /* garbage length byte - resync one byte at a time */
        s_error_count++;
        return;
    }

    if (rx_available() < frame_total)
    {
        return; /* full frame hasn't arrived yet */
    }

    uint8_t payload[MSG_PAYLOAD_MAX];
    for (uint8_t i = 0U; i < len; i++)
    {
        payload[i] = rx_peek((uint16_t)(3U + i));
    }
    uint8_t rx_crc = rx_peek((uint16_t)(3U + len));
    uint8_t rx_end = rx_peek((uint16_t)(3U + len + 1U));

    if (rx_end != FRAME_END)
    {
        rx_drop(1U); /* frame slipped - resync one byte at a time rather than dropping the whole thing blind */
        s_error_count++;
        return;
    }

    uint8_t crc_buf[1U + MSG_PAYLOAD_MAX];
    crc_buf[0] = type;
    for (uint8_t i = 0U; i < len; i++)
    {
        crc_buf[1U + i] = payload[i];
    }

    if (crc8_ccitt(crc_buf, (uint16_t)(1U + len)) != rx_crc)
    {
        rx_drop(1U);
        s_error_count++;
        return;
    }

    rx_drop(frame_total); /* consume the whole validated frame */

    switch (type)
    {
    case MSG_FULL_SET:
        if (len == MSG_FULL_SET_LEN)
        {
            handle_full_set(payload);
        }
        break;
    case MSG_SHIFT:
        if (len == MSG_SHIFT_LEN)
        {
            handle_shift(payload);
        }
        break;
    case MSG_SET_GRID:
        if (len == MSG_SET_GRID_LEN_4 || len == MSG_SET_GRID_LEN_6)
        {
            handle_set_grid(payload, len);
        }
        break;
    default:
        s_error_count++;
        break;
    }
}

uint32_t time_sync_get_error_count(void)
{
    return s_error_count;
}
