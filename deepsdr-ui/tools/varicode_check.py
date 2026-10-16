#!/usr/bin/env python3
"""
Comprueba la tabla del varicode de PSK31 contra su fuente.

POR QUE ESTO EXISTE, y no una prueba mas en sim/psk31_aire.c:

El banco de aire fabrica la emision con una tabla y la lee con otra, pero
LAS DOS SALEN DEL MISMO SITIO. Si la tabla tuviera una errata, el banco la
aplicaria en los dos sentidos y se cancelaria - que es exactamente lo que
paso con el localizador de WSPR, donde una ida y vuelta perfecta escondio
durante semanas que el convenio estaba del reves. Solo se destapo con una
emision de verdad.

Asi que la tabla la mira alguien de FUERA: este script lee el .txt (que
esta comprobado identico contra fldigi, SDRangel y LinPSK) y el .c, y
comprueba que uno es el otro. Ademas comprueba las tres propiedades que
hacen que el varicode funcione, que son las que un error de transcripcion
rompe:

  - ningun codigo lleva dos ceros seguidos (el "00" es el separador)
  - todos empiezan y acaban en uno
  - las 128 claves son distintas entre si
"""
import re
import sys


def lee_txt(ruta):
    tabla = {}
    for ln in open(ruta, encoding='utf-8'):
        m = re.match(r'\s*0x([0-9A-Fa-f]{2})\s+(\S+)\s+"([01]+)"', ln)
        if m:
            tabla[int(m.group(1), 16)] = m.group(3)
    return tabla


def lee_c(ruta):
    s = open(ruta, encoding='utf-8').read()
    m = re.search(r'k_varicode\[128\]\s*=\s*\{(.*?)\};', s, re.S)
    if not m:
        return None
    return [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]+)U', m.group(1))]


def main():
    if len(sys.argv) != 3:
        print("uso: varicode_check.py <psk31_varicode.txt> <psk31.c>")
        return 2
    txt, c = sys.argv[1], sys.argv[2]
    mal = 0

    tabla = lee_txt(txt)
    claves = lee_c(c)

    if len(tabla) != 128:
        print("  el .txt no tiene 128 codigos, tiene %d  MAL" % len(tabla))
        return 1
    if claves is None or len(claves) != 128:
        print("  no encuentro k_varicode[128] en %s  MAL" % c)
        return 1

    for i in range(128):
        bits = tabla[i]
        if '00' in bits:
            print("  0x%02X lleva dos ceros seguidos: %s  MAL" % (i, bits)); mal += 1
        if bits[0] != '1' or bits[-1] != '1':
            print("  0x%02X no empieza y acaba en uno: %s  MAL" % (i, bits)); mal += 1
        esperada = (1 << len(bits)) | int(bits, 2)
        if claves[i] != esperada:
            print("  0x%02X  el .txt dice %s (clave %#05x) y el .c dice %#05x  MAL"
                  % (i, bits, esperada, claves[i]))
            mal += 1

    if len(set(claves)) != 128:
        print("  hay claves repetidas: solo %d distintas de 128  MAL" % len(set(claves)))
        mal += 1

    largos = [len(tabla[i]) for i in range(128)]
    print("  128 codigos, de %d a %d bits, todas las claves distintas"
          % (min(largos), max(largos)))
    print("  el .c dice lo mismo que el .txt en los 128" if mal == 0 else "")
    print("  TODO CORRECTO" if mal == 0 else "  HAY FALLOS (%d)" % mal)
    return 1 if mal else 0


sys.exit(main())
