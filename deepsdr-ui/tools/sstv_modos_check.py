#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE LA CABECERA DE SSTV NO MIENTA SOBRE QUE MODOS HAY.

POR QUE EXISTE.

El comentario de User/sstv.h decia que los modos Robot y PD "no estan" y
que "se pueden anadir despues". Se escribio cuando era verdad, se quedo ahi
cuando dejo de serlo, y un usuario de fuera leyo la cabecera, no la tabla, y
pidio un modo que la radio lleva decodificando desde hace semanas.

Un comentario que miente es peor que no tener comentario: el que no esta no
engana a nadie. Y el repositorio es publico, asi que lo lee cualquiera.

QUE COMPRUEBA: que los nombres de k_modos[] en sstv.c y los que la cabecera
enumera son EXACTAMENTE los mismos. Ni uno de mas ni uno de menos.

  python3 tools/sstv_modos_check.py User/sstv.c User/sstv.h
"""
import re
import sys


def modos_de_la_tabla(ruta):
    """Los nombres de k_modos[]: el segundo campo de cada fila."""
    txt = open(ruta, encoding='utf-8', errors='replace').read()
    i = txt.find('k_modos[] = {')
    if i < 0:
        return None
    fin = txt.find('\n};', i)
    if fin < 0:
        return None
    cuerpo = txt[i:fin]
    # { 44U, "Martin 1", ...
    return re.findall(r'\{\s*\d+U\s*,\s*"([^"]+)"', cuerpo)


def modos_de_la_cabecera(ruta):
    """Los nombres que enumera la frase "ESTAN LOS ..." de sstv.h.

    Se lee SOLO esa frase, hasta el punto que la cierra, para no recoger
    nombres sueltos de otros comentarios que hablan de un modo concreto."""
    txt = open(ruta, encoding='utf-8', errors='replace').read()
    i = txt.find('ESTAN LOS ')
    if i < 0:
        return None
    fin = txt.find('.', txt.find(':', i))
    if fin < 0:
        return None
    frase = txt[i:fin].replace('*', ' ').replace('\n', ' ')
    nombres = []
    for m in re.finditer(r'(Martin|Scottie|Robot)\s+(\d+|DX)|PD(\d+)', frase):
        if m.group(3) is not None:
            nombres.append('PD' + m.group(3))
        else:
            nombres.append(m.group(1) + ' ' + m.group(2))
    return nombres


def main():
    if len(sys.argv) != 3:
        print('uso: sstv_modos_check.py sstv.c sstv.h')
        return 2

    tabla = modos_de_la_tabla(sys.argv[1])
    if tabla is None:
        print('MAL: no encuentro k_modos[] en %s' % sys.argv[1])
        return 1

    cab = modos_de_la_cabecera(sys.argv[2])
    if cab is None:
        print('MAL: no encuentro la frase "ESTAN LOS ..." en %s' % sys.argv[2])
        return 1

    # "Martin 1 y 2" se escribe asi en la frase; la tabla los lleva enteros.
    # Por eso se comparan como CONJUNTOS y la frase tiene que nombrarlos uno
    # a uno. Es a proposito: una abreviatura es justo donde se cuela el que
    # falta.
    faltan = [m for m in tabla if m not in cab]
    sobran = [m for m in cab if m not in tabla]

    print('  %d modos en la tabla, %d nombrados en la cabecera' % (len(tabla), len(cab)))
    if faltan:
        print('MAL: la tabla los tiene y la cabecera no los nombra: %s' % ', '.join(faltan))
    if sobran:
        print('MAL: la cabecera los nombra y la tabla no los tiene: %s' % ', '.join(sobran))
    if faltan or sobran:
        return 1

    print('  la cabecera y la tabla dicen los mismos modos')
    return 0


if __name__ == '__main__':
    sys.exit(main())
