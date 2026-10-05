#include "stanag_modo.h"
#include "stanag_det.h"
#include "stanag_rx.h"
#include "idioma.h"
#include "hfdl_ram.h"
#include "waterfall.h"
#include <stdint.h>
#include <stddef.h>

/* Ver stanag_modo.h para el porque. Aqui esta el como. */

/*
 * LA TABLA DE "CADA CUANTO SE REPITE" -> "QUE ES".
 *
 * DE DONDE SALE CADA NUMERO. Hay dos origenes y conviene no mezclarlos.
 *
 * 1) Los de VHF/UHF salen de limeauto/src/LimeAuto.cpp (sdrpower2/LimeAuto,
 *    Apache 2.0), que los comprueba como indices de muestra a 50 kHz. Aqui
 *    van convertidos a decenas de microsegundos dividiendo por 50.000, con
 *    el indice original al lado para poder volver a la fuente sin fiarse
 *    de mi aritmetica:
 *
 *        indice 1000 / 50 kHz =  20,00 ms   TETRAPOL
 *        indice 2833 / 50 kHz =  56,66 ms   TETRA
 *        indice 4800 / 50 kHz =  96,00 ms   DAB
 *        indice 6750 / 50 kHz = 135,00 ms   INMARSAT Standard C
 *        indice 1333 / 50 kHz =  26,66 ms   POCSAG
 *
 * 2) Los de HF -que son los que importan en esta radio- los he MEDIDO yo
 *    el 02/10/2026 con este mismo detector, sobre siete grabaciones de
 *    verdad que trajo el dueño. No de memoria ni de ninguna norma: el
 *    banco sim/stanag_aire.c las vuelve a medir en cada "make comprueba"
 *    y si alguna se mueve, salta.
 *
 *        106,67 ms   STANAG 4285      110 de 114 ventanas, calidad 100
 *        106,67 ms   STANAG 4481 PSK  296 de 300,          calidad 100
 *        211,69 ms   STANAG 4529      158 de 162,          calidad 100
 *         66,81 ms   STANAG 4415      596 de 600,          calidad  78
 *        119,61 ms   STANAG 4539      585 de 631,          calidad 100
 *         26,61 ms   STANAG 4481 FSK  198 de 837,          calidad  25
 *         13,33 ms   STANAG 4538       31 de 335, a rafagas
 *
 * LO QUE COMPARTE PERIODO, Y POR QUE SE DICE EN EL ROTULO.
 *
 * El 4481 no es una forma de onda: es el sistema de radiodifusion naval,
 * y lleva dos modems. El de PSK es EL 4285, medido: su preambulo
 * correlaciona a 0,886 contra la grabacion de 4481 PSK (fondo 0,084), con
 * la misma estructura 31/31/194 y 243 comienzos de trama separados por
 * 1280 muestras clavadas = 256,000 simbolos. O sea que 106,67 ms puede
 * ser un 4285 o un 4481 PSK y NO hay forma de saberlo por el periodo. Se
 * ponen los dos en el rotulo, que es la verdad, en vez de elegir uno.
 *
 * El 4538 (ALE de tercera generacion) no tiene UN periodo: va a rafagas
 * de formas distintas y da 13,33 / 26,67 / 106,67 / 133,34 ms, todos
 * multiplos de 32 simbolos a 2400 baudios. Dos de esos pisan a vecinos de
 * la tabla. Se mete solo el de 13,33 ms, que no lo comparte con nadie.
 *
 * Y el de 26,61 ms cae sobre el POCSAG de LimeAuto. Pero esta radio va de
 * 0 a 30 MHz y el POCSAG es de VHF/UHF: aqui, ese periodo es casi seguro
 * un 4481 en FSK. Van los dos en el rotulo por el mismo motivo de antes.
 *
 * Callarse cualquiera de estos empates seria decir un nombre con mas
 * seguridad de la que hay, que es justo lo que no quiero que haga la
 * pantalla.
 *
 * TOLERANCIA. Un 1 % a cada lado. A 106,67 ms eso es poco mas de un
 * milisegundo, y el detector interpola el pico con una fraccion de
 * muestra, asi que de sobra. Mas ancho empezaria a pisar a los vecinos:
 * entre 119,61 (4539) y 135,00 (INMARSAT) hay un 13 %, pero entre 133,34
 * (una rafaga del 4538) y 135,00 hay un 1,2 %, y por eso esa no entra.
 *
 * La tabla va ORDENADA POR PERIODO. El bucle se queda con la primera que
 * encaje, asi que el orden importa si dos llegaran a solaparse; ordenada
 * de menor a mayor, eso se ve leyendo.
 */
typedef struct {
    uint16_t    periodo;   /* decenas de microsegundos */
    uint8_t     forma;     /* STANAG_* si es una de las nuestras, 0 si no */
    const char *es;
    const char *en;
} conocida_t;

static const conocida_t k_conocidas[] = {
    {  1333U, STANAG_4538,  "STANAG 4538 (ALE 3G)",  "STANAG 4538 (3G ALE)"  },
    {  2000U, 0U,           "TETRAPOL (VHF/UHF)",    "TETRAPOL (VHF/UHF)"    },
    {  2661U, STANAG_4481,  "S4481 FSK / POCSAG",    "S4481 FSK / POCSAG"    },
    {  5666U, 0U,           "TETRA (VHF/UHF)",       "TETRA (VHF/UHF)"       },
    {  6681U, 0U,           "STANAG 4415",           "STANAG 4415"           },
    {  9600U, 0U,           "DAB (VHF)",             "DAB (VHF)"             },
    { 10667U, STANAG_4285,  "S4285 / S4481 PSK",     "S4285 / S4481 PSK"     },
    { 11961U, 0U,           "STANAG 4539 (M110B)",   "STANAG 4539 (M110B)"   },
    { 13500U, 0U,           "INMARSAT Std C",        "INMARSAT Std C"        },
    { 21169U, STANAG_4529,  "STANAG 4529",           "STANAG 4529"           },
};
#define N_CONOCIDAS (sizeof k_conocidas / sizeof k_conocidas[0])

static const texto_t k_formas[STANAG_FORMAS] = {
    T("Auto",      "Auto"),
    T("S4285",     "S4285"),
    T("S4529",     "S4529"),
    T("S4481",     "S4481"),
    T("S4538",     "S4538"),
};

/*
 * VOLATILE PORQUE LA INTERRUPCION DE AUDIO LA MIRA. stanag_modo_mete()
 * se llama desde demod_am.c dentro de la interrupcion, y lo primero que
 * hace es consultar esto; apagarla antes de soltar la memoria es lo que
 * impide que el audio siga escribiendo en unos buffers que el bucle
 * principal esta repartiendo de nuevo.
 */
static volatile uint8_t s_activo;
static uint8_t  s_forma;
static uint16_t s_periodo;
static uint8_t  s_encaja;
static uint8_t  s_calidad;
static uint8_t  s_picos;
static uint32_t s_medidas;
static uint32_t s_vueltas;   /* llamadas a poll; ver el estado */

/*
 * EL HISTORIAL DEL IDENTIFICADOR NO NECESITA LO MISMO QUE EL PANEL.
 *
 * STANAG_MODO_LINEAS es cuantos renglones admite la PANTALLA, y subio a
 * 16 para que quepan el texto y el diagnostico a la vez. Este buffer es
 * otra cosa: los periodos distintos que ha visto el identificador, y con
 * cinco sobra -se repiten-. Dimensionarlo con la constante de la
 * pantalla costaba medio kilobyte de RAM permanente y el enlace para el
 * GD32 se paso por 468 bytes.
 */
#define DET_LINEAS 3U
static char    s_lin[DET_LINEAS][STANAG_MODO_LARGO];
static uint8_t s_nlin;

/* --- utilidades de texto, sin printf (ver el resto de la casa) --- */
static uint8_t pon(char *b, uint8_t i, const char *s)
{
    while (*s != '\0' && i < (STANAG_MODO_LARGO - 1U)) { b[i++] = *s++; }
    return i;
}
static uint8_t pon_u(char *b, uint8_t i, uint32_t v)
{
    char t[12]; uint8_t n = 0U;
    if (v == 0UL) { t[n++] = '0'; }
    while (v > 0UL && n < 12U) { t[n++] = (char)('0' + (v % 10UL)); v /= 10UL; }
    while (n > 0U && i < (STANAG_MODO_LARGO - 1U)) { b[i++] = t[--n]; }
    return i;
}
/* Los milisegundos con dos decimales, que es lo que hace falta para
 * separar 106,67 de 107,00. */
static uint8_t pon_ms(char *b, uint8_t i, uint16_t decenas_us)
{
    i = pon_u(b, i, (uint32_t)(decenas_us / 100U));
    if (i < (STANAG_MODO_LARGO - 1U)) { b[i++] = ','; }
    if (i < (STANAG_MODO_LARGO - 1U)) { b[i++] = (char)('0' + ((decenas_us / 10U) % 10U)); }
    if (i < (STANAG_MODO_LARGO - 1U)) { b[i++] = (char)('0' + (decenas_us % 10U)); }
    return pon(b, i, " ms");
}

static void linea_nueva(const char *que, uint16_t periodo, uint8_t cal)
{
    uint8_t i = 0U, k;
    char *b;

    /* Las mas nuevas arriba. Cinco renglones son pocos para andar con un
     * anillo y un indice: se corren, que cuesta menos que explicarlo. */
    if (s_nlin < DET_LINEAS) { s_nlin++; }
    for (k = (uint8_t)(s_nlin - 1U); k > 0U; k--) {
        uint8_t j;
        for (j = 0U; j < STANAG_MODO_LARGO; j++) { s_lin[k][j] = s_lin[k-1U][j]; }
    }
    b = s_lin[0];
    /* Separados por tabulador: lo reparte en columnas ui_digi.c. */
    i = pon_ms(b, i, periodo);
    i = pon(b, i, "\t");
    i = pon(b, i, que);
    i = pon(b, i, "\t");
    i = pon_u(b, i, cal);
    i = pon(b, i, "%");
    b[i] = '\0';
}

/*
 * LOS 32 kB, PRESTADOS DE LA CASCADA.
 *
 * Mismo camino que wspr_modo.c: la region entera -cabecera incluida-,
 * porque los modos son exclusivos y mientras este puesto no hay estados de
 * HFDL que respetar. Ver hfdl_ram_todo().
 *
 * Y CASTEAR DE uint8_t* A float* NO ES GRATIS: si la region no empezara en
 * frontera de cuatro bytes, escribir floats ahi es comportamiento
 * indefinido, y en un Cortex-M4 eso sale como una excepcion de bus en el
 * sitio mas raro posible. Hoy la region es una union con floats dentro y
 * por tanto esta alineada, pero eso puede cambiar sin que nadie se acuerde
 * de este fichero. Se comprueba y se dice que no, que es barato y no deja
 * un cepo puesto.
 */
static float *ram_coge(uint32_t n_floats)
{
    uint8_t *ram;
    uint32_t cap = 0UL;

    if (waterfall_prestada()) { return 0; }   /* otro modo la tiene */

    hfdl_ram_coge();
    ram = hfdl_ram_todo(&cap);
    if (ram == (uint8_t *)0 || cap < (n_floats * 4UL)) {
        hfdl_ram_suelta();
        return 0;
    }
    if ((((uintptr_t)ram) & 3U) != 0U) {
        hfdl_ram_suelta();
        return 0;
    }
    return (float *)(void *)ram;
}

/*
 * DOS OFICIOS QUE NO PUEDEN CORRER A LA VEZ, Y ESTA BIEN ASI.
 *
 * El detector (stanag_det.c) dice "aqui hay algo que se repite cada
 * tanto"; el demodulador (stanag_rx.c) saca el texto. Los dos quieren
 * los mismos 32 kB prestados de la cascada, asi que corre uno u otro:
 *
 *     boton de forma en "Auto"  -> detector, para identificar
 *     boton en "S4285"          -> demodulador, para leer
 *
 * Y no es un apaño por la memoria: es lo que pidio el dueño cuando
 * salio llamandose IDENT - "quiero un boton especifico de stanag, que
 * voy a usar cuando sepa seguro que la señal es stanag". Primero se
 * identifica, luego se lee.
 */
/*
 * EL 4481 TAMBIEN DEMODULA, Y NO PONERLO FUE UN DESCUIDO.
 *
 * El 4481 en PSK no se parece al 4285: ES el 4285. Se midio en su dia -su
 * preambulo correlaciona a 0,886 contra el de 4285, con la misma
 * estructura 31/31/194 y 243 comienzos de trama separados por 1280
 * muestras clavadas- y es con una grabacion de 4481 con la que salio el
 * primer texto. Dejarlo corriendo el detector mientras el boton dice
 * "S4481" era tener la puerta buena cerrada con llave.
 *
 * El 4529 y el 4538 no: son formas de onda distintas y de momento solo
 * se identifican. Eso lo dice el estado, para que nadie se quede
 * mirando una pantalla en blanco preguntandose si va.
 */
static uint8_t demod_toca(void)
{
    return (uint8_t)(((s_forma == STANAG_4285) || (s_forma == STANAG_4481)) ? 1U : 0U);
}
uint8_t stanag_modo_demod(void) { return (uint8_t)(s_activo && demod_toca()); }

uint8_t stanag_modo_start(void)
{
    float *t;

    if (s_activo) { return 1U; }
    stanag_modo_borra();
    t = ram_coge(STANAG_DET_FLOATS);
    if (t == 0) { return 0U; }
    if (demod_toca()) {
        if (!stanag_rx_init(t, STANAG_DET_FLOATS)) {
            hfdl_ram_suelta();
            return 0U;
        }
    } else if (!stanag_det_init(t, STANAG_DET_FLOATS)) {
        hfdl_ram_suelta();
        return 0U;
    }
    s_activo = 1U;
    return 1U;
}

void stanag_modo_stop(void)
{
    if (!s_activo) { return; }
    s_activo = 0U;        /* PRIMERO: calla a la interrupcion */
    stanag_rx_fin();
    hfdl_ram_suelta();
}

/* Cambiar de oficio es parar y volver a arrancar: los dos piden la misma
 * memoria y cada uno la reparte a su manera. */
static uint8_t recoloca(void)
{
    if (!s_activo) { return 1U; }
    stanag_modo_stop();
    return stanag_modo_start();
}

uint8_t stanag_modo_activo(void) { return s_activo; }

void stanag_modo_mete(const float *audio, uint16_t n)
{
    if (!s_activo) { return; }
    if (demod_toca()) { stanag_rx_mete(audio, (uint32_t)n); }
    else              { stanag_det_mete(audio, (uint32_t)n); }
}

void stanag_modo_poll(void)
{
    stanag_det_t d;

    if (!s_activo) { return; }
    s_vueltas++;
    if (demod_toca()) {
        /*
         * SOLO SE CUENTA UNA MEDIDA CUANDO HAY NOTICIA, Y ESTO IMPORTA.
         *
         * *** El dueño, 02/10/2026: "voy por busca 10 y nada", con una
         * señal que el identificador ve perfectamente. ***
         *
         * stanag_modo_latido() suma esta cuenta y main.c repinta el panel
         * entero cada vez que cambia. Subiendola en CADA pasada, el panel
         * se repintaba en cada vuelta del bucle principal; el bucle se
         * ponia lento; el audio se acumulaba; el anillo del demodulador
         * -170 ms- se desbordaba, y al desbordarse se pierde el
         * sincronismo. Un circulo que yo mismo monte: cuanto mas miraba
         * la pantalla, menos enganchaba.
         *
         * El latido ya lleva g_msticks/250, que repinta cuatro veces por
         * segundo pase lo que pase. Con eso sobra para ver la chapa.
         */
        if (stanag_rx_paso()) { s_medidas++; }
        return;
    }
    if (!stanag_det_paso(&d)) { return; }

    /*
     * SUBE EN CADA PASADA, haya trama o no: es lo que mira el repintado
     * del panel para saber que hay algo nuevo que enseñar, y los
     * contadores de diagnostico cambian aunque no se vea nada. Si solo
     * subiera con trama, la pantalla se quedaria congelada justo cuando
     * mas falta hace mirarla. Ver ft8_panel_draw() en main.c.
     */
    s_medidas++;
    if (!d.vale) {
        /* No se borra lo anterior: una señal con desvanecimiento deja de
         * verse unos segundos y volver a poner "buscando" cada vez que se
         * va seria no decir nada. Lo que se enseña es la ULTIMA medida
         * buena, y el estado dice si sigue viendose. */
        s_calidad = 0U;
        return;
    }

    s_calidad = d.calidad;
    s_picos   = d.picos;

    /* ¿Es la misma de antes? Entonces no se repite el renglon. */
    {
        uint16_t p = d.periodo_us;
        uint16_t tol = (uint16_t)(p / 100U);   /* el 1 % */
        uint8_t  i, enc = 0U;
        const char *nombre = tr("sin identificar", "unidentified");

        if (tol == 0U) { tol = 1U; }

        for (i = 0U; i < N_CONOCIDAS; i++) {
            uint16_t c = k_conocidas[i].periodo;
            uint16_t ct = (uint16_t)(c / 100U);
            if (ct == 0U) { ct = 1U; }
            if (p >= (c - ct) && p <= (c + ct)) {
                nombre = tr(k_conocidas[i].es, k_conocidas[i].en);
                enc = k_conocidas[i].forma;
                break;
            }
        }

        if (s_periodo == 0U || p < (s_periodo - tol) || p > (s_periodo + tol)) {
            linea_nueva(nombre, p, d.calidad);
        }
        s_periodo = p;
        s_encaja  = enc;
    }
}

void stanag_modo_forma_pon(uint8_t f)
{
    uint8_t antes = s_forma;
    s_forma = (f < STANAG_FORMAS) ? f : STANAG_AUTO;
    {
        uint8_t antes_demod = (uint8_t)(((antes == STANAG_4285)
                                      || (antes == STANAG_4481)) ? 1U : 0U);
        if (demod_toca() != antes_demod) { (void)recoloca(); }
    }
}

/* Los tres botones del demodulador. Cambiar la velocidad o el
 * entrelazado cambia el tamaño del desentrelazador, asi que hay que
 * volver a repartir la memoria; el formato de salida no. */
/*
 * Si el reparto nuevo no cabe -solo pasa con 2400 bps y entrelazado
 * largo- se vuelve al anterior en vez de dejar el modo apagado sin que
 * nadie lo diga. El boton parecera que no hace nada en esa posicion, y
 * eso es mejor que una pantalla muerta.
 */
void stanag_modo_vel_pon(uint8_t v)
{
    uint8_t antes = stanag_rx_velocidad();
    stanag_rx_velocidad_pon(v);
    if (s_activo && demod_toca() && !recoloca()) {
        stanag_rx_velocidad_pon(antes);
        (void)stanag_modo_start();
    }
}
uint8_t     stanag_modo_vel(void) { return stanag_rx_velocidad(); }
const char *stanag_modo_vel_txt(void)
{ return stanag_rx_velocidad_txt(stanag_rx_velocidad()); }

void stanag_modo_entre_pon(uint8_t largo)
{
    uint8_t antes = stanag_rx_largo();
    stanag_rx_largo_pon(largo);
    if (s_activo && demod_toca() && !recoloca()) {
        stanag_rx_largo_pon(antes);
        (void)stanag_modo_start();
    }
}
uint8_t     stanag_modo_entre(void) { return stanag_rx_largo(); }
const char *stanag_modo_entre_txt(void)
{ return stanag_rx_largo() ? tr("Largo", "Long") : tr("Corto", "Short"); }

void stanag_modo_llenado(uint16_t *hechos, uint16_t *hacen)
{
    stanag_rx_est_t e;
    if (hechos != 0) { *hechos = 0U; }
    if (hacen  != 0) { *hacen  = 0U; }
    if (!stanag_modo_demod()) { return; }
    stanag_rx_estado(&e);
    if (hechos != 0) { *hechos = e.grupos_hechos; }
    if (hacen  != 0) { *hacen  = e.grupos_hacen; }
}

void        stanag_modo_fmt_pon(uint8_t f) { stanag_rx_formato_pon(f); }
uint8_t     stanag_modo_fmt(void)          { return stanag_rx_formato(); }
const char *stanag_modo_fmt_txt(void)
{ return stanag_rx_formato_txt(stanag_rx_formato()); }
uint8_t stanag_modo_forma(void) { return s_forma; }

const char *stanag_modo_forma_txt(uint8_t f)
{
    if (f >= STANAG_FORMAS) { f = STANAG_AUTO; }
    return tr(k_formas[f][0], k_formas[f][1]);
}

void stanag_modo_borra(void)
{
    uint8_t i, j;
    /* Si esta demodulando, el texto de la pantalla sale de stanag_rx,
     * no de aqui: hay que vaciar ESE. Sin esta linea el boton Borrar
     * no hacia nada visible en modo demodulador. */
    if (demod_toca()) { stanag_rx_borra(); }
    for (i = 0U; i < DET_LINEAS; i++) {
        for (j = 0U; j < STANAG_MODO_LARGO; j++) { s_lin[i][j] = '\0'; }
    }
    s_nlin = 0U; s_periodo = 0U; s_encaja = 0U;
    s_calidad = 0U; s_picos = 0U; s_medidas = 0UL; s_vueltas = 0UL;
}

/*
 * MIENTRAS NO HAY TEXTO, UNA LINEA QUE DIGA QUE PASA.
 *
 * Es la otra mitad del "no lo hace, no sale": el renglon de arriba
 * cuenta lo que esta haciendo en vez de dejar el panel vacio. Con
 * entrelazado largo esto se ve diez segundos largos, y hay que verlo.
 */
/*
 * EL BLOQUE DE DIAGNOSTICO, EN LA ZONA NEGRA DEL PANEL.
 *
 * *** El dueño, 02/10/2026, despues de media noche sin que enganchara en
 * el aire y sin poder grabar audio desde el PC: "tienes toda una zona
 * negra abajo, pon ahi todo lo que necesites". ***
 *
 * Pues eso. Mientras no hay texto que enseñar, esas filas valen mas
 * contando lo que pasa que estando vacias. Cuatro renglones que separan
 * los sitios donde esto puede estar fallando:
 *
 *   1  la correlacion del preambulo: por debajo de 15 es que ahi no hay
 *      un 4285; entre 30 y 44 lo ve pero no llega; 45 es el umbral
 *   2  a que desvio de portadora salio lo mejor, y hasta donde se esta
 *      barriendo: si lo mejor cae en el borde del barrido, la sintonia
 *      esta mas lejos de lo que se busca
 *   3  el audio: si se pierde, el bucle va tarde y lo demas no importa
 *   4  tramas vistas y barridos hechos: dice si llega a enganchar a
 *      ratos o no engancha nunca
 */
#define AVISOS 8U
static char s_av[AVISOS][STANAG_MODO_LARGO];

static void avisos_haz(void)
{
    stanag_rx_est_t e;
    uint8_t j;

    {
        uint8_t k2;
        for (k2 = 0U; k2 < AVISOS; k2++) { s_av[k2][0] = '\0'; }
    }

    if (!demod_toca()) {
        if ((s_forma == STANAG_4529) || (s_forma == STANAG_4538)) {
            (void)pon(s_av[0], 0U, tr("esta forma de onda solo se identifica",
                                      "this waveform is identify-only"));
        } else {
            (void)pon(s_av[0], 0U, tr("midiendo el periodo de la trama...",
                                      "measuring the frame period..."));
        }
        return;
    }

    stanag_rx_estado(&e);

    /* --- 0: lo primero, si engancha o no y por donde va */
    if (e.engancha && (e.grupos_hacen != 0U)) {
        j = pon(s_av[5], 0U, tr("ENGANCHADO. llenando ", "LOCKED. filling "));
        j = pon_u(s_av[5], j, e.grupos_hechos);
        if (j < (STANAG_MODO_LARGO - 1U)) { s_av[5][j++] = '/'; }
        j = pon_u(s_av[5], j, e.grupos_hacen);
        s_av[5][j] = '\0';
    } else if (e.engancha) {
        j = pon(s_av[5], 0U, tr("ENGANCHADO. sondeos ", "LOCKED. probes "));
        j = pon_u(s_av[5], j, e.sondeos);
        j = pon(s_av[5], j, "%  e");
        if (e.ber_vale) { j = pon_u(s_av[5], j, e.ber); j = pon(s_av[5], j, "%"); }
        else            { j = pon(s_av[5], j, "--"); }
        s_av[5][j] = '\0';
    }

    /* --- 1: la correlacion del preambulo */
    /*
     * EL NUMERO Y SU FONDO, QUE SIN EL FONDO NO DICE NADA. El ruido
     * correlaciona a 26 por su cuenta, asi que un 27 no es "señal
     * floja": es ruido. Lo que cuenta es despegarse del fondo.
     */
    j = pon(s_av[0], 0U, tr("preambulo ", "preamble "));
    j = pon_u(s_av[0], j, e.corr);
    j = pon(s_av[0], j, tr(" (fondo ", " (floor "));
    j = pon_u(s_av[0], j, e.fondo);
    j = pon(s_av[0], j, tr(", umbral ", ", thresh "));
    j = pon_u(s_av[0], j, e.umbral);
    j = pon(s_av[0], j, ") ");
    /*
     * TRES VECES EL FONDO, NO EL FONDO A SECAS.
     *
     * Medido con ruido puro: la correlacion MEDIA sale 9 y la mejor 26.
     * No es contradictorio - uno se queda con el mejor de miles de
     * intentos, y el mejor de muchos ruidos siempre parece algo. Asi que
     * la referencia no es el fondo sino unas tres veces el fondo, que es
     * donde el ruido deja de llegar. Un preambulo de verdad se despega
     * mucho mas: el del dueño en 4164 dio 63 con el mismo fondo.
     */
    if ((e.fondo != 0U) && (e.corr < (uint8_t)((e.fondo * 3U) + 2U))) {
        j = pon(s_av[0], j, tr("-> es ruido, aqui no hay nada",
                               "-> just noise, nothing here"));
    } else if (e.corr < e.umbral) {
        j = pon(s_av[0], j, tr("-> algo hay, no llega", "-> something, too weak"));
    } else {
        j = pon(s_av[0], j, tr("-> deberia enganchar", "-> should lock"));
    }
    s_av[0][j] = '\0';

    /* --- 2: donde salio y hasta donde se busca */
    j = pon(s_av[1], 0U, tr("mejor a ", "best at "));
    if (e.corr_hz < 0) { j = pon(s_av[1], j, "-"); }
    else               { j = pon(s_av[1], j, "+"); }
    j = pon_u(s_av[1], j, (uint32_t)((e.corr_hz < 0) ? -e.corr_hz : e.corr_hz));
    j = pon(s_av[1], j, tr(" Hz, busco +-", " Hz, sweep +-"));
    j = pon_u(s_av[1], j, e.ancho_hz);
    j = pon(s_av[1], j, " Hz");
    s_av[1][j] = '\0';

    /* --- 3: el audio */
    j = pon(s_av[2], 0U, tr("cola ", "queue "));
    j = pon_u(s_av[2], j, e.cola_ms);
    j = pon(s_av[2], j, tr(" ms, pierde x", " ms, drops x"));
    j = pon_u(s_av[2], j, e.perdidas);
    s_av[2][j] = '\0';

    /* --- 4: cuanto ha llegado a enganchar */
    j = pon(s_av[3], 0U, tr("tramas ", "frames "));
    j = pon_u(s_av[3], j, e.tramas);
    j = pon(s_av[3], j, tr(" (buenas ", " (good "));
    j = pon_u(s_av[3], j, e.tramas_bien);
    j = pon(s_av[3], j, tr(")  barridos ", ")  sweeps "));
    j = pon_u(s_av[3], j, e.busquedas);
    s_av[3][j] = '\0';

    /* --- 6: la trama, que es lo que dice si ahi hay 4285 o no hay nada */
    j = pon(s_av[6], 0U, tr("trama ", "framing "));
    j = pon_u(s_av[6], j, e.trama);
    j = pon(s_av[6], j, tr(" -> ", " -> "));
    if (e.trama >= 40U) {
        j = pon(s_av[6], j, tr("SI hay 106,67 ms aqui", "106.67 ms IS here"));
    } else if (e.trama >= 20U) {
        j = pon(s_av[6], j, tr("quiza, muy flojo", "maybe, very weak"));
    } else {
        j = pon(s_av[6], j, tr("no hay trama, pruebe otra frec",
                               "no framing, try another freq"));
    }
    s_av[6][j] = '\0';

    /* --- 7: los records, por si algo fue bien hace un rato */
    j = pon(s_av[7], 0U, tr("mejor preambulo ", "best preamble "));
    j = pon_u(s_av[7], j, e.corr_max);
    j = pon(s_av[7], j, tr("  mejores sondeos ", "  best probes "));
    j = pon_u(s_av[7], j, e.sondeos_max);
    j = pon(s_av[7], j, tr("%  nivel ", "%  level "));
    j = pon_u(s_av[7], j, e.nivel);
    s_av[7][j] = '\0';

    /* --- 5: los ajustes puestos, que es lo primero que se mira mal */
    j = pon(s_av[4], 0U, stanag_rx_velocidad_txt(stanag_rx_velocidad()));
    j = pon(s_av[4], j, tr(" bps, ", " bps, "));
    j = pon(s_av[4], j, stanag_rx_largo() ? tr("largo", "long") : tr("corto", "short"));
    j = pon(s_av[4], j, ", ");
    j = pon(s_av[4], j, stanag_rx_formato_txt(stanag_rx_formato()));
    j = pon(s_av[4], j, tr(", desvio ", ", offset "));
    {
        int16_t hz = (int16_t)(e.desvio_dhz / 10);
        j = pon(s_av[4], j, (hz < 0) ? "-" : "+");
        if (hz < 0) { hz = (int16_t)-hz; }
        j = pon_u(s_av[4], j, (uint32_t)hz);
    }
    j = pon(s_av[4], j, "Hz");
    s_av[4][j] = '\0';
}

/*
 * ARRIBA EL TEXTO, ABAJO EL DIAGNOSTICO. SIEMPRE LOS DOS.
 *
 * *** El dueño, 02/10/2026: "deja 9 renglones arriba y todo lo demas de
 * abajo es pa que pongas lo que te de la gana" / "pon todo lo que
 * necesites o vayas a necesitar". ***
 *
 * Asi que el panel enseña las dos cosas a la vez y el diagnostico no
 * desaparece en cuanto sale una letra - que es justo cuando mas falta
 * hace saber si esa letra es de verdad.
 */
#define TEXTO_FILAS 9U

uint8_t stanag_modo_lineas(void)
{
    if (!s_activo) { return 0U; }
    if (stanag_modo_demod()) {
        uint8_t n = stanag_rx_nlineas();
        uint8_t d = 0U;
        if (n > TEXTO_FILAS) { n = TEXTO_FILAS; }
        avisos_haz();
        while (d < AVISOS && s_av[d][0] != '\0') { d++; }
        return (uint8_t)(n + d);
    }
    return (uint8_t)((s_nlin != 0U) ? s_nlin : 1U);
}

const char *stanag_modo_linea(uint8_t i)
{
    if (!s_activo) { return ""; }
    if (stanag_modo_demod()) {
        uint8_t n = stanag_rx_nlineas();
        if (n > TEXTO_FILAS) { n = TEXTO_FILAS; }
        if (i < n) { return stanag_rx_linea(i); }
        avisos_haz();
        return ((i - n) < AVISOS) ? s_av[i - n] : "";
    }
    if (s_nlin == 0U) { avisos_haz(); return (i == 0U) ? s_av[0] : ""; }
    return (i < s_nlin) ? s_lin[i] : "";
}

/*
 * LA CHAPA SON TRECE CARACTERES, NI UNO MAS.
 *
 * *** El dueño, 02/10/2026, con una foto: el estado se pintaba ENCIMA
 * del boton de formato. ***
 *
 * ui_digi reserva 140 px a la derecha y ahora los hace cumplir
 * recortando, asi que una frase larga ya no rompe nada. Pero recortada
 * tampoco se entiende, y lo que de verdad hay que mirar cabe de sobra.
 * Lo largo se dice en el renglon del panel, que tiene sitio; aqui van
 * los tres numeros:
 *
 *     e3 s100 +58    error de recodificacion, sondeos, desvio en Hz
 *     e-- s83 +58    cuando el error no mide (ver stanag_rx.c)
 *     llena 46%      mientras se llena el entrelazador
 *     busca 10       mientras no engancha, con las tramas vistas
 *
 * El error va PRIMERO porque es el unico que distingue "esta leyendo"
 * de "se lo esta inventando".
 */
const char *stanag_modo_estado_txt(void)
{
    static char b[STANAG_MODO_LARGO];
    static char b2[STANAG_MODO_LARGO];
    uint8_t i = 0U;

    if (!s_activo) { return tr("apagado", "off"); }

    if ((s_forma == STANAG_4529) || (s_forma == STANAG_4538)) {
        return tr("solo ident.", "ident. only");
    }
    if (demod_toca()) {
        stanag_rx_est_t e;
        int16_t hz;

        stanag_rx_estado(&e);
        if (!e.engancha) {
            i = pon(b, i, tr("busca ", "hunt "));
            i = pon_u(b, i, e.tramas);
            b[i] = '\0';
            return b;
        }
        if (e.grupos_hacen != 0U) {
            i = pon(b, i, tr("llena ", "fill "));
            i = pon_u(b, i, ((uint32_t)e.grupos_hechos * 100UL)
                            / ((e.grupos_hacen != 0U) ? e.grupos_hacen : 1U));
            if (i < (STANAG_MODO_LARGO - 1U)) { b[i++] = '%'; }
            b[i] = '\0';
            return b;
        }
        if (i < (STANAG_MODO_LARGO - 1U)) { b[i++] = 'e'; }
        if (e.ber_vale) { i = pon_u(b, i, e.ber); }
        else            { i = pon(b, i, "--"); }
        i = pon(b, i, " s");
        i = pon_u(b, i, e.sondeos);
        hz = (int16_t)(e.desvio_dhz / 10);
        i = pon(b, i, (hz < 0) ? " -" : " +");
        if (hz < 0) { hz = (int16_t)-hz; }
        i = pon_u(b, i, (uint32_t)hz);
        b[i] = '\0';
        return b;
    }

    if (s_medidas == 0UL) {
        /*
         * Mientras no hay medida, se enseña POR DONDE VA en vez de un
         * "espere" que no dice nada. Los tres numeros separan los tres
         * motivos por los que esto puede no arrancar:
         *
         *   audio 0            no entra sonido: el problema esta antes
         *   audio sube, anillo corto   entra pero aun no hay un segundo
         *   anillo lleno, pasadas < 5  ya mide, le falta promediar
         *
         * Ver stanag_det_diag().
         */
        uint32_t met = 0UL, lle = 0UL;
        uint8_t  pas = 0U;

        stanag_det_diag(&met, &lle, &pas);
        i = pon(b2, 0U, "au ");
        i = pon_u(b2, i, met);
        i = pon(b2, i, " an ");
        i = pon_u(b2, i, lle);
        i = pon(b2, i, " pa ");
        i = pon_u(b2, i, pas);
        i = pon(b2, i, " po ");
        i = pon_u(b2, i, s_vueltas);
        b2[i] = '\0';
        return b2;
    }
    if (s_periodo == 0U) { return tr("sin trama", "no framing"); }

    i = pon_ms(b, i, s_periodo);
    i = pon(b, i, (s_calidad != 0U) ? tr(" ok", " ok") : tr(" --", " --"));
    i = pon(b, i, " x");
    i = pon_u(b, i, s_picos);
    b[i] = '\0';
    return b;
}

uint32_t stanag_modo_medidas(void) { return s_medidas; }

/* Ver el comentario de la cabecera. g_msticks lo lleva main.c. */
extern volatile uint32_t g_msticks;
uint32_t stanag_modo_latido(void)
{
    return s_medidas + (g_msticks / 250UL);
}
uint16_t stanag_modo_periodo(void) { return s_periodo; }
uint8_t  stanag_modo_encaja(void)  { return s_encaja; }
