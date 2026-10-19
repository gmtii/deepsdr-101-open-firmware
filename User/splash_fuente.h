#ifndef SPLASH_FUENTE_H
#define SPLASH_FUENTE_H

#include <stdint.h>
#include "gfx2_font.h"

/* Quien sabe leer la flash: spi_flash_read en la radio, un fichero en el
 * banco. Misma firma que la que ya pide zona_alta_abre(). */
typedef void (*za_lee_fn)(uint32_t addr, uint8_t *buf, uint32_t len);

/*
 * Carga la fuente de la lluvia desde la seccion SPLF de BD.BIN.
 *
 * `pozo` es donde se deja -el que llama presta la memoria, aqui no se
 * reserva nada- y `f` la estructura que se rellena con punteros DENTRO de
 * ese pozo. O sea que el pozo tiene que seguir vivo mientras se use la
 * fuente.
 *
 * Devuelve `f` si todo cuadra, y 0 si no hay seccion, no cabe, la marca no
 * es 'SPLF', la version no es la 1, las tres partes no suman lo que dice
 * la cabecera o algun rango apunta fuera de la tabla de glifos. Quien
 * llama no tiene que distinguir: 0 es "pinta la pantalla fija".
 */
const gfx2_font_t *splash_fuente_carga(za_lee_fn lee, uint8_t *pozo,
                                       uint32_t pozo_n, gfx2_font_t *f);

#endif /* SPLASH_FUENTE_H */
