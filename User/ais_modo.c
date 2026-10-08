#include "ais_modo.h"
#include "ais_rx.h"
#include "ais_msg.h"
#include "hfdl_ram.h"
#include "waterfall.h"
#include "ft8_shared_ram.h"
#include <string.h>

static uint8_t s_on;
static uint8_t s_canal;

/*
 * LA TABLA DE BARCOS.
 *
 * Dieciseis entradas: dos mas de las catorce que caben en el panel, para
 * que un barco que se sale de la pantalla siga vivo por si vuelve a hablar.
 * Son 16 * 48 = 768 bytes. Empezo en 24 y bajo a 16 porque el enlazador
 * desbordo la SRAM: aqui cada entrada que sobra cuesta de verdad.
 *
 * CUANDO SE LLENA se tira la MAS VIEJA, no la que menos ha hablado: un
 * barco que dijo su nombre una vez y se fue sigue siendo un dato bueno
 * durante un rato, y uno que lleva media hora sin aparecer ya no esta ahi.
 */
typedef struct {
    uint32_t mmsi;
    char     nombre[21];
    int32_t  lat_e4, lon_e4;
    uint16_t sog_d, cog_d;
    uint16_t n;            /* mensajes suyos que han llegado */
    uint32_t visto;        /* el numero de mensaje en que se le oyo */
    uint8_t  hay_pos;
    uint8_t  hay_vel;
} barco_t;

/*
 * LA TABLA Y LOS RENGLONES VIVEN EN LA MEMORIA DE LA CASCADA. 27/09/2026.
 *
 * No por gusto: en SRAM propia no caben. La tabla son 768 bytes y los
 * catorce renglones montados otros 1.008, y esta radio tenia 3.300 libres
 * de 196.608 cuando AIS llego. El enlazador desbordo por 996 bytes y la
 * cuenta no salia recortando: con 14 renglones -los que se ven- y 16
 * barcos -dos mas de los que se ven- ya no sobra nada que quitar.
 *
 * Y el sitio estaba delante: AIS es exclusivo con FT8, con HFDL y con WSPR
 * -los cuatro se llevan el panel entero, no se pueden ver dos a la vez- y
 * esos tres ya viven ahi (ver ft8_shared_ram.h). Lo unico que cambia es que
 * ahora son CUATRO los que se reparten esos 54 kB, y AIS es con diferencia
 * el que menos pide: 1.776 de 54.432.
 *
 * LO QUE CUESTA: mientras AIS esta puesto, la cascada no pinta. Da igual en
 * la practica - en AIS el panel de barcos ocupa la pantalla entera, igual
 * que en los otros tres, asi que la cascada no se veria de todos modos-.
 */
typedef struct {
    barco_t bar[AIS_MODO_BARCOS];
    char    lin[AIS_MODO_LINEAS][AIS_MODO_LARGO];
} ais_ram_t;

_Static_assert(sizeof(ais_ram_t) <= sizeof(ft8_shared_ram_t),
               "la tabla de barcos de AIS no cabe en la memoria de la cascada");

static ais_ram_t *s_ram;       /* NULL = el modo no esta puesto */
static uint8_t  s_nbar;
static uint32_t s_reloj;       /* cuenta de mensajes, para saber quien es viejo */
static uint32_t s_tramas;
static uint8_t  s_lin_vale;    /* 0 = hay que volver a montarlas */

#define s_bar (s_ram->bar)
#define s_lin (s_ram->lin)

uint8_t  ais_modo_activo(void) { return s_on; }
uint32_t ais_modo_tramas(void) { return s_tramas; }
uint8_t  ais_modo_barcos(void) { return s_nbar; }
uint8_t  ais_modo_canal(void)  { return s_canal; }

uint32_t ais_modo_vistas(void)
{
    ais_rx_info_t i;

    ais_rx_info(&i);
    return i.tramas;
}

uint8_t ais_modo_senal(void)
{
    ais_rx_info_t i;

    ais_rx_info(&i);
    return i.nivel;
}

void ais_modo_canal_pon(uint8_t c) { s_canal = (uint8_t)(c & 1U); }

void ais_modo_borra(void)
{
    s_nbar = 0U;
    s_tramas = 0U;
    s_reloj = 0U;
    s_lin_vale = 0U;
    ais_rx_cuentas_cero();
}

uint8_t ais_modo_start(void)
{
    uint8_t *ram;
    uint32_t cap = 0UL;

    if (s_on) { return 1U; }

    /*
     * Igual que WSPR: si otro modo tiene la memoria cogida, aqui no se
     * arranca. En la practica no puede pasar -main.c para los otros tres
     * antes de llamar- pero la comprobacion es lo que impide que un dia un
     * reordenamiento del conmutador de modo deje a dos escribiendo en los
     * mismos bytes. Ese fallo ya se cometio una vez entre FT8 y HFDL.
     */
    if (waterfall_prestada()) { return 0U; }

    hfdl_ram_coge();
    ram = hfdl_ram_todo(&cap);
    if ((ram == (uint8_t *)0) || (cap < (uint32_t)sizeof(ais_ram_t))) {
        hfdl_ram_suelta();
        return 0U;
    }
    s_ram = (ais_ram_t *)(void *)ram;
    memset(s_ram, 0, sizeof *s_ram);

    ais_modo_borra();
    /*
     * 96 kHz: la tasa a la que la cadena entrega el audio del discriminador
     * (ver SDR_RX_BLOCK_SAMPLES en sdr_rx.h). A esa tasa salen diez muestras
     * por bit EXACTAS, que es de donde viene la sencillez del lazo de reloj.
     */
    ais_rx_start(96000.0f);
    s_on = 1U;
    return 1U;
}

void ais_modo_stop(void)
{
    if (!s_on) { return; }
    ais_rx_stop();
    s_on = 0U;
    s_ram = (ais_ram_t *)0;
    s_nbar = 0U;
    hfdl_ram_suelta();
}

void ais_modo_mete(const float *disc, uint16_t n)
{
    if (!s_on) { return; }
    ais_rx_mete(disc, (uint32_t)n);
}

/* ------------------------------------------------------------ la tabla */
static barco_t *busca(uint32_t mmsi)
{
    uint8_t i, viejo = 0U;

    if (s_ram == (ais_ram_t *)0) { return (barco_t *)0; }

    for (i = 0U; i < s_nbar; i++) {
        if (s_bar[i].mmsi == mmsi) { return &s_bar[i]; }
    }

    if (s_nbar < (uint8_t)AIS_MODO_BARCOS) {
        barco_t *b = &s_bar[s_nbar];

        s_nbar++;
        memset(b, 0, sizeof *b);
        b->mmsi = mmsi;
        return b;
    }

    /* Llena: fuera el mas viejo. */
    for (i = 1U; i < s_nbar; i++) {
        if (s_bar[i].visto < s_bar[viejo].visto) { viejo = i; }
    }
    memset(&s_bar[viejo], 0, sizeof s_bar[viejo]);
    s_bar[viejo].mmsi = mmsi;
    return &s_bar[viejo];
}

static void apunta(const ais_msg_t *m)
{
    barco_t *b = busca(m->mmsi);

    if (b == (barco_t *)0) { return; }

    s_reloj++;
    b->visto = s_reloj;
    if (b->n < 0xFFFFU) { b->n++; }

    if (m->hay_pos) {
        b->lat_e4 = m->lat_e4;
        b->lon_e4 = m->lon_e4;
        b->hay_pos = 1U;
    }
    if (m->hay_vel) {
        b->sog_d = m->sog_d;
        b->cog_d = m->cog_d;
        b->hay_vel = 1U;
    }
    /*
     * EL NOMBRE NO SE PISA CON UNO VACIO. Los mensajes de posicion no lo
     * traen, y si se copiara siempre, cada informe de posicion borraria el
     * nombre que costo seis minutos de espera.
     */
    if (m->hay_nombre) {
        memcpy(b->nombre, m->nombre, sizeof b->nombre);
    }

    s_lin_vale = 0U;
}

void ais_modo_poll(void)
{
    uint8_t  bits[AIS_BYTES_MAX];
    uint16_t nbits;

    if (!s_on) { return; }

    /* Hasta cuatro por vuelta: es lo que cabe en el anillo, asi que en una
     * vuelta se vacia entero pase lo que pase. */
    {
        uint8_t k;

        for (k = 0U; k < 4U; k++) {
            ais_msg_t m;

            if (!ais_rx_saca(bits, (uint16_t)sizeof bits, &nbits)) { break; }
            s_tramas++;
            if (ais_msg_saca(bits, nbits, &m)) { apunta(&m); }
        }
    }
}

/* ---------------------------------------------------------- los renglones */
static uint8_t pon_u(char *d, uint32_t v)
{
    char t[12];
    uint8_t n = 0U, i;

    do { t[n++] = (char)('0' + (v % 10U)); v /= 10U; } while (v != 0U);
    for (i = 0U; i < n; i++) { d[i] = t[n - 1U - i]; }
    return n;
}

/*
 * GRADOS CON TRES DECIMALES Y LETRA, no con signo.
 *
 * "43,371 N" se lee; "43,3712" con un menos delante hay que pensarlo. Y
 * tres decimales son cien metros: para decir donde esta un barco de
 * trescientos metros de largo, cuatro decimales son precision fingida.
 */
static uint8_t pon_grados(char *d, int32_t e4, char pos, char neg)
{
    uint8_t i = 0U;
    uint32_t v;

    if (e4 < 0) { v = (uint32_t)(-e4); } else { v = (uint32_t)e4; }
    i += pon_u(&d[i], v / 10000U);
    d[i++] = ',';
    d[i++] = (char)('0' + ((v / 1000U) % 10U));
    d[i++] = (char)('0' + ((v / 100U) % 10U));
    d[i++] = (char)('0' + ((v / 10U) % 10U));
    d[i++] = (e4 < 0) ? neg : pos;
    return i;
}

/* Cuantos renglones hay de verdad: los que caben en la pantalla. */
static uint8_t cuantas(void)
{
    return (s_nbar > (uint8_t)AIS_MODO_LINEAS) ? (uint8_t)AIS_MODO_LINEAS
                                               : s_nbar;
}

static void monta(void)
{
    uint8_t k, n = cuantas();

    if (s_ram == (ais_ram_t *)0) { return; }

    for (k = 0U; k < n; k++) {
        const barco_t *b = &s_bar[k];
        char *l = s_lin[k];
        uint8_t p = 0U, c;

        p += pon_u(&l[p], b->mmsi);
        l[p++] = '\t';

        for (c = 0U; (c < 20U) && (b->nombre[c] != '\0'); c++) {
            l[p++] = b->nombre[c];
        }
        l[p++] = '\t';

        if (b->hay_pos) {
            p += pon_grados(&l[p], b->lat_e4, 'N', 'S');
            l[p++] = ' ';
            p += pon_grados(&l[p], b->lon_e4, 'E', 'W');
        }
        l[p++] = '\t';

        if (b->hay_vel) {
            p += pon_u(&l[p], (uint32_t)(b->sog_d / 10U));
            l[p++] = ',';
            l[p++] = (char)('0' + (b->sog_d % 10U));
        }
        l[p++] = '\t';

        if (b->hay_vel) {
            p += pon_u(&l[p], (uint32_t)(b->cog_d / 10U));
            l[p++] = 'o';
        }
        l[p++] = '\t';

        p += pon_u(&l[p], b->n);
        l[p] = '\0';
    }
    s_lin_vale = 1U;
}

uint32_t ais_modo_lineas(void)
{
    return (uint32_t)cuantas();
}

uint8_t ais_modo_pos(uint32_t i, int32_t *lat_e4, int32_t *lon_e4)
{
    const barco_t *b;

    /*
     * VA SOBRE LA TABLA ENTERA, no sobre los renglones que se ven.
     * `cuantas()` recorta a los catorce que caben en la pantalla, pero la
     * tabla guarda dieciseis: en el mapa caben todos y esconder dos
     * barcos porque no hay sitio en una lista seria raro.
     */
    if (s_ram == (ais_ram_t *)0 || i >= (uint32_t)s_nbar) { return 0U; }
    b = &s_bar[i];
    if (!b->hay_pos) { return 0U; }
    if (lat_e4 != 0) { *lat_e4 = b->lat_e4; }
    if (lon_e4 != 0) { *lon_e4 = b->lon_e4; }
    return 1U;
}

const char *ais_modo_linea(uint32_t i)
{
    if (s_ram == (ais_ram_t *)0) { return ""; }
    if (i >= (uint32_t)cuantas()) { return ""; }
    if (!s_lin_vale) { monta(); }
    return s_lin[i];
}

/*
 * EL ORDEN DE LA TABLA ES EL DE LLEGADA, Y NO SE TOCA.
 *
 * La tentacion es ordenar por quien ha hablado mas recientemente, que
 * parece mas util. No lo es: en AIS cada barco manda cada pocos segundos,
 * asi que la lista se reordenaria entera varias veces por segundo y leer un
 * renglon seria imposible. Un barco que aparece se queda en su sitio hasta
 * que se va.
 *
 * Cuando la tabla se llena, el hueco que se reutiliza es el del mas viejo
 * (ver busca()), o sea que la fila cambia de barco pero no se mueve.
 */
