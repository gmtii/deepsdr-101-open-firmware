/*
 * ======================================================================
 * LA FUGA DEL OSCILADOR EN LOS PASOS PEGADOS AL CENTRO - 25/10/2026
 * ======================================================================
 *
 * Reportado por un usuario: "se oyen artefactos en los pasos de sintonia
 * pegados a la frecuencia intermedia cero".
 *
 * QUE ESTABA PASANDO. El oscilador local se auto-mezcla consigo mismo y
 * deja una CONTINUA encima de la I y de la Q. Esta radio la aparta
 * sintonizando siempre a una distancia del oscilador -NCO_OFF_MIN_HZ, seis
 * kilohercios- y confiando en que el filtro de canal la mate. El
 * comentario que lo justifica habla de "el filtro de canal es de +-4 kHz",
 * que era el de AM. Despues llegaron dos filtros mas anchos y nadie volvio
 * a mirar el numero: a 6 kHz, NFM deja pasar la fuga a -2,3 dB y AIS a
 * -0,1 dB. O sea entera.
 *
 * Y EN FM UNA CONTINUA NO ES UN ZUMBIDO, ES DISTORSION. El discriminador
 * saca arg(z[n] . conj(z[n-1])); sumarle un vector constante D a z no
 * desplaza la salida, la TUERCE. Es exactamente el mismo mecanismo que
 * sim/wfm_dc.c midio en la cadena de FM ancha el 11/10/2026, un piso mas
 * abajo: alli la portadora vive en continua y el arreglo necesita un tope;
 * aqui hay mezclador y la señal buena esta siempre a 6 kHz o mas, asi que
 * la continua se puede restar sin ambiguedad ninguna.
 *
 * ESTE BANCO COMPILA demod_am.c DEL FIRMWARE. Le mete por la entrada de
 * verdad -demod_am_process_raw(), muestras crudas de 16 bits I/Q
 * entrelazadas, igual que el codec- una portadora en su FI baja con una
 * continua conocida encima, y lee por donde sale el audio de verdad.
 *
 * LO QUE SE MIDE es la parte de la salida que NO es el tono: se ajusta el
 * tono por correlacion, se resta, y lo que queda es lo que la fuga se ha
 * inventado. Mismo metodo que sim/wfm_dc.c, para que los dos numeros se
 * puedan comparar.
 *
 * LO QUE MIDIO, con 300 cuentas de fuga y antes del arreglo:
 *
 *     NFM, emisora de 400 cuentas, a  6 kHz   45,7 %% de basura
 *                                  a  8 kHz   11,1 %%
 *                                  a 12 kHz    0,1 %%
 *                                  a 24 kHz    0,0 %%
 *
 * O sea exactamente lo que se denuncio: SOLO en los pasos pegados al
 * centro, y peor cuanto mas floja la emisora. Restando la continua sale
 * 0,0 %% en los cuatro.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define FS        96000.0
#define NBLK      SDR_RX_BLOCK_SAMPLES          /* 256 */
#define F_AUDIO   1100.0
#define DESV      3000.0                        /* desviacion de pico en NFM */
#define BLOQUES   900

static int fallos = 0;
static void mal(const char *q) { printf("    MAL: %s\n", q); fallos++; }

static double g_fi;
static double g_t;

/* Un bloque de I/Q crudas: portadora a `off` hercios del centro -o sea
 * donde la deja el NCO-, modulada en frecuencia por un tono, con `dc`
 * cuentas de continua encima. En AM la modulacion va en la envolvente. */
static void bloque(int16_t *raw, double amp, double dc, double off,
                   double desv, int am)
{
    uint32_t n;

    for (n = 0; n < (uint32_t)NBLK; n++) {
        double m = sin(2.0 * M_PI * F_AUDIO * g_t / FS);
        double env = am ? (0.6 + 0.4 * m) : 1.0;
        double i, q;

        i = amp * env * cos(g_fi) + dc;
        q = amp * env * sin(g_fi) + dc;
        g_fi += 2.0 * M_PI * (off + (am ? 0.0 : desv * m)) / FS;
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
static void mide(demod_mode_t modo, double amp, double dc, double off,
                 double *tono, double *basura)
{
    static int16_t raw[2 * NBLK];
    int b;
    int am = (modo == DEMOD_MODE_AM) ? 1 : 0;
    double sc = 0.0, ss = 0.0, e_tot = 0.0, nm = 0.0;

    demod_am_init();
    demod_am_set_active_rate(0U);
    demod_am_set_rf_doble(0U);
    demod_am_set_mode(modo);
    demod_am_set_mix_hz((float)off);
    cons_reinicia();
    cons_captura_tramas((uint32_t)NBLK);
    g_fi = 0.0; g_t = 0.0;

    for (b = 0; b < BLOQUES; b++) {
        const int16_t *sal;
        uint32_t n;
        double t0 = g_t;

        bloque(raw, amp, dc, off, DESV, am);
        demod_am_process_raw(raw);
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

    printf("\n== LA FUGA DEL OSCILADOR EN LOS PASOS PEGADOS AL CENTRO ==\n");

    /*
     * (1) NFM, QUE ES DONDE MAS SE NOTA: su filtro de canal corta a 6,25
     * kHz, o sea que con el desplazamiento en su minimo de 6 kHz la fuga
     * le entra entera. Para cada distancia se mide lo mismo con y sin
     * continua: asi lo que se compara es el daño de la fuga y no el de la
     * distancia, que son dos cosas distintas.
     */
    printf("\n  -- (1) NFM en el paso pegado al centro (6 kHz), 300 de fuga --\n");
    printf("     portadora  fuga/port   basura sin fuga   basura con fuga\n");
    {
        static const double k_amp[] = { 6000.0, 2000.0, 1000.0,
                                        600.0, 400.0, 300.0 };
        for (k = 0; k < (int)(sizeof k_amp / sizeof k_amp[0]); k++) {
            double t0, b0, t1, b1;

            mide(DEMOD_MODE_NFM, k_amp[k],   0.0, 6000.0, &t0, &b0);
            mide(DEMOD_MODE_NFM, k_amp[k], 300.0, 6000.0, &t1, &b1);
            printf("     %9.0f %8.0f%% %15.1f%% %17.1f%%\n",
                   k_amp[k], 100.0 * 300.0 / k_amp[k], b0, b1);
            if (b1 > b0 + 2.0) {
                mal("la fuga todavia ensucia el audio de NFM");
            }
            if (t1 < 0.85 * t0) {
                mal("con fuga se recupera mucho menos tono");
            }
        }
        printf("\n     ANTES DE RESTAR LA CONTINUA, este mismo banco media:\n"
               "       6000 (5 %%)    0,4 %%        600 (50 %%)   26,6 %%\n"
               "       2000 (15 %%)   3,1 %%        400 (75 %%)   45,7 %%\n"
               "       1000 (30 %%)  11,4 %%        300 (100 %%) 100,0 %%\n"
               "     o sea que con la fuga del tamano de la portadora la\n"
               "     emisora no se oia. Ver la cabecera de este fichero.\n");
    }

    /*
     * (1b) Y QUE NO SEA SOLO EL PASO MALO. La fuga cae con la distancia
     * porque el filtro de canal empieza a morderla, pero en NFM no acaba de
     * caer nunca: a 12 kHz el filtro solo le quita 24 dB.
     */
    printf("\n  -- (1b) NFM con emisora floja (400), por distancia --\n");
    printf("     distancia   basura sin fuga   basura con fuga\n");
    {
        static const double k_off[] = { 6000.0, 8000.0, 12000.0,
                                        24000.0, 40000.0 };
        for (k = 0; k < (int)(sizeof k_off / sizeof k_off[0]); k++) {
            double t0, b0, t1, b1;

            mide(DEMOD_MODE_NFM, 400.0,   0.0, k_off[k], &t0, &b0);
            mide(DEMOD_MODE_NFM, 400.0, 300.0, k_off[k], &t1, &b1);
            printf("     %9.0f %15.1f%% %17.1f%%\n", k_off[k], b0, b1);
            if (b1 > b0 + 2.0) {
                mal("la fuga todavia ensucia el audio de NFM");
            }
        }
    }

    /*
     * (2) AM EN EL MISMO SITIO. Su filtro de canal es el estrecho, asi que
     * a 6 kHz la fuga ya le llega a -14,5 dB: se nota menos, pero se nota,
     * y la envolvente de AM suma la fuga directamente al nivel.
     */
    printf("\n  -- (2) AM, con 300 cuentas de fuga --\n");
    printf("     distancia   basura sin fuga   basura con fuga\n");
    {
        static const double k_off[] = { 6000.0, 12000.0, 24000.0 };
        for (k = 0; k < (int)(sizeof k_off / sizeof k_off[0]); k++) {
            double t0, b0, t1, b1;

            mide(DEMOD_MODE_AM, 600.0,   0.0, k_off[k], &t0, &b0);
            mide(DEMOD_MODE_AM, 600.0, 300.0, k_off[k], &t1, &b1);
            printf("     %9.0f %15.1f%% %17.1f%%\n", k_off[k], b0, b1);
            if (b1 > b0 + 2.0) {
                mal("la fuga todavia ensucia el audio de AM");
            }
        }
    }

    /*
     * (3) Y QUE LO QUE QUITA SEA LO QUE HAY. Una estimacion que converja a
     * otra cosa dejaria los dos bancos de arriba en verde mientras se come
     * la señal por otro lado. Aqui NO hay la ambiguedad de WFM -la señal
     * buena no esta en continua- asi que se puede exigir el valor exacto.
     */
    printf("\n  -- (3) lo que el firmware dice que esta restando --\n");
    {
        static const double k_dc[] = { 0.0, 120.0, 300.0, 900.0 };
        for (k = 0; k < (int)(sizeof k_dc / sizeof k_dc[0]); k++) {
            float di = 0.0f, dq = 0.0f;
            double t, b;

            mide(DEMOD_MODE_NFM, 6000.0, k_dc[k], 6000.0, &t, &b);
            demod_am_get_rf_dc(&di, &dq);
            printf("     se le metio %5.0f  ->  dice %8.1f (I)  %8.1f (Q)\n",
                   k_dc[k], (double)di, (double)dq);
            if (fabs((double)di - k_dc[k]) > 0.05 * k_dc[k] + 12.0 ||
                fabs((double)dq - k_dc[k]) > 0.05 * k_dc[k] + 12.0) {
                mal("la estimacion no converge a la fuga que se le metio");
            }
        }
    }

    if (fallos == 0) { printf("\n  TODO CORRECTO (0 fallos)\n\n"); }
    else             { printf("\n  *** %d FALLO(S) ***\n\n", fallos); }
    return fallos ? 1 : 0;
}
