#include "ale_rx.h"
#include "ale_golay.h"
#include <string.h>
#include <math.h>

/* ==========================================================================
 * NUMEROS DEL ESTANDAR
 * ========================================================================== */
#define ALE_TONOS      8U
#define ALE_SPS        96U      /* muestras por simbolo a 12 kHz */
#define ALE_FASES      4U       /* fases de simbolo que se prueban */
#define ALE_PASO       (ALE_SPS / ALE_FASES)   /* 24 */
#define ALE_BITS_PAL   49U      /* bits de una palabra ya con FEC */
#define ALE_SIM_PAL    49U      /* simbolos que ocupa (147 bits / 3) */
#define ALE_ANILLO     (ALE_BITS_PAL * 3U)     /* 147 */

/*
 * TONO -> SIMBOLO, en codigo Gray. Ver la cabecera de ale_rx.h: el orden
 * NO es lineal, y ponerlo lineal da un decodificador que no acierta una.
 */
static const uint8_t k_gray[ALE_TONOS] = { 0U, 1U, 3U, 2U, 6U, 7U, 5U, 4U };

/* Los ocho tonos, en hercios. */
static const float k_hz[ALE_TONOS] = {
    750.0f, 1000.0f, 1250.0f, 1500.0f, 1750.0f, 2000.0f, 2250.0f, 2500.0f
};

/*
 * UMBRALES DE ENGANCHE, los mismos que usa el decodificador de referencia
 * (LinuxALE, modem.h) porque estan probados en el aire:
 *
 *   MAL_VOTOS  de los 48 bits votados, cuantos pueden salir NO unanimes.
 *              25 de 48 es mas de la mitad, y aun asi vale: con 2 de 3, un
 *              bit no unanime sigue decidiendose bien mientras solo una de
 *              las tres copias este mal.
 *   ERR_SINC   al BUSCAR el principio de palabra se exige que las dos
 *              mitades Golay salgan casi limpias (<= 1 error corregido).
 *              Es mas duro que los 3 que el codigo aguanta, y a proposito:
 *              enganchar en una fase equivocada que "casi" cuadra cuesta
 *              una palabra entera de recuperarse.
 *   ERR_SIGUE  ya enganchado se admite todo lo que el Golay pueda arreglar.
 */
#define MAL_VOTOS  25U
#define ERR_SINC    1U
#define ERR_SIGUE   3U

/*
 * EL GOLAY NO BASTA PARA ENCONTRAR EL PRINCIPIO DE PALABRA, Y ESTO ES LO
 * MENOS OBVIO DE TODO EL MODO. 27/09/2026.
 *
 * La idea natural es: pruebo las 49 fases y me quedo con la que decodifica
 * limpio por Golay. No funciona, y no funciona por una propiedad del
 * codigo: el Golay (24,12) de ALE es un ciclico [23,12] extendido, y en un
 * codigo CICLICO la rotacion de una palabra valida es OTRA palabra valida.
 *
 * Y el entrelazado convierte una rotacion de DOS bits del flujo en una
 * rotacion de UN bit de cada una de las dos mitades. O sea que la fase
 * correcta y la fase correcta mas dos bits decodifican las DOS limpiamente,
 * sin un solo error corregido, y no hay forma de distinguirlas mirando el
 * FEC.
 *
 * Lo enseno el banco a la primera: con la palabra descolocada 24 muestras
 * salian cuatro palabras perfectas con 48 votos unanimes... y valian
 * 0x58B05A en vez de 0xB160B4, que es exactamente lo mismo desplazado un
 * bit. Decodificador impecable, resultado inservible, y ni un aviso.
 *
 * LO QUE SI LO DISTINGUE es el contenido, y el estandar lo dice: los
 * criterios de aceptacion de palabra (FED-STD-1045A 5.2.4.3.3) no son dos
 * sino siete, y entre ellos estan "preambulo admisible" y "bits de
 * caracter admisibles". Una palabra desplazada un bit da basura en los tres
 * caracteres -en el caso de arriba, 'b', '`' y 'Z'- y las minusculas y los
 * signos raros NO existen en el alfabeto de ALE.
 *
 * Asi que para ENGANCHAR en frio se exige ademas:
 *   - preambulo de los que abren una direccion (TO, TWAS, FROM, TIS). Los
 *     de extension (DATA, REP) engancharian la rejilla desplazada una
 *     palabra entera, y CMD no tiene alfabeto con el que filtrar.
 *   - los tres caracteres, del alfabeto basico de 38 (A-Z, 0-9, '@', '?').
 *
 * Ya enganchado se afloja: cualquier preambulo y cualquier caracter que
 * exista en ALE, porque entonces la rejilla la sujeta el reloj y no el
 * contenido.
 */

/*
 * El alfabeto: 0 = no existe en ALE, 1 = solo en el juego ampliado de 64,
 * 2 = tambien en el basico de 38 (A-Z, 0-9, '@', '?').
 */
static const uint8_t k_alfa[128] = {
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,   /* 0x00 */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,   /* 0x10 */
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,   /* 0x20  espacio y signos */
    2,2,2,2,2,2,2,2, 2,2,1,1,1,1,1,2,   /* 0x30  0-9 ... '?' */
    2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,   /* 0x40  '@' y A-O */
    2,2,2,2,2,2,2,2, 2,2,2,1,1,1,1,1,   /* 0x50  P-Z y [\]^_ */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,   /* 0x60  minusculas: NO */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0    /* 0x70 */
};

/*
 * Preambulos que pueden ABRIR una direccion. El resto (DATA=0, THRU=1,
 * CMD=6, REP=7) solo valen en medio de una secuencia.
 */
static uint8_t abre_direccion(uint8_t pre)
{
    return (uint8_t)((pre == 2U) || (pre == 3U) ||
                     (pre == 4U) || (pre == 5U));
}

static uint8_t texto_vale(uint32_t pal, uint8_t minimo)
{
    uint8_t i;

    for (i = 0U; i < 3U; i++) {
        uint8_t c = (uint8_t)((pal >> (14U - (7U * i))) & 0x7FU);

        if (k_alfa[c] < minimo) { return 0U; }
    }
    return 1U;
}

/* ==========================================================================
 * ESTADO
 * ========================================================================== */
static uint8_t  s_on;

/* Goertzel: coeficientes y la ventana deslizante. */
static float    s_coef[ALE_TONOS];
static float    s_hist[ALE_SPS];
static uint16_t s_hi;           /* donde escribe la ventana */
static uint16_t s_lleno;

/* Fase de simbolo: cuatro candidatas, se elige la de mas energia. */
static float    s_fase_e[ALE_FASES];
static uint8_t  s_fase;             /* la elegida */
static uint8_t  s_sim_f[ALE_FASES]; /* el simbolo que ve cada fase */
static uint16_t s_cuenta;           /* muestras dentro del simbolo, 0..95 */

/* Anillo de bits: 147, uno por byte para que el acceso sea directo. */
static uint8_t  s_bits[ALE_ANILLO];
static uint16_t s_pos;          /* donde entra el siguiente bit */

/* Sincronismo de palabra. */
static uint8_t  s_eng;          /* 1 = enganchado */
static uint16_t s_pos_pal;      /* la posicion del anillo donde empieza */
static uint16_t s_hasta;        /* simbolos que faltan para la siguiente */

/* Salida: una palabra pendiente basta - salen a 2,5 por segundo. */
static uint32_t s_pal;
static uint8_t  s_pal_cal;
static uint8_t  s_pal_hay;

static ale_rx_info_t s_info;
static float    s_nivel;

/* ==========================================================================
 * ARRANQUE
 * ========================================================================== */
void ale_rx_start(float fs_hz)
{
    uint8_t t;
    float fs = (fs_hz > 1.0f) ? fs_hz : 12000.0f;

    memset(s_hist, 0, sizeof s_hist);
    memset(s_bits, 0, sizeof s_bits);
    memset(s_fase_e, 0, sizeof s_fase_e);
    memset(s_sim_f, 0, sizeof s_sim_f);
    memset(&s_info, 0, sizeof s_info);
    s_hi = 0U; s_lleno = 0U; s_cuenta = 0U; s_fase = 0U;
    s_pos = 0U; s_eng = 0U; s_pos_pal = 0U; s_hasta = 0U;
    s_pal_hay = 0U; s_nivel = 0.0f;

    /*
     * El coeficiente de Goertzel de cada tono: 2*cos(2*pi*f/fs). Los ocho
     * tonos caen en bins exactos de la ventana de 96 muestras, asi que no
     * hay fuga entre ellos y no hace falta ventana de suavizado.
     */
    for (t = 0U; t < ALE_TONOS; t++) {
        s_coef[t] = 2.0f * cosf(2.0f * 3.14159265358979f * k_hz[t] / fs);
    }
    s_on = 1U;
}

void ale_rx_stop(void)      { s_on = 0U; }
uint8_t ale_rx_activo(void) { return s_on; }

void ale_rx_info(ale_rx_info_t *out)
{
    if (out == (ale_rx_info_t *)0) { return; }
    *out = s_info;
    out->enganchado = s_eng;
    out->nivel = (uint8_t)((s_nivel > 99.0f) ? 99.0f : s_nivel);
}

void ale_rx_cuentas_cero(void)
{
    s_info.palabras = 0U;
    s_info.enganches = 0U;
}

uint8_t ale_rx_saca(uint32_t *palabra, uint8_t *calidad)
{
    if ((palabra == (uint32_t *)0) || !s_pal_hay) { return 0U; }
    *palabra = s_pal;
    if (calidad != (uint8_t *)0) { *calidad = s_pal_cal; }
    s_pal_hay = 0U;
    return 1U;
}

/* ==========================================================================
 * LA PRUEBA DE UNA FASE DE PALABRA
 * ==========================================================================
 *
 * Toma 147 bits del anillo empezando en `p`, vota 2 de 3, desentrelaza,
 * decodifica las dos mitades Golay y dice si cuadra.
 */
static uint8_t prueba(uint16_t p, uint8_t err_max, uint8_t frio,
                      uint32_t *pal, uint8_t *cal)
{
    uint8_t  v[48];
    uint8_t  i, unanimes = 0U, ea = 0U, eb = 0U;
    uint32_t a = 0U, b = 0U;
    uint16_t da, db;

    /*
     * VOTACION 2 DE 3, y el bit 48 (el de relleno, S49) NO se vota: vale
     * cero siempre y meterlo falsearia la cuenta de unanimidad, que es la
     * medida de calidad que decide el enganche.
     */
    for (i = 0U; i < 48U; i++) {
        uint8_t n = (uint8_t)(s_bits[(p + i) % ALE_ANILLO] +
                              s_bits[(p + i + 49U) % ALE_ANILLO] +
                              s_bits[(p + i + 98U) % ALE_ANILLO]);

        v[i] = (uint8_t)(n >= 2U);
        if ((n == 0U) || (n == 3U)) { unanimes++; }
    }
    if ((48U - unanimes) > MAL_VOTOS) { return 0U; }

    /*
     * DESENTRELAZADO: los bits pares son de la mitad A y los impares de la
     * B, y dentro de cada una van de mas a menos peso. Es un intercalado
     * alterno, nada mas - pero al reves no decodifica nada.
     */
    for (i = 0U; i < 24U; i++) {
        a |= ((uint32_t)v[2U * i]      << (23U - i));
        b |= ((uint32_t)v[2U * i + 1U] << (23U - i));
    }

    /*
     * Y LOS DOCE BITS DE PARIDAD DE LA MITAD B VIAJAN INVERTIDOS. Lo dice
     * el estandar (5.2.3.3) y sirve para que el receptor distinga una
     * mitad de la otra. Sin deshacerlo, la mitad B nunca decodifica y la A
     * si: el sintoma es "decodifica la mitad de cada palabra", que no se
     * parece en nada a su causa.
     */
    b ^= 0x00000FFFUL;

    da = ale_golay_decodifica(a, &ea);
    db = ale_golay_decodifica(b, &eb);

    if ((ea == ALE_GOLAY_NO) || (eb == ALE_GOLAY_NO)) { return 0U; }
    if ((ea > err_max) || (eb > err_max)) { return 0U; }

    {
        uint32_t w = ((uint32_t)da << 12) | (uint32_t)db;

        /*
         * Y AQUI ES DONDE SE TIRA LA FASE DESPLAZADA. Ver el comentario
         * largo de arriba: el Golay solo no distingue la fase buena de la
         * buena mas dos bits.
         */
        if (frio) {
            if (!abre_direccion((uint8_t)((w >> 21) & 7U))) { return 0U; }
            if (!texto_vale(w, 2U)) { return 0U; }
        } else {
            if (!texto_vale(w, 1U)) { return 0U; }
        }

        *pal = w;
    }
    *cal = unanimes;
    return 1U;
}

static void entrega(uint32_t pal, uint8_t cal)
{
    s_pal = pal;
    s_pal_cal = cal;
    s_pal_hay = 1U;
    s_info.palabras++;
    s_info.calidad = cal;
}

/* ==========================================================================
 * UN SIMBOLO NUEVO
 * ========================================================================== */
static void simbolo(uint8_t sim)
{
    uint8_t i;

    s_info.simbolos++;

    /* Tres bits, de mas a menos peso. */
    for (i = 0U; i < 3U; i++) {
        s_bits[s_pos] = (uint8_t)((sim >> (2U - i)) & 1U);
        s_pos++;
        if (s_pos >= ALE_ANILLO) { s_pos = 0U; }
    }

    if (s_eng) {
        /*
         * Ya enganchado: no se busca nada. Se entrega una palabra cada 49
         * simbolos exactos y, si deja de cuadrar, se vuelve a buscar.
         */
        if (s_hasta > 0U) { s_hasta--; return; }
        s_hasta = (uint16_t)(ALE_SIM_PAL - 1U);
        {
            uint32_t pal = 0U;
            uint8_t  cal = 0U;

            if (prueba(s_pos_pal, ERR_SIGUE, 0U, &pal, &cal)) {
                entrega(pal, cal);
            }
            else { s_eng = 0U; }
        }
        return;
    }

    /*
     * BUSCANDO. Se prueba UNA fase por simbolo, la que toca ahora, y como
     * el anillo avanza tres bits por simbolo, en 49 simbolos se han
     * barrido las 49 posiciones posibles. Probar las 49 en cada simbolo
     * seria 49 veces mas caro para llegar al mismo sitio medio segundo
     * antes.
     *
     * Y son 49 y no 147: el flujo empieza en frontera de simbolo, asi que
     * el principio de palabra siempre cae en un multiplo de tres bits.
     */
    {
        uint32_t pal = 0U;
        uint8_t  cal = 0U;

        if (prueba(s_pos, ERR_SINC, 1U, &pal, &cal)) {
            s_eng = 1U;
            s_pos_pal = s_pos;
            s_hasta = (uint16_t)(ALE_SIM_PAL - 1U);
            s_info.enganches++;
            entrega(pal, cal);
        }
    }
}

/* ==========================================================================
 * LA ENTRADA
 * ========================================================================== */
static void mide(void)
{
    uint8_t  t, mejor = 0U;
    float    pmax = -1.0f;
    uint8_t  fase = (uint8_t)(s_cuenta / ALE_PASO);

    for (t = 0U; t < ALE_TONOS; t++) {
        float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, p;
        uint16_t k;

        for (k = 0U; k < ALE_SPS; k++) {
            s0 = s_hist[(s_hi + k) % ALE_SPS] + s_coef[t] * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        p = (s1 * s1) + (s2 * s2) - (s_coef[t] * s1 * s2);
        if (p > pmax) { pmax = p; mejor = t; }
    }

    /*
     * LA FASE DE SIMBOLO SE ELIGE POR ENERGIA, y esto es lo unico que aqui
     * hace de recuperacion de reloj.
     *
     * La ventana de Goertzel cubre 96 muestras, que es un simbolo entero.
     * Si esta bien centrada, toda la energia cae en un tono; si esta a
     * caballo entre dos simbolos, se reparte entre dos y el pico baja. Asi
     * que midiendo en cuatro fases y quedandose con la que da el pico mas
     * alto se encuentra el centro del simbolo sin lazo ni interpolador.
     *
     * CUATRO FASES son 24 muestras de resolucion, o sea la cuarta parte de
     * un simbolo. ALE no exige mas: no hay deriva de reloj apreciable en
     * los 392 ms que dura una palabra.
     *
     * Y la fase SOLO se cambia mientras no hay enganche. Cambiarla a mitad
     * de palabra desplazaria los bits y tiraria la palabra entera.
     */
    s_fase_e[fase] += 0.02f * (pmax - s_fase_e[fase]);
    s_sim_f[fase] = k_gray[mejor];

    if (!s_eng) {
        uint8_t f, fm = 0U;

        for (f = 1U; f < ALE_FASES; f++) {
            if (s_fase_e[f] > s_fase_e[fm]) { fm = f; }
        }
        s_fase = fm;
    }

    s_nivel += 0.002f * ((pmax * 0.02f) - s_nivel);
    if (s_nivel < 0.0f) { s_nivel = 0.0f; }

    /*
     * UN SIMBOLO CADA NOVENTA Y SEIS MUESTRAS, SIEMPRE, Y NO "CUANDO TOCA
     * LA FASE BUENA". 27/09/2026.
     *
     * La primera version emitia el simbolo dentro del if de la fase
     * elegida, que parece lo natural y esta mal: cuando la eleccion de fase
     * cambia -y cambia sola mientras se busca, porque las medias de energia
     * todavia se estan asentando- el flujo de simbolos se salta uno o
     * emite dos, y eso desplaza el anillo de bits. A partir de ahi el
     * sincronismo de palabra busca en un flujo que ya no es el que se
     * emitio.
     *
     * El sintoma era finisimo: con la palabra descolocada 13 o 24 muestras
     * no decodificaba NADA, y con 0, 37, 48, 61, 72 u 85 decodificaba las
     * cuatro palabras. Ocho retardos probados, seis bien y dos a cero. Un
     * fallo asi en antena se lee como "esta frecuencia no entra".
     *
     * Ahora las cuatro fases miden siempre y cada una deja apuntado SU
     * simbolo; al cerrarse el periodo se emite el de la fase elegida. Una
     * emision por periodo, pase lo que pase con la eleccion, y cambiar de
     * fase ya no mueve el flujo: solo cambia de que ventana sale el valor.
     */
    if (fase == (ALE_FASES - 1U)) { simbolo(s_sim_f[s_fase]); }
}

void ale_rx_mete(const float *audio, uint32_t n)
{
    uint32_t k;

    if (!s_on || (audio == (const float *)0)) { return; }

    for (k = 0U; k < n; k++) {
        s_hist[s_hi] = audio[k];
        s_hi++;
        if (s_hi >= ALE_SPS) { s_hi = 0U; }

        /*
         * El contador va modulo 96 -un simbolo entero- y no libre. Con un
         * contador libre de 16 bits, al dar la vuelta (65.536 no es
         * multiplo de 24) la numeracion de las cuatro fases se desplazaria,
         * y el decodificador se desengancharia solo cada cinco segundos y
         * medio sin que nada lo explicara.
         */
        s_cuenta++;
        if (s_cuenta >= ALE_SPS) { s_cuenta = 0U; }

        if (s_lleno < ALE_SPS) { s_lleno++; continue; }
        if ((s_cuenta % ALE_PASO) == 0U) { mide(); }
    }
}
