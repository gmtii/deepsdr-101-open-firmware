/*
 * EL FORMATEADOR, CONTRA EL LECTOR DE VERDAD.
 *
 * Pedido por el usuario.
 *
 * Lo que se mide aqui no es que el formateador escriba "algo con pinta de
 * FAT": es que lo que escribe LO ACEPTE EL MISMO spi_flash.c que no
 * aceptaba el volumen del issue. Un formateador comprobado contra su
 * propia idea de lo que es correcto no vale para nada.
 *
 * Se prueban los dos chips que hay en la calle -el de 2 MB del issue y el
 * de 8 MB soldado- y ademas el reparto se comprueba a mano: cluster que
 * divida al borrado de 4 kB, dos tablas, y datos alineados.
 */
#include "formato.h"
#include "disco.h"
#include "spi_flash.h"
#include "w25q_sim.h"
#include <stdio.h>
#include <string.h>

static int fallos;
static void di(const char *q, int bien, const char *extra)
{
    printf("    %-50s %s%s%s\n", q, bien ? "ok" : "*** MAL ***",
           extra ? "  " : "", extra ? extra : "");
    if (!bien) { fallos++; }
}

static void prueba(uint32_t mb)
{
    uint8_t spc; uint16_t res, fat, raiz, clus;
    uint32_t f1, rz, dat, clus2;
    formato_r_t r;
    char txt[80];

    printf("\n  chip de %u MB\n", (unsigned)mb);
    w25q_reset();
    w25q_tam(mb * 1024U * 1024U);
    spi_flash_olvida_capacidad();
    spi_flash_geo_olvida();

    printf("      disco: %u bloques (%u kB)\n",
           (unsigned)disco_bloques(), (unsigned)(disco_bloques() / 2U));
    if (!formato_plan(disco_bloques(), &spc, &res, &fat, &raiz, &clus)) {
        di("hay plan de formateo", 0, "formato_plan dice que no");
        return;
    }
    printf("      plan: %u sec/cluster (%u B), reservados %u, FAT %u x2, "
           "raiz %u, %u clusters\n",
           spc, spc * 512U, res, fat, raiz, clus);

    di("el cluster divide al borrado de 4 kB", (4096U % (spc * 512U)) == 0U, 0);
    di("la zona de datos empieza alineada a 4 kB",
       (((uint32_t)res + 2UL * fat + raiz) % 8UL) == 0U, 0);
    di("menos de 4085 clusters (es FAT12 de verdad)", clus <= 4084U, 0);

    r = formato_haz();
    (void)snprintf(txt, sizeof txt, "(%s)", formato_porque_txt(r));
    di("formatea sin error", r == FORMATO_OK, txt);
    if (r != FORMATO_OK) { return; }

    spi_flash_geo_olvida();
    di("y el driver LO PUEDE LEER",
       spi_flash_geometria_lectura(&f1, &rz, &dat, &clus2) != 0U, 0);
    spi_flash_geo_olvida();
    di("y ademas LO PUEDE ESCRIBIR",
       spi_flash_geometria(&f1, &rz, &dat, &clus2) != 0U, 0);
    printf("      el driver ve: fat1 %u, raiz %u, datos %u, %u clusters\n",
           (unsigned)f1, (unsigned)rz, (unsigned)dat, (unsigned)clus2);
    di("el driver cuenta los mismos clusters", clus2 == clus, 0);
    di("y los sitios cuadran con el plan",
       f1 == res && rz == (uint32_t)res + 2UL * fat
                 && dat == (uint32_t)res + 2UL * fat + raiz, 0);
    di("el cluster que ve el driver es el que se puso", spi_flash_spc() == spc, 0);

    /*
     * Y LA COMPROBACION QUE HACE LA RADIO. Pedido por el usuario. Escribe un fichero de verdad y lo relee: es lo unico que pasa por
     * la tabla, el directorio y la zona de datos.
     */
    r = formato_comprueba();
    (void)snprintf(txt, sizeof txt, "(%s)", formato_porque_txt(r));
    di("se comprueba sola: escribe un fichero y lo relee", r == FORMATO_OK, txt);
}

int main(void)
{
    printf("\n  FORMATEAR EL DISCO DESDE LA RADIO\n");
    prueba(2U);    /* el del issue */
    prueba(8U);    /* el soldado en la radio del usuario */
    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}
