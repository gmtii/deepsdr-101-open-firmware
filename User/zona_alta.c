#include "zona_alta.h"
#include "spi_flash.h"

/*
 * Las dos cuentas. Ver el comentario de ZA_BYTES_REGION en zona_alta.h:
 * la region de datos es el ultimo megabyte del chip, mida lo que mida el
 * chip, y spi_flash_capacidad() ya cachea su respuesta.
 */
uint32_t za_tope(void) { return spi_flash_capacidad(); }

uint32_t za_base(void)
{
    return za_disco_de(spi_flash_capacidad()) + ZA_HUECO;
}

/*
 * El hueco del mapa, arriba del todo. Ver ZA_MAPA_BYTES en zona_alta.h.
 *
 * Sale del techo hacia abajo y no de la base hacia arriba a proposito: la
 * base se mueve con la capacidad del chip y el techo tambien, pero lo que
 * tiene que quedar fijo es el TAMANO del hueco, no su direccion. Asi el
 * mismo .map vale en un chip de 2 MB y en uno de 16, igual que el BD.BIN.
 *
 * Cae en bloque de borrado: la capacidad es potencia de dos y 0x30000
 * tambien es multiplo de 4 kB, asi que la resta tambien lo es. Si algun
 * dia deja de serlo, el borrado de un bloque se llevaria por delante el
 * final de los datos, que es justo lo que ZA_HUECO evita por abajo.
 */
uint32_t za_mapa(void)
{
    return spi_flash_capacidad() - ZA_MAPA_BYTES;
}

/*
 * EL DIRECTORIO, byte a byte. Todo entero de menor peso primero.
 *
 *    0   'D','S','D','R'
 *    4   u8  version = 1
 *    5   u8  cuantas secciones
 *    6   u16 cero
 *    8   u32 lo que mide BD.BIN entero
 *   12   u32 suma de los bytes 0..11 y de las secciones de abajo
 *   16   secciones, 16 bytes cada una:
 *          +0   u32 tipo ('I24B', 'EMIS')
 *          +4   u32 desde donde empieza, contado desde 0x101000
 *          +8   u32 lo que mide
 *          +12  u32 su suma
 *
 * y el resto de los 4.096 bytes sin usar.
 *
 * POR QUE LA SUMA DEL DIRECTORIO SE MIRA SIEMPRE Y LA DE LAS SECCIONES NO.
 * El directorio son 144 bytes y se lee en el arranque: comprobarlo es
 * gratis y es lo que impide que unos bytes cualesquiera que hubiera ahi se
 * lean como desplazamientos y manden a leer a cualquier parte. Las
 * secciones son casi medio megabyte por un bus lento, del orden de dos
 * segundos: eso no se hace al arrancar, se hace al cargar.
 */

#define CAB_LEN  16U
#define SEC_LEN  16U

static void (*s_lee)(uint32_t, uint8_t *, uint32_t);
static uint8_t  s_hay;
static uint8_t  s_n;
static uint32_t s_tam;
static uint32_t s_tipo[ZA_SEC_MAX];
static uint32_t s_off[ZA_SEC_MAX];
static uint32_t s_sec_tam[ZA_SEC_MAX];
static uint32_t s_sec_suma[ZA_SEC_MAX];

static uint32_t u32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

uint32_t zona_alta_suma(const uint8_t *b, uint32_t n, uint32_t v)
{
    uint32_t i;

    for (i = 0U; i < n; i++) { v = (v ^ (uint32_t)b[i]) * 16777619UL; }
    return v;
}

uint8_t zona_alta_abre(void (*lee)(uint32_t addr, uint8_t *buf, uint32_t len))
{
    uint8_t b[CAB_LEN + SEC_LEN * ZA_SEC_MAX];
    uint8_t i, n;

    s_hay = 0U;
    s_n = 0U;
    s_lee = lee;
    if (lee == 0) { return 0U; }

    lee(ZA_BASE, b, (uint32_t)sizeof b);
    if (b[0] != 'D' || b[1] != 'S' || b[2] != 'D' || b[3] != 'R') { return 0U; }
    if (b[4] != 1U) { return 0U; }

    n = b[5];
    if (n == 0U || n > (uint8_t)ZA_SEC_MAX) { return 0U; }
    s_tam = u32(&b[8]);

    /*
     * LA SUMA, ANTES DE CREERSE UN SOLO DESPLAZAMIENTO. Se calcula sobre
     * los doce primeros bytes y sobre las secciones, saltandose los cuatro
     * de la propia suma - que es lo unico que no puede entrar en ella-.
     */
    {
        uint32_t v = zona_alta_suma(b, 12U, ZA_SUMA_INICIO);

        v = zona_alta_suma(&b[CAB_LEN], (uint32_t)n * SEC_LEN, v);
        if (v != u32(&b[12])) { return 0U; }
    }

    /*
     * EL LIMITE SIGUE SIENDO EL TECHO DEL CHIP, NO EL DEL MAPA - 12/10/2026.
     *
     * La primera version de esto comparaba contra ZA_TOPE_DATOS, para que
     * un BD.BIN no pudiera meterse en el hueco del mapa. Estaba mal, y de
     * la manera que mas duele: una radio que YA tiene cargada una base de
     * datos grande -hecha con el empaquetador de antes, cuando el techo
     * era el chip entero- se quedaria sin aviones y sin emisoras al
     * actualizar el firmware. Datos que funcionaban, perdidos por un
     * limite nuevo que no tiene nada que ver con ellos.
     *
     * Quien tiene que ceder es el mapa, que todavia no existe, no la base
     * de datos, que ya esta. Asi que aqui se admite lo que haya y es
     * mapa_flash_graba() quien mira si cabe y se niega -diciendolo- si el
     * BD.BIN llega hasta arriba. Ver zona_alta_estorba().
     */
    if (s_tam <= ZA_CAB || s_tam > (ZA_TOPE - ZA_BASE)) { return 0U; }

    for (i = 0U; i < n; i++) {
        const uint8_t *p = &b[CAB_LEN + (uint32_t)i * SEC_LEN];
        uint32_t off = u32(&p[4]);
        uint32_t tam = u32(&p[8]);

        /*
         * Y QUE CADA SECCION QUEPA DENTRO DEL FICHERO. Sin esto, un
         * directorio con la suma buena pero escrito por otra version
         * podria mandar a leer por encima de 0x180000, que es el medio
         * mega que no se toca.
         */
        if (off < ZA_CAB || tam == 0UL) { return 0U; }
        if (off > s_tam || tam > s_tam - off) { return 0U; }
        s_tipo[i] = u32(&p[0]);
        s_off[i] = off;
        s_sec_tam[i] = tam;
        s_sec_suma[i] = u32(&p[12]);
    }

    s_n = n;
    s_hay = 1U;
    return 1U;
}

uint8_t zona_alta_secciones(void) { return s_hay ? s_n : 0U; }

uint8_t zona_alta_seccion(uint8_t i, uint32_t *tipo, uint32_t *addr,
                          uint32_t *tam)
{
    if (!s_hay || i >= s_n) { return 0U; }
    if (tipo != 0) { *tipo = s_tipo[i]; }
    if (addr != 0) { *addr = ZA_BASE + s_off[i]; }
    if (tam != 0)  { *tam = s_sec_tam[i]; }
    return 1U;
}

uint8_t zona_alta_donde(uint32_t tipo, uint32_t *addr, uint32_t *tam)
{
    uint8_t i;

    if (s_hay) {
        for (i = 0U; i < s_n; i++) {
            if (s_tipo[i] == tipo) {
                if (addr != 0) { *addr = ZA_BASE + s_off[i]; }
                if (tam != 0)  { *tam = s_sec_tam[i]; }
                return 1U;
            }
        }
        /*
         * CON DIRECTORIO, LO QUE NO ESTA NO ESTA. Nada de caer en la
         * distribucion vieja: si hay directorio, ahi vive TODO, y una
         * seccion que no figura es una que no se ha cargado. Mirar
         * ademas la direccion vieja leeria los bytes de otra seccion y
         * los daria por buenos si por casualidad cuadraran.
         */
        return 0U;
    }

    /*
     * SIN DIRECTORIO: la distribucion vieja, la de los dos ficheros
     * sueltos. Es lo que permite actualizar el firmware sin tener que
     * volver a cargar los datos que ya estaban.
     */
    if (tipo == ZA_TIPO_AVIONES) {
        if (addr != 0) { *addr = za_base(); }
        if (tam != 0)  { *tam = 0UL; }        /* no se sabe, la lee su cabecera */
        return 1U;
    }
    if (tipo == ZA_TIPO_EMISORAS) {
        if (addr != 0) { *addr = 0x140000UL; }
        if (tam != 0)  { *tam = 0UL; }
        return 1U;
    }
    return 0U;
}

uint8_t zona_alta_verifica(void)
{
    return zona_alta_verifica_p(0);
}

uint8_t zona_alta_verifica_p(void (*avisa)(uint32_t hechos, uint32_t total))
{
    uint8_t i;
    uint32_t total = 0UL, hechos = 0UL;

    if (!s_hay) { return 0U; }

    /* El total ANTES de empezar: una barra cuyo total crece sobre la
     * marcha va hacia atras, y eso se lee como un fallo. */
    for (i = 0U; i < s_n; i++) { total += s_sec_tam[i]; }

    for (i = 0U; i < s_n; i++) {
        uint8_t buf[256];
        uint32_t p = 0UL, v = ZA_SUMA_INICIO;

        while (p < s_sec_tam[i]) {
            uint32_t n = s_sec_tam[i] - p;

            if (n > (uint32_t)sizeof buf) { n = (uint32_t)sizeof buf; }
            s_lee(ZA_BASE + s_off[i] + p, buf, n);
            v = zona_alta_suma(buf, n, v);
            p += n;
            hechos += n;
            if (avisa != 0) { avisa(hechos, total); }
        }
        /*
         * Se sigue devolviendo 0 en cuanto una seccion no cuadra, sin
         * mirar las demas: no hay nada que ganar y son otros ocho
         * segundos de barra.
         */
        if (v != s_sec_suma[i]) { return 0U; }
    }
    return 1U;
}

/*
 * SI LO QUE HAY CARGADO LLEGA HASTA EL HUECO DEL MAPA - 12/10/2026.
 *
 * Devuelve 1 si alguna seccion del directorio termina por encima de
 * ZA_MAPA, es decir, si grabar un mapa escribiria encima de datos que
 * estan en uso. Lo pregunta mapa_flash_graba() ANTES de borrar un solo
 * bloque.
 *
 * Sin directorio devuelve 0: la distribucion vieja acaba en 0x180000 y el
 * hueco del mapa empieza mucho mas arriba en cualquier chip de los que
 * admite esta rama. Ver zona_alta_donde().
 */
uint8_t zona_alta_estorba(void)
{
    uint8_t i;

    if (!s_hay) { return 0U; }
    for (i = 0U; i < s_n; i++) {
        if ((ZA_BASE + s_off[i] + s_sec_tam[i]) > ZA_MAPA) { return 1U; }
    }
    return 0U;
}
