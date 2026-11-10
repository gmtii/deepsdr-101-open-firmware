/*
 * ======================================================================
 * LA CADENA DE CONFIG.CSV EN UN DISCO FORMATEADO POR LA RADIO.
 * ======================================================================
 *
 * Por que hace falta este banco y no valia el que habia. sim/ajustes_flash.c
 * corre sobre out/fat12_vacio.img, que tiene UN SECTOR POR CLUSTER. Ahi
 * "clusters" y "sectores de 512" valen lo mismo, asi que una cuenta de
 * clusters hecha dividiendo por 512 da el resultado correcto por
 * casualidad y el banco pasa.
 *
 * El formateador de la propia radio pone clusters de 4 kB -formato.c, por
 * las razones que estan alli-, y entonces las dos cuentas se separan:
 *
 *     CONFIG.CSV, 2.048 bytes -> UN cluster de 4 kB, pero 2.048/512 = CUATRO
 *     MEM.CSV,     ~300 bytes -> UN cluster de 4 kB, y   300/512 = UNO
 *
 * O sea que spi_flash_cadena_seguida() decia que no para CONFIG.CSV SIEMPRE
 * y que si para MEM.CSV SIEMPRE, en toda radio cuyo disco hubiera
 * formateado ella misma. Eso es, entera, la averia que conto un usuario el
 * 10/11/2026: las memorias se guardan y los ajustes no.
 *
 * Lo que se mide aqui es que las dos funciones que llevaban la cuenta
 * -spi_flash_cadena_seguida() y spi_flash_async_save_start()- aceptan un
 * fichero recien escrito por el driver sobre el volumen que escribe la
 * radio. Si un fichero que el propio driver acaba de poner en clusters
 * seguidos no pasa su propia prueba de "seguida", la prueba esta mal.
 */
#include "formato.h"
#include "disco.h"
#include "spi_flash.h"
#include "w25q_sim.h"
#include <stdio.h>
#include <string.h>

static int fallos;
static uint8_t buf[4096];

static void di(const char *q, int bien)
{
    printf("    %-58s %s\n", q, bien ? "ok" : "*** MAL ***");
    if (!bien) { fallos++; }
}

/* El tamano fijo de CONFIG.CSV. Vive en settings.c (CSV_TAMANO) y aqui se
 * repite a proposito: este banco mide el DRIVER, no settings.c, y lo que
 * importa es que sean mas de 512 bytes, que es donde las dos unidades se
 * separan. */
#define CONFIG_BYTES 2048U
#define MEM_BYTES     300U

static void pon(const char *n8, const char *e3, uint32_t len, uint8_t r)
{
    uint32_t i;
    for (i = 0U; i < len; i++) { buf[i] = r; }
    if (!spi_flash_write_or_update_file(n8, e3, buf, len)) {
        printf("    no se ha podido escribir %.8s.%.3s\n", n8, e3);
        fallos++;
    }
}

static void prueba(uint32_t mb)
{
    uint8_t spc = 0U;
    uint16_t res, fat, raiz, clus;
    char txt[120];
    uint32_t i;

    printf("\n  chip de %u MB\n", (unsigned)mb);
    w25q_reset();
    w25q_tam(mb * 1024U * 1024U);
    spi_flash_olvida_capacidad();
    spi_flash_geo_olvida();

    if (!formato_plan(disco_bloques(), &spc, &res, &fat, &raiz, &clus)) {
        di("hay plan de formateo", 0);
        return;
    }
    if (formato_haz() != FORMATO_OK) {
        di("formatea sin error", 0);
        return;
    }
    spi_flash_geo_olvida();
    printf("      cluster de %u bytes, %u clusters\n",
           (unsigned)(spc * 512U), (unsigned)clus);

    /* En el orden en que los crea la radio: primero el banco, luego los
     * ajustes. Asi el segundo fichero no empieza en el primer cluster. */
    pon("MEM     ", "CSV", MEM_BYTES, 'm');
    pon("CONFIG  ", "CSV", CONFIG_BYTES, 'c');

    (void)snprintf(txt, sizeof txt,
                   "CONFIG.CSV (%u B) recien escrito: la cadena es seguida",
                   (unsigned)CONFIG_BYTES);
    di(txt, spi_flash_cadena_seguida("CONFIG  ", "CSV") != 0U);

    (void)snprintf(txt, sizeof txt,
                   "MEM.CSV (%u B) recien escrito: la cadena es seguida",
                   (unsigned)MEM_BYTES);
    di(txt, spi_flash_cadena_seguida("MEM     ", "CSV") != 0U);

    for (i = 0U; i < CONFIG_BYTES; i++) { buf[i] = 'z'; }
    di("y el guardado suave lo acepta", 
       spi_flash_async_save_start("CONFIG  ", "CSV", buf, CONFIG_BYTES) != 0U);

    /* Y que lo que escribe el camino suave se lee de verdad: aceptarlo y
     * escribir en el sitio equivocado seria peor que negarse. */
    {
        spi_flash_async_status_t st;
        uint32_t vueltas = 0U;
        do {
            st = spi_flash_async_save_poll();
            vueltas++;
        } while (st == SPI_FLASH_ASYNC_BUSY && vueltas < 10000U);
        di("el guardado suave termina bien", st == SPI_FLASH_ASYNC_DONE);
        printf("      ha tardado %u vueltas del bucle principal\n", (unsigned)vueltas);

        memset(buf, 0, sizeof buf);
        di("y lo escrito se relee entero",
           spi_flash_read_file_by_name("CONFIG  ", "CSV", buf, sizeof buf) == CONFIG_BYTES);
        {
            uint32_t malos = 0U;
            for (i = 0U; i < CONFIG_BYTES; i++) { if (buf[i] != 'z') { malos++; } }
            di("con los bytes que se le dieron", malos == 0U);
        }
        memset(buf, 0, sizeof buf);
        di("y MEM.CSV sigue donde estaba, sin tocar",
           spi_flash_read_file_by_name("MEM     ", "CSV", buf, sizeof buf) == MEM_BYTES
           && buf[0] == 'm' && buf[MEM_BYTES - 1U] == 'm');
    }
}

const char *tr(const char *es, const char *en) { (void)en; return es; }

int main(void)
{
    printf("LA CADENA DE CONFIG.CSV CON LOS CLUSTERS QUE PONE LA RADIO\n");
    prueba(2U);    /* el chip de serie */
    prueba(8U);    /* y el que se suelda */

    printf("\n%s  (%d mal)\n", fallos ? "*** HAY FALLOS ***" : "todo correcto", fallos);
    return fallos ? 1 : 0;
}
