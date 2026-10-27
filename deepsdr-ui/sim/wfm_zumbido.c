/*
 * ======================================================================
 * EL ZUMBIDO DE LA FM ANCHA, MEDIDO - 26/10/2026
 * ======================================================================
 *
 * Mete por demod_wfm_process_raw() -la entrada de verdad, muestras crudas
 * de 16 bits I/Q entrelazadas, igual que el codec- una emisora de FM
 * comercial de mentira pero completa: audio, piloto de 19 kHz,
 * subportadora de estereo en 38 y datos en 57, con preenfasis como lo
 * aplica un transmisor de verdad. Lee el audio por donde sale el audio
 * (gd32_i2s_stream_write_half) y le hace un espectro.
 *
 * Lo que se busca: de donde sale el "grrrgrgrgrg".
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define FS       192000.0
#define NBLK     SDR_RX_BLOCK_SAMPLES_WFM       /* 512 */
#define NFFT     16384                          /* 32 bloques */

/* ---------------------------------------------------------------- FFT */
static double re_[NFFT], im_[NFFT];

static void fft(void)
{
    int i, j, len;
    for (i = 1, j = 0; i < NFFT; i++) {
        int bit = NFFT >> 1;
        for (; j & bit; bit >>= 1) { j ^= bit; }
        j ^= bit;
        if (i < j) {
            double t;
            t = re_[i]; re_[i] = re_[j]; re_[j] = t;
            t = im_[i]; im_[i] = im_[j]; im_[j] = t;
        }
    }
    for (len = 2; len <= NFFT; len <<= 1) {
        double ang = -2.0 * M_PI / len;
        double wr = cos(ang), wi = sin(ang);
        for (i = 0; i < NFFT; i += len) {
            double cr = 1.0, ci = 0.0;
            for (j = 0; j < len / 2; j++) {
                double ur = re_[i + j],            ui = im_[i + j];
                double vr = re_[i + j + len/2] * cr - im_[i + j + len/2] * ci;
                double vi = re_[i + j + len/2] * ci + im_[i + j + len/2] * cr;
                double nr;
                re_[i + j] = ur + vr; im_[i + j] = ui + vi;
                re_[i + j + len/2] = ur - vr; im_[i + j + len/2] = ui - vi;
                nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

/* -------------------------------------------------- el transmisor ---- */
typedef struct {
    double amp;        /* amplitud de la portadora en cuentas del ADC */
    double dc;         /* offset de continua en I */
    double f_audio;    /* tono mono */
    double desv_mono;  /* desviacion de pico del mono, Hz */
    double piloto;     /* desviacion del piloto de 19 kHz, Hz (0 = mono puro) */
    double estereo;    /* desviacion de la subportadora de 38 kHz */
    double rds;        /* desviacion de los datos de 57 kHz */
    double df_lo;      /* error de sintonia del oscilador, Hz */
    int    preenf;     /* 1 = preenfasis de 50 us en el audio */
    double ruido;      /* ruido gaussiano en cuentas, rms por eje */
} emisora_t;

static double g_fi, g_t;
static unsigned g_rnd = 12345u;

static double gauss(void)
{
    double u1, u2;
    g_rnd = g_rnd * 1103515245u + 12345u;
    u1 = ((g_rnd >> 8) & 0xFFFFFFu) / (double)0x1000000u + 1e-12;
    g_rnd = g_rnd * 1103515245u + 12345u;
    u2 = ((g_rnd >> 8) & 0xFFFFFFu) / (double)0x1000000u;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

static void bloque(int16_t *raw, const emisora_t *e)
{
    uint32_t n;
    for (n = 0; n < (uint32_t)NBLK; n++) {
        double t = g_t / FS;
        double m = cos(2.0 * M_PI * e->f_audio * t);
        double f;
        double i, q;

        if (e->preenf) {
            /* Preenfasis de 50 us: |H| = sqrt(1+(2 pi f tau)^2). Con un
             * tono puro basta escalar y desfasar. */
            double w = 2.0 * M_PI * e->f_audio * 50e-6;
            double g = sqrt(1.0 + w * w);
            m = cos(2.0 * M_PI * e->f_audio * t + atan(w)) * g / sqrt(1.0 + w * w) * g;
            m /= g;   /* se normaliza al pico para no pasarse de desviacion */
            m *= g;
            m /= sqrt(1.0 + w * w);
        }

        f  = e->desv_mono * m;
        f += e->piloto  * cos(2.0 * M_PI * 19000.0 * t);
        /* L-R en 38 kHz, modulada por otro tono para que no sea el mismo */
        f += e->estereo * cos(2.0 * M_PI * 38000.0 * t) * cos(2.0 * M_PI * 700.0 * t);
        f += e->rds     * cos(2.0 * M_PI * 57000.0 * t);
        f += e->df_lo;

        g_fi += 2.0 * M_PI * f / FS;
        if (g_fi >  M_PI) { g_fi -= 2.0 * M_PI; }
        if (g_fi < -M_PI) { g_fi += 2.0 * M_PI; }
        g_t += 1.0;

        i = e->amp * cos(g_fi) + e->dc;
        q = e->amp * sin(g_fi);
        if (e->ruido > 0.0) { i += e->ruido * gauss(); q += e->ruido * gauss(); }
        if (i >  32767.0) { i =  32767.0; }
        if (i < -32768.0) { i = -32768.0; }
        if (q >  32767.0) { q =  32767.0; }
        if (q < -32768.0) { q = -32768.0; }
        raw[2U * n]      = (int16_t)lrint(i);
        raw[2U * n + 1U] = (int16_t)lrint(q);
    }
}

/* ---------------------------------------------------- la medida ------ */
static float g_audio[NFFT];
static double g_mag[NFFT / 2 + 1];

/* Corre `calienta` bloques para que todo converja y luego captura NFFT
 * muestras seguidas. Deja el espectro en g_mag (amplitud, no potencia). */
static void corre(const emisora_t *e, int calienta, audio_bw_t bw,
                  wfm_ifbw_t ifbw, agc_profile_t agc)
{
    static int16_t raw[2 * NBLK];
    int b, k;
    int tomadas = 0;

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    demod_am_set_audio_bw(bw);
    demod_am_set_wfm_ifbw(ifbw);
    demod_am_set_agc_profile(agc);
    cons_captura_tramas((uint32_t)NBLK);
    g_fi = 0.0; g_t = 0.0; g_rnd = 12345u;

    for (b = 0; b < calienta + NFFT / NBLK + 2; b++) {
        const int16_t *sal;
        bloque(raw, e);
        demod_wfm_process_raw(raw);
        if (b < calienta) { continue; }
        if (tomadas >= NFFT) { continue; }
        sal = cons_audio();
        for (k = 0; k < NBLK && tomadas < NFFT; k++) {
            g_audio[tomadas++] = (float)sal[2 * k];
        }
    }

    for (k = 0; k < NFFT; k++) {
        /* Hann, que es lo que deja ver un armonico a -60 dB al lado de un
         * tono grande sin que la fuga lo tape. */
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * k / NFFT);
        re_[k] = g_audio[k] * w;
        im_[k] = 0.0;
    }
    fft();
    for (k = 0; k <= NFFT / 2; k++) {
        g_mag[k] = sqrt(re_[k] * re_[k] + im_[k] * im_[k]) * 2.0 / (NFFT * 0.5);
    }
}

static int bin_de(double hz) { return (int)lrint(hz * NFFT / FS); }

/* Energia en el entorno de un bin (una campana de Hann ocupa 3). */
static double pico_en(double hz)
{
    int c = bin_de(hz), k;
    double s = 0.0;
    for (k = c - 2; k <= c + 2; k++) {
        if (k >= 0 && k <= NFFT / 2) { s += g_mag[k] * g_mag[k]; }
    }
    return sqrt(s);
}

static double db(double x, double ref)
{
    if (x <= 0.0 || ref <= 0.0) { return -999.0; }
    return 20.0 * log10(x / ref);
}

/* La energia TOTAL y la que cae en los multiplos de la tasa de bloque. */
static void comb_bloque(double *pct_comb, double *peor_hz, double *peor_db)
{
    double tasa = FS / (double)NBLK;      /* 375 Hz */
    double e_tot = 0.0, e_comb = 0.0, mx = 0.0;
    int k, h;
    double mxhz = 0.0;
    for (k = 1; k <= NFFT / 2; k++) { e_tot += g_mag[k] * g_mag[k]; }
    for (h = 1; h * tasa < FS / 2.0 && h <= 200; h++) {
        double hz = h * tasa;
        int c = bin_de(hz), j;
        double s = 0.0;
        for (j = c - 2; j <= c + 2; j++) {
            if (j >= 1 && j <= NFFT / 2) { s += g_mag[j] * g_mag[j]; }
        }
        e_comb += s;
        if (sqrt(s) > mx) { mx = sqrt(s); mxhz = hz; }
    }
    *pct_comb = (e_tot > 0.0) ? 100.0 * e_comb / e_tot : 0.0;
    *peor_hz = mxhz;
    *peor_db = mx;
}

static void informe(const char *rotulo, const emisora_t *e)
{
    double tono = pico_en(e->f_audio);
    double h2 = pico_en(2 * e->f_audio), h3 = pico_en(3 * e->f_audio);
    double h4 = pico_en(4 * e->f_audio), h5 = pico_en(5 * e->f_audio);
    double thd = sqrt(h2*h2 + h3*h3 + h4*h4 + h5*h5);
    double pil = pico_en(19000.0);
    double pct, phz, pdb;
    double e_tot = 0.0;
    int k;
    for (k = 1; k <= NFFT / 2; k++) { e_tot += g_mag[k] * g_mag[k]; }
    comb_bloque(&pct, &phz, &pdb);
    printf("  %-26s tono %7.0f  THD %6.2f%%  H3 %6.1f dBc  piloto %6.1f dBc"
           "  comb375 %5.1f%% (peor %5.0f Hz, %6.1f dBc)  basura %5.1f%%\n",
           rotulo, tono, 100.0 * thd / (tono > 0 ? tono : 1),
           db(h3, tono), db(pil, tono), pct, phz, db(pdb, tono),
           100.0 * (e_tot - tono * tono) / (e_tot > 0 ? e_tot : 1));
}

int main(int argc, char **argv)
{
    emisora_t e;
    (void)argc; (void)argv;

    printf("\n== DE DONDE SALE EL ZUMBIDO DE LA FM ANCHA ==\n");
    printf("   fs=%.0f  bloque=%d  tasa de bloque=%.0f Hz  FFT=%d\n\n",
           FS, (int)NBLK, FS / NBLK, NFFT);

    /* (1) El caso de libro: mono puro, portadora fuerte, sin ruido. */
    printf("  -- (1) mono puro, portadora fuerte, sin piloto ni ruido --\n");
    memset(&e, 0, sizeof e);
    e.amp = 12000.0; e.f_audio = 1000.0; e.desv_mono = 75000.0;
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
    informe("desv +-75k", &e);
    e.desv_mono = 40000.0;
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
    informe("desv +-40k", &e);
    e.desv_mono = 10000.0;
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
    informe("desv +-10k", &e);

    /* (2) Con el piloto de 19 kHz, que es lo que lleva CUALQUIER emisora
     * comercial, estereo o no. */
    printf("\n  -- (2) con piloto de 19 kHz (toda emisora comercial lo lleva) --\n");
    memset(&e, 0, sizeof e);
    e.amp = 12000.0; e.f_audio = 1000.0;
    e.desv_mono = 0.9 * 75000.0 * 0.45; e.piloto = 0.1 * 75000.0;
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
    informe("mono+piloto", &e);
    e.estereo = 0.9 * 75000.0 * 0.45;
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
    informe("+ estereo 38k", &e);
    e.rds = 0.04 * 75000.0;
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
    informe("+ RDS 57k", &e);

    /* (3) Los tres anchos del filtro de audio. */
    printf("\n  -- (3) los tres anchos del filtro de audio, con composite entero --\n");
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
    informe("ANCHO 15k", &e);
    corre(&e, 300, AUDIO_BW_2K3, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
    informe("NORMAL 8k", &e);
    corre(&e, 300, AUDIO_BW_1K8, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
    informe("ESTRECHO 4k", &e);
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_NARROW, AGC_PROFILE_MEDIUM);
    informe("ANCHO + IFBW estrecho", &e);

    /* (4) Los perfiles de AGC: si el lazo bombea a ritmo de bloque, el
     * comb de 375 Hz sube. */
    printf("\n  -- (4) los perfiles del automatico de ganancia --\n");
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MANUAL);
    informe("MANUAL", &e);
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_SLOW);
    informe("LENTO", &e);
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
    informe("MEDIO", &e);
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_FAST);
    informe("RAPIDO", &e);

    /* (5) Portadora floja y con ruido, que es como llega de verdad. */
    printf("\n  -- (5) portadora floja / con ruido --\n");
    e.amp = 2000.0;
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
    informe("amp 2000 sin ruido", &e);
    e.ruido = 100.0;
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
    informe("amp 2000 ruido 100", &e);
    e.amp = 12000.0; e.ruido = 600.0;
    corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
    informe("amp 12000 ruido 600", &e);

    /* (6) Error de sintonia: el oscilador no clavado en el centro. */
    printf("\n  -- (6) el oscilador fuera del centro --\n");
    memset(&e, 0, sizeof e);
    e.amp = 12000.0; e.f_audio = 1000.0;
    e.desv_mono = 0.9 * 75000.0 * 0.45; e.piloto = 0.1 * 75000.0;
    e.estereo = 0.9 * 75000.0 * 0.45;
    {
        static const double k_df[] = { 0.0, 5000.0, 20000.0, 50000.0 };
        int k;
        for (k = 0; k < 4; k++) {
            char r[64];
            e.df_lo = k_df[k];
            sprintf(r, "df_lo = %.0f Hz", k_df[k]);
            corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
            informe(r, &e);
        }
    }

    /* (7) EL RECORTE DEL CONVERTIDOR. En WFM apply_demod_mode() pone el
     * PGA AL TOPE (47,5 dB) a proposito -main.c, "pga: WFM, al tope por
     * perdida de VHF"-, asi que una emisora local fuerte entra con la I y
     * la Q contra los topes del entero de 16 bits. */
    printf("\n  -- (7) el convertidor recortando (PGA al tope en WFM) --\n");
    memset(&e, 0, sizeof e);
    e.amp = 12000.0; e.f_audio = 1000.0;
    e.desv_mono = 0.9 * 75000.0 * 0.45; e.piloto = 0.1 * 75000.0;
    e.estereo = 0.9 * 75000.0 * 0.45; e.rds = 0.04 * 75000.0;
    {
        static const double k_amp[] = { 12000.0, 30000.0, 33000.0, 40000.0,
                                        60000.0, 120000.0, 300000.0 };
        int k;
        for (k = 0; k < 7; k++) {
            char r[64];
            e.amp = k_amp[k];
            sprintf(r, "amp %.0f (%.1f%% del tope)", k_amp[k], 100.0*k_amp[k]/32767.0);
            corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_WIDE, AGC_PROFILE_MEDIUM);
            informe(r, &e);
        }
    }
    printf("\n     Y lo mismo con el prefiltro de FI ESTRECHO puesto:\n");
    {
        static const double k_amp[] = { 33000.0, 60000.0, 300000.0 };
        int k;
        for (k = 0; k < 3; k++) {
            char r[64];
            e.amp = k_amp[k];
            sprintf(r, "amp %.0f + IFBW estrecho", k_amp[k]);
            corre(&e, 300, AUDIO_BW_4K0, WFM_IFBW_NARROW, AGC_PROFILE_MEDIUM);
            informe(r, &e);
        }
    }

    printf("\n");
    return 0;
}
