/*
 * EL ESPEJO EN VHF, QUE ES DONDE NADIE LO HABIA MEDIDO.
 *
 * El banco de iqbaltest.c mide el corrector plano con UNA portadora limpia
 * a 96 kHz y con la banda practicamente vacia de ruido: 8.000 cuentas de
 * señal contra 30 de ruido. Ese es el caso facil, y sale perfecto.
 *
 * La radio en 2 metros no se parece a eso:
 *
 *   - la tasa es 192 kHz, no 96, asi que la constante de tiempo del
 *     estimador plano es la mitad en segundos pero el bloque abarca el
 *     doble de banda;
 *   - hay UNA señal en todo el tramo -un repetidor-, no una banda poblada;
 *   - y la señal esta en FM estrecha, no es una portadora limpia.
 *
 * Lo que este banco contesta es una sola pregunta, y con un numero:
 * CUANTO RECHAZO DE IMAGEN QUEDA en esa escena, pasando por el MISMO
 * iqbal_bloque() que corre dentro de la interrupcion del DMA. iqbaltest.c
 * llama a iqbal_mide()/iqbal_aplica(), que son las otras dos puertas del
 * mismo modulo; el camino que de verdad usa la radio no lo pisaba ningun
 * banco.
 *
 * Y LA SEGUNDA PREGUNTA, QUE ES LA QUE IMPORTA PARA DECIDIR: de donde sale
 * el suelo de ruido. Si nace ANTES del mezclador -la antena, el
 * preamplificador- pasa por el mismo desequilibrio que la señal y por
 * tanto LLEVA SU FIRMA: el estimador puede medirlo aunque no haya ni una
 * emisora. Si nace DESPUES -el convertidor, la cuantizacion- entra
 * equilibrado y lo unico que hace es diluir la medida. La escena de 2
 * metros se corre con las dos mezclas para saber cual de las dos manda.
 */
#include "iqbal.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int fallos = 0;

static void di(const char *que, int bien, const char *det)
{
    printf("    %-56s %s%s%s\n", que, bien ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!bien) { fallos++; }
}

#define FS      192000.0
#define NB      256U
#define NFFT    8192

/* El desequilibrio del aparato: los mismos 25 dB de rechazo nativo que
 * iqbal.h cita como el punto de partida de esta radio. */
#define GAN_DB    0.8
#define FASE_GR   3.0


static int16_t g_crudo[2U * NB];

static uint32_t s_rnd = 20261030u;
static double ruido(void)
{
    int k; double s = 0.0;
    for (k = 0; k < 12; k++) {
        s_rnd = s_rnd * 1103515245u + 12345u;
        s += (double)((s_rnd >> 9) & 0xFFFFu) / 65535.0;
    }
    return s - 6.0;   /* varianza 1, aproximadamente */
}

/* --- la escena ---------------------------------------------------------- */

#define MAX_POR 8

typedef struct {
    int    n;                 /* cuantas portadoras */
    double f[MAX_POR];        /* Hz respecto al oscilador */
    double a[MAX_POR];        /* amplitud en cuentas */
    double desv[MAX_POR];     /* desviacion de FM en Hz, 0 = portadora limpia */
    double n_ant;             /* ruido ANTES del desequilibrio (antena) */
    double n_des;             /* ruido DESPUES (convertidor) */
} escena_t;

static escena_t g_e;
static double g_ph[MAX_POR];     /* fase de cada portadora */
static double g_mod[MAX_POR];    /* fase del tono modulante */

/*
 * El receptor desequilibrado, con el MISMO convenio que iqbaltest.c y que
 * el propio corrector: toda la culpa cargada en Q.
 *
 *     I = a
 *     Q = gan * (b*cos(fase) + a*sin(fase))
 *
 * que para una portadora suelta (a=cos, b=sin) es exactamente
 * gan*sin(wt+fase). Al ser lineal vale igual para la suma de la escena
 * entera mas el ruido de antena, que es justo lo que hace falta aqui.
 */
static void bloque(void)
{
    const double gn = pow(10.0, GAN_DB / 20.0);
    const double ph = FASE_GR * 3.14159265358979 / 180.0;
    const double cph = cos(ph), sph = sin(ph);
    uint32_t k;
    int p;

    for (k = 0U; k < NB; k++) {
        double a = 0.0, b = 0.0, i, q;

        for (p = 0; p < g_e.n; p++) {
            double w = 2.0 * 3.14159265358979 * g_e.f[p] / FS;
            if (g_e.desv[p] > 0.0) {
                /* FM estrecha: un tono de 1 kHz con la desviacion pedida.
                 * La fase instantanea se integra, que es lo que hace una
                 * emisora de verdad. */
                double wm = 2.0 * 3.14159265358979 * 1000.0 / FS;
                g_mod[p] += wm;
                if (g_mod[p] > 6.283185307179586) { g_mod[p] -= 6.283185307179586; }
                g_ph[p] += w + (2.0 * 3.14159265358979 * g_e.desv[p] / FS)
                               * sin(g_mod[p]);
            } else {
                g_ph[p] += w;
            }
            if (g_ph[p] >  6.283185307179586) { g_ph[p] -= 6.283185307179586; }
            if (g_ph[p] < -6.283185307179586) { g_ph[p] += 6.283185307179586; }
            a += g_e.a[p] * cos(g_ph[p]);
            b += g_e.a[p] * sin(g_ph[p]);
        }

        /* Ruido de antena: entra ANTES, o sea que se desequilibra con todo
         * lo demas. Dos muestras independientes, que es lo que es el ruido
         * blanco complejo. */
        a += g_e.n_ant * ruido();
        b += g_e.n_ant * ruido();

        i = a;
        q = gn * (b * cph + a * sph);

        /* Y el del convertidor, DESPUES: entra equilibrado. */
        i += g_e.n_des * ruido();
        q += g_e.n_des * ruido();

        if (i >  32767.0) { i =  32767.0; }
        if (i < -32768.0) { i = -32768.0; }
        if (q >  32767.0) { q =  32767.0; }
        if (q < -32768.0) { q = -32768.0; }
        g_crudo[2U * k]      = (int16_t)i;
        g_crudo[2U * k + 1U] = (int16_t)q;
    }
}

/* --- rechazo de imagen por FFT compleja --------------------------------- */

static float fr[NFFT], fi[NFFT];

static void fft(void)
{
    int n = NFFT, i, j, len;
    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) { j ^= bit; }
        j ^= bit;
        if (i < j) {
            float t;
            t = fr[i]; fr[i] = fr[j]; fr[j] = t;
            t = fi[i]; fi[i] = fi[j]; fi[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * 3.14159265358979f / (float)len;
        float wr = cosf(ang), wi = sinf(ang);
        for (i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (j = 0; j < len / 2; j++) {
                float ur = fr[i + j],         ui = fi[i + j];
                float vr = fr[i + j + len/2] * cr - fi[i + j + len/2] * ci;
                float vi = fr[i + j + len/2] * ci + fi[i + j + len/2] * cr;
                float nr;
                fr[i + j] = ur + vr; fi[i + j] = ui + vi;
                fr[i + j + len/2] = ur - vr; fi[i + j + len/2] = ui - vi;
                nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
}

/* Potencia en una ventana de +-ancho_hz alrededor de f_hz, descontando el
 * suelo: sin descontarlo, en una banda vacia el "espejo" que se mide es el
 * ruido y el numero sale inventado. El suelo se estima con la mediana
 * aproximada de los bins de la mitad de la banda que no tiene nada. */
static double pot_banda(double f_hz, double ancho_hz, double suelo_bin)
{
    int c = (int)(f_hz / FS * (double)NFFT + 0.5);
    int w = (int)(ancho_hz / FS * (double)NFFT + 0.5);
    int k;
    double s = 0.0;
    if (w < 1) { w = 1; }
    for (k = c - w; k <= c + w; k++) {
        int a = ((k % NFFT) + NFFT) % NFFT;
        double p = (double)fr[a] * fr[a] + (double)fi[a] * fi[a];
        p -= suelo_bin;
        if (p > 0.0) { s += p; }
    }
    return s;
}

static double suelo_medio(double f1, double f2)
{
    int a = (int)(f1 / FS * (double)NFFT + 0.5);
    int b = (int)(f2 / FS * (double)NFFT + 0.5);
    int k, n = 0;
    double s = 0.0;
    for (k = a; k <= b; k++) {
        int x = ((k % NFFT) + NFFT) % NFFT;
        s += (double)fr[x] * fr[x] + (double)fi[x] * fi[x];
        n++;
    }
    return (n > 0) ? (s / (double)n) : 0.0;
}

/*
 * Pasa NFFT muestras por iqbal_bloque() -el camino de la interrupcion- y
 * devuelve el rechazo de imagen en dB de la portadora pedida.
 */
static double mide(double f_hz, double ancho_hz)
{
    int n = 0;
    uint32_t m;
    double ps, pe, suelo;

    while (n < NFFT) {
        bloque();
        iqbal_bloque(g_crudo, NB);
        for (m = 0U; m < NB && n < NFFT; m++, n++) {
            double wv = 0.5 - 0.5 * cos(2.0 * 3.14159265358979 * (double)n
                                        / (double)(NFFT - 1));
            fr[n] = (float)((double)g_crudo[2U * m]      * wv);
            fi[n] = (float)((double)g_crudo[2U * m + 1U] * wv);
        }
    }
    fft();

    /* El suelo, de un tramo donde la escena no pone nada ni su espejo. */
    suelo = suelo_medio(70000.0, 90000.0);
    ps = pot_banda( f_hz, ancho_hz, suelo);
    pe = pot_banda(-f_hz, ancho_hz, suelo);
    if (pe <= 0.0) { return 99.0; }
    return 10.0 * log10(ps / pe);
}

/* Deja correr el estimador el tiempo pedido, sin medir. */
static void asienta(double seg)
{
    long b, n = (long)(seg * FS / (double)NB + 0.5);
    for (b = 0; b < n; b++) {
        bloque();
        iqbal_bloque(g_crudo, NB);
    }
}

static void limpia_escena(void)
{
    memset(&g_e, 0, sizeof g_e);
    memset(g_ph, 0, sizeof g_ph);
    memset(g_mod, 0, sizeof g_mod);
}

/* iqbal.c toca el corrector ancho en la prueba A/B. Aqui no corre ninguna,
 * pero los simbolos tienen que existir. */
static uint8_t g_iqa_on = 1U;
void    iqa_set_on(uint8_t on) { g_iqa_on = on ? 1U : 0U; }
uint8_t iqa_get_on(void)       { return g_iqa_on; }

int main(void)
{
    double sin_corregir, r;
    char det[160];

    printf("\n  EL ESPEJO EN VHF (192 kHz, el camino de la interrupcion)\n");
    printf("  desequilibrio del aparato: %.1f dB de ganancia, %.1f grados\n\n",
           GAN_DB, FASE_GR);

    /* ------------------------------------------------------------------
     * 1. CUANTO ESPEJO HAY SIN CORREGIR. El punto de partida.
     * ------------------------------------------------------------------ */
    iqbal_init();
    iqbal_set_on(0U);          /* el corrector, fuera */
    limpia_escena();
    g_e.n = 1; g_e.f[0] = 24000.0; g_e.a[0] = 6000.0; g_e.desv[0] = 3000.0;
    g_e.n_ant = 60.0; g_e.n_des = 0.0;
    asienta(1.0);
    sin_corregir = mide(24000.0, 8000.0);
    snprintf(det, sizeof det, "(%.1f dB)", sin_corregir);
    di("el aparato solo, sin corrector", sin_corregir > 20.0 && sin_corregir < 30.0, det);

    /* ------------------------------------------------------------------
     * 2. LA ESCENA DE 2 METROS, CON EL RUIDO NACIENDO EN LA ANTENA.
     *    Un repetidor en FM estrecha a 24 kHz del oscilador y 192 kHz de
     *    banda por lo demas vacia.
     * ------------------------------------------------------------------ */
    iqbal_init();
    limpia_escena();
    g_e.n = 1; g_e.f[0] = 24000.0; g_e.a[0] = 6000.0; g_e.desv[0] = 3000.0;
    g_e.n_ant = 60.0; g_e.n_des = 0.0;
    asienta(30.0);
    r = mide(24000.0, 8000.0);
    snprintf(det, sizeof det, "(%.1f dB, antes %.1f)", r, sin_corregir);
    di("2 m, ruido de antena: el corrector gana al menos 20 dB",
       r > sin_corregir + 20.0, det);

    /* ------------------------------------------------------------------
     * 3. LA MISMA ESCENA CON EL RUIDO NACIENDO EN EL CONVERTIDOR.
     *    Ese ruido entra EQUILIBRADO: no lleva la firma del aparato y lo
     *    unico que puede hacer es diluir la medida del estimador.
     * ------------------------------------------------------------------ */
    iqbal_init();
    limpia_escena();
    g_e.n = 1; g_e.f[0] = 24000.0; g_e.a[0] = 6000.0; g_e.desv[0] = 3000.0;
    g_e.n_ant = 0.0; g_e.n_des = 60.0;
    asienta(30.0);
    r = mide(24000.0, 8000.0);
    snprintf(det, sizeof det, "(%.1f dB, antes %.1f)", r, sin_corregir);
    di("2 m, ruido del convertidor: el corrector gana al menos 20 dB",
       r > sin_corregir + 20.0, det);

    /* ------------------------------------------------------------------
     * 4. LA BANDA POBLADA, que es la escena de onda corta y la unica que
     *    estaba medida hasta hoy. Sirve de referencia: si esta sale bien y
     *    la de 2 metros no, el problema es la ESCENA y no el corrector.
     * ------------------------------------------------------------------ */
    iqbal_init();
    limpia_escena();
    g_e.n = 5;
    g_e.f[0] = -62000.0; g_e.a[0] = 5000.0;
    g_e.f[1] = -31000.0; g_e.a[1] = 7000.0;
    g_e.f[2] =  13000.0; g_e.a[2] = 6000.0;
    g_e.f[3] =  41000.0; g_e.a[3] = 4000.0;
    g_e.f[4] =  67000.0; g_e.a[4] = 5000.0;
    g_e.n_ant = 60.0; g_e.n_des = 0.0;
    asienta(30.0);
    r = mide(13000.0, 2000.0);
    snprintf(det, sizeof det, "(%.1f dB)", r);
    di("onda corta poblada: el corrector gana al menos 30 dB",
       r > sin_corregir + 30.0, det);

    /* ------------------------------------------------------------------
     * 5. EL PASO POR EL CENTRO, que desde el 30/10/2026 se puede hacer:
     *    nco_lo_aparcado() ya no reaparca ahi. Con la señal en continua,
     *    E[x^2] deja de promediar a cero y el estimador se inventa un
     *    desequilibrio -es el mismo mecanismo que iqbal.c tiene escrito
     *    para FM ancha-. Lo que se mide aqui es LO QUE SE LLEVA PUESTO:
     *    cuanto rechazo pierde la calibracion buena despues de cruzar.
     * ------------------------------------------------------------------ */
    {
        double bueno, tras_cruzar, tras_cruzar_con_puerta;

        /* Primero, una calibracion buena en banda poblada. */
        iqbal_init();
        limpia_escena();
        g_e.n = 5;
        g_e.f[0] = -62000.0; g_e.a[0] = 5000.0;
        g_e.f[1] = -31000.0; g_e.a[1] = 7000.0;
        g_e.f[2] =  13000.0; g_e.a[2] = 6000.0;
        g_e.f[3] =  41000.0; g_e.a[3] = 4000.0;
        g_e.f[4] =  67000.0; g_e.a[4] = 5000.0;
        g_e.n_ant = 60.0; g_e.n_des = 0.0;
        asienta(30.0);
        bueno = mide(13000.0, 2000.0);

        /* Ahora se cruza el centro con el aprendizaje ENCENDIDO: una
         * emisora a 1 kHz del oscilador, cuatro segundos, que es lo que
         * cuesta pasar por ahi con el mando. */
        limpia_escena();
        g_e.n = 1; g_e.f[0] = 1000.0; g_e.a[0] = 6000.0; g_e.desv[0] = 3000.0;
        g_e.n_ant = 60.0; g_e.n_des = 0.0;
        asienta(4.0);

        /* y de vuelta a la banda poblada, a ver que queda */
        limpia_escena();
        g_e.n = 5;
        g_e.f[0] = -62000.0; g_e.a[0] = 5000.0;
        g_e.f[1] = -31000.0; g_e.a[1] = 7000.0;
        g_e.f[2] =  13000.0; g_e.a[2] = 6000.0;
        g_e.f[3] =  41000.0; g_e.a[3] = 4000.0;
        g_e.f[4] =  67000.0; g_e.a[4] = 5000.0;
        g_e.n_ant = 60.0; g_e.n_des = 0.0;
        tras_cruzar = mide(13000.0, 2000.0);

        /* Y lo mismo con la puerta puesta, que es lo que hace la radio:
         * iqbal_aprende(0) mientras se cruza. */
        iqbal_init();
        limpia_escena();
        g_e.n = 5;
        g_e.f[0] = -62000.0; g_e.a[0] = 5000.0;
        g_e.f[1] = -31000.0; g_e.a[1] = 7000.0;
        g_e.f[2] =  13000.0; g_e.a[2] = 6000.0;
        g_e.f[3] =  41000.0; g_e.a[3] = 4000.0;
        g_e.f[4] =  67000.0; g_e.a[4] = 5000.0;
        g_e.n_ant = 60.0; g_e.n_des = 0.0;
        asienta(30.0);

        iqbal_aprende(0U);            /* <- la puerta de main.c */
        limpia_escena();
        g_e.n = 1; g_e.f[0] = 1000.0; g_e.a[0] = 6000.0; g_e.desv[0] = 3000.0;
        g_e.n_ant = 60.0; g_e.n_des = 0.0;
        asienta(4.0);
        iqbal_aprende(1U);

        limpia_escena();
        g_e.n = 5;
        g_e.f[0] = -62000.0; g_e.a[0] = 5000.0;
        g_e.f[1] = -31000.0; g_e.a[1] = 7000.0;
        g_e.f[2] =  13000.0; g_e.a[2] = 6000.0;
        g_e.f[3] =  41000.0; g_e.a[3] = 4000.0;
        g_e.f[4] =  67000.0; g_e.a[4] = 5000.0;
        g_e.n_ant = 60.0; g_e.n_des = 0.0;
        tras_cruzar_con_puerta = mide(13000.0, 2000.0);

        printf("\n  el paso por el centro (4 s con la señal a 1 kHz del LO)\n");
        printf("      calibracion buena ...................... %5.1f dB\n", bueno);
        printf("      despues de cruzar, aprendiendo ......... %5.1f dB\n", tras_cruzar);
        printf("      despues de cruzar, con la puerta ....... %5.1f dB\n\n",
               tras_cruzar_con_puerta);

        di("cruzar aprendiendo estropea la calibracion",
           tras_cruzar < bueno - 6.0, 0);
        di("y con la puerta puesta no se pierde casi nada",
           tras_cruzar_con_puerta > bueno - 3.0, 0);
    }

    printf("\n  %s: %d fallo(s)\n\n", fallos ? "MAL" : "bien", fallos);
    return fallos ? 1 : 0;
}
