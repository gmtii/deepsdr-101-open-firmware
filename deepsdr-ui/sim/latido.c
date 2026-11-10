/*
 * QUE NADIE SE QUEDE SIN MIRAR EL TACTIL.
 *
 * POR QUE EXISTE - 08/11/2026.
 *
 * pump.h nacio el 29/10/2026 porque el usuario dijo que en STANAG y en
 * IDENT el tactil respondia lentamente y que insistiendo acababa entrando.
 * La causa estaba medida: un toque dura unos 150 ms y hacen falta DOS
 * lecturas con el dedo puesto, asi que entre dos miradas no pueden pasar
 * mas de 75 ms o el toque no existe. Los decodificadores largos corren
 * desde el bucle principal, que es quien mira el tactil, y mientras corren
 * nadie mira nada.
 *
 * La cura fue un gancho: pump(). stanag_det.c, s4415.c, stanag_rx.c e
 * ident.c lo llaman desde dentro de su trabajo largo y main.c le cuelga
 * touch_latido().
 *
 * PERO NO HABIA NADA QUE LO VIGILARA. Es un `pump();` suelto en medio de
 * una funcion: se borra en una reorganizacion y no falla nada -compila,
 * enlaza, decodifica igual de bien- y lo unico que se nota es que meses
 * despues la pantalla vuelve a ir dura en un modo. Lo mismo que ya ha
 * pasado en este arbol con otras cosas que se escribieron una vez y
 * dejaron de ser verdad.
 *
 * Asi que aqui se cuelga un contador del gancho DE VERDAD -el mismo
 * pump_pon() que usa main.c- se pone a los dos trabajos mas largos que
 * tiene la radio a correr, y se exige que latan.
 *
 * LOS DOS QUE SE MIDEN, Y POR QUE ESOS:
 *
 *   CERRAR UNA RANURA DE WSPR con la banda vacia, que acaba dentro de
 *   wspr_fano_ex(). Es el peor de todos y con diferencia.
 *   Medido en deepsdr-ui/m4/m4_digi2.c sobre el Cortex-M4: cerrar una
 *   ranura sin emision son 417.628.240 instrucciones de un tiron, 2,09 s
 *   -y en la radio x1,7-. Con emision son 19.876.080. O sea que el caso
 *   caro es el NORMAL: lo que hace la radio cada dos minutos mientras
 *   nadie transmite.
 *
 *   stanag_det_paso(), que es el que empezo todo esto y el que mas veces
 *   corre: once o doce ventanas por segundo, siempre que el IDENT o el
 *   STANAG en Auto esten puestos.
 *
 * LO QUE NO MIDE: cuanto tarda de verdad entre dos latidos. Eso no se
 * puede medir en el anfitrion, donde una ranura de WSPR se decodifica en
 * milisegundos. Lo que mide es que el gancho SE LLAMA, y cuantas veces,
 * que es lo que se borra sin querer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "pump.h"
#include "wspr_fano.h"
#include "wspr_rx.h"
#include "stanag_det.h"

static unsigned long g_latidos;
static void cuenta(void) { g_latidos++; }

static int g_fallos;

static void bien(const char *que, int ok, const char *dato)
{
    printf("    %-52s %s", que, ok ? "bien" : "MAL ");
    if (dato) { printf("  %s", dato); }
    printf("\n");
    if (!ok) { g_fallos++; }
}

/* --- WSPR: la peor espera que tiene la radio ------------------------- */

static uint32_t s_rnd = 7654321u;
static int8_t azar8(void)
{
    s_rnd = s_rnd * 1664525u + 1013904223u;
    return (int8_t)((int32_t)((s_rnd >> 16) & 0xFFu) - 128);
}

static uint8_t s_rxram[WSPR_RX_RAM + 4096U];

/*
 * UNA RANURA ENTERA DE RUIDO, COMO EN LA RADIO.
 *
 * No se le dan valores blandos al azar a wspr_fano_ex() y ya esta: con un
 * vector al azar la busqueda se rinde a los treinta y tantos mil pasos y
 * no toca el tope. Lo que agota los 200.000 es el ruido que sale DEL
 * ESPECTROGRAMA, que no es blanco -lleva dentro el patron de sincronismo
 * que el receptor ha intentado encajar- y es lo que pasa de verdad cada
 * dos minutos. Asi que se recorre el camino entero: meter, dar los pasos
 * del espectrograma y cerrar la ranura.
 */
static void llena_de_ruido(void)
{
    float trozo[600];
    unsigned long n;
    unsigned j = 0U;

    /* 162 simbolos a 1,4648 baudios son 110,6 s; a 12 kHz, 1,33 millones
     * de muestras. Se dan de sobra para que la ventana se cierre. */
    for (n = 0UL; n < 1400000UL; n++) {
        trozo[j++] = (float)azar8() * 40.0f;
        if (j == 600U) {
            wspr_rx_mete(trozo, 600U);
            (void)wspr_rx_pasos_pendientes();
            j = 0U;
        }
    }
    if (j > 0U) { wspr_rx_mete(trozo, (uint16_t)j); }
    (void)wspr_rx_pasos_pendientes();
}

static void el_wspr(void)
{
    wspr_msg_t m;
    int16_t hz = 0;
    uint8_t q = 0U;
    char dato[96];

    printf("\n  cerrar una ranura de WSPR con la banda vacia\n");

    wspr_rx_init(s_rxram, sizeof s_rxram);
    llena_de_ruido();

    g_latidos = 0UL;
    pump_pon(cuenta);
    memset(&m, 0, sizeof m);
    (void)wspr_rx_decodifica(&m, &hz, &q);
    pump_pon(0);

    sprintf(dato, "%lu latidos", g_latidos);
    /*
     * CUANTOS SE PIDEN, Y POR QUE TAN POCOS.
     *
     * En la radio el caso malo agota los 200.000 pasos y late 781 veces
     * -esta medido en m4_digi2.c-. Aqui, con ruido fabricado en el
     * anfitrion, la busqueda se rinde sobre los treinta y cuatro mil y
     * salen unos ciento treinta. No es que el banco mida mal: es que el
     * ruido de un generador no se parece del todo al que sale del
     * espectrograma despues de haber intentado encajarle el sincronismo,
     * y la busqueda se cansa antes.
     *
     * Lo que este banco vigila es QUE EL GANCHO SIGA AHI, que es lo que
     * se borra sin querer. Cincuenta latidos no los da nadie por
     * casualidad y los da cualquier version que llame a pump().
     */
    bien("late mientras decodifica", g_latidos >= 50UL, dato);
    bien("y con ruido no se inventa una baliza", q < 88U, dato);

    /* Y que el contador no cuente solo: sin gancho, ni un latido. */
    g_latidos = 0UL;
    memset(&m, 0, sizeof m);
    (void)wspr_rx_decodifica(&m, &hz, &q);
    sprintf(dato, "%lu latidos sin gancho", g_latidos);
    bien("sin gancho colgado no se llama a nadie", g_latidos == 0UL, dato);
}

/* --- Y el detector, que es el que mas veces corre -------------------- */

static float s_trabajo[STANAG_DET_FLOATS];

static void el_detector(void)
{
    stanag_det_t r;
    float bloque[32];
    unsigned long antes;
    unsigned v = 0U, i, k;
    char dato[96];

    printf("\n  stanag_det_paso(): once o doce ventanas por segundo\n");

    if (!stanag_det_init(s_trabajo, STANAG_DET_FLOATS)) {
        bien("el detector arranca", 0, 0);
        return;
    }

    pump_pon(cuenta);
    g_latidos = 0UL;
    antes = 0UL;

    /* Ruido hasta que se cierren tres ventanas. La primera no puede salir
     * hasta que el anillo se llena. */
    for (k = 0U; k < 4000U && v < 3U; k++) {
        for (i = 0U; i < 32U; i++) { bloque[i] = (float)azar8() * 0.002f; }
        stanag_det_mete(bloque, 32U);
        if (stanag_det_paso(&r)) {
            v++;
            sprintf(dato, "ventana %u: %lu latidos", v, g_latidos - antes);
            bien("cada ventana late al menos tres veces",
                 (g_latidos - antes) >= 3UL, dato);
            antes = g_latidos;
        }
    }
    pump_pon(0);

    sprintf(dato, "%u ventanas", v);
    bien("se han cerrado las tres ventanas que hacian falta", v == 3U, dato);
}

int main(void)
{
    printf("== El latido dentro del trabajo largo ==\n");

    el_wspr();
    el_detector();

    printf("\n");
    if (g_fallos) {
        printf("  Un `pump();` que falta no rompe nada: compila, enlaza y\n");
        printf("  decodifica igual. Lo unico que se nota es que meses\n");
        printf("  despues el tactil vuelve a ir duro en un modo.\n");
        printf("LATIDO: HAY FALLOS (%d)\n", g_fallos);
        return 1;
    }
    printf("TODO CORRECTO (0 fallos)\n");
    return 0;
}
