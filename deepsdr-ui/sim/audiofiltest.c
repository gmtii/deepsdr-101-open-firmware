/*
 * Banco del filtro de audio de dos cortes (User/audiofil.c del firmware).
 *
 * COMO SE MIDE, Y POR QUE ASI
 * ---------------------------
 * audiofil.c trae su propia funcion de respuesta, audiofil_respuesta_db().
 * Usarla para juzgar el filtro no comprobaria nada: las dos cosas salen de
 * la misma cabeza y un error de convenio estaria en las dos.
 *
 * Asi que aqui el filtro se MIDE PASANDOLE SEÑAL, con la ecuacion en
 * diferencias tal como la documenta CMSIS para biquad_df1:
 *
 *     y = b0*x + b1*x1 + b2*x2 + a1*y1 + a2*y2
 *
 * Fijate en los SIGNOS de a1 y a2: en CMSIS van SUMANDO, porque la
 * biblioteca guarda esos dos coeficientes con el signo ya cambiado
 * respecto del convenio de libro. Si audiofil.c se equivocase en eso, el
 * filtro no sonaria "un poco distinto": se iria a infinito. Eso es
 * exactamente lo que este banco tiene que cazar, y por eso mide con
 * señal de verdad y no con formulas.
 *
 * Y al final se comparan las dos - la medida y la formula del modulo -
 * que es la unica forma de que audiofil_respuesta_db() sirva para algo
 * fuera de aqui.
 */
#include "audiofil.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int fallos = 0;
static void comprueba(int ok, const char *que)
{
    printf("  %-62s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/* La cascada, con la ecuacion en diferencias de CMSIS. */
typedef struct { float x1, x2, y1, y2; } bq_t;

static float bq_paso(bq_t *s, const float *c, float x)
{
    float y = c[0] * x + c[1] * s->x1 + c[2] * s->x2
            + c[3] * s->y1 + c[4] * s->y2;
    s->x2 = s->x1; s->x1 = x;
    s->y2 = s->y1; s->y1 = y;
    return y;
}

static float cascada(bq_t *s, const float *coef, float x)
{
    uint8_t k;
    for (k = 0U; k < AUDIOFIL_ETAPAS; k++) { x = bq_paso(&s[k], &coef[k * 5U], x); }
    return x;
}

/* Ganancia medida a una frecuencia, en dB: se mete un seno y se mira la
 * amplitud de salida cuando ya no queda transitorio. */
static float gana_db(const float *coef, float hz, float fs)
{
    bq_t s[AUDIOFIL_ETAPAS];
    long n = (long)(fs * 0.5f);      /* medio segundo */
    long i, arranque = (long)(fs * 0.25f);
    float pico = 0.0f;

    memset(s, 0, sizeof s);
    for (i = 0; i < n; i++) {
        float x = sinf(2.0f * (float)M_PI * hz * (float)i / fs);
        float y = cascada(s, coef, x);
        if (i > arranque) {
            float a = fabsf(y);
            if (a > pico) { pico = a; }
        }
    }
    if (pico <= 1e-9f) { return -240.0f; }
    return 20.0f * log10f(pico);
}

/* Un impulso que no se apaga es un filtro inestable. */
static int estable(const float *coef)
{
    bq_t s[AUDIOFIL_ETAPAS];
    long i;
    float ultimo = 0.0f;

    memset(s, 0, sizeof s);
    for (i = 0; i < 200000; i++) {
        float y = cascada(s, coef, (i == 0) ? 1.0f : 0.0f);
        if (!(y > -1e6f && y < 1e6f)) { return 0; }
        if (i > 190000) { ultimo += fabsf(y); }
    }
    return (ultimo < 1e-3f);
}

/*
 * Donde cae de verdad el -3 dB, por biseccion sobre la MEDIDA.
 *
 * `sube` dice de que lado esta la banda de paso: 0 para el corte de
 * arriba (paso bajo: pasa por debajo, corta por encima) y 1 para el de
 * abajo. El sentido de la comparacion depende de eso, y tenerlo al reves
 * no da un resultado un poco desviado: la biseccion empuja los dos
 * extremos en el mismo sentido y converge al borde del intervalo. La
 * primera version tenia invertido el de arriba y devolvia exactamente el
 * doble de lo pedido en las 50 filas - un numero demasiado redondo para
 * ser una medida, que es lo que lo delato.
 */
static float corte_3db(const float *coef, float fs, float desde, float hasta,
                       int sube)
{
    int k;
    for (k = 0; k < 40; k++) {
        float m = 0.5f * (desde + hasta);
        int pasa = (gana_db(coef, m, fs) > -3.0f);
        if (sube) { if (pasa) { hasta = m; } else { desde = m; } }
        else      { if (pasa) { desde = m; } else { hasta = m; } }
    }
    return 0.5f * (desde + hasta);
}

int main(void)
{
    static const float k_fs[] = { 48000.0f, 96000.0f };
    float coef[AUDIOFIL_COEFS];
    float lo, hi;
    unsigned f;

    printf("banco del filtro de audio de dos cortes\n");

    printf("\n1. estabilidad en los extremos\n");
    printf("    (un signo cambiado en a1/a2 no suena raro: se va a infinito)\n");
    {
        static const float k_lo[] = { 0.0f, 50.0f, 300.0f, 1000.0f, 5000.0f };
        static const float k_hi[] = { 200.0f, 300.0f, 1200.0f, 3000.0f, 6000.0f, 90000.0f };
        unsigned a, b;
        int todos = 1;
        for (f = 0; f < 2; f++) {
            for (a = 0; a < sizeof k_lo / sizeof k_lo[0]; a++) {
                for (b = 0; b < sizeof k_hi / sizeof k_hi[0]; b++) {
                    audiofil_disena(k_lo[a], k_hi[b], k_fs[f], coef, &lo, &hi);
                    if (!estable(coef)) {
                        printf("    SE VA con lo=%.0f hi=%.0f fs=%.0f\n",
                               (double)k_lo[a], (double)k_hi[b], (double)k_fs[f]);
                        todos = 0;
                    }
                }
            }
        }
        comprueba(todos, "las 60 combinaciones, incluidas las imposibles, son estables");
    }

    printf("\n2. los cortes caen donde se piden\n");
    /*
     * CONTADOR PROPIO, NO EL GLOBAL - 11/10/2026.
     *
     * Aqui se sumaba a `fallos` -el contador global, el que mueve
     * comprueba()- y luego se preguntaba `fallos == 0`. O sea que un fallo
     * del apartado 1 se volvia a contar aqui y se reportaba con el texto
     * del apartado 2: "los cortes caen fuera" cuando lo que fallaba era la
     * estabilidad. No oculta nada, pero manda a quien lo lea a buscar al
     * sitio equivocado, que en un banco es casi igual de malo.
     */
    { int fuera = 0;
    for (f = 0; f < 2; f++) {
        static const float k_hi[] = { 1800.0f, 2300.0f, 2700.0f, 4000.0f, 6000.0f };
        static const float k_lo[] = { 0.0f, 100.0f, 200.0f, 300.0f, 500.0f };
        unsigned a, b;
        printf("    a %.0f kHz de muestreo:\n", (double)(k_fs[f] / 1000.0f));
        for (b = 0; b < sizeof k_hi / sizeof k_hi[0]; b++) {
            for (a = 0; a < sizeof k_lo / sizeof k_lo[0]; a++) {
                float c_hi, c_lo = 0.0f;
                int ok;
                audiofil_disena(k_lo[a], k_hi[b], k_fs[f], coef, &lo, &hi);
                c_hi = corte_3db(coef, k_fs[f], hi * 0.5f, hi * 1.9f, 0);
                ok = (fabsf(c_hi - hi) < hi * 0.05f);
                if (lo > 20.0f) {
                    /* El intervalo tiene que CONTENER el corte, no acabar
                     * en el: acabando en el, la biseccion no puede hacer
                     * otra cosa que devolverlo. */
                    c_lo = corte_3db(coef, k_fs[f], lo * 0.3f, lo * 3.0f, 1);
                    ok = ok && (fabsf(c_lo - lo) < lo * 0.10f + 5.0f);
                }
                printf("      pedido %4.0f..%4.0f  medido %4.0f..%4.0f  %s\n",
                       (double)lo, (double)hi, (double)c_lo, (double)c_hi,
                       ok ? "" : "  <-- FUERA");
                if (!ok) { fuera++; }
            }
        }
    }
    comprueba(fuera == 0, "todos los cortes caen a menos de un 5-10% de lo pedido");
    }

    printf("\n3. la banda de paso es plana\n");
    {
        int llano = 1;
        float pico = 0.0f, valle = 0.0f;
        float hz;
        audiofil_disena(300.0f, 2700.0f, 48000.0f, coef, &lo, &hi);
        for (hz = 500.0f; hz < 2000.0f; hz += 25.0f) {
            float g = gana_db(coef, hz, 48000.0f);
            if (g > pico)  { pico = g; }
            if (g < valle) { valle = g; }
            if (g > 0.5f || g < -1.5f) { llano = 0; }
        }
        printf("    entre 500 y 2000 Hz: de %+.2f a %+.2f dB\n",
               (double)valle, (double)pico);
        comprueba(llano, "ni resalta ni hunde nada dentro del paso");
    }

    printf("\n4. las pendientes son las de diseño\n");
    printf("    (cuarto orden arriba = 24 dB por octava; segundo abajo = 12)\n");
    {
        float a, b;
        audiofil_disena(300.0f, 2400.0f, 48000.0f, coef, &lo, &hi);
        a = gana_db(coef, 4800.0f, 48000.0f) - gana_db(coef, 2400.0f, 48000.0f);
        b = gana_db(coef, 150.0f, 48000.0f)  - gana_db(coef, 300.0f, 48000.0f);
        printf("    de 2400 a 4800 Hz: %+.1f dB     de 300 a 150 Hz: %+.1f dB\n",
               (double)a, (double)b);
        comprueba(a < -20.0f && a > -30.0f, "arriba cae entre 20 y 30 dB la octava");
        comprueba(b < -9.0f && b > -16.0f,  "y abajo entre 9 y 16");
    }

    printf("\n5. con el corte de abajo a cero, la continua pasa\n");
    {
        float g;
        audiofil_disena(0.0f, 2700.0f, 48000.0f, coef, &lo, &hi);
        g = gana_db(coef, 20.0f, 48000.0f);
        printf("    a 20 Hz: %+.2f dB (corte de abajo usado: %.0f Hz)\n",
               (double)g, (double)lo);
        comprueba(g > -1.0f && lo == 0.0f, "sin paso alto no hay paso alto");
    }

    printf("\n6. lo que se pide imposible se recorta, no se rompe\n");
    {
        audiofil_disena(5000.0f, 1000.0f, 48000.0f, coef, &lo, &hi);
        printf("    pedido 5000..1000 -> usado %.0f..%.0f\n", (double)lo, (double)hi);
        comprueba(hi > lo && hi - lo >= AUDIOFIL_ANCHO_MIN - 1.0f,
                  "los cortes cruzados salen separados y en orden");
        audiofil_disena(0.0f, 90000.0f, 48000.0f, coef, &lo, &hi);
        printf("    pedido 0..90000 a 48 kHz -> usado %.0f..%.0f\n",
               (double)lo, (double)hi);
        comprueba(hi < 24000.0f, "y un corte por encima de Nyquist se baja");
    }

    printf("\n7. la formula del modulo dice lo mismo que la medida\n");
    {
        float peor = 0.0f, peor_hz = 0.0f;
        float hz;
        audiofil_disena(250.0f, 2600.0f, 48000.0f, coef, &lo, &hi);
        for (hz = 60.0f; hz < 8000.0f; hz *= 1.05f) {
            float m = gana_db(coef, hz, 48000.0f);
            float t = audiofil_respuesta_db(coef, hz, 48000.0f);
            float d = fabsf(m - t);
            if (d > peor) { peor = d; peor_hz = hz; }
        }
        printf("    peor desacuerdo: %.2f dB a %.0f Hz\n",
               (double)peor, (double)peor_hz);
        comprueba(peor < 0.5f,
                  "audiofil_respuesta_db() sirve para lo que dice que sirve");
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}
