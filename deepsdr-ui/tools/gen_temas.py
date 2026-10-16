#!/usr/bin/env python3
"""
Genera sim/temas_tabla.h leyendo k_temas[] de User/main.c.

POR QUE GENERADO. El simulador tiene que poder pintar CADA tema tal y como
lo va a ver la radio: la paleta de interfaz Y la del waterfall, emparejadas
igual. Escribir esa lista a mano en sim/principal.c seria la quinta copia de
un dato que ya existe, y la que mas facil se queda atras - se anade un tema,
se mira en el simulador el que uno cree que es, y sale otro.

Con esto, "make temas" pinta lo que hay, no lo que habia.
"""
import re, sys, os

def main(fw, destino):
    src = open(os.path.join(fw, 'User', 'main.c'), encoding='utf-8').read()
    ini = src.index('static const tema_t k_temas[] = {')
    fin = src.index('\n};', ini)
    cuerpo = src[ini:fin]
    # 30/09/2026: k_temas[] lleva los dos idiomas, T("es", "en"). Aqui se
    # coge el espanol: este banco solo necesita un nombre para el fichero
    # .ppm que escribe, no mide anchos.
    filas = re.findall(
        r'\{\s*T\(\s*"([^"]*)"\s*,\s*"[^"]*"\s*\)\s*,\s*'
        r'T\(\s*"([^"]*)"\s*,\s*"[^"]*"\s*\)\s*,\s*&(\w+)\s*,\s*(\w+)\s*\}', cuerpo)
    if not filas:
        print('  no se ha podido leer k_temas[] de main.c  MAL')
        return 1
    with open(destino, 'w', encoding='utf-8') as f:
        f.write('/* GENERADO por tools/gen_temas.py - no editar a mano. */\n')
        f.write('#ifndef TEMAS_TABLA_H\n#define TEMAS_TABLA_H\n')
        f.write('#include "palette.h"\n#include "spectrum.h"\n\n')
        f.write('typedef struct { const char *nombre; const char *desc;\n'
                '                 const palette_t *pal; spectrum_palette_t wf; } sim_tema_t;\n\n')
        f.write('static const sim_tema_t k_sim_temas[] = {\n')
        for nom, desc, pal, wf in filas:
            f.write('    { "%s", "%s", &%s, %s },\n' % (nom, desc, pal, wf))
        f.write('};\n#define SIM_TEMA_COUNT (int)(sizeof(k_sim_temas)/sizeof(k_sim_temas[0]))\n')
        f.write('#endif\n')
    print('  %d temas -> %s' % (len(filas), destino))
    return 0

if __name__ == '__main__':
    fw = sys.argv[1] if len(sys.argv) > 1 else '../deepsdr'
    dst = sys.argv[2] if len(sys.argv) > 2 else 'sim/temas_tabla.h'
    sys.exit(main(fw, dst))
