#ifndef NB_H
#define NB_H

#include <stdint.h>

/*
 * NOISE BLANKER - 23/09/2026.
 *
 * QUE ES, Y EN QUE SE DIFERENCIA DE LA REDUCCION DE RUIDO QUE YA HABIA.
 * La reduccion de ruido (nr_ss.c) trabaja en frecuencia: estima el espectro
 * del ruido de fondo y lo resta. Sirve para el siseo, que es continuo y
 * ancho. Contra un ruido de PULSOS no hace nada util: un pulso es corto en
 * el tiempo y ancho en frecuencia, asi que al mirarlo por bandas se ve
 * repartido en todas, no se distingue de la señal, y restarlo se lleva la
 * señal por delante. Lo que se oye en la radio es el clac-clac-clac de un
 * motor, una valla electrica o una linea de alta tension, y el NR lo deja
 * igual.
 *
 * El blanker trabaja en el TIEMPO y hace lo contrario: busca las muestras
 * donde la señal se dispara muy por encima del suelo de ruido, y las borra.
 * Un pulso dura decenas de microsegundos; a 96 kHz eso son unas pocas
 * muestras de 256. Quitarlas no se oye, y lo que queda es el hueco sin el
 * chasquido.
 *
 * POR QUE ANTES DEL FILTRO DE CANAL, Y NO DESPUES. Esta es la parte que
 * importa: hay que cazar el pulso mientras sigue siendo corto. El filtro de
 * canal tiene unos pocos kHz de ancho, y meter un pulso por un filtro
 * estrecho lo ALARGA -lo convierte en el timbre del filtro, que dura
 * milisegundos y ya no destaca sobre nada-. Un blanker puesto detras del
 * filtro no tiene nada que cazar. Por eso demod_am.c lo llama justo despues
 * de convertir el bloque crudo a float y ANTES de la traslacion y el
 * filtrado.
 *
 * WFM no lo lleva: tiene su propio camino (demod_wfm_process_raw) y en FM
 * de banda ancha el limitador ya se come los pulsos de amplitud.
 *
 * Esta en su propio fichero, sin nada del GD32 ni de CMSIS, para que el
 * simulador del PC pueda compilarlo tal cual y medirlo con ruido sintetico
 * (sim/nbtest.c). Los umbrales de nb_set_nivel() NO estan elegidos a ojo:
 * salen de ese banco.
 */

/* 0 = apagado. 1/2/3 = suave/medio/fuerte (umbral cada vez mas bajo, o sea
 * cada vez mas agresivo). Cualquier otro valor se trata como 0. */
void    nb_set_nivel(uint8_t nivel);
uint8_t nb_get_nivel(void);

/* Olvida el suelo de ruido estimado. Hay que llamarlo cuando cambia algo que
 * altera el nivel de entrada de golpe -cambio de modo, de tasa de muestreo,
 * de atenuador-, porque si no el blanker pasa un rato midiendo contra un
 * suelo que ya no existe: demasiado alto y no caza nada, demasiado bajo y
 * recorta la señal buena. */
void    nb_reset(void);

/*
 * Procesa un bloque de I/Q en el sitio. Devuelve cuantas muestras ha
 * borrado, que sirve para saber si de verdad esta haciendo algo (0 con el
 * blanker encendido significa que no hay pulsos, no que este roto).
 *
 * n como mucho NB_MAX_BLOQUE. Con el nivel a 0 devuelve 0 sin tocar nada.
 */
uint32_t nb_process(float *i_buf, float *q_buf, uint32_t n);

#define NB_MAX_BLOQUE 256U

#endif /* NB_H */
