#ifndef TIME_SYNC_UART_H
#define TIME_SYNC_UART_H

#include <stdint.h>
#include "debug_uart.h" /* for the DEBUG_UART_ENABLED flag - see this file's own comment */

/*
 * ESP32 -> GD32 time sync link, over the SAME physical pins as
 * debug_uart.c's debug channel (USART0, PA9=TX/PA10=RX, AF7 - PA10
 * confirmed unused anywhere else in this project). The two uses are
 * MUTUALLY EXCLUSIVE, never simultaneous, mirroring debug_uart.h's
 * own stub-when-disabled pattern:
 *
 *   - DEBUG_UART_ENABLED=1 (bring-up/debug session): debug_uart.c owns
 *     PA9 for polled TX prints, as it always has. This module compiles
 *     away to nothing (no GPIO/USART0 config from this file at all).
 *   - DEBUG_UART_ENABLED=0 (production, the default): this module owns
 *     USART0/PA9/PA10 instead, running the framed binary protocol
 *     described in time_sync_uart.c against an external ESP32 that
 *     gets real time via NTP over WiFi.
 *
 * This split exists because debug_uart.c's own comment already flags
 * PA9 toggling as a suspected RF noise source into this board's HF
 * front-end. The time-sync protocol is deliberately low-traffic (one
 * frame at boot, then an occasional resync - see time_sync_uart.c) to
 * keep that exposure far below what the old continuous debug prints
 * used to generate.
 */
#if !DEBUG_UART_ENABLED

/* Configures PA9/PA10 as USART0 TX/RX and enables the RX interrupt.
 * Call once, early in main() - after rtc_hw_init(), since an early
 * FULL_SET frame arriving before the RTC is up would be dropped. */
void time_sync_uart_init(void);

/* Call from the main loop. Cheap when idle - the actual frame
 * parsing/RTC update (rtc_hw_set()/rtc_hw_apply_shift()) only runs
 * once a complete, CRC-valid frame has been assembled by the RX
 * interrupt handler. */
void time_sync_uart_poll(void);

/* Diagnostics: count of frames rejected for a bad CRC or a malformed
 * marker/length, since boot. Wire to a settings/debug screen if
 * useful; not required for the protocol to function. */
uint32_t time_sync_uart_get_error_count(void);

#else

#define time_sync_uart_init()              ((void)0)
#define time_sync_uart_poll()              ((void)0)
#define time_sync_uart_get_error_count()   (0U)

#endif /* !DEBUG_UART_ENABLED */

#endif /* TIME_SYNC_UART_H */
