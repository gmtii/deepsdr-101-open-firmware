#!/usr/bin/env python3
"""
EL FILTRO DEL REMUESTREADOR DE FT8, QUE ESTABA SIN GENERADOR.

User/ft8_decimator.c lleva una tabla de coeficientes con una nota que dice
"generated offline (Python, windowed-sinc design)"... y el Python no estaba
en ningun sitio. O sea que la tabla no se podia ni rehacer ni comprobar: si
alguien cambiaba L o la frecuencia de corte, habia que creerse los numeros.

Esto es ese Python. Y lo primero que hace es DEMOSTRAR que es el mismo:

    python3 tools/gen_ft8_fir.py --comprueba ../User/ft8_decimator.c

reconstruye la tabla con los parametros que declara ese fichero y la compara
coeficiente a coeficiente con la que lleva dentro.

Para escribir una nueva:

    python3 tools/gen_ft8_fir.py --l 8 --corte 3200

EL DISEÑO, que es el de siempre y no se ha tocado: sinc enventanado con
Blackman, N = L * 120 coeficientes, corte referido a la tasa VIRTUAL de
12.000*L Hz (la de despues de interpolar), y ganancia L para compensar lo
que pierde el relleno de ceros. Al final se reparte en L fases, cogiendo
uno de cada L empezando en r, que es lo que pide la forma polifasica
directa.
"""
import argparse
import math
import re
import sys

TASA_ENTRADA = 12000.0   # la que entrega demod_am()
TAPS_POR_FASE = 120


def disena(L, corte_hz, taps_por_fase=TAPS_POR_FASE):
    n_total = L * taps_por_fase
    tasa_virtual = TASA_ENTRADA * L
    centro = (n_total - 1) / 2.0
    h = []
    for n in range(n_total):
        x = n - centro
        if abs(x) < 1e-12:
            s = 2.0 * corte_hz / tasa_virtual
        else:
            s = math.sin(2.0 * math.pi * corte_hz / tasa_virtual * x) / (math.pi * x)
        w = (0.42
             - 0.50 * math.cos(2.0 * math.pi * n / (n_total - 1))
             + 0.08 * math.cos(4.0 * math.pi * n / (n_total - 1)))
        h.append(s * w)
    suma = sum(h)
    return [v * (L / suma) for v in h]


def texto(L, corte_hz, taps_por_fase=TAPS_POR_FASE):
    h = disena(L, corte_hz, taps_por_fase)
    out = ['#define FT8_RESAMPLER_TAPS_PER_PHASE %uU' % taps_por_fase,
           'static const float32_t s_resampler_phase'
           '[FT8_RESAMPLER_L][FT8_RESAMPLER_TAPS_PER_PHASE] = {']
    for r in range(L):
        out.append('{')
        out.append('    /* phase %d */' % r)
        fila = ['%.9ef' % h[r + L * j] for j in range(taps_por_fase)]
        for k in range(0, taps_por_fase, 4):
            out.append('    ' + ', '.join(fila[k:k + 4])
                       + (',' if k + 4 < taps_por_fase else ''))
        out.append('},' if r < L - 1 else '}')
    out.append('};')
    return '\n'.join(out) + '\n'


def lee_parametros(ruta_c, ruta_h):
    """L y los taps salen del .c/.h; el corte, de L (la banda util es la mitad
       de la tasa de salida, o sea 12000*L/M/2 ... que con M=15 es 400*L)."""
    fuente_h = open(ruta_h, encoding='utf-8').read()
    fuente_c = open(ruta_c, encoding='utf-8').read()
    L = int(re.search(r'#define\s+FT8_RESAMPLER_L\s+(\d+)', fuente_h).group(1))
    M = int(re.search(r'#define\s+FT8_RESAMPLER_M\s+(\d+)', fuente_h).group(1))
    tp = int(re.search(r'#define\s+FT8_RESAMPLER_TAPS_PER_PHASE\s+(\d+)U',
                       fuente_c).group(1))
    salida = TASA_ENTRADA * L / M
    return L, M, tp, salida / 2.0


def coeficientes_del_fichero(ruta_c, L, tp):
    fuente = open(ruta_c, encoding='utf-8').read()
    i = fuente.index('s_resampler_phase')
    j = fuente.index('};', i)
    nums = re.findall(r'-?\d+\.\d+e[+-]\d+f', fuente[i:j])
    if len(nums) != L * tp:
        raise SystemExit('el fichero trae %d coeficientes y se esperaban %d'
                         % (len(nums), L * tp))
    return [float(v[:-1]) for v in nums]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--comprueba', metavar='FICHERO_C')
    ap.add_argument('--cabecera', default=None)
    ap.add_argument('--l', type=int, default=8)
    ap.add_argument('--corte', type=float, default=3200.0)
    a = ap.parse_args()

    if a.comprueba:
        ruta_h = a.cabecera or a.comprueba.replace('.c', '.h')
        L, M, tp, corte = lee_parametros(a.comprueba, ruta_h)
        esperado = disena(L, corte, tp)
        # en el mismo orden en que estan escritos: fase r, coeficiente j
        esperado = [esperado[r + L * j] for r in range(L) for j in range(tp)]
        hay = coeficientes_del_fichero(a.comprueba, L, tp)
        peor = max(abs(x - y) for x, y in zip(esperado, hay))
        # la tabla esta escrita con 9 decimales, asi que lo que se exige es
        # que coincida hasta donde se escribio
        tope = 1e-9 * max(1.0, max(abs(v) for v in esperado))
        print('  L=%d M=%d, %d coeficientes, corte %.0f Hz' % (L, M, L * tp, corte))
        print('  la mayor diferencia es %.3e (tope %.3e)   %s'
              % (peor, tope, 'ok' if peor <= tope else '*** MAL ***'))
        return 0 if peor <= tope else 1

    sys.stdout.write(texto(a.l, a.corte))
    return 0


if __name__ == '__main__':
    sys.exit(main())
