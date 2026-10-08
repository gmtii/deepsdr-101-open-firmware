#include "emisoras.h"
#include <string.h>

/*
 * EL FORMATO, que lo escribe tools/emisoras_pack.py. Todo entero de menor
 * peso primero.
 *
 *   cabecera, 56 bytes
 *     0   'E','M','I','S'
 *     4   u8  version = 2   (era 1 cuando se escribio esta especificacion;
 *                             el lector exige 2 y tools/emisoras_pack.py
 *                             empaqueta 2 - corregido el 30/09/2026, que un
 *                             tercer empaquetador escrito leyendo esto
 *                             produciria un fichero que la radio rechaza sin
 *                             decir por que)
 *     5   u8  n_paises (cuantas cadenas del final son codigos ITU)
 *     6   u16 n_textos
 *     8   u32 n_frecuencias
 *     12  u32 n_entradas
 *     16  u32 off_frecuencias
 *     20  u32 off_entradas
 *     24  u32 off_textos
 *     28  u32 off_cadenas (= off_textos + 2*n_textos)
 *     32  u32 tam_total
 *     36  u32 valido_hasta (aaaammdd)
 *     40  i8  horas_utc (lo que hay que restarle al reloj local)
 *     41  7 bytes a cero
 *     48  u32 suma_cabecera (de los bytes 0..47)
 *     52  u32 suma_datos    (de los bytes 56..tam_total)
 *
 * EL DESFASE HORARIO VIENE EN EL FICHERO Y NO EN EL FIRMWARE. El reloj de
 * esta radio marca hora local española (ver reloj.h: no hay RTC, hay un
 * desfase sobre el contador de arranque) y las dos listas dan las horas en
 * UTC. Sin restar nada, la radio enseñaria la emisora de hace dos horas -
 * y eso es de los fallos peores que hay, porque el resultado PARECE bueno:
 * sale un nombre, en un idioma verosimil, de un pais verosimil.
 *
 * Va en el fichero porque es el fichero quien sabe de que temporada es: la
 * lista A26 vale del 29 de marzo al 25 de octubre de 2026, que es
 * exactamente el horario de verano europeo, asi que mientras vale son 2
 * horas siempre. La de invierno son 1. Ponerlo como constante del firmware
 * obligaria a recompilar cada seis meses y, peor, a acordarse.
 *
 * POR QUE HAY UNA SUMA Y NO SOLO CAMPOS QUE CUADREN ENTRE SI. La primera
 * version se apoyaba en eso -que los desplazamientos encajaran- y el
 * banco la midio cambiando un byte de la cabecera cada vez: se colaban
 * SIETE de treinta y seis. La peor era la del byte 5, n_paises, porque
 * desplaza el indice de pais y entonces en la columna del pais sale un
 * NOMBRE DE EMISORA: un dato inventado con pinta de bueno, que es
 * exactamente lo que no puede salir en una pantalla.
 *
 * Con la suma no se cuela ninguna. La de datos NO se mira al abrir -son
 * 206 kB por un bus lento- sino al cargar el fichero, que es cuando de
 * verdad puede haberse copiado mal: ver emisoras_verifica().
 *
 *   frecuencias: 8 bytes, ORDENADAS por hz -> busqueda binaria
 *     u32 hz, u16 primera entrada, u16 cuantas
 *
 *   entradas: 12 bytes
 *     u16 ini, u16 fin, u8 dias, u8 pais, u16 nombre, u16 sitio, u16 idioma
 *
 *   textos: n_textos u16 de desplazamiento y detras las cadenas con su cero.
 *
 * POR QUE ORDENADO POR FRECUENCIA. La radio pregunta siempre lo mismo
 * -"quien hay en estos kilohercios"- y con la tabla ordenada eso son once
 * lecturas de ocho bytes sobre un bus SPI lento en vez de recorrer 15 kB.
 */

#define CAB_LEN   56U
#define FREC_LEN   8U
#define ENT_LEN   12U
#define TXT_MAX   48U     /* lo mas largo que se lee de una cadena de golpe */

static void (*s_lee)(uint32_t, uint8_t *, uint32_t);
static uint32_t s_base;
static uint8_t  s_hay;
static uint16_t s_n_txt;
static uint8_t  s_n_paises;
static uint32_t s_n_frec, s_n_ent;
static uint32_t s_off_frec, s_off_ent, s_off_txt;
static uint32_t s_off_cad, s_tam;
static uint32_t s_valido;
static uint32_t s_suma_datos;
static int8_t   s_utc_h;

/*
 * Suma de 32 bits con rotacion (FNV-1a). No es un CRC y no pretende
 * serlo: aqui solo hay que notar que algo se ha copiado mal o que la zona
 * estaba a medio escribir, no defenderse de nadie. Lo que si tiene, y una
 * suma llana no, es que el ORDEN importa: dos bytes intercambiados dan
 * resultados distintos.
 */
static uint32_t suma(const uint8_t *b, uint32_t n, uint32_t v)
{
    uint32_t i;

    for (i = 0U; i < n; i++) {
        v = (v ^ (uint32_t)b[i]) * 16777619UL;
    }
    return v;
}

static uint16_t u16(const uint8_t *b) { return (uint16_t)(b[0] | ((uint16_t)b[1] << 8)); }
static uint32_t u32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

uint8_t emisoras_abre(uint32_t base,
                      void (*lee)(uint32_t addr, uint8_t *buf, uint32_t len))
{
    uint8_t c[CAB_LEN];

    s_hay = 0U;
    s_lee = lee;
    s_base = base;
    if (lee == 0) { return 0U; }

    lee(base, c, CAB_LEN);
    if (c[0] != 'E' || c[1] != 'M' || c[2] != 'I' || c[3] != 'S') { return 0U; }
    if (c[4] != 2U) { return 0U; }

    s_n_paises = c[5];
    s_n_txt    = u16(&c[6]);
    s_n_frec   = u32(&c[8]);
    s_n_ent    = u32(&c[12]);
    s_off_frec = u32(&c[16]);
    s_off_ent  = u32(&c[20]);
    s_off_txt  = u32(&c[24]);
    s_off_cad  = u32(&c[28]);
    s_tam      = u32(&c[32]);
    s_valido   = u32(&c[36]);
    s_utc_h      = (int8_t)c[40];
    s_suma_datos = u32(&c[52]);

    /*
     * LA SUMA DE LA CABECERA, ANTES QUE NADA. Todo lo de abajo son
     * comprobaciones de que los numeros encajan entre si, y eso deja
     * pasar los campos que no aparecen en ninguna cuenta. La suma no
     * deja pasar ninguno.
     */
    if (suma(c, 48U, 0x811C9DC5UL) != u32(&c[48])) { return 0U; }

    /*
     * QUE LOS NUMEROS DE LA CABECERA CUADREN ENTRE SI, y no solo que la
     * magia este. Una cabecera a medio escribir -una carga interrumpida a
     * la mitad- tiene la magia buena y los desplazamientos apuntando a
     * cualquier sitio, y a partir de ahi se leen bytes de la zona de otro.
     */
    if (s_n_frec == 0UL || s_n_ent == 0UL || s_n_txt == 0U) { return 0U; }
    if (s_n_paises == 0U || (uint16_t)s_n_paises > s_n_txt) { return 0U; }
    if (s_off_frec != CAB_LEN) { return 0U; }
    if (s_off_ent != s_off_frec + FREC_LEN * s_n_frec) { return 0U; }
    if (s_off_txt != s_off_ent + ENT_LEN * s_n_ent) { return 0U; }
    if (s_off_cad != s_off_txt + 2UL * s_n_txt) { return 0U; }
    if (s_tam <= s_off_cad || s_tam > 1024UL * 1024UL) { return 0U; }
    if (s_n_frec > 100000UL || s_n_ent > 200000UL) { return 0U; }

    s_hay = 1U;
    return 1U;
}

uint8_t emisoras_hay(void) { return s_hay; }

void emisoras_info(uint32_t *n_frec, uint32_t *n_ent, uint32_t *valido_hasta)
{
    if (n_frec != 0)       { *n_frec = s_hay ? s_n_frec : 0UL; }
    if (n_ent != 0)        { *n_ent = s_hay ? s_n_ent : 0UL; }
    if (valido_hasta != 0) { *valido_hasta = s_hay ? s_valido : 0UL; }
}

uint8_t emisoras_caducada(uint32_t aaaammdd)
{
    if (!s_hay || s_valido == 0UL || aaaammdd == 0UL) { return 0U; }
    return (uint8_t)(aaaammdd > s_valido);
}

void emisoras_texto(uint16_t i, char *dest, uint8_t len)
{
    uint8_t b[2];
    uint32_t off;
    uint32_t cad;
    uint32_t n;

    if (dest == 0 || len == 0U) { return; }
    dest[0] = '\0';
    if (!s_hay || i >= s_n_txt) { return; }

    s_lee(s_base + s_off_txt + 2UL * i, b, 2U);
    off = (uint32_t)u16(b);
    if (s_off_cad + off >= s_tam) { return; }
    cad = s_base + s_off_cad + off;
    /* Sin recortar al final del fichero, la ultima cadena se leeria
     * TXT_MAX bytes mas alla - dentro de la zona de la base de aviones. */
    n = s_tam - (s_off_cad + off);
    if (n > (uint32_t)len) { n = (uint32_t)len; }
    s_lee(cad, (uint8_t *)dest, n);
    dest[n - 1U] = '\0';
}

void emisoras_pais(uint8_t n, char *dest, uint8_t len)
{
    /*
     * LOS CODIGOS ITU SE GUARDAN AL FINAL DE LA TABLA DE TEXTOS, detras de
     * todas las cadenas normales, y la entrada solo lleva su numero dentro
     * de ese trozo. Cuantos son lo dice la cabecera; restarlo del total es
     * lo que convierte un numero en el otro. Sin ese paso sale un NOMBRE DE
     * EMISORA donde deberia ir "E" o "USA".
     */
    if (!s_hay || n >= s_n_paises) {
        if (dest != 0 && len != 0U) { dest[0] = '\0'; }
        return;
    }
    emisoras_texto((uint16_t)(s_n_txt - (uint16_t)s_n_paises + n), dest, len);
}

/* Busqueda binaria de la frecuencia mas cercana a `hz`. Devuelve el
 * indice del registro, o s_n_frec si la base esta vacia. */
static uint32_t frec_cerca(uint32_t hz, uint32_t *hz_out)
{
    uint32_t lo = 0UL, hi = s_n_frec;
    uint32_t mejor = 0UL, dmejor = 0xFFFFFFFFUL;
    uint8_t  b[FREC_LEN];
    uint32_t k;

    while (lo < hi) {
        uint32_t m = lo + (hi - lo) / 2UL;
        uint32_t v;

        s_lee(s_base + s_off_frec + FREC_LEN * m, b, FREC_LEN);
        v = u32(b);
        if (v < hz) { lo = m + 1UL; } else { hi = m; }
    }
    /*
     * `lo` es el primero que NO es menor que hz. La mas cercana es esa o
     * la de antes: hay que mirar las dos. Mirar solo una da el fallo
     * clasico de "sintonizo 9.419,8 y no encuentra los 9.420".
     */
    for (k = (lo > 0UL) ? (lo - 1UL) : 0UL; k <= lo && k < s_n_frec; k++) {
        uint32_t v, d;

        s_lee(s_base + s_off_frec + FREC_LEN * k, b, FREC_LEN);
        v = u32(b);
        d = (v > hz) ? (v - hz) : (hz - v);
        if (d < dmejor) { dmejor = d; mejor = k; if (hz_out != 0) { *hz_out = v; } }
    }
    return (dmejor == 0xFFFFFFFFUL) ? s_n_frec : mejor;
}

static uint8_t emitiendo(uint16_t ini, uint16_t fin, uint8_t dias,
                         uint16_t minuto, uint8_t dia)
{
    uint8_t dentro;

    /* Sin fecha fiable no se filtra por dia: ver emisoras_dia_semana(). */
    if (dia != EMISORAS_DIA_CUALQUIERA) {
        if ((dias & (uint8_t)(1U << (dia % 7U))) == 0U) { return 0U; }
    }
    if (fin > ini) {
        dentro = (uint8_t)(minuto >= ini && minuto < fin);
    } else {
        /* cruza la medianoche: 2300-0100 */
        dentro = (uint8_t)(minuto >= ini || minuto < fin);
    }
    return dentro;
}

uint8_t emisoras_busca(uint32_t hz, uint32_t tol_hz,
                       uint16_t minuto, uint8_t dia,
                       emisoras_r_t *out, uint8_t max)
{
    uint32_t i, hz_f = 0UL, pri, n;
    uint8_t  b[FREC_LEN];
    uint8_t  n_out = 0U, pasada;

    if (!s_hay || out == 0 || max == 0U) { return 0U; }

    i = frec_cerca(hz, &hz_f);
    if (i >= s_n_frec) { return 0U; }
    if (((hz_f > hz) ? (hz_f - hz) : (hz - hz_f)) > tol_hz) { return 0U; }

    s_lee(s_base + s_off_frec + FREC_LEN * i, b, FREC_LEN);
    pri = (uint32_t)u16(&b[4]);
    n   = (uint32_t)u16(&b[6]);
    /*
     * REVISION A FONDO DEL 08/10/2026: Y EL REGISTRO SE COMPRUEBA CONTRA
     * EL TAMAÑO DE VERDAD DE LA LISTA.
     *
     * pri y n salen del fichero y se usaban tal cual para leer entradas,
     * sin mirarlos contra s_n_ent. La cabecera si se comprueba al cargar
     * -los tres offsets tienen que cuadrar-, pero los registros de
     * frecuencia uno por uno no, y uno a medio escribir -el fichero pasa
     * por el USB, por el volumen FAT y por 52 bloques de borrado- hacia
     * leer MUY por encima del final de la lista de entradas y pintar en
     * pantalla lo que hubiera ahi como si fueran horarios y paises. La
     * resta se hace al reves a proposito -n > s_n_ent - pri- para que no
     * se de la vuelta cuando pri ya este fuera.
     */
    if (pri >= s_n_ent || n > s_n_ent - pri) { return 0U; }

    /*
     * DOS PASADAS: primero los que emiten AHORA y despues los demas. Asi
     * el primer resultado -que es el unico que cabe en la franja de
     * arriba- es siempre el que se esta oyendo, y no el que salga primero
     * por casualidad de como estaba ordenado el fichero.
     */
    for (pasada = 0U; pasada < 2U && n_out < max; pasada++) {
        uint32_t k;

        for (k = 0UL; k < n && n_out < max; k++) {
            uint8_t e[ENT_LEN];
            uint8_t ah;
            uint16_t ini, fin;

            s_lee(s_base + s_off_ent + ENT_LEN * (pri + k), e, ENT_LEN);
            ini = u16(&e[0]); fin = u16(&e[2]);
            ah = emitiendo(ini, fin, e[4], minuto, dia);
            if ((pasada == 0U) != (ah != 0U)) { continue; }

            out[n_out].ini    = ini;
            out[n_out].fin    = fin;
            out[n_out].dias   = e[4];
            out[n_out].pais   = e[5];
            out[n_out].nombre = u16(&e[6]);
            out[n_out].sitio  = u16(&e[8]);
            out[n_out].idioma = u16(&e[10]);
            out[n_out].ahora  = ah;
            n_out++;
        }
    }
    return n_out;
}

/*
 * LA SUMA DE LOS DATOS, que se mira UNA VEZ, al cargar.
 *
 * Son 206 kB por un bus de SPI a pedales: del orden de un segundo. Eso no
 * se puede hacer al arrancar ni menos al sintonizar, pero al terminar de
 * copiar el fichero SI - y es justo el momento en que puede estar mal,
 * porque acaba de pasar por el USB, por el volumen FAT y por 52 bloques
 * de borrado y grabacion.
 *
 * Se lee de 256 en 256 para no pedir pila que no hay.
 */
uint8_t emisoras_verifica(void)
{
    uint8_t buf[256];
    uint32_t p = CAB_LEN, v = 0x811C9DC5UL;

    if (!s_hay) { return 0U; }
    while (p < s_tam) {
        uint32_t n = s_tam - p;

        if (n > sizeof buf) { n = sizeof buf; }
        s_lee(s_base + p, buf, n);
        v = suma(buf, n, v);
        p += n;
    }
    return (uint8_t)(v == s_suma_datos);
}

/*
 * Cuanto hay que restarle al reloj de la radio -que marca hora local- para
 * tener UTC, que es en lo que estan escritas las dos listas. Ver el
 * comentario de la cabecera: viene en el fichero a proposito.
 */
int8_t emisoras_horas_utc(void) { return s_hay ? s_utc_h : 0; }

/*
 * De la hora local de la radio al minuto UTC y al dia de la semana UTC.
 *
 * Se hace aqui y no en main.c porque la vuelta por medianoche cambia el
 * DIA, y ese es el error que no se ve: a las 00:30 del lunes en España son
 * las 22:30 del DOMINGO en UTC, y una emisora que solo emite entre semana
 * no deberia salir.
 */
void emisoras_utc(uint16_t min_local, uint8_t dia_local,
                  uint16_t *min_utc, uint8_t *dia_utc)
{
    int32_t m = (int32_t)min_local - (int32_t)s_utc_h * 60;
    int32_t d = (int32_t)dia_local;

    if (!s_hay) { m = (int32_t)min_local; }
    while (m < 0)      { m += 1440; d -= 1; }
    while (m >= 1440)  { m -= 1440; d += 1; }
    while (d < 0)      { d += 7; }
    while (d > 6)      { d -= 7; }
    if (min_utc != 0) { *min_utc = (uint16_t)m; }
    if (dia_utc != 0) { *dia_utc = (uint8_t)d; }
}

/*
 * EL DIA DE LA SEMANA A PARTIR DE LA FECHA (Sakamoto). 0 = lunes.
 *
 * Hace falta porque unas cuantas entradas solo valen de lunes a viernes o
 * los domingos, y sin el dia habria que enseñarlas todas siempre. La radio
 * tiene la fecha en el RTC; si el cristal no arranco, quien llama pasa
 * EMISORAS_DIA_CUALQUIERA y entonces no se filtra por dia - que es lo
 * honrado: mejor enseñar de mas que enseñar la equivocada.
 *
 * La tabla es la de siempre; lo unico que no se deduce de ella es que hay
 * que restarle uno a enero y febrero, porque el dia de mas de los
 * bisiestos va al final del año que Sakamoto cuenta.
 */
uint8_t emisoras_dia_semana(uint16_t anno, uint8_t mes, uint8_t dia)
{
    static const uint8_t k_t[12] = { 0U, 3U, 2U, 5U, 0U, 3U,
                                     5U, 1U, 4U, 6U, 2U, 4U };
    uint32_t a = anno;
    uint32_t d;

    if (mes < 1U || mes > 12U || dia < 1U || dia > 31U) {
        return EMISORAS_DIA_CUALQUIERA;
    }
    /*
     * REVISION A FONDO DEL 08/10/2026: Y EL AÑO TIENE QUE SER CREIBLE.
     *
     * El "a -= 1UL" de abajo es sobre un uint32_t, asi que con anno == 0
     * -que es lo que devuelve el RTC cuando el cristal no ha arrancado,
     * el caso que la cabecera de aqui arriba dice tener en cuenta- se
     * daba la vuelta a 4294967295 y de ahi salia un dia de la semana
     * cualquiera, con cara de bueno. Y un dia equivocado FILTRA: las
     * entradas de lunes a viernes desaparecen, y eso es peor que no
     * filtrar. Con el año fuera de rango se contesta lo mismo que con el
     * mes imposible, que es lo honrado.
     */
    if (anno < 1900U) { return EMISORAS_DIA_CUALQUIERA; }
    if (mes < 3U) { a -= 1UL; }
    d = (a + a / 4UL - a / 100UL + a / 400UL
         + (uint32_t)k_t[mes - 1U] + (uint32_t)dia) % 7UL;   /* 0 = domingo */
    return (uint8_t)((d + 6UL) % 7UL);                        /* 0 = lunes */
}
