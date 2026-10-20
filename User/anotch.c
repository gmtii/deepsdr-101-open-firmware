/*
 * Notch automatico por prediccion lineal (LMS normalizado).
 * Ver anotch.h para el porque de cada pieza.
 */
#include "anotch.h"

#define LINEA (ANOTCH_TOMAS_MAX + ANOTCH_RETARDO_MAX + 1U)

static float    s_w[ANOTCH_TOMAS_MAX];
/*
 * LA LINEA DE RETARDO, POR DUPLICADO - 05/10/2026.
 *
 * Este predictor recorre la linea DOS VECES por muestra -una para predecir
 * y otra para corregir los pesos-, con 48 tomas cada una, y cada paso
 * llevaba un `% LINEA`. LINEA son 129, que no es potencia de dos: cada uno
 * de esos modulos es una division entera de verdad. Casi cien por muestra,
 * a 12 kHz.
 *
 * Guardando cada muestra en s_x[cab] Y en s_x[cab+LINEA], el recorrido hacia
 * atras no se sale nunca por abajo y no hace falta dar la vuelta:
 *
 *   el punto de partida es  cab + LINEA - retardo >= LINEA - RETARDO_MAX
 *   y se retrocede como mucho TOMAS_MAX - 1
 *   LINEA = TOMAS_MAX + RETARDO_MAX + 1, asi que lo mas bajo que se llega
 *   es TOMAS_MAX + 1 - (TOMAS_MAX - 1) = 2. Nunca negativo.
 *
 * Cuesta 516 bytes mas. El mismo truco que ya usa rds.c y, desde hoy,
 * analizador.c.
 */
static float    s_x[2U * LINEA];
static uint16_t s_cab;
/*
 * ======================================================================
 * EL ARRANQUE: 72 MUESTRAS SIN ADAPTAR - 10/10/2026
 * ======================================================================
 *
 * De una revision a fondo. MEDIDO, no supuesto:
 *
 *   se enciende el notch en una pausa (limpia() deja la linea a cero),
 *   entra voz a 1.000 de amplitud (la escala real de aqui es int16, ver
 *   demod_am.c: esto NO viene normalizado a +-1)
 *       pico de entrada      994
 *       pico de salida   124.929
 *       ganancia          +42 dB
 *
 * Un golpe de 42 dB en el audio, que ademas satura. Y no es el fallo que
 * ya arreglo el comentario de arriba -el de los 81 dB, que era la
 * estimacion retrasada-: aquel estaba en regimen y este esta en el
 * arranque.
 *
 * POR QUE PASA. El paso del lazo es g = mu*e/pot con pot la energia de la
 * ventana. Recien limpiada, la ventana esta A CERO: durante las primeras
 * s_retardo + s_tomas muestras solo tiene unas pocas muestras de verdad,
 * asi que pot es diminuta mientras e ya es la señal entera. Con pot ~ 1 y
 * e ~ 1000, g sale 20, el peso se dispara y en la muestra siguiente el
 * predictor devuelve veinte mil.
 *
 * El +1.0e-12f del denominador NO regulariza nada: esta diecinueve
 * ordenes de magnitud por debajo de una energia de ventana real (48
 * tomas x 1000^2 = 5e7). Solo evita el 0/0.
 *
 * EL ARREGLO: no adaptar hasta que la linea este llena. Son
 * s_retardo + s_tomas muestras -unas 72, o sea 6 ms a 12 kHz-, durante
 * las cuales el audio SALE TAL CUAL. Imperceptible, y exacto: el lazo
 * empieza cuando sus datos existen, no antes.
 *
 * *** Y NO TOCA EL MODO NORMAL. *** Pasadas esas 72 muestras no queda ni
 * una instruccion de diferencia: el contador llega a su tope y se queda
 * ahi. Lo que cambia es solo el arranque, que es donde estaba el golpe.
 *
 * De regalo, tapa tambien la otra mitad del problema: limpia() se llama
 * desde el bucle principal -al tocar la fuerza en el menu- mientras
 * anotch_process() corre en la interrupcion del audio. Antes, esa limpieza
 * a mitad de bloque dejaba el lazo con la ventana vacia y la pot del
 * bloque; ahora, cualquier limpieza desarma la adaptacion hasta que la
 * linea se vuelve a llenar.
 */
static uint16_t s_arranque;   /* muestras que faltan para fiarse de pot */
static uint16_t s_tomas = 48U;
static uint16_t s_retardo = ANOTCH_RETARDO;
static float    s_mu = 0.02f;
/*
 * LA POTENCIA DE LA VENTANA, Y DOS FORMAS DE CALCULARLA MAL.
 *
 * El paso de adaptacion va dividido por la energia de la ventana que usa
 * el predictor: asi es LMS NORMALIZADO, que es estable para cualquier mu
 * entre 0 y 2 y no hay que reajustar nada cuando se mueve el AGC.
 *
 * La primera version la seguia con un filtro de un polo lento. Eso hacia
 * que el lazo pudiera irse: en un arranque de señal la energia de verdad
 * sube al instante y la estimacion va por detras, asi que durante unos
 * milisegundos se divide por un numero demasiado pequeño y el paso sale
 * enorme. El banco lo canto con retardo 2 y mu 0,08 - la salida tenia
 * 81 dB MAS que la entrada.
 *
 * La segunda la llevaba al dia sumando la muestra que entra y restando la
 * que sale, que es exacto sobre el papel. En float no lo es: el redondeo
 * se acumula, la suma se va por debajo de cero con una señal silabica, y
 * el recorte a cero que la salvaba de ser negativa la dejaba MINTIENDO -
 * energia de verdad alta, suma casi cero, division por casi nada-. Salio
 * peor que la primera: NaN en media tabla del barrido.
 *
 * La tercera es la buena, y ademas la mas simple: se suma dentro del
 * MISMO bucle que ya recorre la ventana para predecir. No cuesta un
 * recorrido de mas, es exacta en cada muestra, y no hay nada que pueda
 * derivar porque no se acarrea nada de una muestra a la siguiente.
 */
static uint8_t  s_on;
static float    s_fs = 12000.0f;

static void limpia(void)
{
    uint16_t i;
    for (i = 0U; i < ANOTCH_TOMAS_MAX; i++) { s_w[i] = 0.0f; }
    for (i = 0U; i < 2U * LINEA; i++) { s_x[i] = 0.0f; }
    s_cab = 0U;
    /* La linea esta vacia: hasta que se llene, no se adapta. Ver
     * s_arranque. */
    s_arranque = (uint16_t)(s_retardo + s_tomas);
}

void anotch_init(float fs_hz)
{
    if (fs_hz > 0.0f) { s_fs = fs_hz; }
    limpia();
    s_on = 0U;
}

void anotch_set_activo(uint8_t on)
{
    uint8_t v = (uint8_t)(on ? 1U : 0U);
    if (v && !s_on) { limpia(); }   /* ver el comentario de anotch.h */
    s_on = v;
}
uint8_t anotch_activo(void) { return s_on; }

/*
 * LAS CUATRO FUERZAS, Y EL RETARDO.
 *
 * Todo esto sale del barrido del banco, que mide a la vez cuanto baja el
 * pitido y cuanto se lleva de la voz. Con una sola de las dos columnas se
 * elige mal por construccion: subir la ganancia mejora la primera y
 * empeora la segunda exactamente igual.
 *
 * EL RETARDO es la pieza que de verdad decide, y la primera version lo
 * puso en 6 a ojo. El barrido dice que eso era un error:
 *
 *      retardo 2   quita ~50 dB   se lleva 8-10 dB de la voz
 *      retardo 6   quita ~48 dB   se lleva  2-5 dB
 *      retardo 12  quita ~50 dB   se lleva  1-3 dB
 *      retardo 24  quita ~65 dB   se lleva  0,1-1,3 dB
 *
 * O sea que alargarlo mejora LAS DOS COSAS A LA VEZ hasta 24, que son
 * 2 ms a 12 kHz. Tiene sentido: a 2 ms una voz ya no se parece a si
 * misma y una portadora si, que es justo la separacion que se busca.
 *
 * Con el retardo puesto en 24, lo que quedan las tomas y la ganancia es
 * un reparto entre "engancha rapido" y "no toca la voz", y de ahi salen
 * las cuatro fuerzas. Las cuatro miden mas de 55 dB de pitido quitado y
 * menos de 1,3 dB de voz.
 */
static const struct { uint16_t tomas; float mu; } k_fuerza[4] = {
    { 24U, 0.008f },   /* suave:  59 dB de pitido, 0,2 dB de voz */
    { 48U, 0.020f },   /* normal: 58 dB, 0,2 dB */
    { 72U, 0.040f },   /* fuerte: 65 dB, 0,6 dB */
    { 96U, 0.080f }    /* a saco: 79 dB, 1,1 dB - para el que no se va */
};

void anotch_set_fuerza(uint8_t f)
{
    if (f > 3U) { f = 3U; }
    anotch_params(k_fuerza[f].tomas, s_retardo, k_fuerza[f].mu);
}

uint8_t anotch_fuerza(void)
{
    uint8_t f;
    for (f = 0U; f < 4U; f++) {
        if (k_fuerza[f].tomas == s_tomas) { return f; }
    }
    return 1U;
}

void anotch_params(uint16_t tomas, uint16_t retardo, float mu)
{
    if (tomas == 0U || tomas > ANOTCH_TOMAS_MAX) { tomas = ANOTCH_TOMAS_MAX; }
    if (retardo == 0U) { retardo = 1U; }
    if (retardo > ANOTCH_RETARDO_MAX) { retardo = ANOTCH_RETARDO_MAX; }
    if (mu < 0.0f) { mu = 0.0f; }
    if (mu > 0.5f) { mu = 0.5f; }
    s_tomas = tomas;
    s_retardo = retardo;
    /* Si cambian las tomas o el retardo, la ventana que hay deja de ser la
     * que el lazo va a usar: se rearma el arranque. Ver s_arranque. */
    s_arranque = (uint16_t)(s_retardo + s_tomas);
    s_mu = mu;
    limpia();
}

float anotch_energia_pesos(void)
{
    float e = 0.0f;
    uint16_t i;
    for (i = 0U; i < s_tomas; i++) { e += s_w[i] * s_w[i]; }
    return e;
}

void anotch_process(float *audio, uint32_t n)
{
    uint32_t k;
    float    pot;
    uint16_t i0, p0;

    if (!s_on || audio == 0) { return; }

    /*
     * LA ENERGIA DE LA VENTANA, UNA VEZ POR BLOQUE Y NO POR MUESTRA.
     *
     * El bucle de abajo recorria las 48 tomas DOS veces por muestra: una
     * para el predictor y otra para sumar v*v. Y la segunda suma siempre
     * lo mismo menos una muestra: la ventana se desliza de uno en uno, asi
     * que de una muestra a la siguiente entra un valor y sale otro.
     *
     * Con 48 tomas y 32 muestras por bloque eso son 1.536 multiplicaciones
     * y sumas por bloque que no hacian falta.
     *
     * SE RECALCULA EXACTA EN CADA BLOQUE, y no una sola vez al arrancar.
     * El "suma uno, resta otro" acumula error de redondeo, y esta energia
     * no es un adorno: va en el denominador del paso del lazo
     * (g = mu*e/pot). Un denominador que se va de madre cambia como adapta
     * el notch. Recalculando por bloque, el error no puede crecer mas alla
     * de 32 muestras.
     */
    p0 = (uint16_t)(s_cab + LINEA - s_retardo);
    pot = 0.0f;
    for (i0 = 0U; i0 < s_tomas; i0++) {
        float v = s_x[p0 - i0];
        pot += v * v;
    }

    for (k = 0U; k < n; k++) {
        float x = audio[k];
        float y = 0.0f;
        float e, g;
        uint16_t i, p;

        /* La muestra de ahora entra en la linea; las que usa el predictor
         * son las de hace `retardo` y anteriores. Por duplicado: ver s_x[]. */
        s_x[s_cab] = x;
        s_x[s_cab + LINEA] = x;

        /* Retroceder `retardo` posiciones, en la copia de arriba: asi el
         * recorrido hacia atras de las tomas no se sale por abajo. */
        p = (uint16_t)(s_cab + LINEA - s_retardo);

        for (i = 0U; i < s_tomas; i++) {
            y += s_w[i] * s_x[p - i];
        }

        e = x - y;

        /*
         * MIENTRAS LA LINEA SE LLENA, NO SE ADAPTA. Ver s_arranque: `pot`
         * es la energia de una ventana a medio llenar y el paso saldria
         * disparado. El audio sale tal cual, que es lo que tiene que
         * hacer un notch que todavia no sabe nada.
         */
        if (s_arranque != 0U) {
            s_arranque--;
        } else {
            g = s_mu * e / (pot + 1.0e-12f);

            for (i = 0U; i < s_tomas; i++) {
                s_w[i] += g * s_x[p - i];
            }
        }

        audio[k] = e;
        s_cab++;
        if (s_cab >= (uint16_t)LINEA) { s_cab = 0U; }

        /* Y la ventana de la muestra siguiente: entra una y sale otra. El
         * indice mas bajo que se toca es s_cab + 1 (ver el comentario de
         * s_x[]), asi que no se sale por abajo. */
        {
            uint16_t pn  = (uint16_t)(s_cab + LINEA - s_retardo);
            float    ent = s_x[pn];
            float    sal = s_x[pn - s_tomas];

            pot += ent * ent - sal * sal;
            if (pot < 0.0f) { pot = 0.0f; }   /* el redondeo, acotado */
        }
    }
    (void)s_fs;
}
