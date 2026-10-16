#!/usr/bin/env python3
"""
Comprueba las paletas de la interfaz leyendolas de gfx2/palette.h.

QUE COMPRUEBA Y POR QUE
-----------------------
1. SALTOS ENTRE SUPERFICIES. El panel de esta radio resuelve pasos de 4
   unidades RGB - eso esta medido en la propia placa con una escala de
   escalones, y es la medida que zanjo la discusion de la revision 2 de
   la paleta (ver el comentario de palette.h). Una jerarquia de
   superficies cuyos saltos bajen de ahi no se ve: los tiles y el fondo
   quedan del mismo color.

2. CONTRASTE DE LA TINTA. Relacion WCAG de cada nivel de tinta contra la
   superficie sobre la que se usa. El texto normal necesita 4,5:1 y el
   grande 3:1; la tinta apagada se admite por encima de 3 porque solo se
   usa en etiquetas secundarias de cuerpo grande.

Las MARCAS de datos -traza, acento y banda de paso- no se comprueban
aqui sino con el validador de la guia de visualizacion, que hace las
cuentas de daltonismo que no tiene sentido reimplementar:

    node .../validate_palette.js "<traza>,<acento>,<paso>" \\
         --mode dark --surface "<surf_0>" --pairs all

Este script imprime esa linea ya montada para cada tema, para que
correrlo sea copiar y pegar.
"""
import re
import sys


def luminancia(rgb):
    def canal(c):
        c /= 255.0
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255
    return 0.2126 * canal(r) + 0.7152 * canal(g) + 0.0722 * canal(b)


def contraste(a, b):
    la, lb = luminancia(a), luminancia(b)
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)


def salto(a, b):
    """Diferencia por canal, en unidades RGB de 0 a 255."""
    return tuple(abs(((a >> s) & 255) - ((b >> s) & 255)) for s in (16, 8, 0))


CAMPOS = ['surf_0', 'surf_1', 'surf_2', 'surf_3', 'line', 'grid', 'hdr_top',
          'ink', 'ink_dim', 'ink_mute', 'trace', 'accent', 'passband',
          'ok', 'warn', 'crit']

PANEL_MIN = 4          # el panel resuelve pasos de 4 unidades RGB
TINTA_MIN = {'ink': 4.5, 'ink_dim': 3.0, 'ink_mute': 3.0}

# EL PANEL QUE APLASTA LAS SOMBRAS - 05/10/2026, de un issue.
#
# "my LCD must be far less capable of displaying low intensity colours than
# yours ... anywhere where you have decided on any colour with a main
# component less than 7F makes it unreadable on my screen."
#
# No todos los paneles de esta radio son iguales. En uno de ellos todo lo
# que baja de 0x7F se va al negro, asi que dos tonos oscuros distintos se
# ven IGUAL. Los seis temas que habia construyen la jerarquia de
# superficies justo ahi -la oscura va de 0x0D a 0x2A-, de modo que para ese
# panel son una mancha plana. No se veian mal: no se veian.
#
# Esto NO es un fallo de esos temas y por eso NO suma fallos: un tema
# oscuro es un tema oscuro y para la mayoria de los paneles esta bien. Lo
# que si hace falta es que, cuando alguien anada el tema numero nueve, la
# tabla diga en una linea si ese panel lo podra leer o no, en vez de
# averiguarlo tres meses despues por un issue.
#
# LA REGLA, que es la suya tal cual: un par de colores que hay que
# distinguir ENTRE SI se pierde si los dos tienen el componente mayor por
# debajo de 0x7F. Se miran los pares que de verdad importan.
SOMBRA = 0x7F

# Se mide en DOS grupos, porque son dos preguntas distintas y la respuesta
# a una no sirve para la otra:
#
#   BASE    las superficies y la tinta. Contesta "¿puedo USAR la radio?".
#           Si aqui hay un par aplastado, los tiles no se separan del fondo
#           y el texto secundario desaparece: el tema no vale, punto.
#
#   MARCAS  las tres marcas de datos, contra el fondo y ENTRE ELLAS.
#           Contesta "¿puedo leer el ESPECTRO?". Aqui un par aplastado es
#           mas leve, porque las marcas llevan ademas codificacion
#           geometrica -la traza es un area rellena, el VFO una linea con
#           cabeza, la banda de paso un rectangulo translucido-, que es la
#           misma salida que palette.h ya documenta para el par traza/VFO
#           en tritanopia. Se avisa, no se suspende.
#
# El tema Naranja ensena por que hacen falta los dos: fondo claro -o sea
# que el texto se lee- pero traza #214E6E y acento #300530, los dos bajo el
# umbral. En ese panel el espectro y el marcador de VFO son el mismo borron
# negro, y el tema parece bueno hasta que uno mira la cascada.
PARES_BASE = [('surf_0', 'surf_2'), ('surf_2', 'surf_3'),
              ('surf_0', 'line'), ('surf_0', 'ink')]
PARES_MARCAS = [('surf_0', 'trace'), ('surf_0', 'accent'),
                ('surf_0', 'passband'), ('trace', 'accent'),
                ('trace', 'passband'), ('accent', 'passband')]


def comp_max(rgb):
    return max((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255)


def aplastado(p, pares):
    """Pares que ese panel NO puede separar: los dos por debajo del umbral."""
    return [(a, b) for a, b in pares
            if comp_max(p[a]) < SOMBRA and comp_max(p[b]) < SOMBRA]


def paletas(src):
    out = []
    for m in re.finditer(
            r'static const palette_t (k_pal_\w+)\s*=\s*\{(.*?)\n\};', src, re.S):
        nombre, cuerpo = m.group(1), m.group(2)
        hexes = [int(h, 16) for h in re.findall(r'0x([0-9A-Fa-f]{6})', cuerpo)]
        etiqueta = re.search(r'"([^"]*)"', cuerpo)
        if len(hexes) == len(CAMPOS):
            out.append((nombre, etiqueta.group(1) if etiqueta else nombre,
                        dict(zip(CAMPOS, hexes))))
    return out


def main(ruta):
    src = open(ruta, encoding='utf-8').read()
    ps = paletas(src)
    if not ps:
        print('FALLA: no se ha reconocido ninguna paleta en', ruta)
        return 1

    fallos = 0
    for var, etiqueta, p in ps:
        print('\n  %s  (%s)' % (etiqueta, var))

        # 1. saltos entre superficies
        pares = [('surf_0', 'surf_1'), ('surf_1', 'surf_2'),
                 ('surf_2', 'surf_3'), ('surf_2', 'line')]
        for a, b in pares:
            d = salto(p[a], p[b])
            ok = max(d) >= PANEL_MIN
            if not ok:
                fallos += 1
            print('    salto %-8s -> %-8s  R%+3d G%+3d B%+3d   %s'
                  % (a, b, d[0], d[1], d[2], 'ok' if ok else '*** NO SE VA A VER ***'))

        # 2. contraste de la tinta
        for tinta, minimo in TINTA_MIN.items():
            for fondo in ('surf_0', 'surf_2'):
                c = contraste(p[tinta], p[fondo])
                ok = c >= minimo
                if not ok:
                    fallos += 1
                print('    %-8s sobre %-7s  %5.2f:1  (min %.1f)   %s'
                      % (tinta, fondo, c, minimo, 'ok' if ok else '*** FLOJO ***'))

        print('    marcas -> node scripts/validate_palette.js '
              '"#%06X,#%06X,#%06X" --mode dark --surface "#%06X" --pairs all'
              % (p['trace'], p['accent'], p['passband'], p['surf_0']))

        # 3. panel que aplasta las sombras (informativo, no suma fallos)
        base = aplastado(p, PARES_BASE)
        marc = aplastado(p, PARES_MARCAS)
        print('    panel que aplasta sombras   base: %s   marcas: %s'
              % ('SE LEE' if not base else 'NO (%s)'
                 % ', '.join('%s/%s' % c for c in base),
                 'SE LEEN' if not marc else 'se juntan %s'
                 % ', '.join('%s/%s' % c for c in marc)))

    usables = [e for _, e, p in ps if not aplastado(p, PARES_BASE)]
    print('\n  usables en un panel que aplasta sombras: %d de %d  (%s)'
          % (len(usables), len(ps), ', '.join(usables) if usables else 'ninguno'))
    if not usables:
        print('  *** NINGUN TEMA SIRVE EN ESE PANEL - hace falta uno claro ***')
        fallos += 1

    print('\n  %s' % ('TODO CORRECTO' if not fallos else '%d FALLOS' % fallos))
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'gfx2/palette.h'))
