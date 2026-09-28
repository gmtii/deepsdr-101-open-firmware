#include "wspr_rx.h"
#include "wspr_fano.h"
#include <string.h>
#include <math.h>

/*
 * Las cuentas de WSPR, todas derivadas de una sola: la velocidad de
 * simbolo es 12000/8192 Hz. Se escriben como formulas y no como numeros
 * para que no puedan decir cosas distintas.
 */
#define WSPR_AUDIO_HZ     12000
#define WSPR_DIEZMA          32                      /* 12000 -> 375 */
#define WSPR_FS             (WSPR_AUDIO_HZ / WSPR_DIEZMA)    /* 375 */
#define WSPR_MUESTRAS_SIM  256U                      /* un simbolo a 375 Hz */
#define WSPR_MEDIO         (WSPR_MUESTRAS_SIM / 2U)  /* 128 */

/* El centro de la ventana, en puntos de rejilla. */
#define WSPR_BIN_CENTRO   (WSPR_RX_BINS / 2U)

/*
 * Cuantos pasos de medio simbolo puede haber esperando a la vez.
 *
 * Llegan uno cada 341 ms y el bucle principal pasa cada 16, asi que con
 * DOS ya sobraria. Cuatro por si una vuelta se alarga (un volcado de
 * flash, una pantalla entera repintada) sin que se pierda nada.
 */
#define WSPR_RX_PEND  4U

/*
 * EL OSCILADOR CABE EN OCHO MUESTRAS, Y ESO NO ES UN TRUCO: ES EL NUMERO.
 *
 * La ventana de WSPR se busca en 1.500 Hz y el audio llega a 12.000. 1500
 * entre 12000 es UN OCTAVO exacto, o sea que el oscilador avanza 2*pi/8
 * por muestra y se repite cada ocho. No hace falta ni acumulador de fase
 * ni seno ni coseno: ocho parejas en una tabla y un indice de tres bits.
 *
 * Lo que habia antes era un cosf() y un sinf() POR MUESTRA, a 12.000
 * muestras por segundo: veinticuatro mil llamadas a la trigonometria de
 * newlib cada segundo, dentro de la interrupcion del audio, para calcular
 * ocho valores que se repiten.
 */
static const float k_nco_c[8] = {
    1.0f,  0.70710678f,  0.0f, -0.70710678f,
   -1.0f, -0.70710678f, -0.0f,  0.70710678f
};
static const float k_nco_s[8] = {
    0.0f,  0.70710678f,  1.0f,  0.70710678f,
    0.0f, -0.70710678f, -1.0f, -0.70710678f
};

/* Un punto de rejilla es MEDIA casilla, asi que los cuatro tonos de un
 * simbolo estan a 0, 2, 4 y 6 puntos. */
#define WSPR_PASO_TONO    2U

/* --- el espectrograma, a medio byte por punto --- */
static void pon4(uint8_t *base, uint32_t i, uint8_t v)
{
    uint8_t *p = &base[i >> 1];

    if ((i & 1U) != 0U) { *p = (uint8_t)((*p & 0x0FU) | (uint8_t)(v << 4)); }
    else                { *p = (uint8_t)((*p & 0xF0U) | (v & 0x0FU)); }
}

static uint8_t sac4(const uint8_t *base, uint32_t i)
{
    uint8_t v = base[i >> 1];

    return (uint8_t)(((i & 1U) != 0U) ? (v >> 4) : (v & 0x0FU));
}

typedef struct {
    uint8_t *espectro;        /* [WSPR_RX_PASOS][WSPR_RX_BINS] */
    uint32_t bytes;

    /* --- mezclador y diezmado --- */
    uint8_t  nco;             /* 0..7: el oscilador cabe en ocho muestras */
    float    aci, acq;        /* acumulador del diezmado */
    uint8_t  cuenta;          /* cuantas muestras lleva el acumulador */

    /* --- la ventana de 256 muestras a 375 Hz --- */
    float    vi[WSPR_MUESTRAS_SIM];
    float    vq[WSPR_MUESTRAS_SIM];
    uint16_t escrito;         /* donde va la siguiente, en circular */
    uint16_t desde_paso;      /* cuantas desde el ultimo paso */

    uint16_t paso;            /* pasos de medio simbolo hechos */
    uint8_t  arrancado;

    /*
     * LOS PASOS PENDIENTES: lo que la interrupcion deja apuntado para que
     * lo haga el bucle principal. Ver wspr_rx_pasos_pendientes().
     *
     * Solo se guarda DONDE acababa la ventana; las muestras siguen en
     * vi/vq, que es circular de 256 y se reescribe a la mitad cada medio
     * simbolo (341 ms). El bucle principal da una vuelta cada 16 ms, o
     * sea que llega con veinte veces de margen.
     */
    uint16_t pend_pos[WSPR_RX_PEND];
    uint8_t  pend_cab, pend_col;
    uint16_t perdidos;        /* pasos que no dio tiempo a procesar */
} wspr_rx_t;

static wspr_rx_t s_rx;

/* Lo que eligio la ultima busqueda. Se guarda porque es lo primero que
 * hay que mirar cuando una emision no sale: dice si fallo la busqueda o
 * fallo el decodificador. */
static uint16_t s_ult_bin, s_ult_paso;
static int32_t  s_ult_punt;
static uint8_t s_ult_q;      /* la corroboracion del ultimo intento, 0..100 */

void wspr_rx_ultimo(uint16_t *bin, uint16_t *paso, int32_t *punt)
{
    if (bin  != (uint16_t *)0) { *bin  = s_ult_bin; }
    if (paso != (uint16_t *)0) { *paso = s_ult_paso; }
    if (punt != (int32_t *)0)  { *punt = s_ult_punt; }
}

/*
 * EL DIAGNOSTICO DE LA ULTIMA CAPTURA.
 *
 * `q` es la corroboracion del mejor candidato (0..100, hace falta 88) y
 * `hz_e2` el desvio de ese candidato respecto del centro de la ventana, en
 * centesimas de Hz. Valen tanto si decodifico como si no: cuando no sale
 * nada son lo unico que distingue "no hay senal" de "la hay y llega justa"
 * de "el reloj esta corrido".
 */
void wspr_rx_diag(uint8_t *q, int16_t *hz_e2)
{
    if (q != (uint8_t *)0) { *q = s_ult_q; }
    if (hz_e2 != (int16_t *)0) {
        float hz = (float)((int16_t)s_ult_bin - (int16_t)WSPR_BIN_CENTRO) * 0.5f
                 * ((float)WSPR_AUDIO_HZ / 8192.0f);

        *hz_e2 = (int16_t)(hz * 100.0f);
    }
}

void wspr_rx_init(void *ram, uint32_t bytes)
{
    memset(&s_rx, 0, sizeof s_rx);
    s_rx.espectro = (uint8_t *)ram;
    s_rx.bytes = bytes;
    s_rx.arrancado = (uint8_t)((ram != (void *)0) &&
                               (bytes >= WSPR_RX_RAM)) ;
    wspr_rx_reinicia();
}

void wspr_rx_reinicia(void)
{
    s_rx.nco = 0U;
    s_rx.aci = 0.0f; s_rx.acq = 0.0f;
    s_rx.cuenta = 0U;
    s_rx.escrito = 0U;
    s_rx.desde_paso = 0U;
    s_rx.paso = 0U;
    memset(s_rx.vi, 0, sizeof s_rx.vi);
    memset(s_rx.vq, 0, sizeof s_rx.vq);
    if (s_rx.arrancado) {
        memset(s_rx.espectro, 0, (size_t)WSPR_RX_BINS * WSPR_RX_PASOS / 2U);
    }
}

uint16_t wspr_rx_pasos(void) { return s_rx.paso; }
uint8_t  wspr_rx_listo(void) { return (uint8_t)(s_rx.paso >= WSPR_RX_PASOS); }

/*
 * GOERTZEL. La potencia de UN bin sobre las 256 muestras de la ventana,
 * sin tocar las otras 136.
 *
 * Es el algoritmo de siempre: un filtro resonante de segundo orden que se
 * alimenta muestra a muestra y del que al final se saca la magnitud. Para
 * señal compleja hay que hacerlo en las dos ramas y sumar las potencias,
 * que es lo que distingue un tono a +10 Hz de uno a -10.
 */
static float goertzel(const float *vi, const float *vq, uint16_t ini, int16_t medios)
{
    /*
     * `medios` va en MEDIAS casillas con signo respecto del centro, asi
     * que la frecuencia es medios/2 casillas. Goertzel no pide que el bin
     * sea entero: la recurrencia vale para cualquier w, y esa es
     * justamente la razon de usarlo aqui en vez de una transformada.
     */
    float w = 6.2831853f * (float)medios * 0.5f / (float)WSPR_MUESTRAS_SIM;
    float cw = cosf(w), sw = sinf(w);
    float coef = 2.0f * cw;
    float s1i = 0.0f, s2i = 0.0f, s1q = 0.0f, s2q = 0.0f;
    uint16_t k;

    for (k = 0U; k < WSPR_MUESTRAS_SIM; k++) {
        uint16_t p = (uint16_t)((ini + k) & (WSPR_MUESTRAS_SIM - 1U));
        float ti = coef * s1i - s2i + vi[p];
        float tq = coef * s1q - s2q + vq[p];

        s2i = s1i; s1i = ti;
        s2q = s1q; s1q = tq;
    }
    {
        /*
         * De las dos ramas a la potencia del bin, y aqui hay un signo que
         * importa.
         *
         * Goertzel sobre una secuencia real da X = s1 - e^-jw * s2, o sea
         * parte real (s1 - s2*cos w) e imaginaria (+s2*sin w). Con el
         * signo al reves se calcula el bin -k en vez del +k, y como se
         * aplica igual a las dos ramas lo que sale es el espectro ENTERO
         * DEL REVES: un tono a +1,46 Hz aparece en el bin -1.
         *
         * Lo tuve mal y lo cazo el banco de tono puro -"pico en bin 67,
         * dif -1"-. Espejado, el orden de los cuatro tonos de WSPR se
         * invierte y la correlacion con el sincronismo no engancha jamas,
         * que es exactamente lo que pasaba: ni una decodificacion, sin
         * ninguna pista de por que.
         *
         * Con Z = XI + j*XQ, la potencia del tono complejo sale de
         * juntar las dos: Z = (a - d) + j(b + c).
         */
        float ri = s1i - s2i * cw, ii = s2i * sw;
        float rq = s1q - s2q * cw, iq = s2q * sw;
        float re = ri - iq;
        float im = ii + rq;

        return re * re + im * im;
    }
}

/* Un paso: se miden los 137 bins de la ventana y se guardan en dB a
 * escala de byte. */
/*
 * UN PASO DEL ESPECTROGRAMA: 274 Goertzel de 256 muestras y 274
 * logaritmos. Unos CUATRO MILISEGUNDOS en el micro de la radio.
 *
 * ESTO NO PUEDE CORRER EN LA INTERRUPCION DEL AUDIO, y lo hacia. Un
 * bloque de audio dura 2,67 ms (256 muestras a 96 kHz), asi que cada
 * medio simbolo el manejador se llevaba mas de un bloque entero por
 * delante y el conversor repetia lo que tenia. Repetir un bloque de 2,67
 * ms es exactamente un tono de 375 Hz.
 *
 * *** El dueno, probando en 7.038,60: "escucho como un pitido constante
 * que se para coincidiendo con empieza en ... y que inicia cuando inicia
 * lo de escuchando". *** Sonaba solo mientras capturaba, porque en la
 * espera no se procesa nada. No era una senal: era esto.
 *
 * Y estaba avisado en este mismo proyecto. hfdl_modo.c lo dice desde el
 * 25/09: "si algun dia alguien mueve el Viterbi aqui, el sintoma sera el
 * audio cortado, no un error de compilacion - por eso queda dicho". Lo
 * movi igual, en otro modo, dos dias despues.
 *
 * Ahora la interrupcion solo mezcla y diezma -que es barato y
 * proporcional al bloque- y deja apuntado que hay un paso pendiente.
 * Quien lo hace es wspr_rx_pasos_pendientes(), desde el bucle principal.
 */
static void un_paso(uint16_t pos)
{
    float p[WSPR_RX_BINS];
    float mx = 1e-20f;
    uint16_t b;
    uint8_t *fila;

    if (!s_rx.arrancado || s_rx.paso >= WSPR_RX_PASOS) { return; }

    for (b = 0U; b < WSPR_RX_BINS; b++) {
        p[b] = goertzel(s_rx.vi, s_rx.vq, pos,
                        (int16_t)((int16_t)b - (int16_t)WSPR_BIN_CENTRO));
        if (p[b] > mx) { mx = p[b]; }
    }

    /*
     * Se guarda NORMALIZADO al maximo de la fila y en escala logaritmica.
     * Normalizar por fila es lo que hace que un desvanecimiento -que en
     * WSPR es la norma- no borre media emision: lo que importa es que un
     * tono destaque sobre los de al lado EN ESE INSTANTE, no cuanta señal
     * habia en total.
     */
    {
        uint32_t base = (uint32_t)s_rx.paso * WSPR_RX_BINS;

        for (b = 0U; b < WSPR_RX_BINS; b++) {
            float r = p[b] / mx;                  /* 0..1 */
            float db = 10.0f * log10f(r + 1e-9f); /* <=0 */
            int32_t v = (int32_t)(15.0f + db * (15.0f / (float)WSPR_RX_RANGO_DB));

            if (v < 0) { v = 0; }
            if (v > 15) { v = 15; }
            pon4(s_rx.espectro, base + b, (uint8_t)v);
        }
    }
    (void)fila;
    s_rx.paso++;
}

void wspr_rx_mete(const float *audio, uint16_t n)
{
    uint16_t k;

    if (!s_rx.arrancado || audio == (const float *)0) { return; }
    if (s_rx.paso >= WSPR_RX_PASOS) { return; }

    for (k = 0U; k < n; k++) {
        /* Mezcla a banda base: e^-j2pi*fc*t, con el oscilador de ocho
         * muestras. Ver k_nco_c/k_nco_s. */
        s_rx.aci += audio[k] * k_nco_c[s_rx.nco];
        s_rx.acq -= audio[k] * k_nco_s[s_rx.nco];
        s_rx.nco = (uint8_t)((s_rx.nco + 1U) & 7U);

        s_rx.cuenta++;
        if (s_rx.cuenta < (uint8_t)WSPR_DIEZMA) { continue; }

        /*
         * Diezmado por integra-y-vuelca: la media de 32 muestras. El
         * primer cero de su respuesta cae justo en 375 Hz, o sea en el
         * borde de la banda que queda despues de diezmar, que es
         * exactamente lo que hace falta para que no se cuele nada de
         * fuera doblado encima.
         */
        s_rx.vi[s_rx.escrito] = s_rx.aci / (float)WSPR_DIEZMA;
        s_rx.vq[s_rx.escrito] = s_rx.acq / (float)WSPR_DIEZMA;
        s_rx.escrito = (uint16_t)((s_rx.escrito + 1U) & (WSPR_MUESTRAS_SIM - 1U));
        s_rx.aci = 0.0f; s_rx.acq = 0.0f; s_rx.cuenta = 0U;

        s_rx.desde_paso++;
        if (s_rx.desde_paso >= WSPR_MEDIO) {
            uint8_t sig = (uint8_t)((s_rx.pend_cab + 1U) % WSPR_RX_PEND);

            s_rx.desde_paso = 0U;
            /*
             * Solo se APUNTA. El trabajo lo hace el bucle principal: aqui
             * dentro cuesta cuatro milisegundos y el audio no los tiene.
             */
            if (sig != s_rx.pend_col) {
                s_rx.pend_pos[s_rx.pend_cab] = s_rx.escrito;
                s_rx.pend_cab = sig;
            } else {
                /*
                 * La cola llena quiere decir que el bucle principal lleva
                 * mas de un segundo sin pasar. No deberia ocurrir nunca;
                 * si ocurre, se pierde el paso y se cuenta, porque un
                 * espectrograma con huecos decodifica peor y conviene que
                 * el numero se vea en vez de adivinarlo.
                 */
                if (s_rx.perdidos < 0xFFFFU) { s_rx.perdidos++; }
            }
        }
    }
}

uint8_t wspr_rx_pasos_pendientes(void)
{
    uint8_t hechos = 0U;

    while (s_rx.pend_col != s_rx.pend_cab) {
        uint16_t pos = s_rx.pend_pos[s_rx.pend_col];

        s_rx.pend_col = (uint8_t)((s_rx.pend_col + 1U) % WSPR_RX_PEND);
        un_paso(pos);
        hechos++;
        if (s_rx.paso >= WSPR_RX_PASOS) { break; }
    }
    return hechos;
}

uint16_t wspr_rx_perdidos(void) { return s_rx.perdidos; }

/* ---------------- la busqueda ---------------- */

/*
 * La puntuacion de un candidato (frecuencia f0, arranque t0).
 *
 * Se apoya en lo unico que ya se sabe del mensaje: el bit de sincronismo
 * de cada simbolo. Si el sincronismo dice 0, el tono tiene que ser el 0 o
 * el 2; si dice 1, el 1 o el 3. Asi que se suma la potencia de los dos
 * tonos que TOCAN y se resta la de los dos que no.
 *
 * Con la frecuencia o el instante mal, esa cuenta da alrededor de cero
 * porque los tonos caen donde caen; con los dos bien, sube.
 */
static int32_t puntua(const uint8_t *esp, uint16_t f0, uint16_t t0)
{
    int32_t s = 0;
    uint16_t i;

    for (i = 0U; i < WSPR_SIMBOLOS; i++) {
        uint32_t fila = (uint32_t)(t0 + 2U * i) * WSPR_RX_BINS + f0;
        uint8_t sn = k_wspr_sincro[i];

        s += (int32_t)sac4(esp, fila + (uint32_t)sn * WSPR_PASO_TONO)
           + (int32_t)sac4(esp, fila + (uint32_t)(sn + 2U) * WSPR_PASO_TONO);
        s -= (int32_t)sac4(esp, fila + (uint32_t)(1U - sn) * WSPR_PASO_TONO)
           + (int32_t)sac4(esp, fila + (uint32_t)(3U - sn) * WSPR_PASO_TONO);
    }
    return s;
}

uint8_t wspr_rx_decodifica(wspr_msg_t *m, int16_t *hz_out, uint8_t *calidad_out)
{
    const uint8_t *esp = s_rx.espectro;
    uint16_t f0, t0, mf = 0U, mt = 0U;
    int32_t mejor = -0x7FFFFFFF;
    uint8_t canal[WSPR_SIMBOLOS], desmez[WSPR_SIMBOLOS];
    int8_t  blandos[2U * WSPR_BITS_ENTRA];
    uint8_t bits[7], q = 0U;
    uint16_t i;

    if (!s_rx.arrancado || s_rx.paso < WSPR_RX_PASOS) { return 0U; }

    /* Los cuatro tonos tienen que caber: f0 va de 0 a BINS-4. Y el
     * arranque, de 0 al margen que sobra despues de 162 simbolos. */
    for (t0 = 0U; t0 + 2U * WSPR_SIMBOLOS <= WSPR_RX_PASOS; t0++) {
        for (f0 = 0U; f0 + 3U * WSPR_PASO_TONO < WSPR_RX_BINS; f0++) {
            int32_t p = puntua(esp, f0, t0);

            if (p > mejor) { mejor = p; mf = f0; mt = t0; }
        }
    }

    s_ult_bin = mf; s_ult_paso = mt; s_ult_punt = mejor;
    s_ult_q = 0U;

    /*
     * Los valores blandos. Para cada simbolo, el bit de datos dice si el
     * tono esta arriba (sincro+2) o abajo (sincro): la diferencia de las
     * dos potencias ES la confianza, con su signo puesto.
     */
    for (i = 0U; i < WSPR_SIMBOLOS; i++) {
        uint32_t fila = (uint32_t)(mt + 2U * i) * WSPR_RX_BINS + mf;
        uint8_t sn = k_wspr_sincro[i];
        int32_t d = (int32_t)sac4(esp, fila + (uint32_t)(sn + 2U) * WSPR_PASO_TONO)
                  - (int32_t)sac4(esp, fila + (uint32_t)sn * WSPR_PASO_TONO);

        /* De +-15 a +-120, que es la escala del decodificador. */
        d *= 8;
        if (d > 127) { d = 127; }
        if (d < -127) { d = -127; }
        canal[i] = (uint8_t)(d < 0 ? 0 : 1);
        blandos[i] = (int8_t)d;   /* provisional: falta desentrelazar */
    }

    /*
     * El entrelazado se deshace sobre los valores BLANDOS, no sobre los
     * bits duros: tirar la confianza aqui seria regalarle al decodificador
     * justo lo que necesita para trabajar bajo el ruido.
     */
    {
        uint8_t tmp[WSPR_SIMBOLOS];
        int8_t  bl2[WSPR_SIMBOLOS];

        for (i = 0U; i < WSPR_SIMBOLOS; i++) { tmp[i] = (uint8_t)(blandos[i] + 128); }
        wspr_entrelaza(tmp, desmez, 1U);
        for (i = 0U; i < WSPR_SIMBOLOS; i++) { bl2[i] = (int8_t)((int16_t)desmez[i] - 128); }
        for (i = 0U; i < WSPR_SIMBOLOS; i++) { blandos[i] = bl2[i]; }
    }
    (void)canal;

    /*
     * Y a decodificar, reusando el sitio del espectrograma: ya se ha
     * sacado de el todo lo que hacia falta.
     */
    /*
     * LA CORROBORACION SE GUARDA PASE LO QUE PASE, y eso no es un extra.
     *
     * WSPR no lleva CRC: el decodificador de Fano siempre devuelve ALGO, y
     * lo unico que separa un mensaje de un invento es volver a codificar lo
     * que ha salido y ver cuanto cuadra con lo recibido. Medido en el
     * banco: una emision de verdad da 93-100 y el ruido puro da 74-83, de
     * ahi el corte en 88.
     *
     * Ese numero es ademas el UNICO diagnostico util cuando no sale nada,
     * y por eso se guarda tambien cuando falla. "No decodifica" puede ser
     * que no haya senal (q por los 70), que la haya y llegue justa (q por
     * los 80), o que el reloj este corrido y no se encuentre ni el
     * sincronismo. Sin el numero, las tres se ven igual: una tabla vacia.
     */
    if (!wspr_fano_ex(blandos, s_rx.espectro, bits, (uint32_t *)0, &q)) {
        s_ult_q = 0U;
        return 0U;
    }
    s_ult_q = q;
    if (q < 88U) { return 0U; }
    if (!wspr_desempaqueta(bits, m)) { return 0U; }

    if (hz_out != (int16_t *)0) {
        /* El tono 0 esta en el bin mf; el centro de la ventana es
         * WSPR_BIN_CENTRO. Cada bin son 12000/8192 Hz. */
        float hz = (float)((int16_t)mf - (int16_t)WSPR_BIN_CENTRO) * 0.5f
                 * ((float)WSPR_AUDIO_HZ / 8192.0f);
        *hz_out = (int16_t)(hz * 100.0f);
    }
    if (calidad_out != (uint8_t *)0) { *calidad_out = q; }
    return 1U;
}
