#ifndef ALE_MODO_H
#define ALE_MODO_H

/*
 * ALE como modo de la radio. 27/09/2026.
 *
 * ale_rx.c saca palabras de 24 bits del aire; esto las junta en
 * indicativos y los pone en una tabla.
 *
 * POR QUE HAY QUE JUNTARLAS. Una palabra de ALE lleva solo TRES caracteres,
 * y los indicativos son mas largos. El estandar los trocea de tres en tres
 * y los manda en palabras seguidas, cambiando el preambulo:
 *
 *     TIS EA4   DATA XYZ          ->  "EA4XYZ"
 *     TIS MIA   DATA MI@          ->  "MIAMI"   (el '@' es relleno)
 *     TIS SAM                     ->  "SAM"
 *
 * La primera palabra ABRE (TO, THIS IS, THIS WAS, FROM) y las siguientes
 * EXTIENDEN, alternando DATA y REP. Hasta cinco palabras, o sea quince
 * caracteres.
 *
 * Asi que una fila de la tabla no es una palabra: es una secuencia entera.
 * Ensenar las palabras sueltas daria "EA4" y "XYZ" en renglones distintos,
 * que no es el indicativo de nadie.
 *
 * QUE SE OYE DE VERDAD. Casi todo lo que hay en el aire son SONDEOS: una
 * estacion que emite su propio indicativo anclado en THIS IS -o en THIS
 * WAS, si no se queda disponible- y lo repite mientras barre canales. Es
 * como se anuncian las estaciones militares y de gobierno en onda corta, y
 * con un receptor basta para ir haciendo la lista de quien se oye por
 * donde.
 */

#include <stdint.h>

#define ALE_MODO_ESTACIONES 16U
#define ALE_MODO_LINEAS     14U
#define ALE_MODO_LARGO      48U

uint8_t  ale_modo_start(void);
void     ale_modo_stop(void);
uint8_t  ale_modo_activo(void);

/* Audio de banda lateral a 12 kHz, desde la interrupcion del DMA. */
void     ale_modo_mete(const float *audio, uint16_t n);

/* Desde el bucle principal. */
void     ale_modo_poll(void);

/* La frecuencia de sintonia, para apuntarla junto a cada estacion. */
void     ale_modo_frec_pon(uint32_t khz);

void     ale_modo_borra(void);
uint32_t ale_modo_lineas(void);
const char *ale_modo_linea(uint32_t i);

/* Para la barra y la chapa. */
uint32_t ale_modo_palabras(void);
uint8_t  ale_modo_estaciones(void);
uint8_t  ale_modo_senal(void);
uint8_t  ale_modo_enganchado(void);
uint8_t  ale_modo_calidad(void);

#endif /* ALE_MODO_H */
