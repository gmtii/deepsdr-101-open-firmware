/*
 * ======================================================================
 * LA CADENCIA DE LA MARQUESINA DEL RDS, MEDIDA - 26/10/2026
 * ======================================================================
 *
 * POR QUE EXISTE. El 26/10/2026 se le puso vaiven de +-2 ms a
 * rds_marq_poll() (User/main.c, RDS_MARQ_MS) con este argumento escrito
 * delante: "RDS_MARQ_MS son 40 ms CLAVADOS, y un bloque son 2,6667 ms:
 * QUINCE BLOQUES EXACTOS, sin vaiven ninguno".
 *
 * Nadie lo midio. Y la premisa tiene un agujero: rds_marq_poll() NO
 * cuelga de la llegada de un bloque, como si hace el cuadro del espectro
 * -que vuelve entero si sdr_rx_poll_block_iq() no trae nada-. Cuelga del
 * BUCLE PRINCIPAL, y el bucle principal no da vueltas cada milisegundo:
 * da una vuelta por cuadro, porque el cuadro es lo que se lleva el
 * tiempo. O sea que la marquesina solo puede dispararse en los instantes
 * en que el bucle llega a ella, y esos instantes YA van desparramados por
 * el vaiven del cuadro desde el 06/10/2026.
 *
 * Asi que la pregunta no es "40 ms son 15 bloques?" sino "cuanta energia
 * concentra la marquesina en los multiplos de 375 Hz, con y sin el vaiven
 * nuevo, dado que quien la dispara es el bucle". Que es lo que mide esto.
 *
 * EL METODO es el mismo de tools/vaiven_check.py, para que los numeros se
 * puedan comparar con los suyos: se construye el tren de instantes de
 * repintado, se le saca la DFT en el peine de 375 Hz y en seis
 * frecuencias de fondo, y se da el pico sobre el fondo en dB. Mas dB =
 * mas apilado = mas tonal.
 *
 * LAS TRES REGLAS QUE SE COMPARAN:
 *
 *   ideal          la marquesina se dispara exactamente cuando vence el
 *                  plazo. Es la premisa del comentario del firmware.
 *   bucle, fijo    como estaba ANTES del cambio: plazo de 40 ms clavados,
 *                  pero disparada por el bucle, que solo pasa por aqui en
 *                  los instantes de cuadro (22 ms +- 2, y el cuadro a su
 *                  vez solo puede caer al llegar un bloque).
 *   bucle, vaiven  como esta AHORA: plazo de 40 ms +- 2.
 *
 * Y el cuadro se simula con su regla de verdad, la de main.c: solo puede
 * empezar en el instante en que llega un bloque de audio.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BLOQUE_MS   (1000.0 / 375.0)   /* 512 muestras a 192 kHz */
#define DUR_S       20.0
#define FS          48000.0
#define N           (1 << 15)

static unsigned long s_rnd;
static unsigned sorteo(unsigned n)
{
    s_rnd = s_rnd * 1103515245UL + 12345UL;
    return (unsigned)((s_rnd >> 16) % n);
}

/* El tren de instantes. Devuelve cuantos y los deja en t[] (segundos).
 *
 *   marq_ms     el plazo nominal de la marquesina
 *   marq_vai    +-vaiven en ms (0 = clavado)
 *   por_bucle   0 = se dispara en cuanto vence; 1 = solo cuando el bucle
 *               principal pasa por ella, o sea en un instante de cuadro
 */
static int tren(double *t, int max, double marq_ms, int marq_vai,
                double vuelta_ms, double frame_ms, int frame_vai)
{
    double prox_marq = 0.0, prox_frame = 0.0, prox_vuelta = 0.0, ahora = 0.0;
    int n = 0;

    /* El tiempo avanza en pasos de 1/16 de bloque: fino para los plazos
     * de milisegundo y basta para una DFT hasta 3 kHz. */
    for (ahora = 0.0; ahora < DUR_S * 1000.0 && n < max; ahora += BLOQUE_MS / 16.0) {
        int hay_vuelta = 0;

        /* El cuadro: solo puede EMPEZAR en el instante en que llega un
         * bloque (la regla de main.c), y ademas cuando toca por plazo. */
        if (ahora >= prox_frame) {
            double b = floor(ahora / BLOQUE_MS + 1e-9) * BLOQUE_MS;
            if (fabs(ahora - b) < BLOQUE_MS / 32.0) {
                double paso = frame_ms;
                if (frame_vai > 0) {
                    paso += (double)sorteo((unsigned)(2 * frame_vai + 1)) - frame_vai;
                }
                prox_frame = ahora + paso;
            }
        }

        /* Una vuelta del bucle principal cada vuelta_ms. Con vuelta_ms
         * muy pequeno es como dispararse sola. */
        if (ahora >= prox_vuelta) {
            hay_vuelta = 1;
            prox_vuelta = ahora + vuelta_ms;
        }

        if (hay_vuelta && ahora >= prox_marq) {
            double paso = marq_ms;
            if (marq_vai > 0) {
                paso += (double)sorteo((unsigned)(2 * marq_vai + 1)) - marq_vai;
            }
            prox_marq = ahora + paso;
            t[n++] = ahora / 1000.0;
        }
    }
    return n;
}

static double mag(const double *t, int n, double hz)
{
    double re = 0.0, im = 0.0, w = 2.0 * M_PI * hz;
    int i;
    for (i = 0; i < n; i++) {
        re += cos(w * t[i]);
        im -= sin(w * t[i]);
    }
    return hypot(re, im);
}

/* Pico en el peine de 375 Hz menos la mediana del fondo, en dB. */
static double concentracion(const double *t, int n)
{
    static const double fondo_hz[6] = {137.0, 311.0, 523.0, 907.0, 1303.0, 1709.0};
    double pk = -1e30, f[6];
    int k;

    for (k = 1; k <= 8; k++) {
        double v = 20.0 * log10(mag(t, n, 375.0 * (double)k) + 1e-12);
        if (v > pk) { pk = v; }
    }
    for (k = 0; k < 6; k++) {
        f[k] = 20.0 * log10(mag(t, n, fondo_hz[k]) + 1e-12);
    }
    /* mediana alta, igual que vaiven_check.py (indice 3 de 6 ordenados) */
    {
        int i, j;
        for (i = 0; i < 6; i++) {
            for (j = i + 1; j < 6; j++) {
                if (f[j] < f[i]) { double s = f[i]; f[i] = f[j]; f[j] = s; }
            }
        }
        return pk - f[3];
    }
}

/* Los huecos entre disparos, en bloques: el minimo, el maximo y cuantos
 * valores distintos salen. Un solo valor = enganchado. */
static void huecos(const double *t, int n, double *mn, double *mx, int *distintos)
{
    double v[64];
    int i, d = 0;

    *mn = 1e30; *mx = -1e30;
    for (i = 0; i + 1 < n; i++) {
        double b = (t[i + 1] - t[i]) * 1000.0 / BLOQUE_MS;
        double r = floor(b * 100.0 + 0.5) / 100.0;
        int j, visto = 0;
        if (b < *mn) { *mn = b; }
        if (b > *mx) { *mx = b; }
        for (j = 0; j < d; j++) { if (fabs(v[j] - r) < 0.005) { visto = 1; break; } }
        if (!visto && d < 64) { v[d++] = r; }
    }
    *distintos = d;
}

static int fallos = 0;
static void di(const char *que, int ok, const char *det)
{
    printf("    %-56s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det[0] ? "  " : "", det);
    if (!ok) { fallos++; }
}

static void caso(const char *nombre, double marq_ms, int marq_vai,
                 double vuelta_ms, double *conc_out)
{
    static double t[4096];
    double c, mn, mx;
    int n, d;

    s_rnd = 0x13579BDuL;
    n = tren(t, (int)(sizeof t / sizeof t[0]), marq_ms, marq_vai,
             vuelta_ms, 22.0, 2);
    c = concentracion(t, n);
    huecos(t, n, &mn, &mx, &d);
    printf("    %-30s %5d disparos  %4.1f/s   hueco %5.2f..%5.2f bloques"
           " (%2d distintos)   peine %+6.1f dB\n",
           nombre, n, (double)n / DUR_S, mn, mx, d, c);
    if (conc_out) { *conc_out = c; }
}

int main(void)
{
    double c_ideal_fijo = 0.0, c_ideal_vai = 0.0, c_bucle_fijo = 0.0, c_bucle_vai = 0.0;

    printf("\n==============================================================\n");
    printf(" LA MARQUESINA DEL RDS: CUANTO APILA EN LOS MULTIPLOS DE 375 Hz\n");
    printf("==============================================================\n\n");
    printf("  bloque de audio = %.4f ms (512 muestras a 192 kHz)\n", BLOQUE_MS);
    printf("  cuadro del panel = 22 ms +-2, y solo puede empezar al\n");
    printf("  llegar un bloque (la regla de main.c)\n\n");

    printf("  (A) LA PREMISA DEL COMENTARIO: la marquesina se dispara sola,\n");
    printf("      en cuanto vence el plazo (vuelta del bucle = 0,1 ms)\n\n");
    caso("40 ms clavados",          40.0, 0, 0.1, &c_ideal_fijo);
    caso("40 ms +-2 (lo de ahora)", 40.0, 2, 0.1, &c_ideal_vai);

    printf("\n  (B) LA REALIDAD: la dispara el BUCLE PRINCIPAL, y una vuelta\n");
    printf("      del bucle no es gratis. Barrido del tiempo de vuelta:\n\n");
    {
        static const double vueltas[5] = { 1.0, 3.0, 9.0, 22.0, 47.0 };
        int v;
        for (v = 0; v < 5; v++) {
            char nf[64], nv[64];
            double cf, cv;
            snprintf(nf, sizeof nf, "vuelta %4.0f ms, 40 clavados", vueltas[v]);
            snprintf(nv, sizeof nv, "vuelta %4.0f ms, 40 +-2", vueltas[v]);
            caso(nf, 40.0, 0, vueltas[v], &cf);
            caso(nv, 40.0, 2, vueltas[v], &cv);
            if (vueltas[v] == 22.0) { c_bucle_fijo = cf; c_bucle_vai = cv; }
            printf("\n");
        }
    }

    printf("\n  VEREDICTO\n\n");
    {
        char det[128];

        snprintf(det, sizeof det, "%+.1f -> %+.1f dB", c_ideal_fijo, c_ideal_vai);
        di("(A) con la premisa del comentario, el vaiven baja el peine",
           c_ideal_vai < c_ideal_fijo - 3.0, det);

        snprintf(det, sizeof det, "%+.1f dB con 40 ms clavados", c_bucle_fijo);
        di("(B) pero disparada por el bucle NO estaba enganchada",
           c_bucle_fijo < c_ideal_fijo - 3.0, det);

        snprintf(det, sizeof det, "%+.1f -> %+.1f dB (%+.1f)",
                 c_bucle_fijo, c_bucle_vai, c_bucle_vai - c_bucle_fijo);
        di("(B) y entonces el vaiven nuevo apenas cambia nada",
           fabs(c_bucle_vai - c_bucle_fijo) < 3.0, det);
    }

    printf("\n  %s (%d fallos)\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien", fallos);
    return fallos ? 1 : 0;
}
