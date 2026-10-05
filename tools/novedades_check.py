#!/usr/bin/env python3
"""
QUE EL "QUE HAY DE NUEVO" NO SE QUEDE ATRAS.

  python3 novedades_check.py <User/config.h> <NOVEDADES.md>

POR QUE EXISTE - 05/10/2026.

*** Por el dueño del proyecto: "hay que hacer un whats new que se vaya
rellenando con cada version". ***

Lo de "que se vaya rellenando" es la parte dificil. CAMBIOS.md son medio
megabyte y se escribe solo porque mientras arreglas algo lo estas contando;
un fichero corto y en cristiano NO se escribe solo. Se escribe las dos
primeras versiones y luego se olvida, y un "novedades" con la ultima entrada
tres versiones atras es peor que no tenerlo: el que lo lee cree que esta
mirando lo ultimo.

Asi que no se confia en acordarse. Igual que tools/version_check.py ata la
version del firmware a la del diario, esto la ata a la de NOVEDADES.md: si
subes CONFIG_FW_MENOR y no escribes la entrada, esto para antes de entregar.

QUE COMPRUEBA, Y POR QUE CADA COSA:

  - Que la version del firmware tiene entrada, y que es LA PRIMERA. Una
    entrada escrita pero colocada en medio es una entrada que nadie va a
    ver: lo primero que hace quien abre el fichero es leer de arriba.
  - Que las versiones bajan y no se repiten. Dos "V3.40" es el resultado
    tipico de copiar la entrada de arriba para escribir la nueva.
  - Que cada entrada trae las DOS mitades, castellano e ingles, y que
    ninguna esta vacia. La mitad inglesa es justo la que se queda sin
    escribir "para luego", y luego es nunca.
  - Que la fecha existe y se puede leer. No se compara con el reloj: el que
    compila puede tener la hora mal y eso no es motivo para no entregar.

Lo que NO comprueba: si el texto es bueno. Eso no lo puede mirar un guion.
"""
import datetime
import io
import re
import sys

ENCABEZADO = re.compile(r'^##\s+(V\d+\.\d\d)\s*[·.\-]\s*(\d{4}-\d{2}-\d{2})\s*$')
ES = '### Qué cambia'
EN = "### What's new"


def partes(md):
    """Trocea el fichero en (version, fecha, cuerpo) por cada '## Vx.yy'."""
    lineas = md.split('\n')
    marcas = []
    for i, l in enumerate(lineas):
        m = ENCABEZADO.match(l.rstrip())
        if m:
            marcas.append((i, m.group(1), m.group(2)))
    fuera = []
    for k, (i, ver, fecha) in enumerate(marcas):
        fin = marcas[k + 1][0] if k + 1 < len(marcas) else len(lineas)
        fuera.append((ver, fecha, '\n'.join(lineas[i + 1:fin])))
    return fuera


def num(v):
    a, b = v[1:].split('.')
    return int(a) * 100 + int(b)


def vinetas(cuerpo, desde, hasta):
    """Las viñetas que hay entre dos encabezados de tercer nivel."""
    if desde not in cuerpo:
        return None
    t = cuerpo.split(desde, 1)[1]
    if hasta is not None and hasta in t:
        t = t.split(hasta, 1)[0]
    else:
        t = re.split(r'^##', t, maxsplit=1, flags=re.M)[0]
    return [l for l in t.split('\n') if l.strip().startswith('- ')]


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    config_h, novedades = sys.argv[1], sys.argv[2]

    txt = io.open(config_h, encoding='utf-8', errors='replace').read()
    may = re.search(r'#define\s+CONFIG_FW_MAYOR\s+"([^"]*)"', txt)
    men = re.search(r'#define\s+CONFIG_FW_MENOR\s+"([^"]*)"', txt)
    if not may or not men:
        print('  no encuentro CONFIG_FW_MAYOR/MENOR en %s   MAL' % config_h)
        return 1
    firmware = 'V%s.%s' % (may.group(1), men.group(1))

    try:
        md = io.open(novedades, encoding='utf-8').read()
    except IOError:
        print('  no encuentro %s   MAL' % novedades)
        return 1

    ent = partes(md)
    if not ent:
        print('  %s no tiene ni una entrada "## Vx.yy · AAAA-MM-DD"   MAL'
              % novedades)
        return 1

    mal = 0
    print('  %d entradas, de %s a %s' % (len(ent), ent[0][0], ent[-1][0]))

    if ent[0][0] != firmware:
        print('  el firmware dice %s y lo primero de NOVEDADES.md es %s'
              % (firmware, ent[0][0]))
        if any(v == firmware for v, _, _ in ent):
            # Dos situaciones muy distintas con el mismo sintoma, y decir
            # cual es la mitad del arreglo.
            if num(ent[0][0]) > num(firmware):
                print('  NOVEDADES.md va POR DELANTE del firmware: o subes'
                      ' CONFIG_FW_MENOR a %s' % ent[0][0])
                print('  o quitas esa entrada de ahi   MAL')
            else:
                print('  la entrada de %s existe pero NO esta arriba: la mas'
                      ' nueva va primera   MAL' % firmware)
        else:
            print('  falta la entrada de %s. Escribela antes de entregar: tres'
                  ' lineas en castellano' % firmware)
            print('  y tres en ingles, y no hace falta mas   MAL')
        mal += 1
    else:
        print('  la version de arriba es la del firmware (%s)  ok' % firmware)

    vistas = {}
    ant = None
    for ver, fecha, cuerpo in ent:
        n = num(ver)
        if ver in vistas:
            print('  %s sale dos veces (lineas %s)   MAL' % (ver, vistas[ver]))
            mal += 1
        vistas[ver] = ver
        if ant is not None and n >= ant:
            print('  %s no es mas vieja que la de arriba: el orden va de nueva'
                  ' a vieja   MAL' % ver)
            mal += 1
        ant = n
        try:
            datetime.date.fromisoformat(fecha)
        except ValueError:
            print('  %s: la fecha %r no se puede leer   MAL' % (ver, fecha))
            mal += 1
        for cual, desde, hasta in ((('castellano'), ES, EN), ('ingles', EN, None)):
            v = vinetas(cuerpo, desde, hasta)
            if v is None:
                print('  %s: falta la mitad en %s (%r)   MAL' % (ver, cual, desde))
                mal += 1
            elif not v:
                print('  %s: la mitad en %s esta vacia   MAL' % (ver, cual))
                mal += 1

    if mal:
        print('  *** %d FALLOS ***' % mal)
        return 1
    print('  todas las entradas traen las dos mitades y van en orden  ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
