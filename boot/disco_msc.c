/*
 * EL DISCO, CONTADO EN EL IDIOMA DE LA CLASE MSC - 01/10/2026.
 *
 * La libreria de GigaDevice pide una tabla de seis funciones (usbd_mem_cb)
 * y unos datos de INQUIRY. Todo lo que hay aqui es traduccion: cada
 * peticion de la clase acaba en disco_lee() o disco_escribe() de disco.c,
 * que son las que estan probadas en el PC (sim/disco.c).
 *
 * Es a proposito que este fichero no tenga ni una decision. Si algo falla
 * en el disco, el fallo esta en disco.c y hay banco; si falla el
 * transporte, esta debajo y solo se ve en la placa. Sin zona gris.
 *
 * LO QUE VE WINDOWS: fabricante "DEEPSDR", producto "Radio Disk". El
 * ejemplo de GigaDevice pone "GD32 Internal sram", que en la carpeta de
 * "Este equipo" no dice nada.
 */
#include "usbd_msc_mem.h"
#include "disco.h"

static const int8_t k_inquiry[] = {
    0x00,                                /* dispositivo de acceso directo */
    0x80,                                /* extraible */
    0x00,
    0x01,
    (USBD_STD_INQUIRY_LENGTH - 5U),
    0x00, 0x00, 0x00,
    'D','E','E','P','S','D','R',' ',     /* fabricante, 8 bytes  */
    'R','a','d','i','o',' ','D','i',     /* producto, 16 bytes   */
    's','k',' ',' ',' ',' ',' ',' ',
    '1','.','0','0'                      /* version, 4 bytes     */
};

extern usbd_mem_cb disco_fops;

static int8_t ini(uint8_t lun)
{
    (void)lun;
    /* el tamano se toma de disco.c, no se escribe dos veces */
    disco_fops.mem_block_len[0] = disco_bloques();
    disco_fops.mem_block_size[0] = DISCO_BLOQUE;
    return 0;
}
static int8_t listo(uint8_t lun)  { (void)lun; return 0; }
static int8_t protegido(uint8_t lun) { (void)lun; return 0; }
static int8_t max_lun(void)       { return 0; }   /* una sola unidad */

/* Diagnostico: cuantas peticiones de lectura ha servido la clase y cual
 * fue el ultimo bloque. Lo pinta modo_actualizacion(). */
volatile uint32_t g_msc_lee, g_msc_escribe, g_msc_bloque, g_msc_fallo;

/*
 * OJO: LO QUE LLEGA AQUI ES UNA DIRECCION EN BYTES, NO UN BLOQUE.
 *
 * *** El dueno, media tarde: "sigue sin aparecer el usb", "ni rastro del
 * usb". Y al final, la linea de diagnostico: "int 28861 est 3 lee 10
 * esc 0 blq 4096 mal 2". ***
 *
 * Ese 4096 lo dijo todo. El disco tiene 2048 bloques, asi que el 4096 no
 * existe; y 4096 son exactamente 8 x 512. La librería de GigaDevice, en
 * usbd_msc_scsi.c linea 509, hace esto justo antes de llamarnos:
 *
 *     msc->scsi_blk_addr *= msc->scsi_blk_size[lun];
 *
 * y luego, tras cada trozo enviado (linea 666):
 *
 *     msc->scsi_blk_addr += len;      // len en BYTES
 *
 * O sea que mem_read() y mem_write() reciben un DESPLAZAMIENTO EN BYTES.
 * Yo lo tomaba por el numero de bloque. Windows pedia el LBA 1, me
 * llegaba 512, y yo le devolvia el bloque 512 en vez del 1: la FAT salia
 * basura, el volumen no montaba, y al llegar al LBA 8 -byte 4096- la
 * peticion se salia del disco y fallaba.
 *
 * La comprobacion de rango de la libreria (linea 503) se hace ANTES de
 * multiplicar, con el LBA de verdad, asi que pasaba limpia y no saltaba
 * ningun error: desde fuera solo se veia una unidad que aparecia un
 * instante y desaparecia.
 */
static int8_t lee(uint8_t lun, uint8_t *buf, uint32_t dir_en_bytes, uint16_t n)
{
    uint32_t bloque = dir_en_bytes / DISCO_BLOQUE;

    (void)lun;
    g_msc_lee++; g_msc_bloque = bloque;
    while (n-- > 0U) {
        if (!disco_lee(bloque, buf)) { g_msc_fallo++; return -1; }
        buf += DISCO_BLOQUE;
        bloque++;
    }
    return 0;
}

/* Lo mismo que en lee(): aqui llega una direccion en bytes. */
static int8_t escribe(uint8_t lun, uint8_t *buf, uint32_t dir_en_bytes, uint16_t n)
{
    uint32_t bloque = dir_en_bytes / DISCO_BLOQUE;

    (void)lun;
    g_msc_escribe++; g_msc_bloque = bloque;
    /* de una vez, no bloque a bloque: ver disco_escribe_n() en disco.c */
    if (!disco_escribe_n(bloque, buf, n)) { g_msc_fallo++; return -1; }
    return 0;
}

usbd_mem_cb disco_fops = {
    .mem_init      = ini,
    .mem_ready     = listo,
    .mem_protected = protegido,
    .mem_read      = lee,
    .mem_write     = escribe,
    .mem_maxlun    = max_lun,
    .mem_inquiry_data = { (uint8_t *)k_inquiry },
    .mem_block_size   = { DISCO_BLOQUE },
    .mem_block_len    = { 2048U }        /* ini() lo relee de disco_bloques() */
};

usbd_mem_cb *usbd_mem_fops = &disco_fops;
