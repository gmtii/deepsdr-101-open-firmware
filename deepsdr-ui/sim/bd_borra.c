/*
 * BORRAR BD.BIN DEL DISCO SIN LLEVARSE EL DISCO POR DELANTE.
 *
 * DE DONDE SALE ESTO - 18/10/2026
 * -------------------------------
 * Con cuatro palabras de informacion: el mapa se graba bien,
 * BD.BIN da un error al grabarse y despues el disco queda corrupto. Sin el
 * mensaje de error exacto y sin saber que chip lleva esa radio.
 *
 * Asi que lo que se puede hacer es mirar que diferencia hay entre los dos
 * caminos, porque el ritual es el MISMO -ver mapa_flash.h-: buscar el
 * fichero, copiarlo a la zona alta, comprobar las sumas y borrarlo del
 * disco. Las diferencias son dos, y las dos van en el tamaño:
 *
 *   el mapa      8 kB      16 clusters, casi siempre seguidos
 *   BD.BIN     ~460 kB    920 clusters, y troceados por Windows
 *
 * Y de los cuatro pasos solo UNO escribe en el disco: el borrado. Los
 * otros tres leen, o escriben en la zona alta, que esta por encima del
 * volumen y con su guardia puesta (ver datos_arranca() en main.c). O sea
 * que si despues de grabar el disco queda corrupto, el sospechoso es el
 * borrado, y lo que lo distingue del caso que SI funciona son esos 920
 * eslabones.
 *
 * LO QUE SE MIDE
 * --------------
 * Se monta el volumen que le llega a la radio -directorio raiz con
 * historia, BD.BIN en el segundo sector del raiz, troceado a mala idea- y
 * ademas VECINOS INTERCALADOS entre sus clusters: un fallo de un eslabon
 * en el recorrido no cae en hueco libre, cae encima del fichero de otro.
 * Se guarda una copia del megabyte entero, se borra BD.BIN, y despues se
 * exige que lo UNICO que haya cambiado en todo el volumen sea:
 *
 *   - el primer byte de la entrada de BD.BIN, que pasa a 0xE5;
 *   - las entradas de la FAT de SUS clusters, en las DOS copias.
 *
 * Byte a byte. No "parece bien": identico salvo esos sitios. Un banco que
 * compruebe solo lo que cree que se toca no puede cazar lo que se toca sin
 * querer, que es precisamente lo que se viene a buscar.
 *
 * Y ADEMAS LO JUZGA fsck.fat. El volumen de despues se deja en un .img y
 * tools/bdborra_check.py se lo pasa a fsck.fat, que no sabe nada de este
 * proyecto: cadenas cruzadas, clusters perdidos y las dos copias de la FAT
 * distintas son exactamente lo que se le pregunta.
 *
 * LO QUE ENCONTRO, que no estaba en el guion. El borrado de la cadena
 * esta bien: en las tres distribuciones, con 920 eslabones barajados y
 * vecinos intercalados, no toca un solo byte que no sea suyo. O sea que el
 * disco corrupto NO sale de ahi, y eso tambien es un resultado.
 *
 * Pero fsck.fat, al juzgar el volumen de despues, dijo esto:
 *
 *     Orphaned long file name part ":Fza:FzX:Fzq..."
 *
 * Cuando Windows copia un fichero con nombre largo -o con mayusculas y
 * minusculas mezcladas- pone delante de la entrada 8.3 una o varias
 * entradas de nombre largo. El borrado marcaba la de 8.3 y dejaba aquellas
 * colgando. El fichero desaparece y el volumen queda sucio: eso es lo que
 * chkdsk le enseña al usuario como "este disco tiene errores". Arreglado
 * el mismo dia en fichero_borra_interno(), y las escenas 2, 3 y 4 de aqui
 * son las que impiden que el arreglo se pase de largo.
 *
 * Y SE HA COMPROBADO QUE SABE DECIR QUE NO. Se le rompio el borrado de
 * doce maneras, en un arbol aparte: cortar la cadena un eslabon antes,
 * recorrerla como si fuera seguida, equivocarse de par/impar, actualizar
 * solo una copia de la FAT, no marcar la entrada, borrar la FAT entera,
 * soltar un cluster de al lado, no borrar los nombres largos, borrarlos
 * sin mirar la suma, no parar en el bit 0x40, y salirse por debajo del
 * raiz. Las once saltaron.
 *
 * La doceava -quitar la comprobacion de 0xE5 al mirar atras- NO salta, y
 * se deja dicho en vez de disimularlo: lo unico que hace de mas es
 * escribir un 0xE5 donde ya habia un 0xE5, asi que no hay nada que ver. La
 * comprobacion se queda porque dice en voz alta "no toques lo que ya esta
 * borrado", no porque cambie el resultado.
 *
 * Debajo hay un W25Q16 simulado a nivel de pin (sim/w25q_sim.c), asi que
 * el spi_flash.c que corre aqui es el del firmware sin una linea cambiada.
 */
#include "spi_flash.h"
#include "w25q_sim.h"
#include "zona_alta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * EL VOLUMEN SE MIDE, NO SE SUPONE. Este banco se corre sobre las tres
 * distribuciones que el driver admite -1 MB con clusters de 512, 7 MB con
 * los de 2 kB y 15 MB con los de 4 kB-, y en las tres la FAT cae en sitios
 * distintos y cruza las fronteras de borrado por otros sitios. Un 1 MB a
 * mano aqui probaria una sola de las tres.
 */
#define VOL_MAX   (16U * 1024U * 1024U)

static uint32_t VOLUMEN;
static int fallos = 0;
static uint8_t g_antes[VOL_MAX];

static void comprueba(int ok, const char *que)
{
    printf("    %-58s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/* Igual que el de sim/fat_troceado.c: depende del DESPLAZAMIENTO y tiene
 * periodo largo, para que dos trozos cambiados de sitio no salgan iguales. */
static uint8_t contenido(uint32_t k)
{
    uint32_t h = (k + 0x9E3779B9UL) * 2654435761UL;
    h ^= h >> 15;
    return (uint8_t)(h >> 13);
}

static uint32_t bps(void)   { return (uint32_t)(g_chip[11] | (g_chip[12] << 8)); }
/* El CLUSTER, que no es el sector: con spc = 4 son 2 kB. Tenerlo aparte no
 * es cosmetico - la primera version de este banco repartia los clusters
 * contando de 512 en 512 en los tres volumenes, y en los dos de clusters
 * grandes montaba un fichero que se salia de su propia cadena. El banco
 * marcaba "las dos copias de la FAT no cuadran" y la culpa era suya. */
static uint32_t cbytes(void) { return (uint32_t)g_chip[13] * bps(); }
static uint32_t fat1(void)  { return (uint32_t)(g_chip[14] | (g_chip[15] << 8)) * bps(); }
static uint32_t fatsz(void) { return (uint32_t)(g_chip[22] | (g_chip[23] << 8)) * bps(); }
static uint32_t raiz(void)  { return fat1() + (uint32_t)g_chip[16] * fatsz(); }
static uint32_t datos(void) { return raiz() + (uint32_t)(g_chip[17] |
                                     (g_chip[18] << 8)) * 32U; }

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

/* Lee una entrada de la copia que se le diga, sobre el buffer que se le
 * diga. Dos parametros y no uno porque hay que comparar el ANTES con el
 * DESPUES y las DOS copias entre si. */
static uint16_t fat_lee_de(const uint8_t *img, uint32_t copia, uint32_t c)
{
    uint32_t o = c + (c / 2U);
    const uint8_t *f = &img[fat1() + copia * fatsz() + o];
    uint16_t v = (uint16_t)(f[0] | (f[1] << 8));
    return (c & 1U) ? (uint16_t)(v >> 4) : (uint16_t)(v & 0x0FFFU);
}

static uint32_t clusters_del_volumen(void)
{
    return (VOLUMEN - datos()) / cbytes();
}

static void pon_fichero(const char *n11, const uint32_t *orden, uint32_t nclus,
                        uint32_t bytes, uint32_t ranura, uint32_t semilla)
{
    uint8_t *e = &g_chip[raiz() + ranura * 32U];
    uint32_t i, puestos = 0U;

    memcpy(e, n11, 11);
    e[11] = 0x20U;
    e[26] = (uint8_t)(orden[0] & 0xFFU); e[27] = (uint8_t)(orden[0] >> 8);
    e[28] = (uint8_t)(bytes & 0xFFU);
    e[29] = (uint8_t)((bytes >> 8) & 0xFFU);
    e[30] = (uint8_t)((bytes >> 16) & 0xFFU);
    e[31] = (uint8_t)((bytes >> 24) & 0xFFU);

    for (i = 0U; i < nclus; i++) {
        uint32_t ini = datos() + (orden[i] - 2U) * cbytes();
        uint32_t n = bytes - puestos, k;
        if (n > cbytes()) { n = cbytes(); }
        for (k = 0U; k < n; k++) { g_chip[ini + k] = contenido(semilla + puestos + k); }
        puestos += n;
        fat_pon(orden[i], (i + 1U < nclus) ? (uint16_t)orden[i + 1U]
                                           : (uint16_t)0x0FFFU);
    }
}

/* Ver sim/fat_troceado.c: una ranura a 0x00 corta el directorio, asi que
 * las de delante se llenan como las llena el uso. */
static void directorio_con_historia(uint32_t hasta)
{
    uint32_t r;

    for (r = 0U; r < hasta; r++) {
        uint8_t *e = &g_chip[raiz() + r * 32U];
        if (r == 0U) {
            /* La etiqueta se COPIA del sector de arranque, no se escribe a
             * mano: los tres volumenes de prueba tienen la suya
             * (DEEPSDR, DEEPSDR2K, DEEPSDR16) y poner una distinta en el
             * raiz hace que fsck.fat se queje con razon... de un fallo del
             * banco. Lo dijo el, la primera vez que se corrio sobre el
             * volumen de 7 MB. */
            memcpy(e, &g_chip[43], 11);
            e[11] = 0x08U;
            continue;
        }
        memcpy(e, "BORRADOXTXT", 11);
        e[0] = 0xE5U;
        e[11] = 0x20U;
        e[7] = (uint8_t)('0' + (r % 10U));
        e[26] = 0U; e[27] = 0U;
        e[28] = 0U; e[29] = 0U; e[30] = 0U; e[31] = 0U;
    }
}

/* La suma que liga una entrada de nombre largo con su 8.3 (byte 13). Es
 * la del formato, no una nuestra. */
static uint8_t suma83(const char *n11)
{
    uint8_t s = 0U;
    uint32_t i;

    for (i = 0U; i < 11U; i++) {
        s = (uint8_t)(((s & 1U) ? 0x80U : 0U) + (s >> 1) + (uint8_t)n11[i]);
    }
    return s;
}

/*
 * UNA ENTRADA DE NOMBRE LARGO. `sec` es el numero de orden dentro del
 * grupo, con 0x40 en la ultima - que en el disco va la PRIMERA, porque el
 * grupo se guarda al reves-. `n11` es el 8.3 al que pertenece: de ahi sale
 * la suma del byte 13, y poner una suma que no cuadre es justo lo que
 * convierte una entrada en huerfana de otro.
 */
static void pon_nombre_largo(uint32_t ranura, uint8_t sec, const char *n11,
                             char letra)
{
    uint8_t *e = &g_chip[raiz() + ranura * 32U];
    uint32_t i;

    for (i = 0U; i < 32U; i++) { e[i] = 0xFFU; }
    e[0] = sec;
    e[11] = 0x0FU;
    e[12] = 0x00U;
    e[13] = suma83(n11);
    e[26] = 0U; e[27] = 0U;
    e[1] = (uint8_t)letra; e[3] = 'a'; e[5] = 't'; e[7] = 'o'; e[9] = 's';
}

/*
 * La barajadura de fat_troceado.c: cada eslabon salta al otro extremo de
 * lo que queda, asi que no hay dos clusters seguidos en todo el fichero.
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

static void vuelca(const char *ruta, const uint8_t *img)
{
    FILE *f = fopen(ruta, "wb");
    if (f) { (void)fwrite(img, 1, VOLUMEN, f); fclose(f); }
}

/*
 * SEGUNDA ESCENA: UNA ENTRADA DE NOMBRE LARGO QUE NO ES SUYA.
 *
 * Al borrar la de 8.3 hay que llevarse las de nombre largo de DELANTE,
 * pero solo las que son de ese fichero. Un volumen con historia puede
 * tener una colgando de un borrado de antes -o de otro fichero- justo en
 * la ranura anterior, y comersela seria el mismo fallo al reves: borrar
 * algo de otro.
 *
 * Lo que las distingue es el byte 13, la suma del 8.3 al que pertenecen.
 * Aqui se pone una con la suma de OTRO.TXT pegada a BD.BIN y se exige que
 * siga entera.
 */
static void escena_ajena(const char *plantilla)
{
    FILE *f = fopen(plantilla, "rb");
    uint32_t clus[8], i, o;

    printf("\n  y una entrada de nombre largo que es de otro\n");
    if (!f) { printf("    falta %s   MAL\n", plantilla); fallos++; return; }
    w25q_reset();
    if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
        printf("    no he podido releer %s   MAL\n", plantilla); fclose(f);
        fallos++; return;
    }
    fclose(f);
    spi_flash_init();

    directorio_con_historia(19U);
    for (i = 0U; i < 4U; i++) { clus[i] = 2U + i; }
    pon_nombre_largo(19U, 0x41U, "OTRO    TXT", 'x');   /* la ajena, pegada */
    pon_fichero("BD      BIN", clus, 4U, 4U * cbytes() - 3U, 20U, 0x44000U);

    comprueba(spi_flash_fichero_borra("BD      ", "BIN") != 0U,
              "borra un BD.BIN pequeño que tiene una ajena delante");
    o = raiz() + 20U * 32U;
    comprueba(g_chip[o] == 0xE5U, "la entrada de BD.BIN queda marcada");
    comprueba(g_chip[raiz() + 19U * 32U] == 0x41U,
              "y la de nombre largo de OTRO.TXT sigue entera");
}

/*
 * TERCERA ESCENA: LOS HUERFANOS QUE DEJO LA VERSION DE ANTES.
 *
 * Una radio que venga del firmware anterior tiene en su disco las
 * entradas de nombre largo de los BD.BIN que ya se borraron, colgando sin
 * dueno - ese era el fallo-. Y Windows, al copiar el BD.BIN nuevo, puede
 * ponerlo justo detras.
 *
 * Esas viejas llevan la MISMA suma, porque son del mismo nombre 8.3, asi
 * que la suma no las distingue. Lo que las separa es el bit 0x40, que
 * marca la ultima entrada del grupo: el recorrido para ahi y no sigue
 * hacia atras. Comerselas no haria dano -son basura- pero tampoco son
 * nuestras, y un recorrido que no sabe donde acaba su grupo no sabe donde
 * para en general.
 */
static void escena_huerfanas(const char *plantilla)
{
    FILE *f = fopen(plantilla, "rb");
    uint32_t clus[4], i, o;

    printf("\n  y los huerfanos que dejo la version de antes\n");
    if (!f) { printf("    falta %s   MAL\n", plantilla); fallos++; return; }
    w25q_reset();
    if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
        printf("    no he podido releer %s   MAL\n", plantilla); fclose(f);
        fallos++; return;
    }
    fclose(f);
    spi_flash_init();

    directorio_con_historia(19U);
    for (i = 0U; i < 4U; i++) { clus[i] = 2U + i; }
    /* El grupo VIEJO, con la misma suma, y pegado al nuevo. */
    pon_nombre_largo(16U, 0x42U, "BD      BIN", 'v');
    pon_nombre_largo(17U, 0x01U, "BD      BIN", 'v');
    /* Y el grupo del fichero que hay ahora. */
    pon_nombre_largo(18U, 0x42U, "BD      BIN", 'n');
    pon_nombre_largo(19U, 0x01U, "BD      BIN", 'n');
    pon_fichero("BD      BIN", clus, 4U, 4U * cbytes() - 5U, 20U, 0x55000U);

    comprueba(spi_flash_fichero_borra("BD      ", "BIN") != 0U,
              "borra el BD.BIN de ahora");
    o = raiz() + 20U * 32U;
    comprueba(g_chip[o] == 0xE5U &&
              g_chip[raiz() + 19U * 32U] == 0xE5U &&
              g_chip[raiz() + 18U * 32U] == 0xE5U,
              "se lleva su grupo entero, las dos entradas");
    comprueba(g_chip[raiz() + 17U * 32U] == 0x01U &&
              g_chip[raiz() + 16U * 32U] == 0x42U,
              "y para en el bit 0x40: no toca el grupo de antes");
}

/*
 * CUARTA ESCENA: EL FICHERO EN LA PRIMERA RANURA DEL DIRECTORIO.
 *
 * El recorrido hacia atras tiene que PARAR en el principio del directorio
 * raiz. Si no para, la entrada que mira es la de antes del raiz, o sea los
 * ultimos 32 bytes de la segunda copia de la FAT - y si esos bytes se
 * parecen a una entrada de nombre largo, escribe un 0xE5 DENTRO DE LA FAT.
 * Eso ya no es dejar basura: es estropear el volumen.
 *
 * Que esos bytes se parezcan no es imposible: la cola de la FAT son los
 * bytes que sobran de la tabla -3.072 reservados para 3.003 que se usan- y
 * ahi puede haber cualquier cosa de un formateo anterior. Aqui se ponen a
 * mano, con la suma correcta, que es el peor caso y el unico que prueba la
 * guardia.
 */
static void escena_primera_ranura(const char *plantilla)
{
    FILE *f = fopen(plantilla, "rb");
    uint8_t antes[32];
    uint32_t clus[2], i, o, r0;

    printf("\n  y con BD.BIN en la primera ranura del raiz\n");
    if (!f) { printf("    falta %s   MAL\n", plantilla); fallos++; return; }
    w25q_reset();
    if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
        printf("    no he podido releer %s   MAL\n", plantilla); fclose(f);
        fallos++; return;
    }
    fclose(f);
    spi_flash_init();

    r0 = raiz();
    for (i = 0U; i < 32U; i++) { g_chip[r0 - 32U + i] = 0xFFU; }
    g_chip[r0 - 32U + 0U]  = 0x01U;                       /* no lleva el 0x40 */
    g_chip[r0 - 32U + 11U] = 0x0FU;                       /* y tiene la pinta */
    g_chip[r0 - 32U + 13U] = suma83("BD      BIN");       /* y hasta la suma */
    g_chip[r0 - 32U + 26U] = 0U; g_chip[r0 - 32U + 27U] = 0U;
    memcpy(antes, &g_chip[r0 - 32U], sizeof antes);

    for (i = 0U; i < 2U; i++) { clus[i] = 2U + i; }
    pon_fichero("BD      BIN", clus, 2U, 2U * cbytes() - 9U, 0U, 0x66000U);

    comprueba(spi_flash_fichero_borra("BD      ", "BIN") != 0U,
              "borra un BD.BIN que esta en la ranura 0");
    o = r0;
    comprueba(g_chip[o] == 0xE5U, "la entrada de BD.BIN queda marcada");
    comprueba(memcmp(antes, &g_chip[r0 - 32U], sizeof antes) == 0,
              "y no ha escrito en la cola de la FAT, que es lo de antes");
}

/*
 * QUINTA ESCENA: EL RITUAL ENTERO, CON LA GEOMETRIA DE SERIE.
 *
 * Las cuatro de arriba miran el borrado, que es el ultimo paso. Esta hace
 * los cuatro seguidos sobre un W25Q16 -el chip que lleva la radio de
 * fabrica- con su disco de 1 MB y un BD.BIN DE VERDAD troceado dentro:
 *
 *   1. abrir la copia a la zona alta, con el suelo que pone main.c
 *      (el tamaño que mide el PROPIO sistema de ficheros, no una cuenta);
 *   2. andarla hasta el final;
 *   3. abrir el directorio de la zona alta y COMPROBAR LAS SUMAS de todas
 *      sus secciones, que es lo que hace datos_al_arrancar() antes de
 *      borrar nada;
 *   4. y borrar el fichero del disco.
 *
 * Y despues se compara el megabyte del volumen entero contra la copia de
 * antes. Lo unico que puede haber cambiado es la marca 0xE5 de la entrada
 * y las entradas de la FAT de sus clusters - lo mismo que en la primera
 * escena-, porque de los cuatro pasos solo el ultimo escribe en el disco.
 *
 * Sirve para contestar a una pregunta concreta y con un numero: si en la
 * radio de serie el camino de BD.BIN puede tocar el volumen. Si esto sale
 * en cero, no puede, y lo que haya roto un disco vino por otro sitio.
 */
static void escena_ritual(const char *plantilla, const char *bd)
{
    FILE *f;
    static uint32_t libres[4096], clus[4096];
    uint32_t nl = 0U, n, i, c, total, tam, bloques, cambiados = 0U;
    uint32_t primero = 0xFFFFFFFFUL;
    uint8_t *datos_bd;
    int8_t r;
    long pasos = 0;

    printf("\n  y el ritual entero, con la geometria de serie\n");

    f = fopen(bd, "rb");
    if (!f) { printf("    no encuentro %s - se salta esta escena\n", bd); return; }
    fseek(f, 0, SEEK_END); tam = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    datos_bd = (uint8_t *)malloc(tam);
    if (!datos_bd || fread(datos_bd, 1, tam, f) != tam) {
        printf("    no he podido leer %s   MAL\n", bd); fclose(f); fallos++; return;
    }
    fclose(f);

    f = fopen(plantilla, "rb");
    if (!f) { printf("    falta %s   MAL\n", plantilla); fallos++; return; }
    w25q_reset();
    w25q_tam(VOLUMEN + 0x100000UL);           /* el W25Q16: disco + un mega */
    if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
        printf("    no he podido releer %s   MAL\n", plantilla);
        fclose(f); fallos++; return;
    }
    fclose(f);
    spi_flash_olvida_capacidad();
    spi_flash_init();

    /* HASTA LA 20, no hasta la 19: la ranura que queda a 0x00 corta el
     * directorio y el driver deja de buscar ahi -y hace bien-. En las
     * escenas de arriba ese hueco lo tapa la entrada de nombre largo; aqui
     * no hay, asi que BD.BIN quedaba detras del fin del directorio y la
     * copia se negaba. El banco marcaba cinco fallos y los cinco eran
     * mios. */
    directorio_con_historia(20U);
    total = clusters_del_volumen();
    for (c = 2U; c < 2U + total && nl < 4096U; c++) {
        if (fat_lee_de(g_chip, 0U, c) == 0U) { libres[nl++] = c; }
    }
    n = (tam + cbytes() - 1U) / cbytes();
    if (n > nl) {
        printf("    %s no cabe en el volumen (%u clusters de %u)   MAL\n",
               bd, n, nl); fallos++; free(datos_bd); return;
    }
    for (i = 0U; i < n; i++) { clus[i] = libres[i]; }
    baraja(clus, n);
    /* El contenido lo pone pon_fichero() con su relleno; aqui hace falta
     * el fichero DE VERDAD, asi que se coloca a mano por la cadena. */
    {
        uint8_t *e = &g_chip[raiz() + 20U * 32U];
        uint32_t puestos = 0U;

        memcpy(e, "BD      BIN", 11);
        e[11] = 0x20U;
        e[26] = (uint8_t)(clus[0] & 0xFFU); e[27] = (uint8_t)(clus[0] >> 8);
        e[28] = (uint8_t)(tam & 0xFFU); e[29] = (uint8_t)((tam >> 8) & 0xFFU);
        e[30] = (uint8_t)((tam >> 16) & 0xFFU); e[31] = (uint8_t)((tam >> 24) & 0xFFU);
        for (i = 0U; i < n; i++) {
            uint32_t ini = datos() + (clus[i] - 2U) * cbytes();
            uint32_t m = tam - puestos;
            if (m > cbytes()) { m = cbytes(); }
            memcpy(&g_chip[ini], &datos_bd[puestos], m);
            puestos += m;
            fat_pon(clus[i], (i + 1U < n) ? (uint16_t)clus[i + 1U]
                                          : (uint16_t)0x0FFFU);
        }
    }
    memcpy(g_antes, g_chip, VOLUMEN);

    printf("    BD.BIN de verdad: %u bytes en %u clusters troceados\n", tam, n);
    printf("    chip %u   disco %u   ZA_BASE 0x%06X   ZA_TOPE 0x%06X\n",
           spi_flash_capacidad(), VOLUMEN, ZA_BASE, ZA_TOPE);

    bloques = (tam + 4095UL) & ~4095UL;
    {
        spi_flash_alta_t a;
        spi_flash_mira_alta(&a);
        comprueba(spi_flash_restaura_abre("BD      ", "BIN", ZA_BASE, bloques,
                                          a.fs_bytes, ZA_TOPE) != 0U,
                  "1. la copia a la zona alta abre");
    }
    do { r = spi_flash_volcado_paso(); pasos++; } while (r > 0);
    comprueba(r == 0, "2. y llega al final sin tropezar");

    comprueba(zona_alta_abre(spi_flash_read) != 0U,
              "3. el directorio de la zona alta cuadra");
    comprueba(zona_alta_verifica() != 0U,
              "   y las sumas de todas sus secciones, tambien");

    comprueba(spi_flash_fichero_borra("BD      ", "BIN") != 0U,
              "4. y se borra del disco");

    {
        static uint8_t vale[VOL_MAX];
        uint32_t o = raiz() + 20U * 32U, copia;

        memset(vale, 0, VOLUMEN);
        vale[o] = 1U;
        for (i = 0U; i < n; i++) {
            for (copia = 0U; copia < 2U; copia++) {
                uint32_t b = fat1() + copia * fatsz() + clus[i] + (clus[i] / 2U);
                vale[b] = 1U; vale[b + 1U] = 1U;
            }
        }
        for (i = 0U; i < VOLUMEN; i++) {
            if (g_chip[i] != g_antes[i] && !vale[i]) {
                if (primero == 0xFFFFFFFFUL) { primero = i; }
                cambiados++;
            }
        }
        if (cambiados != 0U) {
            printf("    *** %u bytes del disco cambiados fuera de sitio, el"
                   " primero en 0x%06X ***\n", cambiados, primero);
        }
        comprueba(cambiados == 0U,
                  "y el volumen sigue intacto salvo la marca y su FAT");
    }
    free(datos_bd);
}

int main(int argc, char **argv)
{
    const char *plantilla = (argc > 1) ? argv[1] : "out/fat12_vacio.img";
    const char *salida    = (argc > 2) ? argv[2] : "out/bdborra_despues.img";
    static uint32_t libres[4096], bd[4096], vec[3][64];
    long tam_img;
    uint32_t nlibres = 0U, nbd, nvec[3], i, c, total;
    const uint32_t BD_BYTES = 460U * 1024U + 777U;
    uint32_t cambiados = 0U, primer_cambio = 0xFFFFFFFFUL;
    FILE *f;

    printf("\n=== BORRAR UN BD.BIN GRANDE Y TROCEADO DEL DISCO ===\n\n");

    f = fopen(plantilla, "rb");
    if (!f) { printf("  falta %s\n", plantilla); return 1; }
    fseek(f, 0, SEEK_END); tam_img = ftell(f); fseek(f, 0, SEEK_SET);
    if (tam_img <= 0 || (uint32_t)tam_img > VOL_MAX) {
        printf("  %s mide %ld bytes y no vale\n", plantilla, tam_img);
        fclose(f); return 1;
    }
    VOLUMEN = (uint32_t)tam_img;
    /* El chip tiene que ser el volumen MAS el megabyte de la zona alta: la
     * geometria de la radio es "el disco es todo menos el ultimo mega"
     * (ver disco.c), y spi_flash_capacidad() mide por alias. */
    w25q_tam(VOLUMEN + 0x100000UL);
    w25q_reset();
    if (fread(g_chip, 1, VOLUMEN, f) != VOLUMEN) {
        printf("  no he podido leer %s entero\n", plantilla); fclose(f); return 1;
    }
    fclose(f);
    spi_flash_init();
    printf("  %s: %u bytes de volumen\n", plantilla, VOLUMEN);

    directorio_con_historia(19U);
    /*
     * BD.BIN con DOS entradas de nombre largo delante -que es lo que deja
     * Windows cuando el nombre no cabe en 8.3 o lleva mayusculas y
     * minusculas mezcladas- y, mas atras, una tercera que es huerfana DE
     * OTRO: misma pinta, suma distinta. El borrado tiene que llevarse las
     * dos primeras y no tocar la tercera.
     */
    pon_nombre_largo(2U, 0x41U, "CONFIG  CSV", 'z');  /* el de CONFIG.CSV */
    pon_nombre_largo(18U, 0x42U, "BD      BIN", 'b');   /* ultima del grupo */
    pon_nombre_largo(19U, 0x01U, "BD      BIN", 'd');

    total = clusters_del_volumen();
    for (c = 2U; c < 2U + total && nlibres < 4096U; c++) {
        if (fat_lee_de(g_chip, 0U, c) == 0U) { libres[nlibres++] = c; }
    }
    printf("  el volumen tiene %u clusters de %u bytes, %u libres\n",
           total, cbytes(), nlibres);

    /*
     * EL REPARTO. Uno de cada diecisiete clusters libres se va a los tres
     * vecinos y el resto a BD.BIN, asi que los vecinos quedan METIDOS
     * ENTRE los de BD.BIN por todo el volumen. Si el recorrido de la
     * cadena se despista un eslabon, lo que suelta es el cluster de un
     * vecino, y eso se ve.
     */
    nbd = (BD_BYTES + cbytes() - 1U) / cbytes();
    {
        uint32_t iv = 0U, ib = 0U, k;

        nvec[0] = nvec[1] = nvec[2] = 0U;
        for (i = 0U; i < nlibres; i++) {
            if ((i % 17U) == 3U && iv < 3U * 24U) {
                vec[iv / 24U][nvec[iv / 24U]++] = libres[i];
                iv++;
            } else if (ib < nbd) {
                bd[ib++] = libres[i];
            }
        }
        if (ib < nbd) {
            printf("  no caben los %u clusters de BD.BIN   MAL\n", nbd);
            return 1;
        }
        for (k = 0U; k < 3U; k++) {
            if (nvec[k] == 0U) { printf("  sin vecinos   MAL\n"); return 1; }
        }
    }

    baraja(bd, nbd);
    printf("  BD.BIN: %u bytes, %u clusters troceados\n", BD_BYTES, nbd);
    printf("  vecinos intercalados: %u + %u + %u clusters\n",
           nvec[0], nvec[1], nvec[2]);

    pon_fichero("CONFIG  CSV", vec[0], nvec[0], nvec[0] * cbytes() - 7U, 3U, 0x11000U);
    pon_fichero("CHANNEL CSV", vec[1], nvec[1], nvec[1] * cbytes() - 13U, 7U, 0x22000U);
    pon_fichero("FOTO    BMP", vec[2], nvec[2], nvec[2] * cbytes() - 1U, 11U, 0x33000U);
    pon_fichero("BD      BIN", bd, nbd, BD_BYTES, 20U, 0U);

    comprueba(spi_flash_fichero_hay("BD      ", "BIN") != 0U,
              "el driver encuentra BD.BIN donde Windows lo deja");
    {
        uint32_t tam = 0U;
        comprueba(spi_flash_fichero_tam("BD      ", "BIN", &tam) != 0U &&
                  tam == BD_BYTES, "y lo mide bien estando troceado");
    }

    memcpy(g_antes, g_chip, VOLUMEN);
    vuelca("out/bdborra_antes.img", g_antes);

    printf("\n  el borrado\n");
    comprueba(spi_flash_fichero_borra("BD      ", "BIN") != 0U,
              "spi_flash_fichero_borra() dice que lo ha borrado");
    comprueba(spi_flash_fichero_hay("BD      ", "BIN") == 0U,
              "y despues el driver ya no lo encuentra");

    vuelca(salida, g_chip);

    /*
     * LO UNICO QUE PUEDE HABER CAMBIADO. Se marcan los sitios permitidos
     * en una mascara y despues se compara el megabyte entero contra la
     * copia: asi no hace falta acertar a priori con lo que el borrado
     * toca - lo que no esta permitido salta, venga de donde venga.
     */
    {
        static uint8_t vale[VOL_MAX];
        uint32_t o = raiz() + 20U * 32U;

        memset(vale, 0, VOLUMEN);
        vale[o] = 1U;                       /* la marca 0xE5 */
        vale[raiz() + 18U * 32U] = 1U;      /* y la de sus dos nombres largos */
        vale[raiz() + 19U * 32U] = 1U;
        for (i = 0U; i < nbd; i++) {
            uint32_t copia;
            for (copia = 0U; copia < 2U; copia++) {
                uint32_t b = fat1() + copia * fatsz() + bd[i] + (bd[i] / 2U);
                vale[b] = 1U; vale[b + 1U] = 1U;   /* la entrada de 12 bits */
            }
        }
        for (i = 0U; i < VOLUMEN; i++) {
            if (g_chip[i] != g_antes[i] && !vale[i]) {
                cambiados++;
                if (primer_cambio == 0xFFFFFFFFUL) { primer_cambio = i; }
            }
        }
        if (cambiados != 0U) {
            printf("    *** %u bytes cambiados fuera de sitio, el primero en"
                   " 0x%06X ***\n", cambiados, primer_cambio);
            printf("        (datos empiezan en 0x%06X, raiz en 0x%06X,"
                   " FAT1 en 0x%06X)\n", datos(), raiz(), fat1());
        }
        comprueba(cambiados == 0U,
                  "no se ha tocado un solo byte fuera de la marca y la FAT");
        comprueba(g_chip[o] == 0xE5U, "la entrada de BD.BIN queda marcada");
        comprueba(g_chip[raiz() + 19U * 32U] == 0xE5U &&
                  g_chip[raiz() + 18U * 32U] == 0xE5U,
                  "y sus dos entradas de nombre largo, tambien");
        comprueba(g_chip[raiz() + 2U * 32U] == 0x41U,
                  "el nombre largo de CONFIG.CSV se queda donde estaba");
    }

    printf("\n  la FAT\n");
    {
        uint32_t malos = 0U;

        for (i = 0U; i < nbd; i++) {
            if (fat_lee_de(g_chip, 0U, bd[i]) != 0U ||
                fat_lee_de(g_chip, 1U, bd[i]) != 0U) { malos++; }
        }
        comprueba(malos == 0U, "todos los clusters de BD.BIN quedan libres");

        malos = 0U;
        for (c = 2U; c < 2U + total; c++) {
            if (fat_lee_de(g_chip, 0U, c) != fat_lee_de(g_chip, 1U, c)) { malos++; }
        }
        comprueba(malos == 0U, "las dos copias de la FAT siguen iguales");

        /*
         * Y LA INVARIANTE QUE DE VERDAD IMPORTA, dicha al reves: ningun
         * cluster que estuviera OCUPADO puede haber quedado libre, salvo
         * los de BD.BIN. Las comprobaciones de arriba miran los tres
         * vecinos; esta mira los dos mil clusters del volumen, y es la
         * unica que cubre un fallo que suelte un cluster cualquiera por
         * ahi en medio. Un cluster suelto que sigue teniendo datos es
         * justo lo que luego se lleva por delante el siguiente fichero
         * que se copie: corrupcion de la que no se nota hasta despues.
         */
        {
            static uint8_t es_bd[4096 + 2];
            uint32_t sueltos = 0U, primero_suelto = 0U;

            memset(es_bd, 0, sizeof es_bd);
            for (i = 0U; i < nbd; i++) {
                if (bd[i] < sizeof es_bd) { es_bd[bd[i]] = 1U; }
            }
            for (c = 2U; c < 2U + total; c++) {
                if (fat_lee_de(g_antes, 0U, c) != 0U &&
                    fat_lee_de(g_chip, 0U, c) == 0U &&
                    !(c < sizeof es_bd && es_bd[c])) {
                    if (sueltos == 0U) { primero_suelto = c; }
                    sueltos++;
                }
            }
            if (sueltos != 0U) {
                printf("    *** %u clusters que eran de otro han quedado"
                       " libres, el primero el %u ***\n", sueltos, primero_suelto);
            }
            comprueba(sueltos == 0U,
                      "no se ha soltado ni un cluster que fuera de otro");
        }
    }

    printf("\n  los vecinos\n");
    {
        static const char *k_n[3] = { "CONFIG.CSV", "CHANNEL.CSV", "FOTO.BMP" };
        static const char *k_8[3] = { "CONFIG  ", "CHANNEL ", "FOTO    " };
        static const char *k_3[3] = { "CSV", "CSV", "BMP" };
        uint32_t v;

        for (v = 0U; v < 3U; v++) {
            uint32_t malos = 0U, tam = 0U;
            char q[80];

            for (i = 0U; i < nvec[v]; i++) {
                uint16_t sig = fat_lee_de(g_chip, 0U, vec[v][i]);
                uint16_t debe = (i + 1U < nvec[v]) ? (uint16_t)vec[v][i + 1U]
                                                   : (uint16_t)0x0FFFU;
                if (sig != debe) { malos++; }
            }
            sprintf(q, "%s: su cadena sigue entera", k_n[v]);
            comprueba(malos == 0U, q);
            sprintf(q, "%s: el driver lo sigue midiendo", k_n[v]);
            comprueba(spi_flash_fichero_tam(k_8[v], k_3[v], &tam) != 0U &&
                      tam != 0U, q);
        }
    }

    escena_ajena(plantilla);
    escena_huerfanas(plantilla);
    escena_primera_ranura(plantilla);
    escena_ritual(plantilla, (argc > 3) ? argv[3] : "../BD.BIN");

    printf("\n  %s\n", fallos ? "" : "TODO CORRECTO");
    if (fallos) { printf("  *** %d ***\n", fallos); }
    return fallos ? 1 : 0;
}
