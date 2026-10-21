#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

/*
 * LA BATERIA, POR EL CANAL 18 DEL ADC0 QUE LLEVA DENTRO EL GD32F450.
 *
 * Segun el comentario de ADC_SYNCCTL_VBATEN en gd32f4xx_adc.h -"canal 18
 * (un cuarto del voltaje de la bateria externa), habilitacion del ADC0"-
 * y el capitulo del ADC de la hoja de datos del GD32F450, activar ese
 * interruptor lleva VBAT/4 al canal 18 por dentro. El divisor por cuatro
 * esta DENTRO del periferico, no es una red de resistencias de la placa,
 * asi que battery_get_millivolts() lo deshace -multiplicando por 4- como
 * parte de la conversion.
 *
 * LA CAIDA DEL DIODO, pedido por el usuario el 31/07/2026: la patilla
 * VBAT de esta placa esta detras de un diodo en serie desde los bornes de
 * la bateria -la proteccion de siempre contra polaridad invertida y
 * retorno de corriente-, asi que la patilla lee MENOS que la bateria de
 * verdad, exactamente la caida directa de ese diodo.
 * BATTERY_DIODE_DROP_MV son 700 mV, la Vf tipica de un diodo de silicio,
 * y se suman despues del por cuatro para aproximar el voltaje real en los
 * bornes.
 *
 * Es una aproximacion FIJA, no una Vf medida ni compensada en
 * temperatura: un diodo de verdad cae algo menos a poca corriente y algo
 * mas a mucha corriente o con frio. O sea que esto vale como medidor, no
 * como instrumento calibrado. Si lee sistematicamente desviado contra un
 * multimetro puesto en los bornes, se reajusta BATTERY_DIODE_DROP_MV: la
 * cuenta del ADC y del divisor es exacta segun la hoja de datos, y la
 * caida del diodo es el unico numero a ojo que hay en este fichero.
 */
/*
 * EL VOLTAJE DE REFERENCIA YA NO SE SUPONE: SE MIDE - 12/10/2026.
 *
 * Tiene razon, y el comentario que habia justo encima de la cuenta ya lo
 * admitia: "NOT VREFINT-calibrated, so this inherits whatever tolerance
 * that rail has". Lo que no decia -y es lo gordo- es que la constante era
 * 3300 mV sobre un regulador de TRES voltios. O sea que al error de
 * unidad a unidad se le sumaba un error sistematico de un diez por
 * ciento en todas.
 *
 * COMO SE ARREGLA. El GD32F450 lleva dentro una referencia de 1,2 V en el
 * canal 17 del ADC. Esa referencia NO depende del regulador de la placa,
 * asi que midiendola contra el propio ADC sale lo que de verdad vale el
 * raíl:
 *
 *     VDDA = 1200 mV * 4095 / (lectura del canal 17)
 *
 * y a partir de ahi todas las demas medidas se hacen contra ese numero
 * medido en vez de contra una constante. Arregla de paso el termometro
 * del chip, que hacia la misma cuenta con el mismo 3300.
 *
 * LO QUE ESTO NO ARREGLA, Y HAY QUE DECIRLO. La referencia interna
 * tambien tiene tolerancia: 1,2 V es el valor TIPICO y de un chip a otro
 * se mueve unas centesimas. O sea que esto cambia un error de un diez por
 * ciento mas dispersion del regulador por una dispersion de un dos o tres
 * por ciento, que es mucho mejor pero no es un calibrador. Para afinar
 * mas hace falta un punto de calibracion del usuario - ver
 * battery_calib_pon()-.
 */
#define BATTERY_VREFINT_MV  1200U

/*
 * EL VALOR DE VUELTA SI LA REFERENCIA NO CONTESTA. Es el nominal del
 * regulador de esta placa (3 V), no los 3,3 de antes: si la medida falla,
 * mejor equivocarse por la tolerancia del regulador que por un 10 % fijo.
 */
#define BATTERY_VDDA_PORSI  3000U

/*
 * LA CAIDA DEL DIODO VUELVE A SER LA DE VERDAD - 12/10/2026.
 *
 * Estaba en 400 mV con el comentario "Ajustado aquí a ojo....", y ahora
 * se entiende por que: con la referencia 300 mV por encima de la real, la
 * lectura del pin salia un 10 % alta y luego se multiplica por cuatro.
 * Bajar el diodo de 700 a 400 tapaba parte de ese error. Eran DOS errores
 * que se restaban, y eso es peor que un error solo: en cuanto se arregla
 * uno, el otro aparece entero y parece que el arreglo ha empeorado las
 * cosas.
 *
 * Con la referencia medida, el diodo vuelve a ser lo que es: la caida
 * tipica de un diodo de silicio en serie. Sigue siendo una aproximacion
 * -un diodo real cae menos a poca corriente y mas en frio- y por eso
 * existe el ajuste del usuario.
 */
#define BATTERY_DIODE_DROP_MV 700U

/*
 * EL AJUSTE DEL USUARIO, EN MILIVOLTIOS - 12/10/2026.
 *
 * Lo que queda despues de medir la referencia son dos cosas que NO se
 * pueden saber desde dentro del chip: cuanto vale de verdad la referencia
 * interna de ESTA unidad, y cuanto cae de verdad ESE diodo. Las dos se
 * resuelven con un multimetro y un numero.
 *
 * Se guarda con los demas ajustes y se suma al final. El rango va de
 * -500 a +500 mV, que cubre de sobra las dos tolerancias juntas; mas que
 * eso no seria calibrar, seria tapar una averia.
 */
#define BATTERY_CALIB_MIN  (-500)
#define BATTERY_CALIB_MAX   (500)

void    battery_calib_pon(int16_t mv);
int16_t battery_calib_lee(void);

/*
 * LO QUE VALE EL RAIL AHORA MISMO, en milivoltios, medido contra la
 * referencia interna. Devuelve BATTERY_VDDA_PORSI si el ADC no contesta.
 *
 * Esta en la cabecera porque la fila de Informacion lo enseña: un numero
 * que explica por que la bateria marca lo que marca vale mas que el de la
 * bateria solo.
 */
uint16_t battery_vdda_mv(void);

/*
 * EL NUMERO CRUDO DEL CONVERSOR PARA LA REFERENCIA INTERNA, 0..4095.
 *
 * Lo enseña la fila "Calibrar voltaje" a proposito: la primera version de
 * esto caia en el valor de reserva sin decirlo, y en pantalla 3,00 V de
 * reserva y 3,00 V medidos se escriben igual. Con el crudo al lado se
 * distinguen de un vistazo: 0 es "el conversor no contesta", y un numero
 * fuera de 900..2600 es "contesta algo que no puede ser la referencia".
 */
uint16_t battery_vrefint_crudo(void);

/*
 * LAS CUENTAS, SIN EL ADC. Entra un numero crudo del conversor y sale uno
 * con unidades. Las usa el banco del PC (sim/bateria.c) para medir lo que
 * de verdad se puede equivocar aqui: las cuentas y los topes.
 */
uint16_t battery_vdda_de_raw(uint16_t raw);
uint16_t battery_mv_de_raw(uint16_t raw, uint16_t vdda_mv, int16_t calib_mv);
int16_t  battery_temp_de_raw(uint16_t raw, uint16_t vdda_mv);

/* Configure ADC0 for a single-channel, software-triggered, blocking
 * VBAT read (channel 18, longest sample time - see battery.c for why
 * that's fine here). Call once at boot, alongside the other
 * peripheral _init() calls in main.c - no dependency on I2C/I2S/the
 * audio chain, so it can go anywhere in that sequence. */
void battery_init(void);

/* Una conversion, esperando a que acabe: unos pocos microsegundos, ver
 * battery.c. Lee VBAT ahora mismo, deshace el divisor por cuatro de
 * dentro del chip y suma BATTERY_DIODE_DROP_MV. NO vale desde una
 * interrupcion: hoy no comparte el ADC0 con nadie, pero si eso cambia
 * algun dia, que no se llame desde la interrupcion de demodulacion. Esta
 * pensado para el bucle principal, mirado de vez en cuando desde donde
 * main.c llama a battery_display_draw(). */
uint16_t battery_get_millivolts(void);

/*
 * Convenience wrapper: maps battery_get_millivolts() onto 0..100 for
 * the UI gauge, linearly between BATTERY_EMPTY_MV and BATTERY_FULL_MV
 * (see battery.c - both currently a single-cell Li-ion/Li-Po
 * placeholder range, 3.0-4.2V, NOT confirmed against this board's
 * actual pack). Adjust those two constants once the real
 * chemistry/cell count is known; nothing else needs to change.
 */
uint8_t battery_get_percent(void);

/* El mismo porcentaje a partir de unos milivoltios que ya se tienen, sin
 * volver a medir. Ver battery_pct_de_mv() en battery.c. */
uint8_t battery_pct_de_mv(uint16_t mv);

/*
 * Temperatura del SENSOR INTERNO DEL MICRO, en grados enteros.
 *
 * Mide el die del GD32F450, no la habitacion: con la radio encendida da
 * bastante mas que la temperatura ambiente, y esa diferencia es normal.
 * Sirve para ver si el aparato se esta calentando de mas, que es para lo
 * que esta, no para saber que tiempo hace.
 *
 * *** SIN CALIBRAR Y SIN COMPROBAR CONTRA UN TERMOMETRO ***. Usa los
 * valores tipicos de la hoja de datos (1,45 V a 25 grados y 4,1 mV por
 * grado) y estos sensores se van facilmente diez grados de un chip a
 * otro sin una calibracion por unidad. O sea: la TENDENCIA es fiable,
 * el numero absoluto no. Por eso en pantalla pone "del chip" y no
 * "temperatura" a secas.
 *
 * Comparte el ADC0 con la medida de bateria y cambia de canal para leer,
 * dejandolo como estaba al terminar. Solo desde el bucle principal,
 * igual que battery_get_millivolts().
 */
int16_t battery_get_chip_temp_c(void);

#endif /* BATTERY_H */
