# Avisos extra SOLO para User/ (23/09/2026). Este fragmento lo incluye el
# Makefile con "-include avisos.mk", y existe por una razon practica: el
# puente que usa Claude para escribir en este ordenador se niega a escribir
# ficheros llamados Makefile (son codigo que se ejecuta), pero si deja
# escribir un .mk. Asi los cambios de opciones de compilacion no obligan a
# renombrar nada a mano cada vez.
#
# Que cazan: la clase de fallo que en este proyecto ya ha mordido varias
# veces y que el compilador aceptaba sin rechistar - la misma condicion
# escrita dos veces en un else-if (una rama muerta; asi estuvo el boton
# "Paso" sin iluminarse), dos ramas identicas, un || donde iba un &&.
#
# Firmware/ y CMSIS/ son codigo del fabricante, tienen hallazgos propios que
# no vamos a tocar, y se quedan con -Wall a secas.
#
# -Werror SOLO en duplicated-cond: de todos estos es el que no tiene falsos
# positivos, y es exactamente el que fallo. Los demas avisan y ya.
WARN_USER    = -Wduplicated-cond -Werror=duplicated-cond -Wduplicated-branches \
               -Wlogical-op -Wnull-dereference -Wjump-misses-init
USER_OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(patsubst %.c,%.o,$(wildcard User/*.c))))
$(USER_OBJECTS): CFLAGS += $(WARN_USER)

# --- DEPENDENCIAS DE CABECERAS. 24/09/2026 ---
#
# Hasta hoy el Makefile solo sabia que un .o depende de su .c (y del propio
# Makefile). De las cabeceras no sabia nada. O sea que tocar User/config.h
# -donde esta, por ejemplo, el numero de version- y escribir "make" dejaba
# los .o viejos en su sitio: compilaba, enlazaba, decia que todo bien, y el
# binario salia con lo de antes dentro. Sin aviso, sin nada.
#
# Paso hoy mismo, al subir la version a V1.11: el .bin seguia diciendo
# "Rediseño 11" y solo se vio porque se fue a buscar la cadena dentro del
# fichero. Es el mismo fallo del update4.bin viejo de anteayer, otra vez:
# una herramienta que entrega en silencio algo que no es lo que se pidio.
#
# -MMD hace que gcc escriba, al lado de cada .o, un .d con la lista de
# cabeceras que ese .o ha leido de verdad; el -include de abajo se la da a
# make. -MMD y no -MD: las cabeceras del sistema no se listan, que no se
# mueven y engordan los .d para nada. -MP añade una regla vacia por cabecera
# para que BORRAR una cabecera no rompa el make con "no rule to make target".
#
# Coste: cero en tiempo de compilacion y unos pocos kB de .d dentro de
# build/, que ya se borra entero con "make clean".
CFLAGS  += -MMD -MP
ASFLAGS += -MMD -MP
-include $(wildcard $(BUILD_DIR)/*.d)

# ---------------------------------------------------------------------
# Optimizacion mixta: -Os en lo frio, -O2 en lo caliente (24/09/2026)
# ---------------------------------------------------------------------
# A V1.30 le quedaban 14.692 bytes libres de los 262.144 que caben en la
# particion de aplicacion. Con eso no entraba la galeria de imagenes, ni
# las memorias, ni el modo oscuro. La alternativa era abrir la radio y
# programar por SWD para usar los 384 kB reales -y eso son un
# destornillador y un cable-, asi que antes de eso tocaba mirar donde se
# estaba yendo la flash.
#
# La respuesta: el proyecto entero se compilaba a -O2. Y -O2 engorda a
# proposito -desenrolla bucles, duplica codigo, alinea saltos- para ganar
# velocidad. Eso tiene todo el sentido del mundo en la cadena de audio,
# que procesa muestras sin parar, y ninguno en los menus, que dibujan una
# pantalla cuando tocas el cristal.
#
# Asi que: -Os global, y -O2 devuelto a mano a lo que de verdad corre.
# En GCC gana la ultima opcion de la linea de ordenes, de modo que
# "-Os ... -O2" acaba siendo -O2. Medido:
#
#     -O2 en todo (V1.30)   247.452 usados   14.692 libres
#     esta mezcla           216.760 usados   45.384 libres
#     -Os en todo           210.904 usados   51.240 libres
#
# Los 5.856 bytes que separan esta mezcla del -Os pelado son el precio de
# no tocar el DSP. Se pagan con gusto.
#
# COMPROBACION, no confianza: guarde los .o de los 14 ficheros calientes
# compilados a -O2 puro, reconstrui con esta mezcla y compare seccion
# .text contra seccion .text. Los catorce salen IDENTICOS byte a byte.
# Sin LTO no hay inlining entre unidades de compilacion, o sea que el
# codigo maquina de la cadena de audio es literalmente el mismo de antes,
# instruccion por instruccion. Si alguna vez tocas esta lista, repite esa
# comparacion: es la unica prueba que vale.
#
# AUDITORIA DE RETARDOS. Lo unico que -Os puede romper de verdad son los
# retardos calibrados "a ojo" por numero de instrucciones, que es el fallo
# que ya nos mordio una vez al pasar de -O0 a -O2 (ver el comentario
# gordo en i2c_bitbang.c: ruido continuo y saltos de espectro por una
# escritura I2C torcida). Los revise todos uno a uno:
#
#   i2c_bitbang.c  delay_i2c()        ya clavado con optimize("O0")   OK
#   touch.c        delay_us_approx()  ya clavado con optimize("O0")   OK
#   spi_flash.c    (su retardo)       ya clavado con optimize("O0")   OK
#   rm68120_exmc.c delay_ms()         usa g_msticks (SysTick real)    OK
#   battery.c      2000 NOPs volatile multiplo holgado del datasheet  OK
#   main.c         HardFault_Handler  bucle infinito, da igual        OK
#   gd32_i2s.c / sdr_rx.c             se quedan en -O2, no cambian    OK
#
# El atributo optimize() de una funcion gana a la linea de ordenes, o sea
# que los tres clavados a -O0 siguen a -O0 pase lo que pase aqui. No
# queda ni un retardo dependiente del nivel de optimizacion suelto.
#
# POR QUE ESTO VIVE EN avisos.mk Y NO EN EL Makefile: el Makefile no se
# puede reescribir desde aqui (el puente se niega), y "-include avisos.mk"
# es el unico gancho que hay. Queda raro en un fichero que se llama
# "avisos", si, pero funciona hoy y sin renombrar nada. Si algun dia el
# Makefile se puede tocar, esto se mueve a su propio optim.mk.
CFLAGS += -Os

# La lista caliente: todo lo que toca una muestra de audio o un pixel del
# espectro por fotograma, mas los CMSIS-DSP que llaman. Si anades un
# fichero nuevo a la cadena de audio, tiene que entrar AQUI.
CALIENTES = demod_am.o nr_ss.o anotch.o audiofil.o sam.o disc_fm.o nco.o \
            fft.o spectrum.o waterfall.o aic3204.o gd32_i2s.o sdr_rx.o \
            nb.o \
            arm_biquad_cascade_df1_f32.o arm_cmplx_mag_f32.o arm_fir_f32.o \
            arm_fir_decimate_f32.o arm_fir_interpolate_f32.o


# ---------------------------------------------------------------------
# FT8 - portado de la rama hfdl del proyecto padre (24/09/2026)
# ---------------------------------------------------------------------
# Ft8Lib es la biblioteca de Karlis Goba YL3JG (github.com/kgoba/ft8_lib),
# que trae el LDPC, el CRC y el empaquetado de mensajes. Vive fuera de
# User/ a proposito: es codigo de otro, no se toca, y separarla deja claro
# donde acaba lo nuestro.
#
# Se engancha desde aqui y no desde el Makefile porque el Makefile no se
# puede reescribir desde este lado (el puente se niega). Funciona porque
# OBJECTS y CFLAGS se declaran con "=" y no con ":=", o sea que se
# expanden cuando se usan y no cuando se leen: lo que se anada aqui, mas
# abajo en el fichero, sigue entrando. La unica que no es perezosa es
# vpath, que es una directiva y se procesa segun se lee - por eso hay que
# declararla aqui explicitamente.
C_SOURCES += $(wildcard Ft8Lib/ft8/*.c)
vpath %.c Ft8Lib/ft8
INCLUDES  += -IFt8Lib

# El decodificador es el camino caliente de este modo -una ranura de FT8
# son 15 s de audio que hay que digerir en los 2,4 s que quedan hasta la
# siguiente- asi que no se compila a -Os con el resto.
CALIENTES += ft8_decoder.o ft8_fft1024.o ft8_decimator.o \
             ft8_waterfall_adapter.o decode.o ldpc.o

# ENGANCHE DEL .elf. Esto es lo que faltaba y costo un rato entender, asi que
# queda escrito: en make, los PREREQUISITOS de una regla se expanden cuando se
# LEE la regla, y la RECETA cuando se EJECUTA. La regla del .elf esta en la
# linea 112 del Makefile y este fichero se incluye en la 171, o sea que la
# receta -que se expande la ultima- si veia los .o de FT8, pero la lista de
# dependencias no. Resultado: make se los pasaba al enlazador sin haberlos
# compilado nunca, y ld decia "cannot find build/crc.o" sin que hubiera nada
# roto en el codigo. Que "=" en vez de ":=" no basta: eso arregla la receta,
# no los prerequisitos.
#
# Anadir prerequisitos a una regla que ya existe es legal mientras no se le
# anada una segunda receta, que es justo lo que hacemos aqui.
FT8_OBJETOS = $(addprefix $(BUILD_DIR)/,$(notdir $(patsubst %.c,%.o,$(wildcard Ft8Lib/ft8/*.c))))
$(BUILD_DIR)/$(TARGET).elf: $(FT8_OBJETOS)

# La regla de los calientes va AQUI, al final, y por el mismo motivo: la lista
# de objetivos de una regla con variable especifica tambien se expande al
# leerla. Si se queda arriba, los "CALIENTES +=" de FT8 llegan tarde y el
# decodificador se compila a -Os sin que nadie avise.
$(addprefix $(BUILD_DIR)/,$(CALIENTES)): CFLAGS += -O2

# ---------------------------------------------------------------------
# COMPROBACION DE PILA EN CADA COMPILACION (24/09/2026)
# ---------------------------------------------------------------------
# -fstack-usage hace que gcc escriba, al lado de cada .o, un .su con los
# bytes de marco de cada funcion. No cuesta tiempo de compilacion y son
# unos pocos kB dentro de build/, que ya se borra con "make clean".
#
# tools/pila.py los junta con el grafo de llamadas del binario enlazado y
# FALLA la compilacion si el peor caso no cabe en la pila real. El motivo
# esta escrito entero ahi y en el comentario de los borradores de
# spi_flash.c: esto ya paso, y paso en silencio.
#
# Cuelga de "all" y de "update4" a proposito: no se puede generar un
# binario para la radio sin que esto haya pasado por medio.
CFLAGS += -fstack-usage

# Y de paso: hasta ahora tocar ESTE fichero no recompilaba nada. El Makefile
# pone "Makefile" como dependencia de cada .o, pero de avisos.mk no sabia
# nada, o sea que cambiar aqui una opcion de compilacion dejaba los .o viejos
# en su sitio y el binario salia con las opciones de antes. Es el mismo fallo
# que el de las cabeceras documentado arriba, otra vez, y me acaba de morder:
# anadir -fstack-usage no genero ni un solo .su. Anadirlo a la regla de patron
# los coge a todos, incluidos los de Ft8Lib.
# CUIDADO con como se escribe esto: una regla de PATRON sin receta
# ("$(BUILD_DIR)/%.o: avisos.mk") no anade una dependencia, sino que CANCELA
# la regla de patron entera. Lo probe asi y el resultado fue que make dejo de
# saber compilar .c, sin decir nada: no recompilo, no fallo, siguio enlazando
# los .o viejos. Con objetivos explicitos no pasa. Va al final del fichero,
# donde OBJECTS ya incluye los .o de Ft8Lib.

.PHONY: pila
pila: $(BUILD_DIR)/$(TARGET).elf
	@python3 tools/pila.py $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)

all: pila
update4: pila

$(OBJECTS): avisos.mk

# ---------------------------------------------------------------------
# El DMA de la pantalla, encendido o no de fabrica (25/09/2026).
#
#   make            -> con DMA
#   make DMA=0      -> el mismo firmware con el DMA apagado de fabrica
#
# Para que exista un binario de vuelta: si el DMA saliera mal en la radio,
# lo que se veria es la pantalla escrita de cualquier manera, y desde ahi no
# se puede llegar al interruptor de la ventana de informacion. Ver el
# comentario de LCD_DMA_ON_DEF en User/lcd_dma.c.
DMA ?= 1
CFLAGS += -DLCD_DMA_ON_DEF=$(DMA)U

# ---------------------------------------------------------------------
# LA FIRMA DE ARRANQUE, COMPROBADA EN CADA COMPILACION. 27/09/2026.
#
# Desde que la imagen pasa de 0x40000 bytes, los ocho bytes que el gestor
# de arranque mira en 0x08060000 van DENTRO del binario (User/firma_app.c)
# en vez de pegados al final por la receta de update4. Eso abre tres formas
# de generar un update4.bin que la radio rechaza sin decir por que -que
# --gc-sections se lleve la firma, que acabe en otra direccion, o que el
# fichero pase de 0x50000-, y las tres se ven igual: nada.
#
# tools/firma.py las caza sobre el .bin ya generado. Cuelga de "all" y de
# "update4" por el mismo motivo que "pila": no se puede sacar un binario
# para la radio sin que esto haya pasado por medio. El razonamiento entero,
# con el desensamblado del gestor, esta en la cabecera de ese fichero y en
# el comentario de MEMORY de GD32F450VE_FLASH.ld.
.PHONY: firma
firma: $(BUILD_DIR)/$(TARGET).bin
	@python3 tools/firma.py $(BUILD_DIR)/$(TARGET).bin

all: firma
update4: firma

# ---------------------------------------------------------------------
# EL PANEL DIGITAL, VIGILADO. 28/09/2026.
#
# digi_panel_active() decide si el espectro cede su sitio al panel digital,
# y llevaba una lista de modos escrita a mano que se quedo en HFDL: WSPR,
# AIS y ALE entraron despues y ninguno de los tres se anadio. El modo se
# seleccionaba, decodificaba, y la pantalla no cambiaba. "Le doy a wspr y la
# ventana no cambia".
#
# Ahora sale de k_demod_modes[] y esto comprueba que sigue saliendo de ahi.
# Cuelga de "all" y de "update4" como pila y firma.
.PHONY: panel
panel: User/main.c
	@python3 tools/panel_check.py User/main.c

all: panel
update4: panel
