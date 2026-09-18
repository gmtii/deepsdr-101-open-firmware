#ifndef DEBUG_UART_H
#define DEBUG_UART_H

#include "gd32f4xx.h"

/*
 * Debug por USART0: PA9 = TX (AF7), 115200 8N1 (o 57600 si a ti te
 * funciono a esa velocidad).
 *
 * MASTER SWITCH - DEBUG_UART_ENABLED
 * -----------------------------------
 * Every debug_print*() call in the project (238 call sites across 12
 * files, as of 06/08/2026) funnels through this one flag. Default is
 * OFF: the blocking, polled, character-at-a-time UART TX on PA9 (see
 * uart_putc() in debug_uart.c - it busy-waits on USART_FLAG_TBE per
 * byte) was suspected of both slowing things down and injecting RF
 * noise via synchronous digital switching on PA9, especially from the
 * periodic prints inside main()'s main loop (cycle-count/timing dumps
 * every ~1.5s) and the per-block diagnostics in demod_am.c/gd32_i2s.c.
 *
 * Override from the Makefile/build system with -DDEBUG_UART_ENABLED=1
 * (or define it above this #ifndef block before including this
 * header) to bring debug logging back for a bring-up/debug session -
 * no need to touch any of the 238 call sites either way.
 *
 * When OFF, debug_uart_init()/debug_print*() expand to ((void)0):
 * arguments are NOT evaluated, so nothing gets pushed onto the stack
 * and no UART traffic happens at all (not just "prints nothing" -
 * the calls compile away entirely). None of the call sites found in
 * the project pass arguments with side effects (no ++/--/= inside a
 * debug_print*() call), so this is safe project-wide.
 */
#ifndef DEBUG_UART_ENABLED
#define DEBUG_UART_ENABLED 0
#endif

#if DEBUG_UART_ENABLED

void debug_uart_init(void);
void debug_print(const char *s);
void debug_print_hex32(const char *label, uint32_t val);
void debug_print_hex16(const char *label, uint16_t val);
void debug_print_dec(const char *label, uint32_t val);

/*
 * _always() variants (16/09/2026, brought in with the HFDL module -
 * see hfdl_payload_decode.c - from the HFDL branch's separate
 * debug_uart.h, where they existed to bypass a "quiet mode" this
 * project doesn't have. Here they're plain aliases for the base
 * functions - the name is kept only so hfdl_payload_decode.c didn't
 * need touching. If quiet mode is ever added to this branch too,
 * these are the three call sites that would need to start actually
 * bypassing it. */
void debug_print_always(const char *s);
void debug_print_hex32_always(const char *label, uint32_t val);
void debug_print_dec_always(const char *label, uint32_t val);
/* Human-readable float print (ported 16/09/2026 with the HFDL module -
 * see hfdl_scope_tuning_diag_tick(). Fixed-point, adaptive fractional
 * digits, built from plain integer math - no libc float printf on
 * this link/no-syscalls target. */
void debug_print_float_always(const char *label, float val);

/* QUIET MODE - ver el comentario en debug_uart.c. Silencia los ~238
 * puntos de log normales del proyecto (waterfall ticks, smeter,
 * block budget...) dejando vivos los debug_print*_always() de HFDL.
 * Independiente de debug_uart_isr_silence_begin/end() de abajo. */
void debug_uart_set_quiet(uint8_t quiet);

/* Hard-silences ALL debug output, even the _always() variants that
 * bypass quiet mode - see debug_uart.c's own comment on s_isr_silence
 * for why and when to use this (specifically: around
 * hfdl_payload_decode_begin_segment()/finish() when called from ISR
 * context, since that module's own internal debug_print*_always()
 * calls aren't deferred like this project's other ISR-to-main-loop
 * prints). Keep the silenced span as short as possible - this is a
 * plain flag, not a counter, so nested calls are NOT supported: don't
 * call _begin() again before a matching _end(). */
void debug_uart_isr_silence_begin(void);
void debug_uart_isr_silence_end(void);

#else

#define debug_uart_init()             ((void)0)
#define debug_print(s)                ((void)0)
#define debug_print_hex32(label, val) ((void)0)
#define debug_print_hex16(label, val) ((void)0)
#define debug_print_dec(label, val)   ((void)0)

#define debug_print_always(s)                ((void)0)
#define debug_print_hex32_always(label, val) ((void)0)
#define debug_print_dec_always(label, val)   ((void)0)
#define debug_print_float_always(label, val) ((void)0)
#define debug_uart_set_quiet(quiet)           ((void)0)
#define debug_uart_isr_silence_begin()        ((void)0)
#define debug_uart_isr_silence_end()          ((void)0)

#endif /* DEBUG_UART_ENABLED */

#endif
