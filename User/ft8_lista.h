#ifndef FT8_LISTA_H
#define FT8_LISTA_H

#include <stdint.h>

/*
 * ======================================================================
 * QUE RENGLONES DE FT8 SE ENSEÑAN: TODOS O SOLO LAS CQ - 13/10/2026
 * ======================================================================
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
 *   nuevos, que son los que se pueden contestar... salvo que el dedo haya
 *   subido la lista. Ver `subidos` en ft8_lista_escoge().
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
/*
 * Cuantos renglones pasan el filtro. Es la PRIMERA pasada de escoge(),
 * sacada aparte porque quien lleva el scroll necesita ese numero ANTES de
 * pedir los renglones: con el dedo subido, lo que hay que mover es la
 * ventana tantos renglones como hayan entrado nuevos, y eso no se sabe
 * hasta haber contado los que pasan.
 */
uint8_t ft8_lista_cuantos(const char *(*linea)(uint8_t), uint8_t guardadas,
                          uint8_t solo_cq);

/*
 * `subidos` es EL SCROLL, en renglones contados desde el final.
 *
 *   0  = pegado al mas nuevo, que es lo de siempre y lo que se quiere casi
 *        todo el rato: la lista se mira para ver lo que ACABA de entrar.
 *   n  = n renglones mas arriba, o sea hacia lo viejo.
 *
 * Se pasa por PUNTERO y sale RECORTADO a lo que de verdad hay: asi quien
 * lleva el dedo no tiene que saber cuantos renglones quedan por encima -ni
 * repetir aqui la cuenta de los que pasan el filtro-, empuja y esto le
 * dice donde se ha quedado. Con la lista mas corta que el panel sale 0,
 * que es lo correcto: no hay nada por encima que enseñar.
 */
uint8_t ft8_lista_escoge(const char *(*linea)(uint8_t), uint8_t guardadas,
                         uint8_t solo_cq, uint8_t caben,
                         uint8_t *subidos, uint8_t *idx);

/*
 * EL DEDO, EN RENGLONES.
 *
 * `dy` es lo que se ha movido el dedo desde la ultima vez, en pixeles, y
 * `alto` lo que mide un renglon. Devuelve 1 si `subidos` ha cambiado -y
 * entonces hay que repintar- y 0 si el dedo todavia no ha recorrido un
 * renglon entero.
 *
 * `acum` es lo que sobra y se queda para la proxima: sin el, un arrastre
 * lento de tres pixeles cada vez no subiria nunca, porque cada trozo por
 * separado es menos de un renglon. Es el mismo motivo por el que el
 * arrastre del dial acumula hercios.
 *
 * EL DEDO BAJA Y LA LISTA SUBE, que es como se mueve el papel y no la
 * ventana: arrastrar hacia abajo saca lo VIEJO, igual que en cualquier
 * lista de un telefono. Por eso `dy` positivo -dedo hacia abajo- aumenta
 * `subidos`.
 *
 * Aqui NO se sabe cuantos renglones hay por encima: el recorte de verdad
 * lo hace ft8_lista_escoge() en la siguiente pasada, que es quien cuenta
 * los que pasan el filtro. Esto solo evita salirse por abajo del cero y
 * por arriba del byte.
 */
uint8_t ft8_lista_arrastra(int16_t *acum, int16_t dy, uint8_t alto,
                           uint8_t *subidos);

/*
 * ======================================================================
 * CUANTOS DE LOS `cuantos` MAS NUEVOS PASAN EL FILTRO.
 * ======================================================================
 *
 * Esto es lo que mantiene la ventana quieta cuando el dedo la ha subido,
 * y no es lo que parece a primera vista. El primer intento fue restar los
 * que pasaban antes de los que pasan ahora, y ESTA MAL con la lista
 * llena: entran dos mensajes, se caen los dos mas viejos, la cuenta no
 * cambia... pero el mensaje que estaba en el renglon j esta ahora en el
 * j+2, asi que una ventana que no se mueve resbala dos renglones hacia lo
 * nuevo. Que es exactamente lo que el usuario no quiere.
 *
 * Lo que de verdad desplaza la ventana no es cuantos renglones hay, es
 * CUANTOS HAN ENTRADO POR ARRIBA. Esos son los `cuantos` primeros -los
 * mas nuevos-, y con el filtro de CQ puesto solo cuentan los que pasan:
 * un mensaje que no se enseña no mueve nada.
 *
 * `cuantos` lo saca quien llama restando el contador de decodificaciones
 * (ver ft8_modo_total()), que SI cuenta lo que ha entrado aunque la lista
 * este llena.
 */
uint8_t ft8_lista_nuevos(const char *(*linea)(uint8_t), uint8_t cuantos,
                         uint8_t solo_cq);

#endif /* FT8_LISTA_H */
