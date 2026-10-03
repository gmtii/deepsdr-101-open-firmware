#include "jtty_modo.h"
#include "jtty_rx.h"
#include "hfdl_ram.h"
#include "waterfall.h"
#include "rtc_hw.h"
#include "reloj.h"
#include "dxcc.h"
#include <string.h>

extern volatile uint32_t g_msticks;

/* Ver jtty_modo.h. */

/*
 * TODO VIVE EN LA MEMORIA PRESTADA. En esta radio quedan 152 bytes libres
 * en .bss y esto pide 29 kB, asi que sale de la que se reparten FT8, HFDL,
 * WSPR, AIS y ALE: se paran todos y se arranca uno.
 */
typedef struct {
    jtty_rx_ram_t rx;
    char          linea[JTTY_MODO_LINEAS][JTTY_MODO_LARGO];

    /* Los mensajes a medias, uno por cubo de frecuencia. */
    struct {
        int16_t  hz;        /* el del primer atomo */
        uint32_t ms;        /* cuando llego el ultimo, para el plazo */
        uint32_t fila;      /* en que fila acabo la trama del ultimo */
        uint8_t  usado;
        uint8_t  n;         /* cuantos atomos llevan */
        uint8_t  ant5;      /* el anterior era un TEXT5 */
        uint8_t  q;
        char     txt[JTTY_MODO_LARGO];
    } cubo[JTTY_MODO_CUBOS];
} jtty_ram_t;

static jtty_ram_t *s_ram;
static uint8_t  s_on;
static uint8_t  s_n;          /* renglones puestos */
static uint32_t s_atomos;
static char     s_chapa[16];

/*
 * LOS HUECOS SE MIDEN EN FILAS, NO EN LA HORA A LA QUE ME ENTERO.
 *
 * *** Por el dueño del proyecto, con la radio en antena, en tres pasos:
 * primero "creo que juntas mensajes", luego "es como que no tienen mucho
 * sentido los mensajes porque se pierden trozos entonces no los encadena
 * bien", y por ultimo la foto de 14.090 que lo dejo claro:
 *
 *     16:49   46 Hz   90   RX1AG TU CQ PD5LDB CQ
 *     16:49   46 Hz   85   W3GQ TU CQ PD5LDB CQ
 *     16:50   46 Hz   68   CQ PD5LDB CQ
 *
 * "CQ PD5LDB CQ" no es el final de los dos primeros mensajes: es un
 * TERCERO, y sale solo en el ultimo renglon porque esa vez le toco cubo
 * aparte. ***
 *
 * LA VERSION ANTERIOR MEDIA EL TIEMPO MAL. Pegaba o cortaba segun los
 * milisegundos entre un atomo y el siguiente, pero ese instante no es
 * cuando llego la trama: es cuando el bucle principal se entero. Y
 * `jtty_modo_poll()` resuelve como mucho dos candidatos por pasada, asi
 * que dos atomos de tramas separadas dos segundos pueden salir en la
 * MISMA pasada con la misma hora -hueco cero, y se pegan-, y dos de
 * tramas seguidas pueden salir en pasadas distintas. El numero que se
 * comparaba no era la magnitud que se queria medir.
 *
 * LO QUE SI SE MIDE. Una trama son 59 simbolos y las filas van a media
 * fila por simbolo, o sea que las tramas de un mensaje acaban EXACTAMENTE
 * 118 filas seguidas -y jtty_rx.c ya sabe en que fila acabo cada una-.
 * Entonces:
 *
 *   118 filas     viene pegada: es la siguiente del mismo mensaje;
 *   236, 354...   encaja en la rejilla pero faltan tramas: se pega con
 *                 unos puntos suspensivos EN MEDIO, que es lo unico
 *                 verdadero que se puede decir;
 *   cualquier     NO es de este mensaje aunque este en la misma
 *   otra fase     frecuencia: es otra estacion, y abre su propio cubo.
 *
 *     CQ F4BAL JO10 ... 599 002
 *
 * Eso separa las tres cosas que antes se confundian -la trama siguiente,
 * la trama perdida y la otra estacion- con la magnitud que de verdad las
 * distingue, y ninguna de las tres es un ajuste que haya que afinar a
 * ojo: 118 sale de 59 simbolos.
 *
 * El margen es de dos filas porque el sincronismo se prueba cada media
 * fila y puede clavarse en la que toca o en la de al lado, o sea una fila
 * por trama y dos entre dos tramas. La deriva de reloj no cuenta: 100 ppm
 * sobre 1,888 s son 0,19 ms, la milesima parte de una fila.
 */

/*
 * EL PLAZO DE CERRAR. Tiene que ser en milisegundos y no en filas porque
 * es el unico que salta CUANDO NO LLEGA NADA, y entonces no hay fila
 * nueva que comparar.
 *
 * VA ATADO A JTTY_SALTOS_MAX y no se puede tocar uno sin el otro: si el
 * plazo se acaba antes de que pueda llegar la trama del ultimo salto, el
 * alcance que dice la constante es mentira. El ultimo salto son
 * (SALTOS_MAX + 1) * 118 filas = 590, y una fila son 16 ms: 9.440 ms.
 * 9.600 deja margen para el bucle principal.
 *
 * Y ESE ES EL PRECIO DE ARREGLAR LOS MENSAJES PARTIDOS: estaba en 4.200 y
 * sube a 9.600, o sea que un mensaje al que se le pierde la trama del
 * final tarda casi diez segundos en salir a pantalla en vez de cuatro.
 * Se acepta porque el modo ya es lento -un mensaje de dieciseis tramas
 * dura treinta segundos- y porque tres trozos sueltos al momento se leen
 * peor que un mensaje entero diez segundos despues, que es justo lo que
 * dijo el dueño de la radio mirando la pantalla.
 */
#define JTTY_ESPERA_MS 9600UL

/* Y que el plazo llegue de verdad al ultimo salto, no de palabra. */
_Static_assert(JTTY_ESPERA_MS
               > ((JTTY_SALTOS_MAX + 1UL) * JTTY_RX_FILAS_PASO * 16UL),
               "el plazo de cerrar se acaba antes del ultimo salto: el "
               "alcance de JTTY_SALTOS_MAX no se cumple");

/* ------------------------------------------------------------ */

static void u2(char *p, uint8_t v)
{
    p[0] = (char)('0' + (v / 10U) % 10U);
    p[1] = (char)('0' + v % 10U);
}

static char *pon(char *p, const char *t)
{
    while (*t != '\0') { *p++ = *t++; }
    return p;
}

static char *pon_i(char *p, int16_t v)
{
    uint16_t a = (uint16_t)((v < 0) ? -v : v);
    char t[6];
    uint8_t n = 0U;

    if (v < 0) { *p++ = '-'; }
    do { t[n++] = (char)('0' + (a % 10U)); a = (uint16_t)(a / 10U); } while (a != 0U);
    while (n > 0U) { *p++ = t[--n]; }
    return p;
}

/*
 * UN RENGLON. Las columnas las pone ui_digi.c; aqui van tabuladas.
 *
 *     Hora   Hz   Cal.   Mensaje
 */
static void apunta(const char *txt, int16_t hz, uint8_t q, uint8_t entero)
{
    char *l;
    uint8_t hh = 0U, mm = 0U;

    if (s_ram == (jtty_ram_t *)0) { return; }

    /* Se empuja hacia arriba: el mas nuevo abajo, como FT8 y HFDL. */
    if (s_n >= (uint8_t)JTTY_MODO_LINEAS) {
        uint8_t i;

        for (i = 1U; i < (uint8_t)JTTY_MODO_LINEAS; i++) {
            memcpy(s_ram->linea[i - 1U], s_ram->linea[i], JTTY_MODO_LARGO);
        }
        s_n = (uint8_t)(JTTY_MODO_LINEAS - 1U);
    }
    l = s_ram->linea[s_n];

    {
        rtc_hw_datetime_t d;

        rtc_hw_get(&d);
        hh = d.hour; mm = d.minute;
    }
    u2(l, hh); l[2] = ':'; u2(&l[3], mm); l[5] = '\t';
    l = &l[6];

    l = pon_i(l, hz);
    l = pon(l, " Hz\t");
    l = pon_i(l, (int16_t)q);
    *l++ = '\t';

    {
        uint8_t k = 0U;

        /*
         * Lo que quepa, y si no cabe se corta: el renglon tiene su tope y
         * pasarse escribe encima del siguiente. Se le guarda sitio al pais
         * del final, que es la ultima columna (JTTY_MODO_PAIS).
         */
        while (txt[k] != '\0'
               && (uint32_t)(l - s_ram->linea[s_n])
                  < (JTTY_MODO_LARGO - JTTY_MODO_PAIS - 6U)) {
            *l++ = txt[k++];
        }
    }
    if (!entero) { l = pon(l, "..."); }

    /*
     * Y LA COLUMNA DEL PAIS, del prefijo del indicativo.
     *
     * *** Por el dueño del proyecto: "estaria bien poner una columna en
     * jtty con el pais". ***
     *
     * El indicativo sale de jtty_msg_indicativo(), que coge el ULTIMO del
     * mensaje -quien transmite- reconociendolo por su forma, porque aqui
     * el mensaje es texto libre y no hay campos que preguntar. Ver
     * jtty_msg.h para la forma y por que es estricta.
     *
     * Si no hay indicativo, o su prefijo no esta en la tabla, la columna se
     * queda VACIA. Es lo mismo que hace FT8 y por lo mismo: un hueco es una
     * respuesta honrada y un pais equivocado no (ver dxcc.h).
     */
    *l++ = '\t';
    {
        char ind[16];

        if (jtty_msg_indicativo(txt, ind)) {
            const char *pais = dxcc_pais(ind);

            if (pais != 0) { l = pon(l, pais); }
        }
    }
    *l = '\0';
    s_n++;
}

/* ------------------------------------------------------------ */

uint8_t jtty_modo_start(void)
{
    uint8_t *ram;
    uint32_t cap = 0UL;

    if (s_on) { return 1U; }
    /*
     * Igual que WSPR y AIS: si otro modo tiene la memoria cogida, aqui no
     * se arranca. En la practica main.c para los otros antes de llamar,
     * pero la comprobacion es lo que impide que un reordenamiento del
     * conmutador deje a dos escribiendo en los mismos bytes.
     */
    if (waterfall_prestada()) { return 0U; }

    hfdl_ram_coge();
    ram = hfdl_ram_todo(&cap);
    if ((ram == (uint8_t *)0) || (cap < (uint32_t)sizeof(jtty_ram_t))) {
        hfdl_ram_suelta();
        return 0U;
    }
    s_ram = (jtty_ram_t *)(void *)ram;
    memset(s_ram, 0, sizeof *s_ram);

    s_n = 0U;
    s_atomos = 0UL;
    jtty_rx_start(&s_ram->rx, 12000.0f);
    s_on = 1U;
    return 1U;
}

void jtty_modo_stop(void)
{
    if (!s_on) { return; }
    jtty_rx_stop();
    s_on = 0U;
    s_ram = (jtty_ram_t *)0;
    s_n = 0U;          /* los renglones se van con la memoria prestada */
    hfdl_ram_suelta();
}

uint8_t jtty_modo_activo(void) { return s_on; }

void jtty_modo_mete(const float *audio, uint16_t n)
{
    if (!s_on) { return; }
    jtty_rx_mete(audio, (uint32_t)n);
}

void jtty_modo_borra(void)
{
    uint8_t i;

    s_n = 0U;
    s_atomos = 0UL;
    if (s_ram == (jtty_ram_t *)0) { return; }
    for (i = 0U; i < (uint8_t)JTTY_MODO_CUBOS; i++) { s_ram->cubo[i].usado = 0U; }
}

/* ------------------------------------------------------------ */

/*
 * LA REJILLA DE 118 FILAS. Ver jtty_modo.h para el porque; esto es solo la
 * cuenta, y esta suelta para que el banco la pueda probar sola.
 */
uint8_t jtty_modo_encadena(uint32_t fila_ant, uint32_t fila, uint8_t *saltos)
{
    uint32_t d;
    uint8_t k;

    if (fila <= fila_ant) { return 0U; }     /* ni la misma ni hacia atras */
    d = fila - fila_ant;

    for (k = 1U; k <= (uint8_t)JTTY_SALTOS_MAX; k++) {
        uint32_t esperada = (uint32_t)k * (uint32_t)JTTY_RX_FILAS_PASO;
        uint32_t dif = (d > esperada) ? (d - esperada) : (esperada - d);

        if (dif <= (uint32_t)JTTY_FASE_TOL) {
            if (saltos != (uint8_t *)0) { *saltos = (uint8_t)(k - 1U); }
            return 1U;
        }
    }
    return 0U;
}

/*
 * EL CUBO QUE CONTINUA ESTE ATOMO, o 0xFF si no hay ninguno.
 *
 * Hacen falta LAS DOS COSAS: la frecuencia -que dice que puede ser el
 * mismo transmisor- y la fase de la rejilla -que dice que lo es-. Con la
 * frecuencia sola, dos QSO en el mismo cubo de +-20 Hz salian pegados, que
 * es lo que se veia en 14.090.
 */
static uint8_t cubo_sigue(int16_t hz, uint32_t fila, uint8_t *saltos)
{
    uint8_t i;

    for (i = 0U; i < (uint8_t)JTTY_MODO_CUBOS; i++) {
        int16_t d;

        if (!s_ram->cubo[i].usado) { continue; }
        d = (int16_t)(s_ram->cubo[i].hz - hz);
        if (d < 0) { d = (int16_t)-d; }
        if (d > 20) { continue; }
        if (jtty_modo_encadena(s_ram->cubo[i].fila, fila, saltos)) { return i; }
    }
    return 0xFFU;
}

/*
 * UN CUBO PARA EMPEZAR UN MENSAJE: uno libre, y si no hay, el mas viejo
 * CERRADO ANTES.
 *
 * Lo de cerrarlo antes no es un detalle: la version anterior devolvia el
 * mas viejo y el que llamaba solo lo inicializaba si estaba sin usar, o
 * sea que con todos los cubos ocupados el atomo nuevo se pegaba al mensaje
 * de otra estacion. Ver JTTY_MODO_CUBOS.
 */
static uint8_t cubo_nuevo(void);

static void cierra(uint8_t i, uint8_t entero)
{
    if (!s_ram->cubo[i].usado) { return; }
    /* Los espacios de la derecha del mensaje entero sobran: los de un
     * TEXT5 de en medio no se quitan (ver jtty_msg.h) y el ultimo puede
     * acabar en varios. */
    jtty_msg_cierra(s_ram->cubo[i].txt);
    apunta(s_ram->cubo[i].txt, s_ram->cubo[i].hz, s_ram->cubo[i].q, entero);
    s_ram->cubo[i].usado = 0U;
}

static uint8_t cubo_nuevo(void)
{
    uint8_t i, viejo = 0U;
    uint32_t t_viejo = 0xFFFFFFFFUL;

    for (i = 0U; i < (uint8_t)JTTY_MODO_CUBOS; i++) {
        if (!s_ram->cubo[i].usado) { return i; }
        if (s_ram->cubo[i].ms < t_viejo) {
            t_viejo = s_ram->cubo[i].ms; viejo = i;
        }
    }
    /* Sale a pantalla con sus puntos suspensivos, que es lo que es: un
     * mensaje del que no se ha visto el final. */
    cierra(viejo, 0U);
    return viejo;
}

void jtty_modo_poll(void)
{
    jtty_rx_r_t r;
    uint32_t ahora;
    uint8_t i;

    if (!s_on || s_ram == (jtty_ram_t *)0) { return; }
    ahora = g_msticks;

    /*
     * COMO MUCHO DOS POR PASADA, y no la cola entera.
     *
     * Con el umbral en 0,45 entran unos cuatro candidatos por segundo -ver
     * JTTY_SYNC_UMBRAL- y cada uno es un Viterbi de 512 estados, que en
     * esta placa son un par de milisegundos. De media no es nada; en rafaga
     * si: seis seguidos en una sola pasada del bucle principal se comerian
     * la mitad de un cuadro y la pantalla daria un tiron.
     *
     * Con dos por pasada hay sitio para sesenta por segundo, quince veces
     * lo que llega, y el coste por pasada esta acotado.
     */
    for (i = 0U; i < 2U && jtty_rx_saca(&r); i++) {
        uint8_t saltos = 0U;
        uint8_t c = cubo_sigue(r.hz, r.fila, &saltos);

        s_atomos++;
        if (c == 0xFFU) {
            /* No continua nada de lo que hay abierto: empieza mensaje. */
            c = cubo_nuevo();
            s_ram->cubo[c].usado = 1U;
            s_ram->cubo[c].hz = r.hz;
            s_ram->cubo[c].n = 0U;
            s_ram->cubo[c].q = r.calidad;
            s_ram->cubo[c].txt[0] = '\0';
            s_ram->cubo[c].ant5 = 0U;
            saltos = 0U;
        }
        s_ram->cubo[c].ms = ahora;
        s_ram->cubo[c].fila = r.fila;
        if (s_ram->cubo[c].q > r.calidad) { s_ram->cubo[c].q = r.calidad; }

        /*
         * Los atomos se pegan con la regla de jtty_msg_pega(): dos TEXT5
         * seguidos SIN nada en medio -son trozos de cinco caracteres de la
         * misma cadena- y el resto con un espacio. Ver jtty_msg.h.
         */
        /*
         * Y si faltan tramas en medio, el hueco se dice. `saltos` lo da la
         * rejilla de 118 filas, no un temporizador: ver el comentario de
         * arriba.
         */
        if (saltos > 0U) {
            jtty_atomo_t hueco;

            memset(&hueco, 0, sizeof hueco);
            hueco.texto[0] = '.'; hueco.texto[1] = '.';
            hueco.texto[2] = '.'; hueco.texto[3] = '\0';
            jtty_msg_pega(s_ram->cubo[c].txt, (uint32_t)JTTY_MODO_LARGO,
                          &hueco, s_ram->cubo[c].ant5);
            s_ram->cubo[c].ant5 = 0U;   /* el hueco corta la cadena de TEXT5 */
        }
        jtty_msg_pega(s_ram->cubo[c].txt, (uint32_t)JTTY_MODO_LARGO,
                      &r.atomo, s_ram->cubo[c].ant5);
        s_ram->cubo[c].ant5 = r.atomo.texto5;
        s_ram->cubo[c].n++;

        /*
         * El bit de fin cierra el mensaje. Y dieciseis atomos tambien: el
         * estandar no deja mas, asi que un decimoseptimo quiere decir que
         * se ha perdido el final del anterior y ha empezado otro.
         */
        if (r.atomo.fin || s_ram->cubo[c].n >= 16U) { cierra(c, 1U); }
    }

    /* Y los que se han quedado a medias, pasado el plazo. */
    for (i = 0U; i < (uint8_t)JTTY_MODO_CUBOS; i++) {
        if (s_ram->cubo[i].usado
            && (ahora - s_ram->cubo[i].ms) > JTTY_ESPERA_MS) {
            cierra(i, 0U);
        }
    }
}

/* ------------------------------------------------------------ */

uint8_t jtty_modo_lineas(void) { return s_n; }

const char *jtty_modo_linea(uint8_t i)
{
    if (s_ram == (jtty_ram_t *)0 || i >= s_n) { return ""; }
    return s_ram->linea[i];
}

uint32_t jtty_modo_atomos(void) { return s_atomos; }

/*
 * LA CHAPA: cuantos atomos llevan. En FT8 la chapa es la cuenta atras de
 * la ranura, pero aqui NO HAY RANURA que contar - es justamente lo que
 * hace distinto a este modo-. Lo unico que dice "esto esta vivo" es que la
 * cuenta suba.
 */
const char *jtty_modo_estado_txt(void)
{
    uint32_t v = s_atomos;
    uint8_t i = 0U, k;
    char t[8];

    if (v > 9999UL) { v = 9999UL; }
    do { t[i++] = (char)('0' + (uint8_t)(v % 10UL)); v /= 10UL; } while (v != 0UL);
    for (k = 0U; k < i; k++) { s_chapa[k] = t[i - 1U - k]; }
    s_chapa[i] = '\0';
    return s_chapa;
}
