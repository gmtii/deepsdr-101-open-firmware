#!/usr/bin/env python3
"""
Extrae la maquina del barrido de memorias de User/main.c a una cabecera
que compila el banco de pruebas del host.

Existe por lo mismo que bandas_dump.py: para que la prueba no pueda estar
probando una copia. El barrido se entrego mal CUATRO veces -se paraba en
la primera, en la segunda, en la segunda vuelta, y iba tan despacio que se
perdia las frases cortas- y las cuatro eran de logica, no de medida. Una
copia a mano en sim/ habria seguido pasando las cuatro veces.

Lo que saca son las funciones TAL CUAL, sin tocar una linea: si alguien
cambia el reparto en main.c, aqui se compila lo nuevo o no compila.
"""
import re, sys, pathlib

src = pathlib.Path(sys.argv[1]).read_text(encoding='utf-8')
out = pathlib.Path(sys.argv[2])
# Las constantes van EN SU PROPIO FICHERO porque las usan dos bancos: este
# y sim/scan_nivel.c, que mide la lectura. Alli estaban copiadas a mano, y
# una copia a mano de un numero que acaba de cambiar es como se entrega un
# banco que mide el firmware de la semana pasada.
out_c = out.with_name('barrido_consts.h')

FUNCS = ['scan_lee', 'scan_mira', 'mem_hay_senal', 'scan_ventana',
         'mem_scan_para', 'mem_scan_salta', 'mem_scan_conmuta',
         'mem_scan_poll']

trozos = []

# Las constantes, que son la mitad de lo que hay que comprobar.
defs = re.findall(r'^#define\s+(SCAN_[A-Z_0-9]+)\s+([^\n/]+?)\s*(?:/\*.*)?$',
                  src, re.M)
if len(defs) < 7:
    sys.exit('faltan constantes SCAN_*: encontradas %d' % len(defs))
consts = ['#define %-22s %s' % (n, v.strip()) for n, v in defs]
trozos.append('#include "barrido_consts.h"')
trozos.append('')

for f in FUNCS:
    # La firma, en el cuerpo (no el prototipo: el prototipo acaba en ';').
    m = re.search(r'^static\s+[\w ]+\**%s\s*\([^)]*\)\s*\n\{' % re.escape(f),
                  src, re.M)
    if not m:
        sys.exit('no encuentro el cuerpo de %s()' % f)
    fin = src.index('\n}\n', m.start())
    trozos.append(src[m.start():fin + 3].rstrip() + '\n')

cab = ('/* GENERADO por tools/barrido_dump.py desde User/main.c. No se\n'
       ' * edita: lo que valga se cambia alli. */\n\n')
out.write_text(cab + '\n'.join(trozos) + '\n', encoding='utf-8')
out_c.write_text(cab + '\n'.join(consts) + '\n', encoding='utf-8')
print('%s: %d funciones; %s: %d constantes'
      % (out.name, len(FUNCS), out_c.name, len(defs)))
