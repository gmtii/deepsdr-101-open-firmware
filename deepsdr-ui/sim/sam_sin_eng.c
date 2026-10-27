/*
 * ======================================================================
 * QUE SE OYE CUANDO EL SAM DE UNA SOLA BANDA PIERDE EL ENGANCHE
 * ======================================================================
 *
 * Avisado por un usuario del repositorio: en SAM de una sola banda, perder
 * el enganche devuelve a AM, y eso deshace lo unico que se le habia pedido
 * al modo.
 *
 * POR QUE IMPORTA, Y POR QUE NO ES LO MISMO QUE EN SAM DE DOS BANDAS.
 *
 * A SAM-U/SAM-L no se llega por gusto: se llega porque hay otra emisora
 * pisando por un lado y quedarse con el otro es lo que deja escuchar. El
 * detector de envolvente, en cambio, ve las dos bandas sumadas -no sabe
 * separarlas, es su naturaleza-, asi que al volver a el vuelve ENTERA la
 * intrusa de la que se acababa de huir, y ademas batiendo contra la
 * portadora buena. O sea que la vuelta a AM no es "lo de antes": en este
 * modo es peor que lo de antes.
 *
 * COMO SE MIDE. Se sintoniza con la portadora FUERA del margen del lazo
 * -asi no puede engancharse por mucho que se espere, que es el caso del
 * aviso-, con:
 *
 *   - el tono que se quiere oir en la banda PEDIDA,
 *   - una intrusa fuerte en la banda RECHAZADA.
 *
 * Y se mira cuanto de la intrusa sale por el altavoz. Lo que se exige es
 * que salga RECHAZADA, igual que saldria con el lazo enganchado: el que
 * el oscilador no sepa donde esta la portadora no cambia de que lado
 * esta cada cosa.
 *
 * El caso de las DOS bandas se mide tambien y se exige que NO cambie: ahi
 * la vuelta a la envolvente es lo correcto y sigue siendolo.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "demod_am.h"
#include "sam.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define NBLK    SDR_RX_BLOCK_SAMPLES
#define BLOQUES 1600
#define ASIENTO 1200

/* La portadora, MUY fuera del margen por omision (250 Hz): aqui el lazo no
 * llega ni queriendo, que es de lo que va el aviso. */
static double F_PORT = 900.0;
#define F_TONO  1500.0   /* lo que se quiere oir, en la banda de arriba */
#define F_INTR  2700.0   /* la intrusa, en la de abajo. DISTINTA del tono
                          * bueno: con las dos a la misma altura el
                          * correlador mide la suma de las dos y el banco
                          * dice lo que sea. Primera version de esto, que
                          * no media nada. */

static int fallos;
static double g_t, g_fs;

/*
 * QUE NINGUNA DE LAS CINCO MEDIDAS CAIGA ENCIMA DE OTRA.
 *
 * *** ESTA GUARDA EXISTE PORQUE EL BANCO SE PILLO A SI MISMO. ***
 *
 * La primera version ponia el tono bueno y la intrusa a la MISMA altura
 * (1500 Hz los dos): el correlador medía la suma de los dos y el banco
 * decía que el rechazo era de 0,0 dB con el codigo bueno delante. Se
 * separaron, y al barrer el desvio volvio a pasar por otro sitio: con la
 * portadora a 500 Hz, el tono bueno sale a 500+1500 = 2000 y la intrusa a
 * 2500-500 = 2000 TAMBIEN. Otra vez el mismo bin, otra vez un numero
 * inventado, otra vez con el codigo bueno.
 *
 * Dos veces el mismo fallo es que hace falta una red y no mas cuidado. Se
 * comprueba, para cada desvio, que las cinco frecuencias que se miden
 * esten separadas; si no lo estan, el banco lo dice y falla en vez de dar
 * un numero que no significa nada.
 *
 * 80 Hz es lo que separa dos bins con la integracion que hay aqui
 * (BLOQUES-ASIENTO bloques de NBLK muestras).
 */
#define SEPARA_HZ 80.0
static int frecuencias_separadas(double d)
{
    double f[5];
    unsigned a, b;
    f[0] = F_TONO;            /* el tono bueno por el camino de envolvente */
    f[1] = d + F_TONO;        /* y por el de banda lateral fija            */
    f[2] = F_INTR;            /* la intrusa por el de envolvente           */
    f[3] = F_INTR - d;        /* y por el de banda lateral fija            */
    f[4] = d;                 /* la portadora suelta                       */
    for (a = 0; a < 5U; a++) {
        if (f[a] < 0.0) { f[a] = -f[a]; }
    }
    for (a = 0; a < 5U; a++) {
        for (b = a + 1U; b < 5U; b++) {
            double sep = f[a] - f[b];
            if (sep < 0.0) { sep = -sep; }
            if (sep < SEPARA_HZ) {
                printf("  MAL: con la portadora a %.0f Hz, dos de las medidas caen\n"
                       "       en %.0f y %.0f Hz: el banco estaria midiendo la suma\n"
                       "       de dos cosas y llamandola una.\n", d, f[a], f[b]);
                return 0;
            }
        }
    }
    return 1;
}

static void bloque(int16_t *raw, double amp_intrusa)
{
    uint32_t n;
    for (n = 0; n < (uint32_t)NBLK; n++) {
        double t = g_t / g_fs;
        double wp = 2.0 * M_PI * F_PORT * t;
        double ws = 2.0 * M_PI * (F_PORT + F_TONO) * t;
        double wi = 2.0 * M_PI * (F_PORT - F_INTR) * t;
        double i = 6000.0 * (cos(wp) + 0.5 * cos(ws)) + amp_intrusa * cos(wi);
        double q = 6000.0 * (sin(wp) + 0.5 * sin(ws)) + amp_intrusa * sin(wi);
        g_t += 1.0;
        if (i >  32767.0) { i =  32767.0; }
        if (i < -32768.0) { i = -32768.0; }
        if (q >  32767.0) { q =  32767.0; }
        if (q < -32768.0) { q = -32768.0; }
        raw[2U * n]      = (int16_t)lrint(i);
        raw[2U * n + 1U] = (int16_t)lrint(q);
    }
}

/* Amplitud del tono bueno y de la intrusa a la salida, y si el lazo decia
 * estar enganchado al terminar. */
static void mide(sam_banda_t banda, double amp_intrusa,
                 double *bueno, double *intrusa, double *porta,
                 int *enganchado)
{
    static int16_t raw[2 * NBLK];
    int b;
    double sc_b = 0, ss_b = 0, sc_i = 0, ss_i = 0, nm = 0;
    double sc_b2 = 0, ss_b2 = 0, sc_i2 = 0, ss_i2 = 0;
    double sc_c = 0, ss_c = 0;

    demod_am_init();
    demod_am_set_mode(DEMOD_MODE_SAM);
    demod_am_set_sam_banda(banda);
    cons_captura_tramas((uint32_t)NBLK);
    g_fs = (double)demod_am_get_active_fs_hz();
    g_t = 0.0;

    for (b = 0; b < BLOQUES; b++) {
        const int16_t *sal;
        uint32_t n;
        double t0 = g_t;

        bloque(raw, amp_intrusa);
        demod_am_process_raw(raw);
        if (b < ASIENTO) { continue; }

        sal = cons_audio();
        for (n = 0; n < (uint32_t)NBLK; n++) {
            double y = (double)sal[2U * n];
            double ts = (t0 + (double)n) / g_fs;
            /*
             * CADA TONO SE BUSCA EN LAS DOS ALTURAS POSIBLES, y esto es lo
             * que hace que el banco siga valiendo con los dos caminos.
             *
             * Con la envolvente, lo que sale son las DIFERENCIAS contra la
             * portadora, asi que el tono bueno cae en F_TONO y la intrusa
             * en F_INTR. Con el oscilador parado en la sintonia, en cambio,
             * todo sale medido desde la SINTONIA: el tono bueno se va a
             * F_PORT+F_TONO y la intrusa, si se colara, a F_INTR-F_PORT.
             *
             * Buscar solo donde "deberia" estar es como no medir: el banco
             * diria que el tono bueno ha desaparecido en cuanto el camino
             * cambie, que es justo lo que se esta cambiando.
             */
            double wb1 = 2.0 * M_PI * F_TONO * ts;
            double wb2 = 2.0 * M_PI * (F_PORT + F_TONO) * ts;
            double wi1 = 2.0 * M_PI * F_INTR * ts;
            double wi2 = 2.0 * M_PI * (F_INTR - F_PORT) * ts;
            double wc  = 2.0 * M_PI * F_PORT * ts;
            sc_b += y * cos(wb1); ss_b += y * sin(wb1);
            sc_b2 += y * cos(wb2); ss_b2 += y * sin(wb2);
            sc_i += y * cos(wi1); ss_i += y * sin(wi1);
            sc_i2 += y * cos(wi2); ss_i2 += y * sin(wi2);
            sc_c += y * cos(wc); ss_c += y * sin(wc);
            nm += 1.0;
        }
    }
    {
        double b1 = 2.0 * sqrt(sc_b * sc_b + ss_b * ss_b) / nm;
        double b2 = 2.0 * sqrt(sc_b2 * sc_b2 + ss_b2 * ss_b2) / nm;
        double i1 = 2.0 * sqrt(sc_i * sc_i + ss_i * ss_i) / nm;
        double i2 = 2.0 * sqrt(sc_i2 * sc_i2 + ss_i2 * ss_i2) / nm;
        *bueno   = (b1 > b2) ? b1 : b2;
        *intrusa = (i1 > i2) ? i1 : i2;
        *porta   = 2.0 * sqrt(sc_c * sc_c + ss_c * ss_c) / nm;
    }
    *enganchado = (int)demod_am_sam_enganchado();
}

int main(void)
{
    double b_amb, i_amb, b_usb, i_usb, b_nada, i_nada;
    double p_amb, p_usb, p_nada;
    int e_amb, e_usb, e_nada;

    printf("\n== SAM DE UNA BANDA SIN ENGANCHE ==\n\n");
    printf("  portadora a %.0f Hz de la sintonia, o sea FUERA del margen\n"
           "  del lazo (%u Hz): no se puede enganchar.\n\n",
           F_PORT, (unsigned)demod_am_sam_margen_get());

    if (!frecuencias_separadas(F_PORT)) {
        printf("\nHAY FALLOS (1)\n\n");
        return 1;
    }

    /* 1. Sin intrusa: cuanto vale el tono bueno, que es la referencia. */
    mide(SAM_BANDA_USB, 0.0, &b_nada, &i_nada, &p_nada, &e_nada);
    printf("  sin intrusa, banda superior:\n");
    printf("    el tono que se quiere oir   %8.0f\n", b_nada);
    printf("    el lazo dice enganchado     %8s\n\n", e_nada ? "SI" : "no");
    if (e_nada) {
        printf("  MAL: con la portadora a %.0f Hz el lazo NO puede enganchar.\n"
               "       Si dice que si, el banco no esta midiendo lo que cree.\n",
               F_PORT);
        fallos++;
    }
    if (b_nada < 100.0) {
        printf("  MAL: sin intrusa tampoco se oye el tono bueno: no hay nada que medir\n");
        printf("\nHAY FALLOS (%d)\n\n", fallos + 1);
        return 1;
    }

    /* 2. Con la intrusa, igual de fuerte que la portadora buena. */
    mide(SAM_BANDA_USB,   6000.0, &b_usb, &i_usb, &p_usb, &e_usb);
    mide(SAM_BANDA_AMBAS, 6000.0, &b_amb, &i_amb, &p_amb, &e_amb);

    printf("  con una intrusa en la banda de ABAJO, tan fuerte como la buena:\n\n");
    printf("    banda pedida      tono bueno     intrusa    intrusa rel.\n");
    printf("    ambas          %12.0f %11.0f %11.1f dB\n",
           b_amb, i_amb, 20.0 * log10((i_amb + 1e-9) / (b_amb + 1e-9)));
    printf("    superior       %12.0f %11.0f %11.1f dB\n\n",
           b_usb, i_usb, 20.0 * log10((i_usb + 1e-9) / (b_usb + 1e-9)));

    /*
     * LO QUE SE EXIGE. Con la banda superior pedida, la intrusa esta en la
     * de abajo y tiene que salir al menos 12 dB por debajo del tono bueno.
     * Doce y no veinte porque sin enganche el oscilador esta donde esta y
     * el Hilbert no trabaja centrado; el numero sale de medir, no de
     * desear, y lo que vigila es que el rechazo EXISTA.
     */
    {
        double rel = 20.0 * log10((i_usb + 1e-9) / (b_usb + 1e-9));
        printf("    en superior, la intrusa queda %.1f dB por debajo del tono bueno\n", -rel);
        if (rel > -12.0) {
            printf("  MAL: sin enganche, pedir una sola banda no rechaza la otra:\n"
                   "       la intrusa de la que se huia vuelve entera.\n");
            fallos++;
        }
    }

    /*
     * Y EL CASO DE LAS DOS BANDAS NO CAMBIA. Ahi la vuelta a la envolvente
     * es lo correcto -un SAM desenganchado no tiene nada que ofrecer- y
     * esto esta para que nadie lo "arregle" de paso.
     */
    printf("\n    con las dos bandas la intrusa SI sale, y tiene que seguir saliendo:\n");
    printf("      %s\n", (i_amb > 0.2 * b_amb) ? "sale   ok" : "no sale   MAL");
    if (i_amb <= 0.2 * b_amb) {
        printf("  MAL: con las dos bandas la intrusa ha dejado de salir.\n"
               "       Eso quiere decir que se ha tocado el camino que no tocaba.\n");
        fallos++;
    }

    /*
     * Y EL PRECIO A LO LARGO DEL DESVIO, que es lo que decide si el cambio
     * vale la pena. Un solo desvio comodo no mide: lo que importa es el
     * caso de verdad, que es una portadora JUSTO fuera del margen.
     */
    printf("\n    el precio, portadora a portadora:\n");
    printf("      desvio   tono bueno   portadora   portadora rel.   intrusa rel.\n");
    {
        static const double k_d[] = { 300.0, 400.0, 500.0, 700.0, 900.0, 1000.0 };
        unsigned j;
        for (j = 0; j < sizeof k_d / sizeof k_d[0]; j++) {
            double b, i2, pc;
            int e;
            F_PORT = k_d[j];
            if (!frecuencias_separadas(F_PORT)) { fallos++; continue; }
            mide(SAM_BANDA_USB, 6000.0, &b, &i2, &pc, &e);
            printf("      %5.0f Hz %11.0f %11.0f %12.1f dB %12.1f dB%s\n",
                   k_d[j], b, pc,
                   20.0 * log10((pc + 1e-9) / (b + 1e-9)),
                   20.0 * log10((i2 + 1e-9) / (b + 1e-9)),
                   e ? "   (engancho!)" : "");
            if (e) {
                printf("  MAL: con la portadora a %.0f Hz el lazo dice haber enganchado\n", k_d[j]);
                fallos++;
            }
            if (20.0 * log10((i2 + 1e-9) / (b + 1e-9)) > -12.0) {
                printf("  MAL: a %.0f Hz de desvio la intrusa no queda rechazada\n", k_d[j]);
                fallos++;
            }
        }
        F_PORT = 900.0;
    }

    /*
     * Y EL PRECIO, DICHO EN VOZ ALTA. Con el oscilador parado en la
     * sintonia, la portadora de la emisora NO esta en el cero del audio:
     * esta donde este, y se oye como un tono. Eso no es un fallo del
     * cambio, es lo que significa escuchar en banda lateral algo que lleva
     * portadora, y es exactamente lo que se arregla girando el mando. Se
     * mide y se enseña para que nadie se lo encuentre de sorpresa.
     */
    printf("    y el precio: la portadora suelta, a %.0f Hz   %8.0f  (%+.1f dB sobre el tono)\n",
           F_PORT, p_usb, 20.0 * log10((p_usb + 1e-9) / (b_usb + 1e-9)));
    printf("      Con el oscilador parado, la portadora se oye como un tono.\n"
           "      Se quita girando el mando, que es lo que no se podia hacer\n"
           "      cuando lo que sonaba era la intrusa.\n");

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
