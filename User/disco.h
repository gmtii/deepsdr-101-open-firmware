/* El volumen de la flash SPI visto como bloques de 512 bytes, que es lo
 * unico que entiende la clase MSC de USB. Ver disco.c. */
#ifndef DISCO_H
#define DISCO_H
#include <stdint.h>

#define DISCO_BLOQUE  512U

uint32_t disco_bloques(void);
uint8_t  disco_lee(uint32_t lba, uint8_t *buf);
uint8_t  disco_escribe(uint32_t lba, const uint8_t *buf);

#endif

/* Varios bloques seguidos. Si la tirada cubre sectores de 4 kB enteros y
 * alineados los escribe de golpe -ocho veces menos borrados-; el resto cae
 * en disco_escribe(). Ver el comentario en disco.c. */
uint8_t disco_escribe_n(uint32_t lba, const uint8_t *buf, uint32_t n);
