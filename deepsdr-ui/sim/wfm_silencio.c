/*
 * ======================================================================
 * EL SILENCIO DE PROGRAMA: QUE LE HACE EL QUITADOR DE CONTINUA A UNA
 * EMISORA QUE SE CALLA - 26/10/2026
 * ======================================================================
 *
 * En FM ancha no hay mezclador: la portadora esta EN CONTINUA. El
 * quitador de continua no puede distinguirla de la fuga del convertidor,
 * y por eso lleva topes. Lo que ningun banco medía es el caso que pasa
 * todos los dias:
 *
 *     una emisora se queda SIN MODULAR unos segundos -el hueco entre
 *     canciones, una pausa de locucion, el identificativo- y despues
 *     vuelve el audio.
 *
 * Mientras esta quieta, su portadora ES continua pura, asi que el
 * estimador la persigue y se la come. Y cuando vuelve el audio, lo que
 * queda es la portadora MENOS lo que el estimador aprendio: si se comio
 * casi toda, lo que entra en el discriminador es un vector pequeño y
 * torcido, y de ahi sale aspereza.
 *
 * Esto lo mide: mantiene la emisora modulada hasta que el estimador
 * converge, la calla SEGUNDOS, mira cuanto ha aprendido a restar, y
 * devuelve la modulacion para medir el tono y la basura del audio que
 * sale INMEDIATAMENTE despues.
 *
 * LO QUE SE ESPERA. Que el estimador NO se coma la portadora: lo que
 * reste tiene que quedarse en una fraccion pequeña de ella, pase lo que
 * pase con la modulacion, y el tono de vuelta tiene que salir limpio.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define FS     192000.0
#define NBLK   SDR_RX_BLOCK_SAMPLES_WFM
#define NFFT   16384

static double re_[NFFT], im_[NFFT];
static void fft(void)
{
    int i, j, len;
    for (i = 1, j = 0; i < NFFT; i++) {
        int bit = NFFT >> 1;
        for (; j & bit; bit >>= 1) { j ^= bit; }
        j ^= bit;
        if (i < j) { double t;
            t=re_[i];re_[i]=re_[j];re_[j]=t; t=im_[i];im_[i]=im_[j];im_[j]=t; }
    }
    for (len = 2; len <= NFFT; len <<= 1) {
        double ang=-2.0*M_PI/len, wr=cos(ang), wi=sin(ang);
        for (i = 0; i < NFFT; i += len) {
            double cr=1.0, ci=0.0;
            for (j = 0; j < len/2; j++) {
                double ur=re_[i+j], ui=im_[i+j];
                double vr=re_[i+j+len/2]*cr-im_[i+j+len/2]*ci;
                double vi=re_[i+j+len/2]*ci+im_[i+j+len/2]*cr;
                double nr;
                re_[i+j]=ur+vr; im_[i+j]=ui+vi;
                re_[i+j+len/2]=ur-vr; im_[i+j+len/2]=ui-vi;
                nr=cr*wr-ci*wi; ci=cr*wi+ci*wr; cr=nr;
            }
        }
    }
}

static double g_fi, g_t;
static float  g_audio[NFFT];
static double g_mag[NFFT/2+1];

/* Un bloque de emisora. desv = 0 significa portadora quieta. */
static void bloque(int16_t *raw, double amp, double dc, double desv,
                   double piloto, double faudio)
{
    uint32_t n;
    for (n = 0; n < (uint32_t)NBLK; n++) {
        double t = g_t / FS;
        double f = desv*cos(2.0*M_PI*faudio*t)
                 + piloto*cos(2.0*M_PI*19000.0*t);
        double i, q;
        g_fi += 2.0*M_PI*f/FS;
        if (g_fi >  M_PI) { g_fi -= 2.0*M_PI; }
        if (g_fi < -M_PI) { g_fi += 2.0*M_PI; }
        g_t += 1.0;
        i = amp*cos(g_fi) + dc;
        q = amp*sin(g_fi);
        if (i >  32767.0) { i =  32767.0; }
        if (i < -32768.0) { i = -32768.0; }
        if (q >  32767.0) { q =  32767.0; }
        if (q < -32768.0) { q = -32768.0; }
        raw[2U*n] = (int16_t)lrint(i);
        raw[2U*n+1U] = (int16_t)lrint(q);
    }
}

static double g_quita;      /* lo que el quitador resta al final del silencio */
static double g_crudo;      /* y lo que mide sin topar */
static double g_fuga;       /* el suelo que la cadena se ha medido */

/* Ruido gaussiano, para el apartado del ruido. */
static unsigned long g_sem = 12345UL;
static double ruido(void)
{
    double u1, u2;
    g_sem = g_sem * 1103515245UL + 12345UL;
    u1 = ((g_sem >> 16) & 0x7FFFU) / 32768.0 + 1e-9;
    g_sem = g_sem * 1103515245UL + 12345UL;
    u2 = ((g_sem >> 16) & 0x7FFFU) / 32768.0;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

/*
 * EL RUIDO, QUE ES LO QUE NINGUN BANCO DE ESTE ARBOL METIA EN EL QUITADOR
 * DE CONTINUA - 26/10/2026.
 *
 * Y es la condicion normal de recepcion: en FM ancha el firmware clava el
 * PGA en el tope, asi que cualquier emisora que no sea local llega con
 * ruido. Toda la decision de "hay continua encima" sale de la envolvente,
 * y el ruido tambien mueve la envolvente: si la medida no lo aguanta, el
 * quitador se pone a restar el residuo de la propia emisora y distorsiona
 * TODAS las emisoras, sin que la radio tenga fuga ninguna.
 *
 * Esto barre modulacion y ruido SIN fuga de continua. Lo correcto en todas
 * las casillas es que no se reste casi nada.
 */
static double corre_ruido(double amp, double desv, double sigma, double faudio)
{
    static int16_t raw[2*NBLK];
    int b, k, tomadas = 0;
    float di = 0.0f, dq = 0.0f;

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    demod_wfm_reset_diag();
    demod_am_set_audio_bw(AUDIO_BW_4K0);
    demod_am_set_wfm_ifbw(WFM_IFBW_WIDE);
    demod_am_set_agc_profile(AGC_PROFILE_MEDIUM);
    cons_captura_tramas((uint32_t)NBLK);
    g_fi = 0.0; g_t = 0.0; g_sem = 12345UL;

    for (b = 0; b < 1500; b++) {
        uint32_t n;
        for (n = 0; n < (uint32_t)NBLK; n++) {
            double f = desv * cos(2.0*M_PI*faudio*(g_t/FS));
            double i, q;
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
        demod_wfm_process_raw(raw);
    }
    demod_wfm_get_dc(&di, &dq);
    (void)k; (void)tomadas;
    return sqrt((double)di*di + (double)dq*dq);
}

/*
 * LA RADIO DE VERDAD LLEGA A FM ANCHA HABIENDO PASADO POR UN MODO CON
 * MEZCLADOR, y eso importa: alli es donde se mide la fuga de continua que
 * el recorte de WFM usa de suelo (ver s_rf_fuga en demod_am.c). Un banco
 * que entra en WFM desde el arranque en frio mide la radio con el suelo a
 * cero, que es solo la mitad de la pregunta.
 *
 * Esto hace lo que hace la radio: unos cuantos cientos de bloques de AM
 * con desplazamiento de mezclador y la misma fuga, para que el lazo de
 * aquella cadena converja, y despues se pasa a FM ancha.
 */
static void ceba_fuga(double fuga)
{
    static int16_t raw[2*SDR_RX_BLOCK_SAMPLES];
    const double off = 10000.0;
    double fi = 0.0;
    int b;
    uint32_t n;

    demod_am_set_active_rate(0U);
    demod_am_set_rf_doble(0U);
    demod_am_set_mode(DEMOD_MODE_AM);
    demod_am_set_mix_hz((float)off);
    for (b = 0; b < 400; b++) {
        for (n = 0; n < (uint32_t)SDR_RX_BLOCK_SAMPLES; n++) {
            double i, q;
            fi += 2.0*M_PI*off/96000.0;
            if (fi > M_PI) { fi -= 2.0*M_PI; }
            i = 3000.0*cos(fi) + fuga;
            q = 3000.0*sin(fi) + fuga;
            if (i >  32767.0) { i =  32767.0; }
            if (i < -32768.0) { i = -32768.0; }
            if (q >  32767.0) { q =  32767.0; }
            if (q < -32768.0) { q = -32768.0; }
            raw[2U*n] = (int16_t)lrint(i);
            raw[2U*n+1U] = (int16_t)lrint(q);
        }
        demod_am_process_raw(raw);
    }
}

/*
 * amp     portadora en cuentas del convertidor
 * fuga    offset de continua del convertidor, en cuentas
 * quietos segundos que la emisora se queda sin modular
 * piloto  si se deja el piloto de 19 kHz puesto durante el silencio
 */
static void corre2(double amp, double fuga, double quietos, int piloto_quieto,
                   double faudio, int cebar)
{
    static int16_t raw[2*NBLK];
    int b, k, tomadas = 0;
    const int calienta = 900;                 /* el estimador converge lento */
    const int callados = (int)(quietos * FS / NBLK);
    float di = 0.0f, dq = 0.0f, ci = 0.0f, cq = 0.0f;
    const double desv   = 0.45*0.9*75000.0;
    const double pil    = 0.1*75000.0;

    demod_am_init();
    if (cebar) { ceba_fuga(fuga); }
    demod_am_set_mode(DEMOD_MODE_WFM);
    /* Lo que hace apply_demod_mode() al entrar en el modo, y que importa:
     * ademas del silencio, pone a cero el estimador de continua Y el
     * seguidor lento del nivel. Sin esto el banco entra en FM ancha
     * arrastrando el nivel del modo anterior. */
    demod_wfm_reset_diag();
    demod_am_set_audio_bw(AUDIO_BW_4K0);
    demod_am_set_wfm_ifbw(WFM_IFBW_WIDE);
    demod_am_set_agc_profile(AGC_PROFILE_MEDIUM);
    cons_captura_tramas((uint32_t)NBLK);
    g_fi = 0.0; g_t = 0.0;

    for (b = 0; b < calienta; b++) {
        bloque(raw, amp, fuga, desv, pil, faudio);
        demod_wfm_process_raw(raw);
    }
    for (b = 0; b < callados; b++) {
        bloque(raw, amp, fuga, 0.0, piloto_quieto ? pil : 0.0, faudio);
        demod_wfm_process_raw(raw);
    }
    demod_wfm_get_dc(&di, &dq);
    demod_wfm_get_dc_crudo(&ci, &cq);
    g_quita = (double)di;
    g_crudo = (double)ci;
    g_fuga = (double)demod_wfm_get_fuga();

    for (b = 0; tomadas < NFFT; b++) {
        const int16_t *sal;
        bloque(raw, amp, fuga, desv, pil, faudio);
        demod_wfm_process_raw(raw);
        sal = cons_audio();
        for (k = 0; k < NBLK && tomadas < NFFT; k++) {
            g_audio[tomadas++] = (float)sal[2*k];
        }
    }
    for (k = 0; k < NFFT; k++) {
        double w = 0.5 - 0.5*cos(2.0*M_PI*k/NFFT);
        re_[k] = g_audio[k]*w; im_[k] = 0.0;
    }
    fft();
    for (k = 0; k <= NFFT/2; k++) {
        g_mag[k] = sqrt(re_[k]*re_[k]+im_[k]*im_[k])*2.0/(NFFT*0.5);
    }
}

static void corre(double amp, double fuga, double quietos, int piloto_quieto,
                  double faudio)
{
    corre2(amp, fuga, quietos, piloto_quieto, faudio, 0);
}

/*
 * LA EMISORA FLOJA DETRAS DE UNA LOCAL, SIN REINICIAR NADA EN MEDIO.
 * 26/10/2026, cuarta vuelta de la revision a fondo.
 *
 * Esto es lo que las tres tablas de arriba NO miden, y es exactamente por
 * lo que no lo median: todas llaman a demod_am_init() al empezar, asi que
 * el suelo del recorte arranca en cero y nunca cruza de una emisora a
 * otra. La radio no hace eso: mueve el dial.
 *
 * Aqui se oye una emisora FUERTE unos segundos, se cambia a una FLOJA sin
 * reiniciar nada -que es lo que hace girar el mando-, se la deja callarse
 * el hueco de programa, y se mide lo que sale cuando vuelve el audio.
 */
static void corre_detras(double amp_fuerte, double amp_floja, double fuga,
                         double segundos_fuerte, double quietos,
                         double faudio)
{
    static int16_t raw[2*NBLK];
    int b, k, tomadas = 0;
    const int fuertes  = (int)(segundos_fuerte * FS / NBLK);
    const int callados = (int)(quietos * FS / NBLK);
    float di = 0.0f, dq = 0.0f, ci = 0.0f, cq = 0.0f;
    const double desv = 0.45*0.9*75000.0;
    const double pil  = 0.1*75000.0;

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    demod_wfm_reset_diag();
    demod_am_set_audio_bw(AUDIO_BW_4K0);
    demod_am_set_wfm_ifbw(WFM_IFBW_WIDE);
    demod_am_set_agc_profile(AGC_PROFILE_MEDIUM);
    cons_captura_tramas((uint32_t)NBLK);
    g_fi = 0.0; g_t = 0.0;

    /* 1. la local, modulada */
    for (b = 0; b < fuertes; b++) {
        bloque(raw, amp_fuerte, fuga, desv, pil, faudio);
        demod_wfm_process_raw(raw);
    }
    /* 2. se mueve el dial a la floja. Nada se reinicia: en la radio esto es
     *    apply_lo_tune(), que no toca la cadena. */
    for (b = 0; b < 400; b++) {
        bloque(raw, amp_floja, fuga, desv, pil, faudio);
        demod_wfm_process_raw(raw);
    }
    /* 3. y la floja se calla */
    for (b = 0; b < callados; b++) {
        bloque(raw, amp_floja, fuga, 0.0, pil, faudio);
        demod_wfm_process_raw(raw);
    }
    demod_wfm_get_dc(&di, &dq);
    demod_wfm_get_dc_crudo(&ci, &cq);
    g_quita = sqrt((double)di*di + (double)dq*dq);
    g_crudo = sqrt((double)ci*ci + (double)cq*cq);

    /* 4. vuelve el audio */
    for (b = 0; tomadas < NFFT; b++) {
        const int16_t *sal;
        bloque(raw, amp_floja, fuga, desv, pil, faudio);
        demod_wfm_process_raw(raw);
        sal = cons_audio();
        for (k = 0; k < NBLK && tomadas < NFFT; k++) {
            g_audio[tomadas++] = (float)sal[2*k];
        }
    }
    for (k = 0; k < NFFT; k++) {
        double w = 0.5 - 0.5*cos(2.0*M_PI*k/NFFT);
        re_[k] = g_audio[k]*w; im_[k] = 0.0;
    }
    fft();
    for (k = 0; k <= NFFT/2; k++) {
        g_mag[k] = sqrt(re_[k]*re_[k]+im_[k]*im_[k])*2.0/(NFFT*0.5);
    }
}

static double pico_en(double hz)
{
    int c=(int)lrint(hz*NFFT/FS), k; double s=0.0;
    for (k=c-2;k<=c+2;k++) if(k>=0&&k<=NFFT/2) s+=g_mag[k]*g_mag[k];
    return sqrt(s);
}

static void mide(double faudio, double *tono, double *basura)
{
    int k; double e=0.0;
    *tono = pico_en(faudio);
    for (k=(int)(100.0*NFFT/FS); k<=(int)(8000.0*NFFT/FS); k++) e += g_mag[k]*g_mag[k];
    *basura = 100.0*(e - (*tono)*(*tono))/(e>0?e:1);
}

static int fallos = 0;
static void comprueba(int ok, const char *que, const char *det)
{
    printf("    %-56s %s%s%s\n", que, ok ? "bien" : "*** MAL ***",
           det && det[0] ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

int main(void)
{
    static const double k_amp[] = { 300.0, 500.0, 800.0, 1200.0, 2000.0,
                                    4000.0, 12000.0 };
    int j;
    double peor_pct = 0.0, peor_bas = 0.0;

    printf("\n== LA EMISORA QUE SE CALLA: EL QUITADOR DE CONTINUA EN EL "
           "HUECO DE PROGRAMA ==\n");

    printf("\n  -- radio SIN fuga de continua, 5 s de portadora quieta --\n");
    printf("   portadora   quita   %% de la portadora   mide sin topar   "
           "tono al volver   basura\n");
    for (j = 0; j < 7; j++) {
        double tono, bas, pct;
        corre(k_amp[j], 0.0, 5.0, 1, 1000.0);
        mide(1000.0, &tono, &bas);
        pct = 100.0*g_quita/k_amp[j];
        printf("   %9.0f %7.1f %18.1f%% %16.1f %16.0f %8.1f%%\n",
               k_amp[j], g_quita, pct, g_crudo, tono, bas);
        if (pct > peor_pct) { peor_pct = pct; }
        if (bas > peor_bas) { peor_bas = bas; }
    }

    printf("\n  -- lo mismo, SIN piloto durante el silencio (mono callado) --\n");
    printf("   portadora   quita   %% de la portadora   tono al volver   basura\n");
    for (j = 0; j < 7; j++) {
        double tono, bas, pct;
        corre(k_amp[j], 0.0, 5.0, 0, 1000.0);
        mide(1000.0, &tono, &bas);
        pct = 100.0*g_quita/k_amp[j];
        printf("   %9.0f %7.1f %18.1f%% %16.0f %8.1f%%\n",
               k_amp[j], g_quita, pct, tono, bas);
        if (pct > peor_pct) { peor_pct = pct; }
        if (bas > peor_bas) { peor_bas = bas; }
    }

    printf("\n  -- y con fuga de verdad: el caso para el que nacieron los topes --\n");
    printf("     (SIN pasar antes por un modo con mezclador: la fuga no esta\n"
           "      medida, asi que el suelo del recorte vale cero)\n");
    printf("   portadora    fuga   quita   residuo   tono al volver   basura\n");
    {
        static const double k_f[][2] = { {120.0, 300.0}, {300.0, 300.0},
                                         {800.0, 300.0}, {4000.0, 3000.0},
                                         {12000.0, 3000.0} };
        int m;
        double peor_sin = 0.0, peor_con = 0.0;

        for (m = 0; m < 5; m++) {
            double tono, bas;
            corre2(k_f[m][0], k_f[m][1], 5.0, 1, 1000.0, 0);
            mide(1000.0, &tono, &bas);
            if (bas > peor_sin) { peor_sin = bas; }
            printf("   %9.0f %7.0f %7.1f %9.1f %16.0f %8.1f%%  (suelo %.0f)\n",
                   k_f[m][0], k_f[m][1], g_quita, k_f[m][1]-g_quita, tono, bas,
                   g_fuga);
        }

        printf("\n     (COMO LA RADIO: pasando antes por AM con mezclador, que es\n"
               "      donde la fuga se mide de verdad)\n");
        printf("   portadora    fuga   quita   residuo   tono al volver   basura\n");
        for (m = 0; m < 5; m++) {
            double tono, bas;
            corre2(k_f[m][0], k_f[m][1], 5.0, 1, 1000.0, 1);
            mide(1000.0, &tono, &bas);
            if (bas > peor_con) { peor_con = bas; }
            printf("   %9.0f %7.0f %7.1f %9.1f %16.0f %8.1f%%  (suelo %.0f)\n",
                   k_f[m][0], k_f[m][1], g_quita, k_f[m][1]-g_quita, tono, bas,
                   g_fuga);
        }
        /*
         * LAS DOS TABLAS TIENEN QUE SALIR IGUALES, y eso es el resultado.
         *
         * Cuando se escribio este banco la fuga la media la cadena con
         * mezclador, asi que entrar en FM ancha sin haber pasado por ella
         * dejaba el suelo a cero y las dos tablas salian distintas. Desde
         * que la mide la propia cadena de FM ancha -ver el final del
         * bloqueador de continua de demod_am.c- da igual por donde se haya
         * llegado: la radio se mide a si misma en el modo en que esta.
         *
         * Asi que lo que se comprueba es que las dos salgan limpias, no que
         * una sea mejor que la otra.
         */
        {
            char det[96];
            snprintf(det, sizeof det, "sin pasar por AM %.1f%%, pasando %.1f%%",
                     peor_sin, peor_con);
            comprueba(peor_sin <= 15.0 && peor_con <= 15.0,
                      "con fuga sale limpio, se haya llegado como se haya",
                      det);
        }
    }

      printf("\n  -- LA EMISORA FLOJA DETRAS DE UNA LOCAL (sin reiniciar nada) --\n");
    printf("     8 s de una de 30.000 cuentas, se mueve el dial, y la floja\n"
           "     se calla 5 s. Radio SIN fuga de continua.\n");
    printf("   floja   quita   %% de la portadora   tono al volver   basura\n");
    {
        static const double k_f[] = { 400.0, 600.0, 1000.0, 2000.0 };
        int m;
        double peor = 0.0;

        for (m = 0; m < 4; m++) {
            double tono, bas, pct;
            corre_detras(30000.0, k_f[m], 0.0, 8.0, 5.0, 1000.0);
            mide(1000.0, &tono, &bas);
            pct = 100.0*g_quita/k_f[m];
            printf("   %5.0f %7.1f %18.1f%% %16.0f %8.2f%%\n",
                   k_f[m], g_quita, pct, tono, bas);
            if (pct > peor) { peor = pct; }
        }
        {
            char det[80];
            snprintf(det, sizeof det, "el peor %.1f%% de la portadora", peor);
            comprueba(peor <= 50.0,
                      "lo que oiste antes no se come la emisora de ahora",
                      det);
        }
    }

    printf("\n  -- CON RUIDO Y SIN FUGA: lo correcto es no restar casi nada --\n");
    printf("     (portadora 4000 cuentas, tono de 1 kHz, barriendo desviacion\n"
           "      y ruido. La columna es lo que el quitador resta.)\n");
    {
        static const double k_desv[]  = { 200.0, 800.0, 5000.0, 30000.0 };
        static const double k_sigma[] = { 0.0, 10.0, 40.0, 200.0 };
        int a, m;
        double peor = 0.0;

        printf("   desv\\ruido %10s %10s %10s %10s\n",
               "0 %", "0,25 %", "1 %", "5 %");
        for (a = 0; a < 4; a++) {
            printf("   %7.0f Hz", k_desv[a]);
            for (m = 0; m < 4; m++) {
                double q = corre_ruido(4000.0, k_desv[a], k_sigma[m], 1000.0);
                printf(" %10.1f", q);
                if (q > peor) { peor = q; }
            }
            printf("\n");
        }
        {
            char det[80];
            snprintf(det, sizeof det, "lo peor %.0f cuentas de 4.000", peor);
            comprueba(peor <= 400.0,
                      "el ruido no le hace restar la propia emisora", det);
        }
    }

  printf("\n  veredicto\n");
    {
        char det[80];
        snprintf(det, sizeof det, "el peor %.1f%% de la portadora", peor_pct);
        comprueba(peor_pct <= 50.0,
                  "sin fuga, el quitador no se come la portadora", det);
        snprintf(det, sizeof det, "la peor %.1f%%", peor_bas);
        comprueba(peor_bas <= 15.0,
                  "sin fuga, al volver el audio sale limpio", det);
    }

    printf("\n%s\n\n", fallos ? "*** MAL ***" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}
