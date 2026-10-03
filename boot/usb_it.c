/*
 * LAS DOS INTERRUPCIONES QUE PIDE LA LIBRERIA USB DE GIGADEVICE.
 *
 * El ejemplo msc_udisk trae un gd32f4xx_it.c con los diez manejadores del
 * proyecto de la placa de evaluacion. Aqui hacen falta dos: la del USBFS y
 * la del TIMER2, que es el que la libreria usa para sus retardos
 * (usb_timer_init() en gd32f4xx_hw.c).
 *
 * Se reescriben en vez de copiar el fichero entero para que se vea que el
 * cargador NO tiene ningun otro manejador: si salta cualquier otra cosa,
 * es un fallo y no algo que hemos enganchado sin querer.
 */
#include "drv_usbd_int.h"
#include "drv_usb_hw.h"
#include "drv_usb_hw.h"

extern usb_core_driver g_usb;

/* Contador para la linea de diagnostico de modo_actualizacion(). Sin el,
 * "no aparece el USB" no distingue entre "no llega ni una interrupcion" y
 * "llegan todas pero algo mas abajo falla". */
volatile uint32_t g_usb_isr;

void USBFS_IRQHandler(void) { g_usb_isr++; usbd_isr(&g_usb); }
extern void usb_timer_irq(void);   /* en usb_hw.c; no esta en drv_usb_hw.h */
void TIMER2_IRQHandler(void) { usb_timer_irq(); }
