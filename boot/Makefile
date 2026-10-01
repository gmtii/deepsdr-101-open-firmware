# El cargador de arranque. Se compila SOLO, con su propio linker script.
#   make -C boot          -> boot/boot_nuestro.bin
FW      := ..
CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
CFLAGS  := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
           -DGD32F450 -DUSE_STDPERIPH_DRIVER -DHXTAL_VALUE=12288000U \
           -DDEBUG_UART_ENABLED=0 -DUSE_USB_FS -DCARGADOR_ARRANQUE \
           -I$(FW)/CMSIS/Include -I$(FW)/CMSIS/GD/GD32F4xx/Include \
           -I$(FW)/Firmware/Include -I$(FW)/User -I. \
           -I$(FW)/usb/GD32F4xx_usb_library/driver/Include \
           -I$(FW)/usb/GD32F4xx_usb_library/device/core/Include \
           -I$(FW)/usb/GD32F4xx_usb_library/device/class/msc/Include \
           -I$(FW)/usb/GD32F4xx_usb_library/ustd/common \
           -I$(FW)/usb/GD32F4xx_usb_library/ustd/class/msc \
           -Wall -Os -ffunction-sections -fdata-sections -g3
LDFLAGS := -TGD32F450VE_BOOT.ld -Wl,--gc-sections -nostartfiles -specs=nano.specs \
           -Wl,-Map=boot_nuestro.map

SRC := boot_main.c soporte.c usb_it.c atajo_dfu.c atajo_gesto.c panel_serie.c \
       disco_msc.c \
       $(wildcard $(FW)/usb/GD32F4xx_usb_library/driver/Source/drv_usb_core.c) \
       $(FW)/usb/GD32F4xx_usb_library/driver/Source/drv_usb_dev.c \
       $(FW)/usb/GD32F4xx_usb_library/driver/Source/drv_usbd_int.c \
       $(FW)/usb/GD32F4xx_usb_library/device/core/Source/usbd_core.c \
       $(FW)/usb/GD32F4xx_usb_library/device/core/Source/usbd_enum.c \
       $(FW)/usb/GD32F4xx_usb_library/device/core/Source/usbd_transc.c \
       $(FW)/usb/GD32F4xx_usb_library/device/class/msc/Source/usbd_msc_core.c \
       $(FW)/usb/GD32F4xx_usb_library/device/class/msc/Source/usbd_msc_bbb.c \
       $(FW)/usb/GD32F4xx_usb_library/device/class/msc/Source/usbd_msc_scsi.c \
       usb_hw.c \
       $(FW)/Firmware/Source/gd32f4xx_timer.c $(FW)/Firmware/Source/gd32f4xx_syscfg.c \
       $(FW)/User/cargador.c $(FW)/User/spi_flash.c $(FW)/User/idioma.c \
       $(FW)/User/rm68120_exmc.c $(FW)/User/disco.c $(FW)/User/fonts_data.c \
       $(FW)/CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c \
       $(FW)/Firmware/Source/gd32f4xx_fmc.c $(FW)/Firmware/Source/gd32f4xx_rcu.c \
       $(FW)/Firmware/Source/gd32f4xx_gpio.c $(FW)/Firmware/Source/gd32f4xx_exmc.c \
       $(FW)/Firmware/Source/gd32f4xx_misc.c $(FW)/Firmware/Source/gd32f4xx_spi.c $(FW)/Firmware/Source/gd32f4xx_pmu.c
ASM := $(FW)/CMSIS/GD/GD32F4xx/Source/GCC/startup_gd32f450_470.S

all: boot_nuestro.bin
boot_nuestro.elf: $(SRC) $(ASM) GD32F450VE_BOOT.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $(ASM) -o $@
	arm-none-eabi-size $@
boot_nuestro.bin: boot_nuestro.elf
	$(OBJCOPY) -O binary $< $@
clean: ; rm -f boot_nuestro.elf boot_nuestro.bin boot_nuestro.map
