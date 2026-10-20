#ifndef W25Q_SIM_H
#define W25Q_SIM_H
#include <stdint.h>

void    w25q_reset(void);
void    w25q_cs(uint8_t nivel);
void    w25q_clk(uint8_t nivel);
void    w25q_mosi(uint8_t nivel);
uint8_t w25q_miso(void);

extern uint8_t  g_chip[];

/* Cambia el tamaño del chip simulado: donde da la vuelta la direccion y lo
 * que contesta al JEDEC. Por omision 2 MB, el W25Q16 de la radio. */
void     w25q_tam(uint32_t bytes);
/* 1 = el chip NO da la vuelta: por encima de su final contesta 0xFF.
 * Ver el comentario de w25q_sin_alias() en w25q_sim.c. */
void     w25q_sin_alias(uint8_t si);
uint32_t w25q_tam_actual(void);

/* Hace que el identificador JEDEC mienta sobre la capacidad: 0 = que diga
 * la verdad. Hay chips que lo hacen - ver sim/capacidad.c. */
void     w25q_jedec_capacidad(uint8_t codigo);
extern uint32_t g_borrados, g_grabados;

/* Lo que tarda el chip por dentro. Ver el comentario del .c: por omision
 * los tres a 0 -nunca ocupado, como siempre-, y g_us_bloqueado sale lo
 * que el bucle principal se ha quedado parado esperandolo. */
extern uint32_t g_us_sector, g_us_bloque, g_us_pagina, g_us_espera;
extern uint64_t g_us_bloqueado;
extern uint32_t g_miradas;
extern uint64_t g_bits;
extern int      g_fallos_chip;
extern uint32_t g_intocable_ini, g_intocable_fin;

/*
 * PARA LOS BANCOS QUE BUSCAN LA QUEJA DEL CHIP A PROPOSITO.
 *
 * Mientras `g_chip_espera` vale 1, una queja se imprime como "esperado" y
 * no cuenta como suelta. Con el flag a 0 -lo normal- se imprime "MAL" y,
 * si el banco acaba sin haberla reclamado, el programa sale con error por
 * su cuenta. Ver el comentario largo de mal() en w25q_sim.c.
 */
extern int      g_chip_espera;
extern uint32_t g_chip_sueltas;

#endif
