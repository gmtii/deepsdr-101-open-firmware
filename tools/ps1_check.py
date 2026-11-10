#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
LO POCO QUE SE PUEDE COMPROBAR DE UN .ps1 SIN PowerShell.

POR QUE EXISTE - 06/11/2026.

tools/cat_vuelta.ps1 son quinientas lineas que NO se pueden ejecutar ni
analizar en la maquina donde se escribe este proyecto: no hay PowerShell. O
sea que un fallo de sintaxis no sale hasta que alguien esta delante de la
radio con el cable puesto, y entonces el viaje se ha perdido.

Y ya ha pasado: al anadir el veredicto del audio se colo un ACENTO GRAVE
dentro de una cadena de comillas DOBLES. En PowerShell el acento grave es la
escapada - como la barra invertida en C -, asi que "el tramo `dib`" no dice
lo que parece: se come la d y la b. Lo escribi, lo lei dos veces y no lo vi.

QUE COMPRUEBA, que es poco y va dicho:

  1. ACENTOS GRAVES dentro de comillas dobles que no sean ni una escapada
     valida de PowerShell (`n `r `t `0 `a `b `f `v `" `` `$) ni la
     continuacion de renglon (acento grave y fin de linea).
  2. Que en cada cadena de formato de -f el hueco mas alto {N} cuadre con
     el numero de argumentos que lleva detras, contando comas de nivel
     cero y siguiendo las continuaciones de renglon. Un {28} con 28
     argumentos no falla al compilar: falla al imprimir, y a mitad de
     tanda.

QUE NO COMPRUEBA: todo lo demas. Esto no es un analizador de PowerShell y
no pretende serlo. Es una red para los dos fallos que ya se han cometido.

  python3 tools/ps1_check.py tools/cat_vuelta.ps1
"""
import re
import sys

ESCAPADAS = set('nrt0abfv"`$\'')


def graves(texto):
    """Acentos graves sospechosos dentro de comillas dobles."""
    malos = []
    for nl, linea in enumerate(texto.splitlines(), 1):
        dentro = False
        i = 0
        while i < len(linea):
            c = linea[i]
            if c == '"':
                dentro = not dentro
            elif c == '`' and dentro:
                # Al final del renglon es continuacion, y vale.
                if i + 1 >= len(linea):
                    pass
                elif linea[i + 1] not in ESCAPADAS:
                    malos.append((nl, linea.strip()))
                    break
                else:
                    i += 1   # la escapada se come el siguiente
            i += 1
    return malos


def formatos(texto):
    """Huecos {N} contra numero de argumentos, en cada -f."""
    lineas = texto.splitlines()
    malos = []
    for i, l in enumerate(lineas):
        if ' -f ' not in l and not l.rstrip().endswith('-f `'):
            continue
        idx = set(int(m.group(1)) for m in re.finditer(r'\{(\d+)[,:}]', l))
        if not idx:
            continue
        # Los argumentos: lo que va detras del -f, siguiendo continuaciones.
        resto = l.split(' -f ', 1)[1] if ' -f ' in l else ''
        partes = [resto.rstrip().rstrip('`')]
        j = i
        while lineas[j].rstrip().endswith('`') and j + 1 < len(lineas):
            j += 1
            partes.append(lineas[j].strip().rstrip('`'))
        texto_args = ' '.join(p for p in partes if p)
        if not texto_args.strip():
            continue
        n = 1
        prof = 0
        for c in texto_args:
            if c in '([':
                prof += 1
            elif c in ')]':
                prof -= 1
            elif c == ',' and prof == 0:
                n += 1
        if max(idx) != n - 1:
            malos.append((i + 1, max(idx), n, l.strip()[:60]))
    return malos


def main():
    if len(sys.argv) < 2:
        print('uso: ps1_check.py fichero.ps1 [...]')
        return 2
    fallos = 0
    for ruta in sys.argv[1:]:
        texto = open(ruta, encoding='utf-8', errors='replace').read()
        g = graves(texto)
        f = formatos(texto)
        print('  %s' % ruta)
        for nl, l in g:
            print('    MAL  renglon %d: acento grave dentro de comillas dobles' % nl)
            print('         %s' % l[:70])
            fallos += 1
        for nl, mx, n, l in f:
            print('    MAL  renglon %d: el hueco mas alto es {%d} y hay %d argumentos'
                  % (nl, mx, n))
            print('         %s' % l)
            fallos += 1
        if not g and not f:
            print('    ni acentos graves raros ni huecos de formato descuadrados  ok')
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
