/*
 * Paquete AX.25 / APRS. Ver ax25.h para el porque de cada pieza.
 */
#include "ax25.h"
#include "nco.h"
#include <string.h>

#ifdef __GNUC__
#define TCMRAM_BSS __attribute__((section(".tcmram")))
#else
#define TCMRAM_BSS
#endif

/* Muestras por bit, como mucho. A 96 kHz y 1200 baudios son 80. */
#define SPB_MAX 96U

/* ==========================================================================
 * LA COMPROBACION (FCS)
 * ==========================================================================
 *
 * Es el CRC-16 del X.25: polinomio 0x1021 al reves (0x8408), empieza en
 * 0xFFFF y se procesa bit a bit empezando por el de menos peso, que es el
 * orden en que viajan los bits por la radio.
 *
 * El receptor NO compara dos numeros: pasa por el molino la trama ENTERA,
 * los dos bytes de comprobacion incluidos, y lo que tiene que salir es una
 * constante, 0xF0B8. Es la misma cuenta escrita de la forma que no necesita
 * separar la trama de su comprobacion, o sea una forma menos de equivocarse.
 */
#define FCS_BUENO 0xF0B8U

uint16_t ax25_fcs(const uint8_t *d, uint16_t n)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t  b;

    for (i = 0U; i < n; i++) {
        uint8_t v = d[i];
        for (b = 0U; b < 8U; b++) {
            uint16_t x = (uint16_t)((crc ^ v) & 1U);
            crc >>= 1;
            if (x) { crc ^= 0x8408U; }
            v = (uint8_t)(v >> 1);
        }
    }
    return crc;
}

/* ==========================================================================
 * ESTADO
 * ========================================================================== */
static uint8_t  s_on;
static float    s_fs;

/* --- 1. los dos tonos --- */
static nco_t    s_nco_m, s_nco_e;
static TCMRAM_BSS float s_mi[SPB_MAX], s_mq[SPB_MAX];
static TCMRAM_BSS float s_ei[SPB_MAX], s_eq[SPB_MAX];
static uint16_t s_hidx, s_spb, s_llenado;
static float    s_smi, s_smq, s_sei, s_seq;
static float    s_nivel;

/* --- 2. el reloj de bit --- */
/*
 * UN LAZO DE PRIMER ORDEN, SIN INTEGRADOR DE FRECUENCIA.
 *
 * La primera version copio el lazo del NAVTEX: detector de Gardner mas un
 * integrador que persigue el error de velocidad del transmisor. En el NAVTEX
 * eso es lo correcto, porque la emisora manda sin parar y el lazo tiene todo
 * el tiempo del mundo para asentarse. Aqui no: el paquete va a RAFAGAS, con
 * silencio entre trama y trama, y un integrador que corre suelto durante el
 * silencio se pasea. Medido: con un transmisor PERFECTO, el lazo se iba a
 * +2500 ppm despues de dos tramas y la tercera y la cuarta ya no pasaban la
 * comprobacion.
 *
 * Asi que se hace lo que hace cualquier controlador de paquete: no estimar la
 * velocidad, solo corregir la FASE en cada cruce por cero. La velocidad se da
 * por sabida -1200 baudios son 1200 baudios- y lo unico que se persigue es
 * donde cae el borde. Sin integrador no hay nada que se pueda pasear, y el
 * relleno de bits del HDLC garantiza un cruce cada seis bits como mucho, o
 * sea que nunca esta mucho rato a ciegas.
 *
 * La ganancia es un cuarto del error: medida contra el banco, es la que
 * aguanta mas ruido sin perder tramas seguidas.
 */
#define DPLL_G 2            /* desplazamiento: la correccion es err >> 2 */
static uint32_t s_fase, s_inc;
static uint8_t  s_sig_ant;

/* --- 3. NRZI y HDLC --- */
static uint8_t  s_nrzi_ant;
static uint8_t  s_hay_ant;
static uint8_t  s_reg;        /* los ultimos 8 bits crudos, para las banderas */
static uint8_t  s_unos;
static uint8_t  s_en_trama;
static uint8_t  s_byte;
static uint8_t  s_bbits;
static uint16_t s_n;
static uint8_t  s_trama[AX25_TRAMA_MAX];
static uint32_t s_ult_bandera;
static uint32_t s_muestra;

/* --- 4. salida --- */
#define RING_N 512U
static char     s_ring[RING_N];
static uint16_t s_cab, s_col;

static ax25_info_t s_info;

/* ==========================================================================
 * ARRANQUE
 * ========================================================================== */
void ax25_start(float fs_hz)
{
    uint32_t spb;

    if (fs_hz <= 0.0f) { return; }
    s_fs = fs_hz;

    spb = (uint32_t)(fs_hz / AX25_BAUD + 0.5f);
    if (spb < 8U)      { spb = 8U; }
    if (spb > SPB_MAX) { spb = SPB_MAX; }
    s_spb = (uint16_t)spb;

    nco_init();
    nco_fase_cero(&s_nco_m);
    nco_fase_cero(&s_nco_e);
    nco_freq(&s_nco_m, AX25_MARCA_HZ, fs_hz);
    nco_freq(&s_nco_e, AX25_ESPACIO_HZ, fs_hz);

    memset(s_mi, 0, sizeof s_mi); memset(s_mq, 0, sizeof s_mq);
    memset(s_ei, 0, sizeof s_ei); memset(s_eq, 0, sizeof s_eq);
    s_hidx = 0U; s_llenado = 0U;
    s_smi = s_smq = s_sei = s_seq = 0.0f;
    s_nivel = 1e-6f;

    s_fase = 0U;
    s_inc = (uint32_t)((double)AX25_BAUD / (double)fs_hz * 4294967296.0 + 0.5);
    s_sig_ant = 0U;

    s_nrzi_ant = 0U; s_hay_ant = 0U;
    s_reg = 0U; s_unos = 0U; s_en_trama = 0U;
    s_byte = 0U; s_bbits = 0U; s_n = 0U;
    s_ult_bandera = 0UL; s_muestra = 0UL;

    s_cab = s_col = 0U;
    memset(&s_info, 0, sizeof s_info);
    s_on = 1U;
}

void ax25_stop(void)      { s_on = 0U; }
uint8_t ax25_activo(void) { return s_on; }

/* ==========================================================================
 * SALIDA
 * ========================================================================== */
static void push(char c)
{
    uint16_t sig = (uint16_t)((s_cab + 1U) % RING_N);
    if (sig == s_col) { return; }
    s_ring[s_cab] = c;
    s_cab = sig;
}

uint8_t ax25_get_char(char *out)
{
    if (s_col == s_cab) { return 0U; }
    if (out) { *out = s_ring[s_col]; }
    s_col = (uint16_t)((s_col + 1U) % RING_N);
    return 1U;
}

void ax25_info(ax25_info_t *out)
{
    if (!out) { return; }
    *out = s_info;
    /* "Enganchado" es haber visto una bandera hace menos de diez segundos.
     * En paquete no hay portadora continua: entre trama y trama no hay NADA,
     * asi que "esta llegando señal" no es una pregunta que se pueda contestar
     * mirando el instante de ahora. */
    out->enganchado = (uint8_t)(s_on && s_ult_bandera != 0UL
                                && (s_muestra - s_ult_bandera) < (uint32_t)(10.0f * s_fs));
    out->nivel = s_nivel;
}

void ax25_lazo_dbg(float *fase, float *nivel)
{
    if (fase)  { *fase = (float)s_fase / 4294967296.0f; }
    if (nivel) { *nivel = s_nivel; }
}

/* ==========================================================================
 * FORMATEAR UNA TRAMA
 * ==========================================================================
 *
 * Las direcciones van con cada letra CORRIDA UN BIT a la izquierda, que es
 * como AX.25 se guarda un bit por byte para marcar donde acaba la lista. Asi
 * que hay que devolverlas a su sitio antes de poder leerlas.
 */
static uint16_t dir_saca(const uint8_t *d, char *out)
{
    uint8_t i, ssid, n = 0U;

    for (i = 0U; i < 6U; i++) {
        char c = (char)(d[i] >> 1);
        if (c != ' ' && c != '\0') { out[n++] = c; }
    }
    ssid = (uint8_t)((d[6] >> 1) & 0x0FU);
    if (ssid != 0U) {
        out[n++] = '-';
        if (ssid >= 10U) { out[n++] = '1'; ssid = (uint8_t)(ssid - 10U); }
        out[n++] = (char)('0' + ssid);
    }
    out[n] = '\0';
    return (uint16_t)(d[6] & 1U);   /* 1 = esta es la ultima direccion */
}

static void trama_texto(const uint8_t *d, uint16_t n)
{
    char dest[12], orig[12], via[12];
    uint16_t p = 0U;
    uint8_t ultima;
    const char *q;

    if (n < 14U) { return; }

    ultima = (uint8_t)dir_saca(&d[0], dest);   p = 7U;
    (void)ultima;
    ultima = (uint8_t)dir_saca(&d[7], orig);   p = 14U;

    for (q = orig; *q != '\0'; q++) { push(*q); }
    push('>');
    for (q = dest; *q != '\0'; q++) { push(*q); }

    /* Los repetidores por los que ha pasado, hasta ocho. */
    while (!ultima && p + 7U <= n && p < 7U * 10U) {
        ultima = (uint8_t)dir_saca(&d[p], via);
        p = (uint16_t)(p + 7U);
        push(',');
        for (q = via; *q != '\0'; q++) { push(*q); }
    }

    push(':');

    /*
     * Y los datos. Se saltan el byte de control y el de protocolo: son
     * maquinaria del enlace, no informacion, y lo que se quiere leer en
     * pantalla es lo que la estacion ha querido decir.
     *
     * Los dos ultimos bytes son la comprobacion y tampoco se enseñan.
     */
    if (p + 2U < n) {
        uint16_t i;
        p = (uint16_t)(p + 2U);
        for (i = p; i + 2U < n; i++) {
            uint8_t c = d[i];
            /* Lo que no sea imprimible sale como un punto: una trama con un
             * byte raro no puede descolocar la rejilla de texto. */
            push((c >= 32U && c < 127U) ? (char)c : '.');
        }
    }
    push('\n');
}

void ax25_trama_dbg(const uint8_t *d, uint16_t n) { trama_texto(d, n); }

/* ==========================================================================
 * HDLC
 * ========================================================================== */
static void trama_fin(void)
{
    if (s_n >= AX25_TRAMA_MIN) {
        if (ax25_fcs(s_trama, s_n) == FCS_BUENO) {
            s_info.tramas_ok++;
            trama_texto(s_trama, s_n);
        } else {
            s_info.tramas_mal++;
        }
    }
    s_n = 0U; s_byte = 0U; s_bbits = 0U;
}

static void hdlc_bit(uint8_t bit)
{
    /* Los bits viajan empezando por el de menos peso, asi que el registro se
     * llena por arriba y se desplaza hacia abajo. */
    s_reg = (uint8_t)((s_reg >> 1) | (bit ? 0x80U : 0U));

    if (s_reg == 0x7EU) {
        /* Bandera. Cierra lo que hubiera y abre lo siguiente. */
        s_info.banderas++;
        s_ult_bandera = s_muestra;
        trama_fin();
        s_en_trama = 1U;
        s_unos = 0U;
        s_bbits = 0U;
        return;
    }

    /*
     * Quitar el relleno. Despues de cinco unos el transmisor mete un cero a
     * la fuerza para que nunca puedan salir seis seguidos dentro de los
     * datos - que es lo que hace que la bandera sea reconocible sin
     * ambiguedad-. Ese cero no es informacion y se tira.
     */
    if (bit == 0U && s_unos == 5U) {
        s_unos = 0U;
        return;
    }
    s_unos = bit ? (uint8_t)(s_unos + 1U) : 0U;

    /* Siete unos seguidos es un aborto: la estacion ha cancelado la trama. */
    if (s_unos >= 7U) {
        s_en_trama = 0U;
        s_n = 0U; s_bbits = 0U; s_unos = 0U;
        return;
    }

    if (!s_en_trama) { return; }

    s_byte = (uint8_t)((s_byte >> 1) | (bit ? 0x80U : 0U));
    s_bbits++;
    if (s_bbits >= 8U) {
        s_bbits = 0U;
        if (s_n < AX25_TRAMA_MAX) {
            s_trama[s_n] = s_byte;
            s_n++;
        } else {
            /* Mas larga de lo que puede ser una trama: no lo es. */
            s_en_trama = 0U;
            s_n = 0U;
        }
        s_byte = 0U;
    }
}

/* ==========================================================================
 * NRZI
 * ==========================================================================
 *
 * Lo que significa un bit no es su nivel sino si ha CAMBIADO respecto al
 * anterior: un cero es un cambio y un uno es quedarse igual.
 *
 * Eso tiene una consecuencia practica que se agradece: si el receptor oye los
 * dos tonos al reves -algo que pasa segun por donde se sintonice-, TODOS los
 * bits salen invertidos, y al comparar cada uno con el anterior la inversion
 * se cancela sola. Por eso aqui no hace falta el conmutador de polaridad que
 * si necesita el NAVTEX.
 */
static void nrzi_bit(uint8_t crudo)
{
    if (!s_hay_ant) {
        s_nrzi_ant = crudo;
        s_hay_ant = 1U;
        return;
    }
    hdlc_bit((uint8_t)(crudo == s_nrzi_ant));
    s_nrzi_ant = crudo;
}

/* ==========================================================================
 * EL RELOJ DE BIT
 * ========================================================================== */
static void discriminador(float d)
{
    uint32_t fase_ant = s_fase;
    uint8_t  sig = (uint8_t)(d > 0.0f);

    s_fase += s_inc;

    /*
     * La vuelta del acumulador es donde se DECIDE, y se comprueba antes de
     * corregir para que una correccion no pueda inventarse un bit de mas.
     *
     * Por que ahi y no a mitad: la media movil es CAUSAL, o sea que su valor
     * resume el ultimo bit y no el bit centrado en ahora. En la vuelta la
     * ventana cubre exactamente un bit. Esto mismo, escrito al reves, costo
     * una tarde en el NAVTEX.
     */
    if (s_fase < fase_ant) {
        nrzi_bit(sig);
    }

    /*
     * Y un cruce por cero es una referencia de reloj. Por el mismo retraso de
     * media ventana, un cambio de bit no se ve en el borde sino en MITAD del
     * siguiente, asi que hacia ahi es hacia donde se tira.
     *
     * El error se mide con signo sobre el acumulador: la resta da un numero
     * que ya sale positivo o negativo segun por que lado se haya pasado, sin
     * ningun caso aparte para la vuelta por cero. Esa es toda la gracia de
     * usar un acumulador de fase en vez de un contador.
     */
    if (sig != s_sig_ant) {
        int32_t err = (int32_t)(s_fase - 0x80000000UL);
        s_fase = (uint32_t)((int32_t)s_fase - (err >> DPLL_G));
        s_sig_ant = sig;
    }
}

/* ==========================================================================
 * LA ENTRADA
 * ========================================================================== */
void ax25_process(const float *audio, uint32_t n)
{
    uint32_t k;

    if (!s_on || !audio) { return; }

    for (k = 0U; k < n; k++) {
        float x = audio[k];
        float sn, cs, mi, mq, ei, eq, pm, pe;

        s_muestra++;

        nco_paso(&s_nco_m, &sn, &cs);
        mi = x * cs;  mq = -x * sn;
        nco_paso(&s_nco_e, &sn, &cs);
        ei = x * cs;  eq = -x * sn;

        s_smi += mi - s_mi[s_hidx];  s_mi[s_hidx] = mi;
        s_smq += mq - s_mq[s_hidx];  s_mq[s_hidx] = mq;
        s_sei += ei - s_ei[s_hidx];  s_ei[s_hidx] = ei;
        s_seq += eq - s_eq[s_hidx];  s_eq[s_hidx] = eq;
        s_hidx++;
        if (s_hidx >= s_spb) { s_hidx = 0U; }

        if (s_llenado < s_spb) { s_llenado++; continue; }

        pm = s_smi * s_smi + s_smq * s_smq;
        pe = s_sei * s_sei + s_seq * s_seq;

        s_nivel += 0.0005f * ((pm + pe) - s_nivel);
        if (s_nivel < 1e-12f) { s_nivel = 1e-12f; }

        discriminador(pm - pe);
    }
}
