#!/usr/bin/env python3
"""
EL CONTRATO DE FT8 CON LA MEMORIA QUE LE PRESTA LA CASCADA - 25/09/2026.

sim/cascada_prestada.c comprueba que el CERROJO funciona: mientras el buffer
esta marcado como prestado, la cascada no toca ni un byte. Esto comprueba la
otra mitad, que es la que el banco en C no puede ver: que ft8_modo.c LO
MARQUE, y que lo desmarque al salir.

Las dos mitades tienen que cumplirse para que el cerrojo sirva de algo. Un
cerrojo perfecto que nadie echa no protege nada.

Y hay un orden que importa, tambien comprobado aqui: dentro de
ft8_modo_start() la peticion tiene que ir ANTES de las llamadas que escriben
en la union (ft8_decimator_reset, ft8_waterfall_reset, ft8_decoder_init).
Marcarlo despues deja una rendija -corta, pero real- en la que el bucle
principal puede colarse a pintar sobre datos que ya son de FT8.

SEGUNDA PARTE, anadida el mismo dia despues de que la radio se colgara:
que ft8_modo_start() llame a TODOS los arranques de FT8.

Faltaba ft8_fft1024_init(). Sus cuatro tablas viven dentro de la memoria
prestada, o sea que se borran cada vez que se sale de FT8 y hay que
rehacerlas en cada arranque. Sin rehacer, la tabla de bits invertidos -que se
usa como INDICE- contiene pixeles de cascada, y leer con un indice de hasta
65535 en una tabla de 1024 se sale de la SRAM: la radio se paro con un pitido
continuo y hubo que apagarla por el interruptor.

El compilador no puede avisar de esto: el fallo esta en la llamada que NO
esta, y eso no se ve leyendo el codigo que si esta. Aqui se ve, y la lista no
se escribe a mano: sale de las propias cabeceras de FT8, asi que el dia que
alguien anada un ft8_loquesea_init() esta comprobacion lo pedira sola.
"""
import glob
import os
import re
import sys


def cuerpo(src, nombre):
    """El texto entre la llave que abre la funcion y la que la cierra en la
    columna 0. Vale porque este fichero -como todo el arbol- cierra sus
    funciones pegadas al margen."""
    m = re.search(r'\n[A-Za-z_][^\n]*\b' + nombre + r'\s*\([^)]*\)\s*\n?\{(.*?)\n\}',
                  src, re.S)
    return m.group(1) if m else None


def main(ruta):
    src = open(ruta, encoding='utf-8').read()
    fallos = 0

    esperado = [
        ('ft8_modo_start', '1U', 'pide'),
        ('ft8_modo_stop',  '0U', 'devuelve'),
    ]

    for fn, arg, verbo in esperado:
        c = cuerpo(src, fn)
        if c is None:
            print('  FALLA: no se encuentra %s()' % fn)
            fallos += 1
            continue
        if not re.search(r'waterfall_presta\s*\(\s*' + arg + r'\s*\)', c):
            print('  FALLA: %s() no %s el buffer '
                  '(falta waterfall_presta(%s))' % (fn, verbo, arg))
            fallos += 1
        else:
            print('    %-16s %s el buffer' % (fn + '()', verbo))

    # El orden dentro de start(): primero pedirlo, luego escribir.
    c = cuerpo(src, 'ft8_modo_start')
    if c:
        m = re.search(r'waterfall_presta\s*\(\s*1U\s*\)', c)
        escritores = ['ft8_decimator_reset', 'ft8_waterfall_reset',
                      'ft8_decoder_init']
        for e in escritores:
            w = re.search(re.escape(e) + r'\s*\(', c)
            if m and w and w.start() < m.start():
                print('  FALLA: %s() escribe en la union ANTES de pedir '
                      'el buffer' % e)
                fallos += 1
        if m:
            print('    %-16s pide el buffer antes de escribir en el'
                  % 'ft8_modo_start()')

    # Y al devolverlo hay que borrarlo: lo que queda dentro son magnitudes.
    c = cuerpo(src, 'ft8_modo_stop')
    if c and not re.search(r'waterfall_init\s*\(\s*\)', c):
        print('  FALLA: ft8_modo_stop() devuelve el buffer sin borrarlo '
              '(falta waterfall_init())')
        fallos += 1
    elif c:
        print('    %-16s lo borra al devolverlo' % 'ft8_modo_stop()')

    fallos += arranques(src, os.path.dirname(ruta))

    print('  TODO CORRECTO' if not fallos else '  %d FALLOS' % fallos)
    return 1 if fallos else 0


def arranques(src, dircab):
    """Todos los ft8_*_init()/ft8_*_reset() que declaran las cabeceras de
    FT8 tienen que llamarse desde ft8_modo_start(). La lista sale de las
    cabeceras y no de aqui, que es lo que hace que no se quede vieja."""
    c = cuerpo(src, 'ft8_modo_start')
    if c is None:
        print('  FALLA: no se encuentra ft8_modo_start()')
        return 1

    quiere = set()
    for h in sorted(glob.glob(os.path.join(dircab, 'ft8_*.h'))):
        if os.path.basename(h) in ('ft8_modo.h', 'ft8_shared_ram.h'):
            continue   # el envoltorio y la union no arrancan nada
        texto = open(h, encoding='utf-8').read()
        for m in re.finditer(r'\bvoid\s+(ft8_\w+_(?:init|reset))\s*\(\s*void\s*\)',
                             texto):
            quiere.add(m.group(1))

    if not quiere:
        print('  FALLA: no he encontrado ningun ft8_*_init() en las cabeceras '
              '(¿han cambiado de sitio? esta comprobacion se ha quedado ciega)')
        return 1

    fallos = 0
    for fn in sorted(quiere):
        if re.search(r'\b' + fn + r'\s*\(', c):
            print('    %-24s lo llama ft8_modo_start()' % (fn + '()'))
        else:
            print('  FALLA: ft8_modo_start() NO llama a %s(). Sus datos viven '
                  'en la memoria prestada y se borran al salir de FT8: hay '
                  'que rehacerlos en cada arranque.' % fn)
            fallos += 1
    return fallos


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1
                  else '../deepsdr/User/ft8_modo.c'))
