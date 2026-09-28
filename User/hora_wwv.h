#ifndef HORA_WWV_H
#define HORA_WWV_H

#include <stdint.h>
#include "dcf77.h"

/*
 * WWV - LA HORA DE LOS EEUU EN ONDA CORTA. 23/09/2026.
 *
 * QUE ES Y POR QUE ESTA AQUI. Las otras cuatro emisoras de este firmware
 * estan en onda larga, entre 60 y 162 kHz, y todas dependen de que la
 * entrada de RF de esta radio deje pasar algo por debajo de 200 kHz. WWV es
 * la unica que NO: emite en 2,5 / 5 / 10 / 15 y 20 MHz desde Fort Collins
 * (Colorado), o sea en plena onda corta, que es donde sabemos con certeza
 * que esta radio recibe bien. Desde España entra de noche en 10 MHz y por
 * la tarde en 15 MHz, con las mismas condiciones que cualquier otra emisora
 * de onda corta transatlantica. Es la apuesta mas distinta de todas y por
 * eso merece la pena tenerla.
 *
 * COMO VA. La portadora es AM normal, con voz y tonos. La hora viaja en una
 * SUBPORTADORA DE 100 Hz metida en el audio, cuyo NIVEL sube y baja:
 *
 *   - Sube al 18% de modulacion a los 30 ms de empezar cada segundo.
 *   - Baja al 3% a los 200 ms (eso es un CERO), a los 500 ms (un UNO) o a
 *     los 800 ms (una MARCA de posicion).
 *   - El segundo 00 no lleva subportadora: ese hueco marca el minuto.
 *
 * O sea que lo que hay que medir es cuanto dura el tramo "fuerte" de un
 * tono de 100 Hz: 170, 470 o 770 ms. Se hace igual que en DCF77 -umbral,
 * flancos y duraciones- solo que en vez de medir la envolvente de la
 * portadora se mide la AMPLITUD DE LOS 100 Hz dentro del audio. Eso es lo
 * unico que cambia en el frente, y es tambien lo que la hace inmune a los
 * tonos de 500 y 600 Hz y a la voz: un filtro estrecho centrado en 100 Hz
 * los deja fuera.
 *
 * LOS 60 BITS. El reparto es el de IRIG-H, y NO se parece al de DCF77 en
 * nada salvo en que tambien es BCD del bit menos significativo al mas:
 *
 *   00            sin subportadora: marca de minuto
 *   01            siempre 0
 *   02            DST1, horario de verano de EEUU a las 00:00Z (no se usa)
 *   03            aviso de segundo intercalar
 *   04..07        unidades del año
 *   08            siempre 0
 *   09,19,29,39,49,59   MARCAS de posicion (P1..P5 y P0)
 *   10..13,15..17 minutos
 *   14,18         siempre 0
 *   20..23,25,26  horas
 *   24,27,28      siempre 0
 *   30..33,35..38,40,41  dia del AÑO (1..366), no la fecha
 *   34,42..48     siempre 0
 *   50            signo de DUT1
 *   51..54        decenas del año
 *   55            DST2
 *   56..58        DUT1 en decimas
 *
 * DOS COSAS QUE SE OLVIDAN Y CUESTAN UNA HORA DE RELOJ:
 *
 *   1. WWV emite UTC, no hora local. Se convierte a hora de pared española
 *      AQUI, con la regla europea del horario de verano (ver hora_comun.h),
 *      y NO con los bits DST de la trama: esos son los de Estados Unidos y
 *      no valen para nada aqui.
 *   2. Al contrario que DCF77, ALS162, MSF y RBU -que describen el minuto
 *      SIGUIENTE-, la trama de WWV describe el minuto que acaba de EMPEZAR
 *      (convenio IRIG). Asi que cuando llega el hueco que cierra la trama ya
 *      ha pasado un minuto entero, y la hora que vale en ese instante es la
 *      decodificada MAS UNO. Esta implementacion lo hace, y es la unica
 *      diferencia de fondo entre esta emisora y las demas.
 *
 * BPM - LA OTRA QUE VA EN UNA SUBPORTADORA, DESDE CHINA. 23/09/2026.
 *
 * Emite en 2,5 / 5 / 10 y 15 MHz desde Pucheng (Shaanxi), y en 10 MHz lo
 * hace LAS 24 HORAS, que es lo que la hace interesante desde aqui: WWV
 * depende de que se abra el camino a Colorado, y esta no.
 *
 * Su hora tambien viaja en una subportadora dentro del audio y con el mismo
 * esquema de anchos de pulso que WWV, porque se diseño mirandolo. Cambian
 * tres cosas: los anchos son 200/500/800 ms en vez de 170/470/770, el
 * reparto de los bits es otro, y da el DIA DEL MES y el MES en vez del dia
 * del año -lo cual es mejor: una conversion menos-.
 *
 * LA SUBPORTADORA ES EL CABO SUELTO. Su memoria tecnica dice 125 Hz; hay
 * grabaciones recientes en las que aparece en 100. No he podido resolverlo
 * con fuentes, asi que no se elige: se buscan LAS DOS a la vez y se usa la
 * que tenga mas nivel. La pantalla dice cual encontro.
 *
 * Y se identifica en Morse en los minutos :29 y :59 de cada hora, 40
 * segundos, seguidos de una voz en chino. Eso sirve para saber si llega
 * antes de esperar tres minutos a una sincronizacion.
 *
 * NO HAY PARIDADES. Ninguna de las dos lleva, asi que lo unico que separa una
 * trama buena de una inventada son las seis marcas de posicion, los quince
 * bits que valen siempre cero, que cada cifra BCD sea una cifra de verdad, y
 * -como en las demas- que dos tramas seguidas se lleven exactamente un
 * minuto. Son suficientes, pero por eso se exigen TODAS.
 */

/*
 * Este modulo sirve a DOS emisoras: WWV y BPM. Comparten todo el frente -la
 * hora va en una subportadora dentro del audio, y lo que la codifica es
 * cuanto dura el tramo fuerte- porque BPM copio el formato de WWV, y su
 * memoria tecnica lo dice con todas las letras. Lo que cambia es el reparto
 * de los bits, las duraciones (170/470/770 ms en WWV, 200/500/800 en BPM) y
 * la frecuencia de la subportadora. Ver hora_bpm en el cuerpo del .c.
 */
void wwv_start(hora_emisora_t emisora, float fs_hz);
void wwv_feed(float envolvente);
void wwv_info(dcf_info_t *out);

#endif /* HORA_WWV_H */
