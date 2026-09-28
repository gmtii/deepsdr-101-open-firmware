#include "zona_alta.h"

/*
 * EL DIRECTORIO, byte a byte. Todo entero de menor peso primero.
 *
 *    0   'D','S','D','R'
 *    4   u8  version = 1
 *    5   u8  cuantas secciones
 *    6   u16 cero
 *    8   u32 lo que mide DATOS.BIN entero
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
        if (addr != 0) { *addr = 0x101000UL; }
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
