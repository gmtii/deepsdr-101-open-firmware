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
 * ======================================================================
 * EL HUSO HORARIO, QUE HASTA HOY ERA "ESPAÑA" ESCRITO EN TRES SITIOS
 * ======================================================================
 *
 * Pedido por el usuario, 10/10/2026.
 *
 * Lo que habia: hora_wwv.c y hora_rbu.c llamaban a hora_verano_espanna()
 * y sumaban una hora o dos. O sea que las emisoras que dan UTC daban hora
 * ESPAÑOLA, con la regla EUROPEA del horario de verano, estuviera la radio
 * donde estuviera. En Sidney eso son ocho o nueve horas de error, y encima
 * la regla va al reves porque el hemisferio sur cambia en los meses
 * contrarios. Y el error no se ve: sale una hora plausible.
 *
 * Y habia un tercer sitio: el byte 40 de EMISORAS.BIN, que lleva congelado
 * el desplazamiento con el que se empaqueto en el PC.
 *
 * AHORA el desplazamiento es UN numero, vive aqui, y lo usan los tres.
 *
 * EN MINUTOS Y NO EN HORAS, que es lo que obliga a que sea un int16_t: la
 * India esta en +5:30, Nepal en +5:45 y parte de Australia en +9:30. Un
 * i8 de horas -lo que hay hoy en el .BIN- no puede representarlos.
 *
 * DE DONDE SALE, por orden de lo que vale cada fuente:
 *
 *   1. EL RDS LO DICE. El grupo 4A trae el desplazamiento local en MEDIAS
 *      HORAS con signo, que es exactamente esto. Lo da la emisora de FM
 *      local de donde estes, asi que acierta en cualquier pais y con el
 *      horario de verano ya incluido. Es la mejor con diferencia, y
 *      llevaba desde septiembre decodificandose para usarse como sumando
 *      y tirarse.
 *   2. MEDIRLO contra una emisora que de UTC (WWV, BPM, MSF): tu reloj
 *      lleva tu hora de pared, el UTC lo da ella, y la resta es esto.
 *   3. LA TABLA DE CIUDADES, que propone por el QTH.
 *   4. A mano, en Ajustes.
 *
 * `medido` distingue las dos primeras de las dos ultimas: un huso que ha
 * dicho una emisora no lo pisa una tabla.
 */
void    hora_huso_pon(int16_t minutos);   /* a mano o de la tabla */
void    hora_huso_mide(int16_t minutos);  /* lo dijo una emisora */
int16_t hora_huso(void);
uint8_t hora_huso_medido(void);

/* El mayor y el menor que existen de verdad: Kiribati esta en +14 y Baker
 * en -12. Lo que salga de aqui se recorta a esto, venga de donde venga. */
#define HORA_HUSO_MIN  (-720)
#define HORA_HUSO_MAX  (840)

/*
 * Suma minutos a una fecha y hora completa, arrastrando el calendario.
 * Es hora_suma_horas() con la resolucion que hacen falta para +5:30.
 */
void hora_suma_minutos(int16_t minutos,
                       uint8_t *hora, uint8_t *minuto, uint8_t *dia,
                       uint8_t *mes, uint8_t *anno2, uint8_t *dia_semana);

/*
 * *** AQUI SE DECLARABA hora_verano_espanna(), BORRADA EL 10/10/2026. ***
 *
 * Era la regla europea del horario de verano -ultimo domingo de marzo y de
 * octubre a las 01:00 UTC-, y la llamaban hora_wwv.c, hora_rbu.c y dcf77.c
 * para convertir UTC en hora de pared ESPAÑOLA. Al quitar España de los
 * cuatro sitios donde estaba escrita se quedo sin llamadas, y resulto ser
 * una SEGUNDA implementacion de esa misma regla: la otra es la fila
 * HUSO_DST_EU de husos.c. Ver el comentario del hueco que ha dejado en
 * hora_comun.c.
 *
 * Quien quiera "la regla europea" ahora llama a
 *     husos_verano(HUSO_DST_EU, 60, dia, mes, anno2, hora_utc)
 * que es la misma y es la unica.
 */

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
