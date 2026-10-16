/*
 * LA TRADUCCION DE LA CLASE MSC, PROBADA EN EL PC - 01/10/2026.
 *
 * ESTE BANCO EXISTE POR UN FALLO QUE COSTO UNA TARDE. Y por una frase que
 * yo mismo escribi en la cabecera de disco_msc.c:
 *
 *     "Es a proposito que este fichero no tenga ni una decision. Si algo
 *      falla en el disco, el fallo esta en disco.c y hay banco; si falla
 *      el transporte, esta debajo y solo se ve en la placa. Sin zona gris."
 *
 * Tenia una decision, estaba mal, y era justo la zona gris que yo decia
 * que no existia: **la librería de GigaDevice pasa a mem_read() y
 * mem_write() una direccion en BYTES, no un numero de bloque**
 * (usbd_msc_scsi.c, linea 509: scsi_blk_addr *= scsi_blk_size[lun]).
 * disco_msc.c la tomaba por el bloque, asi que todo lo que Windows leia
 * salia del sitio equivocado y el volumen no montaba. El sintoma desde
 * fuera: una unidad que aparece un instante y desaparece.
 *
 * sim/disco.c prueba disco_lee() y disco_escribe() a conciencia, pero
 * prueba la capa de ABAJO. El fallo estaba en la de arriba, en las tres
 * lineas que yo habia declarado libres de decisiones.
 *
 * Asi que esto compila el disco_msc.c de verdad -el del cargador, no una
 * copia- contra un disco de mentira que apunta que bloque le piden, y
 * comprueba la unica cosa que importa: que una direccion en bytes se
 * convierte en el bloque correcto.
 */
#include "usbd_msc_mem.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define DISCO_BLOQUE_SIM 512U

/* --- el disco de mentira -------------------------------------------- */
static uint32_t s_pedidos[64];
static unsigned s_n;
static uint32_t s_bloques = 2048U;

uint32_t disco_bloques(void) { return s_bloques; }

bool disco_lee(uint32_t bloque, uint8_t *buf)
{
    if (bloque >= s_bloques) { return false; }
    if (s_n < 64U) { s_pedidos[s_n++] = bloque; }
    memset(buf, (int)(bloque & 0xFF), DISCO_BLOQUE_SIM);
    return true;
}

bool disco_escribe_n(uint32_t bloque, const uint8_t *buf, uint32_t n)
{
    (void)buf;
    if (bloque + n > s_bloques) { return false; }
    while (n-- > 0U) { if (s_n < 64U) { s_pedidos[s_n++] = bloque++; } }
    return true;
}

/* --- el banco -------------------------------------------------------- */
static int fallos = 0;

static void bien(const char *que, int ok)
{
    printf("    %-58s %s\n", que, ok ? "bien" : "MAL");
    if (!ok) { fallos++; }
}

int main(void)
{
    uint8_t buf[8U * DISCO_BLOQUE_SIM];

    printf("\nLA TRADUCCION DE LA CLASE MSC (boot/disco_msc.c)\n\n");

    printf("  lo que la libreria pasa son BYTES, no bloques\n");
    s_n = 0; usbd_mem_fops->mem_read(0U, buf, 0U, 1U);
    bien("direccion 0 -> bloque 0", s_n == 1U && s_pedidos[0] == 0U);

    s_n = 0; usbd_mem_fops->mem_read(0U, buf, 512U, 1U);
    bien("direccion 512 -> bloque 1, NO el 512", s_n == 1U && s_pedidos[0] == 1U);

    s_n = 0; usbd_mem_fops->mem_read(0U, buf, 4096U, 1U);
    bien("direccion 4096 -> bloque 8 (el que delato el fallo)",
         s_n == 1U && s_pedidos[0] == 8U);

    s_n = 0; usbd_mem_fops->mem_read(0U, buf, 8192U, 4U);
    bien("4 bloques desde 8192 -> 16, 17, 18, 19",
         s_n == 4U && s_pedidos[0] == 16U && s_pedidos[1] == 17U &&
         s_pedidos[2] == 18U && s_pedidos[3] == 19U);

    printf("\n  el ultimo bloque del disco, que es el que Windows comprueba\n");
    s_n = 0;
    bien("direccion 1048064 -> bloque 2047, y entra",
         usbd_mem_fops->mem_read(0U, buf, 1048064U, 1U) == 0 &&
         s_n == 1U && s_pedidos[0] == 2047U);
    bien("direccion 1048576 -> bloque 2048, y se sale",
         usbd_mem_fops->mem_read(0U, buf, 1048576U, 1U) < 0);

    printf("\n  y lo mismo al escribir\n");
    s_n = 0; usbd_mem_fops->mem_write(0U, buf, 1024U, 2U);
    bien("escribir en 1024 toca los bloques 2 y 3",
         s_n == 2U && s_pedidos[0] == 2U && s_pedidos[1] == 3U);

    printf("\n  y la capacidad que se le declara a la clase\n");
    usbd_mem_fops->mem_init(0U);
    bien("bloques = los que dice disco_bloques()",
         usbd_mem_fops->mem_block_len[0] == 2048U);
    bien("tamano de bloque = 512", usbd_mem_fops->mem_block_size[0] == 512U);
    bien("una sola unidad", usbd_mem_fops->mem_maxlun() == 0);

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
