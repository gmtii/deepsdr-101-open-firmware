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

Faltaba ft8_fft_init(). Sus cuatro tablas viven dentro de la memoria
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
    fallos += prestatarios(os.path.dirname(ruta))
    fallos += orden_en_apply(os.path.dirname(ruta))

    print('  TODO CORRECTO' if not fallos else '  %d FALLOS' % fallos)
    return 1 if fallos else 0


# Los OTROS modos que viven en la misma memoria. Cada uno con el par de
# funciones por donde entra y sale del modo.
#
# POR QUE ESTA LISTA EXISTE: el cerrojo lo echa quien entra y lo quita quien
# sale, y eso no lo puede comprobar el compilador. Un modo que coge la
# memoria y no la suelta deja la cascada apagada para siempre; uno que la
# suelta sin cogerla pinta sobre los pixeles del siguiente. Los dos fallos
# son de los que se ven una semana despues y en otro modo.
#
# El 28/10/2026 entraron el fax y -ya estaban- el SSTV, HELL, DSC y WSPR,
# al ensanchar FT8 a la banda entera y tener que devolver SRAM.
PRESTATARIOS = [
    ('wefax.c', 'wefax_start', 'wefax_stop'),
    ('sstv.c',  'sstv_start',  'sstv_stop'),
    ('hell.c',  'hell_start',  'hell_stop'),
    ('wspr_modo.c', 'wspr_modo_start', 'wspr_modo_stop'),
]


def prestatarios(dircab):
    """Que cada prestatario coja la memoria al entrar y la suelte al salir."""
    fallos = 0

    for fichero, arranca, para in PRESTATARIOS:
        ruta = os.path.join(dircab, fichero)
        if not os.path.exists(ruta):
            print('  FALLA: no esta %s, que esta en la lista de '
                  'prestatarios' % fichero)
            fallos += 1
            continue
        txt = open(ruta, encoding='utf-8').read()
        for fn, llamada, verbo in ((arranca, 'hfdl_ram_coge', 'coge'),
                                   (para, 'hfdl_ram_suelta', 'suelta')):
            c = cuerpo(txt, fn)
            if c is None:
                print('  FALLA: no se encuentra %s() en %s' % (fn, fichero))
                fallos += 1
                continue
            if not re.search(re.escape(llamada) + r'\s*\(', c):
                print('  FALLA: %s() no %s la memoria prestada '
                      '(falta %s())' % (fn, verbo, llamada))
                fallos += 1
            else:
                print('    %-18s %s la memoria' % (fn + '()', verbo))

    return fallos


# Los que SUELTAN la memoria al salir de su modo, dentro de
# apply_demod_mode(). Son los nueve del panel entero mas FT8.
SUELTAN = ('ft8_modo_stop', 'hfdl_modo_stop', 'wspr_modo_stop',
           'ais_modo_stop', 'ale_modo_stop', 'jtty_modo_stop',
           'stanag_modo_stop', 'ident_modo_stop')

# Y los que la COGEN al entrar.
COGEN = ('wefax_start', 'sstv_start', 'hell_start', 'dsc_start',
         'ft8_modo_start', 'hfdl_modo_start', 'wspr_modo_start',
         'ais_modo_start', 'ale_modo_start', 'jtty_modo_start',
         'stanag_modo_start', 'ident_modo_start')


def sin_comentarios(t):
    """Tapa /* ... */ y // ... con espacios, dejando los saltos de linea.

    Hace falta porque main.c EXPLICA este reparto en prosa y nombra a
    wefax_start() y compania dentro de los comentarios que documentan el
    arreglo. Sin esto, el guion se denunciaba a si mismo.
    """
    out = []
    i = 0
    n = len(t)
    while i < n:
        if t.startswith('/*', i):
            j = t.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(c if c == '\n' else ' ' for c in t[i:j]))
            i = j
        elif t.startswith('//', i):
            j = t.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        else:
            out.append(t[i])
            i += 1
    return ''.join(out)


def orden_en_apply(dircab):
    """PRIMERO TODAS LAS PARADAS, Y DESPUES TODOS LOS ARRANQUES.

    Dentro de menu_mode_preset_callback(), que es la funcion que reparte
    los modos.

    29/10/2026. Esta es la regla de la que depende todo lo demas, y este
    guion no la comprobaba: cinco ficheros del arbol lo citan por su nombre
    como el guardian de esta memoria -"el que coja esta RAM, que pregunte"-
    y lo unico que miraba era que cada start() tuviera su hfdl_ram_coge().

    Por eso paso el fax. wefax_start() estaba CIENTO CINCUENTA LINEAS antes
    de la lista de paradas, asi que de HFDL a FAX el fax montaba su anillo
    sobre la union con HFDL todavia vivo escribiendo encima, y acto seguido
    hfdl_modo_stop() soltaba el prestamo, que es un memset de los 69.852
    bytes sobre ese anillo recien montado. Y a partir de ahi el fax corria
    con la memoria sin marcar como prestada, o sea con la cascada
    escribiendo pixeles dentro de sus renglones.

    El cerrojo no lo puede evitar: es UN BIT, sin cuenta de anidamiento.
    Lo unico que lo evita es el orden, y el orden ahora se mide.
    """
    ruta = os.path.join(dircab, 'main.c')
    if not os.path.exists(ruta):
        print('  (no esta main.c: me salto el orden de las listas)')
        return 0
    src = sin_comentarios(open(ruta, encoding='utf-8').read())
    # El reparto de modos vive en menu_mode_preset_callback(), que es quien
    # tiene las dos listas; apply_demod_mode() solo rehace la cadena.
    c = cuerpo(src, 'menu_mode_preset_callback')
    if c is None:
        print('  FALLA: no se encuentra menu_mode_preset_callback()')
        return 1

    def ultimo(nombres):
        pos = -1
        cual = None
        for n in nombres:
            for m in re.finditer(r'\b' + n + r'\s*\(', c):
                if m.start() > pos:
                    pos, cual = m.start(), n
        return pos, cual

    def primero(nombres):
        pos = len(c) + 1
        cual = None
        for n in nombres:
            for m in re.finditer(r'\b' + n + r'\s*\(', c):
                if m.start() < pos:
                    pos, cual = m.start(), n
        return pos, cual

    p_ult, n_ult = ultimo(SUELTAN)
    p_pri, n_pri = primero(COGEN)
    if p_ult < 0 or n_pri is None:
        print('  FALLA: no encuentro las paradas o los arranques dentro '
              'de menu_mode_preset_callback()')
        return 1

    if p_pri < p_ult:
        print('  FALLA: %s() coge la memoria prestada ANTES de que %s() la '
              'suelte.' % (n_pri, n_ult))
        print('         Primero TODAS las paradas y despues TODOS los '
              'arranques: el cerrojo es un bit y no cuenta anidamiento.')
        return 1

    print('    %-18s todos los arranques van detras de la ultima parada (%s)'
          % ('la rejilla de modos:', n_ult + '()'))
    return 0


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
        # *_init, *_reset y *_arranca: los tres nombres con los que este
        # arbol llama a "deja esto como recien encendido". El tercero entro
        # el 23/10/2026 con ft8_waterfall_arranca(), que es justo la llamada
        # cuya ausencia dejaba FT8 sin decodificar al volver a entrar; sin
        # anadirlo aqui, esta comprobacion habria mirado para otro lado
        # exactamente en el caso para el que existe.
        for m in re.finditer(r'\bvoid\s+(ft8_\w+_(?:init|reset)|ft8_\w+_arranca)'
                             r'\s*\(\s*void\s*\)',
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
