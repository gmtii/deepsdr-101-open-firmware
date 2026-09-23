#ifndef SPLASH_H_INCLUDED
#define SPLASH_H_INCLUDED

#include <stdint.h>

/*
 * Pantalla de arranque: lluvia de codigo verde sobre negro y el titulo
 * "DeepSDR reload".
 *
 * COMO ESTA HECHA, Y POR QUE ASI
 * ------------------------------
 * Una lluvia de Matrix es, en el fondo, unas decenas de columnas de
 * caracteres cayendo. Redibujar la pantalla entera 30 veces por segundo para
 * eso serian ~384.000 accesos al bus por fotograma - 21 ms, casi todo el
 * presupuesto - y ademas seria trabajo tirado: entre un fotograma y el
 * siguiente solo cambian cuatro celdas por columna.
 *
 * Asi que no se redibuja la pantalla: se dibuja el CAMBIO. Cuando una columna
 * avanza un paso, se pintan cuatro celdas de 18x20 px - la cabeza nueva en
 * verde claro, la anterior en verde medio, una mas atras en verde oscuro, y
 * se borra la que sale por la cola. Con eso el degradado de la estela sale
 * solo, sin guardar mas que los ultimos caracteres de cada columna.
 *
 * Cuesta ~2 ms por fotograma en vez de 21. Y como el arranque no tiene nada
 * mas que hacer, la animacion no le quita tiempo a nadie.
 *
 * Los caracteres son katakana de verdad (ver font_rain_16), que es lo que
 * cae en la pelicula, mas cifras. El titulo lleva su propia fuente con SOLO
 * sus diez letras: la completa a 60 px serian 90 KB de flash para una
 * pantalla que dura dos segundos.
 */

/* Dibuja la pantalla entera, animada, y vuelve cuando termina. Bloquea:
 * esta pensada para llamarse una vez en el arranque, donde no hay nada mas
 * que hacer. `ms` es el reloj de milisegundos del sistema, que se le pasa en
 * vez de leerlo para que el simulador de host pueda compilar este fichero
 * tal cual. */
typedef uint32_t (*splash_ms_fn)(void);

void splash_run(splash_ms_fn ms);

/* Un solo fotograma de la animacion, para el simulador: t es el tiempo
 * transcurrido en ms. Devuelve 0 cuando la animacion ha terminado. */
uint8_t splash_paso(uint32_t t);
void    splash_reinicia(void);

#endif /* SPLASH_H_INCLUDED */
