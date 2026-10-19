#!/usr/bin/env python3
"""
RECORTA UNA FUENTE A LOS CARACTERES QUE DE VERDAD SE PINTAN - 17/10/2026.

POR QUE EXISTE. El cargador de arranque lleva font_ui_18b entera -117
glifos, 11.462 bytes- y pinta 68 caracteres. Los otros 49 son acentos,
mayusculas acentuadas y signos que no aparecen en ninguna de sus cadenas.
Son 5.206 bytes de los 41.212 que ocupa el cargador, y el cargador esta
peleando por bajar de 32.768 para caber en dos sectores de flash en vez
de tres (ver boot/GD32F450VE_BOOT.ld).

RECORTA, NO REGENERA. Se podria volver a sacar la fuente del TTF con otra
lista de caracteres, y seria mas corto de escribir. Pero entonces los
glifos que quedan dependerian de la version de freetype, del redondeo del
hinting y de la luna: la pantalla CAMBIARIA sin que nadie lo hubiera
pedido. Esto copia los bytes que ya estan, tal cual, y tira los demas. Lo
que queda es identico bit a bit a lo que habia, y eso se comprueba al
final de este mismo guion.

COMO SE SABE QUE CARACTERES HACEN FALTA. Se leen de los ficheros fuente
que se le pasen: todas las cadenas entrecomilladas de su codigo. Mas los
digitos y el hexadecimal en mayuscula, que no salen en ninguna cadena
porque los escriben u2s() y x2s() cifra a cifra, mas el espacio y la '?',
que es el glifo que letra() pinta cuando no encuentra otro.

   python3 tools/poda_fuente.py font_ui_14b boot/font_boot boot/*.c boot/*.h

SI EL CARGADOR GANA UN TEXTO NUEVO con una letra que no estaba, esa letra
sale como '?'. Por eso el guion imprime la lista y por eso hay que
volverlo a pasar cuando se toque un texto del cargador: es generado, no
escrito a mano, y el fichero lo dice en su primera linea.
"""
import os, re, sys

FUENTE_C = 'User/fonts_data.c'

def lee_tabla(txt, nombre, patron):
    i = txt.index(nombre)
    j = txt.index('};', i)
    return re.findall(patron, txt[i:j])

def lee_bitmap(txt, nombre):
    i = txt.index('%s[' % nombre)
    j = txt.index('};', i)
    return [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', txt[i:j])]

def lee_cab(nombre):
    """n_ranges, ascent, descent, line_h de la gfx2_font_t de la fuente.

    SE LEE LA LISTA ENTERA Y NO "LOS ULTIMOS TRES NUMEROS", QUE FUE EL
    PRIMER INTENTO Y ESTABA MAL DOS VECES. Uno: los NOMBRES llevan
    digitos -font_ui_14b_bitmap tiene un 14- y entraban en la cuenta.
    Dos: el corte en '};' dejaba fuera el ultimo numero, que es
    justamente line_h. Con los dos fallos juntos, la 18b devolvia
    ascent 22, descent 18, line_h 5 en vez de 18, 5, 23: el cargador
    habria salido con las lineas a cinco pixeles unas de otras.

    Asi que se busca la forma entera -tres nombres y luego cuatro
    numeros- y se comprueba que son cuatro."""
    txt = open(FUENTE_C, encoding='utf-8').read()
    m = re.search(r'const\s+gfx2_font_t\s+%s\s*=\s*\{'
                  r'\s*\w+\s*,\s*\w+\s*,\s*\w+\s*,'
                  r'\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,?\s*\}'
                  % re.escape(nombre), txt)
    if m is None:
        raise SystemExit('  no se puede leer la cabecera de %s' % nombre)
    n_ranges, ascent, descent, line_h = (int(x) for x in m.groups())
    return ascent, descent, line_h

def caracteres(ficheros):
    LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
    ch = set(' ?0123456789ABCDEF')
    for f in ficheros:
        t = open(f, encoding='utf-8', errors='replace').read()
        t = re.sub(r'/\*.*?\*/', ' ', t, flags=re.S)
        t = re.sub(r'//[^\n]*', ' ', t)
        for m in LIT.finditer(t):
            s = m.group(1)
            s = (s.replace('\\n', '').replace('\\t', '').replace('\\"', '"')
                  .replace('\\\\', '\\').replace('\\r', ''))
            ch.update(s)
    return {c for c in ch if c.isprintable() and ord(c) < 0x110000}

def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    fuente, destino, ficheros = sys.argv[1], sys.argv[2], sys.argv[3:]
    txt = open(FUENTE_C, encoding='utf-8').read()

    glifos = [tuple(int(x) for x in g) for g in lee_tabla(
        txt, '%s_glyphs' % fuente,
        r'\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}')]
    rangos = [(int(a, 16), int(b, 16), int(c)) for a, b, c in lee_tabla(
        txt, '%s_ranges' % fuente,
        r'\{\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*(\d+)\s*\}')]
    bitmap = lee_bitmap(txt, '%s_bitmap' % fuente)
    ascent, descent, line_h = lee_cab(fuente)

    cp2idx = {}
    for lo, hi, first in rangos:
        for k, cp in enumerate(range(lo, hi + 1)):
            cp2idx[cp] = first + k

    quiero = caracteres(ficheros)
    tengo  = sorted(cp for cp in (ord(c) for c in quiero) if cp in cp2idx)
    faltan = sorted(c for c in quiero if ord(c) not in cp2idx)

    # El bitmap nuevo: los glifos que quedan, en orden de codepoint.
    sal_bitmap, sal_glifos, sal_rangos = [], [], []
    for cp in tengo:
        off, w, h, bx, by, adv = glifos[cp2idx[cp]]
        n = ((w + 1) // 2) * h
        trozo = bitmap[off:off + n]
        assert len(trozo) == n, 'el bitmap se queda corto en U+%04X' % cp
        sal_glifos.append((len(sal_bitmap), w, h, bx, by, adv))
        sal_bitmap.extend(trozo)
    # Rangos: tramos de codepoints seguidos.
    i = 0
    while i < len(tengo):
        j = i
        while j + 1 < len(tengo) and tengo[j + 1] == tengo[j] + 1:
            j += 1
        sal_rangos.append((tengo[i], tengo[j], i))
        i = j + 1

    # COMPROBACION: cada glifo que queda tiene que ser el mismo byte a byte.
    for k, cp in enumerate(tengo):
        off_v, w, h, bx, by, adv = glifos[cp2idx[cp]]
        off_n = sal_glifos[k][0]
        n = ((w + 1) // 2) * h
        if bitmap[off_v:off_v + n] != sal_bitmap[off_n:off_n + n]:
            print('  MAL: U+%04X no sale igual' % cp)
            return 1
        if sal_glifos[k][1:] != (w, h, bx, by, adv):
            print('  MAL: la metrica de U+%04X ha cambiado' % cp)
            return 1

    base = os.path.basename(destino)
    with open(destino + '.c', 'w', encoding='utf-8') as f:
        f.write('/* GENERADO por tools/poda_fuente.py desde %s.\n'
                ' * NO EDITAR A MANO: se vuelve a generar cuando cambie un texto\n'
                ' * del cargador, porque la lista de caracteres sale de sus cadenas.\n'
                ' * %d de los %d glifos de la fuente original, mismos pixeles. */\n'
                % (fuente, len(tengo), len(glifos)))
        f.write('#include "%s.h"\n\n' % base)
        f.write('const uint8_t %s_bitmap[%d] = {\n' % (base, len(sal_bitmap)))
        for k in range(0, len(sal_bitmap), 16):
            f.write('    ' + ', '.join('0x%02X' % b for b in sal_bitmap[k:k+16]) + ',\n')
        f.write('};\n\n')
        f.write('const gfx2_glyph_t %s_glyphs[%d] = {\n' % (base, len(sal_glifos)))
        for (off, w, h, bx, by, adv), cp in zip(sal_glifos, tengo):
            f.write('    { %5d, %3d, %3d, %3d, %3d, %3d },   /* U+%04X %s */\n'
                    % (off, w, h, bx, by, adv, cp,
                       chr(cp) if 32 < cp < 127 else ' '))
        f.write('};\n\n')
        f.write('const gfx2_range_t %s_ranges[%d] = {\n' % (base, len(sal_rangos)))
        for lo, hi, first in sal_rangos:
            f.write('    { 0x%04X, 0x%04X, %3d },\n' % (lo, hi, first))
        f.write('};\n\n')
        f.write('const gfx2_font_t %s = {\n'
                '    %s_bitmap, %s_glyphs, %s_ranges, %d, %d, %d, %d\n};\n'
                % (base, base, base, base, len(sal_rangos), ascent, descent, line_h))

    with open(destino + '.h', 'w', encoding='utf-8') as f:
        f.write('/* GENERADO por tools/poda_fuente.py desde %s. NO EDITAR A MANO. */\n'
                '#ifndef %s_H\n#define %s_H\n#include "gfx2_font.h"\n\n'
                'extern const uint8_t %s_bitmap[%d];\n'
                'extern const gfx2_glyph_t %s_glyphs[%d];\n'
                'extern const gfx2_range_t %s_ranges[%d];\n'
                'extern const gfx2_font_t %s;\n\n#endif\n'
                % (fuente, base.upper(), base.upper(),
                   base, len(sal_bitmap), base, len(sal_glifos),
                   base, len(sal_rangos), base))

    viejo = (len(bitmap) + len(glifos) * 12 + len(rangos) * 6 + 16)
    nuevo = (len(sal_bitmap) + len(sal_glifos) * 12 + len(sal_rangos) * 6 + 16)
    print('  %s -> %s' % (fuente, base))
    print('    glifos   %3d -> %3d' % (len(glifos), len(tengo)))
    print('    bitmap %5d -> %5d' % (len(bitmap), len(sal_bitmap)))
    print('    total  %5d -> %5d   (%d bytes menos)' % (viejo, nuevo, viejo - nuevo))
    print('    los %d glifos que quedan son identicos byte a byte  ok' % len(tengo))
    print('    ascent %d  descent %d  alto de linea %d  (los de la fuente original)'
          % (ascent, descent, line_h))
    if line_h < ascent:
        print('    MAL: un alto de linea menor que el ascent no puede ser')
        return 1
    if faltan:
        print('    OJO: estos caracteres se piden y la fuente no los tiene: %s'
              % ''.join(sorted(faltan)))
    return 0

if __name__ == '__main__':
    sys.exit(main())
