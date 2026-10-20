#ifndef USB_PATILLAS_H
#define USB_PATILLAS_H

#include <stdint.h>

/*
 * EN QUE PATILLAS DEL GD32 ESTA EL CONECTOR GRANDE, PREGUNTANDOSELO A LA
 * RADIO EN VEZ DE A UNA FOTO.
 *
 * POR QUE HACE FALTA SABERLO, Y POR QUE NO VALE LA FOTO
 * -----------------------------------------------------
 * Para grabar en un pincho hace falta que el GD32 haga de ANFITRION, y para
 * eso las lineas D+/D- del conector grande tienen que llegar a un periferico
 * USB. En este integrado -LQFP100- solo hay dos sitios donde eso es posible:
 *
 *     USBFS  ->  PA11 (D-) / PA12 (D+)      patillas 70 y 71
 *     USBHS  ->  PB14 (D-) / PB15 (D+)      patillas 53 y 54
 *
 * Y EL SEGUNDO ESTA OCUPADO. PB12/13/14/15 son el bus I2S del codec (ver
 * gd32_i2s.c): PB15 es la linea por la que entran las muestras I/Q de la
 * radio y PB14 la que saca el audio al altavoz. O sea que el USB de alta
 * velocidad no existe en esta placa aunque el chip lo tenga.
 *
 * Luego lo unico que puede haber es USBFS, en PA11/PA12 - las mismas dos
 * patillas que usa el conector pequeño para grabar arrastrando el fichero.
 * La pregunta pasa a ser una sola: ¿el conector grande ESTA YA colgado de
 * esas dos, o va a otro sitio?
 *
 * La foto de la placa no lo contesta: las pistas se meten en un abanico de
 * cien patillas a medio milimetro de paso y ahi no se cuenta nada. Pero la
 * radio si lo puede contestar sola, sin polimetro y sin abrirla.
 *
 * COMO SE MIDE
 * ------------
 * Un dispositivo USB lleva de fabrica una resistencia de 1,5 kOhm de una de
 * sus dos lineas a 3,3 V: de D+ si es de velocidad plena y de D- si es de
 * baja. Es asi como el anfitrion se entera de que han enchufado algo.
 *
 * Asi que: PA11 y PA12 como ENTRADA con la resistencia de bajada del propio
 * GD32 (unos 40 kOhm). Sin nada enchufado las dos leen 0. Con un pincho
 * dentro, si sus lineas llegan hasta aqui, el divisor 1,5 k / 40 k deja la
 * patilla en 3,2 V y esa lee 1.
 *
 *     las dos a 0  ->  el conector grande NO llega a PA11/PA12
 *     una a 1      ->  SI llega, y ademas dice cual es cual
 *
 * Esto no toca ningun periferico: la aplicacion no usa el USB para nada
 * (lo unico que hay es dfu_salto.c, que mueve PA12 un instante justo antes
 * de saltar al cargador), asi que las dos patillas estan libres.
 *
 * LO QUE SE DECIDE CON EL RESULTADO
 * ---------------------------------
 * Si lee "D+ en PA12", el conector grande ya esta donde tiene que estar y
 * el anfitrion USB es SOLO firmware: ni cortar, ni soldar, ni abrir nada.
 *
 * Si lee "nada", hacen falta los dos cables del mod - pero NO los dos
 * cortes que lleva dibujados, porque esos cortes dejan al conector pequeño
 * sin D+/D- y con el se va la unica forma de grabar esta radio.
 */

enum {
    USB_PAT_NADA = 0U,  /* las dos abajo: no llega ninguna resistencia */
    USB_PAT_DP   = 1U,  /* PA12 arriba: dispositivo de velocidad plena */
    USB_PAT_DM   = 2U,  /* PA11 arriba: dispositivo de baja velocidad */
    USB_PAT_LAS2 = 3U   /* las dos arriba: eso no es un USB enchufado */
};

/*
 * Deja PA9, PA10, PA11 y PA12 como ENTRADA SIN RESISTENCIA, que es su
 * estado de reposo. Idempotente. usb_pat_init() es lo mismo, y esta solo
 * porque ya habia llamadas con ese nombre.
 */
void usb_pat_parque(void);
void usb_pat_init(void);

/* Lee las dos patillas AHORA. Devuelve uno de los USB_PAT_*. */
uint8_t usb_pat_lee(void);

/*
 * Y SI NO LLEGA, ¿A DONDE LLEGA? - 09/10/2026.
 *
 * Tiene razon, y la respuesta estaba en el propio codigo de la radio. Lo
 * que no se puede es mirarlo en una foto de la placa.
 *
 * El firmware configura estas patillas, y de estas se sabe para que son:
 *
 *   PA1 PA2 PA5      filtro de entrada (rf_lpf.c)
 *   PA3              retroiluminacion
 *   PA6 PA7          el oscilador por debajo de 300 kHz
 *   PA8              el amplificador del altavoz
 *   PA9              consola serie
 *   PA11 PA12        USBFS - el conector pequeño
 *   PB3 PB5 PB6      flash SPI externa
 *   PB4              flash SPI + tactil
 *   PB7              altavoz
 *   PB8 PB9          I2C del codec y del sintetizador
 *   PB11             reset del codec
 *   PB12..PB15       bus I2S: por PB15 entran las muestras I/Q
 *   PC6              reloj maestro del codec
 *   PC7              reset de la pantalla
 *   PC9  PD12 PD13   el mando giratorio
 *   PD2 PD3 PD6      tactil
 *   PD0..PD15 PE7..PE15   bus EXMC de la pantalla
 *
 * Las que NO aparece configurando NADIE son estas, y por eliminacion las
 * dos del conector grande tienen que estar entre ellas:
 *
 *   PA0 PA4 PA10 PA15
 *   PB0 PB1 PB2 PB10
 *   PC0..PC5 PC8 PC10..PC13
 *   PE0..PE6
 *
 * (Quedan fuera a proposito PA13/PA14, que son el puerto de depuracion, y
 * PC14/PC15, que son el cristal del reloj.)
 *
 * usb_pat_barre() las pone TODAS como entrada con bajada, las lee con el
 * pincho puesto, y vuelve a dejar cada una exactamente como estaba -se
 * guardan los registros CTL y PUD antes de tocar nada-. La que se ponga a
 * 1 es una de las dos del conector, y se dice su nombre.
 *
 * Esto no necesita polimetro, ni abrir la radio, ni contar patillas en una
 * foto: la patilla se nombra sola.
 */

/* Cuantas patillas como mucho se nombran en el texto. */
#define USB_PAT_MAX_NOMBRES  4U

/*
 * Barre las patillas que el firmware no usa y escribe en `texto` los
 * nombres de las que esten a 1 ("PC4 PC5"), o cadena vacia si ninguna.
 * `n` es el tamaño de `texto`, que necesita 24 bytes para el peor caso.
 * Devuelve cuantas se encontraron (puede ser mas de las que caben).
 *
 * Deja cada patilla como estaba. No toca ninguna de las que el firmware
 * configura, asi que se puede llamar con la radio funcionando.
 */
uint8_t usb_pat_barre(char *texto, uint8_t n);

#endif /* USB_PATILLAS_H */
