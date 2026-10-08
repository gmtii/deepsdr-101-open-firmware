#include "rtty_auto.h"
#include "rtty.h"
#include "rtty_scope.h"
#include "debug_uart.h"
#include <string.h>
#include <math.h>

/* Las mismas cuatro de la lista del boton. Ver k_rtty_baud_values[] en main.c. */
#define AUTO_NBAUD 4U
static const float k_baud[AUTO_NBAUD] = { 45.45f, 50.0f, 75.0f, 100.0f };

/* Y los desplazamientos de la lista. */
#define AUTO_NSALTO 4U
static const float k_salto[AUTO_NSALTO] = { 170.0f, 425.0f, 450.0f, 850.0f };

/*
 * POR QUE LA VELOCIDAD SE BUSCA PROBANDO Y NO SE MIDE - 06/10/2026.
 *
 * La primera version media la velocidad con una rejilla: para cada
 * candidata, cuanto se apiñan las fases de las transiciones en un periodo
 * de 1/R. Es lo que yo mismo use fuera de la radio sobre la grabacion de
 * 3712 y dio 50 Bd clavados.
 *
 * Dentro de la radio dio 100. Y las dos cosas son ciertas.
 *
 * El boletin del DWD va a 50 baudios CON UNA UNIDAD Y MEDIA DE PARADA. O
 * sea que un caracter dura 7,5 bits, y la transicion de parada a arranque
 * del siguiente cae a MEDIO BIT de la rejilla. Con eso, TODAS las
 * transiciones estan en multiplos de 10 ms -no de 20- y la rejilla,
 * honradamente, contesta 100 Hz. Lo que mide es el medio bit.
 *
 * Y no hay forma de deshacer el empate mirando las transiciones: un 100
 * baudios con una parada y un 50 baudios con parada y media dejan las
 * transiciones EN LOS MISMOS SITIOS. La diferencia no esta en donde caen,
 * esta en que uno de los dos deja los caracteres cuadrados y el otro no.
 *
 * Asi que la velocidad no se mide: se PRUEBA. Se pone una candidata, se
 * deja decodificar unos segundos y se mira cuantos caracteres cierran con
 * su bit de parada - que es exactamente la misma medida con la que ya se
 * decide la inversion, y que ya esta demostrada en antena. La que mas
 * saque, gana.
 *
 * Cuesta unos segundos por candidata y solo se paga cuando algo va mal,
 * que es cuando no hay nada que perder.
 */
#define RTTY_AUTO_SANO      85U   /* por encima de esto no se toca nada */
/*
 * Y DOS VENTANAS MALAS SEGUIDAS ANTES DE MOVER NADA. Con una sola, un
 * desvanecimiento de ocho segundos bastaba para que la radio se pusiera a
 * probar velocidades en mitad de un boletin que estaba saliendo bien.
 */
#define AUTO_MALAS_MIN       2U

/*
 * Los tonos. Un pico vale si saca esto al fondo de la traza; la PAREJA
 * vale si ademas se separan como un desplazamiento de verdad y los dos
 * tienen un tamaño parecido.
 */
#define AUTO_PICO_VECES     5.0f
#define AUTO_HZ_LO        300.0f
#define AUTO_HZ_HI       2800.0f
#define AUTO_SEP_LO       140.0f
#define AUTO_SEP_HI      1000.0f
#define AUTO_PICOS_MAX      8U

/*
 * EL ESPECTRO NECESITA MAS MEMORIA DE LA QUE TIENE EL OSCILADOR - 06/10/2026.
 *
 * El primer intento miraba directamente el cuadro de rtty_scope.c. No
 * valia, y el banco lo enseño de una: buscando los dos tonos de la
 * grabacion de 3712 encontraba UNO, el de 914 Hz, a 48 veces el fondo. El
 * otro no estaba.
 *
 * Y es logico. La FFT del oscilador son 512 muestras a 12 kHz: 43 ms. A 50
 * baudios eso son DOS BITS, o sea que en esa ventana casi siempre suena un
 * tono y el otro no. El oscilador promedia unos pocos cuadros porque lo que
 * quiere es que la traza no parpadee, no que salgan los dos tonos.
 *
 * Para encontrar una PAREJA hace falta promediar lo bastante como para que
 * los dos hayan sonado seguro: un par de segundos. Asi que esto se lleva su
 * propia media, exponencial, de la banda que importa.
 *
 * En uint16 y solo de 281 a 3258 Hz: 128 casillas son 256 bytes. Con
 * flotantes de la banda entera serian 1 kB, y de RAM libre quedan menos de
 * dos.
 */
#define AUTO_PROM_N    128U
#define AUTO_PROM_B0    12U        /* primera casilla: 12 * 23,4 = 281 Hz */
#define AUTO_PROM_ALFA   3U        /* desplazamiento: media de ~8 cuadros */
static uint16_t s_prom[AUTO_PROM_N];
static uint8_t  s_prom_hay;

static uint32_t s_prom_visto;   /* ultimo cuadro promediado */

static void prom_mete(void)
{
    const float *m;
    uint32_t n = rtty_scope_frame_n();
    uint16_t i;
    float mx = 0.0f;

    /*
     * UNA VENTANA SE PROMEDIA UNA VEZ, y antes no era asi: se cogia el
     * cuadro en cada vuelta del bucle aunque fuera el mismo de antes, con
     * lo que la media de ocho cuadros era la media de ocho copias de uno.
     * Y la gracia de promediar es que en 43 ms solo suena uno de los dos
     * tonos: sin cuadros DISTINTOS no aparece la pareja. Ver
     * rtty_scope_frame_peek().
     */
    if (n == s_prom_visto) { return; }
    s_prom_visto = n;

    m = rtty_scope_frame_peek();
    if (m == 0) { return; }
    for (i = 0U; i < AUTO_PROM_N; i++) {
        float v = m[AUTO_PROM_B0 + i];
        if (v > mx) { mx = v; }
    }
    if (mx <= 0.0f) { return; }
    /* Cada cuadro se normaliza a 0..1000 ANTES de promediar: si no, un
     * desvanecimiento general pesaria como si fuera forma del espectro. */
    for (i = 0U; i < AUTO_PROM_N; i++) {
        uint16_t v = (uint16_t)((m[AUTO_PROM_B0 + i] / mx) * 1000.0f);
        s_prom[i] = (uint16_t)(s_prom[i] - (s_prom[i] >> AUTO_PROM_ALFA)
                               + (v >> AUTO_PROM_ALFA));
    }
    if (s_prom_hay < 255U) { s_prom_hay++; }
}

static uint8_t  s_on = 1U;
static uint16_t s_tonos_n;
static uint16_t s_baud_n;
static uint8_t  s_malas;
static uint8_t  s_cual;              /* velocidad de la lista que se esta probando */
static uint8_t  s_incoh;             /* medidas seguidas que no se creen */
static float    s_baud_arranque;     /* con la que se entro en el modo */
static uint8_t  s_baud_contado;
static uint16_t s_vent_vista;
static uint16_t s_punt[AUTO_NBAUD];

/*
 * LA VELOCIDAD SE MIDE EN LA RACHA MAS CORTA - 06/10/2026, tercer intento.
 *
 * El primero fue una rejilla de fases. Contesto 100 sobre una señal de 50,
 * y con razon: con unidad y media de parada las transiciones caen cada
 * medio bit, asi que la rejilla mide el medio bit (ver arriba).
 *
 * El segundo fue probar las cuatro velocidades y quedarse con la que mas
 * caracteres cerrara. Tampoco: el banco saco 97% a 100 baudios y 97% a
 * 45,45 SOBRE LA MISMA señal de 50. Y es logico - la maquina de estados se
 * resincroniza en cada flanco de arranque, asi que un 10% de error de
 * velocidad solo desplaza el bit de parada tres cuartos de bit, y la
 * parada dura bit y medio. Cierra igual. Cerrar el marco no quiere decir
 * haber leido los bits de dentro.
 *
 * El tercero es el bueno, y es el mas tonto de los tres: UN BIT ES LA
 * RACHA MAS CORTA QUE PUEDE HABER. Midiendo cuanto duran las rachas de la
 * decision, sobre la grabacion de 3712 sale esto:
 *
 *      18,7 ms ... 136      <- 20 ms, el bit
 *      21,3 ms ... 125      <-
 *      29,3 ms ...  97      <- 30 ms, la parada de bit y medio
 *      32,0 ms ...  42      <-
 *      por debajo de 16 ms:  110 en total, ruido repartido
 *
 * Los dos montones estan donde tienen que estar y no hay nada en medio. La
 * moda son 20 ms clavados: 50 baudios. A 100 estaria en 10 ms y no habria
 * forma de confundirlos.
 *
 * El histograma se lleva en bloques -2,67 ms cada uno- y se mira la moda
 * afinada con sus dos vecinos, que es lo que convierte "entre 7 y 8
 * bloques" en 19,9 ms.
 *
 * PERO LA MEDIDA DEPENDE DE LA VELOCIDAD PUESTA, y eso hay que tenerlo en
 * cuenta o se muerde la cola. El filtro del decodificador mide sobre medio
 * bit, asi que con 100 baudios puestos son 60 muestras -200 Hz de ancho- y
 * por ahi entra ruido de sobra para que la decision parpadee. Con la
 * grabacion de 3712 -que es de 50- y 100 baudios puestos, la moda salio en
 * 3 bloques: 8 ms, que no es ningun bit de nada, es el filtro.
 *
 * Asi que no se usa la moda como medida absoluta sino como PRUEBA DE
 * COHERENCIA: se prueba cada velocidad de la lista y se mira si la moda que
 * sale dice esa misma velocidad. Solo una lo cumple.
 *
 *     puesta 100 Bd  ->  moda 3,0 bloques  ->  dice 125 Bd   (no cuadra)
 *     puesta  50 Bd  ->  moda 7,5 bloques  ->  dice  50 Bd   (cuadra)
 *
 * La buena es un punto fijo: la unica que se cree a si misma.
 */
#define AUTO_RACHA_N    48U     /* hasta 128 ms de racha */
#define AUTO_RACHA_MIN   3U     /* por debajo de 8 ms es ruido, no un bit */
#define AUTO_RACHA_TOT 200U     /* rachas que hay que juntar antes de opinar */
static uint16_t s_racha[AUTO_RACHA_N];
static uint16_t s_racha_tot;
static uint8_t  s_racha_ant = 1U;
static uint16_t s_racha_cur;

void rtty_auto_bit(uint8_t bit)
{
    if (!s_on) { return; }
    if (bit == s_racha_ant) {
        if (s_racha_cur < 0xFFFFU) { s_racha_cur++; }
        return;
    }
    if (s_racha_cur >= AUTO_RACHA_MIN && s_racha_cur < AUTO_RACHA_N) {
        s_racha[s_racha_cur]++;
        s_racha_tot++;
        if (s_racha_tot >= 2000U) {       /* se olvida lo viejo */
            uint16_t i;
            for (i = 0U; i < AUTO_RACHA_N; i++) { s_racha[i] >>= 1; }
            s_racha_tot >>= 1;
        }
    }
    s_racha_ant = bit;
    s_racha_cur = 1U;
}

/* La velocidad que dice la moda, o 0 si todavia no hay con que. */
static float velocidad_medida(void)
{
    uint16_t i, moda = 0U, mx = 0U;
    float num, den;

    if (s_racha_tot < AUTO_RACHA_TOT) { return 0.0f; }
    for (i = AUTO_RACHA_MIN; i < AUTO_RACHA_N; i++) {
        if (s_racha[i] > mx) { mx = s_racha[i]; moda = i; }
    }
    /*
     * La moda tiene que destacar, pero no tanto como pedia al principio.
     * Con la velocidad equivocada puesta el filtro es estrecho y la
     * decision parpadea: la moda buena existe pero solo junta el 15% de
     * las rachas, no el 17 que se le pedia, y el banco se quedaba callado
     * justo cuando hacia falta que hablara.
     */
    if (moda == 0U || mx * 10U < s_racha_tot) { return 0.0f; }

    /* afinada con los dos vecinos: la rejilla es de 2,67 ms y un bit a 50
     * baudios son 7,5 bloques, o sea que la moda SIEMPRE cae partida */
    num = (float)(moda - 1U) * (float)s_racha[moda - 1U]
        + (float)moda * (float)mx;
    den = (float)s_racha[moda - 1U] + (float)mx;
    if (moda + 1U < AUTO_RACHA_N) {
        num += (float)(moda + 1U) * (float)s_racha[moda + 1U];
        den += (float)s_racha[moda + 1U];
    }
    if (den <= 0.0f) { return 0.0f; }
    {
        float bloques = num / den;
        float seg = bloques * (float)RTTY_BLOCK_SAMPLES / 12000.0f;
        if (seg <= 0.0f) { return 0.0f; }
        return 1.0f / seg;
    }
}

void rtty_auto_set(uint8_t on) { s_on = on ? 1U : 0U; }
uint8_t rtty_auto_get(void)    { return s_on; }

void rtty_auto_reset(void)
{
    s_malas = 0U;
    s_cual = 0U;
    s_incoh = 0U;
    s_baud_arranque = 0.0f;
    s_baud_contado = 0U;
    s_vent_vista = rtty_salud_n();
    memset(s_punt, 0, sizeof s_punt);
    memset(s_prom, 0, sizeof s_prom);
    s_prom_hay = 0U;
    s_prom_visto = rtty_scope_frame_n();   /* empezar a contar desde ahora */
    memset(s_racha, 0, sizeof s_racha);
    s_racha_tot = 0U; s_racha_cur = 0U; s_racha_ant = 1U;
}

uint16_t rtty_auto_tonos_veces(void) { return s_tonos_n; }
uint16_t rtty_auto_baud_veces(void)  { return s_baud_n; }
void rtty_auto_puntos(uint16_t out[4])
{
    uint8_t i;
    for (i = 0U; i < AUTO_NBAUD; i++) { out[i] = s_punt[i]; }
}

/*
 * LOS DOS TONOS, ELEGIDOS POR PAREJAS Y NO DE UNO EN UNO - 06/10/2026.
 *
 * La primera version cogia el pico mas alto y luego el segundo mas alto
 * que estuviera lo bastante lejos. Sobre la grabacion del dueño eligio
 * 1378 y 780 Hz, y el de 780 NO ES UNA SEÑAL: la grabacion esta hecha con
 * el movil delante del altavoz, y 1374 - 927 = 447... no, 839. El altavoz
 * y el microfono distorsionan y sacan la DIFERENCIA de los dos tonos, 839
 * Hz, que en el promedio abulta mas que la marca cuando esta desvanecida.
 *
 * O sea que el segundo pico mas alto no tiene por que ser el segundo tono.
 *
 * Lo que SI es cierto de una pareja de tonos de RTTY: se separan como uno
 * de los desplazamientos de la lista, y los dos miden parecido -son el
 * mismo transmisor alternando-. Asi que se miran TODAS las parejas y se
 * puntua cada una por las tres cosas: que se separen como un
 * desplazamiento conocido, que esten equilibradas, y que sean altas. La
 * pareja 927/1374 se separa 447 -450 clavados- y esta equilibrada; la
 * pareja 780/1378 se separa 598, que no es ningun desplazamiento, y se
 * cae sola.
 */
static uint8_t dos_picos(float *bajo, float *alto)
{
    float hz = rtty_scope_hz_per_bin();
    uint16_t i;
    float fondo = 0.0f;
    uint16_t pk[AUTO_PICOS_MAX]; float pv[AUTO_PICOS_MAX];
    uint8_t np = 0U, a, b;
    float mejor = 0.0f; uint8_t ma = 0U, mb = 0U;

    if (hz <= 0.0f || s_prom_hay < 16U) { return 0U; }

    for (i = 0U; i < AUTO_PROM_N; i++) { fondo += (float)s_prom[i]; }
    fondo /= (float)AUTO_PROM_N;
    if (fondo <= 0.0f) { return 0U; }

    for (i = 1U; i + 1U < AUTO_PROM_N; i++) {
        float v = (float)s_prom[i];
        if (v < fondo * AUTO_PICO_VECES) { continue; }
        if (v < (float)s_prom[i - 1U] || v < (float)s_prom[i + 1U]) { continue; }
        if (np < AUTO_PICOS_MAX) { pk[np] = i; pv[np] = v; np++; }
        else {
            uint8_t peor = 0U, k;
            for (k = 1U; k < np; k++) { if (pv[k] < pv[peor]) { peor = k; } }
            if (v > pv[peor]) { pk[peor] = i; pv[peor] = v; }
        }
    }
    if (np < 2U) { return 0U; }

    /* la MEJOR PAREJA, no los dos mas altos - ver el comentario de arriba */
    for (a = 0U; a < np; a++) {
        for (b = (uint8_t)(a + 1U); b < np; b++) {
            float fa = (float)(pk[a] + AUTO_PROM_B0) * hz;
            float fb = (float)(pk[b] + AUTO_PROM_B0) * hz;
            float sep = (fb > fa) ? (fb - fa) : (fa - fb);
            float eq, alt, dsalto = 1e9f, p;
            uint8_t k;

            if (sep < AUTO_SEP_LO || sep > AUTO_SEP_HI) { continue; }
            for (k = 0U; k < AUTO_NSALTO; k++) {
                float d = sep - k_salto[k]; if (d < 0.0f) { d = -d; }
                d /= k_salto[k];
                if (d < dsalto) { dsalto = d; }
            }
            if (dsalto > 0.12f) { continue; }
            eq  = (pv[a] < pv[b]) ? (pv[a] / pv[b]) : (pv[b] / pv[a]);
            alt = (pv[a] + pv[b]) / fondo;
            p   = (1.0f - dsalto) * eq * alt;
            if (p > mejor) { mejor = p; ma = a; mb = b; }
        }
    }
    if (mejor <= 0.0f) { return 0U; }

    {
        uint16_t i0 = (pk[ma] < pk[mb]) ? pk[ma] : pk[mb];
        uint16_t i1 = (pk[ma] < pk[mb]) ? pk[mb] : pk[ma];
        float d0 = 0.0f, d1 = 0.0f, den;
        den = (float)s_prom[i0 - 1U] - 2.0f * (float)s_prom[i0] + (float)s_prom[i0 + 1U];
        if (den != 0.0f) { d0 = 0.5f * ((float)s_prom[i0 - 1U] - (float)s_prom[i0 + 1U]) / den; }
        den = (float)s_prom[i1 - 1U] - 2.0f * (float)s_prom[i1] + (float)s_prom[i1 + 1U];
        if (den != 0.0f) { d1 = 0.5f * ((float)s_prom[i1 - 1U] - (float)s_prom[i1 + 1U]) / den; }
        if (d0 > 0.5f || d0 < -0.5f) { d0 = 0.0f; }
        if (d1 > 0.5f || d1 < -0.5f) { d1 = 0.0f; }
        *bajo = ((float)(i0 + AUTO_PROM_B0) + d0) * hz;
        *alto = ((float)(i1 + AUTO_PROM_B0) + d1) * hz;
    }
    return 1U;
}

/*
 * El de la lista que MAS SE LE PAREZCA - y "mas", no "el primero que entre
 * dentro del 12%". Con 425 medidos entraban 425 y 450 los dos, y como la
 * lista va de menor a mayor se devolvia siempre 425: el ajuste automatico
 * cambiaba el desplazamiento de 450 a 425 y volvia a cambiarlo en cuanto
 * la medida se movia un hercio.
 */
static float redondea_salto(float medido)
{
    uint8_t i, mejor = 0U;
    float dmin = 1e9f;

    for (i = 0U; i < AUTO_NSALTO; i++) {
        float d = medido - k_salto[i];
        if (d < 0.0f) { d = -d; }
        if (d < dmin) { dmin = d; mejor = i; }
    }
    return (dmin <= k_salto[mejor] * 0.12f) ? k_salto[mejor] : medido;
}

static void pon_tonos(float centro, float salto)
{
    float h = salto * 0.5f;
    /* Se respeta la polaridad de ahora; si esta del reves, la sombra de
     * rtty.c se encarga - ver rtty_sombra_t. */
    if (rtty_get_mark_hz() > rtty_get_space_hz()) {
        rtty_set_mark_space_hz(centro + h, centro - h);
    } else {
        rtty_set_mark_space_hz(centro - h, centro + h);
    }
}

void rtty_auto_poll(void)
{
    float bajo, alto;
    uint16_t v = rtty_salud_n();

    if (!s_on || !rtty_get_enabled()) { return; }

    /* El espectro se promedia SIEMPRE, vaya bien o mal: cuando haga falta
     * mirarlo tiene que estar ya hecho, no empezando. */
    if (s_baud_arranque <= 0.0f) { s_baud_arranque = rtty_get_baud(); }
    prom_mete();

    /* Solo se trabaja con ventanas NUEVAS: una ventana se mira una vez. */
    if (v == s_vent_vista) { return; }
    s_vent_vista = v;

    /* --- quieto: se mira si hay motivo para moverse --- */
    /*
     * Una ventana buena no borra la cuenta, la baja de una en una. Con la
     * velocidad equivocada la salud bailaba entre 79 y 87 -la puerta esta
     * en 85-, asi que a cada ventana buena se reiniciaba la cuenta y no se
     * llegaba nunca a las dos malas seguidas. Lo que importa no es que
     * sean seguidas, es que sean mayoria.
     */
    if (rtty_salud_pc() >= RTTY_AUTO_SANO) {
        if (s_malas > 0U) { s_malas--; }
        return;
    }
    if (s_malas < 0xFFU) { s_malas++; }
    if (s_malas < AUTO_MALAS_MIN) { return; }

    /* Primero los tonos: es lo barato y lo que mas suele fallar. */
    if (dos_picos(&bajo, &alto)) {
        float c_nuevo = (bajo + alto) * 0.5f;
        float c_ahora = (rtty_get_mark_hz() + rtty_get_space_hz()) * 0.5f;
        float sep = redondea_salto(alto - bajo);
        float dc = c_nuevo - c_ahora; if (dc < 0.0f) { dc = -dc; }
        float ds = sep - rtty_get_shift_hz(); if (ds < 0.0f) { ds = -ds; }

        if (dc > 25.0f || ds > 25.0f) {
            pon_tonos(c_nuevo, sep);
            s_tonos_n++;
            s_malas = 0U;       /* y se le dan dos ventanas para demostrarlo */
            debug_print_dec("rtty auto: tonos puestos, centro Hz", (uint32_t)c_nuevo);
            return;
        }
    }

    /*
     * Con los tonos encima y sigue sin cuadrar: la velocidad. Se recorre la
     * lista y se le pide a cada una que se crea a si misma; ver arriba.
     */
    {
        float v = velocidad_medida();

        if (v > 0.0f) {
            float puesta = rtty_get_baud();
            float err = v - puesta; if (err < 0.0f) { err = -err; }
            err /= puesta;
            s_punt[s_cual] = (uint16_t)(v * 10.0f);

            if (err <= 0.08f) {
                s_incoh = 0U;
                /* Se cuenta el cambio SOLO al dar una por buena, y solo si
                 * no es con la que se arranco: asi "velocidad 0" quiere
                 * decir "no te he tocado nada", que es lo que se mira. */
                {
                    float d = rtty_get_baud() - s_baud_arranque;
                    if (d < 0.0f) { d = -d; }
                    if (d > 0.5f && !s_baud_contado) { s_baud_n++; s_baud_contado = 1U; }
                }
                /* Esta se cree a si misma: es la buena. Si ademas es la que
                 * ya estaba puesta, no hay nada que hacer. */
                s_malas = 0U;
                debug_print_dec("rtty auto: velocidad coherente x10",
                                (uint32_t)(v * 10.0f));
                return;
            }
            /*
             * No se cree. Pero una sola medida mala no basta: con un
             * desvanecimiento la moda se ensucia, y cambiar de velocidad
             * por eso saca de sintonia una radio que iba bien. Dos
             * seguidas.
             */
            if (s_incoh < 1U) { s_incoh++; return; }
            s_incoh = 0U;
            s_cual = (uint8_t)((s_cual + 1U) % AUTO_NBAUD);
            rtty_set_baud(k_baud[s_cual]);
            memset(s_racha, 0, sizeof s_racha);
            s_racha_tot = 0U; s_racha_cur = 0U;
            debug_print_dec("rtty auto: pruebo x100",
                            (uint32_t)(k_baud[s_cual] * 100.0f));
        }
    }
}
