/*
 * Banco del notch automatico (User/anotch.c del firmware).
 *
 * UN NOTCH AUTOMATICO TIENE DOS FORMAS DE SER MALO, Y LAS DOS SE MIDEN
 * AQUI A LA VEZ:
 *
 *   1. no quitar el pitido. Se ve enseguida y se arregla subiendo la
 *      ganancia del lazo.
 *   2. llevarse la voz por delante. NO se ve enseguida, porque el pitido
 *      ha desaparecido y parece que funciona de maravilla - hasta que te
 *      das cuenta de que la voz suena hueca y apagada. Subir la ganancia
 *      empeora esto exactamente igual que mejora lo otro.
 *
 * Por eso aqui no hay una medida sino un PAR, y el barrido del final las
 * enseña juntas: cualquier ajuste que solo mire una de las dos elige mal
 * por construccion. Los tres ajustes de fuerza de anotch.c salen de ese
 * barrido.
 */
#include "anotch.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#define FS 12000.0f
#define N  (unsigned)(FS * 3.0f)   /* tres segundos */

static int fallos = 0;
static void comprueba(int ok, const char *que)
{
    printf("  %-62s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

static unsigned long g_sem = 7777UL;
static float ruido(void)
{
    float a, b;
    g_sem = g_sem * 1103515245UL + 12345UL;
    a = (float)((g_sem >> 16) & 0x7FFF) / 32767.0f - 0.5f;
    g_sem = g_sem * 1103515245UL + 12345UL;
    b = (float)((g_sem >> 16) & 0x7FFF) / 32767.0f - 0.5f;
    return a + b;
}

/* Energia a una frecuencia concreta, sobre la SEGUNDA mitad del tramo
 * (la primera es donde el lazo esta enganchando todavia). */
static float energia_en(const float *x, unsigned n, float hz)
{
    float sr = 0.0f, si = 0.0f;
    unsigned i, d = n / 2U;
    for (i = d; i < n; i++) {
        float w = 2.0f * (float)M_PI * hz * (float)i / FS;
        sr += x[i] * cosf(w);
        si += x[i] * sinf(w);
    }
    return (sr * sr + si * si) / (float)((n - d) * (n - d)) * 4.0f;
}

static float energia_total(const float *x, unsigned n)
{
    float s = 0.0f;
    unsigned i, d = n / 2U;
    for (i = d; i < n; i++) { s += x[i] * x[i]; }
    return s / (float)(n - d);
}

static float db(float a, float b)
{
    if (a <= 0.0f || b <= 0.0f) { return 0.0f; }
    return 10.0f * log10f(a / b);
}

static float g_ent[N], g_sal[N];

/*
 * Algo que se parezca a una voz lo bastante para que el notch pueda
 * estropearla: ruido filtrado con tres formantes que se mueven, mas una
 * envolvente de silabas. No es voz, pero tiene lo que importa aqui - es
 * de banda ancha, cambia todo el rato y NO es predecible a medio
 * milisegundo, que es justo lo que el notch tiene que respetar.
 */
static void voz(float *x, unsigned n)
{
    float e1 = 0, e2 = 0, e3 = 0, b1 = 0, b2 = 0, b3 = 0;
    unsigned i;
    for (i = 0; i < n; i++) {
        float t = (float)i / FS;
        float r = ruido();
        float f1 = 500.0f  + 200.0f * sinf(2.0f * (float)M_PI * 3.1f * t);
        float f2 = 1200.0f + 400.0f * sinf(2.0f * (float)M_PI * 2.3f * t);
        float f3 = 2400.0f + 300.0f * sinf(2.0f * (float)M_PI * 1.7f * t);
        float sil = 0.5f + 0.5f * sinf(2.0f * (float)M_PI * 4.0f * t);
        /* Tres resonadores de dos polos, cada uno centrado en su formante. */
        #define RES(e, b, f, q) do { \
            float w = 2.0f * (float)M_PI * (f) / FS; \
            float c = 2.0f * cosf(w) * (q); \
            float o = r + c * (e) - (q) * (q) * (b); \
            (b) = (e); (e) = o; } while (0)
        RES(e1, b1, f1, 0.97f);
        RES(e2, b2, f2, 0.96f);
        RES(e3, b3, f3, 0.95f);
        #undef RES
        x[i] = sil * 0.02f * (e1 + 0.6f * e2 + 0.4f * e3);
    }
}

static void tono(float *x, unsigned n, float hz, float amp, int sumar)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        float v = amp * sinf(2.0f * (float)M_PI * hz * (float)i / FS);
        x[i] = sumar ? (x[i] + v) : v;
    }
}

static void pasa(void)
{
    memcpy(g_sal, g_ent, sizeof g_ent);
    anotch_set_activo(1U);
    /* En bloques de 32, como en la radio. */
    {
        unsigned o;
        for (o = 0; o + 32U <= N; o += 32U) { anotch_process(&g_sal[o], 32U); }
    }
}

/*
 * Devuelve, en dB: cuanto baja el tono y cuanto baja la voz.
 *
 * EL TONO SE MIDE CON LA VOZ DEBAJO, no solo. Medirlo solo era lo que
 * hacia la primera version de este banco y no servia para nada: un seno
 * sin nada alrededor es perfectamente predecible, asi que CUALQUIER
 * ajuste lo quitaba unos 128 dB y la columna salia igual en las 64 filas
 * del barrido. Una medida que da lo mismo pase lo que pase no mide.
 *
 * Con la voz debajo, el predictor tiene que repartirse entre las dos
 * cosas, que es el problema de verdad, y entonces la columna separa.
 */
static void mide(float hz_tono, float *quita_tono, float *dana_voz)
{
    float e0, e1;

    /* 1. voz + tono: cuanto baja el tono. */
    voz(g_ent, N);
    tono(g_ent, N, hz_tono, 0.3f, 1);
    e0 = energia_en(g_ent, N, hz_tono);
    pasa();
    e1 = energia_en(g_sal, N, hz_tono);
    *quita_tono = -db(e1, e0);

    /* 2. solo la voz: cuanto se lleva de ella. */
    voz(g_ent, N);
    e0 = energia_total(g_ent, N);
    pasa();
    e1 = energia_total(g_sal, N);
    *dana_voz = -db(e1, e0);
}

int main(void)
{
    float q, d;

    printf("banco del notch automatico\n");
    anotch_init(FS);

    printf("\n1. apagado no toca el audio\n");
    {
        unsigned i; int igual = 1;
        anotch_init(FS);
        tono(g_ent, N, 1000.0f, 0.3f, 0);
        memcpy(g_sal, g_ent, sizeof g_ent);
        anotch_set_activo(0U);
        for (i = 0; i + 32U <= N; i += 32U) { anotch_process(&g_sal[i], 32U); }
        for (i = 0; i < N; i++) { if (g_sal[i] != g_ent[i]) { igual = 0; break; } }
        comprueba(igual, "apagado, la salida es la entrada byte a byte");
    }

    printf("\n2. un pitido de 1 kHz en medio de una voz\n");
    anotch_init(FS);
    anotch_set_fuerza(1U);
    {
        float e_tono_antes, e_tono_despues, e_voz_antes, e_voz_despues;

        voz(g_ent, N);
        e_voz_antes = energia_total(g_ent, N);
        tono(g_ent, N, 1000.0f, 0.3f, 1);
        e_tono_antes = energia_en(g_ent, N, 1000.0f);
        pasa();
        e_tono_despues = energia_en(g_sal, N, 1000.0f);

        voz(g_ent, N);
        pasa();
        e_voz_despues = energia_total(g_sal, N);

        printf("    el pitido baja %.1f dB; la voz baja %.1f dB\n",
               -db(e_tono_despues, e_tono_antes),
               -db(e_voz_despues, e_voz_antes));
        comprueba(-db(e_tono_despues, e_tono_antes) > 20.0f,
                  "el pitido baja mas de 20 dB");
        comprueba(-db(e_voz_despues, e_voz_antes) < 3.0f,
                  "y la voz no baja ni 3 dB");
    }

    printf("\n3. el pitido, donde sea que este\n");
    {
        static const float k_hz[] = { 300.0f, 700.0f, 1500.0f, 2400.0f, 3000.0f };
        unsigned i;
        for (i = 0; i < sizeof k_hz / sizeof k_hz[0]; i++) {
            anotch_init(FS); anotch_set_fuerza(1U);
            mide(k_hz[i], &q, &d);
            printf("    %6.0f Hz: quita %5.1f dB del pitido, %4.1f dB de la voz\n",
                   (double)k_hz[i], (double)q, (double)d);
            comprueba(q > 18.0f, "lo quita aqui tambien");
        }
    }

    printf("\n4. dos pitidos a la vez\n");
    anotch_init(FS); anotch_set_fuerza(2U);
    {
        float a0, b0, a1, b1;
        tono(g_ent, N, 800.0f, 0.3f, 0);
        tono(g_ent, N, 1900.0f, 0.3f, 1);
        a0 = energia_en(g_ent, N, 800.0f);
        b0 = energia_en(g_ent, N, 1900.0f);
        pasa();
        a1 = energia_en(g_sal, N, 800.0f);
        b1 = energia_en(g_sal, N, 1900.0f);
        printf("    800 Hz baja %.1f dB, 1900 Hz baja %.1f dB\n",
               -db(a1, a0), -db(b1, b0));
        comprueba(-db(a1, a0) > 15.0f && -db(b1, b0) > 15.0f,
                  "quita los dos, no solo el mas fuerte");
    }

    printf("\n5. no se inventa un notch cuando no hay pitido\n");
    anotch_init(FS); anotch_set_fuerza(1U);
    {
        unsigned i;
        for (i = 0; i < N; i++) { g_ent[i] = 0.1f * ruido(); }
        {
            float e0 = energia_total(g_ent, N), e1;
            pasa();
            e1 = energia_total(g_sal, N);
            printf("    con ruido blanco solo, la salida baja %.1f dB\n", -db(e1, e0));
            comprueba(-db(e1, e0) < 2.0f, "el ruido blanco pasa casi entero");
        }
    }

    printf("\n6. no se va con señal muy fuerte ni con silencio\n");
    {
        unsigned i; int sano = 1;
        anotch_init(FS); anotch_set_fuerza(3U);
        for (i = 0; i < N; i++) {
            g_ent[i] = (i < N / 3U) ? 0.0f
                     : ((i < 2U * N / 3U) ? 40.0f * sinf(0.5f * (float)i)
                                          : 1e-7f * ruido());
        }
        pasa();
        for (i = 0; i < N; i++) {
            if (!(g_sal[i] > -1e6f && g_sal[i] < 1e6f)) { sano = 0; break; }
        }
        comprueba(sano, "silencio, luego 40 de amplitud, luego casi nada: no se va");
    }

    printf("\n7. el barrido (de donde salen los tres ajustes de fuerza)\n");
    printf("    tomas retardo    mu   quita pitido   daña voz\n");
    {
        static const uint16_t k_t[] = { 24U, 48U, 72U, 96U };
        static const uint16_t k_r[] = { 2U, 6U, 12U, 24U };
        static const float    k_mu[] = { 0.008f, 0.02f, 0.04f, 0.08f };
        unsigned a, b, c;
        int divergen = 0;
        for (b = 0; b < sizeof k_r / sizeof k_r[0]; b++) {
            for (a = 0; a < sizeof k_t / sizeof k_t[0]; a++) {
                for (c = 0; c < sizeof k_mu / sizeof k_mu[0]; c++) {
                    anotch_init(FS);
                    anotch_params(k_t[a], k_r[b], k_mu[c]);
                    mide(1000.0f, &q, &d);
                    /*
                     * "Sirve" pide TRES cosas, y la tercera se aprendio
                     * aqui: el daño a la voz tiene que ser POSITIVO. Un
                     * daño negativo quiere decir que la salida tiene mas
                     * energia que la entrada, o sea que el lazo se esta
                     * yendo - y la primera version del marcador daba por
                     * buena justo esa fila (24 tomas, retardo 2, mu 0,08)
                     * porque "menos de 3 dB de daño" tambien lo cumple
                     * -38 dB. Un criterio que acepta un filtro que
                     * diverge no es un criterio.
                     */
                    if (d < -0.5f) { divergen++; }
                    printf("    %5u %7u %6.3f   %8.1f dB  %7.1f dB%s\n",
                           k_t[a], k_r[b], (double)k_mu[c], (double)q, (double)d,
                           (d < -0.5f) ? "   <-- SE VA"
                           : ((q > 15.0f && d < 2.0f) ? "   <-- sirve" : ""));
                }
            }
        }
        comprueba(divergen == 0,
                  "ninguna combinacion del barrido hace que el lazo se vaya");
    }
    /*

     * ==================================================================
     * EL ARRANQUE: ENCENDER EL NOTCH NO PUEDE PEGAR UN GOLPE
     * ==================================================================
     *
     * De una revision a fondo del 10/10/2026. Antes de arreglarlo, esto
     * media +42 dB: anotch_set_activo(1) llama a limpia(), la linea de
     * retardo se queda a cero, y durante las primeras muestras el paso
     * del lazo -g = mu*e/pot- lleva en el denominador la energia de una
     * ventana casi vacia mientras el numerador ya es la señal entera.
     *
     * DOS COSAS QUE HAY QUE HACER BIEN AQUI, y que el resto del banco no
     * hacia:
     *
     *   - la señal va en la ESCALA DE LA RADIO, miles, no en +-1. Con
     *     +-1 el fallo sale igual de grande en proporcion pero no se ve
     *     que satura, que es lo que se oye.
     *   - y se enciende el notch ANTES de que entre la señal, con un
     *     trozo de casi-silencio delante: encenderlo con la señal ya
     *     sonando llena la ventana en seguida y tapa el fallo.
     *
     * El tope es 1,5x -3,5 dB-: un notch adaptandose puede dar un poco
     * de sobreimpulso, pero no un golpe.
     */
    {
        static float a[256];
        unsigned k, b;
        float pico_ent = 0.0f, pico_sal = 0.0f;

        anotch_init(FS);
        anotch_set_fuerza(2);
        anotch_set_activo(1);

        for (b = 0; b < 12; b++) {
            for (k = 0; k < 256; k++) {
                unsigned t = b * 256u + k;
                float v = (t < 24u) ? 1.0f
                        : 1000.0f * sinf(2.0f * 3.14159265f * 800.0f * (float)t / FS);

                a[k] = v;
                if (fabsf(v) > pico_ent) { pico_ent = fabsf(v); }
            }
            anotch_process(a, 256);
            for (k = 0; k < 256; k++) {
                if (fabsf(a[k]) > pico_sal) { pico_sal = fabsf(a[k]); }
            }
        }
        printf("\n  AL ENCENDER EL NOTCH, CON LA LINEA VACIA\n");
        printf("    pico de entrada %9.1f\n", pico_ent);
        printf("    pico de salida  %9.1f   (%+.1f dB)\n", pico_sal,
               20.0f * log10f(pico_sal / pico_ent));
        comprueba(pico_sal <= pico_ent * 1.5f,
                  "encender el notch no pega un golpe");
    }
    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");

    return fallos ? 1 : 0;
}
