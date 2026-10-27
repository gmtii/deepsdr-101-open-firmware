#!/usr/bin/env python3
"""
EL BLOQUE «ESTADO» DEL README, CONTRA LO QUE DICE EL ENLAZADOR.

LO QUE PASABA
-------------
Ese bloque es lo primero que mira quien llega al repositorio: cuanta flash
y cuanta RAM quedan, cuanto mide el cargador, cuanto margen tiene la pila.
Y lo escribia una persona a mano cada vez que se acordaba.

El 20/10/2026, con el repositorio ya publicado, decia:

    FLASH   472.832 de 491.520   libre  18.688     <- sobraban 4.096
    RAM     195.172 de 196.608   libre   1.436     <- faltaban 5.976
    PILA      5.672 de  6.768    margen  1.096     <- ni una de las tres

y el parrafo de debajo remataba con «la RAM va al 99,98 %, treinta y dos
bytes libres», que ya se contradecia con el propio bloque de encima. Tres
numeros publicados, los tres falsos, y ninguna comprobacion que los mirara.

No es un descuido de quien lo escribio: es que un numero copiado a mano a
un documento se pudre SOLO, y el unico momento en que alguien se entera es
cuando lo lee un desconocido.

QUE HACE
--------
Saca cada numero de donde se genera -los .bin, los dos mapas del
enlazador, los .ld y tools/pila.py- y lo compara con lo que dice el README.
Si no cuadran, falla y dice cual.

Con --arregla reescribe el bloque con los numeros buenos, que es lo que
hay que hacer al cambiar algo: no tocarlo a mano otra vez.
"""

import os
import struct
import re
import subprocess
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(AQUI)
sys.path.insert(0, AQUI)

import ram_mapa                                     # noqa: E402


def secciones_bd(ruta):
    """Las secciones de BD.BIN y lo que mide cada una, de su directorio.

    Devuelve [(tipo, bytes)] en el orden en que estan. El formato esta
    contado en tools/datos_pack.py; aqui solo se lee la cabecera.
    """
    # Contra RAIZ y no contra el directorio actual: este guion lo llama el
    # Makefile del simulador desde deepsdr-ui/, igual que a tam().
    try:
        d = open(os.path.join(RAIZ, ruta), 'rb').read(4096)
    except IOError:
        return []
    if len(d) < 16 or d[0:4] != b'DSDR':
        return []
    n = d[5]
    fuera = []
    for i in range(n):
        if 16 + 16 * i + 16 > len(d):
            break
        t, _off, ln, _s = struct.unpack_from('<4I', d, 16 + 16 * i)
        fuera.append((struct.pack('<I', t).decode('ascii', 'replace'), ln))
    return fuera


K_BD_NOMBRE = {'I24B': 'aviones', 'EMIS': 'emisoras',
               'SPLF': 'la fuente de la lluvia'}


def desglose_bd(secs):
    """ "294.909 aviones, 220.736 emisoras, 5.592 la fuente de la lluvia" """
    return ', '.join('%s %s' % (mil(n), K_BD_NOMBRE.get(t, t))
                     for t, n in secs)


def mil(n):
    """123456 -> '123.456', que es como lo escribe el README."""
    return '{:,}'.format(int(n)).replace(',', '.')


def tam(ruta):
    try:
        return os.path.getsize(os.path.join(RAIZ, ruta))
    except OSError:
        return None


def ram_de(mapa):
    """(usado, tope) de un mapa, contados como los cuenta ram_mapa.py."""
    ruta = os.path.join(RAIZ, mapa)
    if not os.path.exists(ruta):
        return None, None
    filas = (ram_mapa.secciones(ruta, '.bss') + ram_mapa.secciones(ruta, '.data')
             + ram_mapa.secciones(ruta, '.noinit'))
    if not filas:
        return None, None
    tope, _ = ram_mapa.ram_total(ruta)
    return sum(f[0] for f in filas), tope


def region(mapa, nombre):
    """El tamaño de una region del mapa (TCMRAM, FLASH...)."""
    ruta = os.path.join(RAIZ, mapa)
    if not os.path.exists(ruta):
        return None
    for n, _org, lon in ram_mapa.regiones(ruta):
        if n == nombre:
            return lon
    return None


def tcm_de(mapa):
    """
    Lo que ocupa TCMRAM. No sale de .bss: las secciones que van ahi tienen
    su propio nombre de salida en el .ld, asi que se busca la region en la
    tabla de memoria y se suman las secciones que caen dentro.
    """
    ruta = os.path.join(RAIZ, mapa)
    if not os.path.exists(ruta):
        return None, None
    tope = region(mapa, 'TCMRAM')
    if not tope:
        return None, None
    org = None
    for n, o, _l in ram_mapa.regiones(mapa if os.path.isabs(mapa) else ruta):
        if n == 'TCMRAM':
            org = o
    if org is None:
        return None, None
    usado = 0
    txt = open(ruta, encoding='utf-8', errors='replace').read()
    # Las lineas de seccion de salida: ".tcm  0x10000000  0x1234"
    for ln in txt.split('\n'):
        c = ln.split()
        if len(c) == 3 and c[0].startswith('.') and c[1].startswith('0x'):
            try:
                o, l = int(c[1], 0), int(c[2], 0)
            except ValueError:
                continue
            if org <= o < org + tope:
                usado += l
    return usado, tope


def pila():
    """(usado, sitio, margen, pct) de tools/pila.py, que ya lo calcula."""
    try:
        sal = subprocess.run([sys.executable, os.path.join(AQUI, 'pila.py')],
                             cwd=RAIZ, capture_output=True, text=True).stdout
    except OSError:
        return None
    u = re.search(r'TOTAL\s*\.+\s*(\d+)', sal)
    s = re.search(r'sitio real\s*\.+\s*(\d+)', sal)
    m = re.search(r'margen\s*\.+\s*(\d+)\s*bytes\s*\((\d+)%\)', sal)
    if not (u and s and m):
        return None
    return int(u.group(1)), int(s.group(1)), int(m.group(1)), int(m.group(2))


def tope_ld(ld, region_nombre):
    """El LENGTH de una region, leido del .ld. Admite 480K, 0x80000, 491520."""
    try:
        txt = open(os.path.join(RAIZ, ld), encoding='utf-8').read()
    except OSError:
        return None
    m = re.search(r'^\s*' + region_nombre +
                  r'\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*\S+\s*,\s*LENGTH\s*=\s*(\S+)',
                  txt, re.M)
    if not m:
        return None
    v = m.group(1).rstrip(',')
    mul = 1
    if v[-1:] in ('K', 'k'):
        v, mul = v[:-1], 1024
    elif v[-1:] in ('M', 'm'):
        v, mul = v[:-1], 1024 * 1024
    try:
        return int(v, 0) * mul
    except ValueError:
        return None


def calcula():
    """Los numeros de verdad, cada uno de su fuente."""
    d = {}

    d['bd'] = tam('BD.BIN')
    d['bd_tope'] = 847872                 # el techo de la zona alta
    # LO QUE MIDE CADA SECCION, LEIDO DEL PROPIO BD.BIN - 26/10/2026.
    #
    # Estos tres numeros estaban ESCRITOS A MANO aqui abajo ("294.909
    # aviones, 211.387 emisoras, 5.592 la fuente"), que es exactamente lo
    # que este guion existe para impedir en todos los demas renglones. Se
    # noto al rehacer la base con HFCC: el total cambio, --arregla lo puso
    # al dia, y el desglose se quedo diciendo el de antes - o sea, un
    # renglon donde las partes no suman el total y nadie se entera.
    d['bd_secciones'] = secciones_bd('BD.BIN')

    d['fw'] = tam('update.bin')
    d['fw_tope'] = tope_ld('GD32F450VE_FLASH.ld', 'FLASH') or 491520
    d['fw470'] = tam('update_470.bin')
    d['fw470_tope'] = tope_ld('GD32F470VG_FLASH.ld', 'FLASH') or 1015808

    d['boot'] = tam('customboot.bin')
    d['boot470'] = tam('boot/customboot_470.bin')
    d['boot_tope'] = tope_ld('boot/GD32F450VE_BOOT.ld', 'FLASH') or 32768

    d['ram'], d['ram_tope'] = ram_de('build/firmware.map')
    d['ram470'], d['ram470_tope'] = ram_de('build_470/firmware_470.map')
    d['tcm'], d['tcm_tope'] = tcm_de('build/firmware.map')
    d['pila'] = pila()
    return d


def lineas_buenas(d, viejas):
    """
    El bloque como tiene que quedar. Las dos primeras lineas -version,
    cargador y los nombres de los binarios- no salen de aqui: las vigila
    tools/version_check.py, asi que se copian tal cual.
    """
    fuera = []
    for ln in viejas:
        c = ln.strip()
        if c.startswith('ZONA ALTA'):
            fuera.append('  ZONA ALTA  %s de  %s   libre %s  (%s,'
                         % (mil(d['bd']), mil(d['bd_tope']),
                            mil(d['bd_tope'] - d['bd']),
                            desglose_bd(d['bd_secciones'])
                            .split(', ')[0]))
        elif ' emisoras, ' in c and c.endswith('la fuente)'):
            fuera.append('                                     %s)'
                         % ', '.join(desglose_bd(d['bd_secciones'])
                                     .split(', ')[1:]).replace(
                             ' la fuente de la lluvia', ' la fuente'))
        elif c.startswith('FLASH 470'):
            fuera.append('  FLASH 470  %s de %s libre %s <- el mismo codigo, otro .ld'
                         % (mil(d['fw470']), mil(d['fw470_tope']),
                            mil(d['fw470_tope'] - d['fw470'])))
        elif c.startswith('FLASH '):
            fuera.append('  FLASH   %s de %s   libre  %s   <- UNA region, sin desvan'
                         % (mil(d['fw']), mil(d['fw_tope']),
                            mil(d['fw_tope'] - d['fw'])))
        elif c.startswith('CARGADOR '):
            fuera.append('  CARGADOR %s de  %s   libre     %s   <- DOS sectores, no tres'
                         % (mil(d['boot']), mil(d['boot_tope']),
                            mil(d['boot_tope'] - d['boot'])))
        elif c.startswith('RAM 470'):
            fuera.append('  RAM 470    %s de %s  libre %s  <- 448 kB contiguos'
                         % (mil(d['ram470']), mil(d['ram470_tope']),
                            mil(d['ram470_tope'] - d['ram470'])))
        elif c.startswith('RAM '):
            fuera.append('  RAM     %s de %s   libre   %s   <- .bss + .data + .noinit'
                         % (mil(d['ram']), mil(d['ram_tope']),
                            mil(d['ram_tope'] - d['ram'])))
        elif c.startswith('TCM '):
            fuera.append('  TCM      %s de  %s   libre   %s   <- y eso ES la pila'
                         % (mil(d['tcm']), mil(d['tcm_tope']),
                            mil(d['tcm_tope'] - d['tcm'])))
        elif c.startswith('PILA '):
            u, s, m, p = d['pila']
            fuera.append('  PILA      %s de  %s    margen  %s (%d%%)  <- CON anidamiento'
                         % (mil(u), mil(s), mil(m), p))
        else:
            fuera.append(ln)
    return fuera


#
# EL MISMO BLOQUE, EN EL CUADERNO DE TALLER - 21/10/2026.
#
# docs/CAMBIOS.md lleva su propio «Estado», mas detallado, y estaba igual de
# podrido que el del README: la misma flash de 472.832 y la misma RAM de
# 195.172. Arreglar uno y dejar el otro es volver a dejar un numero
# publicado sin nadie que lo mire, que es justo la enfermedad.
#
# Sus rotulos no son los del README -dice SRAM y no RAM, y trae ademas el
# cargador del 470-, asi que se escriben aparte en vez de forzar los dos a
# un formato comun: un cuaderno no tiene por que parecerse a un README.
#
def lineas_cuaderno(d, viejas):
    fuera = []
    for ln in viejas:
        c = ln.strip()
        if c.startswith('ZONA ALTA'):
            fuera.append('  ZONA ALTA  %s de %s    libre %s  <- %s'
                         % (mil(d['bd']), mil(d['bd_tope']),
                            mil(d['bd_tope'] - d['bd']),
                            desglose_bd(d['bd_secciones'])))
        elif c.startswith('FLASH 470'):
            fuera.append('  FLASH 470  %s de %s libre  %s   <- el mismo codigo enlazado con GD32F470VG_FLASH.ld'
                         % (mil(d['fw470']), mil(d['fw470_tope']),
                            mil(d['fw470_tope'] - d['fw470'])))
        elif c.startswith('FLASH '):
            fuera.append('  FLASH   %s de %s      libre   %s   <- la frontera bajo a 0x08008000 el 17/10'
                         % (mil(d['fw']), mil(d['fw_tope']), mil(d['fw_tope'] - d['fw'])))
        elif c.startswith('CARGADOR 470'):
            fuera.append('  CARGADOR 470 %s de %s   libre      %s'
                         % (mil(d['boot470']), mil(d['boot_tope']),
                            mil(d['boot_tope'] - d['boot470'])))
        elif c.startswith('CARGADOR ') and ' de ' in c:
            # "CARGADOR v2.19" es la VERSION, y de esa se ocupa
            # tools/version_check.py: aqui solo las que traen un tamaño.
            fuera.append('  CARGADOR %s de  %s      libre      %s   <- DOS sectores, no tres'
                         % (mil(d['boot']), mil(d['boot_tope']),
                            mil(d['boot_tope'] - d['boot'])))
        elif c.startswith('SRAM 470'):
            fuera.append('  SRAM 470   %s de %s   libre  %s   <- 448 kB contiguos desde 0x20000000'
                         % (mil(d['ram470']), mil(d['ram470_tope']),
                            mil(d['ram470_tope'] - d['ram470'])))
        elif c.startswith('SRAM '):
            fuera.append('  SRAM    %s de %s      libre    %s   <- .bss + .data + .noinit, como los cuenta tools/ram_mapa.py'
                         % (mil(d['ram']), mil(d['ram_tope']), mil(d['ram_tope'] - d['ram'])))
        elif c.startswith('TCM '):
            fuera.append('  TCM      %s de  %s      libre    %s   <- tools/gordos.py, y ese hueco ES la pila'
                         % (mil(d['tcm']), mil(d['tcm_tope']), mil(d['tcm_tope'] - d['tcm'])))
        elif c.startswith('PILA '):
            u, si, m, pc = d['pila']
            fuera.append('  PILA     %s de  %s        margen   %s (%d%%)  <- CON anidamiento'
                         % (mil(u), mil(si), mil(m), pc))
        else:
            fuera.append(ln)
    return fuera


def mira(ruta, d, hacer, arregla, titulo):
    """Compara un bloque «Estado» con los numeros de verdad. Devuelve fallos."""
    if not os.path.exists(ruta):
        print('  (%s no esta: no lo miro)' % os.path.basename(ruta))
        return 0
    txt = open(ruta, encoding='utf-8').read()
    m = re.search(r'(## Estado\n\n```\n)(.*?)(\n```)', txt, re.S)
    if not m:
        print('  no encuentro el bloque «Estado» de %s   MAL' % os.path.basename(ruta))
        return 1

    viejas = m.group(2).split('\n')
    nuevas = hacer(d, viejas)

    print('')
    print('  %s' % titulo)
    mal = 0
    for a, b in zip(viejas, nuevas):
        if a != b:
            mal += 1
            print('    dice   %s' % a.strip())
            print('    es     %s' % b.strip())
    if mal == 0:
        print('    los %d renglones cuadran  ok' % len(viejas))
        return 0
    if arregla:
        txt = txt[:m.start(2)] + '\n'.join(nuevas) + txt[m.end(2):]
        open(ruta, 'w', encoding='utf-8').write(txt)
        print('    %d renglon(es) arreglados' % mal)
        return 0
    return mal


def main():
    arregla = '--arregla' in sys.argv
    ruta = os.path.join(RAIZ, 'README.md')
    txt = open(ruta, encoding='utf-8').read()

    d = calcula()

    faltan = [k for k, v in d.items() if v is None]
    if faltan:
        print('')
        print('EL BLOQUE «Estado» DEL README')
        print('  no se puede comprobar: faltan %s' % ', '.join(sorted(faltan)))
        print('  (compila el firmware, el del 470 y el cargador primero)')
        return 0

    print('')
    print('LOS BLOQUES «Estado», CONTRA EL ENLAZADOR')
    mal  = mira(ruta, d, lineas_buenas, arregla, 'README.md (publicado)')
    mal += mira(os.path.join(RAIZ, '..', 'docs', 'CAMBIOS.md'),
                d, lineas_cuaderno, arregla, 'docs/CAMBIOS.md (el cuaderno)')

    if mal == 0:
        return 0
    print('')
    print('  %d renglon(es) MAL. Son numeros escritos: arreglalos con' % mal)
    print('      python3 tools/estado_check.py --arregla')
    return 1


if __name__ == '__main__':
    sys.exit(main())
