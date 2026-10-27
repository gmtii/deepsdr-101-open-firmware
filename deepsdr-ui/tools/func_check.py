#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE EL BOTON FUNC TENGA SALIDA EN SUS TRES ESTADOS.

  python3 func_check.py <User/main.c>

POR QUE EXISTE - 23/10/2026.

*** Avisado por un usuario del repositorio: "it would also be nice to be
able to exit the Func menu by pressing the same button again. Currently, to
exit this menu, you have to select a function again, even if you
accidentally entered it." ***

Func tiene TRES estados, no dos, y esa es toda la historia:

  1. suelto                -> el boton ABRE la rejilla para elegir
  2. esperando a que elijas -> ???
  3. con algo enganchado   -> el boton SUELTA

El 1 y el 3 estaban desde el primer dia y son simetricos, asi que leyendo
el codigo parece completo. El 2 no tenia salida: el boton volvia a abrir la
MISMA pantalla, y la unica forma de salir era elegir un ajuste -o sea
enganchar algo que no querias para despues soltarlo-.

Y es justo el estado en el que cae quien toca el boton sin querer, que es
el que menos quiere quedarse dentro.

LO QUE MIDE. En la rama del boton Func, que los tres estados esten y que
cada uno haga lo suyo:

  - una condicion sobre s_encoder_ajuste que llame a ajuste_soltar()
  - una condicion sobre s_cfg_func que llame a menu_screen_close()
  - y una salida final que llame a cfg_show()

LO QUE NO MIDE: que lo que hagan sea bonito. Eso no lo puede mirar un
guion. Lo que si puede mirar -y es lo que fallaba- es que no haya un estado
del que no se pueda salir.

Probado roto: quitando cualquiera de los tres, esto se pone rojo y dice
cual falta.
"""
import io
import re
import sys


def rama_func(src):
    """El cuerpo del `else if (widget == &s_btn_func) { ... }`."""
    m = re.search(r'widget\s*==\s*&s_btn_func\s*\)\s*\{', src)
    if not m:
        return None
    i = m.end() - 1          # la llave que abre
    hondo = 0
    j = i
    while j < len(src):
        if src[j] == '{':
            hondo += 1
        elif src[j] == '}':
            hondo -= 1
            if hondo == 0:
                return src[i + 1:j]
        j += 1
    return None


def tiene(cuerpo, cond, accion):
    """¿Hay una condicion con `cond` cuyo tramo llame a `accion`?

    Se mira el tramo que va DESDE la condicion hasta la siguiente, que es
    lo que de verdad ejecuta ese estado. Buscar los dos sueltos por todo
    el cuerpo daria por bueno tener la condicion en un sitio y la accion
    en otro, que es exactamente el fallo que esto persigue.
    """
    for m in re.finditer(re.escape(cond), cuerpo):
        resto = cuerpo[m.end():]
        sig = re.search(r'\}\s*else\b', resto)
        tramo = resto[:sig.start()] if sig else resto
        if accion in tramo:
            return True
    return False


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    src = io.open(sys.argv[1], encoding='utf-8', errors='replace').read()

    cuerpo = rama_func(src)
    if cuerpo is None:
        print('  no encuentro la rama de &s_btn_func en %s   MAL' % sys.argv[1])
        print('  (esto no es que este bien: es que no se donde mirar)')
        return 1

    pruebas = (
        ('con algo enganchado, suelta',
         's_encoder_ajuste', 'ajuste_soltar'),
        ('esperando a que elijas, sale',
         's_cfg_func', 'menu_screen_close'),
        ('suelto, abre la rejilla',
         'else', 'cfg_show'),
    )

    malos = []
    for que, cond, accion in pruebas:
        ok = tiene(cuerpo, cond, accion)
        print('  %-44s %s' % (que, 'ok' if ok else 'MAL'))
        if not ok:
            malos.append((que, cond, accion))

    if malos:
        print('')
        for que, cond, accion in malos:
            print('  falta el estado "%s": una condicion con %s' % (que, cond))
            print('  cuyo tramo llame a %s()' % accion)
        print('')
        print('  Un estado del que no se puede salir no se ve leyendo, porque')
        print('  los otros dos son simetricos y parecen el juego completo.')
        return 1

    print('  los tres estados de Func tienen salida   ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
