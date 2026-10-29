#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE AL CAMBIAR DE TEMA -O DE IDIOMA- NO QUEDE NADA DE LO ANTERIOR.

  python3 tools/tema_check.py <User/>

POR QUE EXISTE - 29/10/2026.

*** El usuario, con una foto del tema claro puesto: que se ha quedado un
trozo gris al cambiar de tema. ***

Y se quedaba. tema_aplicar() repintaba con freq_display_draw(), con el
comentario "la cabecera entera" al lado. Esa funcion NO pinta la cabecera
entera: pinta de 0 a rds_x1() -la frecuencia y la pastilla de banda- y
nada mas. Lo dice su propio cuerpo desde el 05/10/2026, cuando se recorto
a proposito para no gastar 32.000 accesos de bus en cada clic del mando.
El comentario de tema_aplicar() se quedo describiendo lo que la funcion
hacia ANTES de aquel recorte, y nadie los volvio a leer juntos.

Lo que quedaba fuera son los dos trozos de la derecha: el fondo de la
franja de la marquesina, y la pastilla MEM con el reloj y la bateria. El
segundo solo lo repinta time_display_draw(), o sea AL CAMBIAR DE MINUTO.
Hasta un minuto entero con la esquina del reloj del tema anterior.

ESTE ES EL FALLO DE "UN COMENTARIO QUE SE PUDRE". No se caza leyendo
tema_aplicar() -lo que pone al lado de la llamada es exactamente lo que
uno querria que pasara- ni leyendo freq_display_draw(), que es correcta y
explica su recorte. Solo se ve poniendo las dos al lado, y eso no pasa
solo.

LAS DOS FUNCIONES, NO UNA. idioma_cambiar() copio la solucion de
tema_aplicar() el 06/10 -lo dice su propio comentario, "se resuelve igual,
copiando su solucion a proposito"- y copio tambien el agujero. Hoy en esa
esquina no hay ninguna palabra que cambie con el idioma, asi que no se ve;
pero dos funciones gemelas con una arreglada y la otra no es exactamente
como vuelven a separarse. Las dos tienen que pasar por aqui.

LAS TRES REGLAS, PARA CADA UNA DE LAS DOS:

  1. tema_aplicar() repinta con ui_top_draw(), que son los 800 px de
     ancho: cabecera -esquina del reloj incluida-, marquesina y franja de
     estado;
  2. y con act_draw(), que es la barra de abajo;
  3. y NO con freq_display_draw(), que es justo el recorte que causo
     esto. Esta regla esta escrita en negativo a proposito: la tentacion
     de volver a poner la "barata" es la que hay que cazar.

Y una cuarta que sostiene a la primera:

  4. que ui_top_draw() siga pintando desde x=0 y con el ancho entero
     (GFX2_W). Si alguien la recorta, la regla 1 dejaria de significar lo
     que dice y esto lo diria en vez de dejarlo pasar.

LO QUE NO COMPRUEBA: lo que haya EN MEDIO de la pantalla. Eso se lo deja
tema_aplicar() a quien llama -cada pantalla sabe repintarse a si misma- y
desde aqui no se puede saber cual hay puesta.
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
    print("    %s %s" % (texto.ljust(42, '.'), "ok" if bien else mal))
    return 0 if bien else 1


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    d = sys.argv[1]
    src = sin_comentarios(
        io.open(os.path.join(d, 'main.c'), encoding='utf-8',
                errors='replace').read())
    top = sin_comentarios(
        io.open(os.path.join(d, 'ui_top.c'), encoding='utf-8',
                errors='replace').read())
    mal = 0

    print("  el repintado al cambiar de tema y de idioma:")

    # Las dos gemelas, con la misma vara de medir.
    GEMELAS = (
        ('static void tema_aplicar(uint8_t i)', 'tema'),
        ('static void idioma_cambiar(void)',    'idioma'),
    )
    for firma, que in GEMELAS:
        c = cuerpo(src, firma)
        if c is None:
            print("    no encuentro %s   *** MAL ***" % firma)
            return 1

        mal += di("cambiar de %s repinta los 800 px" % que,
                  re.search(r'\bui_top_draw\s*\(', c) is not None,
                  "*** NO: el reloj y la marquesina se quedan como estaban ***")
        mal += di("  y la barra de abajo",
                  re.search(r'\bact_draw\s*\(\s*\)', c) is not None,
                  "*** NO: la barra de acciones se queda como estaba ***")
        mal += di("  sin volver al recorte de la frecuencia",
                  re.search(r'\bfreq_display_draw\s*\(\s*\)', c) is None,
                  "*** NO: esa solo pinta de 0 a rds_x1(). Es el fallo de la "
                  "foto del 29/10 ***")

    # La cuarta: que ui_top_draw() siga siendo los 800 px.
    c = cuerpo(top, 'void ui_top_draw(const ui_top_state_t *st)')
    if c is None:
        print("    no encuentro ui_top_draw()   *** MAL ***")
        return 1
    g = re.search(r'gfx2_render\s*\(\s*0\s*,\s*0\s*,\s*GFX2_W\s*,', c)
    mal += di("ui_top_draw() pinta los 800 px", g is not None,
              "*** NO: si se recorta, la regla de arriba ya no vale ***")

    if mal:
        print("  *** %d fallo(s): ver la cabecera de este guion ***" % mal)
        return 1
    print("  cambiar de tema o de idioma no deja nada del anterior  ok")
    return 0


if __name__ == '__main__':
    sys.exit(main())
