#include "debug_uart.h"

#if DEBUG_UART_ENABLED
#include "usb_serial.h" /* debug_print() also mirrors to the USB CDC-ACM port (09/2026), per the project owner: useful when no USB-TTL adapter is on hand but the board's own USB connector is already plugged in */
/* Whole file compiles to nothing when DEBUG_UART_ENABLED is 0 - see
 * debug_uart.h's comment. Keeps PA9/USART0 untouched (no GPIO AF
 * config, no clock enable) rather than just skipping the prints. */

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

#endif /* DEBUG_UART_ENABLED */
