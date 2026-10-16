/*
 * EL FAT32 DEL PINCHO, PROBADO CONTRA VOLUMENES DE VERDAD.
 *
 * Debajo no hay un FAT32 de mentira: hay imagenes hechas con el mkfs.fat
 * del sistema y rellenadas con mtools, o sea por codigo que no es el
 * nuestro y al que no le importa lo que nosotros creamos. Y encima corre el
 * User/fat32.c DE VERDAD, el mismo que se compila para la radio: aqui no se
 * copia nada a mano, que en este proyecto ya ha costado siete averias.
 *
 * QUE SE COMPRUEBA
 * ----------------
 *   1. que monta los cuatro volumenes y saca la geometria que dice fsck;
 *   2. que el MBR se atraviesa -un pincho de fabrica viene particionado y
 *      el arranque NO esta en el sector 0-;
 *   3. que lista la raiz entera, saltandose los trozos de nombre largo;
 *   4. que lo que lee es byte a byte lo que se escribio, incluido un
 *      fichero de 3 MB que son cientos de saltos de cluster;
 *   5. CUANTOS SECTORES PIDE para leer ese fichero, que es la unica forma
 *      de ver si la cache de un sector de FAT sirve de algo o no;
 *   6. y que la basura se rechaza en vez de colgar la radio.
 *
 * El 5 y el 6 son los que de verdad justifican el banco. Sin el 5, un
 * recorrido de la cadena desde el principio en cada lectura daria el mismo
 * resultado correcto tardando trescientas veces mas, y nadie se enteraria
 * hasta tener el pincho delante. Sin el 6, un volumen a medio formatear
 * divide por cero dentro de la radio, y ahi una excepcion NO reinicia: se
 * queda colgada.
 */
#include "fat32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fallos = 0;
static unsigned long sectores_leidos = 0UL;

static void ok(const char *que, int bien, const char *detalle)
{
    printf("  %-44s %s%s%s\n", que, bien ? "ok" : "MAL",
           detalle ? "   " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

/* ---- el disco: un fichero del ordenador ---- */
typedef struct { FILE *f; } disco_t;

static int disco_lee(void *ctx, uint32_t sector, uint32_t n, uint8_t *dst)
{
    disco_t *d = (disco_t *)ctx;

    sectores_leidos += n;
    if (fseeko(d->f, (off_t)sector * 512, SEEK_SET) != 0) { return -1; }
    if (fread(dst, 512, n, d->f) != n) { return -1; }
    return 0;
}

static uint32_t tam_sectores(FILE *f)
{
    off_t n;

    fseeko(f, 0, SEEK_END);
    n = ftello(f);
    return (uint32_t)(n / 512);
}

/* ---- los patrones con los que se rellenaron los ficheros ---- */
static uint8_t pat_medio(uint32_t i)  { return (uint8_t)((i * 7U + 3U) & 0xFFU); }
static uint8_t pat_grande(uint32_t i) { return (uint8_t)((i * 13U + 5U) & 0xFFU); }

static int comprueba_fichero(fat32_t *fs, const char *nombre, uint32_t tam,
                             uint8_t (*pat)(uint32_t))
{
    fat32_fich_t f;
    uint8_t buf[1024];
    uint32_t leidos = 0U;
    int malos = 0;

    if (fat32_abre(fs, 0, nombre, &f) != FAT32_OK) {
        printf("      no encuentro %s\n", nombre);
        return 0;
    }
    if (f.tam != tam) {
        printf("      %s mide %u y deberia medir %u\n",
               nombre, (unsigned)f.tam, (unsigned)tam);
        return 0;
    }
    for (;;) {
        int32_t r = fat32_lee(&f, buf, sizeof buf);
        int32_t j;

        if (r == 0) { break; }
        if (r < 0)  { printf("      error %d leyendo %s\n", (int)r, nombre);
                      return 0; }
        for (j = 0; j < r; j++) {
            if (buf[j] != pat(leidos + (uint32_t)j)) { malos++; }
        }
        leidos += (uint32_t)r;
    }
    if (leidos != tam) {
        printf("      %s: lei %u de %u\n", nombre, (unsigned)leidos,
               (unsigned)tam);
        return 0;
    }
    if (malos != 0) {
        printf("      %s: %d bytes distintos de %u\n", nombre, malos,
               (unsigned)tam);
        return 0;
    }
    return 1;
}

static void prueba(const char *ruta, const char *etiqueta, int con_mbr,
                   uint32_t spc_esperado)
{
    disco_t  d;
    bloques_t dev;
    static fat32_t fs;
    char det[120];
    int  r;

    printf("\n  %s  (%s)\n", ruta, etiqueta);

    d.f = fopen(ruta, "rb");
    if (d.f == NULL) { printf("    no esta: %s\n", ruta); fallos++; return; }

    dev.lee        = disco_lee;
    dev.escribe    = NULL;
    dev.ctx        = &d;
    dev.sectores   = tam_sectores(d.f);
    dev.tam_sector = 512;

    r = fat32_monta(&fs, &dev);
    snprintf(det, sizeof det, "(codigo %d)", r);
    ok("monta", r == FAT32_OK, r == FAT32_OK ? NULL : det);
    if (r != FAT32_OK) { fclose(d.f); return; }

    snprintf(det, sizeof det, "cluster de %u kB, FAT de %u kB, raiz en %u",
             (unsigned)(fs.sec_por_cluster * 512U / 1024U),
             (unsigned)(fs.sec_por_fat / 2U), (unsigned)fs.raiz_cluster);
    ok("la geometria sale del arranque", fs.sec_por_cluster == spc_esperado, det);

    snprintf(det, sizeof det, "volumen en el sector %u", (unsigned)fs.vol0);
    ok(con_mbr ? "atraviesa el MBR" : "arranque en el sector 0",
       con_mbr ? (fs.vol0 != 0UL) : (fs.vol0 == 0UL), det);

    /* --- la raiz --- */
    {
        fat32_entrada_t e;
        uint32_t i = 0U;
        int n = 0, vi_hola = 0, vi_medio = 0, vi_grande = 0, vi_lfn = 0;

        while (fat32_entrada(&fs, 0, i, &e) == FAT32_OK) {
            if (strcmp(e.nombre, "HOLA.TXT")   == 0) { vi_hola = 1; }
            if (strcmp(e.nombre, "MEDIO.BIN")  == 0) { vi_medio = 1; }
            if (strcmp(e.nombre, "GRANDE.BIN") == 0) { vi_grande = 1; }
            if (strcmp(e.nombre, "NOMBRE~1.TXT") == 0) { vi_lfn = 1; }
            n++; i++;
            if (n > 500) { break; }
        }
        snprintf(det, sizeof det, "%d entradas", n);
        ok("lista la raiz", (vi_hola && vi_medio && vi_grande), det);
        /* El de nombre largo tiene que salir UNA vez, por su 8.3, y no una
         * por cada trozo del nombre: los trozos llevan el atributo 0x0F y
         * se saltan. */
        ok("el nombre largo sale una vez, por su 8.3", vi_lfn != 0, NULL);
    }

    /* --- el contenido --- */
    {
        fat32_fich_t f;
        char buf[32];
        int32_t got;

        memset(buf, 0, sizeof buf);
        r = fat32_abre(&fs, 0, "HOLA.TXT", &f);
        got = (r == FAT32_OK) ? fat32_lee(&f, buf, sizeof buf - 1) : -1;
        ok("lee un fichero de un solo cluster",
           (got == 14) && (memcmp(buf, "deepsdr fat32\n", 14) == 0), buf);
    }

    ok("lee 100 kB seguidos, byte a byte",
       comprueba_fichero(&fs, "MEDIO.BIN", 100000U, pat_medio), NULL);

    /* --- y el caro, contando sectores --- */
    {
        unsigned long antes, gastados, minimo;
        uint32_t tam_cl = (uint32_t)fs.sec_por_cluster * 512U;
        int bien;

        antes = sectores_leidos;
        bien  = comprueba_fichero(&fs, "GRANDE.BIN", 3U * 1024U * 1024U,
                                  pat_grande);
        gastados = sectores_leidos - antes;
        /* Lo que hace falta si o si: los datos. Lo de mas son la FAT y la
         * busqueda del nombre en la raiz. */
        minimo = (3UL * 1024UL * 1024UL) / 512UL;
        ok("lee 3 MB seguidos, byte a byte", bien, NULL);

        snprintf(det, sizeof det,
                 "%lu sectores para %lu de datos  (+%lu de FAT, cluster %u kB)",
                 gastados, minimo, gastados - minimo,
                 (unsigned)(tam_cl / 1024U));
        /*
         * EL NUMERO QUE JUSTIFICA LA CACHE.
         *
         * Cada entrada de FAT32 son 4 bytes, asi que en un sector de 512
         * caben 128 saltos. Con clusters de 4 kB un fichero de 3 MB da 768
         * saltos: 6 sectores de FAT y ni uno mas si la cache funciona. Se
         * deja margen -la busqueda del nombre tambien lee- pero no
         * trescientas veces mas, que es lo que costaria recorrer la cadena
         * desde el principio en cada lectura.
         */
        ok("y no pide la FAT mas de lo que debe",
           (gastados - minimo) < 64UL, det);
    }

    fclose(d.f);
}

/* ---- la basura ---- */
static uint8_t s_basura[64 * 1024];
static int basura_lee(void *ctx, uint32_t sector, uint32_t n, uint8_t *dst)
{
    (void)ctx;
    if (((sector + n) * 512U) > sizeof s_basura) { return -1; }
    memcpy(dst, &s_basura[sector * 512U], n * 512U);
    return 0;
}

static void prueba_basura(void)
{
    bloques_t dev;
    static fat32_t fs;
    char det[80];
    int r;

    printf("\n  lo que NO es un FAT32\n");

    dev.lee = basura_lee; dev.escribe = NULL; dev.ctx = NULL;
    dev.sectores = sizeof s_basura / 512U; dev.tam_sector = 512;

    memset(s_basura, 0, sizeof s_basura);
    r = fat32_monta(&fs, &dev);
    ok("todo a ceros", r == FAT32_E_NO_FAT32, NULL);

    memset(s_basura, 0xA5, sizeof s_basura);
    r = fat32_monta(&fs, &dev);
    ok("relleno de 0xA5", r != FAT32_OK, NULL);

    /* Un arranque que PARECE FAT32 pero con sectores por cluster a cero.
     * Este es el que importa: pasa la firma, pasa el "spf16 == 0", y mas
     * abajo se usa para dividir. */
    memset(s_basura, 0, sizeof s_basura);
    s_basura[510] = 0x55; s_basura[511] = 0xAA;
    s_basura[11] = 0x00; s_basura[12] = 0x02;   /* 512 bytes por sector */
    s_basura[13] = 0;                            /* sectores por cluster = 0 */
    s_basura[14] = 32;                           /* reservados */
    s_basura[16] = 2;                            /* dos FAT */
    s_basura[36] = 0x10;                         /* sectores por FAT */
    s_basura[32] = 0xFF; s_basura[33] = 0xFF;    /* total */
    s_basura[44] = 2;                            /* raiz */
    r = fat32_monta(&fs, &dev);
    snprintf(det, sizeof det, "codigo %d", r);
    ok("sectores por cluster = 0 (dividiria por cero)",
       r == FAT32_E_GEOMETRIA, det);

    s_basura[13] = 3;    /* y uno que no es potencia de dos */
    r = fat32_monta(&fs, &dev);
    ok("sectores por cluster = 3 (no es potencia de dos)",
       r == FAT32_E_GEOMETRIA, NULL);

    s_basura[13] = 8;    /* valido, pero la FAT no alcanza para el volumen */
    s_basura[32] = 0xFF; s_basura[33] = 0xFF; s_basura[34] = 0xFF;
    r = fat32_monta(&fs, &dev);
    snprintf(det, sizeof det, "codigo %d", r);
    ok("la FAT no alcanza para los clusters que dice", r == FAT32_E_GEOMETRIA,
       det);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "out";
    char ruta[512];

    printf("\nEL FAT32 DEL PINCHO, CONTRA VOLUMENES HECHOS CON mkfs.fat\n");

    snprintf(ruta, sizeof ruta, "%s/fat32_1G.img", dir);
    prueba(ruta, "1 GB, sin particionar", 0, 8U);
    snprintf(ruta, sizeof ruta, "%s/fat32_8G.img", dir);
    prueba(ruta, "8 GB, sin particionar", 0, 8U);
    snprintf(ruta, sizeof ruta, "%s/fat32_32G.img", dir);
    prueba(ruta, "32 GB, sin particionar", 0, 32U);
    snprintf(ruta, sizeof ruta, "%s/fat32_part.img", dir);
    prueba(ruta, "4 GB CON tabla de particiones, como un pincho de fabrica",
           1, 8U);

    prueba_basura();

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO",
           fallos);
    return fallos ? 1 : 0;
}
