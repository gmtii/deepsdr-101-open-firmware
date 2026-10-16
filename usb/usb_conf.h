/*!
    \file    usb_conf.h
    \brief   USB core driver basic configuration

    \version 2026-02-05, V3.3.3, firmware for GD32F4xx
*/

/*
    Copyright (c) 2026, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this 
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, 
       this list of conditions and the following disclaimer in the documentation 
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors 
       may be used to endorse or promote products derived from this software without 
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
OF SUCH DAMAGE.
*/

#ifndef USB_CONF_H
#define USB_CONF_H

#include "gd32f4xx.h"
/*
 * EL INCLUDE DE LA PLACA DE EVALUACION, FUERA - 09/10/2026.
 *
 * El fichero original del fabricante incluye "gd32f450i_eval.h", que es la
 * cabecera de SU placa de demostracion y aqui no existe. O sea que este
 * fichero, tal y como venia, no compilaba: el cargador tenia su propia
 * copia en boot/usb_conf.h con esa linea quitada, y esta se quedo ahi sin
 * que la leyera nadie.
 *
 * Ahora es UNA sola, y la usan el cargador y la aplicacion. Dos copias de
 * la configuracion del USB que se separan solas son exactamente la averia
 * que este proyecto ya ha pagado siete veces; que una de las dos ni
 * compilara no la hacia inofensiva, la hacia invisible.
 */

/* USB FS/HS PHY CONFIGURATION */
#ifdef USE_USB_FS
    #define USB_FS_CORE
#endif /* USE_USB_FS */

#ifdef USE_USB_HS
    #define USB_HS_CORE
#endif /* USE_USB_HS */

/* USB FIFO size config */
#ifdef USB_FS_CORE
    /*
     * EL REPARTO DE LA MEMORIA DEL PERIFERICO - 09/10/2026.
     *
     * Estos numeros son PALABRAS DE 32 BITS de la memoria que el USBFS
     * lleva dentro -1,25 kB, o sea 320 palabras- y NO salen de la RAM del
     * sistema. Los tres originales suman 128+64+128 = 320: exactamente
     * todo, ni una palabra libre.
     *
     * El disco del cargador usa dos endpoints y le vale asi. El puerto
     * serie de la aplicacion usa TRES -los dos bulk y el de interrupcion
     * de CDC, que no se usa nunca pero tiene que existir-, y el tercero
     * necesita su propia cola de salida. Sin ella, el nucleo configura un
     * endpoint con cero sitio y lo que llega es que el equipo enumera y no
     * transfiere.
     *
     * Por eso cada uno va bajo un #ifndef: la aplicacion le quita 16
     * palabras al bulk y se las da al de interrupcion desde su propio
     * Makefile, y EL CARGADOR NO SE ENTERA - su binario sigue saliendo byte
     * a byte identico, que es justo lo que no se puede tocar a la ligera.
     *
     * El reparto de la aplicacion (ver reglas.mk):
     *   RX 128   TX0 64   TX1 112   TX2 16   = 320
     * El bulk se queda con 448 bytes de cola, que son siete tramas de 64:
     * de sobra para un puerto de ordenes de texto.
     */
    #ifndef RX_FIFO_FS_SIZE
    #define RX_FIFO_FS_SIZE                         128U
    #endif
    #ifndef TX0_FIFO_FS_SIZE
    #define TX0_FIFO_FS_SIZE                        64U
    #endif
    #ifndef TX1_FIFO_FS_SIZE
    #define TX1_FIFO_FS_SIZE                        128U
    #endif
    #ifndef TX2_FIFO_FS_SIZE
    #define TX2_FIFO_FS_SIZE                        0U
    #endif
    #ifndef TX3_FIFO_FS_SIZE
    #define TX3_FIFO_FS_SIZE                        0U
    #endif

    /*
     * Y QUE SUMEN LO QUE HAY, NI UNA PALABRA MAS.
     *
     * La memoria del USBFS son 320 palabras y punto. Pasarse no da un
     * error: el nucleo configura las colas unas encima de otras y lo que
     * sale es un equipo que enumera y transfiere basura, o que funciona
     * hasta que se usan dos endpoints a la vez. Quedarse corto es peor
     * todavia, porque sobra memoria sin usar y el endpoint que se quedo
     * sin cola NAKea para siempre.
     *
     * Se comprueba al compilar porque es lo unico que se puede: este
     * reparto no se puede medir desde el ordenador y en la radio solo se
     * ve como "a veces falla".
     */
    _Static_assert(RX_FIFO_FS_SIZE + TX0_FIFO_FS_SIZE + TX1_FIFO_FS_SIZE
                   + TX2_FIFO_FS_SIZE + TX3_FIFO_FS_SIZE == 320U,
                   "las colas del USBFS tienen que sumar 320 palabras");

    #define USBFS_SOF_OUTPUT                        0U
    #define USBFS_LOW_POWER                         0U
#endif /* USB_FS_CORE */

#ifdef USB_HS_CORE
    #define RX_FIFO_HS_SIZE                         512U
    #define TX0_FIFO_HS_SIZE                        128U
    #define TX1_FIFO_HS_SIZE                        384U
    #define TX2_FIFO_HS_SIZE                        0U
    #define TX3_FIFO_HS_SIZE                        0U
    #define TX4_FIFO_HS_SIZE                        0U
    #define TX5_FIFO_HS_SIZE                        0U

    #ifdef USE_ULPI_PHY
        #define USB_ULPI_PHY_ENABLED
    #endif

    #ifdef USE_EMBEDDED_PHY
        #define USB_EMBEDDED_PHY_ENABLED
    #endif

//    #define USB_HS_INTERNAL_DMA_ENABLED
//    #define USB_HS_DEDICATED_EP1_ENABLED

    #define USBHS_SOF_OUTPUT                        0U
    #define USBHS_LOW_POWER                         0U
#endif /* USB_HS_CORE */

/* if uncomment it, need jump to USB JP */
//#define VBUS_SENSING_ENABLED

//#define USE_HOST_MODE
#define USE_DEVICE_MODE
//#define USE_OTG_MODE

#ifndef USB_FS_CORE
    #ifndef USB_HS_CORE
        #error  "USB_HS_CORE or USB_FS_CORE should be defined!"
    #endif
#endif

#ifndef USE_DEVICE_MODE
    #ifndef USE_HOST_MODE
        #error  "USE_DEVICE_MODE or USE_HOST_MODE should be defined!"
    #endif
#endif

#ifndef USE_USB_HS
    #ifndef USE_USB_FS
        #error  "USE_USB_HS or USE_USB_FS should be defined!"
    #endif
#endif

/* all variables and data structures during the transaction process should be 4-bytes aligned */

#ifdef USB_HS_INTERNAL_DMA_ENABLED
    #if defined (__GNUC__)         /* GNU Compiler */
        #define __ALIGN_END __attribute__ ((aligned (4U)))
        #define __ALIGN_BEGIN
    #else
        #define __ALIGN_END

        #if defined (__CC_ARM)     /* ARM Compiler */
            #define __ALIGN_BEGIN __align(4U)
        #elif defined (__ICCARM__) /* IAR Compiler */
            #define __ALIGN_BEGIN
        #elif defined (__TASKING__)/* TASKING Compiler */
            #define __ALIGN_BEGIN __align(4U)
        #endif /* __CC_ARM */
    #endif /* __GNUC__ */
#else
    #define __ALIGN_BEGIN
    #define __ALIGN_END
#endif /* USB_HS_INTERNAL_DMA_ENABLED */

/* __packed keyword used to decrease the data type alignment to 1-byte */
#if defined (__GNUC__)       /* GNU Compiler */
    #ifdef __packed
        #undef __packed
    #endif
    #define __packed
#elif defined (__TASKING__)    /* TASKING Compiler */
    #define __packed __unaligned
#endif /* __GNUC__ */

#endif /* USB_CONF_H */
