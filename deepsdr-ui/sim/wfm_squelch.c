/*
 * ======================================================================
 * EL SILENCIADOR DE FM ANCHA - 26/10/2026
 * ======================================================================
 *
 * Mide el numero del que vive: el ruido de ARRIBA del multiplex, entre los
 * 70 kHz del codo y los 96 kHz de Nyquist, que es donde una emisora no
 * pone nada y un canal vacio lo pone todo.
 *
 * LO QUE SE EXIGE:
 *   - que el canal vacio quede MUY por encima de cualquier emisora, para
 *     que la raya tenga sitio donde caer;
 *   - que la lectura NO dependa del nivel de la emisora, que es la razon
 *     entera de no medir por nivel como hace el de AM;
 *   - que una emisora con el composite entero -piloto, estereo y RDS, que
 *     llegan hasta los 60 kHz- siga leyendose como emisora;
 *   - y que con el mando en su suelo no cierre NUNCA.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define FS   192000.0
#define NBLK SDR_RX_BLOCK_SAMPLES_WFM

static double g_fi, g_t;
static unsigned long g_sem = 2026UL;
static double ruido(void)
{
    double u1,u2;
    g_sem=g_sem*1103515245UL+12345UL; u1=((g_sem>>16)&0x7FFFU)/32768.0+1e-9;
    g_sem=g_sem*1103515245UL+12345UL; u2=((g_sem>>16)&0x7FFFU)/32768.0;
    return sqrt(-2.0*log(u1))*cos(2.0*M_PI*u2);
}

/* Un bloque de emisora con el composite que se quiera. */
static void bloque(int16_t *raw, double amp, double desv, double fa,
                   double piloto, double estereo, double rds, double sigma)
{
    uint32_t n;
    for (n = 0; n < (uint32_t)NBLK; n++) {
        double t = g_t / FS;
        double f = desv*cos(2.0*M_PI*fa*t)
                 + piloto*cos(2.0*M_PI*19000.0*t)
                 + estereo*cos(2.0*M_PI*38000.0*t)*cos(2.0*M_PI*700.0*t)
                 + rds*cos(2.0*M_PI*57000.0*t)*((((int)(t*1187.5))&1)?1.0:-1.0);
        double i,q;
        g_fi += 2.0*M_PI*f/FS;
        if (g_fi >  M_PI) { g_fi -= 2.0*M_PI; }
        if (g_fi < -M_PI) { g_fi += 2.0*M_PI; }
        g_t += 1.0;
        i = amp*cos(g_fi) + sigma*ruido();
        q = amp*sin(g_fi) + sigma*ruido();
        if (i >  32767.0) { i =  32767.0; }
        if (i < -32768.0) { i = -32768.0; }
        if (q >  32767.0) { q =  32767.0; }
        if (q < -32768.0) { q = -32768.0; }
        raw[2U*n] = (int16_t)lrint(i);
        raw[2U*n+1U] = (int16_t)lrint(q);
    }
}

static void arranca(float umbral_db)
{
    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    demod_wfm_reset_diag();
    demod_am_set_audio_bw(AUDIO_BW_4K0);
    demod_am_set_wfm_ifbw(WFM_IFBW_WIDE);
    demod_am_set_agc_profile(AGC_PROFILE_MEDIUM);
    demod_am_set_wfm_sql_db(umbral_db);
    cons_captura_tramas((uint32_t)NBLK);
    g_fi = 0.0; g_t = 0.0; g_sem = 2026UL;
}

/* Corre y devuelve el ruido en dB; deja en *abierto si quedo abierto y en
 * *rms el nivel del audio que SALE. */
static double corre(float umbral_db, double amp, double desv, double sigma,
                    int composite, int *abierto, double *rms)
{
    static int16_t raw[2*NBLK];
    int b, k;
    double e = 0.0;
    uint32_t tom = 0U;

    arranca(umbral_db);
    for (b = 0; b < 400; b++) {
        bloque(raw, amp, desv, 1000.0,
               composite ? 7500.0  : 0.0,
               composite ? 20000.0 : 0.0,
               composite ? 2000.0  : 0.0, sigma);
        demod_wfm_process_raw(raw);
    }
    for (b = 0; b < 40; b++) {
        const int16_t *s;
        bloque(raw, amp, desv, 1000.0,
               composite ? 7500.0  : 0.0,
               composite ? 20000.0 : 0.0,
               composite ? 2000.0  : 0.0, sigma);
        demod_wfm_process_raw(raw);
        s = cons_audio();
        for (k = 0; k < NBLK; k++) { e += (double)s[2*k]*s[2*k]; tom++; }
    }
    *abierto = (int)demod_wfm_get_sql_abierto();
    *rms = sqrt(e / (tom ? tom : 1U));
    return (double)demod_wfm_get_sql_db();
}

int main(void)
{
    int fallos = 0;
    int ab; double rms, db;
    double vacio = 0.0, peor_emisora = 0.0;

    printf("== EL SILENCIADOR DE FM ANCHA ==\n");
    printf("   ruido de ARRIBA del multiplex (70..96 kHz), en dB\n");
    printf("   (las tres primeras tablas, con el mando en 1 dB: mide pero no cierra)\n\n");

    printf("  -- (1) el numero NO depende del nivel de la emisora --\n");
    printf("   portadora   composite   ruido   dB de arriba   audio\n");
    {
        static const double k_a[] = { 500.0, 1500.0, 4000.0, 12000.0, 30000.0 };
        unsigned j;
        for (j = 0; j < sizeof k_a/sizeof k_a[0]; j++) {
            db = corre(1.0f, k_a[j], 20000.0, 0.0, 1, &ab, &rms);
            if (db > peor_emisora) { peor_emisora = db; }
            printf("   %9.0f       si        0%%   %8.1f    %8.0f\n",
                   k_a[j], db, rms);
        }
    }

    printf("\n  -- (2) y con ruido de recepcion encima --\n");
    printf("   portadora   ruido/port   dB de arriba   audio\n");
    {
        static const double k_r[] = { 0.0, 0.02, 0.05, 0.10, 0.20 };
        unsigned j;
        for (j = 0; j < sizeof k_r/sizeof k_r[0]; j++) {
            db = corre(1.0f, 4000.0, 20000.0, k_r[j]*4000.0, 1, &ab, &rms);
            printf("        4000       %4.0f%%     %8.1f    %8.0f\n",
                   k_r[j]*100.0, db, rms);
            if (k_r[j] <= 0.10 && db > peor_emisora) { peor_emisora = db; }
        }
    }

    printf("\n  -- (3) EL CANAL VACIO: solo ruido, sin portadora --\n");
    printf("      sigma   dB de arriba   audio\n");
    {
        static const double k_s[] = { 200.0, 900.0, 3000.0 };
        unsigned j;
        for (j = 0; j < sizeof k_s/sizeof k_s[0]; j++) {
            db = corre(1.0f, 0.0, 0.0, k_s[j], 0, &ab, &rms);
            if (vacio == 0.0 || db < vacio) { vacio = db; }
            printf("   %8.0f     %8.1f    %8.0f\n", k_s[j], db, rms);
        }
    }

    printf("\n  -- (4) el margen que queda para poner la raya --\n");
    printf("    %-46s %6.1f dB\n", "la peor emisora llega a", peor_emisora);
    printf("    %-46s %6.1f dB\n", "el canal vacio mas flojo", vacio);
    printf("    %-46s %6.1f dB   %s\n", "margen entre las dos",
           vacio - peor_emisora,
           (vacio - peor_emisora > 12.0) ? "ok " : "*** MAL ***");
    if (vacio - peor_emisora <= 12.0) { fallos++; }

    printf("\n  -- (5) con el mando en su suelo NO cierra nunca --\n");
    db = corre(0.0f, 0.0, 0.0, 3000.0, 0, &ab, &rms);
    printf("    %-46s %6s   %s\n", "canal vacio y mando al suelo: abierto",
           ab ? "si" : "no", ab ? "ok " : "*** MAL ***");
    if (!ab) { fallos++; }

    printf("\n  -- (6) y con el mando puesto, cierra el vacio y no la emisora --\n");
    {
        float u = (float)(75.0 - (vacio + peor_emisora) * 0.5);
        printf("      (mando a %.0f dB, que pone la raya a medio camino)\n", u);
        db = corre(u, 0.0, 0.0, 3000.0, 0, &ab, &rms);
        printf("    %-46s %6s   %s\n", "el canal vacio se calla",
               ab ? "abierto" : "callado", (!ab) ? "ok " : "*** MAL ***");
        printf("      ruido %.1f dB, audio que sale %.0f\n", db, rms);
        if (ab) { fallos++; }

        db = corre(u, 1500.0, 20000.0, 0.0, 1, &ab, &rms);
        printf("    %-46s %6s   %s\n", "la emisora FLOJA sigue oyendose",
               ab ? "abierto" : "callado", ab ? "ok " : "*** MAL ***");
        printf("      ruido %.1f dB, audio que sale %.0f\n", db, rms);
        if (!ab) { fallos++; }

        db = corre(u, 30000.0, 20000.0, 0.0, 1, &ab, &rms);
        printf("    %-46s %6s   %s\n", "y la LOCAL tambien",
               ab ? "abierto" : "callado", ab ? "ok " : "*** MAL ***");
        if (!ab) { fallos++; }
    }

    /*
     * (7) LOS DOS SILENCIADORES SON DOS NUMEROS, Y NO SE PISAN.
     *
     * Esto nace de un fallo de verdad del 26/10/2026: el de FM ancha se
     * colgo del mismo ajuste que el de AM, y como las dos escalas no se
     * solapan, un umbral normal de onda media (40 dB) dejaba la FM
     * comercial MUDA hasta en una local, con el medidor lleno. Y se
     * guardaba en CONFIG.CSV, asi que sobrevivia al apagado.
     *
     * Aqui se fija que son independientes en los dos sentidos.
     */
    printf("\n  -- (7) los dos silenciadores no se pisan --\n");
    {
        float g;

        demod_am_set_squelch_db(40.0f);          /* lo normal en onda media */
        demod_am_set_wfm_sql_db(0.0f);           /* y el de FM, apagado */
        g = demod_am_get_wfm_sql_db();
        printf("    %-46s %6.0f   %s\n",
               "el umbral de AM no mueve el de FM", (double)g,
               (g == 0.0f) ? "ok " : "*** MAL ***");
        if (g != 0.0f) { fallos++; }

        demod_am_set_wfm_sql_db(12.0f);
        g = demod_am_get_squelch_db();
        printf("    %-46s %6.0f   %s\n",
               "y el de FM no mueve el de AM", (double)g,
               (g == 40.0f) ? "ok " : "*** MAL ***");
        if (g != 40.0f) { fallos++; }

        /* Y con el de AM en 40, que es donde mataba la FM, la local sigue
         * sonando. */
        demod_am_set_squelch_db(40.0f);
        db = corre(0.0f, 30000.0, 20000.0, 0.0, 1, &ab, &rms);
        printf("    %-46s %6s   %s\n",
               "con el de AM en 40 dB, la local SUENA",
               ab ? "abierto" : "callado", ab ? "ok " : "*** MAL ***");
        if (!ab) { fallos++; }
        printf("      audio que sale %.0f\n", rms);

        /* Y el recorte: fuera del rango util no se puede poner. */
        demod_am_set_wfm_sql_db(999.0f);
        g = demod_am_get_wfm_sql_db();
        printf("    %-46s %6.0f   %s\n",
               "el umbral de FM se recorta a su techo", (double)g,
               (g == demod_am_wfm_sql_max()) ? "ok " : "*** MAL ***");
        if (g != demod_am_wfm_sql_max()) { fallos++; }
        demod_am_set_wfm_sql_db(-999.0f);
        g = demod_am_get_wfm_sql_db();
        printf("    %-46s %6.0f   %s\n",
               "y a su suelo", (double)g,
               (g == demod_am_wfm_sql_min()) ? "ok " : "*** MAL ***");
        if (g != demod_am_wfm_sql_min()) { fallos++; }
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
