/*
 * Banco de "cargar un fichero TROCEADO del pendrive a la zona alta"
 * (spi_flash_restaura_abre() + spi_flash_volcado_paso(), del
 * User/spi_flash.c del firmware).
 *
 * DE DONDE SALE ESTO
 * ------------------
 * Pedido por el usuario, 28/09/2026.
 *
 * Y estaba. El cargador EXIGIA que los clusters del fichero fueran
 * seguidos, y el motivo de que no lo fueran era mio: al juntar las dos
 * bases de datos en un solo BD.BIN -que es lo que el pidio- el bloque
 * que hay que meter seguido paso de 258 kB a 512, y en un volumen de 1 MB
 * con historia Windows no tiene medio mega seguido que dar.
 *
 * LO QUE SE MIDE AQUI
 * -------------------
 * Que los bytes que aterrizan en la zona alta son los del fichero, EN SU
 * ORDEN, con la cadena de clusters montada a mala idea: saltando adelante
 * y atras en cada eslabon, que es el caso que el codigo viejo declaraba
 * imposible de hacer bien.
 *
 * Y se mide byte a byte contra el contenido que este banco escribio, no
 * contra una suma: una suma se puede cuadrar con los bloques cambiados de
 * sitio, y cambiar bloques de sitio es EXACTAMENTE el fallo que puede
 * tener un recorrido de cadena mal escrito.
 *
 * Debajo hay un W25Q16 simulado a nivel de pin (sim/w25q_sim.c), asi que
 * el spi_flash.c que corre aqui es el del firmware sin una linea
 * cambiada, bit-banging incluido.
 *
 * POR QUE SE PUEDE ADMITIR EL TROCEADO AHORA Y ANTES NO
 * -----------------------------------------------------
 * El comentario que lo prohibia decia que con un mapa mal escrito "los
 * bytes acabarian en el sitio equivocado SIN QUE NADA SE QUEJE". Era
 * verdad. Ya no: BD.BIN lleva una suma por seccion que
 * zona_alta_verifica() comprueba ANTES de que el cargador borre el
 * fichero del disco. La red existe; este banco es la otra mitad.
 *
 * Y SE HA COMPROBADO QUE ESTE BANCO SABE DECIR QUE NO. Un banco que pasa
 * contra un codigo roto no vale nada, asi que antes de darlo por bueno se
 * le rompio el recorrido a proposito, en un arbol aparte, de dos formas:
 *
 *   - leyendo seguido desde el primer cluster (o sea, el codigo viejo
 *     haciendo como si el fichero fuera contiguo): 260.948 bytes mal, el
 *     primero en el 512, que es justo donde acaba el primer cluster;
 *   - cambiando el par/impar de fat12_del_chip(), que es el fallo clasico
 *     de FAT12 y el que mas facil se cuela: lo mismo, y ademas se lleva
 *     por delante el caso del fichero seguido.
 *
 * Las dos saltaron a la primera.
 */
#include "spi_flash.h"
#include "w25q_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOLUMEN   (1024U * 1024U)
#define CHIP      (2U * 1024U * 1024U)
#define ZA_INI    0x101000UL          /* la zona alta de la radio */
#define ZA_FIN    0x180000UL

static int fallos = 0;

static void comprueba(int ok, const char *que)
{
    printf("  %-58s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/*
 * El contenido del fichero de prueba. Tiene que depender del
 * DESPLAZAMIENTO y tener periodo largo: con un relleno que se repita cada
 * sector, un recorrido que cambie dos clusters de sitio sale idéntico y
 * el banco lo da por bueno. Justo el fallo que venimos a buscar.
 */
static uint8_t contenido(uint32_t k)
{
    uint32_t h = (k + 0x9E3779B9UL) * 2654435761UL;
    h ^= h >> 15;
    return (uint8_t)(h >> 13);
}

static uint32_t bps(void)   { return (uint32_t)(g_chip[11] | (g_chip[12] << 8)); }
static uint32_t fat1(void)  { return (uint32_t)(g_chip[14] | (g_chip[15] << 8)) * bps(); }
static uint32_t fatsz(void) { return (uint32_t)(g_chip[22] | (g_chip[23] << 8)) * bps(); }
static uint32_t raiz(void)  { return fat1() + (uint32_t)g_chip[16] * fatsz(); }
static uint32_t datos(void) { return raiz() + (uint32_t)(g_chip[17] |
                                     (g_chip[18] << 8)) * 32U; }

/* Escribe una entrada de la FAT en las DOS copias, como haria un PC. */
static void fat_pon(uint32_t c, uint16_t v)
{
    uint32_t o = c + (c / 2U), copia;

    for (copia = 0U; copia < 2U; copia++) {
        uint8_t *f = &g_chip[fat1() + copia * fatsz() + o];
        uint16_t p = (uint16_t)(f[0] | (f[1] << 8));
        p = (c & 1U) ? (uint16_t)((p & 0x000FU) | (v << 4))
                     : (uint16_t)((p & 0xF000U) | (v & 0x0FFFU));
        f[0] = (uint8_t)(p & 0xFFU); f[1] = (uint8_t)(p >> 8);
    }
}

static uint16_t fat_lee(uint32_t c)
{
    uint32_t o = c + (c / 2U);
    uint8_t *f = &g_chip[fat1() + o];
    uint16_t v = (uint16_t)(f[0] | (f[1] << 8));
    return (c & 1U) ? (uint16_t)(v >> 4) : (uint16_t)(v & 0x0FFFU);
}

/*
 * PONE EL FICHERO CON LA CADENA QUE SE LE DIGA.
 *
 * `orden` es la lista de clusters EN EL ORDEN DEL FICHERO: orden[0] lleva
 * los primeros 512 bytes, orden[1] los siguientes, etc. Escribir la FAT
 * asi -y no "el siguiente libre"- es lo que permite montar a proposito el
 * caso que importa.
 *
 * `ranura` decide en que ranura del directorio raiz cae la entrada. Se
 * usa una del SEGUNDO sector a proposito: las funciones viejas del driver
 * solo miraban el primero, y un fichero copiado desde Windows -sobre todo
 * con entrada de nombre largo delante- cae mas adelante.
 */
static void pon_fichero(const char *n11, const uint32_t *orden, uint32_t nclus,
                        uint32_t bytes, uint32_t ranura)
{
    uint32_t off = raiz() + ranura * 32U;
    uint8_t *e = &g_chip[off];
    uint32_t i, puestos = 0U;

    memcpy(e, n11, 11);
    e[11] = 0x20U;
    e[26] = (uint8_t)(orden[0] & 0xFFU); e[27] = (uint8_t)(orden[0] >> 8);
    e[28] = (uint8_t)(bytes & 0xFFU);
    e[29] = (uint8_t)((bytes >> 8) & 0xFFU);
    e[30] = (uint8_t)((bytes >> 16) & 0xFFU);
    e[31] = (uint8_t)((bytes >> 24) & 0xFFU);

    for (i = 0U; i < nclus; i++) {
        uint32_t ini = datos() + (orden[i] - 2U) * bps();
        uint32_t n = bytes - puestos;
        if (n > bps()) { n = bps(); }
        {
            uint32_t k;
            for (k = 0U; k < n; k++) { g_chip[ini + k] = contenido(puestos + k); }
        }
        puestos += n;
        fat_pon(orden[i], (i + 1U < nclus) ? (uint16_t)orden[i + 1U]
                                           : (uint16_t)0x0FFFU);
    }
}

/*
 * UN DIRECTORIO RAIZ CON HISTORIA.
 *
 * Hace falta para poder poner BD.BIN en el SEGUNDO sector del
 * directorio, que es donde cae un fichero copiado desde Windows a un
 * pendrive que lleva meses de uso. No vale con dejar las ranuras de
 * delante vacias: una ranura a 0x00 significa "fin del directorio" y el
 * driver deja de buscar ahi, y hace bien.
 *
 * Asi que se llenan como las llena el uso: entradas borradas (0xE5, que
 * es lo que deja el PC al borrar un fichero), la etiqueta del volumen,
 * y una entrada de nombre largo justo delante de BD.BIN - que es
 * precisamente lo que empuja al fichero mas alla del primer sector-.
 */
static void directorio_con_historia(uint32_t hasta)
{
    uint32_t r;

    for (r = 0U; r < hasta; r++) {
        uint8_t *e = &g_chip[raiz() + r * 32U];
        if (r == 0U) {
            memcpy(e, "DEEPSDR    ", 11);
            e[11] = 0x08U;                 /* etiqueta del volumen */
            continue;
        }
        memcpy(e, "BORRADOXTXT", 11);
        e[0] = 0xE5U;
        e[11] = 0x20U;
        e[7] = (uint8_t)('0' + (r % 10U)); /* que no sean todas la misma */
        e[26] = 0U; e[27] = 0U;            /* sin cadena: nada huerfano */
        e[28] = 0U; e[29] = 0U; e[30] = 0U; e[31] = 0U;
    }
}

/* La entrada de nombre largo que Windows pone DELANTE del 8.3. El driver
 * tiene que saltarsela (atributo 0x0F) y no confundirla con un fichero. */
static void pon_nombre_largo(uint32_t ranura)
{
    uint8_t *e = &g_chip[raiz() + ranura * 32U];
    uint32_t i;

    for (i = 0U; i < 32U; i++) { e[i] = 0xFFU; }
    e[0] = 0x41U;        /* primera y ultima parte del nombre */
    e[11] = 0x0FU;       /* atributo de nombre largo */
    e[12] = 0x00U;
    e[26] = 0U; e[27] = 0U;
    e[1] = 'd'; e[3] = 'a'; e[5] = 't'; e[7] = 'o'; e[9] = 's';
}

/* Reserva `n` clusters libres, en orden ascendente, y los marca como
 * ocupados para que la siguiente llamada no los vuelva a dar. */
static uint32_t coge_libres(uint32_t *out, uint32_t n)
{
    uint32_t total = (VOLUMEN - datos()) / bps();
    uint32_t c, k = 0U;

    for (c = 2U; c < 2U + total && k < n; c++) {
        if (fat_lee(c) == 0U) { out[k++] = c; fat_pon(c, 0x0FFEU); }
    }
    return k;
}

/*
 * La barajadura: primero el ultimo, luego el primero, luego el penultimo,
 * luego el segundo... Asi CADA eslabon salta al otro extremo de lo que
 * queda, y no hay dos clusters consecutivos seguidos en todo el fichero.
 * Un recorrido que se despiste un solo eslabon sale mal a la primera.
 */
static void baraja(uint32_t *v, uint32_t n)
{
    uint32_t *t = malloc(n * sizeof(uint32_t));
    uint32_t a = 0U, b = n, k = 0U;

    if (!t) { return; }
    memcpy(t, v, n * sizeof(uint32_t));
    while (a < b) {
        v[k++] = t[--b];
        if (a < b) { v[k++] = t[a++]; }
    }
    free(t);
}

static uint32_t saltos_atras(const uint32_t *v, uint32_t n)
{
    uint32_t i, s = 0U;
    for (i = 1U; i < n; i++) { if (v[i] < v[i - 1U]) { s++; } }
    return s;
}

/* Corre la carga entera y devuelve el numero de pasos, o -1 si no abrio. */
static long carga(const char *n8, const char *e3, uint32_t destino, uint32_t len)
{
    long pasos = 0;
    int8_t r;

    if (!spi_flash_restaura_abre(n8, e3, destino, len, ZA_INI, ZA_FIN)) {
        return -1;
    }
    do {
        r = spi_flash_volcado_paso();
        pasos++;
        if (r < 0) { return -2; }
    } while (r > 0);
    return pasos;
}

int main(int argc, char **argv)
{
    const char *plantilla = (argc > 1) ? argv[1] : "out/fat12_vacio.img";
    static uint32_t clus[2048];
    uint32_t n, i;
    FILE *f;
    long pasos;

    printf("\n=== Cargar un BD.BIN TROCEADO a la zona alta ===\n\n");

    w25q_reset();

    f = fopen(plantilla, "rb");
    if (!f) { printf("  falta %s\n", plantilla); return 1; }
    if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
        printf("  %s no mide 1 MB\n", plantilla); fclose(f); return 1;
    }
    fclose(f);

    spi_flash_init();

    /* Un directorio raiz usado, para que BD.BIN pueda caer donde cae
     * de verdad: en el segundo sector. Ver directorio_con_historia(). */
    directorio_con_historia(19U);
    pon_nombre_largo(19U);

    /*
     * EL ESCENARIO: 256 kB troceados a mala idea, en la ranura 20 del
     * directorio -o sea, en el SEGUNDO sector del directorio raiz-.
     *
     * 256 kB y no medio mega para que el banco tarde segundos y no
     * minutos: el bus va a patadas por GPIO y cada byte son dieciseis
     * cambios de pata simulados. Lo que se mide -que el recorrido de la
     * cadena no se despiste- no depende del tamaño: depende de que la
     * cadena salte, y aqui salta en los 512 eslabones.
     */
    {
        const uint32_t bytes = 256U * 1024U + 333U;   /* y que no sea multiplo */
        uint32_t nclus = (bytes + 511U) / 512U;

        n = coge_libres(clus, nclus);
        if (n != nclus) { printf("  no caben %u clusters   MAL\n", nclus); return 1; }
        for (i = 0U; i < n; i++) { fat_pon(clus[i], 0U); }   /* soltarlos: los pone pon_fichero */
        baraja(clus, n);
        printf("  BD.BIN: %u bytes, %u clusters, %u saltos hacia atras\n",
               bytes, n, saltos_atras(clus, n));
        printf("  cadena: %u -> %u -> %u -> %u -> ... -> %u\n",
               clus[0], clus[1], clus[2], clus[3], clus[n - 1U]);
        comprueba(saltos_atras(clus, n) > n / 3U,
                  "la cadena de prueba salta de verdad hacia atras");
        pon_fichero("BD      BIN", clus, n, bytes, 20U);

        /* Y que el propio driver lo encuentre y lo mida, estando donde
         * esta: fuera del primer sector del directorio. */
        {
            uint32_t tam = 0U;
            comprueba(spi_flash_fichero_hay("BD      ", "BIN") != 0U,
                      "el driver lo encuentra en el segundo sector del raiz");
            comprueba(spi_flash_fichero_tam("BD      ", "BIN", &tam) != 0U &&
                      tam == bytes,
                      "y lo mide bien aunque este troceado");
        }

        /*
         * LOS TRES CAMINOS TIENEN QUE DECIR LO MISMO.
         *
         * Esto es lo que de verdad le paso al usuario. La carga automatica
         * del arranque empieza asi:
         *
         *     if (!spi_flash_fichero_hay("BD      ", "BIN")) { return 0U; }
         *
         * o sea que si `fichero_hay` dice que no, **no se pinta nada**: la
         * radio arranca en silencio, como si no hubiera fichero. Y el
         * fichero estaba. Que `fichero_hay`, `fichero_tam` y
         * `restaura_abre` busquen cada uno por su cuenta es lo que permite
         * que dos de ellos opinen distinto, y cuando el que opina primero
         * es el del `return` mudo, no queda ni un mensaje que mirar.
         *
         * Ahora los tres pasan por fichero_busca_cadena(). Esto lo mide en
         * vez de confiar en que se quede asi.
         */
        {
            uint32_t t = 0U;
            uint8_t hay = spi_flash_fichero_hay("BD      ", "BIN");
            uint8_t mide = spi_flash_fichero_tam("BD      ", "BIN", &t);
            uint8_t abre = spi_flash_restaura_abre("BD      ", "BIN", ZA_INI,
                                                   ZA_FIN - ZA_INI, ZA_INI, ZA_FIN);
            comprueba(hay == mide && mide == abre,
                      "encontrar, medir y abrir dicen los tres lo mismo");
        }

        pasos = carga("BD      ", "BIN", ZA_INI, ZA_FIN - ZA_INI);
        if (pasos < 0) {
            printf("  no se pudo cargar: %s   MAL\n",
                   spi_flash_volcado_porque_txt());
            fallos++;
        } else {
            uint32_t malos = 0U, primero = 0xFFFFFFFFUL;
            printf("  cargado en %ld pasos de 4 kB\n", pasos);
            for (i = 0U; i < bytes; i++) {
                if (g_chip[ZA_INI + i] != contenido(i)) {
                    if (malos == 0U) { primero = i; }
                    malos++;
                }
            }
            if (malos) {
                printf("      %u bytes mal, el primero en el %u (cluster %u)\n",
                       malos, primero, primero / 512U);
            }
            comprueba(malos == 0U,
                      "los 256 kB aterrizan byte a byte en su sitio");
            comprueba(g_fallos_chip == 0, "el chip no se ha quejado ni una vez");
        }

        /* Y lo que NO se ha tocado: por debajo de la zona alta vive el
         * propio fichero del que se acaba de leer. */
        {
            uint32_t tocados = 0U;
            for (i = VOLUMEN; i < ZA_INI; i++) {
                if (g_chip[i] != 0xFFU) { tocados++; }
            }
            comprueba(tocados == 0U,
                      "no se ha escrito por debajo de la zona alta");
        }
        {
            uint32_t tam2 = 0U;
            comprueba(spi_flash_fichero_tam("BD      ", "BIN", &tam2) != 0U &&
                      tam2 == bytes,
                      "y el fichero de origen sigue entero en el disco");
        }
    }

    /*
     * LA OTRA MITAD: UNA CADENA ROTA TIENE QUE DECIR QUE NO.
     *
     * Admitir el troceado no es admitir cualquier cosa. Si la cadena se
     * acaba antes que el tamaño que dice el directorio, el fichero esta a
     * medias y copiarlo seria meter basura en la zona alta... y el
     * cargador borra el original despues. Se comprueba aparte que el
     * motivo que da NO es "troceado": decirlo mandaria a mirar al sitio
     * equivocado, que es el fallo de siempre de esta casa.
     */
    {
        const uint32_t bytes = 8192U;
        uint32_t nclus = bytes / 512U;

        n = coge_libres(clus, nclus);
        for (i = 0U; i < n; i++) { fat_pon(clus[i], 0U); }
        baraja(clus, n);
        pon_fichero("CORTO   BIN", clus, n, bytes, 21U);
        fat_pon(clus[n / 2U], 0x0FFFU);       /* la cortamos por la mitad */

        printf("\n  CORTO.BIN: %u bytes pero la cadena se acaba en el %u\n",
               bytes, n / 2U);
        comprueba(carga("CORTO   ", "BIN", ZA_INI, ZA_FIN - ZA_INI) == -1,
                  "una cadena que no llega al final se rechaza");
        comprueba(spi_flash_volcado_porque() == SPI_VOL_CORTO,
                  "y el motivo es 'a medias', no 'troceado'");
        printf("      dice: \"%s\"\n", spi_flash_volcado_porque_txt());
    }

    /*
     * Y EL CASO FACIL SIGUE FUNCIONANDO. El camino nuevo es el unico que
     * queda para restaurar, tambien cuando el fichero esta seguido: si
     * solo se hubiera probado el troceado, una regresion aqui se colaria
     * entera.
     */
    {
        const uint32_t bytes = 16384U;
        uint32_t nclus = bytes / 512U;

        n = coge_libres(clus, nclus);
        for (i = 0U; i < n; i++) { fat_pon(clus[i], 0U); }
        /* sin barajar: seguidos, como los deja un volumen recien formateado */
        pon_fichero("SEGUIDO BIN", clus, n, bytes, 22U);
        printf("\n  SEGUIDO.BIN: %u bytes, %u clusters seguidos desde el %u\n",
               bytes, n, clus[0]);
        if (carga("SEGUIDO ", "BIN", ZA_INI + 0x10000UL, bytes) < 0) {
            printf("  no se pudo cargar: %s   MAL\n",
                   spi_flash_volcado_porque_txt());
            fallos++;
        } else {
            uint32_t malos = 0U;
            for (i = 0U; i < bytes; i++) {
                if (g_chip[ZA_INI + 0x10000UL + i] != contenido(i)) { malos++; }
            }
            comprueba(malos == 0U, "un fichero seguido tambien sale bien");
        }
    }

    /*
     * LO QUE CUESTA COMPROBAR LAS SUMAS, MEDIDO.
     *
     * Pedido por el usuario.
     *
     * No estaba colgada: estaba sumando medio megabyte por este bus. El
     * comentario de zona_alta.h decia "del orden de dos segundos" y era
     * una cuenta a ojo; esto lo mide con el patron de lectura exacto de
     * zona_alta_verifica() -trozos de 256 bytes- sobre el chip simulado a
     * nivel de pin, y el resultado es lo que se pone en los comentarios y
     * lo que justifica que la comprobacion tenga su propia barra.
     */
    {
        static uint8_t b[256];
        uint64_t bits0 = g_bits;
        uint32_t p;

        for (p = 0U; p < 512U * 1024U; p += 256U) {
            spi_flash_read(ZA_INI + p, b, 256U);
        }
        printf("\n  comprobar las sumas de 512 kB en trozos de 256 bytes:\n");
        printf("    %llu bits a 2 us por bit = %.1f s\n",
               (unsigned long long)(g_bits - bits0),
               (double)(g_bits - bits0) * 2e-6);
    }

    printf("\n  (%u borrados, %u grabaciones de pagina, %llu bits por el bus)\n",
           g_borrados, g_grabados, (unsigned long long)g_bits);
    printf("\n%s\n\n", fallos ? "HAY FALLOS" : "todo en orden");
    return fallos ? 1 : 0;
}
