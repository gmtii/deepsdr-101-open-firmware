#ifndef FT8_MODO_H
#define FT8_MODO_H

#include <stdint.h>

/*
 * FT8 COMO MODO DE LA RADIO. 25/09/2026.
 *
 * Envuelve las cuatro piezas portadas de la rama hfdl del proyecto padre
 * -decimador, FFT de 1024, adaptador de cascada y decodificador- y les pone
 * delante la misma forma que ya tienen NAVTEX, WEFAX, SSTV y APRS:
 * arrancar, parar, comer audio y soltar texto. Asi main.c no se entera de
 * como funciona FT8 por dentro, igual que no se entera de como funciona el
 * SITOR-B.
 *
 * LO QUE FT8 TIENE Y LOS DEMAS NO: el RELOJ. Una emision dura 12,64 s
 * dentro de una ranura de 15, y las ranuras empiezan en los segundos 0, 15,
 * 30 y 45 de cada minuto UTC. Si la radio no sabe la hora, no sabe cuando
 * empieza una ranura, y sin eso no hay nada que decodificar. Por eso esto
 * mira el RTC (ver rtc_hw.c, portado el 24/09) y por eso ft8_modo_hay_hora()
 * existe: para poder decir "no tengo hora" en vez de no decodificar nunca y
 * dejar que el usuario adivine por que.
 *
 * EL REPARTO DE TRABAJO, que es lo que decide donde va cada llamada:
 *
 *   ft8_modo_come()   desde la interrupcion del audio. Solo guarda muestras
 *                     y remuestrea; barato y acotado.
 *   ft8_modo_tick()   desde el bucle principal. Aqui esta lo caro: la FFT
 *                     de cada subbloque y, al cerrarse la ranura, el
 *                     decodificador entero (que se lleva sus buenos cientos
 *                     de milisegundos).
 *
 * Meter lo segundo en lo primero seria colgar el audio 300 ms cada 15
 * segundos, que es justo lo que no se puede hacer.
 */

#define FT8_MODO_LINEAS   16   /* mensajes que se guardan: los que caben en el panel entero, ver FT8_PANEL_FILAS en main.c */
#define FT8_MODO_LARGO    88   /* ver FT8_DECODER_LINE_LEN - tiene que ser >= */

/* Arranca FT8. `fs_hz` es la tasa del audio que se le va a dar (12000 en el
 * camino de banda lateral, igual que NAVTEX y los demas). */
void ft8_modo_start(float fs_hz);

/* Para y libera. Deja la cascada normal como estaba: FT8 y la cascada
 * comparten memoria (ver ft8_shared_ram.h), asi que esto NO es opcional. */
void ft8_modo_stop(void);

uint8_t ft8_modo_activo(void);

/* Desde la interrupcion del audio, con el bloque de 12 kHz. */
void ft8_modo_come(const float *muestras, uint16_t n);

/* Desde el bucle principal, en cada vuelta mientras FT8 este encendido. */
void ft8_modo_tick(void);

/* 1 si el RTC tiene hora buena y por tanto se sabe donde empiezan las
 * ranuras. Si es 0, FT8 no puede decodificar y la pantalla lo dice. */
uint8_t ft8_modo_hay_hora(void);

/* Segundo dentro de la ranura de 15 s (0..14), para la barra de progreso. */
uint8_t ft8_modo_segundo(void);

/* "El cero de la ranura es AHORA". Cuadra la ranura a mano sin tocar el
 * reloj: las ranuras de FT8 empiezan en los segundos 0/15/30/45 UTC, y si el
 * RTC esta corrido unos segundos la radio captura a destiempo. Un toque en el
 * momento bueno lo arregla. El desfase se conserva al apagar y encender FT8.
 * Ver el comentario de s_desfase en ft8_modo.c. */
void    ft8_modo_cuadra(void);
uint8_t ft8_modo_desfase(void);

/*
 * El DT medio de la ultima tanda, en ms. Es lo que la radio usa para
 * cuadrarse sola (ver cuadra_sola() en ft8_modo.c) y sale en la barra para
 * poder verlo hacerlo: tiene que irse acercando a cero. Si en vez de eso se
 * doblara, el signo de la correccion estaria del reves.
 */
int16_t ft8_modo_dt_ms(void);

/* Mensajes decodificados, del mas nuevo al mas viejo. `i` de 0 a
 * FT8_MODO_LINEAS-1; devuelve cadena vacia si ahi no hay nada todavia. */
const char *ft8_modo_linea(uint8_t i);

/* Cuantas ranuras se han perdido por estar todavia decodificando la
 * anterior. Si esto sube, la decodificacion no cabe en los 15 segundos:
 * ver el comentario de FT8_PASO_MS en ft8_modo.c. */
uint16_t ft8_modo_perdidas(void);

/* Vacia la lista de mensajes. No toca la captura en curso: lo que se este
 * oyendo ahora sigue su camino y saldra al cerrarse la ranura. */
void ft8_modo_borra(void);

/* Cuantos van desde que se encendio, y cuantos en la ultima ranura. */
uint32_t ft8_modo_total(void);
uint8_t  ft8_modo_ultima(void);

/*
 * LOS DOS NUMEROS QUE CONTESTAN A "¿POR QUE NO DECODIFICA?".
 *
 * `candidatos` son los sitios donde la busqueda de sincronismo creyo ver una
 * emision en la ultima ranura, ANTES de intentar descifrarla. Y el margen de
 * dB es lo que midio la FFT en esa misma ranura.
 *
 * Con la lista vacia, esos dos numeros separan tres situaciones que desde
 * fuera se ven exactamente igual: cero candidatos y dB planos = no llega
 * nada o la senal cae fuera de la ventana; cero candidatos con dB vivos =
 * hay senal pero no es FT8 (o la ranura esta descuadrada); candidatos sin
 * mensajes = es FT8 y llega rota. Sin ellos hay que adivinar.
 *
 * Salen por aqui, y no leyendo ft8_decoder.h desde main.c, por lo mismo que
 * ft8_modo_ventana_hz(): la pantalla no tiene por que conocer las tripas.
 */
uint16_t ft8_modo_candidatos(void);

/* Lo que tardo la ultima decodificacion, en ms, y cuantos subbloques de
 * audio se han perdido desde que se encendio FT8. El segundo DEBERIA ser 0:
 * si no lo es, a la captura le faltan trozos y FT8 decodifica peor sin que se
 * vea por que. Ver ft8_decimator_perdidos(). */
uint16_t ft8_modo_ms(void);
uint16_t ft8_modo_perdidos(void);
void     ft8_modo_db(int16_t *lo, int16_t *hi);

/* Estado en una palabra, para el panel: "sin hora", "escuchando",
 * "decodificando". */
const char *ft8_modo_estado_txt(void);

/* Ancho de la ventana de busqueda, en hercios del audio de 12 kHz, contando
 * desde 0. El decodificador solo mira ahi dentro: lo que caiga por encima no
 * se decodifica aunque se oiga. La pantalla lo pinta como una raya para que
 * sintonizar FT8 sea "meter la senal a la izquierda de esa raya" y no
 * adivinar. Sale de las constantes del adaptador, no de un numero escrito a
 * mano, para que no se quede vieja el dia que la ventana cambie. */
float ft8_modo_ventana_hz(void);

#endif /* FT8_MODO_H */
