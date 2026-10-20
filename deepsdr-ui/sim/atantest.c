/*
 * LA ARCOTANGENTE RAPIDA, CONTRA LA CARA - 06/10/2026.
 *
 * POR QUE EXISTE.
 *
 * rapido_atan2() sustituye a atan2f() en el discriminador de FM, que la
 * llama una vez POR MUESTRA: 96.000 veces por segundo en NFM y 192.000 en
 * WFM. El atajo esta justificado -atan2f() son un par de cientos de ciclos
 * de software- pero la version del 04/10/2026 usaba la razon
 * (x-|y|)/(x+|y|), que vale tan(pi/4 - theta): con el angulo pequeño la
 * razon se va a 1, el extremo del intervalo donde el ajuste minimax tiene su
 * error maximo. O sea que era peor justo donde mas se usa.
 *
 * Medido entonces: error de PENDIENTE del -21,5 % cerca de cero, y una THD
 * del 2 % en el discriminador de NFM con el tercer armonico clavado en -35
 * dBc. Y la firma al reves de lo que uno espera: cuanto mas floja la
 * modulacion, peor sonaba.
 *
 * Lo que no se mide se rompe, y este fichero mide las tres cosas que
 * importan:
 *
 *   1. el error absoluto maximo sobre los cuatro cuadrantes;
 *   2. el error de la PENDIENTE cerca de cero, que es el que se convierte en
 *      distorsion y el que el error maximo NO enseña;
 *   3. la THD del discriminador de FM escrito tal cual esta en demod_am.c,
 *      en cuatro casos, comparada con la de atan2f().
 *
 * Los topes estan puestos con holgura sobre lo medido, no al borde: si
 * alguien vuelve a cambiar la aproximacion y empeora cualquiera de las tres,
 * esto para antes de que salga un binario.
 */
#include <stdio.h>
#include <math.h>
#include "rapido.h"

/* Topes. Lo medido el 06/10/2026 va entre parentesis. */
#define TOPE_ERR_MAX   0.0015    /* rad  (medido 0,00062) */
#define TOPE_PENDIENTE 2.0       /* %    (medido 0,47)    */
#define TOPE_THD       0.30      /* %    (medido 0,083 en el peor caso) */

#define N 16384

static double I[N], Q[N], o[N];

/*
 * El discriminador, copiado de fm_discriminate() en demod_am.c.
 *
 * NO es una aproximacion de aquel: es la misma cuenta -producto por el
 * conjugado de la muestra anterior y arcotangente del resultado- con el
 * mismo arrastre de la muestra previa. Si aquello cambia de forma, esto deja
 * de medir lo que se cree y hay que traerlo otra vez.
 */
static void disc(double (*f)(double, double), double fs, double fdev,
                 int *ciclos_out)
{
    int ciclos = (int)(1000.0 * N / fs);   /* 1 kHz, redondeado a bin exacto */
    double fmod = ciclos * fs / N;
    double ph = 0.0, ip, qp;
    int k;

    for (k = 0; k < N; k++) {
        double fr = fdev * sin(2.0 * M_PI * fmod * k / fs);
        ph += 2.0 * M_PI * fr / fs;
        I[k] = 10000.0 * cos(ph);
        Q[k] = 10000.0 * sin(ph);
    }
    ip = I[0]; qp = Q[0];
    for (k = 1; k < N; k++) {
        double i = I[k], q = Q[k];
        o[k - 1] = f(q * ip - i * qp, i * ip + q * qp);
        ip = i; qp = q;
    }
    *ciclos_out = ciclos;
}

/* THD sobre los nueve primeros armonicos. */
static double thd(int ciclos)
{
    int M = N - 1, kk, i;
    double m0 = 0.0, f1 = 0.0, h = 0.0;

    for (i = 0; i < M; i++) { m0 += o[i]; }
    m0 /= M;
    for (i = 0; i < M; i++) { o[i] -= m0; }
    for (kk = 1; kk <= 9; kk++) {
        int bin = (int)(kk * ciclos * M / (double)N + 0.5);
        double sr = 0.0, si = 0.0, a;
        for (i = 0; i < M; i++) {
            double w = -2.0 * M_PI * bin * i / M;
            sr += o[i] * cos(w); si += o[i] * sin(w);
        }
        a = sqrt(sr * sr + si * si);
        if (kk == 1) { f1 = a; } else { h += a * a; }
    }
    return 100.0 * sqrt(h) / f1;
}

static double rap(double y, double x) { return rapido_atan2((float)y, (float)x); }
static double car(double y, double x) { return atan2(y, x); }

int main(void)
{
    int malos = 0, c, ciclos;
    double emax = 0.0, peor_th = 0.0;
    long k;
    struct { double fs, dev; const char *nom; } casos[] = {
        {  96000.0,  5000.0, "NFM +-5 kHz"        },
        {  96000.0,  1500.0, "NFM +-1,5 kHz"      },
        { 192000.0, 20000.0, "WFM +-20 kHz"       },
        { 192000.0, 75000.0, "WFM +-75 kHz"       },
    };

    printf("LA ARCOTANGENTE RAPIDA, CONTRA LA CARA\n\n");

    /* 1. el origen: atan2f(0,0) vale 0 y esta tiene que valer lo mismo. */
    {
        float v = rapido_atan2(0.0f, 0.0f);
        printf("  rapido_atan2(0,0) = %.6f", (double)v);
        if (v != 0.0f) {
            printf("   *** MAL: tiene que ser 0, como atan2f ***\n");
            printf("      Con I y Q a cero exacto -arranque, bloque silenciado-\n");
            printf("      el discriminador suelta esto por la ganancia: un golpe.\n");
            malos++;
        } else {
            printf("   ok\n");
        }
    }

    /* 2. error absoluto maximo sobre los cuatro cuadrantes. */
    for (k = 0; k <= 2000000L; k++) {
        double th = -M_PI + 2.0 * M_PI * (double)k / 2000000.0;
        double e = fabs((double)rapido_atan2((float)sin(th), (float)cos(th)) - th);
        if (e > M_PI) { e = 2.0 * M_PI - e; }
        if (e > emax) { emax = e; peor_th = th; }
    }
    printf("  error maximo      %.6f rad (%.4f grados, en theta=%+.4f)   tope %.4f",
           emax, emax * 180.0 / M_PI, peor_th, TOPE_ERR_MAX);
    if (emax > TOPE_ERR_MAX) { printf("   *** MAL ***\n"); malos++; }
    else { printf("   ok\n"); }

    /* 3. la pendiente cerca de cero. Esto es lo que el error maximo NO
     *    enseña y lo que se convierte en distorsion: la version de antes
     *    tenia 0,0102 rad de error maximo -poco- y un -21,5 % de pendiente. */
    {
        double th = 1e-3;
        double a = rapido_atan2((float)sin(th), (float)cos(th));
        double rel = fabs(100.0 * (a - th) / th);
        printf("  pendiente en 0    %+.3f %% de error                            tope %.1f %%",
               100.0 * (a - th) / th, TOPE_PENDIENTE);
        if (rel > TOPE_PENDIENTE) { printf("   *** MAL ***\n"); malos++; }
        else { printf("   ok\n"); }
    }

    /* 4. la THD del discriminador, que es lo que de verdad se oye. */
    printf("\n  distorsion del discriminador de FM (un tono de 1 kHz):\n");
    printf("    %-16s %10s %10s   tope %.2f %%\n", "caso", "rapido", "atan2f", TOPE_THD);
    for (c = 0; c < (int)(sizeof casos / sizeof casos[0]); c++) {
        double t_rap, t_car;

        disc(rap, casos[c].fs, casos[c].dev, &ciclos);
        t_rap = thd(ciclos);
        disc(car, casos[c].fs, casos[c].dev, &ciclos);
        t_car = thd(ciclos);
        printf("    %-16s %9.4f%% %9.4f%%", casos[c].nom, t_rap, t_car);
        if (t_rap > TOPE_THD) { printf("   *** MAL ***\n"); malos++; }
        else { printf("   ok\n"); }
    }

    if (malos) {
        printf("\n  HAY FALLOS (%d)\n", malos);
        printf("  Ver la cabecera de este fichero y la de User/rapido.c.\n");
        return 1;
    }
    printf("\n  TODO CORRECTO\n");
    return 0;
}
