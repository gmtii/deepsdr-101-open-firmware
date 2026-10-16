#ifndef USB_CDC_HW_H
#define USB_CDC_HW_H

#include <stdint.h>

/* Levanta el PLLSAI y pone el PLL48M a colgar de el. Devuelve 0 si el
 * PLLSAI no engancha, y entonces NO hay que arrancar el USB: ver el
 * comentario en el .c. */
uint8_t usb_cdc_hw_reloj(void);

#endif /* USB_CDC_HW_H */
