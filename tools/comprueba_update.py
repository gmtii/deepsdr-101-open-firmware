#!/usr/bin/env python3
"""
comprueba_update.py - mira un update.bin como lo mira el cargador.
14/10/2026.

POR QUE EXISTE
--------------

Lo que paso, sin adornos: habia que entregar un binario, hice `make` y
copie build/firmware.bin encima de update.bin a mano. firmware.bin es la
imagen PELADA. update.bin es esa imagen CON CABECERA, y la cabecera la
pone `make update` llamando a tools/cabecera.py. O sea que entregue un
fichero que el cargador no puede arrancar, y la radio dijo exactamente eso:
"sin cabecera: ahi no hay imagen".

El fallo no fue de la compilacion. Fue mio por saltarme el target y hacer
el ultimo paso a mano. Y lo peor es que no lo comprobe: dije "entregado"
sobre un fichero que no habia mirado.

LA REGLA, para que esto no dependa de que yo me acuerde:

  1. El binario se genera SIEMPRE con `make update`. Nunca copiando
     build/firmware.bin a ningun sitio. Nunca.
  2. Antes de decir "entregado", se pasa este comprobador por los BYTES
     QUE HAN LLEGADO AL DISCO DEL DUEÑO, no por los que hay aqui.

El punto 2 no es paranoia: el puente devuelve a veces una copia cacheada,
asi que leer el fichero de vuelta y compararlo es la unica forma de saber
que lo que hay en su disco es lo que yo creo.

QUE COMPRUEBA, y por que cada cosa
----------------------------------
  a) la cabecera en +0x200: magia "DSDR", longitud y CRC-32/MPEG-2 con el
     propio campo del CRC contado como cero. Es literalmente lo que hace
     User/cabecera_app.c. Si esto falla, la radio no arranca.

  b) la tabla de vectores del ARM, que el CRC NO cubre en el sentido util:
     un fichero puede tener la cabecera perfecta y aun asi no ser una
     imagen -por ejemplo si alguien pego la cabecera sobre datos que no
     son codigo-. Las dos primeras palabras de una imagen Cortex-M son el
     puntero de pila inicial y el vector de reset, y los dos tienen que
     caer donde hay memoria de verdad. Es barato y caza justo el caso que
     el CRC se come.

  c) que NO sea build/firmware.bin disfrazado: si en +0x200 no hay magia,
     se dice con todas las letras cual fue el error, porque es el que ya
     costo una tarde.

Uso:
    python3 tools/comprueba_update.py update.bin
    python3 tools/comprueba_update.py E:/update.bin      (el del USB)

Devuelve 0 si la radio lo va a aceptar y 1 si no, asi que sirve igual
dentro de un Makefile que a mano.
"""
import struct
import sys

MAGIA = 0x52445344          # "DSDR"
OFF_CABECERA = 0x200

# El mapa del GD32F450VET6, de GD32F450VE_FLASH.ld y de tools/cabecera.py.
#
# OJO CON LA PILA, que me costo un susto el 14/10/2026: en este chip la
# pila NO vive en la SRAM principal sino en la CCM, en 0x10000000, y el
# puntero inicial sale 0x10010000 (el tope de sus 64 kB). La primera
# version de este comprobador solo aceptaba 0x2000xxxx y daba por malo un
# binario perfecto. Un comprobador que da falsos positivos es peor que no
# tenerlo: manda a buscar el fallo donde no esta.
FLASH_APP_INI, FLASH_FIN = 0x0800C000, 0x08080000   # la region de la app
SRAM_INI,  SRAM_FIN  = 0x20000000, 0x20030000       # 192 kB principales
CCM_INI,   CCM_FIN   = 0x10000000, 0x10010000       # 64 kB, donde va la pila


def crc32_mpeg2(datos):
    """Polinomio 0x04C11DB7, arranca en 0xFFFFFFFF, sin reflejar y sin xor
    final. NO es el de zlib. Se calcula bit a bit, que es la definicion,
    para no heredar un fallo de la tabla que usa el firmware: asi los dos
    lados son implementaciones independientes. Ver tools/cabecera.py."""
    c = 0xFFFFFFFF
    for b in datos:
        c ^= b << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if c & 0x80000000 else (c << 1) & 0xFFFFFFFF
    return c


def comprueba(ruta):
    fallos = []
    with open(ruta, 'rb') as f:
        d = f.read()

    print('  fichero    %s' % ruta)
    print('  tamaño     %d bytes' % len(d))

    if len(d) < OFF_CABECERA + 16:
        print('  *** demasiado corto para tener cabecera siquiera')
        return False

    magia, longitud, crc, version = struct.unpack('<IIII', d[OFF_CABECERA:OFF_CABECERA + 16])

    if magia != MAGIA:
        print('  magia      %08X  <- TENIA QUE SER %08X ("DSDR")' % (magia, MAGIA))
        print()
        print('  *** ESTO NO ES UN update.bin. ***')
        print('  Lo mas probable, porque ya paso el 14/10/2026: es')
        print('  build/firmware.bin copiado a mano. La imagen pelada no')
        print('  lleva cabecera y el cargador la rechaza diciendo')
        print('  "sin cabecera: ahi no hay imagen".')
        print('  Se genera con  make update  y con nada mas.')
        return False
    print('  magia      %08X  ok' % magia)

    print('  version    %d.%d' % (version >> 16, version & 0xFFFF))

    if longitud != len(d):
        print('  longitud   %d  <- el fichero mide %d' % (longitud, len(d)))
        fallos.append('la longitud de la cabecera no cuadra con el fichero')
    else:
        print('  longitud   %d  ok' % longitud)

    cuerpo = bytearray(d[:longitud])
    cuerpo[OFF_CABECERA + 8:OFF_CABECERA + 12] = b'\x00\x00\x00\x00'
    calc = crc32_mpeg2(bytes(cuerpo))
    if calc != crc:
        print('  crc32      %08X  <- calculado %08X' % (crc, calc))
        fallos.append('el CRC no cuadra: la imagen esta incompleta o tocada')
    else:
        print('  crc32      %08X  ok' % crc)

    sp, reset = struct.unpack('<II', d[0:8])
    en_ram = (SRAM_INI <= sp <= SRAM_FIN) or (CCM_INI <= sp <= CCM_FIN)
    if not en_ram:
        print('  pila       %08X  <- ni en SRAM ni en CCM' % sp)
        fallos.append('el puntero de pila inicial no apunta a RAM')
    else:
        print('  pila       %08X  ok (%s)' % (sp, 'CCM' if sp >= CCM_INI and sp <= CCM_FIN else 'SRAM'))
    if not (FLASH_APP_INI <= (reset & ~1) < FLASH_FIN):
        print('  reset      %08X  <- fuera de la region de la app (%08X..%08X)'
              % (reset, FLASH_APP_INI, FLASH_FIN))
        fallos.append('el vector de reset no apunta a la region de la aplicacion')
    else:
        print('  reset      %08X  ok' % reset)

    print()
    if fallos:
        print('  *** LA RADIO VA A RECHAZAR ESTE FICHERO ***')
        for f in fallos:
            print('      - %s' % f)
        return False
    print('  La radio lo va a aceptar.')
    return True


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    sys.exit(0 if comprueba(sys.argv[1]) else 1)
