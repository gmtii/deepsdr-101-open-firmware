/*
 * usb_serial.h - USB CDC-ACM virtual COM port for the DeepSDR 101.
 *
 * Wraps the GD32F4xx USB device library (USB/ in this tree) into a
 * small, project-friendly API: init once at boot, poll once per main
 * loop pass (same pattern as time_sync_poll()/rf_agc_poll()),
 * read/write bytes from wherever a command parser needs them.
 *
 * HARDWARE NOTE: the vendor CDC_ACM demo's delay unit (usb_udelay()/
 * usb_mdelay(), used during enumeration) is normally built on TIMER2.
 * On this board TIMER2 is already fully owned by lo_gen_gd32.c (the
 * GD32-generated quadrature LO for tuning below 300kHz, PA6/PA7) -
 * reusing TIMER2 here would silently reconfigure it out from under
 * the LO every time the delay routine runs. This module's hardware
 * layer (usb_serial_hw.c) uses TIMER5 instead - it's a basic timer
 * (APB1, no GPIO/capture-compare channels), free in this project, and
 * doesn't care about being reused as a plain time base.
 *
 * Comments in English, README-style - shared with other ham radio
 * operators via the project's open firmware repo.
 */

#ifndef USB_SERIAL_H
#define USB_SERIAL_H

#include <stdint.h>

/* One-time init: clocks, GPIO (PA11/PA12), NVIC, USB device core +
 * CDC-ACM class. Call once during boot, alongside the other
 * peripheral _init() calls in main() - order doesn't matter much,
 * but doing it after debug_uart_init() (so early failures are still
 * logged) and before the main loop starts is the natural spot. */
void usb_serial_init(void);

/* Call once per main loop pass, unconditionally (same spot as
 * time_sync_poll()/rf_agc_poll() - NOT inside the screen-asleep/
 * touch-calibration branches, so the virtual COM port keeps working
 * regardless of what the display is doing). Drains any bytes received
 * from the host into the internal RX ring buffer and re-arms the next
 * OUT transfer. Cheap when idle (a couple of flag checks). */
void usb_serial_poll(void);

/* Returns 1 once the host has enumerated the device and the CDC
 * interface is configured (i.e. a terminal/program has actually
 * opened the COM port on the PC side - or at least the OS has bound
 * it). Callers should gate any "send unsolicited data" logic on this,
 * since writes before enumeration are simply dropped. */
uint8_t usb_serial_is_ready(void);

/* Copy up to maxlen received bytes into buf, oldest first. Returns
 * the number of bytes actually copied (0 if nothing pending). Safe to
 * call from the main loop only (not from an ISR). */
uint32_t usb_serial_read(uint8_t *buf, uint32_t maxlen);

/* Queue len bytes for transmission to the host. Returns 0 on success,
 * 1 if the previous IN transfer hasn't completed yet (try again next
 * poll) or if len is too large for the internal TX buffer. Non-
 * blocking - never spins waiting for the host to read. */
uint8_t usb_serial_write(const uint8_t *buf, uint16_t len);

#endif /* USB_SERIAL_H */
