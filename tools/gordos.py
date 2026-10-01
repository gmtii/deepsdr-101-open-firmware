#!/usr/bin/env python3
"""
DE QUE SE LLENA LA FLASH Y LA RAM, POR MODULO.

POR QUE EXISTE (25/09/2026). Al pedir HFDL, la primera pregunta no es como se
porta sino DONDE CABE: quedaban 4.236 bytes de flash de 262.144. La respuesta
a "de donde sacamos hueco" no puede ser una opinion sobre lo que parece
gordo - este proyecto ya se equivoco una vez asi (se predijo que el cuello de
botella era el bus de la pantalla y el numero medido dijo que no).

Asi que esto lo mide, leyendo el .map que escribe el enlazador: quien aporta
cada byte de .text, .rodata, .data y .bss, agrupado por fichero objeto, y
ademas separado por REGION (flash, SRAM principal y TCM), porque no son
intercambiables: sobrar RAM no sirve de nada si lo que falta es flash.

Lo que NO dice: si algo se puede quitar. Eso lo decide quien conoce la radio.
Esto solo pone los numeros delante para que la conversacion sea sobre datos.
"""
import re, sys, os, collections

MAPA = sys.argv[1] if len(sys.argv) > 1 else 'build/firmware.map'
CUANTOS = int(sys.argv[2]) if len(sys.argv) > 2 else 25

txt = open(MAPA, encoding='utf-8', errors='replace').read()

# SOLO EL MAPA DE VERDAD. El .map empieza con "Discarded input sections", que
# son justamente las que --gc-sections tiro: contarlas daba 266.468 bytes de
# flash usados de 262.144, o sea "-4.324 libres". Un numero imposible es la
# forma mas amable que tiene una herramienta de avisar de que esta mal, y esta
# aviso a la primera. Se empieza a leer donde empieza el mapa real.
corte = txt.find('Linker script and memory map')
if corte < 0:
    sys.exit('gordos.py: este .map no trae "Linker script and memory map"')
txt = txt[corte:]

# El .map de GNU ld lista, por seccion de salida, lineas del tipo:
#     .text.foo      0x0800abcd       0x34 build/bar.o
# y cuando el nombre es largo, la direccion y el tamano caen en la linea
# siguiente. Las dos formas se contemplan.
sec = collections.defaultdict(lambda: collections.defaultdict(int))
ent = []
pend = None

#
# LA FLASH SON DOS REGIONES, NO UNA - 28/09/2026.
#
# Esta herramienta decia "FLASH 304.793 de 262.144 usados, -42.649 libres" y
# nadie lo miraba. Un numero imposible es la forma mas amable que tiene una
# herramienta de avisar de que esta mal - lo dice su propio comentario
# treinta lineas mas abajo, a proposito de otro imposible que si se
# arreglo en su dia.
#
# La causa: desde que se desensamblo el gestor de arranque, la flash esta
# PARTIDA. Abajo viven los 256 kB de la imagen que el gestor carga; en
# 0x08060000 hay 8 bytes de firma, y por encima un desvan de 64 kB
# (.arriba) donde se suben a mano fuentes y tablas. Ver el comentario
# grande de GD32F450VE_FLASH.ld.
#
# Sumar las dos y compararlas con 262.144 es sumar peras con manzanas. El
# Makefile ya lo hace bien y por eso dice "OK: cabe" mientras esto decia
# que faltaban 42 kB.
#
FLASH_BAJA_FIN = 0x08040000   # 256 kB: donde acaba la imagen que carga el gestor
FIRMA_DIR      = 0x08060000   # los 8 bytes de firma, ver el .ld

def region_de(dirn):
    if 0x08000000 <= dirn < FLASH_BAJA_FIN:  return 'FLASH'
    if FIRMA_DIR  <= dirn < 0x08100000:      return 'DESVAN'
    if 0x08000000 <= dirn < 0x08100000:      return 'FLASH'   # entre medias no hay nada
    if 0x20000000 <= dirn < 0x20030000:      return 'SRAM'
    if 0x10000000 <= dirn < 0x10010000:      return 'TCM'
    return None

lineas = txt.split('\n')
for i, ln in enumerate(lineas):
    m = re.match(r'^ (\.\S+)$', ln)
    if m:
        pend = m.group(1); continue
    # El relleno de alineacion tambien ocupa, y sin el las cuentas no cuadran
    # con el binario por unos cientos de bytes. Se le apunta a "(relleno)"
    # porque no es de nadie: lo pone el enlazador entre seccion y seccion.
    m = re.match(r'^ \*fill\*\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)', ln)
    if m:
        dirn, tam = int(m.group(1), 16), int(m.group(2), 16)
        r = region_de(dirn)
        if r: ent.append((r, dirn, tam, '(relleno)'))
        pend = None
        continue

    m = re.match(r'^ (\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)', ln)
    if m:
        nom, dirn, tam, obj = m.group(1), int(m.group(2),16), int(m.group(3),16), m.group(4)
        pend = None
    else:
        m = re.match(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)', ln)
        if not (m and pend): continue
        nom, dirn, tam, obj = pend, int(m.group(1),16), int(m.group(2),16), m.group(3)
        pend = None
    if tam == 0: continue
    region = region_de(dirn)
    if not region: continue
    obj = os.path.basename(obj)

    #
    # CADENAS FUNDIDAS: NO SON DE QUIEN LAS FIRMA.
    #
    # Cuando el enlazador funde las cadenas de todo el firmware, el mapa pone
    # TODO el charco a nombre del primer objeto que llego, y debajo una linea
    # "(size before relaxing)" con lo que ese objeto puso de verdad. Sin
    # mirarla, dcf77.o salia con 12.461 bytes cuando aporta 4.178: los otros
    # 8.379 son las cadenas de main.c, de la interfaz y de todos los demas.
    #
    # Importa mas de lo que parece. Esta herramienta existe para decidir que
    # se quita, y con ese numero dcf77 parecia el tercer gasto de la radio.
    # Un instrumento que miente es peor que no tener ninguno: lleva a cortar
    # donde no hay nada que cortar.
    #
    if i + 1 < len(lineas):
        mr = re.match(r'^\s+0x([0-9a-f]+) \(size before relaxing\)\s*$', lineas[i+1])
        if mr:
            propio = int(mr.group(1), 16)
            if 0 < propio < tam:
                ent.append((region, dirn, propio, obj))
                ent.append((region, dirn + propio, tam - propio, '(cadenas fundidas)'))
                continue
    ent.append((region, dirn, tam, obj))

#
# SUMAR LOS TAMANOS DEL .map DA DE MAS, Y HAY QUE CONTARLO POR DIRECCIONES.
#
# Primera version: sumar el tamano de cada entrada por objeto. Dio 266.468
# bytes de flash de 262.144, o sea "-4.324 libres". Un imposible es la forma
# mas amable que tiene una herramienta de avisar de que esta mal.
#
# La causa: las secciones de cadenas (.rodata.str1.1) las FUNDE el enlazador -
# dos ficheros que escriban "error" comparten los mismos bytes- pero el mapa
# sigue listando el tamano de entrada de cada uno, en la misma direccion. Son
# 118 entradas que se pisan y unos 9 kB contados dos veces.
#
# Asi que se cuenta por DIRECCIONES ocupadas, no por tamanos declarados: se
# recorre en orden y cada entrada solo suma la parte que nadie habia contado
# todavia. El total cuadra entonces con el binario, que es la unica forma de
# saber que esta bien. Lo compartido se le apunta al primero que llega: es
# arbitrario, pero solo afecta a las cadenas y el total no miente.
#
ent.sort(key=lambda e: (e[0], e[1]))
alto = {}
for region, dirn, tam, obj in ent:
    ini = max(dirn, alto.get(region, 0))
    fin = dirn + tam
    if fin > ini:
        sec[region][obj] += fin - ini
        alto[region] = fin

TOPE = {'FLASH': 262144, 'DESVAN': 65520, 'SRAM': 196608, 'TCM': 65536}

#
# Y ESTA HERRAMIENTA DICE CUANTO SE EQUIVOCA.
#
# Lo que se lee aqui es un mapa de texto, no el binario, y ya conto mal una
# vez (las cadenas fundidas). Asi que se compara el total de flash con lo que
# dice arm-none-eabi-size del .elf y se imprime la diferencia. Mientras sea
# de unos cientos de bytes son huecos de alineacion entre secciones de salida,
# que el mapa no lista; si algun dia se dispara, esto lo dice en vez de
# presentar un numero redondo y falso.
#
def real_del_elf(ruta_map):
    elf = ruta_map[:-4] + '.elf' if ruta_map.endswith('.map') else None
    if not elf or not os.path.exists(elf):
        return None
    try:
        import subprocess
        sal = subprocess.run(['arm-none-eabi-size', elf],
                             capture_output=True, text=True).stdout.split('\n')[1].split()
        return int(sal[0]) + int(sal[1])
    except Exception:
        return None
real = real_del_elf(MAPA)
if real is not None:
    # LAS DOS REGIONES JUNTAS, que es lo que cuenta arm-none-eabi-size: suma
    # text+data de TODO el binario, y en este firmware eso incluye el desvan.
    # Compararlo solo con la region baja daba "-55.267 de hueco", que es el
    # tamano del desvan disfrazado de error de medida. Ver region_de().
    medido = sum(sec['FLASH'].values()) + sum(sec['DESVAN'].values())
    print("  (el binario dice %s bytes de flash -las dos regiones-; aqui "
          "salen %s, %+d de hueco entre secciones)" %
          (format(real, ',d').replace(',', '.'),
           format(medido, ',d').replace(',', '.'), medido - real))

for region in ('FLASH', 'DESVAN', 'SRAM', 'TCM'):
    d = sec[region]
    total = sum(d.values())
    print("\n%s  %s de %s usados, %s libres" %
          (region, format(total, ',d').replace(',', '.'),
           format(TOPE[region], ',d').replace(',', '.'),
           format(TOPE[region]-total, ',d').replace(',', '.')))
    print("  %-34s %9s  %5s" % ("objeto", "bytes", "%"))
    for obj, n in sorted(d.items(), key=lambda kv: -kv[1])[:CUANTOS]:
        print("  %-34s %9s  %4.1f%%" %
              (obj, format(n, ',d').replace(',', '.'), 100.0*n/total))
    resto = sum(n for _, n in sorted(d.items(), key=lambda kv: -kv[1])[CUANTOS:])
    if resto:
        print("  %-34s %9s  %4.1f%%" % ("(los demas)",
              format(resto, ',d').replace(',', '.'), 100.0*resto/total))
