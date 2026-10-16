#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE EL DIBUJO NO SE ENGANCHE AL AUDIO.

  python3 vaiven_check.py <User/main.c>

POR QUE EXISTE - 06/10/2026.

Pedido por el usuario.

EL ENGANCHE. sdr_spectrum_waterfall_tick() vuelve entera si no ha llegado un
bloque de audio:

    if (sdr_rx_poll_block_iq(...) == 0U) { return; }

Asi que el dibujo SOLO PUEDE EMPEZAR en el instante en que llega un bloque. Y
el plazo del cuadro siguiente se cuenta desde ESE instante. Resultado: el
tiempo entre cuadros es siempre un numero ENTERO de bloques, y no se suelta
nunca.

Con 22 ms pedidos salen 24,0 ms: NUEVE bloques clavados. Y entonces todos los
armonicos de la perturbacion del repintado caen ENCIMA de los multiplos de
375 Hz -la tasa de bloques- donde el oido los junta en un tono. Eso es el
siseo: no es mas energia, es energia APILADA.

Medido en el audio del usuario antes del arreglo: pico de +31,6 dB sobre el
fondo en 750 Hz, con bandas laterales separadas 41,75 Hz, que son esos 24,0
ms exactos.

LA REGLA: el plazo del cuadro siguiente no puede ser una constante. Tiene que
llevar un termino que varie, o el enganche vuelve.

Este guion mira que la linea que pone s_next_frame_ms no sea simplemente
`g_msticks + SPECTRUM_FRAME_MS`, y ADEMAS simula la secuencia de instantes de
dibujo con la regla de arriba para enseñar cuanto se concentra la energia en
multiplos de 375 Hz con y sin vaiven.

Lo que NO comprueba: que el vaiven sea bastante. Eso lo dice la simulacion de
abajo, que se imprime siempre.
"""
import io
import re
import sys
import math


def simula(frame_ms, vaiven_ms):
    """Los instantes de dibujo, con la regla real: solo se pinta al llegar
    un bloque. Devuelve el pico en multiplos de 375 Hz sobre el fondo."""
    BLOQUE = 1000.0 / 375.0
    FS = 48000.0
    DUR = 20.0
    t = []
    prox = 0.0
    rnd = 0x2F6E2B1
    k = 0
    n = 2 * vaiven_ms + 1
    while k * BLOQUE < DUR * 1000.0:
        ahora = k * BLOQUE
        if ahora >= prox:
            t.append(ahora / 1000.0)
            if vaiven_ms > 0:
                rnd = (rnd * 1103515245 + 12345) & 0xFFFFFFFF
                prox = ahora + frame_ms + ((rnd >> 16) % n) - vaiven_ms
            else:
                prox = ahora + frame_ms
        k += 1
    # tren de impulsos -> espectro, sin numpy
    N = 1 << 15
    xs = [0.0] * N
    for v in t:
        i = int(v * FS)
        if i < N:
            xs[i] = 1.0
    # DFT solo en las frecuencias que interesan y en un peine de fondo
    def mag(hz):
        w = 2.0 * math.pi * hz / FS
        re = im = 0.0
        for i, v in enumerate(xs):
            if v:
                re += math.cos(w * i)
                im -= math.sin(w * i)
        return math.hypot(re, im)
    pk = max(20.0 * math.log10(mag(375.0 * kk) + 1e-12) for kk in range(1, 9))
    fondo = sorted(20.0 * math.log10(mag(f) + 1e-12)
                   for f in (137.0, 311.0, 523.0, 907.0, 1303.0, 1709.0))[3]
    bloques = sorted(set(round((t[i + 1] - t[i]) * 1000.0 / BLOQUE)
                         for i in range(len(t) - 1)))
    return pk - fondo, len(t) / DUR, [int(b) for b in bloques]


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    src = io.open(sys.argv[1], encoding='utf-8', errors='replace').read()
    src = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'),
                 src, flags=re.S)
    src = re.sub(r'//[^\n]*', '', src)

    sitios = list(re.finditer(r's_next_frame_ms\s*=\s*([^;]+);', src))
    if not sitios:
        print("  no encuentro donde se pone s_next_frame_ms   MAL")
        return 1

    malos = 0
    print("  donde se pone el plazo del cuadro siguiente:")
    for m in sitios:
        ln = src[:m.start()].count('\n') + 1
        expr = ' '.join(m.group(1).split())
        # El enganche es exactamente "ahora + una constante".
        pelado = re.fullmatch(
            r'g_msticks\s*\+\s*(SPECTRUM_FRAME_MS|s_frame_ms|\d+U?)', expr)
        print("    linea %-6d %-40s %s" % (
            ln, expr[:38],
            "*** ENGANCHADO: plazo constante ***" if pelado else "ok"))
        if pelado:
            malos += 1

    print()
    print("  lo que cuesta el enganche (simulado, pico en multiplos de 375 Hz")
    print("  sobre el fondo; menos es mejor):")
    print("    fps pedidos   enganchado          con vaiven +-2 ms")
    for fm in (33, 28, 25, 22, 20, 18, 16):
        p0, f0, b0 = simula(fm, 0)
        p1, f1, b1 = simula(fm, 2)
        print("      %2d ms      %+6.1f dB (%-9s)  %+6.1f dB (%s)" % (
            fm, p0, ','.join(str(x) for x in b0),
            p1, ','.join(str(x) for x in b1)))

    if malos:
        print()
        print("  *** el plazo del cuadro es una constante: el dibujo vuelve")
        print("      a engancharse al audio y el siseo vuelve a ser un tono ***")
        print("  Ver la cabecera de este guion.")
        return 1
    print()
    print("  el plazo del cuadro lleva vaiven  ok")
    return 0


sys.exit(main())
