# Simulador de host del DEEPSDR 101.
#
#   make before   render de la UI actual, compilando el User/gfx.c del repo
#   make after    render del rediseno con el nucleo gfx2
#   make smoke    diagnostico de coste de bus y muestra de la fuente actual
#   make guard    comprueba que la banda reservada de arriba no deja pasar nada
#   make spec3b   render del espectro de la etapa 3b + comprobacion de anchos
#   make principal render de la pantalla principal completa
#   make bandas   render de la pantalla de bandas + comprobacion de la lista
#   make ajustes  render de ajustes y del detalle + comprobacion de anchos
#   make autoesc  autoescala del espectro: extremos frente a percentiles
#   make cw       decodificador de CW: barrido de velocidad y de senal-ruido
#   make modos    comprueba la tabla de modos de main.c
#   make bandas   comprueba los limites de banda contra su texto
#   make paletas  dibuja las 16 paletas con los mismos datos, para elegir
#   make comprueba todas las comprobaciones de geometria, sin renders
#   make armcheck compila gfx2 + assets para el GD32F450 y mide flash/RAM
#   make assets   regenera fuentes y colormap desde las TTF
#
# No hace falta la placa para nada de esto.

# Raiz del repositorio del firmware, de donde se lee la tabla de bandas.
FIRMWARE ?= ..

CC       = gcc
CFLAGS   = -O2 -Wall -I hostinc -I sim
VENDOR   = -I vendor
GFX2     = -I gfx2
# gfx2/idioma.c va en TODOS los enlaces, no en las reglas que lo necesitan.
#
# Por que: desde la traduccion (29/09/2026) medio firmware llama a tr(), y
# un fichero que hoy no lo llama puede llamarlo manana sin avisar. Ponerlo
# regla por regla significa que cada vez que alguien mete un tr() en un
# fichero ya copiado, el banco de ese fichero deja de enlazar - y el fallo
# no se parece en nada a su causa ("undefined reference to `tr'" en un
# banco del reloj). Cuesta unos kilobytes de binario de anfitrion que no
# se van a notar. Es una copia, asi que copiaschk la vigila.
LDLIBS   = gfx2/idioma.c -lm
ARMCC    = arm-none-eabi-gcc
ARMFLAGS = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
           -O2 -Wall -ffunction-sections -fdata-sections -I hostinc -I gfx2

.PHONY: all before after smoke guard spec3b principal bandas bandaschk ajustes cw autoesc modos persist csvlen comprueba armcheck assets png clean
all: before after

out/before: sim/before.c sim/lcd_sim.c vendor/gfx.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(VENDOR) -o $@ $^ $(LDLIBS)

out/after: sim/after.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

out/smoke: sim/smoke.c sim/lcd_sim.c vendor/gfx.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(VENDOR) -o $@ $^ $(LDLIBS)

out/guardtest: sim/guardtest.c sim/lcd_sim.c vendor/gfx.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(VENDOR) -o $@ $^ $(LDLIBS)

out/spec3b: sim/spec3b.c sim/lcd_sim.c vendor/gfx.c vendor/spectrum.c gfx2/gfx2.c gfx2/spec_chrome.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) $(VENDOR) -o $@ $^ $(LDLIBS)

out/spec3blayout: sim/spec3b_layout.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

out/rejilla: sim/rejilla.c
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

out/snaptest: sim/snaptest.c gfx2/spec_snap.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

out/cwbw: sim/cwbw.c
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

out/digi: sim/digi.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_digi.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

out/kbd: sim/kbd.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_kbd.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

out/freqtap: sim/freqtap.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_top.c gfx2/fonts_data.c sim/bandas_tabla.h
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

out/paletachk: sim/paletachk.c sim/lcd_sim.c vendor/gfx.c vendor/spectrum.c gfx2/gfx2.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) $(VENDOR) -o $@ $^ $(LDLIBS)

out/paletas: sim/paletas.c sim/lcd_sim.c vendor/gfx.c vendor/spectrum.c gfx2/gfx2.c gfx2/spec_chrome.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) $(VENDOR) -o $@ $^ $(LDLIBS)

out/chromecost: sim/chromecost.c sim/lcd_sim.c vendor/gfx.c vendor/spectrum.c gfx2/gfx2.c gfx2/spec_chrome.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) $(VENDOR) -o $@ $^ $(LDLIBS)

out/principal: sim/principal.c sim/lcd_sim.c vendor/gfx.c vendor/spectrum.c gfx2/gfx2.c gfx2/spec_chrome.c gfx2/ui_top.c gfx2/ui_act.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) $(VENDOR) -o $@ $^ $(LDLIBS)

out/acttest: sim/acttest.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_act.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

out/layout: sim/layout.c sim/lcd_sim.c gfx2/gfx2.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

# Y DEPENDE DE main.c, no solo del generador - 30/09/2026.
#
# Le faltaba `$(FIRMWARE)/User/main.c` (las otras cinco tablas generadas si
# lo llevan). O sea que cambiar la tabla de bandas en main.c NO regeneraba
# esto, y out/bandas y out/freqtap seguian midiendo la lista vieja, en
# silencio. Se noto porque el .py llevaba meses sin tocarse: la foto que
# habia era de 46 bandas y la tabla tenia 49.
sim/bandas_tabla.h: tools/bandas_dump.py $(FIRMWARE)/User/main.c
	python3 tools/bandas_dump.py $(FIRMWARE)/User/main.c $@

out/bandas: sim/bandas2.c sim/bandas_tabla.h sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_cfg.c gfx2/ui_grid.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $(filter %.c,$^) $(LDLIBS)

out/ajustes: sim/ajustes.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_grid.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

out/detalle: sim/detalle.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_det.c gfx2/ui_grid.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

out/autoesc: sim/autoesc.c gfx2/spec_agc.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

out/cwtest: sim/cwtest.c gfx2/cw.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

out/config: sim/config.c sim/lcd_sim.c gfx2/gfx2.c gfx2/ui_cfg.c gfx2/ui_grid.c gfx2/fonts_data.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(GFX2) -o $@ $^ $(LDLIBS)

before: out/before ; ./out/before && $(MAKE) png
after:  out/after  ; ./out/after  && $(MAKE) png
smoke:  out/smoke  ; ./out/smoke  && $(MAKE) png
guard:  out/guardtest ; ./out/guardtest
spec3b: out/spec3b out/spec3blayout ; ./out/spec3blayout && ./out/spec3b 0 && $(MAKE) png
ajustes: out/config out/detalle ; ./out/config 0 && ./out/detalle 0 && $(MAKE) png
bandas: out/bandas ; ./out/bandas 0 && $(MAKE) png
principal: out/principal out/layout out/acttest ; ./out/layout && ./out/acttest && ./out/principal && $(MAKE) png
cw: out/cwtest ; ./out/cwtest
autoesc: out/autoesc ; ./out/autoesc
modos: ; python3 tools/modos_check.py $(FIRMWARE)/User/main.c
bandaschk: ; python3 tools/bandas_check.py $(FIRMWARE)/User/main.c
comprueba: out/guardtest out/layout out/acttest out/spec3blayout out/bandas out/config out/cwtest out/autoesc out/paletachk out/freqtap out/kbd out/digi out/snaptest out/rejilla ; ./out/guardtest && ./out/layout && ./out/acttest && ./out/spec3blayout && ./out/bandas 0 && ./out/config 0 && ./out/cwtest && ./out/autoesc && ./out/paletachk && ./out/freqtap && ./out/kbd 0 && ./out/kbd 1 && ./out/digi && ./out/snaptest && ./out/rejilla && $(MAKE) modos && $(MAKE) bandaschk

png:
	@python3 -c "from PIL import Image; import glob,os; \
[Image.open(p).save(p[:-4]+'.png') for p in glob.glob('out/*.ppm')]"

assets:
	python3 tools/gen_assets.py

armcheck:
	@mkdir -p armcheck
	$(ARMCC) -c $(ARMFLAGS) armcheck/armmain.c -o armcheck/armmain.o
	$(ARMCC) -c $(ARMFLAGS) gfx2/gfx2.c        -o armcheck/gfx2.o
	$(ARMCC) -c $(ARMFLAGS) armcheck/stubs.c   -o armcheck/stubs.o
	arm-none-eabi-size -t armcheck/*.o

clean:
	rm -rf out armcheck/*.o

# Comprobaciones y reglas extra, en un fichero aparte (23/09/2026). El "-"
# de "-include" significa "si no esta, no pasa nada". Esta separado porque
# el puente con el que se escriben los ficheros de este proyecto no puede
# escribir ficheros llamados Makefile -son codigo que se ejecuta- pero si un
# .mk: asi anadir un banco nuevo no obliga a renombrar nada a mano cada vez.
#
# Un objetivo puede tener varias reglas mientras solo UNA lleve receta, asi
# que local.mk puede colgarle requisitos nuevos a "comprueba" sin tener que
# volver a escribir su receta entera.
-include local.mk
