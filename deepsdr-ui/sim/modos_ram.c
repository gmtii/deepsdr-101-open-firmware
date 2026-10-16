/*
 * QUE LOS MODOS QUE SE REPARTEN LA MEMORIA DE LA CASCADA ARRANCAN DE VERDAD.
 *
 * POR QUE EXISTE (28/09/2026). Porque este fallo ya se ha cometido DOS
 * VECES, en el mismo sitio, con un dia de diferencia:
 *
 *   - FT8 y HFDL: al pasar de HFDL a FT8, ft8_modo_start() cogia el
 *     prestamo y hfdl_modo_stop(), que corria DESPUES, lo soltaba. A
 *     partir de ahi waterfall_prestada() decia "libre" con FT8 vivo
 *     dentro, y la cascada volvia a pintar como pixeles los bytes del
 *     decodificador.
 *
 *   - AIS, tres horas mas tarde: su tabla paso a vivir tambien en esa
 *     memoria y la primera version lo dejaba fuera del grupo, parando AIS
 *     despues de arrancar FT8. El mismo fallo, con un modo mas.
 *
 * Y el sintoma NO es un error: es que el modo se pone, la pantalla sale, y
 * no decodifica nada. O peor, que la cascada pinta basura. Desde fuera se
 * ve como "esta frecuencia no entra".
 *
 * Aqui se prueba lo unico que de verdad importa: que cada modo ARRANCA
 * cuando le toca, que SUELTA al salir, y que se NIEGA si otro tiene la
 * memoria cogida. Todas las parejas, en los dos sentidos.
 *
 * FT8 y HFDL no se enlazan -arrastran medio firmware- sino que se fingen
 * con waterfall_presta(), que es exactamente lo que hacen ellos.
 */
#include "wspr_modo.h"
#include "ais_modo.h"
#include "ale_modo.h"
#include "waterfall.h"
#include <stdio.h>
#include <string.h>

volatile uint32_t g_msticks;

/* El reloj, que WSPR necesita. Fijo: aqui no se mide nada del ciclo. */
#include "rtc_hw.h"
void rtc_hw_get(rtc_hw_datetime_t *out)
{
    out->year = 2026; out->month = 9; out->day = 28;
    out->hour = 8; out->minute = 14; out->second = 0;
}

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

static int fallos;

static void bien(int ok, const char *que)
{
    printf("    %-56s %s\n", que, ok ? "bien" : "MAL");
    if (!ok) { fallos++; }
}

typedef struct {
    const char *nombre;
    uint8_t (*arranca)(void);
    void    (*para)(void);
    uint8_t (*activo)(void);
} modo_t;

static const modo_t k_modos[] = {
    { "WSPR", wspr_modo_start, wspr_modo_stop, wspr_modo_activo },
    { "AIS",  ais_modo_start,  ais_modo_stop,  ais_modo_activo  },
    { "ALE",  ale_modo_start,  ale_modo_stop,  ale_modo_activo  },
};
#define N (sizeof k_modos / sizeof k_modos[0])

int main(void)
{
    unsigned i, j;
    char t[96];

    printf("\nEL PRESTAMO DE LA MEMORIA DE LA CASCADA\n\n");

    /* --- 1. cada uno, solo --- */
    for (i = 0; i < N; i++) {
        snprintf(t, sizeof t, "%s arranca con la memoria libre", k_modos[i].nombre);
        bien(k_modos[i].arranca() == 1U && k_modos[i].activo() == 1U, t);

        snprintf(t, sizeof t, "%s deja la memoria prestada mientras corre",
                 k_modos[i].nombre);
        bien(waterfall_prestada() == 1U, t);

        k_modos[i].para();
        snprintf(t, sizeof t, "%s la suelta al salir", k_modos[i].nombre);
        bien(k_modos[i].activo() == 0U && waterfall_prestada() == 0U, t);
    }

    /* --- 2. con otro dentro (FT8 o HFDL fingidos) --- */
    printf("\n  con la memoria ya cogida por otro (FT8 o HFDL):\n");
    for (i = 0; i < N; i++) {
        waterfall_presta(1U);
        snprintf(t, sizeof t, "%s se NIEGA a arrancar", k_modos[i].nombre);
        bien(k_modos[i].arranca() == 0U && k_modos[i].activo() == 0U, t);
        /*
         * Y no se la queda al negarse: si lo hiciera, el modo que si la
         * tenia se quedaria escribiendo en memoria que ya no es suya.
         */
        snprintf(t, sizeof t, "%s la deja como estaba al negarse",
                 k_modos[i].nombre);
        bien(waterfall_prestada() == 1U, t);
        waterfall_presta(0U);
    }

    /* --- 3. todas las parejas, en los dos sentidos --- */
    printf("\n  cambiando de un modo a otro (parar los dos, arrancar uno):\n");
    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            if (i == j) { continue; }

            bien(k_modos[i].arranca() == 1U, "  (arranca el primero)");

            /*
             * EL ORDEN QUE USA main.c: se paran TODOS y despues se arranca
             * uno. Probarlo al reves -arrancar el nuevo y parar el viejo
             * despues- es lo que fallaba, y aqui se comprueba tambien, mas
             * abajo.
             */
            k_modos[i].para();
            snprintf(t, sizeof t, "%s -> %s: el segundo arranca",
                     k_modos[i].nombre, k_modos[j].nombre);
            bien(k_modos[j].arranca() == 1U && k_modos[j].activo() == 1U, t);
            k_modos[j].para();
            bien(waterfall_prestada() == 0U, "  (y queda libre)");
        }
    }

    /* --- 4. el orden MALO, el que ya fallo dos veces --- */
    printf("\n  y el orden equivocado, para que se vea lo que hacia:\n");
    {
        (void)k_modos[0].arranca();          /* WSPR dentro */
        (void)k_modos[1].arranca();          /* AIS intenta entrar: se niega */
        k_modos[0].para();                   /* y ahora se para el primero */
        /*
         * Aqui esta la trampa: AIS no arranco, WSPR se paro, y la memoria
         * queda libre - o sea que el segundo modo se queda SIN arrancar y
         * con la pantalla puesta. Es exactamente el sintoma que se ve en la
         * radio: el modo entra y no decodifica nunca.
         */
        bien(k_modos[1].activo() == 0U && waterfall_prestada() == 0U,
             "arrancar antes de parar deja el modo puesto pero muerto");
        k_modos[1].para();
    }

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
