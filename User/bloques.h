#ifndef BLOQUES_H
#define BLOQUES_H

#include <stdint.h>

/*
 * UN DISCO, VISTO POR SECTORES.
 *
 * Esto existe porque el FAT32 va a tener DOS sitios debajo y no puede
 * saber en cual esta:
 *
 *   - un pincho, que por dentro es transporte masivo USB y habla por
 *     sectores de 512 bytes (SCSI READ(10) / WRITE(10));
 *   - y, para probarlo aqui, un fichero del ordenador.
 *
 * El disco interno de la radio NO entra por aqui: ese es una flash SPI que
 * se direcciona por bytes y lo lleva spi_flash.c, con su FAT12 metido
 * dentro. Mezclarlos seria reescribir 4.700 lineas que ya funcionan para
 * no ganar nada.
 *
 * Las funciones devuelven 0 si va bien y distinto de 0 si no. `escribe`
 * puede ser NULL: un volumen montado de solo lectura es un caso normal, no
 * un error, y asi no hay que inventarse una funcion que falle siempre.
 */
typedef struct bloques_s {
    int (*lee)(void *ctx, uint32_t sector, uint32_t n, uint8_t *dst);
    int (*escribe)(void *ctx, uint32_t sector, uint32_t n, const uint8_t *src);
    void    *ctx;
    uint32_t sectores;     /* cuantos tiene el medio entero */
    uint16_t tam_sector;   /* 512 siempre, de momento; ver fat32_monta() */
} bloques_t;

#endif /* BLOQUES_H */
