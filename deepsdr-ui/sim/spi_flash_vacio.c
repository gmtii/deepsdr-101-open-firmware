/*
 * spi_flash_vacio.c - el chip de flash, fingido y vacio.  27/09/2026.
 *
 * Los bancos de HFDL arrastran hfdl_modo.c, que desde que existe la base
 * de aviones llama a icao24_db_lookup() para poner el modelo en la tabla.
 * Eso arrastra icao24_db.c, que lee del chip SPI. En el ordenador no hay
 * chip.
 *
 * Contestar "flash borrada" -todo 0xFF- es lo correcto, y no lo correcto
 * por comodidad: lo que miden esos bancos son las autopruebas de la
 * cadena de HFDL, la polaridad y los falsos enganches del preambulo,
 * ninguna de las cuales sabe que existen los modelos de avion. La base de
 * datos tiene su propio banco -sim/icao24_test.c, que si finge el chip
 * leyendo de un fichero de verdad- y ahi es donde se comprueba.
 *
 * Sin esto el enlazado falla con "undefined reference to
 * icao24_db_lookup". Estuvo roto desde que se anadio la base de aviones,
 * porque estos tres bancos no se volvieron a ejecutar hasta hoy.
 */

#include "spi_flash.h"

void spi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    (void)addr;
    while (len-- > 0U) { *buf++ = 0xFFU; }   /* flash borrada: no hay nada */
}

spi_flash_async_status_t spi_flash_async_save_poll(void)
{
    return SPI_FLASH_ASYNC_IDLE;
}

/* Solo mirar, sin avanzar la maquina de estados. Ver spi_flash.c. */
uint8_t spi_flash_async_save_en_vuelo(void)
{
    return 0U;
}

/*
 * LA CAPACIDAD, DESDE EL 02/10/2026.
 *
 * zona_alta.c saca de aqui donde empieza y acaba la zona alta -el ultimo
 * megabyte del chip, mida lo que mida-, asi que todo banco que enlace
 * zona_alta.c necesita esta respuesta aunque no mire ni un byte del chip.
 *
 * Se contesta 2 MB, el W25Q16 de la radio, porque es lo que hace que estos
 * bancos sigan comprobando lo que comprobaban: las direcciones de siempre.
 * Quien comprueba que la cuenta se adapta a otros chips es
 * sim/capacidad.c, que enlaza el spi_flash.c de VERDAD contra un W25Q
 * simulado al que se le cambia el tamaño.
 */
uint32_t spi_flash_capacidad(void)   { return 2UL * 1024UL * 1024UL; }
void     spi_flash_olvida_capacidad(void) { }
