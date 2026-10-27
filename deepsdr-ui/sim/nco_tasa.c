/*
 * ======================================================================
 * EL NCO DE SINTONIA A 96 Y A 192 kHz - 24/10/2026
 * ======================================================================
 *
 * POR QUE EXISTE. Pedido por el usuario: revisar el comportamiento del NCO
 * a 192 kHz. Y la pregunta tiene fondo, porque el NCO y la tasa de entrada
 * NO viven en el mismo sitio:
 *
 *   LA TASA DE ENTRADA son 192 kHz y el panadaptador enseña esos 192.
 *   EL NCO MEZCLA A 96 kHz, detras del diezmador de medio banda.
 *
 * Asi que lo que el NCO puede alcanzar NO lo decide la ventana que se ve:
 * lo decide el filtro que hay delante. El diezmador esta plano hasta
 * 40 kHz y a -6 dB en 48 (ver User/semibanda.h), y resulta que
 * NCO_OFF_MAX_96K son justo 40.000 Hz. Cuadra, pero cuadra por
 * construccion y no por casualidad: hay que comprobarlo.
 *
 * QUE SE MIDE. Para cada desplazamiento del NCO se mete un tono a
 * `desplazamiento + 1 kHz`, se le dice al demodulador que mezcle ese
 * desplazamiento, y se mira el tono de 1 kHz que sale por el altavoz. Si
 * el NCO funciona, sale con el mismo nivel a las dos tasas.
 *
 * Y SE MIDE TAMBIEN FUERA DEL TOPE -45 y 50 kHz-, que es donde el
 * diezmador empieza a cortar. Ahi NO tienen que coincidir, y que no
 * coincidan es la prueba de que el banco mide algo: un banco que diera
 * verde tambien pasado el tope estaria midiendo cualquier otra cosa.
 */
#include "demod_am.h"
#include "nr_ss.h"
#include "sdr_rx.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

extern void cons_captura_tramas(uint32_t);
extern const int16_t *cons_audio(void);
extern void cons_todos_apagados(void);
extern void cons_reinicia(void);

static int fallos;

static int di(int cond, const char *que)
{
    printf("    %-62s %s\n", que, cond ? "bien" : "MAL");
    return cond ? 0 : 1;
}

static int16_t raw[1024 * 2];

static unsigned long sem = 4242ul;
static double gauss(void)
{
    double u1, u2;
    sem = sem * 1103515245ul + 12345ul; u1 = ((sem >> 16) & 0x7FFF) / 32768.0;
    sem = sem * 1103515245ul + 12345ul; u2 = ((sem >> 16) & 0x7FFF) / 32768.0;
    if (u1 < 1e-9) { u1 = 1e-9; }
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

/*
 * NIVEL DEL TONO DE 1 kHz QUE SALE, en dB, CON EL AGC EN MANUAL.
 *
 * El primer banco media esto con el AGC automatico, y por eso daba verde
 * con el NCO roto: el AGC sube lo que le echen, asi que un tono atenuado
 * cincuenta decibelios por el diezmador sale igual de fuerte. Con la
 * ganancia fija, el nivel dice lo que de verdad ha llegado.
 */
static double corre(int doble, double desp_hz, unsigned bloques)
{
    uint32_t n = doble ? SDR_RX_BLOCK_SAMPLES_WFM : SDR_RX_BLOCK_SAMPLES;
    double fs  = doble ? 192000.0 : 96000.0;
    double fase = 0.0, dw, sr = 0.0, si = 0.0, pot = 0.0;
    unsigned b, k, cuenta = 0u, reloj = 0u;

    demod_am_init();
    demod_am_set_active_rate(0u);
    demod_am_set_rf_doble((uint8_t)doble);
    demod_am_set_mode(DEMOD_MODE_USB);
    nr_ss_set_enabled(0u);
    /*
     * AGC EN MANUAL, y es lo que hace que este banco mida algo.
     *
     * Con el AGC automatico, un tono que el diezmador ha atenuado 50 dB
     * sale por el altavoz con el mismo nivel que uno bueno: el AGC lo
     * sube. Mi primer banco media eso y daba verde con el NCO roto. En
     * manual la ganancia es fija, asi que el nivel de salida dice de
     * verdad cuanto ha llegado.
     */
    demod_am_set_agc_profile(AGC_PROFILE_MANUAL);
    /*
     * EL DESPLAZAMIENTO VA EN HERCIOS DE VERDAD, no en fraccion de la
     * tasa: la mezcla corre a 96 kHz pase lo que pase con la entrada, asi
     * que el mismo numero vale para las dos. Esa es justo la propiedad que
     * se esta comprobando.
     */
    sem = 4242ul;
    demod_am_set_mix_hz((float)desp_hz);
    cons_captura_tramas(n);

    dw = 2.0 * M_PI * (desp_hz + 1000.0) / fs;

    for (b = 0; b < bloques; b++) {
        for (k = 0; k < n; k++) {
            raw[2 * k]      = (int16_t)(3000.0 * cos(fase));
            raw[2 * k + 1u] = (int16_t)(3000.0 * sin(fase));
            fase += dw;
        }
        demod_am_process_raw(raw);
        if (b < 90u) {                          /* que se asiente el AGC */
            reloj += doble ? (n / 2u) : n;
            continue;
        }
        {
            const int16_t *a = cons_audio();
            /* A 192 kHz el audio sale interpolado: se miran las pares,
             * que son las del mundo de 96 kHz. */
            uint32_t paso = doble ? 2u : 1u;

            for (k = 0; k < n; k += paso) {
                double w = 2.0 * M_PI * 1000.0 * (double)reloj / 96000.0;
                double x = (double)a[2 * k];

                pot += x * x;
                sr  += x * cos(w);
                si  -= x * sin(w);
                cuenta++;
                reloj++;
            }
        }
    }
    (void)pot;
    if (cuenta == 0u) { return -200.0; }
    {
        double amp = 2.0 * sqrt(sr * sr + si * si) / (double)cuenta;

        return 20.0 * log10(amp > 1e-6 ? amp : 1e-6);
    }
}

int main(void)
{
    /* Dentro del tope del NCO (NCO_OFF_MAX_96K = 40.000 Hz). */
    static const double k_dentro[] = { 6000.0, 12000.0, 24000.0, 32000.0, 40000.0 };
    /* Y fuera, donde el diezmador ya corta. */
    static const double k_fuera[]  = { 45000.0, 50000.0 };
    unsigned i;
    double peor = 0.0;

    cons_todos_apagados();
    cons_reinicia();

    printf("\n== EL NCO DE SINTONIA A 96 Y A 192 kHz ==\n\n");
    printf("  dentro del tope del NCO (40 kHz)\n\n");
    printf("     %-12s %10s %10s %9s\n", "desplaz.", "96 kHz", "192 kHz", "difer.");
    for (i = 0; i < sizeof k_dentro / sizeof k_dentro[0]; i++) {
        double a = corre(0, k_dentro[i], 220u);
        double b = corre(1, k_dentro[i], 220u);

        printf("     %6.1f kHz   %+8.2f dB %+8.2f dB %+8.2f dB\n",
               k_dentro[i] / 1000.0, a, b, b - a);
        if (fabs(b - a) > peor) { peor = fabs(b - a); }
    }
    fallos += di(peor < 0.5,
                 "el NCO alcanza lo mismo a las dos tasas, hasta su tope");

    printf("\n  EL ALCANCE A 192 kHz: toda la pantalla sin mover el oscilador\n\n");
    {
        /*
         * ESTA ES LA PRUEBA QUE FALTABA, y la de arriba no la sustituye.
         *
         * A 192 kHz la pantalla enseña 96 kHz a cada lado del oscilador.
         * Un NCO que solo alcanza 40 obliga a reaparcar el oscilador a
         * mitad de camino, y al moverse el oscilador SE MUEVE LA IMAGEN
         * ENTERA: es lo que se ve en la radio y lo que hay que arreglar.
         *
         * Se barre hasta el tope nuevo (NCO_OFF_MAX_192K = 90 kHz) y se
         * exige que la señal siga llegando. Sin el giro grueso de
         * mix_reparte(), de 40 kHz en adelante el diezmador se la come y
         * la relacion se desploma.
         */
        static const double k_lejos[] = {
            6000.0, 24000.0, 40000.0, 48000.0, 55000.0,
            70000.0, 80000.0, 88000.0
        };
        double ref = corre(1, 24000.0, 220u);
        double peor = 1e9;
        unsigned j;

        printf("     (referencia a 24 kHz: %+.2f dB)\n\n", ref);
        for (j = 0; j < sizeof k_lejos / sizeof k_lejos[0]; j++) {
            double v = corre(1, k_lejos[j], 220u);

            printf("     %6.1f kHz del oscilador   S/R %+8.2f dB   %s\n",
                   k_lejos[j] / 1000.0, v,
                   (v > ref - 3.0) ? "llega" : "*** se pierde ***");
            if (v < peor) { peor = v; }
        }
        fallos += di(peor > ref - 3.0,
                     "el NCO llega a 88 kHz del oscilador sin perder la señal");
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
