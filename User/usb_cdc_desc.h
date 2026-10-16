#ifndef USB_CDC_DESC_H
#define USB_CDC_DESC_H

#include <stdint.h>

/*
 * LOS DESCRIPTORES: UN PUERTO SERIE Y UNA TARJETA DE SONIDO, EN EL MISMO
 * CABLE.
 *
 * *** De lo que publica la radio comercial de referencia: "se ve a la vez
 * como tarjeta de sonido y como un transceptor Kenwood TS-2000. Funciona
 * con WSJT-X, JTDX, hamlib - el programa recibe audio digital limpio y el
 * mismo lee y fija la frecuencia con el modo". ***
 *
 * POR QUE BYTES Y NO LAS ESTRUCTURAS DEL FABRICANTE
 * -------------------------------------------------
 * La libreria trae estructuras con campos con nombre y el MSC del cargador
 * las usa; se leen mejor. Pero son estructuras EMPAQUETADAS, y lo que
 * importa es la cadena de bytes que acaba en el cable: entre una cosa y la
 * otra hay un serializador -el empaquetado del compilador- en el que habria
 * que confiar sin medirlo. Y arrastran medio arbol de cabeceras del GD32,
 * asi que un banco en el ordenador no las podria compilar.
 *
 * Escrito como bytes, lo que hay aqui ES lo que sale por el cable, y este
 * fichero compila tal cual en el PC. El banco (sim/cdctest.c) lo recorre
 * como lo recorre un anfitrion.
 *
 * QUE DESCRIBE, Y POR QUE HACEN FALTA LAS DOS "IAD"
 * -------------------------------------------------
 * Cuatro interfaces en dos funciones:
 *
 *   IAD 0..1     interfaz 0  CDC Communication  + los 4 funcionales
 *                interfaz 1  CDC Data           + 2 bulk de 64
 *   IAD 2..3     interfaz 2  Audio Control      + terminales
 *                interfaz 3  Audio Streaming    + 1 isocrono de entrada
 *                            (con un ajuste alternativo 0 sin endpoints)
 *
 * Los dos descriptores de ASOCIACION son obligatorios en cuanto hay mas de
 * una funcion. Sin ellos Windows ve cuatro interfaces sueltas, no sabe que
 * la 0 y la 1 son la misma cosa, y **no monta el puerto serie**: no da
 * error, simplemente no sale ningun COM. Por lo mismo, la clase del
 * DISPOSITIVO pasa a ser 0xEF/0x02/0x01 ("Miscellaneous / Common / IAD") en
 * vez de 0x02 (CDC): ese trio es el que le dice al anfitrion que mire las
 * asociaciones.
 *
 * EL AJUSTE ALTERNATIVO 0 DE LA INTERFAZ 3, QUE PARECE DE ADORNO
 * -------------------------------------------------------------
 * Es una interfaz de audio SIN endpoints, y es obligatoria. El anfitrion la
 * deja puesta mientras nadie este grabando, y asi el ancho de banda
 * isocrono -que se reserva al configurar, no al usar- no se gasta. Sin
 * ella, el equipo reservaria sus 196 bytes por milisegundo desde que se
 * enchufa, y un concentrador con otras cosas colgando podria rechazarlo
 * entero.
 */

/* --- puerto serie --- */
#define USB_CDC_EP_NOTIF   0x82U   /* interrupcion, entrada */
#define USB_CDC_EP_IN      0x81U   /* bulk, entrada  (radio -> ordenador) */
#define USB_CDC_EP_OUT     0x01U   /* bulk, salida   (ordenador -> radio) */
#define USB_CDC_EP_TAM     64U     /* lo maximo en velocidad plena */

/* --- audio --- */
#define USB_AUD_EP_IN      0x83U   /* isocrono, entrada */
#define USB_AUD_HZ         48000UL
#define USB_AUD_CANALES    2U
#define USB_AUD_BITS       16U
/*
 * 192 bytes es lo justo: 48000 x 2 canales x 2 bytes / 1000 tramas. Se
 * declaran 196 -una muestra estereo mas- porque el reloj de la radio y el
 * del ordenador no son el mismo, y cuando el nuestro va un pelo rapido hay
 * tramas de 49 muestras en vez de 48. Declarando 192 justos, esas se
 * perderian y se oiria un chasquido cada pocos segundos.
 */
#define USB_AUD_EP_TAM     196U

#define USB_CDC_DESC_DEV_LEN     18U
#define USB_CDC_DESC_CONFIG_LEN 174U

extern const uint8_t usb_cdc_desc_dev[USB_CDC_DESC_DEV_LEN];
extern const uint8_t usb_cdc_desc_config[USB_CDC_DESC_CONFIG_LEN];

#endif /* USB_CDC_DESC_H */
