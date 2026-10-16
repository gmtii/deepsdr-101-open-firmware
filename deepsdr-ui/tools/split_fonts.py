#!/usr/bin/env python3
"""
Separa los datos de las fuentes de sus cabeceras.

POR QUE EXISTE
--------------
gen_assets.py emite cada fuente como `static const` dentro de su .h. Eso
significa una COPIA COMPLETA en cada .c que la incluya: en cuanto dos modulos
usan la misma fuente, el binario la lleva dos veces. No da ni error ni aviso;
solo se ve mirando los simbolos del .elf.

Esto deja las cabeceras con DECLARACIONES (extern) y junta todos los datos en
fonts_data.c, una sola vez.

Se llama desde gen_assets.py al terminar, para que regenerar las fuentes no
pueda volver a dejar el problema puesto - que es justo lo que paso la primera
vez que se regeneraron despues de separarlas a mano.
"""

import glob
import os
import re
import sys


def split(directorio):
    cabeceras = sorted(glob.glob(os.path.join(directorio, "font_*.h")))
    if not cabeceras:
        return 0

    salida = ['''/*
 * Datos de las fuentes antialiasadas, en UN solo sitio.
 *
 * Las cabeceras font_*.h solo DECLARAN (extern); los datos viven aqui una
 * vez. Ver tools/split_fonts.py para el porque.
 *
 * NO EDITAR A MANO: se regenera con tools/gen_assets.py.
 */
''']

    for h in cabeceras:
        src = open(h, encoding="utf-8").read()
        if "extern const" in src and "= {" not in src:
            print("  %s ya estaba separada" % os.path.basename(h))
            continue

        m = re.search(r'#include "gfx2_font\.h"\n', src)
        if not m:
            sys.exit("no encuentro el include en " + h)
        cabeza, cuerpo = src[:m.end()], src[m.end():]
        fin = cuerpo.rindex("#endif")
        datos, cola = cuerpo[:fin], cuerpo[fin:]

        decls = re.findall(r'static const (\S+) (\w+)(\[[0-9]*\])? = \{', datos)
        if not decls:
            sys.exit("no encuentro tablas en " + h)

        ext = "\n/* Los datos estan en fonts_data.c, no aqui. */\n"
        for typ, sym, arr in decls:
            ext += "extern const %s %s%s;\n" % (typ, sym, arr or "")

        open(h, "w", encoding="utf-8").write(cabeza + ext + "\n" + cola)
        salida.append('/* ------------------------------------------------------------------ */\n'
                      '#include "%s"\n\n%s'
                      % (os.path.basename(h), datos.replace("static const ", "const ")))

    destino = os.path.join(directorio, "fonts_data.c")
    open(destino, "w", encoding="utf-8").write("\n".join(salida))
    return len(cabeceras)


if __name__ == "__main__":
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "gfx2")
    n = split(d)
    print("fuentes separadas: %d" % n)
