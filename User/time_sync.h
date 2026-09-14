#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdint.h>

/*
 * ESP32 -> GD32 time sync link, now carried over the USB CDC-ACM
 * virtual COM port (see usb_serial.h) instead of USART0/PA9/PA10.
 *
 * This replaces time_sync_uart.c/.h outright - the ESP32 is rewired
 * to the USB connector, PA9/PA10 are no longer used by this link at
 * all. That also removes the old mutual-exclusion problem with
 * debug_uart.c (DEBUG_UART_ENABLED and this module can now both be
 * on at the same time, since they no longer share pins), and drops
 * debug_uart.h's suspected PA9-toggling RF noise source from this
 * path entirely.
 *
 * Frame format on the wire is UNCHANGED from the old UART link
 * (same bytes, just arriving as CDC data instead of USART0 RX) - see
 * time_sync.c's header comment for the exact layout. An ESP32 that
 * already speaks the old protocol only needs its transport swapped
 * from a UART peripheral to a USB serial port; the frame bytes it
 * sends don't change.
 */

/* Call once, early in main() - after both rtc_hw_init() (an early
 * FULL_SET frame arriving before the RTC is up would be dropped) and
 * usb_serial_init() (so the CDC port already exists to read from). */
void time_sync_init(void);

/* Call from the main loop, unconditionally (same spot as
 * usb_serial_poll()/rf_agc_poll()). Cheap when idle - the actual
 * frame parsing/RTC update (rtc_hw_set()/rtc_hw_apply_shift()) only
 * runs once a complete, CRC-valid frame has arrived over USB. */
void time_sync_poll(void);

/* Diagnostics: count of frames rejected for a bad CRC or a malformed
 * marker/length, since boot. Wire to a settings/debug screen if
 * useful; not required for the protocol to function. */
uint32_t time_sync_get_error_count(void);

#endif /* TIME_SYNC_H */
