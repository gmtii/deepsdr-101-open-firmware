/*
 * GUARDAR Y RECUPERAR LOS AJUSTES - 25/09/2026.
 *
 * NACE DE UN SINTOMA REAL. Pedido por el usuario.
 *
 * Y AL IR A BUSCARLO, EL HUECO: de los caminos de escritura a la flash, el
 * banco cubria anadir trozos, reservar un fichero entero y recortarlo. El
 * que usan los AJUSTES -escribir o actualizar un fichero pequeno, leerlo por
 * nombre, y el guardado asincrono por bloques- no lo cubria NADIE. Es el
 * camino que mas veces corre en la radio, varias veces por sesion, y era el
 * unico sin banco.
 *
 * Asi que esto hace exactamente lo que hace la radio, contra el simulador
 * del chip a nivel de patas:
 *
 *   1. Guardar CONFIG.CSV por primera vez (no existe).
 *   2. Leerlo y comprobar que sale lo mismo que entro.
 *   3. Cambiar un ajuste y volver a guardar, muchas veces, como quien toca
 *      la velocidad de la cascada y el tema una y otra vez.
 *   4. Leerlo otra vez: tiene que salir lo ULTIMO, no lo primero.
 *   5. Lo mismo por el camino ASINCRONO, que es el que usa de verdad cuando
 *      el fichero ya existe (settings_poll -> spi_flash_async_save_start).
 *   6. Y con vecinos alrededor, comprobando que no se los lleva por delante.
 *
 * Si esto pasa y en la radio sigue sin guardarse, el fallo no esta en la
 * flash y hay que mirar mas arriba. Si esto falla, esta aqui.
 */
#include "spi_flash.h"
#include "w25q_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOLUMEN (1024U * 1024U)

/* Los mismos que usa settings.c. Copiados a proposito y no incluidos: este
 * banco prueba el DRIVER, y arrastrar settings.c traeria medio firmware. */
#define CONFIG_NOMBRE "CONFIG  "
#define CONFIG_EXT    "CSV"

static int fallos = 0;

static void comprueba(int ok, const char *que)
{
    printf("  %-58s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

static uint32_t bps(void)   { return (uint32_t)(g_chip[11] | (g_chip[12] << 8)); }
static uint32_t fat1(void)  { return (uint32_t)(g_chip[14] | (g_chip[15] << 8)) * bps(); }
static uint32_t fatsz(void) { return (uint32_t)(g_chip[22] | (g_chip[23] << 8)) * bps(); }
static uint32_t raiz(void)  { return fat1() + (uint32_t)g_chip[16] * fatsz(); }

static uint32_t s_prox_cluster = 2U;

/* Un fichero puesto por el PC, como los que ya hay en el volumen. */
static void pc_pone(const char *n11, uint32_t bytes, uint8_t patron)
{
    uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
    uint32_t datos = raiz() + rde * 32U;
    uint32_t clus = (bytes + bps() - 1U) / bps();
    uint32_t primero = s_prox_cluster, off, c;
    uint8_t *e;

    s_prox_cluster += clus;
    for (off = raiz(); off < datos; off += 32U) {
        if (g_chip[off] == 0x00U || g_chip[off] == 0xE5U) { break; }
    }
    e = &g_chip[off];
    memcpy(e, n11, 11);
    e[11] = 0x20U;
    e[26] = (uint8_t)(primero & 0xFFU); e[27] = (uint8_t)(primero >> 8);
    e[28] = (uint8_t)(bytes & 0xFFU);         e[29] = (uint8_t)((bytes >> 8) & 0xFFU);
    e[30] = (uint8_t)((bytes >> 16) & 0xFFU); e[31] = (uint8_t)((bytes >> 24) & 0xFFU);
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
    memset(&g_chip[datos + (primero - 2U) * bps()], patron, bytes);
}

/* El CSV de verdad tiene esta pinta: claves y valores, una por linea. Lo que
 * importa aqui no es el contenido sino que salga IGUAL que entro. */
/*
 * El CSV de verdad mide SIEMPRE 1536 bytes: build_csv() lo rellena con
 * lineas en blanco hasta un tamano fijo para que el guardado rapido pueda
 * dar por hecho que ocupa los mismos bloques. Y eso importa AQUI: 1536
 * bytes son TRES clusters de 512, o sea que leerlo obliga a seguir la
 * cadena de la FAT. La primera version de este banco escribia 79 bytes -un
 * solo cluster- y entonces la cadena no se consulta nunca: el banco pasaba
 * con el fallo dentro y sin el, que es la peor clase de banco.
 */
#define CSV_TAMANO_REAL 1536U

static uint32_t haz_csv(char *out, uint32_t max, int wf, int tema, unsigned long hz)
{
    int n = snprintf(out, max,
                     "vfo_hz,%lu\nmode,1\nwf_speed,%d\ntema,%d\nvol,0\n"
                     "step,1000\nbw,3\npga,0\nsmooth,50\n", hz, wf, tema);
    uint32_t p = (uint32_t)((n < 0) ? 0 : n);

    while ((p < CSV_TAMANO_REAL) && (p < max - 1U)) { out[p++] = '\n'; }
    return p;
}

/*
 * EL ARRANQUE DE VERDAD. Un proceso escribe y vuelca el chip a un fichero;
 * OTRO proceso lo carga y solo lee. Eso es lo que pasa al apagar y encender:
 * toda la memoria del driver -incluida la geometria que lleva cacheada-
 * arranca a cero. Hacerlo en el mismo proceso no valdria: la geometria
 * seguiria leida de antes y el banco diria que todo va bien.
 *
 * Nace de una medida en la radio: el contador de guardados marca "1 ok, 0
 * mal" y el testigo del arranque marca "0 claves". O sea que se escribe y
 * luego no se encuentra.
 */
/*
 * EL VOLUMEN DESDE CERO, otra vez, para poder probar varias colocaciones de
 * CONFIG.CSV en la misma ejecucion. Recarga la plantilla vacia y pone el
 * repartidor de clusters a su sitio.
 */
static const char *s_plantilla = "out/fat12_vacio.img";

static void volumen_de_cero(void)
{
    FILE *f = fopen(s_plantilla, "rb");

    w25q_reset();
    if (f) {
        if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) { printf("  plantilla corta\n"); }
        fclose(f);
    }
    g_intocable_ini = VOLUMEN;
    g_intocable_fin = 2U * 1024U * 1024U;
    s_prox_cluster = 2U;
}

#define CSV_MAX 2048

static uint32_t csv_de_mentira(char *out, uint32_t max, int sello)
{
    return haz_csv(out, max, sello % 5, sello % 3, 7000000UL + (unsigned long)sello);
}

static int solo_lee(const char *img)
{
    char leido[512];
    uint32_t n;
    FILE *f = fopen(img, "rb");

    printf("\n=== Arranque en frio: leer CONFIG.CSV sin haber escrito nada ===\n\n");
    if (!f) { printf("  falta %s\n", img); return 1; }
    w25q_reset();
    if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) { printf("  %s no mide 1 MB\n", img); fclose(f); return 1; }
    fclose(f);
    g_intocable_ini = VOLUMEN; g_intocable_fin = 2U*1024U*1024U;

    spi_flash_init();
    n = spi_flash_read_file_by_name(CONFIG_NOMBRE, CONFIG_EXT, (uint8_t *)leido, sizeof leido);
    printf("  spi_flash_read_file_by_name devuelve %u bytes\n", n);
    comprueba(n > 0U, "CONFIG.CSV se encuentra y se lee al arrancar");
    if (n > 0U) {
        printf("  primeros 40 bytes: %.40s\n", leido);
    }
    if (fallos) { printf("\n  HAY FALLOS (%d)\n\n", fallos); return 1; }
    printf("\n  TODO CORRECTO\n\n");
    return 0;
}

int main(int argc, char **argv)
{
    const char *plantilla = (argc > 1) ? argv[1] : "out/fat12_vacio.img";

    s_plantilla = plantilla;

    if (argc > 2 && strcmp(argv[1], "--lee") == 0) { return solo_lee(argv[2]); }
    if (argc > 1 && strcmp(argv[1], "--lee") == 0) { return solo_lee("out/volumen_con_config.img"); }
    char csv[2048], leido[2048];
    uint32_t len, n;
    FILE *f;
    int i;

    printf("\n=== Guardar y recuperar los ajustes (CONFIG.CSV) ===\n\n");

    w25q_reset();
    f = fopen(plantilla, "rb");
    if (!f) { printf("  falta %s\n", plantilla); return 1; }
    if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
        printf("  %s no mide 1 MB\n", plantilla); fclose(f); return 1;
    }
    fclose(f);
    g_intocable_ini = VOLUMEN;
    g_intocable_fin = 2U * 1024U * 1024U;

    spi_flash_init();

    /*
     * UN DIRECTORIO CON HISTORIA, no uno de laboratorio. En la radio del
     * usuario el volumen lleva meses: ficheros que el PC puso y quito, huecos
     * donde estuvieron, y ficheros reales DETRAS de esos huecos. Ese reparto
     * es el que decide si una busqueda que se para en el primer 0x00 llega o
     * no llega a CONFIG.CSV, y es justo lo que un volumen recien formateado
     * no puede ensenar.
     */
    pc_pone("ZONAALTABIN", 64U * 1024U, 0xA5U);
    pc_pone("CONFIG  CSV", 1536U, 0x11U);
    pc_pone("CHANNEL CSV", 1024U, 0x5AU);
    pc_pone("RTTY    O  ", 2048U, 0x22U);
    pc_pone("PRUEBA  BIN", 8192U, 0x33U);
    {
        /* El PC borra dos: sus entradas quedan marcadas con 0xE5 pero el
         * resto del directorio no se mueve. */
        uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
        uint32_t datos = raiz() + rde * 32U, off;

        for (off = raiz(); off < datos; off += 32U) {
            if (memcmp(&g_chip[off], "RTTY    O  ", 11) == 0 ||
                memcmp(&g_chip[off], "PRUEBA  BIN", 11) == 0) {
                g_chip[off] = 0xE5U;
            }
        }
    }
    printf("  directorio con historia: ZONAALTA.BIN, CONFIG.CSV, channel.csv\n");
    printf("  y dos entradas borradas (0xE5) detras\n\n");

    /* --- 1. el primer guardado, cuando CONFIG.CSV todavia no existe --- */
    len = haz_csv(csv, sizeof csv, 30, 0, 7100000UL);
    comprueba(spi_flash_write_or_update_file(CONFIG_NOMBRE, CONFIG_EXT,
                                             (const uint8_t *)csv, len) != 0,
              "el primer guardado, con CONFIG.CSV sin existir");

    n = spi_flash_read_file_by_name(CONFIG_NOMBRE, CONFIG_EXT,
                                    (uint8_t *)leido, sizeof leido);
    comprueba(n == len && memcmp(leido, csv, len) == 0,
              "y lo leido es exactamente lo escrito");

    /* --- 2. cambiar un ajuste y volver a guardar, muchas veces --- */
    for (i = 0; i < 20; i++) {
        len = haz_csv(csv, sizeof csv, (i & 1) ? 60 : 15, i % 6, 7100000UL + (unsigned long)i);
        if (!spi_flash_write_or_update_file(CONFIG_NOMBRE, CONFIG_EXT,
                                            (const uint8_t *)csv, len)) {
            printf("  el guardado numero %d fallo\n", i + 1);
            fallos++;
            break;
        }
    }
    n = spi_flash_read_file_by_name(CONFIG_NOMBRE, CONFIG_EXT,
                                    (uint8_t *)leido, sizeof leido);
    comprueba(n == len && memcmp(leido, csv, len) == 0,
              "tras 20 guardados seguidos sale el ULTIMO, no el primero");

    /* --- 3. el camino asincrono, que es el que usa la radio de verdad --- */
    len = haz_csv(csv, sizeof csv, 60, 3, 14074000UL);
    if (spi_flash_async_save_start(CONFIG_NOMBRE, CONFIG_EXT,
                                   (const uint8_t *)csv, len)) {
        int vueltas = 0;
        spi_flash_async_status_t e = SPI_FLASH_ASYNC_BUSY;

        while ((e == SPI_FLASH_ASYNC_BUSY) && (vueltas < 100000)) {
            e = spi_flash_async_save_poll();
            vueltas++;
        }
        comprueba(e == SPI_FLASH_ASYNC_DONE, "el guardado asincrono termina bien");
        n = spi_flash_read_file_by_name(CONFIG_NOMBRE, CONFIG_EXT,
                                        (uint8_t *)leido, sizeof leido);
        comprueba(n == len && memcmp(leido, csv, len) == 0,
                  "y lo que deja el asincrono es lo que se le dio");
    } else {
        comprueba(0, "el camino asincrono se puede arrancar (el fichero ya existe)");
    }

    /* ---------------------------------------------------------------
     * 3b. LA CADENA ROTA. 25/09/2026.
     * ---------------------------------------------------------------
     * El caso que explica el sintoma del usuario: la entrada de CONFIG.CSV
     * esta bien y apunta a su cluster, pero en la FAT ese cluster figura
     * como LIBRE. Asi queda un volumen que ha pasado por un PC, o por un
     * apagon a media escritura.
     *
     * Lo que hacia antes del arreglo: el camino rapido del guardado no
     * mira la cadena -se la salta a proposito para ir mas deprisa-, asi
     * que escribia los datos, actualizaba el tamano y devolvia "bien".
     * Pero la LECTURA si sigue la cadena, y la cadena no lleva a ninguna
     * parte: devolvia cero bytes. El contador decia "1 ok, 0 mal" y el
     * arranque decia "0 claves", y las dos cosas eran verdad.
     */
    {
        uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
        uint32_t datos = raiz() + rde * 32U, off, primero = 0U;

        for (off = raiz(); off < datos; off += 32U) {
            if (memcmp(&g_chip[off], CONFIG_NOMBRE, 8) == 0 &&
                memcmp(&g_chip[off + 8], CONFIG_EXT, 3) == 0) {
                primero = (uint32_t)(g_chip[off + 26] | (g_chip[off + 27] << 8));
                break;
            }
        }
        printf("\n  --- se rompe a mala idea la cadena de CONFIG.CSV (cluster %u a LIBRE)\n", primero);
        if (primero >= 2U) {
            uint32_t o = primero + (primero / 2U), copia;
            for (copia = 0U; copia < 2U; copia++) {
                uint8_t *f = &g_chip[fat1() + copia * fatsz() + o];
                uint16_t pv = (uint16_t)(f[0] | (f[1] << 8));
                pv = (primero & 1U) ? (uint16_t)(pv & 0x000FU) : (uint16_t)(pv & 0xF000U);
                f[0] = (uint8_t)(pv & 0xFFU); f[1] = (uint8_t)(pv >> 8);
            }
        }
        len = haz_csv(csv, sizeof csv, 60, 5, 21074000UL);
        comprueba(spi_flash_write_or_update_file(CONFIG_NOMBRE, CONFIG_EXT,
                                                 (const uint8_t *)csv, len) != 0,
                  "con la cadena rota, el guardado sigue diciendo que si");
        n = spi_flash_read_file_by_name(CONFIG_NOMBRE, CONFIG_EXT,
                                        (uint8_t *)leido, sizeof leido);
        comprueba(n == len && memcmp(leido, csv, len) == 0,
                  "Y AHORA SE PUEDE LEER (esto es lo que fallaba)");
    }

    /* --- 4. los vecinos siguen enteros --- */
    /*
     * LOS DATOS, NO SOLO LA ENTRADA DE DIRECTORIO - 30/09/2026.
     *
     * Esta seccion decia en su cabecera "con vecinos alrededor, comprobando
     * que no se los lleva por delante", y lo unico que miraba era que sus
     * ENTRADAS siguieran en el directorio. Las entradas viven en la raiz,
     * que esta en otro bloque, asi que ese vistazo no podia fallar nunca.
     *
     * Y mientras tanto si se los llevaba: el guardado borraba el bloque de
     * 4 kB ENTERO -ocho clusters- y CONFIG.CSV solo ocupa tres. Los 1024
     * bytes de CHANNEL.CSV pasaban de 0x5A a 0xFF en el primer guardado.
     * Un banco que promete mirar algo y mira otra cosa es peor que no
     * tenerlo, porque tapa el hueco en la lista.
     */
    {
        uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
        uint32_t datos = raiz() + rde * 32U, off;
        int za = 0, ch = 0;

        for (off = raiz(); off < datos; off += 32U) {
            if (memcmp(&g_chip[off], "ZONAALTABIN", 11) == 0) { za = 1; }
            if (memcmp(&g_chip[off], "CHANNEL CSV", 11) == 0) { ch = 1; }
        }
        comprueba(za && ch, "ZONAALTA.BIN y channel.csv siguen en el directorio");

        /* Y AHORA SUS BYTES. Se leen por el mismo camino que usaria la
         * radio, no mirando el chip a mano: si el fichero se lee, es que
         * esta. */
        {
            static uint8_t buf[2048];
            uint32_t n2 = spi_flash_read_file_by_name("CHANNEL ", "CSV",
                                                      buf, sizeof buf);
            uint32_t i2, malos = 0U;

            for (i2 = 0U; i2 < n2; i2++) { if (buf[i2] != 0x5AU) { malos++; } }
            comprueba(n2 == 1024U && malos == 0U,
                      "los 1024 bytes de CHANNEL.CSV siguen intactos");
            if (n2 != 1024U || malos != 0U) {
                printf("        leidos %u bytes, %u distintos de 0x5A\n",
                       (unsigned)n2, (unsigned)malos);
            }
        }
        {
            static uint8_t buf[4096];
            uint32_t n2 = spi_flash_read_file_by_name("ZONAALTA", "BIN",
                                                      buf, sizeof buf);
            uint32_t i2, malos = 0U;

            for (i2 = 0U; i2 < n2; i2++) { if (buf[i2] != 0xA5U) { malos++; } }
            comprueba(n2 == sizeof buf && malos == 0U,
                      "el principio de ZONAALTA.BIN sigue intacto");
        }
    }

    /* --- 5. CONFIG.CSV que NO empieza en frontera de bloque --- */
    /*
     * *** El caso que se perdia en silencio. ***
     *
     * Un bloque son 4 kB = ocho clusters de 512. Si el primer cluster del
     * fichero no es el primero de su bloque, el escritor hacia `& ~4095` y
     * los datos acababan al PRINCIPIO del bloque, no donde vive el fichero.
     * Cada guardado decia que habia ido bien y cada arranque leia 0xFF.
     *
     * Pasa en cuanto alguien edita o restaura CONFIG.CSV desde el PC: el
     * sistema de ficheros lo coloca en el primer hueco, que con
     * probabilidad 7/8 no cae en frontera.
     *
     * Se prueban LAS OCHO posiciones dentro del bloque, no una: con una
     * sola se acierta o se falla por suerte, que es justo lo que le pasaba
     * a este banco -le tocaba un cluster alineado y por eso pasaba-.
     */
    {
        uint32_t desfase;

        for (desfase = 0U; desfase < 8U; desfase++) {
            char csv2[CSV_MAX];
            static char leido2[CSV_MAX];
            uint32_t len2, n2;
            uint32_t rde = (uint32_t)(g_chip[17] | (g_chip[18] << 8));
            uint32_t datos = raiz() + rde * 32U, off;
            uint8_t *e = 0;

            /* Se reconstruye el volumen desde cero y se coloca CONFIG.CSV
             * en el cluster que toca para este desfase. */
            volumen_de_cero();
            pc_pone("CHANNEL CSV", 1024U, 0x5AU);
            while (((s_prox_cluster - 2U) % 8U) != desfase) {
                pc_pone("RELLENO BIN", 512U, 0x77U);
            }
            pc_pone("CONFIG  CSV", 1536U, 0x11U);
            spi_flash_init();

            len2 = csv_de_mentira(csv2, sizeof csv2, (int)desfase);
            if (!spi_flash_write_or_update_file(CONFIG_NOMBRE, CONFIG_EXT,
                                                (const uint8_t *)csv2, len2)) {
                comprueba(0, "el guardado dice que si");
                continue;
            }
            n2 = spi_flash_read_file_by_name(CONFIG_NOMBRE, CONFIG_EXT,
                                             (uint8_t *)leido2, sizeof leido2);
            {
                char q[80];
                sprintf(q, "desfase %u: lo guardado es lo que se lee",
                        (unsigned)desfase);
                comprueba(n2 == len2 && memcmp(leido2, csv2, len2) == 0, q);
            }
            (void)datos; (void)off; (void)e;
        }
    }
    comprueba(g_fallos_chip == 0, "el chip no se ha quejado ni una vez");

    /* Y el volumen tal como queda, para que otro proceso lo lea en frio. */
    {
        FILE *g = fopen("out/volumen_con_config.img", "wb");
        if (g) { fwrite(g_chip, 1, VOLUMEN, g); fclose(g);
                 printf("  volcado a out/volumen_con_config.img para la prueba en frio\n"); }
    }

    if (fallos) { printf("\n  HAY FALLOS (%d)\n\n", fallos); return 1; }
    printf("\n  TODO CORRECTO\n\n");
    return 0;
}
