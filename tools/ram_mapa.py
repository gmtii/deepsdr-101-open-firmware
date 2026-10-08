#!/usr/bin/env python3
"""
EL REPARTO DE LA RAM, LEIDO DEL MAPA Y NO DEL CODIGO FUENTE.

*** El dueño, al enterarse de que sobraban quince kilobytes de una Ethernet
que esta placa no usa: "esto me interesa, deberia de salir cuando te digo que
revises el codigo, porque no ha salido?". ***

Y la respuesta es que no podia salir. Un repaso del codigo fuente mira
ficheros .c; la RAM no se decide en un .c, se decide en el ENLAZADO. El mapa
es el unico sitio del proyecto donde pone que acaba de verdad en los 192 kB, y
hasta hoy no lo leia nadie. Los quince kilobytes de gd32f4xx_enet.c llevaban
ahi desde el primer dia y salieron por accidente: porque el enlazador se nego
a enlazar al meter 28 bytes mas.

Esto lo convierte en algo que se ve todos los dias:

  - saca el reparto de .bss y .data ordenado de mayor a menor, con el fichero
    del que viene cada cosa;
  - agrupa por fichero objeto, que es como se ve de un vistazo que un modulo
    entero pesa mas de lo que deberia;
  - y avisa de los ficheros de Firmware/Source -la libreria del fabricante-
    que estan ocupando RAM, porque esos son los que nadie repasa nunca.

No falla la compilacion por si solo: no todo lo grande sobra -g_ft8_shared_ram
son 54 kB y son a proposito-. Avisa, que es lo que hace falta cuando lo que se
quiere es que un humano lo mire.
"""
import re
import sys
import os

RAM_TOTAL = 192 * 1024


def secciones(mapa, cual):
    """Las entradas de la seccion de salida `cual` (.bss o .data)."""
    lineas = open(mapa, encoding='utf-8', errors='replace').read().split('\n')
    i = 0
    while i < len(lineas) and not lineas[i].startswith(cual + ' '):
        i += 1
    if i >= len(lineas):
        return []
    i += 1
    filas = []
    pend = None
    while i < len(lineas):
        l = lineas[i]
        if l and not l[0].isspace():
            break
        # "' .bss.algo'" solo, con la direccion en la linea siguiente
        m = re.match(r'^ (' + re.escape(cual) + r'\.\S+)\s*$', l)
        if m:
            pend = m.group(1)
            i += 1
            continue
        if pend:
            m = re.match(r'^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)$', l)
            if m:
                filas.append((int(m.group(2), 16), pend, m.group(3)))
            pend = None
            i += 1
            continue
        # todo en una linea
        m = re.match(r'^ (' + re.escape(cual) + r'\.\S+)\s+(0x[0-9a-f]+)\s+'
                     r'(0x[0-9a-f]+)\s+(\S+)$', l)
        if m:
            filas.append((int(m.group(3), 16), m.group(1), m.group(4)))
        i += 1
    return filas


def main():
    mapa = sys.argv[1] if len(sys.argv) > 1 else 'build/firmware.map'
    if not os.path.exists(mapa):
        print('ram_mapa: no hay mapa en %s (compila primero)' % mapa)
        return 0

    filas = secciones(mapa, '.bss') + secciones(mapa, '.data')
    if not filas:
        print('ram_mapa: el mapa no tiene secciones .bss/.data reconocibles')
        return 0

    total = sum(f[0] for f in filas)
    filas.sort(reverse=True)

    print('')
    print('EL REPARTO DE LA RAM (de build/firmware.map, no del codigo)')
    print('')
    print('  %d bytes en .bss + .data, de %d (%.1f%%); libres %d' %
          (total, RAM_TOTAL, 100.0 * total / RAM_TOTAL, RAM_TOTAL - total))
    print('')

    print('  los quince mayores')
    for sz, nom, obj in filas[:15]:
        print('    %8d  %-44s %s' % (sz, nom[:44], os.path.basename(obj)))

    porobj = {}
    for sz, _nom, obj in filas:
        porobj[os.path.basename(obj)] = porobj.get(os.path.basename(obj), 0) + sz
    print('')
    print('  por fichero objeto, los diez mayores')
    for obj, sz in sorted(porobj.items(), key=lambda x: -x[1])[:10]:
        print('    %8d  %s' % (sz, obj))

    # Lo que de verdad hay que mirar: la libreria del fabricante ocupando RAM.
    # Esos ficheros no los repasa nadie y entran en la compilacion por el
    # comodin del Makefile, no porque alguien los haya pedido.
    vendor = sorted(
        [(sz, nom, os.path.basename(obj)) for sz, nom, obj in filas
         if re.match(r'^gd32f4xx_', os.path.basename(obj))],
        reverse=True)
    print('')
    if vendor:
        tot_v = sum(v[0] for v in vendor)
        print('  *** AVISO: %d bytes de RAM vienen de la libreria del fabricante' % tot_v)
        print('      (Firmware/Source/*.c). Entran por el comodin del Makefile, no')
        print('      porque alguien los haya pedido. Mira si el periferico se usa:')
        for sz, nom, obj in vendor[:10]:
            print('        %8d  %-40s %s' % (sz, nom[:40], obj))
        print('      Para sacar uno: añadelo a FW_EXCLUIR en el Makefile. Si hacia')
        print('      falta, el enlazador lo dira a gritos y no en silencio.')
    else:
        print('  la libreria del fabricante no ocupa RAM: correcto.')
    print('')
    return 0


if __name__ == '__main__':
    sys.exit(main())
