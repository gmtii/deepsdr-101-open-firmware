# =====================================================================
# QUE $(FIRMWARE) SEA EL FIRMWARE DE ESTA RADIO. 10/10/2026.
#
# Esto va ARRIBA DEL TODO y es lo unico del proyecto que mata el make en
# seco, porque es lo unico que, si falla, hace que TODO LO DEMAS MIENTA.
#
# El Makefile traia "FIRMWARE ?= ../deepsdr", de cuando solo habia una rama
# y el simulador vivia FUERA, de hermano. Entonces, en la misma carpeta,
# habia tres cosas:
#
#     ../deepsdr          la rama vieja, parada el 28/09, viva en el disco
#     ../deepsdr-reload   la que corre en la radio
#     ../deepsdr-ui       esto
#
# Desde el 13/10/2026 el simulador vive DENTRO del arbol del firmware, asi
# que su raiz es el padre y el Makefile pone "FIRMWARE ?= ..". Lo de antes
# apuntaba a una rama hermana que aqui ya no existe.
#
# Y ESA LINEA TIENE QUE ESTAR EN EL Makefile, NO AQUI. Se intento traerla y
# no vale: make expande los prerequisitos de una regla al LEER el fichero, y
# las reglas que usan $(FIRMWARE) estan en el Makefile, mucho antes del
# "-include local.mk" del final. Puesta aqui, "make comprueba" a secas se iba
# a buscar ../deepsdr/User/main.c y moria. Un valor por defecto llega tarde
# si se escribe despues de quien lo usa. Lo que si vive aqui es el $(error)
# de abajo, que comprueba que lo que haya en FIRMWARE sea esta radio.
#
# O sea que "make" a secas, sin FIRMWARE=, no fallaba: compilaba y aprobaba
# EL FIRMWARE VIEJO. Durante semanas. Y las copias de vendor/ llevaban desde
# el 25/09 congeladas y al dia... de la rama vieja: gfx.c 10.001 bytes
# cuando hoy son 12.464, spectrum.c 64.064 cuando hoy son 80.847.
#
# Lo gordo no es que midiera de menos: `make copias`, la orden que pone las
# copias al dia, habria copiado el firmware de septiembre ENCIMA de las
# buenas. La herramienta de arreglarlo era la que mas daño hacia.
#
# POR QUE AQUI Y NO EN UNA REGLA
#
# Una regla solo protege a quien la pide. "make comprueba" si -raizchk
# cuelga de ella mas abajo- pero "make principal" o "make spec3b", que son
# recetas del Makefile, no: y no se les puede colgar un requisito porque
# sus recetas compilan $^ tal cual y le pasarian el requisito a gcc (eso ya
# esta explicado donde BANCOS_LOCALES). Un $(error) al leer el fichero se
# ejecuta SIEMPRE, con cualquier objetivo, incluidos los del Makefile.
#
# Tres sondas y no las 139 rutas: esto se lee en cada make y tiene que
# costar cero. Son tres ficheros que nacieron en la rama nueva, asi que
# ninguna version de la vieja los tiene. La comprobacion completa -las 139-
# la hace tools/raiz_check.py, que cuelga de "comprueba".
FW_SONDAS := User/rapido.c User/ident.c User/fat32.c
FW_FALTAN := $(strip $(foreach s,$(FW_SONDAS),\
                       $(if $(wildcard $(FIRMWARE)/$(s)),,$(s))))
ifneq ($(FW_FALTAN),)

# Si la raiz es la mala, se mira al lado: las carpetas hermanas que SI
# tienen las tres sondas. Si hay exactamente una, se coge esa y se dice en
# voz alta; si no hay ninguna, o hay varias y habria que adivinar, se para.
#
# Coger la buena en silencio seria repetir el fallo de hoy con otro signo.
# Por eso el $(warning) sale SIEMPRE que se corrige: el usuario no tiene que
# escribir FIRMWARE= en cada orden, pero se entera de que el valor por
# defecto del Makefile -que no se puede tocar- esta viejo.
FW_VECINAS := $(strip $(patsubst %/$(firstword $(FW_SONDAS)),%,\
                $(wildcard $(dir $(FIRMWARE))*/$(firstword $(FW_SONDAS)))))
FW_BUENAS  := $(strip $(foreach d,$(FW_VECINAS),\
                $(if $(strip $(foreach s,$(FW_SONDAS),\
                       $(if $(wildcard $(d)/$(s)),,x))),,$(d))))

# Y SOLO SI EL VALOR MALO VIENE DEL MAKEFILE, NO DE SU MANO.
#
# make da prioridad a lo que se escribe en la orden: un "FIRMWARE=x" en la
# linea de ordenes no lo puede pisar una asignacion de aqui. Si no se mira
# el origen, el aviso diria "uso la otra" y luego seguiria usando la suya:
# un mensaje que miente sobre lo que hace el programa. Asi que cuando la
# raiz mala la ha puesto el usuario a mano, no se adopta nada - se para y se
# le dice, que para eso la ha escrito el.
ifeq ($(origin FIRMWARE),command line)
FW_ADOPTA :=
else
FW_ADOPTA := $(FW_BUENAS)
endif

ifeq ($(words $(FW_ADOPTA)),1)
$(warning )
$(warning   FIRMWARE=$(FIRMWARE) es la rama vieja: le faltan $(FW_FALTAN))
$(warning   Uso $(FW_ADOPTA) en su lugar.)
$(warning )
$(warning   Pasa esto porque el Makefile trae "FIRMWARE ?= ../deepsdr" y ese)
$(warning   fichero no se toca. Para que no salga el aviso, pasa a mano)
$(warning   FIRMWARE=$(FW_ADOPTA))
$(warning )
FIRMWARE := $(FW_ADOPTA)
else ifeq ($(origin FIRMWARE),command line)

# APUNTAR A LA RAMA VIEJA A PROPOSITO SIGUE VALIENDO.
#
# Esto no es una hipotesis: esta escrito en CAMBIOS.md, en "El simulador
# vale para las dos ramas" (02/10/2026), y se uso -"el banco del cargador
# tambien en verde contra deepsdr"-. Ahi esta media gracia de que el
# simulador compile el firmware desde fuera en vez de llevarlo dentro.
#
# Asi que cuando la raiz mala la ha escrito el usuario EN LA ORDEN, esto
# avisa y SIGUE: los bancos que necesiten un fichero que esa rama no tiene
# fallaran solos, diciendo que fichero les falta, que es la verdad. Parar
# aqui seria quitarle una cosa que funciona para protegerle de un fallo que
# no esta cometiendo.
#
# Lo que NO se ablanda es `make copias`: copiar DESDE la rama vieja encima
# de las copias del simulador se lleva el trabajo por delante tanto si es a
# proposito como si no, y eso lo sigue parando copias_check.py en seco.
$(warning )
$(warning   FIRMWARE=$(FIRMWARE) no tiene $(FW_FALTAN))
$(warning   Es otra rama. Sigo, porque me lo has pedido tu en la orden: los)
$(warning   bancos que necesiten esos ficheros fallaran diciendo cual.)
$(warning )
$(warning   Lo que no voy a hacer con esta raiz es `make copias`.)
$(warning )

else
$(warning )
$(warning   FIRMWARE=$(FIRMWARE) no es el firmware de esta radio.)
$(warning )
$(warning   Le faltan: $(FW_FALTAN))
$(warning )
ifeq ($(words $(FW_BUENAS)),0)
$(warning   Y no he encontrado ninguna carpeta al lado que los tenga.)
else
$(warning   Hay $(words $(FW_BUENAS)) candidatas y no voy a adivinar:)
$(warning     $(FW_BUENAS))
$(warning )
$(warning   Lanza las cosas asi:)
$(warning     make comprueba FIRMWARE=$(firstword $(FW_BUENAS)))
endif
$(warning )
$(warning   Si has renombrado alguno de esos ficheros en el firmware,)
$(warning   actualiza FW_SONDAS aqui arriba.)
$(warning )
$(error no sigo: esa raiz no la has elegido tu, la trae el Makefile por)
endif
endif

# =====================================================================
# Y QUE out/ NO SE QUEDE CON BINARIOS DE OTRA RAIZ - 10/10/2026.
#
# El guardian de arriba comprueba que $(FIRMWARE) SEA el firmware bueno.
# Esto comprueba otra cosa distinta, y es la que faltaba: que los binarios
# que ya hay en out/ se hayan construido CON ESA MISMA RAIZ.
#
# COMO MUERDE. Haces `make FIRMWARE=../otra_rama algo`, mide esa rama, y
# luego `make FIRMWARE=../fw-reload algo`. Make compara la fecha de
# out/algo con la de ../fw-reload/User/*.c, y si esos ficheros son mas
# VIEJOS que el binario -que lo son, porque el binario se acaba de
# construir- decide que out/algo esta al dia. Y no lo esta: dentro lleva
# la otra rama. El banco no falla: PASA, midiendo codigo que no es el que
# crees.
#
# No es una hipotesis. Hoy mismo, probando una mutacion en una copia del
# firmware, `make FIRMWARE=<la copia>` y luego `make FIRMWARE=../fw-reload`
# devolvio el binario del mutante: hubo que borrarlo a mano para que
# volviera a medir el arbol bueno. Y es la MISMA familia del 25/09, cuando
# el simulador llevaba una semana midiendo la rama vieja.
#
# EL ARREGLO: out/ se queda con una nota de con que raiz se lleno. Si la
# raiz de ahora no es esa, los binarios de out/ se borran antes de que
# make mire una sola fecha. Cuesta recompilar al cambiar de rama, que es
# exactamente lo que hay que hacer al cambiar de rama.
#
# Se borran SOLO los ficheros sueltos de out/ -no lo de dentro de
# carpetas, que son salidas (capturas, volcados) y no binarios-.
RAIZ_MARCA := out/.raiz
RAIZ_AHORA := $(abspath $(FIRMWARE))
$(shell mkdir -p out; \
        if [ "$$(cat $(RAIZ_MARCA) 2>/dev/null)" != "$(RAIZ_AHORA)" ]; then \
            find out -maxdepth 1 -type f ! -name '.raiz' -delete 2>/dev/null; \
            printf '%s' '$(RAIZ_AHORA)' > $(RAIZ_MARCA); \
        fi)
# =====================================================================

# Comprobaciones que se cuelgan de "comprueba" sin tocar su receta.
# Ver el "-include local.mk" del final del Makefile para por que van aqui.
#
#   escala   quien puede escribir en s_db_min/s_db_max y en el par del
#            usuario. Ver tools/escala_check.py: son cuatro reglas de
#            QUIEN escribe DONDE, y escribir en la variable equivocada
#            compila igual de bien.
#   persist  el enum, la tabla de claves y las dos funciones de main.c que
#            guardan y restauran ajustes cubren los mismos identificadores
#   csvlen   CONFIG.CSV cabe en su buffer en el peor caso, y no cruza un
#            limite de 512 bytes (que estropearia el guardado rapido)
#   qth      el localizador compilado (FT8_OWN_GRID) es el de la radio:
#            de el salen el aspa de los mapas y las distancias de los CQ,
#            y un localizador valido que no es el tuyo no da ningun aviso
#   tapado   el renglon de titulos del panel digital no queda debajo del
#            mapa: el mapa empieza en el MISMO pixel donde ui_digi pinta
#            los titulos, asi que un titulo ahi no se ve nunca
#   hfdlcan  la tabla de canales de HFDL cumple lo que la rejilla da por
#            hecho: los grupos seguidos, las frecuencias subiendo, ninguna
#            repetida, y cada grupo con su rotulo. Una fila fuera de su
#            tramo no da error: abre una pagina de una celda
.PHONY: persist csvlen qth tapado escala hfdlcan nb dcf77 msf puntas als

persist: ; python3 tools/persist_check.py $(FIRMWARE)
escala:  ; python3 $(FIRMWARE)/tools/escala_check.py $(FIRMWARE)
hfdlcan: ; python3 $(FIRMWARE)/tools/hfdl_canales_check.py $(FIRMWARE)
csvlen:  ; python3 tools/csvlen_check.py $(FIRMWARE)/User/settings.c
qth:     ; python3 tools/qth_check.py $(FIRMWARE)
tapado:  ; python3 tools/tapado_check.py $(FIRMWARE)

comprueba: persist csvlen qth tapado escala hfdlcan

# Banco del noise blanker (mide cuanto quita y cuanto daña, ver sim/nbtest.c).
# Compila User/nb.c del firmware directamente: ese fichero no depende de nada
# del GD32 a proposito, justo para poder medirlo aqui.
out/nbtest: sim/nbtest.c $(FIRMWARE)/User/nb.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/nbtest.c $(FIRMWARE)/User/nb.c $(LDLIBS)

nb: out/nbtest ; ./out/nbtest

comprueba: nb

# Banco del alfabeto de siete bits (CCIR 476). Ver sim/cr7_test.c: el par
# que importa es CCIR contra BITS AL AZAR con el mismo salto y la misma
# velocidad, que es lo unico que no se puede separar mirando el espectro.
# Senal sintetica a proposito: un banco de "comprueba" no puede depender de
# una grabacion de dos megas. La de verdad -16.911 kHz- esta en
# sim/muestras/fsk200_16911.f32 y se mira con "make identaire".
out/cr7test: sim/cr7_test.c $(FIRMWARE)/User/stanag_det.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/cr7_test.c $(FIRMWARE)/User/stanag_det.c $(LDLIBS)

cr7: out/cr7test ; ./out/cr7test

comprueba: cr7

# EL AGC, Y EL PRIMER BANCO DE LA CADENA DE AUDIO - 09/10/2026.
#
# Pedido por el usuario.
#
# Lo que salio: habia DOS bucles de AGC copiados a mano en demod_am.c, se
# optimizo uno y el otro se quedo con una division de coma flotante por
# muestra -192.000 por segundo, en la interrupcion mas apretada que hay-.
# Y nadie lo vio porque de toda la cadena de audio el simulador no
# compilaba ni una linea: todos los decodificadores tienen banco, lo que
# suena por el altavoz no tenia ninguno.
#
# Ahora el lazo esta una sola vez, en User/agc.c, y esto lo compila DE
# VERDAD y lo compara muestra a muestra contra un AGC de libro escrito en
# el propio banco. El tope es 1 LSB del int16. Ver sim/agc_test.c.
out/agctest: sim/agc_test.c $(FIRMWARE)/User/agc.c $(FIRMWARE)/User/agc.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/agc_test.c $(FIRMWARE)/User/agc.c $(LDLIBS)

.PHONY: agc
agc: out/agctest ; ./out/agctest

comprueba: agc

# EL OSCILADOR DE POR DEBAJO DE 300 kHz - 09/10/2026.
#
# Pedido por el usuario.
#
# No hace falta osciloscopio: la aritmetica del temporizador es entera. Lo
# que faltaba era poder compilarla fuera de la radio, y por eso vive ahora en
# lo_gen_gd32.h. Esto la barre hercio a hercio de 6 a 300 kHz y comprueba que
# el periodo es par -o sea la cuadratura exacta-, que el divisor cabe y que
# el error de frecuencia no pasa de un escalon. Ver sim/logen.c.
out/logen: sim/logen.c $(FIRMWARE)/User/lo_gen_gd32.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/logen.c $(LDLIBS)

.PHONY: logen
logen: out/logen ; ./out/logen

comprueba: logen

# El oscilador de toda la radio, de 300 kHz a 180 MHz. Ver sim/lo5351.c:
# hace pasar al ms5351.c de verdad por un I2C de mentira y deshace la cuenta
# de los registros. Es el hermano de logen, que mira el tramo de por debajo
# de 300 kHz.
out/lo5351: sim/lo5351.c $(FIRMWARE)/User/ms5351.c $(FIRMWARE)/User/ms5351.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/lo5351.c $(FIRMWARE)/User/ms5351.c $(LDLIBS)

.PHONY: lo5351
lo5351: out/lo5351 ; ./out/lo5351

comprueba: lo5351

# ---------------------------------------------------------------------
# BASURA POR LA PUERTA DEL AIRE. Ver la cabecera de sim/aire_basura.c.
#
# Mete siete clases de señal hostil por la MISMA puerta que el audio de la
# radio, en los VEINTICINCO decodificadores, con -fsanitize=address,undefined
# puesto. La pregunta que contesta -"¿hay algun indice calculado con datos
# del aire que escriba fuera de su sitio?"- no se contesta leyendo codigo.
#
# Lleva sus propias banderas porque el resto del proyecto no se compila con
# los detectores: aqui hacen falta y en los demas bancos estorban.
#
# EN "comprueba" VA CON 20 SEMILLAS, que son unos siete segundos. El barrido
# profundo del 08/10/2026 fueron 500 semillas, 3.780 millones de muestras
# -87 horas de audio- en tres minutos, y salio limpio. Para repetirlo:
#     make basura FIRMWARE=... SEMILLAS=500
BASURA_SAN := -fsanitize=address,undefined -fno-omit-frame-pointer
BASURA_FW  := ecualiz cw rtty rtty_auto rtty_scope navtex dsc psk31 ais_rx ale_rx \
              ale_golay s4415 stanag_rx wspr_rx wspr_msg wspr_fano jtty_rx \
              jtty_fec jtty_msg ax25 sstv wefax hell nco nco_tabla hfdl_ram \
              hfdl_viterbi disc_fm \
              stanag_det dcf77 rapido hora_comun hora_rbu hora_wwv rds rds_pi \
              nr_ss anotch \
              hfdl_modo hfdl_acars hfdl_agc hfdl_aircraft hfdl_costas hfdl_crc \
              hfdl_data_segment hfdl_deinterleaver hfdl_demod_chain \
              hfdl_descrambler hfdl_equalizer hfdl_framer hfdl_hfnpdu \
              hfdl_iq_mixer hfdl_matched_filter hfdl_payload_decode hfdl_pdu \
              hfdl_preamble_seq hfdl_preamble_sync hfdl_rational_resampler \
              hfdl_soft_demod hfdl_symsync icao24_db \
              ft8_modo ft8_decoder ft8_snr ft8_decimator ft8_fft1024 ft8_tablas \
              ft8_waterfall_adapter
BASURA_FT8 := $(FIRMWARE)/Ft8Lib/ft8/decode.c $(FIRMWARE)/Ft8Lib/ft8/ldpc.c \
              $(FIRMWARE)/Ft8Lib/ft8/crc.c $(FIRMWARE)/Ft8Lib/ft8/message.c \
              $(FIRMWARE)/Ft8Lib/ft8/text.c $(FIRMWARE)/Ft8Lib/ft8/constants.c
SEMILLAS   ?= 20

out/basura: sim/aire_basura.c sim/waterfall_prestamo.c sim/sin_flash.c \
            $(addprefix $(FIRMWARE)/User/,$(addsuffix .c,$(BASURA_FW))) \
            $(BASURA_FT8)
	@mkdir -p out
	$(CC) -O1 -g $(BASURA_SAN) -Wall -I hostinc -I sim -I$(FIRMWARE)/User \
	      -I$(FIRMWARE)/Ft8Lib -o $@ $^ gfx2/idioma.c -lm

.PHONY: basura
basura: out/basura ; ./out/basura $(SEMILLAS)

comprueba: basura

# Cobertura del manual, y que el manual sea UNO. Ver tools/manual_check.py:
# existia desde el 06/10, apuntaba a la copia vieja de docs/ y NO LO CORRIA
# NADIE -no estaba en ningun Makefile-, asi que llevaba dos dias diciendo
# que la tabla del ident tiene 27 firmas cuando tiene 28. Un informe que no
# falla y que nadie lanza no es un banco.
#
# LOS TRES QUE NECESITAN LO QUE NO SE PUBLICA - 16/10/2026.
#
# *** Repasando la carpeta para el push salio esto: manual, modosman y
# version abren ficheros en $(FIRMWARE)/.. -o sea en manual\ y docs\, que
# son carpetas HERMANAS del repositorio y NO se publican-. ***
#
# Quien clone el repositorio y haga "make comprueba" -que es justo lo que
# el README le manda hacer- se estrella con un "No such file or directory"
# en el tercer objetivo. La tanda entera en rojo, y no por un fallo suyo.
#
# No se quitan: aqui dentro, en la maquina del usuario, esos tres SI valen y
# son de los que mas han pillado (el manual iba por V4.30 con el firmware
# en V4.40 y nadie lo miraba). Lo que se hace es saltarlos cuando el
# material no esta, diciendolo:
#
#   - si manual/ y docs/ estan, se corren como siempre.
#   - si no, se avisa en una linea y la tanda sigue.
#
# Un banco que no puede correr no debe tumbar a los otros doscientos; pero
# saltarlo en silencio seria peor, porque entonces nadie sabria que de los
# tres no se ha mirado ninguno.
#
HAY_MANUAL := $(wildcard $(FIRMWARE)/../manual/manual_check.py)
HAY_DOCS   := $(wildcard $(FIRMWARE)/../docs/CAMBIOS.md)

.PHONY: manual
manual:
ifeq ($(HAY_MANUAL),)
	@echo "  (manual/ no esta en el repositorio: me salto manual_check)"
else
	python3 $(FIRMWARE)/../manual/manual_check.py $(FIRMWARE)
endif

comprueba: manual

# Que ningun enlace ni ninguna imagen de los .md apunte a donde no hay nada.
# Ver tools/md_enlaces_check.py: HARDWARE.md llevaba meses abriendo con un
# ![](deepsdr_1.jpg) que no existe, y como iba a ser la primera linea que
# viese nadie al abrir el repositorio en GitHub, era un icono roto de
# portada. Ninguna comprobacion miraba DENTRO de los .md.
.PHONY: enlaces
enlaces:
	python3 $(FIRMWARE)/tools/md_enlaces_check.py $(FIRMWARE)

comprueba: enlaces

# La aritmetica del calendario del RTC. Ver sim/rtc_cal.c: rtc_hw.c se
# reescribio entero el 13/10 y toca registros, asi que aqui no se puede
# compilar; por eso las cuentas se sacaron a User/rtc_cal.c, que si.
out/rtccal: sim/rtc_cal.c $(FIRMWARE)/User/rtc_cal.c $(FIRMWARE)/User/rtc_cal.h
	$(CC) $(CFLAGS) -I $(FIRMWARE)/User -o $@ sim/rtc_cal.c $(FIRMWARE)/User/rtc_cal.c

.PHONY: rtccal
rtccal: out/rtccal ; ./out/rtccal

comprueba: rtccal

# Que el manual nombre TODOS los modos y los cuente bien. Ver
# tools/modos_manual_check.py: el manual abria su seccion 15 con "Los 22
# modos" cuando la tabla de main.c tenia 28, y le faltaban PKT300, DSC y
# HELL enteros, en los dos idiomas. manual_check.py no lo veia porque
# comprueba otra cosa -que las cadenas de pantalla esten documentadas- y
# un modo que nunca tuvo fila no deja ninguna cadena sin documentar.
.PHONY: modosman
modosman:
ifeq ($(HAY_MANUAL),)
	@echo "  (manual/ no esta en el repositorio: me salto modos_manual_check)"
else
	python3 $(FIRMWARE)/../manual/modos_manual_check.py $(FIRMWARE)
endif

comprueba: modosman

# Que no haya un cargador viejo rondando por el arbol. Ver la cabecera de
# tools/copias_bin_check.py: customboot.bin vivia en tres sitios con tres
# contenidos, y el README mandaba grabar el de rescate/, que llevaba dos
# versiones de retraso. Un manual atrasado se lee raro; un cargador
# atrasado se GRABA, y es la unica pieza que si sale mal deja la radio sin
# forma de arreglarse por USB.
.PHONY: copiasbin
copiasbin:
	python3 $(FIRMWARE)/tools/copias_bin_check.py

comprueba: copiasbin

# Banco del decodificador DCF77. Igual que el del blanker: User/dcf77.c del
# firmware no depende de nada del GD32 a proposito, justo para poder medirlo
# aqui con señal sintetica. Lo que comprueba de verdad es que SIN señal no
# se invente una hora.
out/dcf77test: sim/dcf77test.c $(FIRMWARE)/User/dcf77.c $(FIRMWARE)/User/rapido.c $(FIRMWARE)/User/hora_rbu.c $(FIRMWARE)/User/hora_wwv.c $(FIRMWARE)/User/hora_comun.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/dcf77test.c $(FIRMWARE)/User/dcf77.c $(FIRMWARE)/User/rapido.c $(FIRMWARE)/User/hora_rbu.c $(FIRMWARE)/User/hora_wwv.c $(FIRMWARE)/User/hora_comun.c $(LDLIBS)

dcf77: out/dcf77test ; ./out/dcf77test

comprueba: dcf77

# Banco del formato MSF (la otra mitad de User/dcf77.c). Va aparte del de
# DCF77 porque son dos formatos distintos con dos juegos de trampas
# distintos: paridades impares, BCD al reves y hora en UTC que hay que
# convertir. Comparten el motor de flancos, y por eso los dos tienen que
# pasar: un cambio en ese motor que arregle uno y rompa el otro se ve aqui.
out/msftest: sim/msftest.c $(FIRMWARE)/User/dcf77.c $(FIRMWARE)/User/rapido.c $(FIRMWARE)/User/hora_rbu.c $(FIRMWARE)/User/hora_wwv.c $(FIRMWARE)/User/hora_comun.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/msftest.c $(FIRMWARE)/User/dcf77.c $(FIRMWARE)/User/rapido.c $(FIRMWARE)/User/hora_rbu.c $(FIRMWARE)/User/hora_wwv.c $(FIRMWARE)/User/hora_comun.c $(LDLIBS)

msf: out/msftest ; ./out/msftest

comprueba: msf

# Render del trazo del espectro compilando el User/spectrum.c DE VERDAD, no
# la copia de vendor/. Esa copia lleva ahi desde antes y hoy solo se
# diferencia en un comentario, pero eso es suerte: una copia que nadie
# obliga a estar al dia acaba mintiendo, y las preguntas sobre como se
# dibuja una punta no admiten una respuesta sobre codigo que a lo mejor ya
# no es el que corre. Necesita hostinc/gd32f4xx.h, que es un sucedaneo con
# solo los tipos.
out/puntas: sim/puntas.c sim/lcd_sim.c vendor/gfx.c $(FIRMWARE)/User/spectrum.c gfx2/gfx2.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -I vendor -o $@ $(FUENTES) $(LDLIBS)

puntas: out/puntas ; ./out/puntas && $(MAKE) png

# Banco de ALS162 (Allouis, 162 kHz). El unico que va por FASE, y el unico
# donde la deriva del oscilador local es un problema de primer orden: a
# 162 kHz el temporizador del micro da pasos de ~190 Hz, y hay que demodular
# un radian por encima de eso. Por eso el banco emite la fase CON deriva, de
# 1 a 500 Hz, y no solo limpia.
out/alstest: sim/alstest.c $(FIRMWARE)/User/dcf77.c $(FIRMWARE)/User/rapido.c $(FIRMWARE)/User/hora_rbu.c $(FIRMWARE)/User/hora_wwv.c $(FIRMWARE)/User/hora_comun.c
	@mkdir -p out
	$(CC) $(CFLAGS) -D_GNU_SOURCE -I$(FIRMWARE)/User -o $@ sim/alstest.c $(FIRMWARE)/User/dcf77.c $(FIRMWARE)/User/rapido.c $(FIRMWARE)/User/hora_rbu.c $(FIRMWARE)/User/hora_wwv.c $(FIRMWARE)/User/hora_comun.c $(LDLIBS)

als: out/alstest ; ./out/alstest

# ---------------------------------------------------------------------
# ALS162 DE VERDAD. Ver la cabecera de sim/als_real.c.
#
# NACIO FUERA DE "comprueba" -como cwvara- porque lo que imprimia era una
# MEDIDA y no un veredicto: la grabacion decia 6,24 elementos por segundo
# contra los dos que admitia feed_fase(), y el modo no decodificaba nada.
#
# ENTRA EN "comprueba" EL 10/10/2026, cuando esa medida se convirtio en un
# arreglo y el banco en una pregunta con respuesta conocida: la grabacion
# del WebSDR trae su hora UTC en el nombre del fichero, asi que lo que se
# comprueba ya no es que la hora decodificada se parezca a la de antes,
# sino que salgan las 04:21 del sabado 10/10/26. Eso si es un veredicto, y de los buenos: no se compara contra
# nosotros mismos.
out/alsreal: sim/als_real.c $(FIRMWARE)/User/dcf77.c $(FIRMWARE)/User/rapido.c $(FIRMWARE)/User/hora_rbu.c $(FIRMWARE)/User/hora_wwv.c $(FIRMWARE)/User/hora_comun.c
	@mkdir -p out
	$(CC) $(CFLAGS) -D_GNU_SOURCE -I$(FIRMWARE)/User -o $@ sim/als_real.c $(FIRMWARE)/User/dcf77.c $(FIRMWARE)/User/rapido.c $(FIRMWARE)/User/hora_rbu.c $(FIRMWARE)/User/hora_wwv.c $(FIRMWARE)/User/hora_comun.c $(LDLIBS)

.PHONY: alsreal
alsreal: out/alsreal ; ./out/alsreal

# La fase DESDE I/Q, con ruido. Ver la cabecera de sim/als_iq.c: als_real.c
# mide la fase ya extraida -y filtrada a 40 Hz- por tools/als_fase.py, que
# es un paso que la radio no daba. Esto mide la cadena de verdad.
out/alsiq: sim/als_iq.c $(FIRMWARE)/User/dcf77.c $(FIRMWARE)/User/hora_rbu.c $(FIRMWARE)/User/hora_wwv.c $(FIRMWARE)/User/hora_comun.c $(FIRMWARE)/User/rapido.c
	@mkdir -p out
	$(CC) $(CFLAGS) -D_GNU_SOURCE -I$(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: alsiq
alsiq: out/alsiq ; ./out/alsiq
comprueba: alsiq

comprueba: alsreal

comprueba: als

# Banco del calendario compartido (User/hora_comun.c): dia de la semana, dia
# del año, y sobre todo el horario de verano europeo, que es lo que convierte
# el UTC de WWV y la hora de Moscu de RBU en hora de pared española. Se
# compara contra zoneinfo Europe/Madrid, que es una fuente independiente:
# marzo y octubre enteros hora a hora de 2000 a 2099.
# sim/cambios_generado.h se genera con "python3 tools/gen_cambios.py
# sim/cambios_generado.h" y va GUARDADO en el repositorio a proposito, sin
# regla que lo rehaga: hacerlo requiere la base de datos de husos horarios
# (zoneinfo), que en Windows no viene con Python y hay que instalar aparte.
# Que "make comprueba" dependiera de eso seria cambiar una comprobacion que
# funciona por una que a veces no compila.
out/caltest: sim/calendariotest.c sim/cambios_generado.h $(FIRMWARE)/User/hora_comun.c \
             $(FIRMWARE)/User/husos.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Isim -o $@ sim/calendariotest.c \
	    $(FIRMWARE)/User/hora_comun.c $(FIRMWARE)/User/husos.c $(LDLIBS)

calendario: out/caltest ; ./out/caltest

comprueba: calendario

# Banco del huso horario y de la tabla de ciudades (User/husos.c). Mismo
# planteamiento que el del calendario y por lo mismo: sim/husos_generado.h
# sale de zoneinfo con "python3 tools/gen_husos.py sim/husos_generado.h" y va
# GUARDADO en el repositorio, sin regla que lo rehaga, porque generarlo pide
# la base de husos del sistema que en Windows no viene con Python.
out/husotest: sim/husotest.c sim/husos_generado.h $(FIRMWARE)/User/husos.c \
              $(FIRMWARE)/User/hora_comun.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Isim -o $@ sim/husotest.c \
	    $(FIRMWARE)/User/husos.c $(FIRMWARE)/User/hora_comun.c $(LDLIBS)

huso: out/husotest ; ./out/husotest

comprueba: huso

# Banco de WWV (onda corta, subportadora de 100 Hz). Genera la envolvente con
# TODO lo que lleva encima la emisora de verdad: el tono de 500 Hz, los tics
# de segundo y la voz del anuncio horario, que es la que manda en el diseño
# del filtro.
out/wwvtest: sim/wwvtest.c $(FIRMWARE)/User/hora_wwv.c $(FIRMWARE)/User/hora_comun.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/wwvtest.c $(FIRMWARE)/User/hora_wwv.c $(FIRMWARE)/User/hora_comun.c $(LDLIBS)

wwv: out/wwvtest ; ./out/wwvtest

comprueba: wwv

# Banco de RBU (66,66 kHz, fase modulada por tonos). Ademas de la hora,
# comprueba que encuentra sola la alineacion de los elementos entre diez
# posibles y que aguanta la deriva del oscilador local.
out/rbutest: sim/rbutest.c $(FIRMWARE)/User/hora_rbu.c $(FIRMWARE)/User/hora_comun.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/rbutest.c $(FIRMWARE)/User/hora_rbu.c $(FIRMWARE)/User/hora_comun.c $(LDLIBS)

rbu: out/rbutest ; ./out/rbutest

comprueba: rbu

# Banco de BPM (China, 10 MHz). Comparte fichero con WWV -User/hora_wwv.c-
# porque BPM copio su formato, pero va aparte porque los anchos, el reparto
# de los bits y la subportadora son otros. Ademas es el unico sitio donde se
# comprueba que encuentra la subportadora este en 125 o en 100 Hz, que es el
# cabo suelto que no he podido cerrar con fuentes.
out/bpmtest: sim/bpmtest.c $(FIRMWARE)/User/hora_wwv.c $(FIRMWARE)/User/hora_comun.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/bpmtest.c $(FIRMWARE)/User/hora_wwv.c $(FIRMWARE)/User/hora_comun.c $(LDLIBS)

bpm: out/bpmtest ; ./out/bpmtest

comprueba: bpm

# =====================================================================
# EL CAMINO DE AUDIO, CORRIENDO DE VERDAD. 10/10/2026.
#
# demod_am.c era el unico fichero grande del arbol que NINGUN banco
# compilaba, y lo decia el propio fichero al lado de la union s_via:
#
#     "no esta probado en marcha, solo comprobado estaticamente. Es la
#      deuda que deja este cambio."
#
# Eran 4.096 bytes de RAM apoyados en un argumento de lectura. El
# argumento es bueno; lo que faltaba era alguien que lo viese pasar.
#
# Lo que hizo falta para poder compilarlo aqui:
#
#   - hostinc/arm_math.h, que solo tenia el FIR, ahora trae tambien el
#     biquad en forma directa I, el diezmador y el interpolador, copiando
#     el convenio del CMSIS al detalle y no "algo que filtre";
#   - sim/demod_stubs.c, los veintitantos decodificadores vacios y
#     contando, porque lo que se mide es EL REPARTO y no ellos;
#   - y sus modulos de verdad donde se puede: rapido, nco, nco_tabla, agc,
#     nb, audiofil, anotch, nr_ss y sam. Nueve ficheros del firmware
#     corriendo sin tocar una linea.
#
# Y de camino salio un fallo que llevaba meses tapado: arm_fir_f32 aplicaba
# los coeficientes AL REVES que el CMSIS. No se habia notado porque su
# unico usuario -el filtro adaptado de HFDL- es simetrico. El siguiente era
# el Hilbert de demod_am.c, que es ANTISIMETRICO, donde dar la vuelta es
# multiplicar por -1: la banda lateral contraria. De ahi el banco de abajo.
#
#   make armmath   el sucedaneo del CMSIS contra numeros hechos a mano
#   make demod     el camino de audio y la union s_via, en marcha
.PHONY: armmath demod

out/armmathtest: sim/armmath_test.c
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ $(FUENTES) $(LDLIBS)

armmath: out/armmathtest ; ./out/armmathtest

# El banco del CMSIS va ANTES que el del camino de audio y no al reves: si
# el sucedaneo esta mal, lo que diga demod_am.c sobre su propio audio no
# vale nada. Primero se comprueba la regla, luego lo que se mide con ella.
demod: armmath out/demodtest ; ./out/demodtest

DEMOD_REALES = $(FIRMWARE)/User/rapido.c $(FIRMWARE)/User/nco.c \
               $(FIRMWARE)/User/nco_tabla.c $(FIRMWARE)/User/agc.c \
               $(FIRMWARE)/User/nb.c $(FIRMWARE)/User/audiofil.c \
               $(FIRMWARE)/User/anotch.c $(FIRMWARE)/User/nr_ss.c \
               $(FIRMWARE)/User/sam.c

out/demodtest: sim/demodtest.c sim/demod_stubs.c $(FIRMWARE)/User/demod_am.c \
               $(DEMOD_REALES)
	@mkdir -p out
	$(CC) $(CFLAGS) -D_GNU_SOURCE -I $(FIRMWARE)/User -o $@ $(FUENTES) $(LDLIBS)

comprueba: demod
# =====================================================================

# Las copias que el simulador tiene del firmware, al dia (23/09/2026).
#
# El simulador compila su propia copia de 46 ficheros del firmware. Ninguna
# esta obligada a estar al dia, asi que se separan solas. Ya ha pasado tres
# veces, y la tercera fue en el propio banco de geometria: se ensancho la
# rejilla de ajustes, el banco dijo "TODO CORRECTO" y lo que habia medido era
# la copia vieja. Ver tools/copias_check.py.
#
#   make copias     pone las copias al dia desde el firmware
#   make comprueba  para si alguna se ha quedado atras
# Y ANTES QUE TODO ESO: QUE LA RAIZ SEA LA RAIZ. 10/10/2026.
#
# El Makefile trae "FIRMWARE ?= ../deepsdr", de cuando solo habia una rama.
# Hoy hay dos y las dos estan en la misma carpeta, al lado de esta:
#
#     ../deepsdr          la vieja, parada el 28/09
#     ../deepsdr-reload   la que corre en la radio
#
# O sea que "make comprueba" a secas, sin FIRMWARE=, compilaba y aprobaba EL
# FIRMWARE VIEJO. Y copiaschk comparaba las copias contra la rama vieja y
# decia "ninguno se ha quedado atras" - con razon: estaban al dia de la
# otra. Cuatro ficheros de vendor/ coinciden al byte con deepsdr/User/ y no
# con deepsdr-reload/User/ (gfx.c 10.001 contra 12.464, spectrum.c 64.064
# contra 80.847).
#
# raizchk va PRIMERO y es requisito de los demas: no comprueba nombres de
# carpeta, comprueba que esten los 139 ficheros que estos makefiles nombran
# bajo $(FIRMWARE). Si falta uno, dice cual y que carpeta hermana si los
# tiene. Ver tools/raiz_check.py.
.PHONY: raizchk
raizchk: ; @python3 tools/raiz_check.py $(FIRMWARE)

comprueba: raizchk

.PHONY: copias copiaschk

# Los dos cuelgan de raizchk, y no por orden: `make copias` con la raiz
# equivocada copiaria el firmware de septiembre ENCIMA de las copias buenas.
# La orden de arreglarlo era la que mas dano hacia. copias_check.py tambien
# lo comprueba por su cuenta, por si alguien lo lanza a mano.
copias:    raizchk ; python3 tools/copias_check.py $(FIRMWARE) --sincronizar
copiaschk: raizchk ; python3 tools/copias_check.py $(FIRMWARE)

comprueba: copiaschk

# EL ORDEN DE LAS EMISORAS Y EL MEGA DE BD.BIN - 05/10/2026.
#
# Es un banco de Python sobre tools/emisoras_pack.py y tools/datos_pack.py,
# que ahora viven en el firmware y se copian aqui (lo vigila copiaschk).
# No compila nada y tarda un par de segundos. Si estan eibi.txt y aoki.txt
# en datos/, ademas comprueba la tabla de paises contra las listas de
# verdad; si no estan, se salta esa parte y sigue valiendo.
# El nombre `emisoras` ya estaba cogido por el banco de User/emisoras.c
# (mas abajo), que es otra cosa: aquel mide como LEE la radio y este como
# se ESCRIBE el fichero.
.PHONY: emispack
emispack: ; python3 tools/emisoras_check.py datos
comprueba: emispack

# ---------------------------------------------------------------------
# QUE CADA CENTRO EMISOR ESTE EN SU PAIS - 10/10/2026.
#
# Nace de una aportacion de 593 sitios geolocalizados con Nominatim, donde
# quien la mandaba escribio "no se como probarlo contra el mapa para
# validarlo". Y llevaba razon: emisoras_check.py comprueba que el fichero
# parsee y que el empaquetado cuadre, pero nada miraba si una coordenada es
# la BUENA. Con esto salieron 43 filas en otro pais -"Gobo" en Camerun,
# "Iba" en Jerusalen, "Dushanbe" en una calle de Vilnius-.
#
# Ver la cabecera de tools/sitios_check.py para las dos cribas.
.PHONY: sitios
sitios: ; python3 $(FIRMWARE)/tools/sitios_check.py

comprueba: sitios

# EL "QUE HAY DE NUEVO" - 05/10/2026.
#
# NOVEDADES.md es el changelog corto y en cristiano, uno por version y
# bilingue. No se escribe solo -CAMBIOS.md si, porque se escribe mientras
# se arregla- asi que esto lo ata a CONFIG_FW_MENOR: si la version sube y
# la entrada no esta, para. Ver tools/novedades_check.py.
.PHONY: novedades
novedades: ; python3 tools/novedades_check.py $(FIRMWARE)/User/config.h $(FIRMWARE)/NOVEDADES.md
comprueba: novedades

# QUIEN SE QUEDA CON EL MANDO - 05/10/2026.
#
# De un issue: con el menu de ajustes abierto el volumen no respondia. El
# reparto del
# mando en tune_encoder_poll() es una ESCALERA y el volumen esta al final:
# cualquier pantalla que coja los detentes y vuelva antes le deja sin giro,
# mientras el boton -que se puede pulsar siempre- ya ha encendido la luz y
# sacado la caja. El orden cambia cada vez que se anade una pantalla, asi
# que esto se mide y no se recuerda. Ver tools/mando_check.py.
.PHONY: mando
mando: ; python3 tools/mando_check.py $(FIRMWARE)/User/main.c
comprueba: mando

# QUE LA RAMA B DEL PANEL NO CAMBIE SIN QUERER - 05/10/2026.
#
# Es la rama que usa la radio del usuario. El 05/10 se rehicieron las tres
# ramas sacandolas de ejecutar el boot.bin de serie, la B cambio por el
# camino, y su pantalla dejo de verse como se veia. Tres entregas seguidas
# diciendo "esto es equivalente" y las tres mal. Esto lo COMPARA contra la
# secuencia sacada del binario v2.2, que es la mirada en la radio y dada
# por buena. Ver tools/panel_rama_check.py.
.PHONY: ramab
ramab: ; python3 $(FIRMWARE)/tools/panel_rama_check.py $(FIRMWARE)/boot/panel_rama_b_v22.h $(FIRMWARE)/tools/panel_rama_b_oro.txt
comprueba: ramab

# La lista de ajustes y de paginas que miden los bancos, sacada de main.c
# (23/09/2026). Antes cada banco tenia su copia a mano: al anadir una pagina,
# los bancos seguian creyendo que habia cuatro y se caian con un fallo de
# segmentacion. Tuvieron suerte - una de mas habria pasado desapercibida.
# Ver tools/gen_ajustes.py.
sim/ajustes_tabla.h: $(FIRMWARE)/User/main.c tools/gen_ajustes.py
	python3 tools/gen_ajustes.py $(FIRMWARE)/User/main.c $@

out/configreal: sim/config.c sim/ajustes_tabla.h sim/peores.h sim/lcd_sim.c $(FIRMWARE)/User/ui_cfg.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ sim/config.c sim/lcd_sim.c \
	      gfx2/gfx2.c $(FIRMWARE)/User/ui_cfg.c gfx2/ui_grid.c gfx2/fonts_data.c $(LDLIBS)

# Se llama "configreal" y no "config" porque la regla de "config" vive en el
# Makefile, que no se puede editar desde aqui, y compila la COPIA de
# gfx2/ui_cfg.c. Esta compila el fichero del firmware directamente, que es lo
# que de verdad corre.
configreal: out/configreal ; ./out/configreal 0

comprueba: configreal

# Banco del analizador de audio (User/analizador.c). Un analizador es un
# instrumento: si miente, miente con pinta de dato. Se comprueban las cuatro
# cosas que lo harian mentir - que la frecuencia sea la frecuencia, que la
# banda sea plana, que no se invente rayas por alias, y que separe 100 de
# 125 Hz, que es la pregunta que motivo escribirlo.
out/aniztest: sim/aniztest.c $(FIRMWARE)/User/analizador.c $(FIRMWARE)/User/fft.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/aniztest.c \
	      $(FIRMWARE)/User/analizador.c $(FIRMWARE)/User/fft.c -lm

analiz: out/aniztest ; ./out/aniztest

comprueba: analiz

# Los temas, pintados de verdad. La tabla de temas se GENERA de main.c
# (tools/gen_temas.py) para que el simulador pinte los emparejamientos que
# lleva el firmware y no los que llevaba cuando se escribio esto.
.PHONY: temas temasgen
temasgen: ; python3 tools/gen_temas.py $(FIRMWARE) sim/temas_tabla.h
sim/temas_tabla.h: $(FIRMWARE)/User/main.c ; python3 tools/gen_temas.py $(FIRMWARE) sim/temas_tabla.h
temas: sim/temas_tabla.h out/principal
	@mkdir -p out/temas
	@i=0; n=`grep -c "^    { \"" sim/temas_tabla.h`; while [ $$i -lt $$n ]; do \
	  ./out/principal -1 -110 -20 0 $$i >/dev/null; \
	  cp out/principal.ppm out/temas/tema$$i.ppm; i=`expr $$i + 1`; done
	@python3 -c "from PIL import Image; import glob; \
[Image.open(p).save(p[:-4]+'.png') for p in glob.glob('out/temas/*.ppm')]"
	@echo "  los temas, en out/temas/"

# Las paletas, comprobadas: saltos de superficie por encima de lo que el
# panel resuelve y contraste de cada nivel de tinta sobre las dos
# superficies donde se escribe. Con los temas claros esto deja de ser un
# lujo: la rampa de tinta va al reves y una de por si plausible se queda en
# 2,8:1 sin que se note hasta tenerlo en la mano.
.PHONY: paletas
paletas: ; python3 tools/paleta_check.py gfx2/palette.h
comprueba: paletas

# ---------------------------------------------------------------------
# EL ESPECTRO, TEMA POR TEMA - 17/10/2026
#
# De un issue que pedia fondo claro para el espectro con los temas
# claros. Compila el User/spectrum.c DE VERDAD -no la copia de vendor/-
# porque lo que hay que medir es lo que pinta la radio, y renderiza la
# escena entera para LEER LOS PIXELES: mirar los #define contestaria a
# otra pregunta. Ver la cabecera de sim/espectro_tema.c.
#
# La mitad que mas importa no es que los diez nuevos se vean: es que los
# ocho de antes sigan pintando el mismo fondo, bit a bit.
out/espectrotema: sim/espectro_tema.c sim/lcd_sim.c vendor/gfx.c \
                  $(FIRMWARE)/User/spectrum.c gfx2/gfx2.c gfx2/fonts_data.c \
                  sim/temas_tabla.h
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -I vendor \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

#
# CUELGA DE copiaschk, Y HACE FALTA. gfx2/ y vendor/ tienen COPIAS de
# palette.h y spectrum.h, y en la orden de arriba -I gfx2 va DELANTE de
# -I $(FIRMWARE)/User: este banco compila contra la copia, no contra el
# original.
#
# Se vio rompiendolo a proposito. Cuatro roturas seguidas en
# User/palette.h -el acento de Lino casi igual al fondo, la rejilla negra,
# la rejilla igual al fondo, la traza del color del fondo- y el banco
# decia "TODO CORRECTO" a las cuatro, porque estaba midiendo la copia de
# antes de editarlas.
#
# Reordenar los -I NO es el arreglo: se probo y revienta. gfx2/gfx2.c
# necesita su propia gfx2.h, y mezclarla con la del firmware cambia el
# tamaño de las estructuras - compila y luego peta por la pila.
#
# El arreglo es este: copiaschk compara copia por copia y para en seco si
# alguna se ha quedado atras, asi que cuando este banco mide, la copia y
# el original son el mismo fichero.
.PHONY: espectrotema
espectrotema: copiaschk out/espectrotema ; ./out/espectrotema

# ---------------------------------------------------------------------
# LA FUENTE DEL SPLASH, LEIDA DE BD.BIN - 17/10/2026.
#
# font_rain_16 eran 5.592 bytes de flash interna para una pantalla de tres
# segundos y ahora vive en la zona alta, como una seccion mas de BD.BIN.
# Eso quiere decir que el firmware construye TRES PUNTEROS con numeros que
# vienen de un fichero que graba el usuario: si uno miente, lo siguiente
# que hace gfx2_text_in() es leer fuera del buffer, y en una radio eso no
# se ve como un BD.BIN malo sino como que no enciende.
#
# Por eso el banco no prueba que la buena cargue -esa es la mitad facil-
# sino que las rotas NO: marca cambiada, version desconocida, tamaños que
# mienten, un rango que apunta fuera de la tabla, la flash en blanco.
#
# El BD.BIN se fabrica aqui mismo con las dos herramientas de verdad, asi
# que si cambia el formato de una de las dos, esto se entera.
out/SPLF.BIN: $(FIRMWARE)/User/fonts_data.c $(FIRMWARE)/tools/fuente_pack.py
	@mkdir -p out
	@cd $(FIRMWARE) && python3 tools/fuente_pack.py font_rain_16 \
	        deepsdr-ui/out/SPLF.BIN >/dev/null

out/BD_fuente.bin: out/SPLF.BIN $(FIRMWARE)/tools/datos_pack.py
	@cd $(FIRMWARE) && python3 tools/datos_pack.py \
	        deepsdr-ui/out/BD_fuente.bin deepsdr-ui/out/SPLF.BIN >/dev/null

out/splashfuente: sim/splash_fuente_test.c $(FIRMWARE)/User/splash_fuente.c \
                  $(FIRMWARE)/User/zona_alta.c $(FIRMWARE)/User/splash_fuente.h
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -I vendor \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: splashfuente
splashfuente: out/splashfuente out/BD_fuente.bin
	./out/splashfuente out/BD_fuente.bin
comprueba: splashfuente
comprueba: espectrotema

# Que el update4.bin que se entrega sea el firmware que hay compilado, byte
# a byte. Ver la cabecera del script: se entrego uno viejo que tenia el
# tamano, la firma y el puntero de pila correctos. Comprobar la fecha no
# vale; el contenido si.
#
# EL FICHERO DE ACTUALIZACION SE LLAMA DISTINTO EN CADA RAMA. 02/10/2026.
#
# "deepsdr" lo llama update4.bin, que era el nombre que le ponia el gestor
# de fabrica. "deepsdr-reload" lo llama update.bin, por el usuario, ahora que
# el cargador es nuestro y no hay que heredar su nomenclatura.
#
# Se resuelve con un wildcard en vez de con una variable que haya que pasar
# a mano: asi "make FIRMWARE=... comprueba" sigue funcionando igual en las
# dos ramas sin que nadie tenga que acordarse. Si algun dia estuvieran los
# dos ficheros en el mismo arbol gana update.bin, que es el nuevo.
UPD = $(firstword $(wildcard $(FIRMWARE)/update.bin $(FIRMWARE)/update4.bin))

# ---------------------------------------------------------------------
# QUE LA RADIO SE ADAPTE AL CHIP QUE TENGA SOLDADO. 02/10/2026.
#
# El tamaño del disco USB y donde empieza la zona alta salen de medir el
# chip, asi que un W25Q64 da 7 MB de disco sin tocar una linea. Lo unico
# que puede corromper el volumen es anunciar de MAS, y eso es lo que este
# banco vigila en cuatro tamaños y en los dos casos raros (chip recien
# borrado, y chip que no se deja medir).
out/capacidad: sim/capacidad.c sim/w25q_sim.c sim/gd32_pines.c \
               $(FIRMWARE)/User/spi_flash.c $(FIRMWARE)/User/disco.c \
               $(FIRMWARE)/User/zona_alta.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -o $@ \
	      sim/capacidad.c sim/w25q_sim.c sim/gd32_pines.c \
	      $(FIRMWARE)/User/spi_flash.c $(FIRMWARE)/User/disco.c \
	      $(FIRMWARE)/User/zona_alta.c $(LDLIBS)

.PHONY: capacidad
capacidad: out/capacidad ; ./out/capacidad
comprueba: capacidad

# ---------------------------------------------------------------------
# GUARDAR LAS CARTAS DE FAX: EL BOTON MANDA. 20/10/2026.
#
# *** Esta regla se perdio al quitar el banco del ruido del bus: el corte se
# llevo por delante la linea de compilar, y durante unas horas "comprueba"
# ejecuto un BINARIO VIEJO que ya nadie reconstruia. Un banco que no se
# recompila es un banco que miente. ***
#
# Ver la cabecera de sim/fax_guarda.c.
sim/wfx_tabla.h: $(FIRMWARE)/User/main.c tools/gen_wfx.py
	python3 tools/gen_wfx.py $(FIRMWARE)/User/main.c $@

#
# QUE NADIE CIERRE EL FICHERO DE OTRO. Ver tools/gsv_dueno_check.py: es el
# guardian del fallo de los ficheros de 8 kB, que estuvo semanas sin que
# nada lo mirara. Probado a mano contra el fuente roto -los tres cierres de
# sstv_poll() y el nombre viejo- antes de darlo por bueno.
#
.PHONY: gsvdueno
gsvdueno: ; python3 tools/gsv_dueno_check.py $(FIRMWARE)/User/main.c
comprueba: gsvdueno

#
# QUE NO SE PUBLIQUEN CONVERSACIONES. tools/citas_check.py existe desde el
# 18/10/2026 y NO ESTABA COLGADO DE AQUI: nadie lo ejecutaba. Un guardian
# que no corre no es una red, es un fichero - que es justo lo que dice su
# propia cabecera sobre otro agujero.
#
# Mira la carpeta del firmware y las dos hermanas, porque docs/ y manual/
# tambien se escriben y una de ellas ya acumulo 94 citas sin que nada lo
# viera.
#
.PHONY: citas
citas: ; python3 $(FIRMWARE)/tools/citas_check.py $(FIRMWARE)
comprueba: citas

out/fax_guarda: sim/fax_guarda.c sim/wfx_tabla.h $(FIRMWARE)/User/wfx_guarda.h
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -o $@ sim/fax_guarda.c

.PHONY: fax_guarda
fax_guarda: out/fax_guarda ; ./out/fax_guarda
comprueba: fax_guarda

# ---------------------------------------------------------------------
# EL DIAL POR DEBAJO DE 300 kHz. 20/10/2026.
#
# No habia recepcion entre 78 kHz y 100 kHz. Recorre el dial con
# la logica de verdad -nco_lo_aparcado() y la aritmetica del temporizador-
# y mira si hay alguna frecuencia en la que la emisora se salga de la
# ventana del IQ. Ver la cabecera de sim/lf_hueco.c.
out/lf_hueco: sim/lf_hueco.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c \
              $(FIRMWARE)/User/lo_gen_gd32.h $(FIRMWARE)/User/nco.h
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -o $@ \
	      sim/lf_hueco.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c $(LDLIBS)

.PHONY: lf_hueco
lf_hueco: out/lf_hueco ; ./out/lf_hueco
comprueba: lf_hueco

#
# Y SE SALTA SI NO HAY FIRMWARE COMPILADO - 16/10/2026.
#
# Compara build/firmware.bin con update.bin, o sea que necesita que
# alguien haya compilado ANTES el firmware de verdad, con el compilador
# de ARM. En un repositorio recien clonado no hay ninguna de las dos
# cosas, y este objetivo tumbaba `make comprueba` entero con un "no
# existe ../build/firmware.bin" que no es un fallo de nadie.
#
# Lo mismo que ya se hace con `manual` y `modosman` cuando faltan las
# carpetas hermanas: si no esta el material, se dice y se sigue.
.PHONY: updatechk
updatechk:
	@if [ -f "$(FIRMWARE)/build/firmware.bin" ] && [ -f "$(UPD)" ]; then \
	    python3 tools/update_check.py $(FIRMWARE)/build/firmware.bin $(UPD); \
	 else \
	    echo "  (sin firmware compilado: me salto update_check.py."; \
	    echo "   Para pasarlo: make -C .. update)"; \
	 fi
comprueba: updatechk

# Banco del NCO. User/nco.c no depende de nada del GD32 a proposito - igual
# que nb.c o dcf77.c- para poder medirlo aqui de verdad.
out/ncotest: sim/ncotest.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/ncotest.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c $(LDLIBS)

nco: out/ncotest ; ./out/ncotest

# La captura de pantalla tiene que salir IGUAL que la pantalla. Ver la
# cabecera de sim/captura.c.
out/capturatest: sim/captura.c sim/lcd_sim.c vendor/gfx.c vendor/spectrum.c \
                 gfx2/gfx2.c gfx2/spec_chrome.c gfx2/ui_top.c gfx2/ui_act.c \
                 gfx2/fonts_data.c $(FIRMWARE)/User/waterfall.c sim/ipa_sim.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) $(VENDOR) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib -I sim/ipa -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: capturatest
capturatest: out/capturatest ; ./out/capturatest

ifneq ($(wildcard vendor/gfx.c),)
comprueba: capturatest
endif

# El eje de dB del espectro se repintaba 45 veces por segundo para dejar la
# pantalla igual. Ver sim/cromorepinta.c.
out/cromorepinta: sim/cromorepinta.c sim/lcd_sim.c gfx2/gfx2.c gfx2/spec_chrome.c \
                  gfx2/spec_agc.c gfx2/fonts_data.c vendor/spectrum.c vendor/gfx.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) $(VENDOR) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: cromorepinta
cromorepinta: out/cromorepinta ; ./out/cromorepinta

ifneq ($(wildcard vendor/gfx.c),)
comprueba: cromorepinta
endif

# SAM: que el atajo del PLL (tabla del NCO + arcotangente aproximado) de el
# mismo resultado que la cuenta exacta. Ver sim/samtest.c.
out/samtest: sim/samtest.c $(FIRMWARE)/User/sam.c $(FIRMWARE)/User/nco.c \
             $(FIRMWARE)/User/nco_tabla.c $(FIRMWARE)/User/rapido.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/samtest.c \
	      $(FIRMWARE)/User/sam.c $(FIRMWARE)/User/nco.c \
	      $(FIRMWARE)/User/nco_tabla.c $(FIRMWARE)/User/rapido.c $(LDLIBS)

.PHONY: sam
sam: out/samtest ; ./out/samtest

comprueba: sam

# La tabla de senos del NCO esta generada y vive en flash: que nadie la
# edite a mano y que siga siendo un seno. Ver tools/nco_tabla_check.py.
.PHONY: ncotabla
ncotabla: ; python3 tools/nco_tabla_check.py $(FIRMWARE)

comprueba: ncotabla

comprueba: nco

# Peor caso de la barra de estado: que los cuatro chips de estado -AGC, BW,
# NR y NCO- esten SIEMPRE, con el mando enganchado a cualquiera de los 37
# ajustes y con su peor valor. Un indicador que desaparece miente por
# omision, y hasta que esto se midio el del NCO se caia en catorce de los
# treinta y siete.
out/chipstest: sim/chipstest.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_top.c gfx2/fonts_data.c sim/peores.h $(FIRMWARE)/User/rds_pi.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ sim/chipstest.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_top.c gfx2/fonts_data.c $(FIRMWARE)/User/rds_pi.c $(LDLIBS)

chips: out/chipstest ; ./out/chipstest

comprueba: chips

# ---------------------------------------------------------------------
# sim/toptest.c ESTABA SIN REGLA - 13/10/2026.
#
# El fichero existia desde la etapa 2 y no lo compilaba nadie: no habia
# regla, asi que no entraba en "comprueba" ni se enteraba nadie de si
# seguia compilando. Entra hoy porque hacia falta un sitio donde medir el
# tanto por ciento dentro del dibujo de la bateria, pedido por el usuario, y el sitio natural era este.
#
# Y de paso trae lo suyo: el peor caso de la pastilla del mando y el de la
# lectura S+dBm, que llevaban desde entonces sin comprobarse.
out/toptest: sim/toptest.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_top.c \
             gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

top: out/toptest ; ./out/toptest

comprueba: top

# ---------------------------------------------------------------------
# EL RELOJ SOLO EN SU ESQUINA (20/10/2026).
#
# De una medida en antena: time_display_draw() repintaba la cabecera entera
# una vez por minuto -91.003 accesos al bus para cambiar cinco caracteres- y
# se llevaba 109 ms de una vuelta de 168. Esto comprueba que pintar solo la
# esquina deja los 384.000 pixeles del marco EXACTAMENTE igual, y que sigue
# costando menos de 12.000 accesos. Ver la cabecera del .c.
out/horaesquina: sim/hora_esquina.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_top.c \
                 gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

.PHONY: horaesquina
horaesquina: out/horaesquina ; ./out/horaesquina

comprueba: horaesquina

# ---------------------------------------------------------------------
# Que nadie baje una bandera de repintado sin mirar si se ha pintado. La
# logica vive en main.c, que este simulador no compila, asi que se lee el
# fuente - igual que via_audio_check.py y tapado_check.py. Ver el
# comentario de la propia herramienta.
repinta:
	python3 $(FIRMWARE)/tools/repinta_check.py $(FIRMWARE)/User/main.c

comprueba: repinta

# Banco del RDS. rds.c y nco.c no dependen de nada del GD32 a proposito, asi
# que el banco mide EL MISMO codigo que corre en la placa - incluida la
# division polinomica del CRC, que el banco llama en vez de reimplementar.
out/rdstest: sim/rdstest.c $(FIRMWARE)/User/rds.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/rdstest.c $(FIRMWARE)/User/rds.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c $(LDLIBS)

rds: out/rdstest ; ./out/rdstest

comprueba: rds

# Aritmetica del reloj (User/reloj.c). Compila el fichero del firmware, no una
# copia: ver el comentario de cabecera de User/reloj.h.
out/relojtest: sim/relojtest.c $(FIRMWARE)/User/reloj.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/relojtest.c $(FIRMWARE)/User/reloj.c

reloj: out/relojtest ; ./out/relojtest

comprueba: reloj

# NAVTEX (SITOR-B). Compila User/navtex.c del firmware y fabrica la señal con
# la misma tabla del alfabeto que usa el decodificador - ver sim/navtextest.c.
out/navtextest: sim/navtextest.c $(FIRMWARE)/User/navtex.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/navtextest.c $(FIRMWARE)/User/navtex.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c $(LDLIBS)

NAVTEX_MUESTRAS := out/muestras/sitorb_tfc.wav out/muestras/navtex8424.wav

navtex: out/navtextest $(NAVTEX_MUESTRAS)
	./out/navtextest $(NAVTEX_MUESTRAS)

comprueba: navtex

# WEFAX. Compila User/wefax.c del firmware; deja las imagenes en out/*.pgm
# para poder MIRARLAS, que en un decodificador de imagen es media medida.
out/wefaxtest: sim/wefaxtest.c $(FIRMWARE)/User/wefax.c $(FIRMWARE)/User/disc_fm.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/wefaxtest.c $(FIRMWARE)/User/wefax.c $(FIRMWARE)/User/disc_fm.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c $(LDLIBS)

wefax: out/wefaxtest ; ./out/wefaxtest

comprueba: wefax

# SSTV. Compila User/sstv.c del firmware y fabrica la señal con sus mismos
# tiempos; deja las imagenes en out/*.ppm para poder mirarlas.
out/sstvtest: sim/sstvtest.c sim/sstv_genera.h $(FIRMWARE)/User/sstv.c $(FIRMWARE)/User/disc_fm.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/sstvtest.c $(FIRMWARE)/User/sstv.c $(FIRMWARE)/User/disc_fm.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c $(LDLIBS)

# hell.c guarda TODO su estado en la RAM prestada (ver hell_start()), asi
# que el banco no enlazaba: le faltaban hfdl_ram_coge/todo/suelta. Y como
# helltest NO colgaba de "comprueba", llevaba roto desde V3.75 sin que nadie
# se enterase. Un banco que no corre no puede fallar; uno que ni enlaza,
# menos.
#
# El hfdl_ram.c de verdad no vale aqui: arrastra waterfall.h y con el todo
# el arbol de FT8. El banco se pone su propio sustituto, que ademas CUENTA
# las peticiones y las sueltas y comprueba que cuadran - cosa que el de
# verdad no hace.
out/helltest: sim/helltest.c $(FIRMWARE)/User/hell.c $(FIRMWARE)/User/hell.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/helltest.c $(FIRMWARE)/User/hell.c $(LDLIBS)


hell: out/helltest ; ./out/helltest
comprueba: hell

out/cwaire: sim/cwaire.c $(FIRMWARE)/User/cw.c $(FIRMWARE)/User/cw.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/cwaire.c $(FIRMWARE)/User/cw.c $(LDLIBS)

out/sstvaire: sim/sstvaire.c $(FIRMWARE)/User/sstv.c $(FIRMWARE)/User/disc_fm.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c $(FIRMWARE)/User/sstv.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/sstvaire.c $(FIRMWARE)/User/sstv.c $(FIRMWARE)/User/disc_fm.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c $(LDLIBS)

sstv: out/sstvtest ; ./out/sstvtest

comprueba: sstv

# ---------------------------------------------------------------------
# ADIVINAR EL MODO DE SSTV SIN CABECERA (11/10/2026). Ver sim/sstv_auto.c.
#
# El generador de senal lo comparten los dos bancos a traves de
# sim/sstv_genera.h: dos copias se desincronizan, y entonces un banco mide
# una senal que el otro no emite.
out/sstvauto: sim/sstv_auto.c sim/sstv_genera.h $(FIRMWARE)/User/sstv.c \
              $(FIRMWARE)/User/disc_fm.c $(FIRMWARE)/User/nco.c \
              $(FIRMWARE)/User/nco_tabla.c $(FIRMWARE)/User/sstv.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/sstv_auto.c \
	      $(FIRMWARE)/User/sstv.c $(FIRMWARE)/User/disc_fm.c \
	      $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c $(LDLIBS)

.PHONY: sstvauto
sstvauto: out/sstvauto ; ./out/sstvauto

# ---------------------------------------------------------------------
# Que un hueco en el audio estropee UN renglon y no una zona. Por el video
# del 13/10/2026: la foto se corrompe a trozos con la señal perfecta, y lo
# que falta no es señal, son muestras. Ver la cabecera del banco.
out/sstvhueco: sim/sstv_hueco.c sim/sstv_genera.h $(FIRMWARE)/User/sstv.c \
               $(FIRMWARE)/User/disc_fm.c $(FIRMWARE)/User/nco.c \
               $(FIRMWARE)/User/nco_tabla.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -o $@ \
	    sim/sstv_hueco.c $(FIRMWARE)/User/sstv.c $(FIRMWARE)/User/disc_fm.c \
	    $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c $(LDLIBS)

sstvhueco: out/sstvhueco ; ./out/sstvhueco

comprueba: sstvhueco

comprueba: sstvauto

# AX.25 / APRS. Compila User/ax25.c del firmware y monta las tramas con su
# misma comprobacion - ver sim/ax25test.c.
out/ax25test: sim/ax25test.c $(FIRMWARE)/User/ax25.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/ax25test.c $(FIRMWARE)/User/ax25.c $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c $(LDLIBS)

ax25: out/ax25test ; ./out/ax25test

comprueba: ax25

# El reparto de prioridades de interrupcion - ver tools/irq_check.py para la
# historia de por que hace falta vigilarlo.
irq: ; python3 tools/irq_check.py $(FIRMWARE)

comprueba: irq

# La pantalla de MODO, pintada y medida. Igual que la de ajustes: la tabla
# se GENERA de main.c (tools/gen_modos.py) para que el banco mida los modos
# que lleva el firmware y no los que llevaba cuando se escribio esto.
sim/modos_tabla.h: $(FIRMWARE)/User/main.c tools/gen_modos.py
	python3 tools/gen_modos.py $(FIRMWARE)/User/main.c $@

out/modostest: sim/modos.c sim/modos_tabla.h sim/lcd_sim.c $(FIRMWARE)/User/ui_cfg.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -Isim -o $@ sim/modos.c sim/lcd_sim.c \
	      gfx2/gfx2.c $(FIRMWARE)/User/ui_cfg.c gfx2/ui_grid.c gfx2/fonts_data.c $(LDLIBS)

modos2: out/modostest ; ./out/modostest

comprueba: modos2

# EL ALMACEN DE IMAGENES SE FUE EL 08/10/2026, y con el su banco.
#
# User/img_store.c nunca llego a usarse: las capturas y los SSTV van al
# disco USB como BMP (ver main.c, "Probar imagenes"), y el enlazador tiraba
# el modulo entero -2.380 bytes en 26 funciones- por no alcanzable. O sea
# que este banco medía codigo muerto, y un banco que mide codigo que no
# corre es peor que no tenerlo: da confianza sobre algo que no existe.
#
# Si algun dia hacen falta imagenes en la flash, el modulo esta en el
# historial de CAMBIOS.md con su banco.

# Anchos de la pantalla de Informacion. Ver la cabecera de sim/infotest.c:
# la etapa 33 le metio cinco celdas cuyos textos arma main.c al vuelo, y
# una celda que se sale no avisa, se pinta encima de la de al lado.
sim/volmsg_tabla.h: $(FIRMWARE)/User/spi_flash.c tools/gen_volmsg.py
	python3 tools/gen_volmsg.py $(FIRMWARE)/User/spi_flash.c $@

out/infotest: sim/infotest.c sim/volmsg_tabla.h
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ sim/infotest.c sim/lcd_sim.c \
	      gfx2/gfx2.c gfx2/fonts_data.c $(LDLIBS)

info: out/infotest ; ./out/infotest

comprueba: info

# Notch automatico. anotch.c no depende de nada del GD32 a proposito, asi
# que el banco mide EL MISMO codigo que corre en la placa. Ver la cabecera
# de sim/anotchtest.c: lo que se mide es un PAR -cuanto quita el pitido y
# cuanto se lleva de la voz- porque mirar solo lo primero elige mal.
out/anotchtest: sim/anotchtest.c $(FIRMWARE)/User/anotch.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/anotchtest.c $(FIRMWARE)/User/anotch.c $(LDLIBS)

anotch: out/anotchtest ; ./out/anotchtest

comprueba: anotch

# Filtro de audio de dos cortes. audiofil.c no depende de nada del GD32,
# asi que el banco mide EL MISMO diseñador que corre en la placa - y lo
# mide PASANDOLE SEÑAL, no con su propia formula. Ver la cabecera de
# sim/audiofiltest.c para por que eso importa.
out/audiofiltest: sim/audiofiltest.c $(FIRMWARE)/User/audiofil.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/audiofiltest.c $(FIRMWARE)/User/audiofil.c $(LDLIBS)

audiofil: out/audiofiltest ; ./out/audiofiltest

comprueba: audiofil

# La pantalla de detalle, compilando el User/ui_det.c DEL FIRMWARE y no la
# copia de gfx2/ - ver la cabecera de sim/detreal.c. Comprueba que la fila
# de ajustes rapidos de la etapa 34 no se pise con los botones grandes ni
# con el pie, y que sus tres botones respondan donde se dibujan.
out/detreal: sim/detreal.c sim/lcd_sim.c $(FIRMWARE)/User/ui_det.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User $(GFX2) -o $@ sim/detreal.c sim/lcd_sim.c \
	      gfx2/gfx2.c $(FIRMWARE)/User/ui_det.c gfx2/fonts_data.c $(LDLIBS)

detreal: out/detreal ; ./out/detreal

comprueba: detreal

# La frontera de la pantalla de Informacion. Ver la cabecera de
# tools/info_check.py: la pagina 1 abre al instante solo mientras ninguna
# de sus filas toque la flash SPI, y eso no lo puede ver el compilador.
.PHONY: infochk
infochk: ; python3 tools/info_check.py $(FIRMWARE)/User/main.c

comprueba: infochk

# Anadir un trozo a un fichero del volumen FAT. Ver la cabecera de
# sim/fatanade.c: debajo hay un W25Q16 simulado A NIVEL DE PIN
# (sim/w25q_sim.c), asi que el spi_flash.c que corre aqui es el del
# firmware sin una linea cambiada, bit-banging incluido. Y al volumen que
# sale de cada pulsacion le pasa fsck.fat, que no sabe nada de este
# proyecto y por eso vale como tribunal.
out/fat12_vacio.img:
	@mkdir -p out
	dd if=/dev/zero of=$@ bs=512 count=2048 status=none
	mkfs.fat -F 12 -R 4 -f 2 -r 512 -s 1 -S 512 -n DEEPSDR $@ >/dev/null

# Y el mismo volumen con la distribucion que pone un PC al formatearlo: 1
# sector reservado en vez de 4, con lo que el area de datos deja de caer en
# frontera de bloque de borrado. El driver tiene que NEGARSE a escribir ahi.
out/fat12_pc.img:
	@mkdir -p out
	dd if=/dev/zero of=$@ bs=512 count=2048 status=none
	mkfs.fat -F 12 -R 1 -f 2 -r 512 -s 1 -S 512 -n PRUEBAPC $@ >/dev/null

# Y EL QUE MOTIVO TODO ESTO (02/10/2026): los 7 MB de un W25Q64 formateados
# con lo que Windows propone por su cuenta. Los numeros no son inventados,
# son los del cuadro de dialogo que se vio en pantalla: clusters de
# 2048, FAT de 6144 bytes (la de antes solo cabia hasta 4096) y area de
# datos en el byte 30.720, que es frontera de 2 kB pero NO de los 4 kB del
# bloque de borrado. Los dos motivos por los que la radio decia que no.
# Y UN VOLUMEN FORMATEADO MAS GRANDE QUE EL DISCO (18/10/2026). Se formatea
# a 1,25 MB y se corta a 1 MB: el BPB declara 2.560 sectores y el disco tiene
# 2.048. Es lo que queda de un formateo hecho cuando la capacidad se media
# mal, y el driver tiene que dejarlo LEER y no ESCRIBIR. Ver sim/chip_chico.c.
out/fat12_inflado.img:
	@mkdir -p out
	dd if=/dev/zero of=$@ bs=512 count=2560 status=none
	mkfs.fat -F 12 -R 4 -f 2 -r 512 -s 1 -S 512 -n INFLADO $@ >/dev/null
	truncate -s 1048576 $@

out/fat12_2k.img:
	@mkdir -p out
	dd if=/dev/zero of=$@ bs=512 count=14336 status=none
	mkfs.fat -F 12 -R 4 -f 2 -r 512 -s 4 -S 512 -n DEEPSDR2K $@ >/dev/null

# Y EL CHIP MAS GRANDE QUE ADMITE EL DRIVER: un W25Q128, 16 MB, o sea 15 de
# disco. Es el tope y es duro - el comando de lectura lleva la direccion en
# tres bytes y tres bytes no pasan de 16 MB-, asi que este volumen es el
# ultimo que hay que saber manejar y conviene tenerlo probado ANTES de que
# alguien suelde el chip.
#
# Y aqui salio un fallo de verdad (02/10/2026): mkfs.fat reserva 8.192
# bytes de FAT para una tabla que solo usa 5.750, que es legal, y el driver
# miraba el tamaño DECLARADO en vez del usado y rechazaba el volumen. Ver
# FAT_UTIL en spi_flash.c.
out/fat12_16m.img:
	@mkdir -p out
	dd if=/dev/zero of=$@ bs=512 count=30720 status=none
	mkfs.fat -F 12 -R 6 -f 2 -r 512 -s 8 -S 512 -n DEEPSDR16 $@ >/dev/null

out/fatanade: sim/fatanade.c sim/wfx_tabla.h sim/w25q_sim.c sim/gd32_pines.c $(FIRMWARE)/User/spi_flash.c $(FIRMWARE)/User/bmp.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/fatanade.c sim/w25q_sim.c sim/gd32_pines.c \
	      $(FIRMWARE)/User/spi_flash.c $(FIRMWARE)/User/bmp.c $(LDLIBS)

# Si se cambia FIRMWARE (probar un arbol roto a proposito y volver), el
# binario de out/ es mas nuevo que los fuentes del arbol nuevo y make no
# lo reconstruye: acabas comparando el mismo binario contigo mismo y la
# prueba negativa miente en las DOS direcciones. Se apunta con que arbol
# se construyo y se rehace si ha cambiado.
fatanade: out/fat12_vacio.img out/fat12_pc.img out/fat12_2k.img out/fat12_16m.img
	@if [ "$$(cat out/fatanade.arbol 2>/dev/null)" != "$(FIRMWARE)" ]; then 	   rm -f out/fatanade; fi
	@$(MAKE) --no-print-directory out/fatanade FIRMWARE=$(FIRMWARE)
	@echo "$(FIRMWARE)" > out/fatanade.arbol
	rm -f out/fatanade_*.img out/fatotro*.img
	./out/fatanade out/fat12_vacio.img
	python3 tools/fat_check.py out sim/fatanade.c
	@echo '  -- y fsck.fat sobre los volumenes de otra distribucion --'
	@for i in out/fatotro*.img; do \
	   fsck.fat -n $$i >/dev/null 2>&1 && echo "  $$i  fsck limpio   ok" \
	   || { echo "  $$i  fsck SE QUEJA   MAL"; exit 1; }; \
	 done

comprueba: fatanade

# ---------------------------------------------------------------------
# CARGAR UN FICHERO TROCEADO (28/09/2026)
#
# Pedido por el usuario.
#
# Y estaba: lo que fallaba es que el cargador exigia los clusters
# seguidos. Ver la cabecera de sim/fat_troceado.c. Igual que fatanade,
# debajo hay un W25Q16 simulado A NIVEL DE PIN, asi que el spi_flash.c
# que corre aqui es el del firmware sin una linea cambiada.
out/fattroceado: sim/fat_troceado.c sim/w25q_sim.c sim/gd32_pines.c $(FIRMWARE)/User/spi_flash.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/fat_troceado.c sim/w25q_sim.c sim/gd32_pines.c \
	      $(FIRMWARE)/User/spi_flash.c $(LDLIBS)

# El mismo cuidado que fatanade con el arbol: ver alli el porque.
.PHONY: fattroceado
fattroceado: out/fat12_vacio.img
	@if [ "$$(cat out/fattroceado.arbol 2>/dev/null)" != "$(FIRMWARE)" ]; then \
	   rm -f out/fattroceado; fi
	@$(MAKE) --no-print-directory out/fattroceado FIRMWARE=$(FIRMWARE)
	@echo "$(FIRMWARE)" > out/fattroceado.arbol
	./out/fattroceado out/fat12_vacio.img

comprueba: fattroceado

# ---------------------------------------------------------------------
# BORRAR BD.BIN DEL DISCO SIN LLEVARSE EL DISCO (18/10/2026)
#
# De un issue con cuatro palabras: el mapa se graba bien, BD.BIN da un
# error al grabarse y despues el disco queda corrupto. El ritual de los dos
# es el mismo y solo UNO de sus pasos escribe en el volumen -el borrado-,
# asi que ahi es donde se mira. Ver la cabecera de
# sim/bd_borra.c: lo que distingue a BD.BIN del mapa son 920 eslabones de
# cadena contra 16.
#
# Se corre sobre las TRES distribuciones que el driver admite, porque la
# FAT cae en sitio distinto en cada una y cruza las fronteras de borrado
# por otro sitio. Y a los volumenes que deja les pasa fsck.fat, que no
# sabe nada de este proyecto - fue el quien encontro lo de las entradas de
# nombre largo huerfanas.
out/bdborra: sim/bd_borra.c sim/w25q_sim.c sim/gd32_pines.c \
             $(FIRMWARE)/User/spi_flash.c $(FIRMWARE)/User/zona_alta.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/bd_borra.c sim/w25q_sim.c sim/gd32_pines.c \
	      $(FIRMWARE)/User/spi_flash.c $(FIRMWARE)/User/zona_alta.c $(LDLIBS)

# El mismo cuidado que fatanade con el arbol: ver alli el porque.
.PHONY: bdborra
bdborra: out/fat12_vacio.img out/fat12_2k.img out/fat12_16m.img
	@if [ "$$(cat out/bdborra.arbol 2>/dev/null)" != "$(FIRMWARE)" ]; then \
	   rm -f out/bdborra; fi
	@$(MAKE) --no-print-directory out/bdborra FIRMWARE=$(FIRMWARE)
	@echo "$(FIRMWARE)" > out/bdborra.arbol
	@for img in out/fat12_vacio.img out/fat12_2k.img out/fat12_16m.img; do \
	   ./out/bdborra $$img out/bdborra_despues.img $(FIRMWARE)/BD.BIN || exit 1; \
	   python3 tools/bdborra_check.py out/bdborra_antes.img \
	           out/bdborra_despues.img || exit 1; \
	 done

comprueba: bdborra

# ---------------------------------------------------------------------
# LA ZONA ALTA NO PUEDE CAER DENTRO DEL DISCO (18/10/2026)
#
# Del mismo issue que bdborra, y es la causa de verdad: el suelo de 2 MB
# de spi_flash_capacidad() subia una medida buena, asi que un chip de
# 1 MB se anunciaba como de 2 y za_base() daba la vuelta hasta dentro del
# volumen. Ver la cabecera de sim/chip_chico.c, que hace el ritual entero
# y cuenta los bytes del disco que cambian: tienen que ser cero.
out/chipchico: sim/chip_chico.c sim/w25q_sim.c sim/gd32_pines.c \
               $(FIRMWARE)/User/spi_flash.c $(FIRMWARE)/User/disco.c \
               $(FIRMWARE)/User/zona_alta.c $(FIRMWARE)/User/formato.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/chip_chico.c sim/w25q_sim.c sim/gd32_pines.c \
	      $(FIRMWARE)/User/spi_flash.c $(FIRMWARE)/User/disco.c \
	      $(FIRMWARE)/User/zona_alta.c $(FIRMWARE)/User/formato.c $(LDLIBS)

.PHONY: chipchico
chipchico: out/fat12_vacio.img out/fat12_inflado.img
	@if [ "$$(cat out/chipchico.arbol 2>/dev/null)" != "$(FIRMWARE)" ]; then \
	   rm -f out/chipchico; fi
	@$(MAKE) --no-print-directory out/chipchico FIRMWARE=$(FIRMWARE)
	@echo "$(FIRMWARE)" > out/chipchico.arbol
	./out/chipchico out/fat12_vacio.img out/fat12_inflado.img

comprueba: chipchico

# ---------------------------------------------------------------------
# COPIAR UN FICHERO GRANDE DESDE WINDOWS (18/10/2026)
#
# La tercera pata del mismo issue. Con el borrado y el ritual de carga ya
# medidos y limpios, lo que quedaba era el camino por el que el fichero
# LLEGA al disco: disco_escribe_n(), la clase MSC del cargador. sim/disco.c
# mira un bloque suelto y una tirada de ocho; esto replica la copia ENTERA
# de un BD.BIN de verdad en tiradas de longitud y alineacion variables, y
# exige que el megabyte quede IDENTICO a la referencia. Ver la cabecera de
# sim/disco_fichero.c.
out/discofichero: sim/disco_fichero.c sim/w25q_sim.c sim/gd32_pines.c \
                  $(FIRMWARE)/User/spi_flash.c $(FIRMWARE)/User/disco.c \
                  $(FIRMWARE)/User/zona_alta.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/disco_fichero.c sim/w25q_sim.c sim/gd32_pines.c \
	      $(FIRMWARE)/User/spi_flash.c $(FIRMWARE)/User/disco.c \
	      $(FIRMWARE)/User/zona_alta.c $(LDLIBS)

.PHONY: discofichero
discofichero: out/fat12_vacio.img
	@if [ "$$(cat out/discofichero.arbol 2>/dev/null)" != "$(FIRMWARE)" ]; then \
	   rm -f out/discofichero; fi
	@$(MAKE) --no-print-directory out/discofichero FIRMWARE=$(FIRMWARE)
	@echo "$(FIRMWARE)" > out/discofichero.arbol
	./out/discofichero out/fat12_vacio.img $(FIRMWARE)/BD.BIN out/discofichero.img

comprueba: discofichero

# ---------------------------------------------------------------------
# LA PANTALLA DE ARRANQUE (28/09/2026)
#
# Pedido por el usuario.
#
# Se quito el 25/09 porque HFDL necesitaba flash, y con ella se fue esta
# regla. Vuelve con ella: una queja visual no se contesta compilando, se
# contesta mirando, y esto saca la animacion a ficheros que se abren.
#
# Los tres momentos que importan: la lluvia sola, el titulo recien
# aparecido, y el final. Mas el camino de "no hay memoria que prestar",
# que tiene que dar la pantalla fija y no una rota.
out/splashtest: sim/splashtest.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
                $(FIRMWARE)/User/splash.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -Igfx2 -o $@ $^ $(LDLIBS)

.PHONY: splash
splash: out/splashtest
	./out/splashtest 900
	./out/splashtest 1600
	./out/splashtest 3500
	./out/splashtest 3500 2      # sin memoria prestada: la pantalla fija

comprueba: splash

# Cabeceras BMP. Ver la cabecera de sim/bmptest.c: los ficheros que escribe
# se los lee Pillow, que es un lector de imagenes de otra gente y por eso
# vale como tribunal. Una cabecera de BMP mal puesta da un fichero que casi
# funciona, y eso no se ve mirando el codigo.
out/bmptest: sim/bmptest.c $(FIRMWARE)/User/bmp.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/bmptest.c $(FIRMWARE)/User/bmp.c $(LDLIBS)

bmp: out/bmptest
	./out/bmptest
	python3 tools/bmp_check.py

comprueba: bmp

# Los rotulos del segundo boton del panel digital, sacados de main.c para
# que sim/digi.c mida LOS DE VERDAD. Ver tools/gen_rotulos.py.
sim/rotulos_tabla.h: $(FIRMWARE)/User/main.c tools/gen_rotulos.py
	python3 tools/gen_rotulos.py $(FIRMWARE)/User/main.c $@

out/digi: sim/rotulos_tabla.h

# ---------------------------------------------------------------------
# EL REPINTADO POR RENGLONES SUCIOS, PIXEL A PIXEL (19/10/2026)
#
# ui_digi_draw_cambios() solo repinta lo que ha cambiado, y eso abre la
# puerta al fallo clasico de los repintados parciales: que lo que queda en
# pantalla no sea lo que habria quedado repintando entero. No avisa: no
# sale nada raro, sale lo de antes.
#
# Este banco pinta cada escena por los dos caminos y compara los dos
# lienzos pixel a pixel. Ver la cabecera de sim/digi_sucios.c.
out/digisucios: sim/digi_sucios.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
                $(FIRMWARE)/User/ui_digi.c gfx2/ui_digi.h
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: digisucios
digisucios: out/digisucios ; ./out/digisucios

comprueba: digisucios

# ---------------------------------------------------------------------
# LA CHAPA DE LAS TRES CABECERAS DE IMAGEN (19/10/2026)
#
# WEFAX, SSTV y Feld-Hell arman su cabecera a mano, fuera de digi_sync(), y
# la chapa recorta POR DELANTE lo que no le cabe. En esas tres lo de delante
# es la palabra que dice en que fase va, asi que recortar la deja diciendo
# otra cosa sin avisar: "Esperando el tono de arranque" se leia "ono de
# arranque". Avisado por el usuario.
#
# Los estados no estan copiados en el banco: los saca gen_chapas.py de
# main.c, igual que gen_rotulos.py con los rotulos del segundo boton y por
# la misma razon. Ver la cabecera de sim/chapa_fax.c.
sim/chapas_tabla.h: $(FIRMWARE)/User/main.c tools/gen_chapas.py
	python3 tools/gen_chapas.py $(FIRMWARE)/User/main.c $@

out/chapafax: sim/chapa_fax.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
              $(FIRMWARE)/User/ui_digi.c sim/chapas_tabla.h gfx2/ui_digi.h
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: chapafax
chapafax: out/chapafax ; ./out/chapafax

comprueba: chapafax

# ---------------------------------------------------------------------
# Imagen patron del espectro (24/09/2026). Ver la cabecera de
# sim/espectro_oro.c: existe para poder reescribir el paso 3 de
# spectrum_draw() -el bucle que decide el color de cada uno de los 157.248
# pixeles del espectro, y que se come 53 de los 69 ms del fotograma- sin
# cambiar ni un pixel de lo que sale en pantalla.
#
# La referencia (sim/espectro_oro.dat) se graba UNA VEZ, con el codigo
# bueno, y a partir de ahi manda ella:
#
#   ./out/espectrooro --graba     regraba la referencia (solo a proposito)
#   make espectrooro              compara contra ella
#
# DOS PASADAS desde el 05/10/2026, y contra la MISMA referencia:
#
#   1. con el repintado parcial, que es como pinta la radio
#   2. --sinparcial, que es como pintaba antes de que existiera
#
# El repintado parcial se salta las bandas de filas que no han cambiado
# desde el cuadro anterior (ver "LAS BANDAS QUE NO HACE FALTA VOLCAR" en
# spectrum.c). Si las dos pasadas cuadran con la imagen patron, entonces el
# atajo pinta exactamente lo que pintaba el camino largo.
#
# Dos EJECUCIONES y no dos vueltas dentro de una: el espectro arrastra
# estado de un cuadro al siguiente -media temporal, retencion de picos-, y
# el primer intento de compararlos dentro del mismo proceso reventó nueve
# casos de doce por eso.
out/espectrooro: sim/espectro_oro.c sim/lcd_sim.c vendor/gfx.c $(FIRMWARE)/User/spectrum.c gfx2/gfx2.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -I vendor -o $@ $(FUENTES) $(LDLIBS)

.PHONY: espectrooro
espectrooro: out/espectrooro ; ./out/espectrooro && ./out/espectrooro --sinparcial

ifneq ($(wildcard vendor/gfx.c),)
comprueba: espectrooro
endif

# ---------------------------------------------------------------------
# DONDE SE VAN LOS MILISEGUNDOS DE LA TRAZA (20/10/2026).
#
# No es una comprobacion, es una MEDIDA, y por eso no cuelga de
# `comprueba`: lo que saca depende del reloj del PC. Lo que no depende es
# el REPARTO -cuanto del cuadro va por pixel y cuanto por columna-, que es
# el mismo codigo aqui y en la radio. Ver la cabecera del .c.
out/espectrocoste: sim/espectro_coste.c sim/lcd_sim.c vendor/gfx.c $(FIRMWARE)/User/spectrum.c gfx2/gfx2.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -I vendor -o $@ $^ $(LDLIBS)

.PHONY: espectrocoste
espectrocoste: out/espectrocoste ; ./out/espectrocoste

# ---------------------------------------------------------------------
# CUANTO SE QUEDA PARADO EL BUCLE ESPERANDO AL CHIP (20/10/2026).
#
# En la radio salio un pico de 237 ms en el segundo bloque de poll, que es
# donde viven la captura de pantalla y el volcado de ficheros. Esto pone en
# el chip simulado los tiempos del fabricante y mide lo que el firmware se
# queda esperando de verdad en cada paso. Ver la cabecera del .c.
out/flashbloqueo: sim/flash_bloqueo.c sim/w25q_sim.c sim/gd32_pines.c \
                  $(FIRMWARE)/User/spi_flash.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: flashbloqueo
flashbloqueo: out/flashbloqueo ; ./out/flashbloqueo
comprueba: flashbloqueo

# ---------------------------------------------------------------------
# "EL WEFAX SE QUEDA FRITO EN CUANTO PONE GUARDANDO". 20/10/2026.
#
# Mide, contra el chip simulado con los tiempos del fabricante, cuanto se
# queda parado el bucle escribiendo una carta de las dos maneras: bloque a
# bloque sobre flash sin borrar (como estaba) y borrando por delante (como
# esta). Ver la cabecera de sim/fax_bloqueo.c.
out/faxbloqueo: sim/fax_bloqueo.c sim/w25q_sim.c sim/gd32_pines.c \
                $(FIRMWARE)/User/spi_flash.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: faxbloqueo
faxbloqueo: out/faxbloqueo ; ./out/faxbloqueo
comprueba: faxbloqueo

# ---------------------------------------------------------------------
# El camino de guardar los AJUSTES (25/09/2026). Ver la cabecera de
# sim/ajustes_flash.c: de los caminos de escritura a la flash, este -el que
# usa CONFIG.CSV varias veces por sesion- era el unico sin banco, y el usuario
# del proyecto reporto que los ajustes no se le guardaban entre arranques.
out/ajustesflash: sim/ajustes_flash.c sim/w25q_sim.c sim/gd32_pines.c $(FIRMWARE)/User/spi_flash.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $(FUENTES) $(LDLIBS)

.PHONY: ajustesflash
ajustesflash: out/fat12_vacio.img out/ajustesflash ; ./out/ajustesflash

comprueba: ajustesflash

# ---------------------------------------------------------------------
# EL BUFFER QUE LA CASCADA LE PRESTA A FT8 (25/09/2026). Dos mitades:
#
#   cascadaprestada  el cerrojo de waterfall.c, sobre los bytes de verdad
#   prestamochk      que ft8_modo.c lo eche y lo quite, y en ese orden
#
# Ver la cabecera de sim/cascada_prestada.c para por que esto no podia
# quedarse en una regla escrita en un comentario.
out/cascadaprestada: sim/cascada_prestada.c sim/ipa_sim.c sim/lcd_sim.c vendor/gfx.c gfx2/gfx2.c gfx2/fonts_data.c $(FIRMWARE)/User/waterfall.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib -o $@ $(FUENTES) $(LDLIBS)

.PHONY: cascadaprestada
cascadaprestada: out/cascadaprestada ; ./out/cascadaprestada

ifneq ($(wildcard vendor/gfx.c),)
comprueba: cascadaprestada
endif

.PHONY: prestamochk
prestamochk:
	python3 tools/prestamo_check.py $(FIRMWARE)/User/ft8_modo.c

comprueba: prestamochk

# ---------------------------------------------------------------------
# LAS TABLAS DE LA FFT DE FT8 (25/09/2026). Ver la cabecera de
# sim/ft8_tablas.c: la radio se colgaba con un pitido continuo porque
# ft8_fft1024_init() no la llamaba nadie y una tabla de INDICES contenia
# pixeles de cascada.
out/ft8tablas: sim/ft8_tablas.c sim/ipa_sim.c sim/lcd_sim.c vendor/gfx.c gfx2/gfx2.c gfx2/fonts_data.c $(FIRMWARE)/User/ft8_fft1024.c $(FIRMWARE)/User/waterfall.c $(FIRMWARE)/User/ft8_tablas.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib -o $@ $(FUENTES) $(LDLIBS)

.PHONY: ft8tablas
ft8tablas: out/ft8tablas ; ./out/ft8tablas

ifneq ($(wildcard vendor/gfx.c),)
comprueba: ft8tablas
endif

# ---------------------------------------------------------------------
# EL DESVIO DE LA CAPTURA DE FT8 (25/09/2026). Ver la cabecera de
# sim/ft8_desvio.c: sustituye a una copia entera de mag[] y mueve el sitio
# donde escribe la captura segun un estado, que es justo lo que falla en
# silencio si se cuela.
out/ft8desvio: sim/ft8_desvio.c sim/ipa_sim.c sim/lcd_sim.c vendor/gfx.c gfx2/gfx2.c gfx2/fonts_data.c $(FIRMWARE)/User/ft8_waterfall_adapter.c $(FIRMWARE)/User/ft8_fft1024.c $(FIRMWARE)/User/ft8_tablas.c $(FIRMWARE)/User/waterfall.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib -o $@ $(FUENTES) $(LDLIBS)

.PHONY: ft8desvio
ft8desvio: out/ft8desvio ; ./out/ft8desvio

ifneq ($(wildcard vendor/gfx.c),)
comprueba: ft8desvio
endif

# ---------------------------------------------------------------------
# LOS BANCOS TIENEN QUE DEPENDER DE LAS CABECERAS. 25/09/2026.
#
# Cazado en caliente: al devolver GFX2_BAND_H de 8 a 16 filas, "make
# espectrooro" siguio diciendo 52 ventanas -el numero de la banda de 8-
# porque su regla nombra los .c pero ningun .h. El binario viejo seguia
# ahi, make lo dio por bueno, y el banco midio una configuracion que ya no
# existia.
#
# Un banco que mide otra cosa no falla: APRUEBA. Es el mismo fallo que ya
# tuvieron pila.py (miraba donde no era) y modos_check.py (dejo de
# reconocer la tabla y dijo "no hay modos"). Tres veces la misma familia.
#
# La lista de bancos NO se escribe a mano: se saca de los propios
# makefiles, para que anadir uno nuevo no obligue a acordarse de esto.
CABECERAS_TODAS := $(wildcard gfx2/*.h vendor/*.h hostinc/*.h sim/*.h) \
                   $(wildcard $(FIRMWARE)/User/*.h)

# Las cabeceras son prerequisitos DE VERDAD -tienen que disparar la
# recompilacion- pero no son ficheros que compilar. Por eso las recetas usan
# $(FUENTES) y no $^: si una cabecera entra en la linea de gcc, gcc intenta
# precompilarla y deja .gch tirados por el arbol.
FUENTES = $(filter-out %.h,$^)
#
# EL CRITERIO NO ES "ESTA EN ESTE FICHERO" SINO "SU RECETA SABE FILTRAR".
#
# Anadir las cabeceras como prerequisitos las mete en $^, y una receta que
# compile $^ tal cual le pasaria los .h a gcc. Las recetas de aqui usan
# $(FUENTES) precisamente para eso; las del Makefile usan $^ y ese fichero no
# se puede tocar (esta protegido en la maquina del usuario del proyecto).
#
# El primer intento fue "todos los out/ que nombre local.mk", y fallo: aqui se
# nombra out/digi para anadirle un prerequisito, pero su receta vive en el
# Makefile. O sea que la lista incluia targets que no podian con ello.
#
# Asi que se sacan los que TIENEN una receta con $(FUENTES): el que sabe
# filtrar se apunta solo, y el que no, no. La lista sigue sin escribirse a
# mano.
BANCOS_LOCALES := $(sort $(shell awk '\
    /^out\/[A-Za-z0-9_]*:/ { t = substr($$1, 1, length($$1)-1); next } \
    /^\t/ { if (t != "" && index($$0, "FUENTES")) { print t; t = "" } } \
    /^[^\t]/ { if ($$0 !~ /^out\//) t = "" } \
' local.mk))

$(BANCOS_LOCALES): $(CABECERAS_TODAS)

# ---------------------------------------------------------------------
# LA PANTALLA DE FT8 (25/09/2026). Ver la cabecera de sim/ft8_panel.c: lo
# de "bonito" no lo comprueba un banco, pero que las columnas no se pisen
# ni se salgan con los valores mas anchos, si.
out/ft8panel: sim/ft8_panel.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_digi.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -o $@ $(FUENTES) $(LDLIBS)

.PHONY: ft8panel
ft8panel: out/ft8panel ; ./out/ft8panel

comprueba: ft8panel

# ---------------------------------------------------------------------
# LA TABLA DE PAISES POR PREFIJO (25/09/2026). Ver la cabecera de
# sim/dxcc_test.c: lo importante es que gane el prefijo mas largo y que un
# prefijo desconocido diga "no se" en vez de inventarse un pais.
# 30/09/2026: dxcc.c llama a idioma() para elegir el nombre del pais, asi
# que el banco enlaza tambien User/idioma.c del firmware - el de verdad, no
# un sustituto: si un dia tr() cambiara de criterio, el banco lo veria.
out/dxcc: sim/dxcc_test.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_digi.c gfx2/fonts_data.c $(FIRMWARE)/User/dxcc.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -o $@ $(FUENTES) $(LDLIBS)

.PHONY: dxcc
dxcc: out/dxcc ; ./out/dxcc

comprueba: dxcc

# ---------------------------------------------------------------------
# QUE HACE EL NR EN CADA POSICION DEL MANDO (25/09/2026)
#
# Nacio de un susto: el NR parecia haberse vuelto casi inapreciable justo
# despues de cambiar powf por expf en nr_ss_set_strength(). Compila el
# nr_ss.c DEL FIRMWARE y mide, en vez de opinar. Deja dicho ademas donde
# esta el escalon del mando, que es lo que de verdad desconcierta.
out/nrcurva: sim/nr_curva.c $(FIRMWARE)/User/nr_ss.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $(FUENTES) $(LDLIBS)

.PHONY: nrcurva
nrcurva: out/nrcurva ; ./out/nrcurva

comprueba: nrcurva

# ---------------------------------------------------------------------
# EL BRILLO (25/09/2026)
#
# Compila el backlight.c DEL FIRMWARE con sucedaneos de timer y mira el
# CCR que escribe. Vigila las tres cosas que es facil romper a la vez: el
# techo del 100% (que tiene que conmutar), el suelo del 10% (que nunca
# apaga) y que dormir SI apague.
out/brillo: sim/brillo.c $(FIRMWARE)/User/backlight.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $(FUENTES) $(LDLIBS)

.PHONY: brillo
brillo: out/brillo ; ./out/brillo

comprueba: brillo

# ---------------------------------------------------------------------
# LAS AUTOPRUEBAS DE HFDL (25/09/2026)
#
# Ejecuta aqui las 19 autopruebas que traen sus modulos, mas la medida de
# falsos enganches del sincronismo de preambulo (ver la cabecera de
# sim/hfdl_pruebas.c para por que su version de esa prueba no se usa).
#
# Esto va ANTES de que HFDL toque la radio. Con FT8 se hizo al reves.
# icao24_db.c + sim/spi_flash_vacio.c: hfdl_modo.c llama a
# icao24_db_lookup() desde hfdl_modo_poll() para poner el modelo del avion
# en la tabla. Faltaban en los tres objetivos de HFDL y el enlazado fallaba
# con "undefined reference to icao24_db_lookup" -no lo pillo nadie porque
# estos bancos no se volvieron a ejecutar tras anadir la base de aviones-.
# ---------------------------------------------------------------------
# LA LINEA DE CRC DE HFDL, Y SU BUFFER (16/10/2026)
#
# hfdl_modo_linea_crc() arma el renglon de diagnostico a mano sobre 58
# bytes, con numeros que son contadores de tramas. Este banco los pone en
# su peor caso. Es de caja blanca -incluye hfdl_modo.c- porque llegar ahi
# por el aire exigiria inyectar miles de tramas y aun asi no garantizaria
# el extremo.
#
# Y VA CON AddressSanitizer, QUE NO ES ADORNO. Mirar la cadena que sale NO
# BASTA: el cierre recorta `p` antes de poner el '\0', asi que una
# escritura de mas ya ha ocurrido cuando strlen() vuelve a dar un numero
# que cabe. Medido: con pon_u32t() sin tope -como estaba- este banco
# seguia en verde y ASan cantaba global-buffer-overflow sobre `l`. El
# unico que ve el fallo es ASan, asi que el banco se compila con el.
#
# Cuesta poco: es un solo ejecutable y tarda menos de un segundo.
# Y NO DEPENDE DE vendor/ - 16/10/2026. La lista salio de copiar la de
# hfdlpruebas, que si lo usa para comparar dibujados. Este banco no pinta
# nada: mide una cadena. Con vendor/gfx.c dentro, un repositorio clonado
# -que no lo trae, porque no es nuestro- se paraba aqui.
out/hfdlcrclinea: sim/hfdl_crc_linea.c sim/rtc_sim.c sim/ipa_sim.c sim/lcd_sim.c \
                  gfx2/gfx2.c gfx2/fonts_data.c \
                  $(FIRMWARE)/User/waterfall.c \
                  $(filter-out $(FIRMWARE)/User/hfdl_modo.c,$(wildcard $(FIRMWARE)/User/hfdl_*.c)) \
                  $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c \
                  $(FIRMWARE)/User/icao24_db.c $(FIRMWARE)/User/zona_alta.c \
                  sim/spi_flash_vacio.c
	@mkdir -p out
	$(CC) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	      -Wall -I hostinc -I sim -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib \
	      -o $@ $(FUENTES) $(LDLIBS)

.PHONY: hfdlcrclinea
hfdlcrclinea: out/hfdlcrclinea ; ./out/hfdlcrclinea

comprueba: hfdlcrclinea

out/hfdlpruebas: sim/hfdl_pruebas.c sim/rtc_sim.c sim/ipa_sim.c sim/lcd_sim.c vendor/gfx.c \
                 gfx2/gfx2.c gfx2/fonts_data.c $(FIRMWARE)/User/waterfall.c \
                 $(wildcard $(FIRMWARE)/User/hfdl_*.c) \
                 $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c \
                 $(FIRMWARE)/User/icao24_db.c $(FIRMWARE)/User/zona_alta.c sim/spi_flash_vacio.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib -o $@ $(FUENTES) $(LDLIBS)

.PHONY: hfdlpruebas
hfdlpruebas: out/hfdlpruebas ; ./out/hfdlpruebas

ifneq ($(wildcard vendor/gfx.c),)
comprueba: hfdlpruebas
endif

# ---------------------------------------------------------------------
# LA POLARIDAD GLOBAL DE HFDL (25/09/2026)
#
# La unica pieza de HFDL que no viene del proyecto padre, asi que la unica
# que no validan sus autopruebas. Ver la cabecera de sim/hfdl_polaridad.c.
out/hfdlpol: sim/hfdl_polaridad.c sim/rtc_sim.c sim/ipa_sim.c sim/lcd_sim.c vendor/gfx.c \
             gfx2/gfx2.c gfx2/fonts_data.c $(FIRMWARE)/User/waterfall.c \
             $(wildcard $(FIRMWARE)/User/hfdl_*.c) \
                 $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c \
                 $(FIRMWARE)/User/icao24_db.c $(FIRMWARE)/User/zona_alta.c sim/spi_flash_vacio.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib -o $@ $(FUENTES) $(LDLIBS)

.PHONY: hfdlpol
hfdlpol: out/hfdlpol ; ./out/hfdlpol

ifneq ($(wildcard vendor/gfx.c),)
comprueba: hfdlpol
endif

# ---------------------------------------------------------------------
# EL PORTERO DE RAFAGA DE HFDL (25/09/2026)
#
# Codigo nuestro, no del proyecto padre: su detector lleva una FFT que no
# cabe en la RAM que queda. Asi que lo valida esto y nadie mas. Prueba los
# DOS lados - que pare el ruido y que deje pasar la senal - porque un banco
# que solo comprobara lo primero aprobaria un portero tapiado.
out/hfdlport: sim/hfdl_porton.c sim/rtc_sim.c sim/ipa_sim.c sim/lcd_sim.c \
              vendor/gfx.c gfx2/gfx2.c gfx2/fonts_data.c \
              $(FIRMWARE)/User/waterfall.c $(wildcard $(FIRMWARE)/User/hfdl_*.c) \
                 $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c \
                 $(FIRMWARE)/User/icao24_db.c $(FIRMWARE)/User/zona_alta.c sim/spi_flash_vacio.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib -o $@ $(FUENTES) $(LDLIBS)

.PHONY: hfdlport
hfdlport: out/hfdlport ; ./out/hfdlport

ifneq ($(wildcard vendor/gfx.c),)
comprueba: hfdlport
endif

# ---------------------------------------------------------------------
# LA BASE DE MODELOS DE AVION (25/09/2026)
#
# Junta las dos mitades que tienen que encajar: tools/icao24.py escribe el
# fichero y User/icao24_db.c lo lee. Es el unico fallo de la sesion que no
# se nota en pantalla - un desplazamiento mal saca un modelo plausible y
# equivocado- asi que se comprueba aqui.
out/ICAO24.BIN: $(FIRMWARE)/tools/icao24.py $(FIRMWARE)/tools/icao24_prueba.csv
	@mkdir -p out
	python3 $(FIRMWARE)/tools/icao24.py $(FIRMWARE)/tools/icao24_prueba.csv $@

out/icao24: sim/icao24_test.c $(FIRMWARE)/User/icao24_db.c $(FIRMWARE)/User/zona_alta.c
	@mkdir -p out
	$(CC) -O2 -Wall -DHFDL_ICAO24_DB=1 -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $(FUENTES) $(LDLIBS)

# Y el MISMO fichero sacado del otro formato de OpenSky. Cambiaron el CSV a
# comillas simples: leido como el viejo, las columnas se llaman "'icao24'" y
# no sale ni un avion. El banco corre dos veces, una por formato, porque el
# dia que vuelvan a cambiarlo esto tiene que saltar aqui y no en la radio.
out/ICAO24N.BIN: $(FIRMWARE)/tools/icao24.py $(FIRMWARE)/tools/icao24_prueba_nuevo.csv
	@mkdir -p out
	python3 $(FIRMWARE)/tools/icao24.py $(FIRMWARE)/tools/icao24_prueba_nuevo.csv $@

.PHONY: icao24
icao24: out/icao24 out/ICAO24.BIN out/ICAO24N.BIN
	./out/icao24 out/ICAO24.BIN
	@echo '  -- el mismo, desde el CSV de comillas simples --'
	./out/icao24 out/ICAO24N.BIN
	@cmp out/ICAO24.BIN out/ICAO24N.BIN && echo '  los dos formatos dan el MISMO fichero'

comprueba: icao24

# ---------------------------------------------------------------------
# LA TABLA DE AVIONES (25/09/2026)
#
# El quinto modulo del repo padre que existe, esta probado por ellos y no
# lo llamaba nadie. La pieza que se mide es la fusion ID -> OACI: si no
# funde, salen dos filas del mismo avion y parece trafico; si funde de
# mas, dos aviones se comen las posiciones el uno del otro. Ninguna de
# las dos cosas se ve mirando la pantalla.
out/hfdlaviones: sim/hfdl_aviones.c $(FIRMWARE)/User/hfdl_aircraft.c
	@mkdir -p out
	$(CC) -O2 -Wall -Wextra -DHFDL_AIRCRAFT_TABLE=1 -I hostinc -I sim \
	      -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: hfdlaviones
hfdlaviones: out/hfdlaviones ; ./out/hfdlaviones

comprueba: hfdlaviones

# ---------------------------------------------------------------------
# QUE LA VERSION SUBA (25/09/2026)
#
# Pedido por el usuario. Y no lo estaba:
# cuatro binarios del mismo dia diciendo V1.80. La red no es acordarse
# de subirla, es que el numero del firmware y el del diario de cambios
# tengan que ser el mismo; asi tocar uno obliga a tocar el otro.
.PHONY: version
# Desde el 10/10/2026 tambien el MANUAL: iba por V4.30 con el firmware en
# V4.40 y ningun banco lo miraba. Ver mira_el_manual() en version_check.py.
# Y DESDE EL 17/10/2026 TAMBIEN SIN docs/. Antes, cuando faltaban docs/ y
# manual/ -o sea, SIEMPRE en un clon del repositorio publico- este objetivo
# se saltaba el banco ENTERO. Y dentro del banco iba el barrido del README,
# que si esta en el repositorio. Resultado: el README se quedo en V5.03 con
# el firmware en V5.04, con el tamano y el CRC de la version anterior, y
# justo en un clon -lo que ve quien llega- no habia nada que lo mirara.
# Ahora, sin docs/, corre la parte que solo necesita lo que hay dentro.
version:
ifeq ($(HAY_DOCS),)
	@echo "  (docs/ y manual/ no estan: comparo solo lo que vive dentro"
	@echo "   del repositorio -config.h, README.md y los .bin-)"
	python3 tools/version_check.py $(FIRMWARE)/User/config.h
else
	python3 tools/version_check.py $(FIRMWARE)/User/config.h $(FIRMWARE)/../docs/CAMBIOS.md \
	        $(FIRMWARE)/../manual/MANUAL_V2_ES.md
endif

comprueba: version

# ---------------------------------------------------------------------
# LOS ANCHOS DE LA TABLA DE HFDL (25/09/2026)
#
# Se miden con la fuente de verdad. Ver la cabecera de sim/hfdl_cols.c:
# el nombre mas largo solo sale cuando se cruza un 777, asi que una
# columna corta puede pasar semanas sin que se note en pantalla.
out/hfdlcols: sim/hfdl_cols.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c $(FIRMWARE)/User/ui_digi.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: hfdlcols
hfdlcols: out/hfdlcols ; ./out/hfdlcols

comprueba: hfdlcols

# ---------------------------------------------------------------------
# LA PANTALLA DE LA BASE DE AVIONES (25/09/2026)
#
# Si una pantalla queda pobre no se contesta compilando, se contesta
# mirando. Esto
# deja las cuatro fases en out/aviones_*.ppm y mide que los rotulos no
# se salgan. Ver la cabecera de sim/aviones_pant.c.
out/avipant: sim/aviones_pant.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c $(FIRMWARE)/User/aviones_pantalla.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: avipant
avipant: out/avipant ; ./out/avipant

comprueba: avipant

# ---------------------------------------------------------------------
# WSPR (27/09/2026)
#
# El nucleo del modo: mensaje, codigo convolucional, entrelazado y el
# decodificador secuencial. No toca audio ni hardware, asi que se prueba
# entero aqui: se codifica un mensaje, se le echa ruido y tiene que
# volver el mismo. Ver la cabecera de sim/wspr_pruebas.c.
out/wsprtest: sim/wspr_pruebas.c $(FIRMWARE)/User/wspr_msg.c $(FIRMWARE)/User/wspr_fano.c
	@mkdir -p out
	$(CC) -O2 -Wall -Wextra -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: wspr
wspr: out/wsprtest ; ./out/wsprtest

comprueba: wspr

# ---------------------------------------------------------------------
# WSPR, del audio al indicativo (27/09/2026)
#
# Fabrica una emision de WSPR como la que saldria del altavoz y se la da
# al receptor de verdad. Es lo que sustituye a tener una antena: una
# emision tarda dos minutos, no se repite y cuando falla no dice por
# que. Ver la cabecera de sim/wspr_aire.c.
#
# *** Y VA CON LOS DETECTORES PUESTOS DESDE EL 09/10/2026. ***
#
# Este banco tiene un caso -"lo que NO puede pasar: inventarse una baliza"-
# que mete OCHO VENTANAS ENTERAS DE RUIDO PURO por wspr_rx_mete() y llama a
# wspr_rx_decodifica() en cada una. O sea que ya era, sin saberlo, la unica
# prueba de entrada hostil del Fano y de la busqueda sobre 324 pasos... y se
# compilaba sin -fsanitize, asi que una escritura fuera de sitio ahi dentro
# habria pasado sin que nada lo dijera.
#
# Se vio al ampliar sim/aire_basura.c: alli el Fano NO se puede probar
# -hacen falta 110,6 segundos de audio por ventana y a -O1 con detectores
# eso son minutos por semilla-, y la salida era encenderlos AQUI, donde las
# ventanas ya estan hechas. Medido: 17 s antes, 19 s despues.
out/wspraire: sim/wspr_aire.c $(FIRMWARE)/User/wspr_msg.c $(FIRMWARE)/User/wspr_fano.c $(FIRMWARE)/User/wspr_rx.c
	@mkdir -p out
	$(CC) -O2 -g $(BASURA_SAN) -Wall -Wextra -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: wspraire
wspraire: out/wspraire ; ./out/wspraire

comprueba: wspraire

# ---------------------------------------------------------------------
# WSPR: EL CICLO Y LAS COLUMNAS (27/09/2026)
#
# Los dos bancos que hacian falta al cablear WSPR a la radio. Los otros dos
# -wspr y wspraire- miden el DECODIFICADOR; estos dos miden el MODO.
#
# El del ciclo es el que importa: una emision de WSPR dura 110,6 segundos y
# solo empieza en los minutos pares, asi que ver dos ciclos en la radio son
# cinco minutos mirando una barra. Aqui el reloj lo pone el banco. A la
# primera cazo que la captura abria en el minuto PAR en vez del impar, o sea
# un minuto y un segundo antes de tiempo: WSPR no habria decodificado nunca
# y desde fuera se habria visto como falta de propagacion.
out/wsprciclo: sim/wspr_ciclo.c sim/waterfall_prestamo.c \
               $(FIRMWARE)/User/wspr_modo.c $(FIRMWARE)/User/wspr_rx.c \
               $(FIRMWARE)/User/wspr_msg.c $(FIRMWARE)/User/wspr_fano.c \
               $(FIRMWARE)/User/hfdl_ram.c
	@mkdir -p out
	$(CC) -O2 -Wall -Wextra -I hostinc -I sim -I $(FIRMWARE)/User \
	      -I $(FIRMWARE)/Ft8Lib -o $@ $^ $(LDLIBS)

.PHONY: wsprciclo
wsprciclo: out/wsprciclo ; ./out/wsprciclo

comprueba: wsprciclo

out/wsprcols: sim/wspr_cols.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
              $(FIRMWARE)/User/ui_digi.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: wsprcols
wsprcols: out/wsprcols ; ./out/wsprcols

comprueba: wsprcols

# ---------------------------------------------------------------------
# AIS: LA SENAL SINTETICA Y LAS COLUMNAS (27/09/2026)
#
# El de la senal es el que importa. Entre el barco y la tabla hay SEIS capas
# -GMSK, reloj de bit, NRZI, relleno de HDLC, CRC y campos de seis bits- y
# cada una tiene una convencion de orden de bits que se puede poner del
# reves sin que nada se queje: lo que sale no es un error, es un MMSI
# plausible en mitad del Atlantico. Aqui se construye la senal desde el otro
# extremo y se comprueba que vuelve el mismo barco.
#
# Cazo a la primera los SIETE BITS DE MAS del HDLC (ver trama_fin() en
# ais_rx.c): la trama salia de 191 bits en vez de 184 y el CRC no cuadraba
# nunca. Con un CRC de 16 bits, "no cuadra nunca" y "no hay senal" se ven
# igual en la radio.
out/aisaire: sim/ais_aire.c $(FIRMWARE)/User/ais_rx.c $(FIRMWARE)/User/ais_msg.c
	@mkdir -p out
	$(CC) -O2 -Wall -Wextra -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: aisaire
aisaire: out/aisaire ; ./out/aisaire

comprueba: aisaire

out/aiscols: sim/ais_cols.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
             $(FIRMWARE)/User/ui_digi.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: aiscols
aiscols: out/aiscols ; ./out/aiscols

comprueba: aiscols

# ---------------------------------------------------------------------
# ALE: EL GOLAY, LA SENAL SINTETICA Y LAS COLUMNAS (27/09/2026)
#
# El del Golay DEMUESTRA, no muestrea: el codigo es lineal, asi que probar
# los 2.925 patrones de hasta tres errores sobre la palabra cero cubre los
# 4.096 x 2.925 casos posibles. Y de paso mide que con cuatro errores -que
# ya no son corregibles- los detecta el 100% de las veces.
#
# El de la senal sintetica cazo el fallo de fondo de todo el modo: el Golay
# de ALE es CICLICO, asi que una rotacion de dos bits del flujo da otras dos
# palabras Golay perfectamente validas. Con la palabra descolocada 24
# muestras salian cuatro palabras impecables con 48 votos unanimes... y
# valian lo esperado desplazado un bit. El FEC solo NO puede encontrar el
# principio de palabra: hacen falta ademas el preambulo y el alfabeto, que
# es justo lo que dice el estandar y lo que parece un detalle.
out/alegolay: sim/ale_golay_test.c $(FIRMWARE)/User/ale_golay.c
	@mkdir -p out
	$(CC) -O2 -Wall -Wextra -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: alegolay
alegolay: out/alegolay ; ./out/alegolay

comprueba: alegolay

out/alesaire: sim/ale_aire.c $(FIRMWARE)/User/ale_rx.c $(FIRMWARE)/User/ale_golay.c
	@mkdir -p out
	$(CC) -O2 -Wall -Wextra -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: alesaire
alesaire: out/alesaire ; ./out/alesaire

comprueba: alesaire

out/alecols: sim/ale_cols.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
             $(FIRMWARE)/User/ui_digi.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: alecols
alecols: out/alecols ; ./out/alecols

comprueba: alecols

# ---------------------------------------------------------------------
# EL PRESTAMO DE LA MEMORIA DE LA CASCADA (28/09/2026)
#
# Este fallo se ha cometido DOS VECES en el mismo sitio con un dia de
# diferencia -FT8/HFDL primero, AIS despues- y las dos veces el sintoma fue
# el mismo: el modo se pone, la pantalla sale, y no decodifica nada. Desde
# fuera se lee como "esta frecuencia no entra".
#
# Prueba que cada uno de los tres que se reparten esos 54 kB arranca cuando
# le toca, suelta al salir y se niega si otro la tiene. Todas las parejas,
# en los dos sentidos. FT8 y HFDL se fingen con waterfall_presta(), que es
# exactamente lo que hacen ellos, para no arrastrar medio firmware.
out/modosram: sim/modos_ram.c sim/waterfall_prestamo.c \
              $(FIRMWARE)/User/wspr_modo.c $(FIRMWARE)/User/wspr_rx.c \
              $(FIRMWARE)/User/wspr_msg.c $(FIRMWARE)/User/wspr_fano.c \
              $(FIRMWARE)/User/ais_modo.c $(FIRMWARE)/User/ais_rx.c \
              $(FIRMWARE)/User/ais_msg.c \
              $(FIRMWARE)/User/ale_modo.c $(FIRMWARE)/User/ale_rx.c \
              $(FIRMWARE)/User/ale_golay.c $(FIRMWARE)/User/hfdl_ram.c
	@mkdir -p out
	$(CC) -O2 -Wall -Wextra -I hostinc -I sim -I $(FIRMWARE)/User \
	      -I $(FIRMWARE)/Ft8Lib -o $@ $^ $(LDLIBS)

.PHONY: modosram
modosram: out/modosram ; ./out/modosram

comprueba: modosram

# ---------------------------------------------------------------------
# PSK31: DE LA PORTADORA AL TEXTO (28/09/2026)
#
# Pedido por el usuario.
#
# Fabrica una emision de PSK31 -con su envolvente de coseno, su desvio de
# sintonia y su ruido- y se la da al receptor de verdad. El modulador esta
# escrito a partir de la descripcion del modo, no llamando a psk31.c, asi
# que un convenio del reves NO se cancela. Ver la cabecera del fichero.
out/pskaire: sim/psk31_aire.c $(FIRMWARE)/User/psk31.c
	@mkdir -p out
	$(CC) -O2 -Wall -Wextra -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: pskaire
pskaire: out/pskaire ; ./out/pskaire

comprueba: pskaire

# La tabla del varicode, mirada desde fuera. Lo que el banco de aire NO
# puede comprobar, porque el transmisor y el receptor la sacan del mismo
# sitio y una errata se cancelaria - el fallo del localizador de WSPR,
# otra vez. Ver la cabecera de tools/varicode_check.py.
.PHONY: varicode
varicode:
	@echo "Varicode de PSK31: la tabla contra su fuente."
	python3 tools/varicode_check.py $(FIRMWARE)/psk31_varicode.txt $(FIRMWARE)/User/psk31.c

comprueba: varicode

# ---------------------------------------------------------------------
# QUIEN EMITE EN ESTA FRECUENCIA (28/09/2026)
#
# Pedido por el usuario.
#
# El banco abre el EMISORAS.BIN DE VERDAD -los 206 kB que salen de las dos
# listas publicas, que estan en datos/- y le pregunta a User/emisoras.c,
# el mismo que corre en la radio. Comprueba la busqueda binaria contra un
# recorrido completo de las 1.898 frecuencias, que la suma pilla un solo
# bit cambiado en 206 kB, y que la conversion de hora local a UTC cambia
# el DIA cuando toca.
#
# Si las listas no estan, el banco lo dice y se salta: no se para la
# compilacion por no tener 1 MB de texto a mano.
# LAS LISTAS DE EMISORAS NO SE GUARDAN DOS VECES - 13/10/2026.
#
# datos/eibi.txt y datos/aoki.txt eran byte a byte los mismos ficheros que
# $(FIRMWARE)/tools/datos/, un megabyte largo repetido. Mientras el
# simulador vivia en otra carpeta no se veia; dentro del mismo
# repositorio son dos copias de lo mismo, y este proyecto tiene escrito
# en la pared lo que pasa con dos copias de lo mismo.
#
# Asi que la copia buena es la del firmware -la que documenta
# tools/datos/LEEME.md, con la procedencia de cada lista- y aqui se
# mira primero ahi. Si alguien deja una copia local, gana la local: el
# que la pone a mano sabe por que.
LISTAS_DIR := $(if $(wildcard datos/eibi.txt),datos,$(FIRMWARE)/tools/datos)

out/EMISORAS.BIN: tools/emisoras_pack.py $(LISTAS_DIR)/eibi.txt $(LISTAS_DIR)/aoki.txt
	@mkdir -p out
	python3 tools/emisoras_pack.py $(LISTAS_DIR)/eibi.txt $(LISTAS_DIR)/aoki.txt $@ 2

# hora_comun.c entra desde el 10/10/2026: emisoras_utc() ya no resta "las
# horas de España" del byte 40, resta hora_huso(). Ver hora_comun.h.
out/emis: sim/emisoras_test.c $(FIRMWARE)/User/emisoras.c $(FIRMWARE)/User/hora_comun.c
	@mkdir -p out
	$(CC) -O2 -Wall -Wextra -Wno-unused-result -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

#
# Y EL "SE SALTA" TENIA QUE SER DE VERDAD - 28/09/2026.
#
# Esto ponia `emisoras: out/emis out/EMISORAS.BIN`, o sea que exigia el
# fichero... que solo se puede construir con eibi.txt y aoki.txt delante.
# Sin las listas, make no se saltaba nada: paraba con "No rule to make
# target 'datos/eibi.txt'" y se llevaba por delante el resto de la suite.
#
# El comentario de arriba decia lo contrario desde el dia que se escribio,
# y no se noto porque en esta maquina las listas estaban. Lo destapo
# montar el zip para otra persona y correr los bancos desde la copia.
#
# Ahora se hace como en `zonaalta`: si las listas estan, se empaqueta y se
# prueba; si no, se llama al banco con un fichero que no existe y el banco
# dice que se salta - que es lo que ya sabia hacer-.
# SIN "-f Makefile" - 13/10/2026.
#
# Estas dos sub-llamadas nombraban el fichero a mano. Al pasar el
# simulador a GNUmakefile -porque el puente no puede escribir un fichero
# llamado "Makefile"- se quedaron pidiendo uno que ya no esta, y la tanda
# murio con "No rule to make target 'Makefile'" DESPUES de 77 bancos en
# verde: media tanda perdida por una palabra.
#
# Un $(MAKE) a secas hereda el fichero que se esta leyendo, sea cual sea
# su nombre. Nombrarlo era escribir dos veces lo mismo, que es de donde
# salen estas cosas.
.PHONY: emisoras
emisoras: out/emis
	@if [ -f $(LISTAS_DIR)/eibi.txt ] && [ -f $(LISTAS_DIR)/aoki.txt ]; then \
	   $(MAKE) --no-print-directory FIRMWARE=$(FIRMWARE) out/EMISORAS.BIN >/dev/null && \
	   ./out/emis out/EMISORAS.BIN; \
	 else \
	   ./out/emis out/no-estan-las-listas.bin; \
	 fi

comprueba: emisoras

# ---------------------------------------------------------------------
# EL MAPA DEL MUNDO (28/09/2026)
#
# Pedido por el usuario.
#
# Un mapa no se comprueba compilando: sale en out/mapa_mundo.ppm y se
# mira. Pero ademas se mide lo que mirando NO se ve - que un localizador
# conocido cae en el pixel que le toca, y que la costa lleva el corte del
# meridiano 180 sin el cual salen rayas de lado a lado.
out/mapapant: sim/mapa_pant.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c $(FIRMWARE)/User/mapa.c $(FIRMWARE)/User/mapa_datos.c $(FIRMWARE)/User/mapa_bin.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: mapa
mapa: out/mapapant ; ./out/mapapant

comprueba: mapa

# ---------------------------------------------------------------------
# LOS INDICATIVOS DEL MAPA, EL RAYO Y LA FICHA (12/10/2026)
#
# Pedido por el usuario.
#
# Dos de las nueve pruebas MIRAN PIXELES PINTADOS y no numeros, y es a
# proposito: que el rayo sea un arco y no una raya, y que no cruce la
# pantalla por el meridiano 180, son cosas del dibujo. Las otras siete
# miran cuentas, que es lo que no se puede mirar en una pantalla.
# dxcc.c entra desde el 13/10/2026: la ficha pone el pais, y el banco
# recorre la tabla ENTERA para medir que el nombre mas largo cabe.
out/mapaeti: sim/mapa_eti.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
             $(FIRMWARE)/User/mapa.c $(FIRMWARE)/User/mapa_datos.c \
             $(FIRMWARE)/User/mapa_bin.c $(FIRMWARE)/User/mapa_eti.c \
             $(FIRMWARE)/User/dxcc.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: mapaeti
mapaeti: out/mapaeti ; ./out/mapaeti

comprueba: mapaeti

# ---------------------------------------------------------------------
# EL FILTRO DE CQ DE LA LISTA DE FT8 (13/10/2026)
#
# Pedido por el usuario.
#
# La prueba que de verdad importa es la 3: dieciseis mensajes con las tres
# unicas CQ entre los mas VIEJOS. Quien recorte a quince renglones y filtre
# despues las pierde, y el sintoma es "el filtro a veces no encuentra
# nada", que no se parece a un fallo de programa.
out/ft8lista: sim/ft8_lista.c $(FIRMWARE)/User/ft8_lista.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: ft8lista
ft8lista: out/ft8lista ; ./out/ft8lista

comprueba: ft8lista

# ---------------------------------------------------------------------
# JTTY (28/09/2026)
#
# Pedido por el usuario.
#
# Dos bancos, y los dos comprueban contra ALGO DE FUERA, que es la unica
# forma que vale para un formato ajeno:
#
#   jttyfec   los tonos que salen de la correccion de errores, contra los
#             que saca tbcc.f90 de WSJT-X v3.2.0-rc1 compilado con
#             gfortran. Los vectores estan en datos/, y sim/jtty_oro.h se
#             genera de ahi.
#   jttymsg   el texto, contra los diez vectores de 34 bits de la tabla
#             "Representative 34-bit vectors" del documento normativo
#             lib/jtty/jtty_source_encoding.txt.
#
# Despues de lo del localizador de WSPR -donde codificador y
# decodificador compartian el mismo error y la ida y vuelta salia limpia-
# comprobar contra uno mismo no cuenta.
out/jttyfec: sim/jtty_fec_test.c sim/jtty_oro.h $(FIRMWARE)/User/jtty_fec.c
	@mkdir -p out
	$(CC) -O2 -Wall -Wextra -I hostinc -I sim -I $(FIRMWARE)/User -o $@ \
	  sim/jtty_fec_test.c $(FIRMWARE)/User/jtty_fec.c $(LDLIBS)

.PHONY: jttyfec
jttyfec: out/jttyfec ; ./out/jttyfec

comprueba: jttyfec

out/jttymsg: sim/jtty_msg_test.c $(FIRMWARE)/User/jtty_msg.c
	@mkdir -p out
	$(CC) -O2 -Wall -Wextra -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: jttymsg
jttymsg: out/jttymsg ; ./out/jttymsg

comprueba: jttymsg

# ---------------------------------------------------------------------
# JTTY DEL AIRE AL TEXTO (28/09/2026)
#
# Pedido por el usuario.
#
# Los dos bancos de arriba comprueban la correccion de errores y el texto
# contra fuentes de fuera, y los dos estaban en verde desde por la mañana
# mientras el modo NO EXISTIA: no habia nada que les diera tonos.
#
# Esto es lo que faltaba. Genera la señal -modulador 4-GFSK completo, con
# su filtro gaussiano-, le suma ruido medido, y se la da al receptor de
# verdad. Y mete un retardo distinto y no multiplo del simbolo en cada
# prueba, porque lo que hace a este modo distinto de todos los demas es que
# la emision empieza cuando le da la gana. Ver la cabecera de
# sim/jtty_aire.c.
out/jttyaire: sim/jtty_aire.c $(FIRMWARE)/User/jtty_rx.c \
              $(FIRMWARE)/User/jtty_fec.c $(FIRMWARE)/User/jtty_msg.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $^ $(LDLIBS)

.PHONY: jttyaire
jttyaire: out/jttyaire ; ./out/jttyaire

comprueba: jttyaire

# ---------------------------------------------------------------------
# EL DIRECTORIO DE LA ZONA ALTA (28/09/2026)
#
# Pedido por el usuario.
#
# Las dos bases de datos van ahora en un solo BD.BIN con un directorio
# delante. El banco abre el BD.BIN de verdad -con las dos dentro- y le
# pregunta a User/zona_alta.c, el mismo que corre en la radio: que
# encuentra las secciones, que en la direccion que dice cada una empiezan
# sus cuatro letras, que un byte cambiado en el directorio no se cuela, y
# que sin directorio se cae a la distribucion vieja.
out/BD.BIN: tools/datos_pack.py out/EMISORAS.BIN $(ICAO24)
	@mkdir -p out
	python3 tools/datos_pack.py $@ $(ICAO24) out/EMISORAS.BIN

out/zonaalta: sim/zona_alta_test.c $(FIRMWARE)/User/zona_alta.c sim/spi_flash_vacio.c
	@mkdir -p out
	$(CC) -O2 -Wall -Wextra -Wno-unused-result -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

# ICAO24 no esta en el arbol -son 300 kB de datos que salen de un CSV de
# 108 MB-, asi que si no se le pasa la ruta el banco se salta solo.
ICAO24 ?= datos/ICAO24.BIN

.PHONY: zonaalta
zonaalta: out/zonaalta
	@if [ -f "$(ICAO24)" ]; then \
	   $(MAKE) --no-print-directory FIRMWARE=$(FIRMWARE) out/BD.BIN >/dev/null && \
	   ./out/zonaalta out/BD.BIN; \
	 else \
	   ./out/zonaalta out/no-esta.bin; \
	 fi

comprueba: zonaalta

# ---------------------------------------------------------------------
# JTTY: DE LOS ATOMOS A LOS MENSAJES (28/09/2026)
#
# Pedido por el usuario.
#
# jttyaire comprueba que UNA trama sale del aire. Lo que nadie comprobaba
# es quien decide si dos atomos son el MISMO mensaje, que es jtty_modo.c, y
# de ahi salieron las tres maneras distintas de juntar mensajes que no van
# juntos - las tres descubiertas con la antena, una por foto-.
#
# Esto le da al modo de verdad una banda con varias estaciones a la vez,
# con sus fases y sus tramas perdidas, y lee los renglones que saca. Ver la
# cabecera de sim/jtty_cadena.c.
# dxcc.c entra el 28/09 por la tarde: la columna del pais se monta dentro
# de jtty_modo.c, asi que el banco que prueba el encadenado tambien la
# monta - y eso esta bien, porque asi los renglones que imprime son los
# mismos que va a enseñar la radio-.
out/jttycadena: sim/jtty_cadena.c $(FIRMWARE)/User/jtty_modo.c \
                $(FIRMWARE)/User/jtty_rx.c $(FIRMWARE)/User/jtty_fec.c \
                $(FIRMWARE)/User/jtty_msg.c $(FIRMWARE)/User/dxcc.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $^ $(LDLIBS)

.PHONY: jttycadena
jttycadena: out/jttycadena ; ./out/jttycadena

comprueba: jttycadena

# ---------------------------------------------------------------------
# LAS TRES MEDIDAS DE JTTY QUE NO CABEN EN LA SUITE (28/09/2026)
#
# Estas tres tardan de diez segundos a varios minutos por punto, asi que no
# van en `make comprueba` -que tarda eso en total-. Pero los numeros que
# hay escritos en CAMBIOS.md y en los comentarios salieron de ellas, y un
# numero que nadie puede volver a sacar es un numero en el que hay que
# creer. Se corren a mano cuando se toca el receptor o el encadenado:
#
#   make jttysens    sensibilidad: tramas buenas por SNR, con y sin
#                    desvanecimiento Rayleigh. De aqui salio el umbral de
#                    sincronismo 0,45 -antes 0,55, puesto a ojo-.
#   make jttyfalsos  la otra mitad de eso: atomos falsos con ruido puro.
#   make jttytrozos  en cuantos renglones sale un mensaje de diez tramas.
#                    De aqui salio el alcance de cuatro saltos.
out/jttysens: sim/jtty_sens.c $(FIRMWARE)/User/jtty_rx.c \
              $(FIRMWARE)/User/jtty_fec.c $(FIRMWARE)/User/jtty_msg.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $^ $(LDLIBS)

out/jttyfalsos: sim/jtty_falsos.c $(FIRMWARE)/User/jtty_rx.c \
                $(FIRMWARE)/User/jtty_fec.c $(FIRMWARE)/User/jtty_msg.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $^ $(LDLIBS)

# dxcc.c entro en la receta el 07/10/2026: jtty_modo.c llama a dxcc_pais()
# para poner el pais al lado del indicativo, y sin el fichero esto no
# enlazaba. Como jttytrozos tampoco colgaba de "comprueba", llevaba roto sin
# que nadie se enterase - el mismo caso que helltest.
out/jttytrozos: sim/jtty_trozos.c $(FIRMWARE)/User/jtty_modo.c \
                $(FIRMWARE)/User/jtty_rx.c $(FIRMWARE)/User/jtty_fec.c \
                $(FIRMWARE)/User/jtty_msg.c $(FIRMWARE)/User/dxcc.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $^ $(LDLIBS)

.PHONY: jttysens jttyfalsos jttytrozos
jttysens:   out/jttysens   ; ./out/jttysens
jttyfalsos: out/jttyfalsos ; ./out/jttyfalsos
jttytrozos: out/jttytrozos ; ./out/jttytrozos -9 && ./out/jttytrozos -11

# ---------------------------------------------------------------------
# LOS ANCHOS DE LA TABLA DE JTTY (28/09/2026)
#
# Pedido por el usuario.
#
# Las cinco columnas se movieron todas a la vez, asi que se miden todas.
# Y la del pais se mide contra los 153 nombres de dxcc.c recorriendo la
# tabla, no contra una muestra: ver la cabecera de sim/jtty_cols.c.
#
# UN SUSTO Y UN ARREGLO MAL HECHO, los dos por el orden de los -I.
#
# La primera version dijo que al mensaje le quedaban 624 px cuando le
# quedan 496: leia `gfx2/ui_digi.h`, que es la COPIA del simulador y
# todavia tenia cuatro columnas. Puse `-I$(FIRMWARE)/User` DELANTE para que
# ganara el del firmware... y eso es justo lo que NO se puede hacer aqui:
# en esa carpeta viven tambien los headers del HARDWARE -rm68120_exmc.h y
# compañia- que el simulador sustituye a proposito por los suyos de
# hostinc/ y sim/. El siguiente banco que escribi con ese orden se fue de
# cabeza a escribir en la direccion del panel y murio con SIGSEGV.
#
# Asi que el orden se queda como en todos los demas -hostinc y sim mandan-
# y lo que se arregla es lo otro: los HEADERS VAN EN LOS PREREQUISITOS. Sin
# eso, al cambiar el .h make no reconstruia y el banco seguia contestando
# lo de antes, que es como el numero equivocado sobrevivio a dos ejecuciones.
# Y que la copia no se quede atras ya lo vigila `copiaschk`, que es de lo
# primero que hace `make comprueba`.
out/jttycols: sim/jtty_cols.c gfx2/gfx2.c gfx2/fonts_data.c sim/lcd_sim.c \
              $(FIRMWARE)/User/ui_digi.c $(FIRMWARE)/User/dxcc.c \
              gfx2/ui_digi.h $(FIRMWARE)/User/dxcc.h
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ \
	  $(filter %.c,$^) $(LDLIBS)

.PHONY: jttycols
jttycols: out/jttycols ; ./out/jttycols

comprueba: jttycols

# Un render del panel de JTTY para mirarlo: los anchos los comprueba
# jttycols, pero "se lee bien" no lo contesta un numero.
out/jttyvista: sim/jtty_vista.c gfx2/gfx2.c gfx2/fonts_data.c sim/lcd_sim.c \
               $(FIRMWARE)/User/ui_digi.c gfx2/ui_digi.h
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: jttyvista
jttyvista: out/jttyvista ; ./out/jttyvista

# ---------------------------------------------------------------------
# LA CABECERA: QUE UN REPINTADO PARCIAL NO BORRE A OTRO (28/09/2026)
#
# La quinta vez de la misma familia de fallo, y la primera fuera del panel
# digital: en SAM el error de PLL no se veia nunca porque lo borraba la
# franja de la marquesina. sim/digi.c vigila el panel desde la cuarta; esto
# vigila la cabecera. Ver la cabecera de sim/top_franjas.c.
out/topfranjas: sim/top_franjas.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
                gfx2/ui_top.c gfx2/ui_top.h $(FIRMWARE)/User/rds_pi.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: topfranjas
topfranjas: out/topfranjas ; ./out/topfranjas

comprueba: topfranjas

# ---------------------------------------------------------------------
# EL LOCALIZADOR A COORDENADAS (30/09/2026). Ver la cabecera de
# sim/qthcoord.c: habia dos conversiones y solo una estaba bien, y la mala
# era la de la pantalla que existe para darte cuenta de que te equivocas.
# Enlaza User/ft8_decoder.c DEL FIRMWARE: es lo unico que distingue este
# banco de tools/qth_check.py, que hace la conversion en Python.
out/qthcoord: sim/qthcoord.c $(FIRMWARE)/User/ft8_decoder.c $(FIRMWARE)/User/ft8_snr.c sim/waterfall_prestamo.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib -o $@ $^ $(LDLIBS)

.PHONY: qthcoord
qthcoord: out/qthcoord ; ./out/qthcoord

comprueba: qthcoord

# ---------------------------------------------------------------------
# LA PANTALLA DEL QTH (29/09/2026), enchufada al fin el 30/09.
#
# El banco estaba escrito -y bueno: compila el User/ui_qth.c DEL FIRMWARE,
# mide los anchos en los dos idiomas, los solapes dentro de la banda
# 104..421 y el enrutado de ui_qth_hit()- pero NINGUNA regla lo nombraba,
# ni aqui ni en el Makefile. O sea que nunca ha corrido.
#
# Un banco escrito y no enchufado es peor que no tenerlo: cuenta en la lista
# de lo que esta cubierto y no cubre nada. Y la geometria de ui_qth.h no la
# mide ningun otro (tools/qth_check.py mira de donde sale el localizador, no
# como se pinta).
out/qthreal: sim/qthreal.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
             $(FIRMWARE)/User/ui_qth.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: qthreal
qthreal: out/qthreal ; ./out/qthreal

comprueba: qthreal

# ---------------------------------------------------------------------
# EL CARGADOR DE ARRANQUE (01/10/2026). Ver la cabecera de sim/cargador.c.
#
# Pedido por el usuario. Pues esto: el
# User/cargador.c del firmware corriendo contra el spi_flash.c de verdad,
# el W25Q16 simulado, un volumen FAT12 de 1 MB y el update4.bin que acaba
# de salir del make.
#
# El comportamiento que copia NO sale de suponer: sale de desensamblar el
# boot.bin del proyecto. El f_unlink de 0x800a7a8 -que borra el fichero
# despues de grabarlo- no estaba documentado en ningun sitio.
out/cargador: sim/cargador.c sim/fmc_sim.c sim/w25q_sim.c sim/gd32_pines.c \
              $(FIRMWARE)/User/cargador.c $(FIRMWARE)/User/spi_flash.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -Igfx2 -o $@ \
	      sim/cargador.c sim/fmc_sim.c sim/w25q_sim.c sim/gd32_pines.c \
	      $(FIRMWARE)/User/cargador.c $(FIRMWARE)/User/spi_flash.c $(LDLIBS)

.PHONY: cargador
# Y EL MISMO BANCO CONTRA DOS VOLUMENES. 02/10/2026.
#
# El de fabrica tiene UN sector por cluster; Windows, al formatear, pone
# clusters de 4 kB. El driver tiene que tragar los dos, y el de fabrica es
# el que no se puede romper.
#
# Las imagenes las hace mkfs.fat y las valida fsck.fat, que no saben nada de
# este proyecto y por eso valen como tribunal.
#
# -R 6 Y NO -R 4, Y NO ES UN CAPRICHO: con 4 sectores reservados el area de
# datos cae en el sector 38, y 38 no es multiplo de 8 (los sectores que
# tiene un cluster de 4 kB). O sea que NINGUN cluster de ese volumen
# empieza en frontera de bloque de borrado, y el driver tiene que
# rechazarlo -no es que no sepa, es que ahi no se puede escribir sin
# borrarle el bloque al vecino-. Con 6 reservados los datos caen en el
# sector 40, que si lo es. Windows alinea solo; mkfs.fat no tiene por que.
out/fat12_4k.img:
	@mkdir -p out
	dd if=/dev/zero of=$@ bs=512 count=2048 status=none
	mkfs.fat -F 12 -R 6 -f 2 -r 512 -s 8 -S 512 -n DEEPSDR4K $@ >/dev/null

# ---------------------------------------------------------------------
# Y EL MISMO CARGADOR CONTRA UN VOLUMEN QUE HA LLENADO OTRO (02/10/2026)
#
# Pedido por el usuario.
#
# El banco de arriba coloca el fichero EL MISMO, asi que no puede cazar una
# diferencia entre como lo coloco yo y como lo coloca un sistema operativo.
# Aqui la imagen la hace mkfs.fat y el fichero lo mete mcopy: ni uno ni otro
# saben nada de este proyecto. Ver la cabecera de sim/cargafuera.c.
out/fuera_2k.img: out/fat12_2k.img $(UPD)
	cp out/fat12_2k.img $@
	mcopy -i $@ $(UPD) ::UPDATE.BIN

out/fuera_16m.img: out/fat12_16m.img $(UPD)
	cp out/fat12_16m.img $@
	mcopy -i $@ $(UPD) ::UPDATE.BIN

out/fuera_fab.img: out/fat12_vacio.img $(UPD)
	cp out/fat12_vacio.img $@
	mcopy -i $@ $(UPD) ::UPDATE.BIN

out/cargafuera: sim/cargafuera.c sim/w25q_sim.c sim/gd32_pines.c \
                $(FIRMWARE)/User/spi_flash.c $(FIRMWARE)/User/cargador.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/cargafuera.c sim/w25q_sim.c sim/gd32_pines.c \
	      $(FIRMWARE)/User/spi_flash.c $(FIRMWARE)/User/cargador.c $(LDLIBS)

# ---------------------------------------------------------------------
# EL DETECTOR DE TRAMAS (02/10/2026). Ver sim/stanag_det_test.c y, sobre
# todo, la cabecera de User/stanag_det.h: por que se hace sobre la señal
# compleja y no sobre la envolvente, y de donde sale el numero del 4285.
#
# Este banco NO prueba que sepamos demodular STANAG. Prueba una cosa mas
# pequeña y comprobable hoy: que si le pones delante una señal que repite
# su estructura cada X milisegundos, dice X. Y el banco calcula X por su
# cuenta, en simbolos, en vez de preguntarselo al firmware.
out/stanagdet: sim/stanag_det_test.c $(FIRMWARE)/User/stanag_det.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/stanag_det_test.c $(FIRMWARE)/User/stanag_det.c -lm

.PHONY: stanagdet
stanagdet: out/stanagdet ; ./out/stanagdet

comprueba: stanagdet

# Y el modo: del periodo medido al NOMBRE. Lo que de verdad se comprueba
# aqui es que cuando NO es ninguna de las conocidas lo dice, en vez de
# forzar la mas cercana - que es como un identificador de señales pasa de
# util a peligroso.
out/stanagmodo: sim/stanag_modo_test.c $(FIRMWARE)/User/stanag_modo.c \
                $(FIRMWARE)/User/stanag_det.c $(FIRMWARE)/User/stanag_rx.c \
                $(FIRMWARE)/User/s4415.c $(FIRMWARE)/User/hfdl_viterbi.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/stanag_modo_test.c $(FIRMWARE)/User/stanag_modo.c \
	      $(FIRMWARE)/User/stanag_det.c $(FIRMWARE)/User/stanag_rx.c \
	      $(FIRMWARE)/User/s4415.c $(FIRMWARE)/User/hfdl_viterbi.c \
	      gfx2/idioma.c -lm

.PHONY: stanagmodo
stanagmodo: out/stanagmodo ; ./out/stanagmodo

comprueba: stanagmodo

# ---------------------------------------------------------------------
# Y CONTRA UNA GRABACION DE VERDAD (02/10/2026)
#
# sim/muestras/stanag4285_tfc.mp3 es una muestra publica de un STANAG 4285
# real, de sigidwiki.com (el Signal Identification Wiki). La trajo el usuario
# despues de cuatro entregas peleandose con la radio, y es lo que faltaba
# desde el principio: todo lo demas prueba el detector contra señales que
# me he fabricado yo, y eso solo demuestra que se leer lo que yo mismo
# escribo. Si mi generador y mi detector comparten una idea equivocada, los
# dos se ponen de acuerdo y el banco pasa.
#
# Esta no. La grabo otra gente de una estacion de verdad, y de ella tiene
# que salir 106,67 ms. Ver la cabecera de sim/stanag_aire.c.
#
# SIETE GRABACIONES, NO UNA (02/10/2026). El usuario fue trayendo las demas:
# 4529, 4415, 4539, 4538 y las DOS caras del 4481 -hay dos ejemplos de ese,
# uno PSK y otro FSK-. Se pasan todas en cada comprobacion, y lo
# que cada una tiene que dar esta en k_espera[] de sim/stanag_aire.c con
# el numero de ventanas que engancho el dia que se midio.
#
# Van en mp3 (1,9 MB las siete) y se convierten al vuelo: en WAV ocuparian
# diez veces mas dentro del repositorio.
MUESTRAS := $(patsubst sim/muestras/%.mp3,out/muestras/%.wav,$(wildcard sim/muestras/stanag*.mp3))

out/muestras/%.wav: sim/muestras/%.mp3
	@mkdir -p out/muestras
	@command -v ffmpeg >/dev/null 2>&1 || { \
	   echo "FALTA ffmpeg: hace falta para pasar sim/muestras/*.mp3 a WAV de 12 kHz"; \
	   exit 1; }
	@ffmpeg -loglevel error -y -i $< -ac 1 -ar 12000 -acodec pcm_s16le $@

out/stanagaire: sim/stanag_aire.c $(FIRMWARE)/User/stanag_det.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/stanag_aire.c $(FIRMWARE)/User/stanag_det.c -lm

.PHONY: stanagaire
stanagaire: out/stanagaire $(MUESTRAS)
	@for w in $(MUESTRAS); do ./out/stanagaire $$w || exit 1; done

comprueba: stanagaire

# Y lo que ata las dos listas que tienen que decir lo mismo y viven en
# ficheros distintos: la tabla de nombres de la radio (k_conocidas[] de
# User/stanag_modo.c) y lo que mide el banco de aqui arriba. Sin esto se
# puede medir bien una grabacion y que la radio diga "sin identificar",
# y no salta nada. Ver tools/stanag_check.py.
.PHONY: stanagtabla
stanagtabla: ; python3 tools/stanag_check.py $(FIRMWARE)

comprueba: stanagtabla

.PHONY: cargafuera
cargafuera: out/cargafuera out/fuera_2k.img out/fuera_fab.img out/fuera_16m.img
	@echo '  -- volumen de FABRICA, fichero puesto por mcopy --'
	./out/cargafuera out/fuera_fab.img $(UPD) 2
	@echo '  -- 7 MB con clusters de 2 kB, fichero puesto por mcopy --'
	./out/cargafuera out/fuera_2k.img $(UPD) 8
	@echo '  -- 15 MB en un W25Q128, el chip mas grande que admite --'
	./out/cargafuera out/fuera_16m.img $(UPD) 16

comprueba: cargafuera

cargador: out/cargador out/fat12_vacio.img out/fat12_2k.img out/fat12_4k.img
	@echo '  -- volumen de FABRICA: 1 sector por cluster --'
	./out/cargador out/fat12_vacio.img $(UPD)
	@echo '  -- volumen de WINDOWS POR OMISION: clusters de 2 kB en 7 MB --'
	./out/cargador out/fat12_2k.img $(UPD)
	@echo '  -- volumen de WINDOWS a mano: clusters de 4 kB --'
	./out/cargador out/fat12_4k.img $(UPD)

comprueba: cargador

# ---------------------------------------------------------------------
# EL DISCO QUE VE WINDOWS (01/10/2026). Ver sim/disco.c.
#
# Pedido por el usuario. La clase MSC escribe
# bloques de 512 en el orden que quiere y la flash borra de 4 kB en 4 kB.
# Esto comprueba, en los OCHO desplazamientos posibles, que escribir un
# bloque no se lleva por delante los otros siete de su sector.
out/disco: sim/disco.c sim/w25q_sim.c sim/gd32_pines.c \
           $(FIRMWARE)/User/disco.c $(FIRMWARE)/User/spi_flash.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -Igfx2 -o $@ \
	      sim/disco.c sim/w25q_sim.c sim/gd32_pines.c \
	      $(FIRMWARE)/User/disco.c $(FIRMWARE)/User/spi_flash.c $(LDLIBS)

.PHONY: disco
disco: out/disco ; ./out/disco out/volumen_con_config.img

comprueba: disco

# ---------------------------------------------------------------------
# EL ATAJO DE DFU DEL CARGADOR (01/10/2026). Ver sim/atajo.c.
#
# Pedido por el usuario. Es la salida de emergencia: si el cargador
# nuevo no arranca, no hay aplicacion y no hay boton de DFU. Este gesto se
# mira antes que nada, asi que sigue sirviendo.
#
# Y por eso tiene banco: un falso positivo aqui deja la radio en negro al
# encender, que es indistinguible de un cargador roto. La version que
# contaba cambios de estado en vez de pasos con signo lo habria hecho con
# solo que las lineas del mando tuvieran ruido.
out/atajo: sim/atajo.c $(FIRMWARE)/boot/atajo_gesto.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/boot -Ihostinc -o $@ \
	      sim/atajo.c $(FIRMWARE)/boot/atajo_gesto.c $(LDLIBS)

.PHONY: atajo
atajo: out/atajo ; ./out/atajo

comprueba: atajo

# ---------------------------------------------------------------------
# LA TRADUCCION DE LA CLASE MSC (01/10/2026). Ver sim/msc.c.
#
# Existe por el fallo que costo una tarde entera: la libreria de
# GigaDevice pasa a mem_read()/mem_write() una direccion en BYTES, no un
# numero de bloque (usbd_msc_scsi.c linea 509), y disco_msc.c la tomaba
# por el bloque. Windows montaba la unidad un instante y la soltaba.
#
# sim/disco.c prueba la capa de abajo a conciencia. Esto prueba la de
# arriba, que es donde estaba el fallo y donde yo habia escrito que "no
# hay ni una decision".
out/msc: sim/msc.c $(FIRMWARE)/boot/disco_msc.c hostinc_msc/usbd_msc_mem.h
	@mkdir -p out
	$(CC) $(CFLAGS) -Ihostinc_msc -I$(FIRMWARE)/User -o $@ \
	      sim/msc.c $(FIRMWARE)/boot/disco_msc.c

.PHONY: msc
msc: out/msc ; ./out/msc

comprueba: msc

# ---------------------------------------------------------------------
# EL DEMODULADOR DE STANAG 4285 (02/10/2026). Ver User/stanag_rx.h.
#
# Este banco es distinto de todos los demas de stanag: no comprueba una
# metrica, comprueba UNA FRASE. Le pasa a User/stanag_rx.c -el codigo que
# va en la radio, compilado tal cual- la grabacion de aire
# sim/muestras/stanag4481_psk.mp3, y exige que de ahi salga
#
#     "This is a Stanag4481 test message"
#
# Se eligio asi a proposito: un Viterbi SIEMPRE devuelve algo, y con
# ruido devuelve bits con buena cara. Lo unico que separa "ha
# enganchado" de "se lo ha inventado" es que salga ingles.
out/stanagrx: sim/stanag_rx_aire.c $(FIRMWARE)/User/stanag_rx.c \
              $(FIRMWARE)/User/hfdl_viterbi.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $^ $(LDLIBS)

.PHONY: stanagrx
stanagrx: out/stanagrx out/muestras/stanag4481_psk.wav
	./out/stanagrx out/muestras/stanag4481_psk.wav

comprueba: stanagrx

# ---------------------------------------------------------------------
# EL BORRADO POR ADELANTADO DE LA CAPTURA (05/10/2026). Ver la cabecera de
# sim/borrado.c: un borrado de 64 kB se lleva los 64 kB enteros, y la zona
# reservada casi nunca empieza en un limite de 64 kB.
out/borrado: sim/borrado.c sim/w25q_sim.c sim/gd32_pines.c $(FIRMWARE)/User/spi_flash.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/borrado.c sim/w25q_sim.c sim/gd32_pines.c \
	      $(FIRMWARE)/User/spi_flash.c $(LDLIBS)

.PHONY: borrado
borrado: out/borrado ; ./out/borrado

comprueba: borrado

# ---------------------------------------------------------------------
# LA COLA DE PITIDOS (05/10/2026). Ver la cabecera de sim/pitidos.c: se
# quedaba llena para siempre despues de la primera captura.
out/pitidos: sim/pitidos.c $(FIRMWARE)/User/bip.c $(FIRMWARE)/User/nco.c \
             $(FIRMWARE)/User/nco_tabla.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: pitidos
pitidos: out/pitidos ; ./out/pitidos

comprueba: pitidos

# ---------------------------------------------------------------------
# LA CAPTURA EN LOS MENUS (05/10/2026). Ver la cabecera de
# sim/captura_menus.c: las seis pantallas de menu, pixel a pixel contra lo
# que se ve, y cada una dentro de su hueco de 104 a 421.
#
# Compila los ficheros del FIRMWARE, no las copias de gfx2/: es lo que de
# verdad corre, y ui_hora.c y ui_qth.c ni siquiera tienen copia.
out/capturamenus: sim/captura_menus.c sim/lcd_sim.c vendor/gfx.c \
                  gfx2/gfx2.c gfx2/fonts_data.c \
                  $(FIRMWARE)/User/ui_top.c $(FIRMWARE)/User/ui_act.c \
                  $(FIRMWARE)/User/ui_grid.c $(FIRMWARE)/User/ui_cfg.c \
                  $(FIRMWARE)/User/ui_det.c $(FIRMWARE)/User/ui_kbd.c \
                  $(FIRMWARE)/User/ui_hora.c $(FIRMWARE)/User/ui_qth.c \
                  $(FIRMWARE)/User/ui_digi.c vendor/spectrum.c sim/ipa_sim.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) $(VENDOR) -I$(FIRMWARE)/User -I sim/ipa -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: capturamenus
capturamenus: out/capturamenus ; ./out/capturamenus

ifneq ($(wildcard vendor/gfx.c),)
comprueba: capturamenus
endif

# ---------------------------------------------------------------------
# El rotulo de la emisora de la pantalla de la hora, medido LEYENDO
# PIXELES. Cinco de los diez salian cortados y ningun banco lo veia:
# captura_menus.c rellena ese campo con "DCF77", que es un marcador corto
# y no el nombre de verdad. Ver la cabecera del banco.
# dcf77.c arrastra medio arbol (RBU, WWV, husos, rapido_atan2), y lo unico
# que hace falta de el son los NOMBRES de k_emisoras[]. Se enlaza entero y
# se le dan los cabos sueltos en el propio banco: asi los nombres salen del
# fichero de verdad y no de una copia que se quede vieja.
out/horabtn: sim/hora_btn.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
             $(FIRMWARE)/User/ui_hora.c $(FIRMWARE)/User/dcf77.c \
             $(FIRMWARE)/User/rapido.c $(FIRMWARE)/User/hora_comun.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: horabtn
horabtn: out/horabtn ; ./out/horabtn

comprueba: horabtn

# ---------------------------------------------------------------------
# Lo que cuesta pintar el mapa. Intercepta gfx2_line() con --wrap y cuenta
# cuantos trazos entran y cuantos escriben de verdad un pixel: sin la
# guarda de banda de mapa.c era el 6 %. Ver la cabecera del banco.
out/mapacoste: sim/mapa_coste.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
               $(FIRMWARE)/User/mapa.c $(FIRMWARE)/User/mapa_datos.c \
               $(FIRMWARE)/User/mapa_bin.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) \
	    -Wl,--wrap=gfx2_line $(LDLIBS)

.PHONY: mapacoste
mapacoste: out/mapacoste ; ./out/mapacoste

comprueba: mapacoste

# ---------------------------------------------------------------------
# Que gfx2_text_cabe() da EXACTAMENTE lo mismo que el recorte de una letra
# en una letra que habia antes en ui_digi.c. Una optimizacion que cambia
# el dibujo no es una optimizacion. Ver la cabecera del banco.
out/textocabe: sim/texto_cabe.c gfx2/gfx2.c gfx2/fonts_data.c sim/lcd_sim.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: textocabe
textocabe: out/textocabe ; ./out/textocabe

comprueba: textocabe

# ---------------------------------------------------------------------
# QUE EL CARTEL DE LA CAPTURA QUEPA (05/10/2026). Ver la cabecera de
# sim/avisocabe.c: la mitad del texto lo escribe spi_flash.c, la fuente es
# proporcional y gfx2_text_in() recorta sin decir nada.
sim/avisos_tabla.h: $(FIRMWARE)/User/main.c $(FIRMWARE)/User/spi_flash.c tools/gen_avisos.py
	python3 tools/gen_avisos.py $(FIRMWARE)/User/main.c $(FIRMWARE)/User/spi_flash.c $@

out/avisocabe: sim/avisocabe.c sim/avisos_tabla.h sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -Isim -o $@ \
	      sim/avisocabe.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c $(LDLIBS)

.PHONY: avisocabe
avisocabe: out/avisocabe ; ./out/avisocabe

comprueba: avisocabe

# ---------------------------------------------------------------------
# "DIRECTORIO LLENO" CON UN FICHERO DENTRO (05/10/2026). Ver la cabecera de
# sim/dirlleno.c: los recorridos de la raiz leian UN sector y Windows
# formatea con 224 o 512 entradas.
out/dirlleno: sim/dirlleno.c sim/w25q_sim.c sim/gd32_pines.c $(FIRMWARE)/User/spi_flash.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/dirlleno.c sim/w25q_sim.c sim/gd32_pines.c \
	      $(FIRMWARE)/User/spi_flash.c $(LDLIBS)

.PHONY: dirlleno
dirlleno: out/fat12_2k.img out/dirlleno ; ./out/dirlleno

comprueba: dirlleno

# ---------------------------------------------------------------------
# LA TABLA DE FIRMAS DEL IDENT (05/10/2026). Ver la cabecera de
# sim/identtest.c: ident_casa() es pura, asi que la tabla se prueba entera
# sin fabricar una sola señal.
out/identtest: sim/identtest.c $(FIRMWARE)/User/ident.c \
               $(FIRMWARE)/User/stanag_det.c \
               $(FIRMWARE)/User/hfdl_ram.c sim/waterfall_prestamo.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib -Ihostinc -Isim \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: identtest
identtest: out/identtest ; ./out/identtest

# ---------------------------------------------------------------------
# LA REGLA DEL BLOQUE CORRUPTO - 10/10/2026.
#
# Ver la cabecera de sim/corrupto.c. La decision de tirar un bloque de I/Q
# vivia DENTRO del manejador del DMA, o sea en el unico sitio del proyecto
# donde no se puede medir. Y es una decision que ya se equivoco una vez -en
# WFM, el 05/10- y que cuando se equivoca no da un error: repite 2,67 ms de
# audio ya sonado, 375 veces por segundo, y eso suena a siseo.
#
# Ahora la regla vive en User/rx_corrupto.c, pura, y esto le da bloques
# fabricados y mira que contesta. Lo primero que contesto fue que una senal
# LIMPIA al 49% del fondo de escala, a 20 kHz del oscilador aparcado -que es
# el margen normal de la radio- se tira.
out/corrupto: sim/corrupto.c $(FIRMWARE)/User/rx_corrupto.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ $(FUENTES) $(LDLIBS)

.PHONY: corrupto
corrupto: out/corrupto ; ./out/corrupto

comprueba: corrupto

# ---------------------------------------------------------------------
# EL IDENT CONTRA LAS GRABACIONES, CONTANDO VENTANAS - 10/10/2026.
#
# Ver la cabecera de sim/ident_grabaciones.c. identtest prueba la TABLA con
# medidas inventadas; esto prueba EL MOTOR ENTERO contra nueve grabaciones
# de verdad, y no mirando como acaban -una grabacion que se desvanece al
# final deja las ultimas ventanas sin señal, y juzgar por ahi es juzgar por
# el silencio- sino contando en cuantas ventanas la radio acierta.
#
# Nacio del pendiente #193 ("fsk4481_5339.mp3 sale como CW 40%") y de lo que
# salio detras: ident_modo_poll() publicaba la ULTIMA ventana. En esa
# grabacion la radio acertaba 1 ventana de 380; ahora, 139.
IDENT_GRAB_WAV := out/muestras/stanag4481_fsk.wav out/muestras/fsk4481_5339.wav \
                  out/muestras/sitorb_tfc.wav out/muestras/navtex8424.wav \
                  out/muestras/stanag4285_tfc.wav out/muestras/stanag4529.wav

out/identgrab: sim/ident_grabaciones.c $(FIRMWARE)/User/ident.c \
               $(FIRMWARE)/User/stanag_det.c \
               $(FIRMWARE)/User/hfdl_ram.c sim/waterfall_prestamo.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib -Ihostinc -Isim \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: identgrab
identgrab: out/identgrab $(IDENT_GRAB_WAV) ; ./out/identgrab

comprueba: identgrab

# EL IDENT CON VARIAS SEÑALES A LA VEZ. Ver la cabecera de sim/ident_varias.c:
# se grabaron tres RTTY de aficionado en 7 MHz durante un concurso y la
# radio contesto "APRS 70%". La grabacion es suya y va en el repositorio
# porque es el unico material que tenemos de ese caso.
out/identvarias: sim/ident_varias.c $(FIRMWARE)/User/ident.c \
                 $(FIRMWARE)/User/stanag_det.c \
                 $(FIRMWARE)/User/hfdl_ram.c sim/waterfall_prestamo.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib -Ihostinc -Isim \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: identvarias
identvarias: out/identvarias ; ./out/identvarias

comprueba: identvarias

# LA POSICION DE LOS AVIONES EN HFDL. Ver la cabecera de sim/hfdl_posicion.c:
# hfdl_hfnpdu.c no lo compilaba NINGUN banco, y es el unico sitio del arbol
# que convierte bytes del aire en una latitud y una longitud.
out/hfdlpos: sim/hfdl_posicion.c $(FIRMWARE)/User/hfdl_hfnpdu.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $^ $(LDLIBS)

.PHONY: hfdlpos
hfdlpos: out/hfdlpos ; ./out/hfdlpos

comprueba: hfdlpos

# Que los dos caminos de audio no se miren. Ver la cabecera de
# tools/via_audio_check.py: desde el 10/10/2026 WFM y el estrecho comparten
# los mismos bytes, y eso solo es correcto mientras cada uno toque solo lo
# suyo. No hay banco que compile demod_am.c -37 cabeceras propias- asi que
# esto es lo que vigila el invariante.
.PHONY: viaaudio
viaaudio: ; python3 $(FIRMWARE)/tools/via_audio_check.py $(FIRMWARE)/User/demod_am.c

comprueba: viaaudio

comprueba: identtest

# sim/identancho.c: que los nombres y las notas de k_firmas[] quepan en las
# dos cosas que los pueden cortar -el buffer de IDENT_LARGO y el ancho del
# panel en pixeles-, en los dos idiomas. Nacio el 06/10/2026 porque la nota
# del STANAG 4285 salia en la radio cortada a mitad de palabra: snprintf la
# recortaba en silencio y no habia nada que lo midiera.
# ident.h va de prerequisito a proposito: lo que esta prueba mide es
# IDENT_LARGO, que vive ahi. Sin esto, tocar el tope no rehace la prueba y
# la prueba contesta por el binario viejo - que es justo lo que ocurrio al
# escribirla.
out/identancho: sim/identancho.c $(FIRMWARE)/User/ident.h \
                $(FIRMWARE)/User/ident.c \
                $(FIRMWARE)/User/stanag_det.c \
                $(FIRMWARE)/User/hfdl_ram.c sim/waterfall_prestamo.c \
                gfx2/gfx2.c gfx2/fonts_data.c sim/lcd_sim.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib \
	      -Ihostinc -Isim -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: identancho
identancho: out/identancho ; ./out/identancho

comprueba: identancho

# tools/sin_llamar.py: las funciones de motor (_poll/_tick/_paso/_arranca)
# que no llama nadie. Nacio el 06/10/2026 porque rtty_auto_poll() llevaba
# ocho versiones sin llamarse: el modulo entero del ajuste automatico del
# RTTY estaba montado y sin enchufar, y no lo vio ni el compilador, ni el
# enlazador, ni su propio banco, ni el usuario usando la radio.
#
# YA NO AVISA: CORTA - 08/10/2026. Llevaba dos dias sacando
# "cargador_arranca: NADIE la llama" en cada comprueba, y era MENTIRA -la
# llama boot/boot_main.c:1037, que es justo quien graba el update.bin-,
# porque este script solo leia User/. Un aviso falso que sale siempre se
# convierte en parte del paisaje, y entonces el dia que salga uno de verdad
# tampoco lo va a leer nadie. Arreglado el script para que lea boot/, y
# puesto a cortar: los motivos legitimos van en su BLANCA con el porque
# escrito, que es barato, y asi la lista vacia significa algo.
.PHONY: sinllamar
sinllamar: ; python3 $(FIRMWARE)/tools/sin_llamar.py

# QUE EL TRAMPOLIN DEL MAKE SIGA SIENDO UN TRAMPOLIN - 09/10/2026.
#
# Pedido por el usuario.
#
# El puente que escribe en su disco rechaza los ficheros llamados "Makefile",
# asi que cada regla nueva habia que pasarsela por el chat. GNU make lee
# GNUmakefile ANTES que Makefile, asi que ahora el de verdad es
# GNUmakefile -una linea, include reglas.mk- y todo lo que yo toco vive en
# reglas.mk, que si se puede escribir.
#
# Esto vigila lo unico que puede romper el reparto: que alguien escriba
# reglas dentro del trampolin, que es justo el fichero que luego no se podra
# arreglar desde fuera. Ver tools/mkfile_check.py.
.PHONY: mkfile
mkfile: ; python3 $(FIRMWARE)/tools/mkfile_check.py

comprueba: mkfile

# CUANTO SE MUEVE LA ESTIMACION DE VELOCIDAD DEL CW - 09/10/2026.
#
# El pendiente decia "s_dot_hops se mueve de 10 a 14, un 40%", y el
# problema es la vara de medir, no los umbrales. cwtest.c barre ruido y
# cuenta letras, pero imprime la velocidad UNA vez, al final: una
# estimacion puede acabar clavada en 20 habiendo pasado media pasada en 14
# y media en 28, y eso es justo lo que hace daño, porque TODO lo que decide
# cw.c se mide contra ella.
#
# Esto la mira bloque a bloque. Fuera de comprueba de momento y a
# proposito: primero sale el numero, despues se decide que es aceptable.
# Ver sim/cw_vara.c.
out/cwvara: sim/cw_vara.c sim/cw_sintetiza.h $(FIRMWARE)/User/cw.c
	@mkdir -p out
	$(CC) $(CFLAGS) -Isim -I$(FIRMWARE)/User -o $@ sim/cw_vara.c $(FIRMWARE)/User/cw.c $(LDLIBS)

.PHONY: cwvara
cwvara: out/cwvara ; ./out/cwvara

comprueba: sinllamar

# ---------------------------------------------------------------------
# LAS CADENAS DE PANTALLA SIN TRADUCIR (06/10/2026). Ver la cabecera de
# tools/idioma_check.py: recorre User/*.c y falla si una cadena de interfaz
# no pasa por tr() ni por T().
.PHONY: idioma
idioma: ; python3 tools/idioma_check.py $(FIRMWARE)

comprueba: idioma

# ---------------------------------------------------------------------
# FORMATEAR EL DISCO DESDE LA RADIO (06/10/2026). Ver sim/formato_test.c:
# lo que escribe el formateador tiene que aceptarlo el MISMO spi_flash.c
# que rechazaba el volumen del issue.
out/formato: sim/formato_test.c sim/w25q_sim.c sim/gd32_pines.c \
             $(FIRMWARE)/User/formato.c $(FIRMWARE)/User/disco.c \
             $(FIRMWARE)/User/spi_flash.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib -Ihostinc -Isim \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: formato
formato: out/formato ; ./out/formato

comprueba: formato

# ---------------------------------------------------------------------
# EL FICHERO .map DEL USUARIO (12/10/2026). Ver sim/mapa_fichero.c: el
# lector de la radio contra ficheros fabricados aqui -buenos y rotos de
# las seis formas que se rompen- y contra uno de verdad salido de
# tools/mapa_pack.py, que es lo unico que comprueba que los dos lados
# entienden el mismo formato.
out/mapafich: sim/mapa_fichero.c $(FIRMWARE)/User/mapa_bin.c \
              $(FIRMWARE)/User/mapa_bin.h datos/prueba.map
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/mapa_fichero.c \
	      $(FIRMWARE)/User/mapa_bin.c $(LDLIBS)

.PHONY: mapafich
mapafich: out/mapafich ; ./out/mapafich

comprueba: mapafich

# EL MAPA GRABADO EN LA FLASH (12/10/2026). Ver sim/mapa_guardado.c: el
# ritual entero contra el chip simulado a nivel de pin - grabar, verificar,
# borrar del disco- y sobre todo los caminos que NO pueden salir bien: una
# copia cortada por la mitad, un .map roto, y una base de datos que ocupa
# el hueco. En los tres, lo que tiene que quedar es el mapa de serie y el
# fichero del usuario intacto en el disco.
out/mapaguard: sim/mapa_guardado.c sim/w25q_sim.c sim/gd32_pines.c \
               $(FIRMWARE)/User/mapa_flash.c $(FIRMWARE)/User/mapa_bin.c \
               $(FIRMWARE)/User/zona_alta.c $(FIRMWARE)/User/spi_flash.c \
               $(FIRMWARE)/User/formato.c $(FIRMWARE)/User/disco.c \
               datos/prueba.map
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib -Ihostinc -Isim \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: mapaguard
mapaguard: out/mapaguard ; ./out/mapaguard

comprueba: mapaguard

# EL VOLTIMETRO CONTRA LA REFERENCIA INTERNA (12/10/2026). Ver
# sim/bateria.c: lo que de verdad decide si el arreglo del issue vale no
# es que la cuenta sea bonita, es que el ABANICO de lecturas entre
# unidades se estreche. Se simulan diez radios con el regulador entre
# 2,85 y 3,15 V midiendo la MISMA bateria, y se compara el abanico de
# antes con el de ahora.
out/bateria: sim/bateria.c $(FIRMWARE)/User/battery_cuentas.c \
             $(FIRMWARE)/User/battery.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: bateria
bateria: out/bateria ; ./out/bateria

comprueba: bateria

# LOS TRES NIVELES Y LA VENTANA (12/10/2026). Ver sim/mapa_niveles.c: lo
# que decide si el diseno vale no es que compile, es que un ARRASTRE de
# punta a punta no dispare una recarga por fotograma. Se mide.
out/mapaniv: sim/mapa_niveles.c $(FIRMWARE)/User/mapa_bin.c \
             $(FIRMWARE)/User/mapa_bin.h datos/prueba.map
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/mapa_niveles.c \
	      $(FIRMWARE)/User/mapa_bin.c $(LDLIBS)

.PHONY: mapaniv
mapaniv: out/mapaniv ; ./out/mapaniv

comprueba: mapaniv

# ---------------------------------------------------------------------
# LO QUE WINDOWS DICE DEL DISCO (11/10/2026). Ver sim/espacio_win.c: imita
# la cuenta que hace Windows -sector de arranque y FAT, sin preguntarle a
# la radio- y comprueba que formatear deja el disco vacio DE VERDAD y que
# escribir, borrar y reservar no dejan clusters huerfanos.
out/espaciowin: sim/espacio_win.c sim/w25q_sim.c sim/gd32_pines.c \
                $(FIRMWARE)/User/formato.c $(FIRMWARE)/User/disco.c \
                $(FIRMWARE)/User/spi_flash.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib -Ihostinc -Isim \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: espaciowin
espaciowin: out/espaciowin ; ./out/espaciowin

comprueba: espaciowin

# ---------------------------------------------------------------------
# EL RTTY AJUSTANDOSE SOLO (06/10/2026). Ver sim/rtty_solo.c. La muestra
# va dentro del repo porque el banco no vale sin ella.
out/rttysolo: sim/rtty_solo.c $(FIRMWARE)/User/rtty.c $(FIRMWARE)/User/rtty_auto.c \
              $(FIRMWARE)/User/rtty_scope.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib -Ihostinc -Isim \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: rttysolo
rttysolo: out/rttysolo ; ./out/rttysolo sim/muestras/rtty3712.f32

comprueba: rttysolo

# ---------------------------------------------------------------------
# EL RTTY CONTRA UNA GRABACION (06/10/2026). Ver sim/rtty_aire.c. No entra
# en "comprueba": hace falta un fichero.
out/rttyaire: sim/rtty_aire.c $(FIRMWARE)/User/rtty.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib -Ihostinc -Isim \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: rttyaire
rttyaire: out/rttyaire

# ---------------------------------------------------------------------
# DOS SEÑALES TURNANDOSE (06/10/2026). Ver la cabecera de
# sim/ident_turnos.c: el audio se fabrica dentro, no hace falta grabacion.
out/identturnos: sim/ident_turnos.c $(FIRMWARE)/User/ident.c \
                 $(FIRMWARE)/User/stanag_det.c \
                 $(FIRMWARE)/User/hfdl_ram.c sim/waterfall_prestamo.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib -Ihostinc -Isim \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: identturnos
identturnos: out/identturnos ; ./out/identturnos

comprueba: identturnos

# ---------------------------------------------------------------------
# EL IDENT CONTRA UN AUDIO DE VERDAD (05/10/2026). Ver la cabecera de
# sim/identaire.c. No entra en "comprueba": hace falta un fichero.
#   ffmpeg -i loquesea.ogg -ac 1 -ar 12000 -f f32le x.f32
#   make identaire FIRMWARE=... && ./out/identaire x.f32
out/identaire: sim/identaire.c $(FIRMWARE)/User/ident.c \
               $(FIRMWARE)/User/stanag_det.c \
               $(FIRMWARE)/User/hfdl_ram.c sim/waterfall_prestamo.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib -Ihostinc -Isim \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: identaire
identaire: out/identaire

# LA GANANCIA QUE SE GUARDA Y LA QUE SE RESTAURA - 05/10/2026.
#
# Del video del arranque: la radio arrancaba en WFM con el espectro plano
# y S6, y bastaba cambiar de modo y volver para que saliera la emisora en
# S9+7. El bloque de main() que aplica lo guardado corre DESPUES del
# apply_demod_mode() del arranque y le pisaba el tope de WFM. El mismo
# fallo al reves lo tenian los dos settings_save_now() de las
# calibraciones: guardaban la ganancia VIVA, que en WFM es un apano de la
# radio y no un ajuste tuyo. Hay dos variables que se parecen mucho y se
# confunden solas, asi que esto se mide. Ver tools/pga_check.py.
.PHONY: pga
pga: ; python3 tools/pga_check.py $(FIRMWARE)/User/main.c
comprueba: pga

# LA ARCOTANGENTE RAPIDA - 06/10/2026.
#
# rapido_atan2() la llama el discriminador de FM una vez por MUESTRA, o sea
# 192.000 veces por segundo en WFM. La version del 04/10 usaba una razon que
# empeora cuando el angulo se acerca a cero -justo donde mas se usa- y metia
# un 2 % de distorsion en NFM con el tercer armonico a -35 dBc, con la firma
# al reves de lo que uno espera: cuanto mas floja la modulacion, peor sonaba.
# Esto mide el error, la PENDIENTE cerca de cero -que el error maximo no
# enseña- y la THD del discriminador contra atan2f(). Ver sim/atantest.c.
out/atantest: sim/atantest.c $(FIRMWARE)/User/rapido.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/atantest.c \
	      $(FIRMWARE)/User/rapido.c $(LDLIBS)

.PHONY: atan
atan: out/atantest ; ./out/atantest
comprueba: atan

# LEER NO ES ESCRIBIR - 06/10/2026.
#
# De un issue: el cargador no veia update.bin en el disco, con la ficha
# diciendo "Volumen: no se reconoce" y la radio con el formateo DE FABRICA.
# Los dos sintomas eran el mismo fallo: cargador_busca_update() se va sin
# mirar el directorio si la geometria no vale, y geo_lee() contestaba a dos
# preguntas distintas -leer y escribir- con un solo si o no. Las
# comprobaciones duras (dos FAT, cluster que divida el bloque de borrado,
# datos alineados) son de este driver y no del formato: hacen falta para
# escribir, no para leer. Esto comprueba las dos cosas, y sobre todo que la
# negativa a ESCRIBIR sigue intacta. Ver sim/geo_lectura.c.
out/geolectura: sim/geo_lectura.c sim/w25q_sim.c sim/gd32_pines.c $(FIRMWARE)/User/spi_flash.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/geo_lectura.c sim/w25q_sim.c sim/gd32_pines.c \
	      $(FIRMWARE)/User/spi_flash.c $(LDLIBS)

.PHONY: geolectura
geolectura: out/fat12_vacio.img out/fat12_pc.img out/fat12_1fat.img out/fat16.img out/geolectura ; ./out/geolectura
comprueba: geolectura

# QUE EL PIE DE LA REJILLA NO SE SALGA DE SU HUECO - 06/10/2026.
#
# El usuario leyo "0 0 1 75-255/10" en la fila de audio: el numero acababa en
# "/10" porque el texto estaba CORTADO. Son 27 bytes en un hueco de 24, y
# los tres de mas iban al pie de la celda siguiente, en silencio. El pie se
# arma byte a byte sin nada que recorte, asi que el tamaño lo tiene que
# contar el que añade un numero - y en el PEOR caso, que para un uint32_t
# son diez cifras y no las dos que se ven probando. Ahora lo escriben
# pie_txt()/pie_num(), que recortan, y esto comprueba que siguen siendo las
# unicas. Ver tools/pie_check.py.
.PHONY: pie
pie: ; python3 tools/pie_check.py $(FIRMWARE)/User/main.c
comprueba: pie

# Un FAT12 con UNA sola copia de la tabla: legal y legible, pero este driver
# no sabe mantener coherente un numero de copias que no sea dos. Es el caso
# que separa "puedo leer" de "puedo escribir" - ver sim/geo_lectura.c.
out/fat12_1fat.img:
	@mkdir -p out
	dd if=/dev/zero of=$@ bs=512 count=2048 status=none
	mkfs.fat -F 12 -R 4 -f 1 -r 512 -s 1 -S 512 -n UNAFAT $@ >/dev/null

# Y un FAT16: mas de 4084 clusters, entradas de 16 bits. No se lee NI se
# escribe, y la comprobacion va del lado de LEER porque recorrerlo como
# FAT12 no da error: da clusters equivocados.
out/fat16.img:
	@mkdir -p out
	dd if=/dev/zero of=$@ bs=512 count=20480 status=none
	mkfs.fat -F 16 -R 4 -f 2 -r 512 -s 1 -S 512 -n ESFAT16 $@ >/dev/null

# QUE EL PIE DE LA CELDA QUEPA EN LA CELDA - 06/10/2026.
#
# Pedido por el usuario: el pie de la celda salia cortado, y la segunda vez
# seguia saliendo asi. La primera lo arregle contando caracteres y ampliando el
# buffer, y la segunda seguia cortado - porque el limite que manda NO es el
# buffer: es el ANCHO de la celda, 192 px, con una fuente PROPORCIONAL. Por
# eso los dos recortes caen en sitios distintos. Contar no sirve: esto MIDE,
# con gfx2_text_w() y la misma fuente que pinta el renglon. Ver
# sim/pie_ancho.c.
out/pieancho: sim/pie_ancho.c sim/lcd_sim.c vendor/gfx.c gfx2/gfx2.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) -Igfx2 -Ihostinc -Isim -Ivendor -I$(FIRMWARE)/User -o $@ \
	      sim/pie_ancho.c sim/lcd_sim.c vendor/gfx.c gfx2/gfx2.c gfx2/fonts_data.c $(LDLIBS)

.PHONY: pieancho
pieancho: out/pieancho ; ./out/pieancho
ifneq ($(wildcard vendor/gfx.c),)
comprueba: pieancho
endif

# QUE EL DIBUJO NO SE ENGANCHE AL AUDIO - 06/10/2026.
#
# sdr_spectrum_waterfall_tick() vuelve entera si no ha llegado un bloque de
# audio, asi que el dibujo solo puede empezar en el instante de un bloque y
# el plazo del siguiente se cuenta desde ahi: el tiempo entre cuadros era
# siempre un numero ENTERO de bloques y no se soltaba nunca. Con eso, todos
# los armonicos del repintado caen encima de los multiplos de 375 Hz -la
# tasa de bloques- y el oido los junta en un TONO. Eso es el siseo: no es
# mas energia, es energia apilada. Medido en el audio del usuario: +31,6 dB
# sobre el fondo en 750 Hz con bandas laterales a 41,75 Hz, que son los 24,0
# ms exactos de nueve bloques. Ver tools/vaiven_check.py.
.PHONY: vaiven
vaiven: ; python3 tools/vaiven_check.py $(FIRMWARE)/User/main.c
comprueba: vaiven

# EL DETECTOR DE DOS TONOS, CONTRA GRABACIONES DE VERDAD - 06/10/2026.
#
# Pedido por el usuario. Y la pantalla decia "un solo tono o fase: no es FSK de
# dos" delante de una FSK que la propia radio estaba decodificando en RTTY-U
# con 850 Hz y 75 Bd. Los criterios que miran las rayas POR SEPARADO daban
# por hecho que los dos tonos llegan parejos, y eso es cierto en el
# transmisor: despues de 2.000 km de ionosfera uno llega 11 dB mas flojo que
# el otro, y un minuto despues al reves. Por eso "iba variando".
# Ver sim/tonos_aire.c.
TONOMUESTRAS := $(patsubst sim/muestras/%.mp3,out/muestras/%.wav,$(wildcard sim/muestras/*.mp3))

out/tonosaire: sim/tonos_aire.c $(FIRMWARE)/User/stanag_det.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/tonos_aire.c $(FIRMWARE)/User/stanag_det.c -lm

.PHONY: tonosaire
tonosaire: out/tonosaire $(TONOMUESTRAS)
	@echo '  -- el detector de dos tonos, sobre las grabaciones --'
	@for w in $(TONOMUESTRAS); do ./out/tonosaire $$w || exit 1; done

comprueba: tonosaire

# ALE SINTETICO: DE LA PALABRA AL AIRE Y DE VUELTA - enganchado el 06/10/2026.
#
# El fichero estaba escrito desde hace tiempo y NO LO CORRIA NADIE: no tenia
# objetivo en este Makefile. Un banco que no se ejecuta es peor que no
# tenerlo, porque da la sensacion de que algo esta probado. Se engancha al
# preguntar el usuario si las rafagas ALE de 5339 kHz se pueden decodificar.
# Prueba las siete convenciones que hay entre el indicativo y la pantalla y
# que se pueden poner del reves sin que nada se queje. Ver sim/ale_aire.c.
out/aleaire: sim/ale_aire.c $(FIRMWARE)/User/ale_rx.c $(FIRMWARE)/User/ale_golay.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/ale_aire.c $(FIRMWARE)/User/ale_rx.c \
	      $(FIRMWARE)/User/ale_golay.c $(LDLIBS)

.PHONY: aleaire
aleaire: out/aleaire ; ./out/aleaire
comprueba: aleaire

# ===========================================================================
# BANCOS QUE EXISTIAN Y NO CORRIAN - 07/10/2026
# ===========================================================================
#
# Al repasar el arbol salieron 105 binarios de banco definidos y solo 82
# colgados de "comprueba". De los 23 que faltaban, la mayoria son
# HERRAMIENTAS que piden un fichero por la linea de ordenes (cwaire,
# identaire, rttyaire, sstvaire, stanagaire, tonosaire, stanagrx,
# cargafuera): esas no pintan nada aqui y se quedan fuera a proposito.
#
# Pero seis eran bancos de verdad, con su veredicto y su codigo de salida,
# que nadie ejecutaba nunca. Un banco que no corre no puede fallar, y la
# unica forma de enterarse de que lleva roto desde hace semanas es
# ejecutarlo. (helltest, el septimo, ni siquiera ENLAZABA desde V3.75; se
# arregla mas arriba, en su propia regla.)
#
# Los seis pasan hoy. El valor no es ese: es que a partir de ahora, cuando
# dejen de pasar, se note.
navtex2:    out/navtextest ; ./out/navtextest
icao24chk:  out/icao24     ; ./out/icao24
dirllenochk: out/dirlleno  ; ./out/dirlleno
fatanadechk: out/fatanade  ; ./out/fatanade
digichk:    out/digi       ; ./out/digi
puntaschk:  out/puntas     ; ./out/puntas
comprueba: navtex2 icao24chk dirllenochk fatanadechk digichk
ifneq ($(wildcard vendor/gfx.c),)
comprueba: puntaschk
endif

# DSC maritimo. Dos cosas distintas y las dos hacen falta:
#
#   out/dsctest  el entramado, la diversidad DX/RX, el ECC y el parseo de cada
#                formato, con trama sintetica. Es lo UNICO que respalda el
#                formato de SOCORRO, del que no hay grabacion.
#   out/dscaire  el cañon entero contra las grabaciones de verdad. No cuelga de
#                "comprueba" porque pide un fichero por la linea de ordenes.
out/dsctest: sim/dsctest.c $(FIRMWARE)/User/dsc.c $(FIRMWARE)/User/dsc.h \
             $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c \
             $(FIRMWARE)/User/rtty_scope.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ sim/dsctest.c \
	      $(FIRMWARE)/User/dsc.c $(FIRMWARE)/User/nco.c \
	      $(FIRMWARE)/User/nco_tabla.c $(FIRMWARE)/User/rtty_scope.c \
	      $(LDLIBS)

out/dscaire: sim/dscaire.c $(FIRMWARE)/User/dsc.c $(FIRMWARE)/User/nco.c \
             $(FIRMWARE)/User/nco_tabla.c $(FIRMWARE)/User/rtty_scope.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ sim/dscaire.c \
	      $(FIRMWARE)/User/dsc.c $(FIRMWARE)/User/nco.c \
	      $(FIRMWARE)/User/nco_tabla.c $(FIRMWARE)/User/rtty_scope.c \
	      $(LDLIBS)

dsc: out/dsctest ; ./out/dsctest
comprueba: dsc

# ---------------------------------------------------------------------------
# EL ECUALIZADOR ADAPTATIVO, CONTRA UN CANAL CON ECO.
# Ver la cabecera de sim/ecualiztest.c y la de $(FIRMWARE)/User/ecualiz.h.
#
# EL FICHERO QUE FALTABA - 10/10/2026. Esta regla NO enlazaba ecualiz.c:
# nombraba su cabecera y compilaba solo el banco. Desde cero no enlaza, y
# nunca pudo. Pasaba porque out/ecualiztest existia de una regla anterior
# que si lo traia, y make lo daba por al dia mirando fechas. O sea que este
# banco llevaba quien sabe cuanto midiendo un binario viejo.
#
# Lo ha destapado la marca de raiz de arriba, en su PRIMERA pasada, al
# obligar a reconstruir out/ desde cero. Es para lo que esta.
out/ecualiztest: sim/ecualiztest.c \
                 $(FIRMWARE)/User/ecualiz.c $(FIRMWARE)/User/ecualiz.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/ecualiztest.c \
	      $(FIRMWARE)/User/ecualiz.c -lm

.PHONY: ecualiz
ecualiz: out/ecualiztest ; ./out/ecualiztest

comprueba: ecualiz

# ---------------------------------------------------------------------------
# EL ESPEJO I/Q: CUANTO LO BAJA EL CORRECTOR.
# Ver la cabecera de sim/iqbaltest.c y la de $(FIRMWARE)/User/iqbal.h.
# fft.c entra desde el 19/10/2026: el FIR del espejo se calibra DENTRO de
# fft_compute_db_iq(), que es el unico sitio donde existe el espectro
# complejo. El banco prueba ese camino entero, no iqbal.c a solas.
out/iqbaltest: sim/iqbaltest.c $(FIRMWARE)/User/iqbal.c $(FIRMWARE)/User/iqbal.h \
               $(FIRMWARE)/User/fft.c $(FIRMWARE)/User/fft.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/iqbaltest.c \
	      $(FIRMWARE)/User/iqbal.c $(FIRMWARE)/User/fft.c -lm

.PHONY: iqbal
iqbal: out/iqbaltest ; ./out/iqbaltest

comprueba: iqbal

# ---------------------------------------------------------------------------
# EL ESPEJO QUE DEPENDE DE LA FRECUENCIA: el corrector de banda ancha contra
# el plano, tono a tono. Ver la cabecera de sim/iqanchotest.c.
out/iqanchotest: sim/iqanchotest.c $(FIRMWARE)/User/iqancho.c \
                 $(FIRMWARE)/User/iqancho.h $(FIRMWARE)/User/iqbal.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/iqanchotest.c \
	      $(FIRMWARE)/User/iqancho.c $(FIRMWARE)/User/iqbal.c -lm

.PHONY: iqancho
iqancho: out/iqanchotest ; ./out/iqanchotest

comprueba: iqancho

# ---------------------------------------------------------------------------
# EL AVISADOR DE ARMONICOS. Ver la cabecera de sim/armonicotest.c: la prueba
# principal es el caso de verdad del 07/10, dial 3.411 kHz oyendo el FT8 de
# 30 metros por el tercer armonico.
out/armonicotest: sim/armonicotest.c $(FIRMWARE)/User/armonico.c \
                  $(FIRMWARE)/User/armonico.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/armonicotest.c \
	      $(FIRMWARE)/User/armonico.c -lm

.PHONY: armonico
armonico: out/armonicotest ; ./out/armonicotest

comprueba: armonico

# ---------------------------------------------------------------------
# LO QUE CUESTA LA INTERRUPCION DEL ADC (08/10/2026).
#
# Pedido por el usuario: mirar que habia cambiado. Y era eso: dos funciones
# metidas en DMA0_Channel3_IRQHandler,
# por donde pasan TODAS las muestras de TODOS los modos. Tarde tres entregas
# y tres hipotesis falladas en encontrarlo, porque NO HABIA NINGUNA MEDIDA de
# lo que cuesta ese sitio. Ahora la hay, con tope.
out/iqcoste: sim/iq_coste.c $(FIRMWARE)/User/iqbal.c $(FIRMWARE)/User/iqancho.c \
              $(FIRMWARE)/User/fft.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: iqcoste
iqcoste: out/iqcoste ; ./out/iqcoste

comprueba: iqcoste

# ---------------------------------------------------------------------
# LA VELOCIDAD QUE MIDE EL DETECTOR, CONTRA LA QUE ES (07/10/2026).
#
# YA ENTRA EN `comprueba` - 08/10/2026. Estuvo fuera a proposito mientras
# salia en rojo: era la vara de medir del arreglo, no un guardia. El arreglo
# esta hecho -el peine, ver mide_rachas() en stanag_det.c- y las tres pasan:
#
#     sitorb_tfc       100,0 Bd   de 103,3 a 101,1     3% -> 1%
#     navtex8424       100,0 Bd   de 610,8 a 100,7   511% -> 1%
#     stanag4481_fsk    75,0 Bd   de 636,0 a  74,9   748% -> 0%
#
# Un banco que se deja fuera para que no cante es la version educada de no
# tener banco, asi que en cuanto deja de cantar entra. Ver la cabecera de
# sim/det_velocidad.c, donde esta ademas la correccion de una afirmacion mia
# que no estaba medida.
#   make detvel FIRMWARE=... && ./out/detvel
out/detvel: sim/det_velocidad.c $(FIRMWARE)/User/stanag_det.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: detvel
detvel: out/detvel ; ./out/detvel

comprueba: detvel

# ---------------------------------------------------------------------
# CUANTOS REPINTADOS POR SEGUNDO PIDE EL PANEL DE STANAG (07/10/2026).
#
# Del usuario lo pidio. El panel se repintaba DIECISEIS veces por
# segundo porque el latido sumaba la cuenta de medidas del detector, que sube
# 11,7 veces por segundo. Este fallo ha salido TRES VECES en una semana -4285,
# 4415 e identificador- y los comentarios que lo explican no impidieron
# ninguna. Esto si: cuenta los repintados y falla por encima de seis.
out/stanaglatido: sim/stanag_latido.c $(FIRMWARE)/User/stanag_modo.c \
                  $(FIRMWARE)/User/stanag_det.c $(FIRMWARE)/User/stanag_rx.c \
                  $(FIRMWARE)/User/s4415.c $(FIRMWARE)/User/hfdl_viterbi.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      $(filter %.c,$^) $(LDLIBS)

.PHONY: stanaglatido
stanaglatido: out/stanaglatido ; ./out/stanaglatido

comprueba: stanaglatido

# ---------------------------------------------------------------------
# EL BOTON DE APRENDIZAJE DEL CW - 08/10/2026
#
# Del usuario, viendo 4XZ en 4.330 kHz: "osea que hay 2 formas de leer, la
# buena y la mala" ... "pon un boton al lado de borrar que sea
# aprendizaje". Hay dos suposiciones sobre quien teclea, no una buena y
# una mala, y la eligio bien: boton, apagado por omision.
#
# Tres cosas tienen que ser verdad. Que APAGADO no mueva una letra -se
# compara contra la salida exacta de antes de existir el boton-. Que
# ENCENDIDO lea a 4XZ, que apagado no se lee. Y que ENCENDIDO siga
# CALLANDO con ruido, que es la importante: el modelo que se relaja es
# parte de lo que impide escribir basura.
#
# Visto rojo rompiendo cada mitad del arreglo por separado.
out/cwaprende: sim/cw_aprende.c $(FIRMWARE)/User/cw.c $(FIRMWARE)/User/cw.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ \
	      sim/cw_aprende.c $(FIRMWARE)/User/cw.c $(LDLIBS)

.PHONY: cwaprende
cwaprende: out/cwaprende
	./out/cwaprende sim/muestras/cw4xz_4330b.f32 sim/muestras/cw4xz_4330.f32

comprueba: cwaprende

# ---------------------------------------------------------------------
# LA BUSQUEDA AUTOMATICA DE VELOCIDAD Y ENTRELAZADO DEL 4285 - 08/10/2026
#
# Del usuario lo pidio. Once
# combinaciones que no van en la señal y habia que acertarlas a mano.
# Este banco exige que la radio las recorra sola, acierte la que lleva la
# grabacion del aire, saque la frase, no tarde mas de 30 s y no se lo
# trague con ruido. Y mide el MARGEN, que es lo que dice si acerto o tuvo
# suerte.
out/stanagbusca: sim/stanag_busca.c $(FIRMWARE)/User/stanag_rx.c \
                 $(FIRMWARE)/User/hfdl_viterbi.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: stanagbusca
stanagbusca: out/stanagbusca out/muestras/stanag4481_psk.wav
	./out/stanagbusca out/muestras/stanag4481_psk.wav

comprueba: stanagbusca

# ---------------------------------------------------------------------
# LA FASE DE BYTE DEL 4285 - 08/10/2026
#
# El fallo gordo: el Viterbi suelta bits y el demodulador los partia en
# bytes empezando por donde cayera. Una de ocho. Con las once
# combinaciones de arriba son OCHENTA Y OCHO maneras de mirar un 4285 de
# las que UNA daba texto.
#
# No se puede ver con una grabacion -la unica que hay acierta de
# casualidad-, asi que este banco trae un TRANSMISOR (sim/stanag_tx.h):
# genera la forma de onda, corre la fase a proposito y exige la frase en
# los ocho desfases de las once combinaciones, con marcas SOM/EOM y sin
# ellas. Visto rojo con el arreglo quitado: 11 de 88.
out/stanagfase: sim/stanag_fase.c sim/stanag_tx.h $(FIRMWARE)/User/stanag_rx.c \
                $(FIRMWARE)/User/hfdl_viterbi.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: stanagfase
stanagfase: out/stanagfase ; ./out/stanagfase

comprueba: stanagfase

# ---------------------------------------------------------------------
# EL BURBUJEO DEL REDUCTOR DE RUIDO (07/10/2026, de un aviso recibido).
# Ver la cabecera de sim/nr_burbujeo.c: NO
# demuestra que el burbujeo exista -no se ha podido reproducir ni en el banco
# ni en el video de referencia, y alli esta escrito por que- pero deja
# fijado lo que el reductor hace HOY, con tope, para que ningun cambio futuro
# lo empeore sin que salte. Y ensena donde esta la ventana util del mando,
# que es un 7 % de su recorrido y eso si es un defecto medido.
out/nrburbujeo: sim/nr_burbujeo.c $(FIRMWARE)/User/nr_ss.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -o $@ $(FUENTES) $(LDLIBS)

.PHONY: nrburbujeo
nrburbujeo: out/nrburbujeo ; ./out/nrburbujeo

comprueba: nrburbujeo

# ---------------------------------------------------------------------
# EL ORDEN DE ARRANQUE: TEMA -> PALETA DE LA CASCADA (07/10/2026, de un
# issue). Ver tools/orden_arranque.py: un comentario que decia "no hay
# dependencia de orden" era cierto cuando se escribio y dejo de serlo dos
# semanas despues. Un comentario no comprueba nada; esto si.
.PHONY: ordenarranque
ordenarranque:
	@python3 $(FIRMWARE)/tools/orden_arranque.py $(FIRMWARE)

comprueba: ordenarranque

# ---------------------------------------------------------------------
# STANAG 4415 / MIL-STD-188-110A A 75 bps. Ver la cabecera de
# sim/s4415test.c y, sobre todo, docs/STANAG_4415_MEDIDO.md.
#
# El banco hace cuatro cosas y la cuarta es la que lo convierte en banco:
# genera una emision entera con un texto conocido dentro y comprueba que sale
# igual; la rompe (fase de muestreo a medio simbolo, portadora corrida, 6 dB
# de ruido) y comprueba que sigue saliendo; la pasa por la grabacion del
# usuario; y la pasa por RUIDO BLANCO y por Robser-s.mp3, donde NO puede
# enganchar. Sin esa cuarta, lo unico que se prueba es que el detector dice
# que si.
# El 4415 contra una grabacion. NO entra en comprueba: necesita fichero.
# Ver la cabecera de sim/s4415_aire.c; de aqui salio el numero que cerro la
# duda del orden de los dibits (85% de ceros en la grabacion, y el EOM que
# aparece con un orden y no con el otro).
#   make s4415aire FIRMWARE=... && ./out/s4415aire x.f32
out/s4415aire: sim/s4415_aire.c $(FIRMWARE)/User/s4415.c \
               $(FIRMWARE)/User/hfdl_viterbi.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $^ $(LDLIBS)

.PHONY: s4415aire
s4415aire: out/s4415aire

out/s4415test: sim/s4415test.c $(FIRMWARE)/User/s4415.c \
               $(FIRMWARE)/User/hfdl_viterbi.c $(FIRMWARE)/User/ecualiz.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $^ $(LDLIBS)

.PHONY: s4415
s4415: out/s4415test out/muestras/stanag4415.wav out/muestras/stanag_robser.wav
	./out/s4415test out/muestras/stanag4415.wav out/muestras/stanag_robser.wav

comprueba: s4415

# ---------------------------------------------------------------------
# EL 4285 DE VERDAD, ROTO A PROPOSITO CON MULTITRAYECTO. Ver la cabecera de
# sim/stanag_multi.c: es la vara de medir con la que habra que comparar
# cualquier ecualizador que se meta en el camino del tono serie.
out/stanagmulti: sim/stanag_multi.c $(FIRMWARE)/User/stanag_rx.c \
                 $(FIRMWARE)/User/hfdl_viterbi.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -Ihostinc -Isim -o $@ $^ $(LDLIBS)

.PHONY: stanagmulti
stanagmulti: out/stanagmulti out/muestras/stanag4481_psk.wav
	./out/stanagmulti out/muestras/stanag4481_psk.wav

comprueba: stanagmulti

# ---------------------------------------------------------------------
# EL FAT32 DEL PINCHO. Ver la cabecera de sim/fat32test.c.
#
# Las imagenes las hace el mkfs.fat del sistema y las rellena mtools, o sea
# codigo que no es nuestro: el banco no puede "aprobarse a si mismo". Son
# dispersas -32 GB aparentes ocupan 17 MB de verdad- asi que tenerlas en
# out/ no cuesta nada.
#
# La de particiones es la que importa de las cuatro: un pincho de fabrica
# viene particionado y su sector de arranque NO esta en el sector 0. Sin
# ella, el codigo pasaria el banco entero y fallaria con el primer pincho
# que se enchufe.
# ---------------------------------------------------------------------
FAT32_IMGS = out/fat32_1G.img out/fat32_8G.img out/fat32_32G.img \
             out/fat32_part.img

out/fat32_pl/.hecho:
	@mkdir -p out/fat32_pl
	@python3 -c "open('out/fat32_pl/HOLA.TXT','wb').write(b'deepsdr fat32\n')"
	@python3 -c "open('out/fat32_pl/MEDIO.BIN','wb').write(bytes((i*7+3)&0xFF for i in range(100000)))"
	@python3 -c "open('out/fat32_pl/GRANDE.BIN','wb').write(bytes((i*13+5)&0xFF for i in range(3*1024*1024)))"
	@python3 -c "open('out/fat32_pl/nombre_largo_de_verdad.txt','wb').write(b'LFN\n')"
	@touch $@

out/fat32_1G.img out/fat32_8G.img out/fat32_32G.img: out/fat32_pl/.hecho
	@rm -f $@
	truncate -s $(patsubst out/fat32_%.img,%,$@) $@
	mkfs.fat -F 32 -n PINCHO $@ >/dev/null
	mcopy -i $@ out/fat32_pl/HOLA.TXT out/fat32_pl/MEDIO.BIN \
	      out/fat32_pl/GRANDE.BIN out/fat32_pl/nombre_largo_de_verdad.txt ::/

out/fat32_part.img: out/fat32_pl/.hecho
	@rm -f $@
	truncate -s 4G $@
	python3 tools/mbr_fat32.py $@ 2048
	mkfs.fat -F 32 -n CONPART --offset 2048 $@ >/dev/null
	mcopy -i $@@@1M out/fat32_pl/HOLA.TXT out/fat32_pl/MEDIO.BIN \
	      out/fat32_pl/GRANDE.BIN out/fat32_pl/nombre_largo_de_verdad.txt ::/

out/fat32test: sim/fat32test.c $(FIRMWARE)/User/fat32.c $(FIRMWARE)/User/fat32.h
	$(CC) $(CFLAGS) -D_FILE_OFFSET_BITS=64 -I$(FIRMWARE)/User -o $@ \
	      sim/fat32test.c $(FIRMWARE)/User/fat32.c

.PHONY: fat32
fat32: out/fat32test $(FAT32_IMGS) ; ./out/fat32test out

comprueba: fat32

# ---------------------------------------------------------------------
# EL CAT DE KENWOOD. Ver la cabecera de sim/cattest.c.
#
# La mitad que importa de este banco le lanza el `rigctl` de hamlib DE
# VERDAD por encima, contra un puerto serie de mentira con el cat.c del
# firmware al otro lado. Si hamlib no esta instalado, el banco corre igual
# -la mitad manual sigue midiendo- y dice que le falta la otra, pero no
# tumba la compilacion: no todo el mundo tiene hamlib.
# ---------------------------------------------------------------------
out/cattest: sim/cattest.c $(FIRMWARE)/User/cat.c $(FIRMWARE)/User/cat.h
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/cattest.c $(FIRMWARE)/User/cat.c

.PHONY: cat
cat: out/cattest
	@if ! command -v rigctl >/dev/null 2>&1; then \
	   echo "  OJO: no hay rigctl (hamlib). Solo corre la mitad manual."; \
	 fi
	./out/cattest

comprueba: cat

# ---------------------------------------------------------------------
# LOS DESCRIPTORES DEL PUERTO SERIE USB. Ver la cabecera de sim/cdctest.c.
#
# Compila el usb_cdc_desc.c del firmware tal cual -no lleva ni un include
# del GD32, a proposito- y lo recorre como lo recorre un anfitrion.
# ---------------------------------------------------------------------
out/cdctest: sim/cdctest.c $(FIRMWARE)/User/usb_cdc_desc.c \
             $(FIRMWARE)/User/usb_cdc_desc.h
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/cdctest.c \
	      $(FIRMWARE)/User/usb_cdc_desc.c

.PHONY: cdc
cdc: out/cdctest ; ./out/cdctest

comprueba: cdc

# ---------------------------------------------------------------------
# EL ANILLO ENTRE LA INTERRUPCION DEL USB Y EL BUCLE. Ver sim/anillotest.c.
#
# Lleva dos hilos de verdad pasandose dos millones de bytes, asi que
# necesita -lpthread y tarda un par de segundos. Visto fallar: quitando el
# hueco que se desperdicia, un anillo lleno se lee como vacio y se pierden
# 917.000 bytes de los dos millones.
# ---------------------------------------------------------------------
out/anillotest: sim/anillotest.c $(FIRMWARE)/User/usb_cdc_anillo.c \
                $(FIRMWARE)/User/usb_cdc_anillo.h
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/anillotest.c \
	      $(FIRMWARE)/User/usb_cdc_anillo.c -lpthread

.PHONY: anillo
anillo: out/anillotest ; ./out/anillotest

comprueba: anillo

# ---------------------------------------------------------------------
# LA MEMORIA PRESTADA DE HFDL CONTRA LOS OCHO GIROS - 10/10/2026.
#
# Nace del "X7" que salio en 13.312: siete rafagas de 66 que ni se
# abren. Lo que pide cada giro estaba en un comentario a mano desde el
# 28/09; esto lo mide llamando a begin_segment_ex() de verdad, con la
# capacidad sacada de sizeof(ft8_shared_ram_t). Ver sim/hfdl_memoria.c.
# ---------------------------------------------------------------------
out/hfdlmem: sim/hfdl_memoria.c $(FIRMWARE)/User/hfdl_payload_decode.c \
             $(FIRMWARE)/User/hfdl_deinterleaver.c \
             $(FIRMWARE)/User/hfdl_descrambler.c \
             $(FIRMWARE)/User/hfdl_viterbi.c \
             $(FIRMWARE)/User/hfdl_soft_demod.c \
             $(FIRMWARE)/User/hfdl_pdu.c $(FIRMWARE)/User/hfdl_crc.c \
             $(FIRMWARE)/User/ft8_shared_ram.h $(FIRMWARE)/User/hfdl_ram.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib -o $@ \
	    sim/hfdl_memoria.c $(FIRMWARE)/User/hfdl_payload_decode.c \
	    $(FIRMWARE)/User/hfdl_deinterleaver.c \
	    $(FIRMWARE)/User/hfdl_descrambler.c \
	    $(FIRMWARE)/User/hfdl_viterbi.c \
	    $(FIRMWARE)/User/hfdl_soft_demod.c \
	    $(FIRMWARE)/User/hfdl_pdu.c $(FIRMWARE)/User/hfdl_crc.c $(LDLIBS)

.PHONY: hfdlmem
hfdlmem: out/hfdlmem ; ./out/hfdlmem

comprueba: hfdlmem

# ---------------------------------------------------------------------
# EL SOLAPE DE LA MEMORIA PRESTADA DE HFDL - 10/10/2026.
# Mide la PREMISA del cerrojo de hfdl_modo.c (s_remate): que la salida del
# Viterbi cae encima de la tabla del desentrelazador. Ver sim/hfdl_solape.c.
# ---------------------------------------------------------------------
out/hfdlsolape: sim/hfdl_solape.c $(FIRMWARE)/User/hfdl_payload_decode.c \
                $(FIRMWARE)/User/hfdl_deinterleaver.c \
                $(FIRMWARE)/User/hfdl_descrambler.c \
                $(FIRMWARE)/User/hfdl_viterbi.c \
                $(FIRMWARE)/User/hfdl_soft_demod.c \
                $(FIRMWARE)/User/hfdl_pdu.c $(FIRMWARE)/User/hfdl_crc.c \
                $(FIRMWARE)/User/ft8_shared_ram.h $(FIRMWARE)/User/hfdl_ram.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -I$(FIRMWARE)/Ft8Lib -o $@ \
	    sim/hfdl_solape.c $(FIRMWARE)/User/hfdl_payload_decode.c \
	    $(FIRMWARE)/User/hfdl_deinterleaver.c \
	    $(FIRMWARE)/User/hfdl_descrambler.c \
	    $(FIRMWARE)/User/hfdl_viterbi.c \
	    $(FIRMWARE)/User/hfdl_soft_demod.c \
	    $(FIRMWARE)/User/hfdl_pdu.c $(FIRMWARE)/User/hfdl_crc.c $(LDLIBS)

.PHONY: hfdlsolape
hfdlsolape: out/hfdlsolape ; ./out/hfdlsolape

comprueba: hfdlsolape

# ---------------------------------------------------------------------
# LA PASTILLA DE BANDA AL CAMBIAR DE ANCHO - 11/10/2026.
# Nace de un aviso del usuario: en SSTV de 20 el boton de arriba se cortaba.
# Mira PIXELES, no cuentas. Ver sim/bandacorte.c.
# ---------------------------------------------------------------------
out/bandacorte: sim/bandacorte.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_top.c \
                gfx2/fonts_data.c gfx2/idioma.c $(FIRMWARE)/User/rds_pi.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/bandacorte.c sim/lcd_sim.c \
	    gfx2/gfx2.c gfx2/ui_top.c gfx2/fonts_data.c \
	    $(FIRMWARE)/User/rds_pi.c $(LDLIBS)

.PHONY: bandacorte
bandacorte: out/bandacorte ; ./out/bandacorte

comprueba: bandacorte

# ---------------------------------------------------------------------
# EL DEDO QUE ARRASTRA (11/10/2026)
#
# Pedido por el usuario.
#
# Compila User/touch.c DEL FIRMWARE. El filtro que decide si una lectura
# del panel vale -y que estaba contestando "no hay dedo" en mitad de un
# arrastre- vive ahi dentro en variables estaticas: un banco que se lo
# reimplemente mide su propia copia y sale verde pase lo que pase en la
# radio. Como el XPT2046 se habla bit a bit por GPIO, el banco hace de chip
# y le dicta el dedo que quiera.
out/tactilarr: sim/tactil_arrastre.c $(FIRMWARE)/User/touch.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I hostinc -I $(FIRMWARE)/User -o $@ $^ $(LDLIBS)

.PHONY: tactilarr
tactilarr: out/tactilarr ; ./out/tactilarr

comprueba: tactilarr

# ---------------------------------------------------------------------
# LA CONTINUA ENCIMA DE LA PORTADORA, EN WFM (11/10/2026)
#
# Pedido por el usuario.
#
# Mete muestras crudas de 16 bits I/Q por demod_wfm_process_raw() -la
# entrada de verdad, la misma que el codec- y lee por donde sale el audio
# de verdad. Mide las DOS caras: que un offset ya no se carga una emisora
# debil, y que el tope impide que el arreglo se coma una portadora fuerte
# poco modulada, que es el caso que casi se rompe arreglando el primero.
out/wfmdc: sim/wfm_dc.c sim/demod_stubs.c $(FIRMWARE)/User/demod_am.c \
           $(DEMOD_REALES)
	@mkdir -p out
	$(CC) $(CFLAGS) -D_GNU_SOURCE -I $(FIRMWARE)/User -o $@ $(FUENTES) $(LDLIBS)

.PHONY: wfmdc
wfmdc: out/wfmdc ; ./out/wfmdc

comprueba: wfmdc

# ---------------------------------------------------------------------
# LA REDUCCION DE RUIDO EN SAM (11/10/2026)
#
# *** De un issue: la reduccion de ruido no funcionaba en SAM y se
# pedia que lo hiciera. ***
#
# Y era peor que "no funciona": el boton de Ruido no esta limitado por
# modo, asi que en SAM se encendia, ponia "activo" en la barra, y
# demod_am.c no entraba en el bloque.
#
# Mete una portadora de AM modulada con ruido por demod_am_process_raw()
# -la entrada de verdad- y mide el ruido que queda, NO el nivel: el AGC
# devuelve el nivel, asi que una atenuacion uniforme no cuenta como
# reduccion. Y lo mide en los dos modos: AM es el testigo.
out/samnr: sim/sam_nr.c sim/demod_stubs.c $(FIRMWARE)/User/demod_am.c \
           $(DEMOD_REALES)
	@mkdir -p out
	$(CC) $(CFLAGS) -D_GNU_SOURCE -I $(FIRMWARE)/User -o $@ $(FUENTES) $(LDLIBS)

.PHONY: samnr
samnr: out/samnr ; ./out/samnr

comprueba: samnr

# ---------------------------------------------------------------------
# SAM CON UNA SOLA BANDA (11/10/2026)
#
# *** De un issue: se pedia que el detector sincrono de AM pudiera trabajar sobre una
# sola banda lateral, para escapar de una interferencia. ***
#
# El estimulo lleva UN TONO DISTINTO EN CADA BANDA, y eso no es un detalle:
# con un AM normal -el mismo tono arriba y abajo- quedarse con una banda
# suena igual que quedarse con las dos, asi que el banco diria "bien" con
# el codigo roto. Un estimulo que no distingue no mide.
out/sambanda: sim/sam_banda.c sim/demod_stubs.c $(FIRMWARE)/User/demod_am.c \
              $(DEMOD_REALES)
	@mkdir -p out
	$(CC) $(CFLAGS) -D_GNU_SOURCE -I $(FIRMWARE)/User -o $@ $(FUENTES) $(LDLIBS)

.PHONY: sambanda
sambanda: out/sambanda ; ./out/sambanda

comprueba: sambanda

# ---------------------------------------------------------------------
# EL MARGEN DE ENGANCHE DEL SAM (12/10/2026)
#
# *** De un probador: "if there is strong interference nearby and the
# received station is weak, the synchronous detector seems to lock onto the
# interference. Most likely, the capture range is too wide". ***
#
# El estimulo no es "una intrusa al lado": es la emisora buena
# DESVANECIENDOSE con la intrusa delante y volviendo despues. Con las dos a
# la vez el batido va mas deprisa que el lazo y el lazo lo promedia, asi que
# un banco que las ponga juntas y ya diria "bien" con el fallo dentro. Lo
# que rompe un PLL es quedarse a solas con la intrusa, y lo que se mira es
# donde se queda cuando la buena vuelve.
out/samcaptura: sim/sam_captura.c sim/demod_stubs.c $(FIRMWARE)/User/demod_am.c \
                $(DEMOD_REALES)
	@mkdir -p out
	$(CC) $(CFLAGS) -D_GNU_SOURCE -I $(FIRMWARE)/User -o $@ $(FUENTES) $(LDLIBS)

.PHONY: samcaptura
samcaptura: out/samcaptura ; ./out/samcaptura

comprueba: samcaptura

# ---------------------------------------------------------------------
# LO QUE NO SE PUBLICA, Y QUE SE NOTE
# ---------------------------------------------------------------------
#
# Pedido por el usuario, 13/10/2026.
#
# Dos carpetas de este simulador NO van al repositorio:
#
#   sim/muestras/   veinte grabaciones de aire -STANAG, NAVTEX, CW, RTTY,
#                   ALS162-. Algunas las grabo el usuario y otras se las
#                   paso gente o salen de un WebSDR. Redistribuir la
#                   grabacion de otro no es nuestro para decidirlo.
#
#   vendor/         gfx.c y spectrum.c del firmware ORIGINAL de la radio,
#                   guardados para comparar contra ellos. Es codigo del
#                   fabricante, no nuestro, y sin licencia que invocar.
#
# NINGUNA de las dos hace falta para "make comprueba": los diez bancos que
# compilan vendor/ y los que leen grabaciones se lanzan a mano y no cuelgan
# de la tanda. Medido, no supuesto.
#
# PERO QUE NO FALLE EN JEROGLIFICO. Sin esto, quien clone el repositorio y
# pruebe "make puntas" se come un "No rule to make target 'vendor/gfx.c'",
# que suena a simulador roto cuando lo que pasa es que ese fichero no se
# publica. Un error que miente sobre su causa cuesta una tarde.

#
# Y LOS DIECIOCHO QUE LO NECESITAN, QUE SE SALTEN SOLOS - 16/10/2026.
#
# *** Aqui ponia, y el README y el .gitignore lo repetian: "Ninguno cuelga
# de 'make comprueba': esa tanda pasa entera sin esta carpeta. Medido, no
# supuesto". Medido hoy otra vez, moviendo vendor/ de sitio y lanzando la
# tanda: MUERE. Exit 2, en el primero de los dieciocho. ***
#
# O sea que lo que leia quien clonaba el repositorio era un mensaje de
# error que le mandaba hacer exactamente lo que acababa de fallar. Y este
# fichero avisa, doce lineas mas arriba, de que "un error que miente sobre
# su causa cuesta una tarde".
#
# La lista sale de recorrer el grafo de prerequisitos de "comprueba", no
# de acordarse. Si manana un banco nuevo usa vendor/ y no se añade aqui,
# la tanda vuelve a morir en un clon - por eso tools/vendor_check.py la
# vuelve a calcular y compara.
#
ifeq ($(wildcard vendor/gfx.c),)
vendor/%:
	@echo ""
	@echo "  FALTA $@, y el simulador NO esta roto."
	@echo ""
	@echo "  vendor/ son gfx.c y spectrum.c del firmware ORIGINAL de la"
	@echo "  radio, que este repositorio no distribuye porque no son suyos."
	@echo "  Los bancos que los usan comparan el dibujado nuevo contra el"
	@echo "  viejo, asi que sin ellos no hay contra que comparar."
	@echo ""
	@echo "  Veinte objetivos de 'make comprueba' los usan, y la tanda"
	@echo "  los salta sola cuando esta carpeta no esta: los otros 205 se"
	@echo "  corren igual. Si has llegado aqui es porque has pedido este"
	@echo "  banco por su nombre."
	@echo ""
	@false
endif

ifeq ($(wildcard sim/muestras),)
sim/muestras/%:
	@echo ""
	@echo "  FALTA $@, y el simulador NO esta roto."
	@echo ""
	@echo "  sim/muestras/ son grabaciones de aire que este repositorio no"
	@echo "  distribuye: parte no son nuestras. Este banco mide contra una"
	@echo "  senal de verdad, asi que necesita su fichero."
	@echo ""
	@echo "  Graba la tuya -el nombre y el formato estan en la cabecera del"
	@echo "  banco- o lanza 'make comprueba', que no la necesita."
	@echo ""
	@false
endif

# --- LO QUE COSTARIA CENTRAR EL ESPECTRO A 1X - 13/10/2026 --------------
#
# Dos usuarios preguntaron por que la marca de sintonia no se mueve por la
# mitad izquierda con el NCO puesto. La respuesta es geometria -la marca vive
# entre 6 y 40 kHz POR ENCIMA del oscilador, y cruzarlo metería su fuga en el
# filtro de canal-, y a zoom >1X ya sale centrada porque zoom_process_block()
# rota antes de diezmar. Esto mide si esa misma rotacion cabria tambien a 1X.
#
# Se mide CONTRA fft_compute_db_iq(), que corre en el mismo camino y ya cabe,
# no contra un numero inventado: esto es un PC y lo unico que se transporta al
# Cortex-M4 es la razon entre las dos.
out/espcentrado: sim/espectro_centrado.c $(FIRMWARE)/User/nco.c \
                 $(FIRMWARE)/User/nco_tabla.c $(FIRMWARE)/User/fft.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ sim/espectro_centrado.c \
	      $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c \
	      $(FIRMWARE)/User/fft.c -lm

espcentrado: out/espcentrado ; ./out/espcentrado

comprueba: espcentrado

# --- EL SNR DE FT8, CONTRA UNA SEÑAL DE SNR CONOCIDO - 13/10/2026 ------
#
# De un issue: el numero que ponia la radio no era un SNR, era la puntuacion
# de sincronismo por 0,5. Esto mide el calculo nuevo (User/ft8_snr.c) contra
# una emision fabricada con el SNR que se quiere, pasada por el ADAPTADOR DE
# VERDAD del firmware. Barre siete niveles y exige pendiente 1, porque un
# solo punto no distingue un error de escala de uno de desplazamiento.
out/ft8snr: sim/ft8_snr.c $(FIRMWARE)/User/ft8_snr.c \
            $(FIRMWARE)/User/ft8_waterfall_adapter.c \
            $(FIRMWARE)/User/ft8_fft1024.c $(FIRMWARE)/User/ft8_tablas.c \
            sim/ipa_sim.c sim/lcd_sim.c vendor/gfx.c gfx2/gfx2.c \
            gfx2/fonts_data.c $(FIRMWARE)/User/waterfall.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib -o $@ $(FUENTES) $(LDLIBS)

.PHONY: ft8snr
ft8snr: out/ft8snr ; ./out/ft8snr

ifneq ($(wildcard vendor/gfx.c),)
comprueba: ft8snr
endif

# --- LO QUE COSTARIA RELLENAR LA TIERRA - 13/10/2026 ------------------
#
# Pedido por el usuario. El color ya esta -MAPA_COL_TIERRA, el
# hueco 1 de los quince de cada .map- y no lo lee nadie. Esto mide lo que
# costaria el relleno por lineas de barrido sobre LA GEOMETRIA DE VERDAD,
# capturada interceptando gfx2_line() en un fotograma.
out/mapareller: sim/mapa_relleno.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
                $(FIRMWARE)/User/mapa.c $(FIRMWARE)/User/mapa_datos.c \
                $(FIRMWARE)/User/mapa_bin.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) \
	    -Wl,--wrap=gfx2_line $(LDLIBS)

.PHONY: mapareller
mapareller: out/mapareller ; ./out/mapareller

comprueba: mapareller

# --- Y LO QUE COSTARIA RELLENAR AL ACERCARSE - 14/10/2026 --------------
#
# El relleno se ve al zoom 1 y no mas alla. Antes de escribir un dibujante
# nuevo, los numeros, igual que se hizo con mapareller.
out/mapatroc: sim/mapa_troceado.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
              $(FIRMWARE)/User/mapa.c $(FIRMWARE)/User/mapa_datos.c \
              $(FIRMWARE)/User/mapa_bin.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) \
	    -Wl,--wrap=gfx2_line $(LDLIBS)

.PHONY: mapatroc
mapatroc: out/mapatroc ; ./out/mapatroc

comprueba: mapatroc

# --- DE QUIEN ES EL MANDO CON EL MAPA PUESTO - 15/10/2026 --------------
#
# Por un informe: con el mapa en pantalla los controles dejaban de
# responder. Ver User/mando_mapa.h.
out/mandomapa: sim/mando_mapa_test.c $(FIRMWARE)/User/mando_mapa.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^)

.PHONY: mandomapa
mandomapa: out/mandomapa ; ./out/mandomapa

comprueba: mandomapa

# --- LOS NOMBRES DE LAS EMISORAS SOBRE EL ESPECTRO - 15/10/2026 --------
out/emisrango: sim/emisoras_rango.c $(FIRMWARE)/User/emisoras.c \
               $(FIRMWARE)/User/hora_comun.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: emisrango
emisrango: out/emisrango ; ./out/emisrango datos/EMISORAS.BIN

comprueba: emisrango

# ---------------------------------------------------------------------
# LO QUE CUESTA EL CARTEL DE LA CABECERA (20/10/2026).
#
# De un pico de 167 ms medido en antena: el tramo culpable era el de antes
# del dibujo, y dentro de el la sospecha era la base de emisoras, que son
# 206 kB al otro lado de un bus de patadas. Esto lo mide -104..420 bytes
# por cartel, tres decimas de milisegundo- y de paso se queda de guardian:
# si alguien vuelve a recorrer la tabla entera por ahi, se pone rojo.
out/emiscartel: sim/emis_cartel.c $(FIRMWARE)/User/emisoras.c \
                $(FIRMWARE)/User/hora_comun.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: emiscartel
emiscartel: out/emiscartel ; ./out/emiscartel datos/EMISORAS.BIN

comprueba: emiscartel

out/rotulos: sim/rotulos.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
             $(FIRMWARE)/User/spec_rotulos.c $(FIRMWARE)/User/spec_rotulos.h
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: rotulos
rotulos: out/rotulos ; ./out/rotulos

comprueba: rotulos

# --- Y QUIEN SALE DE LOS SIETE, CON LA BASE DE VERDAD - 15/10/2026 -----
#
# out/rotulos mide el DIBUJO con listas a mano y estaba verde mientras la
# radio enseñaba cuatro carteles amontonados a la izquierda. El fallo
# estaba en la ELECCION, que vivia dentro de main.c y aqui no se enlaza.
# Hoy la eleccion es spec_rotulos_filtra() y esto la mide con el
# EMISORAS.BIN de verdad. Ver sim/rotulos_elige.c.
out/rotulos_elige: sim/rotulos_elige.c sim/lcd_sim.c gfx2/gfx2.c \
                   gfx2/fonts_data.c $(FIRMWARE)/User/spec_rotulos.c \
                   $(FIRMWARE)/User/emisoras.c $(FIRMWARE)/User/hora_comun.c \
                   $(FIRMWARE)/User/spec_rotulos.h sim/temas_tabla.h
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: rotelige
rotelige: out/rotulos_elige ; ./out/rotulos_elige datos/EMISORAS.BIN

comprueba: rotelige

# --- Y QUE EL MANDO SALTE DE EMISORA EN EMISORA - 15/10/2026 ----------
#
# El modo JMP: cada detente lleva a la siguiente emisora EN ANTENA en vez
# de mover un paso de sintonia. Esto mide emisoras_salto() contra el
# EMISORAS.BIN de verdad, barriendo las 24 horas y toda la HF, y exige
# ida y vuelta exacta. Ver sim/salto.c.
out/salto: sim/salto.c $(FIRMWARE)/User/emisoras.c \
           $(FIRMWARE)/User/hora_comun.c $(FIRMWARE)/User/emisoras.h
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: salto
salto: out/salto ; ./out/salto datos/EMISORAS.BIN

comprueba: salto

# --- Y QUE LA TRAZA Y LOS CARTELES SEAN UN SOLO DIBUJO - 15/10/2026 ----
#
# Compila el User/spectrum.c DE VERDAD, le pone el gancho y mira lo que
# sale en el framebuffer: que las letras llegan, y que el sombreado del
# paso de banda no sube hasta el techo. Ver sim/rotulos_banda.c.
out/rotulos_banda: sim/rotulos_banda.c sim/lcd_sim.c vendor/gfx.c \
                   $(FIRMWARE)/User/spectrum.c $(FIRMWARE)/User/spec_rotulos.c \
                   gfx2/gfx2.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -I vendor \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: rotbanda
rotbanda: out/rotulos_banda ; ./out/rotulos_banda

# --- Y LO QUE CUESTA PINTAR UN SALTO - 15/10/2026 ---------------------
#
# Con JMP puesto el usuario nota la cascada lenta, y con su flash rapida las
# cuentas de lecturas no lo explican. Esto mide el REPINTADO: cuantas
# bandas se salta spectrum_draw() con un paso de 1 kHz y cuantas con un
# salto de emisora. Ver sim/salto_pinta.c.
out/salto_pinta: sim/salto_pinta.c sim/lcd_sim.c vendor/gfx.c \
                 $(FIRMWARE)/User/spectrum.c gfx2/gfx2.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I gfx2 -I $(FIRMWARE)/User -I vendor \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: saltopinta
saltopinta: out/salto_pinta ; ./out/salto_pinta

ifneq ($(wildcard vendor/gfx.c),)
comprueba: saltopinta
endif

ifneq ($(wildcard vendor/gfx.c),)
comprueba: rotbanda
endif

# --- LOS ANILLOS DE TIERRA DEL EMPAQUETADOR - 14/10/2026 ---------------
#
# mapa_pack.py marca ahora los trazos cerrados cuyo interior es tierra, para
# que la radio pueda rellenarla. Esto NO comprueba las estructuras de Python
# que lo generaron -ahi es donde se esconden los errores que el propio
# generador no ve- sino los .map YA ESCRITOS: que los anillos cierren, que
# ninguna frontera vaya marcada, que el estilo del nombre y el bit digan lo
# mismo, y que lo que encierran sea tierra y no el mundo entero.
#
# Se empaqueta en un directorio temporal a proposito: el banco no toca
# mapas/ del usuario.
.PHONY: anillos
anillos:
	@rm -rf out/mapas_tmp && mkdir -p out/mapas_tmp
	python3 $(FIRMWARE)/tools/mapa_pack.py $(FIRMWARE)/tools/datos/mundo.bin \
	        $(FIRMWARE)/tools/datos/tipos.json --salida out/mapas_tmp
	@n=0; for f in out/mapas_tmp/*.map; do \
	   python3 $(FIRMWARE)/tools/mapa_anillos_check.py $$f >/dev/null || n=$$((n+1)); \
	 done; \
	 echo "  los 100 mapas: $$n fallos"; \
	 test $$n -eq 0
	@python3 $(FIRMWARE)/tools/mapa_anillos_check.py out/mapas_tmp/015.map | tail -9
	@echo "  y uno de contorno, que NO debe traer anillos:"
	@python3 $(FIRMWARE)/tools/mapa_anillos_check.py out/mapas_tmp/014.map | tail -4

comprueba: anillos

# --- QUE LA TIERRA SE RELLENE - 14/10/2026 ----------------------------
#
# sim/mapa_relleno.c midio lo que COSTARIA antes de escribir firmware. Esto
# mide que el dibujante lo haga: pinta con User/mapa.c de verdad y cuenta
# los pixeles. Necesita los .map que fabrica el objetivo `anillos`.
out/mapatierra: sim/mapa_tierra.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
                $(FIRMWARE)/User/mapa.c $(FIRMWARE)/User/mapa_datos.c \
                $(FIRMWARE)/User/mapa_bin.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: mapatierra
mapatierra: out/mapatierra anillos ; ./out/mapatierra

comprueba: mapatierra

# --- Y QUE SE RELLENE TAMBIEN AL ACERCARSE - 15/10/2026 ---------------
#
# mapatierra mide el zoom 1, donde el par-impar arranca solo porque la fila
# empieza en el borde del mundo. Al acercarse hace falta la PARIDAD DE
# PARTIDA, que ahora viene en el .map. Esto la mide contra ocho sitios de
# un atlas y, sobre todo, comprueba que pasear la ventana no le cambie el
# color a un punto: ese es el fallo propio de sembrar. Ver
# sim/mapa_tierra_zoom.c.
#
# ESTE BANCO MIDE EL CAMINO VIEJO, Y HAY QUE DECIRLO - 16/10/2026.
#
# Lo que prueba es la SIEMBRA por tabla de paridad, que es como se
# rellenaba al acercarse hasta hoy. Desde que cada casilla trae sus
# propios anillos cerrados (ver User/mapa.c y tools/mapa_pack.py), un
# .map recien hecho ya no trae tabla y la radio ni la mira.
#
# Se queda, y no es por nostalgia: los .map que ya estan grabados en las
# radios SI la traen, y la radio tiene que seguir pintandolos. Este banco
# es lo unico que vigila ese camino. Usa out/mapas_tmp, que genera con el
# empaquetador de antes.
# Y SUS MAPAS SON LOS DE ANTES, generados a proposito con --viejo: es el
# formato que este banco mide y el que llevan las radios que ya estan por
# ahi. Carpeta propia para no mezclarlos con out/mapas_tmp, que son los de
# hoy.
.PHONY: mapasviejos
mapasviejos:
	@rm -rf out/mapas_viejos && mkdir -p out/mapas_viejos
	@python3 $(FIRMWARE)/tools/mapa_pack.py $(FIRMWARE)/tools/datos/mundo.bin \
	        $(FIRMWARE)/tools/datos/tipos.json --viejo --solo 15 \
	        --salida out/mapas_viejos >/dev/null
	@echo "  out/mapas_viejos/015.map: el formato de antes del 16/10/2026"

out/mapatierrazoom: sim/mapa_tierra_zoom.c sim/lcd_sim.c gfx2/gfx2.c \
                    gfx2/fonts_data.c \
                    $(FIRMWARE)/User/mapa.c $(FIRMWARE)/User/mapa_datos.c \
                    $(FIRMWARE)/User/mapa_bin.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: mapatierrazoom
mapatierrazoom: out/mapatierrazoom mapasviejos ; ./out/mapatierrazoom

comprueba: mapatierrazoom

# --- EL CATALOGO DE LOS CIEN MAPAS - 14/10/2026 -----------------------
#
# DeepSDR_mapas_100.pdf. NO entra en `comprueba`: tarda minutos, escribe un
# PDF de cuatro megas y necesita Pillow con JPEG. Es un objetivo que se
# lanza a mano cuando cambian los mapas o el dibujante.
#
# Lo importante es de donde sale el dibujo: de User/mapa.c, el mismo codigo
# que corre en la radio, y NO de un imitador. El catalogo anterior se pinto
# aparte y acabo prometiendo cosas que la radio no hacia.
#
# Usa mapas/ del usuario a proposito -es el catalogo de SUS mapas-, al reves
# que el objetivo `anillos`, que se fabrica los suyos en out/mapas_tmp.
out/mapacatalogo: sim/mapa_catalogo.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
                  $(FIRMWARE)/User/mapa.c $(FIRMWARE)/User/mapa_datos.c \
                  $(FIRMWARE)/User/mapa_bin.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: catalogo
catalogo: out/mapacatalogo
	@rm -rf out/catalogo && mkdir -p out/catalogo
	./out/mapacatalogo $(FIRMWARE)/mapas out/catalogo
	python3 $(FIRMWARE)/tools/mapa_catalogo.py out/catalogo \
	        $(FIRMWARE)/tools/datos/tipos.json \
	        $(FIRMWARE)/DeepSDR_mapas_100.pdf

# --- QUE UN .map NUEVO NO ROMPA UN FIRMWARE VIEJO - 14/10/2026 ---------
#
# El banco que faltaba. Los anillos de tierra se metieron primero en el bit
# 14 de la cuenta de puntos "porque un .map viejo no lo trae"; la direccion
# que nadie miro es la contraria, y ahi 51 de los cien mapas se rompian en
# silencio en cualquier radio sin V4.79. Ver la cabecera de sim/mapa_compat.c.
#
# Recorre cada .map con el lector de verdad y con una COPIA del lector de
# version 2, y exige que vean los mismos trazos y los mismos puntos. La
# copia es deliberada: representa un firmware ya instalado que no se va a
# actualizar solo, asi que no se toca cuando cambie mapa_bin.c.
out/mapacompat: sim/mapa_compat.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
                $(FIRMWARE)/User/mapa.c $(FIRMWARE)/User/mapa_datos.c \
                $(FIRMWARE)/User/mapa_bin.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: mapacompat
mapacompat: out/mapacompat anillos ; ./out/mapacompat out/mapas_tmp/*.map

comprueba: mapacompat

# --- EL MISMO MAPA POR LAS DOS PUERTAS - 14/10/2026 --------------------
#
# Pedido por el usuario.
#
# Un mapa se carga de dos maneras: mapa_bin_pon() con el fichero entero en
# RAM -la que usan TODOS los demas bancos- y mapa_bin_abre_flash() a
# trozos, que es la que usa LA RADIO. El relleno se entrego leyendo el byte
# del estilo solo en la primera, asi que la radio no rellenaba nunca y los
# cien mapas salian verdes en cinco bancos.
#
# Esto carga el mismo fichero por las dos y compara el dibujo PIXEL A
# PIXEL, que es lo unico que caza un campo que se olvide en una de ellas
# aunque a nadie se le ocurra ese campo.
out/mapados: sim/mapa_dospuertas.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
             $(FIRMWARE)/User/mapa.c $(FIRMWARE)/User/mapa_datos.c \
             $(FIRMWARE)/User/mapa_bin.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: mapados
mapados: out/mapados anillos ; ./out/mapados out/mapas_tmp/*.map

comprueba: mapados

# --- LAS RAYAS AL ARRASTRAR EL MAPA - 14/10/2026 -----------------------
#
# Pedido por el usuario.
#
# El relleno descarta entera una fila que pase de MAPA_CRUCES_MAX cruces, y
# yo elegi 40 midiendo UNA vista -el mundo centrado- y escribi al lado que
# estaba medido. Al arrastrar, la misma fila cae sobre costas mucho mas
# densas. Esto barre el mundo en las dos escalas que rellenan y cuenta las
# filas que se quedan sin pintar SOBRE LOS PIXELES, que es lo que se ve.
# DOS BINARIOS: el del firmware y uno de referencia con sitio de sobra,
# donde por construccion no desborda ninguna fila. Lo que se exige es que
# pinten EL MISMO PIXEL. Comparar dos renders y no contar rayas sobre los
# pixeles: contarlas dio un barrido NO MONOTONO -42 cruces mejor que 44-
# porque dos filas caidas pegadas no las veia el detector. Ver la cabecera
# de sim/mapa_rayas.c.
#
# La referencia va a 128 y no mas arriba: s_ncruce[] es uint8_t, asi que
# un tope de 256 no se alcanza nunca y el barrido escribe fuera. Tambien
# medido, tambien a base de un resultado imposible.
MAPARAYAS_SRC = sim/mapa_rayas.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c \
                $(FIRMWARE)/User/mapa.c $(FIRMWARE)/User/mapa_datos.c \
                $(FIRMWARE)/User/mapa_bin.c

out/maparayas: $(MAPARAYAS_SRC)
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

out/maparayas_ref: $(MAPARAYAS_SRC)
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -I$(FIRMWARE)/User -DMAPA_CRUCES_MAX=128 \
	      -o $@ $(filter %.c,$^) $(LDLIBS)

# UN SOLO OBJETIVO Y DOS MEDIDAS - 16/10/2026, Y ESTO SE ROMPIO.
#
# El 16/10 se añadio a sim/mapa_rayas.c el CONTEO de rayas sobre los .map
# del disco, y con el una segunda regla `maparayas` mas arriba en este
# fichero. En make los prerequisitos se acumulan pero las RECETAS no: la
# segunda descarta la primera con un warning, y lo que corria era solo una
# de las dos. Peor: la que corria llamaba al binario con dos argumentos
# que el main nuevo ignoraba, asi que out/rayas.bin NO SE REESCRIBIA y el
# `cmp` de abajo comparaba dos ficheros de hacia cuatro horas. Decia "ok"
# sin medir nada.
#
# Es la segunda vez en el mismo dia que una receta repetida apaga un banco
# en silencio. Por eso ahora hay UN solo `maparayas`, y hace las dos:
# primero el conteo -sin argumentos- y luego los dos volcados que se
# comparan pixel a pixel.
#
# Los .bin se borran antes de cada pasada. Un fichero viejo que sobrevive
# a su generador es justo lo que convirtio esto en un aprobado falso.
.PHONY: maparayas
maparayas: out/maparayas out/maparayas_ref
	@./out/maparayas
	@rm -f out/rayas.bin out/rayas_ref.bin
	@./out/maparayas     $(FIRMWARE)/mapas/033.map out/rayas.bin
	@./out/maparayas_ref $(FIRMWARE)/mapas/033.map out/rayas_ref.bin >/dev/null
	@if cmp -s out/rayas.bin out/rayas_ref.bin; then \
	   echo "  el tope del firmware pinta igual que la referencia   ok"; \
	 else \
	   echo "  *** el tope del firmware CORTA: hay filas sin rellenar ***"; \
	   cmp -l out/rayas.bin out/rayas_ref.bin | wc -l; \
	   exit 1; \
	 fi

comprueba: maparayas


# --- ACARS DENTRO DE HFDL - 14/10/2026 --------------------------------
#
# *** Un probador: "hasn't decoded a single set of coordinates... with
# other firmware, logs coordinates for practically every transmission". ***
#
# La respuesta salio de los BYTES, no del razonamiento: el volcado en
# pantalla enseño que las tramas que no daban posicion son de tipo 0xFF,
# o sea ACARS, y que el tipo va en el byte 1 y no en el 10 como decia un
# comentario mio de hace un mes.
#
# LO QUE HACE ESTE BANCO DISTINTO DE TODOS LOS DE HFDL QUE HABIA: tiene
# DOS TRAMAS DE VERDAD dentro, copiadas de la pantalla de la radio del
# usuario en 17.967 kHz. El decodificador de posicion que habia se verifico
# contra una trama que nadie guardo, y de aquel recuerdo salio un mes de
# diseño equivocado.
out/acars: sim/hfdl_acars.c $(FIRMWARE)/User/hfdl_acars.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: acars
acars: out/acars ; ./out/acars

comprueba: acars

# --- EL DETECTOR DE FASE DE M-PSK - 14/10/2026 ------------------------
#
# Pedido por el usuario.
#
# Cero tramas de 4-PSK y 8-PSK habian pasado el CRC nunca, y la causa
# llevaba semanas escrita en hfdl_costas.c: la correccion de fase de cada
# simbolo suponia BPSK siempre.
#
# El banco mide LAS DOS cosas que hay que creerse: que con BPSK el camino
# nuevo es identico bit a bit -si no, se estaria arriesgando el 39 % que
# funciona para arreglar un 0 %- y que con 8-PSK el viejo NO converge y el
# nuevo si. Sin la primera mitad, la segunda no prueba nada.
out/fase: sim/hfdl_fase.c $(FIRMWARE)/User/hfdl_costas.c \
          $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c
	@mkdir -p out
	$(CC) $(CFLAGS) -I$(FIRMWARE)/User -o $@ $(filter %.c,$^) $(LDLIBS)

.PHONY: fase
fase: out/fase ; ./out/fase

comprueba: fase

# --- CUANTO GIRO AGUANTA EL DECODIFICADOR - 14/10/2026 ----------------
#
# *** Medido en la radio toda una tarde: CERO tramas de 4-PSK y 8-PSK
# decodificadas, 0 de 25. ***
#
# La autoprueba que ya existia demuestra que el decodificador va bien a las
# cuatro velocidades con simbolos limpios. Asi que el fallo esta ANTES. Esto
# mide lo concreto: cuanto giro de constelacion aguanta. Si medio paso lo
# tumba, el giro absoluto explica el cero de 25 por si solo.
#
# Se enlaza igual que hfdlpruebas -con todo hfdl_*.c- porque el
# decodificador arrastra media cadena, y con el mismo hfdl_ram de mentira:
# el de verdad trae waterfall.h detras. Ver out/hfdlpruebas.
out/giro: sim/hfdl_giro.c sim/rtc_sim.c sim/ipa_sim.c sim/lcd_sim.c vendor/gfx.c \
          gfx2/gfx2.c gfx2/fonts_data.c $(FIRMWARE)/User/waterfall.c \
          $(wildcard $(FIRMWARE)/User/hfdl_*.c) \
          $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c \
          $(FIRMWARE)/User/icao24_db.c $(FIRMWARE)/User/zona_alta.c sim/spi_flash_vacio.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib -o $@ $(FUENTES) $(LDLIBS)

.PHONY: giro
giro: out/giro ; ./out/giro

ifneq ($(wildcard vendor/gfx.c),)
comprueba: giro
endif

# --- LA RAFAGA ENTERA, HASTA EL CRC - 14/10/2026 ----------------------
#
# Pedido por el usuario.
#
# El banco que faltaba, y su ausencia es la causa de todo lo de hoy: hay
# veintitantos bancos de HFDL y todos prueban PIEZAS. Ninguno cogia una
# rafaga, la metia por la cadena y miraba si salia el CRC. Con esa
# coleccion se pueden entregar tres arreglos seguidos, todos en verde, sin
# mover el numero de la radio ni una decima.
# --- LA ROTACION M1: ¿SE ELIGE LA BUENA? - 14/10/2026 ----------------
#
# *** "es fisicamente imposible que en 45 minutos solo tenga dos
# paquetes position" - y la pantalla: BPSK 14/157, 4PSK 0/15, 8PSK 0/6. ***
#
# 157 de 178 rafagas dicen ser BPSK. Eso no lo decide el aire: lo decide
# la rotacion M1 que elige hfdl_preamble_sync.c. Ver sim/hfdl_m1.c.
out/m1: sim/hfdl_m1.c $(FIRMWARE)/User/hfdl_preamble_seq.c \
        $(FIRMWARE)/User/hfdl_preamble_sync.c $(FIRMWARE)/User/hfdl_framer.c \
        $(FIRMWARE)/User/hfdl_ram.c sim/waterfall_prestamo.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib -o $@ $^ -lm

.PHONY: m1
m1: out/m1 ; ./out/m1

comprueba: m1

# --- EL AIRE DE VERDAD - 14/10/2026 -----------------------------------
#
# Pedido por el usuario.
#
# Grabaciones reales de HFDL metidas por la cadena del firmware. Ver
# sim/hfdl_aire.c: es el unico banco del proyecto cuya señal no la ha
# fabricado el mismo que escribio el decodificador.
# --- 4-PSK BLANDA - 14/10/2026 ----------------------------------------
out/psk4: sim/hfdl_psk4.c $(FIRMWARE)/User/hfdl_soft_demod.c \
          $(FIRMWARE)/User/hfdl_preamble_seq.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib -o $@ $^ -lm

.PHONY: psk4
psk4: out/psk4 ; ./out/psk4

comprueba: psk4

# LAS CABECERAS TAMBIEN - 14/10/2026. Hoy se barrio el umbral de A1 de
# 0,36 a 0,20 y el banco dio OCHO VECES el mismo resultado exacto: make
# no reconstruia porque hfdl_preamble_seq.h no estaba aqui. Un banco que
# no recompila no mide nada, y encima contesta deprisa y convencido.
out/aire: sim/hfdl_aire.c sim/rtc_sim.c sim/ipa_sim.c sim/lcd_sim.c vendor/gfx.c \
          gfx2/gfx2.c gfx2/fonts_data.c $(FIRMWARE)/User/waterfall.c \
          $(wildcard $(FIRMWARE)/User/hfdl_*.h) \
          $(wildcard $(FIRMWARE)/User/hfdl_*.c) \
          $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c \
          $(FIRMWARE)/User/icao24_db.c $(FIRMWARE)/User/zona_alta.c sim/spi_flash_vacio.c
	@mkdir -p out
	$(CC) -O2 -Wall -DHFDL_DIAG=1 -I hostinc -I sim -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib -o $@ $(filter %.c,$^) gfx2/idioma.c -lm

.PHONY: aire
aire: out/aire ; ./out/aire

ifneq ($(wildcard vendor/gfx.c),)
comprueba: aire
endif

out/extremo: sim/hfdl_extremo.c sim/rtc_sim.c sim/ipa_sim.c sim/lcd_sim.c vendor/gfx.c \
             gfx2/gfx2.c gfx2/fonts_data.c $(FIRMWARE)/User/waterfall.c \
             $(wildcard $(FIRMWARE)/User/hfdl_*.c) \
             $(FIRMWARE)/User/nco.c $(FIRMWARE)/User/nco_tabla.c \
             $(FIRMWARE)/User/icao24_db.c $(FIRMWARE)/User/zona_alta.c sim/spi_flash_vacio.c
	@mkdir -p out
	$(CC) -O2 -Wall -I hostinc -I sim -I $(FIRMWARE)/User -I $(FIRMWARE)/Ft8Lib -o $@ $(FUENTES) $(LDLIBS)

.PHONY: extremo
extremo: out/extremo ; ./out/extremo

ifneq ($(wildcard vendor/gfx.c),)
comprueba: extremo
endif
