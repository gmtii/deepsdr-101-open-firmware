/*
 * ======================================================================
 * EL MARGEN DE ENGANCHE DEL SAM - 12/10/2026
 * ======================================================================
 *
 * *** De un probador: "I tested the updated firmware today. (...) there are
 * issues with the synchronous mode. It works fine with a strong signal, but
 * if there is strong interference nearby and the received station is weak,
 * the synchronous detector seems to lock onto the interference. Most likely,
 * the capture range is too wide; for proper operation, it should be limited
 * to a hundred hertz on either side of the carrier." ***
 *
 * Era cierto, y esto lo mide. El lazo venia con los 4.000 Hz del original
 * -que es justo lo que mide el filtro de canal, asi que el lazo podia irse
 * a CUALQUIER sitio que pasara el filtro-.
 *
 * COMO SE MIDE, y esto es lo unico que hace que el banco valga algo: NO se
 * pone la intrusa y ya. Se SINTONIZA bien, se deja que el lazo se enganche
 * a la portadora buena, y despues la buena se DESVANECE unos segundos
 * mientras la intrusa sigue. Eso es lo que describe el probador -"the
 * received station is weak"- y es lo que de verdad rompe un PLL: con las
 * dos presentes a la vez el batido va mas deprisa que el lazo y el lazo lo
 * promedia; es cuando se queda solo con la intrusa cuando se la lleva. Y lo
 * que se mira despues NO es donde esta el lazo durante el desvanecimiento,
 * sino DONDE SE QUEDA CUANDO LA BUENA VUELVE: un lazo que vuelve solo no es
 * un problema; uno que se queda en la intrusa con la buena delante, si.
 *
 * Y HAY UN CASO QUE SE EXIGE QUE SIGA SALIENDO MAL: con el margen ancho el
 * lazo tiene que irse. Es la razon de que el margen exista como ajuste y de
 * que por omision sea estrecho; el dia que eso deje de pasar, el ajuste
 * sobra y hay que venir aqui a enterarse.
 *
 * Todo por demod_am_process_raw(), o sea por el filtro de canal y la cadena
 * de verdad, no por sam.c a pelo.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sam.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define NBLK  SDR_RX_BLOCK_SAMPLES

static int fallos;
static double g_t, g_fs;
static double g_fase_w, g_fase_i;

static void di(const char *que, int ok)
{
    printf("  %-62s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/*
 * Un bloque de I/Q crudo: la portadora deseada (AM al 50% con un tono de
 * 800 Hz) y una portadora intrusa limpia. `a_w` a cero es el
 * desvanecimiento.
 */
static void bloque(int16_t *raw, double f_w, double a_w, double f_i, double a_i)
{
    uint32_t n;
    for (n = 0; n < (uint32_t)NBLK; n++) {
        double m = 1.0 + 0.5 * sin(2.0 * M_PI * 800.0 * g_t / g_fs);
        double i = a_w * m * cos(g_fase_w) + a_i * cos(g_fase_i);
        double q = a_w * m * sin(g_fase_w) + a_i * sin(g_fase_i);
        g_fase_w += 2.0 * M_PI * f_w / g_fs;
        g_fase_i += 2.0 * M_PI * f_i / g_fs;
        g_t += 1.0;
        if (i >  32767.0) { i =  32767.0; }
        if (i < -32768.0) { i = -32768.0; }
        if (q >  32767.0) { q =  32767.0; }
        if (q < -32768.0) { q = -32768.0; }
        raw[2U * n]      = (int16_t)lrint(i);
        raw[2U * n + 1U] = (int16_t)lrint(q);
    }
}

static void arranca(uint16_t margen_hz)
{
    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_SAM);
    demod_am_set_sam_banda(SAM_BANDA_AMBAS);
    demod_am_sam_margen_set(margen_hz);
    cons_captura_tramas((uint32_t)NBLK);
    g_fs = (double)demod_am_get_active_fs_hz();
    g_t = 0.0; g_fase_w = 0.0; g_fase_i = 0.0;
}

static void corre(double seg, double f_w, double a_w, double f_i, double a_i)
{
    static int16_t raw[2 * NBLK];
    int b, n = (int)(seg * g_fs / (double)NBLK);
    for (b = 0; b < n; b++) {
        bloque(raw, f_w, a_w, f_i, a_i);
        demod_am_process_raw(raw);
    }
}

/*
 * La secuencia del probador: enganchado a la buena, la buena se desvanece
 * dos segundos con la intrusa delante, y la buena vuelve DEBIL (20 dB por
 * debajo de la intrusa). Devuelve donde se ha quedado el lazo y si dice
 * estar enganchado.
 */
static void secuencia(uint16_t margen_hz, double f_i, float *hz, int *eng)
{
    arranca(margen_hz);
    corre(2.0, 0.0, 6000.0, f_i, 0.0);          /* la buena sola: engancha */
    corre(2.0, 0.0,    0.0, f_i, 6000.0);       /* se desvanece del todo */
    corre(3.0, 0.0,  600.0, f_i, 6000.0);       /* vuelve, 20 dB por debajo */
    *hz  = demod_am_get_sam_carrier_hz();
    *eng = demod_am_sam_enganchado() ? 1 : 0;
}

/* Portadora limpia desplazada: engancha o no, y donde lee. */
static int engancha_en(uint16_t margen_hz, double df, float *hz)
{
    arranca(margen_hz);
    corre(8.0, df, 6000.0, 0.0, 0.0);
    *hz = demod_am_get_sam_carrier_hz();
    return demod_am_sam_enganchado() ? 1 : 0;
}

int main(void)
{
    static const double k_int[] = { -500.0, -1000.0, 500.0, 1000.0 };
    unsigned i;
    float hz;
    int eng, se_van, se_quedan;

    printf("\n== EL MARGEN DE ENGANCHE DEL SAM ==\n\n");
    printf("  Secuencia: 2 s con la emisora buena sola (el lazo engancha),\n"
           "  2 s de desvanecimiento total con una intrusa delante, y 3 s\n"
           "  con la buena de vuelta 20 dB POR DEBAJO de la intrusa.\n\n");

    /* ---------------------------------------------------------------- */
    printf("  1) CON EL MARGEN ANCHO (4000 Hz, lo que traia el original)\n\n");
    printf("     intrusa      el lazo se queda en   dice estar\n");
    se_van = 0;
    for (i = 0; i < sizeof k_int / sizeof k_int[0]; i++) {
        secuencia(4000U, k_int[i], &hz, &eng);
        printf("     %+7.0f Hz   %+14.1f Hz   %s\n", k_int[i], (double)hz,
               eng ? "ENGANCHADO" : "suelto");
        if (fabs((double)hz - k_int[i]) < 200.0 && eng) { se_van++; }
    }
    printf("\n");
    /*
     * Esto es lo que contaba el probador: el lazo no solo se va a la
     * intrusa, sino que ademas dice que esta enganchado. O sea que la
     * pantalla ensena PPM de una portadora que no es la tuya, y "Calibrar
     * PPM" se dejaria calibrar contra ella.
     */
    di("con el margen ancho el lazo se va a la intrusa (por eso el ajuste)",
       se_van >= 2);

    /* ---------------------------------------------------------------- */
    /*
     * Y LA QUE NO ES: UNA EMISORA FUERTE PERO LEJOS.
     *
     * Pedido por el usuario. Esa NO es la que rompe el lazo, y conviene
     * que quede medido para que nadie "arregle" el filtro de canal
     * buscando un fallo que esta en otro sitio: a 10 kHz el filtro ya la ha
     * tumbado lo bastante, el lazo se queda en la portadora buena y
     * engancha. La que se lleva el lazo es la de CERCA -hasta ~1 kHz-, que
     * es exactamente lo que describe el probador con "nearby".
     */
    printf("\n  1b) UNA EMISORA FUERTE PERO LEJOS: ESA NO ES\n\n");
    {
        static const double k_lejos[] = { -5000.0, -10000.0, -15000.0 };
        int bien = 1;
        for (i = 0; i < sizeof k_lejos / sizeof k_lejos[0]; i++) {
            secuencia((uint16_t)SAM_MARGEN_DEF_HZ, k_lejos[i], &hz, &eng);
            printf("     %+7.0f Hz   %+14.1f Hz   %s\n", k_lejos[i], (double)hz,
                   eng ? "ENGANCHADO" : "suelto");
            if (fabs((double)hz) > 20.0) { bien = 0; }
        }
        printf("\n     El filtro de canal ya se ocupa de esas. Lo que rompia el\n"
               "     lazo era la de cerca.\n\n");
        di("una intrusa lejana no mueve el lazo de la portadora buena", bien);
    }

    /* ---------------------------------------------------------------- */
    printf("\n  2) CON EL MARGEN POR OMISION (%u Hz)\n\n",
           (unsigned)SAM_MARGEN_DEF_HZ);
    printf("     intrusa      el lazo se queda en   dice estar\n");
    se_quedan = 0;
    for (i = 0; i < sizeof k_int / sizeof k_int[0]; i++) {
        secuencia((uint16_t)SAM_MARGEN_DEF_HZ, k_int[i], &hz, &eng);
        printf("     %+7.0f Hz   %+14.1f Hz   %s\n", k_int[i], (double)hz,
               eng ? "ENGANCHADO" : "suelto");
        if (fabs((double)hz) <= (double)SAM_MARGEN_DEF_HZ + 15.0 && !eng) {
            se_quedan++;
        }
    }
    printf("\n     Se queda contra el tope y dice QUE NO ESTA ENGANCHADO, que\n"
           "     es la verdad: con una intrusa 20 dB mas fuerte dentro del\n"
           "     filtro no hay demodulacion que salve eso -lo que salva es\n"
           "     SAM-U/SAM-L o estrechar el filtro-. Una cosa es no poder y\n"
           "     otra mentir.\n\n");
    di("con 100 Hz el lazo no llega a la intrusa", se_quedan == 4);

    /* ---------------------------------------------------------------- */
    printf("\n  3) QUE SIGA ENGANCHANDO LO QUE TIENE QUE ENGANCHAR\n\n");
    {
        static const double k_df[] = { 0.0, 30.0, 60.0, -60.0 };
        int todos = 1;
        for (i = 0; i < sizeof k_df / sizeof k_df[0]; i++) {
            int e = engancha_en((uint16_t)SAM_MARGEN_DEF_HZ, k_df[i], &hz);
            printf("     portadora limpia a %+5.0f Hz:  %s, lee %+7.1f Hz\n",
                   k_df[i], e ? "engancha" : "NO engancha", (double)hz);
            if (!e || fabs((double)hz - k_df[i]) > 10.0) { todos = 0; }
        }
        di("con 100 Hz engancha y mide bien dentro de su margen", todos);
    }

    /* ---------------------------------------------------------------- */
    printf("\n  4) Y POR QUE EL MARGEN ANCHO SIGUE EXISTIENDO\n\n");
    {
        int e100 = engancha_en((uint16_t)SAM_MARGEN_DEF_HZ, 300.0, &hz);
        float hz100 = hz;
        int e4k  = engancha_en(4000U, 300.0, &hz);
        printf("     portadora a +300 Hz (una radio sin calibrar a 30 MHz):\n");
        printf("       con  100 Hz: %s, lee %+7.1f Hz\n",
               e100 ? "engancha" : "NO engancha", (double)hz100);
        printf("       con 4000 Hz: %s, lee %+7.1f Hz\n",
               e4k ? "engancha" : "NO engancha", (double)hz);
        printf("\n     Por eso 'Calibrar PPM' dice 'abre el margen' en vez de\n"
               "     'sin enganche': sin margen no hay enganche, sin enganche\n"
               "     no hay calibracion, y sin calibracion nunca habria\n"
               "     enganche. Un callejon sin salida con un cartel inutil.\n\n");
        di("a +300 Hz: el estrecho no engancha y el ancho si", !e100 && e4k);
    }

    /* ---------------------------------------------------------------- */
    printf("\n  5) CAMBIARLO CON LA RADIO ANDANDO\n\n");
    {
        static int16_t raw[2 * NBLK];
        float antes, medio, despues;
        int b;

        arranca(4000U);
        corre(2.0, 0.0, 6000.0, -1000.0, 0.0);
        corre(2.0, 0.0,    0.0, -1000.0, 6000.0);
        corre(1.0, 0.0,  600.0, -1000.0, 6000.0);
        antes = demod_am_get_sam_carrier_hz();

        /* Y ahora se estrecha el margen SIN reiniciar nada. */
        demod_am_sam_margen_set((uint16_t)SAM_MARGEN_DEF_HZ);
        for (b = 0; b < 40; b++) {   /* ~0,1 s */
            bloque(raw, 0.0, 600.0, -1000.0, 6000.0);
            demod_am_process_raw(raw);
        }
        medio = demod_am_get_sam_carrier_hz();
        corre(4.0, 0.0, 600.0, -1000.0, 6000.0);
        despues = demod_am_get_sam_carrier_hz();

        printf("     pegado a la intrusa:                  %+8.1f Hz\n",
               (double)antes);
        printf("     0,1 s despues de estrechar el margen: %+8.1f Hz\n",
               (double)medio);
        printf("     4,1 s despues:                        %+8.1f Hz\n\n",
               (double)despues);
        /*
         * LO QUE SE MIDE AQUI ES LA LECTURA, NO EL LAZO, y se dice porque
         * la primera version de esta prueba daba MAL y era cierto en
         * darlo: el recorte del integrador es inmediato -lo hace
         * sam_margen_pon() en la misma llamada- pero el numero que sale es
         * la media de SAM_TAU_S = 1 segundo (ver carrier_hz en sam.h), asi
         * que a los 0,1 s todavia esta llegando. Exigirle al numero que
         * vaya mas deprisa que su propio promedio seria exigirle que
         * mintiera. Lo que se exige es que se MUEVA ya -el promedio no se
         * mueve si el lazo no se ha movido- y que haya llegado cuando su
         * constante de tiempo dice.
         */
        di("al estrechar, el lazo se mueve ya",
           fabs((double)antes) > 300.0 &&
           fabs((double)medio) < fabs((double)antes) - 50.0);
        di("y cuatro constantes de tiempo despues la lectura ya esta dentro",
           fabs((double)despues) <= (double)SAM_MARGEN_DEF_HZ + 15.0);
    }

    /* ---------------------------------------------------------------- */
    printf("\n  6) EL AJUSTE NO ADMITE CUALQUIER COSA\n\n");
    {
        int bien = 1;
        unsigned k;
        for (k = 0; k < (unsigned)SAM_MARGEN_OPCIONES; k++) {
            demod_am_sam_margen_set(k_sam_margen_hz[k]);
            if (demod_am_sam_margen_get() != k_sam_margen_hz[k]) { bien = 0; }
            if (demod_am_sam_margen_idx() != (uint8_t)k) { bien = 0; }
        }
        demod_am_sam_margen_set(3000U);   /* no es ninguno de los cuatro */
        printf("     un 3000 de un CONFIG.CSV a mano se queda en %u Hz\n\n",
               (unsigned)demod_am_sam_margen_get());
        if (demod_am_sam_margen_get() != (uint16_t)SAM_MARGEN_DEF_HZ) { bien = 0; }
        di("los cuatro valen, cualquier otro cae en el de por omision", bien);
    }

    /* ---------------------------------------------------------------- */
    /*
     * 7) EL PITIDO DE LA SINTONIA TORCIDA.
     *
     * Pedido por un usuario.
     *
     * Y es lo que tiene que pasar con un detector de producto cuyo
     * oscilador no esta enganchado: sale la señal batida contra un
     * oscilador que esta a otra frecuencia, y esa diferencia es un tono.
     *
     * La V4.61 lo EMPEORO al bajar el margen de 4.000 a 100 Hz: una
     * portadora a 400 Hz ya no se alcanza, asi que el lazo se queda contra
     * el tope y pita. Desde la V4.66, sin enganche sale la ENVOLVENTE -AM
     * de toda la vida- y no hay pitido.
     *
     * COMO SE MIDE: una portadora a +400 Hz modulada con un tono de
     * 1.000 Hz. En la salida se busca cuanta energia hay en 400 Hz -el
     * pitido- comparada con la de 1.000 -lo que se quiere oir-.
     */
    printf("\n  7) el pitido cuando la sintonia esta torcida\n\n");
    {
        static int16_t raw[2 * NBLK];
        /*
         * EL PITIDO NO ESTA EN LOS 400 Hz DE LA PORTADORA, y la primera
         * version de esta prueba lo busco ahi y no lo encontro: el batido
         * sale a la diferencia entre la portadora y DONDE SE HAYA QUEDADO
         * EL LAZO, que con el margen a 100 Hz es contra el tope. O sea a
         * unos 300, no a 400. Un banco que busca el tono en el sitio
         * equivocado dice que no hay tono.
         *
         * Asi que se barre de 100 a 900 Hz y se coge el mayor que no sea
         * el tono de la emisora. Y se mide ademas el propio tono, que es
         * el otro sintoma: con el lazo suelto el detector de producto
         * tambien se come la modulacion.
         */
        double sc1 = 0, ss1 = 0, nm = 0;
        double sc[33], ss[33];
        int b, j;
        const double F_OFF = 400.0, F_TONO = 1000.0;

        for (j = 0; j < 33; j++) { sc[j] = 0.0; ss[j] = 0.0; }

        arranca((uint16_t)SAM_MARGEN_DEF_HZ);
        for (b = 0; b < 900; b++) {
            uint32_t k;

            for (k = 0; k < (uint32_t)NBLK; k++) {
                double t = g_t / g_fs;
                double m = 1.0 + 0.6 * sin(2.0 * M_PI * F_TONO * t);
                double i = 6000.0 * m * cos(2.0 * M_PI * F_OFF * t);
                double q = 6000.0 * m * sin(2.0 * M_PI * F_OFF * t);

                g_t += 1.0;
                if (i >  32767.0) { i =  32767.0; }
                if (i < -32768.0) { i = -32768.0; }
                if (q >  32767.0) { q =  32767.0; }
                if (q < -32768.0) { q = -32768.0; }
                raw[2U * k]      = (int16_t)lrint(i);
                raw[2U * k + 1U] = (int16_t)lrint(q);
            }
            demod_am_process_raw(raw);

            if (b >= 600) {
                const int16_t *sal = cons_audio();
                double t0 = g_t - (double)NBLK;

                for (k = 0; k < (uint32_t)NBLK; k++) {
                    double y = (double)sal[2U * k];
                    double ts = (t0 + (double)k) / g_fs;

                    for (j = 0; j < 33; j++) {
                        double f = 100.0 + 25.0 * (double)j;   /* 100..900 */
                        sc[j] += y * cos(2.0 * M_PI * f * ts);
                        ss[j] += y * sin(2.0 * M_PI * f * ts);
                    }
                    sc1 += y * cos(2.0 * M_PI * F_TONO * ts);
                    ss1 += y * sin(2.0 * M_PI * F_TONO * ts);
                    nm += 1.0;
                }
            }
        }
        {
            double aud = 2.0 * sqrt(sc1 * sc1 + ss1 * ss1) / nm;
            double pit = 0.0, fpit = 0.0;
            double db;

            for (j = 0; j < 33; j++) {
                double f = 100.0 + 25.0 * (double)j;
                double a2 = 2.0 * sqrt(sc[j] * sc[j] + ss[j] * ss[j]) / nm;

                if (a2 > pit) { pit = a2; fpit = f; }
            }
            db = 20.0 * log10((pit + 1e-9) / (aud + 1e-9));

            printf("     portadora a +400 Hz, margen %u Hz -> %s\n",
                   (unsigned)SAM_MARGEN_DEF_HZ,
                   demod_am_sam_enganchado() ? "ENGANCHADO" : "sin enganche");
            printf("     tono de 1.000 Hz (lo que se quiere oir): %8.0f\n", aud);
            printf("     lo mas gordo entre 100 y 900 Hz:         %8.0f a %.0f Hz"
                   "   %+6.1f dB\n\n", pit, fpit, db);
            di("la modulacion de la emisora llega entera", aud > 5000.0);
            di("y no hay pitido: 20 dB por debajo del tono", db < -20.0);
        }
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
