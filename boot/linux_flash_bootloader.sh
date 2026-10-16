#!/bin/sh
#
# Graba el cargador de arranque por DFU, desde Linux y sin abrir la radio.
#
#   apt install dfu-util
#
# Para entrar en DFU: enciende la radio con el mando pulsado y girandolo,
# o, si ya llevas un firmware de esta rama, Ajustes -> Equipo ->
# Informacion -> "Modo DFU" (en la pagina oculta, cinco toques sobre la
# fila Version). La pantalla se queda quieta y sin audio: es lo normal, y
# se deshace apagando y encendiendo.
#
# 28e9:0189 es el DFU que el propio GD32F450 trae de fabrica en ROM.
#
# NOTA 13/10/2026: esto grababa "boot_nuestro.bin", que es como se llamaba
# el cargador antes de la V3.00. Ese nombre lleva meses sin existir -hoy es
# customboot.bin- asi que este guion llevaba todo ese tiempo fallando para
# cualquiera que lo usara desde Linux, que era justo para quien se escribio.

dfu-util -d 28e9:0189 -a 0 -s 0x08000000:leave -D customboot.bin
