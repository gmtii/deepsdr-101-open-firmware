/*
 * LA ARITMETICA DEL CALENDARIO, APARTE DEL PERIFERICO - 13/10/2026.
 *
 * Esto no toca un solo registro, y por eso existe como fichero propio:
 * rtc_hw.c habla con el RTC del GD32 y el simulador NO puede compilarlo
 * -no hay periferico al otro lado-, asi que cualquier cuenta que viva
 * dentro de rtc_hw.c es una cuenta que ningun banco puede mirar.
 *
 * Es la regla que este proyecto lleva meses pagando: "el fallo no fue
 * escribir dos lineas mal, fue escribirlas donde ningun banco podia
 * llegar". Las conversiones BCD y los dias desde una fecha son
 * exactamente el tipo de cuenta que se escribe mal una vez y no se nota
 * hasta que el reloj da el 1 de marzo de un bisiesto.
 *
 * Aqui dentro no hay nada del GD32: son enteros. El banco es
 * sim/rtc_cal.c, y compila ESTE fichero, no una copia.
 */
#ifndef RTC_CAL_H
#define RTC_CAL_H

#include <stdint.h>

/* El RTC guarda cada campo en BCD empaquetado: 0x59 son cincuenta y
 * nueve, no ochenta y nueve. Fuera de rango devuelven 0, que es lo que
 * hace un registro recien borrado y no un numero inventado. */
uint8_t rtc_cal_bcd_a_dec(uint8_t bcd);
uint8_t rtc_cal_dec_a_bcd(uint8_t dec);

/* Dias civiles desde 1970-01-01 para una fecha propia (year completo,
 * mes 1..12, dia 1..31). Algoritmo de dias-desde-civil con el año
 * empezando en marzo, que es el que hace que el bisiesto caiga al FINAL
 * y desaparezcan los casos especiales de febrero. Vale de 1970 en
 * adelante; por debajo devuelve 0. */
int32_t rtc_cal_dias_desde_1970(uint16_t year, uint8_t month, uint8_t day);

/* Segundos desde 1970-01-01 00:00:00 UTC. Sin husos: lo que entra es lo
 * que marca el calendario del RTC, y quien quiera hora local que le
 * sume su desfase, que es como lo hace el resto del arbol. */
uint32_t rtc_cal_epoca(uint16_t year, uint8_t month, uint8_t day,
                       uint8_t hour, uint8_t minute, uint8_t second);

/* Si una fecha es posible. Lo usa rtc_hw_set() para no meterle al
 * periferico un 31 de febrero: el RTC lo aceptaria y lo guardaria, y a
 * partir de ahi el calendario cuenta desde una fecha que no existe. */
uint8_t rtc_cal_valida(uint16_t year, uint8_t month, uint8_t day,
                       uint8_t hour, uint8_t minute, uint8_t second);

/* Dia de la semana, 1 = lunes ... 7 = domingo, que es como lo numera el
 * RTC del GD32. El periferico NO lo calcula solo: hay que dárselo, y si
 * se le da mal se queda mal para siempre. */
uint8_t rtc_cal_dia_semana(uint16_t year, uint8_t month, uint8_t day);

#endif /* RTC_CAL_H */
