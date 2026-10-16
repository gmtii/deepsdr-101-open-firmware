#!/usr/bin/env python3
"""
Saca de main.c los rotulos de los botones del panel digital y los deja en
una cabecera para que sim/digi.c los mida.

POR QUE GENERARLOS Y NO COPIARLOS
---------------------------------
Un rotulo que no cabe en su boton no avisa: se pinta encima del de al lado
y hay que verlo para enterarse. Asi que el banco los mide todos... pero
solo sirve si mide LOS DE VERDAD. Una lista copiada a mano en el banco es
una lista que el dia que alguien cambie "No guardar" por "No guardar nada"
seguira diciendo que todo cabe.

Mismo motivo y mismo patron que tools/gen_ajustes.py y tools/gen_modos.py.
"""
import re
import sys


# Las tablas de main.c cuyos textos acaban en el segundo boton. Añadir un
# modo nuevo que guarde significa añadir aqui su tabla: si se olvida, el
# banco sigue diciendo que todo cabe y el rotulo nuevo se pinta encima del
# vecino sin que nadie se entere. Paso el 24/09/2026 con k_wfx_guarda.
TABLAS = ("k_sstv_guarda", "k_sstv_gsv_estado", "k_wfx_guarda")


def saca(src, nombre):
    m = re.search(nombre + r"\[[^\]]*\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        sys.exit(f"  no encuentro {nombre}[] en main.c   MAL")
    cuerpo = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    return re.findall(r'"((?:[^"\\]|\\.)*)"', cuerpo)


def main():
    if len(sys.argv) != 3:
        sys.exit("uso: gen_rotulos.py <main.c> <salida.h>")
    src = open(sys.argv[1], encoding="utf-8").read()
    rot = []
    for t in TABLAS:
        rot += saca(src, t)
    if not rot:
        sys.exit("  no he sacado ni un rotulo   MAL")

    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write("/* GENERADO por tools/gen_rotulos.py desde User/main.c.\n"
                " * No editar: se rehace en cada 'make comprueba'. */\n"
                "#ifndef ROTULOS_TABLA_H\n#define ROTULOS_TABLA_H\n\n"
                "static const char *const k_rotulos_btn2[] = {\n")
        for r in rot:
            f.write(f'    "{r}",\n')
        f.write("};\n\n#endif\n")
    print(f"  {len(rot)} rotulos del segundo boton sacados de main.c")


if __name__ == "__main__":
    main()
