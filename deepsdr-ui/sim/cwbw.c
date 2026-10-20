/*
 * ANCHO REAL DEL FILTRO DE CW - 22/09/2026
 *
 * Tiene razon y aqui esta el numero. demod_am_set_cw_filter_hz() monta un
 * paso banda RBJ con Q = 0,9 en DOS etapas iguales. Q = 0,9 a 700 Hz es un
 * ancho de unos 780 Hz por etapa: en CW eso no es un filtro, es media banda
 * de 40 metros entrando a la vez.
 *
 * Este banco calcula la respuesta EXACTA de la cascada -los mismos
 * coeficientes que el firmware- y mide donde caen los 3 dB y los 20 dB. Sin
 * medir no se puede elegir la Q: la de la cascada no es la de una etapa,
 * porque dos etapas en serie estrechan el resultado.
 */
#include <stdio.h>
#include <math.h>

static int ETAPAS = 2;

/* Los mismos coeficientes que demod_am.c, convenio CMSIS (a1/a2 cambiados
 * de signo) - aqui se deshace el cambio para evaluar la respuesta. */
static void coef(float f0, float fs, float Q, double *b0, double *b2, double *a1, double *a2)
{
    double w0 = 2.0 * M_PI * (f0 / fs);
    double alpha = sin(w0) / (2.0 * Q);
    double a0 = 1.0 + alpha;
    *b0 =  alpha / a0;
    *b2 = -alpha / a0;
    *a1 = -(-2.0 * cos(w0)) / a0;   /* CMSIS guarda -a1; aqui el a1 de verdad */
    *a2 = -( (1.0 - alpha) / a0 );
    *a1 = -(2.0 * cos(w0)) / a0;
    *a2 =  (1.0 - alpha) / a0;
}

/* |H(f)| de la cascada, en dB */
static double resp_db(float f, float f0, float fs, float Q)
{
    double b0, b2, a1, a2;
    double w = 2.0 * M_PI * (f / fs);
    double cr, ci, dr, di, mag;

    coef(f0, fs, Q, &b0, &b2, &a1, &a2);
    /* numerador b0 + 0*z^-1 + b2*z^-2 */
    cr = b0 + b2 * cos(2 * w);  ci = -b2 * sin(2 * w);
    dr = 1.0 + a1 * cos(w) + a2 * cos(2 * w);
    di = -(a1 * sin(w) + a2 * sin(2 * w));
    mag = sqrt((cr * cr + ci * ci) / (dr * dr + di * di));
    return 20.0 * log10(mag) * (double)ETAPAS;   /* dos etapas iguales */
}

/* Ancho a -x dB, buscando a los dos lados del pico */
static double ancho(float f0, float fs, float Q, double caida)
{
    double pico = resp_db(f0, f0, fs, Q);
    double lo = f0, hi = f0;
    while (lo > 1.0   && resp_db((float)lo, f0, fs, Q) > pico - caida) { lo -= 0.5; }
    while (hi < fs/2.0 && resp_db((float)hi, f0, fs, Q) > pico - caida) { hi += 0.5; }
    return hi - lo;
}

int main(void)
{
    const float fs = 96000.0f;
    const float f0 = 700.0f;
    float Q;

    printf("filtro de CW: paso banda RBJ, centro %.0f Hz, Fs %.0f\n",
           (double)f0, (double)fs);
    printf("     Q      -3 dB     -20 dB\n");
    for (Q = 0.9f; Q < 30.0f; Q *= 1.5f) {
        printf("  %5.2f   %6.0f Hz  %6.0f Hz%s\n", (double)Q,
               ancho(f0, fs, Q, 3.0), ancho(f0, fs, Q, 20.0),
               (Q < 1.0f) ? "   <- lo que hay ahora" : "");
    }

    /*
     * Lo que de verdad decide si "se escuchan muchas senales a la vez" no es
     * el ancho a -3 dB sino LA FALDA: a -20 dB una senal todavia se oye.
     * Dos etapas iguales dan una falda muy tumbada. Aqui se compara, a
     * IGUAL ancho de -3 dB, lo que cuesta cada numero de etapas.
     */
    printf("\n  a igual ancho de -3 dB, que deja pasar la falda:\n");
    printf("   etapas   -3 dB      -20 dB     -40 dB     Q de cada una\n");
    {
        const double quiero = 250.0;
        int n;
        for (n = 2; n <= 8; n += 2) {
            double lo = 0.3, hi = 80.0, mid = 0.0;
            int it;
            ETAPAS = n;
            for (it = 0; it < 60; it++) {
                mid = 0.5 * (lo + hi);
                if (ancho(f0, fs, (float)mid, 3.0) > quiero) { lo = mid; } else { hi = mid; }
            }
            printf("     %d     %5.0f Hz  %6.0f Hz  %6.0f Hz    %5.2f\n", n,
                   ancho(f0, fs, (float)mid, 3.0),
                   ancho(f0, fs, (float)mid, 20.0),
                   ancho(f0, fs, (float)mid, 40.0), mid);
        }
        ETAPAS = 2;
    }

    /*
     * La Q que hay que poner para un ancho pedido, con CUATRO etapas, y si
     * esa relacion es una simple constante. Si lo es, el firmware puede
     * calcular la Q con una multiplicacion en vez de llevar una tabla.
     */
    printf("\n  con 4 etapas: ancho pedido -> Q, y la constante k = Q / (f0/ancho)\n");
    {
        const double pedidos[] = {1000.0, 500.0, 250.0, 150.0};
        int i;
        ETAPAS = 4;
        for (i = 0; i < 4; i++) {
            double lo = 0.2, hi = 80.0, mid = 0.0;
            int it;
            for (it = 0; it < 60; it++) {
                mid = 0.5 * (lo + hi);
                if (ancho(f0, fs, (float)mid, 3.0) > pedidos[i]) { lo = mid; } else { hi = mid; }
            }
            printf("  %6.0f Hz  ->  Q = %5.3f    k = %.4f   (-20 dB en %.0f Hz)\n",
                   pedidos[i], mid, mid / ((double)f0 / pedidos[i]),
                   ancho(f0, fs, (float)mid, 20.0));
        }
        /* y a otro tono, para ver que k no depende del centro */
        for (i = 0; i < 2; i++) {
            float otro = i ? 400.0f : 1000.0f;
            double lo = 0.2, hi = 80.0, mid = 0.0;
            int it;
            for (it = 0; it < 60; it++) {
                mid = 0.5 * (lo + hi);
                if (ancho(otro, fs, (float)mid, 3.0) > 250.0) { lo = mid; } else { hi = mid; }
            }
            printf("  centro %4.0f Hz, ancho 250 -> Q = %5.3f   k = %.4f\n",
                   (double)otro, mid, mid / ((double)otro / 250.0));
        }
        ETAPAS = 2;
    }

    /* Y al reves: que Q hace falta para un ancho dado. Es lo que necesita
     * el firmware, porque quien elige es el usuario en hercios. */
    printf("\n  ancho pedido -> Q de cada etapa\n");
    {
        const double pedidos[] = {1000.0, 500.0, 250.0, 150.0};
        int i;
        for (i = 0; i < 4; i++) {
            double lo = 0.5, hi = 60.0, mid = 0.0;
            int it;
            for (it = 0; it < 60; it++) {
                mid = 0.5 * (lo + hi);
                if (ancho(f0, fs, (float)mid, 3.0) > pedidos[i]) { lo = mid; } else { hi = mid; }
            }
            printf("  %6.0f Hz  ->  Q = %5.2f   (-20 dB en %.0f Hz)\n",
                   pedidos[i], mid, ancho(f0, fs, (float)mid, 20.0));
        }
    }
    return 0;
}
