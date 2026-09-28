#ifndef SPLASH_H_INCLUDED
#define SPLASH_H_INCLUDED

#include <stdint.h>

/*
 * Pantalla de arranque: lluvia de codigo sobre negro y el titulo
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
 * tinta clara, la anterior en media, una mas atras en oscura, y se borra la
 * que sale por la cola. Con eso el degradado de la estela sale solo, sin
 * guardar mas que los ultimos caracteres de cada columna.
 *
 * Cuesta ~2 ms por fotograma en vez de 21. Y como el arranque no tiene nada
 * mas que hacer, la animacion no le quita tiempo a nadie.
 *
 * Los caracteres son katakana de verdad (ver font_rain_16), que es lo que
 * cae en la pelicula, mas cifras.
 *
 * IDA Y VUELTA - 25/09/2026 y 28/09/2026
 * --------------------------------------
 * *** El dueño, el 25: "quita la lluvia del splash, que solo aparezca el
 * texto fijo y ya". Y el 28: "ahora que volvemos a tener espacio podemos
 * recuperar la lluvia de la ventana de inicio?". ***
 *
 * Se quito porque HFDL necesitaba flash, no porque estuviera mal. Volvio
 * cuando desensamblar el gestor de arranque abrio 64 kB mas arriba. Lo que NO
 * ha vuelto es font_title_60 (8.215 bytes para diez letras de una pantalla
 * que dura tres segundos): el titulo lo pinta font_ui_18b con las letras
 * separadas, que es lo que se quedo en su lugar y se lee igual de bien.
 *
 * EL SITIO DE LAS COLUMNAS, QUE NO ESTA EN .bss
 * ---------------------------------------------
 * El estado de las 44 columnas son 396 bytes y en esta radio quedaban 196
 * libres. Asi que NO se reserva aqui: se le PASA de fuera, y en el firmware
 * eso es un trozo del buffer de la cascada -54 kB que en el arranque estan
 * recien puestos a cero y que nadie va a mirar hasta mucho despues-. Es el
 * mismo sitio donde entraron FT8, HFDL, WSPR, AIS y ALE.
 *
 * Y si quien llama no tiene memoria que prestar -o presta menos de la que
 * hace falta-, esto NO falla: se pinta la pantalla fija, que es exactamente
 * lo que habia entre el 25 y el 28 de septiembre. Degradarse a algo que ya
 * funcionaba es mejor que no arrancar por una animacion.
 */

/* Cuanta memoria pide la lluvia. Quien la presta lo pregunta y compara: asi
 * el numero no esta escrito en dos sitios. */
uint32_t splash_ram_bytes(void);

/* Dibuja la pantalla entera, animada, y vuelve cuando termina. Bloquea:
 * esta pensada para llamarse una vez en el arranque, donde no hay nada mas
 * que hacer. `ms` es el reloj de milisegundos del sistema, que se le pasa en
 * vez de leerlo para que el simulador de host pueda compilar este fichero
 * tal cual. `ram`/`bytes`: ver el comentario de arriba; 0 = sin lluvia. */
typedef uint32_t (*splash_ms_fn)(void);

void splash_run(splash_ms_fn ms, void *ram, uint32_t bytes);

/* Un solo fotograma de la animacion, para el simulador: t es el tiempo
 * transcurrido en ms. Devuelve 0 cuando la animacion ha terminado. */
uint8_t splash_paso(uint32_t t);
void    splash_reinicia(void *ram, uint32_t bytes);

#endif /* SPLASH_H_INCLUDED */
