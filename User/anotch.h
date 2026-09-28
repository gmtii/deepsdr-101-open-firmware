#ifndef ANOTCH_H
#define ANOTCH_H

#include <stdint.h>

/*
 * NOTCH AUTOMATICO. Etapa 35, 24/09/2026.
 *
 * QUE QUITA
 * ---------
 * Las portadoras y los heterodinos: esos pitidos fijos que se cuelan en
 * medio de una conversacion en banda lateral y que no se van bajando el
 * volumen porque estan dentro del filtro. El operador no tiene que
 * buscarlos ni ajustar nada - de ahi lo de automatico.
 *
 * COMO, EN UNA LINEA: prediciendo. Un pitido es la cosa mas predecible que
 * existe -dentro de un milisegundo vale exactamente lo que dice su seno- y
 * una voz no lo es. Asi que se monta un predictor que mira el audio de
 * hace D muestras e intenta adivinar el de ahora; lo que consigue adivinar
 * es, por construccion, la parte tonal. Y LO QUE SE SACA NO ES LA
 * PREDICCION, ES EL ERROR: la voz entera menos los pitidos.
 *
 * El predictor es un FIR que se ajusta solo por LMS normalizado. "Se
 * ajusta solo" es literal: nadie le dice en que frecuencia esta el pitido,
 * y si el pitido se mueve, los pesos lo siguen.
 *
 * POR QUE HAY UN RETARDO D
 * ------------------------
 * Es la pieza que decide si esto sirve o estropea. Sin retardo, el
 * predictor tambien acierta con la voz -y con el ruido- porque a un paso
 * de distancia casi todo se parece a si mismo, y entonces el error se
 * queda sin nada y lo que sale es silencio. Con un retardo de D muestras
 * solo sobrevive lo que sigue siendo predecible DESPUES de D muestras, que
 * es exactamente la definicion de "banda estrecha".
 *
 * O sea que D es el mando que reparte entre "cuanto pitido quita" y
 * "cuanta voz se lleva por delante". No se ha elegido a ojo: el banco
 * barre D, el numero de tomas y la ganancia, y mide las dos cosas a la
 * vez. Ver sim/anotchtest.c.
 *
 * DONDE VA
 * --------
 * En el camino decimado de 12 kHz, el mismo sitio y la misma tasa que la
 * reduccion de ruido (ver nr_ss.h). En AM, SAM y banda lateral; en FM no,
 * porque ahi un tono fijo no es una interferencia sino parte de lo que se
 * esta escuchando.
 *
 * Este fichero no depende de nada del GD32, igual que nb.c o dcf77.c, para
 * que el banco mida ESTE codigo y no una copia.
 */

#define ANOTCH_TOMAS_MAX 96U
#define ANOTCH_RETARDO_MAX 32U

/*
 * El retardo, en muestras. A 12 kHz son 2 ms. NO es un numero redondo
 * elegido a ojo: ver la tabla del barrido en anotch.c, donde se ve que
 * alargarlo hasta aqui mejora a la vez lo que quita y lo que respeta.
 */
#define ANOTCH_RETARDO 24U

/* Pone los pesos a cero y la linea de retardo a cero. `fs_hz` solo se
 * guarda para poder contarlo; el algoritmo no depende de la tasa. */
void anotch_init(float fs_hz);

/* Encendido/apagado. Apagado no gasta ni un ciclo: process() sale sin
 * tocar el audio. Al encenderlo se empieza de cero, porque unos pesos de
 * hace media hora no dicen nada de la señal de ahora. */
void    anotch_set_activo(uint8_t on);
uint8_t anotch_activo(void);

/*
 * La fuerza, 0..3. No es un volumen: mueve a la vez la ganancia de
 * adaptacion y el numero de tomas, que es el reparto de verdad entre
 * "engancha rapido y quita mucho" y "no toca la voz". Ver la tabla de
 * anotch.c, que sale del barrido del banco.
 */
void    anotch_set_fuerza(uint8_t f);
uint8_t anotch_fuerza(void);

/* Procesa `n` muestras EN EL SITIO. */
void anotch_process(float *audio, uint32_t n);

/* ----------------------------------------------------------------------
 * Para el banco.
 * ---------------------------------------------------------------------- */
/* Cuanta energia hay en los pesos. Con pitido sube; sin pitido se queda
 * cerca de cero. Sirve para ver si ha enganchado algo sin tener que
 * mirar la salida. */
float anotch_energia_pesos(void);

/* Ajuste a mano de los tres parametros, para que el banco pueda barrerlos
 * sin tener que recompilar. Fuera del banco no se usa: el firmware llama
 * a anotch_set_fuerza(). */
void anotch_params(uint16_t tomas, uint16_t retardo, float mu);

#endif /* ANOTCH_H */
