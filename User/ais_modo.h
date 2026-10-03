#ifndef AIS_MODO_H
#define AIS_MODO_H

/*
 * AIS como modo de la radio. 27/09/2026.
 *
 * Lo de siempre: ais_rx.c y ais_msg.c no saben nada de la radio, y este
 * fichero los engancha al audio, a la pantalla y al boton de frecuencia.
 *
 * LO QUE TIENE DE PARTICULAR FRENTE A LOS OTROS MODOS DIGITALES.
 *
 * NO ES UNA LISTA, ES UNA TABLA DE BARCOS. En FT8 y en HFDL cada mensaje
 * es un renglon y la lista crece. En AIS no sirve: un barco manda su
 * posicion cada pocos segundos y su NOMBRE una vez cada seis minutos, en
 * un mensaje distinto. Si cada mensaje fuera un renglon, la pantalla seria
 * el mismo barco repetido cuarenta veces y sin nombre.
 *
 * Asi que se junta por MMSI, que es la matricula: un renglon por barco, y
 * cada mensaje que llega actualiza lo que traiga -la posicion, el nombre, o
 * las dos-. Es lo mismo que hace hfdl_aircraft.c con los aviones y por el
 * mismo motivo.
 *
 * NO NECESITA LA HORA. Ni ranuras ni minutos pares: las rafagas llegan
 * cuando llegan, como en HFDL. El reloj no pinta nada aqui.
 *
 * Y AL REVES, PUEDE DARLA: los mensajes de tipo 4 son de estaciones
 * costeras y traen la hora UTC. Esta radio no tiene internet para ponerse
 * en hora, asi que eso es un regalo - pero de momento solo se ensena, no
 * se aplica: poner el reloj con algo que llega por radio sin mas
 * comprobacion que un CRC de 16 bits es como se estropea una hora que
 * estaba bien. Queda apuntado para cuando haya con que contrastarlo.
 */

#include <stdint.h>

/*
 * LA TABLA GUARDA MAS BARCOS DE LOS QUE ENSENA, Y LOS RENGLONES NO.
 *
 * Son dos numeros distintos a proposito, y el segundo casi cuesta el doble
 * que el primero:
 *
 *   un barco en la tabla   48 bytes  (matricula, nombre, posicion, cuentas)
 *   un renglon montado     72 bytes  (el mismo barco, ya escrito con sus
 *                                     tabuladores y sus comas)
 *
 * Los barcos se guardan de sobra -16- porque en un puerto se oyen mas de
 * los que caben en la pantalla y el que se sale sigue vivo por si vuelve a
 * hablar. Los RENGLONES, en cambio, solo hacen falta para lo que se ve:
 * montar veinte para ensenar catorce eran 432 bytes de SRAM escribiendo
 * texto que no mira nadie. Y la SRAM de esta radio va al 97%: la primera
 * version con veinte de los dos desbordo el enlazador por 500 bytes.
 *
 * El tope de 72 esta medido sobre el peor renglon posible: matricula de
 * nueve cifras (9) + nombre de veinte (20) + "43,371N 179,999W" (16) +
 * "102,3" (5) + "359o" (4) + "9999" (4) + cinco tabuladores + el cero = 64.
 */
#define AIS_MODO_BARCOS  16U    /* cuantos caben en la tabla */
#define AIS_MODO_LINEAS  14U    /* cuantos se llegan a ensenar */
#define AIS_MODO_LARGO   72U    /* lo que ocupa un renglon ya montado */

uint8_t  ais_modo_start(void);
void     ais_modo_stop(void);
uint8_t  ais_modo_activo(void);

/* Audio del discriminador, desde la interrupcion del DMA. */
void     ais_modo_mete(const float *disc, uint16_t n);

/* Desde el bucle principal: saca las tramas del anillo y las desempaqueta.
 * Barato - AIS no tiene nada parecido a un Viterbi. */
void     ais_modo_poll(void);

void     ais_modo_borra(void);
uint32_t ais_modo_lineas(void);
const char *ais_modo_linea(uint32_t i);

/* Para la barra y la chapa. */
uint32_t ais_modo_tramas(void);   /* con el CRC bueno */
uint32_t ais_modo_vistas(void);   /* cerradas, con o sin CRC bueno */
uint8_t  ais_modo_senal(void);    /* 0..99, sin calibrar */
uint8_t  ais_modo_barcos(void);

/*
 * La posicion del barco `i` (mismo orden que ais_modo_linea), en grados
 * por diez mil. Devuelve 0 si ese barco todavia no ha mandado posicion -
 * los mensajes de AIS van por tipos y el del nombre no lleva coordenadas,
 * asi que un barco puede estar en la tabla sin saberse donde esta-.
 *
 * Va sobre la TABLA entera -ais_modo_barcos()- y no sobre los renglones
 * que caben en la pantalla: en el mapa caben todos.
 *
 * Existe para el mapa (ver mapa.h). La tabla es privada a proposito; esto
 * es lo unico que se saca de ella.
 */
uint8_t  ais_modo_pos(uint32_t i, int32_t *lat_e4, int32_t *lon_e4);

/* El canal: 0 = A (161,975), 1 = B (162,025). */
uint8_t  ais_modo_canal(void);
void     ais_modo_canal_pon(uint8_t c);

#endif /* AIS_MODO_H */
