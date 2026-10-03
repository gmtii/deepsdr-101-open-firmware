/*
 * Almacen de imagenes en la flash externa. Ver img_store.h para el porque.
 *
 * COMO ESTA PUESTO EN EL CHIP
 * ---------------------------
 *
 *   ...datos que ya habia, intocables...
 *   +----------------------------+  <- suelo (se mide una vez y se apunta)
 *   | imagen 1: cabecera + datos |
 *   +----------------------------+
 *   | imagen 2: cabecera + datos |
 *   +----------------------------+  <- puntero de escritura
 *   | borrado                    |
 *   +----------------------------+
 *   | CABECERA DE ZONA           |  <- el ULTIMO bloque del chip
 *   +----------------------------+  <- tope
 *
 * La cabecera de zona va en el ultimo bloque porque es la unica direccion
 * que se sabe sin haber leido nada. Se escribe UNA vez, la primera, y no se
 * vuelve a tocar: guarda donde empieza la zona, que es el dato que no se
 * puede volver a deducir cuando ya hay imagenes encima.
 *
 * Todo lo demas -cuantas imagenes hay, donde acaba la ultima- se deduce
 * recorriendo hacia arriba desde el suelo, que es un recorrido que no
 * necesita que nada este apuntado en ningun sitio. Por eso un corte de
 * corriente a mitad de una imagen no rompe el almacen: la imagen a medias
 * no lleva la marca de cerrada, se salta, y las de antes siguen ahi.
 */
#include "img_store.h"
#include "idioma.h"

#include <string.h>

static const uint8_t k_magia_zona[8] = { 'D','S','D','R','Z','O','N','A' };
static const uint8_t k_magia_img[8]  = { 'D','S','D','R','I','M','G','1' };

/* La cabecera de zona: 8 de magia, version, tres de relleno, suelo, tope y
 * una suma. Corta a proposito: cuanto menos guarde, menos puede mentir. */
#define ZONA_CAB 24U

/* Estado de una imagen. Solo se puede pasar de 0xFF a otra cosa -grabar
 * baja bits, nunca los sube-, asi que "abierta" es el valor de fabrica y
 * cerrarla es bajar un bit. No hace falta borrar nada para cerrar. */
#define IMG_ABIERTA 0xFFU
#define IMG_CERRADA 0xFEU

#define IMGS_MAX 32U   /* imagenes que se indexan; de sobra para medio mega */

static uint8_t s_veredicto = IMGS_NO_MEDIDO;
static const imgs_medio_t *s_m;
static uint32_t s_suelo, s_tope;      /* la zona; s_tope EXCLUYE la cabecera */
static uint32_t s_ptr;                /* siguiente byte libre */
static uint32_t s_dir[IMGS_MAX];      /* donde esta la cabecera de cada una */
static uint8_t  s_n;

/* Escritura en curso. */
static uint8_t  s_abierta;
static uint32_t s_esc_cab, s_esc_ptr, s_esc_fin;
static uint32_t s_esc_suma;
static uint32_t s_esc_verificado;     /* primer bloque aun sin comprobar */

/* ---------------------------------------------------------------- utiles */
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void pon32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t le16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}
static void pon16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static uint32_t suma(const uint8_t *p, uint32_t n)
{
    uint32_t s = 0x1234U, i;
    for (i = 0U; i < n; i++) { s = (s * 31U) + p[i]; }
    return s;
}
static uint32_t arriba(uint32_t v)   /* redondea al bloque de arriba */
{
    return (v + IMGS_BLOQUE - 1U) & ~(uint32_t)(IMGS_BLOQUE - 1U);
}

/*
 * BUSCAR NO ES AUTORIZAR - 24/09/2026.
 *
 * Hay dos preguntas distintas sobre un bloque y hasta hoy las contestaba
 * la misma funcion, leyendo los 4096 bytes:
 *
 *   "¿donde empieza la zona?"      es una BUSQUEDA
 *   "¿puedo escribir en este?"     es una AUTORIZACION
 *
 * La segunda tiene que leerlo entero: de ella depende no destruir lo que
 * haya. La primera no, y hacerlo salia carisimo - el recorrido hacia
 * abajo son 128 bloques, o sea medio megabyte por un bus bit a bit, y
 * corria dentro del toque que abre la pantalla de Informacion. La radio
 * se quedaba clavada al entrar.
 *
 * Asi que la busqueda MUESTREA (principio y final de cada bloque) y la
 * autorizacion sigue leyendo entero. Y esto no afloja la garantia: si el
 * muestreo se equivoca e incluye un bloque con datos, al ir a escribir en
 * el la autorizacion lo lee entero, dice que no, y lo unico que se pierde
 * es sitio. El invariante de img_store.h se cumple igual, porque quien lo
 * hace cumplir es la de escribir.
 */
static uint8_t bloque_borrado_rapido(uint32_t addr)
{
    uint8_t buf[64];
    uint32_t i;

    s_m->lee(addr, buf, (uint32_t)sizeof buf);
    for (i = 0U; i < sizeof buf; i++) {
        if (buf[i] != 0xFFU) { return 0U; }
    }
    s_m->lee(addr + IMGS_BLOQUE - (uint32_t)sizeof buf, buf, (uint32_t)sizeof buf);
    for (i = 0U; i < sizeof buf; i++) {
        if (buf[i] != 0xFFU) { return 0U; }
    }
    return 1U;
}

/*
 * Un bloque ENTERO a 0xFF. Los 4096 bytes, no una muestra: esta funcion es
 * la que AUTORIZA a destruir lo que haya en un sitio, y un muestreo puede
 * decir que esta vacio un bloque que no lo esta. Ver el invariante de
 * img_store.h y el comentario de aqui arriba.
 */
static uint8_t bloque_borrado(uint32_t addr)
{
    uint8_t buf[64];
    uint32_t o, i;

    for (o = 0U; o < IMGS_BLOQUE; o += sizeof buf) {
        s_m->lee(addr + o, buf, (uint32_t)sizeof buf);
        for (i = 0U; i < sizeof buf; i++) {
            if (buf[i] != 0xFFU) { return 0U; }
        }
    }
    return 1U;
}

/* --------------------------------------------------------- las cabeceras */
/*
 * Cabecera de imagen, 32 bytes:
 *   0  magia[8]
 *   8  tipo
 *   9  bpp
 *  10  ancho (2)
 *  12  alto  (2)
 *  14  relleno (2)
 *  16  bytes de datos (4)
 *  20  ms del dia (4)
 *  24  suma de los datos (4)   <- se graba AL CERRAR
 *  28  suma de los bytes 0..23 (2)
 *  30  relleno (1)
 *  31  estado                  <- se graba AL CERRAR
 *
 * LOS DOS CAMPOS QUE SE GRABAN AL CERRAR ESTAN FUERA DE LA SUMA, y no por
 * gusto: en una flash NOR grabar solo puede BAJAR bits de 1 a 0. La
 * primera version escribia la cabecera entera al abrir -con la suma de
 * los datos a cero- y la volvia a escribir entera al cerrar con la suma de
 * verdad: eso es SUBIR bits, que el chip simplemente no hace. El banco lo
 * canto a la primera ("bits subidos: 6") y ademas ninguna imagen se leia,
 * porque la suma de la cabecera tampoco cuadraba.
 *
 * Asi que al abrir se graban los bytes 0..29 y se dejan a 0xFF los dos
 * campos de cerrar; al cerrar se graban SOLO esos dos, cada uno saliendo
 * de 0xFF, que admite cualquier valor encima.
 */
static uint8_t cab_valida(const uint8_t *c)
{
    if (memcmp(c, k_magia_img, 8) != 0) { return 0U; }
    return (uint8_t)(le16(&c[28]) == (suma(c, 24U) & 0xFFFFU));
}

static uint32_t cab_ocupa(const uint8_t *c)
{
    return arriba(IMGS_CAB + le32(&c[16]));
}

/* ------------------------------------------------------------- recorrido */
static void recorre(void)
{
    uint8_t c[IMGS_CAB];

    s_n = 0U;
    s_ptr = s_suelo;

    while (s_ptr + IMGS_BLOQUE <= s_tope) {
        s_m->lee(s_ptr, c, (uint32_t)sizeof c);
        if (!cab_valida(c)) { break; }
        {
            uint32_t ocupa = cab_ocupa(c);
            if (ocupa == 0U || s_ptr + ocupa > s_tope) { break; }
            /* Las cerradas se cuentan; las que se quedaron a medias se
             * saltan pero su sitio sigue gastado - ver img_store.h. */
            if (c[31] == IMG_CERRADA && s_n < IMGS_MAX) {
                s_dir[s_n] = s_ptr;
                s_n++;
            }
            s_ptr += ocupa;
        }
    }
}

/* --------------------------------------------------------------- arranque */
uint32_t imgs_init(const imgs_medio_t *medio, uint32_t suelo, uint32_t tope)
{
    uint8_t z[ZONA_CAB];

    s_m = medio;
    s_suelo = 0UL; s_tope = 0UL; s_ptr = 0UL; s_n = 0U;
    s_abierta = 0U;

    s_veredicto = IMGS_NO_MEDIDO;

    if (medio == 0 || medio->lee == 0 || medio->borra == 0 || medio->graba == 0) {
        return 0UL;
    }
    /* tope == 0 es lo que devuelve la medida por alias cuando no vale, y
     * hay que distinguirlo de "el chip es pequeño". */
    if (tope == 0UL) { return 0UL; }
    /*
     * El suelo absoluto. Con el chip de fabrica esto solo es 0, que es la
     * respuesta correcta: ver el comentario de IMGS_SUELO_MIN.
     */
    if (suelo < IMGS_SUELO_MIN) { suelo = IMGS_SUELO_MIN; }

    /* Hace falta sitio para la cabecera de zona y al menos un bloque mas. */
    if (tope < suelo + 2U * IMGS_BLOQUE) {
        s_veredicto = IMGS_CHIP_CHICO;
        return 0UL;
    }
    tope &= ~(uint32_t)(IMGS_BLOQUE - 1U);
    suelo = arriba(suelo);

    {
        uint32_t zdir = tope - IMGS_BLOQUE;

        s_m->lee(zdir, z, (uint32_t)sizeof z);

        if (memcmp(z, k_magia_zona, 8) == 0 &&
            le32(&z[20]) == suma(z, 20U)) {
            /* Ya habia zona: se usa la que dice, recortada a lo que hoy
             * cabe. Si el chip fuese otro y saliera fuera, no se usa. */
            uint32_t s = le32(&z[12]);
            uint32_t t = le32(&z[16]);
            if (s < t && t <= tope && s >= suelo) {
                s_suelo = s;
                s_tope  = t;
                s_veredicto = IMGS_OK;
                recorre();
                return s_tope - s_suelo;
            }
            /* Hay cabecera de zona pero apunta fuera de este chip: o se
             * cambio el chip por uno menor, o no es nuestra. No se toca. */
            s_veredicto = IMGS_ZONA_AJENA;
            return 0UL;
        }

        if (!bloque_borrado(zdir)) {
            /* El ultimo bloque del chip es de otro. No se toca nada.
             * Este SI se lee entero: es el unico en el que se va a
             * escribir ahora mismo. */
            s_veredicto = IMGS_ZONA_AJENA;
            return 0UL;
        }

        /* Zona nueva: se mide la cola de bloques ENTEROS borrados bajando
         * desde debajo de la cabecera. */
        {
            uint32_t b = zdir;
            uint32_t tope_pasos = 4096U;   /* red de seguridad: nunca sin fin */
            while (b > suelo && tope_pasos != 0U) {
                if (!bloque_borrado_rapido(b - IMGS_BLOQUE)) { break; }
                b -= IMGS_BLOQUE;
                tope_pasos--;
            }
            if (zdir - b < 4U * IMGS_BLOQUE) {
                /* Menos de 16 kB libres: no merece la pena escribir una
                 * cabecera de zona que luego no sirva para nada. */
                s_veredicto = IMGS_POCO_SITIO;
                return 0UL;
            }
            s_suelo = b;
            s_tope  = zdir;
        }

        memset(z, 0xFF, sizeof z);
        memcpy(z, k_magia_zona, 8);
        z[8] = 1U;                      /* version */
        pon32(&z[12], s_suelo);
        pon32(&z[16], s_tope);
        pon32(&z[20], suma(z, 20U));
        s_m->graba(tope - IMGS_BLOQUE, z, (uint32_t)sizeof z);
    }

    s_veredicto = IMGS_OK;
    recorre();
    return s_tope - s_suelo;
}

uint8_t imgs_veredicto(void) { return s_veredicto; }
void    imgs_fuerza_veredicto(uint8_t v) { s_veredicto = v; }

const char *imgs_veredicto_texto(void)
{
    switch (s_veredicto) {
    case IMGS_OK:          return tr("disponible", "available");
    case IMGS_CHIP_CHICO:  return tr("no: chip de fábrica", "no: stock chip");
    case IMGS_ZONA_AJENA:  return tr("no: final ocupado", "no: end in use");
    case IMGS_POCO_SITIO:  return tr("no: casi sin libre", "no: almost full");
    default:               return tr("no: chip sin medir", "no: chip unmeasured");
    }
}

uint32_t imgs_total(void) { return (s_tope > s_suelo) ? (s_tope - s_suelo) : 0UL; }
uint32_t imgs_libre(void) { return (s_tope > s_ptr) ? (s_tope - s_ptr) : 0UL; }
uint8_t  imgs_cuantas(void) { return s_n; }
uint8_t  imgs_abierta(void) { return s_abierta; }

/* --------------------------------------------------------------- escribir */
uint8_t imgs_abre(uint8_t tipo, uint16_t ancho, uint16_t alto, uint8_t bpp,
                  uint32_t bytes, uint32_t fecha_ms)
{
    uint8_t c[IMGS_CAB];
    uint32_t ocupa;

    if (s_m == 0 || s_abierta || s_tope <= s_suelo) { return 0U; }
    if (bytes == 0UL) { return 0U; }
    /*
     * Lleno tambien es quedarse sin indice. Sin esto, la imagen 33 se
     * escribia en la flash y no aparecia en ningun sitio: gastaba el sitio
     * y no se podia ver, que es la peor de las dos formas de estar lleno.
     * Ahora "no cabe" tiene una sola respuesta.
     */
    if (s_n >= IMGS_MAX) { return 0U; }

    ocupa = arriba(IMGS_CAB + bytes);
    if (s_ptr + ocupa > s_tope) { return 0U; }

    /* El bloque donde va la cabecera tiene que estar ENTERO borrado antes
     * de escribir un solo byte. Ver el invariante. */
    if (!bloque_borrado(s_ptr)) { return 0U; }

    memset(c, 0xFF, sizeof c);
    memcpy(c, k_magia_img, 8);
    c[8] = tipo;
    c[9] = bpp;
    pon16(&c[10], ancho);
    pon16(&c[12], alto);
    pon32(&c[16], bytes);
    pon32(&c[20], fecha_ms);
    /* [24..27] y [31] se quedan a 0xFF: son los dos campos de cerrar. */
    pon16(&c[28], suma(c, 24U) & 0xFFFFU);

    /* Solo los 30 primeros bytes: los dos que faltan van al cerrar. */
    s_m->graba(s_ptr, c, 30U);

    s_esc_cab = s_ptr;
    s_esc_ptr = s_ptr + IMGS_CAB;
    s_esc_fin = s_ptr + IMGS_CAB + bytes;
    s_esc_suma = 0x1234U;
    s_esc_verificado = s_ptr + IMGS_BLOQUE;   /* el primero ya se comprobo */
    s_abierta = 1U;
    return 1U;
}

uint8_t imgs_escribe(const uint8_t *datos, uint32_t n)
{
    if (!s_abierta || datos == 0) { return 0U; }
    if (s_esc_ptr + n > s_esc_fin) { return 0U; }

    while (n > 0U) {
        uint32_t hueco, trozo, i;

        /* Cada bloque nuevo se comprueba JUSTO antes de entrar en el, y no
         * todos de golpe al abrir: comprobar medio megabyte por un bus a
         * mano son segundos, y repartido no se nota. La garantia es la
         * misma - en ningun bloque se graba sin haberlo leido entero. */
        while (s_esc_ptr >= s_esc_verificado) {
            if (!bloque_borrado(s_esc_verificado)) {
                imgs_cancela();
                return 0U;
            }
            s_esc_verificado += IMGS_BLOQUE;
        }

        hueco = IMGS_PAGINA - (s_esc_ptr & (IMGS_PAGINA - 1U));
        trozo = (n < hueco) ? n : hueco;
        /* Y que el trozo no se salga del bloque ya comprobado. */
        if (s_esc_ptr + trozo > s_esc_verificado) {
            trozo = s_esc_verificado - s_esc_ptr;
        }

        s_m->graba(s_esc_ptr, datos, trozo);
        for (i = 0U; i < trozo; i++) { s_esc_suma = (s_esc_suma * 31U) + datos[i]; }

        s_esc_ptr += trozo;
        datos += trozo;
        n -= trozo;
    }
    return 1U;
}

uint8_t imgs_cierra(void)
{
    uint8_t c[IMGS_CAB];
    uint32_t ocupa;

    if (!s_abierta) { return 0U; }

    /* Una imagen que no se lleno entera no se cierra: cerrarla seria decir
     * que esta completa cuando le faltan lineas. */
    if (s_esc_ptr != s_esc_fin) { imgs_cancela(); return 0U; }

    /*
     * Se graban SOLO los dos campos de cerrar, cada uno saliendo de 0xFF.
     * Nada mas de la cabecera se vuelve a tocar: ver el comentario del
     * formato mas arriba y el fallo que costo escribirlo.
     */
    pon32(c, s_esc_suma);
    s_m->graba(s_esc_cab + 24U, c, 4U);
    c[0] = IMG_CERRADA;
    s_m->graba(s_esc_cab + 31U, c, 1U);

    ocupa = arriba(IMGS_CAB + (s_esc_fin - s_esc_cab - IMGS_CAB));
    if (s_n < IMGS_MAX) { s_dir[s_n] = s_esc_cab; s_n++; }
    s_ptr = s_esc_cab + ocupa;
    s_abierta = 0U;
    return 1U;
}

void imgs_cancela(void)
{
    if (!s_abierta) { return; }
    /* El sitio se da por gastado hasta el proximo borrado general: la
     * cabecera esta escrita y no se puede "desescribir" sin borrar el
     * bloque, y borrarlo aqui costaria decenas de ms en el peor momento. */
    s_ptr = s_esc_cab + arriba(IMGS_CAB + (s_esc_fin - s_esc_cab - IMGS_CAB));
    s_abierta = 0U;
}

/* ------------------------------------------------------------------- leer */
uint8_t imgs_lee_info(uint8_t i, imgs_info_t *out)
{
    uint8_t c[IMGS_CAB];

    if (i >= s_n || out == 0) { return 0U; }
    s_m->lee(s_dir[i], c, (uint32_t)sizeof c);
    if (!cab_valida(c)) { return 0U; }

    out->tipo     = c[8];
    out->bpp      = c[9];
    out->ancho    = (uint16_t)le16(&c[10]);
    out->alto     = (uint16_t)le16(&c[12]);
    out->bytes    = le32(&c[16]);
    out->fecha_ms = le32(&c[20]);
    out->addr     = s_dir[i];
    return 1U;
}

uint8_t imgs_lee_datos(uint8_t i, uint32_t desde, uint8_t *buf, uint32_t n)
{
    imgs_info_t in;

    if (!imgs_lee_info(i, &in) || buf == 0) { return 0U; }
    if (desde + n > in.bytes) { return 0U; }
    s_m->lee(in.addr + IMGS_CAB + desde, buf, n);
    return 1U;
}

uint8_t imgs_verifica(uint8_t i)
{
    uint8_t c[IMGS_CAB];
    uint8_t buf[128];
    uint32_t o, acc = 0x1234U;
    imgs_info_t in;

    if (!imgs_lee_info(i, &in)) { return 0U; }
    s_m->lee(s_dir[i], c, (uint32_t)sizeof c);
    if (c[31] != IMG_CERRADA) { return 0U; }

    for (o = 0U; o < in.bytes; o += (uint32_t)sizeof buf) {
        uint32_t n = in.bytes - o;
        uint32_t k;
        if (n > (uint32_t)sizeof buf) { n = (uint32_t)sizeof buf; }
        s_m->lee(in.addr + IMGS_CAB + o, buf, n);
        for (k = 0U; k < n; k++) { acc = (acc * 31U) + buf[k]; }
    }
    return (uint8_t)(acc == le32(&c[24]));
}

/* --- la misma verificacion, a pasos --- */
static uint8_t  s_vf_i;
static uint32_t s_vf_off, s_vf_bytes, s_vf_acc;
static uint8_t  s_vf_vivo;

void imgs_verif_inicia(uint8_t i)
{
    imgs_info_t in;

    s_vf_vivo = 0U;
    s_vf_off = 0UL; s_vf_bytes = 0UL; s_vf_acc = 0x1234U;
    if (!imgs_lee_info(i, &in)) { return; }
    s_vf_i = i;
    s_vf_bytes = in.bytes;
    s_vf_vivo = 1U;
}

int8_t imgs_verif_paso(void)
{
    uint8_t buf[256];
    imgs_info_t in;
    uint32_t n, k;

    if (!s_vf_vivo) { return -1; }
    if (!imgs_lee_info(s_vf_i, &in)) { s_vf_vivo = 0U; return -1; }

    if (s_vf_off >= s_vf_bytes) {
        uint8_t c[IMGS_CAB];
        s_m->lee(s_dir[s_vf_i], c, (uint32_t)sizeof c);
        s_vf_vivo = 0U;
        if (c[31] != IMG_CERRADA) { return -1; }
        return (int8_t)((s_vf_acc == le32(&c[24])) ? 0 : -1);
    }

    n = s_vf_bytes - s_vf_off;
    if (n > (uint32_t)sizeof buf) { n = (uint32_t)sizeof buf; }
    s_m->lee(in.addr + IMGS_CAB + s_vf_off, buf, n);
    for (k = 0U; k < n; k++) { s_vf_acc = (s_vf_acc * 31U) + buf[k]; }
    s_vf_off += n;
    return 1;
}

uint32_t imgs_verif_hechos(void) { return s_vf_off; }
uint32_t imgs_verif_total(void)  { return s_vf_bytes; }

void imgs_borra_todo(void)
{
    uint32_t b;

    if (s_m == 0 || s_tope <= s_suelo || s_abierta) { return; }

    for (b = s_suelo; b < s_ptr; b += IMGS_BLOQUE) {
        /*
         * Solo se borra lo que sea NUESTRO o ya este borrado. Un bloque con
         * datos que no empiecen por nuestra magia puede ser un bloque de
         * datos de una imagen nuestra... o algo de otro, si las medidas
         * fallaron. Distinguirlos aqui de verdad cuesta poco: los bloques
         * de una imagen estan entre su cabecera y su final, y el recorrido
         * de arriba ya sabe donde acaba cada una.
         */
        uint8_t c[IMGS_CAB];
        s_m->lee(b, c, (uint32_t)sizeof c);
        if (cab_valida(c)) {
            uint32_t ocupa = cab_ocupa(c);
            uint32_t k;
            if (ocupa == 0U || b + ocupa > s_tope) { break; }
            for (k = 0U; k < ocupa; k += IMGS_BLOQUE) { s_m->borra(b + k); }
            b += ocupa - IMGS_BLOQUE;
        } else if (bloque_borrado(b)) {
            /* nada que hacer */
        } else {
            /* Ni nuestro ni vacio: aqui se para. */
            break;
        }
    }
    recorre();
}
