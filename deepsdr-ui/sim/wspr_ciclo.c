/*
 * EL CICLO DE WSPR: cuando abre la oreja, cuando la cierra y cuanto falta.
 *
 * POR QUE EXISTE (27/09/2026). Porque esto es lo unico de WSPR que NO se
 * puede comprobar mirando la radio en un rato razonable. Una emision dura
 * 110,6 segundos y solo empieza en los minutos pares, asi que ver dos
 * ciclos completos son cinco minutos delante de la pantalla, y lo que se
 * ve es una barra. Si la cuenta atras se equivoca en un segundo, o si la
 * captura abre en el minuto impar en vez del par, lo que se nota no es un
 * numero raro: es que no decodifica NUNCA. Y eso se parece demasiado a
 * "no hay propagacion".
 *
 * Aqui el reloj lo pone este fichero, asi que cinco minutos son un
 * instante y se puede mirar segundo a segundo.
 *
 * LO QUE COMPRUEBA, y por que cada cosa:
 *
 *   1. Que wspr_modo_start() ARRANCA. Parece tonto y no lo es: el
 *      espectrograma pide 47.056 bytes y el trozo de memoria prestada que
 *      usa HFDL son 45.216. Con ese, start() devolvia 0 SIEMPRE y el modo
 *      se ponia sin ponerse - pantalla entera, cero mensajes, ni un aviso.
 *      Se caza con hfdl_ram_todo() y con el _Static_assert de wspr_modo.c;
 *      esta es la tercera red, la que lo prueba de verdad ejecutandolo.
 *
 *   2. Que la captura abre UNA vez por minuto par, y en el segundo 59 del
 *      impar anterior - no en el 0 del par. Ese segundo de adelanto es
 *      WSPR_RX_ANTES_MS: lo que llegue antes de abrir no esta, y la
 *      busqueda de sincronismo no puede ir hacia atras.
 *
 *   3. Que la cuenta atras baja de segundo en segundo, sin saltos ni
 *      vueltas atras, y llega a 0 justo al abrir. Es lo unico que se mueve
 *      en la pantalla durante los dos minutos en que WSPR no puede hacer
 *      nada; si se queda quieta o pega un salto, lo que el usuario ve es un
 *      cuelgue.
 *
 *   4. Que con SILENCIO no sale ni un indicativo. WSPR no lleva CRC, asi
 *      que el decodificador de Fano siempre devuelve ALGO: lo que separa
 *      un mensaje de un invento es la corroboracion (ver WSPR_CUADRA_MIN).
 *      Dos ciclos completos de silencio tienen que dar cero lineas.
 *
 * El audio es silencio y no ruido a proposito: aqui no se mide la
 * sensibilidad -de eso va sim/wspr_aire.c, que sintetiza una emision de
 * verdad- sino el reloj de la maquina de estados.
 */
#include "wspr_modo.h"
#include "wspr_rx.h"
#include "rtc_hw.h"
#include <stdio.h>
#include <string.h>

volatile uint32_t g_msticks;

/* El reloj, puesto a mano. */
static rtc_hw_datetime_t s_ahora = { 2026U, 9U, 27U, 12U, 0U, 0U };

void rtc_hw_get(rtc_hw_datetime_t *out) { *out = s_ahora; }

/*
 * Y "alguien le ha puesto la hora alguna vez", que desde el 30/09/2026 es lo
 * que miran ft8_modo.c y wspr_modo.c para decidir si sale el aviso de poner
 * el reloj en hora. Pedido por el usuario. Aqui va a 1: lo que mide
 * este banco es el decodificador, no el aviso.
 *
 * No se coge de sim/rtc_sim.c -que tiene la misma funcion- porque este banco
 * necesita SU PROPIO reloj y rtc_sim.c da uno fijo; enlazar los dos choca en
 * rtc_hw_get().
 */
bool rtc_hw_has_ever_synced(void) { return true; }

static void avanza_un_segundo(void)
{
    g_msticks += 1000UL;
    if (++s_ahora.second < 60U) { return; }
    s_ahora.second = 0U;
    if (++s_ahora.minute < 60U) { return; }
    s_ahora.minute = 0U;
    s_ahora.hour = (uint8_t)((s_ahora.hour + 1U) % 24U);
}

static int fallos;

static void bien(int ok, const char *que)
{
    printf("    %-52s %s\n", que, ok ? "bien" : "MAL");
    if (!ok) { fallos++; }
}

int main(void)
{
    static float silencio[1200];
    unsigned s, aperturas = 0U, cierres = 0U;
    unsigned abrio_en_minuto_impar = 0U, abrio_en_segundo_59 = 0U;
    unsigned saltos = 0U, fuera_de_rango = 0U;
    uint16_t antes = 0U;
    uint8_t  capturando_antes = 0U;
    int primera = 1;

    printf("\nEL CICLO DE WSPR\n\n");

    memset(silencio, 0, sizeof silencio);

    bien(wspr_modo_start() == 1U,
         "el modo arranca (el espectrograma cabe en la RAM prestada)");
    if (fallos) {
        printf("\n  sin memoria no hay nada mas que medir\n\n");
        return 1;
    }
    bien(wspr_modo_activo() == 1U, "y se queda activo");

    /*
     * 5 min 55 s de reloj, de 12:00:00 a 12:05:54. La ventana esta elegida
     * para que entren DOS ciclos COMPLETOS y ni un trozo del tercero:
     *
     *   12:01:59  abre (emision del minuto 2)
     *   12:03:54  cierra   (la captura dura 336 medios simbolos = 114,7 s)
     *   12:03:59  abre (emision del minuto 4)
     *   12:05:54  cierra
     *   12:05:59  abriria el tercero, ya fuera
     *
     * Asi "dos aperturas y dos cierres" es un numero exacto y no un "mas o
     * menos". Y el reloj arranca en un minuto PAR a proposito: si empezara
     * en impar, la primera apertura seria inmediata y el banco no
     * distinguiria "abre en el minuto que toca" de "abre en cuanto puede".
     */
    for (s = 0U; s < 355U; s++) {
        unsigned k;

        /* Un segundo de audio a 12 kHz, en bloques como los del DMA. */
        for (k = 0U; k < 10U; k++) { wspr_modo_mete(silencio, 1200U); }

        wspr_modo_poll();

        {
            uint8_t  cap = wspr_modo_capturando();
            uint16_t f   = wspr_modo_espera_s();

            if (cap && !capturando_antes) {
                aperturas++;
                if ((s_ahora.minute & 1U) != 0U) { abrio_en_minuto_impar++; }
                if (s_ahora.second == 59U)       { abrio_en_segundo_59++; }
            }
            if (!cap && capturando_antes) { cierres++; }

            /* La cuenta atras solo tiene sentido mientras NO captura. */
            if (!cap) {
                if (f > 119U) { fuera_de_rango++; }
                if (!primera && !capturando_antes && f != 0U
                    && f + 1U != antes) {
                    saltos++;
                }
                antes = f;
                primera = 0;
            } else {
                primera = 1;
            }
            capturando_antes = cap;
        }

        avanza_un_segundo();
    }

    printf("\n  en 5 min 55 s de reloj:\n");
    printf("    %u aperturas, %u cierres\n", aperturas, cierres);

    bien(aperturas == 2U, "abre dos veces (una por cada emision de minuto par)");
    bien(cierres   == 2U, "y cierra las dos");
    bien(abrio_en_minuto_impar == aperturas,
         "siempre en un minuto IMPAR (el de antes del par)");
    bien(abrio_en_segundo_59 == aperturas,
         "y en el segundo 59 (el adelanto de WSPR_RX_ANTES_MS)");
    bien(saltos == 0U, "la cuenta atras baja de uno en uno, sin saltos");
    bien(fuera_de_rango == 0U, "y nunca pasa de 119 s");
    bien(wspr_modo_lineas() == 0U,
         "con silencio no se inventa ni un indicativo");
    bien(wspr_modo_buenas() == 0U, "ni cuenta ninguna como buena");

    /* Y que soltar la memoria no deja el modo a medias. */
    wspr_modo_stop();
    bien(wspr_modo_activo() == 0U, "al salir del modo se suelta todo");
    bien(wspr_modo_start() == 1U, "y se puede volver a entrar");
    wspr_modo_stop();

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
