/*
 * EL DETECTOR DE FASE DE M-PSK - 14/10/2026.
 *
 * La causa estaba escrita en hfdl_costas.c desde hacia semanas: la
 * correccion que corre en cada simbolo supone BPSK siempre. `sign(I)*Q`
 * mide el error contra una constelacion de DOS puntos; con cuatro u ocho
 * empuja la fase hacia un punto que no existe, simbolo tras simbolo.
 *
 * ESTE BANCO MIDE LAS DOS COSAS QUE HAY QUE CREERSE:
 *
 *   1. que con BPSK el camino nuevo es EL MISMO, bit a bit. Si no lo
 *      fuera, estaria arriesgando el 39 % que hoy funciona para arreglar
 *      un 0 %, y eso no es un arreglo: es una apuesta.
 *
 *   2. que con 8-PSK el viejo NO converge y el nuevo SI. Sin la primera
 *      mitad, "el nuevo converge" no dice nada: habria que creerse que el
 *      viejo era el problema.
 */
#include "hfdl_costas.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int fallos = 0;
static void di(int cond, const char *que, const char *como)
{
    printf("    %-48s %s  %s\n", que, cond ? "ok " : "MAL", como ? como : "");
    if (!cond) { fallos++; }
}

#define N 4000

/* Una rafaga de M-PSK con un desvio de portadora: simbolos al azar de la
 * constelacion, girados un poquito mas en cada simbolo. */
static void rafaga(float *si, float *sq, uint32_t m, float dw, uint32_t sem)
{
    uint32_t i, r = sem;
    float ph = 0.0f;
    for (i = 0U; i < N; i++) {
        float a;
        r = r * 1103515245U + 12345U;
        a = (float)((r >> 16) % m) * (2.0f * 3.14159265f / (float)m);
        si[i] = cosf(a + ph);
        sq[i] = sinf(a + ph);
        ph += dw;
    }
}

/*
 * Cuanto se parece la salida a la constelacion que deberia ser. Si el
 * lazo engancha, cada simbolo cae cerca de uno de los M puntos y esto se
 * va a cero; si persigue un punto que no existe, se queda alto.
 */
static float error_medio(const float *i_o, const float *q_o, uint32_t m,
                         uint32_t desde)
{
    const float paso = 2.0f * 3.14159265f / (float)m;
    float suma = 0.0f;
    uint32_t i, n = 0U;
    for (i = desde; i < N; i++) {
        float th = atan2f(q_o[i], i_o[i]);
        float k  = th / paso;
        k = (k >= 0.0f) ? (float)(int)(k + 0.5f) : (float)(int)(k - 0.5f);
        th -= k * paso;
        suma += (th < 0.0f) ? -th : th;
        n++;
    }
    return (n != 0U) ? (suma / (float)n) : 9.9f;
}

static float si[N], sq[N], oi[N], oq[N];

int main(void)
{
    hfdl_costas_t c1, c2;
    uint32_t i;

    printf("\nEL DETECTOR DE FASE DE M-PSK\n");
    printf("============================\n\n");

    printf("  1. con BPSK, el camino nuevo tiene que ser EL MISMO\n");
    {
        float ai[N], aq[N];
        uint32_t distintos = 0U;

        rafaga(si, sq, 2U, 0.0015f, 7U);
        hfdl_costas_init(&c1, HFDL_COSTAS_DEFAULT_ALPHA, HFDL_COSTAS_DEFAULT_BETA);
        hfdl_costas_init(&c2, HFDL_COSTAS_DEFAULT_ALPHA, HFDL_COSTAS_DEFAULT_BETA);
        for (i = 0U; i < N; i++) {
            hfdl_costas_step_bpsk(&c1, si[i], sq[i], &ai[i], &aq[i]);
            hfdl_costas_step_mpsk(&c2, si[i], sq[i], 1U, &oi[i], &oq[i]);
            if (ai[i] != oi[i] || aq[i] != oq[i]) { distintos++; }
        }
        {
            char d[64];
            snprintf(d, sizeof d, "%u simbolos de %d distintos", distintos, N);
            di(distintos == 0U, "bit a bit igual que step_bpsk()", d);
        }
    }

    printf("\n  2. con 8-PSK, el viejo NO engancha y el nuevo SI\n");
    {
        float e_viejo, e_nuevo;
        char d[96];

        rafaga(si, sq, 8U, 0.0015f, 11U);

        hfdl_costas_init(&c1, HFDL_COSTAS_DEFAULT_ALPHA, HFDL_COSTAS_DEFAULT_BETA);
        for (i = 0U; i < N; i++) { hfdl_costas_step_bpsk(&c1, si[i], sq[i], &oi[i], &oq[i]); }
        e_viejo = error_medio(oi, oq, 8U, N / 2U);

        hfdl_costas_init(&c2, HFDL_COSTAS_DEFAULT_ALPHA, HFDL_COSTAS_DEFAULT_BETA);
        for (i = 0U; i < N; i++) { hfdl_costas_step_mpsk(&c2, si[i], sq[i], 3U, &oi[i], &oq[i]); }
        e_nuevo = error_medio(oi, oq, 8U, N / 2U);

        snprintf(d, sizeof d, "viejo %.3f rad, nuevo %.3f rad", e_viejo, e_nuevo);
        /* El reparto uniforme sobre un sector de 45 grados da 0,196 rad de
         * error medio: ese es el valor de "no ha enganchado nada". */
        di(e_viejo > 0.15f, "el de BPSK se queda en el error de no enganchar", d);
        di(e_nuevo < 0.05f, "y el de M-PSK converge", d);
    }

    printf("\n  3. y con 4-PSK igual\n");
    {
        float e_viejo, e_nuevo;
        char d[96];

        rafaga(si, sq, 4U, 0.0015f, 13U);
        hfdl_costas_init(&c1, HFDL_COSTAS_DEFAULT_ALPHA, HFDL_COSTAS_DEFAULT_BETA);
        for (i = 0U; i < N; i++) { hfdl_costas_step_bpsk(&c1, si[i], sq[i], &oi[i], &oq[i]); }
        e_viejo = error_medio(oi, oq, 4U, N / 2U);
        hfdl_costas_init(&c2, HFDL_COSTAS_DEFAULT_ALPHA, HFDL_COSTAS_DEFAULT_BETA);
        for (i = 0U; i < N; i++) { hfdl_costas_step_mpsk(&c2, si[i], sq[i], 2U, &oi[i], &oq[i]); }
        e_nuevo = error_medio(oi, oq, 4U, N / 2U);
        snprintf(d, sizeof d, "viejo %.3f rad, nuevo %.3f rad", e_viejo, e_nuevo);
        di(e_viejo > 0.15f, "el de BPSK tampoco engancha 4-PSK", d);
        di(e_nuevo < 0.05f, "y el de M-PSK si", d);
    }

    /*
     * 4. EL GIRO ABSOLUTO. Lo de arriba engancha el lazo a UN punto de la
     * constelacion; esto decide a CUAL. Sin ello, 4-PSK y 8-PSK no
     * decodifican nunca aunque el lazo este perfecto, y eso es lo que se
     * midio en la radio: cero de trece.
     *
     * La prueba es directa: se finge una correlacion contra la secuencia
     * de entrenamiento -que es REAL, +-1- girada un numero conocido de
     * pasos, y se exige que lo que devuelve la deshaga.
     */
    printf("\n  4. el giro absoluto de la constelacion\n");
    {
        uint8_t bits;
        uint8_t malos = 0U;
        for (bits = 2U; bits <= 3U; bits++) {
            uint32_t m = 1U << bits, kk;
            for (kk = 0U; kk < m; kk++) {
                float ang = (float)kk * 2.0f * 3.14159265f / (float)m;
                /* correlacion de 135 simbolos, girada kk pasos, con una
                 * miga de ruido para que no sea un caso de laboratorio */
                float ci = 135.0f * cosf(ang) + 3.0f;
                float cq = 135.0f * sinf(ang) - 2.0f;
                float gc, gs, ri, rq;
                hfdl_costas_giro_absoluto(ci, cq, bits, &gc, &gs);
                /* deshacerlo tiene que dejar la correlacion en el eje real
                 * positivo, que es donde estaria sin ambiguedad */
                ri = ci * gc - cq * gs;
                rq = ci * gs + cq * gc;
                if (ri < 100.0f || rq > 6.0f || rq < -6.0f) { malos++; }
            }
        }
        {
            char d[80];
            snprintf(d, sizeof d, "%u giros mal de 12", malos);
            di(malos == 0U, "los 4 giros de 4-PSK y los 8 de 8-PSK", d);
        }
    }

    printf("\n  5. y que BPSK no pase por ahi\n");
    {
        float gc = 0.0f, gs = 9.0f;
        hfdl_costas_giro_absoluto(-135.0f, 2.0f, 1U, &gc, &gs);
        di(gc == 1.0f && gs == 0.0f,
           "con BPSK devuelve la identidad, manda su polaridad", "");
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
