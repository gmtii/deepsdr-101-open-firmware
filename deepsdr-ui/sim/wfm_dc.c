/*
 * ======================================================================
 * LA COMPONENTE DE CONTINUA ENCIMA DE LA PORTADORA, EN WFM - 11/10/2026
 * ======================================================================
 *
 * Pedido por el usuario.
 *
 * Eso no es una opinion, es una prediccion: dice que el estropicio depende
 * de la RELACION entre la portadora y el offset, no del offset a secas. Y
 * una prediccion asi se mide.
 *
 * POR QUE UN QUITADOR DE DC DETRAS NO SIRVE. La cadena de WFM de esta
 * radio es: deinterleave -> discriminador -> deenfasis -> filtro de audio
 * -> quitador de DC -> AGC -> salida. El quitador de DC esta en el AUDIO,
 * DESPUES del discriminador. Delante, sobre la I y la Q, no hay nada: el
 * filtro de canal previo (WFM_IFBW) esta por omision en WIDE, que no
 * filtra.
 *
 * Y un offset en I/Q no sale como un DC quitable al otro lado. El
 * discriminador calcula arg(z[n] . conj(z[n-1])). Sumarle un vector
 * constante D a z no desplaza la salida: la DISTORSIONA, porque el angulo
 * de A.exp(j.fi) + D no es fi mas una constante. Cuanto mas pequena es la
 * portadora frente a D, mas se tuerce. De ahi que subir la ganancia
 * ayude: sube A y D se queda donde estaba.
 *
 * ESTE BANCO COMPILA demod_am.c DEL FIRMWARE. Le mete por la entrada de
 * verdad -demod_wfm_process_raw(), muestras crudas de 16 bits I/Q
 * entrelazadas, igual que el codec- una portadora de FM modulada por un
 * tono, con un offset de continua conocido, y mide lo que sale por donde
 * sale el audio de verdad (gd32_i2s_stream_write_half).
 *
 * LO QUE SE MIDE es la parte de la salida que NO es el tono: se ajusta el
 * tono por correlacion, se resta, y lo que queda es lo que el
 * discriminador se ha inventado.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define FS        192000.0
#define NBLK      SDR_RX_BLOCK_SAMPLES_WFM     /* 512 */
#define F_AUDIO   1000.0
#define DESV      50000.0                      /* desviacion de pico, Hz */
#define BLOQUES   700


static int fallos = 0;
static void mal(const char *q) { printf("  MAL: %s\n", q); fallos++; }

/* La fase del generador se arrastra entre bloques: una portadora que
 * reempieza en cada bloque tendria un salto de fase cada 512 muestras y el
 * discriminador lo veria como un pinchazo. Eso mediria mi generador, no el
 * firmware. */
static double g_fi;
static double g_t;

static void bloque(int16_t *raw, double amp, double dc, double desv)
{
    uint32_t n;
    for (n = 0; n < (uint32_t)NBLK; n++) {
        double f = desv * sin(2.0 * M_PI * F_AUDIO * g_t / FS);
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

/* Devuelve por `tono` la amplitud del tono recuperado y por `basura` el
 * porcentaje de la salida que NO es ese tono. */
static void mide(double amp, double dc, double desv, double *tono, double *basura)
{
    static int16_t raw[2 * NBLK];
    int b;
    double sc = 0.0, ss = 0.0, e_tot = 0.0, nm = 0.0;

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_WFM);
    cons_captura_tramas((uint32_t)NBLK);
    g_fi = 0.0; g_t = 0.0;

    for (b = 0; b < BLOQUES; b++) {
        const int16_t *sal;
        uint32_t n;
        double t0 = g_t;

        bloque(raw, amp, dc, desv);
        demod_wfm_process_raw(raw);
        if (b < BLOQUES - 60) { continue; }

        sal = cons_audio();
        for (n = 0; n < (uint32_t)NBLK; n++) {
            double y = (double)sal[2U * n];
            double w = 2.0 * M_PI * F_AUDIO * (t0 + (double)n) / FS;
            sc += y * cos(w);
            ss += y * sin(w);
            e_tot += y * y;
            nm += 1.0;
        }
    }
    {
        double e_tono = 2.0 * (sc * sc + ss * ss) / nm;
        double e_resto = e_tot - e_tono;
        if (e_resto < 0.0) { e_resto = 0.0; }
        *tono   = sqrt(e_tono / nm);
        *basura = (e_tot > 0.0) ? (100.0 * e_resto / e_tot) : -1.0;
    }
}

int main(void)
{
    int k;

    printf("\n== LA CONTINUA ENCIMA DE LA PORTADORA, EN WFM ==\n\n");

    /*
     * (1) LO QUE SE DENUNCIO: una emisora que se debilita hasta quedar por
     * debajo del offset del convertidor.
     *
     * 300 cuentas de offset es un numero de hardware plausible. Lo que se
     * compara, para cada portadora, es la MISMA medida con y sin offset:
     * asi lo que se mide es el dano del offset y no el de la portadora
     * pequena, que son dos cosas distintas.
     */
    printf("  -- (1) emisora debil contra un offset de 300 cuentas --\n");
    printf("   portadora  dc/port   tono sin offset   tono con offset   basura\n");
    {
        static const double k_amp[] = { 4000.0, 1000.0, 600.0, 400.0,
                                        300.0, 200.0, 120.0 };
        for (k = 0; k < (int)(sizeof k_amp / sizeof k_amp[0]); k++) {
            double t_lim, b_lim, t_dc, b_dc;
            mide(k_amp[k], 0.0,   DESV, &t_lim, &b_lim);
            mide(k_amp[k], 300.0, DESV, &t_dc,  &b_dc);
            printf("   %9.0f %7.0f%% %17.0f %17.0f %8.1f%%\n",
                   k_amp[k], 100.0 * 300.0 / k_amp[k], t_lim, t_dc, b_dc);
            if (b_dc > b_lim + 2.0) {
                mal("el offset todavia se carga la emisora debil");
            }
            if (t_dc < 0.80 * t_lim) {
                mal("con offset se recupera mucho menos tono que sin el");
            }
        }
        printf("\n   Antes del 11/10/2026, con la portadora igual al offset\n"
               "   salia 98,8 %% de basura y la emisora no se oia.\n");
    }

    /*
     * (2) Y EL CASO CONTRARIO, que es el que casi se rompe arreglando el
     * primero: portadora FUERTE, offset CERO, modulacion cada vez mas
     * floja. Con beta pequena casi toda la senal esta en continua, asi que
     * un quitador sin tope se la lleva. Ver WFM_DC_TOPE en demod_am.c.
     */
    printf("\n  -- (2) portadora fuerte sin offset, modulacion floja --\n");
    printf("   desviacion    beta   tono    basura\n");
    {
        static const double k_desv[] = { 10000.0, 2000.0, 800.0, 400.0, 200.0 };
        for (k = 0; k < (int)(sizeof k_desv / sizeof k_desv[0]); k++) {
            double t, b;
            mide(8000.0, 0.0, k_desv[k], &t, &b);
            printf("   %10.0f %7.1f %6.0f %8.1f%%\n",
                   k_desv[k], k_desv[k] / F_AUDIO, t, b);
            if (b > 2.0) {
                mal("el quitador se esta comiendo una portadora poco modulada");
            }
        }
        printf("\n   Sin el tope esto llegaba al 44,8 %% de basura. El tope\n"
               "   acota la correccion a lo que un offset puede SER.\n");
    }

    /*
     * (3) Y QUE LO QUE QUITA SEA LO QUE HAY. Una estimacion que converja a
     * otra cosa dejaria los dos bancos de arriba en verde mientras se come
     * la senal por otro lado.
     */
    printf("\n  -- (3) lo que el firmware dice que esta restando --\n");
    {
        static const double k_dc[] = { 0.0, 120.0, 300.0 };
        for (k = 0; k < (int)(sizeof k_dc / sizeof k_dc[0]); k++) {
            float di = 0.0f, dq = 0.0f;
            double t, b;
            mide(2000.0, k_dc[k], DESV, &t, &b);
            demod_wfm_get_dc(&di, &dq);
            printf("     se le metio %5.0f  ->  dice %7.1f (I)  %7.1f (Q)\n",
                   k_dc[k], (double)di, (double)dq);
            if (fabs((double)di - k_dc[k]) > 0.15 * k_dc[k] + 160.0) {
                mal("la estimacion no converge al offset que se le metio");
            }
        }
        printf("\n     El resto hasta el valor exacto es el termino J0 de la\n"
               "     propia FM -la portadora residual, que tambien vive en\n"
               "     continua-. Por eso la correccion lleva tope.\n");
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
