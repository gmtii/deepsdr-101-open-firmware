#ifndef WSPR_FANO_H
#define WSPR_FANO_H

/*
 * WSPR: deshacer el codigo convolucional. 27/09/2026.
 *
 * POR QUE NO ES UN VITERBI. El codigo de WSPR tiene K=32, o sea 31 bits
 * de memoria: un Viterbi tendria que llevar la cuenta de 2^31 estados -dos
 * mil millones- y eso no cabe en ningun sitio ni de lejos. Con codigos
 * tan largos se usa decodificacion SECUENCIAL, que no explora el arbol
 * entero: solo el camino que va pareciendo bueno, y si se tuerce, vuelve
 * atras.
 *
 * DE LAS DOS SECUENCIALES, LA DE PILA Y NO LA DE FANO. La de Fano es la
 * clasica y la que usa la implementacion de referencia; esta es la de
 * pila (Zigangirov-Jelinek), que hace lo mismo con una regla mucho mas
 * simple -"extiende siempre el camino que mejor pinta"- y sin el juego de
 * umbrales que se aprieta y se afloja. En un sitio donde no puedo probar
 * contra una señal de verdad, prefiero el algoritmo que puedo demostrar
 * correcto en el banco al que tiene mas literatura detras.
 *
 * COMO FALLA, que es lo que hay que decidir de antemano: si la señal es
 * demasiado mala, el camino bueno no destaca y la busqueda se dispara. En
 * vez de tardar lo que haga falta, se le pone un tope de pasos y se
 * RINDE. Una baliza que no sale es lo normal en WSPR -se escucha media
 * hora para cazar unas pocas-; una baliza inventada seria un indicativo
 * de alguien que no ha transmitido, y eso no se puede distinguir de uno
 * bueno mirando la pantalla.
 */

#include <stdint.h>
#include "wspr_msg.h"

/*
 * Cuanta memoria de trabajo hace falta. Va fuera a proposito: en la radio
 * esto vive en la union que comparten FT8, HFDL y la cascada, igual que
 * todo lo demas que necesita kilobytes durante un rato.
 */
#define WSPR_FANO_NODOS   512U
#define WSPR_FANO_NODO_B   20U
#define WSPR_FANO_RAM  (WSPR_FANO_NODOS * WSPR_FANO_NODO_B)   /* 10.240 */

/*
 * Decodifica. `blandos` son 162 valores con signo, uno por bit de canal:
 * positivo = "parece un uno", y cuanto mas grande, mas seguro. 0 es "ni
 * idea", que es lo que hay que poner en un simbolo que no se ha podido
 * medir.
 *
 * `ram` tiene que apuntar a WSPR_FANO_RAM bytes alineados a 4.
 *
 * Devuelve 1 y deja los 50 bits del mensaje en `bits`; 0 si se ha
 * rendido. `pasos_out`, si no es 0, dice cuantos pasos costo - que es la
 * medida de lo dificil que estaba.
 */
uint8_t wspr_fano(const int8_t blandos[2U * WSPR_BITS_ENTRA],
                  void *ram, uint8_t bits[7], uint32_t *pasos_out);

/*
 * Lo mismo, pero contando ademas CUANTO cuadra lo decodificado con lo
 * recibido (0..100). Existe para poder mirar las dos poblaciones por
 * separado -señal de verdad y ruido puro- y elegir el umbral con datos
 * en vez de a ojo. wspr_fano() es esta con el umbral ya aplicado.
 */
uint8_t wspr_fano_ex(const int8_t blandos[2U * WSPR_BITS_ENTRA],
                     void *ram, uint8_t bits[7], uint32_t *pasos_out,
                     uint8_t *cuadra_out);

#endif /* WSPR_FANO_H */
