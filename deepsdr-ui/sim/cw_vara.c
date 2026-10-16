/*
 * LA VARA DE MEDIR DEL CW: CUANTO SE MUEVE LA ESTIMACION DE VELOCIDAD.
 *
 * *** De la lista de pendientes: "estabilizar s_dot_hops en señal debil.
 * Los huecos salen bimodales (12-17 y 26-31 saltos) pero s_dot_hops se
 * mueve de 10 a 14, un 40%. El problema es LA VARA DE MEDIR, no los
 * umbrales." ***
 *
 * POR QUE ESTE BANCO Y NO OTRO BARRIDO DE RUIDO
 * --------------------------------------------
 * sim/cwtest.c ya barre relacion señal-ruido y cuenta letras acertadas, y
 * de paso imprime la velocidad estimada... UNA VEZ, al terminar la pasada.
 * Y una sola lectura al final no dice nada del problema: una estimacion
 * puede acabar clavada en 20 ppm habiendo pasado la mitad del mensaje en
 * 14 y la otra mitad en 28. Lo que hace daño no es donde acaba: es
 * CUANTO SE MUEVE MIENTRAS, porque casi todo en cw.c se mide contra ella
 * -la frontera punto/raya son 2,0 puntos, el hueco de letra 2,0, el de
 * palabra 5,0, el suelo de chasquido medio punto, el techo de absurdo
 * seis-. Si la referencia respira, todos esos umbrales respiran con ella.
 *
 * Asi que esto mira la estimacion DURANTE la pasada, bloque a bloque, y
 * saca el numero que faltaba: el recorrido pico a pico en tanto por
 * ciento de la mediana.
 *
 * QUE SE MIDE, Y CUANDO
 * ---------------------
 * Solo se apunta mientras la puerta de calidad esta abierta
 * (cw_get_decode_ok()). Antes de que abra, la estimacion esta arrancando
 * desde la pista que se le dio y moverse es lo que TIENE que hacer;
 * contarlo ahi seria medir el calentamiento y llamarlo inestabilidad.
 *
 * Y se mide sobre varias realizaciones de ruido distintas, porque con una
 * sola lo que sale es la anecdota de esa semilla.
 *
 * EL NUMERO
 * ---------
 *   mediana   donde se queda, comparada con la verdad
 *   p10-p90   el recorrido donde cae el 80% del tiempo
 *   pico      el recorrido entero, en % de la mediana  <- ESTE es el que
 *                                                         importa
 *
 * No hay tope todavia a proposito: primero sale el numero, DESPUES se
 * decide que es aceptable. Al reves -poner un umbral y buscar como
 * cumplirlo- es como se acaba ajustando el banco en vez del codigo.
 */
#include "cw.h"
#include "cw_sintetiza.h"

#define MAX_M 20000

static float s_m[MAX_M];
static int   s_n;
/*
 * Y LO MISMO SIN EL ARRANQUE. La primera version mezclaba dos cosas que no
 * son la misma: la estimacion CONVERGIENDO despues de que la puerta abra
 * -que moverse ahi es lo que tiene que hacer- y la estimacion moviendose
 * YA ENGANCHADA, que es la averia. Se separan descartando los primeros
 * CALMA elementos de cada apertura.
 *
 * Y se cuenta cuantas veces la puerta se ha vuelto a cerrar despues de
 * haber abierto: cada cierre deja al ancla resembrar la estimacion (ver
 * CW_ANCHOR_LO/HI en cw.c, que solo actua con la puerta cerrada), asi que
 * un reenganche a mitad de mensaje es un salto de la referencia y no un
 * temblor.
 */
#define CALMA 40
static float s_s[MAX_M];
static int   s_ns;
static int   s_reenganches;

static int cmpf(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

/* Una pasada: sintetiza, mete el audio en bloques por cw.c de verdad y
 * apunta la estimacion de velocidad despues de cada bloque. */
static void pasada(const char *texto, float wpm, float snr_db,
                   const ratios_t *w, float pista_wpm)
{
    size_t n, off;
    float *buf = synth(texto, wpm, snr_db, w, -99.0f, PITCH, &n);

    cw_init();
    cw_set_pitch_hz(PITCH);
    cw_set_wpm_hint(pista_wpm);
    cw_set_enabled(1u);

    {
        int abierta = 0, desde = 0, visto = 0;

        for (off = 0; off + CW_BLOCK_SAMPLES <= n; off += CW_BLOCK_SAMPLES) {
            char c;
            cw_process(buf + off, CW_BLOCK_SAMPLES);
            while (cw_get_char(&c)) { }
            if (cw_get_decode_ok()) {
                if (!abierta) {
                    abierta = 1; desde = 0;
                    if (visto) { s_reenganches++; }
                    visto = 1;
                }
                if (s_n < MAX_M) { s_m[s_n++] = cw_get_wpm(); }
                if (desde >= CALMA && s_ns < MAX_M) {
                    s_s[s_ns++] = cw_get_wpm();
                }
                desde++;
            } else {
                abierta = 0;
            }
        }
    }
    free(buf);
}

static const char *k_msg =
    "CQ CQ DE EA4XYZ EA4XYZ K = UR RST 579 579 NAME OSCAR QTH MADRID = "
    "HW? AR DL1ABC DE EA4XYZ KN";

static int detallado = 0;

static void caso(const char *etiqueta, float wpm, float snr,
                 const ratios_t *w, float pista, int realizaciones)
{
    int r;
    float med, p10, p90, lo, hi, pico;

    s_n = 0; s_ns = 0; s_reenganches = 0;
    for (r = 0; r < realizaciones; r++) {
        /*
         * CADA REALIZACION POR SEPARADO, ademas del total. La primera
         * version solo daba el recorrido del monton entero, y ese numero
         * no distingue dos averias opuestas: que la estimacion se mueva
         * DENTRO de una pasada, o que cada pasada se quede quieta en un
         * sitio distinto. La primera es temblor; la segunda es que el
         * resultado depende del ruido que te toque, que es peor.
         */
        int desde = s_n;
        float lo_r = 1e9f, hi_r = -1e9f;
        int q;

        g_rng = 2000u + (uint32_t)r * 7919u;
        pasada(k_msg, wpm, snr, w, pista);
        for (q = desde; q < s_n; q++) {
            if (s_m[q] < lo_r) { lo_r = s_m[q]; }
            if (s_m[q] > hi_r) { hi_r = s_m[q]; }
        }
        if (s_n > desde && detallado) {
            printf("        pasada %d: %5.1f - %5.1f ppm  (%4.1f%%)\n",
                   r, (double)lo_r, (double)hi_r,
                   (double)(100.0f * (hi_r - lo_r) / ((lo_r + hi_r) * 0.5f)));
        }
    }

    if (s_n < 20) {
        printf("  %-26s %3.0f ppm %5.0f dB    sin enganche "
               "(%d muestras)\n", etiqueta, (double)wpm, (double)snr, s_n);
        return;
    }

    qsort(s_m, (size_t)s_n, sizeof s_m[0], cmpf);
    med = s_m[s_n / 2];
    p10 = s_m[(int)(0.10 * s_n)];
    p90 = s_m[(int)(0.90 * s_n)];
    lo  = s_m[0];
    hi  = s_m[s_n - 1];
    pico = (med > 0.01f) ? 100.0f * (hi - lo) / med : 999.0f;

    {
        float pico_s = 999.0f;

        if (s_ns >= 20) {
            float ms;
            qsort(s_s, (size_t)s_ns, sizeof s_s[0], cmpf);
            ms = s_s[s_ns / 2];
            pico_s = (ms > 0.01f) ? 100.0f * (s_s[s_ns - 1] - s_s[0]) / ms
                                  : 999.0f;
        }
        printf("  %-26s %3.0f ppm %5.0f dB   mediana %5.1f (%+5.1f%%)   "
               "pico %5.1f%%   ya enganchada %5.1f%%   reenganches %d\n",
               etiqueta, (double)wpm, (double)snr,
               (double)med, (double)(100.0f * (med - wpm) / wpm),
               (double)pico, (double)pico_s, s_reenganches);
        (void)p10; (void)p90;
    }
}

/*
 * EL DETALLE DE UNA PASADA, que es lo que separa "tiembla" de "da saltos".
 *
 * El resumen de arriba decia 20% de recorrido con la puerta abierta y SIN
 * reenganches, o sea que no era ni el calentamiento ni el ancla. Pero
 * tambien decia que entre el 10 y el 90 por ciento del tiempo la
 * estimacion cabe en cuatro decimas: o sea que el 20% lo meten UNOS POCOS
 * elementos, no un temblor continuo.
 *
 * Esto los saca por su nombre. Mide la longitud de cada marca CONTANDO
 * SALTOS CON cw_get_key(), que es una medida del banco y no de cw.c -o
 * sea, no es preguntarle al sospechoso-, y al lado pone lo que la
 * estimacion hizo justo despues. Lo que salte se ve con su longitud
 * delante.
 */
static void detalle(const char *etiqueta, float wpm, float snr,
                    const ratios_t *w, float pista)
{
    size_t n, off;
    float *buf = synth(k_msg, wpm, snr, w, -99.0f, PITCH, &n);
    int marca = 0, largo = 0, hops = 0;
    float antes = 0.0f;
    int saltos = 0, elems = 0;
    float punto_real = 450.0f / wpm;

    cw_init();
    cw_set_pitch_hz(PITCH);
    cw_set_wpm_hint(pista);
    cw_set_enabled(1u);

    printf("\n  DETALLE DE UNA PASADA: %s, %.0f ppm, %.0f dB "
           "(el punto de verdad son %.1f saltos)\n",
           etiqueta, (double)wpm, (double)snr, (double)punto_real);
    printf("    saltos  = cuanto dura la marca, contada por el banco\n");
    printf("    ppm     = la estimacion justo despues de esa marca\n\n");

    for (off = 0; off + CW_BLOCK_SAMPLES <= n; off += CW_BLOCK_SAMPLES) {
        char c;
        cw_process(buf + off, CW_BLOCK_SAMPLES);
        while (cw_get_char(&c)) { }
        hops++;
        if (cw_get_key()) {
            if (!marca) { marca = 1; largo = 0; }
            largo++;
        } else if (marca) {
            float ahora = cw_get_wpm();
            float dif = (antes > 0.01f) ? 100.0f * (ahora - antes) / antes : 0.0f;

            marca = 0;
            elems++;
            if (cw_get_decode_ok() && (dif > 2.0f || dif < -2.0f)) {
                saltos++;
                printf("      salto %2d   hop %5d   marca %3d saltos "
                       "(%.2f puntos)   ppm %5.1f -> %5.1f  (%+5.1f%%)\n",
                       saltos, hops, largo, (double)(largo / punto_real),
                       (double)antes, (double)ahora, (double)dif);
            }
            antes = ahora;
        }
    }
    printf("\n      %d elementos, %d saltos de mas del 2%%\n", elems, saltos);
    free(buf);
}

int main(void)
{
    static const ratios_t k_humano = { 2.6f, 1.15f, 4.2f, 9.0f };

    printf("\nCUANTO SE MUEVE LA ESTIMACION DE VELOCIDAD MIENTRAS DECODIFICA\n");
    printf("  (solo con la puerta de calidad abierta; 6 realizaciones por caso)\n");
    printf("  el numero que importa es el ultimo: el recorrido pico a pico\n");
    printf("  en %% de la mediana. Todo lo que se decide en cw.c se mide\n");
    printf("  contra esta estimacion.\n\n");

    printf("  MAQUINA, proporciones perfectas, con la pista acertada:\n");
    detallado = 1;
    caso("limpio",            20.0f,  40.0f, &k_perfecto, 20.0f, 6);
    caso("ruido moderado",    20.0f,  10.0f, &k_perfecto, 20.0f, 6);
    caso("ruido fuerte",      20.0f,   3.0f, &k_perfecto, 20.0f, 6);
    caso("al limite",         20.0f,   0.0f, &k_perfecto, 20.0f, 6);
    detallado = 0;

    printf("\n  CON LA PISTA EQUIVOCADA (la radio arranca en 20 y la\n");
    printf("  estacion va a otra cosa): es el caso de verdad, porque\n");
    printf("  nadie le dice a la radio a que velocidad va el otro.\n");
    caso("lento, pista 20",   10.0f,  20.0f, &k_perfecto, 20.0f, 6);
    caso("rapido, pista 20",  32.0f,  20.0f, &k_perfecto, 20.0f, 6);
    caso("lento y con ruido", 10.0f,   6.0f, &k_perfecto, 20.0f, 6);

    /*
     * LA SOSPECHA QUE SALIO DE LOS NUMEROS DE ARRIBA.
     *
     * 10 ppm se queda en 2,1% y 20 ppm se mueve 20,6%, con el MISMO ruido
     * y el mismo mensaje. La diferencia no puede ser la velocidad en si.
     * Pero si se traduce a saltos -que es en lo que cw.c mide, bloques de
     * 32 muestras a 12 kHz- aparece otra cosa:
     *
     *     10 ppm -> 45,0 saltos por punto   entero      2,1%
     *     32 ppm -> 14,1 saltos             casi entero 4,7%
     *     12 ppm -> 37,5 saltos             y medio     8,0%
     *     20 ppm -> 22,5 saltos             y medio    20,6%
     *
     * Con un punto de 22,5 saltos, la marca mide 22 o 23 segun donde caiga
     * contra la rejilla de bloques, y la raya 67 o 68. La estimacion no
     * esta temblando por el ruido: esta temblando por la REGLA con la que
     * se mide.
     *
     * Esto lo prueba o lo tira: mismas condiciones, velocidades elegidas
     * para que el punto caiga justo en un numero entero de saltos y justo
     * en la mitad.
     */
    printf("\n  LA REJILLA DE BLOQUES: el punto en saltos enteros o en mitades\n");
    printf("  (mismo mensaje y mismo ruido; lo unico que cambia es donde\n");
    printf("   cae el punto contra la rejilla de 32 muestras)\n");
    caso("25,0 saltos (18 ppm)",   18.00f, 20.0f, &k_perfecto, 18.00f, 6);
    caso("22,5 saltos (20 ppm)",   20.00f, 20.0f, &k_perfecto, 20.00f, 6);
    caso("20,0 saltos (22,5 ppm)", 22.50f, 20.0f, &k_perfecto, 22.50f, 6);
    caso("18,0 saltos (25 ppm)",   25.00f, 20.0f, &k_perfecto, 25.00f, 6);
    caso("17,5 saltos (25,7 ppm)", 25.714f,20.0f, &k_perfecto, 25.714f,6);
    caso("30,0 saltos (15 ppm)",   15.00f, 20.0f, &k_perfecto, 15.00f, 6);
    caso("29,5 saltos (15,3 ppm)", 15.254f,20.0f, &k_perfecto, 15.254f,6);

    printf("\n  A MANO, proporciones de persona (raya 2,6; huecos 1,15/4,2/9):\n");
    caso("a mano, limpio",    20.0f,  40.0f, &k_humano, 20.0f, 6);
    caso("a mano, con ruido", 20.0f,   6.0f, &k_humano, 20.0f, 6);
    caso("a mano, lento",     12.0f,   6.0f, &k_humano, 20.0f, 6);

    detalle("limpio", 20.0f, 40.0f, &k_perfecto, 20.0f);

    printf("\n");
    return 0;
}
