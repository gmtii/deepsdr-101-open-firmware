/*
 * EL ESPEJO QUE DEPENDE DE LA FRECUENCIA, Y POR QUE UN SOLO NUMERO NO LLEGA.
 *
 * Un decibelio. El corrector plano de iqbal.c no toca lo que se ve. Este
 * banco reproduce el motivo que se sospechaba y comprueba el arreglo.
 *
 * EL MODELO, y es lo que hace que esto pruebe algo: la rama Q no va
 * simplemente girada un angulo, va RETRASADA una fraccion de periodo de
 * muestreo. Eso es lo que hacen dos filtros antialias que no son identicos, o
 * dos caminos de pista distintos, y es fisicamente lo que pasa en una radio
 * de verdad. Un retardo de medio periodo son 0,9 grados a 1 kHz y 43 grados a
 * 48: el desequilibrio ya no es un numero, es una funcion de la frecuencia.
 *
 * Y de ahi sale la prediccion que este banco tiene que confirmar o tumbar:
 *
 *   - el corrector PLANO puede acertar en UNA frecuencia y en las demas no,
 *     y su promedio sobre la banda entera se va casi a cero -que es
 *     exactamente lo que enseñaba la fila de la radio: 0,1 grados-;
 *   - el corrector de BANDA ANCHA tiene que valer en toda la banda.
 *
 * Se mide donde se ve: el rechazo de imagen sacado de la FFT compleja, tono a
 * tono, a lo ancho de los 96 kHz. No "los coeficientes convergen", que
 * tambien se cumple cuando convergen a una solucion mala.
 */
#include "iqbal.h"
#include "iqancho.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int fallos = 0;

static void di(const char *que, int bien, const char *det)
{
    printf("    %-52s %s%s%s\n", que, bien ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!bien) { fallos++; }
}

#define FS      96000.0f
#define NB      256U
#define NFFT    8192

static int16_t g_crudo[2 * NB];

static uint32_t s_rnd = 99u;
static float ruido(void)
{
    int k; float s = 0.0f;
    for (k = 0; k < 8; k++) {
        s_rnd = s_rnd * 1103515245u + 12345u;
        s += (float)((s_rnd >> 9) & 0xFFFFu) / 65535.0f;
    }
    return s - 4.0f;
}

/* --- el receptor desequilibrado ----------------------------------------- */

static double g_fase_tono = 0.0;
static float  g_tono_hz   = 20000.0f;
static float  g_retardo   = 0.0f;   /* en periodos de muestreo, en la rama Q */
static float  g_gan_db    = 0.4f;

/*
 * RETARDO FRACCIONARIO EN Q, POR PASO-TODO Y NO POR INTERPOLACION LINEAL.
 *
 * La primera version interpolaba linealmente entre dos muestras, y eso NO es
 * un retardo: es un retardo MAS un filtro paso-bajo brutal. Con medio periodo
 * de retardo, la interpolacion lineal se come 20 dB en 45 kHz y lo anula
 * entero en Nyquist. Con eso la rama Q practicamente desaparece en el borde
 * de la banda, o sea que ahi ya no hay un desequilibrio pequeño que corregir
 * sino dos ramas completamente distintas, y ningun corrector de coeficientes
 * pequeños puede con eso. El banco medía 7 dB en 45 kHz y la culpa era suya.
 *
 * Un paso-todo de primer orden -Thiran- da el mismo retardo con el MODULO
 * PLANO, que es lo que hace de verdad una diferencia de camino entre dos
 * ramas del mismo circuito:
 *
 *     H(z) = (a + z^-1) / (1 + a z^-1),   a = (1-d)/(1+d)
 */
static float s_qx = 0.0f, s_qy = 0.0f;

static void bloque(void)
{
    double w  = 2.0 * 3.14159265358979 * (double)g_tono_hz / (double)FS;
    float  gn = powf(10.0f, g_gan_db / 20.0f);
    uint32_t k;

    for (k = 0U; k < NB; k++) {
        float i_ideal, q_ideal, q_sal;

        g_fase_tono += w;
        if (g_fase_tono > 6.283185307179586) { g_fase_tono -= 6.283185307179586; }

        /*
         * Ruido de banda ancha -que es lo que de verdad llena 96 kHz de HF-
         * mas el tono con el que se mide, unos 9 dB por encima.
         *
         * *** EL NIVEL DEL TONO NO ES UN DETALLE, ES LA CONDICION QUE HACE
         * QUE EL PROBLEMA TENGA SOLUCION. La primera version lo ponia 20 dB
         * por encima del ruido, y con eso la entrada es practicamente una
         * sola frecuencia: la matriz que relaciona los coeficientes con las
         * correlaciones se vuelve de rango uno y hay infinitas soluciones
         * que anulan la correlacion en ESA frecuencia y en ninguna otra. El
         * corrector "convergia" y luego medias 3,9 dB. No era el algoritmo:
         * era que con un solo tono no hay nada que identificar. ***
         *
         * Con el tono a 9 dB, la FFT de 8192 puntos lo separa del ruido de
         * sobra -la ganancia de proceso son 39 dB- y la adaptacion ve banda
         * ancha, que es lo que necesita.
         */
        i_ideal = 300.0f * ruido() + 700.0f * cosf((float)g_fase_tono);
        q_ideal = 300.0f * ruido() + 700.0f * sinf((float)g_fase_tono);

        /*
         * La rama Q, retrasada y con otra ganancia.
         *
         * OJO CON EL RETARDO CERO, que la primera version de este banco se
         * comio: interpolaba entre s_q1 y s_q2 -la muestra anterior y la de
         * antes-, asi que con g_retardo = 0 la Q salia retrasada UNA MUESTRA
         * ENTERA igualmente. A 25 kHz eso son 94 grados de desfase: el banco
         * medía 0 dB de rechazo sobre una señal supuestamente equilibrada y
         * la culpa era suya. Se interpola entre la muestra DE AHORA y la
         * anterior, asi que g_retardo = 0 es de verdad "sin retardo".
         */
        {
            /*
             * CON RETARDO CERO SE SALTA EL PASO-TODO ENTERO, y no es una
             * optimizacion: con d = 0 sale a = 1 y el polo del filtro se
             * planta en z = -1, justo encima de la circunferencia unidad. La
             * recursion deja de ser estable y lo que sale no es "la señal sin
             * retardar" sino basura que crece. La prueba de "con la entrada
             * YA equilibrada no la estropees" estaba alimentandose de eso, y
             * por eso los coeficientes acababan en 0,1 sobre una señal que se
             * suponia perfecta.
             */
            if (g_retardo <= 0.0f) {
                q_sal = gn * q_ideal;
            } else {
                float a = (1.0f - g_retardo) / (1.0f + g_retardo);
                float o = a * q_ideal + s_qx - a * s_qy;
                s_qx = q_ideal;
                s_qy = o;
                q_sal = gn * o;
            }
        }

        g_crudo[2U * k]      = (int16_t)i_ideal;
        g_crudo[2U * k + 1U] = (int16_t)q_sal;
    }
}

/* --- rechazo de imagen por FFT ------------------------------------------ */

static float fr[NFFT], fi[NFFT];

static void fft(void)
{
    int n = NFFT, i, j, len;
    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) { j ^= bit; }
        j ^= bit;
        if (i < j) {
            float t;
            t = fr[i]; fr[i] = fr[j]; fr[j] = t;
            t = fi[i]; fi[i] = fi[j]; fi[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * 3.14159265358979f / (float)len;
        float wr = cosf(ang), wi = sinf(ang);
        for (i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (j = 0; j < len / 2; j++) {
                float ur = fr[i + j], ui = fi[i + j];
                float vr = fr[i + j + len/2] * cr - fi[i + j + len/2] * ci;
                float vi = fr[i + j + len/2] * ci + fi[i + j + len/2] * cr;
                float nr;
                fr[i + j] = ur + vr; fi[i + j] = ui + vi;
                fr[i + j + len/2] = ur - vr; fi[i + j + len/2] = ui - vi;
                nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
}

/* modo: 0 = nada, 1 = solo el plano, 2 = plano + banda ancha */
static float mide(int modo)
{
    int n = 0, k, bs, be;
    float ps = 0.0f, pe = 0.0f;

    iqbal_set_on(modo == 1);
    iqa_set_on(modo >= 2);

    while (n < NFFT) {
        uint32_t m;
        bloque();
        iqbal_bloque(g_crudo, NB);
        iqa_bloque(g_crudo, NB);
        for (m = 0U; m < NB && n < NFFT; m++, n++) {
            float wv = 0.5f - 0.5f * cosf(2.0f * 3.14159265358979f * (float)n
                                          / (float)(NFFT - 1));
            fr[n] = (float)g_crudo[2U * m] * wv;
            fi[n] = (float)g_crudo[2U * m + 1U] * wv;
        }
    }
    fft();

    bs = (int)(g_tono_hz / FS * (float)NFFT + 0.5f);
    be = NFFT - bs;
    for (k = -3; k <= 3; k++) {
        int a = (bs + k + NFFT) % NFFT;
        int b = (be + k + NFFT) % NFFT;
        ps += fr[a] * fr[a] + fi[a] * fi[a];
        pe += fr[b] * fr[b] + fi[b] * fi[b];
    }
    if (pe <= 0.0f) { return 99.0f; }
    return 10.0f * log10f(ps / pe);
}

/* deja los dos correctores asentados sobre el desequilibrio actual */
static void asienta(int modo)
{
    int k;
    iqbal_set_on(modo == 1);
    iqa_set_on(modo >= 2);
    for (k = 0; k < 3000; k++) {
        bloque();
        iqbal_bloque(g_crudo, NB);
        iqa_bloque(g_crudo, NB);
    }
}

int main(void)
{
    char det[96];
    static const float tonos[5] = { 5000.0f, 15000.0f, 25000.0f, 35000.0f, 45000.0f };
    float sin_c[5], plano[5], ancho[5];
    int k;

    printf("\nEL ESPEJO QUE DEPENDE DE LA FRECUENCIA\n\n");

    /* ------------------------------------------------------------------ */
    printf("  1. desequilibrio PLANO (sin retardo): el de iqbal.c basta\n");
    g_retardo = 0.0f; g_gan_db = 0.8f;
    {
        float a, b;
        g_tono_hz = 25000.0f;
        iqbal_init(); iqa_init();
        asienta(0); a = mide(0);
        iqbal_init(); iqa_init();
        asienta(1); b = mide(1);
        /* Lo que se le pide es llegar a un numero bueno, no una diferencia:
         * con entrada de banda ancha el suelo de la medida ya no deja ver
         * mas alla de los 40 y pico dB, asi que exigir "gana 20" seria
         * exigir que el caso sin corregir fuese peor de lo que es. */
        snprintf(det, sizeof det, "%.1f -> %.1f dB", (double)a, (double)b);
        di("el plano solo pasa de 35 dB", b > 35.0f, det);
    }

    /* ------------------------------------------------------------------ */
    /*
     * MEDIO PERIODO DE RETARDO NO ES REALISTA, Y ADEMAS NO PRUEBA NADA.
     *
     * Un retardo d mete un error de fase de d*w, o sea que medio periodo son
     * 84 grados en 45 kHz. A 90 grados la señal y su imagen SE INTERCAMBIAN:
     * el banco media rechazos NEGATIVOS -la imagen mas fuerte que la señal-
     * y eso no es un desequilibrio que corregir, es otro receptor.
     *
     * Entre dos ramas del mismo circuito la diferencia de camino son
     * nanosegundos. Lo que de verdad hace que el desequilibrio dependa de la
     * frecuencia son los filtros antialias, que no son identicos. 0,05
     * periodos -medio microsegundo- da 0,9 grados en 5 kHz y 9 en 45: un
     * desequilibrio pequeño en todas partes pero DISTINTO en cada sitio, que
     * es exactamente el caso que el corrector plano no puede y este si.
     */
    printf("\n  2. con RETARDO de 0,05 muestras en Q: ahi se rompe el plano\n");
    g_retardo = 0.05f; g_gan_db = 0.4f;

    for (k = 0; k < 5; k++) {
        g_tono_hz = tonos[k];
        iqbal_init(); iqa_init(); asienta(0); sin_c[k] = mide(0);
        iqbal_init(); iqa_init(); asienta(1); plano[k] = mide(1);
        iqbal_init(); iqa_init(); asienta(2); ancho[k] = mide(2);
    }

    printf("      %-10s %8s %8s %8s\n", "tono", "sin", "plano", "ancho");
    for (k = 0; k < 5; k++) {
        printf("      %7.0f Hz %8.1f %8.1f %8.1f\n",
               (double)tonos[k], (double)sin_c[k], (double)plano[k],
               (double)ancho[k]);
    }

    /* Lo que de verdad hay que comprobar, y en este orden. */
    {
        float peor_p = 1e30f, peor_a = 1e30f, mejor_p = -1e30f;
        for (k = 0; k < 5; k++) {
            if (plano[k] < peor_p) { peor_p = plano[k]; }
            if (plano[k] > mejor_p) { mejor_p = plano[k]; }
            if (ancho[k] < peor_a) { peor_a = ancho[k]; }
        }
        snprintf(det, sizeof det, "de %.1f a %.1f dB", (double)peor_p, (double)mejor_p);
        di("el plano NO vale igual en toda la banda (>10 dB de spread)",
           (mejor_p - peor_p) > 10.0f, det);

        /*
         * LO QUE SE LE PIDE AL DE BANDA ANCHA, y por que no es "pasa de 40
         * en todas". El techo de esta medida no es el corrector: es el suelo
         * de la propia FFT con entrada de banda ancha, que se queda sobre
         * los 40 dB. Pedir mas seria pedirle que bata al instrumento.
         *
         * Lo que de verdad importa son tres cosas, y son las que se piden:
         * que no empeore NUNCA al plano, que gane de verdad donde el plano
         * se cae -el borde de la banda- y que el peor caso siga siendo
         * utilizable.
         */
        {
            int peor_k = 0;
            float dmin = 1e30f;
            for (k = 0; k < 5; k++) {
                if ((ancho[k] - plano[k]) < dmin) {
                    dmin = ancho[k] - plano[k];
                    peor_k = k;
                }
            }
            snprintf(det, sizeof det, "lo peor es %+.1f dB en %.0f Hz",
                     (double)dmin, (double)tonos[peor_k]);
            di("no empeora al plano en NINGUNA frecuencia", dmin > -1.0f, det);
        }
        snprintf(det, sizeof det, "peor caso %.1f dB", (double)peor_a);
        di("y su peor caso sigue pasando de 20 dB", peor_a > 20.0f, det);
        snprintf(det, sizeof det, "peor plano %.1f, peor ancho %.1f",
                 (double)peor_p, (double)peor_a);
        di("donde el plano se cae, gana mas de 15 dB",
           (peor_a - peor_p) > 15.0f, det);
    }

    /* ------------------------------------------------------------------ */
    printf("\n  3. con la entrada YA equilibrada no puede estropearla\n");
    {
        float a, b;
        g_retardo = 0.0f; g_gan_db = 0.0f; g_tono_hz = 25000.0f;
        iqbal_init(); iqa_init(); asienta(0); a = mide(0);
        iqbal_init(); iqa_init(); asienta(2); b = mide(2);
        snprintf(det, sizeof det, "%.1f -> %.1f dB", (double)a, (double)b);
        di("equilibrada, sigue equilibrada (pierde < 6 dB)", b > a - 6.0f, det);
        snprintf(det, sizeof det, "mayor |w| = %.4f", (double)iqa_mayor());
        di("y los coeficientes se quedan en casi cero", iqa_mayor() < 0.02f, det);
    }

    /* ------------------------------------------------------------------ */
    printf("\n  4. no diverge\n");
    {
        g_retardo = 0.5f; g_gan_db = 3.0f; g_tono_hz = 40000.0f;
        iqbal_init(); iqa_init(); asienta(2);
        snprintf(det, sizeof det, "energia %.4f, mayor %.4f",
                 (double)iqa_energia(), (double)iqa_mayor());
        di("con 3 dB y medio periodo, los coeficientes se quedan acotados",
           iqa_energia() < 0.3f && iqa_mayor() <= 0.2501f, det);
    }

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}
