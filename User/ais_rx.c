#include "ais_rx.h"
#include <string.h>

/* ==========================================================================
 * EL CRC, QUE ES EL MISMO DEL AX.25
 * ==========================================================================
 *
 * AIS viaja dentro de HDLC y usa el mismo FCS del X.25: polinomio 0x1021 al
 * reves (0x8408), empezando en 0xFFFF y de menos a mas peso, que es el
 * orden en que los bits salen al aire.
 *
 * SE CALCULA AL CERRAR LA TRAMA, no bit a bit segun llegan. La primera
 * version lo hacia al vuelo y estaba MAL por una razon que no se ve hasta
 * que se mira: ver los SIETE BITS DE MAS en trama_fin().
 *
 * Y no se comparan dos numeros: se pasa por el molino la trama ENTERA, los
 * dos bytes de comprobacion incluidos, y lo que tiene que salir es la
 * constante 0xF0B8. Una forma menos de equivocarse.
 */
#define FCS_BUENO 0xF0B8U

static uint16_t fcs_bits(const uint8_t *b, uint16_t n)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;

    for (i = 0U; i < n; i++) {
        uint16_t bit = (uint16_t)((b[i >> 3] >> (7U - (i & 7U))) & 1U);
        uint16_t x = (uint16_t)((crc ^ bit) & 1U);

        crc >>= 1;
        if (x != 0U) { crc ^= 0x8408U; }
    }
    return crc;
}

/* ==========================================================================
 * ESTADO
 * ========================================================================== */
static uint8_t  s_on;
static float    s_fs;

/* Continua del discriminador: el desvio de sintonia sumado al de la
 * portadora del barco. Ver quita_continua(). */
static float    s_dc;
static float    s_dc_a;

/* Media movil de un bit: el filtro adaptado, barato. */
#define SPB_MAX 16U
static float    s_vent[SPB_MAX];
static float    s_suma;
static uint8_t  s_vi, s_spb, s_lleno;

/* Lazo de reloj: acumulador de fase de 32 bits. */
static uint32_t s_fase, s_inc;
static uint8_t  s_sig_ant;

/* NRZI */
static uint8_t  s_nrzi_ant, s_hay_ant;

/* HDLC */
static uint8_t  s_reg;        /* los ultimos ocho bits crudos, para la bandera */
static uint8_t  s_unos;
static uint8_t  s_en_trama;
static uint16_t s_nbits;
static uint8_t  s_bits[AIS_BYTES_MAX];

/*
 * Anillo de tramas cerradas, que llena la interrupcion y vacia el bucle.
 *
 * DOS HUECOS BASTAN, y no es apurar: una ranura de AIS dura 26,7 ms y el
 * bucle principal da una vuelta cada 16, asi que para que se llenara harian
 * falta tres rafagas seguidas sin que el bucle llegue a pasar ni una vez.
 * Cada hueco son 58 bytes de una SRAM que va al 97%.
 */
#define ANILLO_N 2U
typedef struct {
    uint8_t  bits[AIS_BYTES_MAX];
    uint16_t nbits;
} trama_t;
static trama_t  s_anillo[ANILLO_N];
static volatile uint8_t s_cab, s_col;

static ais_rx_info_t s_info;
static float    s_nivel;

/* ==========================================================================
 * ARRANQUE
 * ========================================================================== */
#define AIS_BAUD 9600.0f

void ais_rx_start(float fs_hz)
{
    uint32_t spb;

    memset(s_vent, 0, sizeof s_vent);
    memset(&s_info, 0, sizeof s_info);
    s_suma = 0.0f; s_vi = 0U; s_lleno = 0U;
    s_dc = 0.0f;
    s_fase = 0U; s_sig_ant = 0U;
    s_nrzi_ant = 0U; s_hay_ant = 0U;
    s_reg = 0U; s_unos = 0U; s_en_trama = 0U; s_nbits = 0U;
    s_cab = 0U; s_col = 0U;
    s_nivel = 0.0f;

    s_fs = (fs_hz > 1.0f) ? fs_hz : 96000.0f;

    /*
     * Muestras por bit. A 96 kHz salen 10 exactas; el redondeo es la red por
     * si algun dia la cadena entrega otra tasa. Si saliera mas de SPB_MAX la
     * media movil se quedaria corta, asi que se recorta - y entonces el modo
     * no decodificara, pero no escribira fuera de su array.
     */
    spb = (uint32_t)((s_fs / AIS_BAUD) + 0.5f);
    if (spb < 2U) { spb = 2U; }
    if (spb > SPB_MAX) { spb = SPB_MAX; }
    s_spb = (uint8_t)spb;

    /*
     * El incremento del acumulador: una vuelta entera por bit. 2^32 / spb,
     * escrito para que no haga falta un entero de 64 bits.
     */
    s_inc = (uint32_t)(0xFFFFFFFFUL / (uint32_t)s_spb) + 1U;

    /*
     * LA CONSTANTE DE TIEMPO DE LA CONTINUA: 32 BITS, Y MEDIDA.
     *
     * Por arriba la limita el HDLC: el relleno garantiza que nunca hay mas
     * de cinco unos seguidos en los datos, y la bandera tiene seis; en NRZI
     * un uno es "no cambies", asi que lo mas que puede estar la senal quieta
     * en un nivel son siete periodos de bit. Con 32 de constante, siete no
     * la arrastran.
     *
     * Por abajo la limita el arranque de la rafaga. AIS empieza de golpe:
     * hasta ese momento el discriminador da ruido centrado en cero, y al
     * entrar la portadora del barco la continua salta al desvio de
     * frecuencia de ESE transmisor. El preambulo son 24 bits, o sea que la
     * constante tiene que llegar en menos de eso.
     *
     * SE PROBO PONER UN SEGUIDOR RAPIDO FUERA DE LA TRAMA Y LENTO DENTRO.
     * Funciona -aguanta desvios de tres veces la desviacion en vez de 0,8-
     * pero CUESTA SENSIBILIDAD, y se midio cuanto (sim/ais_aire.c, diez
     * semillas por punto):
     *
     *                         sigma 0,2   0,3   0,4   0,5
     *     solo lenta (32)        10/10 10/10  8/10  4/10
     *     rapida de 12 bits      10/10  8/10  4/10  3/10
     *     rapida de 8 bits        7/10  5/10  3/10  2/10
     *
     * La rapida se come parte del preambulo -que es una onda cuadrada a
     * medio bit- y al restarla encoge el ojo. Y el margen que compra no
     * hace falta: un transmisor de AIS en regla se va unos 500 Hz, o sea
     * 0,2 veces la desviacion, y la lenta sola aguanta 0,8. Se paga
     * sensibilidad por un margen que nadie va a usar.
     *
     * Asi que queda una sola, lenta. Si algun dia aparece un transmisor muy
     * corrido, el sintoma sera que ESE barco no sale y los demas si, y la
     * respuesta esta escrita aqui.
     */
    s_dc_a = 1.0f / (32.0f * (float)s_spb);

    s_on = 1U;
}

void ais_rx_stop(void)      { s_on = 0U; }
uint8_t ais_rx_activo(void) { return s_on; }

void ais_rx_info(ais_rx_info_t *out)
{
    if (out == (ais_rx_info_t *)0) { return; }
    *out = s_info;
    out->nivel = (uint8_t)((s_nivel > 99.0f) ? 99.0f : s_nivel);
}

void ais_rx_cuentas_cero(void)
{
    s_info.banderas = 0U; s_info.tramas = 0U;
    s_info.buenas = 0U;   s_info.largas = 0U;
}

/* ==========================================================================
 * HDLC
 * ========================================================================== */
/*
 * LOS SIETE BITS DE MAS. 27/09/2026.
 *
 * La bandera de cierre se reconoce cuando el registro de los ultimos ocho
 * bits crudos vale 0x7E. Pero para entonces los SIETE PRIMEROS de esa
 * bandera -un cero y seis unos- ya han pasado por aqui y se han guardado
 * como si fueran datos. No hay forma de evitarlo mirando solo hacia atras:
 * hasta que llega el octavo bit, esos siete son indistinguibles de datos.
 *
 * El relleno no se lleva ninguno por delante (el destuffer solo tira un
 * CERO detras de cinco unos, y en la bandera detras de los cinco unos viene
 * un uno), asi que siempre sobran exactamente siete. Ni uno mas ni uno
 * menos, en todos los casos.
 *
 * POR QUE EL AX.25 NO TIENE ESTE PROBLEMA y este si: ax25.c monta BYTES y
 * solo escribe cuando junta ocho bits. Los siete de la bandera se quedan a
 * medias en el byte en curso y no llegan nunca al buffer. Se le cae solo,
 * por accidente. Aqui se guarda bit a bit -porque AIS tiene campos que no
 * estan alineados a byte- y el accidente no ocurre.
 *
 * Costo una tarde: la trama salia de 191 bits en vez de 184 y el CRC nunca
 * cuadraba. Con un CRC de 16 bits, "no cuadra nunca" y "no hay senal" se
 * ven exactamente igual.
 */
#define BANDERA_COLA 7U

static void trama_fin(void)
{
    uint16_t n, util;

    if (!s_en_trama) { return; }
    s_en_trama = 0U;

    if (s_nbits < BANDERA_COLA) { s_nbits = 0U; return; }
    n = (uint16_t)(s_nbits - BANDERA_COLA);
    s_nbits = 0U;

    /*
     * Lo minimo que puede ser una trama de AIS son 168 bits de mensaje mas
     * 16 de comprobacion. Por debajo de eso no es una trama corta: es dos
     * banderas seguidas, que es lo que sale entre rafagas.
     */
    if (n < (168U + 16U)) { return; }

    s_info.tramas++;

    if (fcs_bits(s_bits, n) != FCS_BUENO) { return; }

    s_info.buenas++;
    util = (uint16_t)(n - 16U);   /* fuera los del CRC */

    {
        uint8_t sig = (uint8_t)((s_cab + 1U) % ANILLO_N);

        /*
         * Si el bucle principal no ha vaciado, se TIRA LA NUEVA y no se
         * pisa la vieja. Con cuatro huecos y una trama cada cuatro
         * milisegundos como mucho, esto no deberia pasar nunca; si pasara,
         * perder la ultima es menos malo que entregar media.
         */
        if (sig != s_col) {
            memcpy(s_anillo[s_cab].bits, s_bits, sizeof s_bits);
            s_anillo[s_cab].nbits = util;
            s_cab = sig;
        }
    }
}

static void hdlc_bit(uint8_t bit)
{
    /* Los bits salen al aire empezando por el de menos peso, asi que el
     * registro de banderas se llena por arriba y se desplaza hacia abajo. */
    s_reg = (uint8_t)((s_reg >> 1) | (bit ? 0x80U : 0U));

    if (s_reg == 0x7EU) {
        s_info.banderas++;
        trama_fin();
        s_en_trama = 1U;
        s_unos = 0U;
        s_nbits = 0U;
        return;
    }

    /* El cero que el transmisor mete a la fuerza tras cinco unos no es
     * informacion: no se guarda. */
    if ((bit == 0U) && (s_unos == 5U)) {
        s_unos = 0U;
        return;
    }
    s_unos = bit ? (uint8_t)(s_unos + 1U) : 0U;

    /* Siete unos seguidos es un aborto. */
    if (s_unos >= 7U) {
        s_en_trama = 0U;
        s_nbits = 0U; s_unos = 0U;
        return;
    }

    if (!s_en_trama) { return; }

    if (s_nbits >= AIS_BITS_MAX) {
        /* Mas larga de lo que guardamos: se abandona entera. Contarlas
         * aparte importa - si suben mucho, o hay mensajes de varias ranuras
         * en el aire o el lazo de reloj esta patinando y no encuentra la
         * bandera de cierre. Son dos problemas distintos. */
        s_info.largas++;
        s_en_trama = 0U;
        s_nbits = 0U;
        return;
    }

    /* DE MAS A MENOS PESO, en orden de llegada: lo que quiere ais_msg.c. */
    if ((s_nbits & 7U) == 0U) { s_bits[s_nbits >> 3] = 0U; }
    if (bit) { s_bits[s_nbits >> 3] |= (uint8_t)(0x80U >> (s_nbits & 7U)); }
    s_nbits++;
}

/* ==========================================================================
 * NRZI
 * ==========================================================================
 *
 * Un cero es un CAMBIO de nivel y un uno es quedarse igual. Eso hace que
 * oir la senal del reves -que en FM pasa con solo sintonizar por el otro
 * lado- no importe: la inversion se cancela al comparar cada nivel con el
 * anterior. Por eso aqui no hay conmutador de polaridad.
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
 * ==========================================================================
 *
 * El mismo acumulador de fase que el AX.25, y por las mismas razones: se
 * decide en la vuelta del acumulador -donde la media movil cubre justo un
 * bit- y los cambios de nivel tiran de la fase hacia el centro.
 *
 * La ganancia es mas corta que la del AX.25 (4 en vez de 5): una rafaga de
 * AIS dura 26,7 ms y el preambulo son 24 bits, o sea que el lazo tiene diez
 * veces menos tiempo para engancharse que en el paquete de VHF. Merece la
 * pena pagarlo con algo mas de nerviosismo.
 */
#define DPLL_G 4

static void reloj(float d)
{
    uint32_t fase_ant = s_fase;
    uint8_t  sig = (uint8_t)(d > 0.0f);

    s_fase += s_inc;

    if (s_fase < fase_ant) { nrzi_bit(sig); }

    if (sig != s_sig_ant) {
        int32_t err = (int32_t)(s_fase - 0x80000000UL);

        s_fase = (uint32_t)((int32_t)s_fase - (err >> DPLL_G));
        s_sig_ant = sig;
    }
}

/* ==========================================================================
 * LA ENTRADA
 * ========================================================================== */
void ais_rx_mete(const float *disc, uint32_t n)
{
    uint32_t k;

    if (!s_on || (disc == (const float *)0)) { return; }

    for (k = 0U; k < n; k++) {
        float x = disc[k];
        float y;

        /*
         * QUITAR LA CONTINUA, que aqui no es higiene sino el requisito
         * central. Lo que sale del discriminador es la desviacion
         * instantanea MAS el desvio de sintonia: si la radio esta 500 Hz
         * fuera, toda la senal se va para un lado y el cortador de cero
         * decide siempre lo mismo. Con la continua quitada, el cero vuelve
         * a caer en medio de los dos niveles y la sintonia deja de tener
         * que ser exacta.
         */
        s_dc += s_dc_a * (x - s_dc);
        y = x - s_dc;

        /* Media movil de un bit: el filtro adaptado del pobre. Para NRZ es
         * el optimo; para GMSK, que reparte cada bit entre sus vecinos, se
         * queda algo corto - pero es lo que cabe y lo que ya esta probado
         * en el AX.25. */
        s_suma += y - s_vent[s_vi];
        s_vent[s_vi] = y;
        s_vi++;
        if (s_vi >= s_spb) { s_vi = 0U; }

        if (s_lleno < s_spb) { s_lleno++; continue; }

        /* Un nivel sin calibrar, para la chapa: sirve para comparar una
         * frecuencia con otra, no para decir dB. */
        {
            float a = (s_suma < 0.0f) ? -s_suma : s_suma;

            s_nivel += 0.0005f * ((a * 300.0f) - s_nivel);
            if (s_nivel < 0.0f) { s_nivel = 0.0f; }
        }

        reloj(s_suma);
    }
}

uint8_t ais_rx_saca(uint8_t *bits, uint16_t max_bytes, uint16_t *nbits_out)
{
    uint16_t nb;
    uint16_t bytes;

    if ((bits == (uint8_t *)0) || (nbits_out == (uint16_t *)0)) { return 0U; }
    if (s_col == s_cab) { return 0U; }

    nb = s_anillo[s_col].nbits;
    bytes = (uint16_t)((nb + 7U) / 8U);
    if (bytes > max_bytes) { bytes = max_bytes; nb = (uint16_t)(bytes * 8U); }

    memcpy(bits, s_anillo[s_col].bits, bytes);
    *nbits_out = nb;
    s_col = (uint8_t)((s_col + 1U) % ANILLO_N);
    return 1U;
}
