#
# POR QUE ESTE FICHERO EXISTE, Y POR QUE GNUmakefile ES UNA LINEA.
# ================================================================
#
# Pedido por el usuario, 09/10/2026.
#
# El puente que escribe en su disco RECHAZA cualquier fichero que se llame
# "Makefile" -"is a protected file and cannot be written via remote
# tools"-, y el 16/10/2026 se midio que RECHAZA "GNUmakefile" IGUAL. O sea
# que cambiarle el nombre no quitaba la piedra: la movia un metro.
#
# Lo que la quita es este reparto:
#
#   GNU make busca su fichero en este orden:  GNUmakefile, makefile, Makefile.
#
# Asi que GNUmakefile gana y "Makefile" no lo lee nadie. GNUmakefile es UNA
# LINEA -include reglas.mk- que no va a cambiar nunca, y todo lo que haya
# que tocar vive aqui, que es un nombre que si se puede escribir. El mismo
# reparto que el simulador lleva con su local.mk desde hace meses, y que
# boot/ y deepsdr-ui/ llevan tambien con sus propios trampolines.
#
# Esta explicacion vive AQUI y no alli a proposito: alla arriba se perderia
# la proxima vez que haya que volver a pegar esa linea a mano.
#
# "Y si su make no es GNU make?" No es una suposicion: estos ficheros ya
# usan $(wildcard), $(filter-out), $(patsubst) y make -C. Con un make que
# no fuera GNU esto no habria compilado nunca, ni hoy ni el primer dia.
#
# tools/mkfile_check.py, dentro de "make comprueba", vigila que los tres
# trampolines sigan siendo una sola linea de include y que el .mk que
# nombran exista. Si alguien vuelve a escribir reglas ahi, se pone rojo.
#
TARGET     = firmware
BUILD_DIR  = build

# --- Toolchain ---
PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc -x assembler-with-cpp
LD      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

# --- MCU ---
CPU    = -mcpu=cortex-m4
FPU    = -mfpu=fpv4-sp-d16
FLOAT  = -mfloat-abi=hard
MCUFLAGS = $(CPU) -mthumb $(FPU) $(FLOAT)

# --- Defines ---
DEFS = -DGD32F450 -DUSE_STDPERIPH_DRIVER -DHXTAL_VALUE=12288000U
# Master switch for all debug_print*() UART logging (238 call sites,
# see User/debug_uart.h) - off by default. For a bring-up/debug
# session, build with `make DEBUG_UART_ENABLED=1` (no source edits
# needed).
DEBUG_UART_ENABLED ?= 0
DEFS += -DDEBUG_UART_ENABLED=$(DEBUG_UART_ENABLED)
# RTTY_ENABLED build flag REMOVED 08/08/2026: RTTY (User/rtty.c,
# rtty_scope.c) graduated from a debug-build-only tool to a real
# selectable mode (RTTY-L/RTTY-U in main.c's k_demod_modes[]) once
# validated against a real signal - both files are always compiled in
# and reachable from the normal mode picker now, no special build
# needed. For a debug session with the verbose per-character bit-level
# dump, flip CONFIG_RTTY_DIAG_ENABLED in User/config.h instead of a
# Makefile flag (still want `make DEBUG_UART_ENABLED=1` too, to
# actually see any of rtty.c's own debug_print*() output).
# ARM_MATH_DSP: the GD32F450 (Cortex-M4F) really does have the DSP
# extension, so let CMSIS-DSP call the real __QADD8/__SSAT/... it
# needs instead of pulling in its dsp/none.h software-emulation
# fallback (which would redefine the same intrinsics our CMSIS-Core
# headers already provide - see CMSIS/DSP/Include/cmsis_compiler.h).
DEFS += -DARM_MATH_DSP=1 -DARM_MATH_CM4

# --- Includes ---
INCLUDES  = -ICMSIS/Include
INCLUDES += -ICMSIS/GD/GD32F4xx/Include
INCLUDES += -IFirmware/Include
INCLUDES += -IUser
# CMSIS-DSP: PrivateInclude before Include isn't required, but keeping
# both on the path matches upstream's own build files.
INCLUDES += -ICMSIS/DSP/Include
INCLUDES += -ICMSIS/DSP/PrivateInclude

# --- Sources ---
C_SOURCES   = $(wildcard User/*.c)
#
# LA ETHERNET FUERA DEL BUILD, Y NO AHORRA NI UN BYTE - 07/10/2026.
#
# AVISO, PARA QUE NADIE SE LO CREA OTRA VEZ: esta linea NO libera memoria.
# Medido compilando con ella y sin ella:
#
#   con enet:  text 381900  data 412  bss 259292
#   sin enet:  text 381900  data 412  bss 259292
#
# Identicos. El --gc-sections del enlazador ya tiraba gd32f4xx_enet.c entero
# desde siempre, porque en todo el proyecto no hay ni una llamada a enet_*.
#
# La primera version de este comentario decia que se recuperaban 15 kB de
# RAM. Era mentira, y de la peor clase: salia de leer el .map con un grep a
# mano, y el mapa lista TAMBIEN las secciones descartadas. tx_buff y rx_buff
# aparecian con sus 7.620 bytes cada uno y no estaban dentro de nada. La
# prueba de que era falso estaba delante -se saco el enet, se volvio a
# enlazar y seguian faltando los mismos 28 bytes- y no se leyo.
#
# Se queda porque no hace daño y compila un fichero menos. Para medir RAM,
# tools/ram_mapa.py, que si sabe distinguir lo descartado de lo que entra.
#
# --- MEDICION: el nucleo USB de dispositivo dentro de la app ---
# Temporal, para saber cuanta RAM cuesta antes de escribir la clase CDC.
USB_DEV ?= 1
ifeq ($(USB_DEV),1)
DEFS     += -DUSE_USB_FS
# EL REPARTO DE LA MEMORIA DEL PERIFERICO USB.
#
# Son 320 palabras de 32 bits y no hay mas; usb/usb_conf.h lleva un
# _Static_assert que exige que sumen exactamente eso. Cuatro endpoints de
# entrada, y cada uno necesita su cola:
#
#   RX   96   todo lo que entra, mas los paquetes de control   384 B
#   TX0  64   el endpoint 0, el de control                     256 B
#   TX1  48   el bulk del puerto serie: 3 tramas de 64         192 B
#   TX2  16   el de interrupcion de CDC, que no se usa          64 B
#   TX3  96   el isocrono del audio: 2 tramas de 196           384 B
#        ---
#        320
#
# El bulk se queda con tres tramas y sobra: por ahi van ordenes de texto de
# una docena de bytes. Al isocrono hay que darle dos tramas enteras porque
# sale una cada milisegundo y no se puede llegar tarde: una trama de audio
# que no esta lista cuando el anfitrion la pide es un hueco que se oye.
# El numero de interfaz mas alto que el nucleo USB atiende. Son cuatro
# (0 y 1 del puerto serie, 2 y 3 del audio) y la comprobacion del nucleo es
# "<=", asi que el tope es 3. Con el 1 que viene de serie, Windows pedia la
# interfaz 3, se llevaba un STALL y reiniciaba el equipo en bucle.
DEFS     += -DUSBD_ITF_MAX_NUM=3U
DEFS     += -DRX_FIFO_FS_SIZE=96U -DTX1_FIFO_FS_SIZE=48U \
            -DTX2_FIFO_FS_SIZE=16U -DTX3_FIFO_FS_SIZE=96U
INCLUDES += -Iusb -Iusb/GD32F4xx_usb_library/driver/Include \
            -Iusb/GD32F4xx_usb_library/device/core/Include \
            -Iusb/GD32F4xx_usb_library/ustd/common
C_SOURCES += usb/GD32F4xx_usb_library/driver/Source/drv_usb_core.c \
             usb/GD32F4xx_usb_library/driver/Source/drv_usb_dev.c \
             usb/GD32F4xx_usb_library/driver/Source/drv_usbd_int.c \
             usb/GD32F4xx_usb_library/device/core/Source/usbd_core.c \
             usb/GD32F4xx_usb_library/device/core/Source/usbd_enum.c \
             usb/GD32F4xx_usb_library/device/core/Source/usbd_transc.c
endif

FW_EXCLUIR  = Firmware/Source/gd32f4xx_enet.c
C_SOURCES  += $(filter-out $(FW_EXCLUIR),$(wildcard Firmware/Source/*.c))
C_SOURCES  += CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c
# CMSIS-DSP: only the specific functions demod_am.c uses (biquad
# cascade DF1 float32 + complex magnitude float32), not the whole
# library - keeps build times and flash usage down. Add more
# CMSIS/DSP/Source/*/arm_*.c files here if you use more of it.
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c
C_SOURCES  += CMSIS/DSP/Source/ComplexMathFunctions/arm_cmplx_mag_f32.c
# arm_fir_f32/_init_f32: the Hilbert transformer for SSB (USB/LSB).
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_fir_f32.c
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_fir_init_f32.c
# arm_fir_decimate/_interpolate: the SSB decimated architecture
# (192kHz -> 12kHz for the Hilbert stage, then back up for the DAC).
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_fir_decimate_f32.c
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_fir_decimate_init_f32.c
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_fir_interpolate_f32.c
C_SOURCES  += CMSIS/DSP/Source/FilteringFunctions/arm_fir_interpolate_init_f32.c

ASM_SOURCES = CMSIS/GD/GD32F4xx/Source/GCC/startup_gd32f450_470.S

# --- Linker ---
LDSCRIPT = GD32F450VE_FLASH.ld
# -lm: needed as of 31/07/2026 for atan2f() (demod_am.c's WFM
# discriminator, see demod_am.h's WFM note on why libm instead of
# CMSIS-DSP's arm_atan2_f32()) - nano.specs/nosys.specs alone don't
# pull libm in, only libc; without this the link fails with
# "undefined reference to atan2f". Nothing else in the project uses
# libm, so this wasn't needed before.
LDFLAGS  = $(MCUFLAGS) -specs=nano.specs -specs=nosys.specs -T$(LDSCRIPT) \
           -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections -lm

# -O0 was costing far more than the AM filter itself: with the FPU
# enabled but nothing optimized, every per-sample float op in the
# demod/filter path re-spilled to memory instead of staying in FPU
# registers, run from the RX DMA ISR on top of it. -O2 -g3 keeps full
# debug symbols (just some locals become "optimized out" in gdb).
#
# *** -fno-common, Y NO ES COSMETICA - 10/10/2026. ***
#
# El usuario compilo con GCC 9.2.1 y el informe de RAM dijo 120.672 bytes
# cuando `size -A` decia 176.492: 55.820 sin contar, o sea el buffer de la
# cascada entero.
#
# GCC 9 compila con -fcommon por defecto; GCC 10 y posteriores con
# -fno-common. Con -fcommon, una global SIN INICIALIZAR no va a su propia
# `.bss.nombre`: va a COMMON. Medido con nm sobre el mismo fichero:
#
#     -fcommon     C  g_ft8_shared_ram   0xd4a0 = 54.432
#     -fno-common  B  g_ft8_shared_ram   0xd4a0
#
# El informe esta arreglado para contar COMMON igual (ver ram_mapa.py),
# pero la bandera se pone de todas formas, y por algo mas gordo que un
# informe: con -fcommon, DOS definiciones tentativas de la misma global en
# dos ficheros distintos NO dan error, se FUNDEN en una sola. Dos modulos
# creyendo cada uno que tiene su variable y compartiendola sin saberlo es
# una averia que no se encuentra leyendo, y este proyecto ya ha pagado esa
# clase de cosa varias veces.
#
# Con la bandera puesta, eso es un error de enlace y no una sorpresa. En
# GCC 10+ no cambia nada porque ya es el comportamiento por defecto: el
# binario sale identico. Lo que hace es que la 9 se comporte como la 13.
#

#
# UNA PUERTA PARA LA VARIANTE DEL 470 - 16/10/2026.
#
# Pedido por el usuario.
#
# DEFS_EXTRA tiene que existir AQUI y no pasarse como DEFS= por la linea de
# ordenes: una variable dada en la linea de ordenes machaca TODAS las
# asignaciones del makefile, incluidos los quince "DEFS +=" de arriba -USB,
# CMSIS-DSP, los FIFO-, y lo que saldria no seria el firmware.
#
# GD32F450 se queda definido a proposito aunque el chip sea un 470: en este
# arbol los dos son el mismo integrado. gd32f4xx.h los agrupa en el mismo
# "#if defined (GD32F450) || defined (GD32F470)", system_gd32f4xx.c no
# menciona el 470 por ningun lado y el arranque ya es comun a los dos
# (startup_gd32f450_470.S). Lo unico que cambia de verdad es el mapa.
#
DEFS += $(DEFS_EXTRA)

CFLAGS  = $(MCUFLAGS) $(DEFS) $(INCLUDES) -Wall -O2 -g3 -ffunction-sections -fdata-sections -fno-common
ASFLAGS = $(MCUFLAGS) -g3


OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.S=.o)))

vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.S $(sort $(dir $(ASM_SOURCES)))

.PHONY: all clean flash erase update470 dos comprueba comprueba-fw

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin
	$(SIZE) $(BUILD_DIR)/$(TARGET).elf

#
# *** POR QUE AQUI PONE GNUmakefile Y NO Makefile - 10/10/2026. ***
#
# El usuario, compilando en su maquina:
#
#     ld: cannot open map file build/firmware.map: No such file or directory
#     make: *** [reglas.mk:177: build/firmware.elf] Error 1
#
# Dos fallos a la vez, los dos mios y los dos del dia que el puente nos
# obligo a renombrar el Makefile:
#
# 1. ESTAS REGLAS PEDIAN `Makefile` COMO REQUISITO. En este arbol ya no hay
#    ningun fichero que se llame asi -se llama GNUmakefile, ver su cabecera-,
#    y una regla de patron cuyo requisito no existe NO SE APLICA. En mi
#    maquina seguia habiendo un Makefile viejo tirado y por eso compilaba;
#    en la suya no, y por eso no. Un requisito que solo existe en la maquina
#    de quien lo escribio es la peor clase de requisito.
#
#    Van los dos ficheros de verdad: si tocas una bandera en cualquiera de
#    los dos, se recompila todo, que es para lo que estaba puesto.
#
# 2. Y EL ENLACE NO SE ASEGURABA DE QUE `build/` EXISTIERA. Los .o si -ahi
#    estaba el `| $(BUILD_DIR)`-, pero el .elf, el .hex y el .bin no. Con
#    `make -j` eso es una carrera: en cuanto algo haga que el enlace arranque
#    sin que un .o haya creado el directorio, ld no puede abrir el .map y
#    para. Order-only (`|`) y no requisito normal: el directorio cambia de
#    fecha cada vez que se escribe algo dentro, y como requisito normal
#    reenlazaria en cada make.
#
$(BUILD_DIR)/%.o: %.c GNUmakefile reglas.mk | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.S GNUmakefile reglas.mk | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) $(LDSCRIPT) | $(BUILD_DIR)
	$(LD) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(OBJCOPY) -O binary -S $< $@

$(BUILD_DIR):
	mkdir -p $@

# OJO - 16/10/2026: esto era "rm -rf $(BUILD_DIR)" a secas, y con los
# valores por defecto eso es build/ y nada mas: build_470/ se quedaba
# entero. Un "clean" que limpia la mitad es peor que ninguno, porque uno
# cree que ha partido de cero y no.
#
# Lo que NO se borra, a proposito: update.bin, update_470.bin y
# customboot.bin. Esos no son basura de compilar, son los ficheros que se
# copian al disco de la radio y los que van al repositorio (ver .gitignore).
# Un "clean" que se lleva el update.bin que ibas a grabar no es limpiar.
clean:
	rm -rf build build_470 $(BUILD_DIR)

# --- Programming with ST-Link + OpenOCD ---
# We use our own target/gd32f450.cfg (in openocd/) instead of the
# stock target/stm32f4x.cfg: the GD32F450's silicon ID makes OpenOCD
# detect it as a dual-bank, 2048KB STM32F42x/43x, when the real VET6
# part is 512KB single-bank. See openocd/gd32f450.cfg.
#
# IMPORTANT: no address at the end of the program command. The .elf
# already carries the load address embedded in each section
# (0x0800C000 in this branch, set by the linker script). Passing an extra address
# here makes OpenOCD treat it as a "relocation offset" that gets ADDED
# to each section's address in the .elf (a feature meant for .bin
# files, which carry no address of their own) - that's what used to
# happen: it tried to write at 0x08020000+0x08020000 = 0x10040000,
# outside the flash range (0x08000000-0x08080000), silently failing to
# program anything.
flash: $(BUILD_DIR)/$(TARGET).elf
	openocd -f interface/stlink.cfg -f openocd/gd32f450.cfg \
		-c "program $(BUILD_DIR)/$(TARGET).elf verify reset exit"

erase:
	openocd -f interface/stlink.cfg -f openocd/gd32f450.cfg \
		-c "init; reset halt; stm32f2x mass_erase 0; reset; exit"

# --- update.bin para nuestro cargador (rama "reload") ---
# Aqui habia un one-liner de python que rellenaba el .bin con ceros hasta
# 0x40000 y pegaba ocho bytes constantes al final, que era lo unico que
# miraba el gestor de fabrica. Nuestro cargador comprueba cabecera,
# longitud y CRC32, y eso ya no cabe en una linea: lo hace tools/cabecera.py,
# que ademas se para con un mensaje distinto por cada cosa que puede ir mal
# en vez de generar un fichero que la radio rechaza sin decir por que.
#
# EL NOMBRE ES update.bin DESDE EL 02/10/2026, por el usuario. El "4" lo
# ponia el gestor de fabrica y en esta rama ya no heredamos nada suyo. El
# target se sigue llamando "update4" ademas de "update" para que un
# "make update4" de la memoria muscular no falle en silencio sin hacer
# nada; hace lo mismo y avisa.
#
# Y SE COMPRUEBA ANTES DE SALIR DE AQUI - 14/10/2026.
#
# Pedido por el usuario.
#
# Lo que paso fue que hice `make`, no `make update`, y copie
# build/firmware.bin encima de update.bin a mano. La imagen pelada no
# lleva cabecera y la radio la rechazo. El fallo no fue de la compilacion:
# fue saltarse este target.
#
# tools/comprueba_update.py mira el fichero como lo mira el cargador
# -magia, longitud, CRC-32/MPEG-2 y la tabla de vectores- y devuelve
# error si la radio lo va a rechazar. Esta aqui para que un update.bin
# invalido NO PUEDA salir de un `make update`, y ademas se puede llamar a
# mano sobre el fichero que hay en el USB, que es el que de verdad importa:
#
#     python3 tools/comprueba_update.py E:/update.bin
#
update: $(BUILD_DIR)/$(TARGET).bin
	@python3 tools/cabecera.py $(BUILD_DIR)/$(TARGET).bin update.bin
	@python3 tools/comprueba_update.py update.bin
	@python3 tools/ram_mapa.py $(BUILD_DIR)/$(TARGET).map

#
# Y EL DEL GD32F470VGT6 - 16/10/2026.
#
# Pedido por el usuario.
#
#   make update      ->  update.bin        450VET6,  512 kB  (el de siempre)
#   make update470   ->  update_470.bin    470VGT6, 1024 kB
#
# Y los cargadores, que es donde esta la diferencia de verdad:
#
#   make -C boot          ->  customboot.bin
#   make -C boot f470     ->  boot/customboot_470.bin
#
# --tope: 0x08100000 - 0x08008000 = 1.015.808 bytes, el doble que el chip de
# serie. Sin decirselo, cabecera.py rechazaria por "sobran" una imagen que
# cabe de sobra el dia que la aplicacion pase de 475.136.
#
# --clave: el registro guarda un CRC por version y el firmware normal ya
# ocupa la linea "V5.02". Sin sufijo los dos se acusarian mutuamente de
# haber cambiado sin subir la version.
#
.PHONY: update470
update470:
	@$(MAKE) BUILD_DIR=build_470 TARGET=firmware_470 \
	         LDSCRIPT=GD32F470VG_FLASH.ld DEFS_EXTRA=-DCHIP_F470VG
	@python3 tools/cabecera.py build_470/firmware_470.bin update_470.bin \
	         --tope=1015808 --clave=" 470"
	@python3 tools/comprueba_update.py update_470.bin
	@python3 tools/ram_mapa.py build_470/firmware_470.map
#
# Y SI SALE IGUAL QUE EL DE SIEMPRE, QUE LO DIGA.
#
# Hoy, con V5.02, los dos salen el MISMO fichero byte a byte. Tiene
# sentido -lo que cambia con el 470 es cuanto borra el CARGADOR, y el
# cargador no es esto- pero no es una garantia: en cuanto la aplicacion
# pase de 475.136 bytes o use la RAM de mas, dejaran de coincidir.
#
# Mientras coincidan conviene saberlo, porque dos ficheros identicos con
# nombres distintos es la forma mas tonta de grabar el que no era.
#
	@cmp -s update.bin update_470.bin \
	  && echo "  (identico a update.bin: con esta version vale el mismo para los dos chips)" \
	  || echo "  (DISTINTO de update.bin: este es el del chip de 1024 kB)"

#
# LOS DOS, Y EN SERIE A PROPOSITO - 16/10/2026.
#
# No es "update update470" a secas porque con make -j los dos corren a la
# vez, y los dos escriben tools/entregas.csv entero (leer-modificar-
# reescribir): una de las dos entregas se perderia del registro. Ademas el
# cmp final de update470 leeria un update.bin que el otro esta escribiendo,
# y el veredicto "identico/distinto" saldria al azar.
#
.PHONY: dos
dos:
	@$(MAKE) update
	@$(MAKE) update470

#
# TODAS LAS COMPROBACIONES, DE UNA - 16/10/2026.
#
# Pedido por el usuario.
#
# Y al ir a hacerlo salio esto: hay quince bancos en tools/*_check.py y no
# habia ninguna forma de correrlos todos. Cada uno se llamaba a mano el dia
# que se escribio y despues ya no lo llamaba nadie. Un banco que nadie
# ejecuta no es una red: es un fichero.
#
# Peor todavia: cuatro de ellos necesitan argumentos y, llamados a secas,
# imprimen su ayuda y salen con error. O sea que quien probara a pasarlos
# en bloque veria cuatro rojos que no son fallos, se cansaria y no los
# volveria a mirar.
#
# Y falta uno que no se llama *_check.py: tools/mapa.py, que es el que
# User/cargador.h nombra -"los tres sitios que tienen que cuadrar los
# vigila tools/mapa.py"- y que vivia suelto en un target de avisos.mk que
# no colgaba de aqui. El guardian nombrado no lo corria nadie.
#
#   make comprueba     <- antes de cada push. Corre las de aqui Y LUEGO las
#                        del simulador.
#   make comprueba-fw  <- solo las de aqui, sin compilar el host.
#
# Y LAS CARPETAS HERMANAS - 18/10/2026. citas_check.py se colgo de aqui el
# 17/10, y aqui se mira SOLO la carpeta del firmware. docs/ y manual/ son
# repositorios aparte, al lado de este, y se quedaron fuera: en
# docs/CAMBIOS.md habia noventa y cuatro citas mientras la tanda decia que
# no habia ninguna. Un guardian que no mira donde esta el problema no es
# una red, es un fichero. Ahora se le pasan las tres carpetas, y si las
# hermanas no estan al lado se salta y sigue.
#
# CUANTAS SON, SIN ESCRIBIRLO A MANO - 18/10/2026. Aqui ponia "dieciseis"
# en cinco sitios y hacia tiempo que eran dieciocho: la frase se quedo
# quieta mientras la lista crecia debajo. Es exactamente lo que cazan la
# mitad de estos bancos -un numero que dice una cosa mientras el codigo
# hace otra-, asi que el numero sale ahora de contar la receta.
#
# OJO, Y ESTO CASI SALE MAL: deepsdr-ui/local.mk YA tenia un target
# llamado "comprueba", con otras setenta y una. Dos targets con el mismo
# nombre y distinto alcance es la mejor forma de creerse cubierto: uno
# corre el de la raiz, lee "en verde" y se va tranquilo habiendose saltado
# las setenta y una de verdad. Por eso este encadena con aquel en vez de
# competir con el, y el que solo quiere las de aqui pide comprueba-fw.
#
# Se para en el primero que falle (ni -k ni ||true): si algo esta mal, lo
# que hace falta es mirarlo, no seguir y enterrarlo en pantallas de texto.
#
N_BANCOS_FW := $(shell awk '/^comprueba-fw:/{d=1;next} d&&/^[^\t]/{exit} d&&/python3 tools\//{n++} END{print n+0}' reglas.mk)

.PHONY: comprueba
comprueba: comprueba-fw
	@echo ""
	@echo "== y la tanda del simulador (deepsdr-ui/local.mk) =="
	@$(MAKE) -C deepsdr-ui comprueba

.PHONY: comprueba-fw
comprueba-fw:
	@echo "== los $(N_BANCOS_FW) bancos del firmware =="
	@python3 tools/mkfile_check.py
	@python3 tools/citas_check.py .
	@for d in ../docs ../manual; do \
	   if [ -d "$$d" ]; then python3 tools/citas_check.py "$$d" || exit 1; fi; \
	 done
	@python3 tools/interruptores_check.py .
	@python3 tools/prestamo_check.py .
	@python3 tools/claves_check.py
	@python3 tools/gancho_check.py
	@python3 tools/copias_bin_check.py
	@python3 tools/fft_coste_check.py
	@python3 tools/novedades_check.py User/config.h NOVEDADES.md
	@python3 tools/md_enlaces_check.py .
	@python3 tools/tope_check.py
	@python3 tools/isr_coste_check.py
	@python3 tools/espectro_coste_check.py
	@python3 tools/rds_coste_check.py
	@python3 tools/mapa.py
	@python3 tools/mando_check.py User/main.c
	@python3 tools/nco_centro_check.py
	@python3 tools/panel_check.py
	@python3 tools/osci_check.py .
	@python3 tools/tactil_check.py User/main.c
	@python3 tools/panel_rama_check.py
	@python3 tools/repinta_check.py
	@python3 tools/via_audio_check.py
	@python3 tools/escala_check.py
	@python3 tools/sitios_check.py
	@python3 tools/emisoras_check.py
	@python3 tools/hfdl_canales_check.py
	@python3 tools/mapa_anillos_check.py mapas/001.map
	@echo ""
	@echo "  los $(N_BANCOS_FW) bancos del firmware, en verde."

# El reparto de la RAM, sin compilar nada. Ver tools/ram_mapa.py: la RAM no se
# decide en un .c, se decide en el enlazado, y el mapa es el unico sitio donde
# pone que acaba de verdad en los 192 kB.
.PHONY: ram
ram:
	@python3 tools/ram_mapa.py $(BUILD_DIR)/$(TARGET).map

.PHONY: update4
update4: update
	@echo "   (ojo: ahora se llama 'make update', y el fichero update.bin)"

# Opciones extra de compilacion, en un fichero aparte (23/09/2026). El "-"
# de "-include" significa "si no esta, no pasa nada", asi que el proyecto
# sigue compilando sin el. Esta separado porque el puente con el que se
# escriben los ficheros de este proyecto no puede escribir ficheros llamados
# Makefile -son codigo que se ejecuta- pero si un .mk: asi cambiar las
# opciones no obliga a renombrar nada a mano cada vez.
-include avisos.mk
