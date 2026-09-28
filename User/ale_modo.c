#include "ale_modo.h"
#include "ale_rx.h"
#include "hfdl_ram.h"
#include "waterfall.h"
#include "ft8_shared_ram.h"
#include <string.h>

/* Los preambulos, tal y como los numera el estandar. */
#define P_DATA 0U
#define P_THRU 1U
#define P_TO   2U
#define P_TWAS 3U
#define P_FROM 4U
#define P_TIS  5U
#define P_CMD  6U
#define P_REP  7U

static const char *const k_tipo[8] = {
    "DATA", "THRU", "TO", "TWAS", "FROM", "TIS", "CMD", "REP"
};

typedef struct {
    char     ind[16];      /* hasta quince caracteres y el cero */
    uint32_t khz;          /* donde se le oyo la ultima vez */
    uint16_t n;            /* veces que ha aparecido */
    uint8_t  tipo;         /* el preambulo con el que abre */
    uint8_t  cal;          /* votos unanimes de la ultima, 0..48 */
    uint32_t visto;
} estacion_t;

typedef struct {
    estacion_t est[ALE_MODO_ESTACIONES];
    char       lin[ALE_MODO_LINEAS][ALE_MODO_LARGO];
} ale_ram_t;

_Static_assert(sizeof(ale_ram_t) <= sizeof(ft8_shared_ram_t),
               "la tabla de ALE no cabe en la memoria de la cascada");

static ale_ram_t *s_ram;
static uint8_t    s_on;
static uint8_t    s_nest;
static uint32_t   s_reloj;
static uint32_t   s_palabras;
static uint32_t   s_khz;
static uint8_t    s_lin_vale;

/* La secuencia que se esta juntando ahora mismo. */
static char       s_seq[16];
static uint8_t    s_seq_n;
static uint8_t    s_seq_tipo;
static uint8_t    s_seq_cal;
static uint8_t    s_seq_hay;
static uint8_t    s_sin_nada;   /* palabras seguidas sin extension */

#define s_est (s_ram->est)
#define s_lin (s_ram->lin)

uint8_t  ale_modo_activo(void)     { return s_on; }
uint32_t ale_modo_palabras(void)   { return s_palabras; }
uint8_t  ale_modo_estaciones(void) { return s_nest; }
void     ale_modo_frec_pon(uint32_t khz) { s_khz = khz; }

uint8_t ale_modo_senal(void)
{
    ale_rx_info_t i;

    ale_rx_info(&i);
    return i.nivel;
}

uint8_t ale_modo_enganchado(void)
{
    ale_rx_info_t i;

    ale_rx_info(&i);
    return i.enganchado;
}

uint8_t ale_modo_calidad(void)
{
    ale_rx_info_t i;

    ale_rx_info(&i);
    return i.calidad;
}

void ale_modo_borra(void)
{
    s_nest = 0U;
    s_palabras = 0U;
    s_reloj = 0U;
    s_seq_n = 0U;
    s_seq_hay = 0U;
    s_lin_vale = 0U;
    ale_rx_cuentas_cero();
}

uint8_t ale_modo_start(void)
{
    uint8_t *ram;
    uint32_t cap = 0UL;

    if (s_on) { return 1U; }
    if (waterfall_prestada()) { return 0U; }

    hfdl_ram_coge();
    ram = hfdl_ram_todo(&cap);
    if ((ram == (uint8_t *)0) || (cap < (uint32_t)sizeof(ale_ram_t))) {
        hfdl_ram_suelta();
        return 0U;
    }
    s_ram = (ale_ram_t *)(void *)ram;
    memset(s_ram, 0, sizeof *s_ram);

    ale_modo_borra();
    /*
     * 12 kHz: la tasa del camino de banda lateral ya diezmado, la misma que
     * comen FT8 y WSPR. A esa tasa un simbolo de ALE son 96 muestras
     * exactas y los ocho tonos caen en bins exactos de esa ventana.
     */
    ale_rx_start(12000.0f);
    s_on = 1U;
    return 1U;
}

void ale_modo_stop(void)
{
    if (!s_on) { return; }
    ale_rx_stop();
    s_on = 0U;
    s_ram = (ale_ram_t *)0;
    s_nest = 0U;
    hfdl_ram_suelta();
}

void ale_modo_mete(const float *audio, uint16_t n)
{
    if (!s_on) { return; }
    ale_rx_mete(audio, (uint32_t)n);
}

/* ------------------------------------------------------------ la tabla */
static void guarda(void)
{
    uint8_t i, hueco;

    if (!s_seq_hay || (s_seq_n == 0U) || (s_ram == (ale_ram_t *)0)) {
        s_seq_hay = 0U; s_seq_n = 0U;
        return;
    }
    s_seq[s_seq_n] = '\0';

    /*
     * FUERA EL RELLENO. Las direcciones se trocean de tres en tres y el
     * ultimo trozo se rellena con '@': "MIAMI" viaja como "MIA" + "MI@".
     * Sin quitarlo, la tabla ensena "MIAMI@" y dos estaciones que son la
     * misma salen como dos.
     */
    while ((s_seq_n > 0U) && (s_seq[s_seq_n - 1U] == '@')) {
        s_seq_n--;
        s_seq[s_seq_n] = '\0';
    }
    if (s_seq_n == 0U) { s_seq_hay = 0U; return; }

    for (i = 0U; i < s_nest; i++) {
        if (strcmp(s_est[i].ind, s_seq) == 0) {
            s_est[i].n = (uint16_t)((s_est[i].n < 0xFFFFU) ?
                                    (s_est[i].n + 1U) : s_est[i].n);
            s_est[i].cal = s_seq_cal;
            s_est[i].khz = s_khz;
            s_est[i].tipo = s_seq_tipo;
            s_reloj++;
            s_est[i].visto = s_reloj;
            s_lin_vale = 0U;
            s_seq_hay = 0U; s_seq_n = 0U;
            return;
        }
    }

    if (s_nest < (uint8_t)ALE_MODO_ESTACIONES) {
        hueco = s_nest;
        s_nest++;
    } else {
        hueco = 0U;
        for (i = 1U; i < s_nest; i++) {
            if (s_est[i].visto < s_est[hueco].visto) { hueco = i; }
        }
    }
    memset(&s_est[hueco], 0, sizeof s_est[hueco]);
    memcpy(s_est[hueco].ind, s_seq, (size_t)(s_seq_n + 1U));
    s_est[hueco].n = 1U;
    s_est[hueco].cal = s_seq_cal;
    s_est[hueco].khz = s_khz;
    s_est[hueco].tipo = s_seq_tipo;
    s_reloj++;
    s_est[hueco].visto = s_reloj;

    s_lin_vale = 0U;
    s_seq_hay = 0U;
    s_seq_n = 0U;
}

static void mete_chars(uint32_t pal)
{
    uint8_t i;

    for (i = 0U; i < 3U; i++) {
        char c = (char)((pal >> (14U - (7U * i))) & 0x7FU);

        if (s_seq_n < 15U) { s_seq[s_seq_n++] = c; }
    }
}

static void palabra(uint32_t pal, uint8_t cal)
{
    uint8_t pre = (uint8_t)((pal >> 21) & 7U);

    s_palabras++;

    if ((pre == P_TO) || (pre == P_TWAS) || (pre == P_FROM) || (pre == P_TIS)) {
        /* Abre direccion: cierra la anterior y empieza otra. */
        guarda();
        s_seq_n = 0U;
        s_seq_tipo = pre;
        s_seq_cal = cal;
        s_seq_hay = 1U;
        s_sin_nada = 0U;
        mete_chars(pal);
        return;
    }

    if ((pre == P_DATA) || (pre == P_REP)) {
        /* Extiende la de antes, si la hay. Si no, no es nada: una
         * extension suelta sin su ancla no dice de quien es. */
        if (s_seq_hay) {
            if (cal < s_seq_cal) { s_seq_cal = cal; }
            mete_chars(pal);
            s_sin_nada = 0U;
        }
        return;
    }

    /*
     * THRU y CMD no extienden una direccion, asi que cierran la que
     * hubiera. Una secuencia se cierra tambien cuando pasan dos palabras
     * sin extenderla: el sondeo se repite, y sin cerrar se irian pegando
     * copias del mismo indicativo una detras de otra.
     */
    guarda();
    s_sin_nada = 0U;
}

void ale_modo_poll(void)
{
    uint32_t pal;
    uint8_t  cal, k;

    if (!s_on) { return; }

    for (k = 0U; k < 4U; k++) {
        if (!ale_rx_saca(&pal, &cal)) { break; }
        palabra(pal, cal);
    }

    /*
     * Cerrar por silencio. Una palabra dura 392 ms; si pasan dos sin que
     * llegue nada, lo que se estuviera juntando ya no va a crecer mas y hay
     * que apuntarlo. Sin esto, un indicativo se queda a medias en la
     * pantalla hasta la siguiente emision.
     */
    if (s_seq_hay) {
        s_sin_nada++;
        if (s_sin_nada > 200U) { guarda(); s_sin_nada = 0U; }
    }
}

/* ---------------------------------------------------------- renglones */
static uint8_t pon_u(char *d, uint32_t v)
{
    char t[12];
    uint8_t n = 0U, i;

    do { t[n++] = (char)('0' + (v % 10U)); v /= 10U; } while (v != 0U);
    for (i = 0U; i < n; i++) { d[i] = t[n - 1U - i]; }
    return n;
}

static uint8_t cuantas(void)
{
    return (s_nest > (uint8_t)ALE_MODO_LINEAS) ? (uint8_t)ALE_MODO_LINEAS
                                               : s_nest;
}

static void monta(void)
{
    uint8_t k, n = cuantas();

    if (s_ram == (ale_ram_t *)0) { return; }

    for (k = 0U; k < n; k++) {
        const estacion_t *e = &s_est[k];
        char *l = s_lin[k];
        uint8_t p = 0U, c;

        for (c = 0U; (e->ind[c] != '\0') && (c < 15U); c++) { l[p++] = e->ind[c]; }
        l[p++] = '\t';

        for (c = 0U; k_tipo[e->tipo & 7U][c] != '\0'; c++) {
            l[p++] = k_tipo[e->tipo & 7U][c];
        }
        l[p++] = '\t';

        if (e->khz >= 1000U) {
            p += pon_u(&l[p], e->khz / 1000U);
            l[p++] = '.';
            l[p++] = (char)('0' + ((e->khz / 100U) % 10U));
            l[p++] = (char)('0' + ((e->khz / 10U) % 10U));
            l[p++] = (char)('0' + (e->khz % 10U));
        } else {
            p += pon_u(&l[p], e->khz);
        }
        l[p++] = '\t';

        p += pon_u(&l[p], e->cal);
        l[p++] = '\t';

        p += pon_u(&l[p], e->n);
        l[p] = '\0';
    }
    s_lin_vale = 1U;
}

uint32_t ale_modo_lineas(void) { return (uint32_t)cuantas(); }

const char *ale_modo_linea(uint32_t i)
{
    if (s_ram == (ale_ram_t *)0) { return ""; }
    if (i >= (uint32_t)cuantas()) { return ""; }
    if (!s_lin_vale) { monta(); }
    return s_lin[i];
}
