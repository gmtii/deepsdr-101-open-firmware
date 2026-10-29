#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE DE UN MODO DE PANEL ENTERO SE PUEDA SALIR SIN EL TACTIL.

  python3 tools/salida_check.py <User/main.c>

POR QUE EXISTE - 29/10/2026.

*** El usuario: que el modo IDENT le deja la radio tostada, que no va el
tactil y que el encoder si va. ***

El tactil fallando en esos modos es un fallo. Que de esos modos no se pueda
SALIR sin tactil es peor, y es independiente: el boton Modo vive en la
barra de abajo, la rejilla se abre con el dedo y los modos se eligen con el
dedo. Sin tactil, la unica salida era apagar la radio.

Asi que la pulsacion larga del mando -que ya quiere decir "sacame de
aqui": devuelve el mando a la sintonia y cierra el menu- apaga ademas el
modo digital cuando no habia nada mas que deshacer.

LAS TRES REGLAS:

  1. tune_encoder_poll() tiene una rama de pulsacion larga.
  2. Dentro de ella se llama a digi_todo_apagado().
  3. Y esa llamada esta bajo modo_panel_entero(), para no apagar nada
     cuando no hay nada digital puesto.

LO QUE NO COMPRUEBA: que el tactil funcione. Eso es otro problema y tiene
que seguir siendo otro problema: esta salida existe precisamente para que
un fallo del tactil no deje la radio sin gobierno.
"""
import io
import re
import sys


def sin_comentarios(t):
    """Tapa /* ... */ y // ... con espacios, dejando los saltos de linea."""
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


def cuerpo(src, firma):
    """El cuerpo de la DEFINICION, no el del prototipo.

    Este fichero declara casi todo dos veces -el prototipo arriba y la
    definicion donde toca-, asi que buscar la firma a secas y despues la
    primera llave encuentra el cuerpo de OTRA funcion. Se exige que detras
    de la firma venga la llave, con espacios o un salto de linea en medio y
    nada mas.
    """
    i = -1
    desde = 0
    while True:
        k = src.find(firma, desde)
        if k < 0:
            return None
        resto = src[k + len(firma):k + len(firma) + 8]
        if resto.lstrip().startswith('{'):
            i = k
            break
        desde = k + 1
    i = src.find('{', i)
    if i < 0:
        return None
    n = 0
    for j in range(i, len(src)):
        if src[j] == '{':
            n += 1
        elif src[j] == '}':
            n -= 1
            if n == 0:
                return src[i:j + 1]
    return None


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    src = sin_comentarios(
        io.open(sys.argv[1], encoding='utf-8', errors='replace').read())
    mal = 0

    print("  la salida sin tactil de los modos de panel entero:")

    c = cuerpo(src, 'static void tune_encoder_poll(void)')
    if c is None:
        print("    no encuentro tune_encoder_poll()   *** MAL ***")
        return 1

    m = re.search(r'if\s*\(\s*long_press\s*\)\s*\{', c)
    print("    hay rama de pulsacion larga ........... %s"
          % ("ok" if m else "*** NO ***"))
    if not m:
        return 1

    # El cuerpo de esa rama
    i = c.index('{', m.start())
    n = 0
    fin = len(c)
    for j in range(i, len(c)):
        if c[j] == '{':
            n += 1
        elif c[j] == '}':
            n -= 1
            if n == 0:
                fin = j
                break
    rama = c[i:fin + 1]

    apaga = re.search(r'digi_todo_apagado\s*\(\s*\)\s*;', rama)
    print("    y apaga el modo digital ............... %s"
          % ("ok" if apaga else
             "*** NO: de esos ocho modos no se sale sin tactil ***"))
    mal += 0 if apaga else 1

    if apaga:
        antes = rama[:apaga.start()]
        bajo = re.search(r'if\s*\([^)]*modo_panel_entero\s*\(\s*\)[^)]*\)\s*\{'
                         r'[^{}]*$', antes, re.S)
        print("    bajo modo_panel_entero() .............. %s"
              % ("ok" if bajo else
                 "*** NO: apagaria cosas sin que haya nada digital ***"))
        mal += 0 if bajo else 1

    if mal:
        print("  *** %d fallo(s): ver la cabecera de este guion ***" % mal)
        return 1
    print("  del panel entero se sale con el mando  ok")
    return 0


if __name__ == '__main__':
    sys.exit(main())
