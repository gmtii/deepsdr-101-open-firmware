#ifndef FT8_LISTA_H
#define FT8_LISTA_H

#include <stdint.h>

/*
 * ======================================================================
 * QUE RENGLONES DE FT8 SE ENSEÑAN: TODOS O SOLO LAS CQ - 13/10/2026
 * ======================================================================
 *
 * Pedido por el usuario.
 *
 * Esto son veinte lineas que podrian estar en main.c, y estaban. Viven
 * aqui por lo que paso ayer con el toque del mapa: el fallo no fue
 * escribir dos lineas mal, fue escribirlas donde ningun banco llegaba.
 * main.c no lo compila el simulador; esto si (ver sim/ft8_lista.c).
 *
 * LO QUE TIENE MIGA, y es lo unico:
 *
 *   SE MIRAN TODOS LOS GUARDADOS, NO LOS QUE CABEN. Sin filtro da igual
 *   -hay dieciseis mensajes y caben quince, asi que recorrer hasta quince
 *   deja fuera el mas viejo y ya esta-. Con filtro NO da igual: si de los
 *   dieciseis solo tres son CQ y se mira hasta el decimoquinto, el panel
 *   enseña las CQ que hubiera ahi dentro y se pierde las de mas abajo. Un
 *   filtro que busca en media lista no filtra: recorta y despues filtra.
 *
 *   Y SI PASAN MAS DE LOS QUE CABEN, se enseñan LOS ULTIMOS. Los mas
 *   nuevos, que son los que se pueden contestar.
 */

/*
 * Un renglon de FT8 empieza por CQ?
 *
 * El mensaje es el QUINTO campo -Hora, Hz, dB, DT, Mensaje-, asi que hay
 * que saltarse cuatro tabuladores. Mirar al principio del renglon miraria
 * la hora.
 *
 * Se exige "CQ" y detras algo que no sea letra ni numero: hay indicativos
 * que empiezan por CQ y marcarlos seria decir que se les puede contestar
 * cuando no.
 */
uint8_t ft8_lista_es_cq(const char *linea);

/*
 * Escoge los renglones a enseñar y los deja en `idx`, DEL MAS VIEJO AL MAS
 * NUEVO, que es el orden en que los pinta el panel. Devuelve cuantos.
 *
 * `linea(i)` da el renglon i con 0 = EL MAS NUEVO, que es como los guarda
 * ft8_modo.c. `guardadas` es cuantos hay; `caben`, cuantos renglones tiene
 * el panel. `idx` tiene que admitir `caben` entradas.
 */
uint8_t ft8_lista_escoge(const char *(*linea)(uint8_t), uint8_t guardadas,
                         uint8_t solo_cq, uint8_t caben, uint8_t *idx);

#endif /* FT8_LISTA_H */
