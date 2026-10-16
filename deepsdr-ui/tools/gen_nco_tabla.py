#!/usr/bin/env python3
"""
Genera User/nco_tabla.c: la tabla de senos del NCO, en FLASH y no en RAM.

POR QUE
-------
La tabla son 1025 floats, 4.100 bytes, y nco.c la calculaba al arrancar
dentro de un array estatico. O sea 4.100 bytes de los 196.608 que tiene esta
radio gastados en unos numeros que no cambian nunca y que se podrian haber
escrito en la flash, de la que sobran 140 kB.

Eso no se noto mientras hubo sitio. El 04/10/2026 el enlazador se planto por
16 bytes al añadir cuatro medidas al identificador de señales, y entonces si:
  region `RAM' overflowed by 16 bytes

Pasandola a flash se recuperan 4.100 de golpe, que es sitio para lo que venga
detras y no solo para hoy.

SE GENERA, NO SE ESCRIBE A MANO, y tools/nco_tabla_check.py lo comprueba en
cada `make comprueba`: si alguien edita el .c a mano, se pone rojo.
"""
import math
import sys

N = 1024


def tabla():
    return [math.sin(2.0 * math.pi * k / N) for k in range(N + 1)]


def flt(x):
    """Un float literal de C que SIEMPRE lleva punto.

    '%.9g' % 0.0 da '0', y '0f' no compila. Nueve cifras significativas son
    las que hacen falta para que un float de 32 bits vuelva a leerse igual.
    """
    t = '%.9g' % x
    if 'e' in t or 'E' in t:
        m, e = t.split('e') if 'e' in t else t.split('E')
        if '.' not in m:
            m += '.0'
        return m + 'e' + e + 'f'
    if '.' not in t:
        t += '.0'
    return t + 'f'


def texto():
    v = tabla()
    out = []
    out.append('/*')
    out.append(' * GENERADO POR tools/gen_nco_tabla.py - NO EDITAR A MANO.')
    out.append(' *')
    out.append(' * La tabla de senos del NCO: %d puntos de una vuelta entera,' % (N + 1))
    out.append(' * mas uno repetido al final para que la interpolacion lineal no')
    out.append(' * tenga que mirar si se sale.')
    out.append(' *')
    out.append(' * Vive en FLASH (const) y no en RAM. Son 4.100 bytes, y en esta radio')
    out.append(' * la RAM se acabo literalmente -el enlazador se planto por 16 bytes el')
    out.append(' * 04/10/2026- mientras de flash sobran 140 kB. Ver nco.c.')
    out.append(' */')
    out.append('#include "nco_tabla.h"')
    out.append('')
    out.append('const float k_nco_sen[NCO_TABLA_N + 1U] = {')
    for i in range(0, N + 1, 6):
        fila = ', '.join(flt(x) for x in v[i:i + 6])
        out.append('    ' + fila + ',')
    out.append('};')
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    destino = sys.argv[1] if len(sys.argv) > 1 else 'User/nco_tabla.c'
    with open(destino, 'w', encoding='utf-8') as f:
        f.write(texto())
    print('  %s: %d puntos' % (destino, N + 1))
