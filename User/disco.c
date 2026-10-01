/*
 * EL DISCO QUE VE WINDOWS - 01/10/2026.
 *
 * *** El dueno: "quiero acceder al modo usb como siempre, teniendo pulsado
 * el encoder al encender ... y se ve un disco duro en windows". ***
 *
 * La clase MSC de USB pide bloques de 512 bytes por numero (LBA) y punto.
 * Aqui esta esa traduccion, y esta SEPARADA del USB a proposito: el
 * transporte USB no se puede probar sin placa, pero esto si, y es donde
 * estan los fallos que duelen -los que dejan el volumen corrupto y la
 * radio sin forma de recibir la siguiente version-.
 *
 * EL PROBLEMA DE ESCRIBIR, que es todo el fichero en una frase: Windows
 * escribe bloques de 512 bytes en cualquier orden, y la flash SPI solo se
 * borra de 4 kB en 4 kB. Escribir 512 bytes "encima" sin mas se lleva por
 * delante los otros 3.584 del bloque.
 *
 * No se resuelve aqui: se resuelve llamando a
 * spi_flash_block_read_modify_write(), que lee el bloque entero, empalma
 * en RAM, borra y regraba. Ese camino ya existe en el firmware y ya tiene
 * banco desde el dia que los ajustes no se guardaban.
 *
 * QUE NO SE TOCA. El volumen es el primer 1 MB del chip. El megabyte de
 * arriba (0x100000-0x1FFFFF) lleva la zona alta -bases de datos, imagenes-
 * y NO forma parte del disco: si se dejara entrar, un formateo desde
 * Windows se la llevaria entera. El tope esta aqui y es fijo.
 */
#include "disco.h"
#include "spi_flash.h"

#define DISCO_BYTES   (1024UL * 1024UL)    /* la mitad de abajo del W25Q16 */
#define FLASH_SECTOR_4K  4096UL

uint32_t disco_bloques(void) { return DISCO_BYTES / DISCO_BLOQUE; }

uint8_t disco_lee(uint32_t lba, uint8_t *buf)
{
    if (lba >= disco_bloques()) { return 0U; }
    spi_flash_read(lba * DISCO_BLOQUE, buf, DISCO_BLOQUE);
    return 1U;
}

uint8_t disco_escribe(uint32_t lba, const uint8_t *buf)
{
    uint32_t addr;

    if (lba >= disco_bloques()) { return 0U; }
    addr = lba * DISCO_BLOQUE;

    /*
     * El desplazamiento DENTRO del bloque de 4 kB, que es lo que pide la
     * funcion: (addr & 0xFFF). Un LBA siempre cae alineado a 512, asi que
     * son 0, 512, 1024... y nunca cruza el borde de 4 kB. Por eso una sola
     * llamada basta y no hay que trocear.
     */
    spi_flash_block_read_modify_write(addr, addr & 0x0FFFU, buf, DISCO_BLOQUE);
    return 1U;
}

/*
 * VARIOS BLOQUES DE UNA VEZ, Y POR QUE HACE FALTA.
 *
 * La clase MSC entrega los datos en paquetes de MSC_MEDIA_PACKET_SIZE, que
 * son 4096 bytes: ocho bloques seguidos. Escribirlos uno a uno con
 * disco_escribe() hace OCHO ciclos de leer-borrar-regrabar sobre el MISMO
 * sector de 4 kB.
 *
 * La cuenta: un borrado de sector son unos 200 ms. Un update4.bin de 320 kB
 * son 80 sectores. Uno a uno: 640 borrados, unos 128 segundos. Asi: 80
 * borrados, unos 16. Y ocho veces menos desgaste de la flash.
 *
 * Cuando la tirada NO cubre un sector entero y alineado -el principio o el
 * final de una escritura, o una de un solo bloque- se cae al camino de
 * siempre, que es el que tiene banco. Este de aqui solo se mete cuando
 * puede reemplazar el sector COMPLETO, que es justo cuando leer antes no
 * sirve para nada.
 */
uint8_t disco_escribe_n(uint32_t lba, const uint8_t *buf, uint32_t n)
{
    const uint32_t por_sector = FLASH_SECTOR_4K / DISCO_BLOQUE;   /* 8 */

    while (n > 0U) {
        if ((lba % por_sector) == 0U && n >= por_sector) {
            if (lba + por_sector > disco_bloques()) { return 0U; }
            spi_flash_write_block_4k(lba * DISCO_BLOQUE, buf);
            lba += por_sector;
            buf += FLASH_SECTOR_4K;
            n   -= por_sector;
        } else {
            if (!disco_escribe(lba, buf)) { return 0U; }
            lba++;
            buf += DISCO_BLOQUE;
            n--;
        }
    }
    return 1U;
}
