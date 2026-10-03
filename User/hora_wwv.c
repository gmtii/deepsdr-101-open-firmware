#include "hora_wwv.h"
#include "hora_comun.h"
#include <math.h>

/* ---------------------------------------------------------------------------
 * CONSTANTES. Todas barridas en sim/wwvtest.c; el barrido que eligio cada
 * numero esta anotado al lado. Ninguna esta puesta "a ojo".
 * ------------------------------------------------------------------------- */

/*
 * LA SUBPORTADORA NO ES UNA SOLA. 23/09/2026.
 *
 * WWV lleva la hora en 100 Hz y no hay duda. BPM la lleva en 125 Hz segun su
 * memoria tecnica -"BPM 时码采用在 125 Hz 的副载波上发送 BCD 码方式", del
 * articulo del National Time Service Center que describe el formato-, pero
 * hay grabaciones recientes en las que aparece en 100. No he podido
 * resolverlo desde aqui, y no pienso elegir a cara o cruz un numero del que
 * depende que funcione o no.
 *
 * Asi que en BPM se buscan LAS DOS a la vez: dos mezcladores y dos filtros
 * corriendo en paralelo, y se usa la que tenga mas nivel. Cuesta unas quince
 * operaciones por muestra y quita la suposicion de en medio. Ademas la
 * pantalla dice cual encontro, que es como acabaremos sabiendo cual es.
 */
#define NSUB_MAX 2U

/*
 * Ancho del filtro que la aisla. Se implementa como dos polos en cascada
 * DESPUES de bajar los 100 Hz a banda base, asi que la banda de paso real es
 * el doble de este numero, centrada en 100 Hz.
 *
 * QUIEN MANDA AQUI NO ES EL RUIDO, ES LA VOZ. WWV anuncia la hora hablada
 * durante los ultimos 7,5 segundos de cada minuto, y una voz masculina tiene
 * el fundamental sobre 120 Hz: a VEINTE hercios de la subportadora que hay
 * que medir, y con mucha mas energia. O sea que el filtro no esta ahi para
 * quitar ruido blanco -para eso sobraria cualquier cosa- sino para separar
 * 100 de 120, y por eso sale mucho mas estrecho de lo que uno pondria.
 *
 * Barrido con la trama completa (tono de 500 Hz, tics de segundo y la voz
 * modelada como fundamental de 120 Hz con armonicos), contando tramas buenas
 * de seis minutos y hasta cuanto ruido aguanta:
 *
 *     fc     limpia   aguanta hasta
 *      5 Hz    2/4      20%    el flanco tarda demasiado
 *      8 Hz    2/4      35%    <- elegido
 *     12 Hz    2/4       5%
 *     15 Hz    1/4       5%    la voz ya pisa
 *     20 Hz    0/4       -
 *     30 Hz    0/4       -     no decodifica ni una
 *     40 Hz    0/4       -
 *
 * El salto entre 8 y 12 no es ruido de medida: es exactamente donde la banda
 * de paso empieza a tocar los 120 Hz de la voz.
 */
#ifndef FILTRO_HZ
#define FILTRO_HZ 8.0f
#endif

/*
 * QUITAR LA CONTINUA ANTES DE MEZCLAR. Esto no es un adorno: sin ello no
 * funciona nada, y la razon merece estar escrita.
 *
 * La envolvente de una AM es "portadora + modulacion": una continua de valor
 * 1 con los 100 Hz encima valiendo 0,18 como mucho. Al bajar los 100 Hz a
 * banda base multiplicando por e^-j2pi100t, esa continua NO se queda quieta:
 * se va a 100 Hz de la banda base, con amplitud 1, o sea ONCE VECES la señal
 * que se quiere medir. El filtro de dos polos a 15 Hz la deja en 0,024 -que
 * ya es poco-, pero 0,024 sobre una señal de 0,09 es un rizado del 42% a
 * 200 Hz, y con ese rizado el comparador da tres flancos por segundo en vez
 * de uno. Medido asi, tal cual, el 23/09/2026.
 *
 * Se quita con un paso ALTO de un polo antes de la mezcla. A 100 Hz deja
 * pasar el 99% y a la continua nada, y el rizado baja del 42% al 2%.
 */
#define DC_HZ 10.0f

/*
 * ANTIRREBOTE. Aunque el rizado sea pequeño, el desvanecimiento de onda
 * corta hace que el nivel cruce el umbral varias veces en el mismo flanco.
 * Un cambio de estado solo se da por bueno si el nivel nuevo AGUANTA este
 * tiempo, y entonces se apunta con la marca de tiempo del primer cruce, no
 * la del final: asi el antirrebote limpia sin falsear las duraciones, que es
 * justo lo unico que aqui significa algo.
 *
 * 50 ms es seguro por construccion: el tramo alto mas corto de WWV dura
 * 170 ms y el hueco mas corto 230 ms.
 */
#define ESTABLE_MS 50.0f

/* Seguidor del nivel alto. Igual que el de DCF77 y por lo mismo: con AGC el
 * nivel absoluto no significa nada y lo unico util es "respecto a lo mas
 * alto que se ha visto hace poco". 8 s cubre de sobra un minuto de trama
 * sin quedarse pegado a un pico de ruido. */
#define PICO_CAIDA_S 8.0f

/*
 * Umbrales con histeresis, en fraccion del nivel alto. El estado bajo de WWV
 * NO es cero: son 15 dB menos, o sea 0,18 del alto, asi que el umbral puede
 * ir alto sin riesgo de cortar el pulso, y conviene que vaya alto para que la
 * voz y el ruido no lo alarguen. Barrido con la trama completa:
 *
 *     baja/sube    aguanta hasta   tramas malas con solo ruido
 *      0,25/0,35        5%           11    demasiado flojo
 *      0,35/0,45       20%            0
 *      0,45/0,55       35%            0    <- elegido
 *      0,55/0,65       20%            9
 *      0,65/0,75        5%            3    ya se come el pulso
 */
#ifndef UMBRAL_BAJA
#define UMBRAL_BAJA 0.45f
#endif
#ifndef UMBRAL_SUBE
#define UMBRAL_SUBE 0.55f
#endif

/* Clasificacion de la duracion del tramo alto. Los valores nominales son
 * 170, 470 y 770 ms; las fronteras van en el medio geometrico de cada par,
 * que es donde el error relativo se reparte igual entre los dos vecinos. */
/*
 * Las tres duraciones nominales, que NO son las mismas en las dos emisoras:
 *
 *   WWV  170 / 470 / 770 ms, empezando a los 30 ms del segundo (los primeros
 *        30 ms se los reserva el tic de segundo).
 *   BPM  200 / 500 / 800 ms, empezando EN el segundo. Su memoria tecnica lo
 *        dice asi: "200 ms 脉冲代表 0，500 ms 脉冲代表 1", y añade que el
 *        flanco de subida de cada bit sirve de marca de tiempo exacta.
 *
 * Que empiecen en sitios distintos da igual para decodificar: lo que se mide
 * es de subida a bajada, y el ciclo de subida a subida es un segundo en las
 * dos. Lo que importa son los anchos.
 *
 * Las fronteras se calculan en el arranque como el punto medio entre cada par
 * de duraciones nominales, que es donde el error relativo se reparte igual
 * entre los dos vecinos. No se escriben a mano para que añadir un formato
 * nuevo no obligue a recalcularlas.
 */
#define DUR_MARGEN 0.55f   /* cuanto se acepta por debajo del 0 y por encima
                            * de la marca, en fraccion de la duracion */

/* El ciclo entre dos subidas: un segundo, o dos cuando falta el pulso del
 * segundo 00. Las ventanas son las mismas que usa DCF77 para su hueco. */
#define SEG_MIN_MS    800.0f
#define SEG_MAX_MS   1200.0f
#define HUECO_MIN_MS 1800.0f
#define HUECO_MAX_MS 2200.0f

/* Por debajo de este contraste no hay WWV, hay ruido. El contraste de WWV
 * de verdad es 0,83 por construccion (18% contra 3% de modulacion). */
#define CONTRASTE_MIN 0.25f

/*
 * Que bits de la trama valen SIEMPRE cero. Quince en cada formato, y como
 * ninguna de las dos emisoras lleva paridades son -junto con las seis
 * marcas- lo unico que impide que una trama de ruido pase por buena.
 *
 * En BPM el reparto tiene una regularidad que conviene ver, porque es lo que
 * me hizo fiarme de el: TODOS los campos tienen un bit de relleno justo
 * despues de las unidades, antes de las decenas. Minutos 01-04 y hueco en
 * 05; horas 10-13 y hueco en 14; dia 20-23 y hueco en 24; mes 30-33 y hueco
 * en 34; año 40-43 y hueco en 44. Una tabla que encaja consigo misma cinco
 * veces seguidas no es una tabla mal copiada.
 */
static const uint8_t k_ceros_wwv[15] = {
    1U, 8U, 14U, 18U, 24U, 27U, 28U, 34U,
    42U, 43U, 44U, 45U, 46U, 47U, 48U
};
static const uint8_t k_ceros_bpm[15] = {
    5U, 14U, 17U, 18U, 24U, 27U, 28U, 34U,
    36U, 37U, 38U, 44U, 55U, 56U, 58U
};
/* Y estos son los segundos donde tiene que haber una MARCA de posicion. Son
 * los mismos en las dos: BPM copio el esquema de WWV, y su memoria tecnica
 * lo dice ("参照 WWV/WWVH 的时码格式"). */
static const uint8_t k_marcas[6] = { 9U, 19U, 29U, 39U, 49U, 59U };

/* ---------------------------------------------------------------------------
 * ESTADO
 * ------------------------------------------------------------------------- */

static float s_fs = 1500.0f;

static uint8_t s_bpm;        /* 1 si el formato es el de BPM */
static uint8_t s_n_sub;      /* cuantas subportadoras se buscan a la vez */
static uint8_t s_sel;        /* cual de ellas se esta usando */
static float   s_sub_hz[NSUB_MAX];

/* Un oscilador complejo por candidata, que baja su subportadora a banda base.
 * Se llevan girando en vez de llamar a cosf()/sinf() por muestra; cada cierto
 * rato se les devuelve el modulo a 1 con un paso de Newton, mas barato que
 * una raiz. */
static float s_osc_r[NSUB_MAX], s_osc_i[NSUB_MAX];
static float s_w_r[NSUB_MAX], s_w_i[NSUB_MAX];
static uint32_t s_osc_n;

static float s_dc, s_a_dc;                 /* paso alto que quita la portadora */
/* Dos polos en I y en Q, uno por candidata. */
static float s_fi1[NSUB_MAX], s_fq1[NSUB_MAX];
static float s_fi2[NSUB_MAX], s_fq2[NSUB_MAX];
static float s_a_filtro;

static float s_nivel;        /* amplitud de la subportadora elegida */
static float s_picos[NSUB_MAX];  /* nivel alto de cada candidata */
static float s_pico;         /* el de la elegida */
static float s_caida;

/* Duraciones del formato en curso y las fronteras que salen de ellas. */
static float s_dur0, s_dur1, s_durm;
static float s_fr_min, s_fr_01, s_fr_1m, s_fr_max;
static float s_min_bajo;     /* lo mas bajo que se ha visto en el hueco */
static float s_prof;         /* cuanto baja la subportadora, 0..1 */
static uint16_t s_ritmo;     /* los ultimos 16 ciclos: 1 = duro lo que debia */
static float s_contraste;

static uint8_t  s_alto;          /* 1 mientras la subportadora esta fuerte */
static uint32_t s_n_alto;        /* muestras que lleva fuerte */
static uint32_t s_n_desde_subida;
static uint8_t  s_hubo_subida;
static uint32_t s_n_cambio;      /* muestras que lleva pidiendo cambiar */
static uint32_t s_n_estable;     /* las que hacen falta para creerselo */

static uint8_t  s_bits[60];      /* 0 no se usa: el segundo 00 no lleva bit */
static uint8_t  s_visto[60];
static uint8_t  s_seg;           /* en que segundo de la trama vamos */
static uint8_t  s_enganchado;

static dcf_estado_t s_estado;
static uint16_t s_ok, s_malas;
static uint16_t s_ms_marca, s_ms_ciclo, s_raras;

static uint8_t s_h, s_m, s_d, s_mes, s_a, s_ds, s_ver;
static uint8_t s_prev_valida, s_prev_h, s_prev_m, s_prev_d;

/* ---------------------------------------------------------------------------
 * ARRANQUE
 * ------------------------------------------------------------------------- */

void wwv_start(hora_emisora_t emisora, float fs_hz)
{
    uint8_t k, j;

    s_fs = (fs_hz > 1.0f) ? fs_hz : 1.0f;
    s_bpm = (uint8_t)(emisora == HORA_BPM);

    /*
     * El formato: que subportadoras buscar y cuanto duran sus pulsos.
     *
     * WWV: 100 Hz y nada mas que discutir, y pulsos de 170/470/770 ms.
     * BPM: 125 Hz segun su memoria tecnica y 100 Hz segun grabaciones
     *      recientes, asi que se buscan las dos (ver NSUB_MAX); pulsos de
     *      200/500/800 ms.
     */
    if (s_bpm) {
        s_n_sub = 2U;
        s_sub_hz[0] = 125.0f;
        s_sub_hz[1] = 100.0f;
        s_dur0 = 200.0f; s_dur1 = 500.0f; s_durm = 800.0f;
    } else {
        s_n_sub = 1U;
        s_sub_hz[0] = 100.0f;
        s_sub_hz[1] = 100.0f;
        s_dur0 = 170.0f; s_dur1 = 470.0f; s_durm = 770.0f;
    }
    s_sel = 0U;

    /* Las fronteras, en el punto medio de cada par, y los extremos con
     * DUR_MARGEN de holgura. Salen de las duraciones y no al reves, para que
     * cambiar un formato no obligue a recalcular cuatro numeros a mano. */
    s_fr_01  = 0.5f * (s_dur0 + s_dur1);
    s_fr_1m  = 0.5f * (s_dur1 + s_durm);
    s_fr_min = s_dur0 * (1.0f - DUR_MARGEN);
    s_fr_max = s_durm * (1.0f + DUR_MARGEN * 0.25f);

    for (j = 0U; j < NSUB_MAX; j++) {
        float ang = -6.2831853f * s_sub_hz[j] / s_fs;
        s_w_r[j] = cosf(ang);
        s_w_i[j] = sinf(ang);
        s_osc_r[j] = 1.0f;
        s_osc_i[j] = 0.0f;
        s_fi1[j] = 0.0f; s_fq1[j] = 0.0f;
        s_fi2[j] = 0.0f; s_fq2[j] = 0.0f;
        s_picos[j] = 0.0f;
    }
    s_osc_n = 0U;

    /* 1 - exp(-2*pi*fc/fs) linealizado; con fc muy por debajo de fs el error
     * no llega al 1%. Igual que en dcf77.c, y por la misma razon: ahorrarse
     * una exp() en el arranque sin que cambie nada medible. */
    s_a_filtro = 6.2831853f * FILTRO_HZ / s_fs;
    if (s_a_filtro > 1.0f) { s_a_filtro = 1.0f; }

    s_a_dc = 6.2831853f * DC_HZ / s_fs;
    if (s_a_dc > 1.0f) { s_a_dc = 1.0f; }
    s_dc = 0.0f;

    s_n_estable = (uint32_t)(ESTABLE_MS * s_fs / 1000.0f);
    if (s_n_estable < 1U) { s_n_estable = 1U; }
    s_n_cambio = 0U;

    s_caida = 1.0f - (1.0f / (PICO_CAIDA_S * s_fs));
    if (s_caida < 0.0f) { s_caida = 0.0f; }

    s_nivel = 0.0f;
    s_pico = 0.0f;
    s_min_bajo = 0.0f;
    s_prof = 0.0f;
    s_ritmo = 0U;
    s_contraste = 0.0f;
    s_alto = 0U;
    s_n_alto = 0U;
    s_n_desde_subida = 0U;
    s_hubo_subida = 0U;

    for (k = 0U; k < 60U; k++) { s_bits[k] = 0U; s_visto[k] = 0U; }
    s_seg = 0U;
    s_enganchado = 0U;

    s_estado = DCF_BUSCANDO;
    s_ok = 0U; s_malas = 0U;
    s_ms_marca = 0U; s_ms_ciclo = 0U; s_raras = 0U;
    s_prev_valida = 0U;
}

/* ---------------------------------------------------------------------------
 * DECODIFICACION
 * ------------------------------------------------------------------------- */

/* BCD del bit menos significativo al mas, dando la lista de pesos y la de
 * segundos donde vive cada uno. Se pasan las dos listas juntas porque en WWV
 * los bits de un mismo campo NO son consecutivos -hay huecos siempre-cero en
 * medio-, y escribir "del 10 al 17" seria mentira. */
static uint16_t campo(const uint8_t *b, const uint8_t *seg,
                      const uint16_t *peso, uint8_t n)
{
    uint8_t k;
    uint16_t v = 0U;
    for (k = 0U; k < n; k++) { if (b[seg[k]]) { v = (uint16_t)(v + peso[k]); } }
    return v;
}

/* Nibble BCD de cuatro segundos consecutivos, para comprobar que una cifra
 * decimal es una cifra decimal. Sin paridades -ninguna de las dos emisoras
 * lleva- esto es lo que caza un bit cambiado dentro de un campo: cuatro bits
 * llegan a 15 y una cifra solo a 9. */
static uint8_t nibble(uint8_t s0)
{
    return (uint8_t)(s_bits[s0] + 2U * s_bits[s0 + 1U]
                   + 4U * s_bits[s0 + 2U] + 8U * s_bits[s0 + 3U]);
}

/*
 * Lo que las dos emisoras comprueban igual: las seis marcas donde tocan,
 * ninguna marca de mas, ningun segundo sin simbolo, y los quince bits que
 * valen siempre cero -que son distintos en cada una-.
 */
static uint8_t estructura_ok(const uint8_t *ceros)
{
    uint8_t k, j;

    for (k = 0U; k < 6U; k++) {
        if (s_bits[k_marcas[k]] != 2U) { return 0U; }
    }
    for (k = 1U; k < 60U; k++) {
        uint8_t es_marca = 0U;
        for (j = 0U; j < 6U; j++) {
            if (k_marcas[j] == k) { es_marca = 1U; }
        }
        if (!es_marca && s_bits[k] > 1U) { return 0U; }
        if (!s_visto[k]) { return 0U; }   /* falto algun segundo */
    }
    for (k = 0U; k < 15U; k++) {
        if (s_bits[ceros[k]] != 0U) { return 0U; }
    }
    return 1U;
}

/*
 * Lo ultimo que hacen las dos: la trama describe el minuto que EMPEZO con el
 * hueco anterior, no el que empieza ahora. Es el convenio IRIG, al reves que
 * DCF77, y lo comparten porque BPM copio el formato de WWV. Aqui ya ha
 * pasado ese minuto entero, asi que lo que vale AHORA es un minuto mas.
 *
 * Despues, de UTC a hora de pared española con la regla europea. Los bits de
 * horario de verano de la trama son los de Estados Unidos y no sirven para
 * nada aqui.
 */
static void publica_utc(uint8_t m, uint8_t h, uint8_t dia, uint8_t mes, uint8_t anno)
{
    uint8_t hh = h, dd = dia, mm = mes, aa = anno, dsx = 0U, verano;

    if (m >= 59U) {
        m = 0U;
        hora_suma_horas(1, &hh, &dd, &mm, &aa, &dsx);
    } else {
        m = (uint8_t)(m + 1U);
    }

    verano = hora_verano_espanna(dd, mm, aa, hh);
    hora_suma_horas((int8_t)(verano ? 2 : 1), &hh, &dd, &mm, &aa, &dsx);
    s_h = hh; s_d = dd; s_mes = mm; s_a = aa; s_ds = dsx;
    s_m = m;
    s_ver = verano;
}

static uint8_t decodifica_wwv(void)
{
    static const uint8_t  k_min_s[7]  = { 10U, 11U, 12U, 13U, 15U, 16U, 17U };
    static const uint16_t k_min_p[7]  = {  1U,  2U,  4U,  8U, 10U, 20U, 40U };
    static const uint8_t  k_hor_s[6]  = { 20U, 21U, 22U, 23U, 25U, 26U };
    static const uint16_t k_hor_p[6]  = {  1U,  2U,  4U,  8U, 10U, 20U };
    static const uint8_t  k_doy_s[10] = { 30U, 31U, 32U, 33U, 35U, 36U, 37U, 38U, 40U, 41U };
    static const uint16_t k_doy_p[10] = {  1U,  2U,  4U,  8U, 10U, 20U, 40U, 80U, 100U, 200U };
    static const uint8_t  k_ann_s[8]  = {  4U,  5U,  6U,  7U, 51U, 52U, 53U, 54U };
    static const uint16_t k_ann_p[8]  = {  1U,  2U,  4U,  8U, 10U, 20U, 40U, 80U };

    uint16_t m, h, doy, anno;
    uint8_t dia, mes;

    if (!estructura_ok(k_ceros_wwv)) { return 0U; }

    m    = campo(s_bits, k_min_s, k_min_p, 7U);
    h    = campo(s_bits, k_hor_s, k_hor_p, 6U);
    doy  = campo(s_bits, k_doy_s, k_doy_p, 10U);
    anno = campo(s_bits, k_ann_s, k_ann_p, 8U);

    if (nibble(10U) > 9U) { return 0U; }   /* unidades del minuto */
    if (nibble(20U) > 9U) { return 0U; }   /* unidades de la hora */
    if (nibble(30U) > 9U) { return 0U; }   /* unidades del dia del año */
    if (nibble(35U) > 9U) { return 0U; }   /* decenas del dia del año */
    if (nibble(4U)  > 9U) { return 0U; }   /* unidades del año */
    if (nibble(51U) > 9U) { return 0U; }   /* decenas del año */

    if (m > 59U) { return 0U; }
    if (h > 23U) { return 0U; }
    if (anno > 99U) { return 0U; }
    if (!hora_dia_del_anno_a_fecha(doy, (uint8_t)anno, &dia, &mes)) { return 0U; }

    publica_utc((uint8_t)m, (uint8_t)h, dia, mes, (uint8_t)anno);
    return 1U;
}

/*
 * BPM. El reparto de los bits sale de la memoria tecnica del National Time
 * Service Center chino (MENG Zhi-mou, "Dissemination scheme of BPM shortwave
 * time code time-service", 2014):
 *
 *   00        referencia de trama, sin subportadora
 *   01..04    unidades del minuto      05  relleno
 *   06..08    decenas del minuto       09  marca P1
 *   10..13    unidades de la hora      14  relleno
 *   15..16    decenas de la hora       17,18 relleno   19  P2
 *   20..23    unidades del dia         24  relleno
 *   25..26    decenas del dia          27,28 relleno   29  P3
 *   30..33    unidades del mes         34  relleno
 *   35        decenas del mes          36..38 relleno  39  P4
 *   40..43    unidades del año         44  relleno
 *   45..48    decenas del año          49  P5
 *   50        signo de DUT1            51..54 DUT1 en decimas
 *   55,56     relleno                  57  aviso de segundo intercalar
 *   58        relleno                  59  marca P0
 *
 * Frente a WWV tiene una ventaja real: da el DIA DEL MES y el MES, mientras
 * que WWV da el dia del año y hay que convertirlo sabiendo si el año es
 * bisiesto. Menos cuentas es menos sitio donde equivocarse.
 */
static uint8_t decodifica_bpm(void)
{
    static const uint8_t  k_min_s[7] = {  1U,  2U,  3U,  4U,  6U,  7U,  8U };
    static const uint16_t k_min_p[7] = {  1U,  2U,  4U,  8U, 10U, 20U, 40U };
    static const uint8_t  k_hor_s[6] = { 10U, 11U, 12U, 13U, 15U, 16U };
    static const uint16_t k_hor_p[6] = {  1U,  2U,  4U,  8U, 10U, 20U };
    static const uint8_t  k_dia_s[6] = { 20U, 21U, 22U, 23U, 25U, 26U };
    static const uint16_t k_dia_p[6] = {  1U,  2U,  4U,  8U, 10U, 20U };
    static const uint8_t  k_mes_s[5] = { 30U, 31U, 32U, 33U, 35U };
    static const uint16_t k_mes_p[5] = {  1U,  2U,  4U,  8U, 10U };
    static const uint8_t  k_ann_s[8] = { 40U, 41U, 42U, 43U, 45U, 46U, 47U, 48U };
    static const uint16_t k_ann_p[8] = {  1U,  2U,  4U,  8U, 10U, 20U, 40U, 80U };

    uint16_t m, h, dia, mes, anno;

    if (!estructura_ok(k_ceros_bpm)) { return 0U; }

    m    = campo(s_bits, k_min_s, k_min_p, 7U);
    h    = campo(s_bits, k_hor_s, k_hor_p, 6U);
    dia  = campo(s_bits, k_dia_s, k_dia_p, 6U);
    mes  = campo(s_bits, k_mes_s, k_mes_p, 5U);
    anno = campo(s_bits, k_ann_s, k_ann_p, 8U);

    if (nibble(1U)  > 9U) { return 0U; }
    if (nibble(10U) > 9U) { return 0U; }
    if (nibble(20U) > 9U) { return 0U; }
    if (nibble(30U) > 9U) { return 0U; }
    if (nibble(40U) > 9U) { return 0U; }
    if (nibble(45U) > 9U) { return 0U; }   /* decenas del año */

    if (m > 59U) { return 0U; }
    if (h > 23U) { return 0U; }
    if (mes < 1U || mes > 12U) { return 0U; }
    if (anno > 99U) { return 0U; }
    if (dia < 1U || dia > hora_dias_del_mes((uint8_t)mes, (uint8_t)anno)) { return 0U; }

    publica_utc((uint8_t)m, (uint8_t)h, (uint8_t)dia, (uint8_t)mes, (uint8_t)anno);
    return 1U;
}

static uint8_t decodifica(void)
{
    return s_bpm ? decodifica_bpm() : decodifica_wwv();
}

/* Un minuto mas que la trama anterior. Misma comprobacion que en dcf77.c y
 * por la misma razon: es lo unico que el ruido no se salta. */
static uint8_t es_el_minuto_siguiente(void)
{
    uint8_t m = (uint8_t)(s_prev_m + 1U);
    uint8_t h = s_prev_h;

    if (m > 59U) { m = 0U; h = (uint8_t)(h + 1U); }
    if (h > 23U) { h = 0U; if (s_prev_d != s_d) { return (uint8_t)(s_m == m && s_h == h); } }
    return (uint8_t)(s_m == m && s_h == h && s_d == s_prev_d);
}

static void cierra_trama(void)
{
    if (!decodifica()) {
        s_malas++;
        s_prev_valida = 0U;
        s_estado = DCF_LEYENDO;
        return;
    }

    s_ok++;
    if (s_prev_valida && es_el_minuto_siguiente()) {
        s_estado = DCF_LISTO;
    } else {
        s_estado = DCF_CONFIRMANDO;
    }
    s_prev_valida = 1U;
    s_prev_h = s_h; s_prev_m = s_m; s_prev_d = s_d;
}

/* ---------------------------------------------------------------------------
 * FRENTE: de la envolvente a los bits
 * ------------------------------------------------------------------------- */

/* Guarda el simbolo que acaba de medirse (0, 1 o 2=marca) en el segundo que
 * toca, y avanza el contador. */
static void guarda(uint8_t simbolo)
{
    if (!s_enganchado) { return; }
    if (s_seg < 60U) {
        s_bits[s_seg] = simbolo;
        s_visto[s_seg] = 1U;
    }
}

/* Llega el hueco de dos segundos: acaba una trama y empieza la siguiente. El
 * pulso que viene despues del hueco es el del segundo 01. */
static void cierra_minuto(void)
{
    uint8_t k;

    if (s_enganchado) {
        /* Solo se cierra si se han visto los 59 segundos que hay. Si el
         * hueco llego antes o despues de tiempo, los bits estan corridos y
         * la trama no vale aunque todo lo demas cuadrara. */
        if (s_seg == 59U) {
            cierra_trama();
        } else {
            s_malas++;
            s_prev_valida = 0U;
            s_estado = DCF_LEYENDO;
        }
    } else {
        s_enganchado = 1U;
        s_estado = DCF_LEYENDO;
    }

    for (k = 0U; k < 60U; k++) { s_bits[k] = 0U; s_visto[k] = 0U; }
    s_seg = 1U;   /* el pulso que viene es el del segundo 01 */
}

void wwv_feed(float envolvente)
{
    float mag, x, umbral;
    uint8_t deseado, j;
    uint8_t renorm;

    /* --- 1. Quitarle la portadora (ver DC_HZ) --------------------------- */
    s_dc += s_a_dc * (envolvente - s_dc);
    x = envolvente - s_dc;

    /* --- 2. Bajar CADA subportadora candidata a banda base y filtrarla -- */
    renorm = (uint8_t)(++s_osc_n >= 512U);
    if (renorm) { s_osc_n = 0U; }

    for (j = 0U; j < s_n_sub; j++) {
        float r = s_osc_r[j] * s_w_r[j] - s_osc_i[j] * s_w_i[j];
        float i = s_osc_r[j] * s_w_i[j] + s_osc_i[j] * s_w_r[j];
        float m;
        s_osc_r[j] = r;
        s_osc_i[j] = i;
        if (renorm) {
            /* Un paso de Newton para devolverle el modulo a 1 sin una raiz:
             * g = 1,5 - 0,5*|z|^2 converge a 1/|z| cuando |z| ya esta cerca. */
            float g = 1.5f - 0.5f * (s_osc_r[j] * s_osc_r[j] + s_osc_i[j] * s_osc_i[j]);
            s_osc_r[j] *= g;
            s_osc_i[j] *= g;
        }

        s_fi1[j] += s_a_filtro * (x * s_osc_r[j] - s_fi1[j]);
        s_fq1[j] += s_a_filtro * (x * s_osc_i[j] - s_fq1[j]);
        s_fi2[j] += s_a_filtro * (s_fi1[j] - s_fi2[j]);
        s_fq2[j] += s_a_filtro * (s_fq1[j] - s_fq2[j]);

        m = sqrtf(s_fi2[j] * s_fi2[j] + s_fq2[j] * s_fq2[j]);
        s_picos[j] *= s_caida;
        if (m > s_picos[j]) { s_picos[j] = m; }
        if (j == s_sel) { mag = m; }
    }

    /*
     * Y se usa la que mas nivel tiene. La comparacion se hace solo cuando el
     * decodificador NO esta enganchado: cambiar de subportadora a mitad de
     * trama mezclaria bits de dos sitios, y eso es peor que quedarse en la
     * peor de las dos un minuto mas. Hace falta un 20% de ventaja para
     * cambiar, para que dos candidatas parecidas no se turnen.
     */
    if (s_n_sub > 1U && !s_enganchado) {
        uint8_t mejor = s_sel;
        for (j = 0U; j < s_n_sub; j++) {
            if (s_picos[j] > s_picos[mejor] * 1.20f) { mejor = j; }
        }
        if (mejor != s_sel) {
            s_sel = mejor;
            s_hubo_subida = 0U;
            s_alto = 0U;
            s_n_cambio = 0U;
        }
    }

    mag = sqrtf(s_fi2[s_sel] * s_fi2[s_sel] + s_fq2[s_sel] * s_fq2[s_sel]);
    s_nivel = mag;

    /* --- 3. Seguidor del nivel alto ------------------------------------ */
    s_pico = s_picos[s_sel];
    if (s_pico <= 0.0f) { return; }

    s_n_desde_subida++;
    if (s_alto) { s_n_alto++; } else if (mag < s_min_bajo) { s_min_bajo = mag; }

    /* --- 4. Comparador con histeresis Y antirrebote --------------------- */
    umbral = s_pico * (s_alto ? UMBRAL_BAJA : UMBRAL_SUBE);
    deseado = (uint8_t)(s_alto ? (mag >= umbral) : (mag > umbral));

    if (deseado == s_alto) { s_n_cambio = 0U; return; }

    s_n_cambio++;
    if (s_n_cambio < s_n_estable) { return; }

    /*
     * Cambio confirmado. Ocurrio s_n_estable muestras ATRAS, y con ese
     * numero se corrigen las dos duraciones: si se apuntaran con la hora de
     * ahora, cada tramo alto saldria ESTABLE_MS mas largo de la cuenta y el
     * umbral entre un 1 y una marca dejaria de estar donde dice estar.
     */
    s_n_cambio = 0U;

    if (!s_alto) {
        /* SUBIDA: empieza el tramo fuerte de este segundo. Lo primero es el
         * ciclo -cuanto ha pasado desde la subida anterior-, porque es lo
         * que distingue un segundo normal del hueco del minuto. */
        uint32_t ms_ciclo = (uint32_t)((float)(s_n_desde_subida - s_n_estable)
                                       * 1000.0f / s_fs);
        if (s_hubo_subida) {
            uint8_t bueno;
            s_ms_ciclo = (uint16_t)((ms_ciclo > 65535U) ? 65535U : ms_ciclo);
            /*
             * Para CONTAR el ritmo se usa una ventana mas estrecha que para
             * decodificar: +-5% en vez de +-20%. No es incoherencia, es a
             * proposito. Decodificar tiene que perdonar -un desvanecimiento
             * mueve el flanco y no por eso hay que tirar la trama-, pero el
             * numero de la pantalla tiene que distinguir señal de ruido, y
             * con la ventana ancha el ruido acertaba uno de cada tres ciclos
             * por pura casualidad y la barra se quedaba en 0,27 mirando nada.
             * Con +-5% eso baja a la mitad. Medido el 23/09/2026.
             */
            bueno = (uint8_t)(((float)ms_ciclo >= 950.0f && (float)ms_ciclo <= 1050.0f)
                           || ((float)ms_ciclo >= 1900.0f && (float)ms_ciclo <= 2100.0f));
            s_ritmo = (uint16_t)((s_ritmo << 1) | bueno);
            if ((float)ms_ciclo >= HUECO_MIN_MS && (float)ms_ciclo <= HUECO_MAX_MS) {
                cierra_minuto();
            } else if ((float)ms_ciclo >= SEG_MIN_MS && (float)ms_ciclo <= SEG_MAX_MS) {
                if (s_enganchado) {
                    s_seg++;
                    if (s_seg > 59U) {
                        /* Han pasado 60 subidas sin el hueco: o se perdio el
                         * hueco o esto no es WWV. La trama en curso ya no
                         * significa nada. */
                        s_enganchado = 0U;
                        s_estado = DCF_PERDIDO;
                        s_prev_valida = 0U;
                    }
                }
            } else {
                /* Un ciclo imposible: se ha perdido el ritmo. */
                s_raras++;
                if (s_enganchado) {
                    s_enganchado = 0U;
                    s_estado = DCF_PERDIDO;
                    s_prev_valida = 0U;
                }
            }
        }
        s_hubo_subida = 1U;
        s_n_desde_subida = s_n_estable;
        s_n_alto = s_n_estable;
        s_alto = 1U;
        {
            /*
             * EL NUMERO QUE SE ENSEÑA TIENE QUE PODER DECIR QUE NO HAY NADA.
             *
             * Lo evidente seria enseñar solo cuanto baja la subportadora,
             * (alto - bajo) / alto, que en WWV de verdad vale 0,83. El
             * problema es que con RUIDO PURO tambien vale casi 1: el seguidor
             * de pico se queda con el maximo de ocho segundos y el minimo
             * baja hasta el suelo, asi que la barra sale llena mirando nada.
             * Medido el 23/09/2026: 0,86, 1,00 y 0,93 con tres niveles de
             * ruido distintos.
             *
             * Por eso se multiplica por el RITMO: que fraccion de los ultimos
             * dieciseis ciclos han durado lo que tienen que durar, un segundo
             * -o dos, en el hueco del minuto-. Eso el ruido no lo imita: sus
             * cruces del umbral caen donde quieren. Lo que se enseña es la
             * conjuncion de las dos cosas, que es justo lo que hace que WWV
             * sea WWV y no una portadora cualquiera con ruido.
             */
            float c = (s_pico - s_min_bajo) / s_pico;
            uint8_t n, buenos = 0U;
            s_prof = (c < 0.0f) ? 0.0f : ((c > 1.0f) ? 1.0f : c);
            for (n = 0U; n < 16U; n++) {
                if (s_ritmo & (uint16_t)(1U << n)) { buenos++; }
            }
            s_contraste = s_prof * ((float)buenos / 16.0f);
        }
        s_min_bajo = mag;
    } else {
        /* BAJADA: se acabo el tramo fuerte. Su duracion es el simbolo. */
        uint32_t ms_alto = (uint32_t)((float)(s_n_alto - s_n_estable)
                                      * 1000.0f / s_fs);
        s_ms_marca = (uint16_t)((ms_alto > 65535U) ? 65535U : ms_alto);
        s_alto = 0U;
        s_min_bajo = mag;

        if ((float)ms_alto < s_fr_min || (float)ms_alto > s_fr_max) {
            /* Una duracion imposible: ese segundo se queda sin simbolo, y
             * decodifica() lo vera como un hueco en s_visto[] y tirara la
             * trama entera. Es lo correcto: media trama no es una hora. */
            s_raras++;
        } else if ((float)ms_alto < s_fr_01) {
            guarda(0U);
        } else if ((float)ms_alto < s_fr_1m) {
            guarda(1U);
        } else {
            guarda(2U);
        }
    }
}

void wwv_info(dcf_info_t *out)
{
    out->estado = (s_contraste < CONTRASTE_MIN && s_estado == DCF_LEYENDO)
                  ? DCF_BUSCANDO : s_estado;
    out->hora = s_h; out->minuto = s_m;
    out->dia = s_d; out->mes = s_mes; out->anno = s_a;
    out->dia_semana = s_ds;
    out->verano = s_ver;
    out->bits = s_seg;
    out->tramas_ok = s_ok;
    out->tramas_malas = s_malas;
    out->nivel = s_nivel;
    out->contraste = s_contraste;
    out->ms_marca = s_ms_marca;
    out->ms_ciclo = s_ms_ciclo;
    out->marcas_raras = s_raras;
}
