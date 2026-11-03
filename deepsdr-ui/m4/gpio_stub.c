/*
 * El GPIO, de mentira. Lo que se mide es delay_i2c(), que son 200 NOP a -O0
 * por medio bit: lo que haga el acceso al pin no mueve la aguja. Se deja en
 * RAM volatil para que el compilador no se coma ni un acceso.
 */
#include "gd32f4xx.h"
volatile uint32_t g_pin;
void gpio_bit_set(uint32_t port, uint32_t pin){ g_pin = port ^ pin; }
void gpio_bit_reset(uint32_t port, uint32_t pin){ g_pin = port + pin; }
void gpio_bit_write(uint32_t port, uint32_t pin, bit_status v){ g_pin = port ^ pin ^ (uint32_t)v; }
/* El nivel que devuelve la lectura de pin. Por defecto SET, que es lo que
 * hacia antes. El banco del tactil lo pone a RESET porque el PENIRQ del
 * XPT2046 es activo a nivel bajo: con SET, touch_read_raw() se vuelve
 * por donde ha venido y no mide nada. */
volatile FlagStatus g_nivel_pin = SET;
FlagStatus gpio_input_bit_get(uint32_t port, uint32_t pin){ g_pin = port|pin; return g_nivel_pin; }
void gpio_mode_set(uint32_t p,uint32_t m,uint32_t u,uint32_t q){ g_pin=p^m^u^q; }
void gpio_output_options_set(uint32_t p,uint8_t o,uint32_t s,uint32_t q){ g_pin=p^o^s^q; }
void rcu_periph_clock_enable(rcu_periph_enum p){ g_pin=(uint32_t)p; }
