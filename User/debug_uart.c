#include "debug_uart.h"

#if DEBUG_UART_ENABLED
#include "usb_serial.h" /* debug_print() also mirrors to the USB CDC-ACM port (09/2026), per the project owner: useful when no USB-TTL adapter is on hand but the board's own USB connector is already plugged in */
/* Whole file compiles to nothing when DEBUG_UART_ENABLED is 0 - see
 * debug_uart.h's comment. Keeps PA9/USART0 untouched (no GPIO AF
 * config, no clock enable) rather than just skipping the prints. */

/* ISR HARD SILENCE (16/09/2026, real-hardware UART corruption found
 * with the HFDL module - see hfdl_payload_decode.c's internal
 * debug_print*_always() calls). Different from s_quiet (debug_uart.h's
 * existing quiet mode): quiet mode is a VOLUME control that the
 * _always() variants deliberately bypass by design (see their own
 * comment - they're the lines meant to survive quiet mode). This is
 * the opposite: a small number of call sites (hfdl_payload_decode.c's
 * internal prints, called from inside the audio ISR via
 * demod_am.c's hfdl_payload_decode_begin_segment()/finish() calls)
 * need to be fully silenced - EVEN the _always ones - for the
 * specific duration of that one ISR-context call, to avoid the same
 * ISR-vs-main-loop uart_putc() reentrancy corruption already found and
 * fixed once for this module's own top-level summary prints (see
 * demod_am_hfdl_probe_debug_poll()'s deferral pattern) - that fix
 * doesn't cover hfdl_payload_decode.c's OWN internal prints, which
 * this project intentionally never modifies for delicate protocol-
 * parsing code. Checked once, at the bottom of debug_print() (which
 * every other debug_print*() function/alias funnels through for its
 * actual character output), so one guard covers all of them. */
static volatile uint8_t s_isr_silence = 0U;

void debug_uart_isr_silence_begin(void)
{
    s_isr_silence = 1U;
}

void debug_uart_isr_silence_end(void)
{
    s_isr_silence = 0U;
}

/* QUIET MODE (16/09/2026, ported properly with the HFDL module - see
 * debug_uart.h's comment. This project's own ~238 other debug_print()
 * call sites (waterfall ticks, smeter dBFS, block budget cycle
 * breakdowns, etc.) drown out the handful of HFDL lines that matter
 * during a focused test session - debug_uart_set_quiet(1) silences
 * all of THOSE (they funnel through the quiet-respecting debug_print()
 * below) while every debug_print*_always() call (this module's own
 * summaries) keeps printing, via debug_print_raw() below, which never
 * checks s_quiet. Independent of s_isr_silence above - that one
 * silences EVERYTHING, even _always(), for a specific brief ISR-only
 * window; this one silences only the ordinary (non-always) call
 * sites, for as long as the caller wants. Default 0 (normal,
 * unchanged behaviour for anyone who never calls this). */
static uint8_t s_quiet = 0U;

void debug_uart_set_quiet(uint8_t quiet)
{
    s_quiet = quiet;
}

void debug_uart_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART0);

    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_9);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);
    gpio_af_set(GPIOA, GPIO_AF_7, GPIO_PIN_9);

    usart_deinit(USART0);
    usart_baudrate_set(USART0, 115200U);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT);
    usart_parity_config(USART0, USART_PM_NONE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_enable(USART0);
}

static void uart_putc(char c)
{
    if (s_isr_silence) {
        return; /* see s_isr_silence's own comment - the true single choke point:
                  * debug_print_hex32/hex16's digit loops call uart_print_hex_nibble()
                  * -> uart_putc() directly, bypassing debug_print() entirely, so the
                  * guard belongs here, not there. */
    }
    while (usart_flag_get(USART0, USART_FLAG_TBE) == RESET) {
    }
    usart_data_transmit(USART0, (uint8_t)c);
}

/*
 * USB CDC-ACM mirror (09/2026) - per the project owner's request:
 * they were debugging the FT8 slot-alignment issue without a USB-TTL
 * adapter on hand, but the board's own USB connector (already needed
 * for firmware updates - see usb_serial.h) was right there. Builds
 * the FULL \r\n-converted line into a small local buffer, then sends
 * it in <=USB_DEBUG_CHUNK_MAX-byte pieces (retrying/polling between
 * attempts, since usb_serial_write() is non-blocking and can report
 * "previous transfer still in flight") until it succeeds or a bound
 * is hit. Silently drops what's left of the line if the host never
 * enumerated (usb_serial_is_ready()==0) - a debug aid shouldn't
 * itself be allowed to hang the firmware waiting for a PC that never
 * opens the port.
 *
 * REAL BUG FIXED HERE (09/2026, per the project owner: "no veo
 * salidas por el usb"): the first version of this mirror called
 * usb_serial_write() with the ENTIRE line in one shot. usb_serial.c's
 * own usb_serial_write() silently rejects (returns "busy", the same
 * code as a transient one) anything over USB_CDC_DATA_PACKET_SIZE (64
 * bytes, USB/conf/usbd_conf.h - duplicated here as
 * USB_DEBUG_CHUNK_MAX since that header isn't part of usb_serial.h's
 * public surface and shouldn't need to be pulled into this file just
 * for one constant; keep the two in sync if that ever changes) - ANY
 * debug_print() call whose line (or, worse, whose full accumulated
 * multi-call line before this fix split things up) exceeded 64 bytes
 * would spin through every retry attempt and then get silently
 * dropped, every single time, indistinguishable from "nothing is
 * connected at all". Chunking fixes this for lines of any length.
 */
#define USB_DEBUG_CHUNK_MAX 64U /* MUST match USB/conf/usbd_conf.h's USB_CDC_DATA_PACKET_SIZE - see comment above */
#define DEBUG_USB_LINE_MAX 160U
#define DEBUG_USB_WRITE_RETRY_MAX 200U /* bounded defensively (not the original 20000 - see the project owner's own report this could otherwise spin needlessly long per call if the host side ever stops draining) */
static void usb_debug_flush(const char *buf, uint16_t len)
{
    uint16_t offset = 0U;

    if (!usb_serial_is_ready()) {
        return; /* no PC has opened the port - nothing to send to, drop it rather than block */
    }

    while (offset < len) {
        uint16_t chunk_len = (uint16_t)(len - offset);
        uint32_t attempts;
        if (chunk_len > USB_DEBUG_CHUNK_MAX) {
            chunk_len = USB_DEBUG_CHUNK_MAX;
        }

        for (attempts = 0U; attempts < DEBUG_USB_WRITE_RETRY_MAX; attempts++) {
            if (usb_serial_write((const uint8_t *)&buf[offset], chunk_len) == 0U) {
                break; /* this chunk queued successfully - move on to the next one below */
            }
            usb_serial_poll(); /* let the previous IN transfer actually complete before retrying */
        }
        if (attempts >= DEBUG_USB_WRITE_RETRY_MAX) {
            return; /* gave up on this chunk - drop the rest of the line too rather than send it out of order */
        }

        offset = (uint16_t)(offset + chunk_len);
    }
}

void debug_print(const char *s)
{
    char usb_buf[DEBUG_USB_LINE_MAX];
    uint16_t usb_len = 0U;

    if (s_isr_silence || s_quiet) {
        return; /* s_isr_silence: see its own comment, hard mute even for _always() callers
                  * (not relevant here since THIS is the quiet-respecting path anyway).
                  * s_quiet: this project's ordinary call sites - see s_quiet's own comment. */
    }

    while (*s) {
        char c = *s;
        if (c == '\n') {
            uart_putc('\r');
            if (usb_len < DEBUG_USB_LINE_MAX) { usb_buf[usb_len++] = '\r'; }
        }
        uart_putc(c);
        if (usb_len < DEBUG_USB_LINE_MAX) { usb_buf[usb_len++] = c; }
        s++;
    }

    usb_debug_flush(usb_buf, usb_len);
}

/* Unconditional raw writer (16/09/2026) - bypasses s_quiet entirely
 * (but NOT s_isr_silence, via uart_putc() - see that flag's own
 * comment: it silences EVERYTHING, _always() included, for its brief
 * ISR-only window). debug_print_always() and friends funnel through
 * this instead of debug_print() so this project's quiet mode (added
 * alongside this) doesn't accidentally swallow HFDL's own summary
 * lines the way it's meant to swallow everything else. */
static void debug_print_raw(const char *s)
{
    char usb_buf[DEBUG_USB_LINE_MAX];
    uint16_t usb_len = 0U;

    while (*s) {
        char c = *s;
        if (c == '\n') {
            uart_putc('\r');
            if (usb_len < DEBUG_USB_LINE_MAX) { usb_buf[usb_len++] = '\r'; }
        }
        uart_putc(c);
        if (usb_len < DEBUG_USB_LINE_MAX) { usb_buf[usb_len++] = c; }
        s++;
    }

    usb_debug_flush(usb_buf, usb_len);
}

static void uart_print_hex_nibble(uint8_t nibble)
{
    if (s_quiet) {
        return; /* see s_quiet's own comment - debug_print_hex32/hex16's nibble loop
                  * calls this directly, bypassing debug_print(), so needs its own check
                  * to actually respect quiet mode (this project's ~238 other call sites
                  * include a few hex32/hex16 dumps). */
    }
    if (nibble < 10) {
        uart_putc('0' + nibble);
    } else {
        uart_putc('A' + (nibble - 10));
    }
}

void debug_print_hex32(const char *label, uint32_t val)
{
    debug_print(label);
    debug_print(" = 0x");
    for (int i = 7; i >= 0; i--) {
        uart_print_hex_nibble((val >> (i * 4)) & 0xF);
    }
    debug_print("\n");
}

void debug_print_hex16(const char *label, uint16_t val)
{
    debug_print(label);
    debug_print(" = 0x");
    for (int i = 3; i >= 0; i--) {
        uart_print_hex_nibble((val >> (i * 4)) & 0xF);
    }
    debug_print("\n");
}

void debug_print_dec(const char *label, uint32_t val)
{
    char buf[11];
    int i = 10;
    buf[10] = '\0';

    if (val == 0) {
        buf[--i] = '0';
    } else {
        while (val > 0 && i > 0) {
            buf[--i] = '0' + (val % 10);
            val /= 10;
        }
    }

    debug_print(label);
    debug_print(" = ");
    debug_print(&buf[i]);
    debug_print("\n");
}

/* _always() variants (16/09/2026) - now REAL bypasses of s_quiet, via
 * debug_print_raw() - see that function's own comment. Before quiet
 * mode existed in this branch, these were plain aliases to the
 * ordinary functions (see the git history/conversation for why); now
 * that s_quiet exists, aliasing would defeat the whole point of
 * "_always" (HFDL's own summaries would go silent along with
 * everything else the moment someone calls debug_uart_set_quiet(1)). */
void debug_print_always(const char *s) { debug_print_raw(s); }

void debug_print_hex32_always(const char *label, uint32_t val)
{
    debug_print_raw(label);
    debug_print_raw(" = 0x");
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (val >> (i * 4)) & 0xF;
        char c = (nibble < 10) ? (char)('0' + nibble) : (char)('A' + (nibble - 10));
        char buf[2] = { c, '\0' };
        debug_print_raw(buf);
    }
    debug_print_raw("\n");
}

void debug_print_dec_always(const char *label, uint32_t val)
{
    char buf[11];
    int i = 10;
    buf[10] = '\0';

    if (val == 0) {
        buf[--i] = '0';
    } else {
        while (val > 0 && i > 0) {
            buf[--i] = (char)('0' + (val % 10));
            val /= 10;
        }
    }

    debug_print_raw(label);
    debug_print_raw(" = ");
    debug_print_raw(&buf[i]);
    debug_print_raw("\n");
}

/* Fixed-point float print (ported 16/09/2026 with the HFDL module,
 * verbatim from the HFDL branch's debug_uart.c - see debug_uart.h's
 * comment). uart_print_udec() is a local helper, same shape as
 * debug_print_dec()'s own digit loop but returning through
 * debug_print_always() so it composes with the sign/decimal-point
 * pieces below without an extra buffer. */
static void uart_print_udec(uint32_t val)
{
    char buf[11];
    int i = 10;
    buf[10] = '\0';

    if (val == 0) {
        buf[--i] = '0';
    } else {
        while (val > 0 && i > 0) {
            buf[--i] = (char)('0' + (val % 10));
            val /= 10;
        }
    }
    debug_print_always(&buf[i]);
}

void debug_print_float_always(const char *label, float val)
{
    uint8_t negative = 0U;
    uint32_t frac_digits;
    float scale;
    uint32_t scaled, divisor, int_part, frac_part, pad, d;

    if (val != val) { /* portable NaN check - NaN never equals itself */
        debug_print_always(label);
        debug_print_always(" = NaN\n");
        return;
    }
    if (val > 3.0e38f) {
        debug_print_always(label);
        debug_print_always(" = +Inf (or huge)\n");
        return;
    }
    if (val < -3.0e38f) {
        debug_print_always(label);
        debug_print_always(" = -Inf (or huge negative)\n");
        return;
    }

    if (val < 0.0f) {
        negative = 1U;
        val = -val;
    }

    /* Adaptive fractional-digit count: a fixed 1e6 scale silently
     * saturates the uint32_t cast below for any val >= ~4294.967296 -
     * drop fractional digits as needed (down to a floor of 0) so the
     * integer part stays correct, trading decimal precision only when
     * the magnitude actually needs the headroom. */
    frac_digits = 6u;
    scale = 1000000.0f;
    while (frac_digits > 0u && val > (4290000000.0f / scale)) {
        frac_digits--;
        scale *= 0.1f;
    }

    scaled = (uint32_t)(val * scale + 0.5f);
    divisor = 1u;
    for (d = 0u; d < frac_digits; d++) {
        divisor *= 10u;
    }
    int_part = scaled / divisor;
    frac_part = scaled % divisor;

    debug_print_always(label);
    debug_print_always(" = ");
    if (negative) {
        debug_print_always("-");
    }
    uart_print_udec(int_part);
    if (frac_digits > 0u) {
        debug_print_always(".");
        pad = divisor / 10u;
        while (pad > 1u && frac_part < pad) {
            debug_print_always("0");
            pad /= 10u;
        }
        uart_print_udec(frac_part);
    }
    debug_print_always("\n");
}

#endif /* DEBUG_UART_ENABLED */
