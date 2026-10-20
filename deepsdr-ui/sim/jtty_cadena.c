/*
 * JTTY: JUNTAR ATOMOS EN MENSAJES, CON EL MODO DE VERDAD - 28/09/2026.
 *
 * POR QUE ESTE BANCO NO EXISTIA Y TENIA QUE EXISTIR
 * -------------------------------------------------
 * sim/jtty_aire.c manda UNA trama y mira si sale el atomo. jtty_modo.c
 * -que es quien decide si dos atomos son el mismo mensaje- no lo tocaba
 * NADIE: se escribio, se leyo, y se dio por bueno. Las tres maneras
 * distintas de juntar mensajes que no van juntos salieron todas de la
 * antena, una por foto:
 *
 *   1. pegar por frecuencia sola, sin mirar la fase de la trama;
 *   2. medir el hueco con la hora a la que el bucle principal se entera,
 *      que no es la hora a la que llego la trama;
 *   3. y la peor, porque no se veia leyendo: con los cuatro cubos
 *      ocupados, `cubo_de()` devolvia el mas viejo SIN cerrarlo y el
 *      atomo nuevo se pegaba al mensaje de otra estacion.
 *
 * Aqui se le da al modo de verdad -User/jtty_modo.c, el mismo que corre en
 * la radio- una banda con varias estaciones a la vez, con sus fases y sus
 * tramas perdidas, y se leen los renglones que saca por jtty_modo_linea().
 *
 * COMPROBADO QUE ESTE BANCO CAZA LOS TRES, compilandolo contra el
 * jtty_modo.c de antes:
 *
 *   la prueba 3 daba "CQ K1ABC CQ AGN?" en un renglon   <- fallo 1
 *   la prueba 4 perdia los puntos del hueco             <- fallo 2
 *
 * Un banco que pasa con el codigo viejo y con el nuevo no comprueba nada,
 * y en este proyecto ya ha pasado tres veces -el del splash comparaba con
 * el fondo equivocado, el de TEXT5 tenia escrita la regla al reves, y
 * habia un exige() que no podia fallar-. Asi que se mira.
 *
 * DEL FALLO 3 NO PUEDO DECIR LO MISMO, y conviene que quede escrito. Ese
 * -reutilizar un cubo sin cerrarlo- salio de LEER el codigo mientras
 * arreglaba el 1, no de este banco: para reproducirlo hay que volver a
 * poner tambien el `if (!usado)` que lo gateaba, y la version vieja que
 * monte para la comprobacion de arriba ya no lo tiene. Lo que este banco
 * hace por el 3 es vigilar que no vuelva -la prueba 6 tiene mas mensajes
 * abiertos que cubos y exige que ningun renglon pegue dos estaciones-,
 * que no es lo mismo que haberlo cazado.
 *
 * LO QUE DE VERDAD SEPARA DOS ESTACIONES. Las tramas de un mensaje van
 * pegadas: 59 simbolos a media fila por simbolo son 118 filas EXACTAS de
 * una a la siguiente. Otra estacion cae en cualquier otra fase. Eso no es
 * una heuristica ajustable, es la longitud de la trama.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "jtty_fec.h"
#include "jtty_msg.h"
#include "jtty_rx.h"
#include "jtty_modo.h"

static int fallos = 0;

static void exige(int cond, const char *que)
{
    printf("      %-58s %s\n", que, cond ? "vale" : "MAL");
    if (!cond) { fallos++; }
}

/* ---------------------------------------------------------- */
/* Los andamios que el modo espera de la radio.               */

volatile uint32_t g_msticks = 0;

/* La memoria prestada: en la radio es la union de FT8/HFDL/WSPR/AIS/ALE. */
static uint8_t g_prestada[54432];
static uint8_t g_cogida = 0;

uint8_t *hfdl_ram_todo(uint32_t *cap)
{
    if (cap) { *cap = (uint32_t)sizeof g_prestada; }
    return g_prestada;
}
void hfdl_ram_coge(void)   { g_cogida = 1; }
void hfdl_ram_suelta(void) { g_cogida = 0; }
uint8_t waterfall_prestada(void) { return 0U; }

/* El reloj solo pone la hora del renglon; aqui da igual cual. */
#include "rtc_hw.h"
void rtc_hw_get(rtc_hw_datetime_t *out)
{
    if (out) { memset(out, 0, sizeof *out); out->hour = 16U; out->minute = 49U; }
}

/* ---------------------------------------------------------- */
/* El modulador, igual que en sim/jtty_aire.c.                */

#define FS      12000.0f
#define CENTRO  1500.0f
#define TONO_HZ 31.25f
#define MS_SIM  384             /* 12000/31,25 */

/*
 * AQUI NO HAY RUIDO, Y ES A PROPOSITO.
 *
 * Habia un generador gaussiano copiado de sim/jtty_aire.c que no usaba
 * ninguna prueba. Lo que se mide en este banco es el ENCADENADO -si dos
 * atomos son el mismo mensaje- y eso se ve mejor con las tramas llegando
 * limpias: si una prueba falla, que sea porque la regla esta mal y no
 * porque esa semilla tuvo mala suerte. La sensibilidad se mide en
 * sim/jtty_sens.c, que si tiene ruido y desvanecimiento.
 *
 * Se borra en vez de dejarlo apagado: codigo muerto en un banco es codigo
 * que alguien da por probado.
 */
static float gauss_pulso(float t, float bt)
{
    float a = 2.0f * 3.14159265f * bt / sqrtf(logf(2.0f));
    return 0.5f * (erff(a * (t + 0.5f) / 1.41421356f)
                   - erff(a * (t - 0.5f) / 1.41421356f));
}

/*
 * Mete una trama en `out` EMPEZANDO en la muestra `ini`, sumandose a lo
 * que ya hubiera: las estaciones se solapan de verdad, como en la banda.
 */
static void mete_trama(const uint8_t carga[JTTY_CARGA_BITS], float desvio,
                       long ini, float amp, float *out, long max)
{
    uint8_t datos[JTTY_SIMBOLOS], tonos[JTTY_TRAMA_SIM];
    static float inst[JTTY_TRAMA_SIM * MS_SIM];
    float fase = 0.0f;
    int i, m, n = (int)JTTY_TRAMA_SIM;

    if (ini < 0 || ini + (long)n * MS_SIM > max) {
        printf("      (no cabe la trama en %ld)\n", ini);
        exit(2);
    }
    jtty_fec_codifica(carga, datos);
    for (i = 0; i < (int)JTTY_SYNC_SIM; i++) { tonos[i] = k_jtty_sync[i]; }
    for (i = 0; i < (int)JTTY_SIMBOLOS; i++) {
        tonos[JTTY_SYNC_SIM + i] = datos[i];
    }

    for (i = 0; i < n * MS_SIM; i++) { inst[i] = 0.0f; }
    for (i = 0; i < n; i++) {
        float f = ((float)tonos[i] - 1.5f) * TONO_HZ;

        for (m = 0; m < n * MS_SIM; m++) {
            float t = ((float)m - ((float)i + 0.5f) * (float)MS_SIM)
                      / (float)MS_SIM;
            if (t > -2.5f && t < 2.5f) { inst[m] += f * gauss_pulso(t, 1.0f); }
        }
    }
    for (m = 0; m < n * MS_SIM; m++) {
        fase += 6.28318531f * (CENTRO + desvio + inst[m]) / FS;
        if (fase > 6.28318531f) { fase -= 6.28318531f; }
        out[ini + m] += amp * cosf(fase);
    }
}

static void bits_de(const char *txt, uint8_t carga[JTTY_CARGA_BITS])
{
    uint8_t i;
    for (i = 0U; i < (uint8_t)JTTY_CARGA_BITS; i++) {
        carga[i] = (uint8_t)(txt[i] == '1');
    }
}

/* La misma carga pero SIN el bit de fin: una trama de en medio. */
static void sin_fin(const char *txt, uint8_t carga[JTTY_CARGA_BITS])
{
    bits_de(txt, carga);
    carga[JTTY_CARGA_BITS - 1U] = 0U;
}

/* ---------------------------------------------------------- */

#define SEG      30L
#define AUDIO_N  ((long)(12000 * SEG))
static float g_audio[AUDIO_N];

/* Una trama dura esto en muestras de 12 kHz: 59 * 384. */
#define TRAMA_MU ((long)JTTY_TRAMA_SIM * MS_SIM)

static void corre(long n)
{
    long i;

    for (i = 0; i + 64 <= n; i += 64) {
        jtty_modo_mete(&g_audio[i], 64U);
        /*
         * El bucle principal de la radio pasa cada 33 ms y resuelve como
         * mucho dos candidatos. Aqui se imita: 64 muestras son 5,3 ms, o
         * sea una pasada cada seis bloques.
         */
        if ((i / 64) % 6 == 0) {
            g_msticks = (uint32_t)(i * 1000L / 12000L);
            jtty_modo_poll();
        }
    }
    /*
     * Y AL FINAL SE VACIA LA COLA. En la radio el bucle principal no para,
     * asi que aqui tampoco: una sola pasada resuelve DOS candidatos y con
     * nueve estaciones quedan siete sin mirar. El primer intento de la
     * prueba 6 dio seis renglones de nueve por esto, no por el codigo del
     * modo - el banco se estaba midiendo a si mismo-.
     */
    for (i = 0; i < 40; i++) { jtty_modo_poll(); }
    /* Y el plazo de cerrar los que quedan a medias. */
    g_msticks += 9000U;
    for (i = 0; i < 40; i++) { jtty_modo_poll(); }
}

/* Cuantos renglones contienen `t`. */
static int cuantos_con(const char *t)
{
    uint8_t i, n = jtty_modo_lineas();
    int c = 0;

    for (i = 0U; i < n; i++) {
        if (strstr(jtty_modo_linea(i), t) != NULL) { c++; }
    }
    return c;
}

static void pinta(void)
{
    uint8_t i, n = jtty_modo_lineas();

    for (i = 0U; i < n; i++) { printf("        | %s\n", jtty_modo_linea(i)); }
    if (n == 0U) { printf("        | (ningun renglon)\n"); }
}

/*
 * Los mismos diez vectores normativos que usa sim/jtty_aire.c: 34 bits y
 * el texto que tienen que dar. Salen de lib/jtty/jtty_source_encoding.txt
 * de WSJT-X v3.2.0-rc1.
 *
 * El bit 34 -el ultimo- es el de FIN DE MENSAJE, y aqui importa: para
 * mandar varias tramas del mismo mensaje hay que poner a cero el de las
 * de en medio, que es lo que hace `sin_fin()`.
 */
static const struct { const char *bits, *esp; } k_oro[] = {
    { "0000100110111101111000110101000001", "CQ K1ABC CQ"  },
    { "1000000000000001111011000000001001", "599 123"      },
    { "1000000000000110111010000000011001", "599 CA"       },
    { "0000001011011110010000110100101001", "599 05 NWT"   },
    { "0010000010110001011000000000101001", "1D EMA"       },
    { "1000000100111001000010110100111001", "599 156 1749" },
    { "0000000000000000000000000001001001", "AGN?"         },
    { "0001001010000110011000000001001001", "FN42"         },
    { "0100010011100101010101010110001101", "HELLO"        }
};
#define ORO_N ((int)(sizeof k_oro / sizeof k_oro[0]))

int main(void)
{
    uint8_t a[JTTY_CARGA_BITS], b[JTTY_CARGA_BITS];
    float amp;

    printf("\n  JTTY: DE LOS ATOMOS A LOS MENSAJES\n\n");
    printf("    una trama son %d simbolos = %ld muestras = %d filas\n",
           (int)JTTY_TRAMA_SIM, TRAMA_MU, (int)JTTY_RX_FILAS_PASO);

    /* Sin ruido: lo que se mide aqui es el encadenado, no la sensibilidad. */
    amp = 0.35f;

    /* ---- 1. la rejilla de 118 filas, la cuenta sola ---- */
    printf("\n  1. la rejilla: %d filas de una trama a la siguiente\n",
           (int)JTTY_RX_FILAS_PASO);
    {
        uint8_t s = 99U;

        exige(jtty_modo_encadena(1000U, 1000U + JTTY_RX_FILAS_PASO, &s)
              && s == 0U, "trama pegada (118 filas): encadena, 0 saltos");
        exige(jtty_modo_encadena(1000U, 1000U + 2U * JTTY_RX_FILAS_PASO, &s)
              && s == 1U, "una trama perdida (236): encadena, 1 salto");
        exige(jtty_modo_encadena(1000U,
                  1000U + JTTY_SALTOS_MAX * JTTY_RX_FILAS_PASO, &s)
              && s == (uint8_t)(JTTY_SALTOS_MAX - 1U),
              "el ultimo salto que se admite: encadena");
        exige(!jtty_modo_encadena(1000U,
                  1000U + (JTTY_SALTOS_MAX + 1U) * JTTY_RX_FILAS_PASO, NULL),
              "uno mas: NO, el plazo ya lo habra cerrado");
        exige(jtty_modo_encadena(1000U, 1000U + JTTY_RX_FILAS_PASO + 2U, NULL),
              "118 + 2 filas: encadena, es el margen del sincronismo");
        exige(!jtty_modo_encadena(1000U, 1000U + JTTY_RX_FILAS_PASO + 5U, NULL),
              "118 + 5 filas: NO, esa fase es de otra estacion");
        exige(!jtty_modo_encadena(1000U, 1000U + 59U, NULL),
              "media trama (59 filas): NO, otra estacion");
        exige(!jtty_modo_encadena(1000U, 1000U, NULL),
              "la misma fila: NO");
        exige(!jtty_modo_encadena(1000U, 900U, NULL),
              "hacia atras: NO");
    }
    /*
     * Y CUANTAS FASES SE CUELAN, que es el numero que dice si la regla
     * sirve: de todas las separaciones posibles dentro del plazo, cuantas
     * pasan por buenas.
     */
    {
        uint32_t d, cuelan = 0U, total = 0U;

        /*
         * El barrido llega hasta donde llega el plazo de cerrar, que es lo
         * que de verdad limita cuantas fases se pueden probar. Se deduce
         * del alcance en vez de ponerlo a mano, que es como se queda uno
         * de los dos numeros atras cuando se cambia el otro.
         */
        for (d = 1U; d <= (JTTY_SALTOS_MAX + 1U) * JTTY_RX_FILAS_PASO; d++) {
            total++;
            if (jtty_modo_encadena(1000U, 1000U + d, NULL)) { cuelan++; }
        }
        printf("      de %u separaciones dentro del plazo, %u encadenan"
               " (%.1f%%)\n", total, cuelan,
               100.0 * (double)cuelan / (double)total);
        /*
         * En PROPORCION, que es lo que dice si la regla discrimina. Y la
         * proporcion no empeora al alargar el alcance: crecen las dos, las
         * fases que valen y las que hay. Subir de 2 saltos a 4 paso del
         * 3,8% al 3,3%.
         */
        exige(cuelan * 20U <= total, "menos del 5% de las fases se cuelan");
    }

    /* ---- 2. un mensaje de tres tramas seguidas: UN renglon ---- */
    printf("\n  2. tres tramas seguidas de la misma estacion\n");
    memset(g_audio, 0, sizeof g_audio);
    sin_fin(k_oro[0].bits, a);
    bits_de(k_oro[0].bits, b);      /* la tercera lleva el bit de fin */
    {
        long i;
        for (i = 0; i < 3; i++) {
            mete_trama((i == 2) ? b : a, 0.0f, 12000L + i * TRAMA_MU,
                       amp, g_audio, AUDIO_N);
        }
    }
    exige(jtty_modo_start() == 1U, "el modo arranca");
    corre(12000L + 4 * TRAMA_MU);
    pinta();
    exige(jtty_modo_lineas() == 1U, "sale UN renglon, no tres");
    exige(cuantos_con("...") == 0U, "y sin puntos suspensivos");
    jtty_modo_stop();

    /* ---- 3. DOS ESTACIONES EN LA MISMA FRECUENCIA ---- */
    /*
     * Esta es la prueba de la foto, y hay que montarla como pasa de
     * verdad. Dos estaciones de un modo de teclado en la misma frecuencia
     * casi nunca pisan la misma trama: se ALTERNAN. Lo que las junta no es
     * que suenen a la vez, es que la version anterior solo mirava la
     * frecuencia y la hora a la que el bucle principal se enteraba.
     *
     *   A manda una trama SIN bit de fin -su mensaje se queda abierto-;
     *   B manda la suya vez y media despues, 12 Hz mas alla, dentro del
     *   mismo cubo de +-20 Hz.
     *
     * Antes: la hora entre las dos daba 2,8 s, pasaba de JTTY_HUECO_MS y
     * se pegaban con puntos en medio, que es literalmente el renglon
     * "RX1AG TU CQ PD5LDB CQ" de la foto.
     *
     * Ahora: 177 filas no esta en la rejilla de 118, asi que B abre su
     * propio mensaje.
     *
     * Y VEZ Y MEDIA A PROPOSITO, no una. Si B mandara EXACTAMENTE una
     * trama despues, su fase caeria clavada en la rejilla de A y se
     * pegarian - y eso no es un fallo que se pueda arreglar: son dos
     * tramas separadas 59 simbolos en la misma frecuencia, que es
     * exactamente lo que es un mensaje de dos tramas. El limite es del
     * metodo, no del codigo, y las estaciones de verdad no se alinean:
     * cada una empieza cuando su operador le da a la tecla.
     */
    printf("\n  3. dos estaciones alternandose en la misma frecuencia\n");
    memset(g_audio, 0, sizeof g_audio);
    sin_fin(k_oro[0].bits, a);          /* "CQ K1ABC CQ", mensaje abierto */
    bits_de(k_oro[6].bits, b);          /* "AGN?", de otra estacion */
    mete_trama(a,  0.0f, 12000L,                       amp, g_audio, AUDIO_N);
    mete_trama(b, 12.0f, 12000L + 3 * TRAMA_MU / 2,    amp, g_audio, AUDIO_N);
    exige(jtty_modo_start() == 1U, "el modo arranca");
    corre(12000L + 3 * TRAMA_MU);
    pinta();
    exige(jtty_modo_lineas() == 2U, "salen DOS renglones, no uno pegado");
    {
        uint8_t i, n = jtty_modo_lineas(), mezcla = 0U;
        const char *ta = k_oro[0].esp, *tb = k_oro[6].esp;

        for (i = 0U; i < n; i++) {
            const char *l = jtty_modo_linea(i);

            if (strstr(l, ta) != NULL && strstr(l, tb) != NULL) { mezcla = 1U; }
        }
        exige(!mezcla, "y ningun renglon lleva los dos mensajes");
        exige(cuantos_con(ta) == 1, "el de la estacion A, en el suyo");
        exige(cuantos_con(tb) == 1, "el de la estacion B, en el suyo");
    }
    jtty_modo_stop();

    /* ---- 4. una trama perdida en medio: puntos suspensivos ---- */
    printf("\n  4. la trama del medio no llega\n");
    memset(g_audio, 0, sizeof g_audio);
    sin_fin(k_oro[0].bits, a);
    bits_de(k_oro[0].bits, b);
    mete_trama(a, 0.0f, 12000L,                amp, g_audio, AUDIO_N);
    /* La del medio no se manda: la siguiente llega a 236 filas. */
    mete_trama(b, 0.0f, 12000L + 2 * TRAMA_MU, amp, g_audio, AUDIO_N);
    exige(jtty_modo_start() == 1U, "el modo arranca");
    corre(12000L + 4 * TRAMA_MU);
    pinta();
    exige(jtty_modo_lineas() == 1U, "sigue siendo UN mensaje");
    exige(cuantos_con("...") == 1U, "con los puntos del hueco EN MEDIO");
    jtty_modo_stop();

    /* ---- 4b. TRES tramas perdidas seguidas ---- */
    /*
     *
     * El alcance estaba en dos saltos porque me parecio bastante. Medido
     * con desvanecimiento a -11 dB, el 10% de los enlaces necesita tres o
     * mas: ver JTTY_SALTOS_MAX. Esta prueba es ese caso.
     */
    printf("\n  4b. se pierden TRES tramas seguidas y sigue siendo un mensaje\n");
    memset(g_audio, 0, sizeof g_audio);
    sin_fin(k_oro[0].bits, a);
    bits_de(k_oro[0].bits, b);
    mete_trama(a, 0.0f, 12000L,                amp, g_audio, AUDIO_N);
    /* y las tramas 2, 3 y 4 no llegan: la siguiente va a 4 * 118 filas */
    mete_trama(b, 0.0f, 12000L + 4 * TRAMA_MU, amp, g_audio, AUDIO_N);
    exige(jtty_modo_start() == 1U, "el modo arranca");
    corre(12000L + 6 * TRAMA_MU);
    pinta();
    exige(jtty_modo_lineas() == 1U, "UN mensaje, no dos trozos");
    exige(cuantos_con("...") == 1, "con los puntos del hueco en medio");
    jtty_modo_stop();

    /* ---- 5. mas estaciones que cubos ---- */
    /*
     * La tercera manera de juntar mensajes: con los cubos llenos, el mas
     * viejo se reutilizaba SIN cerrarlo y el atomo nuevo se pegaba a su
     * mensaje. Aqui se mandan JTTY_MODO_CUBOS+2 estaciones, cada una en su
     * frecuencia, y ninguna puede acabar dentro del mensaje de otra.
     */
    printf("\n  5. %d estaciones distintas y %u cubos\n",
           ORO_N, (unsigned)JTTY_MODO_CUBOS);
    /*
     * Nueve textos DISTINTOS -uno por vector normativo- para que "dos
     * estaciones en el mismo renglon" se pueda medir contando cuantos
     * textos aparecen en el. Con diez estaciones y nueve textos, dos
     * repetian y el propio banco se contaba la mezcla a si mismo: el
     * primer intento dio "MAL" por eso y no por el codigo.
     */
    memset(g_audio, 0, sizeof g_audio);
    {
        int e;

        for (e = 0; e < ORO_N; e++) {
            /* Cada una en su sitio, separadas mas de 20 Hz. */
            float desvio = -320.0f + 80.0f * (float)e;

            sin_fin(k_oro[e].bits, a);
            bits_de(k_oro[e].bits, b);
            /* Dos tramas cada una, con arranques distintos. */
            mete_trama(a, desvio, 12000L + (long)e * 700L, amp,
                       g_audio, AUDIO_N);
            mete_trama(b, desvio, 12000L + (long)e * 700L + TRAMA_MU, amp,
                       g_audio, AUDIO_N);
        }
    }
    exige(jtty_modo_start() == 1U, "el modo arranca");
    corre(12000L + 3 * TRAMA_MU);
    pinta();
    {
        uint8_t i, n = jtty_modo_lineas(), mezcla = 0U;

        /* Ningun renglon puede llevar los textos de dos estaciones. */
        for (i = 0U; i < n; i++) {
            const char *l = jtty_modo_linea(i);
            int e, c = 0;

            for (e = 0; e < ORO_N; e++) {
                if (strstr(l, k_oro[e].esp) != NULL) { c++; }
            }
            if (c > 1) { mezcla = 1U; }
        }
        printf("        %u renglones de %d estaciones, %u candidatos"
               " tirados por cola llena\n",
               (unsigned)n, ORO_N, (unsigned)jtty_rx_perdidos());
        exige(!mezcla, "ningun renglon mezcla dos estaciones");
        /*
         * Y NO SE TIRA NINGUNO. Con JTTY_CAND_N en 6 se tiraban, y una
         * estacion entera se quedaba fuera: el fallo no era del ruido ni
         * del Viterbi, era la cola. Ver jtty_rx.c.
         */
        exige(jtty_rx_perdidos() == 0UL,
              "y ningun candidato se tira por falta de sitio en la cola");
        exige(n >= (uint8_t)(ORO_N - 1),
              "salen casi todas: mas estaciones que cubos cuesta una");
    }
    jtty_modo_stop();

    /* ---- 6. mas mensajes ABIERTOS que cubos ---- */
    /*
     * La prueba 5 no llega a enseñar el tercer fallo: sus estaciones
     * cierran con el bit de fin, asi que los cubos se liberan solos y
     * nunca hacen falta los nueve a la vez.
     *
     * Aqui NINGUNA manda el bit de fin -nueve mensajes abiertos, ocho
     * cubos-, que es cuando el viejo `cubo_de()` devolvia el mas viejo sin
     * cerrarlo y el atomo nuevo se pegaba al mensaje de otra estacion.
     */
    printf("\n  6. %d mensajes abiertos a la vez y %u cubos\n",
           ORO_N, (unsigned)JTTY_MODO_CUBOS);
    memset(g_audio, 0, sizeof g_audio);
    {
        int e;

        for (e = 0; e < ORO_N; e++) {
            /*
             * 105 Hz de separacion y no 80: la señal ocupa los tres saltos
             * de tono -94 Hz- mas las faldas, asi que a 80 Hz las vecinas
             * se pisan y lo que se mediria seria eso y no los cubos. Con
             * 80 salian 6 de 9 por interferencia mutua.
             */
            sin_fin(k_oro[e].bits, a);          /* sin bit de fin: abierto */
            mete_trama(a, -420.0f + 105.0f * (float)e,
                       12000L + (long)e * 900L, amp, g_audio, AUDIO_N);
        }
    }
    exige(jtty_modo_start() == 1U, "el modo arranca");
    corre(12000L + 2 * TRAMA_MU);
    pinta();
    {
        uint8_t i, n = jtty_modo_lineas(), mezcla = 0U;

        for (i = 0U; i < n; i++) {
            const char *l = jtty_modo_linea(i);
            int e, c = 0;

            for (e = 0; e < ORO_N; e++) {
                if (strstr(l, k_oro[e].esp) != NULL) { c++; }
            }
            if (c > 1) { mezcla = 1U; }
        }
        printf("        %u renglones de %d mensajes abiertos\n",
               (unsigned)n, ORO_N);
        exige(!mezcla,
              "ningun renglon pega el mensaje de dos estaciones distintas");
        exige(n >= (uint8_t)JTTY_MODO_CUBOS,
              "y salen al menos tantos como cubos hay");
    }
    jtty_modo_stop();

    /* ---- 7. EL ORDEN DE LOS RENGLONES ---- */
    /*
     *
     * El fallo no se ve como un fallo -salen puntos y estan bien puestos-,
     * solo que el resalte no significa nada. Y no habia nada que dijera
     * cual de los dos ordenes usa este modulo, asi que se escribe aqui:
     * `jtty_modo_linea(0)` es EL MAS VIEJO, y quien lea la lista para otra
     * cosa -un mapa, un fichero, lo que sea- tiene esto para saberlo sin
     * ir a leer `apunta()`.
     */
    printf("\n  7. jtty_modo_linea(0) es el renglon MAS VIEJO\n");
    memset(g_audio, 0, sizeof g_audio);
    /* Dos mensajes de una trama, en frecuencias distintas y uno despues del
     * otro: el primero que cierra tiene que salir en el renglon 0. */
    bits_de(k_oro[0].bits, a);          /* "CQ K1ABC CQ", el primero */
    bits_de(k_oro[6].bits, b);          /* "AGN?", el segundo */
    mete_trama(a, -150.0f, 12000L,                amp, g_audio, AUDIO_N);
    mete_trama(b,  150.0f, 12000L + 2 * TRAMA_MU, amp, g_audio, AUDIO_N);
    exige(jtty_modo_start() == 1U, "el modo arranca");
    corre(12000L + 4 * TRAMA_MU);
    pinta();
    if (jtty_modo_lineas() >= 2U) {
        const char *l0 = jtty_modo_linea(0U);
        const char *lu = jtty_modo_linea((uint8_t)(jtty_modo_lineas() - 1U));

        exige(strstr(l0, k_oro[0].esp) != NULL,
              "el renglon 0 lleva el mensaje que llego ANTES");
        exige(strstr(lu, k_oro[6].esp) != NULL,
              "y el ultimo el que llego DESPUES");
    } else {
        exige(0, "hacen falta dos renglones para comprobar el orden");
    }
    jtty_modo_stop();

    printf("\n  %s: %d fallo(s)\n\n", fallos ? "MAL" : "TODO EN VERDE", fallos);
    return fallos ? 1 : 0;
}
