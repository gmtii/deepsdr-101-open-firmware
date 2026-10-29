#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE LA LISTA DE FT8 SE LLENE HASTA EL FINAL Y EL DEDO NO SE DESCOLOQUE.

  python3 tools/ft8_lista_check.py <User/>

POR QUE EXISTE - 29/10/2026.

*** El usuario: que la lista de FT8 parece que no se llena bien, como si
solo cupieran quince. ***

Quince son los que CABEN EN EL PANEL; la lista guarda cuarenta y ocho y a
los demas se llega arrastrando con el dedo. Eso estaba bien. Pero al ir a
mirarlo aparecieron dos cosas que no, y las dos viven en el pegamento de
main.c, que es el unico trozo de este camino que ningun simulador compila:
sim/ft8_lista.c mide ft8_lista.c -y pasa-, y nadie miraba las veinte lineas
que lo llaman.

LO QUE SE ENCONTRO
------------------
1. LA COLA DEL DECODIFICADOR ERA MAS CORTA QUE LA LISTA. Cuarenta contra
   cuarenta y ocho. La cola se vacia entera nada mas decodificar, asi que
   lo unico que tiene que caber ahi es una ranura; con cuarenta, una ranura
   de banda abierta que trajera cuarenta y cinco mensajes perdia cinco
   ANTES de llegar a la lista, que tenia sitio de sobra. Y en silencio.
   Eso lo ata ahora un _Static_assert en ft8_modo.c, no este guion.

2. EL SCROLL SE MOVIA CON EL CONTADOR EQUIVOCADO. Con el dedo subido, la
   ventana tiene que subir tantos renglones como mensajes hayan ENTRADO
   POR ARRIBA, para que lo que estas leyendo no se te vaya. Se le estaba
   dando ft8_modo_total(), que es lo que vio el DECODIFICADOR e incluye lo
   que se le cayo de la cola. Un desborde subia la ventana de mas.
   ft8_modo_metidas() cuenta inserciones, que es la pregunta de verdad.

LAS REGLAS
----------
  0. que siga puesto el _Static_assert que ata la cola a la lista. El
     aserto lo comprueba el compilador, pero que el ASERTO EXISTA no lo
     comprueba nadie: borrarlo compila igual de bien y deja el fallo de
     arriba listo para volver;
  1. el scroll usa ft8_modo_metidas(), y en esa parte NO aparece
     ft8_modo_total();
  2. el buffer de indices se dimensiona con FT8_MODO_LINEAS, no con un
     numero escrito a mano;
  3. la cuenta de guardados se topa con FT8_MODO_LINEAS;
  4. los renglones del panel salen de FT8_PANEL_FILAS y se topan con
     FT8_MODO_LINEAS.

LO QUE NO COMPRUEBA: que ft8_lista.c escoja bien. Eso es de
sim/ft8_lista.c, que lo mide con listas de verdad y con el dedo subido.
"""
import io
import os
import re
import sys


def sin_comentarios(t):
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


def di(texto, bien, mal):
    print("    %s %s" % (texto.ljust(46, '.'), "ok" if bien else mal))
    return 0 if bien else 1


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    d = sys.argv[1]
    src = sin_comentarios(
        io.open(os.path.join(d, 'main.c'), encoding='utf-8',
                errors='replace').read())
    mal = 0

    modo = sin_comentarios(
        io.open(os.path.join(d, 'ft8_modo.c'), encoding='utf-8',
                errors='replace').read())

    print("  la lista de FT8 y su scroll:")

    # 0. El aserto que ata la cola a la lista.
    mal += di("la cola atada a la lista por _Static_assert",
              re.search(r'_Static_assert\s*\(\s*\(int\)\s*'
                        r'FT8_DECODER_MSG_QUEUE_SIZE\s*>=\s*\(int\)\s*'
                        r'FT8_MODO_LINEAS', modo) is not None,
              "*** NO: sin el, una cola mas corta que la lista vuelve a "
              "tirar mensajes que si caben, y en silencio ***")

    # 1. El scroll, con el contador de inserciones.
    pon = re.findall(r's_ft8_total_ant\s*=\s*(\w+)\s*\(\s*\)\s*;', src)
    mal += di("s_ft8_total_ant sale de alguien", len(pon) > 0,
              "*** NO: no encuentro quien lo pone ***")
    if pon:
        mal += di("  y ese alguien es ft8_modo_metidas()",
                  all(v == 'ft8_modo_metidas' for v in pon),
                  "*** NO: usa %s. Con total() un desborde de la cola sube "
                  "la ventana de mas ***" % ', '.join(sorted(set(pon))))

    m = re.search(r'uint32_t\s+total\s*=\s*(\w+)\s*\(\s*\)\s*;', src)
    mal += di("el `total` del scroll sale de alguien", m is not None,
              "*** NO ***")
    if m:
        mal += di("  y ese alguien es ft8_modo_metidas()",
                  m.group(1) == 'ft8_modo_metidas',
                  "*** NO: usa %s ***" % m.group(1))

    # Y que ft8_lista_nuevos() reciba ESE numero y no otro.
    mal += di("ft8_lista_nuevos() recibe lo que entro",
              re.search(r'ft8_lista_nuevos\s*\(\s*ft8_modo_linea\s*,\s*'
                        r'\(uint8_t\)\s*ins\s*,', src) is not None,
              "*** NO: el desplazamiento de la ventana no sale de ahi ***")

    # 2, 3 y 4: las cuentas, de la constante y no a mano.
    mal += di("idx[] se dimensiona con FT8_MODO_LINEAS",
              re.search(r'uint8_t\s+idx\s*\[\s*FT8_MODO_LINEAS\s*\]', src)
              is not None,
              "*** NO: un numero a mano ahi se queda atras al crecer la "
              "lista ***")
    mal += di("guardados se topa con FT8_MODO_LINEAS",
              re.search(r'guardados\s*<\s*\(uint8_t\)\s*FT8_MODO_LINEAS', src)
              is not None,
              "*** NO ***")
    mal += di("los renglones salen de FT8_PANEL_FILAS",
              re.search(r'n\s*=\s*\(uint8_t\)\s*\(\s*FT8_PANEL_FILAS\s*-\s*1U\s*\)',
                        src) is not None,
              "*** NO: un numero a mano no sigue a la geometria ***")
    mal += di("y se topan con FT8_MODO_LINEAS",
              re.search(r'if\s*\(\s*n\s*>\s*\(uint8_t\)\s*FT8_MODO_LINEAS\s*\)',
                        src) is not None,
              "*** NO: el panel pediria mas renglones de los que hay ***")

    if mal:
        print("  *** %d fallo(s): ver la cabecera de este guion ***" % mal)
        return 1
    print("  la lista de FT8 se llena hasta el final  ok")
    return 0


if __name__ == '__main__':
    sys.exit(main())
