#ifndef JTTY_RX_H
#define JTTY_RX_H

#include <stdint.h>
#include "jtty_fec.h"
#include "jtty_msg.h"

/*
 * JTTY: DEL AUDIO A LOS ATOMOS - 28/09/2026.
 *
 * *** Por el dueño del proyecto: "al final no implementaste jtty?" ...
 * "atacalo ahora". ***
 *
 * jtty_fec.c sabe pasar de 46 tonos a 34 bits y jtty_msg.c de 34 bits a
 * texto. Los dos estaban comprobados contra el Fortran de WSJT-X y contra
 * los vectores normativos desde el 28 por la mañana, y los dos estaban
 * MUERTOS: no habia nada que les diera tonos. Esto es lo que faltaba.
 *
 * LO QUE HACE ESTE MODO DISTINTO DE TODOS LOS DEMAS DE LA RADIO
 * -------------------------------------------------------------
 * NO HAY RANURAS. FT8 y WSPR empiezan en un segundo conocido del reloj,
 * asi que su receptor puede dormir, despertarse, capturar una ventana y
 * buscar dentro de ella. JTTY empieza cuando el otro le da a la tecla.
 *
 * Eso quita la mitad facil del problema: aqui hay que tener SIEMPRE
 * suficiente pasado guardado como para que, en el momento en que una trama
 * termina, se pueda mirar hacia atras y encontrarla entera. Por eso lo de
 * abajo es un anillo que no para nunca, y por eso el sincronismo se prueba
 * en CADA media fila de simbolo en vez de una vez por ranura.
 *
 * LA CADENA, Y POR QUE CADA NUMERO ES EL QUE ES
 * ----------------------------------------------
 *   1. Audio de banda lateral a 12 kHz, el mismo que comen FT8, WSPR y
 *      ALE (ver demod_am.c). Se mezcla a banda base alrededor de 1.500 Hz.
 *      1.500 no es un gusto: 12000/1500 = 8 EXACTO, o sea que el
 *      oscilador es una tabla de ocho valores y no acumula error de fase
 *      nunca. Es el mismo truco de wspr_rx.c.
 *
 *   2. Diezmado por 24 -integra y vuelca- hasta 500 Hz complejos. El
 *      primer cero de la media de 24 cae justo en 500 Hz, que es donde
 *      empieza lo que se doblaria encima. Otra vez igual que WSPR, que
 *      diezma por 32 hasta 375 con el mismo razonamiento.
 *
 *   3. Y AQUI ESTA EL ENCAJE QUE HACE QUE TODO LO DEMAS SALGA SOLO:
 *
 *          31,25 baudios  ->  un simbolo son 16 muestras a 500 Hz
 *          DFT de 16 puntos  ->  bins cada 500/16 = 31,25 Hz
 *          separacion de los tonos de JTTY  ->  31,25 Hz
 *
 *      Los cuatro tonos caen en cuatro bins CONSECUTIVOS y ninguno se
 *      reparte entre dos. Con cualquier otra tasa de diezmado habria que
 *      interpolar y se tiraria señal, que es justo lo que no sobra en un
 *      modo pensado para trabajar bajo el ruido.
 *
 *   4. La DFT se evalua en 32 frecuencias en vez de 16 -media resolucion
 *      de bin, 15,625 Hz-. No mejora la resolucion, que la fija la
 *      ventana; lo que hace es poder ENCONTRAR una señal que no caiga
 *      clavada en un bin, que es el caso normal. Es lo mismo que hace FT8
 *      con FREQ_OSR y por el mismo motivo.
 *
 *   5. Y se calcula cada MEDIO simbolo, no cada simbolo, por lo mismo
 *      pero en el tiempo: la trama puede empezar en cualquier instante.
 *      Con medio simbolo el peor desajuste es un cuarto de simbolo, que
 *      cuesta poco menos de 1 dB.
 *
 * LO QUE CUESTA. Por cada media fila -cada 16 ms- son 32 frecuencias por
 * 16 muestras de DFT y una correlacion de sincronismo de 13 simbolos en
 * 26 posiciones de frecuencia. Eso va en el camino del audio. El Viterbi
 * de 512 estados NO: se apunta el candidato y lo resuelve el bucle
 * principal, porque ahi dentro cuesta milisegundos y el audio no los
 * tiene. Es la misma division que hace wspr_rx.c.
 */

/*
 * LA VENTANA DE BUSQUEDA: 64 medios bines de 15,625 Hz = 1.000 Hz, o sea
 * de 1.000 a 2.000 Hz de audio.
 *
 * *** POR QUE NO 500 Hz, QUE ES LO QUE PUSE PRIMERO. El dueño grabo 34
 * segundos de 14.090 y se los di a este mismo receptor. Con la ventana de
 * 500 Hz salio UN atomo. Moviendola 300 Hz salieron OCHO -un QSO entero,
 * "TU F5TLF 73 CIAO", con su bit de final, mas un IK3CHK mas alla-. Tres
 * estaciones en medio minuto y la ventana veia una. ***
 *
 * La razon de fondo es que en JTTY, como en FT8, el que transmite ELIGE su
 * desvio de audio dentro del filtro. No hay un sitio donde esten todos, y
 * 500 Hz alrededor de 1.500 daba por hecho que si.
 *
 * 1.000 Hz y no mas porque lo que manda es la memoria: el espectrograma
 * crece con el ancho y con las filas, y son 160 filas por 64 bines de
 * cuatro bytes = 40 kB de los 54 que hay para todo el modo. Con 1.500 Hz
 * no cabria el Viterbi.
 */
#define JTTY_RX_BINS      64U
#define JTTY_RX_MS_SIM    32U   /* muestras por simbolo a 1000 Hz */
#define JTTY_RX_DIEZMA    12U   /* 12000 -> 1000 */
#define JTTY_RX_CENTRO_HZ 1500

/*
 * CUANTO PASADO HAY QUE GUARDAR. Una trama son 59 simbolos y las filas van
 * cada medio simbolo, asi que ocupa 2*58+1 = 117 filas. Se guardan 160
 * para que el bucle principal pueda ir un poco por detras del audio sin
 * que le pisen la trama que esta mirando.
 */
#define JTTY_RX_FILAS    160U
#define JTTY_RX_FILAS_TRAMA  (2U * (JTTY_TRAMA_SIM - 1U) + 1U)   /* 117 */

/*
 * La memoria de trabajo. No cabe en .bss -son 27 kB y en esta radio quedan
 * 152 bytes- asi que sale de la que se reparten FT8, HFDL, WSPR, AIS y
 * ALE. Ver jtty_modo.c.
 */
typedef struct {
    float          espectro[JTTY_RX_FILAS][JTTY_RX_BINS];
    jtty_fec_ram_t fec;
    float          energias[JTTY_SIMBOLOS][4];
    /*
     * LA TABLA DE GIROS: UNA VUELTA ENTERA DE 64 PUNTOS, Y NADA MAS.
     *
     * La primera version guardaba la DFT entera precalculada -una tabla de
     * [bin][muestra]-. Con 32 bines de 16 muestras eran 4 kB; al ensanchar
     * la ventana a 64 bines de 32 muestras serian 16 kB, y con eso ya no
     * cabe el Viterbi.
     *
     * No hace falta: la fase del bin b en la muestra m es -2*pi*(b-32)*m/64,
     * asi que todos los valores que se necesitan son los 64 de UNA vuelta,
     * indexados por ((b-32)*m) modulo 64. 512 bytes en vez de 16.384, y el
     * coste es un producto y un AND por muestra.
     *
     * Se reconstruye en cada arranque del modo, como las tablas de la FFT
     * de FT8 y por lo mismo: viven en memoria prestada y se borran al
     * salir (ver ft8_shared_ram.h).
     */
    float          giro_c[JTTY_RX_BINS];
    float          giro_s[JTTY_RX_BINS];
    /* Y la ventana deslizante de un simbolo: 128 bytes que en .bss eran
     * justo los que faltaban para caber. Aqui no cuestan nada. */
    float          vi[JTTY_RX_MS_SIM];
    float          vq[JTTY_RX_MS_SIM];
} jtty_rx_ram_t;

/* Un atomo recien decodificado, con de donde salio. */
typedef struct {
    jtty_atomo_t atomo;
    int16_t      hz;     /* desvio del centro de la ventana, en Hz */
    uint8_t      calidad; /* 0..100, de la correlacion de sincronismo */
    /*
     * EN QUE FILA ACABO LA TRAMA, contada desde que arranco el modo.
     *
     * *** 28/09/2026, por el dueño del proyecto con la radio en 14.090:
     * "RX1AG TU CQ PD5LDB CQ" y "W3GQ TU CQ PD5LDB CQ" en dos renglones
     * seguidos, los dos a 46 Hz. "CQ PD5LDB CQ" no es el final de ninguno
     * de los dos: es OTRA estacion en la misma frecuencia. ***
     *
     * Esto es lo que permite separarlas, y no es una heuristica: las
     * tramas de UN mensaje van pegadas, o sea que la siguiente acaba
     * EXACTAMENTE 118 filas despues -59 simbolos a media fila por
     * simbolo-. Otra estacion cae en cualquier otra fase.
     *
     * Y hace falta la fila y no la hora del reloj porque el bucle
     * principal resuelve como mucho dos candidatos por pasada: dos atomos
     * que salen en la misma pasada llevan la MISMA hora aunque sus tramas
     * estuvieran a dos segundos, y dos de la misma trama pueden llevar
     * horas distintas. La fila es cuando la trama acabo de verdad.
     */
    uint32_t     fila;
} jtty_rx_r_t;

/* Filas de una trama a otra: 59 simbolos a media fila por simbolo. */
#define JTTY_RX_FILAS_PASO   (2U * JTTY_TRAMA_SIM)          /* 118 */

void    jtty_rx_start(jtty_rx_ram_t *ram, float fs);
void    jtty_rx_stop(void);
uint8_t jtty_rx_activo(void);

/* Desde la interrupcion del audio: banda lateral a 12 kHz. Barato. */
void    jtty_rx_mete(const float *audio, uint32_t n);

/* Desde el bucle principal: resuelve los candidatos apuntados. Devuelve 1
 * y rellena `out` cuando sale un atomo bueno; hay que llamarlo hasta que
 * devuelva 0. */
uint8_t jtty_rx_saca(jtty_rx_r_t *out);

/* Cuentas, para la chapa: candidatos probados y atomos buenos. */
uint32_t jtty_rx_probados(void);
uint32_t jtty_rx_buenos(void);

/* Y los que se tiraron sin probar por no caber en la cola. Tiene que ser
 * cero en la banda; si no, JTTY_CAND_N esta corto. Ver jtty_rx.c. */
uint32_t jtty_rx_perdidos(void);

#endif /* JTTY_RX_H */
