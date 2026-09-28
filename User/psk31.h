#ifndef PSK31_H
#define PSK31_H

#include <stdint.h>

/*
 * PSK31 - 28/09/2026, por el dueño del proyecto: "hay que implementar
 * psk31".
 *
 * QUE ES. Treinta y un baudios y pico de texto en una portadora que solo
 * cambia de fase. Nada de tonos: una sola frecuencia, y lo que lleva la
 * informacion es si la fase da la vuelta o no. Un cero es media vuelta
 * (180 grados); un uno es seguir igual. Ocupa 31 Hz -menos que un
 * teletipo, mucho menos que una voz- y por eso en 14.070 caben veinte
 * conversaciones donde cabria una sola de SSB.
 *
 * DE DONDE COME. De `s_ssb_dec`, la banda lateral ya diezmada a 12 kHz,
 * igual que el RTTY, el CW, el NAVTEX, el WEFAX, el SSTV, el FT8, el WSPR
 * y el ALE. Cruda, antes de la reduccion de ruido, por lo de siempre: la
 * sustraccion espectral esta pensada para que se entienda una voz y lo
 * que le hace a una portadora estrecha no ayuda a quien vive de medirle
 * la fase. Ver el comentario del sitio donde se llama, en demod_am.c.
 *
 * QUE CUESTA. Dentro de la interrupcion, por muestra: girar un fasor
 * (cuatro multiplicaciones) y sumar. Por cada doce muestras, una suma
 * corrida de 32 terminos, que es O(1). Y una vez por simbolo -treinta y
 * una veces por segundo- el resto. No hay nada caro aqui dentro, a
 * diferencia de lo que paso con WSPR.
 *
 * LO QUE NO LLEVA. No transmite. No tiene correccion de errores porque
 * PSK31 no la tiene (el BPSK normal; QPSK31 si, y no esta hecho).
 */

/* La velocidad, que no es un numero redondo por casualidad: 31,25 = 8000/256
 * en el diseño original de G3PLX, y aqui sale igual de 12000/384. */
#define PSK31_BAUDIO_X100  3125U

/*
 * Arranca y para. `fs_hz` tiene que ser 12000: se pasa para que el modulo
 * lo compruebe en vez de darlo por supuesto, igual que navtex_start().
 */
void    psk31_start(float fs_hz);
void    psk31_stop(void);
uint8_t psk31_activo(void);

/*
 * Le mete audio. Acepta cualquier `n`: trabaja muestra a muestra, sin
 * ventana de bloque, como navtex_process() y a diferencia de
 * rtty_process(). De solo lectura: no toca `audio`.
 */
void psk31_process(const float *audio, uint32_t n);

/*
 * Saca un caracter ya descodificado. Devuelve 1 y escribe *out si habia
 * alguno; 0 y no toca *out si no. Del bucle principal, no de la
 * interrupcion.
 */
uint8_t psk31_get_char(char *out);

/*
 * DONDE ESCUCHA. PSK31 no tiene frecuencia fija dentro del audio: la
 * emision cae donde el otro la ponga, y se sintoniza a mano mirando el
 * osciloscopio, igual que en RTTY se llevan las dos rayas a los dos
 * picos. El valor de fabrica son 1000 Hz.
 *
 * El margen que aguanta la deteccion diferencial es +-7,8 Hz (un cuarto
 * del baudio): mas lejos, la vuelta de fase de la portadora se confunde
 * con la vuelta de fase de los datos. Por eso hay ademas un seguimiento
 * automatico -ver psk31_diag()- que corrige la deriva una vez enganchado,
 * pero que NO sirve para encontrar la señal: para eso hay que sintonizar.
 */
void  psk31_set_centro_hz(float hz);
float psk31_get_centro_hz(void);

/*
 * DIAGNOSTICO, y aqui no es un extra.
 *
 * `cal` (0..100) dice cuanto se parece lo recibido a una portadora que
 * solo cambia de fase: para cada simbolo se mira cuanto se aparta el giro
 * de los dos unicos valores que PSK31 admite (0 y 180 grados). Con señal
 * buena sube de 90; con ruido puro se queda alrededor de 64, que no es
 * casualidad -es el promedio de |cos| de un angulo cualquiera, 2/pi-.
 * Por debajo de PSK31_CAL_MIN no se saca texto: un decodificador de
 * PSK31 sin puerta escribe basura a treinta caracteres por segundo.
 *
 * `hz_e100` es el error de frecuencia medido, en centesimas de hercio y
 * con signo. ESTE es el numero que sirve para sintonizar: se gira hasta
 * que se acerca a cero. Sin el, sintonizar PSK31 a ojo es buscar una raya
 * de 31 Hz en una ventana de 3 kHz.
 */
#define PSK31_CAL_MIN 78U

void psk31_diag(uint8_t *cal, int16_t *hz_e100);

#endif /* PSK31_H */
