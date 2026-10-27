/*
 * ======================================================================
 * EL PLL DEL SAM CONTRA UNA PORTADORA DEBIL - 22/10/2026
 * ======================================================================
 *
 * *** El aviso, de un usuario del repositorio, con video: una emisora
 * floja que NO engancha, con la radio diciendo "+- 250 Hz - no lock".
 * "In theory, with this signal level sync should be perfect. What could
 * be preventing this? Poor filtering in the PLL loop is likely the
 * cause." ***
 *
 * LO QUE MIDE, Y POR QUE SON DOS COSAS Y NO UNA
 * ---------------------------------------------
 * "No engancha" puede querer decir dos cosas muy distintas:
 *
 *   1. que el lazo NO esta siguiendo a la portadora, o
 *   2. que SI la sigue y el INDICADOR dice que no.
 *
 * Son fallos distintos y se arreglan en sitios distintos, asi que aqui se
 * miden por separado:
 *
 *   - el error de fase de VERDAD, comparando la fase del oscilador del
 *     lazo contra la de la portadora que se ha generado. Eso solo se puede
 *     hacer en el simulador, que es la razon de que el simulador exista.
 *   - y lo que contesta sam_enganchado(), que es lo que ve el usuario.
 *
 * EL BARRIDO ES EN DOS EJES
 * -------------------------
 * La relacion portadora/ruido, y el ANCHO DEL LAZO (omega_n). El firmware
 * usa 200 Hz, que son los de WDSP. Un lazo ancho mete mas ruido dentro:
 * el ancho de ruido de un lazo de segundo orden es del orden de
 * (wn/2)(zeta + 1/(4 zeta)), o sea unos 115 Hz con los numeros de hoy. Si
 * la hipotesis del aviso es cierta, estrechar tiene que mejorar el
 * seguimiento, y el banco tiene que poder enseñarlo con numeros.
 *
 * SIN RUIDO NO SE MIDE NADA. Un banco de PLL con portadora limpia dice
 * que si siempre; el que decide es el que tiene ruido, que es la unica
 * condicion en la que el aviso ocurre.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "sam.h"
#include "demod_am.h"
#include "sdr_rx.h"
#include "demod_stubs.h"

#define FS       96000.0f
#define BLOQUE   256U
#define TPI      6.283185307179586

static int fallos;

/* Ruido gaussiano reproducible: el mismo sembrado da el mismo resultado,
 * que es lo que hace que un numero de aqui se pueda comparar manana. */
static uint32_t g_sem = 12345U;
static double gauss(void)
{
    double u1, u2;
    g_sem = g_sem * 1664525U + 1013904223U;
    u1 = ((double)(g_sem >> 8) + 1.0) / 16777217.0;
    g_sem = g_sem * 1664525U + 1013904223U;
    u2 = (double)(g_sem >> 8) / 16777216.0;
    return sqrt(-2.0 * log(u1)) * cos(TPI * u2);
}

/*
 * Una tirada: portadora a `off` Hz de la sintonia, con ruido, durante
 * `seg` segundos. Devuelve el error de fase cuadratico medio de la ultima
 * mitad -en grados- y si el indicador decia enganchado al acabar.
 */
static void tirada(float omega_n, double off, double cn_db, double seg,
                   double *err_grados, int *dice_enganchado, double *hz_medido)
{
    sam_t s;
    static float32_t ib[BLOQUE], qb[BLOQUE], au[BLOQUE];
    double fase_port = 0.0;
    double amp = 1.0;
    /* El ruido, repartido por el ancho de banda del muestreo. cn_db es la
     * portadora sobre el ruido EN TODO EL ANCHO, que es lo que se puede
     * medir en una radio sin saber el ancho de lazo. */
    double sigma = amp / pow(10.0, cn_db / 20.0);
    long n_bloques = (long)(seg * FS / (double)BLOQUE);
    long b, k;
    double suma_e2 = 0.0, n_e = 0.0;

    sam_init(&s, FS, 250.0f, omega_n, 65.0f / 75.0f);
    g_sem = 12345U;

    for (b = 0; b < n_bloques; b++) {
        for (k = 0; k < (long)BLOQUE; k++) {
            fase_port += TPI * off / FS;
            if (fase_port > TPI) { fase_port -= TPI; }
            ib[k] = (float32_t)(amp * cos(fase_port) + sigma * gauss());
            qb[k] = (float32_t)(amp * sin(fase_port) + sigma * gauss());
        }
        sam_bloque(&s, ib, qb, au, BLOQUE, FS);

        if (b > n_bloques / 2) {
            /*
             * EL ERROR DE FASE DE VERDAD. La fase del oscilador del lazo
             * esta en s.fase, en la rejilla de 32 bits; la de la portadora
             * la sabemos porque la hemos generado. La diferencia, doblada
             * al intervalo -pi..pi.
             */
            double fl = (double)s.fase / 4294967296.0 * TPI;
            double e = fl - fase_port;
            while (e >  M_PI) { e -= TPI; }
            while (e < -M_PI) { e += TPI; }
            suma_e2 += e * e;
            n_e += 1.0;
        }
    }
    *err_grados = sqrt(suma_e2 / n_e) * 180.0 / M_PI;
    *dice_enganchado = (int)sam_enganchado(&s);
    *hz_medido = (double)s.carrier_hz;
}

/*
 * ======================================================================
 * Y AHORA POR LA CADENA DE VERDAD
 * ======================================================================
 *
 * Lo de arriba mide el LAZO a pelo. Esto mete la señal por
 * demod_am_process_raw(), o sea por el filtro de canal, el automatico de
 * ganancia y todo lo que hay antes del detector, que es lo que corre en
 * la radio. Si los dos dicen cosas distintas, el culpable no es el lazo.
 */
static void bloque_am(int16_t *raw, double *fase, double off, double amp,
                      double sigma, double fs)
{
    uint32_t n;
    for (n = 0; n < (uint32_t)SDR_RX_BLOCK_SAMPLES; n++) {
        double i, q;
        *fase += TPI * off / fs;
        if (*fase > TPI) { *fase -= TPI; }
        /* AM de verdad: portadora mas dos bandas de un tono de 800 Hz al
         * 50 %. Sin modulacion esto no seria una emisora, seria un tono. */
        {
            double m = 1.0 + 0.5 * cos(TPI * 800.0 * (*fase) / (TPI * (off != 0.0 ? off : 1.0)));
            i = amp * m * cos(*fase) + sigma * gauss();
            q = amp * m * sin(*fase) + sigma * gauss();
        }
        if (i >  32767.0) { i =  32767.0; }
        if (i < -32768.0) { i = -32768.0; }
        if (q >  32767.0) { q =  32767.0; }
        if (q < -32768.0) { q = -32768.0; }
        raw[2U * n]      = (int16_t)lrint(i);
        raw[2U * n + 1U] = (int16_t)lrint(q);
    }
}

static void cadena_entera(void)
{
    static const double k_off[] = { 0.0, 50.0, 120.0, 200.0, 240.0, 300.0, 500.0 };
    static const double k_cn[]  = { 20.0, 10.0, 3.0, 0.0, -6.0 };
    static int16_t raw[2 * SDR_RX_BLOCK_SAMPLES];
    unsigned a, b;

    printf("  POR LA CADENA DE VERDAD (demod_am_process_raw, SAM, margen 250)\n\n");
    printf("    desvio    C/N 20 dB   C/N 10 dB    C/N 3 dB    C/N 0 dB   C/N -6 dB\n");

    for (a = 0; a < sizeof k_off / sizeof k_off[0]; a++) {
        printf("    %5.0f Hz", k_off[a]);
        for (b = 0; b < sizeof k_cn / sizeof k_cn[0]; b++) {
            double fase = 0.0, fs;
            double amp = 6000.0;
            double sigma = amp / pow(10.0, k_cn[b] / 20.0);
            int bl;
            demod_am_init();
            demod_am_set_mode(DEMOD_MODE_SAM);
            fs = (double)demod_am_get_active_fs_hz();
            g_sem = 999U;
            for (bl = 0; bl < 1400; bl++) {
                bloque_am(raw, &fase, k_off[a], amp, sigma, fs);
                demod_am_process_raw(raw);
            }
            {
                int8_t t = demod_am_sam_tope();
                printf("   %3s%s %+5.0f",
                       demod_am_sam_enganchado() ? "si" : "NO",
                       (t > 0) ? "^" : ((t < 0) ? "v" : " "),
                       (double)demod_am_get_sam_carrier_hz());
                /*
                 * LO QUE SE EXIGE, Y ES LA RAZON DE QUE ESTO EXISTA:
                 *   - dentro del margen tiene que enganchar, pase lo que
                 *     pase con el ruido;
                 *   - fuera NO, y ademas tiene que decir que esta contra
                 *     el tope y HACIA DONDE.
                 */
                if (k_off[a] <= 240.0) {
                    if (!demod_am_sam_enganchado()) {
                        printf("\n  MAL: a %.0f Hz y C/N %.0f dB no engancha, "
                               "y esta dentro del margen\n", k_off[a], k_cn[b]);
                        fallos++;
                    }
                } else {
                    if (demod_am_sam_enganchado()) {
                        printf("\n  MAL: a %.0f Hz dice enganchado, y esta "
                               "fuera del margen\n", k_off[a]);
                        fallos++;
                    }
                    if (t <= 0) {
                        printf("\n  MAL: a %.0f Hz la portadora esta fuera por "
                               "ARRIBA y el lazo no lo dice\n", k_off[a]);
                        fallos++;
                    }
                }
            }
        }
        printf("\n");
    }
    printf("\n    (al lado del si/no, los hercios que dice haber medido)\n\n");

    /*
     * ¿SE PUEDE DISTINGUIR "PORTADORA FUERA DE ALCANCE" DE "NO HAY NADA"?
     *
     * Esa es la pregunta que decide si la radio puede AVISAR en vez de
     * limitarse a decir "no lock". Con la portadora fuera del margen el
     * integrador se queda clavado contra el tope; con solo ruido deberia
     * pasearse. Si se parecen, no hay aviso posible y hay que decirlo.
     */
    {
        static int16_t raw2[2 * SDR_RX_BLOCK_SAMPLES];
        int bl, j;
        double min_hz = 1e9, max_hz = -1e9;

        printf("    y con SOLO RUIDO, sin portadora ninguna:\n");
        demod_am_init();
        demod_am_set_mode(DEMOD_MODE_SAM);
        g_sem = 4242U;
        for (bl = 0; bl < 1400; bl++) {
            for (j = 0; j < SDR_RX_BLOCK_SAMPLES; j++) {
                double i = 2000.0 * gauss(), q = 2000.0 * gauss();
                if (i >  32767.0) { i =  32767.0; }
                if (i < -32768.0) { i = -32768.0; }
                if (q >  32767.0) { q =  32767.0; }
                if (q < -32768.0) { q = -32768.0; }
                raw2[2 * j]     = (int16_t)lrint(i);
                raw2[2 * j + 1] = (int16_t)lrint(q);
            }
            demod_am_process_raw(raw2);
            if (bl > 700) {
                double h = (double)demod_am_get_sam_carrier_hz();
                if (h < min_hz) { min_hz = h; }
                if (h > max_hz) { max_hz = h; }
            }
        }

        printf("      enganchado: %s   tope: %s   se pasea entre %+.0f y %+.0f Hz\n\n",
               demod_am_sam_enganchado() ? "si  MAL" : "no",
               demod_am_sam_tope() ? "SI  MAL" : "no", min_hz, max_hz);
        if (demod_am_sam_tope() != 0) {
            printf("  MAL: con solo ruido dice estar contra el tope. Entonces\n"
                   "       el aviso no distingue nada y engana.\n");
            fallos++;
        }
        if (demod_am_sam_enganchado()) {
            printf("  MAL: dice estar enganchado a puro ruido\n");
            fallos++;
        }
        if (max_hz > 240.0 || min_hz < -240.0) {
            printf("      OJO: con solo ruido tambien llega al tope, asi que\n"
                   "      estar contra el tope NO distingue los dos casos.\n\n");
        } else {
            printf("      No llega al tope. O sea que SI se puede avisar:\n"
                   "      clavado contra el tope = portadora fuera de alcance.\n\n");
        }
    }
}

/*
 * ======================================================================
 * LA PORTADORA SE VA Y VUELVE - 22/10/2026
 * ======================================================================
 *
 * *** El mismo usuario, en el aviso siguiente: "ideally, a
 * well-functioning PLL should maintain the local oscillator frequency
 * even at a noise level where no information can be discerned at all". ***
 *
 * La primera mitad de lo que dice es CIERTA y esta medida: con la
 * portadora apagada del todo, el lazo sigue integrando ruido y se va de
 * donde estaba. Con la portadora a +120 Hz:
 *
 *     apagon de  0,5 s   al volver el NCO estaba en +117,6 Hz
 *     apagon de  2,0 s                              +164,8 Hz
 *     apagon de  5,0 s                              +203,1 Hz
 *     apagon de 10,0 s                               +87,8 Hz
 *
 * LO QUE NO SE SIGUE DE AHI ES QUE IMPORTE. Lo que se oye no es donde
 * esta el oscilador mientras no hay señal: es cuanto tarda la FASE en
 * volver a estar bien cuando la señal vuelve. Y eso lo arregla el lazo
 * ancho en milisegundos, precisamente porque es ancho.
 *
 * SE PROBO EL VOLANTE DE INERCIA Y SE TIRO. Congelar el integrador al
 * perder el enganche se implemento entero y se midio, 20 semillas por
 * casilla, en milisegundos hasta que la fase vuelve (mediana/peor):
 *
 *     apagon        sin volante        con volante
 *      0,5 s          11 / 19            3 / 1339
 *      2,0 s          11 / 19            3 / 1781
 *      5,0 s          13 / 19            0 / 1789
 *     10,0 s          16 / 53          397 / 413
 *
 * La mediana mejora 10 ms -inaudible- y el peor caso se va a mas de dos
 * segundos, porque congela el valor del instante en que se pierde el
 * enganche -que es cuando el detector ya estaba degradado- y luego se lo
 * impone al lazo bloque a bloque, peleando contra el reenganche.
 *
 * Un volante solo paga cuando el lazo es lento. Este no lo es, y hacerlo
 * lento es lo que se descarto por alcance de captura.
 *
 * ASI QUE LO QUE VIGILA ESTE BANCO ES EL NUMERO BUENO: que la fase vuelva
 * DEPRISA. El dia que alguien estreche el lazo o meta un volante, esto se
 * pone rojo y dice cuanto ha costado.
 */
static void el_desvanecimiento(void)
{
    static const double k_ap[] = { 0.5, 2.0, 5.0, 10.0 };
    unsigned a;

    printf("  LA PORTADORA SE VA Y VUELVE (a +120 Hz, ruido sigma 0,3)\n\n");
    printf("    apagon    la fase vuelve en   el NCO se quedo en\n");

    for (a = 0; a < sizeof k_ap / sizeof k_ap[0]; a++) {
        sam_t s;
        static float32_t ib[BLOQUE], qb[BLOQUE], au[BLOQUE];
        double f = 0.0, off = 120.0, sigma = 0.3;
        long n1 = (long)(3.0 * FS / BLOQUE);
        long n2 = (long)(k_ap[a] * FS / BLOQUE);
        long n3 = (long)(3.0 * FS / BLOQUE);
        long b, nb = n1 + n2 + n3, bien = -1;
        double hz_fade = 0.0;

        sam_init(&s, FS, 250.0f, 200.0f, 65.0f / 75.0f);
        g_sem = 5U;
        for (b = 0; b < nb; b++) {
            int hay = (b < n1) || (b >= n1 + n2);
            long k;
            for (k = 0; k < (long)BLOQUE; k++) {
                double i = sigma * gauss(), q = sigma * gauss();
                f += TPI * off / FS;
                if (f > TPI) { f -= TPI; }
                if (hay) { i += cos(f); q += sin(f); }
                ib[k] = (float32_t)i;
                qb[k] = (float32_t)q;
            }
            sam_bloque(&s, ib, qb, au, BLOQUE, FS);
            if (b == n1 + n2 - 1) { hz_fade = (double)s.carrier_hz; }
            if (b >= n1 + n2) {
                double fl = (double)s.fase / 4294967296.0 * TPI, e = fl - f;
                while (e >  M_PI) { e -= TPI; }
                while (e < -M_PI) { e += TPI; }
                if (bien < 0 && fabs(e) * 180.0 / M_PI < 20.0) {
                    bien = b - (n1 + n2);
                } else if (bien >= 0 && fabs(e) * 180.0 / M_PI >= 20.0) {
                    bien = -1;
                }
            }
        }
        {
            double ms = (bien < 0) ? -1.0
                                   : (double)bien * (double)BLOQUE / FS * 1000.0;
            printf("    %4.1f s %14.0f ms %18.1f Hz%s\n", k_ap[a], ms, hz_fade,
                   (ms < 0.0 || ms > 150.0) ? "   <-- tarda" : "");
            /*
             * 150 ms es el tope, y sale de medir: sin tocar nada la
             * mediana es de 11 a 16 ms y el peor de 20 semillas fue 120.
             * No es un deseo, es lo que hace hoy mas un margen.
             */
            if (ms < 0.0 || ms > 150.0) {
                printf("  MAL: con un apagon de %.1f s la fase tarda %.0f ms en volver.\n"
                       "       Hoy son 11-16 de mediana. Si alguien ha estrechado el\n"
                       "       lazo o le ha puesto un volante, esto es lo que cuesta.\n",
                       k_ap[a], ms);
                fallos++;
            }
        }
    }
    printf("\n");
}

int main(void)
{
    static const float k_wn[] = { 200.0f, 100.0f, 50.0f, 20.0f, 10.0f };
    static const double k_cn[] = { 40.0, 30.0, 20.0, 10.0, 6.0, 3.0 };
    unsigned i, j;

    printf("\n== EL PLL DEL SAM CONTRA UNA PORTADORA DEBIL ==\n\n");
    printf("  Portadora a +120 Hz de la sintonia -dentro del margen de 250-\n"
           "  con ruido, 3 segundos por tirada. El error de fase es el de\n"
           "  VERDAD, medido contra la portadora generada.\n\n");

    printf("  error de fase eficaz, en grados (y si el indicador dice que si)\n\n");
    printf("   C/N      wn=200    wn=100     wn=50     wn=20     wn=10\n");
    for (j = 0; j < sizeof k_cn / sizeof k_cn[0]; j++) {
        printf("  %4.0f dB", k_cn[j]);
        for (i = 0; i < sizeof k_wn / sizeof k_wn[0]; i++) {
            double e, hz; int eng;
            tirada(k_wn[i], 120.0, k_cn[j], 3.0, &e, &eng, &hz);
            printf("  %6.1f%s", e, eng ? " si" : " NO");
        }
        printf("\n");
    }

    printf("\n  Un error de fase por debajo de ~20 grados es seguimiento\n"
           "  bueno; por encima de 60 el detector de producto ya reparte\n"
           "  la señal entre las dos salidas y se oye.\n\n");

    /*
     * Y LA PREGUNTA QUE DECIDE: con el ancho de hoy, ¿a partir de que C/N
     * el lazo deja de seguir? Es el numero que hay que comparar con el que
     * de un lazo estrecho.
     */
    {
        double e, hz; int eng;
        tirada(200.0f, 120.0, 10.0, 3.0, &e, &eng, &hz);
        printf("  con el ancho de HOY (200 Hz) y C/N 10 dB: error %.1f grados, "
               "indicador %s\n", e, eng ? "SI" : "NO");
        tirada(20.0f, 120.0, 10.0, 3.0, &e, &eng, &hz);
        printf("  con un lazo de 20 Hz y el mismo C/N:      error %.1f grados, "
               "indicador %s\n\n", e, eng ? "SI" : "NO");
    }

    cadena_entera();
    el_desvanecimiento();

    printf("%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
