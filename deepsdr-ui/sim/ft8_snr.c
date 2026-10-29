/*
 * EL SNR DE FT8, MEDIDO CONTRA UNA SEÑAL DE SNR CONOCIDO - 13/10/2026.
 *
 * POR QUE ESTE BANCO Y NO AJUSTARLO MIRANDO WSJT-X. Entre la antena y ese
 * numero hay el AGC, el FT8_ADAPTER_DB_OFFSET de -112, la ganancia coherente
 * de la ventana de Hann y su ancho de banda equivalente de ruido. Varias se
 * cancelan al hacer el cociente; la de la ventana NO, porque un tono cae
 * coherente en un bin y el ruido se reparte. Si se ajusta la constante hasta
 * que cuadre con una grabacion, se mete dentro de la constante el AGC de esa
 * grabacion y el numero queda bien esa tarde y mal las demas.
 *
 * ASI QUE SE FABRICA LA SEÑAL. Aqui se genera una emision tipo FT8 -79
 * simbolos de 8-FSK, tonos cada 6,25 Hz, 0,16 s cada uno- mas ruido gaussiano
 * a un nivel CALCULADO para que el SNR verdadero en 2.500 Hz sea exactamente
 * el que se pide. Se mete por el ADAPTADOR DE VERDAD del firmware
 * (ft8_waterfall_adapter.c + ft8_fft.c, los mismos que corren en la
 * radio, no una copia) y se compara lo que sale con lo que se metio.
 *
 * Y SE BARRE, QUE ES LO QUE DE VERDAD MIDE. Una sola medida no distingue un
 * error de escala de uno de desplazamiento: con un punto, "pendiente 0,5 con
 * 13 dB de sesgo" y "pendiente 1 clavada" pueden dar el mismo numero. Con el
 * barrido se exige PENDIENTE 1 y error dentro de +-2 dB. Si la pendiente sale
 * 0,5, la formula tiene un factor de dos; si sale 1 con 6 dB de
 * desplazamiento, es una constante que falta.
 *
 * COMO SE CALCULA EL NIVEL DEL RUIDO. El SNR de WSJT-X es
 * Pseñal / Pruido_en_2500Hz. El muestreo aqui son 3.200 Hz, o sea 1.600 Hz de
 * Nyquist: no caben 2.500 Hz de ruido, y no hace falta que quepan, porque lo
 * que se fija es la DENSIDAD. Con ruido blanco de varianza s2 repartida en
 * 1.600 Hz, la potencia en 2.500 Hz seria s2 * 2500/1600. Igualando eso a
 * Pseñal/snr sale la varianza que hay que generar.
 */
#include "ft8_waterfall_adapter.h"
#include "ft8_snr.h"
#include "ft8/decode.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

static int fallos = 0;
static void di(const char *que, int ok, const char *det)
{
    printf("    %-44s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

/* NO a mano: sale del adaptador, que el 28/10/2026 paso de 3.200 a 6.400
 * Hz al ensanchar la ventana a la banda entera. Escrito a pelo, este banco
 * fabricaba la señal a la mitad de velocidad y todo caia al suelo. */
#define FS        ((double)FT8_ADAPTER_SAMPLE_RATE_HZ)
#define NYQ       (FS / 2.0)       /* la banda que se analiza */
#define REF_BW    2500.0
#define SUB       FT8_ADAPTER_SUBBLOCK_SIZE   /* 512 = un simbolo */
#define SIMBOLOS  79
#define TONO_HZ   6.25

/* Ruido gaussiano, Box-Muller con un generador propio: el banco tiene que
 * dar lo mismo en cada pasada o un fallo intermitente no se puede perseguir. */
static uint32_t g_rnd = 123456789u;
static double uniforme(void)
{
    g_rnd = g_rnd * 1664525u + 1013904223u;
    return ((double)(g_rnd >> 8) + 0.5) / 16777216.0;
}
static double gauss(void)
{
    double u1 = uniforme(), u2 = uniforme();
    if (u1 < 1e-12) { u1 = 1e-12; }
    return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
}

/* Los 79 tonos. No hace falta que sean un mensaje valido: el SNR se mide
 * sobre la potencia de los bins, no sobre lo que diga el texto. Lo que SI
 * importa es que el candidato apunte al sitio correcto, y eso se le da a
 * mano mas abajo. */
static uint8_t s_tonos[SIMBOLOS];
static void tonos_fabrica(void)
{
    static const uint8_t costas[7] = { 3U, 1U, 4U, 0U, 6U, 5U, 2U };
    int i;

    for (i = 0; i < SIMBOLOS; i++) {
        if (i < 7)        { s_tonos[i] = costas[i]; }
        else if (i >= 36 && i < 43) { s_tonos[i] = costas[i - 36]; }
        else if (i >= 72) { s_tonos[i] = costas[i - 72]; }
        else              { s_tonos[i] = (uint8_t)((i * 5U) % 8U); }
    }
}

/*
 * Mete una ranura entera por el adaptador de verdad y devuelve el SNR que
 * dice el firmware para el candidato que se le señala.
 *
 * base_hz: donde empieza el tono 0. amp: amplitud del tono (cuentas de
 * int16). snr_db: el SNR verdadero en 2.500 Hz que se quiere generar.
 */
static float mide(double base_hz, double amp, double snr_db)
{
    ftx_waterfall_t wf;
    ftx_candidate_t cand;
    const ftx_waterfall_t *p;
    int16_t bloque[SUB];
    double fase = 0.0;
    double p_sig, var_ruido, sigma;
    int sym, n, bloques = 0;

    /* La potencia de un tono de amplitud A es A^2/2. */
    p_sig = amp * amp * 0.5;
    /* Pruido_en_2500 = sigma^2 * 2500/1600, y se quiere = p_sig / snr. */
    var_ruido = (p_sig / pow(10.0, snr_db * 0.1)) * (NYQ / REF_BW);
    sigma = sqrt(var_ruido);

    ft8_waterfall_reset();

    /* Un par de bloques de ruido solo delante, para que el candidato no
     * empiece en el borde: time_offset = 2. */
    for (sym = -2; sym < SIMBOLOS && !ft8_waterfall_is_full(); sym++) {
        double hz = (sym < 0) ? 0.0 : (base_hz + (double)s_tonos[sym] * TONO_HZ);
        double dfase = 6.283185307179586 * hz / FS;

        for (n = 0; n < SUB; n++) {
            double v = (sym < 0) ? 0.0 : (amp * sin(fase));
            v += sigma * gauss();
            fase += dfase;
            if (fase > 6.283185307179586) { fase -= 6.283185307179586; }
            if (v >  32767.0) { v =  32767.0; }
            if (v < -32768.0) { v = -32768.0; }
            bloque[n] = (int16_t)v;
        }
        ft8_waterfall_feed_subblock(bloque);
        bloques++;
    }

    /* Y el resto de la ranura con ruido solo, para que haya vecinos de los
     * que sacar la mediana en TODOS los bloques. */
    while (!ft8_waterfall_is_full()) {
        for (n = 0; n < SUB; n++) {
            double v = sigma * gauss();
            if (v >  32767.0) { v =  32767.0; }
            if (v < -32768.0) { v = -32768.0; }
            bloque[n] = (int16_t)v;
        }
        ft8_waterfall_feed_subblock(bloque);
    }

    p = ft8_waterfall_get();
    wf = *p;

    memset(&cand, 0, sizeof cand);
    cand.time_offset = 2;
    cand.freq_offset = (int16_t)(base_hz / TONO_HZ + 0.5);
    cand.time_sub = 0U;
    cand.freq_sub = 0U;
    cand.score = 100;

    return ft8_snr_db(&wf, &cand);
}

int main(void)
{
    static const double k_snr[] = { -20.0, -15.0, -10.0, -5.0, 0.0, 5.0, 10.0 };
    const int N = (int)(sizeof k_snr / sizeof k_snr[0]);
    double salida[8];
    double peor = 0.0, pendiente;
    char det[128];
    int i;

    printf("\nEL SNR DE FT8 CONTRA UNA SEÑAL DE SNR CONOCIDO\n");
    printf("==============================================\n\n");
    printf("  %.0f Hz de muestreo, bins de %.2f Hz, referencia %.0f Hz\n",
           FS, TONO_HZ, REF_BW);
    printf("  %d simbolos de 8-FSK, tono 0 en 500 Hz\n\n", SIMBOLOS);

    tonos_fabrica();

    printf("    SNR puesto    SNR medido    error\n");
    for (i = 0; i < N; i++) {
        double e;

        salida[i] = (double)mide(500.0, 3000.0, k_snr[i]);
        e = salida[i] - k_snr[i];
        if (e < 0.0) { e = -e; }
        if (e > peor) { peor = e; }
        printf("      %+6.1f dB     %+6.1f dB   %+5.1f\n",
               k_snr[i], salida[i], salida[i] - k_snr[i]);
    }
    printf("\n");

    /*
     * LA PENDIENTE, que es lo que un solo punto no puede decir. Minimos
     * cuadrados sobre el barrido entero.
     */
    {
        double sx = 0.0, sy = 0.0, sxy = 0.0, sxx = 0.0;

        for (i = 0; i < N; i++) {
            sx += k_snr[i]; sy += salida[i];
            sxy += k_snr[i] * salida[i]; sxx += k_snr[i] * k_snr[i];
        }
        pendiente = ((double)N * sxy - sx * sy) / ((double)N * sxx - sx * sx);
    }

    snprintf(det, sizeof det, "(%.2f, se pide entre 0,85 y 1,15)", pendiente);
    di("la pendiente es 1: no hay error de escala",
       pendiente > 0.85 && pendiente < 1.15, det);

    snprintf(det, sizeof det, "(el peor es %.1f dB)", peor);
    di("y el error no pasa de 2 dB en todo el barrido", peor <= 2.0, det);

    /* Y QUE EL SIGNO SEA EL QUE DICE EL ISSUE: una señal floja tiene que dar
     * un numero NEGATIVO, que es justo lo que no hacia la formula de antes. */
    snprintf(det, sizeof det, "(%+.1f dB con -10 puestos)", salida[2]);
    di("una señal de -10 dB sale negativa", salida[2] < 0.0, det);

    /* SIN SEÑAL, EL SUELO. Amplitud cero: solo ruido. Tiene que caer al
     * suelo y no inventarse un numero. */
    {
        float v = mide(500.0, 0.0, 0.0);
        snprintf(det, sizeof det, "(%+.1f dB, el suelo es %.0f)", v, FT8_SNR_SUELO_DB);
        di("con ruido solo cae al suelo", v <= FT8_SNR_SUELO_DB + 2.0f, det);
    }

    printf("\n  Esto NO es una calibracion contra WSJT-X: es una medida contra\n");
    printf("  una señal fabricada con un SNR que se conoce porque se ha puesto.\n");
    printf("  El ruido de aqui es gaussiano blanco y el de HF no lo es, y el\n");
    printf("  AGC de la radio solo se cancela si es constante durante los\n");
    printf("  12,6 s de la ranura. Por eso lo que se exige es pendiente 1 y\n");
    printf("  +-2 dB, no clavar el numero de otro programa.\n");

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}
