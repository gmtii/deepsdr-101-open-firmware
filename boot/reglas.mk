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

SRC := boot_main.c font_boot.c soporte.c usb_it.c atajo_dfu.c atajo_gesto.c panel_serie.c \
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

# ---------------------------------------------------------------------
# LOS CARGADORES DE PRUEBA DE LAS TRES RAMAS DEL PANEL - 29/10/2026.
#
# *** Un usuario con la pantalla en blanco SIEMPRE, en todos los modos. ***
#
# El cargador elige la rama de arranque del panel leyendo dos registros
# (ver panel_serie.c). Si esa eleccion sale mal, la pantalla se queda en
# blanco y no hay forma de leer la ficha que dice por que - porque la
# ficha se pinta en la pantalla que no se ve.
#
# Asi que se prueba al reves: tres cargadores, cada uno con una rama
# clavada, y se graban de uno en uno hasta que la pantalla hable.
#
#   make -C boot rama RAMA=A   ->  boot/customboot_rama_A.bin
#   make -C boot rama RAMA=B   ->  (lo mismo con la B)
#   make -C boot rama RAMA=C   ->  (y con la C)
#
# NO SE COPIAN A LA RAIZ, igual que el del 470: son de prueba y la de la
# raiz tiene que seguir siendo la buena.
#
# La ficha pone "v2.19 rama A / <crc>", asi que en cuanto una funcione se
# sabe cual es sin preguntar, y el CRC dice que binario es exactamente.
#
# Hubo una pantalla de prueba DENTRO del cargador que cambiaba de rama con
# el mando, y se quito el 05/10/2026: reiniciaba el panel vivo y no es lo
# mismo que arrancarlo en frio - costo cuatro versiones y seguia saliendo
# mal -. Por eso esto son tres binarios y no un menu: cada rama tiene que
# correr desde el arranque, que es la unica forma en que corre de verdad.
#
.PHONY: rama
rama:
	@test -n "$(RAMA)" || { echo "  usa: make -C boot rama RAMA=A|B|C"; exit 1; }
	$(CC) $(CFLAGS) -DPANEL_RAMA_FORZADA="'$(RAMA)'" \
	      -DCARGADOR_VARIANTE='" rama $(RAMA)"' \
	      $(LDFLAGS) -Wl,-Map=customboot_rama_$(RAMA).map \
	      $(SRC) $(ASM) -o customboot_rama_$(RAMA).elf
	$(OBJCOPY) -O binary customboot_rama_$(RAMA).elf customboot_rama_$(RAMA).bin
	@python3 ../tools/bootsello.py customboot_rama_$(RAMA).bin boot_main.c \
	         customboot_rama_$(RAMA).map " rama $(RAMA)"

# EL CARGADOR DE DIAGNOSTICO DEL PANEL - 29/10/2026. Ver su cabecera en
# boot_main.c (#ifdef PRUEBA_PANEL): seis combinaciones de rama y reset,
# la lectura cruda de nueve registros y el veredicto por parpadeos de la
# retroiluminacion, que es el unico canal que queda si el cristal no
# pinta. NO arranca la aplicacion: se sale apagando.
.PHONY: prueba
prueba:
	$(CC) $(CFLAGS) -DPRUEBA_PANEL \
	      -DCARGADOR_VARIANTE='" PRUEBA"' \
	      $(LDFLAGS) -Wl,-Map=customboot_prueba.map \
	      $(SRC) $(ASM) -o customboot_prueba.elf
	$(OBJCOPY) -O binary customboot_prueba.elf customboot_prueba.bin
	@python3 ../tools/bootsello.py customboot_prueba.bin boot_main.c \
	         customboot_prueba.map " PRUEBA"

# La rama B CAPTURADA (k_rama_B, 375 pasos) en vez de la v22 (367). Ver el
# #ifdef RAMA_B_CAPTURADA en panel_serie.c: son dos tablas distintas y la
# capturada -la que de verdad hace el cargador de serie- no ha corrido
# nunca en una radio.
#   make -C boot bcap   ->  boot/customboot_bcap.bin
.PHONY: bcap
bcap:
	$(CC) $(CFLAGS) -DPANEL_RAMA_FORZADA="'B'" -DRAMA_B_CAPTURADA \
	      -DCARGADOR_VARIANTE='" rama Bcap"' \
	      $(LDFLAGS) -Wl,-Map=customboot_bcap.map \
	      $(SRC) $(ASM) -o customboot_bcap.elf
	$(OBJCOPY) -O binary customboot_bcap.elf customboot_bcap.bin
	@python3 ../tools/bootsello.py customboot_bcap.bin boot_main.c \
	         customboot_bcap.map " rama Bcap"
