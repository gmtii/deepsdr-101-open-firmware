#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>

/*
 * EL PUERTO SERIE POR USB, Y EL CAT POR DENTRO.
 *
 * Enchufando la radio al ordenador sale un puerto serie -"COMx" en Windows,
 * /dev/ttyACM0 en Linux- por el que habla el CAT de Kenwood (ver cat.c). Con
 * eso, WSJT-X, JTDX, fldigi y cualquier cosa que use hamlib manejan la radio
 * sin que nadie escriba un driver.
 *
 * SE ARRANCA A MANO, Y ES A PROPOSITO
 * -----------------------------------
 * usb_cdc_arranca() no se llama sola al encender. Todo lo que hay debajo
 * -el PLLSAI, el nucleo USB del fabricante, la interrupcion- es codigo que
 * NO se ha podido probar en el ordenador: los descriptores y los anillos si
 * (hay bancos), pero el nucleo y el hardware solo los juzga la radio.
 *
 * Arrancandolo desde una fila de Informacion, si algo de eso se porta mal
 * la radio arranca normal y basta con no tocar la fila. Arrancandolo en el
 * encendido, un fallo ahi deja la radio sin arrancar y la unica salida es
 * volver a grabar.
 *
 * Y HAY QUE DESENCHUFAR EL CABLE PARA GRABAR
 * ------------------------------------------
 * El cargador usa el mismo periferico para montar el disco. Son momentos
 * distintos -el cargador corre antes que esto- asi que no se pelean, pero
 * conviene saberlo: con el puerto serie abierto en el ordenador, el
 * arrastrar-y-soltar del cargador sigue funcionando igual tras reiniciar.
 */

/* Arranca el USB. Devuelve 0 si el reloj de 48 MHz no se pudo montar, y
 * entonces no se ha tocado nada mas. Idempotente. */
uint8_t usb_cdc_arranca(void);

/*
 * Lo desconecta sin desmontarlo: para el ordenador es igual que tirar del
 * cable. usb_cdc_arranca() lo vuelve a enchufar.
 */
void usb_cdc_para(void);

/* 1 si ya se ha arrancado, con cable o sin el. */
uint8_t usb_cdc_encendido(void);

/* 1 si esta montado pero desconectado a mano. */
uint8_t usb_cdc_parado(void);

/* 1 si el ordenador ha terminado de enumerar y hay puerto abierto. */
uint8_t usb_cdc_conectado(void);

/* Desde el bucle principal: mueve lo que haya entre los anillos y el CAT.
 * Barato si no hay nada. */
void usb_cdc_poll(void);

/* Cuantas veces ha entrado la interrupcion. Para la fila de Informacion:
 * si esto no sube al enchufar el cable, el problema es el cable o el
 * reloj, no el protocolo. */
uint32_t usb_cdc_isrs(void);

/*
 * Le da audio al USB. Se llama DESDE LA INTERRUPCION del audio, con el
 * bloque estereo entrelazado que va al altavoz y la frecuencia a la que
 * sale. Si nadie esta grabando se sale en dos comparaciones.
 *
 * La frecuencia tiene que ser multiplo de 48 kHz; si no, no manda nada.
 * Ver el porque en el .c.
 */
void usb_cdc_audio(const int16_t *estereo, uint32_t n, uint32_t fs_hz);

/* 1 si el ordenador tiene la grabacion abierta. */
uint8_t usb_cdc_audio_on(void);

/* Tramas de audio que salieron en silencio porque no habia datos. Cero es
 * lo normal; si sube, el audio llega tarde y se oye. */
uint32_t usb_cdc_huecos(void);

/* Cuantos bytes se han tirado por no caber en el anillo de salida. */
uint32_t usb_cdc_perdidos(void);

#endif /* USB_CDC_H */
