#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE LOS ANILLOS DE TIERRA DE UN .map SEAN TIERRA. 14/10/2026.

Para rellenar hace falta saber que lado es tierra, y eso lo decide
mapa_pack.py marcando los trazos cerrados con ANILLO_BIT. Esto lo lee del
fichero YA ESCRITO -no de las estructuras de Python que lo generaron, que es
donde se esconden los errores que el propio generador no ve- y comprueba
cuatro cosas:

  1. que los trazos marcados CIERREN de verdad (primer punto = ultimo). Un
     anillo abierto por un punto derrama el relleno por toda la pantalla, y
     eso puede pasar despues de cuantizar: el bucle de cuantiza() salta los
     puntos repetidos y se puede comer el cierre.
  2. que ninguno se pase del bit, o sea que la cuenta de puntos y las dos
     marcas convivan.
  3. que las fronteras NO vayan marcadas como anillo: una frontera no
     encierra tierra, encierra un pais, y rellenarla pintaria el mar de
     Francia.
  4. Y LA QUE IMPORTA: cuanta superficie encierran. En un mapa
     equirectangular el grado cuadrado ES area de pantalla, asi que la suma
     de las areas se puede comparar con cuanta tierra hay de verdad.

EL MARGEN DE ESE ULTIMO, Y POR QUE ES ANCHO. La cifra conocida -la tierra es
el 29 % de la superficie del planeta- es de area REAL, y esto son grados
cuadrados, que estiran las latitudes altas. Ademas el mundo esta recortado a
+-71,5 grados, o sea casi sin Antartida, que en grados cuadrados pesaria
muchisimo. No se puede exigir un numero exacto sin inventarselo, asi que el
margen es 15-35 %: lo bastante estrecho para cazar las dos averias de verdad
-rellenar el mundo entero (sale 1.076 %, me paso) o no rellenar nada- y lo
bastante ancho para no fingir una precision que no se tiene.
"""
import struct, sys

MARCA = b'DSDRMAP1'
CAB, NIV_MAX, NIV_CAB = 60, 4, 16
CUANT = 32.0
LAT_TOPE = 71.5
FRONTERA_BIT = 0x8000


def trazos(cuerpo, bits=b''):
    """Descodifica la tira: (puntos, frontera, anillo) por trazo.

    EL ANILLO NO SALE DE LA CUENTA DE PUNTOS sino de `bits`, el mapa de
    bits que va pegado detras del cero de cierre. Ver mapa_pack.py. La
    cuenta vuelve a enmascararse con 0x7FFF, que es lo que un lector de
    version 2 entiende, y ese es justo el punto.
    """
    p = 0
    idx = 0
    while p + 2 <= len(cuerpo):
        (h,) = struct.unpack('<H', cuerpo[p:p+2]); p += 2
        if h == 0:
            break
        n = h & 0x7FFF
        fr = bool(h & FRONTERA_BIT)
        an = bool(bits[idx >> 3] & (1 << (idx & 7))) if (idx >> 3) < len(bits) else False
        idx += 1
        x, y = struct.unpack('<hh', cuerpo[p:p+4]); p += 4
        pts = [(x, y)]
        for _ in range(n - 1):
            (b0,) = struct.unpack('<b', cuerpo[p:p+1])
            if b0 == -128:
                p += 1
                dx, dy = struct.unpack('<hh', cuerpo[p:p+4]); p += 4
            else:
                (dy,) = struct.unpack('<b', cuerpo[p+1:p+2]); p += 2
                dx = b0
            x += dx; y += dy
            pts.append((x, y))
        yield pts, fr, an


def area(r):
    s = 0.0
    for i in range(len(r) - 1):
        s += r[i][0] * r[i+1][1] - r[i+1][0] * r[i][1]
    return s * 0.5 / (CUANT * CUANT)


def main():
    if len(sys.argv) != 2:
        print('uso: mapa_anillos_check.py <fichero.map>')
        return 2
    d = open(sys.argv[1], 'rb').read()
    if d[:8] != MARCA:
        print('  %s no es un .map  MAL' % sys.argv[1]); return 1

    n_niv = d[9]
    estilo = d[57]
    fallos = 0
    nombres = {0: 'contorno', 1: 'tierra rellena', 2: 'tierra de puntos',
               3: 'silueta'}
    rellena = estilo != 0

    print('\nLOS ANILLOS DE TIERRA (%s)' % sys.argv[1])
    print('  estilo %d: %s  ->  %s' %
          (estilo, nombres.get(estilo, '???'),
           'tiene que traer anillos' if rellena else 'NO debe traer anillos'))

    # EL ESTILO MANDA, Y POR LOS DOS LADOS. Un mapa de contorno con anillos
    # dejaria que la radio rellenara un mapa cuyo nombre promete lo
    # contrario; y uno de tierra rellena sin anillos es la averia de hoy,
    # la que hace que 013, 014 y 015 sean el mismo fichero. Las dos son
    # fallo, y por eso esto no se limita a contar.

    for i in range(n_niv):
        zoom, nx, ny, off, largo, piezas, n_bits = struct.unpack(
            '<HBBIIHH', d[CAB + i*NIV_CAB: CAB + (i+1)*NIV_CAB])
        if nx != 0:
            continue                      # troceado: aqui no hay anillos
        # El mapa de bits va al final del nivel, dentro de `largo`.
        cuerpo = d[off: off + largo - n_bits]
        bits = d[off + largo - n_bits: off + largo]
        anillos, lineas, fronteras = [], 0, 0
        abiertos = fronteras_marcadas = 0

        for pts, fr, an in trazos(cuerpo, bits):
            if fr:
                fronteras += 1
                if an:
                    fronteras_marcadas += 1
            elif an:
                anillos.append(pts)
                if pts[0] != pts[-1]:
                    abiertos += 1
            else:
                lineas += 1

        print('\n  nivel x%d (entero)' % zoom)
        print('    anillos de tierra   %5d' % len(anillos))
        print('    costas sin cerrar   %5d' % lineas)
        print('    fronteras           %5d' % fronteras)

        if rellena and not anillos:
            print('    *** MAL: el estilo dice relleno y no hay ni un anillo ***')
            fallos += 1
            continue
        if not rellena:
            if anillos:
                print('    *** MAL: %d anillos en un mapa de contorno ***'
                      % len(anillos))
                fallos += 1
            else:
                print('    sin anillos, como pide el estilo        ok')
            continue

        if abiertos:
            print('    *** MAL: %d marcados como anillo NO cierran ***' % abiertos)
            fallos += 1
        else:
            print('    todos cierran (primer punto = ultimo)   ok')

        if fronteras_marcadas:
            print('    *** MAL: %d fronteras marcadas como anillo ***'
                  % fronteras_marcadas)
            fallos += 1
        else:
            print('    ninguna frontera va marcada             ok')

        tot = sum(abs(area(r)) for r in anillos)
        ventana = 360.0 * (2 * LAT_TOPE)
        pct = 100.0 * tot / ventana
        print('    encierran %.0f grados^2 de %.0f  ->  %.1f %%'
              % (tot, ventana, pct))
        if 15.0 <= pct <= 35.0:
            print('    y eso es tierra, no el mundo entero     ok')
        else:
            print('    *** MAL: %.1f %% no es la tierra ***' % pct)
            fallos += 1

    print('\n%s (%d fallos)\n' % ('HAY FALLOS' if fallos else 'TODO CORRECTO', fallos))
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
