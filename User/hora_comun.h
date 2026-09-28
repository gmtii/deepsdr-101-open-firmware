#ifndef HORA_COMUN_H
#define HORA_COMUN_H

#include <stdint.h>

/*
 * CALENDARIO COMPARTIDO POR LAS EMISORAS DE HORA. 23/09/2026.
 *
 * POR QUE EXISTE ESTE FICHERO. Las cinco emisoras de hora necesitan lo
 * mismo: saber cuantos dias tiene un mes, que dia de la semana cae una
 * fecha, y -las que emiten UTC- si en España esta vigente el horario de
 * verano. Antes de tenerlo aqui, la tabla de dias por mes estaba escrita
 * dentro de dcf77.c y solo servia alli; en cuanto llegaron WWV (que da el
 * DIA DEL AÑO y hay que convertirlo a fecha) y RBU (que da hora de Moscu y
 * hay que restar y volver a sumar) habia tres sitios donde escribir el
 * mismo calendario. Ese es exactamente el fallo que este proyecto ya ha
 * pagado varias veces: la misma lista en varios sitios y ningun error de
 * compilacion cuando una se queda atras.
 *
 * TODO ESTO VALE PARA 2000..2099 y no se disimula: los años se manejan como
 * dos cifras porque asi los emiten las cinco emisoras, y la regla de los
 * bisiestos que se usa aqui (año multiplo de 4) es la correcta en ese
 * siglo entero, porque 2000 fue bisiesto y 2100 cae fuera.
 */

/* Dias de cada mes en un año NO bisiesto. El indice es el mes, 1..12; la
 * posicion 0 vale 0 para que nadie tenga que restar uno. */
extern const uint8_t k_hora_dias_mes[13];

/* 1 si el año 2000+anno2 es bisiesto. */
uint8_t hora_bisiesto(uint8_t anno2);

/* Dias que tiene ese mes de ese año, febrero incluido. */
uint8_t hora_dias_del_mes(uint8_t mes, uint8_t anno2);

/* Dia de la semana de una fecha, en el convenio de DCF77: 1 = lunes ...
 * 7 = domingo. Es el mismo convenio que usa dcf_info_t, para que no haya
 * que acordarse de cual es cual segun de donde venga la hora. */
uint8_t hora_dia_semana(uint8_t dia, uint8_t mes, uint8_t anno2);

/*
 * Dia del año (1..366) a dia y mes. Lo necesita WWV, que es la unica que no
 * emite la fecha sino el ordinal del dia.
 * Devuelve 0 si el ordinal no cabe en ese año (y entonces no toca la salida).
 */
uint8_t hora_dia_del_anno_a_fecha(uint16_t doy, uint8_t anno2,
                                  uint8_t *dia, uint8_t *mes);

/*
 * 1 si a esa fecha y hora UTC esta vigente el horario de VERANO en España.
 *
 * La regla europea es la misma en todos los paises de la Union desde 1996 y
 * esta escrita en horas UTC, no en horas locales: el verano empieza a las
 * 01:00 UTC del ULTIMO DOMINGO DE MARZO y acaba a las 01:00 UTC del ULTIMO
 * DOMINGO DE OCTUBRE. Usarla aqui es lo que permite que WWV y RBU, que
 * emiten UTC y hora de Moscu, den hora de pared española sin que quien
 * llame tenga que saber nada.
 *
 * dia/mes/anno2 y hora_utc son la fecha y la hora EN UTC, no la local: con
 * la local la comparacion del dia del cambio saldria mal por una hora.
 */
uint8_t hora_verano_espanna(uint8_t dia, uint8_t mes, uint8_t anno2,
                            uint8_t hora_utc);

/*
 * Suma horas a una fecha y hora, arrastrando el dia, el mes, el año y el dia
 * de la semana. Se usa para pasar de UTC a hora local sin que el calendario
 * se quede atras cuando el ajuste cruza la medianoche.
 * desplazamiento va en horas y puede ser negativo (RBU resta las de Moscu).
 */
void hora_suma_horas(int8_t desplazamiento,
                     uint8_t *hora, uint8_t *dia, uint8_t *mes,
                     uint8_t *anno2, uint8_t *dia_semana);

#endif /* HORA_COMUN_H */
