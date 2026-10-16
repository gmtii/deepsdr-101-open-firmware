#ifndef I2C_BITBANG_H
#define I2C_BITBANG_H

#include <stdint.h>

/*
 * I2C maestro por bit-banging sobre GPIO open-drain, igual filosofia que
 * el SPI bit-banged de touch.c: nos ahorra tener que confirmar contra el
 * datasheet del GD32F450 que AF exacto corresponde a I2C0/I2C1 en
 * PB8/PB9, y a 100kHz (modo estandar, de sobra para configurar un codec)
 * el bit-bang es perfectamente fiable.
 *
 * Pines confirmados en hardware real (Jorge, 27/07/2026):
 *   SCL   PB8
 *   SDA   PB9
 *
 * Implementacion con GPIO_OTYPE_OD (open-drain) + pull-up interno: SET
 * en el registro de salida = linea liberada (alta si nadie mas la tira a
 * masa), RESET = la tira activamente a masa. Asi se puede leer el nivel
 * real de la linea (para el bit de ACK, o para clock-stretching de un
 * esclavo) sin tener que alternar el modo del pin entre entrada/salida.
 */

/* Llamar una vez al arrancar: configura GPIO. */
void i2c_bitbang_init(void);

/* Escribe `len` bytes a la direccion de 7 bits `addr` (sin el bit R/W).
 * Devuelve 1 si el esclavo confirmo con ACK todos los bytes (incluida la
 * direccion), 0 si hubo un NACK en cualquier punto. */
uint8_t i2c_write(uint8_t addr, const uint8_t *data, uint8_t len);

/* Lee `len` bytes de la direccion de 7 bits `addr`. Devuelve 1 si la
 * direccion fue confirmada con ACK, 0 si NACK (en ese caso data no es
 * valido). El ultimo byte leido se confirma con NACK (fin de lectura),
 * el resto con ACK - protocolo I2C estandar. */
uint8_t i2c_read(uint8_t addr, uint8_t *data, uint8_t len);

/* Escaneo de bus: prueba direcciones de 7 bits desde `addr_from` hasta
 * `addr_to` (inclusive) emitiendo START+direccion+STOP y comprobando
 * ACK. Llama a `on_found(addr)` por cada direccion que responda. Util
 * para bring-up cuando no se esta 100% seguro de la direccion I2C de un
 * chip (p.ej. AIC3204 con el pin MODE flotante/a tierra). */
void i2c_scan(uint8_t addr_from, uint8_t addr_to, void (*on_found)(uint8_t addr));

#endif /* I2C_BITBANG_H */
