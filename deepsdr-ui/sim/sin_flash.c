/*
 * UNA RADIO SIN FLASH EXTERNA NI RELOJ, PARA LOS BANCOS. 10/10/2026.
 *
 * Hace falta desde que sim/aire_basura.c mete HFDL y FT8. Los dos arrastran
 * cosas que en la radio viven detras del chip de flash:
 *
 *   icao24_db.c   la base de matriculas de avion, que lee de la zona alta
 *   dxcc.c        la tabla de paises por prefijo
 *   rtc_hw.c      la hora, para ponerla en cada linea
 *
 * Y NINGUNA DE LAS TRES ES LO QUE ESTE BANCO MIDE. Lo que se mide es si el
 * decodificador SOBREVIVE a una entrada hostil; enlazar el controlador de
 * flash entero -con su bit-bang, sus esperas y su FAT- para eso seria meter
 * cinco mil lineas que no tienen nada que ver.
 *
 * CONTESTAN QUE NO HAY NADA, y eso no es hacer trampa: es el estado NORMAL
 * de una radio recien flasheada, sin BD.BIN cargado. O sea que lo que este
 * tapon reproduce es un caso de verdad -de hecho el mas comun- y ademas el
 * mas hostil de los dos: el decodificador tiene que apañarse sin la base en
 * vez de encontrarla siempre.
 */
#include <stdint.h>
#include <stdbool.h>
#include "rtc_hw.h"

/* --- la zona alta de la flash: no hay --- */
uint32_t za_base(void) { return 0U; }

uint8_t zona_alta_donde(uint32_t tipo, uint32_t *addr, uint32_t *tam)
{
    (void)tipo;
    if (addr) { *addr = 0U; }
    if (tam)  { *tam  = 0U; }
    return 0U;              /* ese fichero no esta */
}

/* --- el chip: devuelve ceros, que es lo que lee una flash borrada a cero.
 * Se rellena a 0x00 y no a 0xFF a proposito: 0xFF es "borrado" y varios
 * lectores lo tratan como fin de tabla, o sea que saldrian por la puerta
 * antes de recorrer nada. Con ceros entran, que es lo que se quiere. --- */
void spi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;
    (void)addr;
    if (!buf) { return; }
    for (i = 0U; i < len; i++) { buf[i] = 0U; }
}

/* 0 = no hay ninguna operacion en vuelo. El valor numerico es el del enum
 * SPI_FLASH_ASYNC_IDLE; no se incluye spi_flash.h para no arrastrar el
 * controlador entero. */
int spi_flash_async_save_poll(void) { return 0; }

/* --- la tabla de paises --- */
const char *dxcc_pais(const char *indicativo) { (void)indicativo; return 0; }

/* --- el reloj: fijo, como en sim/rtc_sim.c y por lo mismo (un banco cuya
 * salida cambia sola cada minuto no se puede comparar con el de ayer). Se
 * repite aqui en vez de enlazar rtc_sim.c porque aquel ademas trae
 * rtc_sim_pon_en_hora(), que este banco no usa. --- */
void rtc_hw_get(rtc_hw_datetime_t *out)
{
    if (!out) { return; }
    out->year = 2026; out->month = 10; out->day = 10;
    out->hour = 12;   out->minute = 0; out->second = 0;
}

bool rtc_hw_has_ever_synced(void) { return true; }

/* El contador de milisegundos del SysTick. Lo mira hfdl_modo.c para fechar
 * las tramas; aqui sube solo, un paso por cada vez que alguien lo lee, para
 * que el tiempo avance sin depender del reloj del PC. */
volatile uint32_t g_msticks = 0U;
