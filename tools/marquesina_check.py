#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE LA MARQUESINA DE LA CABECERA NO SE PARE CON UN MENU ABIERTO.

  python3 tools/marquesina_check.py <User/>

POR QUE EXISTE - 29/10/2026.

*** El usuario, con la pantalla de Ajustes abierta y una foto del cartel
rodeado en rojo: "China National Rad" y "CHN - UI - 11:00-14:0", los dos
cortados por la derecha y los dos quietos. ***

Las dos marquesinas de la cabecera -la del RDS y la de la base de
emisoras- se iban en la primera linea con `s_menu_open`. El razonamiento
tenia sentido: con un menu encima nadie mira la cabecera, asi que no se
gasta bus en repintarla.

Salvo que NINGUN menu de esta radio tapa la cabecera. Las seis pantallas
que ponen s_menu_open empiezan en Y=104 y la franja de la marquesina vive
en 10..54. El texto se quedaba a la vista, cortado y parado.

Y es peor de lo que parece: emisoras_tick() SI repinta al cambiar de
minuto, y pone los dos desplazamientos a cero. Con un menu abierto un rato
largo, el cartel se reiniciaba cada minuto al principio del nombre y ahi
se quedaba.

LAS DOS MITADES, y hacen falta las dos:

  1. que ni emis_marq_poll() ni rds_marq_poll() se vayan por s_menu_open,
     y que sigan yendose por s_screen_asleep y por !s_pantalla_pintada,
     que esas si dicen algo;
  2. que NINGUNA pantalla de menu empiece por encima del final de la
     cabecera. Esta es la que de verdad justifica la primera: el dia que
     alguien haga una pantalla a pantalla completa, la guarda tiene que
     volver, y esto saltara para decirlo en vez de dejar la marquesina
     pintando debajo de otra cosa.

LO QUE NO COMPRUEBA: que la marquesina se mueva a la velocidad correcta ni
que el texto de la vuelta donde debe. Eso es de sim/emis_cartel.c y de
sim/top_franjas.c, que miden el cartel y la idempotencia de las franjas.
"""
import io
import os
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
    """El cuerpo de la DEFINICION, no el del prototipo."""
    desde = 0
    i = -1
    while True:
        k = src.find(firma, desde)
        if k < 0:
            return None
        if src[k + len(firma):k + len(firma) + 8].lstrip().startswith('{'):
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


def di(texto, bien, mal):
    print("    %s %s" % (texto.ljust(44, '.'), "ok" if bien else mal))
    return 0 if bien else 1


# Las pantallas de menu y la constante que dice donde empiezan. El nombre
# del fichero va al lado porque es lo que hay que abrir si esto salta.
PANTALLAS = (
    ('ui_grid.h', 'UIG_Y', 'la rejilla (y con ella ui_cfg.h y ui_det.h)'),
    ('ui_hora.h', 'UIH_Y', 'la hora por radio'),
    ('ui_kbd.h',  'UIK_Y', 'el teclado'),
    ('ui_qth.h',  'UIQ_Y', 'el localizador'),
)


def numero(txt, nombre):
    g = re.search(r'#define\s+' + nombre + r'\s+(\d+)', txt)
    return int(g.group(1)) if g else None


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    d = sys.argv[1]
    src = sin_comentarios(
        io.open(os.path.join(d, 'main.c'), encoding='utf-8',
                errors='replace').read())
    top = io.open(os.path.join(d, 'ui_top.h'), encoding='utf-8',
                  errors='replace').read()
    mal = 0

    print("  la marquesina de la cabecera:")

    for fn in ('emis_marq_poll', 'rds_marq_poll'):
        c = cuerpo(src, 'static void %s(void)' % fn)
        if c is None:
            print("    no encuentro %s()   *** MAL ***" % fn)
            return 1
        # Solo la guarda de entrada, que es el primer `if` de la funcion.
        g = re.search(r'if\s*\((.*?)\)\s*\{\s*return\s*;\s*\}', c, re.S)
        guarda = g.group(1) if g else ''
        mal += di("%s() no se para por el menu" % fn,
                  's_menu_open' not in guarda,
                  "*** NO: el cartel se queda cortado y quieto ***")
        mal += di("  y sigue parando con la pantalla dormida",
                  's_screen_asleep' in guarda,
                  "*** NO: pintaria con la pantalla apagada ***")
        mal += di("  y antes de la primera pintada",
                  's_pantalla_pintada' in guarda,
                  "*** NO: pintaria sobre lo que no esta montado ***")

    # Y la razon por la que lo de arriba es correcto.
    fin = numero(top, 'UI_TOP_RDS_Y')
    alto = numero(top, 'UI_TOP_RDS_H')
    if fin is None or alto is None:
        print("    no encuentro UI_TOP_RDS_Y/H en ui_top.h   *** MAL ***")
        return 1
    abajo = fin + alto
    print("    la franja de la marquesina acaba en Y=%d" % abajo)

    for fichero, cte, quien in PANTALLAS:
        ruta = os.path.join(d, fichero)
        try:
            txt = io.open(ruta, encoding='utf-8', errors='replace').read()
        except OSError:
            print("    no puedo leer %s   *** MAL ***" % fichero)
            mal += 1
            continue
        y = numero(txt, cte)
        if y is None:
            print("    no encuentro %s en %s   *** MAL ***" % (cte, fichero))
            mal += 1
            continue
        mal += di("%s empieza en Y=%d" % (quien, y), y >= abajo,
                  "*** NO: TAPA la marquesina. Hay que volver a poner la "
                  "guarda de s_menu_open ***")

    if mal:
        print("  *** %d fallo(s): ver la cabecera de este guion ***" % mal)
        return 1
    print("  la marquesina corre con el menu abierto  ok")
    return 0


if __name__ == '__main__':
    sys.exit(main())
