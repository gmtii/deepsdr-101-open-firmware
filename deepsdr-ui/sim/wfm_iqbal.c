/*
 * ======================================================================
 * EL CORRECTOR DE ESPEJO EN FM ANCHA - 26/10/2026
 * ======================================================================
 *
 * Ningun banco del arbol medía esto: sim/wfm_espejo.c mide el espejo del
 * HARDWARE sobre el audio de FM ancha y no llama a iqbal_bloque(), y
 * sim/iqbaltest.c no tiene ni un caso a 192 kHz ni de FM ancha. En el
 * hueco que dejaban los dos vivia un fallo gordo.
 *
 * EL FALLO: el estimador plano se apoya en que E[x^2] tiende a cero con
 * x = I + jQ, y eso solo pasa si lo recibido esta DESPLAZADO del
 * oscilador. En FM ancha no hay mezclador -la portadora vive en continua-
 * asi que E[x^2] = A^2 * E[e^{j2*fase}], que lo fija la modulacion. El
 * corrector deja de corregir y se pone a INVENTARSE un espejo. Y lo peor:
 * se lo lleva a onda corta, porque las medias tienen 10,9 s de memoria.
 *
 * LO QUE SE EXIGE:
 *   - que en FM ancha el estimador NO aprenda;
 *   - que lo aprendido en onda corta sobreviva intacto a un paseo por la
 *     FM comercial;
 *   - y que en onda corta siga aprendiendo igual que siempre.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "iqbal.h"

#define N 256

static unsigned long g_sem = 31415UL;
static double ruido(void)
{
    double u1,u2;
    g_sem=g_sem*1103515245UL+12345UL; u1=((g_sem>>16)&0x7FFFU)/32768.0+1e-9;
    g_sem=g_sem*1103515245UL+12345UL; u2=((g_sem>>16)&0x7FFFU)/32768.0;
    return sqrt(-2.0*log(u1))*cos(2.0*M_PI*u2);
}

/* El desequilibrio del hardware: ganancia y fase. */
static double g_eg = 1.0, g_psi = 0.0;
static void hw(double i, double q, double *oi, double *oq)
{
    *oi = i;
    *oq = g_eg * (q * cos(g_psi) + i * sin(g_psi));
}

/* Un bloque de FM ancha: portadora en continua, modulada. */
static double g_fi, g_t;
static void bloque_wfm(int16_t *b, double amp, double desv, double sigma)
{
    uint32_t n;
    for (n = 0; n < N; n++) {
        double f = desv*cos(2.0*M_PI*1000.0*(g_t/192000.0))
                 + 0.09*desv*cos(2.0*M_PI*19000.0*(g_t/192000.0));
        double i,q,oi,oq;
        g_fi += 2.0*M_PI*f/192000.0;
        if (g_fi >  M_PI) { g_fi -= 2.0*M_PI; }
        if (g_fi < -M_PI) { g_fi += 2.0*M_PI; }
        g_t += 1.0;
        hw(amp*cos(g_fi), amp*sin(g_fi), &oi, &oq);
        oi += sigma*ruido(); oq += sigma*ruido();
        b[2*n]   = (int16_t)lrint(oi > 32767.0 ? 32767.0 : (oi < -32768.0 ? -32768.0 : oi));
        b[2*n+1] = (int16_t)lrint(oq > 32767.0 ? 32767.0 : (oq < -32768.0 ? -32768.0 : oq));
    }
}

/* Un bloque de onda corta: varias emisoras DESPLAZADAS y no simetricas. */
static double g_fase[5];
static void bloque_hf(int16_t *b, double sigma)
{
    static const double k_off[5] = { 11300.0, -7900.0, 23100.0, -31700.0, 5300.0 };
    static const double k_amp[5] = {  4000.0,  2500.0,  1800.0,   900.0,  3200.0 };
    uint32_t n; int e;
    for (n = 0; n < N; n++) {
        double i = 0.0, q = 0.0, oi, oq;
        for (e = 0; e < 5; e++) {
            g_fase[e] += 2.0*M_PI*k_off[e]/96000.0;
            if (g_fase[e] >  M_PI) { g_fase[e] -= 2.0*M_PI; }
            if (g_fase[e] < -M_PI) { g_fase[e] += 2.0*M_PI; }
            i += k_amp[e]*cos(g_fase[e]);
            q += k_amp[e]*sin(g_fase[e]);
        }
        hw(i, q, &oi, &oq);
        oi += sigma*ruido(); oq += sigma*ruido();
        b[2*n]   = (int16_t)lrint(oi > 32767.0 ? 32767.0 : (oi < -32768.0 ? -32768.0 : oi));
        b[2*n+1] = (int16_t)lrint(oq > 32767.0 ? 32767.0 : (oq < -32768.0 ? -32768.0 : oq));
    }
}

/*
 * EL RECHAZO QUE QUEDA, medido como lo que de verdad importa: se mete un
 * tono limpio DESPLAZADO, se le pasa el corrector tal y como esta ahora
 * mismo, y se mira cuanta energia aparece en la frecuencia espejo.
 */
static double rechazo_db(void)
{
    static int16_t b[2*1024];
    double off = 30000.0, fi = 0.0;
    double sr = 0.0, si = 0.0, er = 0.0, ei = 0.0;
    int n;
    for (n = 0; n < 1024; n++) {
        double i, q, oi, oq;
        fi += 2.0*M_PI*off/96000.0;
        if (fi > M_PI) { fi -= 2.0*M_PI; }
        i = 8000.0*cos(fi); q = 8000.0*sin(fi);
        hw(i, q, &oi, &oq);
        b[2*n]   = (int16_t)lrint(oi);
        b[2*n+1] = (int16_t)lrint(oq);
    }
    /* El corrector plano, aplicado a mano igual que iqbal_bloque(): asi se
     * mide lo que la radio tiene puesto AHORA, sin tocar su estado. */
    {
        float th = iqbal_theta(), g = iqbal_g();
        for (n = 0; n < 1024; n++) {
            double I = b[2*n];
            double Q = g * ((double)b[2*n+1] - th * (double)b[2*n]);
            double a = 2.0*M_PI*off*n/96000.0;
            sr += I*cos(a) + Q*sin(a);  si += Q*cos(a) - I*sin(a);
            er += I*cos(a) - Q*sin(a);  ei += Q*cos(a) + I*sin(a);
        }
    }
    {
        double s = sqrt(sr*sr+si*si), e = sqrt(er*er+ei*ei);
        if (e < 1e-9) { e = 1e-9; }
        return 20.0*log10(s/e);
    }
}

int main(void)
{
    static int16_t b[2*N];
    int fallos = 0, k;
    double nativo, tras_hf, tras_fm, tras_hueco, vuelta;

    printf("== EL CORRECTOR DE ESPEJO EN FM ANCHA ==\n\n");

    /* Una radio con desequilibrio de VERDAD. */
    g_eg = 1.10; g_psi = 3.0*M_PI/180.0;
    iqbal_init();
    iqbal_set_on(1U);
    nativo = rechazo_db();
    printf("  el espejo que trae el hardware .............. %6.1f dB\n", nativo);

    printf("\n  -- (1) en onda corta aprende, que es su trabajo --\n");
    iqbal_aprende(1U);
    for (k = 0; k < 22500; k++) { bloque_hf(b, 30.0); iqbal_bloque(b, N); }
    tras_hf = rechazo_db();
    printf("    %-46s %6.1f dB   %s\n", "60 s de onda corta", tras_hf,
           (tras_hf > nativo + 20.0) ? "ok " : "*** MAL ***");
    if (tras_hf <= nativo + 20.0) { fallos++; }

    printf("\n  -- (2) y en FM ancha NO aprende: lo de HF sobrevive --\n");
    iqbal_aprende(0U);                       /* lo que hace apply_demod_mode() */
    g_fi = 0.0; g_t = 0.0;
    for (k = 0; k < 5625; k++) { bloque_wfm(b, 8000.0, 75000.0, 20.0); iqbal_bloque(b, N); }
    tras_fm = rechazo_db();
    printf("    %-46s %6.1f dB   %s\n", "15 s de musica en FM comercial", tras_fm,
           (fabs(tras_fm - tras_hf) < 0.5) ? "ok " : "*** MAL ***");
    if (fabs(tras_fm - tras_hf) >= 0.5) { fallos++; }

    for (k = 0; k < 2812; k++) { bloque_wfm(b, 8000.0, 6750.0, 20.0); iqbal_bloque(b, N); }
    tras_hueco = rechazo_db();
    printf("    %-46s %6.1f dB   %s\n", "+ 7,5 s de hueco de programa", tras_hueco,
           (fabs(tras_hueco - tras_hf) < 0.5) ? "ok " : "*** MAL ***");
    if (fabs(tras_hueco - tras_hf) >= 0.5) { fallos++; }

    printf("\n  -- (3) de vuelta en onda corta, sigue donde estaba --\n");
    iqbal_aprende(1U);
    vuelta = rechazo_db();
    printf("    %-46s %6.1f dB   %s\n", "nada mas volver", vuelta,
           (fabs(vuelta - tras_hf) < 0.5) ? "ok " : "*** MAL ***");
    if (fabs(vuelta - tras_hf) >= 0.5) { fallos++; }

    printf("\n  -- (4) y que el banco SABE FALLAR: con el aprendizaje puesto --\n");
    {
        double roto;
        iqbal_init(); iqbal_set_on(1U); iqbal_aprende(1U);
        for (k = 0; k < 22500; k++) { bloque_hf(b, 30.0); iqbal_bloque(b, N); }
        g_fi = 0.0; g_t = 0.0;
        for (k = 0; k < 5625; k++) { bloque_wfm(b, 8000.0, 75000.0, 20.0); iqbal_bloque(b, N); }
        for (k = 0; k < 2812; k++) { bloque_wfm(b, 8000.0, 6750.0, 20.0); iqbal_bloque(b, N); }
        roto = rechazo_db();
        printf("    %-46s %6.1f dB   %s\n",
               "dejandolo aprender en FM, se estropea", roto,
               (roto < tras_hf - 20.0) ? "ok " : "*** MAL ***");
        printf("      (pierde %.1f dB: eso es lo que esto arregla)\n", tras_hf - roto);
        if (roto >= tras_hf - 20.0) { fallos++; }
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}

/* El ancho no entra en este banco: ver iqbal.c, "el ancho sale del camino
 * de siempre". Un tapon vacio y nada mas. */
void iqa_set_on(uint8_t on);
void iqa_set_on(uint8_t on) { (void)on; }
