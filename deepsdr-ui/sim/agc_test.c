/*
 * EL AGC, CONTRA UN AGC DE LIBRO. Y ES EL PRIMER BANCO QUE TOCA LA CADENA
 * DE AUDIO.
 *
 * Pedido por el usuario, 08/10/2026.
 *
 * LO QUE SALIO DE ESA REVISION, Y POR QUE HAY QUE MEDIRLO
 * ------------------------------------------------------
 * Habia dos bucles de AGC copiados a mano dentro de demod_am.c -el de
 * AM/BLU/FM estrecha y el de FM ancha-. El 05/10/2026 se optimizo uno de
 * los dos y el otro se quedo con una division de coma flotante por
 * muestra: 192.000 por segundo, en el camino de interrupcion mas apretado
 * que hay. Nadie lo vio en cuatro dias porque NO HABIA NADA QUE MIRARA LOS
 * DOS A LA VEZ.
 *
 * Y la razon de fondo es peor que la division: de toda la cadena de audio
 * -detector, bloqueador de continua, filtro, silenciador, AGC, salida- el
 * simulador no compilaba NI UNA LINEA. Todos los decodificadores tienen su
 * banco; lo que suena por el altavoz no tenia ninguno. Este es el primero.
 *
 * QUE COMPRUEBA
 * -------------
 * Compila agc.c DE VERDAD -el fichero del firmware, no una copia- y lo
 * pasa, muestra a muestra, contra un AGC DE LIBRO escrito aqui abajo: el
 * `AGC_TARGET / pico` por muestra, sin inversos ni trucos, que es
 * literalmente lo que hacia el camino de FM ancha hasta hoy.
 *
 * Son dos cosas distintas y por eso vale: agc.c arrastra un INVERSO
 * multiplicando y solo divide cuando el pico salta, y se resincroniza en
 * cada bloque. Eso es rapido pero no es evidente que de lo mismo. Esto lo
 * decide con un numero: la mayor diferencia vista, en LSB del int16.
 *
 * El tope es UN LSB, y no es un numero elegido por bonito: el arrastre del
 * inverso puede separarse como mucho unas 512 multiplicaciones -el bloque
 * mas largo, el de FM ancha-, o sea una parte en cien mil; sobre los
 * 18.000 de objetivo eso es medio LSB. Si algun dia sale mas, es que el
 * arrastre ha dejado de resincronizarse, y eso NO suena a "un poco peor":
 * suena a que la ganancia deriva sola.
 *
 * SE HA VISTO FALLAR DE DOS MANERAS, que es lo unico que convierte un
 * fichero en un banco. Las dos son roturas de la aritmetica del inverso,
 * que es justo lo que este banco existe para vigilar:
 *
 *   - quitando de agc.c la linea que devuelve el inverso a su suelo
 *     cuando el pico cae por debajo de pico_min: 8 casos en rojo, el peor
 *     a 541 LSB;
 *   - quitando la division de verdad del ataque -o sea dejando que el
 *     inverso siga arrastrandose cuando el pico SALTA-: 33 casos en rojo,
 *     el peor a 28.414 LSB, que es casi fondo de escala.
 *
 * Ninguna de las dos se oye como "un poco peor": las dos son la ganancia
 * yendose sola. Y ninguna de las dos la habria visto nadie leyendo el
 * codigo, porque las dos COMPILAN y las dos suenan hasta que no suenan.
 */
#include "agc.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define N_MAX 512U

static int fallos = 0;

/* ---------------------------------------------------------------- *
 * EL AGC DE LIBRO. Deliberadamente tonto: una division por muestra,
 * sin estado entre muestras que no sea el pico. Es el que estaba
 * escrito a mano en el camino de FM ancha.
 * ---------------------------------------------------------------- */
static float agc_libro(const float *entra, short *sale, unsigned n,
                       float pico, float relajacion, float pico_min,
                       float ganancia)
{
    unsigned i;

    for (i = 0U; i < n; i++) {
        float mag = (entra[i] < 0.0f) ? -entra[i] : entra[i];
        float pk, g, y;
        int out;

        pico *= relajacion;
        if (mag > pico) { pico = mag; }
        pk = (pico < pico_min) ? pico_min : pico;
        g  = AGC_TARGET / pk;
        if (g > AGC_GAIN_MAX) { g = AGC_GAIN_MAX; }
        y = entra[i] * g * ganancia;

        out = (int)y;
        if (out > 32767)  { out = 32767; }
        if (out < -32768) { out = -32768; }
        sale[2U * i]      = (short)out;
        sale[2U * i + 1U] = (short)out;
    }
    return pico;
}

/* ---------------------------------------------------------------- *
 * Las señales. Cada una es un caso que el AGC tiene que atravesar
 * sin separarse del de libro.
 * ---------------------------------------------------------------- */
static void sig_silencio(float *x, unsigned n, unsigned blo)
{
    unsigned i;
    (void)blo;
    for (i = 0U; i < n; i++) { x[i] = 0.0f; }
}

static void sig_ruido_bajo(float *x, unsigned n, unsigned blo)
{
    unsigned i;
    (void)blo;
    /* Por debajo del suelo de AM (40) a proposito: es el caso que hacia
     * una division por muestra hasta hoy. */
    for (i = 0U; i < n; i++) { x[i] = 3.0f * (float)sin(0.031 * (double)i); }
}

static void sig_tono(float *x, unsigned n, unsigned blo)
{
    unsigned i;
    (void)blo;
    for (i = 0U; i < n; i++) { x[i] = 9000.0f * (float)sin(0.11 * (double)i); }
}

static void sig_escalon(float *x, unsigned n, unsigned blo)
{
    unsigned i;
    /* Silencio y de golpe fondo de escala a mitad del segundo bloque:
     * el ataque instantaneo y la division de verdad. */
    for (i = 0U; i < n; i++) {
        x[i] = (blo >= 1U && i > (n / 2U))
                 ? 30000.0f * (float)sin(0.21 * (double)i) : 0.0f;
    }
}

static void sig_golpe(float *x, unsigned n, unsigned blo)
{
    unsigned i;
    /* Un golpe muy fuerte y despues nada: la relajacion mandando sola
     * durante miles de muestras, que es donde el inverso arrastrado
     * tiene mas sitio para separarse. */
    for (i = 0U; i < n; i++) {
        x[i] = (blo == 0U && i < 8U) ? 32000.0f : 0.0f;
    }
}

static void sig_desvanece(float *x, unsigned n, unsigned blo)
{
    unsigned i;
    double a = 20000.0 * pow(0.7, (double)blo);
    for (i = 0U; i < n; i++) { x[i] = (float)(a * sin(0.07 * (double)i)); }
}

static void sig_sube(float *x, unsigned n, unsigned blo)
{
    unsigned i;
    double a = 10.0 * pow(1.9, (double)blo);
    if (a > 31000.0) { a = 31000.0; }
    for (i = 0U; i < n; i++) { x[i] = (float)(a * sin(0.013 * (double)i)); }
}

static void sig_pulsos(float *x, unsigned n, unsigned blo)
{
    unsigned i;
    (void)blo;
    /* Silabico: trozos fuertes y trozos callados dentro del mismo
     * bloque, cruzando el suelo del pico en los dos sentidos. */
    for (i = 0U; i < n; i++) {
        x[i] = ((i / 17U) % 3U == 0U) ? 15000.0f * (float)sin(0.3 * (double)i)
                                       : 0.5f;
    }
}

typedef void (*gen_t)(float *, unsigned, unsigned);

static const struct { const char *nombre; gen_t gen; } k_sig[] = {
    { "silencio",            sig_silencio   },
    { "ruido bajo el suelo", sig_ruido_bajo },
    { "tono constante",      sig_tono       },
    { "escalon a fondo",     sig_escalon    },
    { "golpe y silencio",    sig_golpe      },
    { "se desvanece",        sig_desvanece  },
    { "sube de la nada",     sig_sube       },
    { "silabico",            sig_pulsos     },
};

/* Los dos caminos de verdad: el de AM/BLU/FM estrecha y el de FM ancha,
 * con sus tamaños de bloque y sus suelos de pico. Las relajaciones son
 * las tres de demod_am.c, la mas lenta y la mas rapida de cada tasa. */
static const struct {
    const char *nombre;
    unsigned    n;
    float       pico_min;
    float       pico0;
} k_camino[] = {
    { "AM/BLU/FM estrecha",  256U,   40.0f, 18000.0f },
    { "FM ancha",            512U, 4000.0f,  4000.0f },
};

static const struct { const char *nombre; float rel; } k_rel[] = {
    { "lenta",  0.99998512f },
    { "media",  0.99994048f },
    { "rapida", 0.99982640f },
};

#define BLOQUES 40U

static void caso(unsigned c, unsigned r, unsigned sg, int *peor_global)
{
    float  x[N_MAX];
    short  a[2U * N_MAX];
    short  b[2U * N_MAX];
    float  pa = k_camino[c].pico0;
    float  pb = k_camino[c].pico0;
    unsigned n = k_camino[c].n;
    unsigned blo, i;
    int peor = 0;

    for (blo = 0U; blo < BLOQUES; blo++) {
        k_sig[sg].gen(x, n, blo);

        pa = agc_bloque(x, a, n, pa, k_rel[r].rel, k_camino[c].pico_min, 1.0f);
        pb = agc_libro (x, b, n, pb, k_rel[r].rel, k_camino[c].pico_min, 1.0f);

        for (i = 0U; i < 2U * n; i++) {
            int d = (int)a[i] - (int)b[i];
            if (d < 0) { d = -d; }
            if (d > peor) { peor = d; }
        }
        /* Y los dos canales tienen que llevar lo mismo: la salida es
         * mono duplicada, y una radio con un canal mudo es una averia
         * que se oye antes que cualquier numero de estos. */
        for (i = 0U; i < n; i++) {
            if (a[2U * i] != a[2U * i + 1U]) {
                printf("    %-20s %-6s %-20s  L y R NO coinciden   MAL\n",
                       k_camino[c].nombre, k_rel[r].nombre, k_sig[sg].nombre);
                fallos++;
                return;
            }
        }
    }

    if (peor > *peor_global) { *peor_global = peor; }
    printf("    %-20s %-6s %-20s  peor %d LSB   %s\n",
           k_camino[c].nombre, k_rel[r].nombre, k_sig[sg].nombre, peor,
           (peor <= 1) ? "ok" : "SE SEPARA");
    if (peor > 1) { fallos++; }
}

/* El perfil MANUAL: ni lazo ni pico, pero el recorte y el duplicado
 * tienen que ser EXACTAMENTE los mismos. Es la mitad del "if" que nadie
 * mira, y la que suena cuando el usuario pone el AGC a mano. */
static void manual(void)
{
    float x[8];
    short s[16];
    unsigned i;
    int mal = 0;

    x[0] =      0.0f; x[1] =     1.4f; x[2] =    -1.4f; x[3] =  32766.6f;
    x[4] =  40000.0f; x[5] = -40000.0f; x[6] = -32768.4f; x[7] = 32767.0f;

    agc_manual(x, s, 8U, 1.0f);

    for (i = 0U; i < 8U; i++) {
        long esperado = (long)x[i];           /* trunca hacia cero */
        if (esperado >  32767L) { esperado =  32767L; }
        if (esperado < -32768L) { esperado = -32768L; }
        if ((long)s[2U * i] != esperado || s[2U * i] != s[2U * i + 1U]) {
            printf("    manual: %10.1f -> %6d (esperado %ld)   MAL\n",
                   (double)x[i], (int)s[2U * i], esperado);
            mal = 1;
        }
    }
    printf("    recorte y duplicado del perfil MANUAL   %s\n",
           mal ? "MAL" : "ok");
    if (mal) { fallos++; }
}

int main(void)
{
    unsigned c, r, sg;
    int peor_global = 0;

    printf("\nEL AGC CONTRA UN AGC DE LIBRO\n");
    printf("  %u bloques por caso, los dos caminos, las tres relajaciones.\n",
           (unsigned)BLOQUES);
    printf("  el tope es 1 LSB del int16 - ver la cabecera de este fichero.\n\n");

    for (c = 0U; c < sizeof k_camino / sizeof k_camino[0]; c++) {
        for (r = 0U; r < sizeof k_rel / sizeof k_rel[0]; r++) {
            for (sg = 0U; sg < sizeof k_sig / sizeof k_sig[0]; sg++) {
                caso(c, r, sg, &peor_global);
            }
        }
        printf("\n");
    }

    manual();

    printf("\n  la mayor diferencia de todo el barrido: %d LSB\n", peor_global);
    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
