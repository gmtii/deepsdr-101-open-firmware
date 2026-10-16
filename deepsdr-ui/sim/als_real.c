/*
 * ALS162 DE VERDAD, POR LA MISMA PUERTA QUE EL AUDIO DE LA RADIO.
 * =============================================================
 *
 * Pedido por el usuario, 10/10/2026.
 *
 * sim/alstest.c mide feed_fase() contra una señal que fabrica EL PROPIO
 * BANCO. Esto la mide contra la de verdad, y ademas contra una VERDAD
 * CONOCIDA: el fichero del WebSDR se llama
 *
 *     websdr_recording_start_2026-10-10T02_18_39Z_161.1kHz.wav
 *
 * o sea que empieza a las 02:18:39 UTC del sabado 10 de octubre de 2026.
 * Francia estaba en CEST (UTC+2), asi que lo que la radio saque tiene que
 * ser las 04:2x del 10/10/26 y sabado. Si sale eso, no es que "parezca que
 * funciona": es que coincide con un reloj que no es el nuestro.
 *
 * DE DONDE SALEN LOS FICHEROS
 * ---------------------------
 * Los dos son la FASE de la portadora, desenrollada y sin rampa, a 1.500
 * muestras/s - que es exactamente lo que main.c le da a dcf77_feed() en
 * esta emisora (cuatro por bloque; ver el comentario del desenrollado en
 * dcf77.c)-. Se sacan con tools/als_fase.py, que mide la portadora por
 * interpolacion sobre la FFT, mezcla a banda base, filtra a 40 Hz, coge el
 * angulo, lo desenrolla y le quita la rampa residual.
 *
 *   als162_websdr_fase1500.f32   168,3 s, portadora 869,2 Hz, deriva
 *                                residual 0,0001 Hz, amplitud con un 1,0%
 *                                de variacion: una grabacion limpia.
 *   als162_tdf_fase1500.f32       20,3 s de mp3 a 15 kbit/s: la amplitud
 *                                varia un 10,6% -eso es el codec- y no hay
 *                                minuto entero. Se queda porque es el caso
 *                                de "poca señal y poco tiempo", donde lo
 *                                que hay que comprobar es que NO se invente
 *                                una hora.
 *
 * LO QUE ESTO ENCONTRO, QUE ERA EL FALLO ENTERO DEL MODO
 * ------------------------------------------------------
 * La forma del elemento era la buena y el reparto de los bits tambien. Lo
 * que faltaba estaba en la norma y no en el codigo:
 *
 *   "Additional non-timing data is sent by phase modulation during the
 *    rest of each second."
 *
 * Medido aqui: **6,24 elementos por segundo**, de los cuales solo 1,05/s
 * son horarios. Los otros cinco son ese canal de datos, con la MISMA forma
 * y la MISMA amplitud (1,904 rad de pico a pico contra 1,809). feed_fase()
 * los contaba todos y, con mas de dos en un segundo, declaraba ruido: no
 * completaba una trama jamas.
 *
 * Y lo que los separa NO es el silencio de delante por si solo -con 60 ms
 * exigidos pasan el 78% de los horarios y tambien el 17% de los de datos-
 * sino LA REJILLA DE 1 SEGUNDO: de 337 candidatos, los 165 buenos caen a
 * menos de 50 ms de una rejilla de 1,000 s, con tres huecos de 2,001 s
 * separados exactamente 60,000 s que son los segundos 59. De ahi el
 * enganche que lleva ahora feed_fase().
 */
#include "dcf77.h"
#include <stdio.h>
#include <stdlib.h>

#define FS       1500.0f
#define LIMPIA   "sim/muestras/als162_websdr_fase1500.f32"
#define CORTA    "sim/muestras/als162_tdf_fase1500.f32"

static int fallos = 0;
static void comprueba(const char *q, int ok)
{
    printf("    %-54s %s\n", q, ok ? "OK" : "NO");
    if (!ok) { fallos++; }
}

static float s_ph[400000];

static size_t carga(const char *ruta)
{
    FILE *f = fopen(ruta, "rb");
    size_t n;

    if (f == NULL) { printf("  no encuentro %s\n", ruta); return 0U; }
    n = fread(s_ph, sizeof s_ph[0], sizeof s_ph / sizeof s_ph[0], f);
    fclose(f);
    return n;
}

static void mete(size_t n, dcf_info_t *out)
{
    size_t i;

    dcf77_start(HORA_ALS162, FS);
    for (i = 0U; i < n; i++) { dcf77_feed(s_ph[i]); }
    dcf77_info(out);
    dcf77_stop();
}

int main(void)
{
    dcf_info_t inf;
    size_t n;
    unsigned elem, i;
    uint8_t arriba;

    printf("\nALS162 DE VERDAD (TDF 162 kHz)\n");

    /* --------------------------------------------------------------
     * 1. CUANTOS ELEMENTOS TRAE. Se cuenta aqui, con un umbral propio:
     *    es la medida que destapo el fallo y no se le puede preguntar a
     *    quien estaba bajo sospecha.
     * -------------------------------------------------------------- */
    n = carga(LIMPIA);
    if (n == 0U) { return 2; }
    printf("  %zu muestras a %.0f/s = %.1f s (WebSDR, sin comprimir)\n\n",
           n, (double)FS, n / (double)FS);

    printf("  1. CUANTOS ELEMENTOS HAY EN LA SEÑAL\n");
    elem = 0U; arriba = 0U;
    for (i = 1U; i < (unsigned)n; i++) {
        if (!arriba && s_ph[i] > 0.5f)       { arriba = 1U; elem++; }
        else if (arriba && s_ph[i] < 0.2f)   { arriba = 0U; }
    }
    {
        double por_s = elem / (n / (double)FS);
        printf("    %u elementos  ->  %.2f por segundo\n", elem, por_s);
        printf("    horarios son ~1,05/s: el resto es el canal de datos\n");
        comprueba("trae MUCHOS mas de los dos por segundo del horario",
                  por_s > 4.0);
    }

    /* --------------------------------------------------------------
     * 2. Y AHORA POR LA PUERTA DE LA RADIO, CONTRA UNA VERDAD CONOCIDA.
     * -------------------------------------------------------------- */
    printf("\n  2. POR dcf77_feed(), COMO EN LA RADIO\n");
    mete(n, &inf);
    printf("    estado %d, ciclo %u ms, raras %u, tramas buenas %u, malas %u\n",
           (int)inf.estado, inf.ms_ciclo, inf.marcas_raras,
           inf.tramas_ok, inf.tramas_malas);
    printf("    -> %02u:%02u  %02u/%02u/%02u  dia de la semana %u  verano %u\n",
           inf.hora, inf.minuto, inf.dia, inf.mes, inf.anno,
           inf.dia_semana, inf.verano);

    comprueba("decodifica y confirma", inf.estado == DCF_LISTO);
    comprueba("el ciclo es de un segundo",
              inf.ms_ciclo >= 980U && inf.ms_ciclo <= 1020U);
    /*
     * LA VERDAD CONOCIDA. El fichero empieza a las 02:18:39 UTC y dura
     * 168 s, asi que la ultima trama entera cae sobre las 02:21 UTC. En
     * CEST son las 04:21. Esto no se compara contra lo que la radio dijo
     * la vez anterior: se compara contra el nombre del fichero.
     */
    comprueba("la hora cuadra con el reloj del fichero (04:21 CEST)",
              inf.hora == 4U && inf.minuto == 21U);
    comprueba("la fecha cuadra (10/10/26)",
              inf.dia == 10U && inf.mes == 10U && inf.anno == 26U);
    comprueba("y era sabado", inf.dia_semana == 6U);
    comprueba("y en horario de verano", inf.verano == 1U);

    /* --------------------------------------------------------------
     * 3. LA CORTA Y DAÑADA: lo que NO puede hacer.
     * -------------------------------------------------------------- */
    printf("\n  3. VEINTE SEGUNDOS DE mp3 A 15 kbit/s\n");
    n = carga(CORTA);
    if (n != 0U) {
        mete(n, &inf);
        printf("    estado %d, tramas buenas %u, malas %u\n",
               (int)inf.estado, inf.tramas_ok, inf.tramas_malas);
        /*
         * Veinte segundos no traen un minuto entero, asi que aqui la unica
         * respuesta correcta es NO DAR NINGUNA. Que el modo "casi" funcione
         * con material insuficiente seria peor que no funcionar: pondria el
         * reloj de la radio con una hora inventada.
         */
        comprueba("NO se inventa una hora", inf.estado != DCF_LISTO);
        comprueba("y tampoco da una trama por buena", inf.tramas_ok == 0U);
    }

    printf("\n  %s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
