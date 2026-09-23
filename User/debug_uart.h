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

#else

#define debug_uart_init()             ((void)0)
/*
 * Con el UART apagado estos macros no expanden a nada, asi que las variables
 * que solo se consumen aqui quedan "set but not used" y el compilador avisa.
 *
 * NO se arregla haciendo que los macros evaluen sus argumentos con (void)(x):
 * hay llamadas cuyo argumento es sda_read(), una lectura real del bus I2C, y
 * pasaria a ejecutarse en las compilaciones sin UART. Por eso cada sitio
 * afectado lleva su propio (void)variable; - ver los "solo se consume desde
 * debug_print*" repartidos por main.c, spi_flash.c y sdr_rx.c.
 */
#define debug_print(s)                ((void)0)
#define debug_print_hex32(label, val) ((void)0)
#define debug_print_hex16(label, val) ((void)0)
#define debug_print_dec(label, val)   ((void)0)

#endif /* DEBUG_UART_ENABLED */

#endif
