#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Le pasa a fsck.fat los volumenes que deja sim/bd_borra.c.

POR QUE UN TRIBUNAL DE FUERA. Lo mismo que en tools/fat_check.py: una
comprobacion escrita por nosotros comparte las suposiciones del codigo que
comprueba. El banco ya mira byte a byte lo que el borrado toca, que es la
mitad util; la otra mitad es si el volumen que queda le parece bien a quien
no sabe nada de este proyecto. fsck.fat viene de dosfstools y se queja de
cadenas cruzadas, clusters perdidos, las dos copias de la FAT distintas y
-lo que encontro el 18/10/2026- entradas de nombre largo sin dueno.

SE MIRAN LOS DOS: el de ANTES tambien. Un volumen de partida que ya trajera
una queja dejaria sin valor la del de despues, porque no se sabria de quien
es. Si el de antes no esta limpio, el fallo es del banco y se dice asi.

    python3 tools/bdborra_check.py out/bdborra_antes.img out/bdborra_despues.img
"""
import subprocess
import sys

# Lo que fsck.fat imprime SIEMPRE y no es una queja.
RUIDO = ('fsck.fat', 'Checking for unused clusters', 'Leaving filesystem',
         'Checking free cluster summary', 'Boot sector contents',
         'Dirty bit is set')


def mira(ruta, quien):
    r = subprocess.run(['fsck.fat', '-n', ruta],
                       capture_output=True, text=True)
    # El resumen final es "ruta: N files, M/K clusters": no es una queja.
    lineas = [l for l in (r.stdout + r.stderr).split('\n')
              if l.strip()
              and not l.startswith(RUIDO)
              and not l.startswith(ruta + ':')]
    if r.returncode != 0 or lineas:
        print('  %s (%s): fsck.fat se queja' % (quien, ruta))
        for l in lineas:
            print('      %s' % l)
        if not lineas:
            print('      (salio con %d y sin decir nada)' % r.returncode)
        return 1
    print('  %s: fsck.fat no tiene nada que decir   ok' % quien)
    return 0


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    mal = mira(argv[1], 'el volumen de antes')
    mal += mira(argv[2], 'el volumen de despues')
    if mal:
        print('\n  *** %d ***' % mal)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
