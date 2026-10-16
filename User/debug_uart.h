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
 * REVISION A FONDO DEL 08/10/2026: LAS CUATRO "_always", TAMBIEN AQUI.
 *
 * Estaban definidas SOLO en la rama de abajo, la del UART apagado. Con
 * -DDEBUG_UART_ENABLED=1 -que es justo lo que recomienda el comentario
 * de la cabecera para una sesion de depuracion- hfdl_payload_decode.c
 * las llama diecisiete veces y no habia nada que las declarara: o
 * llegaban como implicitas, que es lo que el comentario de abajo dice
 * querer evitar y lo que ya mordio una vez con backlight_sleep/wake, o
 * directamente no compila. O sea que el interruptor de depuracion rompia
 * la compilacion exactamente cuando se iba a usar.
 *
 * Con el UART encendido "siempre" quiere decir lo que dice, asi que van
 * a las funciones de verdad.
 */
#define debug_print_always(s)                debug_print(s)
#define debug_print_hex32_always(label, val) debug_print_hex32((label), (val))
#define debug_print_hex16_always(label, val) debug_print_hex16((label), (val))
#define debug_print_dec_always(label, val)   debug_print_dec((label), (val))

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

/*
 * LAS VARIANTES "_always" DEL PROYECTO PADRE. 25/09/2026.
 *
 * Los modulos de HFDL las usan para trazas que ellos querian ver SIEMPRE,
 * con el UART de depuracion apagado o encendido. En esta radio el UART no
 * esta cableado a ningun sitio, asi que "siempre" seria escribir en un
 * puerto que nadie lee, pagando ademas las cadenas de texto en flash.
 *
 * Se definen aqui, y no se tocan sus ficheros, por dos razones: para que el
 * codigo del padre siga siendo el suyo -y se pueda volver a traer una
 * version nueva sin rehacer el parche- y porque la alternativa era que el
 * compilador las diera por implicitas, que es como estaban llegando: sin
 * declarar, suponiendo "int f()", y sin comprobar ni argumentos ni retorno.
 * Eso ya mordio una vez en este proyecto con backlight_sleep/wake.
 *
 * Si algun dia se cablea el UART y hace falta verlas, el sitio es este y
 * son cuatro lineas.
 */
#define debug_print_always(s)                ((void)0)
#define debug_print_hex32_always(label, val) ((void)0)
#define debug_print_hex16_always(label, val) ((void)0)
#define debug_print_dec_always(label, val)   ((void)0)

#endif /* DEBUG_UART_ENABLED */

#endif
