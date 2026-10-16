#!/usr/bin/env python3
"""
Abre con Pillow los BMP que escribio el banco y compara punto por punto.

POR QUE PILLOW Y NO UN LECTOR NUESTRO
-------------------------------------
Una cabecera de BMP son treinta y tantos numeros en un orden concreto, y
equivocarse en uno da un fichero que CASI funciona: se abre en un visor y
en otro no, o sale del reves, o con el rojo y el azul cambiados. Un lector
escrito por nosotros compartiria nuestras suposiciones y diria que todo
esta bien.

Pillow no sabe nada de este proyecto. Si el fichero le gusta a Pillow, le
va a gustar a Windows.

EL PATRON (el mismo que pone sim/bmptest.c)
  fila 0 entera ROJA, fila 255 entera AZUL     -> delata que salga del reves
  columna 0 VERDE, columna 319 BLANCA          -> lo mismo en horizontal
  el resto, cuatro barras verticales puras     -> delata colores cambiados

Bordes duros y colores puros a proposito: un degradado suave disimularia
justo esos tres fallos.
"""
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("  falta Pillow (pip install pillow)   MAL")

ANCHO, ALTO = 320, 256


def patron(x, y):
    if y == 0:            return (255, 0, 0)
    if y == ALTO - 1:     return (0, 0, 255)
    if x == 0:            return (0, 255, 0)
    if x == ANCHO - 1:    return (255, 255, 255)
    return [(248, 0, 0), (0, 252, 0), (0, 0, 248), (248, 252, 0)][(x * 4) // ANCHO]


def revisa(ruta, bits):
    global fallos
    try:
        im = Image.open(ruta)
    except Exception as e:                      # noqa: BLE001
        print(f"  {ruta}  Pillow no lo abre: {e}   MAL")
        fallos += 1
        return
    im = im.convert("RGB")
    if im.size != (ANCHO, ALTO):
        print(f"  {ruta}  mide {im.size} y deberia medir {(ANCHO, ALTO)}   MAL")
        fallos += 1
        return

    # A 16 bits se pierden bits por punto: 5 de rojo y azul, 6 de verde.
    # Ese error es del formato, no del codigo, asi que se admite - pero
    # justo ese y no mas.
    tol = 8 if bits == 16 else 0
    if bits == 8:
        # A 8 bits no hay color: se compara contra el gris del patron, con
        # la misma formula que uso el banco al escribirlo. Un punto de
        # margen por el redondeo entero.
        tol = 1
    px = im.load()
    malos = peor = 0
    donde = None
    for y in range(ALTO):
        for x in range(ANCHO):
            e = patron(x, y)
            if bits == 8:
                v = (e[0] * 77 + e[1] * 151 + e[2] * 28) >> 8
                e = (v, v, v)
            g = px[x, y]
            d = max(abs(a - b) for a, b in zip(e, g))
            if d > tol:
                malos += 1
                if d > peor:
                    peor, donde = d, (x, y, e, g)
    ok = malos == 0
    print(f"  {ruta}  Pillow: {im.size[0]}x{im.size[1]}, "
          f"{malos} puntos fuera de tolerancia (±{tol})   {'ok' if ok else 'MAL'}")
    if donde:
        x, y, e, g = donde
        print(f"      el peor en ({x},{y}): se puso {e} y se lee {g}")
    if not ok:
        fallos += 1


fallos = 0
revisa("out/bmp24.bmp", 24)
revisa("out/bmp16.bmp", 16)
revisa("out/bmp8.bmp", 8)
print("  TODO CORRECTO" if fallos == 0 else f"  {fallos} FALLOS")
sys.exit(1 if fallos else 0)
