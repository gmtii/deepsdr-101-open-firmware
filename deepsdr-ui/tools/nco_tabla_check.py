#!/usr/bin/env python3
"""
Comprueba que User/nco_tabla.c es lo que gen_nco_tabla.py genera.

La tabla de senos del NCO esta generada y vive en flash. Un fichero generado
que alguien edita a mano es una bomba de relojeria: compila, pasa
desapercibido y un dia el seno no es un seno. Esto lo regenera en memoria y
lo compara byte a byte.

Y ademas mide el error de la tabla CONTRA LA FUNCION DE VERDAD, con la misma
interpolacion lineal que hace nco.c, porque lo que importa no es que el
fichero cuadre consigo mismo sino que el oscilador sea un oscilador.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_nco_tabla


def main(base):
    ruta = os.path.join(base, 'User/nco_tabla.c')
    if not os.path.exists(ruta):
        print('  FALTA %s' % ruta)
        return 1
    hay = open(ruta, encoding='utf-8').read()
    toca = gen_nco_tabla.texto()
    if hay != toca:
        print('  FALLA: User/nco_tabla.c no es lo que genera tools/gen_nco_tabla.py')
        print('         (se regenera con: python3 tools/gen_nco_tabla.py User/nco_tabla.c)')
        return 1
    print('  nco_tabla.c: %d puntos, generado y sin tocar a mano' % (gen_nco_tabla.N + 1))

    # Y el error real de la interpolacion, que es para lo que existe.
    v = gen_nco_tabla.tabla()
    peor = 0.0
    n = 200000
    for i in range(n):
        x = 2.0 * math.pi * i / n
        p = x / (2.0 * math.pi) * gen_nco_tabla.N
        k = int(p)
        f = p - k
        y = v[k] + f * (v[k + 1] - v[k])
        peor = max(peor, abs(y - math.sin(x)))
    print('  peor error de la interpolacion: %.2e' % peor)
    if peor > 5e-6:
        print('  FALLA: la tabla interpolada se desvia mas de 5e-6')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '.'))
