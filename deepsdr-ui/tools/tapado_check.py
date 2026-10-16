#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Comprueba que el renglon de titulos del panel digital NO queda debajo del mapa.

POR QUE EXISTE ESTO. Con el mapa puesto, panel_pinta() le daba a ui_digi un
renglon de titulos que decia "toca el mapa para volver a la lista", y despues
dibujaba el mapa. ui_digi pinta los titulos en `st->y + UDG_HDR_H`; el mapa
empieza en FT8_MAPA_Y, que era FT8_PANEL_Y + UDG_HDR_H. El mismo pixel. El
mapa lo tapaba entero desde el primer repintado, asi que ese texto no se vio
NUNCA - y para cuando se descubrio ademas ya era mentira, porque tocar el mapa
habia pasado a moverlo y quien lo quita es el boton "Mapa".

Un rotulo tapado no se nota mirando: no sale nada raro, no sale nada. Y no es
un fallo que se arregle una vez, porque el dia que a alguien le parezca util
poner un titulo ahi -o que el mapa suba unos pixeles- vuelve solo. Esto mide
la geometria, no la buena intencion.

LA REGLA, dicha entera: si el panel del mapa pide renglon de titulos, el mapa
tiene que empezar POR DEBAJO de ese renglon. Cualquiera de las dos salidas la
cumple -quitar el titulo, o bajar el mapa UDG_LINE_H pixeles-, y eso es a
proposito: el banco dice que no se tape, no cual de las dos se elige.
"""
import re, sys

def defines(src):
    """Los #define de una sola linea, sin los que viven en comentarios."""
    sin = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    sin = re.sub(r'//[^\n]*', '', sin)
    d = {}
    for m in re.finditer(r'^[ \t]*#define[ \t]+(\w+)[ \t]+(.+)$', sin, re.M):
        d[m.group(1)] = m.group(2).strip()
    return d

def numero(expr, conocidos):
    """Evalua una expresion de #define quitando casts y sustituyendo nombres."""
    e = re.sub(r'\((?:u?int\d+_t|unsigned|signed|int|short|long|char)\s*\)', '', expr)
    e = re.sub(r'\b(\d+)[uUlL]+\b', r'\1', e)
    for _ in range(12):
        antes = e
        for k, v in conocidos.items():
            e = re.sub(r'\b' + k + r'\b', '(' + str(v) + ')', e)
        if e == antes:
            break
    if re.search(r'[A-Za-z_]', e):
        return None
    try:
        return int(eval(e))
    except Exception:
        return None

def cuerpo_rama_mapa(src):
    """El bloque de panel_pinta() que corre con el mapa puesto."""
    i = src.find('static void panel_pinta(void)')
    if i < 0:
        return None
    j = src.find('ft8_mapa_draw();', i)
    if j < 0:
        return None
    return src[i:j]

def main(raiz):
    ui  = open(raiz + '/User/ui_digi.h', encoding='utf-8').read()
    mc  = open(raiz + '/User/main.c',    encoding='utf-8').read()
    dui = defines(ui)
    dmc = defines(mc)

    base = {}
    for nombre in ('UDG_HDR_H', 'UDG_LINE_H'):
        if nombre not in dui:
            print('  *** %s no esta en ui_digi.h ***' % nombre)
            return 1
        v = numero(dui[nombre], {})
        if v is None:
            print('  *** %s no sale un numero: %s ***' % (nombre, dui[nombre]))
            return 1
        base[nombre] = v

    # Todo medido DESDE EL BORDE DE ARRIBA DEL PANEL, que es lo que ui_digi
    # llama st->y: asi no hace falta resolver de donde sale FT8_PANEL_Y.
    conocidos = dict(base)
    conocidos['FT8_PANEL_Y'] = 0
    if 'FT8_MAPA_Y' not in dmc:
        print('  *** FT8_MAPA_Y no esta en main.c ***')
        return 1
    mapa_y = numero(dmc['FT8_MAPA_Y'], conocidos)
    if mapa_y is None:
        print('  *** FT8_MAPA_Y no sale un numero: %s ***' % dmc['FT8_MAPA_Y'])
        return 1

    rama = cuerpo_rama_mapa(mc)
    if rama is None:
        print('  *** no encuentro la rama del mapa de panel_pinta() ***')
        return 1
    rama_sin = re.sub(r'/\*.*?\*/', '', rama, flags=re.S)
    asig = re.findall(r's_digi\.titulos\s*=\s*([^;]+);', rama_sin)
    if len(asig) != 1:
        print('  *** s_digi.titulos se asigna %d veces en la rama del mapa '
              '(esperaba 1) ***' % len(asig))
        return 1
    hay_titulos = asig[0].strip() != '0'

    tit_y0 = base['UDG_HDR_H']
    tit_y1 = tit_y0 + base['UDG_LINE_H']

    print('RENGLON DE TITULOS TAPADO POR EL MAPA\n')
    print('    medido desde el borde de arriba del panel (st->y):')
    print('      titulos      y = %d .. %d   (UDG_HDR_H, alto UDG_LINE_H)'
          % (tit_y0, tit_y1 - 1))
    print('      mapa empieza y = %d           (FT8_MAPA_Y)' % mapa_y)
    print('      la rama del mapa pide titulos: %s   -> %s\n'
          % ('si' if hay_titulos else 'no', asig[0].strip()))

    if not hay_titulos:
        print('    sin renglon de titulos con el mapa puesto            bien')
        print('\n  TODO CORRECTO (0 fallos)')
        return 0
    if mapa_y >= tit_y1:
        print('    el mapa empieza por debajo del renglon de titulos    bien')
        print('\n  TODO CORRECTO (0 fallos)')
        return 0

    tapados = min(tit_y1, mapa_y + 1) - max(tit_y0, mapa_y)
    print('    el renglon de titulos se ve                          MAL')
    print('\n  HAY FALLOS: el mapa empieza en y=%d y el renglon de titulos ocupa'
          % mapa_y)
    print('  y=%d..%d, asi que el mapa lo tapa (%d px de %d). Ese texto no lo'
          % (tit_y0, tit_y1 - 1, max(tapados, tit_y1 - mapa_y), base['UDG_LINE_H']))
    print('  va a leer nadie. O se quita (s_digi.titulos = 0) o el mapa baja')
    print('  UDG_LINE_H pixeles, a FT8_PANEL_Y + UDG_HDR_H + UDG_LINE_H.')
    return 1

if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '.'))
