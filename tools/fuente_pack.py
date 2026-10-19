#!/usr/bin/env python3
"""
SACA UNA FUENTE DEL FIRMWARE Y LA DEJA EN UN FICHERO PARA BD.BIN.

  python3 tools/fuente_pack.py font_rain_16 build/SPLF.BIN

Y SE ESCRIBE EN build/, NO EN LA RAIZ - 19/10/2026, pedido por el usuario.

Aqui ponia "SPLF.BIN" a secas, y el fichero que salia se quedaba en la raiz
del repositorio. No lo lee nadie: lo que se copia al disco de la radio es
BD.BIN, que ya lleva esta seccion dentro (hoy, en el offset 512.000). O sea
que era un binario duplicado a mano, que es justo la enfermedad que persigue
tools/copias_bin_check.py: el dia que alguien toque font_rain_16, BD.BIN se
regenera y esa copia se queda vieja y callada, y el siguiente que arme un
BD.BIN con ella mete la fuente de anteayer.

Es un intermedio, asi que vive donde los intermedios. El simulador ya lo
hacia bien: el suyo sale en deepsdr-ui/out/SPLF.BIN (ver local.mk).

POR QUE - 17/10/2026.

font_rain_16 son 5.592 bytes del firmware -45 glifos de katakana- y los
usa UNA pantalla: la lluvia del arranque. Se ve tres segundos al encender
y no vuelve a aparecer. En una radio a la que le quedaban 2.720 bytes
libres, eso es el doble de lo que queda, parado en flash interna para una
animacion.

La zona alta de la flash SPI, en cambio, tiene sitio de sobra y su
directorio admite cuatro secciones de las que hoy se usan dos (ver
User/zona_alta.h). Asi que la fuente se va ahi, como una seccion mas de
BD.BIN, y el firmware la lee al arrancar.

SI NO ESTA, NO PASA NADA. splash.c ya sabia degradarse: cuando no hay
memoria que prestarle pinta la pantalla fija en vez de la lluvia, y eso
es lo que hara tambien cuando no encuentre la fuente. Una radio con un
BD.BIN viejo arranca igual, solo que sin animacion.

EL FORMATO, pensado para leerse de un tiron y apuntar dentro.

    0   'S','P','L','F'
    4   u8  version = 1
    5   u8  ascent
    6   u8  descent
    7   u8  line_h
    8   u16 cuantos glifos
   10   u16 cuantos rangos
   12   u32 bytes de bitmap
   16   los glifos, 12 bytes cada uno, EXACTAMENTE como gfx2_glyph_t los
        quiere en memoria: off u32, w u8, h u8, bx i8, by i8, adv u8 y
        tres de relleno. Asi el que lo carga no convierte nada: lee el
        bloque y le apunta un gfx2_glyph_t *
        despues los rangos, 6 bytes cada uno (lo, hi, first), que es
        igual que gfx2_range_t
        y al final el bitmap

El relleno de los glifos NO es decorativo: gfx2_glyph_t mide 12 bytes en
el ARM por el alineado del uint32_t de delante. Guardar 9 y "ya se
expandira" obligaria a copiar glifo a glifo en el arranque y a tener los
dos formatos en la cabeza. Doce bytes x 45 glifos son 540: no merece la
pena ahorrar 135 a cambio de un bucle de conversion.
"""
import os
import re
import struct
import sys

FUENTE_C = 'User/fonts_data.c'


def tabla(txt, nombre, patron):
    i = txt.index(nombre)
    j = txt.index('};', i)
    return re.findall(patron, txt[i:j])


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    fuente, salida = sys.argv[1], sys.argv[2]
    txt = open(FUENTE_C, encoding='utf-8').read()

    glifos = [tuple(int(x) for x in g) for g in tabla(
        txt, '%s_glyphs' % fuente,
        r'\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}')]
    rangos = [(int(a, 16), int(b, 16), int(c)) for a, b, c in tabla(
        txt, '%s_ranges' % fuente,
        r'\{\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*(\d+)\s*\}')]

    i = txt.index('%s_bitmap[' % fuente)
    j = txt.index('};', i)
    bitmap = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', txt[i:j]))

    m = re.search(r'const\s+gfx2_font_t\s+%s\s*=\s*\{'
                  r'\s*\w+\s*,\s*\w+\s*,\s*\w+\s*,'
                  r'\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,?\s*\}'
                  % re.escape(fuente), txt)
    if m is None:
        print('  no se puede leer la cabecera de %s' % fuente)
        return 1
    n_ranges, ascent, descent, line_h = (int(x) for x in m.groups())
    if n_ranges != len(rangos):
        print('  MAL: la cabecera dice %d rangos y la tabla trae %d'
              % (n_ranges, len(rangos)))
        return 1

    out = bytearray()
    out += b'SPLF'
    out += struct.pack('<BBBBHHI', 1, ascent, descent, line_h,
                       len(glifos), len(rangos), len(bitmap))
    for off, w, h, bx, by, adv in glifos:
        if not (0 <= w < 256 and 0 <= h < 256 and -128 <= bx < 128
                and -128 <= by < 128 and 0 <= adv < 256):
            print('  MAL: un glifo no cabe en el formato: %r'
                  % ((off, w, h, bx, by, adv),))
            return 1
        if off + ((w + 1) // 2) * h > len(bitmap):
            print('  MAL: un glifo se sale del bitmap')
            return 1
        out += struct.pack('<IBBbbB3x', off, w, h, bx, by, adv)
    for lo, hi, first in rangos:
        if first + (hi - lo) >= len(glifos):
            print('  MAL: el rango U+%04X..U+%04X apunta fuera de la tabla'
                  % (lo, hi))
            return 1
        out += struct.pack('<HHH', lo, hi, first)
    out += bitmap

    open(salida, 'wb').write(bytes(out))
    print('  %s -> %s' % (fuente, salida))
    print('    %d glifos, %d rangos, %d bytes de bitmap' %
          (len(glifos), len(rangos), len(bitmap)))
    print('    ascent %d  descent %d  alto de linea %d' % (ascent, descent, line_h))
    print('    el fichero mide %d bytes' % len(out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
