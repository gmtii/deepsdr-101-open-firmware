#ifndef RTTY_AUTO_H
#define RTTY_AUTO_H

#include <stdint.h>

/*
 * EL RTTY SE AJUSTA SOLO: TONOS, DESPLAZAMIENTO Y VELOCIDAD - 06/10/2026.
 *
 * *** El dueño, despues de que la deteccion de inversion le funcionara a
 * la primera sobre una señal que no habia visto: "una duda, los herzios y
 * los baudios se puede ajustar automaticamente?". ***
 *
 * Se puede, y las dos piezas ya estaban en la radio. Esto solo las
 * conecta.
 *
 * LOS HERCIOS SALEN DE LA TRAZA QUE YA SE PINTA. rtty_scope.c calcula una
 * FFT promediada del audio en cuanto entras en RTTY -es lo que dibuja el
 * oscilador- con 512 puntos a 12 kHz: 23,4 Hz por casilla. Para clavar un
 * desplazamiento de 450 Hz sobra. Los dos tonos son los dos picos, y no
 * hay que medir nada nuevo: ya estaban medidos y solo se miraban.
 *
 * LOS BAUDIOS NO SE MIRAN, SE PRUEBAN. No hay forma de leer la velocidad
 * de un espectro; lo que si se puede es coger las TRANSICIONES y
 * preguntarle a cada velocidad candidata si caen en su rejilla. Si la
 * velocidad buena es R, todas las transiciones estan en multiplos de 1/R,
 * asi que las fases (t*R mod 1) se apiñan; con una velocidad equivocada se
 * reparten por todo el circulo. La medida es el modulo de la media de
 * exp(2*pi*j*t*R), entre 0 y 1.
 *
 * Y no es un empate reñido. Sobre la grabacion de 3712 del dueño:
 *
 *      45,45 Bd  ->  0,02
 *      50,00 Bd  ->  0,52     <- la buena
 *      75,00 Bd  ->  0,05
 *     100,00 Bd  ->  0,03
 *
 * Un pico contra ruido. Y como la lista de velocidades tiene cuatro
 * entradas, son cuatro acumuladores que se tocan una vez por transicion.
 *
 * LAS DOS REGLAS QUE NO SE SALTAN, las mismas que la deteccion de
 * inversion de rtty.c:
 *
 *   1. SOLO SE MUEVE CUANDO LO DE AHORA VA MAL. Se mira la salud del
 *      decodificador -que parte de los caracteres cierran con su bit de
 *      parada- y por encima de RTTY_AUTO_SANO no se toca nada. Una radio
 *      que esta decodificando no se le cambia por detras.
 *   2. SE PUEDE APAGAR. Ajustes -> Digital -> Automatico. Hay veces que se
 *      quiere fijar el desplazamiento a mano y que la radio no opine.
 */

/* Encendido/apagado. Arranca encendido. */
void    rtty_auto_set(uint8_t on);
uint8_t rtty_auto_get(void);

/* Desde rtty_process(), una vez por bloque, con la decision de ese bloque. */
void rtty_auto_bit(uint8_t bit);

/* Desde el bucle principal, mientras el modo RTTY este puesto. */
void rtty_auto_poll(void);

/* Al entrar en el modo o al cambiar algo a mano: olvida lo acumulado. */
void rtty_auto_reset(void);

/* Para la pantalla y el banco: cuantas veces ha cambiado cada cosa. */
uint16_t rtty_auto_tonos_veces(void);
uint16_t rtty_auto_baud_veces(void);

/* La ultima puntuacion de cada velocidad candidata, 0..1000. Para el banco. */
void rtty_auto_puntos(uint16_t out[4]);

#endif /* RTTY_AUTO_H */
