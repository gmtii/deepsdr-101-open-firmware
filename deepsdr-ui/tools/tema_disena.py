#!/usr/bin/env python3
"""
Disena los TEMAS CLAROS (y el de fosforo) y saca el codigo C de palette.h.

POR QUE HACE FALTA UN SCRIPT Y NO SE ESCRIBEN A MANO
---------------------------------------------------
La paleta de esta radio esta disenada AL REVES de lo que piden estos tres
temas: superficies oscuras que se ACLARAN segun se elevan, y tinta clara
que se OSCURECE segun pierde importancia. Un tema de fondo claro con
tinta negra invierte las dos rampas a la vez. Copiar los tres colores que
se ven en la foto y dejar los otros trece como estaban da una pantalla
donde el texto secundario desaparece y los tiles no se distinguen del
fondo - y eso NO lo avisa nadie, se ve cuando ya esta en la radio.

Asi que las dos rampas se generan por regla ("alejate del fondo hasta que
el contraste llegue a X") en vez de a ojo, y las MARCAS de datos se
BUSCAN: se prueban candidatos y se elige el trio que, contra ese fondo
concreto, maximiza la peor distancia de color entre pares incluyendo
daltonismo, exigiendo antes 3:1 de contraste contra la superficie.

SOBRE LOS NUMEROS DE DALTONISMO
-------------------------------
Los dE que imprime este script NO son comparables con los que hay
escritos en palette.h: aquellos salieron de un validador externo con otro
modelo de simulacion. Esta es otra regla de medir. Por eso el criterio
que se usa aqui es RELATIVO: un trio nuevo se acepta si, medido con ESTA
misma regla, no queda por debajo del trio que ya lleva la radio.
"""
import sys, os, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import color

# --------------------------------------------------------------- utilidades

def mezcla(a, b, t):
    ca, cb = color.rgb_tupla(a), color.rgb_tupla(b)
    return color.tupla_rgb(tuple(ca[i] + (cb[i] - ca[i]) * t for i in range(3)))

def busca_contraste(fondo, hacia, objetivo, lo=0.0, hi=1.0):
    """El color mas cercano al fondo, en la direccion 'hacia', que llega a
    'objetivo' de contraste. Bisecciona en vez de tantear: asi el resultado
    es el MINIMO alejamiento que cumple, que es lo que conserva el tono."""
    for _ in range(40):
        t = 0.5 * (lo + hi)
        if color.contraste(mezcla(fondo, hacia, t), fondo) >= objetivo:
            hi = t
        else:
            lo = t
    return mezcla(fondo, hacia, hi)

def escalon(base, hacia, n):
    """n pasos desde base hacia 'hacia', garantizando >= 5 unidades RGB por
    paso (el panel resuelve 4; se deja margen)."""
    out, cur = [], base
    for _ in range(n):
        t = 0.02
        while True:
            sig = mezcla(cur, hacia, t)
            if max(abs(x - y) for x, y in zip(color.rgb_tupla(sig),
                                              color.rgb_tupla(cur))) >= 6:
                break
            t += 0.01
            if t > 1.0:
                break
        out.append(sig)
        cur = sig
    return out

# --------------------------------------------------------------- las marcas

# Candidatos para el trio de marcas. Tonos bien repartidos por la rueda, en
# dos niveles de luz: los oscuros para fondos claros, los claros para fondos
# oscuros. No se inventan sobre la marcha: se prueban todos contra el fondo
# real del tema y gana el que mejor sale medido.
CANDIDATOS = [
    0x0A5C55, 0x14532D, 0x1E3A8A, 0x4C1D95, 0x7F1D1D, 0x78350F, 0x134E4A,
    0x1E40AF, 0x581C87, 0x9A3412, 0x065F46, 0x0C4A6E, 0x3F0D5C, 0x6B1020,
    0x17A394, 0xC77A14, 0x3B7FD4, 0x2F9E52, 0xD94040, 0x8B5CF6, 0x22D3EE,
    0xFFB000, 0x27FF9B, 0x00C2A8, 0x5EEAD4, 0xF472B6, 0xA3E635,
]

def peor_par(trio):
    return min(color.de2000_cvd(a, b, t)
               for a, b in itertools.combinations(trio, 2)
               for t in color.TIPOS)

def busca_marcas(surf, minimo_contraste=3.0, referencia=None):
    """El mejor trio (traza, acento, banda) contra esta superficie."""
    validos = [c for c in CANDIDATOS
               if color.contraste(c, surf) >= minimo_contraste
               and color.croma(c) >= 0.10]
    mejor, mejor_v = None, -1.0
    for trio in itertools.combinations(validos, 3):
        v = peor_par(trio)
        if v > mejor_v:
            mejor, mejor_v = trio, v
    return mejor, mejor_v, len(validos)

# --------------------------------------------------------------- los temas

def tema_claro(nombre, fondo, barra, pulsado, tinta_hacia=0x000000):
    """Fondo claro, tinta negra: las superficies se OSCURECEN al elevarse
    (que es lo que hace el tema original de la foto: la barra y el boton
    pulsado son mas oscuros que el fondo) y la tinta se ACLARA al perder
    importancia."""
    p = {}
    p['surf_0'] = fondo
    p['surf_1'] = barra
    # surf_2 entre la barra y el pulsado, surf_3 el pulsado medido
    p['surf_2'] = mezcla(barra, pulsado, 0.55)
    p['surf_3'] = pulsado
    # contorno: un paso mas alla del pulsado, hacia el negro
    p['line'] = escalon(pulsado, tinta_hacia, 2)[-1]
    # rejilla: recesiva, apenas se separa del fondo
    p['grid'] = escalon(fondo, tinta_hacia, 1)[0]
    p['hdr_top'] = mezcla(fondo, barra, 0.4)
    # tinta: negra de verdad arriba, y los dos escalones apagados BUSCADOS
    # por contraste contra la superficie mas dificil (la mas oscura de las
    # dos sobre las que se escribe: surf_2, el tile).
    p['ink'] = tinta_hacia
    dificil = p['surf_2'] if color.luminancia(p['surf_2']) < color.luminancia(fondo) else fondo
    p['ink_dim'] = busca_contraste(dificil, tinta_hacia, 4.5)
    p['ink_mute'] = busca_contraste(dificil, tinta_hacia, 3.2)
    return p

def tema_fosforo(fondo, tinta, pulsado):
    """Fondo casi negro y tinta de fosforo: la rampa va como siempre, pero
    todo el mundo es verde."""
    p = {}
    p['surf_0'] = fondo
    s = escalon(fondo, tinta, 4)
    p['surf_1'], p['surf_2'] = s[0], s[1]
    p['surf_3'] = pulsado
    p['line'] = s[3]
    p['grid'] = mezcla(fondo, tinta, 0.10)
    p['hdr_top'] = mezcla(fondo, s[0], 0.5)
    p['ink'] = tinta
    p['ink_dim'] = busca_contraste(p['surf_2'], tinta, 4.6, 0.0, 1.0)
    p['ink_mute'] = busca_contraste(p['surf_2'], tinta, 3.2, 0.0, 1.0)
    return p

# ---------------------------------------------- busqueda de marcas por rol

import colorsys

def _sep(a, b):
    return min(color.de2000_cvd(a, b, t) for t in color.TIPOS)

def _gama(h0, h1, surf, minc, minchroma=0.18, smin=0.6, smax=0.9,
          vmin=0.10, vmax=1.0):
    """Todos los colores de ese arco de tono que ya cumplen el contraste.
    vmin/vmax acotan ademas la LUZ, para los sitios donde el tema manda mas
    que la separacion: en el de fosforo la traza tiene que quedarse oscura o
    se confunde con el texto, por mucho que subirla puntuara mejor."""
    out = []
    h = h0
    while h <= h1:
        s = smin
        while s <= smax + 1e-9:
            v = vmin
            while v <= vmax + 1e-9:
                r, g, b = colorsys.hsv_to_rgb((h % 360) / 360.0, s, v)
                c = color.tupla_rgb((r * 255, g * 255, b * 255))
                if color.contraste(c, surf) >= minc and color.croma(c) >= minchroma:
                    out.append(c)
                v += 0.03
            s += 0.1
        h += 5
    return sorted(set(out))

TECHO_DE = 18.0   # a partir de aqui, mas separacion ya no compra nada
PESO_CROMA = 4.0  # ... y se empieza a premiar que el color tenga vida

def trio_por_rol(surf, ventanas, minc=3.0, vueltas=10):
    """Elige un color por ROL (cada rol con su arco de tono, para que el
    resultado siga pareciendo el tema y no una carta de colores) maximizando
    la peor distancia entre pares y contra el fondo, incluyendo daltonismo.

    El techo es deliberado: pasada cierta separacion el ojo ya no gana nada,
    y sin techo el optimizador se va siempre a los extremos de luz y saca
    colores muertos. A partir del techo lo que puntua es el croma."""
    pools = [_gama(w[0], w[1], surf, minc, *( (0.18, 0.6, 0.9) + tuple(w[2:]) )[:3+len(w[2:])])
             if len(w) == 2 else
             _gama(w[0], w[1], surf, minc, 0.18, 0.6, 0.9, w[2], w[3])
             for w in ventanas]
    if any(not p for p in pools):
        return None, 0.0
    def peor(t):
        return min(min(_sep(c, surf) for c in t),
                   min(_sep(a, b) for a, b in itertools.combinations(t, 2)))
    def puntua(t):
        return min(peor(t), TECHO_DE) + PESO_CROMA * sum(color.croma(c) for c in t) / len(t)
    sel = [max(p, key=color.croma) for p in pools]
    m = puntua(tuple(sel))
    for _ in range(vueltas):
        cambio = False
        for i, p in enumerate(pools):
            for c in p:
                t = list(sel); t[i] = c
                v = puntua(tuple(t))
                if v > m + 1e-9:
                    sel, m, cambio = t, v, True
        if not cambio:
            break
    return tuple(sel), peor(tuple(sel))
