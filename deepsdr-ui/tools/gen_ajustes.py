#!/usr/bin/env python3
"""
Saca de main.c la lista de ajustes y de paginas, y la escribe como cabecera
para los bancos del simulador.

POR QUE: sim/config.c y sim/ajustes.c comprueban que ningun nombre ni ningun
valor se sale de su celda, y que ninguna pagina se pasa de nueve. Para eso
tenian su propia copia de la lista, escrita a mano. Tres copias de lo mismo
-main.c, config.c y ajustes.c- y ninguna obligada a estar al dia.

El 23/09/2026 se anadio una pagina y los bancos, que seguian creyendo que
habia cuatro, se cayeron con un fallo de segmentacion. Tuvieron suerte: si
hubieran tenido una de mas en vez de una de menos, habrian seguido diciendo
"TODO CORRECTO" sobre una lista que ya no era la del firmware.

Ahora la lista sale de main.c y el peor caso de cada valor -que eso si es
cosa del banco, no del firmware- se busca por nombre. Un ajuste nuevo sin
peor caso declarado hace fallar el banco en vez de colarse sin medir.

29/09/2026: la tabla del firmware lleva los DOS idiomas, T("Idioma",
"Language"). Aqui salen los dos, porque el banco tiene que medir el ancho de
las dos cadenas: gfx2_text_in() alinea pero NO recorta, asi que una
traduccion mas larga que su celda no se corta - se derrama encima de la
celda de al lado. El indice 0 es siempre el espanol.
"""
import re, sys

def main(ruta_main, salida):
    src = open(ruta_main, encoding='utf-8').read()

    m = re.search(r'k_aj_paginas\[AJ_PAGINAS\]\s*=\s*\{(.*?)\};', src, re.S)
    if not m:
        print("  no encuentro k_aj_paginas[] en main.c"); return 1
    paginas = re.findall(r'T\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)', m.group(1))
    if not paginas:
        print("  k_aj_paginas[] ya no trae los dos idiomas - mira el T() de main.c"); return 1

    # El orden de las paginas es el de los #define, no el de la tabla.
    orden = {}
    for nombre, val in re.findall(r'#define\s+AJ_PAG_([A-Z]+)\s+(\d+)U', src):
        orden[nombre] = int(val)

    m = re.search(r'k_ajustes\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    if not m:
        print("  no encuentro k_ajustes[] en main.c"); return 1
    filas = re.findall(r'\{\s*T\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)\s*,\s*AJ_PAG_([A-Z]+)\s*,',
                      m.group(1))
    if not filas:
        print("  no he podido leer ninguna fila de k_ajustes[]"); return 1

    with open(salida, 'w', encoding='utf-8') as f:
        f.write("/* GENERADO por tools/gen_ajustes.py desde User/main.c - no editar. */\n")
        f.write("#define AJ_TABLA_PAGINAS %d\n" % len(paginas))
        f.write("#define AJ_TABLA_IDIOMAS 2   /* 0 = espanol, 1 = ingles */\n")
        f.write("static const char *const k_aj_pag_nombres[AJ_TABLA_PAGINAS][AJ_TABLA_IDIOMAS] = {\n")
        for es, en in paginas:
            f.write('    { "%s", "%s" },\n' % (es, en))
        f.write("};\n\n")
        f.write("#define AJ_TABLA_N %d\n" % len(filas))
        f.write("static const struct { const char *nombre[AJ_TABLA_IDIOMAS]; int pagina; }\n")
        f.write("k_aj_tabla[AJ_TABLA_N] = {\n")
        for es, en, pag in filas:
            f.write('    { { "%s", "%s" }, %d },\n' % (es, en, orden.get(pag, 0)))
        f.write("};\n")

    print("  %s: %d ajustes en %d paginas, dos idiomas" % (salida, len(filas), len(paginas)))
    return 0

sys.exit(main(sys.argv[1], sys.argv[2]))
