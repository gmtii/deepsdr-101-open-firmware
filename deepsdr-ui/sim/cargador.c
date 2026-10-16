/*
 * EL CARGADOR DE ARRANQUE, PROBADO EN EL PC CON EL FICHERO DE VERDAD.
 *
 * Pedido por el usuario.
 *
 * Esto es eso. Corre el User/cargador.c del firmware -el de verdad, no una
 * copia- contra:
 *
 *   - el User/spi_flash.c del firmware, tambien el de verdad,
 *   - el W25Q16 simulado de sim/w25q_sim.c, bit a bit por las patas,
 *   - un volumen FAT12 de 1 MB con la geometria de verdad,
 *   - el update.bin que acaba de salir del make, byte a byte,
 *   - y una flash interna simulada con las reglas del FMC (sim/fmc_sim.c).
 *
 * EL CRITERIO DE APROBADO no es "no revienta": es que despues de la pasada,
 * los bytes en la base de la aplicacion sean EXACTAMENTE los del fichero,
 * que el fichero haya desaparecido del volumen -porque eso es lo que hace
 * el gestor de verdad, ver abajo- y que el volumen siga siendo un FAT12
 * valido.
 *
 * SIRVE PARA LOS DOS MAPAS DE FLASH, Y SE ENTERA SOLO DE CUAL ES. 02/10/2026.
 *
 * La rama "deepsdr" tiene la aplicacion en 0x08020000, ocho bytes de firma
 * clavados en 0x08060000 y tope de 0x50000. La rama "deepsdr-reload" la
 * tiene en 0x0800C000, de una pieza, y valida con cabecera + CRC32. Las
 * partes comunes -que son casi todas- se escriben una vez, y las cuatro
 * pruebas que dependen del mapa van detras de un #ifdef sobre
 * CARGA_CAB_MAGIA, que solo existe en la rama nueva.
 *
 * Asi un solo simulador vale para las dos ramas. La alternativa era
 * duplicar la carpeta entera, y entonces el proximo arreglo habria que
 * meterlo dos veces o una de las dos copias se pudriria.
 *
 * DE DONDE SALE EL COMPORTAMIENTO. Del desensamblado del boot.bin del
 * proyecto ("Bootloader v4.0.3 by BH5HNU"), no de suponer:
 *
 *   0x800a688  cmp.w r4, #0x50000        el tope, sobre fp.objsize
 *   0x800a68e  si no cabe: f_close y a comprobar, SIN borrar el fichero
 *   0x800a6c8  borrar sectores 5, 6 y 7
 *   0x800a748  bucle de 2048 bytes a 0x08020000 + offset
 *   0x800a7a8  f_unlink("update4.bin")   <- esto no estaba documentado
 *   0x800a7ca  ubfx del SP + memcmp de 8 bytes en 0x08060000
 *
 * El f_unlink es el que importa: sin el, el clon pasaria este banco y
 * luego regrabaria 320 kB en CADA arranque de la radio.
 */
#include "w25q_sim.h"
#include "fmc_sim.h"
#include "cargador.h"
#include "spi_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * EL VOLUMEN LO DICE EL FICHERO, NO UNA CONSTANTE - 08/10/2026, de la
 * revision a fondo de ese dia.
 *
 * Esto era 1 MB fijo, y out/fat12_2k.img mide 7. O sea que el banco
 * cargaba el primer megabyte de una imagen de siete y lo montaba sobre
 * un chip simulado de dos: un volumen cuyo BPB declara 14.336 sectores
 * dentro de un disco de 2.048. Eso NO PUEDE EXISTIR en hardware -un
 * volumen de 7 MB implica un chip de 8- y es exactamente la geometria
 * imposible que spi_flash.c aprendio a rechazar ese mismo dia, cuando se
 * cerro el agujero de generar direcciones por encima de la zona alta.
 *
 * El banco pasaba porque hasta entonces nadie contrastaba el BPB con el
 * tamaño del disco. Al cerrarlo, el banco se puso rojo: no por el
 * arreglo, sino porque estaba simulando hardware que no se puede
 * construir. sim/fatanade.c ya hacia lo correcto con esta misma imagen
 * -empareja cada .img con su w25q_tam()-, y aqui faltaba.
 */
static uint32_t VOLUMEN;
#define BPS      512U

static int fallos = 0;

static uint16_t le16(uint32_t o) { return (uint16_t)(g_chip[o] | (g_chip[o+1] << 8)); }
static uint32_t raiz(void)  { return (uint32_t)(le16(14) + g_chip[16] * le16(22)) * BPS; }
static uint32_t datos(void) { return raiz() + (uint32_t)le16(17) * 32U; }
static uint32_t fat1(void)  { return (uint32_t)le16(14) * BPS; }

/*
 * SECTORES POR CLUSTER, LEIDOS DEL PROPIO VOLUMEN (byte 13 del BPB).
 *
 * *** 02/10/2026. El volumen de fabrica tiene UN sector por cluster y por
 * eso esto no existia: todo el banco daba por hecho cluster = sector. En
 * cuanto Windows formatea con clusters de 4 kB deja de ser verdad. ***
 *
 * Se lee del BPB y no se le pregunta al firmware, por lo de siempre: si el
 * banco usara la misma fuente que el codigo que prueba, no podria cazar
 * que el codigo lee el cluster equivocado.
 */
static uint32_t spc(void)   { return g_chip[13] ? g_chip[13] : 1U; }
static uint32_t cluster_bytes(void) { return spc() * BPS; }

static void fat_pon(uint32_t c, uint16_t v)
{
    uint32_t i;
    for (i = 0U; i < 2U; i++) {                 /* las DOS copias */
        uint32_t b = fat1() + i * le16(22) * BPS + c + (c >> 1);
        if (c & 1U) {
            g_chip[b]   = (uint8_t)((g_chip[b] & 0x0FU) | ((v & 0x0FU) << 4));
            g_chip[b+1] = (uint8_t)(v >> 4);
        } else {
            g_chip[b]   = (uint8_t)(v & 0xFFU);
            g_chip[b+1] = (uint8_t)((g_chip[b+1] & 0xF0U) | ((v >> 8) & 0x0FU));
        }
    }
}

/* Deja `n11` en la ranura `ranura` con la cadena `orden` y `bytes` bytes de
 * `cont`. `orden` permite montar a proposito un fichero troceado. */
static void pon_fichero(const char *n11, const uint32_t *orden, uint32_t nclus,
                        const uint8_t *cont, uint32_t bytes, uint32_t ranura)
{
    uint8_t *e = &g_chip[raiz() + ranura * 32U];
    uint32_t i, puestos = 0U;

    memcpy(e, n11, 11); e[11] = 0x20U;
    e[26] = (uint8_t)(orden[0] & 0xFFU); e[27] = (uint8_t)(orden[0] >> 8);
    e[28] = (uint8_t)(bytes); e[29] = (uint8_t)(bytes >> 8);
    e[30] = (uint8_t)(bytes >> 16); e[31] = (uint8_t)(bytes >> 24);
    for (i = 0U; i < nclus; i++) {
        uint32_t n = bytes - puestos;
        if (n > cluster_bytes()) { n = cluster_bytes(); }
        memcpy(&g_chip[datos() + (orden[i] - 2U) * cluster_bytes()],
               &cont[puestos], n);
        puestos += n;
        fat_pon(orden[i], (i + 1U < nclus) ? (uint16_t)orden[i+1U] : 0x0FFFU);
    }
}

static void baraja(uint32_t *v, uint32_t n)
{
    uint32_t *t;
    uint32_t a = 0U, b = n, k = 0U;

    /* Con n = 0 el malloc puede devolver NULL y el memcpy de abajo leeria
     * de un puntero sin inicializar - el compilador lo avisaba y tenia
     * razon. Barajar cero elementos es no hacer nada. */
    if (n == 0U) { return; }
    t = malloc(n * sizeof(uint32_t));
    if (t == 0) { return; }
    memcpy(t, v, n * sizeof(uint32_t));
    while (a < b) { v[k++] = t[--b]; if (a < b) { v[k++] = t[a++]; } }
    free(t);
}

static uint8_t *plantilla;      /* el volumen vacio, para recargarlo */
static uint8_t *fichero;
static uint32_t fichero_n;

static void monta(void) { memcpy(g_chip, plantilla, VOLUMEN); spi_flash_init(); }

/*
 * EL NOMBRE 8.3 DEL FICHERO, ESCRITO AQUI A MANO.
 *
 * Y a mano a proposito, aunque el firmware tambien lo tenga: si el banco
 * se lo preguntara a cargador.c, no podria cazar NUNCA que cargador.c
 * busca el nombre equivocado -compararia el nombre consigo mismo-. Es
 * exactamente el fallo que se introduciria al renombrar el fichero y
 * dejarse un sitio sin tocar.
 *
 * La rama "deepsdr" usa UPDATE4.BIN, que era como lo llamaba el gestor de
 * fabrica; la rama "reload" usa UPDATE.BIN (02/10/2026, por el usuario).
 * Ocho caracteres de nombre rellenos con espacios, mas tres de extension.
 */
#ifdef CARGA_CAB_MAGIA
#define NOMBRE_DIR  "UPDATE  BIN"
#define NOMBRE_TXT  "update.bin"
#else
#define NOMBRE_DIR  "UPDATE4 BIN"
#define NOMBRE_TXT  "update4.bin"
#endif

/* Deja el fichero de `bytes` bytes, troceado si `trocea`, en la ranura 1. */
static void pon_update(uint32_t bytes, int trocea)
{
    uint32_t nclus = (bytes + cluster_bytes() - 1U) / cluster_bytes(), i;
    uint32_t *cl = malloc(nclus * sizeof(uint32_t));
    for (i = 0U; i < nclus; i++) { cl[i] = 2U + i; }
    if (trocea) { baraja(cl, nclus); }
    pon_fichero(NOMBRE_DIR, cl, nclus, fichero, bytes, 1U);
    free(cl);
}

/*
 * EL MAPA DE SECTORES DEL GD32F4xx, ESCRITO AQUI APARTE.
 *
 * Si el banco se lo preguntara al mismo sitio que el cargador, comprobaria
 * que los dos dicen lo mismo, no que lo que dicen es verdad. Esta tabla es
 * la del manual (tabla 2-1) copiada a mano, y de ella salen dos numeros
 * que antes estaban escritos a pelo: cuantos sectores tiene que borrar el
 * cargador, y donde cae el segundo de ellos.
 */
/* Una palabra little-endian de un buffer, sin cast a uint32_t* (que sobre
 * un puntero de alineacion desconocida es comportamiento indefinido). */
static uint32_t le32v(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const uint32_t k_sect[] = {
    0x08000000UL, 0x08004000UL, 0x08008000UL, 0x0800C000UL,
    0x08010000UL, 0x08020000UL, 0x08040000UL, 0x08060000UL, 0x08080000UL
};
#define N_SECT (sizeof k_sect / sizeof k_sect[0])

/* Cuantos sectores ocupa la region de la aplicacion. */
static uint32_t sectores_app(void)
{
    uint32_t n = 0U, i;
    for (i = 0U; i + 1U < N_SECT; i++) {
        if (k_sect[i] >= CARGA_APP_BASE && k_sect[i] < CARGA_BORRA_HASTA) { n++; }
    }
    return n;
}

/* Un desplazamiento dentro del SEGUNDO sector de la aplicacion, para la
 * prueba de regrabar encima: con la flash virgen, borrar de menos no se
 * nota, porque escribir sobre 0xFF siempre funciona. */
static uint32_t off_segundo_sector(void)
{
    uint32_t i;
    for (i = 0U; i + 1U < N_SECT; i++) {
        if (k_sect[i] > CARGA_APP_BASE) { return k_sect[i] - CARGA_APP_BASE + 0x1000UL; }
    }
    return 0x1000UL;
}

static void di(const char *que, int bien, const char *detalle)
{
    printf("    %-52s %s%s%s\n", que, bien ? "ok " : "*** MAL ***",
           detalle ? "  " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

/* ¿Estan los `n` bytes del fichero en la base de la aplicacion, uno a uno? */
static int imagen_igual(uint32_t n)
{
    const uint8_t *m = fmc_sim_mem() + (CARGA_APP_BASE - FMC_SIM_BASE);
    return memcmp(m, fichero, n) == 0;
}

static int hay_update(void)
{
    uint32_t r, s = (datos() - raiz()) / 32U;
    for (r = 0U; r < s; r++) {
        const uint8_t *e = &g_chip[raiz() + r * 32U];
        if (e[0] == 0x00U) { return 0; }
        if (e[0] == 0xE5U) { continue; }
        if (memcmp(e, NOMBRE_DIR, 11) == 0) { return 1; }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *img = (argc > 1) ? argv[1] : "out/fat12_vacio.img";
    const char *upd = (argc > 2) ? argv[2] : NOMBRE_TXT;
    carga_r_t r;
    FILE *f;

    printf("\nEL CARGADOR DE ARRANQUE, CONTRA EL " NOMBRE_TXT " DE VERDAD\n");

    f = fopen(img, "rb");
    if (!f) { printf("  falta %s\n", img); return 1; }
    fseek(f, 0, SEEK_END); VOLUMEN = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    /*
     * El chip simulado tiene que ser el volumen MAS la zona alta, que es
     * como esta construida la radio: el disco USB es el chip menos el
     * ultimo megabyte. Con un chip del tamaño justo del volumen, la zona
     * alta se solaparia con los datos y el driver -con razon- se negaria.
     */
    w25q_tam(VOLUMEN + (1024U * 1024U));
    plantilla = malloc(VOLUMEN);
    if (!plantilla || fread(plantilla, 1, VOLUMEN, f) != VOLUMEN) {
        printf("  no puedo leer %s\n", img); return 1;
    }
    fclose(f);

    f = fopen(upd, "rb");
    if (!f) { printf("  falta %s\n", upd); return 1; }
    fseek(f, 0, SEEK_END); fichero_n = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    fichero = malloc(fichero_n);
    if (fread(fichero, 1, fichero_n, f) != fichero_n) { return 1; }
    fclose(f);
    printf("  %s: %u bytes\n", upd, fichero_n);
    printf("  volumen: %u bytes/sector, %u sectores/cluster -> cluster de %u\n",
           (unsigned)BPS, (unsigned)spc(), (unsigned)cluster_bytes());

    /* Lo primero: que el fichero que vamos a usar es el que el gestor
     * acepta. Si esto falla, el resto del banco no dice nada. */
    printf("\n  el fichero de prueba\n");
    di("cabe en la region de la aplicacion", fichero_n <= CARGA_TAM_MAX, 0);
#ifdef CARGA_CAB_MAGIA
    {
        /*
         * EL CRC, COMPROBADO CONTRA UNA IMPLEMENTACION QUE NO ES LA SUYA.
         *
         * Aqui abajo esta el CRC-32/MPEG-2 BIT A BIT, que es la definicion.
         * El firmware usa una tabla de nibble y tools/cabecera.py usa otro
         * bucle bit a bit en Python. Son tres caminos distintos y los tres
         * tienen que dar el mismo numero; si alguien cambia uno "porque es
         * lo mismo" -el caso tipico es poner zlib.crc32, que va reflejado y
         * con xor final y da OTRA cosa- se ve aqui y no en la radio.
         */
        uint32_t crc = 0xFFFFFFFFU, i, j, dicho;
        for (i = 0U; i < fichero_n; i++) {
            uint8_t b = fichero[i];
            /* el campo del propio CRC se cuenta como cero */
            if (i >= CARGA_CAB_OFF + 8U && i < CARGA_CAB_OFF + 12U) { b = 0U; }
            crc ^= (uint32_t)b << 24;
            for (j = 0U; j < 8U; j++) {
                crc = (crc & 0x80000000U) ? ((crc << 1) ^ 0x04C11DB7U) : (crc << 1);
            }
        }
        di("el valor de control del CRC es el de CRC-32/MPEG-2",
           cargador_crc32(0xFFFFFFFFU, (const uint8_t *)"123456789", 9U)
           == 0x0376E6E7U, 0);

        di("lleva la magia \"DSDR\" en la cabecera",
           fichero_n > CARGA_CAB_OFF + 16U
           && le32v(&fichero[CARGA_CAB_OFF]) == CARGA_CAB_MAGIA, 0);
        di("la longitud de la cabecera es la del fichero",
           le32v(&fichero[CARGA_CAB_OFF + 4U]) == fichero_n, 0);
        dicho = le32v(&fichero[CARGA_CAB_OFF + 8U]);
        di("y el CRC que trae es el que sale de calcularlo aqui",
           dicho == crc, 0);
    }
#else
    {
        static const uint8_t fir[8] = {0x8F,0x25,0xC8,0x65,0x59,0x9C,0x55,0x31};
        di("lleva la firma en el desplazamiento 0x40000",
           fichero_n > 0x40008U && memcmp(&fichero[0x40000], fir, 8) == 0, 0);
    }
#endif

    /* ---- el caso normal ---------------------------------------------- */
    printf("\n  grabar un " NOMBRE_TXT " seguido\n");
    w25q_reset(); monta(); fmc_sim_reset();
    pon_update(fichero_n, 0);
    r = cargador_arranca(fmc_sim_iface());
    di("devuelve \"grabada y arranca\"", r == CARGA_GRABADA, cargador_porque_txt(r));
    di("los bytes de la base son los del fichero", imagen_igual(fichero_n), 0);
    di("grabo exactamente el tamano del fichero",
       cargador_grabados() == fichero_n, 0);
    di("borro todos los sectores de la aplicacion, ni uno mas",
       fmc_sim_borrados() == sectores_app(), 0);
    di("no grabo sobre nada sin borrar", fmc_sim_fallos() == 0U, 0);
    di("el fichero ya no esta en el volumen", !hay_update(), 0);

    /*
     * SEGUNDA PASADA SOBRE UNA FLASH YA ESCRITA.
     *
     * *** Y esto lo aprendi rompiendo el cargador a proposito. *** Con la
     * flash virgen -todo a 0xFF- se puede borrar de menos y no se nota:
     * escribir sobre 0xFF siempre funciona. El caso que caza el borrado
     * corto es el de VERDAD: regrabar encima de una imagen que ya esta.
     * Aqui NO se llama a fmc_sim_reset(), a proposito.
     */
    printf("\n  regrabar ENCIMA de una imagen que ya esta\n");
    /*
     * Y OTRA VUELTA DE TUERCA: la primera pasada deja a CEROS un trozo que
     * cae dentro del sector 6, y la segunda graba el fichero de verdad, que
     * ahi tiene unos. Un bit no puede subir de 0 a 1 sin borrar el sector,
     * asi que si el cargador borra de menos, fmc_sim_fallos() lo canta.
     * Con el mismo fichero las dos veces NO se cantaria: grabar los mismos
     * bytes encima de si mismos no sube ningun bit.
     */
    w25q_reset(); monta(); fmc_sim_reset();
    {
        uint8_t *ceros = malloc(fichero_n), *viejo = fichero;
        memcpy(ceros, fichero, fichero_n);
        memset(&ceros[off_segundo_sector()], 0x00, 0x3000U);  /* 2o sector */
        fichero = ceros;
        pon_update(fichero_n, 0);
        (void)cargador_arranca(fmc_sim_iface());
        fichero = viejo;
        free(ceros);
    }
    monta();
    pon_update(fichero_n, 0);
    r = cargador_arranca(fmc_sim_iface());
    di("vuelve a grabar y arranca", r == CARGA_GRABADA, cargador_porque_txt(r));
    di("los bytes son los del fichero", imagen_igual(fichero_n), 0);
    di("no grabo un solo bit sobre algo sin borrar", fmc_sim_fallos() == 0U, 0);

    /* ---- troceado ------------------------------------------------------ */
    printf("\n  el mismo fichero TROCEADO a mala idea\n");
    w25q_reset(); monta(); fmc_sim_reset();
    pon_update(fichero_n, 1);
    r = cargador_arranca(fmc_sim_iface());
    di("devuelve \"grabada y arranca\"", r == CARGA_GRABADA, cargador_porque_txt(r));
    di("los bytes siguen siendo los del fichero, EN ORDEN",
       imagen_igual(fichero_n), 0);

    /* ---- sin fichero, con la imagen ya dentro -------------------------- */
    printf("\n  sin fichero, con la imagen buena ya grabada\n");
    monta();
    r = cargador_arranca(fmc_sim_iface());
    di("arranca sin regrabar", r == CARGA_ARRANCA, cargador_porque_txt(r));
    di("no toco la flash interna", cargador_grabados() == 0U, 0);

    /* ---- flash virgen -------------------------------------------------- */
    printf("\n  flash interna virgen y sin fichero\n");
    w25q_reset(); monta(); fmc_sim_reset();
    r = cargador_arranca(fmc_sim_iface());
    di("dice que no hay aplicacion", r == CARGA_SIN_APP, cargador_porque_txt(r));

    /* ---- fichero demasiado grande -------------------------------------- */
    printf("\n  un fichero que no cabe en la region\n");
    w25q_reset(); monta(); fmc_sim_reset();
    {
        uint32_t grande = CARGA_TAM_MAX + 4U;
        uint8_t *g = calloc(grande, 1); uint8_t *viejo = fichero; uint32_t vn = fichero_n;
        memcpy(g, fichero, fichero_n);
        fichero = g; fichero_n = grande;
        pon_update(grande, 0);
        fichero = viejo; fichero_n = vn;
        r = cargador_arranca(fmc_sim_iface());
        di("ni lo graba ni borra sectores",
           r == CARGA_FICHERO_GRANDE && fmc_sim_borrados() == 0U, cargador_porque_txt(r));
        di("y lo DEJA en el volumen, como el gestor de verdad", hay_update(), 0);
        free(g);
    }

    /* ---- que sabe fallar ------------------------------------------------ */
    printf("\n  y que sabe fallar\n");
#ifdef CARGA_CAB_MAGIA
    /*
     * LAS TRES FORMAS DE QUE LA IMAGEN ESTE MAL, Y CADA UNA CON SU MOTIVO.
     *
     * La tercera -la imagen cortada- es la que el gestor de fabrica NO
     * podia ver: con ocho bytes constantes como unica prueba, media imagen
     * con esos ocho bytes dentro pasaba y arrancaba. Es el caso de verdad:
     * se va la luz grabando, o el pendrive se suelta a mitad.
     */
    w25q_reset(); monta(); fmc_sim_reset();
    {
        uint8_t *malo = malloc(fichero_n), *viejo = fichero;
        memcpy(malo, fichero, fichero_n);
        malo[fichero_n / 2U] ^= 0x01U;       /* UN bit, en medio de la imagen */
        fichero = malo;
        pon_update(fichero_n, 0);
        fichero = viejo;
        r = cargador_arranca(fmc_sim_iface());
        di("con UN bit cambiado NO arranca", r == CARGA_CRC_MALO,
           cargador_porque_txt(r));
        free(malo);
    }
    w25q_reset(); monta(); fmc_sim_reset();
    {
        uint8_t *malo = malloc(fichero_n), *viejo = fichero;
        memcpy(malo, fichero, fichero_n);
        memset(&malo[CARGA_CAB_OFF], 0x00U, 4U);      /* sin magia */
        fichero = malo;
        pon_update(fichero_n, 0);
        fichero = viejo;
        r = cargador_arranca(fmc_sim_iface());
        di("sin magia dice que ahi no hay imagen", r == CARGA_SIN_CABECERA,
           cargador_porque_txt(r));
        free(malo);
    }
    w25q_reset(); monta(); fmc_sim_reset();
    {
        uint32_t mitad = (fichero_n / 2U) & ~3UL;
        uint32_t vn = fichero_n;
        fichero_n = mitad;                   /* el fichero, cortado por la mitad */
        pon_update(mitad, 0);
        fichero_n = vn;
        r = cargador_arranca(fmc_sim_iface());
        di("una imagen CORTADA no arranca (la firma vieja no lo veia)",
           r == CARGA_CRC_MALO, cargador_porque_txt(r));
    }
    w25q_reset(); monta(); fmc_sim_reset();
    {
        uint8_t *malo = malloc(fichero_n), *viejo = fichero;
        memcpy(malo, fichero, fichero_n);
        malo[CARGA_CAB_OFF + 4U] = 0x01U;    /* longitud impar e imposible */
        malo[CARGA_CAB_OFF + 5U] = 0x00U;
        malo[CARGA_CAB_OFF + 6U] = 0x00U;
        malo[CARGA_CAB_OFF + 7U] = 0x00U;
        fichero = malo;
        pon_update(fichero_n, 0);
        fichero = viejo;
        r = cargador_arranca(fmc_sim_iface());
        di("una longitud imposible se distingue de un CRC malo",
           r == CARGA_CAB_RARA, cargador_porque_txt(r));
        free(malo);
    }
#else
    w25q_reset(); monta(); fmc_sim_reset();
    {
        uint8_t *malo = malloc(fichero_n), *viejo = fichero;
        memcpy(malo, fichero, fichero_n);
        malo[0x40003] ^= 0xFFU;              /* un bit de la firma */
        fichero = malo;
        pon_update(fichero_n, 0);
        fichero = viejo;
        r = cargador_arranca(fmc_sim_iface());
        di("con la firma tocada NO arranca", r == CARGA_SIN_FIRMA, cargador_porque_txt(r));
        free(malo);
    }
#endif
    w25q_reset(); monta(); fmc_sim_reset();
    {
        uint8_t *malo = malloc(fichero_n), *viejo = fichero;
        memcpy(malo, fichero, fichero_n);
        malo[0] = 0x00U; malo[1] = 0x00U; malo[2] = 0x00U; malo[3] = 0x20U; /* SP fuera del TCM */
        fichero = malo;
        pon_update(fichero_n, 0);
        fichero = viejo;
        r = cargador_arranca(fmc_sim_iface());
        di("con la pila fuera del TCM NO arranca", r == CARGA_SIN_APP, cargador_porque_txt(r));
        free(malo);
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
