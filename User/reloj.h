#ifndef RELOJ_H
#define RELOJ_H

#include <stdint.h>

/*
 * LA ARITMETICA DEL RELOJ. 24/09/2026.
 *
 * Esta radio no tiene RTC: la hora es "milisegundos desde el arranque" mas un
 * desfase. Poner el reloj es calcular ese desfase, y ahi es donde estan todas
 * las trampas: la vuelta por medianoche, el contador de milisegundos que NO
 * viene reducido (lleva desde el arranque, sin vueltas), y un desfase que
 * tiene que quedar dentro del dia.
 *
 * POR QUE ESTA EN SU PROPIO FICHERO Y NO DENTRO DE main.c. Porque hay que
 * poder medirlo. main.c no compila en el simulador -arrastra el GD32 entero-
 * y la alternativa seria escribir la misma formula otra vez en el banco, que
 * es comprobar que dos copias coinciden: no demuestra nada. Este fichero no
 * depende de nada de la placa, asi que el banco llama EXACTAMENTE a las
 * funciones que corren en la radio.
 *
 * Las tres funciones son puras: solo dependen de sus argumentos. El estado
 * -el desfase y si esta anclado o no- vive en main.c, que es de quien es.
 */

#define RELOJ_MS_DIA     86400000UL   /* milisegundos que tiene un dia */
#define RELOJ_MS_MIN         60000UL

/* La hora del dia, 0..86399999, a partir del contador de arranque y el
 * desfase. Es la UNICA forma de leer el reloj. */
uint32_t reloj_ms_del_dia(uint32_t ahora_ms, uint32_t desfase_ms);

/*
 * El desfase que hace que el reloj marque hh:mm, dejando la fase dentro del
 * minuto DONDE ESTABA. Es lo que quiere el teclado: quien teclea 21:47 no ha
 * dicho nada sobre los segundos, y el reloj no se los inventa.
 *
 * Sale siempre multiplo de un minuto.
 */
uint32_t reloj_desfase_minuto(uint32_t ahora_ms, uint8_t hh, uint8_t mm);

/*
 * El desfase que hace que, hace atraso_ms milisegundos, el reloj marcara las
 * mins_locales en punto - segundos incluidos.
 *
 * mins_locales son minutos locales del dia y se admiten fuera de 0..1439 (el
 * desplazamiento horario del RDS puede sacarlos por cualquiera de los dos
 * lados): aqui se meten en rango.
 */
uint32_t reloj_desfase_anclado(uint32_t ahora_ms, int32_t mins_locales,
                               uint32_t atraso_ms);

#endif /* RELOJ_H */
