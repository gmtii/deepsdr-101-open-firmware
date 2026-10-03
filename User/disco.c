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
#include "zona_alta.h"

#define FLASH_SECTOR_4K  4096UL

/*
 * EL DISCO ES TODO EL CHIP MENOS EL ULTIMO MEGABYTE - 02/10/2026.
 *
 * *** Por el dueño: "el software tiene que ser capaz de montar 1 mega si el
 * chip es de 2, 7 megas si el chip es de 8, y todas las variantes
 * posibles". ***
 *
 * Aqui habia un 1 MB fijo, "la mitad de abajo del W25Q16". Ahora sale de
 * spi_flash_capacidad() menos la region de datos, asi que:
 *
 *     W25Q16   2 MB  ->  disco de 1 MB    (exactamente lo de siempre)
 *     W25Q64   8 MB  ->  disco de 7 MB
 *     W25Q128 16 MB  ->  disco de 15 MB
 *
 * ESTO ES LO QUE EL MSC LE DICE A WINDOWS, asi que anunciar de mas seria
 * grave: Windows escribiria en bloques que no existen, la direccion daria
 * la vuelta en el chip y machacaria el principio del propio volumen. Por
 * eso spi_flash_capacidad() contesta de MENOS ante la duda, con suelo de
 * 2 MB. Ver su comentario.
 *
 * Y SIGUE SIN ENTRAR LA ZONA ALTA, que es lo que protegia el numero fijo:
 * el ultimo megabyte queda FUERA del disco pase lo que pase, asi que un
 * formateo desde Windows no puede llevarse las bases de datos por delante.
 * Antes eso dependia de que el chip midiera 2 MB; ahora es verdad en
 * cualquier chip.
 *
 * LO QUE HAY QUE SABER AL CAMBIAR EL CHIP: el tamaño que se anuncia cambia,
 * pero el volumen que haya dentro sigue diciendo en su BPB lo que media al
 * formatearlo. Windows hace caso al BPB, asi que hasta que no se formatee
 * el chip nuevo se vera el disco pequeño de antes. Es lo correcto -nadie
 * debe estirar un sistema de ficheros por su cuenta- pero conviene saberlo
 * para no pensar que esto no funciona.
 */
uint32_t disco_bloques(void)
{
    return (spi_flash_capacidad() - ZA_BYTES_REGION) / DISCO_BLOQUE;
}

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
