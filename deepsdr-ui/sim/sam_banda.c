/*
 * ======================================================================
 * SAM CON UNA SOLA BANDA - 11/10/2026
 * ======================================================================
 *
 * *** De un issue: se pedia que el detector sincrono de AM pudiera trabajar sobre una
 * sola banda lateral, para escapar de una interferencia. ***
 *
 * COMO SE MIDE, y es la unica forma que vale: se mete una portadora con UN
 * TONO DISTINTO EN CADA BANDA. Con las dos bandas salen los dos; eligiendo
 * una tiene que salir uno y desaparecer el otro.
 *
 * Un estimulo simetrico -el mismo tono arriba y abajo, que es como se
 * genera un AM normal- NO sirve para esto: las dos bandas llevan lo mismo,
 * asi que quedarse con una suena igual que quedarse con las dos y el banco
 * diria "bien" con el codigo roto. Es la misma trampa que la voz sin
 * huecos de sim/nr_curva.c: un estimulo que no distingue no mide.
 *
 * Y lo que se exige es el RECHAZO: cuantos dB cae el tono de la banda que
 * no se ha pedido. Que el tono bueno sobreviva se exige aparte, porque un
 * filtro que lo mate todo tambien "rechaza".
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define NBLK    SDR_RX_BLOCK_SAMPLES
#define F_PORT    50.0     /* la portadora, un poco fuera del centro */
static double F_SUP = 1000.0;   /* el tono de la banda de ARRIBA */
static double F_INF = 2000.0;   /* y el de la de ABAJO */
#define BLOQUES 1200
#define ASIENTO  900

static int fallos;
static double g_t, g_fs;

static void bloque(int16_t *raw, double amp)
{
    uint32_t n;
    for (n = 0; n < (uint32_t)NBLK; n++) {
        double t = g_t / g_fs;
        /* Portadora + una raya en cada banda, construidas en complejo:
         * la de arriba gira hacia un lado y la de abajo hacia el otro. */
        double wp = 2.0 * M_PI * F_PORT * t;
        double ws = 2.0 * M_PI * (F_PORT + F_SUP) * t;
        double wi = 2.0 * M_PI * (F_PORT - F_INF) * t;
        double i = amp * (cos(wp) + 0.5 * cos(ws) + 0.5 * cos(wi));
        double q = amp * (sin(wp) + 0.5 * sin(ws) + 0.5 * sin(wi));
        g_t += 1.0;
        if (i >  32767.0) { i =  32767.0; }
        if (i < -32768.0) { i = -32768.0; }
        if (q >  32767.0) { q =  32767.0; }
        if (q < -32768.0) { q = -32768.0; }
        raw[2U * n]      = (int16_t)lrint(i);
        raw[2U * n + 1U] = (int16_t)lrint(q);
    }
}

/* Amplitud de un tono concreto a la salida. */
static void mide(sam_banda_t banda, double *sup, double *inf)
{
    static int16_t raw[2 * NBLK];
    int b;
    double sc_s = 0, ss_s = 0, sc_i = 0, ss_i = 0, nm = 0;

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_SAM);
    demod_am_set_sam_banda(banda);
    cons_captura_tramas((uint32_t)NBLK);
    g_fs = (double)demod_am_get_active_fs_hz();
    g_t = 0.0;

    for (b = 0; b < BLOQUES; b++) {
        const int16_t *sal;
        uint32_t n;
        double t0 = g_t;

        bloque(raw, 6000.0);
        demod_am_process_raw(raw);
        if (b < ASIENTO) { continue; }

        sal = cons_audio();
        for (n = 0; n < (uint32_t)NBLK; n++) {
            double y = (double)sal[2U * n];
            double ts = (t0 + (double)n) / g_fs;
            double ws = 2.0 * M_PI * F_SUP * ts;
            double wi = 2.0 * M_PI * F_INF * ts;
            sc_s += y * cos(ws); ss_s += y * sin(ws);
            sc_i += y * cos(wi); ss_i += y * sin(wi);
            nm += 1.0;
        }
    }
    *sup = 2.0 * sqrt(sc_s * sc_s + ss_s * ss_s) / nm;
    *inf = 2.0 * sqrt(sc_i * sc_i + ss_i * ss_i) / nm;
}

/* Rechazo de la banda NO pedida, con el tono no deseado en `f` y el bueno
 * lejos. Devuelve dB (negativo = rechaza). */
static void rechazo(double f, double *sup, double *inf)
{
    double a_s, a_i, u_s, u_i, l_s, l_i;

    /* El no deseado abajo, el bueno arriba: mide el rechazo en SUPERIOR. */
    F_SUP = 1500.0; F_INF = f;
    mide(SAM_BANDA_AMBAS, &a_s, &a_i);
    mide(SAM_BANDA_USB,   &u_s, &u_i);
    *sup = 20.0 * log10((u_i + 1e-9) / (a_i + 1e-9));

    /* Y al reves para INFERIOR. */
    F_SUP = f; F_INF = 1500.0;
    mide(SAM_BANDA_AMBAS, &a_s, &a_i);
    mide(SAM_BANDA_LSB,   &l_s, &l_i);
    *inf = 20.0 * log10((l_s + 1e-9) / (a_s + 1e-9));
}

int main(void)
{
    static const double k_f[] = { 300.0, 600.0, 1000.0, 2000.0, 3000.0, 3800.0 };
    double a_s, a_i, u_s, u_i, l_s, l_i;
    unsigned i;

    printf("\n== SAM CON UNA SOLA BANDA ==\n\n");
    printf("  portadora con un tono de %.0f Hz en la banda de ARRIBA\n"
           "  y otro de %.0f Hz en la de ABAJO, por demod_am_process_raw()\n\n",
           F_SUP, F_INF);

    mide(SAM_BANDA_AMBAS, &a_s, &a_i);
    mide(SAM_BANDA_USB,   &u_s, &u_i);
    mide(SAM_BANDA_LSB,   &l_s, &l_i);

    printf("    banda        tono de arriba   tono de abajo\n");
    printf("    ambas      %13.0f %15.0f\n", a_s, a_i);
    printf("    superior   %13.0f %15.0f\n", u_s, u_i);
    printf("    inferior   %13.0f %15.0f\n", l_s, l_i);
    printf("\n");

    if (a_s < 1.0 || a_i < 1.0) {
        printf("  MAL: con las dos bandas no salen los dos tonos: el banco no mide nada\n");
        printf("\nHAY FALLOS (1)\n\n");
        return 1;
    }

    /*
     * QUE EL TONO PEDIDO SOBREVIVA, y de hecho SUBE 6 dB: el metodo de
     * fases suma coherentemente las dos mitades de la banda buena. Se
     * comprueba aparte del rechazo porque un filtro que lo mate todo
     * tambien "rechaza".
     */
    printf("    el tono que SI se ha pedido:\n");
    printf("      en superior, el de arriba  %+6.1f dB\n",
           20.0 * log10((u_s + 1e-9) / (a_s + 1e-9)));
    printf("      en inferior, el de abajo   %+6.1f dB\n\n",
           20.0 * log10((l_i + 1e-9) / (a_i + 1e-9)));
    if (20.0 * log10((u_s + 1e-9) / (a_s + 1e-9)) < -4.0) {
        printf("  MAL: en superior tambien se come el tono de arriba\n"); fallos++;
    }
    if (20.0 * log10((l_i + 1e-9) / (a_i + 1e-9)) < -4.0) {
        printf("  MAL: en inferior tambien se come el tono de abajo\n"); fallos++;
    }

    /*
     * Y EL RECHAZO A LO LARGO DE LA BANDA, que es el numero que decide si
     * esto sirve para lo que lo pidieron. No es plano: el Hilbert de esta
     * radio esta disenado para cubrir desde ~300 Hz, asi que ahi abajo es
     * donde peor rechaza. Se ensena entero en vez de dar un solo numero,
     * porque un solo numero escondería justo el extremo malo.
     */
    printf("    rechazo de la banda NO pedida, a lo largo del audio:\n");
    printf("      tono     superior   inferior\n");
    for (i = 0; i < sizeof k_f / sizeof k_f[0]; i++) {
        double rs, ri, tope;
        rechazo(k_f[i], &rs, &ri);
        /* 20 dB de 600 Hz arriba; a 300 Hz se le consienten 15, que es
         * donde el Hilbert empieza a quedarse sin banda. Los numeros son
         * los MEDIDOS menos un par de dB de margen, no un deseo. */
        tope = (k_f[i] < 500.0) ? -15.0 : -20.0;
        printf("      %5.0f Hz %8.1f dB %9.1f dB%s\n", k_f[i], rs, ri,
               (rs > tope || ri > tope) ? "   <-- flojo" : "");
        if (rs > tope) { printf("  MAL: rechazo flojo en superior a %.0f Hz\n", k_f[i]); fallos++; }
        if (ri > tope) { printf("  MAL: rechazo flojo en inferior a %.0f Hz\n", k_f[i]); fallos++; }
    }
    printf("\n      Con 20 dB, una emisora que pise tan fuerte como la tuya\n"
           "      se queda diez veces por debajo. Para eso se pidio.\n\n");

    printf("%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
