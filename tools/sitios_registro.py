#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SITIOS_XY.CSV CONTRA LA POSICION QUE REGISTRO LA PROPIA EMISORA - 26/10/2026.

  python3 sitios_registro.py            informa
  python3 sitios_registro.py --arregla  y ademas corrige lo que toca

DE DONDE SALE ESTO. sitios_xy.csv lo armo un GEOCODIFICADOR al 70 % de
confianza (ver su cabecera y la de sitios_check.py). El 26/10/2026 entraron
en el repositorio dos tablas que no existian entonces: site.txt de HFCC y
el Aoki ancho (xta26.txt). Las dos traen la posicion que la PROPIA EMISORA
registro para su transmisor. O sea que por primera vez hay con que
comparar.

Y la comparacion duele: 34 filas se iban mas de 150 km, y varias mas de
mil. "Colorado De" caia en el condado de Colorado, en TEXAS, en vez de en
Colorado; "Al-Maqwa" de Kuwait caia en el Yemen; "Start Point" caia en las
Orcadas en vez de en Devon. Son los fallos tipicos de pedirle a un
geocodificador un nombre de doce caracteres.

PERO EL REGISTRO TAMPOCO ES LA VERDAD REVELADA, y esto se aprendio
rompiendolo. La primera version de este guion cogia la posicion del
registro siempre que se diferenciara mas de 150 km, y movio "Karatsu" de
Japon a China. No era un fallo del emparejamiento: el Aoki ancho dice de
Karatsu 332645N1195929E, y eso son 119 grados de longitud cuando el sitio
esta en 129. Un digito. La tabla de mano tenia razon y la lista no, que es
exactamente para lo que la cabecera de sitios_xy.csv dice que existe esa
tabla.

LA GUARDA, ENTONCES: no se cree a ninguna de las dos por ser quien es. Se
mide LA DISTANCIA AL CENTROIDE DEL PAIS de la emisora -el ITU que trae
aoki.txt para ese sitio- y gana la que cae mas cerca. Con eso Karatsu se
queda donde estaba (787 km del centro de Japon contra 1.668 de la lista) y
Al-Maqwa se corrige (1.420 km del centro de Kuwait contra 24).

No es un criterio perfecto -para China o Estados Unidos el centroide esta
lejos de todo- pero no hace falta que lo sea: solo tiene que desempatar
entre dos candidatas concretas, y cuando las dos estan igual de lejos la
diferencia no decide nada y se queda lo que habia.

LO QUE NO HACE: inventar filas. Si un sitio no esta en sitios_xy.csv, este
guion no lo mete. Las posiciones del registro para los sitios que no estan
aqui las usa emisoras_pack.py directamente (--hfcc y --aoki-xy), sin pasar
por esta tabla.
"""
import collections
import io
import math
import os
import re
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
DATOS = os.path.join(AQUI, 'datos')
LEJOS = 150.0          # por debajo de esto no hay nada que discutir


def km(a, b):
    r = 6371.0
    p1, p2 = math.radians(a[0]), math.radians(b[0])
    dp, dl = p2 - p1, math.radians(b[1] - a[1])
    h = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2.0 * r * math.asin(min(1.0, math.sqrt(h)))


def lee_csv(ruta):
    """prefijo -> (lat, lon). Se salta comentarios y filas sin coordenadas."""
    t = {}
    for l in io.open(ruta, encoding='utf-8'):
        s = l.strip()
        if not s or s.startswith('#'):
            continue
        c = l.rstrip('\n').split(',')
        if len(c) < 3:
            continue
        try:
            t[c[0].strip()] = (float(c[1]), float(c[2]))
        except ValueError:
            pass
    return t


def grados_hm(txt):
    """'43N17' / '077E00' -> grados decimales (el formato de HFCC)."""
    m = re.match(r'^(\d+)([NSEW])(\d+)$', txt.strip())
    if not m:
        return None
    v = float(m.group(1)) + float(m.group(3)) / 60.0
    return -v if m.group(2) in 'SW' else v


def grados_hms(txt):
    """'372221N1405056E' -> (lat, lon) (el formato del Aoki ancho)."""
    m = re.match(r'^(\d{2})(\d{2})(\d{2})([NS])(\d{3})(\d{2})(\d{2})([EW])$',
                 txt.strip())
    if not m:
        return None
    la = int(m.group(1)) + int(m.group(2)) / 60.0 + int(m.group(3)) / 3600.0
    lo = int(m.group(5)) + int(m.group(6)) / 60.0 + int(m.group(7)) / 3600.0
    if m.group(4) == 'S':
        la = -la
    if m.group(8) == 'W':
        lo = -lo
    return (la, lo)


def registro():
    """nombre de sitio -> [(lat, lon, fuente, nombre)] de las dos tablas."""
    t = collections.defaultdict(list)
    ruta = os.path.join(DATOS, 'aoki_xy.txt')
    if os.path.exists(ruta):
        for l in io.open(ruta, encoding='cp1252', errors='replace'):
            l = l.rstrip('\n\r')
            if len(l) < 129 or l.startswith('FRE'):
                continue
            nom = l[85:109].strip()
            xy = grados_hms(l[113:129])
            if nom and xy:
                t[nom].append((xy[0], xy[1], 'Aoki', nom))
    ruta = os.path.join(DATOS, 'hfcc_sitios.txt')
    if os.path.exists(ruta):
        for l in io.open(ruta, encoding='cp1252', errors='replace'):
            l = l.rstrip('\n\r')
            if not l or l.startswith(';') or len(l) < 42:
                continue
            nom = l[4:34].strip()
            la, lo = grados_hm(l[39:44]), grados_hm(l[45:51])
            if nom and la is not None and lo is not None:
                t[nom].append((la, lo, 'HFCC', nom))
    return t


def itu_por_prefijo(claves):
    """El codigo ITU que aoki.txt le da a cada prefijo de la tabla.

    El emparejamiento es el mismo que hace donde() en emisoras_pack.py: el
    prefijo mas largo que case. Si fuera otro, este guion estaria juzgando
    un sitio con el pais de otro.
    """
    orden = sorted(claves, key=len, reverse=True)
    t = collections.defaultdict(collections.Counter)
    ruta = os.path.join(DATOS, 'aoki.txt')
    if not os.path.exists(ruta):
        return t
    for l in io.open(ruta, encoding='cp1252', errors='replace'):
        if not re.match(r'^\d', l) or len(l) < 75:
            continue
        sit, it = l[63:75].strip().lower(), l[30:34].strip()
        for k in orden:
            if sit.startswith(k.lower()):
                t[k][it] += 1
                break
    return t


def main():
    arregla = '--arregla' in sys.argv
    ruta = os.path.join(AQUI, 'sitios_xy.csv')
    csv = lee_csv(ruta)
    paises = lee_csv(os.path.join(AQUI, 'paises_xy.csv'))
    reg = registro()
    if not reg:
        print('  (no estan las tablas del registro en datos/: no hay con que'
              ' comparar)')
        return 0
    itu = itu_por_prefijo(csv)

    print('SITIOS_XY.CSV CONTRA LA POSICION REGISTRADA')
    print('  %d sitios en la tabla, %d en el registro' % (len(csv), len(reg)))

    corrige, deja, sin_pais = [], [], []
    for k in sorted(csv):
        v = csv[k]
        cand = reg.get(k) or [r for nom, rs in reg.items()
                              if nom.startswith(k) for r in rs]
        if not cand:
            continue
        mejor = min(cand, key=lambda r: km(v, (r[0], r[1])))
        d = km(v, (mejor[0], mejor[1]))
        if d <= LEJOS:
            continue
        it = itu[k].most_common(1)[0][0] if itu.get(k) else ''
        centro = paises.get(it.upper())
        if centro is None:
            sin_pais.append((k, it, d, mejor))
            continue
        d_csv = km(v, centro)
        d_reg = km((mejor[0], mejor[1]), centro)
        (corrige if d_reg <= d_csv else deja).append(
            (k, it, d, d_csv, d_reg, mejor))

    if corrige:
        print('\n  EL REGISTRO CAE MAS CERCA DE SU PAIS: se corrige')
        print('    %-14s %-4s %9s %9s %9s  %s'
              % ('prefijo', 'ITU', 'se mueve', 'csv->pais', 'reg->pais', 'fuente'))
        for k, it, d, a, b, m in corrige:
            print('    %-14s %-4s %8.0f km %8.0f km %8.0f km  %s %s'
                  % (k, it, d, a, b, m[2], m[3][:20]))
    if deja:
        print('\n  LA TABLA DE MANO CAE MAS CERCA: se queda como esta')
        print('    (es para lo que existe: ver la cabecera de sitios_xy.csv)')
        for k, it, d, a, b, m in deja:
            print('    %-14s %-4s %8.0f km %8.0f km %8.0f km  %s %s'
                  % (k, it, d, a, b, m[2], m[3][:20]))
    if sin_pais:
        print('\n  SIN CENTROIDE CON QUE DESEMPATAR: no se tocan')
        for k, it, d, m in sin_pais:
            print('    %-14s %-4s %8.0f km  %s %s' % (k, it or '?', d, m[2], m[3][:20]))

    if not arregla:
        print('\n  %d se corregirian, %d se quedan. Con --arregla se escriben.'
              % (len(corrige), len(deja) + len(sin_pais)))
        return 0

    cambios = {k: m for k, _it, _d, _a, _b, m in corrige}
    lineas = io.open(ruta, encoding='utf-8').read().split('\n')
    n = 0
    for i, l in enumerate(lineas):
        s = l.strip()
        if not s or s.startswith('#'):
            continue
        c = l.rstrip('\n').split(',')
        if len(c) < 3 or c[0].strip() not in cambios:
            continue
        k = c[0].strip()
        m = cambios[k]
        lineas[i] = '%s,%.6f,%.6f,%s  [registro %s; el geocodificador lo ponia a %.0f km]' \
                    % (k, m[0], m[1], m[3], m[2],
                       km(csv[k], (m[0], m[1])))
        n += 1
    io.open(ruta, 'w', encoding='utf-8').write('\n'.join(lineas))
    print('\n  %d filas escritas en %s' % (n, os.path.basename(ruta)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
