#!/usr/bin/env python3
"""
Convierte la costa de Natural Earth en la tabla que dibuja la radio.

  python3 mapa_pack.py ne_110m_coastline.json mapa_datos.c

DE DONDE SALE. Natural Earth, escala 1:110.000.000, dominio publico
(naturalearthdata.com). El fichero de partida trae 134 lineas y 5.128
puntos en coordenadas de grados.

QUE SE LE HACE, Y POR QUE:

1. SE RECORTA A +-71,5 GRADOS de latitud. No es por ahorrar: es que el
   panel mide 800x318 pixeles y un mapa entero (360x180 grados) necesita
   proporcion 2:1, o sea 800x400. Recortando a 143 grados de latitud la
   proporcion sale 2,52:1 y encaja clavada en los 318 que hay. Lo que se
   pierde es la Antartida y el alto Artico, donde no hay nadie emitiendo
   en HF. De paso se lleva por delante casi mil puntos.

2. SE SIMPLIFICA con Douglas-Peucker a 0,35 grados. A 800 pixeles de
   ancho, un grado de longitud son 2,2 pixeles, asi que 0,35 grados es
   menos de un pixel: la simplificacion NO se ve. Poner menos tolerancia
   solo gasta flash en detalle que el panel no puede dibujar.

3. SE TIRAN LAS ISLAS QUE NO SE VERIAN: las que ocupan menos de dos
   pixeles de lado. Dibujarlas seria pintar un punto suelto en mitad del
   mar, que se lee como un error, no como una isla.

4. Y SE GUARDA EN CENTESIMAS DE GRADO, que caben en un entero de 16 bits
   con signo (+-18000 y +-9000) y dan una resolucion de 0,01 grados: casi
   cuarenta veces mas fina que un pixel.

La tabla va a la seccion `.arriba` -el desvan por encima de la firma-
porque son datos de solo lectura y el codigo no cabe ahi.
"""
import json
import math
import sys

LAT_TOPE = 71.5      # ver el punto 1 de arriba
TOL = 0.35           # grados; menos de un pixel a 800 de ancho
MIN_PX = 2.0         # islas mas pequeñas que esto, fuera
ANCHO_PX = 800.0


def dp(pts, tol):
    """Douglas-Peucker, iterativo para no reventar la pila."""
    if len(pts) < 3:
        return pts
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    pila = [(0, len(pts) - 1)]
    while pila:
        a, b = pila.pop()
        if b <= a + 1:
            continue
        x1, y1 = pts[a]
        x2, y2 = pts[b]
        dx, dy = x2 - x1, y2 - y1
        n = math.hypot(dx, dy)
        peor, idx = 0.0, -1
        for i in range(a + 1, b):
            x, y = pts[i]
            if n == 0:
                d = math.hypot(x - x1, y - y1)
            else:
                d = abs(dy * x - dx * y + x2 * y1 - y2 * x1) / n
            if d > peor:
                peor, idx = d, i
        if peor > tol and idx > 0:
            keep[idx] = True
            pila.append((a, idx))
            pila.append((idx, b))
    return [p for p, k in zip(pts, keep) if k]


def recorta_lat(linea):
    """Parte la linea en los trozos que caen dentro de la banda de
    latitud. Partir y no solo tirar: una costa que entra y sale por
    arriba -Noruega, Alaska- tiene que seguir dibujandose por el trozo
    que si se ve."""
    trozos, act = [], []
    for lon, lat in linea:
        if -LAT_TOPE <= lat <= LAT_TOPE:
            act.append((lon, lat))
        else:
            if len(act) >= 2:
                trozos.append(act)
            act = []
    if len(act) >= 2:
        trozos.append(act)
    return trozos


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    d = json.load(open(sys.argv[1], encoding='utf-8'))

    crudas = []
    for f in d['features']:
        g = f['geometry']
        if g['type'] == 'LineString':
            crudas.append(g['coordinates'])
        elif g['type'] == 'MultiLineString':
            crudas.extend(g['coordinates'])
    n0 = sum(len(l) for l in crudas)
    print("  de partida: %d lineas, %d puntos" % (len(crudas), n0))

    trozos = []
    for l in crudas:
        trozos.extend(recorta_lat(l))
    print("  recortadas a +-%.1f grados: %d lineas, %d puntos"
          % (LAT_TOPE, len(trozos), sum(len(t) for t in trozos)))

    fuera = 0
    lineas = []
    for t in trozos:
        s = dp(t, TOL)
        if len(s) < 2:
            continue
        lons = [p[0] for p in s]
        lats = [p[1] for p in s]
        ancho_px = (max(lons) - min(lons)) * ANCHO_PX / 360.0
        alto_px = (max(lats) - min(lats)) * ANCHO_PX / 360.0
        if ancho_px < MIN_PX and alto_px < MIN_PX:
            fuera += 1
            continue
        lineas.append(s)
    n = sum(len(l) for l in lineas)
    print("  simplificadas a %.2f grados: %d lineas, %d puntos "
          "(%d islas invisibles fuera)" % (TOL, len(lineas), n, fuera))

    bytes_tot = 4 * n + 2 * len(lineas) + 4
    print("  la tabla ocupa %d bytes (%.1f kB)" % (bytes_tot, bytes_tot / 1024.0))

    with open(sys.argv[2], 'w', encoding='utf-8') as o:
        o.write('/* GENERADO POR tools/mapa_pack.py - NO EDITAR A MANO.\n'
                ' *\n'
                ' * La costa del mundo, de Natural Earth 1:110M (dominio publico,\n'
                ' * naturalearthdata.com), recortada a +-%.1f grados de latitud y\n'
                ' * simplificada a %.2f grados con Douglas-Peucker.\n'
                ' *\n'
                ' * %d lineas, %d puntos, en centesimas de grado. Ver la cabecera\n'
                ' * del script para por que esos numeros y no otros.\n'
                ' */\n' % (LAT_TOPE, TOL, len(lineas), n))
        o.write('#include "mapa.h"\n\n')
        o.write('const int16_t k_mapa_pts[] MAPA_DESVAN = {\n')
        for l in lineas:
            o.write('    %d,\n' % len(l))
            for i in range(0, len(l), 6):
                o.write('    ' + ' '.join(
                    '%d,%d,' % (int(round(p[0] * 100)), int(round(p[1] * 100)))
                    for p in l[i:i + 6]) + '\n')
        o.write('    0\n};\n\n')
        o.write('const uint16_t k_mapa_n_lineas = %dU;\n' % len(lineas))
        o.write('const uint16_t k_mapa_n_puntos = %dU;\n' % n)
        o.write('const int16_t  k_mapa_lat_tope_c = %d;   '
                '/* centesimas de grado */\n' % int(LAT_TOPE * 100))
    print("  escrito %s" % sys.argv[2])
    return 0


sys.exit(main())
