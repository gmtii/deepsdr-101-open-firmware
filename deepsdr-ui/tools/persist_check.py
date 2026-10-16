#!/usr/bin/env python3
"""
Comprueba que los ajustes que se guardan por tabla estan en LOS CUATRO sitios.

La tabla de settings.h vive en cuatro trozos de codigo que el compilador NO
puede cruzar entre si:

  1. el enum settings_extra_id_t           (settings.h)
  2. k_extra_claves[]                      (settings.c)   - protegido ademas
                                                            por un _Static_assert
                                                            que solo cuenta filas
  3. extras_leer()                         (main.c)
  4. extras_aplicar()                      (main.c)

Olvidar una linea en (3) guarda basura sin avisar; olvidarla en (4) guarda
bien y no restaura nada. Ninguna de las dos da error de compilacion, y son
exactamente la clase de fallo que en este proyecto ya ha mordido cinco veces.
Esto es lo que hace de compilador.
"""
import re
import sys


def main(base):
    h = open(base + '/User/settings.h', encoding='utf-8').read()
    c = open(base + '/User/settings.c', encoding='utf-8').read()
    m = open(base + '/User/main.c', encoding='utf-8').read()

    cuerpo = re.search(r'typedef enum \{(.*?)\} settings_extra_id_t;', h, re.S)
    if not cuerpo:
        print("  *** no encuentro settings_extra_id_t en settings.h ***")
        return 1
    # SIN COMENTARIOS - 30/09/2026. Los identificadores se cuentan sobre el
    # codigo, no sobre lo que haya escrito alrededor: el enum lleva comentarios
    # largos que explican por que cada clave entro donde entro, y nombrar en
    # uno de ellos otra clave del propio enum bastaba para que esta cuenta
    # saliera con una fila de mas y el orden desplazado. Un banco al que se le
    # puede cambiar el veredicto escribiendo prosa no mide el codigo.
    sin_com = re.sub(r'/\*.*?\*/', '', cuerpo.group(1), flags=re.S)
    sin_com = re.sub(r'//[^\n]*', '', sin_com)
    ids = [x for x in re.findall(r'\b(SET_X_[A-Z0-9_]+)\b', sin_com) if x != 'SET_X_N']

    tabla = re.search(r'k_extra_claves\[\]\s*=\s*\{(.*?)\n\};', c, re.S)
    claves = re.findall(r'"([a-z_0-9]+)"', tabla.group(1)) if tabla else []
    # El comentario de cada fila dice a que id corresponde: asi se comprueba
    # el ORDEN, no solo la cuenta (que ya la cubre el _Static_assert).
    orden = re.findall(r'/\*\s*(SET_X_[A-Z0-9_]+)\s*\*/', tabla.group(1)) if tabla else []

    def ids_en(fn, src):
        cu = re.search(r'\b' + fn + r'\(const settings_extra_t \*e\)\s*\n\{(.*?)\n\}\n', src, re.S)
        if cu is None:
            cu = re.search(r'\b' + fn + r'\(settings_extra_t \*e\)\s*\n\{(.*?)\n\}\n', src, re.S)
        return set(re.findall(r'\b(SET_X_[A-Z0-9_]+)\b', cu.group(1))) if cu else None

    leer = ids_en('extras_leer', m)
    aplicar = ids_en('extras_aplicar', m)

    print("ajustes guardados por tabla: %d" % len(ids))
    fallos = 0

    if len(claves) != len(ids):
        print("  *** k_extra_claves[] tiene %d filas y el enum %d ***" % (len(claves), len(ids)))
        fallos += 1
    if orden and orden != ids:
        print("  *** el orden de k_extra_claves[] no es el del enum ***")
        for i, (a, b) in enumerate(zip(orden + [None] * len(ids), ids + [None] * len(orden))):
            if a != b:
                print("      fila %d: la tabla dice %s y el enum %s" % (i, a, b))
        fallos += 1
    elif not orden:
        print("  *** las filas de k_extra_claves[] han perdido su comentario /* SET_X_... */,")
        print("      que es lo unico que ata cada clave a su id ***")
        fallos += 1

    for nombre, conj in (('extras_leer', leer), ('extras_aplicar', aplicar)):
        if conj is None:
            print("  *** no encuentro %s() en main.c ***" % nombre)
            fallos += 1
            continue
        faltan = [i for i in ids if i not in conj]
        if faltan:
            print("  *** %s() no toca: %s ***" % (nombre, ', '.join(faltan)))
            fallos += 1
        sobran = [i for i in conj if i not in ids]
        if sobran:
            print("  *** %s() usa ids que no existen: %s ***" % (nombre, ', '.join(sobran)))
            fallos += 1

    if len(set(claves)) != len(claves):
        print("  *** hay claves repetidas en k_extra_claves[] ***")
        fallos += 1

    # Una clave de tabla no puede llamarse igual que una con nombre propio:
    # el parser mira las de nombre propio primero y la de tabla nunca se leeria.
    propias = set(re.findall(r'append_str\(buf,\s*p,\s*buf_size,\s*"([a-z_0-9]+),"\)', c))
    choque = sorted(set(claves) & propias)
    if choque:
        print("  *** estas claves de tabla chocan con una de nombre propio: %s ***" % ', '.join(choque))
        fallos += 1

    if not fallos:
        print("  el enum, la tabla de claves, extras_leer() y extras_aplicar() cuadran")
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '../deepsdr'))
