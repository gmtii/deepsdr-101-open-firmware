/*
 * WSPR DE PUNTA A PUNTA: DEL AUDIO AL INDICATIVO.
 *
 * Pedido por el usuario.
 *
 * Aqui se fabrica una emision de WSPR como la que saldria del altavoz
 * -110,6 segundos de audio de banda lateral a 12 kHz, con su desvio de
 * frecuencia, su desvio de reloj y su ruido- y se le da al receptor de
 * verdad, el mismo wspr_rx.c que corre en la radio. Si sale el indicativo
 * que se metio, el camino entero funciona.
 *
 * ESTO ES LO QUE SUSTITUYE A TENER UNA ANTENA. Un receptor de WSPR no se
 * puede depurar mirando la pantalla: una emision tarda dos minutos, no se
 * repite, y cuando falla no dice por que. Con esto se prueban en segundos
 * cien combinaciones de frecuencia, reloj y ruido, y cada fallo es
 * reproducible.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include "wspr_msg.h"
#include "wspr_fano.h"
#include "wspr_rx.h"

static int fallos;

/* Reparto de trabajo entre la interrupcion y el bucle. Ver la
 * comprobacion del final. */
double g_t_mete, g_t_pasos;
static uint8_t g_ram[WSPR_RX_RAM];

static void exige(int cond, const char *que)
{
    printf("    %-56s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

#define FS       12000
#define BAUDIO   (12000.0 / 8192.0)          /* 1,46484375 Hz */
#define T_SIM    (8192.0 / 12000.0)          /* 0,682667 s */

/* Ruido gaussiano barato pero honrado: Box-Muller. */
static double gauss(void)
{
    double u1 = (rand() + 1.0) / (RAND_MAX + 2.0);
    double u2 = (rand() + 1.0) / (RAND_MAX + 2.0);

    return sqrt(-2.0 * log(u1)) * cos(6.283185307 * u2);
}

/*
 * Fabrica la emision y se la va dando al receptor en trozos de 600
 * muestras, que es mas o menos lo que le llega a la radio de una vez.
 *
 *   df_hz    donde esta el tono 0 respecto del centro de la ventana
 *   dt_s     cuanto se adelanta o se atrasa respecto de donde miramos
 *   amp      amplitud de la señal; el ruido es siempre 1,0
 */
static void emite(const uint8_t sim[WSPR_SIMBOLOS], double df_hz, double dt_s,
                  double amp, unsigned semilla)
{
    double fase = 0.0;
    long n_total = (long)((WSPR_RX_PASOS / 2.0) * T_SIM * FS);
    long n;
    float trozo[600];
    int j = 0;

    srand(semilla);
    wspr_rx_reinicia();

    for (n = 0; n < n_total; n++) {
        /*
         * La captura empieza WSPR_RX_ANTES_MS antes de la hora, que es
         * lo que hace la radio: ver WSPR_RX_ANTES_MS. Asi `dt_s` es el
         * desvio de reloj de verdad -adelanto o atraso respecto de la
         * hora- y no "cuanto despues de empezar a mirar".
         */
        double t = (double)n / FS - (WSPR_RX_ANTES_MS / 1000.0) - dt_s;
        double v = gauss();          /* ruido, siempre */

        if (t >= 0.0 && t < WSPR_SIMBOLOS * T_SIM) {
            int k = (int)(t / T_SIM);
            double f;

            if (k >= 0 && k < (int)WSPR_SIMBOLOS) {
                f = WSPR_RX_CENTRO_HZ + df_hz + sim[k] * BAUDIO;
                fase += 6.283185307 * f / FS;
                if (fase > 6.283185307) { fase -= 6.283185307; }
                v += amp * cos(fase);
            }
        }
        trozo[j++] = (float)v;
        if (j == 600) {
            struct timespec a, b;

            clock_gettime(CLOCK_MONOTONIC, &a);
            wspr_rx_mete(trozo, 600);
            clock_gettime(CLOCK_MONOTONIC, &b);
            g_t_mete += (double)(b.tv_sec - a.tv_sec)
                      + (double)(b.tv_nsec - a.tv_nsec) * 1e-9;
            clock_gettime(CLOCK_MONOTONIC, &a);
            /*
             * Y el trabajo caro, COMO EN LA RADIO: wspr_rx_mete() solo
             * mezcla y diezma, y deja apuntados los pasos del
             * espectrograma para que los haga el bucle principal. Si el
             * banco no llamara a esto, el espectrograma se quedaria vacio
             * - que es exactamente lo que pasaria en la radio si alguien
             * se olvidara de la llamada en main.c.
             */
            (void)wspr_rx_pasos_pendientes();
            clock_gettime(CLOCK_MONOTONIC, &b);
            g_t_pasos += (double)(b.tv_sec - a.tv_sec)
                       + (double)(b.tv_nsec - a.tv_nsec) * 1e-9;
            j = 0;
        }
    }
    if (j > 0) { wspr_rx_mete(trozo, (uint16_t)j); }
    (void)wspr_rx_pasos_pendientes();
}

static int prueba(const char *cal, const char *loc, uint8_t dbm,
                  double df_hz, double dt_s, double amp, unsigned semilla,
                  int callado)
{
    wspr_msg_t a, b;
    uint8_t bits[7], sim[WSPR_SIMBOLOS], q = 0U;
    int16_t hz = 0;
    int ok;

    memset(&a, 0, sizeof a);
    snprintf(a.indicativo, sizeof a.indicativo, "%s", cal);
    snprintf(a.locutor, sizeof a.locutor, "%s", loc);
    a.dbm = dbm;
    if (!wspr_empaqueta(&a, bits)) { return 0; }
    wspr_codifica(bits, sim);

    emite(sim, df_hz, dt_s, amp, semilla);

    memset(&b, 0, sizeof b);
    ok = wspr_rx_decodifica(&b, &hz, &q)
      && strcmp(a.indicativo, b.indicativo) == 0
      && strcmp(a.locutor, b.locutor) == 0
      && a.dbm == b.dbm;

    if (!callado) {
        printf("    df %+6.1f Hz  dt %+5.2f s  amp %4.2f  ->  ", df_hz, dt_s, amp);
        if (ok) {
            printf("%s %s %u dBm  (%+.2f Hz, cuadra %u)   bien\n",
                   b.indicativo, b.locutor, b.dbm, hz / 100.0, q);
        } else {
            printf("nada                                       %s\n",
                   "MAL");
        }
    }
    return ok;
}

/*
 * EL ANCLA: NO BASTA CON IDA Y VUELTA.
 *
 * Todo lo de abajo codifica con nuestro codificador y decodifica con
 * nuestro decodificador. Si los dos comparten el mismo error de
 * convenio, se cancela y sale "bien" - y eso es exactamente lo que
 * paso con el localizador: se nos olvido el "179 menos" en las dos
 * direcciones, la ida y vuelta salia limpia, y en el aire ON4WS
 * -que esta en JO20- aparecia en la pantalla como IO70.
 *
 * Lo destapo una emision de verdad y una fuente de fuera. Para que no
 * vuelva a hacer falta una antena, aqui van numeros ABSOLUTOS sacados
 * del estandar, no de nuestro propio codigo:
 *
 *     ng = 180*(179 - 10*(g0-'A') - (g2-'0')) + 10*(g1-'A') + (g3-'0')
 *
 * Si alguien cambia el convenio, esto se cae aunque la ida y vuelta
 * siga cuadrando.
 */
static void ancla_localizador(void)
{
    static const struct { const char *g; uint32_t n; } k[] = {
        { "JO20", 15800U },   /* ON4WS, Belgica - salia IO70 */
        { "IO90", 16340U },   /* G4VNM, sur de Inglaterra - salia JO00 */
        { "IN80", 16510U },
        { "FN42", 22632U },
        { "PM95",  3725U },
        { "AA00", 32220U },   /* las dos esquinas */
        { "RR99",   179U }
    };
    unsigned i;

    printf("\n  el localizador contra el estandar, no contra si mismo\n");
    for (i = 0U; i < sizeof k / sizeof k[0]; i++) {
        wspr_msg_t a, b;
        uint8_t bits[7];
        char linea[80];

        memset(&a, 0, sizeof a);
        snprintf(a.indicativo, sizeof a.indicativo, "%s", "EA4XYZ");
        snprintf(a.locutor, sizeof a.locutor, "%s", k[i].g);
        a.dbm = 30U;

        /* 1) el numero que sale tiene que ser EL del estandar */
        if (!wspr_empaqueta(&a, bits)) {
            snprintf(linea, sizeof linea, "%s no se pudo empaquetar", k[i].g);
            exige(0, linea);
            continue;
        }
        {
            /*
             * El localizador son los 15 bits que empiezan en el 28, y
             * los bits van de mas peso a menos: 4 que quedan del byte 3,
             * el byte 4 entero, y 3 del byte 5.
             */
            uint32_t n = (uint32_t)(bits[3] & 0x0FU) << 11;

            n |= (uint32_t)bits[4] << 3;
            n |= (uint32_t)bits[5] >> 5;
            snprintf(linea, sizeof linea, "%s se guarda como %u (tiene que ser %u)",
                     k[i].g, (unsigned)n, (unsigned)k[i].n);
            exige(n == k[i].n, linea);
        }

        /* 2) y al sacarlo tiene que volver el mismo cuadrado */
        memset(&b, 0, sizeof b);
        (void)wspr_desempaqueta(bits, &b);
        snprintf(linea, sizeof linea, "%u vuelve a ser %s", (unsigned)k[i].n, k[i].g);
        exige(strcmp(b.locutor, k[i].g) == 0, linea);
    }
}

/*
 * LA POTENCIA COMO MARCA DE TIPO.
 *
 * En WSPR solo existen 19 potencias, las que acaban en 0, 3 o 7. Un
 * numero fuera de esa lista quiere decir que el mensaje NO es del tipo
 * normal y que el indicativo y el cuadrado que salen son basura.
 *
 * Esto se colo en antena con la v1.95:
 *
 *     05:59  G8SCU  EK07  39 dBm  Cal.100
 *
 * y el 100 no era mentira: los simbolos recibidos cuadraban exactos con
 * ese grupo de bits. Lo que estaba mal era darlos por buenos.
 */
static void ancla_potencia(void)
{
    static const uint8_t k_valen[] = {
        0U, 3U, 7U, 10U, 13U, 17U, 20U, 23U, 27U, 30U,
        33U, 37U, 40U, 43U, 47U, 50U, 53U, 57U, 60U
    };
    static const uint8_t k_no[] = { 1U, 2U, 5U, 9U, 25U, 39U, 44U, 55U, 61U };
    unsigned i;
    char linea[80];
    int bien = 1;

    printf("\n  la potencia dice de que tipo es el mensaje\n");

    for (i = 0U; i < sizeof k_valen; i++) {
        wspr_msg_t m;
        uint8_t bits[7];

        memset(&m, 0, sizeof m);
        snprintf(m.indicativo, sizeof m.indicativo, "%s", "EA4XYZ");
        snprintf(m.locutor, sizeof m.locutor, "%s", "IN80");
        m.dbm = k_valen[i];
        if (!wspr_empaqueta(&m, bits)) { bien = 0; }
    }
    exige(bien, "las 19 potencias del estandar se admiten");

    bien = 1;
    for (i = 0U; i < sizeof k_no; i++) {
        wspr_msg_t m;
        uint8_t bits[7];

        memset(&m, 0, sizeof m);
        snprintf(m.indicativo, sizeof m.indicativo, "%s", "EA4XYZ");
        snprintf(m.locutor, sizeof m.locutor, "%s", "IN80");
        m.dbm = k_no[i];
        if (wspr_empaqueta(&m, bits)) {
            snprintf(linea, sizeof linea, "%u dBm no deberia empaquetarse", k_no[i]);
            exige(0, linea);
            bien = 0;
        }
    }
    if (bien) { exige(1, "las que no existen se rechazan al empaquetar"); }

    /*
     * Y el caso de antena: un mensaje por lo demas perfecto al que se le
     * cambia la potencia a 39. Tiene que dejar de salir.
     */
    {
        wspr_msg_t m, b;
        uint8_t bits[7];

        memset(&m, 0, sizeof m);
        snprintf(m.indicativo, sizeof m.indicativo, "%s", "G8SCU");
        snprintf(m.locutor, sizeof m.locutor, "%s", "IO92");
        m.dbm = 37U;
        exige(wspr_empaqueta(&m, bits) != 0U, "G8SCU IO92 37 dBm se empaqueta");

        /* los 7 bits de potencia empiezan en el 43: 5 abajo del byte 5 y
         * 2 arriba del byte 6. Se meten 39+64 = 103. */
        bits[5] = (uint8_t)((bits[5] & 0xE0U) | (103U >> 2));
        bits[6] = (uint8_t)((bits[6] & 0x3FU) | ((103U & 3U) << 6));

        memset(&b, 0, sizeof b);
        exige(wspr_desempaqueta(bits, &b) == 0U,
              "el mismo con 39 dBm se rechaza (era tipo 2 o 3)");
    }
}

int main(void)
{
    printf("\n  WSPR: DEL AUDIO AL INDICATIVO\n");
    wspr_rx_init(g_ram, sizeof g_ram);

    ancla_localizador();
    ancla_potencia();

    printf("\n  la señal en el centro, sin ruido apreciable\n");
    {
        int ok = prueba("EA4XYZ", "IN80", 37U, 0.0, 0.0, 30.0, 1, 0);
        exige(ok, "sale el mensaje que se metio");
        if (!ok) {
            printf("\n  (sin esto lo de abajo no dice nada)\n");
            printf("\n%s (%d fallos)\n", "HAY FALLOS", ++fallos);
            return 1;
        }
    }

    printf("\n  y ahora movida de sitio: el que transmite elige donde\n");
    {
        int bien = 0, total = 0;
        double df;

        for (df = -60.0; df <= 60.0; df += 20.0) {
            total++;
            bien += prueba("G0ABC", "IO91", 23U, df, 0.0, 30.0, 7, 0);
        }
        exige(bien == total, "la encuentra en toda la ventana");
    }

    printf("\n  y con el reloj desajustado\n");
    {
        int bien = 0, total = 0;
        double dt;

        for (dt = -1.0; dt <= 1.0; dt += 0.5) {
            total++;
            bien += prueba("K1AB", "FN42", 30U, 12.0, dt, 30.0, 11, 0);
        }
        exige(bien >= total - 1, "aguanta un segundo de desfase en cada sentido");
    }

    printf("\n  bajando la señal hasta que se pierde\n");
    {
        double amp;

        for (amp = 8.0; amp >= 0.02; amp /= 2.0) {
            int bien = 0, k;

            for (k = 0; k < 6; k++) {
                bien += prueba("JA1ZZZ", "PM95", 27U, -8.0, 0.2, amp,
                               (unsigned)(100 + k), 1);
            }
            /*
             * En dB referidos a 2,5 kHz, que es como se mide WSPR.
             *
             * El ruido de gauss() tiene varianza 1 repartida por los
             * 12 kHz del audio, o sea 1/12000 por hercio; en 2,5 kHz son
             * 2500/12000 = 0,208. Un tono de amplitud A tiene potencia
             * A*A/2. Asi que:
             *
             *     S/N(2,5 kHz) = (A*A/2) / 0,208 = A*A * 2,4
             *
             * La primera version de esta linea metia el ancho del tono
             * (1,46 Hz) en la cuenta y salian 38 dB de mas: decia -59 dB
             * donde de verdad hay -21. Un numero asi, impreso en un
             * banco, es peor que no imprimir nada - habria quedado
             * escrito que esto oye treinta dB por debajo de lo que oye
             * WSPR de verdad.
             */
            printf("    amplitud %5.2f  (%+5.1f dB en 2,5 kHz)  ->  %d de 6\n",
                   amp, 10.0 * log10(amp * amp * 2.4), bien);
            if (amp >= 4.0) {
                exige(bien == 6, "con señal clara no puede fallar ninguna");
            }
        }
    }

    printf("\n  y lo que NO puede pasar: inventarse una baliza\n");
    {
        uint8_t sim[WSPR_SIMBOLOS];
        int inventados = 0, k;

        memset(sim, 0, sizeof sim);
        for (k = 0; k < 8; k++) {
            wspr_msg_t m;
            int16_t hz; uint8_t q;

            /* Ruido y nada mas: amplitud cero. */
            emite(sim, 0.0, 0.0, 0.0, (unsigned)(5000 + k));
            memset(&m, 0, sizeof m);
            if (wspr_rx_decodifica(&m, &hz, &q)) {
                inventados++;
                printf("      se invento %s %s %u dBm (cuadra %u)\n",
                       m.indicativo, m.locutor, m.dbm, q);
            }
        }
        printf("      %d de 8 tandas de ruido dieron baliza\n", inventados);
        exige(inventados == 0, "ocho tandas de ruido y ni una baliza");
    }

    /*
     * DONDE SE GASTA EL TIEMPO, que en esta radio no es una curiosidad.
     *
     * wspr_rx_mete() corre DENTRO de la interrupcion del audio y
     * wspr_rx_pasos_pendientes() en el bucle principal. Un bloque de audio
     * dura 2,67 ms y no admite que nadie le robe mas de una fraccion, asi
     * que todo lo caro tiene que estar del lado del bucle.
     *
     * Esto ya fallo: el paso del espectrograma -274 Goertzel de 256
     * muestras, unos 4 ms en el micro- estaba dentro de mete(), o sea
     * dentro de la interrupcion, y el conversor repetia bloques viejos.
     * Sonaba un tono durante toda la captura.
     *
     * Aqui se mide el reparto. Si alguien vuelve a meter el trabajo en el
     * lado de la interrupcion, la proporcion se da la vuelta y el banco lo
     * dice - sin necesidad de tener la radio delante.
     */
    {
        double tot = g_t_mete + g_t_pasos;
        double pc = (tot > 0.0) ? (100.0 * g_t_mete / tot) : 0.0;

        printf("\n  donde se gasta el tiempo:\n");
        printf("    interrupcion (mezcla y diezma)   %6.2f %%\n", pc);
        printf("    bucle principal (espectrograma)  %6.2f %%\n", 100.0 - pc);
        exige(pc < 25.0,
             "el trabajo caro NO esta del lado de la interrupcion");
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
