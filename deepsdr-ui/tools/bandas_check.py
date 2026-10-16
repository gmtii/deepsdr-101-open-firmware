#!/usr/bin/env python3
"""
Comprueba que los limites numericos de k_band_presets[] dicen lo mismo que el
texto del rango que se pinta en pantalla.

Por que hace falta: la tabla guarda el rango DOS veces, una como texto
("7.000-7.200 kHz", que es lo que se ve) y otra como dos numeros en hercios
(que es lo que usa la pastilla de banda de la cabecera para saber en que banda
estas). Dos representaciones de lo mismo se separan solas; esto las ata.

El texto manda: los numeros se generan a partir de el (--generar) y despues se
comprueban contra el (por defecto). Asi el unico sitio que se edita a mano es
el que se lee en pantalla.
"""
import re, sys

UNIDAD = {"kHz": 1000.0, "MHz": 1000000.0, "Hz": 1.0}


def num(t):
    """'1.810' -> 1810.0, '135,7' -> 135.7 (puntos de millar, coma decimal)."""
    return float(t.replace(".", "").replace(",", "."))


# Un rango puede llevar delante una SIGLA ("OIRT 65,8-74 MHz"), 23/09/2026:
# las dos bandas de FM comercial perdieron las siglas del nombre -que es lo que
# sale tambien en la cabecera, donde no cabian- y se les quedaron aqui. La
# sigla se quita antes de mirar los numeros; lo que NO se relaja es la
# comprobacion en si, que sigue exigiendo que el rango escrito y los hercios de
# la tabla digan lo mismo.
SIGLA = re.compile(r"^\s*[A-Za-zÁÉÍÓÚÑáéíóúñ]{2,}\s+(?=[\d])")


def rango_a_hz(texto):
    """(lo_hz, hi_hz) del texto, o None si no es un rango (frecuencias sueltas)."""
    texto = SIGLA.sub("", texto)
    if "/" in texto:
        return None                      # "2,5 / 5 / 10 MHz": son puntos, no banda
    m = re.match(r"^\s*([\d.,]+)\s*-\s*([\d.,]+)\s*(kHz|MHz|Hz)\s*$", texto)
    if m:
        u = UNIDAD[m.group(3)]
        return (int(round(num(m.group(1)) * u)), int(round(num(m.group(2)) * u)))
    m = re.match(r"^\s*([\d.,]+)\s*(kHz|MHz|Hz)\s*$", texto)
    if m:
        v = int(round(num(m.group(1)) * UNIDAD[m.group(2)]))
        return (v, v)                    # una sola frecuencia: NAVTEX
    return None


# El nombre de la banda es un T("es","en") desde la traduccion (29/09/2026),
# y esta expresion pedia una cadena suelta: la tabla dejaba de leerse ENTERA
# y salia "no se ha podido leer la tabla de bandas". Se admiten las dos
# formas. El texto del rango (135,7-137,8 kHz) no se traduce y sigue suelto:
# son numeros y una unidad, iguales en los dos idiomas.
FILA = re.compile(
    r'\{\s*' + r'(?:T\s*\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)|"([^"]*)")'
    + r'\s*,\s*"([^"]+)"\s*,\s*(\d+)UL\s*,\s*(\d+)UL\s*,\s*(\d+)UL\s*,'
)


def main(ruta):
    src = open(ruta, encoding="utf-8").read()
    ini = src.index("static const band_preset_t k_band_presets[] = {")
    fin = src.index("\n};", ini)
    cuerpo = src[ini:fin]

    filas = FILA.findall(cuerpo)
    if not filas:
        print("  no se ha podido leer la tabla de bandas  MAL")
        return 1

    fallos = 0
    print("  %d bandas" % len(filas))
    for l_es, l_en, l_1, texto, lo, hi, centro in filas:
        label = l_es or l_1
        lo, hi, centro = int(lo), int(hi), int(centro)
        esperado = rango_a_hz(texto)
        if esperado is None:
            if (lo, hi) != (0, 0):
                print('  %-9s "%s" no es un rango, deberia ir con 0,0  MAL' % (label, texto))
                fallos += 1
            continue
        if (lo, hi) != esperado:
            print('  %-9s "%s" dice %s y la tabla pone %s  MAL'
                  % (label, texto, esperado, (lo, hi)))
            fallos += 1
        elif not (lo <= centro <= hi):
            print("  %-9s la frecuencia de entrada %d se sale de %d..%d  MAL"
                  % (label, centro, lo, hi))
            fallos += 1

    print("  %s" % ("HAY FALLOS" if fallos else
                    "los limites y el texto de cada banda dicen lo mismo"))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "../deepsdr/User/main.c"))
