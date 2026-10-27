/*
 * ======================================================================
 * EL REDUCTOR DE RUIDO A 96 Y A 192 kHz, LADO A LADO - 24/10/2026
 * ======================================================================
 *
 * POR QUE EXISTE. Avisado por el usuario: a 192 kHz el reductor parece
 * reducir menos. En teoria no puede: trabaja a 12 kHz, detras del
 * diezmador, y a esas alturas la cadena ve 96 kHz pase lo que pase con la
 * tasa de entrada. Pero "en teoria no puede" es exactamente lo que dije
 * del coste del diezmador, asi que esto lo MIDE.
 *
 * LO DIFICIL ES QUE LA ENTRADA SEA LA MISMA, y tiene dos trampas en las
 * que cai las dos veces antes de que el banco dijera algo cierto:
 *
 *   1) RUIDO BLANCO CON LA MISMA SIGMA NO ES EL MISMO RUIDO. A 192 kHz la
 *      misma potencia se reparte entre el doble de banda, asi que DENTRO
 *      del canal hay la mitad. Sin el factor raiz(2), 192 sale 3 dB mejor
 *      y el banco da por bueno lo que no ha probado.
 *
 *   2) LA FRECUENCIA INTERMEDIA BAJA SON 24 kHz EN LAS DOS, y no Fs/4 de
 *      la tasa de entrada. La bajada en frecuencia corre DETRAS del
 *      diezmador. Poniendo el tono en 48 kHz para el caso de 192 se mete
 *      en la zona de corte del diezmador -plano hasta 40 kHz, -6 dB en
 *      48- y el banco mide la atenuacion del filtro creyendo que mide
 *      ruido: salian 14 dB de peor relacion que no existian.
 *
 *   3) Y CON SOLO RUIDO NO SE MIDE NADA: el reductor baja el nivel, el
 *      AGC lo vuelve a subir y la cuenta sale igual. Hay que mirar la
 *      RELACION señal/ruido, que el AGC no puede cambiar porque multiplica
 *      a los dos por lo mismo.
 *
 * Y UN BARRIDO DE FUERZA, para que esto no sea un banco vacio: con la
 * fuerza por debajo de 1.500 el reductor no hace nada medible y las dos
 * tasas saldrian iguales por el motivo equivocado. A 3.000 si trabaja
 * -y a 4.000 se come el tono, que tambien conviene tener escrito-.
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

static uint32_t sem = 777u;
static double gauss(void)
{
    double u1, u2;
    sem = sem * 1103515245u + 12345u; u1 = ((sem >> 16) & 0x7FFF) / 32768.0;
    sem = sem * 1103515245u + 12345u; u2 = ((sem >> 16) & 0x7FFF) / 32768.0;
    if (u1 < 1e-9) u1 = 1e-9;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

static int16_t raw[1024 * 2];
static int fallos;

static int di(int cond, const char *que)
{
    printf("    %-62s %s\n", que, cond ? "bien" : "MAL");
    return cond ? 0 : 1;
}

static uint16_t g_fuerza = 50u;

/*
 * LA RELACION SEÑAL/RUIDO DEL AUDIO QUE SALE.
 *
 * Con ruido a secas no se mide nada: el reductor baja el nivel, el AGC lo
 * vuelve a subir y la cuenta sale igual. Lo que hay que mirar es la
 * RELACION, que el AGC no puede cambiar porque multiplica a los dos por
 * lo mismo.
 *
 * Se mete un tono en la FI baja mas mil hercios, asi que por el altavoz
 * sale un tono de 1 kHz. La potencia del tono se saca con un detector
 * sincrono a 1 kHz; el ruido es todo lo demas.
 */
static double corre(int doble, double sigma, int nr, unsigned bloques,
                    double *snr_db)
{
    uint32_t n = doble ? SDR_RX_BLOCK_SAMPLES_WFM : SDR_RX_BLOCK_SAMPLES;
    double fs  = doble ? 192000.0 : 96000.0;
    double fase = 0.0, dw, pot = 0.0, sr = 0.0, si = 0.0;
    unsigned b, k, cuenta = 0u, reloj = 0u;

    demod_am_init();
    demod_am_set_active_rate(0u);
    demod_am_set_rf_doble((uint8_t)doble);
    demod_am_set_mode(DEMOD_MODE_USB);
    /*
     * LA FI BAJA SON 24 kHz EN LAS DOS, y no Fs/4 de la tasa de entrada.
     * La bajada en frecuencia corre DETRAS del diezmador, o sea en el
     * mundo de 96 kHz, asi que Fs/4 es 24.000 tambien a 192. Poner 48.000
     * en el caso de 192 metia el tono en la zona de corte del diezmador
     * -que esta plano hasta 40 kHz y a -6 dB en 48- y el banco medía la
     * atenuacion del filtro creyendo que medía ruido. Se vio como 14 dB
     * de peor relacion a 192, que no existian.
     */
    demod_am_set_mix_hz(24000.0f);
    nr_ss_set_enabled((uint8_t)nr);
    nr_ss_set_strength(g_fuerza);
    sem = 777u;
    cons_captura_tramas(n);

    /* El tono: FI baja + 1 kHz, para que el audio salga a 1 kHz. */
    dw = 2.0 * M_PI * (24000.0 + 1000.0) / fs;

    for (b = 0; b < bloques; b++) {
        for (k = 0; k < n; k++) {
            double i = 2000.0 * cos(fase) + sigma * gauss();
            double q = 2000.0 * sin(fase) + sigma * gauss();

            raw[2 * k]      = (int16_t)i;
            raw[2 * k + 1u] = (int16_t)q;
            fase += dw;
        }
        demod_am_process_raw(raw);
        if (b < 80u) {                          /* que se asienten AGC y NR */
            reloj += doble ? (n / 2u) : n;      /* el audio sale a 96 kHz */
            continue;
        }
        {
            const int16_t *a = cons_audio();
            /* A 192 kHz el audio sale interpolado: se miran las pares,
             * que son las originales del mundo de 96 kHz. */
            uint32_t paso = doble ? 2u : 1u;

            for (k = 0; k < n; k += paso) {
                double x = (double)a[2 * k];
                double w = 2.0 * M_PI * 1000.0 * (double)reloj / 96000.0;

                pot += x * x;
                sr  += x * cos(w);
                si  -= x * sin(w);
                cuenta++;
                reloj++;
            }
        }
    }
    if (cuenta == 0u) { *snr_db = 0.0; return 0.0; }
    {
        double media = pot / (double)cuenta;
        double amp   = 2.0 * sqrt(sr * sr + si * si) / (double)cuenta;
        double p_ton = amp * amp / 2.0;
        double p_rui = media - p_ton;

        if (p_rui < 1e-9) { p_rui = 1e-9; }
        *snr_db = 10.0 * log10(p_ton / p_rui);
        return p_rui;
    }
}

static double db(double p) { return 10.0 * log10(p > 1e-12 ? p : 1e-12); }

int main(void)
{
    const double S96 = 300.0;
    const double S192 = 300.0 * 1.41421356;   /* misma densidad en el canal */
    double sin96, con96, sin192, con192;

    cons_todos_apagados();
    cons_reinicia();

    printf("\n== EL REDUCTOR DE RUIDO A 96 Y A 192 kHz ==\n\n");

    {
        double s1, s2, s3, s4;
        static const uint16_t k_f[] = { 500u, 1500u, 3000u, 4000u };
        unsigned fi;

        printf("  (barrido de fuerza, para que el banco no mida un reductor apagado)\n\n");
        for (fi = 0; fi < sizeof k_f / sizeof k_f[0]; fi++) {
            double a, b2;

            g_fuerza = k_f[fi];
            (void)corre(0, S96, 0, 260u, &a);
            (void)corre(0, S96, 1, 260u, &b2);
            printf("     fuerza %3u  ->  a 96 kHz gana %5.2f dB\n",
                   (unsigned)k_f[fi], b2 - a);
        }
        g_fuerza = 3000u;
        printf("\n");

        sin96  = corre(0, S96,  0, 260u, &s1);
        con96  = corre(0, S96,  1, 260u, &s2);
        sin192 = corre(1, S192, 0, 260u, &s3);
        con192 = corre(1, S192, 1, 260u, &s4);

        printf("   96 kHz   S/R sin reductor %6.2f dB   con %6.2f dB   gana %5.2f dB\n",
               s1, s2, s2 - s1);
        printf("  192 kHz   S/R sin reductor %6.2f dB   con %6.2f dB   gana %5.2f dB\n",
               s3, s4, s4 - s3);
        printf("\n  ruido de partida, 192 contra 96: %+.2f dB\n",
               db(sin192) - db(sin96));
        printf("  lo que gana el reductor, 192 contra 96: %+.2f dB\n",
               (s4 - s3) - (s2 - s1));
        printf("\n");

        /*
         * MEDIO DECIBELIO DE MARGEN, y no cero: el ruido es aleatorio y
         * las dos tasas no ven la MISMA realizacion -a 192 kHz hacen
         * falta el doble de muestras para el mismo trozo de tiempo-, asi
         * que una diferencia de decimas es la semilla y no el firmware.
         * Lo que esto caza es un cambio de verdad: un filtro que atenue,
         * un nivel que se vaya, un diezmador que deje pasar algo.
         */
        fallos += di(fabs(db(sin192) - db(sin96)) < 0.5,
                     "el ruido que llega al reductor es el mismo a las dos tasas");
        fallos += di(fabs((s4 - s3) - (s2 - s1)) < 0.5,
                     "y el reductor gana lo mismo en las dos");
        /* Y que el barrido de fuerza no haya medido un reductor apagado. */
        fallos += di(s2 - s1 > 0.2,
                     "a 96 kHz el reductor hace algo medible (si no, no se mide nada)");
    }
    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
