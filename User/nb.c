#include "nb.h"

/*
 * Ver nb.h para que hace esto y por que va donde va. Aqui esta el como.
 *
 * El bucle es, por bloque:
 *   1. magnitud de cada muestra, |I|+|Q| (ver NOTA L1 abajo)
 *   2. marcar las que pasan de umbral = suelo * K
 *   3. ensanchar cada marca NB_GUARDA muestras a cada lado
 *   4. rellenar cada tramo marcado interpolando entre los bordes buenos
 *   5. actualizar el suelo SOLO con las muestras no marcadas
 *
 * NOTA L1: |I|+|Q| en vez de sqrt(I*I+Q*Q). Es la aproximacion de
 * "city block" y se pasa hasta un 41% (cuando I==Q) y se queda corta 0%
 * (cuando uno de los dos es cero). Da igual: aqui no se mide nada, solo se
 * compara contra un umbral que se deduce del MISMO estimador, asi que el
 * sesgo se cancela en la division. Y ahorra una raiz por muestra a 96 kHz
 * dentro de una interrupcion, que no es poco.
 */

#define NB_GUARDA        3U     /* muestras borradas a cada lado del pico */
#define NB_TOPE_BORRADO  64U    /* si hay que borrar mas de esto, no se borra nada */
#define NB_SUELO_ALFA    0.005f /* ~0,5 s de constante de tiempo a 2,67 ms/bloque */
#define NB_SUELO_MIN     1.0f   /* evita dividir por cero con la entrada en silencio */

/* Umbrales por nivel, en veces el suelo de ruido. Medidos con sim/nbtest.c,
 * no elegidos a ojo - ver la tabla que imprime ese banco. */
static const float k_umbral[4] = { 0.0f, 8.0f, 5.0f, 3.0f };

static uint8_t s_nivel = 0U;
static float   s_suelo = 0.0f;
static uint8_t s_suelo_listo = 0U;

/* Ultima muestra buena del bloque anterior, para poder interpolar un tramo
 * que empiece en la muestra 0: sin esto habria que dejarlo tal cual, y un
 * pulso que caiga justo en el borde del bloque es el caso normal, no el
 * raro -los bloques no saben nada de cuando llegan los pulsos-. */
static float s_ult_i = 0.0f;
static float s_ult_q = 0.0f;

static float mag_l1(float i, float q)
{
    float ai = (i < 0.0f) ? -i : i;
    float aq = (q < 0.0f) ? -q : q;
    return ai + aq;
}

void nb_set_nivel(uint8_t nivel)
{
    s_nivel = (nivel <= 3U) ? nivel : 0U;
}

uint8_t nb_get_nivel(void)
{
    return s_nivel;
}

void nb_reset(void)
{
    s_suelo = 0.0f;
    s_suelo_listo = 0U;
    s_ult_i = 0.0f;
    s_ult_q = 0.0f;
}

uint32_t nb_process(float *i_buf, float *q_buf, uint32_t n)
{
    static float   mag[NB_MAX_BLOQUE];
    static uint8_t marca[NB_MAX_BLOQUE];
    float    umbral, suma;
    uint32_t k, j, marcadas = 0U, buenas = 0U;

    if (s_nivel == 0U || n == 0U || n > NB_MAX_BLOQUE) { return 0U; }

    /* 1. magnitudes */
    suma = 0.0f;
    for (k = 0U; k < n; k++) {
        mag[k] = mag_l1(i_buf[k], q_buf[k]);
        suma += mag[k];
    }

    /* Primer bloque tras un reset: el suelo se siembra con la media de este
     * bloque en vez de arrancar de cero. Arrancando de cero, el umbral sale
     * ridiculo y el primer bloque se borra entero. */
    if (!s_suelo_listo) {
        s_suelo = suma / (float)n;
        if (s_suelo < NB_SUELO_MIN) { s_suelo = NB_SUELO_MIN; }
        s_suelo_listo = 1U;
    }

    umbral = s_suelo * k_umbral[s_nivel];

    /* 2 + 3. marcar y ensanchar, en una sola pasada hacia delante: cada
     * muestra que pasa de umbral marca su entorno. */
    for (k = 0U; k < n; k++) { marca[k] = 0U; }
    for (k = 0U; k < n; k++) {
        if (mag[k] > umbral) {
            uint32_t d = (k > NB_GUARDA) ? (k - NB_GUARDA) : 0U;
            uint32_t h = k + NB_GUARDA;

            if (h >= n) { h = n - 1U; }
            for (j = d; j <= h; j++) { marca[j] = 1U; }
        }
    }
    for (k = 0U; k < n; k++) { marcadas += marca[k]; }

    /*
     * FRENO. Si hay que borrar mas de NB_TOPE_BORRADO muestras, esto NO es
     * ruido de pulsos: es una señal fuerte, o una ráfaga larga, o el suelo
     * se ha quedado desfasado. Borrar un cuarto del bloque se oiria mucho
     * peor que el ruido que se pretende quitar -la señal buena saldria
     * troceada-, asi que en ese caso se deja el bloque intacto y se deja que
     * el suelo se ponga al dia por su cuenta. Sin este freno, una portadora
     * fuerte al sintonizarla se pica a si misma.
     */
    if (marcadas > NB_TOPE_BORRADO) {
        float media = suma / (float)n;
        s_suelo += NB_SUELO_ALFA * (media - s_suelo);
        if (s_suelo < NB_SUELO_MIN) { s_suelo = NB_SUELO_MIN; }
        s_ult_i = i_buf[n - 1U];
        s_ult_q = q_buf[n - 1U];
        return 0U;
    }

    /* 4. rellenar los tramos marcados interpolando entre los bordes buenos.
     * Interpolar y no poner a cero: un hueco a cero es un escalon, y un
     * escalon es otro pulso -mas pequeño, pero pulso-, o sea que el blanker
     * se fabricaria su propio chasquido. */
    k = 0U;
    while (k < n) {
        uint32_t ini, fin;
        float i0, q0, i1, q1, paso_i, paso_q;

        if (!marca[k]) { k++; continue; }

        ini = k;
        fin = k;
        while (fin + 1U < n && marca[fin + 1U]) { fin++; }

        /* Borde de la izquierda: la muestra buena anterior, o la ultima del
         * bloque de antes si el tramo empieza en 0. */
        if (ini > 0U) { i0 = i_buf[ini - 1U]; q0 = q_buf[ini - 1U]; }
        else          { i0 = s_ult_i;         q0 = s_ult_q;         }

        /* Borde de la derecha: la siguiente buena, o -si el tramo llega al
         * final del bloque- se prolonga el valor de la izquierda, que es lo
         * unico honrado sin ver el bloque siguiente. */
        if (fin + 1U < n) { i1 = i_buf[fin + 1U]; q1 = q_buf[fin + 1U]; }
        else              { i1 = i0;              q1 = q0;              }

        paso_i = (i1 - i0) / (float)(fin - ini + 2U);
        paso_q = (q1 - q0) / (float)(fin - ini + 2U);
        for (j = ini; j <= fin; j++) {
            float t = (float)(j - ini + 1U);
            i_buf[j] = i0 + paso_i * t;
            q_buf[j] = q0 + paso_q * t;
        }
        k = fin + 1U;
    }

    /* 5. el suelo se actualiza SOLO con lo que no se ha borrado. Si se
     * actualizara con todo, cada pulso subiria el suelo, el umbral subiria
     * detras, y el blanker se iria apagando solo justo cuando mas falta
     * hace: cuanto peor el ruido, menos lo quitaria. */
    suma = 0.0f;
    for (k = 0U; k < n; k++) {
        if (!marca[k]) { suma += mag[k]; buenas++; }
    }
    if (buenas > 0U) {
        float media = suma / (float)buenas;
        s_suelo += NB_SUELO_ALFA * (media - s_suelo);
        if (s_suelo < NB_SUELO_MIN) { s_suelo = NB_SUELO_MIN; }
    }

    s_ult_i = i_buf[n - 1U];
    s_ult_q = q_buf[n - 1U];
    return marcadas;
}
