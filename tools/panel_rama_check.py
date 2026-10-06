#!/usr/bin/env python3
"""
Comprueba que la rama B del panel sigue siendo la que esta probada.

    python3 panel_check.py boot/panel_tablas.h tools/panel_rama_b_oro.txt

POR QUE EXISTE - 05/10/2026.

La rama B es la que usa la radio del dueno. El 05/10 se rehicieron las tres
ramas sacandolas de ejecutar el boot.bin de serie -que estuvo bien, porque
dos de las tres estaban mal copiadas- y por el camino la B cambio: se le
anadio el desbloqueo que la captura anterior se habia dejado fuera.

Y la pantalla dejo de verse como se veia. *** El dueno: "con ese update a
mi se me ve oscura la pantalla", "los bordes de las letras salen mal", "de
todas ademas", "quiero que mi pantalla se vuelva a ver como se veia antes,
clarita". *** Tres entregas seguidas diciendo "esto es equivalente a lo que
tenias" y las tres equivocadas.

Equivalente no se dice: se compara. Esto compara, paso a paso y espera a
espera, lo que la tabla generada mandara al bus contra lo que mandaba el
cargador v2.2, que es la version mirada en la radio y dada por buena.

LO QUE NO COMPRUEBA: que la rama B sea la mejor. Solo que no ha cambiado
sin que nadie se entere. Si algun dia hay que cambiarla a proposito, se
regenera este fichero de oro Y se escribe por que en CAMBIOS.
"""
import re, sys

def del_header(ruta):
    """Los pasos de boot/panel_rama_b_v22.h, en el mismo formato que el oro."""
    s = open(ruta, encoding='utf-8').read()
    ini = s.index('static const paso_v22_t k_rama_B_v22[] = {')
    fin = s.index('\n};', ini)
    out = []
    for cmd, dat, tipo in re.findall(r'\{0x([0-9A-F]{4}), 0x([0-9A-F]{2}), (\d)\}', s[ini:fin]):
        t = int(tipo)
        if t == 0:   out.append('w %s %s' % (cmd, dat))
        elif t == 1: out.append('w %s -' % cmd)
        else:        out.append('ms %d' % int(dat, 16))
    return out


def del_oro(ruta):
    return [l.strip() for l in open(ruta, encoding='utf-8')
            if l.strip() and not l.startswith('#')]


def main(tablas, oro):
    a, b = del_header(tablas), del_oro(oro)
    print('  rama B: la tabla manda %d pasos, la probada tenia %d' % (len(a), len(b)))
    if a == b:
        print('  identica a lo que EJECUTA el cargador v2.2  ok')
        return 0
    import difflib
    print('  *** LA RAMA B HA CAMBIADO ***')
    for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, b, a).get_opcodes():
        if op == 'equal':
            continue
        print('    %s  probada[%d:%d] -> ahora[%d:%d]' % (op.upper(), i1, i2, j1, j2))
        if i2 > i1: print('       probada: %s' % ' | '.join(b[i1:i2][:6]))
        if j2 > j1: print('       ahora  : %s' % ' | '.join(a[j1:j2][:6]))
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'boot/panel_rama_b_v22.h',
                  sys.argv[2] if len(sys.argv) > 2 else 'tools/panel_rama_b_oro.txt'))
