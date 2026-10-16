/*
 * Lo minimo de la libreria de GigaDevice para que disco_msc.c compile en
 * el PC. Copiado de usb/GD32F4xx_usb_library/device/class/msc/Include/
 * usbd_msc_mem.h: la misma estructura y el mismo largo de INQUIRY.
 */
#ifndef USBD_MSC_MEM_H
#define USBD_MSC_MEM_H
#include <stdint.h>
#define USBD_STD_INQUIRY_LENGTH   36
#define MEM_LUN_NUM               1
typedef struct {
    int8_t (*mem_init)(uint8_t lun);
    int8_t (*mem_ready)(uint8_t lun);
    int8_t (*mem_protected)(uint8_t lun);
    int8_t (*mem_read)(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
    int8_t (*mem_write)(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
    int8_t (*mem_maxlun)(void);
    uint8_t *mem_inquiry_data[MEM_LUN_NUM];
    uint32_t mem_block_size[MEM_LUN_NUM];
    uint32_t mem_block_len[MEM_LUN_NUM];
} usbd_mem_cb;
extern usbd_mem_cb *usbd_mem_fops;
#endif
