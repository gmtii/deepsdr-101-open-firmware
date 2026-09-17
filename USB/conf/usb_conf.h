/*
 * usb_conf.h - USB core driver basic configuration for the DeepSDR 101
 *
 * Adapted from GigaDevice's GD32F4xx USB library CDC_ACM demo
 * (GD32450i-EVAL board). The only real change from the vendor file is
 * removing the "gd32f450i_eval.h" include: that header is the EVAL
 * board's own BSP (LEDs/buttons on that board) and this project has
 * no equivalent nor any need for it - gd32f4xx.h alone is enough for
 * everything usb_conf.h/usbd_conf.h actually use.
 */

#ifndef USB_CONF_H
#define USB_CONF_H

#include "gd32f4xx.h"

/* This board only wires the USBFS (OTG_FS) pins (PA11/PA12) out to
 * the connector - no HS ULPI PHY - so only the FS core is ever
 * selected. Force it here instead of relying on a -D from the
 * Makefile, so this header is self-contained. */
#ifndef USE_USB_FS
    #define USE_USB_FS
#endif

#ifdef USE_USB_FS
    #define USB_FS_CORE
#endif /* USE_USB_FS */

#ifdef USE_USB_HS
    #define USB_HS_CORE
#endif /* USE_USB_HS */

/* USB FIFO size config */
#ifdef USB_FS_CORE
    #define RX_FIFO_FS_SIZE                         128U
    #define TX0_FIFO_FS_SIZE                        64U
    #define TX1_FIFO_FS_SIZE                        64U
    #define TX2_FIFO_FS_SIZE                        64U
    #define TX3_FIFO_FS_SIZE                        0U

    #define USBFS_SOF_OUTPUT                        0U
    #define USBFS_LOW_POWER                         0U
#endif /* USB_FS_CORE */

/* if uncommented, needs a VBUS-sense GPIO wired up - this board
 * doesn't have one, VBUS presence is not monitored */
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

/* all variables and data structures during the transaction process
 * should be 4-bytes aligned. Matches the vendor file's own logic:
 * this only matters for USB_HS_INTERNAL_DMA_ENABLED, which this
 * FS-only board never defines, so both stay empty. */
#define __ALIGN_BEGIN
#define __ALIGN_END

/* __packed keyword used to decrease the data type alignment to
 * 1-byte (GNU compiler - this project is GCC-only, no need for the
 * vendor file's other-toolchain branches) */
#ifdef __packed
    #undef __packed
#endif
#define __packed

#endif /* USB_CONF_H */
