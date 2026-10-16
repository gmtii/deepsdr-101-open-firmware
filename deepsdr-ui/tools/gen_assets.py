#!/usr/bin/env python3
"""
Generador de assets para el nuevo nucleo grafico del DEEPSDR 101.

Produce tablas C listas para compilar en el firmware:

  gfx2/font_*.h      fuentes proporcionales antialiasadas a 4 bpp
  gfx2/wf_cmap.h     LUT de 256 entradas del waterfall en RGB565
  gfx2/palette.h     la paleta de la UI en RGB565

Decisiones que se COMPRUEBAN aqui, no se suponen:
  - la rampa del waterfall sigue siendo monotona en luminosidad DESPUES
    de cuantizar a RGB565 (la cuantizacion puede romper la monotonia y
    entonces un aumento de senal se veria como una bajada);
  - el coste en flash de cada fuente se mide y se imprime.
"""

import os
import sys
import struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(__file__), "..", "gfx2")
os.makedirs(OUT, exist_ok=True)

# --------------------------------------------------------------------------
# Tipografias de origen
# --------------------------------------------------------------------------
# DejaVu Sans Condensed: licencia Bitstream Vera / DejaVu (permisiva, permite
#   incrustacion y redistribucion). Condensada porque las etiquetas de los
#   tiles y los badges van justas de ancho.
# DejaVu Sans Mono Bold: cifras tabulares para el lector de frecuencia -
#   imprescindible que los digitos no bailen de ancho al cambiar.
DEJAVU_C  = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf"
DEJAVU_CB = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf"
DEJAVU_MB = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
# Gothic japonesa: solo para los katakana de la lluvia de la pantalla de
# arranque. Es la fuente que trae el sistema para japones; no se usa en
# ninguna otra parte de la interfaz.
JP_GOTHIC = "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf"

ASCII = "".join(chr(c) for c in range(0x20, 0x7F))
SPANISH = "ºªáéíóúüñÁÉÍÓÚÜÑ¿¡°µ±·"
DIGITS = "0123456789.,:-+ kMHzdBm"

# --------------------------------------------------------------------------
# Pantalla de arranque
# --------------------------------------------------------------------------
# El titulo lleva SOLO sus diez letras. Una fuente completa a 60px serian
# ~90 KB de flash para una pantalla que se ve dos segundos; con el juego
# justo son 7,5 KB.
TITULO = "DeepSDRreload"
# Katakana para la lluvia. Media silabario: de sobra para que no se note el
# patron, y la mitad de flash que el silabario entero. Se incluyen tambien
# las cifras, que en la pelicula tambien caen.
KATAKANA = ("アイウエオカキクケコサシスセソタチツテト"
            "ナニヌネノハヒフヘホマミムメモ0123456789")

# DOS FUENTES QUITADAS EL 30/09/2026, y conviene saber por que estaban.
#
#     ("font_ui_24b",   DEJAVU_CB, 25, ASCII + SPANISH, "valores de detalle")
#     ("font_title_60", DEJAVU_MB, 62, TITULO,          "titulo del arranque")
#
# Entre las dos son 26,7 kB de datos generados y NO LAS USABA NADIE: las
# unicas menciones que quedaban en el firmware eran dos comentarios
# explicando que se habian dejado de usar (ui_kbd.c y splash.c). No costaban
# flash porque -ffunction-sections/-fdata-sections mas --gc-sections las
# tiraba al enlazar, y precisamente por eso llevaban meses ahi sin que nadie
# las echara de menos ni las pagara.
#
# El problema es que eran una trampa. El 29/09, al escribir la pantalla del
# QTH, se uso font_ui_24b para las seis casillas: los 16.956 bytes de su
# mapa de bits volvieron al binario y el desvan se paso 7.862 bytes, o sea
# que la compilacion fallo con un mensaje que no menciona ninguna fuente.
# Una fuente que no se usa no cuesta nada hasta que alguien la usa, y
# entonces cuesta un rato de buscar.
#
# Si vuelve a hacer falta una grande, se anade aqui y se mira el desvan.
FONTS = [
    # (nombre C, ruta ttf, px, charset, comentario)
    ("font_ui_14",    DEJAVU_C,  15, ASCII + SPANISH, "UI normal: etiquetas, unidades, texto secundario"),
    # Pedido por el usuario.
    #
    # Y cabian. Con font_ui_14 (line_height 18) tres filas son 54 px y la
    # franja que hay encima de la primera raya de la rejilla mide menos;
    # con esta, 12 px de cuerpo y line_height 14, tres filas son 42 y
    # entran. SOLO para los carteles del espectro: en cualquier otro sitio
    # de la pantalla esto se lee pequeño.
    ("font_ui_11",    DEJAVU_C,  11, ASCII + SPANISH, "Carteles del espectro: tres filas de nombres"),
    ("font_ui_14b",   DEJAVU_CB, 15, ASCII + SPANISH, "UI negrita: valores, badges activos"),
    ("font_ui_18b",   DEJAVU_CB, 19, ASCII + SPANISH, "UI negrita grande: botones y titulos de tile"),
    ("font_num_44",   DEJAVU_MB, 46, DIGITS,          "Lector de frecuencia: cifras TABULARES, no proporcionales"),
    ("font_num_20",   DEJAVU_MB, 21, DIGITS,          "Cifras tabulares medianas: dBm, S-meter, reloj"),
    ("font_rain_16",  JP_GOTHIC, 17, KATAKANA,        "Pantalla de arranque: katakana de la lluvia"),
]


def quant4(a):
    """alpha 0..255 -> 0..15 con redondeo al mas cercano."""
    return (a * 15 + 127) // 255


def render_font(name, path, px, charset, comment):
    # IMPORTANTE: los glyphs se emiten ORDENADOS POR CODEPOINT.
    # El indice de la tabla de rangos apunta al primer glyph del rango y
    # luego se accede por desplazamiento (first + cp - lo), asi que un
    # rango de codepoints contiguos TIENE que corresponder a glyphs
    # contiguos. Emitirlos en el orden en que aparecen en la cadena de
    # charset hacia que, por ejemplo, '.' resolviera al glyph de 'M'.
    charset = "".join(sorted(set(charset)))
    font = ImageFont.truetype(path, px)
    ascent, descent = font.getmetrics()
    line_h = ascent + descent

    glyphs = []
    blob = bytearray()

    for ch in charset:
        # bbox real de la tinta
        mask = font.getmask(ch, mode="L")
        w, h = mask.size
        bbox = font.getbbox(ch)
        advance = int(round(font.getlength(ch)))

        if w == 0 or h == 0:
            glyphs.append(dict(ch=ch, w=0, h=0, bx=0, by=0, adv=advance, off=len(blob)))
            continue

        bx = bbox[0]
        by = bbox[1]            # desde el top de la linea
        px_data = bytes(mask)

        off = len(blob)
        # empaquetado 4 bpp, dos pixeles por byte, fila a fila, alto nibble = pixel izquierdo
        for row in range(h):
            acc = 0
            nib = 0
            for col in range(w):
                v = quant4(px_data[row * w + col])
                if nib == 0:
                    acc = v << 4
                    nib = 1
                else:
                    blob.append(acc | v)
                    nib = 0
            if nib == 1:
                blob.append(acc)

        glyphs.append(dict(ch=ch, w=w, h=h, bx=bx, by=by, adv=advance, off=off))

    # --- emitir C ---
    hdr = os.path.join(OUT, name + ".h")
    with open(hdr, "w") as f:
        f.write(f"/*\n * {comment}\n *\n")
        f.write(f" * Generado por tools/gen_assets.py desde {os.path.basename(path)} a {px}px.\n")
        f.write(f" * Formato: 4 bpp (16 niveles de alpha), proporcional, empaquetado por filas,\n")
        f.write(f" * dos pixeles por byte (nibble alto = pixel izquierdo).\n")
        f.write(f" * ascent={ascent} descent={descent} line_height={line_h}\n")
        f.write(f" * {len(glyphs)} glyphs, {len(blob)} bytes de bitmap.\n")
        f.write(f" * NO EDITAR A MANO - regenerar con tools/gen_assets.py.\n */\n\n")
        f.write(f"#ifndef {name.upper()}_H\n#define {name.upper()}_H\n\n")
        f.write('#include "gfx2_font.h"\n\n')

        f.write(f"static const uint8_t {name}_bitmap[{len(blob)}] = {{\n")
        for i in range(0, len(blob), 16):
            row = ", ".join(f"0x{b:02X}" for b in blob[i:i + 16])
            f.write(f"    {row},\n")
        f.write("};\n\n")

        f.write(f"static const gfx2_glyph_t {name}_glyphs[{len(glyphs)}] = {{\n")
        for g in glyphs:
            disp = g["ch"] if 0x20 < ord(g["ch"]) < 0x7F and g["ch"] != "\\" else f"U+{ord(g['ch']):04X}"
            f.write(f"    {{ {g['off']:6d}, {g['w']:3d}, {g['h']:3d}, "
                    f"{g['bx']:4d}, {g['by']:4d}, {g['adv']:3d} }}, /* '{disp}' */\n")
        f.write("};\n\n")

        # mapa codepoint -> indice: un array disperso por rangos
        cps = sorted(ord(g["ch"]) for g in glyphs)
        idx_of = {ord(g["ch"]): i for i, g in enumerate(glyphs)}
        ranges = []
        start = prev = cps[0]
        for c in cps[1:]:
            if c == prev + 1:
                prev = c
            else:
                ranges.append((start, prev))
                start = prev = c
        ranges.append((start, prev))

        f.write(f"static const gfx2_range_t {name}_ranges[{len(ranges)}] = {{\n")
        for lo, hi in ranges:
            f.write(f"    {{ 0x{lo:04X}, 0x{hi:04X}, {idx_of[lo]} }},\n")
        f.write("};\n\n")

        f.write(f"static const gfx2_font_t {name} = {{\n")
        f.write(f"    {name}_bitmap, {name}_glyphs, {name}_ranges,\n")
        f.write(f"    {len(ranges)}, {ascent}, {descent}, {line_h}\n")
        f.write("};\n\n")
        f.write(f"#endif /* {name.upper()}_H */\n")

    total = len(blob) + len(glyphs) * 12 + len(ranges) * 6
    print(f"  {name:14s} {px:3d}px  {len(glyphs):3d} glyphs  "
          f"bitmap {len(blob):6d} B  total ~{total:6d} B  ({total/1024:.1f} KB)")
    return total


# --------------------------------------------------------------------------
# Colormap del waterfall
# --------------------------------------------------------------------------
def srgb_to_lin(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def oklab_L(r, g, b):
    """Luminosidad OKLab de un sRGB 0..255. Solo necesitamos L."""
    R, G, B = srgb_to_lin(r), srgb_to_lin(g), srgb_to_lin(b)
    l = 0.4122214708 * R + 0.5363325363 * G + 0.0514459929 * B
    m = 0.2119034982 * R + 0.6806995451 * G + 0.1073969566 * B
    s = 0.0883024619 * R + 0.2817188376 * G + 0.6299787005 * B
    l, m, s = l ** (1 / 3), m ** (1 / 3), s ** (1 / 3)
    return 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s


def to565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def from565(c):
    r = (c >> 11) & 0x1F
    g = (c >> 5) & 0x3F
    b = c & 0x1F
    return ((r << 3) | (r >> 2)), ((g << 2) | (g >> 4)), ((b << 3) | (b >> 2))


def gen_waterfall_cmap():
    """
    Rampa de calor semantica, monotona en luminosidad.

    Por que NO el arcoiris actual (azul->verde->amarillo->rojo): en jet/rainbow
    la luminosidad NO es monotona, asi que el ojo ve bordes y bandas donde la
    senal no cambia, y no ve cambios donde si los hay. Esta rampa es del tipo
    inferno/magma: negro -> purpura -> naranja -> amarillo claro, con L
    creciendo de forma estrictamente monotona. Arranca en el color del panel
    para que "sin senal" se funda con el fondo en vez de pintar un azul solido.
    """
    # puntos de control (posicion 0..1, RGB)  - estilo inferno recortado
    cps = [
        (0.00, (0x0D, 0x0F, 0x12)),   # fondo del panel: sin senal = nada
        (0.10, (0x1B, 0x14, 0x2E)),
        (0.25, (0x3E, 0x16, 0x51)),
        (0.40, (0x6B, 0x1C, 0x59)),
        (0.55, (0x99, 0x2B, 0x47)),
        (0.70, (0xC2, 0x47, 0x2C)),
        (0.82, (0xE0, 0x6F, 0x14)),
        (0.92, (0xF3, 0x9F, 0x22)),
        (1.00, (0xFC, 0xD8, 0x7A)),
    ]

    lut = []
    for i in range(256):
        t = i / 255.0
        for k in range(len(cps) - 1):
            t0, c0 = cps[k]
            t1, c1 = cps[k + 1]
            if t0 <= t <= t1:
                f = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
                r = round(c0[0] + (c1[0] - c0[0]) * f)
                g = round(c0[1] + (c1[1] - c0[1]) * f)
                b = round(c0[2] + (c1[2] - c0[2]) * f)
                lut.append(to565(r, g, b))
                break

    # --- comprobacion: monotonia en L DESPUES de cuantizar a RGB565 ---
    Ls = [oklab_L(*from565(c)) for c in lut]
    drops = [(i, Ls[i - 1], Ls[i]) for i in range(1, 256) if Ls[i] < Ls[i - 1] - 1e-9]
    worst = max((Ls[i - 1] - Ls[i] for i, _, _ in drops), default=0.0)

    print(f"  colormap waterfall: 256 entradas, L de {Ls[0]:.3f} a {Ls[-1]:.3f}")
    if drops:
        print(f"    {len(drops)} retrocesos de luminosidad por cuantizacion, "
              f"el mayor {worst:.5f}")
        if worst > 0.004:
            print("    *** FALLO: retroceso visible, hay que re-escalonar la rampa ***")
        else:
            print("    -> por debajo del umbral perceptible (0.004), aceptable")
    else:
        print("    -> estrictamente monotona, sin retrocesos")

    with open(os.path.join(OUT, "wf_cmap.h"), "w") as f:
        f.write("/*\n * LUT del waterfall: dB normalizado (0..255) -> RGB565.\n *\n")
        f.write(" * Rampa de calor semantica monotona en luminosidad (tipo inferno),\n")
        f.write(" * NO un arcoiris: en jet/rainbow la luminosidad no crece de forma\n")
        f.write(" * monotona, lo que crea bordes falsos donde la senal no cambia.\n")
        f.write(f" * Verificada tras cuantizar a RGB565: {len(drops)} retrocesos, "
                f"el mayor {worst:.5f} en L de OKLab.\n")
        f.write(" * El indice 0 es el color del panel, para que 'sin senal' se funda\n")
        f.write(" * con el fondo en vez de pintar un color solido.\n")
        f.write(" * NO EDITAR A MANO - regenerar con tools/gen_assets.py.\n */\n\n")
        f.write("#ifndef WF_CMAP_H\n#define WF_CMAP_H\n\n#include <stdint.h>\n\n")
        f.write("static const uint16_t wf_cmap[256] = {\n")
        for i in range(0, 256, 8):
            row = ", ".join(f"0x{c:04X}" for c in lut[i:i + 8])
            f.write(f"    {row},\n")
        f.write("};\n\n#endif /* WF_CMAP_H */\n")

    return 512, drops, worst


if __name__ == "__main__":
    print("Fuentes:")
    tot = 0
    for args in FONTS:
        tot += render_font(*args)
    print(f"  {'TOTAL fuentes':14s} {tot:6d} B  ({tot/1024:.1f} KB)")
    print()
    print("Colormap:")
    cmap_bytes, drops, worst = gen_waterfall_cmap()
    print()
    print(f"Coste total en flash de los assets: {(tot + cmap_bytes)/1024:.1f} KB")

# --------------------------------------------------------------------------
# Separar los datos de las cabeceras
# --------------------------------------------------------------------------
# Obligatorio, no opcional: sin esto cada .c que incluya una fuente se lleva
# su copia entera al binario. Ver tools/split_fonts.py.
import split_fonts  # noqa: E402

print()
print("Separando datos de cabeceras:")
split_fonts.split(OUT)
