/*
 * EL MINIMO PARA QUE EL CODIGO DEL FABRICANTE COMPILE EN EL PC.
 *
 * Firmware/Source/gd32f4xx_timer.c solo incluye gd32f4xx_timer.h, y ese
 * solo necesita de aqui REG32, BIT, BITS, la base de los temporizadores y
 * los cuatro tipos de siempre. Poniendo este directorio delante en el -I,
 * el fichero del fabricante se compila SIN TOCARLO y escribe en un mapa de
 * registros de mentira en vez de en el chip.
 *
 * Asi el banco compara lo que hace la biblioteca de verdad con lo que hace
 * la secuencia a mano del cargador, en vez de comparar mi transcripcion
 * con mi transcripcion.
 */
#ifndef GD32F4XX_SHIM_H
#define GD32F4XX_SHIM_H

#include <stdint.h>

/* El mapa: cada direccion que se toque nace a cero y se queda guardada. */
uint32_t *reg_ptr(unsigned long addr);
void      reg_borra(void);

#define REG32(addr)   (*reg_ptr((unsigned long)(addr)))
#define REG16(addr)   (*(uint16_t *)reg_ptr((unsigned long)(addr)))
#define REG8(addr)    (*(uint8_t  *)reg_ptr((unsigned long)(addr)))

#define BIT(x)        ((uint32_t)((uint32_t)0x01U << (x)))
#define BITS(start, end) \
    ((0xFFFFFFFFUL << (start)) & (0xFFFFFFFFUL >> (31U - (uint32_t)(end))))
#define GET_BITS(regval, start, end) \
    (((regval) & BITS((start),(end))) >> (start))

#define APB1_BUS_BASE   ((unsigned long)0x40000000U)
#define TIMER_BASE      (APB1_BUS_BASE + 0x00000000U)

typedef enum { RESET = 0, SET = 1 } FlagStatus;
typedef enum { DISABLE = 0, ENABLE = 1 } ControlStatus;
typedef enum { ERROR = 0, SUCCESS = 1 } ErrStatus;

/* Lo unico del RCU que usa timer_deinit(): un reset por temporizador. */
typedef enum {
    RCU_TIMER0RST = 0, RCU_TIMER1RST,  RCU_TIMER2RST,  RCU_TIMER3RST,
    RCU_TIMER4RST,     RCU_TIMER5RST,  RCU_TIMER6RST,  RCU_TIMER7RST,
    RCU_TIMER8RST,     RCU_TIMER9RST,  RCU_TIMER10RST, RCU_TIMER11RST,
    RCU_TIMER12RST,    RCU_TIMER13RST
} rcu_periph_reset_enum;

void rcu_periph_reset_enable(rcu_periph_reset_enum p);
void rcu_periph_reset_disable(rcu_periph_reset_enum p);

/* Cuantas veces se ha reseteado cada temporizador, para poder exigirlo. */
extern unsigned g_reset_cuenta[14];

#include "gd32f4xx_timer.h"

#endif
