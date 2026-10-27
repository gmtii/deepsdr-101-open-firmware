/*
 * Banco de "anadir un trozo a un fichero del pendrive"
 * (spi_flash_anade_trozo(), del User/spi_flash.c del firmware).
 *
 * LO QUE SE MIDE AQUI
 * -------------------
 * No es que el fichero acabe teniendo los bytes correctos -eso tambien-.
 * Lo importante es que el VOLUMEN siga siendo un FAT12 valido despues de
 * cada pulsacion, porque ese volumen es por donde entra el firmware: si
 * se corrompe, la radio se queda sin forma de recibir la siguiente
 * version y no hay marcha atras desde dentro.
 *
 * Por eso el banco no se conforma con mirar el resultado a ojo: vuelca el
 * megabyte del sistema de ficheros a un .img despues de CADA pulsacion, y
 * tools/fat_check.py se los pasa a fsck.fat, que es un comprobador de
 * FAT12 escrito por otra gente y que no sabe nada de este proyecto. Si
 * nuestra idea de como se encadena un fichero difiere de la suya, salta.
 *
 * Debajo hay un W25Q16 simulado a nivel de pin (sim/w25q_sim.c), asi que
 * el spi_flash.c que corre aqui es el mismo, sin una linea cambiada.
 *
 * Y el megabyte de ARRIBA -el que esta por encima del sistema de
 * ficheros- se marca INTOCABLE. En la radio ahi puede haber cualquier
 * cosa, y el escritor de FAT no tiene ningun motivo para acercarse. Si se
 * acerca, el chip simulado lo dice.
 */
#include "spi_flash.h"
#include "bmp.h"
#include "w25q_sim.h"
#include "wfx_tabla.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOLUMEN   (1024U * 1024U)     /* el FAT12: 2048 sectores de 512 */
#define TROZO     4096U
#define PULSA     20U                 /* cuantas veces le damos al boton */
#define BORRA_EN  8U                  /* en que pulsacion borramos el fichero gordo */
#define GORDO     (512U * 1024U)      /* como el ZONAALTA.BIN de la radio */

static int fallos = 0;
static void comprueba(int ok, const char *que)
{
    printf("  %-58s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/* El mismo relleno que main.c: prueba_byte(). Lo comprueba tambien
 * tools/fat_check.py leyendolo de main.c, para que esta copia no pueda
 * quedarse atras en silencio. */
static uint8_t relleno(uint32_t k)
{
    return (uint8_t)((k * 31U) ^ (k >> 8) ^ 0x5AU);
}

/*
 * LO QUE HARIA EL PC, NO LA RADIO
 * -------------------------------
 * Estas dos funciones no simulan firmware: simulan a Windows. Ponen un
 * fichero gordo en el volumen y luego lo borran, que es exactamente lo
 * que hace el usuario con el cable enchufado.
 *
 * Y eso destapa el caso que de verdad importa. probe_fat_scan() coge el
 * PRIMER bloque libre empezando por el cluster 2, asi que mientras el
 * volumen va en orden cada trozo cae detras del anterior y el fichero
 * sale contiguo: el caso facil. Al borrar el gordo queda un agujero POR
 * DEBAJO de lo ya escrito, y los trozos siguientes se meten ahi. La
 * cadena empieza a saltar hacia atras, que es un fichero fragmentado de
 * verdad y es como va a estar el volumen cuando lleve meses de uso.
 *
 * Escriben directamente en g_chip y no por el bus a proposito: no son
 * parte de lo que se mide, son el escenario.
 */
/*
 * Y SI NO HAY VOLUMEN, QUE LO DIGA EN VEZ DE MATARSE - 22/10/2026.
 *
 * Con una imagen a ceros -lo que deja un `dd` cuyo `mkfs.fat` luego no
 * existe, y make la da por hecha- bytes/sector vale 0, clus_tam() vale 0 y
 * el banco se iba en SIGFPE: "Floating point exception" y ni una palabra
 * sobre que el fichero no era un FAT. Costo media hora de depurador
 * averiguar que el culpable no era el firmware sino el escenario.
 *
 * spc(), tres lineas mas abajo, ya se guardaba del cero desde el 20/10.
 * Esta no, y es la que multiplica.
 */
static uint32_t bps(void)
{
    uint32_t v = (uint32_t)(g_chip[11] | (g_chip[12] << 8));
    if (v == 0U) {
        fprintf(stderr,
                "\n  la imagen no es un volumen FAT: bytes por sector = 0.\n"
                "  (una imagen a ceros da esto; comprueba que mkfs.fat existe\n"
                "   y borra out/*.img para que se rehagan)\n\n");
        exit(2);
    }
    return v;
}
static uint32_t fat1(void)  { return (uint32_t)(g_chip[14] | (g_chip[15] << 8)) * bps(); }
static uint32_t fatsz(void) { return (uint32_t)(g_chip[22] | (g_chip[23] << 8)) * bps(); }
static uint32_t raiz(void)  { return fat1() + (uint32_t)g_chip[16] * fatsz(); }
/*
 * UN CLUSTER NO SON 512 BYTES - 20/10/2026.
 *
 * Esto lo descubri escribiendo la prueba de la carta de fax: el lector de
 * cadenas daba 24 kB de un fichero de 160 y parecia que el firmware se
 * dejaba bloques por encadenar. No se los dejaba. El volumen de 15 MB de
 * mas arriba tiene clusters de 4096, y el lector avanzaba 512 por
 * eslabon, asi que leia una octava parte y en la direccion equivocada.
 *
 * Es el mismo descuido que spi_flash.c ya tuvo el 02/10 y que alli esta
 * comentado. Aqui lo heredo vecino_suma(), que es quien vigila que no se
 * estropee ningun fichero del volumen: con la cuenta mal, comparaba una
 * suma mala con otra suma mala y decia que todo bien.
 */
static uint32_t spc(void)   { return (uint32_t)g_chip[13] ? (uint32_t)g_chip[13] : 1U; }
static uint32_t clus_tam(void) { return spc() * bps(); }

/*
 * OJO: esto reparte clusters de verdad, uno detras de otro.
 *
 * La primera version ponia primero = 2 fijo para todos los ficheros, o
 * sea que se solapaban entre ellos, y al borrar el gordo se llevaba por
 * delante los clusters de los demas. El banco entonces acusaba al
 * firmware de estropear CONFIG.CSV cuando el culpable era el escenario.
 * Un banco mal montado que acusa a un inocente es peor que no tenerlo:
 * te hace buscar un fallo donde no esta.
 */
/*
 * UN HUECO DE VERDAD, NO EL SIGUIENTE DE UN CONTADOR (25/09/2026).
 *
 * Esto era "s_prox_cluster += clus", un contador que empezaba en 2 y no
 * miraba la FAT. Mientras el PC fuera el unico que ponia ficheros daba
 * igual; en cuanto el DRIVER reserva clusters por su cuenta -que es lo que
 * hacen las pruebas de reservar, anadir y recortar de mas arriba- el
 * contador se queda corto y el siguiente fichero del PC aterriza ENCIMA de
 * uno del driver.
 *
 * Lo pillo la prueba de borrar: borrar ICAO24.BIN dejaba SSTV002.BMP roto,
 * y parecia un fallo del borrado. No lo era. Los dos compartian clusters
 * desde que se escribieron, y el borrado solo lo hizo visible.
 *
 * Un banco que acusa a un inocente es peor que no tener banco, asi que
 * ahora el PC busca sitio libre como lo buscaria un PC: en la FAT.
 */
static uint32_t pc_hueco(uint32_t clus)
{
    uint32_t total = (uint32_t)((VOLUMEN - (raiz() + (uint32_t)(g_chip[17] |
                     (g_chip[18] << 8)) * 32U)) / bps());
    uint32_t c, seguidos = 0U, ini = 2U;

    for (c = 2U; c < 2U + total; c++) {
        uint32_t o = c + (c / 2U);
        uint8_t *f = &g_chip[fat1() + o];
        uint16_t v = (uint16_t)(f[0] | (f[1] << 8));
        uint16_t e = (c & 1U) ? (uint16_t)(v >> 4) : (uint16_t)(v & 0x0FFFU);

        if (e == 0U) {
            if (seguidos == 0U) { ini = c; }
            seguidos++;
            if (seguidos >= clus) { return ini; }
        } else {
            seguidos = 0U;
        }
    }
    printf("  el volumen de prueba se ha quedado sin sitio   MAL\n");
    fallos++;
    return 2U;
}

static void pc_pone_gordo(const char *n11, uint32_t bytes)
{
    uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
    uint32_t datos = raiz() + rde * 32U;
    uint32_t clus = (bytes + clus_tam() - 1U) / clus_tam();
    uint32_t primero = pc_hueco(clus), off, c;
    uint8_t *e;

    /* La entrada, en la primera ranura libre. */
    for (off = raiz(); off < datos; off += 32U) {
        if (g_chip[off] == 0x00U || g_chip[off] == 0xE5U) { break; }
    }
    e = &g_chip[off];
    memcpy(e, n11, 11);
    e[11] = 0x20U;
    e[26] = (uint8_t)(primero & 0xFFU); e[27] = (uint8_t)(primero >> 8);
    e[28] = (uint8_t)(bytes & 0xFFU);        e[29] = (uint8_t)((bytes >> 8) & 0xFFU);
    e[30] = (uint8_t)((bytes >> 16) & 0xFFU); e[31] = (uint8_t)((bytes >> 24) & 0xFFU);

    /* Y su cadena, en las dos copias de la FAT. */
    for (c = primero; c < primero + clus; c++) {
        uint16_t v = (c + 1U < primero + clus) ? (uint16_t)(c + 1U) : 0x0FFFU;
        uint32_t o = c + (c / 2U), copia;
        for (copia = 0U; copia < 2U; copia++) {
            uint8_t *f = &g_chip[fat1() + copia * fatsz() + o];
            uint16_t p = (uint16_t)(f[0] | (f[1] << 8));
            p = (c & 1U) ? (uint16_t)((p & 0x000FU) | (v << 4))
                         : (uint16_t)((p & 0xF000U) | (v & 0x0FFFU));
            f[0] = (uint8_t)(p & 0xFFU); f[1] = (uint8_t)(p >> 8);
        }
    }
    memset(&g_chip[datos + (primero - 2U) * bps()], 0xA5, bytes);
}

/*
 * Pone una entrada en una RANURA CONCRETA del directorio, dejando 0xFF
 * por debajo. Existe para poner a prueba el razonamiento de
 * dir_find_end_marker_offset(): "una ranura a 0xFF solo puede significar
 * que ahi no se ha escrito nunca, o sea que detras tampoco hay nada".
 *
 * Si ese razonamiento falla, los 32 ceros que write_file_data_and_entry()
 * pone detras de una entrada nueva se llevan la de despues. En un volumen
 * con historia -restos de macOS, ficheros borrados, entradas sueltas- eso
 * no es una hipotesis academica.
 */
static void pc_pone_en_ranura(const char *n11, uint32_t bytes, uint32_t ranura)
{
    uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
    uint32_t datos = raiz() + rde * 32U;
    uint32_t clus = (bytes + bps() - 1U) / bps();
    uint32_t primero, c, off = raiz() + ranura * 32U;
    uint8_t *e = &g_chip[off];

    (void)datos;
    primero = pc_hueco(clus);   /* ver pc_hueco(): en la FAT, no en un contador */
    memcpy(e, n11, 11);
    e[11] = 0x20U;
    e[26] = (uint8_t)(primero & 0xFFU); e[27] = (uint8_t)(primero >> 8);
    e[28] = (uint8_t)(bytes & 0xFFU);        e[29] = (uint8_t)((bytes >> 8) & 0xFFU);
    e[30] = (uint8_t)((bytes >> 16) & 0xFFU); e[31] = (uint8_t)((bytes >> 24) & 0xFFU);
    for (c = primero; c < primero + clus; c++) {
        uint16_t v = (c + 1U < primero + clus) ? (uint16_t)(c + 1U) : 0x0FFFU;
        uint32_t o = c + (c / 2U), copia;
        for (copia = 0U; copia < 2U; copia++) {
            uint8_t *f = &g_chip[fat1() + copia * fatsz() + o];
            uint16_t p2 = (uint16_t)(f[0] | (f[1] << 8));
            p2 = (c & 1U) ? (uint16_t)((p2 & 0x000FU) | (v << 4))
                          : (uint16_t)((p2 & 0xF000U) | (v & 0x0FFFU));
            f[0] = (uint8_t)(p2 & 0xFFU); f[1] = (uint8_t)(p2 >> 8);
        }
    }
    memset(&g_chip[raiz() + rde * 32U + (primero - 2U) * bps()], 0x5A, bytes);
}

static void pc_borra(const char *n11)
{
    uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
    uint32_t datos = raiz() + rde * 32U;
    uint32_t off, c;

    for (off = raiz(); off < datos; off += 32U) {
        if (memcmp(&g_chip[off], n11, 11) != 0) { continue; }
        c = (uint32_t)(g_chip[off + 26] | (g_chip[off + 27] << 8));
        g_chip[off] = 0xE5U;                      /* asi borra FAT, sin tocar los datos */
        while (c >= 2U && c < 0x0FF8U) {
            uint32_t o = c + (c / 2U), copia, sig;
            uint8_t *f = &g_chip[fat1() + o];
            uint16_t v = (uint16_t)(f[0] | (f[1] << 8));
            sig = (c & 1U) ? (uint32_t)(v >> 4) : (uint32_t)(v & 0x0FFFU);
            for (copia = 0U; copia < 2U; copia++) {
                uint8_t *g = &g_chip[fat1() + copia * fatsz() + o];
                uint16_t p = (uint16_t)(g[0] | (g[1] << 8));
                p = (c & 1U) ? (uint16_t)(p & 0x000FU) : (uint16_t)(p & 0xF000U);
                g[0] = (uint8_t)(p & 0xFFU); g[1] = (uint8_t)(p >> 8);
            }
            c = sig;
        }
        return;
    }
}

/*
 * LOS VECINOS SIGUEN AHI
 * ----------------------
 *
 * Este banco comprobaba que el fichero nuevo saliera bien y que no se
 * tocara el megabyte de arriba, y daba eso por suficiente. No lo era: un
 * fichero correcto no dice NADA de lo que le haya pasado a los de al
 * lado, y los de al lado son CONFIG.CSV, channel.csv y cualquier cosa que
 * el usuario tenga ahi.
 *
 * El sitio por donde puede irse uno esta a la vista en
 * write_file_data_and_entry(): al crear un fichero escribe 64 bytes -la
 * entrada de 32 y otros 32 de ceros detras como marca de fin de
 * directorio-. Si la ranura elegida tuviera una entrada real justo
 * despues, esos ceros se la llevarian. El codigo razona que no puede
 * pasar; razonar no es medir.
 *
 * Asi que ahora, antes de la primera pulsacion, se apunta que ficheros
 * hay y con que contenido, y despues de CADA pulsacion se comprueba que
 * los mismos sigan estando y digan lo mismo.
 */
#define VECINOS_MAX  16U

static struct {
    char     n11[12];
    uint32_t tam;
    uint32_t suma;      /* de su contenido, siguiendo la cadena */
} s_vecinos[VECINOS_MAX];
static uint32_t s_n_vecinos;

/* La suma del contenido de un fichero, siguiendo su cadena FAT12.
 * Devuelve 0 si no esta o si la cadena no tiene sentido. */
static uint32_t vecino_suma(const char *n11, uint32_t *tam_out)
{
    uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
    uint32_t datos = raiz() + rde * 32U;
    uint32_t off, c, leidos = 0U, suma = 1U, vueltas = 0U;
    uint32_t tam = 0U;

    for (off = raiz(); off < datos; off += 32U) {
        if (memcmp(&g_chip[off], n11, 11) != 0) { continue; }
        c = (uint32_t)(g_chip[off + 26] | (g_chip[off + 27] << 8));
        tam = (uint32_t)(g_chip[off + 28] | (g_chip[off + 29] << 8) |
                        ((uint32_t)g_chip[off + 30] << 16) |
                        ((uint32_t)g_chip[off + 31] << 24));
        while (c >= 2U && c < 0x0FF8U && leidos < tam) {
            uint32_t o = c + (c / 2U), i;
            uint8_t *f = &g_chip[fat1() + o];
            uint16_t v = (uint16_t)(f[0] | (f[1] << 8));
            uint32_t ini = datos + (c - 2U) * clus_tam();
            for (i = 0U; i < clus_tam() && leidos < tam; i++, leidos++) {
                suma = (suma * 31U) + g_chip[ini + i];
            }
            c = (c & 1U) ? (uint32_t)(v >> 4) : (uint32_t)(v & 0x0FFFU);
            if (++vueltas > 4096U) { return 0U; }
        }
        if (tam_out) { *tam_out = tam; }
        return (leidos == tam) ? suma : 0U;
    }
    return 0U;
}

static void vecinos_apunta(void)
{
    uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
    uint32_t datos = raiz() + rde * 32U;
    uint32_t off;

    s_n_vecinos = 0U;
    for (off = raiz(); off < datos && s_n_vecinos < VECINOS_MAX; off += 32U) {
        if (g_chip[off] == 0x00U || g_chip[off] == 0xE5U || g_chip[off] == 0xFFU) { continue; }
        if ((g_chip[off + 11] & 0x0FU) == 0x0FU) { continue; }   /* nombre largo */
        if (memcmp(&g_chip[off], "PRUEBA  BIN", 11) == 0) { continue; }
        memcpy(s_vecinos[s_n_vecinos].n11, &g_chip[off], 11);
        s_vecinos[s_n_vecinos].n11[11] = 0;
        s_vecinos[s_n_vecinos].suma = vecino_suma(s_vecinos[s_n_vecinos].n11,
                                                  &s_vecinos[s_n_vecinos].tam);
        s_n_vecinos++;
    }
}

/* Devuelve el nombre del primero que se haya estropeado, o 0 si estan
 * todos. `saltar` permite no mirar uno que el PC acaba de borrar aposta. */
static const char *vecinos_mira(const char *saltar)
{
    uint32_t i, tam = 0U;

    for (i = 0U; i < s_n_vecinos; i++) {
        if (saltar && memcmp(s_vecinos[i].n11, saltar, 11) == 0) { continue; }
        if (vecino_suma(s_vecinos[i].n11, &tam) != s_vecinos[i].suma ||
            tam != s_vecinos[i].tam) {
            return s_vecinos[i].n11;
        }
    }
    return 0;
}

static void vuelca(const char *ruta)
{
    FILE *f = fopen(ruta, "wb");
    if (!f) { printf("  no puedo escribir %s   MAL\n", ruta); fallos++; return; }
    fwrite(g_chip, 1, VOLUMEN, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    static uint8_t trozo[TROZO];
    const char *plantilla = (argc > 1) ? argv[1] : "out/fat12_vacio.img";
    uint32_t i, p, tam = 0U;
    FILE *f;

    printf("\n=== Anadir trozos a un fichero del volumen FAT ===\n\n");

    w25q_reset();

    /* El volumen recien formateado, tal y como lo deja mkfs.fat. */
    f = fopen(plantilla, "rb");
    if (!f) { printf("  falta %s\n", plantilla); return 1; }
    if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
        printf("  %s no mide 1 MB\n", plantilla); fclose(f); return 1;
    }
    fclose(f);

    /* Por encima del sistema de ficheros no se pinta nada. */
    g_intocable_ini = VOLUMEN;
    g_intocable_fin = 2U * 1024U * 1024U;

    spi_flash_init();

    /* Un fichero gordo ya puesto, como el ZONAALTA.BIN de la radio. */
    pc_pone_gordo("ZONAALTABIN", GORDO);
    pc_pone_gordo("CONFIG  CSV", 1536U);
    pc_pone_gordo("CHANNEL CSV", 1024U);
    pc_pone_gordo("MANUAL  PDF", 20480U);
    printf("  el PC deja ZONAALTA.BIN (%u kB), CONFIG.CSV, channel.csv y MANUAL.PDF\n",
           GORDO / 1024U);
    /*
     * Y ahora un directorio CON HISTORIA, como el de una radio que lleva
     * meses: entradas borradas (0xE5), un hueco a 0xFF, y detras de ese
     * hueco un fichero real. Justo la forma que el razonamiento de
     * dir_find_end_marker_offset() da por imposible.
     */
    /*
     * El caso que pone a prueba el razonamiento, montado a mala idea: las
     * ranuras 0..5 ocupadas por entradas reales, la 6 a 0xFF, y la 7 con
     * un fichero real detras. Sin ningun 0x00 antes, para que
     * dir_find_end_marker_offset() no tenga otra salida que coger la del
     * 0xFF - y entonces los 32 ceros del terminador caen justo encima de
     * TESORO.DAT.
     *
     * Se puede discutir si un volumen puede llegar a esta forma: este
     * driver rellena siempre la primera ranura libre, y un PC deja las
     * sobrantes a 0x00, no a 0xFF. Pero el comentario del codigo YA dice
     * que no puede pasar, y la gracia de un banco es medir en vez de
     * repetir el argumento.
     */
    pc_pone_en_ranura("RELLENO1TXT", 512U, 5U);
    pc_pone_en_ranura("RELLENO2TXT", 512U, 6U);
    memset(&g_chip[raiz() + 7U * 32U], 0xFF, 32U);          /* hueco a 0xFF */
    pc_pone_en_ranura("TESORO  DAT", 8192U, 8U);            /* y detras, uno real */
    /* Y uno borrado DE VERDAD, liberando su cadena, que es lo que hace un
     * PC. Marcar la entrada 0xE5 a mano dejaba 40 clusters huerfanos y
     * fsck culpaba al firmware de una guarrada del escenario. */
    pc_borra("MANUAL  PDF");
    /* Y uno borrado DE VERDAD: marcar la entrada 0xE5 a mano dejaba su
     * cadena reservada, y entonces fsck veia clusters perdidos y culpaba
     * al firmware de una guarrada del escenario. Segunda vez que este
     * banco acusa a un inocente por estar mal montado. */
    /* (se borra mas abajo, ya colocados los rellenos) */

    vecinos_apunta();
    {   /* Donde esta cada entrada, para poder afirmar que el hueco a 0xFF
         * seguido de un fichero real es el caso que se esta probando. */
        uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
        uint32_t datos = raiz() + rde * 32U, off, r = 0U;
        printf("  directorio: ");
        for (off = raiz(); off < datos && r < 10U; off += 32U, r++) {
            uint8_t b = g_chip[off];
            if (b == 0x00U)      { printf("[%u fin] ", r); break; }
            else if (b == 0xFFU) { printf("[%u 0xFF] ", r); }
            else if (b == 0xE5U) { printf("[%u borr] ", r); }
            else { printf("[%u %.11s] ", r, (const char *)&g_chip[off]); }
        }
        printf("\n");
    }
    printf("  apuntados %u vecinos: hay que encontrarlos igual despues de cada pulsacion\n\n",
           s_n_vecinos);

    for (p = 1U; p <= PULSA; p++) {
        if (p == BORRA_EN) {
            pc_borra("ZONAALTABIN");
            printf("  --- el PC borra ZONAALTA.BIN: %u kB de agujero POR DEBAJO\n",
                   GORDO / 1024U);
        }
        spi_anade_r_t porque = SPI_ANADE_OK;
        char ruta[64];
        uint8_t ok;

        tam = 0U;
        (void)spi_flash_fichero_tam("PRUEBA  ", "BIN", &tam);
        for (i = 0U; i < TROZO; i++) { trozo[i] = relleno(tam + i); }

        ok = spi_flash_anade_trozo("PRUEBA  ", "BIN", trozo, &porque);
        if (!ok) {
            printf("  pulsacion %2u: %s   MAL\n", p,
                   spi_flash_anade_porque_txt(porque));
            fallos++;
            break;
        }

        tam = 0U;
        (void)spi_flash_fichero_tam("PRUEBA  ", "BIN", &tam);
        printf("  pulsacion %2u -> %6u bytes (%2u trozos)\n", p, tam, tam / TROZO);
        if (tam != p * TROZO) {
            printf("      deberia medir %u   MAL\n", p * TROZO);
            fallos++;
        }

        {   /* Y lo que este banco no miraba: que los vecinos sigan ahi. */
            const char *victima = vecinos_mira(p >= BORRA_EN ? "ZONAALTABIN" : 0);
            if (victima) {
                printf("      se ha estropeado %s   MAL\n", victima);
                fallos++;
            }
        }

        snprintf(ruta, sizeof ruta, "out/fatanade_%02u.img", p);
        vuelca(ruta);
    }

    /*
     * Lo que el PROPIO firmware cree de la forma de la cadena, escrito a
     * un fichero para que tools/fat_check.py lo compare con lo que saca
     * el suyo recorriendo la FAT por su cuenta. Dos implementaciones
     * independientes del mismo recorrido: si coinciden, las dos estan
     * bien o las dos estan mal de la misma forma exacta, que es mucho
     * menos probable.
     */
    {
        uint32_t clus = 0U, saltos = 0U;
        FILE *g;
        if (spi_flash_fichero_cadena("PRUEBA  ", "BIN", &clus, &saltos)) {
            printf("  el firmware dice: %u clusters, %u saltos hacia atras\n",
                   clus, saltos);
            comprueba(clus == (PULSA * TROZO) / 512U,
                      "y cuenta los clusters que tiene que contar");
            g = fopen("out/fatanade_saltos.txt", "w");
            if (g) { fprintf(g, "%u\n", saltos); fclose(g); }
        } else {
            printf("  el firmware no sabe leer su propia cadena   MAL\n");
            fallos++;
        }
    }

    printf("\n");
    comprueba(tam == PULSA * TROZO, "el fichero mide lo que se le ha metido");
    comprueba(g_fallos_chip == 0,   "el chip no se ha quejado ni una vez");
    comprueba(vecinos_mira("ZONAALTABIN") == 0,
              "los ficheros que ya estaban siguen enteros");
    printf("  (%u borrados, %u grabaciones de pagina)\n", g_borrados, g_grabados);

    /*
     * LO QUE CUESTA UN BLOQUE, EN TIEMPO DE VERDAD
     * --------------------------------------------
     * Hace falta para saber si el guardado de SSTV puede seguir el ritmo
     * del decodificador o no. Dos sumandos, y ninguno se puede acelerar
     * desde el software:
     *
     *   el bus   va a patadas por GPIO, con dos retardos de 1 us por bit
     *            (ver spi_xfer_byte en spi_flash.c) -> 2 us por bit
     *   el chip  un borrado de 4 kB en un W25Q son unos 50 ms tipicos y
     *            una pagina de 256 B unos 0,7 ms, y eso lo pone su propia
     *            circuiteria interna
     */
    {
        double bus = (double)g_bits * 2e-6;
        double chip = (double)g_borrados * 0.050 + (double)g_grabados * 0.0007;
        printf("\n  coste medido de %u pulsaciones (un bloque de 4 kB cada una):\n", PULSA);
        printf("    bus a patadas   %6.2f s  (%llu bits a 2 us)\n", bus,
               (unsigned long long)g_bits);
        printf("    borrar+grabar   %6.2f s  (%u borrados, %u paginas)\n",
               chip, g_borrados, g_grabados);
        printf("    -> %.0f ms por bloque\n", (bus + chip) * 1000.0 / (double)PULSA);
    }

    /* Y que no se haya tocado nada por encima del sistema de ficheros:
     * el chip lo vigila, pero ademas se mira aqui, que es mas barato que
     * fiarse de una sola guarda. */
    {
        uint32_t tocados = 0U;
        for (i = VOLUMEN; i < 2U * 1024U * 1024U; i++) {
            if (g_chip[i] != 0xFFU) { tocados++; }
        }
        comprueba(tocados == 0U, "el megabyte de arriba sigue intacto");
    }

    /*
     * ------------------------------------------------------------------
     * Y LA FOTO, POR EL CAMINO DE VERDAD: RESERVA + BLOQUES
     * ------------------------------------------------------------------
     * Esto es lo que hace la radio al guardar una SSTV. Escribe la FAT de
     * una forma NUEVA -una cadena de 488 entradas de golpe en vez de ocho
     * por añadido- y ese es justo el codigo que puede comerse el volumen
     * por donde entra el firmware. Asi que se monta aqui, se vuelca, y lo
     * juzgan fsck.fat (que el volumen siga siendo valido) y Pillow (que la
     * foto se abra y tenga los puntos que se pusieron).
     */
    {
        static uint8_t cab[BMP_CAB_BYTES];
        static uint8_t bloque[TROZO];
        static uint8_t fila[320 * 3];
        static uint8_t conv[320 * 3];
        uint32_t total = bmp_cabecera(cab, 320U, 256U, BMP_24);
        uint32_t addr = 0U, puestos = 0U;
        uint16_t blk = 0U, x, y;
        spi_anade_r_t porque = SPI_ANADE_OK;
        uint64_t bits0 = g_bits;
        uint32_t bor0 = g_borrados, gra0 = g_grabados;

        printf("\n  --- una foto por el camino de la radio: reservar y rellenar\n");
        printf("  SSTV001.BMP son %u bytes = %u bloques seguidos\n",
               total, total / TROZO);

        if (!spi_flash_reserva("SSTV001 ", "BMP", total, &addr, &porque)) {
            printf("      no se pudo reservar: %s   MAL\n",
                   spi_flash_anade_porque_txt(porque));
            fallos++;
        } else {
            /* La cabecera va en el bloque 0, y detras los renglones. El
             * patron es el mismo de sim/bmptest.c: bordes duros y colores
             * puros, que delatan una foto del reves o con el rojo y el azul
             * cambiados. */
            spi_flash_write_block_4k(addr, cab);
            blk = 1U;
            for (y = 0U; y < 256U; y++) {
                for (x = 0U; x < 320U; x++) {
                    uint8_t *p = &fila[x * 3U];
                    if (y == 0U)        { p[0]=255; p[1]=0;   p[2]=0;   }
                    else if (y == 255U) { p[0]=0;   p[1]=0;   p[2]=255; }
                    else if (x == 0U)   { p[0]=0;   p[1]=255; p[2]=0;   }
                    else if (x == 319U) { p[0]=255; p[1]=255; p[2]=255; }
                    else switch ((x * 4U) / 320U) {
                    case 0:  p[0]=248; p[1]=0;   p[2]=0;   break;
                    case 1:  p[0]=0;   p[1]=252; p[2]=0;   break;
                    case 2:  p[0]=0;   p[1]=0;   p[2]=248; break;
                    default: p[0]=248; p[1]=252; p[2]=0;   break;
                    }
                }
                bmp_fila24(fila, conv, 320U);
                {   /* al buffer del bloque, volcando cuando se llena */
                    uint32_t n = 960U, off = 0U;
                    while (n > 0U) {
                        uint32_t hueco = TROZO - puestos;
                        uint32_t c = (n < hueco) ? n : hueco;
                        memcpy(&bloque[puestos], &conv[off], c);
                        puestos += c; off += c; n -= c;
                        if (puestos == TROZO) {
                            spi_flash_write_block_4k(addr + (uint32_t)blk * TROZO, bloque);
                            blk++;
                            puestos = 0U;
                        }
                    }
                }
            }
            comprueba(blk == total / TROZO, "se han escrito los bloques reservados");
            comprueba(vecinos_mira("ZONAALTABIN") == 0,
                      "y los vecinos siguen enteros despues de la foto");
            {
                double bus = (double)(g_bits - bits0) * 2e-6;
                double chip = (double)(g_borrados - bor0) * 0.050
                            + (double)(g_grabados - gra0) * 0.0007;
                printf("      coste: %.2f s en total, %.0f ms por bloque\n",
                       bus + chip, (bus + chip) * 1000.0 / (double)blk);
            }
            vuelca("out/fatanade_foto.img");

            /*
             * Y una foto CORTADA: se reserva entera y solo entran tres
             * renglones, como cuando el ruido cuela una linea o se pierde
             * la señal. Sin recortar, esos 61 bloques se quedarian pillados
             * para siempre. Lo que se mide es que el sitio VUELVE.
             */
            {
                uint32_t libres_antes, libres_despues, usado;
                spi_flash_fat_scan_t sc;
                uint32_t addr2 = 0U;

                spi_flash_probe_fat_scan(&sc);
                libres_antes = sc.free_data_clusters;

                if (!spi_flash_reserva("SSTV002 ", "BMP", total, &addr2, &porque)) {
                    printf("      no se pudo reservar la segunda   MAL\n");
                    fallos++;
                } else {
                    spi_flash_write_block_4k(addr2, cab);       /* cabecera */
                    spi_flash_write_block_4k(addr2 + TROZO, bloque);  /* 3 filas y pico */
                    usado = BMP_CAB_BYTES + 960U * 3U;
                    (void)bmp_cabecera(cab, 320U, 3U, BMP_24);
                    spi_flash_write_block_4k(addr2, cab);       /* altura de verdad */
                    comprueba(spi_flash_recorta("SSTV002 ", "BMP", usado) != 0U,
                              "una foto cortada se puede recortar");
                    spi_flash_probe_fat_scan(&sc);
                    libres_despues = sc.free_data_clusters;
                    printf("      reservados %u clusters, usados 16, devueltos %u\n",
                           total / 512U, libres_despues - (libres_antes - total / 512U));
                    comprueba(libres_despues >= libres_antes - 24U,
                              "y devuelve casi todo el sitio que no uso");
                    comprueba(vecinos_mira("ZONAALTABIN") == 0,
                              "sin tocar a los vecinos al recortar");
                    vuelca("out/fatanade_corta.img");
                }
            }
        }
    }

    /*
     * ===== BORRAR UN FICHERO DEL DISCO (25/09/2026) =====
     *
     * ICAO24.BIN son medio mega, y una vez esta en la zona alta sobra en
     * el disco. spi_flash_fichero_borra() es codigo destructivo sobre la
     * FAT del usuario, asi que se mide aqui antes de que toque un disco de
     * verdad, y lo que se mide no es solo "desaparece": es que los VECINOS
     * siguen enteros y que las dos copias de la FAT siguen diciendo lo
     * mismo. Una FAT que no cuadra con su copia es lo que hace que Windows
     * declare el volumen danado, y eso no se ve borrando un fichero y
     * mirando si esta.
     */
    {
        uint32_t c_ini, n_lib_antes, n_lib_despues, k;
        const char *roto;

        printf("\n  --- borrar un fichero del disco ---\n");

        pc_pone_gordo("ICAO24  BIN", 32768U);
        vecinos_apunta();
        comprueba(spi_flash_fichero_hay("ICAO24  ", "BIN") != 0U,
                  "el PC deja ICAO24.BIN (32 kB) y el driver lo ve");

        /* Cuantos clusters libres hay antes, para saber si los suelta. */
        n_lib_antes = 0U;
        for (k = 2U; k < 2048U; k++) {
            uint32_t o = k + (k / 2U);
            uint8_t *fp = &g_chip[fat1() + o];
            uint16_t v = (uint16_t)(fp[0] | (fp[1] << 8));
            if (((k & 1U) ? (v >> 4) : (v & 0x0FFFU)) == 0U) { n_lib_antes++; }
        }
        c_ini = 0U;
        {
            uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
            uint32_t datos = raiz() + rde * 32U, off;
            for (off = raiz(); off < datos; off += 32U) {
                if (memcmp(&g_chip[off], "ICAO24  BIN", 11) == 0) {
                    c_ini = (uint32_t)(g_chip[off + 26] | (g_chip[off + 27] << 8));
                    break;
                }
            }
        }

        comprueba(spi_flash_fichero_borra("ICAO24  ", "BIN") != 0U,
                  "se borra");
        comprueba(spi_flash_fichero_hay("ICAO24  ", "BIN") == 0U,
                  "y ya no aparece en el directorio");

        /* Lo que de verdad importa: los demas. */
        roto = vecinos_mira("ICAO24  BIN");
        comprueba(roto == 0, "los demas ficheros siguen enteros");
        if (roto) { printf("      se ha estropeado %s\n", roto); }

        /* Sus clusters, libres. */
        n_lib_despues = 0U;
        for (k = 2U; k < 2048U; k++) {
            uint32_t o = k + (k / 2U);
            uint8_t *fp = &g_chip[fat1() + o];
            uint16_t v = (uint16_t)(fp[0] | (fp[1] << 8));
            if (((k & 1U) ? (v >> 4) : (v & 0x0FFFU)) == 0U) { n_lib_despues++; }
        }
        comprueba(n_lib_despues > n_lib_antes,
                  "sus clusters vuelven a estar libres");
        {
            uint32_t o = c_ini + (c_ini / 2U);
            uint8_t *fp = &g_chip[fat1() + o];
            uint16_t v = (uint16_t)(fp[0] | (fp[1] << 8));
            comprueba(((c_ini & 1U) ? (v >> 4) : (v & 0x0FFFU)) == 0U,
                      "el primero de su cadena, el primero en soltarse");
        }

        /* LAS DOS COPIAS. Esto es lo que no se ve a simple vista. */
        comprueba(memcmp(&g_chip[fat1()], &g_chip[fat1() + fatsz()], fatsz()) == 0,
                  "las dos copias de la FAT siguen siendo iguales");

        /* Borrar lo que no esta no es un error silencioso: dice que no. */
        {
            static uint8_t antes[VOLUMEN];
            memcpy(antes, g_chip, sizeof antes);
            comprueba(spi_flash_fichero_borra("NOEXISTEBIN", "BIN") == 0U,
                      "borrar uno que no esta devuelve que no");
            comprueba(memcmp(antes, g_chip, sizeof antes) == 0,
                      "y no toca un solo byte del volumen");
        }

        /*
         * CUANTO CUESTA MIRAR SI ESTA, EN CADA ARRANQUE.
         *
         * No se contesta a ojo. El W25Q de aqui esta simulado a nivel de
         * pin y g_bits cuenta los bits que pasan por el bus, asi que el
         * coste sale de multiplicar por lo que tarda un bit en este bus
         * (2 us, los mismos con los que se mide todo lo demas de este
         * banco).
         */
        {
            uint64_t b0 = g_bits;
            (void)spi_flash_fichero_hay("ICAO24  ", "BIN");
            printf("      mirar si esta, SIN estar:  %.1f ms (%llu bits)\n",
                   (double)(g_bits - b0) * 2e-6 * 1000.0,
                   (unsigned long long)(g_bits - b0));
            comprueba((double)(g_bits - b0) * 2e-6 < 0.050,
                      "mirarlo en el arranque cuesta menos de 50 ms");

            pc_pone_gordo("ICAO24  BIN", 32768U);
            b0 = g_bits;
            (void)spi_flash_fichero_hay("ICAO24  ", "BIN");
            printf("      mirar si esta, ESTANDO:    %.1f ms\n",
                   (double)(g_bits - b0) * 2e-6 * 1000.0);
            (void)spi_flash_fichero_borra("ICAO24  ", "BIN");
        }

        /*
         * Y AHORA EL QUE SE ME COLO (27/09/2026).
         *
         * El borrado buscaba la entrada con dir_find_entry(), que mira
         * SOLO el primer sector del directorio: 16 ranuras. Pero el
         * fichero lo encuentra fichero_busca(), que mira los 32 a
         * proposito porque "un fichero copiado desde Windows puede caer
         * mas adelante". Con el disco algo lleno, ICAO24.BIN se copiaba
         * bien y no se borraba NUNCA, en silencio - y como la carga es
         * automatica al arrancar, eso son treinta segundos de copia en
         * cada encendido, para siempre.
         *
         * Lo que se mide es justo eso: que se borra estando lejos.
         */
        {
            uint32_t k;
            char nom[12];

            /* Se llena el primer sector del directorio para empujar a
             * ICAO24.BIN mas alla de la ranura 16. */
            for (k = 0U; k < 14U; k++) {
                memcpy(nom, "RELLENO0TXT", 12);
                nom[7] = (char)('A' + (char)k);
                pc_pone_gordo(nom, 512U);
            }
            pc_pone_gordo("ICAO24  BIN", 8192U);
            vecinos_apunta();

            comprueba(spi_flash_fichero_hay("ICAO24  ", "BIN") != 0U,
                      "con el directorio lleno, el driver lo sigue viendo");
            comprueba(spi_flash_fichero_borra("ICAO24  ", "BIN") != 0U,
                      "y lo BORRA aunque caiga fuera del primer sector");
            comprueba(spi_flash_fichero_hay("ICAO24  ", "BIN") == 0U,
                      "comprobado: ya no aparece");
            {
                const char *r = vecinos_mira("ICAO24  BIN");
                comprueba(r == 0, "y los 14 de relleno siguen enteros");
                if (r) { printf("      se ha estropeado %s\n", r); }
            }
            comprueba(memcmp(&g_chip[fat1()], &g_chip[fat1() + fatsz()], fatsz()) == 0,
                      "las dos copias de la FAT siguen iguales");
        }

        vuelca("out/fatanade_borra.img");
    }

    /*
     * ------------------------------------------------------------------
     * VOLUMENES QUE NO EMPIEZAN DONDE SOLIAN: TIENEN QUE FUNCIONAR
     * ------------------------------------------------------------------
     *
     * ESTE BANCO PEDIA LO CONTRARIO HASTA HOY, y era cierto entonces: el
     * driver llevaba la geometria escrita a fuego y lo unico decente que
     * podia hacer con otra era negarse. Lo que se exigia era que se
     * negara, no que lo manejara.
     *
     * Ahora lo maneja, asi que lo que se mide cambia. Y lo que se mide NO
     * es "no se ha roto": es la propiedad concreta de la que depende todo
     * lo demas. El borrado de esta flash va por bloques de 4 kB y
     * spi_flash_write_block_4k() hace `& ~4095`, asi que un grupo de
     * clusters que no empiece en frontera escribiria en el bloque de al
     * lado y borraria el del vecino. La regla sigue siendo la misma: SOLO
     * se reservan grupos alineados. Lo unico que cambia es que el primero
     * ya no tiene por que ser el cluster 2.
     *
     * Y el banco calcula DONDE deberia caer por su cuenta, leyendo el
     * sector de arranque a mano. Si preguntara al firmware cual es el
     * primer cluster alineado, estaria comparando una funcion consigo
     * misma y pasaria con cualquier respuesta.
     */
    {
        static const struct { const char *img; uint32_t vol; uint32_t chip; const char *que; } k_casos[] = {
            { "out/fat12_pc.img", 1024U * 1024U, 2U * 1024U * 1024U,
              "1 MB con 1 sector reservado (los datos en el byte 23.040)" },
            { "out/fat12_2k.img", 7U * 1024U * 1024U, 8U * 1024U * 1024U,
              "7 MB con clusters de 2 kB, lo que pone Windows por su cuenta" },
            { "out/fat12_16m.img", 15U * 1024U * 1024U, 16U * 1024U * 1024U,
              "15 MB en un W25Q128, el chip mas grande que admite el driver" },
        };
        uint32_t k;

        for (k = 0U; k < (sizeof k_casos / sizeof k_casos[0]); k++) {
            FILE *g = fopen(k_casos[k].img, "rb");
            spi_anade_r_t porque = SPI_ANADE_OK;
            uint32_t addr = 0U, vol = k_casos[k].vol;
            uint32_t v_bps, v_spc, v_res, v_nfat, v_fatsz, v_rent, v_datos, c, c_ali = 0U;
            uint32_t esperada;
            char ruta[64];

            printf("\n  --- %s\n", k_casos[k].que);
            if (!g) { printf("      falta %s   MAL\n", k_casos[k].img); fallos++; continue; }

            w25q_reset();
            w25q_tam(k_casos[k].chip);
            spi_flash_olvida_capacidad();
            if (fread(g_chip, 1, vol, g) != vol) {
                printf("      %s no mide lo que dice   MAL\n", k_casos[k].img);
                fallos++; fclose(g); continue;
            }
            fclose(g);
            /* Por encima del sistema de ficheros, intocable: ahi vive la
             * base de datos y el escritor de FAT no pinta nada. */
            g_intocable_ini = vol;
            g_intocable_fin = k_casos[k].chip;
            spi_flash_init();

            /* La cuenta del banco, a mano y desde el sector de arranque. */
            v_bps   = (uint32_t)(g_chip[11] | (g_chip[12] << 8));
            v_spc   = g_chip[13];
            v_res   = (uint32_t)(g_chip[14] | (g_chip[15] << 8));
            v_nfat  = g_chip[16];
            v_rent  = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
            v_fatsz = (uint32_t)(g_chip[22] | (g_chip[23] << 8));
            v_datos = v_res + (v_nfat * v_fatsz)
                    + ((v_rent * 32U + v_bps - 1U) / v_bps);
            for (c = 2U; c < 2U + 16U; c++) {
                if ((((v_datos + ((c - 2U) * v_spc)) * v_bps) & 4095U) == 0U) { c_ali = c; break; }
            }
            printf("      cluster de %u bytes, datos en el byte %u, primer cluster alineado: %u\n",
                   (unsigned)(v_spc * v_bps), (unsigned)(v_datos * v_bps), (unsigned)c_ali);

            comprueba(spi_flash_puede_escribir() == 1U,
                      "el driver dice que puede escribir en el");
            printf("      dice: %s\n", spi_flash_geo_txt());
            comprueba(c_ali != 0U, "hay algun cluster en frontera de bloque de borrado");
            if (c_ali == 0U) { continue; }

            esperada = (v_datos + ((c_ali - 2U) * v_spc)) * v_bps;
            comprueba(spi_flash_reserva("PRUEBA09", "BIN", 4096U, &addr, &porque) == 1U,
                      "reservar un bloque funciona");
            comprueba((addr & 4095U) == 0U,
                      "y lo reservado cae en frontera de bloque de borrado");
            comprueba(addr == esperada,
                      "justo donde dice la cuenta del banco, no donde sea");
            if (addr != esperada) {
                printf("      dice %u, el banco esperaba %u\n", (unsigned)addr, (unsigned)esperada);
            }

            /* Y el camino de la radio entero: escribir un fichero por donde
             * lo escribe la radio y volver a leerlo. Si la cuenta de
             * cuantos clusters ocupa estuviera hecha con 512 en vez de con
             * el cluster de verdad, esto se lee corto o se lee de mas. */
            {
                static uint8_t dentro[1536], fuera[1536];
                uint32_t j, n;

                for (j = 0U; j < sizeof dentro; j++) { dentro[j] = relleno(j + 7U); }
                comprueba(spi_flash_write_or_update_file("CONFIG  ", "CSV",
                                                         dentro, sizeof dentro) == 1,
                          "la radio escribe su CONFIG.CSV");
                memset(fuera, 0, sizeof fuera);
                n = spi_flash_read_file_by_name("CONFIG  ", "CSV", fuera, sizeof fuera);
                comprueba(n == sizeof dentro, "y lo vuelve a leer con el tamano correcto");
                comprueba(memcmp(dentro, fuera, sizeof dentro) == 0,
                          "byte por byte igual");
                /* Y que ocupe los clusters que le tocan, no los que
                 * saldrian de contar con 512. La cuenta la hace el banco
                 * con el cluster que leyo del sector de arranque. */
                {
                    uint32_t cl = 0U, salt = 0U;
                    uint32_t debe = (sizeof dentro + (v_spc * v_bps) - 1U) / (v_spc * v_bps);
                    comprueba(spi_flash_fichero_cadena("CONFIG  ", "CSV", &cl, &salt) == 1U
                              && cl == debe,
                              "y su cadena ocupa los clusters que le tocan");
                    if (cl != debe) {
                        printf("      la cadena tiene %u clusters y le tocan %u\n",
                               (unsigned)cl, (unsigned)debe);
                    }
                }
            }

            comprueba(spi_flash_anade_trozo("PRUEBA  ", "BIN", trozo, &porque) == 1U,
                      "anadir un trozo tambien funciona");
            comprueba(g_fallos_chip == 0,
                      "y no se ha tocado el megabyte de arriba");

            /* Y que lo diga fsck.fat, que no sabe nada de este proyecto.
             * El prefijo es OTRO a proposito: tools/fat_check.py no solo
             * pasa fsck, tambien mira el fichero de trozos del escenario
             * de arriba y da por hecho un cluster de 512 bytes. Estas
             * imagenes no son de ese escenario y tienen clusters de otro
             * tamaño, asi que las valida fsck.fat a secas desde el make. */
            snprintf(ruta, sizeof ruta, "out/fatotro%u.img", (unsigned)k);
            {
                FILE *o = fopen(ruta, "wb");
                if (!o) { printf("  no puedo escribir %s   MAL\n", ruta); fallos++; }
                else { fwrite(g_chip, 1, vol, o); fclose(o); }
            }
        }
    }

    /* ==================================================================
     * REHACER LA CABECERA DE UN FICHERO MONTADO A TROZOS
     * ==================================================================
     *
     * Es lo que hace una carta de fax al cerrarse: el BMP lleva la altura
     * delante y no se sabe hasta el final, asi que se escribe con el tope y
     * al acabar se rehace con la de verdad.
     *
     * Y el fichero esta REPARTIDO, asi que para rehacerla hay que
     * localizar su primer cluster por la entrada de directorio. Eso lo hace
     * spi_flash_escribe_cabecera(), que es codigo nuevo y era la unica
     * pieza del camino nuevo sin banco.
     *
     * Lo que se exige: que la cabecera vieja desaparezca, que la nueva este
     * EXACTAMENTE una vez, que los bloques de datos sigan donde estaban, y
     * que no se toque nada fuera del volumen.
     * ================================================================== */
    /* ==================================================================
     * CUANTO CUESTA CADA TROZO, QUE ES LO QUE NO MEDI
     * ==================================================================
     *
     * Al pasar la carta de fax a fichero-que-crece, cada bloque de 4 kB
     * pasa por spi_flash_anade_trozo(), que recorre la tabla de asignacion
     * entera, asigna clusters y reescribe las DOS copias de la FAT y la
     * entrada de directorio. Antes era una escritura en una direccion ya
     * conocida.
     *
     * Lo cambie sin medirlo. Esto lo mide: los microsegundos que el
     * firmware se queda esperando al chip, que es exactamente el tiempo que
     * el bucle principal no avanza - ni tacto, ni pantalla.
     * ================================================================== */
    /* ==================================================================
     * LA CARTA DE FAX, COMO LA GRABA LA RADIO
     * ------------------------------------------------------------------
     *
     * Las dos cosas salian del mismo sitio. El fichero crecia llamando a
     * spi_flash_anade_trozo() una vez por bloque de 4 kB, y eso cuesta
     * 303 ms de media -lo medi aqui mismo, DESPUES de haberlo puesto en la
     * radio, que es justo el orden que no hay que seguir-. Con el bucle
     * principal parado trescientos milisegundos cada dos segundos y medio,
     * el tactil no se mira.
     *
     * Esto graba una carta como la graba el fax -un tramo cuando hace
     * falta, y dentro bloques sueltos- y mide los dos tiempos por
     * separado: lo que para un bloque normal y lo que para pedir tramo.
     * ================================================================== */
    printf("\n  grabar una carta como la graba el fax\n");
    {
/*
 * EL TAMAÑO DE VERDAD, no uno de muestra - 20/10/2026.
 *
 * Esto nacio con 40 bloques "que ya ejercita varios tramos". Pero lo que
 * el usuario graba es una carta de WFX_ALTO_MAX = 1024 renglones de 756
 * bytes mas la cabecera, o sea 190 bloques y DOCE tramos. Y lo que hay que
 * probar es eso, porque el tope de spi_flash_amplia() y la cuenta de
 * tramos solo se tocan al final: con 40 bloques no se llega a rozar.
 */
/*
 * Y EL 1024 TAMPOCO SE ESCRIBE AQUI - 20/10/2026.
 *
 * Ponia 1024 a mano. WFX_ALTO_MAX ya ha cambiado tres veces hoy -1024,
 * 1280, 1536- y este banco se habria quedado probando la primera mientras
 * la radio graba la tercera: justo la familia de fallo que gen_wfx.py vino
 * a cerrar en sim/fax_guarda.c. Sale del mismo sitio.
 */
#define CARTA_BLOQUES ((756U * WFX_ALTO_MAX + 4096U + 4095U) / 4096U)
        static uint8_t bloque[4096];
        static uint8_t escrito[CARTA_BLOQUES][4096];
        static uint8_t leido[CARTA_BLOQUES * 4096U];
        spi_anade_r_t porque = SPI_ANADE_OK;
        uint64_t peor_blq = 0ULL, peor_tramo = 0ULL, total = 0ULL;
        uint32_t ext_addr = 0U, ext_n = 0U, ext_i = 0U;
        uint32_t cab_addr = 0U;
        uint32_t k, i, tramos = 0U, n_ent = 0U;
        uint8_t  ok = 1U;
        int fallos_antes = g_fallos_chip;

        g_us_sector = 45000U; g_us_bloque = 150000U;
        g_us_pagina = 400U;   g_us_espera = 100U;

        vecinos_apunta();

        for (k = 0U; k < CARTA_BLOQUES && ok; k++) {
            uint64_t t0 = g_us_bloqueado, d;

            /* Cada bloque, distinto de todos los demas: un patron que se
             * repite deja que un barrido lo encuentre donde no esta, y eso
             * ya me hizo perder un rato con el caso de antes. */
            for (i = 0U; i < 4096U; i++) {
                bloque[i] = (uint8_t)((i * 7U) + (k * 101U) + (i >> 5));
            }
            memcpy(escrito[k], bloque, 4096U);

            if (ext_i >= ext_n) {
                uint32_t dados = 0U;
                if (!spi_flash_amplia("WFAX001 ", "BMP", 16U,
                                      &ext_addr, &dados, &porque)) {
                    printf("      no cabe el tramo %u: %s\n",
                           (unsigned)tramos, spi_flash_anade_porque_txt(porque));
                    ok = 0U;
                    break;
                }
                ext_n = dados;
                ext_i = 0U;
                tramos++;
                d = g_us_bloqueado - t0;
                if (d > peor_tramo) { peor_tramo = d; }
                t0 = g_us_bloqueado;   /* el bloque se mide aparte del tramo */
            }
            if (k == 0U) { cab_addr = ext_addr; }
            spi_flash_write_block_4k(ext_addr + ext_i * 4096U, bloque);
            ext_i++;

            d = g_us_bloqueado - t0;
            total += d;
            if (d > peor_blq) { peor_blq = d; }
        }
        comprueba(ok == 1U, "la carta entera entra en el pendrive");
        printf("      %u bloques en %u tramos: bloque peor %llu ms, "
               "media %llu ms; pedir tramo %llu ms\n",
               (unsigned)k, (unsigned)tramos,
               (unsigned long long)(peor_blq / 1000ULL),
               (unsigned long long)(total / (k ? k : 1) / 1000ULL),
               (unsigned long long)(peor_tramo / 1000ULL));

        /*
         * El numero que importa. Un bloque entra cada dos segundos y medio
         * a 120 lineas por minuto: lo que no puede es parar el bucle un
         * cuarto de ese rato.
         */
        comprueba(peor_blq < 100000ULL,
                  "un bloque no para el bucle mas de 100 ms");
        /*
         * Y el tramo, que es el caro, solo cae una vez cada dieciseis
         * bloques: cuarenta segundos a 120 lpm.
         */
        comprueba(peor_tramo < 400000ULL,
                  "pedir un tramo no para el bucle mas de 400 ms");
        comprueba(tramos == (CARTA_BLOQUES + 15U) / 16U,
                  "y se piden los tramos justos, ni uno de mas");

        /* UN fichero, no cuarenta. Se cuentan las entradas del directorio
         * que se llaman asi: el sintoma del usuario era justo esto. */
        {
            uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
            uint32_t datos = raiz() + rde * 32U, off;
            for (off = raiz(); off < datos; off += 32U) {
                if ((g_chip[off + 11] & 0x0FU) == 0x0FU) { continue; }
                if (memcmp(&g_chip[off], "WFAX001 BMP", 11) == 0) { n_ent++; }
            }
        }
        comprueba(n_ent == 1U, "y la carta es UN fichero, no uno por bloque");

        /* Y lo escrito se vuelve a leer, en orden, siguiendo la cadena. */
        {
            uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
            uint32_t datos = raiz() + rde * 32U, off, c = 0U, tam = 0U;
            uint32_t leidos = 0U, vueltas = 0U;

            for (off = raiz(); off < datos; off += 32U) {
                if (memcmp(&g_chip[off], "WFAX001 BMP", 11) != 0) { continue; }
                c = (uint32_t)(g_chip[off + 26] | (g_chip[off + 27] << 8));
                tam = (uint32_t)(g_chip[off + 28] | (g_chip[off + 29] << 8) |
                                ((uint32_t)g_chip[off + 30] << 16) |
                                ((uint32_t)g_chip[off + 31] << 24));
                break;
            }
            comprueba(tam >= CARTA_BLOQUES * 4096U,
                      "la entrada de directorio cuenta todo lo escrito");
            while (c >= 2U && c < 0x0FF8U && leidos < sizeof leido) {
                uint32_t o = c + (c / 2U), j;
                uint8_t *f = &g_chip[fat1() + o];
                uint16_t v = (uint16_t)(f[0] | (f[1] << 8));
                uint32_t base = datos + (c - 2U) * clus_tam();

                for (j = 0U; j < clus_tam() && leidos < sizeof leido; j++) {
                    leido[leidos++] = g_chip[base + j];
                }
                c = (c & 1U) ? (uint32_t)(v >> 4) : (uint32_t)(v & 0x0FFFU);
                if (++vueltas > 4096U) { break; }
            }
            comprueba(leidos == sizeof leido,
                      "y la cadena tiene todos los bloques");
            comprueba(leidos == sizeof leido &&
                      memcmp(leido, escrito, sizeof leido) == 0,
                      "con los renglones en su sitio y en su orden");
        }

        /*
         * LA CABECERA. El BMP se escribio con la altura tope y al cerrar
         * se rehace con la de verdad, en el bloque 0 - cuya direccion se
         * guardo al escribirlo, sin ir a buscarla al directorio.
         */
        for (i = 0U; i < 4096U; i++) { bloque[i] = (uint8_t)(0x50U + (i & 63U)); }
        spi_flash_write_block_4k(cab_addr, bloque);
        comprueba(memcmp(&g_chip[cab_addr], bloque, 4096U) == 0,
                  "la cabecera se rehace donde cayo el bloque 0");

        /*
         * Y EL RECORTE. Media carta deja medio tramo sin usar: hasta
         * quince bloques por fichero. Tiene que volver al volumen, y
         * siguiendo la cadena - un fichero a tramos no es seguido, y
         * recortarlo como si lo fuera libera clusters del vecino.
         */
        {
            uint32_t sobra = (CARTA_BLOQUES - 10U) * 4096U;
            comprueba(spi_flash_recorta_cadena("WFAX001 ", "BMP", sobra) == 1U,
                      "el recorte que sigue la cadena dice que si");
            {
                uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
                uint32_t datos = raiz() + rde * 32U, off, tam = 0U;
                for (off = raiz(); off < datos; off += 32U) {
                    if (memcmp(&g_chip[off], "WFAX001 BMP", 11) != 0) { continue; }
                    tam = (uint32_t)(g_chip[off + 28] | (g_chip[off + 29] << 8) |
                                    ((uint32_t)g_chip[off + 30] << 16) |
                                    ((uint32_t)g_chip[off + 31] << 24));
                    break;
                }
                comprueba(tam == sobra, "y el fichero mide lo que se uso");
            }
            {
                uint32_t tam = 0U;
                comprueba(vecino_suma("WFAX001 BMP", &tam) != 0U && tam == sobra,
                          "y su cadena sigue cuadrando con el tamano");
            }
        }

        {
            const char *roto = vecinos_mira(0);
            if (roto) { printf("      se ha estropeado %s\n", roto); }
            comprueba(roto == 0, "y no se ha llevado por delante a ningun vecino");
        }
        comprueba(g_fallos_chip == fallos_antes,
                  "y no se ha tocado nada fuera del volumen");
#undef CARTA_BLOQUES
    }

    /* ==================================================================
     * LA MISMA CARTA, EN UN PENDRIVE REPARTIDO
     * ------------------------------------------------------------------
     * El caso de arriba sale contiguo porque el volumen iba en orden. Un
     * pendrive con meses de uso no va en orden: al borrar un fichero queda
     * un agujero POR DEBAJO de lo ya escrito, y el tramo siguiente se mete
     * ahi. La cadena salta hacia atras.
     *
     * Y ahi es donde spi_flash_recorta() -la de siempre- hace daño: libera
     * "del cluster primero + n en adelante", o sea los que van detras del
     * PRIMER tramo, que son del vecino. Es exactamente la familia de fallo
     * que se llevo por delante el CONFIG.CSV del usuario.
     *
     * Por eso el fax recorta con spi_flash_recorta_cadena(). Esto lo
     * comprueba con un vecino puesto justo donde dolería.
     * ================================================================== */
    printf("\n  y la misma carta en un pendrive repartido\n");
    {
        static uint8_t bloque[4096];
        spi_anade_r_t porque = SPI_ANADE_OK;
        uint32_t ext_addr = 0U, ext_n = 0U, ext_i = 0U, dados = 0U;
        uint32_t k, c, primero = 0U, atras = 0U, vueltas = 0U;
        uint8_t  ok = 1U;
        int fallos_antes = g_fallos_chip;

        pc_pone_gordo("HUECO   BIN", 32U * 1024U);
        pc_pone_gordo("VECINO  BIN", 32U * 1024U);
        vecinos_apunta();

        for (k = 0U; k < 8U && ok; k++) {
            uint32_t i;
            for (i = 0U; i < 4096U; i++) {
                bloque[i] = (uint8_t)((i * 13U) + (k * 71U) + (i >> 4));
            }
            if (ext_i >= ext_n) {
                if (!spi_flash_amplia("WFAX002 ", "BMP", 4U,
                                      &ext_addr, &dados, &porque)) {
                    printf("      no cabe: %s\n", spi_flash_anade_porque_txt(porque));
                    ok = 0U;
                    break;
                }
                ext_n = dados;
                ext_i = 0U;
            }
            spi_flash_write_block_4k(ext_addr + ext_i * 4096U, bloque);
            ext_i++;
            /* En mitad de la carta, el PC borra un fichero de mas abajo:
             * el tramo siguiente caera en ese agujero. */
            if (k == 3U) { pc_borra("HUECO   BIN"); }
        }
        comprueba(ok == 1U, "la carta cabe aunque el disco este repartido");

        /* ¿Salta de verdad hacia atras? Si no, esta prueba no esta
         * probando lo que dice probar y mas vale saberlo. */
        {
            uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
            uint32_t datos = raiz() + rde * 32U, off;
            for (off = raiz(); off < datos; off += 32U) {
                if (memcmp(&g_chip[off], "WFAX002 BMP", 11) != 0) { continue; }
                primero = (uint32_t)(g_chip[off + 26] | (g_chip[off + 27] << 8));
                break;
            }
            c = primero;
            while (c >= 2U && c < 0x0FF8U && vueltas < 4096U) {
                uint32_t o = c + (c / 2U);
                uint8_t *f = &g_chip[fat1() + o];
                uint16_t v = (uint16_t)(f[0] | (f[1] << 8));
                uint32_t sig = (c & 1U) ? (uint32_t)(v >> 4) : (uint32_t)(v & 0x0FFFU);
                if (sig >= 2U && sig < 0x0FF8U && sig < c) { atras++; }
                c = sig;
                vueltas++;
            }
        }
        comprueba(atras > 0U, "y su cadena salta hacia atras, que es el caso malo");

        /* Y AHORA EL RECORTE, que es donde la de siempre haria daño. */
        comprueba(spi_flash_recorta_cadena("WFAX002 ", "BMP", 5U * 4096U) == 1U,
                  "el recorte que sigue la cadena dice que si");
        {
            uint32_t tam = 0U;
            comprueba(vecino_suma("WFAX002 BMP", &tam) != 0U && tam == 5U * 4096U,
                      "y la carta recortada sigue cuadrando");
        }
        {
            const char *roto = vecinos_mira("HUECO   BIN");
            if (roto) { printf("      se ha estropeado %s\n", roto); }
            comprueba(roto == 0, "y el vecino de debajo sigue entero");
        }
        comprueba(g_fallos_chip == fallos_antes,
                  "y no se ha tocado nada fuera del volumen");
    }

    printf("\n  %s\n\n", fallos ? "HAY FALLOS" : "hasta aqui bien "
                                  "(falta que fsck.fat diga lo suyo)");
    return fallos ? 1 : 0;
}
