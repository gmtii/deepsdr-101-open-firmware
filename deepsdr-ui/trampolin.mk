#
# EL TRAMPOLIN DEL SIMULADOR.
# ==========================================
#
# Pedido por el usuario, 09/10/2026, y otra vez el 16/10/2026 porque aqui no
# se habia hecho.
#
# El metodo se monto entonces en la raiz y en boot/, y AQUI NO: este
# directorio se quedo con un Makefile a secas, que es de los nombres que el
# puente que escribe en su disco rechaza -"is a protected file and cannot be
# written via remote tools"-. Asi que cada vez que habia que tocar una regla
# de aqui volviamos al chat a que la pegara a mano, que es justo lo que el
# metodo venia a quitar.
#
# GNU make busca su fichero en este orden:  GNUmakefile, makefile, Makefile.
#
# O sea que GNUmakefile gana y Makefile pasa a leerse solo desde el.
#
# Y GNUmakefile TAMBIEN ESTA PROTEGIDO - 16/10/2026, medido pidiendole al
# puente que lo escribiera y leyendo su negativa: "is a protected file and
# cannot be written via remote tools". O sea que el truco de cambiarle el
# nombre no quitaba la piedra, solo la movia un metro: el fichero seguia
# teniendo que pegarlo a mano.
#
# Por eso GNUmakefile se queda en UNA LINEA que no va a cambiar nunca mas
# -include trampolin.mk- y todo lo de verdad vive aqui, que si se puede
# escribir. Es el mismo reparto que la raiz lleva con reglas.mk.
#
#
# Y LO PRIMERO QUE RESUELVE: vendor/ EN UN CLON - 16/10/2026.
#
# vendor/ son gfx.c y spectrum.c del firmware ORIGINAL de la radio y no se
# publican. Veinte objetivos de "make comprueba" los usan. Dieciocho se
# saltan desde local.mk; los otros dos -out/guardtest y out/paletachk- estan
# en la linea "comprueba:" de Makefile.
#
# En make no se puede QUITAR un prerequisito: se acumulan. Lo que si se puede
# es leer otro makefile. Asi que cuando vendor/ no esta -o sea, en cualquier
# clon del repositorio- se lee una copia de Makefile con esos dos quitados,
# generada aqui con sed. Make rehace los makefiles que incluye antes de nada
# y luego se relee solo; es su comportamiento normal, no un truco.
#
# Con vendor/ delante no se genera nada y se lee Makefile tal cual.
#
# MEDIDO, no supuesto: con vendor/, 175 bancos y salida 0. Sin vendor/, 158
# bancos y salida 0; antes de esto, salida 2.
#
ifeq ($(wildcard vendor/gfx.c),)

out/Makefile.sinvendor: Makefile
	@mkdir -p out
	@sed -e 's| out/guardtest||g' \
	     -e 's| out/paletachk||g' \
	     -e 's|\./out/guardtest && ||g' \
	     -e 's|\./out/paletachk && ||g' $< > $@
	@echo "  (sin vendor/: me salto out/guardtest y out/paletachk)"

include out/Makefile.sinvendor

else
include Makefile
endif
