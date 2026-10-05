#include "stanag_rx.h"
#include "hfdl_ram.h"
#include "hfdl_viterbi.h"
#include <math.h>
#include <string.h>

/* Ver stanag_rx.h para el porque de cada numero. Aqui esta el como. */

#define FS          12000.0f
#define BD          2400.0f
#define SPS         5U                  /* 12000/2400, exactas */
#define PORTADORA   1800.0f
#define SIM_TRAMA   256U                /* 80 + 176 */
#define SIM_PRE     80U
#define SIM_DATOS   128U                /* los desconocidos de los 176 */
#define MUE_TRAMA   (SIM_TRAMA * SPS)   /* 1280 */
#define FASE_MUE    2                   /* medida; ver la cabecera */

#define RRC_SPAN    4U                  /* simbolos a cada lado */
#define RRC_TAPS    (2U * RRC_SPAN * SPS + 1U)   /* 41 */

#define ANILLO      2048U               /* complejas; una trama y media */
/*
 * LA COLA DE AUDIO: 2816 MUESTRAS, 235 ms, Y EN int16.
 *
 * *** El dueño, 02/10/2026: "perdiendo el audio". ***
 *
 * Con 1024 muestras en float la cola aguantaba 85 ms, y resulta que lo
 * que la desbordaba era ESTE MISMO MODULO: el barrido de portadora
 * bloquea el bucle principal mas de eso, asi que se comia su propia
 * entrada. Un decodificador que se queda sordo mientras busca no
 * encuentra nunca.
 *
 * En int16 cabe el doble en los mismos bytes. El precio es que hay que
 * fijar una escala, y para eso la ganancia se calcula en mete() -una
 * media lenta de |audio|, cuatro cuentas por muestra, nada al lado de lo
 * que ya hace la interrupcion-.
 *
 * Y NO ES DE TAMAÑO FIJO: se queda con TODO lo que sobre despues de
 * repartir el resto. El desentrelazador es lo que manda en el reparto y
 * cambia con la velocidad -5984 bytes a 600 bps, 11936 a 1200-, asi que
 * poner un numero fijo seria dimensionar para el caso peor y desperdiciar
 * seis kilobytes en el que mas se usa. A 600 bps con entrelazado largo
 * salen mas de seis mil muestras: medio segundo de margen.
 */
#define CRUDO_MIN   1024U
#define CRUDO_MAX   8192U
/*
 * EL ANILLO VA EN int16, NO EN float, Y NO ES TACAÑERIA.
 *
 * El desentrelazador del 4285 crece con la velocidad: sus 32 lineas
 * suman j*496 + 32 bytes, y eso son 5984 a 600 bps pero 11936 a 1200 y
 * 23840 a 2400, todo con entrelazado largo. Con el anillo en float se
 * iban 16 kB de los 32 prestados y el de 1200 ya no entraba por kilobyte
 * y pico. En int16 el anillo ocupa 8 kB y entran todos menos el de 2400
 * largo, que se rechaza en el arranque diciendo por que.
 *
 * La escala es 8192 = 1,0. El audio viene del demodulador de banda
 * lateral ya con control de ganancia, asi que ronda la unidad; a 8192 se
 * conservan mas de cuatro cifras y quedan dos bits de guarda antes de
 * saturar.
 */
#define ANILLO_ESC  8192.0f

#define FILAS       32U
#define DEIN_LARGO(j)  (((uint32_t)(j) * 496U) + 32U)

/*
 * LA VENTANA DEL VITERBI, Y POR QUE NO ES UN BLOQUE SUELTO.
 *
 * El 4285 emite seguido: no hay bloques con principio y final que
 * permitan arrancar y terminar el trellis en el estado 0. Asi que se
 * decodifica a ventana corrediza: VIT_VENT bits, de los que se tiran los
 * VIT_MARGEN primeros -donde el trellis todavia no ha olvidado que no
 * sabiamos el estado de partida- y los VIT_MARGEN ultimos, que no tienen
 * cola por detras. Lo de en medio es lo que sale, y la ventana avanza
 * justo eso.
 */
#define VIT_MARGEN  32U
#define VIT_SALEN   256U
#define VIT_VENT    (VIT_SALEN + (2U * VIT_MARGEN))

/* --- las dos secuencias de la norma, generadas una vez ---------------- */
static int8_t  s_pre[SIM_PRE];          /* +1 / -1 */
static uint8_t s_scr[176];              /* indice 8PSK 0..7 */
static uint8_t s_conocido[176];

static void tablas(void)
{
    uint8_t est5[5] = { 1U, 1U, 0U, 1U, 0U };
    uint8_t est9[9];
    uint8_t i, k;

    for (i = 0U; i < SIM_PRE; i++) {
        uint8_t b = est5[4];
        uint8_t n = (uint8_t)(est5[2] ^ est5[4]);   /* tomas en 2 y 4 */
        s_pre[i] = (b != 0U) ? (int8_t)-1 : (int8_t)1;   /* 0 -> fase 0 */
        for (k = 4U; k > 0U; k--) { est5[k] = est5[k - 1U]; }
        est5[0] = n;
    }
    for (i = 0U; i < 9U; i++) { est9[i] = 1U; }
    for (i = 0U; i < 176U; i++) {
        uint8_t r;
        s_scr[i] = (uint8_t)((est9[6] << 2) | (est9[7] << 1) | est9[8]);
        for (r = 0U; r < 3U; r++) {
            uint8_t n = (uint8_t)(est9[4] ^ est9[8]);   /* tomas 4 y 8 */
            for (k = 8U; k > 0U; k--) { est9[k] = est9[k - 1U]; }
            est9[0] = n;
        }
        s_conocido[i] = (uint8_t)(((i % 48U) >= 32U) ? 1U : 0U);
    }
}

/* --- el filtro adaptado ---------------------------------------------- */
/*
 * Los coeficientes y las dos colas tambien salen del prestamo, no de RAM
 * permanente: son 492 bytes que solo hacen falta mientras el modo esta
 * encendido, y el enlace para el GD32 se paso por 332 cuando los tenia
 * estaticos. La regla de la casa -cero RAM nueva permanente- no era una
 * manera de hablar.
 */
static float *s_rrc;
static float *s_colaI;
static float *s_colaQ;

static void rrc_haz(void)
{
    const float beta = 0.35f;
    uint32_t i;
    float e = 0.0f;

    for (i = 0U; i < RRC_TAPS; i++) {
        float t = ((float)(int32_t)i - (float)(RRC_TAPS / 2U)) / (float)SPS;
        float v;
        float a = fabsf(t);
        if (a < 1e-6f) {
            v = 1.0f - beta + (4.0f * beta / 3.14159265f);
        } else if (fabsf((4.0f * beta * a) - 1.0f) < 1e-5f) {
            v = (beta / 1.41421356f)
              * ((1.0f + (2.0f / 3.14159265f)) * sinf(3.14159265f / (4.0f * beta))
               + (1.0f - (2.0f / 3.14159265f)) * cosf(3.14159265f / (4.0f * beta)));
        } else {
            float d = 3.14159265f * t * (1.0f - ((4.0f * beta * t) * (4.0f * beta * t)));
            v = (sinf(3.14159265f * t * (1.0f - beta))
               + 4.0f * beta * t * cosf(3.14159265f * t * (1.0f + beta))) / d;
        }
        s_rrc[i] = v;
        e += v * v;
    }
    e = sqrtf(e);
    for (i = 0U; i < RRC_TAPS; i++) { s_rrc[i] /= e; }
}

/* --- memoria prestada ------------------------------------------------- */
/* La cola de audio que llena la interrupcion. Ver mete(). */
static int16_t *s_crudo;
static uint32_t s_crudo_n;     /* cuantas muestras cabe: lo que sobro */
static float    s_nivel;       /* media lenta de |audio|, para la ganancia */
static volatile uint32_t s_crudo_w;   /* lo escribe la interrupcion */
static uint32_t          s_crudo_r;   /* lo lee el bucle principal */
static uint16_t s_perdidas;           /* veces que la cola se desbordo */

static int16_t *s_anillo;      /* 2*ANILLO: complejo intercalado, ya filtrado */
static float   *s_simR;        /* SIM_TRAMA */
static float   *s_simI;        /* SIM_TRAMA */
static int8_t  *s_dein;        /* DEIN_MAX */
static uint8_t *s_vitbuf;      /* decisiones del Viterbi */
static uint8_t *s_metricas;    /* las dos de hfdl_viterbi */
static uint8_t *s_blandos;     /* 2*VIT_BLOQUE entradas al Viterbi */
static uint8_t *s_salida;      /* VIT_BLOQUE/8 bytes */

static uint32_t s_escrito;     /* muestras metidas en el anillo, total */
static uint32_t s_leido;       /* por donde va el sincronismo */
static uint8_t  s_arrancado;

/* estado del oscilador de bajada y del filtro */
static uint32_t s_cola_n;   /* indice del hueco mas nuevo de la cola circular */

/* sincronismo */
static uint8_t  s_engancha;
static uint32_t s_prox;        /* muestra absoluta del proximo preambulo */
static float    s_dw;          /* residuo fino, rad/simbolo */
static float    s_dw_gruesa;   /* lo que se encontro barriendo la portadora */
static uint32_t s_ult_busca;   /* cuando se barrio por ultima vez */
/*
 * EL ANCHO DEL BARRIDO DE PORTADORA, QUE SE ADAPTA.
 *
 * El desvio normal es pequeño -el de la grabacion del dueño eran 58 Hz-
 * asi que casi siempre basta con +-100. Barrer +-250 desde el primer
 * intento es pagar once pasos para encontrarlo en el tercero. Se arranca
 * en +-100 (cinco pasos) y cada busqueda fallida ensancha uno hasta
 * +-250; al enganchar vuelve a estrecharse.
 */
static uint8_t  s_ancho = 2U;  /* pasos a cada lado del centro */
static uint16_t s_busquedas;   /* cuantas se han hecho: lo mira el banco */
static uint8_t  s_hay_portadora;  /* ya se encontro una vez */
static uint8_t  s_fallos_rapidos; /* reenganches baratos fallidos seguidos */
static uint8_t  s_mitad;          /* que mitad de la trama toca barrer */
/* El barrido va a trozos; esto es por donde iba. Ver busca_todo(). */
static uint8_t  s_sw_k;        /* por que portadora va */
static float    s_sw_v = -1.0f;/* lo mejor visto en esta pasada */
static uint32_t s_sw_m;
static float    s_sw_dw;
/* Lo mejor que ha visto el barrido, para enseñarlo aunque no le sirva.
 * Ver el comentario de corr/corr_hz en stanag_rx.h. */
static uint8_t  s_corr;
static int16_t  s_corr_hz;
static float    s_fondo_s;     /* suma de correlaciones, para el fondo */
static uint32_t s_fondo_n;
static uint8_t  s_trama;       /* autocorrelacion a una trama, x100 */
static uint8_t  s_corr_max;
static uint8_t  s_sondeos_max;
static uint16_t s_tramas_bien;
static uint16_t s_tramas;
static uint8_t  s_sondeos;

/* desentrelazador */
static uint16_t s_dein_ini[FILAS];
static uint16_t s_dein_len[FILAS];
static uint16_t s_dein_pos[FILAS];
static uint32_t s_grupos;
static uint8_t  s_j;           /* incremento: 12 largo, 1 corto */

/*
 * LOS VALORES DE PARTIDA, Y NO SON LOS PRIMEROS DE LA LISTA.
 *
 * 600 bps con entrelazado largo: es lo que usan las costeras francesas
 * en sus frecuencias barco-costera, que son las unicas que emiten EN
 * CLARO, y ademas es el caso mas sencillo de la familia -tasa 1/2 sin
 * repeticion-. Arrancar en 75, que es el primero del enum, dejaba la
 * pantalla en blanco catorce segundos con la velocidad menos probable.
 */
static uint8_t s_vel = STANAG_RX_600;
static uint8_t s_largo = 1U;

/* acumulador de bits blandos hacia el Viterbi */
static uint16_t s_nblandos;   /* OJO: 2*VIT_VENT son 640, no cabe en uint8_t */
static uint8_t  s_ber;
static uint8_t  s_ber_vale;   /* 0 = el numero de arriba no mide nada */
static uint8_t  s_ber_equi;   /* la ventana de ahora estaba equilibrada */
static uint8_t  s_ber_mudo;   /* ventanas seguidas sin poder medir */

/* acumuladores de los formatos de salida */
static uint8_t  s_bitacum;
static uint8_t  s_nbitacum;
static uint16_t s_ita2reg;
static uint8_t  s_ita2n;
static uint8_t  s_ita2fase;
static uint8_t  s_ita2cif;
static uint8_t  s_ita2mal;
static uint8_t  s_ita2cnt;

static int8_t  s_resto[16];
static uint8_t s_nresto;

/* texto */
static char    s_lin[STANAG_RX_LINEAS][STANAG_RX_LARGO];
static uint8_t s_nlin;
static uint8_t s_col;

/*
 * LA TABLA DEL ANEXO E, EN TRES FUNCIONES.
 *
 *   bps   simbolo  bits/sim  FEC    repeticion  incremento del
 *                                   de parejas  entrelazador (largo/corto)
 *     75   BPSK        1     1/16       8            12 / 1
 *    150   BPSK        1     1/8        4            12 / 1
 *    300   BPSK        1     1/4        2            12 / 1
 *    600   BPSK        1     1/2        1            12 / 1
 *   1200   QPSK        2     1/2        1            24 / 2
 *   2400   8PSK        3     2/3        1            48 / 4
 *
 * El 2400 no lleva repeticion sino PERFORADO, y la norma lo hace en un
 * sitio raro: a la salida del entrelazador, saltando una fila de cada
 * cuatro. Aqui eso se deshace al reves - se meten las 24 filas que han
 * llegado y en las ocho que faltan (las de indice 3 modulo 4) se pone un
 * cero, que para un Viterbi blando es "no tengo ni idea" y es justo lo
 * que hay que decirle.
 */
static uint8_t repeticiones(void)
{
    switch (s_vel) {
    case STANAG_RX_300: return 2U;
    case STANAG_RX_150: return 4U;
    case STANAG_RX_75:  return 8U;
    default:            return 1U;
    }
}

static uint8_t bits_simbolo(void)
{
    if (s_vel == STANAG_RX_2400) { return 3U; }
    if (s_vel == STANAG_RX_1200) { return 2U; }
    return 1U;
}

static uint8_t incremento(void)
{
    if (s_vel == STANAG_RX_2400) { return (uint8_t)(s_largo ? 48U : 4U); }
    if (s_vel == STANAG_RX_1200) { return (uint8_t)(s_largo ? 24U : 2U); }
    return (uint8_t)(s_largo ? 12U : 1U);
}

static uint8_t perfora(void) { return (uint8_t)((s_vel == STANAG_RX_2400) ? 1U : 0U); }

static const char *s_porque = "";
const char *stanag_rx_porque(void) { return s_porque; }

static const char *k_vel_txt[STANAG_RX_VELOCIDADES] = {
    "75", "150", "300", "600", "1200", "2400"
};
const char *stanag_rx_velocidad_txt(uint8_t v)
{ return (v < STANAG_RX_VELOCIDADES) ? k_vel_txt[v] : "?"; }

static const char *k_fmt_txt[STANAG_RX_FORMATOS] = { "ASCII", "ITA2", "HEX" };
const char *stanag_rx_formato_txt(uint8_t f)
{ return (f < STANAG_RX_FORMATOS) ? k_fmt_txt[f] : "?"; }

static uint8_t s_fmt;
void    stanag_rx_formato_pon(uint8_t f)
{ s_fmt = (uint8_t)((f < STANAG_RX_FORMATOS) ? f : STANAG_RX_ASCII); }
uint8_t stanag_rx_formato(void) { return s_fmt; }

uint8_t stanag_rx_init(float *trabajo, uint32_t n_floats)
{
    uint8_t *b;
    uint32_t i, off;

    s_porque = "";
    if (trabajo == 0 || n_floats < STANAG_RX_FLOATS) {
        s_porque = "no hay memoria que prestar"; return 0U;
    }
    if (((uintptr_t)trabajo & 3U) != 0U) {
        s_porque = "la memoria prestada no esta alineada"; return 0U;
    }

    tablas();

    /*
     * TODO lo que necesita sale del prestamo: ni un byte de RAM nueva
     * permanente, igual que hfdl_viterbi y el detector. El reparto:
     *
     *   anillo     2048 complejas en int16   8192
     *   simbolos   2*256 floats               2048
     *   filtro     3*41  floats                492   (coeficientes y las dos colas)
     *   dein                                  j*496 + 32
     *   blandos                               2*VIT_VENT
     *   salida                                VIT_VENT/8 + 1
     *   metricas                               512
     *   decisiones                            lo que queda, y se comprueba
     *
     * El desentrelazador crece con la velocidad: 5984 bytes a 600 bps,
     * 11936 a 1200 y 23840 a 2400, siempre con entrelazado largo. El
     * ultimo no cabe y se rechaza mas abajo diciendo por que.
     */
    s_j = incremento();
    s_anillo = (int16_t *)trabajo;
    s_simR   = &trabajo[ANILLO];                      /* el anillo ocupa la mitad */
    s_simI   = &trabajo[ANILLO + SIM_TRAMA];
    s_rrc    = &trabajo[ANILLO + (2U * SIM_TRAMA)];
    s_colaI  = &trabajo[ANILLO + (2U * SIM_TRAMA) + RRC_TAPS];
    s_colaQ  = &trabajo[ANILLO + (2U * SIM_TRAMA) + (2U * RRC_TAPS)];
    b = (uint8_t *)&trabajo[ANILLO + (2U * SIM_TRAMA) + (3U * RRC_TAPS)];
    /*
     * CADA TROZO ALINEADO A CUATRO, Y NO ES MANIA.
     *
     * *** El dueño, 02/10/2026: "al darle a auto se ha quedado la radio
     * tostada, el audio sigue pero todo lo demas no" / "me ha reiniciado
     * los ajustes". ***
     *
     * Era esto. s_salida mide VIT_VENT/8+1 = 41 bytes, que no es
     * multiplo de cuatro, asi que las metricas y las decisiones del
     * Viterbi -que son arrays de uint32_t- caian a un byte de su sitio.
     * En el PC eso funciona y el banco pasaba en verde; en el Cortex-M4
     * el compilador usa LDRD/STM sobre esas estructuras y un acceso
     * desalineado es HardFault. La radio se quedaba con el audio
     * sonando -va por DMA- y todo lo demas muerto.
     *
     * Ahora cada trozo se redondea hacia arriba, y la comprobacion de
     * abajo no es decorativa: si alguna vez vuelve a quedar torcido, el
     * modo no arranca y lo dice, en vez de tirar la radio.
     */
    #define ALINEA4(p)  ((p) = (uint8_t *)((((uintptr_t)(p)) + 3U) & ~(uintptr_t)3U))
    ALINEA4(b);
    s_dein    = (int8_t *)b;   b += DEIN_LARGO(s_j);   ALINEA4(b);
    s_blandos = b;             b += 2U * VIT_VENT;     ALINEA4(b);
    s_salida  = b;             b += (VIT_VENT / 8U) + 1U;  ALINEA4(b);
    s_metricas = b;            b += 2U * sizeof(hfdl_viterbi_metric_t);  ALINEA4(b);
    s_vitbuf  = b;             b += HFDL_VITERBI_DECISION_BYTES(VIT_VENT);
    ALINEA4(b);
    /* Y LA COLA DE AUDIO SE QUEDA CON LO QUE SOBRE. Ver su comentario. */
    s_crudo   = (int16_t *)b;
    if ((((uintptr_t)s_metricas | (uintptr_t)s_vitbuf
        | (uintptr_t)s_crudo) & 3U) != 0U) {
        s_porque = "el reparto de memoria ha quedado desalineado";
        return 0U;
    }
    {
        uint32_t usado = (uint32_t)(b - (uint8_t *)trabajo);
        uint32_t total = n_floats * 4U;

        s_crudo_n = (usado < total) ? ((total - usado) / 2U) : 0U;
        if (s_crudo_n > CRUDO_MAX) { s_crudo_n = CRUDO_MAX; }

        /*
         * El unico que no cabe es el de 2400 bps con entrelazado largo:
         * sus 32 lineas con incremento 48 piden 23840 bytes y, con el
         * anillo y lo demas, se pasan de los 32 kB prestados. Se dice
         * cual es y por que, en vez de arrancar y sacar basura.
         */
        if (usado > total || s_crudo_n < CRUDO_MIN) {
            s_porque = "el entrelazado largo de 2400 bps no cabe en la memoria prestada";
            return 0U;
        }
    }

    rrc_haz();            /* ya hay sitio donde dejar los coeficientes */
    for (i = 0U; i < (2U * ANILLO); i++) { s_anillo[i] = 0; }
    for (i = 0U; i < DEIN_LARGO(s_j); i++) { s_dein[i] = 0; }
    for (i = 0U; i < RRC_TAPS; i++) { s_colaI[i] = 0.0f; s_colaQ[i] = 0.0f; }

    off = 0U;
    for (i = 0U; i < FILAS; i++) {
        uint16_t l = (uint16_t)(((uint32_t)s_j * (31U - i)) + 1U);
        s_dein_ini[i] = (uint16_t)off;
        s_dein_len[i] = l;
        s_dein_pos[i] = 0U;
        off += l;
    }

    s_escrito = 0U; s_leido = 0U; s_cola_n = 0U;
    s_crudo_w = 0U; s_crudo_r = 0U; s_perdidas = 0U; s_nivel = 0.0f;
    for (i = 0U; i < s_crudo_n; i++) { s_crudo[i] = 0; }
    s_engancha = 0U; s_prox = 0U; s_dw = 0.0f; s_dw_gruesa = 0.0f; s_ult_busca = 0U; s_ancho = 2U; s_busquedas = 0U;
    s_hay_portadora = 0U; s_fallos_rapidos = 0U; s_mitad = 0U;
    s_corr = 0U; s_corr_hz = 0;
    s_sw_k = 0U; s_sw_v = -1.0f; s_sw_m = 0U; s_sw_dw = 0.0f;
    s_fondo_s = 0.0f; s_fondo_n = 0U;
    s_trama = 0U; s_corr_max = 0U; s_sondeos_max = 0U; s_tramas_bien = 0U;
    s_tramas = 0U; s_sondeos = 0U; s_grupos = 0U;
    s_nblandos = 0U; s_nlin = 0U; s_col = 0U; s_ber = 0U; s_ber_vale = 0U; s_ber_equi = 0U; s_ber_mudo = 0U;
    s_bitacum = 0U; s_nbitacum = 0U; s_nresto = 0U;
    s_ita2reg = 0U; s_ita2n = 0U; s_ita2fase = 0U;
    s_ita2cif = 0U; s_ita2mal = 0U; s_ita2cnt = 0U;
    for (i = 0U; i < STANAG_RX_LINEAS; i++) { s_lin[i][0] = '\0'; }
    s_arrancado = 1U;
    return 1U;
}

void stanag_rx_fin(void) { s_arrancado = 0U; }

void stanag_rx_velocidad_pon(uint8_t v)
{
    s_vel = (uint8_t)((v < STANAG_RX_VELOCIDADES) ? v : STANAG_RX_600);
}
uint8_t stanag_rx_velocidad(void) { return s_vel; }
void    stanag_rx_largo_pon(uint8_t l) { s_largo = (uint8_t)(l ? 1U : 0U); }
uint8_t stanag_rx_largo(void) { return s_largo; }

/*
 * LA INTERRUPCION DE AUDIO SOLO COPIA. NADA MAS.
 *
 * *** El comentario de demod_am.c donde se llama a esto lo dice desde
 * que existe el detector: "lo que hace por bloque es un oscilador de
 * ocho valores y una suma: acotado y proporcional al bloque". ***
 *
 * La primera version de este modulo rompio esa promesa: metia aqui dos
 * sinf, dos cosf y un filtro de 41 tomas POR MUESTRA, o sea unos 700
 * ciclos por muestra dentro de la interrupcion. Que el audio aguantara
 * no lo hace correcto: la interrupcion es de todo el mundo y esto es un
 * modo mas.
 *
 * Ahora se guarda el audio crudo en una cola y el trabajo -bajar a banda
 * base y filtrar- se hace en stanag_rx_paso(), desde el bucle principal.
 * Si el bucle se retrasa tanto que la cola se llena, se tiran las
 * muestras viejas y se CUENTA, que es mejor que mezclar trozos de dos
 * sitios y no enterarse.
 */
void stanag_rx_mete(const float *audio, uint32_t n)
{
    uint32_t k, w;

    if (!s_arrancado || audio == 0) { return; }

    w = s_crudo_w;
    for (k = 0U; k < n; k++) {
        /*
         * La ganancia va aqui porque la cola es de enteros y hay que
         * fijarle una escala. El audio de banda lateral no viene
         * normalizado a la unidad, asi que se mide su nivel medio -una
         * milesima por muestra, unos 80 ms de constante- y se divide.
         * Acotada para que el silencio no la haga explotar.
         */
        float x = audio[k];
        float v;
        s_nivel += 0.001f * (fabsf(x) - s_nivel);
        v = (s_nivel > 1e-12f) ? ((x * 0.25f * ANILLO_ESC) / s_nivel)
                               : (x * ANILLO_ESC);
        if (v >  32767.0f) { v =  32767.0f; }
        if (v < -32767.0f) { v = -32767.0f; }
        s_crudo[w % s_crudo_n] = (int16_t)v;
        w++;
    }
    s_crudo_w = w;
}

/*
 * EL OSCILADOR DE BAJADA, EXACTO Y SIN TRIGONOMETRIA.
 *
 * 1800 / 12000 = 3/20, asi que la fase se repite cada VEINTE muestras
 * clavadas: la tabla no acumula error nunca, al contrario que una
 * recurrencia de fase. Es el mismo truco que el detector usa con su
 * 1500 Hz y sus ocho valores.
 */
static const float k_cos20[20] = {
      1.00000000f,   0.95105652f,   0.80901699f,   0.58778525f,   0.30901699f,
      0.00000000f,  -0.30901699f,  -0.58778525f,  -0.80901699f,  -0.95105652f,
     -1.00000000f,  -0.95105652f,  -0.80901699f,  -0.58778525f,  -0.30901699f,
     -0.00000000f,   0.30901699f,   0.58778525f,   0.80901699f,   0.95105652f
};

static void muele(void)
{
    uint32_t w = s_crudo_w;

    /* si el bucle principal ha ido tarde, se tira lo mas viejo */
    if ((w - s_crudo_r) > s_crudo_n) {
        /*
         * PERDER AUDIO INVALIDA EL RELLENO, Y HAY QUE DECIRLO AL
         * CONTADOR, NO SOLO AL SINCRONISMO.
         *
         * El entrelazado largo junta bits repartidos por diez segundos.
         * Si en medio falta un trozo de audio, lo que sale son bits de
         * antes y de despues del hueco mezclados: basura con buena cara.
         * Asi que el relleno vuelve a cero y las lineas se limpian. Es
         * la misma regla que el "no sueltes nada hasta llenar": mas vale
         * tardar otros diez segundos que sacar letras que no son.
         */
        uint32_t z;
        s_perdidas = (uint16_t)((s_perdidas < 60000U) ? (s_perdidas + 1U) : s_perdidas);
        s_crudo_r = w - s_crudo_n;
        s_engancha = 0U;
        s_grupos = 0U;
        s_nblandos = 0U;
        s_nresto = 0U;
        for (z = 0U; z < DEIN_LARGO(s_j); z++) { s_dein[z] = 0; }
    }

    while (s_crudo_r != w) {
        /*
         * LA GANANCIA, QUE ES LO QUE ME FALTABA.
         *
         * *** El dueño, 02/10/2026: el detector veia la señal al 100 % de
         * calidad y el demodulador decia "buscando 0" en la misma
         * frecuencia. ***
         *
         * Y era esto. El detector no mira la amplitud: normaliza su
         * autocorrelacion por la energia (ver a0 en stanag_det.c), asi
         * que le da igual a que escala venga el audio. Yo guardaba en un
         * anillo de ENTEROS con una escala fija de 8192 = 1,0, y el audio
         * de banda lateral no viene normalizado a la unidad: se saturaba
         * contra el tope del int16 y lo que quedaba era una onda cuadrada
         * donde el preambulo no correlaciona con nada.
         *
         * Ahora se mide el nivel medio y se divide por el. Una media
         * lenta -una milesima por muestra, unos 80 ms de constante- para
         * que un desvanecimiento no la mueva de golpe, y la ganancia
         * acotada para que el silencio no la haga explotar.
         */
        float x = (float)s_crudo[s_crudo_r % s_crudo_n] / ANILLO_ESC;
        uint32_t f = (3U * s_crudo_r) % 20U;   /* exacta: 1800/12000 = 3/20 */
        float ci = k_cos20[f];
        /* hace falta -sen(2*pi*f/20), y cos(a + 90) es justo eso */
        float si = k_cos20[(f + 5U) % 20U];
        float ai = 0.0f, aq = 0.0f;
        uint32_t t, p;

        s_crudo_r++;

        /* cola circular: s_cola_n apunta al hueco mas nuevo */
        s_cola_n = (s_cola_n == 0U) ? (RRC_TAPS - 1U) : (s_cola_n - 1U);
        s_colaI[s_cola_n] = x * ci;
        s_colaQ[s_cola_n] = x * si;

        for (t = 0U; t < RRC_TAPS; t++) {
            uint32_t q = s_cola_n + t;
            if (q >= RRC_TAPS) { q -= RRC_TAPS; }
            ai += s_colaI[q] * s_rrc[t];
            aq += s_colaQ[q] * s_rrc[t];
        }
        {
            float vi = ai * ANILLO_ESC, vq = aq * ANILLO_ESC;
            if (vi >  32767.0f) { vi =  32767.0f; }
            if (vi < -32767.0f) { vi = -32767.0f; }
            if (vq >  32767.0f) { vq =  32767.0f; }
            if (vq < -32767.0f) { vq = -32767.0f; }
            p = (s_escrito % ANILLO) * 2U;
            s_anillo[p] = (int16_t)vi;
            s_anillo[p + 1U] = (int16_t)vq;
        }
        s_escrito++;
    }
}

static void anillo_lee(uint32_t idx, float *re, float *im)
{
    uint32_t p = (idx % ANILLO) * 2U;
    *re = (float)s_anillo[p] / ANILLO_ESC;
    *im = (float)s_anillo[p + 1U] / ANILLO_ESC;
}

/* --- sincronismo de trama -------------------------------------------- */
/*
 * Correlacion del preambulo entero, normalizada por la energia local.
 * El truco para que no cueste un ojo: como el molde vale lo mismo
 * durante las cinco muestras de cada simbolo, primero se suman de cinco
 * en cinco y luego solo hacen falta 80 sumas por desplazamiento en vez
 * de 400.
 */
/*
 * LA CORRELACION DEL PREAMBULO, CON LA PORTADORA COMO PARAMETRO.
 *
 * *** El dueño, 02/10/2026, con una señal que el identificador veia al
 * 100 % de calidad: "no lo hace, no sale", "buscando 0". ***
 *
 * Y la culpa era de dar por supuesto el 1800. En su grabacion de 4390 la
 * portadora esta en 1850: barriendo de 1400 a 2300 la correlacion del
 * preambulo da 0,312 en 1800 y 0,570 en 1850. Con el umbral en 0,45, a
 * 1800 no engancha NUNCA.
 *
 * Cincuenta hercios bastan para esto y sobran: ochenta simbolos a 2400
 * baudios son 33 ms, y 50 Hz giran 10,5 radianes en ese rato. El
 * preambulo deja de parecerse a si mismo.
 *
 * Y el estimador fino que ya habia no podia salvarlo: saca el desvio del
 * periodo 31 del preambulo con un atan2 dividido por 31, asi que solo es
 * univoco mientras |dw*31| < pi, o sea por debajo de 38,7 Hz. Hace falta
 * una busqueda GRUESA delante.
 *
 * La norma, por cierto, lo dice: el centro del decodificador ha de poder
 * moverse +-400 Hz. Eso es lo que se barre.
 *
 * El truco para que no cueste un ojo sigue siendo el mismo: como el
 * molde vale lo mismo durante las cinco muestras de cada simbolo, se
 * suman de cinco en cinco y solo hacen falta 80 giros y 80 sumas por
 * desplazamiento, no 400.
 */
static float correla(uint32_t ini, float dw)
{
    float cr = 0.0f, ci = 0.0f, en = 0.0f;
    float gr = 1.0f, gi = 0.0f;        /* el giro acumulado, simbolo a simbolo */
    float pr = cosf(dw), pi2 = sinf(dw);
    uint32_t i;

    for (i = 0U; i < SIM_PRE; i++) {
        float sr = 0.0f, si = 0.0f;
        float ar, ai;
        uint32_t k;
        for (k = 0U; k < SPS; k++) {
            float re, im;
            anillo_lee(ini + (i * SPS) + k, &re, &im);
            sr += re; si += im;
            en += (re * re) + (im * im);
        }
        ar = (sr * gr) - (si * gi);
        ai = (sr * gi) + (si * gr);
        if (s_pre[i] > 0) { cr += ar; ci += ai; }
        else              { cr -= ar; ci -= ai; }
        {   /* gira un simbolo mas */
            float nr = (gr * pr) - (gi * pi2);
            gi = (gr * pi2) + (gi * pr);
            gr = nr;
        }
    }
    if (en < 1e-20f) { return 0.0f; }
    return sqrtf((cr * cr) + (ci * ci)) / sqrtf(en * (float)(SIM_PRE * SPS));
}

/*
 * EL UMBRAL BAJA A 0,30, Y LA RED SE PONE DONDE DEBE ESTAR.
 *
 * *** El dueño, 02/10/2026, en 4334 (FUX, una de las costeras que emiten
 * en claro): "preambulo 28 de 45 -> lo veo, no llega", sin perder audio
 * y con la portadora dentro del barrido. ***
 *
 * O sea: hay preambulo ahi, pero flojo. Con el umbral en 0,45 -que salio
 * de una grabacion limpia- una señal real de S8 con interferencia al
 * lado no entra nunca. El fondo del ruido anda por 0,09, asi que 0,30
 * sigue siendo tres veces el ruido.
 *
 * Y bajar el umbral de enganche NO es relajar la exigencia, porque la
 * exigencia se ha movido a un sitio mucho mejor: los SIMBOLOS DE SONDEO.
 * Son 48 por trama de valor conocido, asi que si la demodulacion no esta
 * saliendo, no caen donde tienen que caer y se ve enseguida. Una trama
 * con los sondeos mal ya no cuenta para el relleno (ver trama_saca), asi
 * que un enganche falso no llega nunca a soltar texto: se queda dando
 * vueltas sin llenar.
 *
 * Es la misma idea de siempre: mejor intentarlo y comprobar el
 * resultado, que no intentarlo por si acaso.
 */
#define UMBRAL  0.30f
#define SONDEOS_MIN  40U

/* Seguimiento: la portadora ya se sabe, solo se afina el instante. */
static uint8_t busca(uint32_t desde, uint32_t cuantos, uint32_t *mejor, float *q)
{
    uint32_t i, m = desde;
    float v = -1.0f;

    for (i = 0U; i < cuantos; i++) {
        float c = correla(desde + i, -s_dw_gruesa);
        if (c > v) { v = c; m = desde + i; }
    }
    *mejor = m; *q = v;
    return (uint8_t)((v > UMBRAL) ? 1U : 0U);
}

/*
 * REENGANCHE BARATO: la portadora ya se sabe, solo falta el instante.
 *
 * Perder el sincronismo no quiere decir perder la portadora. Pasa cuando
 * el bucle principal se retrasa y el anillo se desborda, o cuando la
 * estacion calla un momento - y en los dos casos la frecuencia sigue
 * siendo la misma. Volver a barrer once portadoras seria tirar el dato
 * que mas cuesta conseguir.
 *
 * Asi que primero se prueba esto: una sola portadora, la conocida, a
 * paso de simbolo. Son 256 correlaciones en vez de 2816, no necesita
 * esperar al permiso de una vez por segundo, y es lo que hace que una
 * costera que emite a rafagas se vuelva a coger al vuelo.
 */
/*
 * ¿HAY TRAMA DE 106,67 ms AQUI? Autocorrelacion de la señal en banda
 * base a un retardo de una trama exacta. No necesita saber nada del
 * preambulo ni de la portadora: si hay un 4285, lo de ahora se parece a
 * lo de hace 1280 muestras. Es lo mismo que hace el identificador, pero
 * en una sola linea y sin pedir la memoria prestada.
 *
 * Se mide sobre 640 muestras porque el anillo son 2048 y hay que leer
 * hasta el retardo mas la ventana: 1280 + 640 cabe, 1280 + 1280 no.
 */
static void mide_trama(void)
{
    float ar = 0.0f, ai = 0.0f, e1 = 0.0f, e2 = 0.0f;
    uint32_t i, base;

    if (s_escrito < (MUE_TRAMA + 640U + 8U)) { s_trama = 0U; return; }
    base = s_escrito - (MUE_TRAMA + 640U);
    for (i = 0U; i < 640U; i++) {
        float xr, xi, yr, yi;
        anillo_lee(base + i, &xr, &xi);
        anillo_lee(base + i + MUE_TRAMA, &yr, &yi);
        ar += (xr * yr) + (xi * yi);
        ai += (xi * yr) - (xr * yi);
        e1 += (xr * xr) + (xi * xi);
        e2 += (yr * yr) + (yi * yi);
    }
    if ((e1 > 1e-20f) && (e2 > 1e-20f)) {
        float v = sqrtf((ar * ar) + (ai * ai)) / sqrtf(e1 * e2);
        int32_t c = (int32_t)((v * 100.0f) + 0.5f);
        if (c < 0) { c = 0; }
        if (c > 100) { c = 100; }
        s_trama = (uint8_t)c;
    }
}

static void anota(float v, float dw)
{
    int32_t c = (int32_t)((v * 100.0f) + 0.5f);
    if (c < 0) { c = 0; }
    if (c > 100) { c = 100; }
    s_corr = (uint8_t)c;
    if (s_corr > s_corr_max) { s_corr_max = s_corr; }
    s_corr_hz = (int16_t)((dw * BD) / 6.28318531f);
}

static uint8_t busca_rapida(uint32_t desde, uint32_t cuantos,
                            uint32_t *mejor, float *q)
{
    uint32_t i, m = desde;
    float v = -1.0f;

    for (i = 0U; i < cuantos; i += SPS) {
        float c = correla(desde + i, -s_dw_gruesa);
        if (c > v) { v = c; m = desde + i; }
    }
    if (v <= (UMBRAL * 0.8f)) { *mejor = m; *q = v; anota(v, s_dw_gruesa); return 0U; }
    {
        uint32_t base = (m > 4U) ? (m - 4U) : 0U;
        for (i = 0U; i < 9U; i++) {
            float c = correla(base + i, -s_dw_gruesa);
            if (c > v) { v = c; m = base + i; }
        }
    }
    *mejor = m; *q = v;
    anota(v, s_dw_gruesa);
    return (uint8_t)((v > UMBRAL) ? 1U : 0U);
}

/*
 * Adquisicion: hay que encontrar el instante Y la portadora a la vez.
 *
 * Primero una pasada gruesa -la portadora de 50 en 50 Hz entre -400 y
 * +400, el instante de dos en dos muestras- y luego se afina alrededor
 * del mejor. Asi son unas 580.000 cuentas en vez de los 2,8 millones de
 * barrerlo todo fino, y solo se paga al enganchar.
 */
/*
 * +-250 Hz de 50 en 50. La norma pide +-400 en el centro del
 * decodificador, pero esos 400 son para mover el centro A MANO cuando el
 * receptor esta mal sintonizado; lo que hay que capturar solo es el
 * desvio de la estacion y el error de sintonia del dueño, y 250 sobra -el
 * que trajo el 02/10/2026 eran 58 Hz-. Cada paso que se quita es un 9 %
 * menos de cuentas en la unica parte cara de todo esto.
 */
/*
 * HASTA +-1000 Hz, ENSANCHANDO. La norma pide +-400 en el centro del
 * decodificador, pero eso da por hecho que uno sintoniza fino; con el
 * paso en 1 kHz y frecuencias que no son redondas -4288,6, 4334,
 * 8478,5- el desvio puede ser mucho mayor. Se empieza en +-100 y se
 * ensancha a cada fallo, asi que lo ancho solo se paga cuando no hay
 * nada cerca.
 */
#define BUSCA_PASOS   41U     /* -1000..+1000 de 50 en 50 */
#define BUSCA_PASO_HZ 50.0f

/*
 * EMPIEZA ESTRECHO Y ENSANCHA SI NO ENCUENTRA.
 *
 * El desvio normal es pequeño -el de la grabacion del dueño eran 58 Hz-
 * asi que casi siempre basta con +-100. Barrer +-250 desde el primer
 * intento es pagar once pasos para encontrarlo en el tercero. Se
 * arranca en +-100 (cinco pasos) y cada busqueda fallida ensancha uno
 * hasta +-250; al enganchar vuelve a estrecharse.
 */
/*
 * EL BARRIDO VA A TROZOS, Y ESTO NO ES UNA OPTIMIZACION: ES LO QUE
 * IMPIDE QUE SE COMA SU PROPIO AUDIO.
 *
 * *** El dueño, 02/10/2026, con el diagnostico ya en pantalla:
 * "preambulo 63 de 45 -> deberia enganchar" y al lado "pierde x17". ***
 *
 * O sea: engancha, pero pierde audio, y sin diez segundos seguidos no
 * hay forma de llenar el entrelazado largo. Lo que se los comia era el
 * propio barrido: con el ancho en +-1000 son 41 portadoras por 128
 * posiciones por 80 simbolos, y eso bloquea el bucle principal mas de
 * medio segundo. La cola aguanta 510 ms. Se comia su entrada otra vez,
 * solo que ahora por el otro lado.
 *
 * Asi que el barrido se reparte: PASOS_VUELTA portadoras cada vez que
 * se llama, guardando lo mejor visto, y cuando se han mirado todas se
 * decide. Cada bloqueo baja a la decima parte y el conjunto tarda lo
 * mismo. Las portadoras se miran sobre datos distintos y da igual: el
 * preambulo esta ahi todo el rato.
 */
#define PASOS_VUELTA  4U

static uint8_t busca_todo(uint32_t desde, uint32_t cuantos,
                          uint32_t *mejor, float *mejor_dw, float *q)
{
    uint32_t i, k, m = desde;
    float v = -1.0f, bdw = 0.0f;
    uint32_t pasos = (uint32_t)((2U * s_ancho) + 1U);
    uint32_t hasta;

    if (s_sw_k >= pasos) { s_sw_k = 0U; s_sw_v = -1.0f; }
    hasta = s_sw_k + PASOS_VUELTA;
    if (hasta > pasos) { hasta = pasos; }

    for (k = s_sw_k; k < hasta; k++) {
        float hz = (((float)(int32_t)k) - (float)s_ancho) * BUSCA_PASO_HZ;
        float dw = (6.28318531f * hz) / BD;        /* rad por simbolo */
        /*
         * De cinco en cinco, no de dos en dos: a resolucion de simbolo.
         * La fase fina de muestreo la pone el afinado de abajo, y asi la
         * pasada gruesa cuesta dos veces y media menos.
         */
        for (i = 0U; i < cuantos; i += SPS) {
            float c = correla(desde + i, -dw);
            /*
             * EL FONDO, QUE ES LA MITAD DE LA MEDIDA.
             *
             * Un 27 no dice nada por si solo: el ruido correlaciona a 26
             * por su cuenta. Lo que separa "hay algo flojo" de "no hay
             * nada" es la diferencia con el fondo, asi que se promedia
             * TODO lo que se mira -que son miles de correlaciones sobre
             * sitios donde no hay preambulo- y se enseña al lado.
             */
            s_fondo_s += c; s_fondo_n++;
            if (c > v) { v = c; m = desde + i; bdw = dw; }
        }
    }
    /* lo mejor de esta vuelta se guarda y se sigue en la siguiente */
    if (v > s_sw_v) { s_sw_v = v; s_sw_m = m; s_sw_dw = bdw; }
    s_sw_k = hasta;
    if (s_sw_k < pasos) {
        /* todavia faltan portadoras por mirar: ni se decide ni se ensancha */
        *mejor = s_sw_m; *mejor_dw = s_sw_dw; *q = s_sw_v;
        anota(s_sw_v, s_sw_dw);
        return 0U;
    }
    v = s_sw_v; m = s_sw_m; bdw = s_sw_dw;
    s_sw_k = 0U; s_sw_v = -1.0f;
    /* Se cuenta el BARRIDO ENTERO, no cada trozo: es la medida del coste
     * y tiene que seguir significando lo mismo que cuando se escribio el
     * banco que la vigila. */
    s_busquedas++;

    if (v <= (UMBRAL * 0.8f)) {
        if (s_ancho < ((BUSCA_PASOS - 1U) / 2U)) { s_ancho++; }
        *mejor = m; *mejor_dw = bdw; *q = v; anota(v, bdw); return 0U;
    }

    /* afinado: el instante muestra a muestra y la portadora de 10 en 10 Hz */
    {
        uint32_t base = (m > 4U) ? (m - 4U) : 0U;
        float    bdw0 = bdw;
        for (k = 0U; k < 11U; k++) {
            float dw = bdw0 + ((((float)(int32_t)k) - 5.0f) * 10.0f
                               * 6.28318531f / BD);
            for (i = 0U; i < 9U; i++) {
                float c = correla(base + i, -dw);
                if (c > v) { v = c; m = base + i; bdw = dw; }
            }
        }
    }
    *mejor = m; *mejor_dw = bdw; *q = v;
    anota(v, bdw);
    if (v > UMBRAL) { s_ancho = 2U; return 1U; }
    if (s_ancho < ((BUSCA_PASOS - 1U) / 2U)) { s_ancho++; }
    return 0U;
}

/* --- una trama: del audio a 128 bits blandos ------------------------- */
static const float k_cos8[8] = { 1.0f, 0.70710678f, 0.0f, -0.70710678f,
                                -1.0f, -0.70710678f, 0.0f, 0.70710678f };
static const float k_sen8[8] = { 0.0f, 0.70710678f, 1.0f, 0.70710678f,
                                 0.0f, -0.70710678f, -1.0f, -0.70710678f };


/*
 * DEL SIMBOLO A LOS BITS BLANDOS.
 *
 * El convenio de signo es el mismo en los tres casos: BLANDO POSITIVO
 * QUIERE DECIR BIT 0. Se mantiene hasta mete_blandos(), que es donde se
 * da la vuelta para hfdl_viterbi, que los quiere al reves.
 *
 * BPSK: la parte real y ya.
 *
 * QPSK con el Gray [0,1,3,2] de la norma: el punto k va en el angulo
 * 2*pi*k/4 y lleva el valor gray[k], o sea 00, 01, 11, 10. De ahi salen
 * dos reglas cortas: el bit de arriba es 1 cuando Re+Im < 0 y el de
 * abajo cuando Im-Re > 0.
 *
 * 8PSK: con ocho puntos las reglas cortas se acaban, asi que se hace lo
 * general - para cada bit, la distancia al punto mas cercano que lo
 * tenga a 0 menos la distancia al mas cercano que lo tenga a 1. Son
 * ocho distancias por simbolo; a 1200 simbolos de datos por segundo no
 * se nota.
 *
 * *** OJO, Y QUE CONSTE: el camino de BPSK esta probado contra una
 * grabacion de aire y saca texto. Los de QPSK y 8PSK estan escritos
 * siguiendo la norma pero NO se han podido probar contra ninguna
 * grabacion, porque las dos que hay de esas velocidades van cifradas y
 * no hay con que saber si el texto que salga es el bueno. En cuanto
 * aparezca una en claro, este banco la cazara. ***
 */
static const uint8_t k_g8[8] = { 1U, 0U, 2U, 3U, 6U, 7U, 5U, 4U };

static int8_t recorta(float v)
{
    if (v >  127.0f) { return (int8_t)127; }
    if (v < -127.0f) { return (int8_t)-127; }
    return (int8_t)v;
}

static uint8_t bits_de(float qr, float qi, int8_t *sal)
{
    uint8_t n = bits_simbolo();

    if (n == 1U) {
        sal[0] = recorta(qr * 90.0f);
        return 1U;
    }
    if (n == 2U) {
        sal[0] = recorta((qr + qi) * 64.0f);   /* bit de arriba */
        sal[1] = recorta((qr - qi) * 64.0f);   /* bit de abajo  */
        return 2U;
    }
    {
        float d[8];
        uint8_t k, b;
        float m = 0.0f;
        for (k = 0U; k < 8U; k++) {
            float dr = qr - k_cos8[k], di2 = qi - k_sen8[k];
            d[k] = (dr * dr) + (di2 * di2);
            if (d[k] > m) { m = d[k]; }
        }
        for (b = 0U; b < 3U; b++) {
            float c0 = 1e30f, c1 = 1e30f;
            for (k = 0U; k < 8U; k++) {
                uint8_t v = (uint8_t)((k_g8[k] >> (2U - b)) & 1U);
                if (v == 0U) { if (d[k] < c0) { c0 = d[k]; } }
                else         { if (d[k] < c1) { c1 = d[k]; } }
            }
            sal[b] = recorta((c1 - c0) * 40.0f);
        }
        return 3U;
    }
}

static void trama_saca(uint32_t ini, int8_t *blandos, uint8_t *sondeos_pc)
{
    uint32_t i;
    float vr = 0.0f, vi = 0.0f;
    float anc_x[4], anc_a[4], anc_f[4];
    uint32_t n_anc = 0U, bien = 0U;

    for (i = 0U; i < SIM_TRAMA; i++) {
        float re, im, c, sn;
        anillo_lee(ini + (uint32_t)FASE_MUE + (i * SPS), &re, &im);
        /* lo grueso, que es lo que encontro la busqueda de portadora */
        c = cosf(-s_dw_gruesa * (float)i); sn = sinf(-s_dw_gruesa * (float)i);
        s_simR[i] = (re * c) - (im * sn);
        s_simI[i] = (re * sn) + (im * c);
    }

    /* desvio: el preambulo es una secuencia m de periodo 31 */
    for (i = 0U; i < 31U; i++) {
        float ar = s_simR[i] * (float)s_pre[i], ai = s_simI[i] * (float)s_pre[i];
        float br = s_simR[i + 31U] * (float)s_pre[i + 31U];
        float bi = s_simI[i + 31U] * (float)s_pre[i + 31U];
        vr += (br * ar) + (bi * ai);
        vi += (bi * ar) - (br * ai);
    }
    if ((vr * vr) + (vi * vi) > 1e-20f) {
        /*
         * El residuo, sobre lo que ya quito la busqueda gruesa. Aqui el
         * atan2/31 si es univoco: lo que queda despues del barrido son
         * menos de 25 Hz, muy por debajo de los 38,7 donde se dobla.
         */
        float d = atan2f(vi, vr) / 31.0f;
        s_dw = (s_tramas == 0U) ? d : ((0.75f * s_dw) + (0.25f * d));
        s_dw_gruesa += 0.10f * s_dw;   /* lo aprendido pasa al grueso */
    }
    for (i = 0U; i < SIM_TRAMA; i++) {
        float c = cosf(-s_dw * (float)i), sn = sinf(-s_dw * (float)i);
        float r = (s_simR[i] * c) - (s_simI[i] * sn);
        float m = (s_simR[i] * sn) + (s_simI[i] * c);
        s_simR[i] = r; s_simI[i] = m;
    }

    /* cuatro anclas: el preambulo y los tres bloques de sondeo */
    {
        float gr = 0.0f, gi = 0.0f;
        for (i = 0U; i < SIM_PRE; i++) {
            gr += s_simR[i] * (float)s_pre[i];
            gi += s_simI[i] * (float)s_pre[i];
        }
        anc_x[n_anc] = 40.0f;
        anc_a[n_anc] = sqrtf((gr * gr) + (gi * gi)) / (float)SIM_PRE;
        anc_f[n_anc] = atan2f(gi, gr);
        n_anc++;
    }
    {
        static const uint16_t k_ini[3] = { 32U, 80U, 128U };
        uint32_t b;
        for (b = 0U; b < 3U; b++) {
            float gr = 0.0f, gi = 0.0f;
            for (i = 0U; i < 16U; i++) {
                uint32_t d = k_ini[b] + i;
                uint8_t  p = s_scr[d];
                float    sr = s_simR[SIM_PRE + d], si = s_simI[SIM_PRE + d];
                gr += (sr * k_cos8[p]) + (si * k_sen8[p]);
                gi += (si * k_cos8[p]) - (sr * k_sen8[p]);
            }
            anc_x[n_anc] = (float)SIM_PRE + (float)k_ini[b] + 7.5f;
            anc_a[n_anc] = sqrtf((gr * gr) + (gi * gi)) / 16.0f;
            anc_f[n_anc] = atan2f(gi, gr);
            n_anc++;
        }
    }
    /* desdoblar la fase para poder interpolarla */
    for (i = 1U; i < n_anc; i++) {
        float d = anc_f[i] - anc_f[i - 1U];
        while (d >  3.14159265f) { anc_f[i] -= 6.28318531f; d = anc_f[i] - anc_f[i - 1U]; }
        while (d < -3.14159265f) { anc_f[i] += 6.28318531f; d = anc_f[i] - anc_f[i - 1U]; }
    }

    /* ecualizar y sacar los bits */
    {
        uint32_t nb = 0U;
        for (i = 0U; i < 176U; i++) {
            float x = (float)SIM_PRE + (float)i;
            float a, f, cr, ci2, sr, si, dr, di;
            uint32_t k = 0U;
            while ((k + 2U) < n_anc && anc_x[k + 1U] < x) { k++; }
            {
                float t = (anc_x[k + 1U] - anc_x[k]);
                float u = (t > 1e-6f) ? ((x - anc_x[k]) / t) : 0.0f;
                a = anc_a[k] + (u * (anc_a[k + 1U] - anc_a[k]));
                f = anc_f[k] + (u * (anc_f[k + 1U] - anc_f[k]));
            }
            if (a < 1e-9f) { a = 1e-9f; }
            cr = cosf(f) * a; ci2 = sinf(f) * a;
            sr = s_simR[SIM_PRE + i]; si = s_simI[SIM_PRE + i];
            {
                float e = (cr * cr) + (ci2 * ci2);
                dr = ((sr * cr) + (si * ci2)) / e;
                di = ((si * cr) - (sr * ci2)) / e;
            }
            /* quitar el scrambler */
            {
                uint8_t p = s_scr[i];
                float qr = (dr * k_cos8[p]) + (di * k_sen8[p]);
                float qi = (di * k_cos8[p]) - (dr * k_sen8[p]);
                if (s_conocido[i]) {
                    if (qr > 0.0f && fabsf(qi) < qr) { bien++; }
                } else {
                    nb += bits_de(qr, qi, &blandos[nb]);
                }
            }
        }
    }
    *sondeos_pc = (uint8_t)((bien * 100U) / 48U);
}

/* --- desentrelazador (anexo E) --------------------------------------- */
/*
 * 32 filas; la fila r retrasa j*(31-r). La entrada es secuencial -el bit
 * r del grupo va a la fila r- y la SALIDA va por (9*i) mod 32, que es la
 * secuencia que la norma define como "los resultados modulo 32 de
 * multiplicar por 9 cada numero de la secuencia normal".
 *
 * Cada linea es un anillo de longitud L = j*(31-r)+1: se escribe en la
 * cabeza, la cabeza retrocede uno y se lee ahi. Para L=1 eso es retardo
 * cero, que es justo lo que le toca a la fila 31.
 */
static void dein_pasa(const int8_t *ent, int8_t *sal)
{
    uint32_t r, i;

    for (r = 0U; r < FILAS; r++) {
        uint16_t L = s_dein_len[r];
        uint16_t h = s_dein_pos[r];
        s_dein[s_dein_ini[r] + h] = ent[r];
        h = (uint16_t)((h + L - 1U) % L);
        s_dein_pos[r] = h;
    }
    for (i = 0U; i < FILAS; i++) {
        uint32_t r2 = (9U * i) % FILAS;
        sal[i] = s_dein[s_dein_ini[r2] + s_dein_pos[r2]];
    }
    s_grupos++;
}

/* --- del flujo desentrelazado al Viterbi ------------------------------ */
/*
 * Las repeticiones van POR PAREJAS: T1 T2 T1 T2. O sea que para la tasa
 * 1/4 hay que sumar la posicion 0 con la 2 y la 1 con la 3, no la 0 con
 * la 1. Probandolo al reves la comprobacion de paridad del codigo daba
 * 0,5 en las 3584 combinaciones que llegue a barrer.
 */
static void mete_blandos(const int8_t *g)
{
    uint8_t rep = repeticiones();
    uint8_t cada = (uint8_t)(2U * rep);
    uint32_t i;

    for (i = 0U; i < FILAS; i++) {
        s_resto[s_nresto++] = g[i];
        if (s_nresto >= cada) {
            int32_t a = 0, b2 = 0;
            uint8_t k;
            for (k = 0U; k < rep; k++) {
                a += s_resto[2U * k];
                b2 += s_resto[(2U * k) + 1U];
            }
            a /= (int32_t)rep; b2 /= (int32_t)rep;
            /* hfdl_viterbi quiere 0..255 con 255 = "seguro que es 1", y
             * aqui un blando POSITIVO es el bit 0 (fase 0 del BPSK). */
            if (s_nblandos < (2U * VIT_VENT)) {
                int32_t u = 128 - a; if (u < 0) { u = 0; } if (u > 255) { u = 255; }
                s_blandos[s_nblandos++] = (uint8_t)u;
                u = 128 - b2; if (u < 0) { u = 0; } if (u > 255) { u = 255; }
                s_blandos[s_nblandos++] = (uint8_t)u;
            }
            s_nresto = 0U;
        }
    }
}

/* --- caracteres ------------------------------------------------------- */
static void pon_char(char c)
{
    if (s_nlin == 0U) { s_nlin = 1U; s_col = 0U; s_lin[0][0] = '\0'; }
    if (c < 32 || c > 126) { c = '.'; }
    if (s_col >= (STANAG_RX_LARGO - 1U)) {
        if (s_nlin >= STANAG_RX_LINEAS) {
            uint8_t i, k;
            for (i = 1U; i < STANAG_RX_LINEAS; i++) {
                for (k = 0U; k < STANAG_RX_LARGO; k++) { s_lin[i - 1U][k] = s_lin[i][k]; }
            }
            s_nlin = (uint8_t)(STANAG_RX_LINEAS - 1U);
        }
        s_lin[s_nlin][0] = '\0';
        s_nlin++; s_col = 0U;
    }
    s_lin[s_nlin - 1U][s_col++] = c;
    s_lin[s_nlin - 1U][s_col] = '\0';
}

/*
 * LOS TRES FORMATOS DE SALIDA.
 *
 * ASCII sincrono: cada ocho bits un caracter, LSB PRIMERO. Es lo que usa
 * el mensaje de prueba del 4481 con el que se valido toda la cadena.
 *
 * ITA2 con trama 5N1: un bit de arranque a 0, cinco de datos LSB
 * primero, uno de parada a 1. Es lo que mandan EN CLARO las costeras
 * francesas en sus frecuencias barco-costera (FUE, FUO, 6WW, FUF, FUX),
 * que cuando no hay barco emiten un mensaje de prueba en bucle. Lleva
 * dos juegos, letras y cifras, y se cambia con dos codigos.
 *
 * HEX: los bytes tal cual, por si lo que va dentro no es texto - que es
 * lo normal, porque la mayoria del trafico va cifrado.
 *
 * LA TRAMA DEL ITA2 HAY QUE BUSCARLA. Los siete bits no vienen marcados;
 * lo unico que los delata es que el primero sea 0 y el ultimo 1. Asi que
 * se prueba una fase, se cuentan los fallos, y si pasan de cuatro en
 * dieciseis caracteres se corre un bit y se vuelve a empezar. Con la
 * trama buena los fallos son los errores de canal y se queda.
 */
static const char k_ita2_let[32] = {
    ' ','E','\n','A',' ','S','I','U','\r','D','R','J','N','F','C','K',
    'T','Z','L','W','H','Y','P','Q','O','B','G',' ','M','X','V',' '
};
static const char k_ita2_cif[32] = {
    ' ','3','\n','-',' ','\'','8','7','\r',' ','4',' ',',','!',':','(',
    '5','+',')','2','#','6','0','1','9','?','&',' ','.','/','=',' '
};
#define ITA2_FIGS  27U
#define ITA2_LTRS  31U

static void pon_hex(uint8_t v)
{
    static const char k[16] = { '0','1','2','3','4','5','6','7',
                                '8','9','A','B','C','D','E','F' };
    pon_char(k[(v >> 4) & 0x0FU]);
    pon_char(k[v & 0x0FU]);
    pon_char(' ');
}

static void pon_bit(uint8_t b)
{
    if (s_fmt == STANAG_RX_ITA2) {
        s_ita2reg = (uint16_t)((s_ita2reg >> 1) | (b ? 0x40U : 0U));  /* 7 bits */
        s_ita2n++;
        if (s_ita2n < 7U) { return; }
        s_ita2n = 0U;
        {
            uint8_t arranque = (uint8_t)(s_ita2reg & 1U);
            uint8_t parada   = (uint8_t)((s_ita2reg >> 6) & 1U);
            uint8_t c        = (uint8_t)((s_ita2reg >> 1) & 0x1FU);
            s_ita2cnt++;
            if (arranque != 0U || parada == 0U) {
                s_ita2mal++;
                if (s_ita2mal > 4U) {
                    /* fase mala: se corre un bit y se vuelve a contar */
                    s_ita2n = 1U; s_ita2mal = 0U; s_ita2cnt = 0U;
                    s_ita2fase = (uint8_t)((s_ita2fase + 1U) % 7U);
                }
                return;
            }
            if (s_ita2cnt >= 16U) { s_ita2cnt = 0U; s_ita2mal = 0U; }
            if (c == ITA2_FIGS) { s_ita2cif = 1U; return; }
            if (c == ITA2_LTRS) { s_ita2cif = 0U; return; }
            pon_char(s_ita2cif ? k_ita2_cif[c] : k_ita2_let[c]);
        }
        return;
    }

    s_bitacum = (uint8_t)((s_bitacum >> 1) | (b ? 0x80U : 0U));
    s_nbitacum++;
    if (s_nbitacum >= 8U) {
        if (s_fmt == STANAG_RX_HEX) { pon_hex(s_bitacum); }
        else                        { pon_char((char)s_bitacum); }
        s_nbitacum = 0U; s_bitacum = 0U;
    }
}

static uint8_t decodifica(void)
{
    hfdl_viterbi_t v;
    uint32_t i;
    uint32_t errores = 0U;

    if (s_nblandos < (2U * VIT_VENT)) { return 0U; }

    if (!hfdl_viterbi_init(&v,
            (hfdl_viterbi_metric_t *)s_metricas,
            (hfdl_viterbi_metric_t *)(s_metricas + sizeof(hfdl_viterbi_metric_t)),
            (hfdl_viterbi_decision_t *)s_vitbuf,
            VIT_VENT + 6U, 0U)) {
        return 0U;
    }
    if (!hfdl_viterbi_update_block(&v, s_blandos, VIT_VENT)) { return 0U; }
    {
        uint32_t mejor = 0U, m = 0xFFFFFFFFUL;
        for (i = 0U; i < HFDL_VITERBI_NUM_STATES; i++) {
            uint32_t x = hfdl_viterbi_get_final_metric(&v, i);
            if (x < m) { m = x; mejor = i; }
        }
        hfdl_viterbi_chainback(&v, s_salida, VIT_VENT, mejor);
    }

    /*
     * RECODIFICAR Y COMPARAR, QUE ES LO UNICO QUE DISTINGUE "HA
     * ENGANCHADO" DE "EL VITERBI SIEMPRE DEVUELVE ALGO". Con sincronismo
     * bueno sale poco por ciento; con ruido, el 13 %.
     *
     * PERO HAY UNA TRAMPA, Y YA ME COMIO UNA VEZ EN EL PROTOTIPO: la
     * palabra TODO CEROS es valida en cualquier codigo convolucional. Si
     * la emision esta en reposo -o si la combinacion de velocidad y
     * entrelazado acaba promediando todo a cero- el recodificado cuadra
     * perfecto y el numero dice 0 % sin que haya enganchado nada.
     *
     * Asi que se mira ademas que la entrada este EQUILIBRADA. Si de los
     * 640 bits duros no hay entre un cuarto y tres cuartos de unos, el
     * numero no mide nada y se dice -la pantalla pone "e--" en vez de un
     * cero mentiroso-.
     */
    {
        uint32_t reg = 0U;
        uint32_t unos = 0U;
        for (i = 0U; i < (2U * VIT_VENT); i++) {
            if (s_blandos[i] > 128U) { unos++; }
        }
        /*
         * Y el numero se GUARDA de la ultima ventana que si estaba
         * equilibrada, no de la ultima a secas. Una emision de verdad
         * alterna reposo y datos, asi que si solo se mirara la ultima el
         * indicador parpadearia entre un numero bueno y un "--" cada vez
         * que la estacion calla un momento. Se da por caducado a las ocho
         * ventanas seguidas sin poder medir, que es un segundo largo.
         */
        s_ber_equi = (uint8_t)(((unos > ((2U * VIT_VENT) / 4U))
                             && (unos < (((2U * VIT_VENT) * 3U) / 4U))) ? 1U : 0U);
        for (i = 0U; i < VIT_VENT; i++) {
            uint32_t bit = (s_salida[i >> 3] >> (7U - (i & 7U))) & 1U;
            uint32_t r7 = ((reg << 1) | bit) & 0x7FU;
            uint32_t pa = r7 & HFDL_VITERBI_POLYA, pb = r7 & HFDL_VITERBI_POLYB;
            uint32_t ea, eb;
            pa ^= pa >> 4; pa ^= pa >> 2; pa ^= pa >> 1; ea = pa & 1U;
            pb ^= pb >> 4; pb ^= pb >> 2; pb ^= pb >> 1; eb = pb & 1U;
            if ((s_blandos[2U * i] > 128U ? 1U : 0U) != ea) { errores++; }
            if ((s_blandos[(2U * i) + 1U] > 128U ? 1U : 0U) != eb) { errores++; }
            reg = r7 & 0x3FU;
        }
        if (s_ber_equi) {
            s_ber = (uint8_t)((errores * 100U) / (2U * VIT_VENT));
            s_ber_vale = 1U;
            s_ber_mudo = 0U;
        } else if (s_ber_mudo < 255U) {
            s_ber_mudo++;
            if (s_ber_mudo > 8U) { s_ber_vale = 0U; }
        }
    }

    /*
     * NI UN CARACTER HASTA QUE EL ENTRELAZADOR ESTE LLENO DE VERDAD.
     *
     * *** El dueño, 02/10/2026: "curioso que en frecuencias distintas
     * saque la primera linea muy parecida". ***
     *
     * Y tenia toda la razon, y es el peor de los fallos: que dos
     * frecuencias distintas saquen lo mismo quiere decir que eso NO
     * viene de la señal.
     *
     * Venia de aqui. Mientras el entrelazador se llena, lo que sale de
     * sus lineas largas son los ceros con los que arranca. Un cero
     * blando no es "el bit es 0": es "no tengo ni idea", y el Viterbi
     * con una entrada sin informacion elige igualmente un camino -el
     * suyo, el mismo siempre-. De ahi salian esos "Wg" y esas arrobas
     * identicos en 4338 y en 4277: era el decodificador hablando solo.
     *
     * Asi que no se suelta nada hasta que los grupos recorridos pasan
     * del relleno. Es la misma cuenta que enseña la barra del panel.
     */
    {
        uint32_t hacen = ((uint32_t)s_j * 31UL) + (20UL * (uint32_t)repeticiones());
        if (s_grupos < hacen) {
            uint32_t quedan = (uint32_t)s_nblandos - (2U * VIT_SALEN);
            for (i = 0U; i < quedan; i++) {
                s_blandos[i] = s_blandos[i + (2U * VIT_SALEN)];
            }
            s_nblandos = (uint16_t)quedan;
            return 0U;
        }
    }
    for (i = VIT_MARGEN; i < (VIT_MARGEN + VIT_SALEN); i++) {
        pon_bit((uint8_t)((s_salida[i >> 3] >> (7U - (i & 7U))) & 1U));
    }

    /* la ventana avanza VIT_SALEN bits de informacion = 2*VIT_SALEN blandos */
    {
        uint32_t quedan = (uint32_t)s_nblandos - (2U * VIT_SALEN);
        for (i = 0U; i < quedan; i++) { s_blandos[i] = s_blandos[i + (2U * VIT_SALEN)]; }
        s_nblandos = (uint16_t)quedan;
    }
    return 1U;
}

uint8_t stanag_rx_paso(void)
{
    int8_t bl[SIM_DATOS * 3U];     /* 8PSK son tres bits por simbolo */
    int8_t g[FILAS], o[FILAS];
    uint8_t hay = 0U;

    if (!s_arrancado) { return 0U; }

    muele();   /* lo que la interrupcion dejo crudo, a banda base */

    /*
     * LO PRIMERO: NO LEER LO QUE YA SE HA PISADO.
     *
     * El anillo guarda ANILLO muestras y nada mas. Si el bucle principal
     * tarda y el puntero de lectura se queda mas atras que eso, lo que
     * hay bajo el son muestras NUEVAS escritas encima, no las viejas, y
     * la correlacion sale de basura con cara de buena. Costo una tarde:
     * la adquisicion pedia 2560 muestras de retraso sobre un anillo de
     * 2048 y el preambulo correlaba a 0,58 en vez de a 0,89.
     */
    if ((s_escrito - s_leido) > (ANILLO - 64U)) {
        s_leido = s_escrito - (ANILLO - 64U);
        s_engancha = 0U;
    }

    /* ¿hay una trama entera por delante del puntero de lectura? */
    while ((s_escrito > s_leido) && ((s_escrito - s_leido) > (MUE_TRAMA + (2U * SPS)))) {
        uint32_t ini;
        float q;

        if (!s_engancha) {
            /* adquisicion: se barre una trama entera */
            /* una trama entera de desplazamientos mas el propio
             * preambulo: 1280 + 400, y cabe de sobra en el anillo */
            if ((s_escrito - s_leido) < (MUE_TRAMA + (SIM_PRE * SPS) + 64U)) { break; }
            /*
             * Si ya se tuvo portadora, el reenganche barato primero.
             */
            if (s_hay_portadora) {
                if (busca_rapida(s_leido, MUE_TRAMA, &ini, &q)) {
                    s_engancha = 1U;
                    s_fallos_rapidos = 0U;
                    goto enganchado;
                }
                if (s_fallos_rapidos < 20U) {
                    s_fallos_rapidos++;
                    s_leido += MUE_TRAMA;
                    continue;
                }
                /* veinte seguidos: la portadora ya no vale, a barrer */
                s_hay_portadora = 0U;
                s_fallos_rapidos = 0U;
            }
            /*
             * UNA BUSQUEDA POR SEGUNDO, NO UNA POR TRAMA.
             *
             * *** El dueño, 02/10/2026: "va con bastante lag toda la
             * radio ahora mismo". ***
             *
             * Y era esto. Barrer 11 portadoras por 256 posiciones por 80
             * simbolos son millones de cuentas, y se hacia NUEVE VECES
             * POR SEGUNDO mientras no enganchara. Medido en el PC: 26
             * segundos de CPU para 20 de ruido. En el M4 eso no se
             * sostiene, y se nota en todo lo demas porque va en el bucle
             * principal.
             *
             * Y no hacia ninguna falta: el preambulo se repite CADA
             * TRAMA, asi que barrer una trama entera de posiciones lo
             * encuentra siempre que este ahi. Repetirlo nueve veces por
             * segundo no mejora la probabilidad de engancharlo, solo
             * gasta. Una vez por segundo tiene exactamente la misma
             * puntería y cuesta nueve veces menos.
             */
            /* Ya no hace falta racionarlo por tiempo: cada vuelta mira
             * cuatro portadoras y cuesta lo que costaba una decima parte
             * del barrido entero. Se raciona a tres por trama para no
             * machacar el bucle cuando no hay nada que encontrar. */
            if ((s_escrito - s_ult_busca) < (MUE_TRAMA * 3U)) {
                s_leido += MUE_TRAMA;
                continue;
            }
            /*
             * POR MITADES. Barrer las 256 posiciones de una trama de
             * golpe bloquea el bucle principal mas de lo que aguanta la
             * cola de audio. Se hace media trama cada vez, alternando:
             * el preambulo se repite cada trama, asi que en dos vueltas
             * se ha mirado todo igual, y cada bloqueo dura la mitad.
             */
            s_ult_busca = s_escrito;
                mide_trama();
                s_mitad = (uint8_t)(s_mitad ? 0U : 1U);
            {
                float dwg = 0.0f;
                uint32_t desde = s_leido + (s_mitad ? (MUE_TRAMA / 2U) : 0U);
                if (!busca_todo(desde, MUE_TRAMA / 2U, &ini, &dwg, &q)) {
                    s_leido += MUE_TRAMA;
                    continue;
                }
                s_dw_gruesa = dwg;
                s_dw = 0.0f;      /* el fino arranca de cero sobre el grueso */
                s_hay_portadora = 1U;
                s_fallos_rapidos = 0U;
            }
            s_engancha = 1U;
            goto enganchado;
        } else {
            /* seguimiento: solo tres muestras a cada lado */
            uint32_t d = (s_prox > 3U) ? (s_prox - 3U) : 0U;
            if (!busca(d, 7U, &ini, &q)) {
                s_engancha = 0U;
                s_leido += MUE_TRAMA;
                continue;
            }
        }
enganchado:

        trama_saca(ini, bl, &s_sondeos);
        s_tramas++;
        if (s_sondeos > s_sondeos_max) { s_sondeos_max = s_sondeos; }
        if (s_sondeos >= SONDEOS_MIN) { s_tramas_bien++; }
        s_prox = ini + MUE_TRAMA;
        s_leido = ini + MUE_TRAMA;

        /*
         * Los bits de la trama al desentrelazador, de 32 en 32.
         *
         * Sin perforar son 128, 256 o 384 segun la modulacion, y todos
         * son multiplos de 32. Con perforado (solo 2400 bps) por el aire
         * vienen 24 de cada 32: se colocan en las filas que no se
         * saltaron -las que no son 3 modulo 4- y en las otras ocho va un
         * cero, que para el Viterbi blando es "no se", que es la verdad.
         */
        {
            uint32_t nb = (uint32_t)SIM_DATOS * bits_simbolo();
            uint32_t p = 0U;
            while ((p + (perfora() ? 24U : FILAS)) <= nb) {
                uint32_t k;
                if (perfora()) {
                    uint32_t q = 0U;
                    for (k = 0U; k < FILAS; k++) {
                        g[k] = ((k % 4U) == 3U) ? (int8_t)0 : bl[p + (q++)];
                    }
                    p += 24U;
                } else {
                    for (k = 0U; k < FILAS; k++) { g[k] = bl[p + k]; }
                    p += FILAS;
                }
                dein_pasa(g, o);
                mete_blandos(o);
            }
        }
        /*
         * SOLO CUENTA PARA EL RELLENO LA TRAMA QUE DEMODULA BIEN.
         *
         * Los 48 simbolos de sondeo de cada trama son de valor conocido:
         * si no caen donde deben, lo que haya salido de esa trama no vale
         * y meterlo en el entrelazador contamina los diez segundos
         * enteros. Una trama mala es un hueco, y un hueco invalida el
         * relleno igual que perder audio.
         */
        if (s_sondeos < SONDEOS_MIN) {
            uint32_t z;
            s_grupos = 0U;
            s_nblandos = 0U;
            s_nresto = 0U;
            for (z = 0U; z < DEIN_LARGO(s_j); z++) { s_dein[z] = 0; }
            continue;
        }
        if (decodifica()) { hay = 1U; }
    }
    return hay;
}

void stanag_rx_estado(stanag_rx_est_t *out)
{
    if (out == 0) { return; }
    out->engancha = s_engancha;
    out->sondeos  = s_sondeos;
    out->desvio_dhz = (int16_t)(((s_dw + s_dw_gruesa) * BD * 10.0f) / 6.28318531f);
    out->tramas   = s_tramas;
    out->grupos   = (uint16_t)((s_grupos > 65535UL) ? 65535UL : s_grupos);
    out->ber      = s_ber;
    out->ber_vale = s_ber_vale;
    out->perdidas = s_perdidas;
    out->busquedas = s_busquedas;
    out->corr = s_corr;
    {
        float f = (s_fondo_n > 0UL) ? (s_fondo_s / (float)s_fondo_n) : 0.0f;
        int32_t c2 = (int32_t)((f * 100.0f) + 0.5f);
        if (c2 < 0) { c2 = 0; }
        if (c2 > 100) { c2 = 100; }
        out->fondo = (uint8_t)c2;
    }
    out->umbral = (uint8_t)((UMBRAL * 100.0f) + 0.5f);
    out->trama = s_trama;
    out->nivel = (uint16_t)((s_nivel * 1000.0f > 65000.0f) ? 65000U
                            : (uint32_t)(s_nivel * 1000.0f));
    out->corr_max = s_corr_max;
    out->sondeos_max = s_sondeos_max;
    out->tramas_bien = s_tramas_bien;
    out->ancho_hz = (uint16_t)((float)s_ancho * BUSCA_PASO_HZ);
    out->corr_hz = s_corr_hz;
    out->cola_ms = (uint16_t)((s_crudo_n * 1000UL) / 12000UL);
    {
        /* j*31 grupos para llenar el entrelazador, mas 20*repeticiones
         * para juntar la primera ventana del Viterbi. */
        uint32_t hacen = ((uint32_t)s_j * 31UL) + (20UL * (uint32_t)repeticiones());
        out->grupos_hacen  = (uint16_t)((s_grupos >= hacen) ? 0UL : hacen);
        out->grupos_hechos = (uint16_t)((s_grupos > 65535UL) ? 65535UL : s_grupos);
    }
}

const char *stanag_rx_linea(uint8_t i)
{
    if (i >= STANAG_RX_LINEAS) { return ""; }
    return s_lin[i];
}
uint8_t stanag_rx_nlineas(void) { return s_nlin; }

/*
 * BORRAR: limpia lo que se ve, no lo que se ha enganchado.
 *
 * El boton Borrar en modo demodulador no hacia nada, porque
 * stanag_modo_borra() solo vaciaba los renglones del identificador y
 * el panel, cuando demodula, pinta los de aqui. Esta es la mitad que
 * faltaba.
 *
 * Lo que SI se borra: el texto, los bits a medio juntar, la trama del
 * ITA2 (que se vuelve a buscar sola) y los contadores que solo sirven
 * para mirar. Lo que NO: el enganche, el desvio de portadora ni el
 * relleno del desentrelazador. Borrar la pantalla no puede costar
 * otros catorce segundos de espera.
 */
void stanag_rx_borra(void)
{
    uint8_t i;
    for (i = 0U; i < STANAG_RX_LINEAS; i++) { s_lin[i][0] = '\0'; }
    s_nlin = 0U; s_col = 0U;
    s_bitacum = 0U; s_nbitacum = 0U;
    s_ita2reg = 0U; s_ita2n = 0U; s_ita2fase = 0U;
    s_ita2cif = 0U; s_ita2mal = 0U; s_ita2cnt = 0U;
    s_corr_max = 0U; s_sondeos_max = 0U; s_tramas_bien = 0U;
    s_perdidas = 0U; s_busquedas = 0U;
}
