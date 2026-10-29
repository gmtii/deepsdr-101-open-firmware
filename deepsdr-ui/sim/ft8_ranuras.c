/*
 * ======================================================================
 * LA MAQUINA DE RANURAS DE FT8 - 28/10/2026
 * ======================================================================
 *
 * *** POR QUE EXISTE: el usuario oia la ranura entera de pitidos de FT8 y
 * no salia nada en la lista, "y eso podria pasar una vez pero no tan a
 * menudo". ***
 *
 * Y NO ESTABA EN NINGUN BANCO. De FT8 habia seis -la lista, el SNR, las
 * tablas de la FFT, el panel, el desvio y sim/ft8_banda.c, que mete audio
 * por un lado y cuenta mensajes por el otro-, y ninguno tocaba EL RELOJ:
 * ft8_banda.c llena la ranura a mano con un bucle y llama al decodificador
 * cuando is_full() dice que si. O sea que toda la maquina de ranuras de
 * User/ft8_modo.c -el borde de ranura, el arranque diferido, el autoajuste
 * y el guardia del desvio- no la probaba nadie.
 *
 * Es la misma familia de fallo que el barrido de memorias: las piezas
 * tenian su banco cada una y el fallo vivia en la costura.
 *
 * ESTO CORRE ft8_modo.c DE VERDAD, con un reloj y un g_msticks de mentira
 * que se pueden mover a voluntad, y mete audio como lo mete la radio.
 *
 * EL FALLO QUE DESTAPO, que es la seccion 2:
 *
 *   LA RANURA SE TIRABA ENTERA SI NO LLEGABA A 93 BLOQUES. Un bloque son
 *   160 ms y 93 son 14,880 s de los 15 que dura la ranura: 120 ms de
 *   margen, MENOS DE UN BLOQUE. El colchon del remuestreador son 320 ms,
 *   asi que un paron del bucle principal mas largo que eso se come 160 ms
 *   de audio, la ranura se queda en 92 bloques de 93 y se perdia el ciclo
 *   entero sin que nada lo dijera. Comprobado: con el codigo de antes, la
 *   seccion 2 saca CERO lineas.
 *
 * Y LA SECCION 3 ESTA AQUI POR LO CONTRARIO: porque yo crei encontrar un
 * segundo fallo en el autoajuste de ranura -"mueve el arranque hasta 999
 * ms despues del borde, asi que la ventana son 15.000 menos el ajuste"- y
 * ESO ES FALSO. El arranque se difiere igual en CADA borde, o sea que la
 * ventana sigue siendo de 15 s enteros, solo que corrida. Lo enseño este
 * mismo banco al correrlo: el ajuste acaba en 640 ms y las ranuras siguen
 * saliendo. La seccion se queda para que nadie -yo el primero- vuelva a
 * razonarlo mal sin que algo lo contradiga.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

#include "ft8_modo.h"
#include "ft8_decimator.h"
#include "ft8_waterfall_adapter.h"
#include "ft8_fft.h"
#include "ft8_decoder.h"
#include "ft8_shared_ram.h"
#include "rtc_hw.h"
#include "ft8_gen.h"

/* ------------------------------------------------- la radio de mentira */

volatile uint32_t g_msticks;

/* El reloj: segundos desde una medianoche cualquiera. Lo mueve el banco. */
static uint32_t g_reloj_ms;

void rtc_hw_get(rtc_hw_datetime_t *o)
{
    uint32_t s = g_reloj_ms / 1000UL;

    if (!o) { return; }
    memset(o, 0, sizeof *o);
    o->second = (uint8_t)(s % 60UL);
    o->minute = (uint8_t)((s / 60UL) % 60UL);
    o->hour   = (uint8_t)((s / 3600UL) % 24UL);
    o->year = 2026; o->month = 10; o->day = 28;
}
bool rtc_hw_has_ever_synced(void) { return true; }
const char *dxcc_pais(const char *ind) { (void)ind; return 0; }

static int fallos = 0;

static void pasa(const char *que, int bien)
{
    printf("    %-56s %s\n", que, bien ? "ok " : "*** MAL ***");
    if (!bien) { fallos++; }
}

/* ------------------------------------------------ correr ranuras enteras */

/*
 * Mete en la radio los `ms` milisegundos siguientes de `g_audio`, llamando
 * a ft8_modo_come() en trozos como hace la interrupcion y a
 * ft8_modo_tick() cada `tick_ms` como hace el bucle principal.
 *
 * `salta_ms` se traga ese rato de audio SIN dar ticks: es un bucle
 * principal atascado, que es como se pierde un subbloque de verdad.
 */
#define TICK_MS   20U      /* lo que tarda una vuelta del bucle principal */

static int    g_pos;       /* por donde va g_audio[] */
static int    g_lineas;    /* mensajes que han salido */

static void corre_ms(uint32_t ms, uint32_t salta_ms)
{
    uint32_t hecho = 0U;

    while (hecho < ms) {
        uint32_t paso = TICK_MS;
        int n, j;
        float trozo[(FS * TICK_MS) / 1000U];

        if (paso > (ms - hecho)) { paso = ms - hecho; }
        n = (int)((FS * paso) / 1000U);
        for (j = 0; j < n; j++) {
            trozo[j] = (g_pos < RANURA_N) ? g_audio[g_pos] : 0.0f;
            g_pos++;
        }
        ft8_modo_come(trozo, (uint16_t)n);

        g_msticks  += paso;
        g_reloj_ms += paso;
        hecho      += paso;

        if (salta_ms > 0U) {
            /* El bucle esta atascado: entra audio pero nadie lo vacia. */
            if (salta_ms >= paso) { salta_ms -= paso; continue; }
            salta_ms = 0U;
        }
        ft8_modo_tick();
    }
}

/* Cuenta las lineas nuevas que hay desde la ultima vez. */
static int lineas_nuevas(void)
{
    int n = 0, i;

    for (i = 0; i < FT8_MODO_LINEAS; i++) {
        const char *l = ft8_modo_linea((uint8_t)i);

        if (l != 0 && l[0] != '\0') { n++; }
    }
    {
        int d = n - g_lineas;

        g_lineas = n;
        return d;
    }
}

/*
 * Una ranura: se fabrica el audio con `cuantas` emisiones repartidas, se
 * mete en la radio los 15 segundos y se devuelve cuantas lineas salieron.
 *
 * `dt_s` las corre todas el mismo rato respecto al borde de ranura, que es
 * lo que el autoajuste persigue.
 *
 * `salta_ms` atasca el bucle principal al principio de la ranura.
 */
static int una_ranura(int cuantas, float dt_s, uint32_t salta_ms)
{
    static const char *k_txt[] = {
        "CQ EA1ABC IN52", "CQ EA2ABC IN83", "CQ EA3ABC JN01",
        "CQ EA4ABC IN80", "CQ EA5ABC IM98", "CQ EA7ABC IM67",
    };
    int i;

    ranura_vacia(12345U + (uint32_t)g_pos);
    for (i = 0; i < cuantas && i < (int)(sizeof k_txt / sizeof k_txt[0]); i++) {
        uint8_t tonos[FT8_NN];

        if (!ft8_tonos(k_txt[i], tonos)) { continue; }
        mete_emision(400.0f + 120.0f * (float)i, dt_s,
                     amplitud_para(6.0f), tonos);
    }
    g_pos = 0;
    corre_ms(15000U, salta_ms);
    return lineas_nuevas();
}

/* Pone el reloj justo en un borde de ranura y arranca FT8 limpio. */
static void arranca(void)
{
    g_msticks  = 1000U;
    g_reloj_ms = 0U;          /* segundo 0 = borde de ranura */
    g_pos = 0;
    g_lineas = 0;
    ft8_modo_start((float)FS);
    ft8_modo_cuadra();        /* desfase y ajuste a cero, sin fracciones */
    g_lineas = 0;
}

int main(void)
{
    char t[96];

    printf("\nLA MAQUINA DE RANURAS DE FT8\n\n");

    /* --------------------------------------------------------------
     * 1. LO BASICO: TRES RANURAS SEGUIDAS, TODAS DECODIFICAN
     * -------------------------------------------------------------- */
    printf("  1. TRES RANURAS SEGUIDAS CON EL RELOJ EN HORA\n");
    {
        int a, b, c;

        arranca();
        a = una_ranura(3, 0.0f, 0U);
        b = una_ranura(3, 0.0f, 0U);
        c = una_ranura(3, 0.0f, 0U);
        printf("      lineas por ranura: %d, %d, %d\n", a, b, c);
        pasa("las tres sacan mensajes", a > 0 && b > 0 && c > 0);
        snprintf(t, sizeof t, "y ninguna se queda corta (%u)",
                 ft8_modo_cortas());
        pasa(t, ft8_modo_cortas() == 0U);
    }

    /* --------------------------------------------------------------
     * 2. *** EL BUCLE SE ATASCA Y SE PIERDE UN SUBBLOQUE ***
     * --------------------------------------------------------------
     * El colchon del remuestreador son dos buffers, 320 ms. Un paron mas
     * largo que eso se come 160 ms de audio, la ranura se queda en 92
     * bloques de 93 y ANTES se tiraba entera.
     * -------------------------------------------------------------- */
    printf("\n  2. UN PARON DEL BUCLE SE COME UN SUBBLOQUE\n");
    {
        int n;

        arranca();
        (void)una_ranura(3, 0.0f, 0U);        /* una buena primero */
        n = una_ranura(3, 0.0f, 600U);        /* 600 ms de atasco */
        printf("      lineas de la ranura atascada: %d  (cortas: %u,"
               " subbloques perdidos: %u)\n",
               n, ft8_modo_cortas(), ft8_modo_perdidos());
        pasa("la ranura corta TAMBIEN decodifica", n > 0);
    }

    /* --------------------------------------------------------------
     * 3. EL AUTOAJUSTE DE RANURA NO DEJA FT8 MUDO
     * --------------------------------------------------------------
     * Todas las emisiones llegan medio segundo tarde, que es lo que hace
     * saltar a cuadra_sola(). El ajuste sube a varias decimas y las
     * ranuras tienen que seguir saliendo.
     *
     * *** Y ESTA SECCION EXISTE PORQUE ME EQUIVOQUE AQUI. *** Razone que
     * un ajuste de N ms dejaba la ventana en 15.000-N y que por encima de
     * 120 ms la ranura no llegaba nunca a 93 bloques. Es falso: el
     * arranque se difiere los mismos N ms en CADA borde, asi que la
     * ventana sigue midiendo 15 s enteros. Lo unico que se pierde es la
     * ranura que esta en vuelo el dia que el ajuste cambia, y eso es lo
     * que se ve abajo: un cero en la segunda.
     * -------------------------------------------------------------- */
    printf("\n  3. CON LAS EMISIONES MEDIO SEGUNDO TARDE\n");
    {
        int r[6];
        int i, con = 0;

        arranca();
        for (i = 0; i < 6; i++) {
            r[i] = una_ranura(4, 0.5f, 0U);
            if (r[i] > 0) { con++; }
        }
        printf("      lineas por ranura: %d %d %d %d %d %d\n",
               r[0], r[1], r[2], r[3], r[4], r[5]);
        printf("      ajuste que dejo el autoajuste: %u ms,  cortas: %u\n",
               ft8_modo_ajuste_ms(), ft8_modo_cortas());
        snprintf(t, sizeof t, "sigue decodificando despues de cuadrarse"
                 " (%d de 6)", con);
        pasa(t, con >= 5);
        pasa("y solo se pierde la ranura en la que cambio el ajuste",
             r[0] > 0 && r[2] > 0 && r[3] > 0);
        /* Y que el autoajuste HA actuado: si no, esto no prueba nada. */
        snprintf(t, sizeof t, "   (y el autoajuste movio el arranque: %u ms)",
                 ft8_modo_ajuste_ms());
        pasa(t, ft8_modo_ajuste_ms() > 120U);
    }

    /* --------------------------------------------------------------
     * 4. LA BANDA LLENA NO DESBORDA EL DESVIO
     * --------------------------------------------------------------
     * Mientras se decodifica, la captura de la ranura SIGUIENTE se va a
     * un buffer de al lado que solo tiene FT8_ADAPTER_LADO_BLOQUES = 24
     * bloques, o sea **3,84 segundos**. Pasado ese plazo se pierde audio
     * de la ranura nueva, y eso lo cuenta ft8_waterfall_desbordes().
     *
     * CUANTO TARDA DE RELOJ NO SE PUEDE MEDIR AQUI, y conviene decirlo en
     * vez de inventar un numero: la decodificacion avanza a trozos de
     * FT8_PASO_MS por vuelta del bucle principal, asi que el reloj que
     * consume depende de lo que tarde una vuelta EN LA RADIO -unos 69 ms
     * repintando el espectro- y de lo que tarde un candidato en un
     * Cortex-M4, que este banco no corre. Quien lo mide es la propia
     * radio: ft8_modo_ms() sale en la chapa, y el desborde tambien.
     *
     * Lo que SI se puede medir aqui es el otro factor, que es el que
     * dispara el trabajo: CUANTOS CANDIDATOS salen con la banda llena.
     * -------------------------------------------------------------- */
    printf("\n  4. LA BANDA LLENA: CANDIDATOS Y DESVIO\n");
    {
        int n;

        arranca();
        n = una_ranura(6, 0.0f, 0U);
        printf("      %d mensajes de 6, %d candidatos (tope %d)\n",
               n, ft8_modo_candidatos(), FT8_DECODER_MAX_CANDIDATES);
        pasa("salen las seis", n == 6);
        snprintf(t, sizeof t, "sin desbordar el desvio (%u)",
                 ft8_waterfall_desbordes());
        pasa(t, ft8_waterfall_desbordes() == 0U);
    }

    /* --------------------------------------------------------------
     * 5. Y SIN HORA NO SE INVENTA NADA
     * -------------------------------------------------------------- */
    printf("\n  5. LA RANURA VACIA NO SACA MENSAJES\n");
    {
        int n;

        arranca();
        n = una_ranura(0, 0.0f, 0U);
        printf("      lineas con solo ruido: %d\n", n);
        pasa("no se inventa nada", n == 0);
    }

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
