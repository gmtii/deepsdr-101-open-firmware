#ifndef AIS_MSG_H
#define AIS_MSG_H

/*
 * AIS: del chorro de bits al barco. 27/09/2026.
 *
 * Esto es la mitad de arriba del modo: coge los bits que ya ha sacado
 * ais_rx.c -destuffeados y con el CRC bueno- y los convierte en algo que se
 * pueda poner en una fila de la tabla. No sabe nada de radio.
 *
 * EL ORDEN DE LOS BITS, QUE ES DONDE SE EQUIVOCA TODO EL MUNDO.
 *
 * AIS viaja dentro de HDLC, igual que el paquete de AX.25, y en HDLC los
 * bits de cada byte salen al aire empezando por el de MENOS peso. Pero los
 * campos del mensaje de AIS (ITU-R M.1371) estan definidos sobre el chorro
 * de bits en el orden en que llegan, de MAS a menos peso dentro de cada
 * campo.
 *
 * Mezclar las dos cosas da un desastre silencioso: los MMSI salen
 * plausibles pero equivocados y las posiciones caen en mitad del oceano.
 * Por eso ais_rx.c NO monta bytes al estilo AX.25: empaqueta los bits en
 * orden de llegada, el primero en el bit 7 del byte 0, y aqui se leen con
 * campo(), que va de mas a menos peso. Asi solo hay una convencion en todo
 * el camino.
 *
 * QUE MENSAJES SE ENTIENDEN, Y POR QUE ESOS.
 *
 *   1, 2, 3   informe de posicion de barco (Clase A). El 95% de lo que se
 *             oye en un puerto.
 *   4         informe de estacion base costera. Trae posicion y HORA UTC,
 *             que es lo unico de AIS que sirve para algo mas que mirar
 *             barcos: pone el reloj de la radio en hora sin internet.
 *   5         datos estaticos del viaje: NOMBRE del barco, indicativo,
 *             tipo y destino. Llega una vez cada seis minutos.
 *   18, 19    informe de posicion de Clase B (barcos pequenos, pesqueros,
 *             recreo). El 19 ademas trae el nombre.
 *   24        datos estaticos de Clase B, en dos partes; la parte A es el
 *             nombre.
 *
 * Los demas se cuentan pero no se desempaquetan: son mensajes de gestion
 * (asignaciones de ranura, avisos de seguridad, mensajes binarios de
 * fabricante) que no tienen nada que ensenar en una tabla de barcos.
 */

#include <stdint.h>

/* Lo que puede salir de un mensaje. Los campos que no traiga el tipo que
 * sea se quedan a cero y su "hay_*" a cero. */
typedef struct {
    uint8_t  tipo;        /* 1..27, el que venga */
    uint32_t mmsi;        /* nueve cifras, la identidad del barco */

    uint8_t  hay_pos;
    int32_t  lat_e4;      /* grados * 10.000, positivo al norte */
    int32_t  lon_e4;      /* grados * 10.000, positivo al este */

    uint8_t  hay_vel;
    uint16_t sog_d;       /* velocidad sobre el fondo, nudos * 10 */
    uint16_t cog_d;       /* rumbo sobre el fondo, grados * 10 */

    uint8_t  hay_nombre;
    char     nombre[21];  /* 20 caracteres y el cero */

    uint8_t  hay_hora;    /* solo el tipo 4: UTC de una estacion costera */
    uint16_t ano;
    uint8_t  mes, dia, hora, minuto, segundo;
} ais_msg_t;

/*
 * Desempaqueta. `bits` son los bits en orden de llegada, empaquetados de
 * mas a menos peso; `nbits` cuantos hay (SIN los 16 del CRC).
 *
 * Devuelve 1 si el tipo se entiende y el MMSI tiene pinta de serlo.
 */
uint8_t ais_msg_saca(const uint8_t *bits, uint16_t nbits, ais_msg_t *out);

/* Cuantos bits pide como minimo un mensaje de ese tipo. 0 = desconocido. */
uint16_t ais_msg_bits_min(uint8_t tipo);

#endif /* AIS_MSG_H */
