/*
 * LEER NO ES ESCRIBIR: LAS DOS PREGUNTAS DEL VOLUMEN - 06/10/2026.
 *
 * POR QUE EXISTE.
 *
 * *** De un issue: el cargador no veia update.bin en el disco, con
 * una foto de la ficha que decia "Volumen: no se reconoce" y la radio con
 * el FORMATEO DE FABRICA, sin reformatear en ningun momento. ***
 *
 * Los dos sintomas eran el mismo fallo. cargador_busca_update() empieza
 * con `if (!geo()) { return 0U; }`, asi que un volumen rechazado deja el
 * fichero sin buscar: estaba en el disco y nadie miraba el directorio. Y la
 * salida que la pantalla ofrece -"copia update.bin al disco USB"- es
 * justo la que no puede funcionar. Pescadilla que se muerde la cola.
 *
 * Y lo que estaba mal de fondo: geo_lee() contestaba a DOS preguntas
 * distintas con un solo si o no.
 *
 *   ¿puedo LEER?      sector de 512, cluster potencia de dos, y que las
 *                     cuentas de LBA cuadren. Nada mas.
 *   ¿puedo ESCRIBIR?  ademas: dos copias de la FAT, cluster que quepa en
 *                     el bloque de borrado de 4 kB y lo divida exacto,
 *                     area de datos en frontera, y FAT12 y no FAT16.
 *
 * Las de escribir son de ESTE driver y de su borrado por bloques; no son
 * del formato. Un volumen perfectamente legal y legible puede no pasar
 * ninguna - y el cargador, que SOLO LEE, se quedaba sin poder arreglar una
 * radio que no arranca.
 *
 * LO QUE COMPRUEBA, y en los dos sentidos:
 *
 *   1. que el volumen que el driver SI sabe escribir se lea tambien;
 *   2. que los volumenes de otra distribucion -los que ya existian en este
 *      banco porque el driver tiene que NEGARSE a escribirlos- ahora SI se
 *      lean;
 *   3. y que seguir sin poder escribirlos siga siendo verdad, porque esa
 *      negativa protege los datos y no se ha tocado.
 *
 * El punto 3 es el que hace que esto no sea un aflojamiento: lo que se
 * aflojo fue la pregunta de leer, no la de escribir.
 *
 * Debajo hay un W25Q16 simulado a nivel de pin (sim/w25q_sim.c), asi que el
 * spi_flash.c que corre aqui es el del firmware sin una linea cambiada.
 */
#include "spi_flash.h"
#include "w25q_sim.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int malos;

/* El mismo orden que sim/fatanade.c: reiniciar el chip, cargar el volumen
 * en su memoria y DESPUES spi_flash_init(). Al reves, el init lee un chip
 * todavia en blanco. */
static void carga(const char *img)
{
    FILE *f;
    size_t n;

    w25q_reset();
    memset(g_chip, 0xFF, 2U * 1024U * 1024U);
    f = fopen(img, "rb");
    if (!f) { printf("  no encuentro %s\n", img); exit(2); }
    n = fread(g_chip, 1U, 1024U * 1024U, f);
    fclose(f);
    (void)n;
    spi_flash_init();
    spi_flash_geo_olvida();   /* el volumen ha cambiado bajo los pies */
}

static void caso(const char *img, const char *nom,
                 int espera_lee, int espera_escribe)
{
    uint32_t f1, rz, dt, nc;
    int lee, esc;

    carga(img);
    lee = spi_flash_geometria_lectura(&f1, &rz, &dt, &nc) ? 1 : 0;
    esc = spi_flash_geometria(&f1, &rz, &dt, &nc) ? 1 : 0;

    printf("  %-26s leer %-3s escribir %-3s   %s\n", nom,
           lee ? "SI" : "no", esc ? "SI" : "no", spi_flash_geo_txt());
    if (lee != espera_lee) {
        printf("      *** MAL: leer tenia que salir %s ***\n",
               espera_lee ? "SI" : "no");
        malos++;
    }
    if (esc != espera_escribe) {
        printf("      *** MAL: escribir tenia que salir %s ***\n",
               espera_escribe ? "SI" : "no");
        malos++;
    }
    /* Y la invariante que lo resume: si se puede escribir, se puede leer.
     * Al reves no tiene por que. */
    if (esc && !lee) {
        printf("      *** MAL: se puede escribir pero no leer, eso no "
               "puede ser ***\n");
        malos++;
    }
}

int main(void)
{
    printf("LEER NO ES ESCRIBIR\n\n");

    /* El que el driver formatea y sabe manejar entero. */
    caso("out/fat12_vacio.img", "el nuestro (R=4, s=1)",        1, 1);
    /*
     * El que pone un PC: 1 sector reservado en vez de 4. ESTE SE ESCRIBE
     * TAMBIEN, y la primera version de este banco esperaba que no - el
     * Makefile lo describe como "el driver tiene que negarse", pero eso
     * era verdad de una version anterior: con clusters de 512 bytes hay
     * ocho por bloque de borrado y siempre cae alguno alineado, asi que
     * geo_lee() se relajo a proposito el 02/10 y lo acepta. Se deja porque
     * comprueba que la distribucion de un PC entra por los dos lados.
     */
    caso("out/fat12_pc.img",    "el de un PC (R=1, s=1)",       1, 1);
    /*
     * UNA SOLA COPIA DE LA FAT. Es un volumen FAT12 legal y perfectamente
     * legible -se lee la primera tabla, que es la unica-, pero este driver
     * no sabe mantener coherente un numero de copias que no sea dos, asi
     * que no escribe. Es el caso que de verdad separa las dos preguntas, y
     * sin el este banco no probaria nada.
     */
    caso("out/fat12_1fat.img",  "con 1 sola FAT (-f 1)",        1, 0);
    /*
     * Y UN FAT16. Mas de 4084 clusters ya NO es FAT12: las entradas miden
     * 16 bits. Recorrer esa tabla como si fuera FAT12 no da error, da
     * clusters equivocados - asi que esto no se lee NI se escribe, y la
     * comprobacion va del lado de leer. Es el error que cometi al separar
     * las dos preguntas la primera vez.
     */
    caso("out/fat16.img",       "un FAT16 (no es FAT12)",       0, 0);

    if (malos) {
        printf("\n  HAY FALLOS (%d)\n", malos);
        printf("  Ver la cabecera de este fichero y el corte dentro de\n");
        printf("  geo_lee() en User/spi_flash.c.\n");
        return 1;
    }
    printf("\n  TODO CORRECTO\n");
    return 0;
}
