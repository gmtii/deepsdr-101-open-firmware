/*
 * usbd_conf.h - USB device configuration for the DeepSDR 101's CDC-ACM
 * virtual COM port.
 *
 * Unchanged from GigaDevice's CDC_ACM demo except for the comments -
 * these values (endpoint assignment, packet sizes) are board-agnostic,
 * they just describe the CDC class layout, not any EVAL-board specifics.
 */

#ifndef USBD_CONF_H
#define USBD_CONF_H

#include "usb_conf.h"

/* USB configure exported defines */
#define USBD_CFG_MAX_NUM                    1U
#define USBD_ITF_MAX_NUM                    1U

#define CDC_COM_INTERFACE                   0U

#define USB_STR_DESC_MAX_SIZE               255U

#define CDC_DATA_IN_EP                      EP1_IN  /* EP1 for data IN (device -> host) */
#define CDC_DATA_OUT_EP                     EP3_OUT /* EP3 for data OUT (host -> device) */
#define CDC_CMD_EP                          EP2_IN  /* EP2 for CDC notifications (line coding etc) */

#define USB_STRING_COUNT                    4U

#define USB_CDC_CMD_PACKET_SIZE             8U    /* Control endpoint packet size */

/* Kept modest - this is a command/telemetry channel (ESP32 time sync,
 * future control commands), not a bulk data pipe, so there's no need
 * to size this for throughput. */
#define APP_RX_DATA_SIZE                    2048U

/* USBFS full-speed bulk endpoints are always 64 bytes max */
#define USB_CDC_DATA_PACKET_SIZE            64U   /* Endpoint IN & OUT packet size */
#define CDC_IN_FRAME_INTERVAL               5U    /* Number of frames between IN transfers */

#endif /* USBD_CONF_H */
