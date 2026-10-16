/*
 * EL ESPEJO: CUANTO LO BAJA EL CORRECTOR.
 *
 * Pedido por el usuario.
 *
 * Esa es la firma de un espejo y de nada mas. Aqui se fabrica uno a
 * proposito -un desequilibrio de fase y de ganancia conocidos-, se mide
 * cuanto espejo sale, se pasa por el corrector y se vuelve a medir.
 *
 * La medida es la unica que vale: el RECHAZO DE IMAGEN, o sea cuantos dB hay
 * entre la señal de verdad y su reflejo, sacado de la FFT compleja. No "los
 * coeficientes convergen" -convergir a una solucion mala tambien es
 * convergir- sino "cuanto espejo queda".
 *
 * Y la prueba que de verdad importa va al final: con la entrada YA
 * EQUILIBRADA, el corrector no puede empeorarla. Un corrector ciego suelto
 * sobre una señal que estaba bien es una forma excelente de estropear los
 * veintidos modos que funcionan.
 */
#include "iqbal.h"
#include "iqancho.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int fallos = 0;

static void di(const char *que, int bien, const char *det)
{
    printf("    %-54s %s%s%s\n", que, bien ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!bien) { fallos++; }
}

#define FS      96000.0f
#define NB      256U            /* el bloque de verdad de la radio */
#define NBLO    4000U           /* bloques: unos 10 s a 375 bloques/s */
#define NFFT    4096

static int16_t g_i[NB], g_q[NB];
static int16_t g_crudo[2 * NB];

static uint32_t s_rnd = 7u;
static float ruido(void)
{
    int k; float s = 0.0f;
    for (k = 0; k < 8; k++) {
        s_rnd = s_rnd * 1103515245u + 12345u;
        s += (float)((s_rnd >> 9) & 0xFFFFu) / 65535.0f;
    }
    return s - 4.0f;
}

/*
 * El receptor desequilibrado. La señal es una portadora en +f_hz mas un
 * poco de ruido de banda ancha, que es lo que hay en una antena de verdad.
 *
 *   I = cos(w t)
 *   Q = gan * sin(w t + fase)
 *
 * que es el modelo clasico: toda la culpa cargada en Q. El corrector esta
 * escrito con el mismo convenio, asi que si el modelo y el corrector
 * estuviesen los dos mal de la misma forma, esto no lo veria - por eso la
 * medida final es la FFT y no los coeficientes.
 */
static float g_fase_grados = 3.0f;
static float g_gan_db      = 0.8f;
static float g_f_hz        = 12000.0f;
static float g_ruido       = 30.0f;
static double g_t = 0.0;

/*
 * LA FASE SE ACUMULA Y SE ENVUELVE, no se multiplica por el numero de
 * muestra. Se hacia asi -w*t con t llegando al millon- y el float se comia
 * el desfase: a 8e5 radianes el salto entre dos floats vale 0,0625, y los
 * 0,052 radianes de los tres grados que se querian meter se redondeaban a
 * cero o al doble segun donde cayera. El banco media 3,91 grados con tres
 * puestos y la culpa era suya, no del corrector.
 */
static void bloque(void)
{
    double w  = 2.0 * 3.14159265358979 * (double)g_f_hz / (double)FS;
    float ph = g_fase_grados * 3.14159265358979f / 180.0f;
    float gn = powf(10.0f, g_gan_db / 20.0f);
    uint32_t k;

    for (k = 0U; k < NB; k++) {
        float t;
        g_t += w;
        if (g_t > 6.283185307179586) { g_t -= 6.283185307179586; }
        t = (float)g_t;
        {
        float a = 8000.0f * cosf(t);
        float b = 8000.0f * gn * sinf(t + ph);
        a += g_ruido * ruido();
        b += g_ruido * ruido();
        g_i[k] = (int16_t)a;
        g_q[k] = (int16_t)b;
        g_crudo[2U * k]      = g_i[k];
        g_crudo[2U * k + 1U] = g_q[k];
        }
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

/* Junta NFFT muestras pasando por el corrector si corrige!=0 y devuelve el
 * rechazo medido en dB. */
static float mide_rechazo(int corrige)
{
    int n = 0, k, bin_s, bin_e;
    float ps, pe;

    while (n < NFFT) {
        uint32_t m;
        bloque();
        iqbal_mide(g_i, g_q, NB);
        if (corrige) { iqbal_aplica(g_i, g_q, NB); }
        for (m = 0U; m < NB && n < NFFT; m++, n++) {
            /* ventana de Hann: sin ella, las faldas de la portadora tapan
             * el espejo y el numero sale inventado */
            float wv = 0.5f - 0.5f * cosf(2.0f * 3.14159265358979f * (float)n
                                          / (float)(NFFT - 1));
            fr[n] = (float)g_i[m] * wv;
            fi[n] = (float)g_q[m] * wv;
        }
    }
    fft();

    bin_s = (int)(g_f_hz / FS * (float)NFFT + 0.5f);
    bin_e = NFFT - bin_s;
    ps = 0.0f; pe = 0.0f;
    for (k = -2; k <= 2; k++) {
        int a = (bin_s + k + NFFT) % NFFT;
        int b = (bin_e + k + NFFT) % NFFT;
        ps += fr[a] * fr[a] + fi[a] * fi[a];
        pe += fr[b] * fr[b] + fi[b] * fi[b];
    }
    if (pe <= 0.0f) { return 99.0f; }
    return 10.0f * log10f(ps / pe);
}

/* deja el estimador asentado antes de medir */
static void asienta(void)
{
    uint32_t b;
    for (b = 0U; b < NBLO; b++) {
        bloque();
        iqbal_mide(g_i, g_q, NB);
    }
}

/* iqbal.c llama a iqa_set_on() para que la prueba A/B toque los DOS
 * correctores. Aqui se cuenta cuantas veces, que es la unica forma de que el
 * banco vea que no se le ha olvidado ninguna de las cuatro llamadas. */
static int g_iqa_on = -1, g_iqa_veces = 0;
void    iqa_set_on(uint8_t on) { g_iqa_on = on ? 1 : 0; g_iqa_veces++; }
uint8_t iqa_get_on(void)       { return (uint8_t)(g_iqa_on > 0); }

int main(void)
{
    char det[96];
    float sin_c, con_c;

    printf("\nEL ESPEJO: CUANTO LO BAJA EL CORRECTOR\n\n");

    printf("  desequilibrio de %.1f grados y %.2f dB, portadora en +%.0f Hz\n",
           (double)g_fase_grados, (double)g_gan_db, (double)g_f_hz);

    iqbal_init();
    iqbal_set_on(0U);
    asienta();
    sin_c = mide_rechazo(0);

    snprintf(det, sizeof det, "%.1f dB", (double)sin_c);
    di("sin corregir el espejo se ve (rechazo < 35 dB)", sin_c < 35.0f, det);

    iqbal_set_on(1U);
    con_c = mide_rechazo(1);
    snprintf(det, sizeof det, "%.1f dB (antes %.1f)", (double)con_c, (double)sin_c);
    di("corrigiendo, el rechazo pasa de 50 dB", con_c > 50.0f, det);
    snprintf(det, sizeof det, "gana %.1f dB", (double)(con_c - sin_c));
    di("y gana mas de 20 dB", (con_c - sin_c) > 20.0f, det);

    snprintf(det, sizeof det, "%.2f grados (de verdad: %.2f)",
             (double)iqbal_fase_grados(), (double)g_fase_grados);
    di("la fase medida se parece a la de verdad (0,3 grados)",
       fabsf(iqbal_fase_grados() - g_fase_grados) < 0.3f, det);
    /* iqbal_gan_db() devuelve el desequilibrio MEDIDO, no la correccion que
     * se aplica: tienen signo contrario y es facil restarlos al reves. */
    snprintf(det, sizeof det, "%.2f dB (de verdad: %.2f)",
             (double)iqbal_gan_db(), (double)g_gan_db);
    di("y la ganancia tambien (0,1 dB)",
       fabsf(iqbal_gan_db() - g_gan_db) < 0.1f, det);
    snprintf(det, sizeof det, "estimado %.1f dB, medido %.1f dB",
             (double)iqbal_rechazo_db(), (double)sin_c);
    di("y el rechazo que calcula cuadra con el medido (3 dB)",
       fabsf(iqbal_rechazo_db() - sin_c) < 3.0f, det);

    printf("\n  con la entrada YA equilibrada no puede empeorarla\n");
    {
        float a, b;
        g_fase_grados = 0.0f; g_gan_db = 0.0f;
        iqbal_init(); iqbal_set_on(0U); asienta();
        a = mide_rechazo(0);
        iqbal_set_on(1U);
        b = mide_rechazo(1);
        snprintf(det, sizeof det, "%.1f dB", (double)a);
        di("equilibrada, el espejo ya no esta", a > 55.0f, det);
        snprintf(det, sizeof det, "%.1f dB (antes %.1f)", (double)b, (double)a);
        di("y con corrector sigue sin estar (pierde < 6 dB)", b > a - 6.0f, det);
    }

    printf("\n  cuanto espejo queda segun el error de partida\n");
    {
        float fases[4] = { 1.0f, 3.0f, 6.0f, 10.0f };
        int k;
        for (k = 0; k < 4; k++) {
            float a, b;
            g_fase_grados = fases[k]; g_gan_db = 0.8f;
            iqbal_init(); iqbal_set_on(0U); asienta();
            a = mide_rechazo(0);
            iqbal_set_on(1U);
            b = mide_rechazo(1);
            printf("      %4.1f grados:  sin %5.1f dB   con %5.1f dB   gana %+5.1f\n",
                   (double)fases[k], (double)a, (double)b, (double)(b - a));
        }
    }

    printf("\n  con la portadora en el centro exacto (el caso que mas asusta)\n");
    {
        float a, b;
        g_f_hz = 0.0f; g_fase_grados = 3.0f; g_gan_db = 0.8f;
        iqbal_init(); iqbal_set_on(0U); asienta();
        a = mide_rechazo(0);
        iqbal_set_on(1U);
        b = mide_rechazo(1);
        printf("      en DC:  sin %.1f dB   con %.1f dB\n", (double)a, (double)b);
        /*
         * En continua la señal y su espejo son el MISMO bin, asi que el
         * numero no significa nada; lo que se vigila es que el corrector no
         * se vuelva loco persiguiendo una correlacion que no es suya.
         */
        snprintf(det, sizeof det, "theta %.3f, g %.3f",
                 (double)iqbal_theta(), (double)iqbal_g());
        di("en DC no se dispara (theta < 0,5 y g entre 0,5 y 2)",
           fabsf(iqbal_theta()) <= 0.5f && iqbal_g() >= 0.5f && iqbal_g() <= 2.0f,
           det);
        (void)a; (void)b;
    }

    /*
     * EL ESPEJO LEIDO DEL ESPECTRO.
     *
     * Pedido por el usuario.
     *
     * El medidor de arriba promedia los 96 kHz enteros y puede salir limpio
     * mientras en la pantalla hay un espejo gordo, si el desequilibrio
     * depende de la frecuencia. iqbal_mira_espectro() mide lo otro: el pico
     * contra su bin simetrico. Aqui se le dan espectros FABRICADOS, con el
     * rechazo puesto a mano, y tiene que devolver ese numero.
     *
     * Y las dos formas de equivocarse que importan: con ruido plano NO
     * puede inventarse un rechazo -dos picos de ruido comparados dan un
     * numero perfectamente formado que no significa nada-, y la continua del
     * centro no puede confundirse con una señal, porque su "espejo" es ella
     * misma y daria 0 dB siempre.
     */
    printf("\n  el espejo leido del espectro\n");
    {
        #define NSP 256
        static float sp[NSP];
        int k, c = NSP / 2;
        float niveles[4] = { 10.0f, 25.0f, 40.0f, 60.0f };
        int j;

        for (j = 0; j < 4; j++) {
            for (k = 0; k < NSP; k++) { sp[k] = -100.0f; }
            sp[c + 37] = -20.0f;                    /* la señal */
            sp[c - 37] = -20.0f - niveles[j];       /* su espejo */
            iqbal_mira_espectro(sp, (uint16_t)NSP);
            snprintf(det, sizeof det, "puesto %.0f dB, medido %.1f dB, bin %+d",
                     (double)niveles[j], (double)iqbal_espejo_db(),
                     (int)iqbal_espejo_bin());
            di("saca el rechazo que se le ha puesto (2 dB)",
               fabsf(iqbal_espejo_db() - niveles[j]) < 2.0f, det);
        }

        /* el espejo en el suelo: el rechazo es "por lo menos" el margen
         * hasta el suelo, nunca mas */
        for (k = 0; k < NSP; k++) { sp[k] = -100.0f; }
        sp[c + 37] = -20.0f;
        iqbal_mira_espectro(sp, (uint16_t)NSP);
        snprintf(det, sizeof det, "%.1f dB", (double)iqbal_espejo_db());
        di("con el espejo en el suelo, 80 dB y no mas",
           iqbal_espejo_db() > 75.0f && iqbal_espejo_db() < 85.0f, det);

        /* ruido plano: no hay nada que medir */
        for (k = 0; k < NSP; k++) {
            s_rnd = s_rnd * 1103515245u + 12345u;
            sp[k] = -100.0f + 3.0f * (float)((s_rnd >> 16) & 0xFFu) / 255.0f;
        }
        iqbal_mira_espectro(sp, (uint16_t)NSP);
        snprintf(det, sizeof det, "%.1f dB", (double)iqbal_espejo_db());
        di("con ruido plano no se inventa un rechazo", iqbal_espejo_db() == 0.0f, det);

        /* una portadora solo en el centro: es la continua, no una señal */
        for (k = 0; k < NSP; k++) { sp[k] = -100.0f; }
        sp[c] = 0.0f; sp[c + 1] = -10.0f; sp[c - 1] = -10.0f;
        iqbal_mira_espectro(sp, (uint16_t)NSP);
        snprintf(det, sizeof det, "%.1f dB", (double)iqbal_espejo_db());
        di("la continua del centro no cuenta como señal",
           iqbal_espejo_db() == 0.0f, det);
    }

    /*
     * LA PRUEBA A/B, QUE LA HACE LA RADIO.
     *
     * *** Despues de tres mensajes pidiendole numeros de la pantalla: "eres
     * un coñazo". ***
     *
     * Se le dan espectros FABRICADOS que cambian segun el corrector este
     * encendido o apagado -que es lo que pasa en la radio, porque el espectro
     * se calcula sobre las muestras ya corregidas-, se lanza la prueba y
     * tiene que salir con los dos numeros y con el corrector como estaba.
     *
     * Los tres fallos que importan, y que un banco flojo no veria:
     *   - que se quede el corrector donde lo dejo la prueba;
     *   - que de un resultado cuando no habia señal en una de las dos fases;
     *   - que un segundo toque a mitad de la prueba la reinicie y mezcle las
     *     dos fases.
     */
    printf("\n  la prueba A/B la hace la radio\n");
    {
        #define NSP2 256
        static float sp[NSP2];
        int k, c = NSP2 / 2, v;

        /* el espectro que ve la radio: con el corrector encendido, el espejo
         * baja de 14 a 38 dB; apagado, se queda en 14 */
        #define PINTA_SP(mejor) do {                                    \
            for (k = 0; k < NSP2; k++) { sp[k] = -100.0f; }             \
            sp[c + 40] = -20.0f;                                        \
            sp[c - 40] = (mejor) ? (-20.0f - 38.0f) : (-20.0f - 14.0f); \
        } while (0)

        iqbal_init();
        iqbal_set_on(1U);
        /* asentar el estimador para que la fila no diga "midiendo" */
        for (k = 0; k < 10; k++) { bloque(); iqbal_mide(g_i, g_q, NB); }

        di("antes de lanzarla no hay resultado", !iqbal_ab_hay(), 0);
        di("ni esta corriendo", iqbal_ab_corriendo() == 0U, 0);

        iqbal_ab_lanza();
        di("al lanzarla, apaga el corrector", iqbal_get_on() == 0U, 0);
        di("y tambien el de banda ancha", g_iqa_on == 0, 0);

        for (v = 0; v < 200 && iqbal_ab_corriendo(); v++) {
            PINTA_SP(iqbal_get_on());
            iqbal_mira_espectro(sp, (uint16_t)NSP2);
            iqbal_ab_paso();
        }
        snprintf(det, sizeof det, "%d cuadros", v);
        di("termina sola en menos de 200 cuadros", iqbal_ab_corriendo() == 0U, det);
        di("y deja el corrector como estaba", iqbal_get_on() == 1U, 0);
        di("y el de banda ancha tambien", g_iqa_on == 1, 0);
        di("hay resultado", iqbal_ab_hay(), 0);
        snprintf(det, sizeof det, "off %.1f dB -> on %.1f dB",
                 (double)iqbal_ab_off(), (double)iqbal_ab_on());
        di("apagado mide 14 dB (1 dB)", fabsf(iqbal_ab_off() - 14.0f) < 1.0f, det);
        di("encendido mide 38 dB (1 dB)", fabsf(iqbal_ab_on() - 38.0f) < 1.0f, det);

        /* Y el caso de "no se mueve": una emisora en el bin espejo, que no
         * cambia al corregir. La prueba tiene que dar los dos iguales, que
         * es exactamente la respuesta util. */
        iqbal_ab_lanza();
        for (v = 0; v < 200 && iqbal_ab_corriendo(); v++) {
            for (k = 0; k < NSP2; k++) { sp[k] = -100.0f; }
            sp[c + 40] = -20.0f;
            sp[c - 40] = -34.0f;        /* fijo: no depende del corrector */
            iqbal_mira_espectro(sp, (uint16_t)NSP2);
            iqbal_ab_paso();
        }
        snprintf(det, sizeof det, "off %.1f -> on %.1f",
                 (double)iqbal_ab_off(), (double)iqbal_ab_on());
        di("con una emisora en el bin espejo, los dos iguales",
           fabsf(iqbal_ab_off() - iqbal_ab_on()) < 0.5f, det);

        /* Sin señal en ninguna fase: NO puede dar resultado. Promediar ceros
         * daria "0 dB de rechazo", que se leeria como catastrofe. */
        iqbal_ab_lanza();
        for (v = 0; v < 200 && iqbal_ab_corriendo(); v++) {
            for (k = 0; k < NSP2; k++) { sp[k] = -100.0f; }
            iqbal_mira_espectro(sp, (uint16_t)NSP2);
            iqbal_ab_paso();
        }
        di("sin señal no da resultado", !iqbal_ab_hay(), 0);
        di("y aun asi deja el corrector como estaba", iqbal_get_on() == 1U, 0);
        di("y el de banda ancha tambien", g_iqa_on == 1, 0);

        /* Un segundo toque a mitad no la reinicia. */
        iqbal_ab_lanza();
        for (v = 0; v < 25; v++) {
            PINTA_SP(iqbal_get_on());
            iqbal_mira_espectro(sp, (uint16_t)NSP2);
            iqbal_ab_paso();
        }
        {
            uint8_t fase = iqbal_ab_corriendo();
            iqbal_ab_lanza();      /* segundo toque */
            di("un segundo toque no reinicia la prueba",
               iqbal_ab_corriendo() == fase, 0);
        }
        for (v = 0; v < 200 && iqbal_ab_corriendo(); v++) {
            PINTA_SP(iqbal_get_on());
            iqbal_mira_espectro(sp, (uint16_t)NSP2);
            iqbal_ab_paso();
        }
        di("y acaba igual", iqbal_ab_hay() && iqbal_get_on() == 1U, 0);

        /*
         * EL PLAZO DE SEGURIDAD, QUE ES EL QUE HABRIA PILLADO EL FALLO.
         *
         * Pedido por el usuario.
         *
         * iqbal_ab_paso() se llamaba desde dentro de un bloque que se salta
         * entero mientras hay un menu abierto, y la fila desde la que se
         * lanza la prueba ESTA en un menu. La prueba no avanzaba un cuadro y
         * dejaba el corrector apagado.
         *
         * El banco de arriba no podia verlo, porque llama a iqbal_ab_paso()
         * el mismo: el fallo era que no la llamaba nadie. Esto si lo ve. Se
         * lanza la prueba y NO se le da un solo paso -se simula justo lo que
         * pasaba-, solo bloques de audio, y la radio tiene que recuperarse
         * sola.
         */
        iqbal_ab_lanza();
        di("lanzada, el corrector queda apagado", iqbal_get_on() == 0U, 0);
        for (v = 0; v < 3000 && iqbal_ab_corriendo(); v++) {
            bloque();
            iqbal_bloque((int16_t *)0, 0U);   /* solo para contar el plazo */
            iqbal_bloque(g_crudo, NB);
        }
        snprintf(det, sizeof det, "%d bloques", v);
        di("sin que nadie la mueva, se aborta sola", iqbal_ab_corriendo() == 0U, det);
        di("y devuelve el corrector a como estaba", iqbal_get_on() == 1U, 0);
        di("y el de banda ancha tambien", g_iqa_on == 1, 0);
        di("sin dejar un resultado inventado", !iqbal_ab_hay(), 0);
    }

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}
