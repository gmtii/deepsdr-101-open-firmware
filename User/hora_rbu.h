#ifndef HORA_RBU_H
#define HORA_RBU_H

#include <stdint.h>
#include "dcf77.h"

/*
 * RBU - MOSCU, 66 2/3 kHz. 23/09/2026.
 *
 * QUE ES. Un transmisor en Taldom, al norte de Moscu, emite 10 kW en
 * 66 2/3 kHz (o sea 200/3 kHz, que se escribe 66,66 y no es lo mismo) con la
 * hora encima. Lo lleva el VNIIFTRI, que es el instituto de metrologia ruso.
 *
 * LO PRIMERO, LA VERDAD SOBRE SI ESTO SE VA A OIR. Taldom esta a unos
 * 3.450 km de España, y la cobertura que se le reconoce a RBU son 2.000 km
 * con 10 kW. Para comparar: DCF77 emite con 50 kW a 1.500 km y ya entra
 * justo, y ALS162 emite con 800 kW a 1.100 km. RBU es, con diferencia, la
 * mas dificil de las cinco, y lo honesto es decirlo antes que despues. Esta
 * implementada porque el coste era bajo -el frente de fase ya estaba hecho
 * para ALS162- y porque el barrido automatico la visita sola y la descarta
 * en veinticinco segundos si no hay nada. Si algun dia entra, entra.
 *
 * COMO VA, QUE NO SE PARECE A NINGUNA DE LAS OTRAS CUATRO. Aqui no se
 * modula la amplitud ni se dibuja un triangulo en la fase: la fase se modula
 * con un TONO, y el tono dice el bit.
 *
 *   Cada 100 ms hay un "elemento":
 *       0..10 ms   portadora limpia
 *      10..90 ms   portadora con la fase modulada por un tono, indice 0,698
 *      90..95 ms   portadora limpia
 *      95..100 ms  portadora apagada
 *
 *   El tono de 100 Hz es un CERO y el de 312,5 Hz es un UNO.
 *
 * Hay DIEZ elementos por segundo y solo DOS llevan datos:
 *
 *   elemento 0   bit de datos 1
 *   elemento 1   bit de datos 2
 *   elementos 2..6   siempre 0
 *   elementos 7 y 8  siempre 0, salvo en el segundo 59: marca de minuto
 *   elemento 9   siempre 1, marca de segundo
 *
 * EL SINCRONISMO ES LO MAS BONITO QUE TIENE. Como el elemento 9 vale siempre
 * 1 y los elementos 0 y 1 del segundo 00 valen siempre 1 (son el "siempre 1"
 * de la cabecera de la trama), en el cambio de minuto -donde ademas los
 * elementos 7 y 8 del segundo 59 valen 1- salen CINCO UNOS SEGUIDOS. En
 * cualquier otro sitio del minuto lo maximo que puede haber son tres. O sea
 * que una racha de cinco unos no puede ser otra cosa que el cambio de
 * minuto, y no hace falta nada mas para engancharse.
 *
 * El minuto empieza en el CUARTO de esos cinco unos, porque tres son del
 * segundo 59 y dos ya son del 00.
 *
 * EL REPARTO DE LOS 60 SEGUNDOS (bit de datos 1):
 *   00        siempre 1
 *   03..07    dUT1 (correccion fina), no se usa
 *   11..15    dUT1 otra vez
 *   18        signo de la diferencia con UTC
 *   19..23    esa diferencia, en horas (vale +3 desde 2014)
 *   25..32    año en BCD
 *   33..37    mes
 *   38..40    dia de la semana (1 = lunes)
 *   41..46    dia del mes
 *   47..52    hora
 *   53..59    minuto
 *   01,02,08,09,10,16,17,24  siempre 0
 *
 * Y el bit de datos 2 lleva DUT1, la fecha juliana truncada, quince ceros
 * fijos y -esto es lo que importa- OCHO PARIDADES PARES, seis de ellas sobre
 * los campos de hora y fecha. DCF77 tiene tres y MSF cuatro: RBU es la mejor
 * protegida de las cinco. Se comprueban todas.
 *
 * LA HORA QUE DA ES LA DE MOSCU, que va tres horas por delante de UTC y no
 * cambia en verano. Para sacar hora de pared española se resta esa
 * diferencia -la que venga en la trama, no un 3 escrito a mano- y se aplica
 * la regla europea del horario de verano sobre la fecha UTC resultante. Ver
 * hora_comun.h.
 */

void rbu_start(float fs_hz);
void rbu_feed(float fase);
void rbu_info(dcf_info_t *out);

#endif /* HORA_RBU_H */
