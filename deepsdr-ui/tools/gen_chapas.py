#!/usr/bin/env python3
"""
Saca de main.c los textos de estado de las chapas de WEFAX, SSTV y
Feld-Hell, y los deja en una cabecera para que sim/chapa_fax.c los mida.

POR QUE GENERARLOS Y NO COPIARLOS - 19/10/2026
----------------------------------------------
Esa chapa recorta POR DELANTE lo que no le cabe, que en un contador es lo
que menos duele, pero en estas tres cabeceras lo de delante es la palabra
que dice en que fase va. Y recortado no avisa: no sale nada raro, sale
menos. En WEFAX "Esperando el tono de arranque" se leia "ono de arranque"
y la radio parecia estar diciendo otra cosa. Avisado por el usuario.

Asi que el banco los mide... pero solo sirve si mide LOS DE VERDAD. Una
lista copiada a mano en el banco es una lista que el dia que alguien
alargue un estado seguira diciendo que todo cabe.

Mismo motivo y mismo patron que tools/gen_rotulos.py.
"""
import re
import sys


# Las tres funciones de main.c que arman una chapa de estado a mano. Una
# cuarta cabecera de imagen significa anadirla aqui.
FUNCIONES = ("fax_hdr_draw", "sstv_hdr_draw", "hell_hdr_draw")


def cuerpo(src, nombre):
    m = re.search(r"static void " + nombre + r"\(void\)\s*\{", src)
    if not m:
        sys.exit("  no encuentro %s() en main.c   MAL" % nombre)
    i = src.index("{", m.start())
    prof = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            prof += 1
        elif src[j] == "}":
            prof -= 1
            if prof == 0:
                return src[i + 1:j]
    sys.exit("  %s() no cierra   MAL" % nombre)


def main():
    if len(sys.argv) != 3:
        sys.exit("uso: gen_chapas.py <main.c> <salida.h>")
    src = open(sys.argv[1], encoding="utf-8").read()

    textos = []
    for fn in FUNCIONES:
        c = cuerpo(src, fn)
        c = re.sub(r"/\*.*?\*/", "", c, flags=re.S)
        # Solo lo que acaba EN LA CHAPA. En estas tres funciones hay tambien
        # rotulos de boton, y un rotulo de boton no se mide contra el ancho
        # de la chapa: se mide contra el de su boton, que ya lo hace
        # sim/digi.c.
        for frase in c.split(";"):
            if not re.search(r"(^|[^\w.])e\s*=|s_digi\.chip\s*=", frase):
                continue
            # Los dos idiomas: el banco mide el mas ancho, que no siempre es
            # el castellano.
            for m in re.finditer(r'tr\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*'
                                 r'"((?:[^"\\]|\\.)*)"\s*\)', frase):
                for t in (m.group(1), m.group(2)):
                    if t and t not in textos:
                        textos.append(t)

    if not textos:
        sys.exit("  no he sacado ni un estado   MAL")

    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write("/* GENERADO por tools/gen_chapas.py desde User/main.c.\n"
                " * No editar: se rehace en cada 'make comprueba'. */\n"
                "#ifndef CHAPAS_TABLA_H\n#define CHAPAS_TABLA_H\n\n"
                "static const char *const k_chapas_estado[] = {\n")
        for t in textos:
            f.write('    "%s",\n' % t)
        f.write("};\n\n#endif\n")
    print("  %d estados de las chapas de imagen sacados de main.c" % len(textos))


if __name__ == "__main__":
    main()
