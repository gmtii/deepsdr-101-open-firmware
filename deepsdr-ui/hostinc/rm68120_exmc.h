#ifndef RM68120_EXMC_H
#define RM68120_EXMC_H

/*
 * Cabecera SUSTITUTA para compilacion en host (simulador).
 *
 * Declara exactamente la misma API que User/rm68120_exmc.h del firmware,
 * pero sin arrastrar gd32f4xx.h ni el mapeo EXMC. La implementacion vive
 * en sim/lcd_sim.c y pinta sobre un canvas en memoria respetando la
 * MISMA semantica del panel real: rm68120_set_window() fija una ventana
 * y cada rm68120_write_data() avanza el cursor primero en X y luego en Y.
 *
 * Objetivo: poder compilar el User/gfx.c REAL del repositorio, sin
 * modificar ni una linea, y obtener pixel por pixel lo que se veria en
 * el panel. Si el gfx.c cambia, el simulador sigue siendo fiel.
 */

#include <stdint.h>
#include <stddef.h>

void rm68120_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void rm68120_write_data(uint16_t data);
void rm68120_write_cmd(uint16_t cmd);
void rm68120_write_cmd_data(uint16_t cmd, uint16_t data);
uint16_t rm68120_read_data(void);
void rm68120_fill_screen(uint16_t color);
void rm68120_init(void);

/* En el firmware esto cuenta ciclos del nucleo (ver User/rm68120_exmc.h).
 * Aqui no hay contador de ciclos ni hace falta: el simulador no mide
 * tiempos, comprueba pixeles. */
extern volatile uint32_t g_lcd_ciclos;

uint32_t rm68120_ciclo(void);

/* En el firmware esto es una escritura directa a la direccion del panel, sin
 * llamada (ver el comentario en User/rm68120_exmc.h). Aqui no hace falta que
 * sea rapido, solo que haga lo mismo. */
static inline void rm68120_dato(uint16_t v)
{
    rm68120_write_data(v);
}

#endif /* RM68120_EXMC_H */
