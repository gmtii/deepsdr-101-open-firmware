/*
 * EL OSCILADOR DE POR DEBAJO DE 300 kHz, MEDIDO SIN OSCILOSCOPIO.
 *
 * Pedido por el usuario.
 *
 * Las dos cosas que pide -exactitud de frecuencia y de cuadratura- NO hacen
 * falta medirlas con un osciloscopio: salen de la aritmetica del
 * temporizador, que es entera y por tanto exacta de calcular. Lo unico que
 * hacia falta era poder compilarla fuera de la radio, y por eso vive ahora
 * en lo_gen_gd32.h en vez de enterrada en el .c.
 *
 * COMO SALE LA CUADRATURA
 * -----------------------
 * TIMER2 en modo conmutacion, dos canales. El primero conmuta en la cuenta
 * 0 y el segundo en `cuentas/2`. Cada canal conmuta UNA vez por periodo de
 * contador, asi que el periodo de SALIDA son dos periodos de contador:
 *
 *     periodo de salida = 2 * cuentas
 *     desfase           = cuentas/2  de  2*cuentas  =  1/4  =  90 grados
 *
 * ...siempre que `cuentas/2` sea exacto. Y esa division es ENTERA: con
 * `cuentas` impar se queda media cuenta corta y el desfase sale de
 * 90 - 90/cuentas grados.
 *
 * QUE SE COMPRUEBA
 * ----------------
 *   1. que el periodo es SIEMPRE par, o sea la cuadratura exacta;
 *   2. que el divisor cabe en los 16 bits del ARR;
 *   3. que el error de frecuencia no pasa de un escalon;
 *   4. y se imprime el rechazo de imagen que habria con el error de fase,
 *      para que el numero este escrito y nadie tenga que volver a
 *      preguntarselo.
 *
 * SE HA VISTO FALLAR: quitando de lo_gen_gd32_cuentas() el ajuste a par, la
 * mitad exacta del rango sale impar y la comprobacion 1 se pone roja 147.338
 * veces, con el peor caso en 298.030 Hz (ARR+1 = 167, 0,539 grados, 46,6 dB
 * de rechazo de imagen).
 */
#include "lo_gen_gd32.h"
#include <stdio.h>
#include <math.h>

/* Rechazo de imagen que da un error de fase phi, con amplitudes iguales:
 * IRR = 20*log10(cot(phi/2)). */
static double irr_db(double phi_deg)
{
    double p = phi_deg * 3.14159265358979 / 180.0;
    if (p <= 0.0) { return 999.0; }
    return 20.0 * log10(1.0 / tan(p / 2.0));
}

#define F_MIN   6000UL     /* lo mas bajo que este proyecto le pide: ver
                            * TUNE_MIN_HZ - DEMOD_IF_OFFSET_HZ */
#define F_MAX 300000UL     /* LO_GEN_CROSSOVER_HZ: por encima manda el otro */

int main(void)
{
    unsigned long f;
    int impares = 0, no_cabe = 0, lejos = 0;
    unsigned long peor_f = 0;
    double peor_irr = 999.0;
    unsigned long peor_err_f = 0;
    double peor_err = 0.0;

    printf("\nEL OSCILADOR DEL GD32 (por debajo de 300 kHz)\n");
    printf("  barrido de %lu a %lu Hz, hercio a hercio\n\n", F_MIN, F_MAX);

    printf("  %9s  %7s  %4s  %10s  %9s  %8s\n",
           "pedido", "ARR+1", "par", "sale", "error", "escalon");
    for (f = F_MIN; f <= F_MAX; f++) {
        unsigned long n = lo_gen_gd32_cuentas((uint32_t)f);
        unsigned long real = lo_gen_gd32_hz_de((uint32_t)n);
        double err = (double)real - (double)f;
        double paso = (double)f * (double)f * 2.0 / (double)LO_GEN_TIMER_CLK_HZ;

        if ((n & 1UL) != 0UL) {
            double phi = 90.0 / (double)n;
            double r = irr_db(phi);

            impares++;
            if (r < peor_irr) { peor_irr = r; peor_f = f; }
        }
        if (n < 2UL || n > LO_GEN_CUENTAS_MAX) { no_cabe++; }
        /* El error nunca puede pasar de un escalon entero: con el periodo
         * forzado a par el escalon se dobla, asi que el margen es 1,0 paso
         * del escalon par (= 2 del impar). */
        if (fabs(err) > 2.0 * paso + 1.0) { lejos++; }
        if (fabs(err) > peor_err) { peor_err = fabs(err); peor_err_f = f; }

        if (f == 6000UL || f == 42600UL || f == 54000UL || f == 66667UL
            || f == 150000UL || f == 200000UL || f == 299000UL) {
            printf("  %9lu  %7lu  %4s  %10lu  %+9.1f  %8.1f\n",
                   f, n, ((n & 1UL) == 0UL) ? "si" : "NO", real, err, paso);
        }
    }

    printf("\n  1. periodo par (cuadratura exacta)      %s",
           impares == 0 ? "todas   ok\n" : "");
    if (impares != 0) {
        printf("%d de %lu IMPARES   MAL\n", impares, F_MAX - F_MIN + 1UL);
        printf("     peor caso: %lu Hz -> %.1f dB de rechazo de imagen\n",
               peor_f, peor_irr);
    }
    printf("  2. el divisor cabe en el ARR de 16 bits %s",
           no_cabe == 0 ? "todas   ok\n" : "FALLA   MAL\n");
    printf("  3. el error no pasa de un escalon       %s",
           lejos == 0 ? "todas   ok\n" : "FALLA   MAL\n");
    printf("     el mayor error de todo el barrido: %+.0f Hz, en %lu Hz\n",
           peor_err, peor_err_f);
    printf("\n  Y la cuadratura, con el periodo par, no es \"buena\": es\n");
    printf("  EXACTA por construccion. No hay error de fase que medir.\n");

    {
        int fallos = (impares != 0) + (no_cabe != 0) + (lejos != 0);
        printf("\n%s (%d fallos)\n",
               fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
        return fallos ? 1 : 0;
    }
}
