/*
 * ======================================================================
 * LA SENSIBILIDAD DE FT8, CONTRA LA REFERENCIA DEL PROTOCOLO
 * ======================================================================
 *
 * LA PREGUNTA: ¿decodifica la radio todo lo que tiene que decodificar?
 *
 * Hay un numero con el que medirse y no es una opinion: FT8 decodifica
 * hasta unos -21 dB de relacion senal/ruido en 2.500 Hz de referencia,
 * con ruido blanco y sin ayuda de contexto. Ese es el suelo del
 * protocolo. Si esta radio se queda tres decibelios por encima, pierde
 * las estaciones que estan entre los dos, y eso en una banda llena son
 * unas cuantas.
 *
 * sim/ft8_banda.c ya hacia un barrido -ocho semillas, de tres en tres
 * decibelios- y daba 8/8 hasta -15 y 0/8 en -21. Util para ver que el
 * camino entero funciona, corto para decir DONDE esta el umbral y de que
 * depende.
 *
 * LO QUE MIDE ESTE, y por que cada cosa:
 *
 *   1. EL UMBRAL DE VERDAD, con la emision donde cae en la vida real:
 *      frecuencia en cualquier sitio DENTRO de un bin de 6,25 Hz y
 *      comienzo en cualquier sitio DENTRO de un simbolo de 160 ms. Es el
 *      numero que hay que comparar con -21.
 *
 *   2. EL MISMO BARRIDO CON LA EMISION CLAVADA en el bin y en el
 *      simbolo. La diferencia entre los dos es lo que cuesta no tener
 *      submuestreo: FT8_ADAPTER_FREQ_OSR y TIME_OSR valen 1 en esta
 *      radio -por RAM, ver ft8_shared_ram.h- donde la referencia de
 *      Ft8Lib usa 2 y 2.
 *
 *   3. EL BARRIDO DEL DESPLAZAMIENTO DENTRO DEL BIN, a un SNR fijo cerca
 *      del umbral. Si el coste esta en la frecuencia, aqui se ve como un
 *      valle en el medio bin.
 *
 *   4. Y EL MISMO DENTRO DEL SIMBOLO, para el eje del tiempo.
 *
 * Las cuatro corren el firmware de verdad -ft8_decimator.c,
 * ft8_waterfall_adapter.c, ft8_fft.c, ft8_decoder.c y Ft8Lib- sobre
 * audio fabricado aqui. El codificador es el mismo de sim/ft8_gen.h.
 *
 * LO QUE ESTO NO ES: una comparacion contra WSJT-X. Para eso haria falta
 * su decodificador al lado y las mismas grabaciones. Esto mide contra el
 * SNR QUE SE HA PUESTO, que para un umbral es suficiente y no depende de
 * que haya otro programa instalado.
 *
 * ======================================================================
 * LO QUE SALIO LA PRIMERA VEZ, 02/11/2026
 * ======================================================================
 *
 * Cuatro geometrias, el mismo codigo del firmware, veinticuatro semillas
 * por punto. "suelto" es la emision donde cae en la vida real; "clavado",
 * encima del bin y del simbolo:
 *
 *     bins  freq_osr time_osr bits   mag[]    suelto  clavado
 *      512     1        1       4   23.808 B   -13,8    -18,4   <- la radio
 *      256     2        1       4   23.808 B   -16,0    -18,4
 *      512     1        1       8   47.616 B   -13,8    -18,7
 *      256     2        2       8   95.232 B   -18,8    -18,7
 *
 * Y lo que lo explica, en las pruebas 3 y 4: con freq_osr = 1, una
 * emision a MEDIO BIN de distancia -3,12 Hz- no se decodifica NUNCA
 * (0 de 24 a -16 dB, contra 24 de 24 en el centro del bin). Con
 * time_osr = 1 pasa lo mismo a medio simbolo. Como una estacion de
 * verdad cae donde le da la gana, eso se come hasta CINCO DECIBELIOS de
 * sensibilidad: -13,8 contra los -18,8 de la misma radio con (2,2).
 *
 * Tres conclusiones que se leen solas:
 *
 *   - los CUATRO BITS por celda no cuestan nada (-18,4 contra -18,7, que
 *     es ruido de medida). Esa decision era buena;
 *   - el submuestreo SI cuesta, y es casi todo lo que falta;
 *   - y queda un suelo propio de -18,7 contra los -21 del protocolo. Eso
 *     es otra cosa -el diezmador, la aproximacion del logaritmo, una sola
 *     pasada sin restar lo ya decodificado- y no se ha mirado todavia.
 *
 * El precio de (2,2) son 95.232 bytes de mag[] contra los 23.808 de hoy,
 * y en el GD32F450VET6 no hay. Ver ft8_shared_ram.h.
 *
 * ======================================================================
 * Y LO QUE SE ENTREGO, 02/11/2026 (V6.45)
 * ======================================================================
 *
 *     bins  freq_osr time_osr bits   mag[]    suelto  clavado
 *      512     1        1       4   23.808 B   -13,8    -18,4   <- antes
 *      512     2        1       4   47.616 B   -16,0    -18,4   <- no cabe
 *      512     2        1       3   35.712 B   -16,0    -17,5   <- la radio
 *
 * Los tres bits son lo que hace que quepa, y no salen gratis aunque el
 * umbral no se mueva: contando aciertos de -11 a -20 dB con la emision
 * suelta, 97 de 240 antes, 129 con cuatro bits y 110 con tres. O sea la
 * mitad del premio por la mitad de la memoria.
 *
 * Lo que SI se arregla del todo es el agujero del medio bin: la prueba 3
 * pasa de 121 de 192 a 192 de 192, y la 5 de 13 de 216 a 216 de 216.
 *
 * Lo que NO se mueve es el eje del tiempo, y era de esperar: time_osr
 * sigue en 1. La prueba 4 da 117 de 192 antes y 121 despues, que es la
 * misma cifra con el ruido de veinticuatro semillas.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "ft8_decimator.h"
#include "ft8_waterfall_adapter.h"
#include "ft8_fft.h"
#include "ft8_decoder.h"
#include "ft8_shared_ram.h"
#include "ft8/constants.h"
#include "ft8/crc.h"
#include "ft8/message.h"
#include "rtc_hw.h"

/* --------------------------------------------------------------- tapones */
void rtc_hw_get(rtc_hw_datetime_t *o) { if (o) { memset(o, 0, sizeof *o); } }
const char *dxcc_pais(const char *ind) { (void)ind; return 0; }

#include "ft8_gen.h"

#define MAX_SALIDA 64
static char g_salida[MAX_SALIDA][FT8_DECODER_LINE_LEN];
static int  g_n_salida;

static void decodifica_ranura(void)
{
    int n;
    ft8_decoded_msg_t m;

    ft8_fft_init();
    ft8_decimator_reset();
    ft8_waterfall_reset();
    ft8_decoder_init();

    for (n = 0; n < RANURA_N && !ft8_waterfall_is_full(); n += 256) {
        int16_t trozo[256];
        int j;
        for (j = 0; j < 256 && (n + j) < RANURA_N; j++) {
            float v = g_audio[n + j];
            if (v >  32767.0f) { v =  32767.0f; }
            if (v < -32768.0f) { v = -32768.0f; }
            trozo[j] = (int16_t)v;
        }
        ft8_decimator_feed_block(trozo, (uint16_t)j);
        ft8_decimator_poll();
    }
    ft8_decoder_process_slot();

    g_n_salida = 0;
    while (ft8_decoder_get_message(&m)) {
        if (g_n_salida < MAX_SALIDA) {
            memcpy(g_salida[g_n_salida], m.line, FT8_DECODER_LINE_LEN);
        }
        g_n_salida++;
    }
}

/*
 * La misma ranura, pero parando a los `tope` candidatos. Es lo que hace
 * el tope de reloj de ft8_modo_poll() cuando la banda no cabe en el
 * desvio: ver su comentario. Aqui se corta por CUENTA y no por reloj -en
 * el PC no hay reloj que valga- porque lo que se quiere comprobar no es
 * el reloj sino QUE se queda sin hacer.
 */
static void decodifica_ranura_cortada(int tope)
{
    int n, hechos = 0;
    ft8_decoded_msg_t m;

    ft8_fft_init();
    ft8_decimator_reset();
    ft8_waterfall_reset();
    ft8_decoder_init();

    for (n = 0; n < RANURA_N && !ft8_waterfall_is_full(); n += 256) {
        int16_t trozo[256];
        int j;
        for (j = 0; j < 256 && (n + j) < RANURA_N; j++) {
            float v = g_audio[n + j];
            if (v >  32767.0f) { v =  32767.0f; }
            if (v < -32768.0f) { v = -32768.0f; }
            trozo[j] = (int16_t)v;
        }
        ft8_decimator_feed_block(trozo, (uint16_t)j);
        ft8_decimator_poll();
    }

    ft8_decoder_slot_empieza();
    while (hechos < tope && ft8_decoder_slot_paso()) { hechos++; }

    g_n_salida = 0;
    while (ft8_decoder_get_message(&m)) {
        if (g_n_salida < MAX_SALIDA) {
            memcpy(g_salida[g_n_salida], m.line, FT8_DECODER_LINE_LEN);
        }
        g_n_salida++;
    }
}

static int salio(const char *texto)
{
    int i;
    for (i = 0; i < g_n_salida && i < MAX_SALIDA; i++) {
        const char *p = g_salida[i];
        int tabs = 0;
        while (*p != '\0' && tabs < 4) { if (*p == '\t') { tabs++; } p++; }
        if (tabs < 4) { continue; }
        if (strncmp(p, texto, strlen(texto)) == 0) { return 1; }
    }
    return 0;
}

/* Un generador aparte del ruido: los desplazamientos tienen que ser los
 * mismos corrida tras corrida, y el ruido tiene su propia semilla. */
static uint32_t s_vai = 12345u;
static float uniforme(void)
{
    s_vai = s_vai * 1103515245u + 12345u;
    return (float)((s_vai >> 8) & 0xFFFFFFu) / 16777216.0f;
}

#define TXT   "CQ EA4XYZ IN80"
#define BIN_HZ    6.25f            /* un bin del buscador */
#define SIM_S     0.16f            /* un simbolo */
#define F0        800.0f
#define DT0       0.5f

static int g_fallos;
static uint8_t g_tonos[FT8_NN];

/* Cuantos de `n` intentos salen a `db`. `suelto` = 1 pone la emision en
 * cualquier sitio del bin y del simbolo; 0 la clava. */
static int aciertos(float db, int n, int suelto)
{
    int s, ok = 0;

    for (s = 0; s < n; s++) {
        float f = F0, dt = DT0;

        if (suelto) {
            f  = F0  + uniforme() * BIN_HZ;
            dt = DT0 + uniforme() * SIM_S;
        }
        ranura_vacia((uint32_t)(1000 + s));
        mete_emision(f, dt, amplitud_para(db), g_tonos);
        decodifica_ranura();
        ok += salio(TXT) ? 1 : 0;
    }
    return ok;
}

/* El SNR al que se decodifica la mitad, interpolado entre los dos puntos
 * que lo cruzan. Devuelve 99 si no cruza. */
static float umbral(const float *db, const int *ok, int n, int tope)
{
    int i;
    for (i = 1; i < n; i++) {
        float a = (float)ok[i - 1] / (float)tope;
        float b = (float)ok[i] / (float)tope;
        if (a >= 0.5f && b < 0.5f) {
            float t = (a - 0.5f) / (a - b);
            return db[i - 1] + t * (db[i] - db[i - 1]);
        }
    }
    return 99.0f;
}

#define N_DB   16
#define N_SEM  24

static float g_db[N_DB];
static int   g_ok_suelto[N_DB];
static int   g_ok_clavado[N_DB];

static void barrido(void)
{
    int i;

    for (i = 0; i < N_DB; i++) { g_db[i] = -11.0f - (float)i; }

    printf("\n  1 y 2. EL UMBRAL, con %d semillas por punto\n\n", N_SEM);
    printf("      SNR    suelto    clavado\n");
    for (i = 0; i < N_DB; i++) {
        s_vai = 7777u + (uint32_t)i;   /* mismos desplazamientos por punto */
        g_ok_suelto[i]  = aciertos(g_db[i], N_SEM, 1);
        g_ok_clavado[i] = aciertos(g_db[i], N_SEM, 0);
        printf("      %+5.0f   %2d/%2d     %2d/%2d\n", (double)g_db[i],
               g_ok_suelto[i], N_SEM, g_ok_clavado[i], N_SEM);
    }
}

static void di(int cond, const char *que, const char *como)
{
    printf("    %-54s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { printf("        %s\n", como); g_fallos++; }
}

int main(void)
{
    float u_suelto, u_clavado;
    int i;

    printf("\nLA SENSIBILIDAD DE FT8\n");
    printf("  La referencia del protocolo son -21 dB al 50 %%, en 2.500 Hz,\n");
    printf("  con ruido blanco y sin ayuda de contexto.\n");

    (void)ft8_tonos(TXT, g_tonos);
    barrido();

    u_suelto  = umbral(g_db, g_ok_suelto,  N_DB, N_SEM);
    u_clavado = umbral(g_db, g_ok_clavado, N_DB, N_SEM);

    printf("\n      umbral del 50 %%, suelto  .... %+.1f dB\n", (double)u_suelto);
    printf("      umbral del 50 %%, clavado .... %+.1f dB\n", (double)u_clavado);
    if (u_suelto < 90.0f && u_clavado < 90.0f) {
        printf("      lo que cuesta no estar clavado  %.1f dB\n",
               (double)(u_clavado - u_suelto));
    }

    /* 3. DENTRO DEL BIN. A un SNR en el que, clavado, se decodifica casi
     *    siempre: asi lo que se ve es el coste del desplazamiento y no el
     *    del ruido. */
    {
        float db = u_clavado + 2.0f;
        if (db > -12.0f) { db = -12.0f; }
        printf("\n  3. DENTRO DEL BIN DE 6,25 Hz, a %+.0f dB\n\n", (double)db);
        printf("      desvio    aciertos\n");
        for (i = 0; i < 8; i++) {
            float f = F0 + BIN_HZ * (float)i / 8.0f;
            int s, ok = 0;
            for (s = 0; s < N_SEM; s++) {
                ranura_vacia((uint32_t)(2000 + s));
                mete_emision(f, DT0, amplitud_para(db), g_tonos);
                decodifica_ranura();
                ok += salio(TXT) ? 1 : 0;
            }
            printf("      %4.2f Hz   %2d/%2d\n",
                   (double)(BIN_HZ * (float)i / 8.0f), ok, N_SEM);
        }
    }

    /* 4. Y DENTRO DEL SIMBOLO. */
    {
        float db = u_clavado + 2.0f;
        if (db > -12.0f) { db = -12.0f; }
        printf("\n  4. DENTRO DEL SIMBOLO DE 160 ms, a %+.0f dB\n\n", (double)db);
        printf("      desvio    aciertos\n");
        for (i = 0; i < 8; i++) {
            float dt = DT0 + SIM_S * (float)i / 8.0f;
            int s, ok = 0;
            for (s = 0; s < N_SEM; s++) {
                ranura_vacia((uint32_t)(3000 + s));
                mete_emision(F0, dt, amplitud_para(db), g_tonos);
                decodifica_ranura();
                ok += salio(TXT) ? 1 : 0;
            }
            printf("      %3.0f ms    %2d/%2d\n",
                   (double)(SIM_S * (float)i / 8.0f * 1000.0f), ok, N_SEM);
        }
    }

    /*
     * 5. Y LA PREGUNTA QUE DE VERDAD IMPORTA: ¿CUANTAS ESTACIONES?
     *
     * El umbral del 50 % esta bien para comparar dos geometrias, pero no
     * dice cuantas emisoras se pierden. Eso sale de aqui: a cada relacion
     * senal/ruido, cuantas se decodifican con la emision en el CENTRO del
     * bin y cuantas a MEDIO BIN. Una estacion de verdad cae en cualquier
     * sitio, asi que lo que se ve de verdad esta entre las dos columnas, y
     * la diferencia es lo que cuesta no tener submuestreo A ESA FUERZA DE
     * SEÑAL.
     */
    {
        static const float k_db[9] = { -8, -10, -11, -12, -13, -14, -15, -16, -17 };
        int j;
        int tot_c = 0, tot_m = 0;

        printf("\n  5. CUANTAS SE PIERDEN POR CAER A MEDIO BIN\n\n");
        printf("      SNR    centro   medio bin\n");
        for (j = 0; j < 9; j++) {
            int sm, okc = 0, okm = 0;
            for (sm = 0; sm < N_SEM; sm++) {
                ranura_vacia((uint32_t)(4000 + sm));
                mete_emision(F0, DT0, amplitud_para(k_db[j]), g_tonos);
                decodifica_ranura();
                okc += salio(TXT) ? 1 : 0;

                ranura_vacia((uint32_t)(4000 + sm));
                mete_emision(F0 + BIN_HZ * 0.5f, DT0,
                             amplitud_para(k_db[j]), g_tonos);
                decodifica_ranura();
                okm += salio(TXT) ? 1 : 0;
            }
            tot_c += okc; tot_m += okm;
            printf("      %+5.0f   %2d/%2d     %2d/%2d\n", (double)k_db[j],
                   okc, N_SEM, okm, N_SEM);
        }
        printf("\n      sumando las nueve fuerzas: %d de %d en el centro,\n",
               tot_c, 9 * N_SEM);
        printf("      %d de %d a medio bin. Una emisora cae donde le da la\n",
               tot_m, 9 * N_SEM);
        printf("      gana, asi que de cada cuatro se pierden alrededor de %d.\n",
               (int)((4.0 * (double)(tot_c - tot_m)) / (double)(2 * 9 * N_SEM) + 0.5));
    }

    /*
     * 6. AL CORTAR A MEDIAS, LO QUE SE QUEDA FUERA ES LO MAS FLOJO
     *
     * El tope de reloj de ft8_modo_poll() deja candidatos sin hacer. Que
     * eso sea aceptable depende de UNA cosa: que esten ordenados de mas
     * fuerte a mas flojo (ver ft8_decoder_slot_empieza). Sin ordenar, el
     * monticulo de ftx_find_candidates() deja la fuerte donde caiga y
     * cortar pierde estaciones buenas mientras se gasta el tiempo en
     * ruido.
     *
     * Tres emisiones a fuerzas muy distintas, y se corta a un candidato:
     * el que sobreviva tiene que ser el de -5 dB.
     */
    {
        static const char *k_txt[3] = { "CQ EA1AAA IN80",
                                        "CQ EA2BBB IN91",
                                        "CQ EA3CCC JN01" };
        static const float k_db[3]  = { -18.0f, -5.0f, -15.0f };
        int s_i, fuerte = 0, flojas = 0;

        printf("\n  6. AL CORTAR, SE QUEDA LO MAS FUERTE\n\n");
        for (s_i = 0; s_i < N_SEM; s_i++) {
            int t;

            ranura_vacia((uint32_t)(4000 + s_i));
            for (t = 0; t < 3; t++) {
                uint8_t tonos[FT8_NN];
                (void)ft8_tonos(k_txt[t], tonos);
                mete_emision(F0 + 200.0f * (float)t, DT0,
                             amplitud_para(k_db[t]), tonos);
            }
            decodifica_ranura_cortada(1);
            if (salio("CQ EA2BBB")) { fuerte++; }
            if (salio("CQ EA1AAA") || salio("CQ EA3CCC")) { flojas++; }
        }
        printf("      cortando al primer candidato, de %d ranuras:\n", N_SEM);
        printf("        sale la fuerte (-5 dB) .......... %2d\n", fuerte);
        printf("        sale alguna floja en su lugar ... %2d\n", flojas);

        di(fuerte * 2 >= N_SEM,
           "al cortar sobrevive la mas fuerte",
           "cortar pierde la fuerte: los candidatos no van ordenados");
    }

    printf("\n");
    di(u_suelto < 90.0f, "el barrido cruza el 50 %: hay umbral que medir",
       "no decodifica ni en el punto mas fuerte, o decodifica en todos");
    /*
     * EL TOPE, Y POR QUE ESTE Y NO -21.
     *
     * -21 es el suelo del protocolo y esta radio no llega: el banco esta
     * para que no EMPEORE mientras se trabaja en acercarse. El numero de
     * aqui se pone a lo medido con holgura de un decibelio, y se baja el
     * dia que se gane sensibilidad, nunca se sube.
     */
    di(u_suelto <= -15.0f,
       "y no se ha perdido sensibilidad desde la ultima medida",
       "el umbral ha subido: algo del camino de FT8 decodifica menos");

    printf("\n%s (%d fallos)\n\n",
           g_fallos ? "HAY FALLOS" : "TODO CORRECTO", g_fallos);
    return g_fallos ? 1 : 0;
}
