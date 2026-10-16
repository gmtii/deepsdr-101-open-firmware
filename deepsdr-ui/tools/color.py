#!/usr/bin/env python3
"""
Aritmetica de color compartida: sRGB -> Lab, CIEDE2000 y simulacion de
daltonismo (Vienot 1999, la misma que usan los validadores al uso).

Esta aqui y no dentro de paleta_check.py porque el disenador de temas
(tools/tema_disena.py) necesita exactamente las mismas cuentas: si cada
uno llevara las suyas, se podria disenar un tema que aprueba con una
aritmetica y suspende con la otra.
"""
import math

# ---------------------------------------------------------------- sRGB / Lab

def _lin(c):
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

def _gam(c):
    c = max(0.0, min(1.0, c))
    return 255.0 * (12.92 * c if c <= 0.0031308 else 1.055 * (c ** (1 / 2.4)) - 0.055)

def rgb_tupla(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)

def tupla_rgb(t):
    return (int(round(t[0])) << 16) | (int(round(t[1])) << 8) | int(round(t[2]))

def lineal(v):
    r, g, b = rgb_tupla(v)
    return (_lin(r), _lin(g), _lin(b))

def luminancia(v):
    """Luminancia relativa WCAG."""
    r, g, b = lineal(v)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b

def contraste(a, b):
    la, lb = luminancia(a), luminancia(b)
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)

_M_XYZ = ((0.4124, 0.3576, 0.1805),
          (0.2126, 0.7152, 0.0722),
          (0.0193, 0.1192, 0.9505))

def xyz(v):
    r, g, b = lineal(v)
    return tuple(m[0] * r + m[1] * g + m[2] * b for m in _M_XYZ)

_WP = (0.95047, 1.00000, 1.08883)

def lab(v):
    X, Y, Z = xyz(v)
    def f(t):
        return t ** (1 / 3) if t > 216 / 24389 else (841 / 108) * t + 4 / 29
    fx, fy, fz = f(X / _WP[0]), f(Y / _WP[1]), f(Z / _WP[2])
    return (116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz))

def croma(v):
    _, a, b = lab(v)
    return math.hypot(a, b) / 100.0

# ---------------------------------------------------------------- CIEDE2000

def de2000(v1, v2):
    L1, a1, b1 = lab(v1)
    L2, a2, b2 = lab(v2)
    C1, C2 = math.hypot(a1, b1), math.hypot(a2, b2)
    Cb = 0.5 * (C1 + C2)
    G = 0.5 * (1 - math.sqrt(Cb ** 7 / (Cb ** 7 + 25.0 ** 7))) if Cb > 0 else 0.5
    a1p, a2p = (1 + G) * a1, (1 + G) * a2
    C1p, C2p = math.hypot(a1p, b1), math.hypot(a2p, b2)
    h1p = math.degrees(math.atan2(b1, a1p)) % 360 if (a1p or b1) else 0.0
    h2p = math.degrees(math.atan2(b2, a2p)) % 360 if (a2p or b2) else 0.0
    dLp = L2 - L1
    dCp = C2p - C1p
    if C1p * C2p == 0:
        dhp = 0.0
    else:
        dh = h2p - h1p
        dhp = dh - 360 if dh > 180 else (dh + 360 if dh < -180 else dh)
    dHp = 2 * math.sqrt(C1p * C2p) * math.sin(math.radians(dhp) / 2)
    Lbp = 0.5 * (L1 + L2)
    Cbp = 0.5 * (C1p + C2p)
    if C1p * C2p == 0:
        hbp = h1p + h2p
    else:
        d = abs(h1p - h2p)
        if d <= 180:
            hbp = 0.5 * (h1p + h2p)
        elif h1p + h2p < 360:
            hbp = 0.5 * (h1p + h2p + 360)
        else:
            hbp = 0.5 * (h1p + h2p - 360)
    T = (1 - 0.17 * math.cos(math.radians(hbp - 30))
         + 0.24 * math.cos(math.radians(2 * hbp))
         + 0.32 * math.cos(math.radians(3 * hbp + 6))
         - 0.20 * math.cos(math.radians(4 * hbp - 63)))
    dTh = 30 * math.exp(-(((hbp - 275) / 25) ** 2))
    Rc = 2 * math.sqrt(Cbp ** 7 / (Cbp ** 7 + 25.0 ** 7)) if Cbp > 0 else 0.0
    Sl = 1 + (0.015 * (Lbp - 50) ** 2) / math.sqrt(20 + (Lbp - 50) ** 2)
    Sc = 1 + 0.045 * Cbp
    Sh = 1 + 0.015 * Cbp * T
    Rt = -math.sin(math.radians(2 * dTh)) * Rc
    return math.sqrt((dLp / Sl) ** 2 + (dCp / Sc) ** 2 + (dHp / Sh) ** 2
                     + Rt * (dCp / Sc) * (dHp / Sh))

# ------------------------------------------------------- daltonismo (Vienot)

_LMS = ((17.8824, 43.5161, 4.11935),
        (3.45565, 27.1554, 3.86714),
        (0.0299566, 0.184309, 1.46709))
_LMS_INV = ((0.0809444479, -0.130504409, 0.116721066),
            (-0.0102485335, 0.0540193266, -0.113614708),
            (-0.000365296938, -0.00412161469, 0.693511405))

_SIM = {
    'protanopia':   ((0.0, 2.02344, -2.52581), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)),
    'deuteranopia': ((1.0, 0.0, 0.0), (0.494207, 0.0, 1.24827), (0.0, 0.0, 1.0)),
    'tritanopia':   ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (-0.395913, 0.801109, 0.0)),
}

def _mul(m, v):
    return tuple(sum(m[i][j] * v[j] for j in range(3)) for i in range(3))

def simula(v, tipo):
    """Como ve ese color alguien con ese tipo de daltonismo."""
    if tipo == 'normal':
        return v
    r, g, b = (c / 255.0 for c in rgb_tupla(v))
    lms = _mul(_LMS, (r, g, b))
    lms2 = _mul(_SIM[tipo], lms)
    rgb2 = _mul(_LMS_INV, lms2)
    return tupla_rgb(tuple(_gam(c) for c in rgb2))

TIPOS = ('normal', 'protanopia', 'deuteranopia', 'tritanopia')

def de2000_cvd(v1, v2, tipo):
    return de2000(simula(v1, tipo), simula(v2, tipo))
