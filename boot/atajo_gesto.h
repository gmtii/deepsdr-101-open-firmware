/*
 * EL GESTO DE LA SALIDA DE EMERGENCIA, SIN HARDWARE DE POR MEDIO.
 *
 * Esta separado de atajo_dfu.c a proposito: lo de alli son registros del
 * GD32 y no se puede correr en el PC, y esto es la DECISION, que es justo
 * lo que puede salir mal en silencio. Asi lo compila tambien el banco
 * (sim/atajo.c) y se prueba contra giros de verdad, rebotes y sueltas.
 *
 * Es un automata de una muestra: se le va dando lo que leen las patas y
 * contesta que hacer.
 */
#ifndef ATAJO_GESTO_H
#define ATAJO_GESTO_H

#include <stdint.h>

/*
 * EL PERIODO DE MUESTREO NO ES UN NUMERO CUALQUIERA - lo dijo el banco.
 *
 * Contando pasos con signo, un salto de DOS bits a la vez no es un paso
 * valido y vale cero: es lo que nos protege del ruido. Pero si el mando
 * avanza dos cuartos de paso ENTRE DOS LECTURAS, lo que llega es
 * exactamente eso, un salto de dos bits, y el paso se pierde.
 *
 * O sea que el muestreo tiene que ir mas rapido que el mando. Un giro de
 * los bruscos son unos 30 topes por segundo, que son 120 cuartos de paso,
 * uno cada 8 ms. A 2 ms por lectura hay cuatro lecturas por cuarto de paso:
 * sobra. A los 5 ms que tenia esto al principio NO sobraba, y el banco lo
 * dijo antes que la radio.
 *
 * 600 lecturas x 2 ms siguen siendo la misma ventana de 1,2 s.
 */
#define ATAJO_MUESTRAS_MAX   600U   /* 600 x ~2 ms = ~1,2 s de ventana */
#define ATAJO_MUESTRAS_SECA  120U   /* ~240 ms sin moverse -> no es el gesto */
#define ATAJO_PASOS_PIDE       8    /* 8 cuartos de paso = 2 topes del mando */

typedef struct {
    uint32_t n;        /* muestras consumidas */
    uint32_t ant;      /* estado de cuadratura anterior, (A << 1) | B */
    int32_t  pasos;    /* cuartos de paso acumulados, CON SIGNO */
} atajo_t;

typedef enum {
    ATAJO_SIGUE = 0,   /* aun no se sabe, dame otra muestra */
    ATAJO_NO    = 1,   /* no es el gesto: arranque normal */
    ATAJO_SI    = 2    /* es el gesto: a la ROM de fabrica */
} atajo_r;

/* Primera lectura: fija el estado de partida. Si el boton no esta pulsado
 * ya contesta que no. */
atajo_r atajo_empieza(atajo_t *a, uint32_t boton, uint32_t ab);

/* Cada muestra siguiente. 'boton' es 1 si esta pulsado; 'ab' es
 * (A << 1) | B, 0..3. */
atajo_r atajo_muestra(atajo_t *a, uint32_t boton, uint32_t ab);

#endif
