/*
 * EL AVISADOR DE ARMONICOS - 07/10/2026
 *
 * *** El dueño, despues de un dia entero persiguiendo un espejo: "mira en
 * 3411 estoy escuchando ft8 y ahi no hay ft8". ***
 *
 * No lo hay. Lo que hay es el FT8 de 30 metros, en 10.136 kHz, entrando por
 * el TERCER ARMONICO del oscilador local: con el dial en 3.411 el LO esta en
 * 3.387, y 3 x 3.387 = 10.161, a 25 kHz de 10.136. Dentro del tramo.
 *
 * POR QUE PASA, Y POR QUE NO LO ARREGLA NINGUN FIRMWARE. El oscilador local
 * de esta radio es una señal CUADRADA, y una cuadrada mezcla tambien en sus
 * armonicos. Lo unico que podria impedirlo es un filtro delante del
 * mezclador, y rf_lpf.c tiene UN SOLO filtro para toda la HF -de 0 a 36 MHz-,
 * asi que nada impide que una señal de 10 MHz llegue al mezclador mientras
 * estas en 3,4. Eso es el hardware que hay.
 *
 * LO QUE SI SE PUEDE HACER ES DECIRLO. Y hace falta de verdad, porque un
 * armonico y una imagen I/Q SE MUEVEN EXACTAMENTE IGUAL en la pantalla:
 *
 *     recibida por          al mover el dial, en el eje
 *     el fundamental        0x   (se queda quieta)
 *     el 2.o armonico      -1x
 *     el 3.o armonico      -2x
 *     una imagen I/Q       -2x
 *
 * Mirandolas no se distinguen. El dueño vio una señal "que va al reves al
 * doble de velocidad" y se tardo un dia entero en averiguar que no era una
 * imagen, con dos versiones de firmware y un corrector de banda ancha por el
 * camino. Esto lo habria contestado en un segundo.
 *
 * Y NO LLEVA TABLA PROPIA. Esa es la parte importante del diseño: la radio ya
 * tiene las 49 bandas escritas, con sus limites y sus nombres, y una lista
 * nueva aqui seria una lista que algun dia no mencionaria la banda que
 * acaban de añadir. Es el fallo que mas veces ha aparecido en este proyecto.
 * Asi que esto no sabe nada de bandas: quien llama le pasa una funcion que
 * recorre la tabla que YA existe.
 */
#ifndef ARMONICO_H
#define ARMONICO_H

#include <stdint.h>

/* Hasta el septimo. Mas arriba la cuadrada ya reparte muy poca energia y la
 * lista se llena de cosas que no se oyen. */
#define ARM_N_MIN   2U
#define ARM_N_MAX   7U

typedef struct {
    uint8_t     n;        /* que armonico */
    uint32_t    f_hz;     /* n * LO */
    int32_t     off_hz;   /* a que distancia del centro del tramo cae la banda */
    const char *nombre;   /* el de la banda, tal y como lo escribe la tabla */
} arm_hit_t;

/*
 * Recorre una tabla de rangos que vive en otro sitio. Devuelve 0 cuando se
 * acaba. `lo`/`hi` en hercios; un rango con lo==hi vale, es una frecuencia
 * suelta.
 */
typedef uint8_t (*arm_rango_fn)(uint16_t i, uint32_t *lo, uint32_t *hi,
                                const char **nombre);

/*
 * Busca que rangos de la tabla caen encima de n*LO, para n de ARM_N_MIN a
 * ARM_N_MAX, con el tramo de `span_hz` centrado en n*LO.
 *
 * Devuelve cuantos ha encontrado y escribe hasta `max` en `out`, ordenados
 * por armonico de menor a mayor: el 2.o entra con mucha mas fuerza que el
 * 7.o, asi que el primero de la lista es el sospechoso principal.
 */
uint8_t arm_busca(uint32_t lo_hz, uint32_t span_hz,
                  arm_rango_fn rangos, uint16_t n_rangos,
                  arm_hit_t *out, uint8_t max);

#endif /* ARMONICO_H */
