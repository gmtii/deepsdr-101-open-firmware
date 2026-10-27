/*
 * BANCO DE LA REVISION DEL 26/10/2026 - comprueba dos cosas de los
 * cambios de ese dia:
 *
 *   (A) demod_wfm_get_dc_crudo() dice ser "la estimacion SIN topar".
 *       Aqui se le mete una fuga conocida y se compara lo que devuelve
 *       con la fuga de verdad.
 *
 *   (B) El caso que ningun banco cubre: beta = 0, o sea portadora SIN
 *       MODULAR (silencio entre programas, mono sin piloto, portadora
 *       de prueba). wfm_dc.c solo baja a beta = 0,2.
 *
 * Compilar con -DWFM_DC_TOPE=512 para comparar con el tope viejo.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define FS       192000.0
#define NBLK     SDR_RX_BLOCK_SAMPLES_WFM
#define F_AUDIO  1000.0
#ifndef WFM_DC_TOPE
#define WFM_DC_TOPE 2048
#endif

static double g_fi, g_t;

static void bloque(int16_t *raw, double amp, double dc, double desv)
{
    uint32_t n;
    for (n = 0; n < (uint32_t)NBLK; n++) {
        double f = (desv != 0.0)
                 ? desv * sin(2.0 * M_PI * F_AUDIO * g_t / FS) : 0.0;
        double i = amp * cos(g_fi) + dc;
        double q = amp * sin(g_fi);
        g_fi += 2.0 * M_PI * f / FS;
        if (g_fi >  M_PI) { g_fi -= 2.0 * M_PI; }
        if (g_fi < -M_PI) { g_fi += 2.0 * M_PI; }
        g_t += 1.0;
        if (i >  32767.0) { i =  32767.0; }
        if (i < -32768.0) { i = -32768.0; }
        if (q >  32767.0) { q =  32767.0; }
        if (q < -32768.0) { q = -32768.0; }
        raw[2U * n]      = (int16_t)lrint(i);
        raw[2U * n + 1U] = (int16_t)lrint(q);
    }
}

static void arranca(void)
{
    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    demod_wfm_reset_diag();
    cons_captura_tramas((uint32_t)NBLK);
    g_fi = 0.0; g_t = 0.0;
}

/* ---------------------------------------------------------------- (A) */
static void parte_a(void)
{
    static const double k_fuga[] = { 300.0, 1000.0, 2000.0, 3000.0,
                                     6000.0, 10000.0 };
    static int16_t raw[2 * NBLK];
    int k, b;

    printf("\n== (A) LO QUE DICE demod_wfm_get_dc_crudo() ==\n\n");
    printf("   tope compilado = %d\n\n", (int)WFM_DC_TOPE);
    printf("   fuga de verdad   'mide' (crudo)   'quita' (topado)   error de 'mide'\n");
    for (k = 0; k < (int)(sizeof k_fuga / sizeof k_fuga[0]); k++) {
        float ci = 0.f, cq = 0.f, qi = 0.f, qq = 0.f;
        double mide, quita;

        arranca();
        for (b = 0; b < 1200; b++) {           /* >3 s, de sobra */
            bloque(raw, 4000.0, k_fuga[k], 50000.0);
            demod_wfm_process_raw(raw);
        }
        demod_wfm_get_dc_crudo(&ci, &cq);
        demod_wfm_get_dc(&qi, &qq);
        mide  = sqrt((double)ci * ci + (double)cq * cq);
        quita = sqrt((double)qi * qi + (double)qq * qq);
        printf("   %14.0f   %13.1f   %16.1f   %13.1f%%\n",
               k_fuga[k], mide, quita,
               100.0 * (mide - k_fuga[k]) / k_fuga[k]);
    }
    printf("\n   Si 'mide' no se despega de 'quita', el renglon de\n"
           "   Informacion no puede decir que el tope este actuando.\n");
}

/* ---------------------------------------------------------------- (B) */
/* Portadora SIN MODULAR: cuanto se le come el quitador, que se oye
 * mientras, y que sale cuando la modulacion vuelve. */
static void recupera(double amp, double *rms_mudo, double *tono, double *basura)
{
    static int16_t raw[2 * NBLK];
    int b;
    double e_mudo = 0.0, n_mudo = 0.0;
    double sc = 0.0, ss = 0.0, e_tot = 0.0, nm = 0.0;

    arranca();
    /* 1,5 s de portadora quieta - menos que un silencio entre programas.
     * El silenciador de entrada son 200 bloques: se saltan. */
    for (b = 0; b < 560; b++) {
        bloque(raw, amp, 0.0, 0.0);
        demod_wfm_process_raw(raw);
        if (b >= 300) {
            const int16_t *sal = cons_audio(); uint32_t n;
            for (n = 0; n < (uint32_t)NBLK; n++) {
                double y = (double)sal[2U * n];
                e_mudo += y * y; n_mudo += 1.0;
            }
        }
    }
    *rms_mudo = sqrt(e_mudo / n_mudo);
    /* y vuelve la modulacion: los primeros 125 ms */
    for (b = 0; b < 47; b++) {
        const int16_t *sal; uint32_t n; double t0 = g_t;
        bloque(raw, amp, 0.0, 50000.0);
        demod_wfm_process_raw(raw);
        sal = cons_audio();
        for (n = 0; n < (uint32_t)NBLK; n++) {
            double y = (double)sal[2U * n];
            double w = 2.0 * M_PI * F_AUDIO * (t0 + (double)n) / FS;
            sc += y * cos(w); ss += y * sin(w);
            e_tot += y * y; nm += 1.0;
        }
    }
    {
        double e_tono = 2.0 * (sc * sc + ss * ss) / nm;
        double e_res = e_tot - e_tono;
        if (e_res < 0.0) { e_res = 0.0; }
        *tono = sqrt(e_tono / nm);
        *basura = (e_tot > 0.0) ? 100.0 * e_res / e_tot : -1.0;
    }
}

static void parte_b(void)
{
    static int16_t raw[2 * NBLK];
    static const double k_amp[] = { 800.0, 1000.0, 1500.0, 2000.0,
                                    3000.0, 4000.0, 8000.0 };
    int k, b;

    printf("\n== (B) PORTADORA SIN MODULAR (beta = 0), offset CERO ==\n\n");
    printf("   tope compilado = %d\n\n", (int)WFM_DC_TOPE);
    printf("   portadora   le resta   %% de la portadora   ruido en el mudo   "
           "tono luego   basura\n");
    for (k = 0; k < (int)(sizeof k_amp / sizeof k_amp[0]); k++) {
        float qi = 0.f, qq = 0.f;
        double quita, rms, tono, basura;

        arranca();
        for (b = 0; b < 560; b++) {
            bloque(raw, k_amp[k], 0.0, 0.0);
            demod_wfm_process_raw(raw);
        }
        demod_wfm_get_dc(&qi, &qq);
        quita = sqrt((double)qi * qi + (double)qq * qq);
        recupera(k_amp[k], &rms, &tono, &basura);
        printf("   %9.0f   %8.1f   %17.1f%%   %16.0f   %10.0f   %5.1f%%\n",
               k_amp[k], quita, 100.0 * quita / k_amp[k], rms, tono, basura);
    }
}

int main(void)
{
    parte_a();
    parte_b();
    printf("\n");
    return 0;
}
