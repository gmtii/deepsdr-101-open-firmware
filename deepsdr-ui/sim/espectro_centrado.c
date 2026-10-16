/*
 * LO QUE COSTARIA CENTRAR EL ESPECTRO EN LA SINTONIA A 1X - 13/10/2026.
 *
 * *** De dos usuarios: "con el NCO activado en Spectrum, el indicador de la
 * frecuencia de sintonia solo se desplaza desde el centro hacia el borde
 * derecho"; y "it's possible to configure the NCO button so that pressing it
 * moves the aiming reticle ... to the top. In principle, that would probably
 * be the correct way to do it". ***
 *
 * POR QUE ESTA DESCENTRADO. A 1X el espectro se dibuja centrado en el
 * OSCILADOR, y la marca de sintonia se pinta a la distancia real
 * (center_mark_offset_px = mix_hz * W / span, en main.c). Sin NCO esa
 * distancia es Fs/4 fija -tres cuartos de pantalla-; con NCO pasea entre 6 y
 * 40 kHz POR ENCIMA del oscilador, nunca por debajo, porque la fuga del
 * oscilador se automezcla a 0 Hz y cruzarla metería una portadora que no esta
 * en el aire. O sea que la marca vive siempre en la mitad derecha, y eso es
 * geometria, no un defecto.
 *
 * A zoom >1X ya esta centrada: zoom_process_block() rota el bloque con
 * nco_mezcla() ANTES de diezmar -hace falta, si no el antialias se come la
 * propia emisora- y entonces la marca se dibuja en el centro del panel.
 *
 * LA PREGUNTA QUE MIDE ESTE BANCO. Hacer lo mismo a 1X es llamar a
 * nco_mezcla() sobre los 256 pares de CADA bloque crudo, en el camino del
 * espectro. ¿Cabe?
 *
 * CONTRA QUE SE MIDE. No contra una constante inventada: contra
 * fft_compute_db_iq(), que es la otra cosa que corre en ese mismo camino por
 * cada ventana y que YA cabe en el presupuesto de cuadro. La radio ademas se
 * cronometra a si misma esa FFT con el DWT (fft_us en main.c), asi que el
 * numero de aqui tiene con que compararse en el aparato de verdad.
 *
 * Esto es un PC, no un Cortex-M4: el valor absoluto no sirve. Lo que sirve es
 * la RAZON entre las dos, que es lo que se transporta.
 */
#include "nco.h"
#include "fft.h"
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

static int fallos = 0;
static void di(const char *que, int ok, const char *det)
{
    printf("    %-50s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

#define N        256          /* SDR_RX_BLOCK_SAMPLES */
#define FS       96000.0f
#define BLOQUES  22500L       /* 60 s a 96 kHz en bloques de 256 */

static double ahora(void) { return (double)clock() / (double)CLOCKS_PER_SEC; }

static float  g_i[N], g_q[N];
static int16_t g_ii[FFT_SIZE], g_qq[FFT_SIZE];
static float  g_db[FFT_BINS_IQ];

int main(void)
{
    nco_t osc;
    long k;
    double t0, t_mez = 0.0, t_fft = 0.0;
    double razon, por_bloque_rel;
    char det[160];
    int n;

    printf("\nCENTRAR EL ESPECTRO EN LA SINTONIA A 1X: LO QUE COSTARIA\n");
    printf("========================================================\n\n");

    nco_init();
    nco_freq(&osc, 24000.0f, FS);

    for (n = 0; n < N; n++) {
        g_i[n] = 1000.0f * cosf(6.2831853f * 0.1f * (float)n);
        g_q[n] = 1000.0f * sinf(6.2831853f * 0.1f * (float)n);
    }
    for (n = 0; n < FFT_SIZE; n++) {
        g_ii[n] = (int16_t)(1000.0 * cos(6.2831853 * 0.1 * n));
        g_qq[n] = (int16_t)(1000.0 * sin(6.2831853 * 0.1 * n));
    }

    /* 1. la rotacion, una por bloque crudo */
    t0 = ahora();
    for (k = 0; k < BLOQUES; k++) {
        nco_mezcla(&osc, g_i, g_q, (uint32_t)N);
    }
    t_mez = ahora() - t0;

    /* 2. la FFT, una por ventana. A 1X la ventana es FFT_SIZE=256 muestras,
     *    o sea UNA por bloque crudo: la misma cuenta de llamadas. */
    t0 = ahora();
    for (k = 0; k < BLOQUES; k++) {
        fft_compute_db_iq(g_ii, g_qq, g_db);
    }
    t_fft = ahora() - t0;

    printf("  60 s a 96 kHz = %ld bloques de %d pares\n\n", BLOQUES, N);
    printf("    nco_mezcla()        la rotacion que haria falta  %8.1f ms\n", t_mez * 1e3);
    printf("    fft_compute_db_iq() lo que YA corre ahi al lado  %8.1f ms\n", t_fft * 1e3);

    razon = (t_fft > 0.0) ? (t_mez / t_fft) : 0.0;
    printf("\n    la rotacion cuesta %.2f veces lo que la FFT\n", razon);

    por_bloque_rel = razon;   /* misma cuenta de llamadas: 1 y 1 por bloque */

    snprintf(det, sizeof det, "(%.0f %% de lo que cuesta la FFT)", 100.0 * razon);
    di("la rotacion cuesta MENOS que la FFT de su lado", razon < 1.0, det);

    /*
     * EL TOPE, Y POR QUE ESE. La FFT ya cabe en el presupuesto de cuadro, y
     * ademas la radio la cronometra sola. Que la rotacion sea menos de la
     * MITAD de la FFT significa que anadirla sube el coste del camino del
     * espectro menos de un 50 %: queda margen para que en el Cortex-M4, donde
     * las dos escalan distinto, siga cabiendo. Por encima de eso la decision
     * ya no se puede tomar desde un PC y habria que medirla con el DWT en la
     * radio antes de encenderlo para nadie.
     */
    snprintf(det, sizeof det, "(razon %.2f, el tope es 0,50)", por_bloque_rel);
    di("y menos de la mitad, que es donde se puede decidir desde aqui",
       por_bloque_rel < 0.50, det);

    /* 3. que la rotacion haga lo que dice: un tono a +24 kHz tiene que
     *    quedarse en continua despues de mezclar con -24 kHz. */
    {
        nco_t o2;
        double sum_i = 0.0, sum_q = 0.0, mod;
        nco_init();
        /*
         * EL SIGNO ES EL DE LA RADIO, NO EL QUE PARECE - 13/10/2026.
         *
         * La primera version de este banco pedia -24000 "para bajar el tono
         * de +24 kHz" y salio MAL: modulo 0,0 de 1000. Y estaba bien que
         * saliera mal. nco_mezcla() hace i = vi*c + vq*s, q = vq*c - vi*s
         * con la fase subiendo, o sea que ya rota HACIA ABAJO: pidiendole
         * -24 kHz sube el tono a +48 kHz, que a 96 kHz de muestreo es
         * exactamente Nyquist, y la media de Nyquist sobre 256 muestras es
         * cero. O sea que el banco no media la rotacion: medía mi error de
         * signo, y por eso existe esta prueba al lado de la del coste.
         *
         * El signo bueno es el POSITIVO, y no me lo invento: es el que usa
         * zoom_process_block() en main.c, que llama
         * nco_freq(&s_zoom_nco, demod_am_get_mix_hz(), fs) con mix_hz tal
         * cual. Si algun dia alguien cambia el convenio de nco_mezcla(),
         * esta prueba se pone roja antes de que la radio se centre al reves.
         */
        nco_freq(&o2, 24000.0f, FS);
        for (n = 0; n < N; n++) {
            g_i[n] = 1000.0f * cosf(6.2831853f * (24000.0f / FS) * (float)n);
            g_q[n] = 1000.0f * sinf(6.2831853f * (24000.0f / FS) * (float)n);
        }
        nco_mezcla(&o2, g_i, g_q, (uint32_t)N);
        for (n = 0; n < N; n++) { sum_i += g_i[n]; sum_q += g_q[n]; }
        mod = sqrt(sum_i * sum_i + sum_q * sum_q) / (double)N;
        snprintf(det, sizeof det, "(modulo en continua %.1f de 1000)", mod);
        di("y rotando 24 kHz, el tono de +24 kHz cae en el centro",
           mod > 900.0, det);
    }

    printf("\n  Esto es un PC. El valor absoluto no se transporta; la razon\n");
    printf("  contra la FFT de al lado si, porque las dos corren en el mismo\n");
    printf("  sitio y la FFT ya cabe. La ultima palabra la tiene el DWT de la\n");
    printf("  radio (fft_us en main.c), no este banco.\n");

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}
