/*
 * Los cuatro pines del bus SPI, encaminados al W25Q16 simulado.
 *
 * Esto es lo unico que separa al User/spi_flash.c del firmware de correr
 * tal cual en el PC. Por encima de aqui corre su spi_xfer_byte() de
 * verdad, bit a bit; por debajo, la maquina de estados de w25q_sim.c.
 *
 * El tactil comparte reloj, MOSI y MISO con la flash (PB3/PB4/PB5) y solo
 * se distingue por su propio CS, que esta en GPIOD y aqui no se usa: en
 * el banco no hay tactil, asi que cualquier cosa en esos pines es de la
 * flash por definicion.
 */
#include "gd32f4xx.h"
#include "w25q_sim.h"
#include <stdio.h>
#include <stdlib.h>

#define F_SCLK_PIN  GPIO_PIN_3
#define F_MISO_PIN  GPIO_PIN_4
#define F_MOSI_PIN  GPIO_PIN_5
#define F_CS_PIN    GPIO_PIN_6

void gpio_mode_set(uint32_t port, uint32_t mode, uint32_t pupd, uint32_t pin)
{ (void)port; (void)mode; (void)pupd; (void)pin; }

void gpio_output_options_set(uint32_t port, uint32_t otype, uint32_t speed, uint32_t pin)
{ (void)port; (void)otype; (void)speed; (void)pin; }

void rcu_periph_clock_enable(uint32_t periph) { (void)periph; }

void gpio_bit_write(uint32_t port, uint32_t pin, bit_status val)
{
    uint8_t v = (val == SET) ? 1U : 0U;
    (void)port;
    if (pin == F_CS_PIN)        { w25q_cs(v); }
    else if (pin == F_SCLK_PIN) { w25q_clk(v); }
    else if (pin == F_MOSI_PIN) { w25q_mosi(v); }
}

bit_status gpio_input_bit_get(uint32_t port, uint32_t pin)
{
    (void)port;
    return (pin == F_MISO_PIN && w25q_miso()) ? SET : RESET;
}

/* El firmware habla por el UART de depuracion mientras trabaja. Aqui se
 * tira, salvo que se pida verlo con DEPURA=1: el banco ya dice lo suyo. */
void debug_print(const char *s)
{ if (getenv("DEPURA")) { fputs(s, stdout); } }
void debug_print_hex32(const char *l, uint32_t v)
{ if (getenv("DEPURA")) { printf("%s 0x%08X\n", l, v); } }
void debug_print_hex16(const char *l, uint16_t v)
{ if (getenv("DEPURA")) { printf("%s 0x%04X\n", l, v); } }
void debug_print_dec(const char *l, uint32_t v)
{ if (getenv("DEPURA")) { printf("%s %u\n", l, v); } }
