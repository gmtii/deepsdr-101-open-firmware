# El cargador de arranque. Se compila SOLO, con su propio linker script.
#   make -C boot          -> boot/customboot.bin
FW      := ..
CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
CFLAGS  := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
           -DGD32F450 -DUSE_STDPERIPH_DRIVER -DHXTAL_VALUE=12288000U \
           -DDEBUG_UART_ENABLED=0 -DUSE_USB_FS -DCARGADOR_ARRANQUE \
           -I$(FW)/CMSIS/Include -I$(FW)/CMSIS/GD/GD32F4xx/Include \
           -I$(FW)/Firmware/Include -I$(FW)/User -I. \
           -I$(FW)/usb \
           -I$(FW)/usb/GD32F4xx_usb_library/driver/Include \
           -I$(FW)/usb/GD32F4xx_usb_library/device/core/Include \
           -I$(FW)/usb/GD32F4xx_usb_library/device/class/msc/Include \
           -I$(FW)/usb/GD32F4xx_usb_library/ustd/common \
           -I$(FW)/usb/GD32F4xx_usb_library/ustd/class/msc \
           -Wall -Os -ffunction-sections -fdata-sections -g3
LDFLAGS := -TGD32F450VE_BOOT.ld -Wl,--gc-sections -nostartfiles -specs=nano.specs

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

#
# Y LAS CABECERAS, QUE NO ESTABAN - 16/10/2026.
#
# Las dos reglas dependian de $(SRC), $(ASM) y el .ld, y de ninguna .h. Con
# lo cual: editas User/cargador.h -que es DONDE VIVE CARGA_BORRA_HASTA, el
# numero que decide cuanta flash se borra-, lanzas make -C boot f470, y
# make contesta "Nothing to be done". Lo que grabas es el binario de antes.
#
# En un fichero cualquiera eso es una compilacion perdida. Aqui no: un
# cargador atrasado se GRABA, y es el mismo argumento con el que se puso la
# copia automatica a la raiz unas lineas mas abajo.
#
# No hay -MMD aqui porque esto compila y enlaza de una sola pasada, sin .o
# intermedios, asi que no hay donde colgar los .d. Se listan a mano las
# cabeceras de las que depende de verdad: las de boot/ y las de User/ que
# incluye. Es una lista con comodin, asi que una cabecera nueva entra sola.
#
HDR := $(wildcard *.h) $(wildcard $(FW)/User/*.h)

all: customboot.bin
customboot.elf: $(SRC) $(ASM) $(HDR) GD32F450VE_BOOT.ld reglas.mk
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-Map=$(@:.elf=.map) $(SRC) $(ASM) -o $@
	arm-none-eabi-size $@
customboot.bin: customboot.elf
	$(OBJCOPY) -O binary $< $@
# El sello: version, tamaño y CRC del contenido, mas el aviso si esa
# version ya salio con otro contenido. Ver tools/bootsello.py - y el
# comentario de CARGADOR_VERSION en boot_main.c, que es el porque.
	@python3 ../tools/bootsello.py customboot.bin boot_main.c customboot.map
# Y LA COPIA DE LA RAIZ, AQUI Y NO A MANO - 08/10/2026.
#
# En la raiz hay una copia de este fichero: es la comoda, la que se arrastra
# para flashear. Se copiaba a mano, asi que se quedo atras, y el README
# llego a mandar grabar una TERCERA copia -la de rescate/- que llevaba dos
# versiones de retraso. Un cargador atrasado no se lee raro: se GRABA.
#
# tools/copias_bin_check.py vigila que no se separen; esto hace que no se
# puedan separar, que es mejor que vigilarlo. rescate/ no se toca: esa esta
# congelada a proposito.
	@cp -f customboot.bin ../customboot.bin
	@echo "  y copiado a la raiz (la que se arrastra para flashear)"
#
# EL CARGADOR DEL GD32F470VGT6 - 16/10/2026.
#
# Pedido por el usuario.
#
# Mismo codigo con CHIP_F470VG puesto. Lo que cambia esta en
# User/cargador.h (CARGA_BORRA_HASTA: 1024 kB en vez de 512) y en la cadena
# de sectores de boot_main.c, que con el 470 llega al 11. El linker script
# es el mismo: el cargador vive en los primeros 48 kB, que los hay en los
# dos chips.
#
#   make -C boot f470   ->  boot/customboot_470.bin
#
# No se copia a la raiz: la de la raiz es la que se arrastra por defecto y
# tiene que seguir siendo la del chip de serie. Esta se coge por su nombre,
# que es justo lo que hace falta para no equivocarse.
#
.PHONY: f470
f470: customboot_470.bin

customboot_470.elf: $(SRC) $(ASM) $(HDR) GD32F450VE_BOOT.ld reglas.mk
	$(CC) $(CFLAGS) -DCHIP_F470VG $(LDFLAGS) -Wl,-Map=$(@:.elf=.map) \
	      $(SRC) $(ASM) -o $@
	arm-none-eabi-size $@

customboot_470.bin: customboot_470.elf
	$(OBJCOPY) -O binary $< $@
	@python3 ../tools/bootsello.py customboot_470.bin boot_main.c \
	         customboot_470.map " 470"

clean: ; rm -f customboot.elf customboot.bin customboot.map \
                customboot_470.elf customboot_470.bin customboot_470.map
