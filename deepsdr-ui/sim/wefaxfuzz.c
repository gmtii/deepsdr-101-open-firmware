/*
 * EL FAX APORREADO - 21/10/2026.
 *
 * POR QUE EXISTE
 * --------------
 * sim/wefaxtest.c le manda al decodificador señales BIEN FORMADAS: tono de
 * arranque, señal de fase, imagen, tono de fin. Cada caso prueba un camino
 * que alguien penso.
 *
 * Y las pasadas 6 a 11 de la revision de WEFAX encontraron esto, por orden:
 * una bandera que sobrevivia a una carta y le tiraba una linea a la
 * siguiente; un descarte que se quedaba armado hasta la carta de despues;
 * el detector de tonos entrando en imagen cargado y rebotando en 20 ms; el
 * seguidor de fase reiniciado a medias por el mando de velocidad. Las
 * cuatro son lo mismo: ESTADO QUE SOBREVIVE A UNA TRANSICION QUE NADIE
 * PROBO. Y las cuatro se encontraron leyendo, porque ningun banco mandaba
 * las transiciones en un orden que a nadie se le ocurriria.
 *
 * Esto manda justo eso. Ruido, trozos de tono, trozos de fase y trozos de
 * imagen en el orden que salga, con los botones -Sincroniza, forzar imagen,
 * el mando de velocidad- pulsados cuando salga, y el bucle principal
 * recogiendo renglones cuando salga. Despues para de aporrear y manda UNA
 * CARTA LIMPIA ENTERA.
 *
 * LO QUE COMPRUEBA, Y ES UNA SOLA COSA QUE IMPORTA
 * ------------------------------------------------
 * Que despues del aporreo la carta limpia ENTRA. O sea que no hay
 * combinacion de botones y de señal que deje el decodificador clavado:
 * siempre se recupera solo.
 *
 * Y de paso, en cada vuelta, que lo que asoma por wefax_info() sea posible:
 * un estado de los cuatro, una velocidad de las cuatro, y una cuenta de
 * lineas de fase que no pase de las que caben.
 *
 * NO COMPRUEBA la imagen. Con la señal aporreada no hay imagen correcta que
 * comparar; eso es trabajo de wefaxtest.c, que manda señales de verdad.
 *
 * Se corre tambien con los detectores de gcc -ver `make sanitiza`-, que es
 * donde esto vale el doble: miles de transiciones en orden raro es
 * exactamente lo que hace falta para que un detector encuentre algo.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "wefax.h"

#define FS       12000.0
#define BLOQUE   64

static int fallos = 0;
static void comprueba(int ok, const char *que)
{
    printf("  %-62s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/* Un generador propio y sembrado: un banco que no se puede repetir no
 * sirve para contar lo que paso. */
static unsigned long g_sem = 1UL;
static unsigned rnd(unsigned n)
{
    g_sem = g_sem * 1103515245UL + 12345UL;
    return (unsigned)((g_sem >> 16) & 0x7FFFU) % n;
}
static double rnd01(void) { return (double)rnd(10000U) / 10000.0; }

/* --- el transmisor, lo justo ------------------------------------------ */
typedef struct { double fase; double lpm; } tx_t;

static float emite(tx_t *t, double hz, double ruido)
{
    double y;
    t->fase += 2.0 * 3.14159265358979 * hz / FS;
    if (t->fase > 2.0 * 3.14159265358979) { t->fase -= 2.0 * 3.14159265358979; }
    y = sin(t->fase) + ruido * (rnd01() - 0.5) * 2.0;
    return (float)y;
}

static uint8_t g_una[WEFAX_ANCHO];
static long g_recogidas = 0;

/* Mete un bloque y recoge lo que el bucle principal decida recoger. */
static void bloque(const float *b, int cuantas_recoge)
{
    int k;
    wefax_process(b, (uint32_t)BLOQUE);
    for (k = 0; k < cuantas_recoge; k++) {
        if (!wefax_linea_en(g_una)) { break; }
        g_recogidas++;
    }
}

/* Lo que asoma por la API tiene que ser posible SIEMPRE. */
static int plausible(void)
{
    wefax_info_t in;
    unsigned i;
    int lpm_vale = 0;

    wefax_info(&in);
    if (in.estado > (uint8_t)WEFAX_IMAGEN) { return 0; }
    /* La lista se pregunta, no se copia: aqui estaba escrita a mano. */
    for (i = 0U; i < (unsigned)WEFAX_LPM_N; i++) {
        if (in.lpm == wefax_lpm_opcion((uint8_t)i)) { lpm_vale = 1; }
    }
    if (!lpm_vale) { return 0; }
    if (in.fase_n > 16U) { return 0; }
    return 1;
}

/* --- trozos de señal --------------------------------------------------- */
static void trozo_tono(tx_t *t, double hz_alterna, double seg, double ruido,
                       int recoge)
{
    long n = (long)(FS * seg), k, j = 0;
    float b[BLOQUE];
    for (k = 0; k < n; k++) {
        int blanco = (((long)((double)k * hz_alterna * 2.0 / FS)) & 1);
        b[j++] = emite(t, (double)wefax_hz_de_gris(blanco ? 255U : 0U), ruido);
        if (j == BLOQUE) { bloque(b, recoge); j = 0; }
    }
}

static void trozo_fase(tx_t *t, int lineas, double ruido, int recoge)
{
    double ml = FS * 60.0 / t->lpm;
    int i; long k, j = 0;
    float b[BLOQUE];
    for (i = 0; i < lineas; i++) {
        for (k = 0; k < (long)ml; k++) {
            int blanco = ((double)k / ml) < 0.05;
            b[j++] = emite(t, (double)wefax_hz_de_gris(blanco ? 255U : 0U), ruido);
            if (j == BLOQUE) { bloque(b, recoge); j = 0; }
        }
    }
}

static void trozo_imagen(tx_t *t, int lineas, double ruido, int recoge)
{
    double ml = FS * 60.0 / t->lpm;
    int i; long k, j = 0;
    float b[BLOQUE];
    for (i = 0; i < lineas; i++) {
        for (k = 0; k < (long)ml; k++) {
            int x = (int)((double)k / ml * (double)WEFAX_ANCHO);
            uint8_t g = (uint8_t)(40 + ((x * 180) / (int)WEFAX_ANCHO));
            b[j++] = emite(t, (double)wefax_hz_de_gris(g), ruido);
            if (j == BLOQUE) { bloque(b, recoge); j = 0; }
        }
    }
}

static void trozo_ruido(tx_t *t, double seg, int recoge)
{
    long n = (long)(FS * seg), k, j = 0;
    float b[BLOQUE];
    for (k = 0; k < n; k++) {
        b[j++] = emite(t, 1500.0 + 800.0 * rnd01(), 1.5);
        if (j == BLOQUE) { bloque(b, recoge); j = 0; }
    }
}

int main(void)
{
    int ronda;
    int plausible_mal = 0;
    int sin_recuperar = 0;
    long total_aporreo = 0;
    /*
     * CUARENTA RONDAS son un segundo y medio, y tres con los detectores
     * puestos. Es el numero para que esto entre en "make comprueba" sin que
     * nadie deje de correrlo por lento.
     *
     * Para mirar de verdad, se sube. Comprobado con SEISCIENTAS: ocho
     * millones y medio de renglones aporreados, cero fallos y nada que los
     * detectores vieran. Esa es la cifra que hay que batir el dia que se
     * sospeche de este decodificador otra vez.
     */
    const int RONDAS = 40;

    printf("el fax aporreado\n");
    printf("  %d rondas: ruido, tonos, fase e imagen en orden aleatorio,\n",
           RONDAS);
    printf("  con los botones pulsados al azar, y despues una carta limpia\n\n");

    for (ronda = 0; ronda < RONDAS; ronda++) {
        tx_t t;
        int paso, pasos = 6 + (int)rnd(8U);
        long antes;

        g_sem = 1000UL + (unsigned long)ronda * 7919UL;
        memset(&t, 0, sizeof t);
        t.lpm = 120.0;

        wefax_start((float)FS);
        wefax_set_lpm_idx((uint8_t)rnd(4U));

        /* --- el aporreo --- */
        for (paso = 0; paso < pasos; paso++) {
            int recoge = (int)rnd(4U);          /* el bucle recoge lo que le da la gana */
            double ruido = rnd01() * 1.2;

            switch (rnd(6U)) {
            case 0: trozo_ruido(&t, 0.3 + rnd01(), recoge); break;
            case 1: trozo_tono(&t, 300.0, 0.2 + rnd01() * 3.0, ruido, recoge); break;
            case 2: trozo_tono(&t, 450.0, 0.2 + rnd01() * 3.0, ruido, recoge); break;
            case 3: trozo_fase(&t, 1 + (int)rnd(25U), ruido, recoge); break;
            case 4: trozo_imagen(&t, 1 + (int)rnd(20U), ruido, recoge); break;
            default: trozo_ruido(&t, 0.1, recoge); break;
            }
            if (!plausible()) { plausible_mal++; }

            /* Y los botones, cuando salga. */
            switch (rnd(8U)) {
            case 0: wefax_sincroniza(); break;
            case 1: wefax_sincroniza(); wefax_fuerza_imagen(); break;
            case 2: wefax_set_lpm_idx((uint8_t)rnd(4U)); t.lpm = (double)wefax_get_lpm(); break;
            case 3: wefax_fuerza_imagen(); break;
            default: break;
            }
            if (!plausible()) { plausible_mal++; }
        }
        total_aporreo += g_recogidas;

        /*
         * --- Y AHORA UNA CARTA LIMPIA, QUE ES LO QUE SE COMPRUEBA ---
         *
         * Sin tocar un boton mas. Si alguna combinacion de las de arriba
         * hubiera dejado el decodificador clavado -una bandera armada, un
         * estado sin salida, el detector de tonos cargado- esta carta no
         * entraria.
         */
        {
            wefax_info_t in;
            long entran;

            t.lpm = 120.0;
            wefax_set_lpm(120U);
            antes = g_recogidas;
            trozo_tono(&t, 300.0, 5.0, 0.0, 4);

            /*
             * AQUI YA TIENE QUE HABER ENGANCHADO EL TONO DE ARRANQUE.
             *
             * Y esta comprobacion es la mitad que falta: contar renglones
             * sola no vale. Si el decodificador se quedara clavado en
             * imagen -que es el fallo que arreglo la pasada 5, la salida de
             * imagen por el tono de ARRANQUE- la carta de recuperacion
             * entraria igual, solo que pintando el tono y los treinta
             * segundos de fase como imagen. O sea que saldrian MAS
             * renglones, no menos, y un tope por abajo lo dejaria pasar.
             */
            wefax_info(&in);
            if (in.estado != (uint8_t)WEFAX_FASE) {
                if (sin_recuperar < 5) {
                    printf("    ronda %2d: tras el tono de arranque el estado es %u, "
                           "no la señal de fase\n", ronda, in.estado);
                }
                sin_recuperar++;
            }

            trozo_fase(&t, 60, 0.0, 4);
            trozo_imagen(&t, 40, 0.0, 4);
            entran = g_recogidas - antes;

            /*
             * Y los renglones, por arriba Y por abajo: ni 5 -no engancho-
             * ni 110 -se trago el tono y la fase como imagen-.
             *
             * *** Y LA BANDA ERA DE VEINTIUN RENGLONES - 21/10/2026. ***
             *
             * Ponia `entran < 30 || entran > 50` y «entrega unas 39».
             * Entrega 38, y las CUARENTA rondas entregan 38: despues del
             * aporreo la carta limpia es siempre la misma señal, asi que
             * este numero no tiene por que bailar. Una banda de veintiun
             * renglones alrededor de un numero que no se mueve no vigila
             * nada: lo que decide este numero es el tope de
             * tope_lineas_fase(), y moverlo a 40 s da 36 y a 32 s da 44.
             * Los dos pasaban.
             *
             * Y esos renglones no son cualquier cosa: son los de ARRIBA de
             * la carta, que es donde el DWD pone la hora y el nombre.
             *
             * Se admite uno arriba y uno abajo, por si alguna ronda futura
             * cambia el instante del enganche; dos ya es el efecto que
             * esto busca.
             */
            if (entran < 37 || entran > 39) {
                if (sin_recuperar < 5) {
                    printf("    ronda %2d: tras el aporreo entran %ld renglones "
                           "de los 40 de la carta\n", ronda, entran);
                }
                sin_recuperar++;
            }
        }
        wefax_stop();
    }

    printf("    %ld renglones durante el aporreo, %d rondas de recuperacion\n",
           total_aporreo, RONDAS);
    comprueba(plausible_mal == 0,
              "lo que asoma por wefax_info() es siempre posible");
    comprueba(sin_recuperar == 0,
              "y tras cualquier aporreo la carta limpia sigue entrando");

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}
