#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE NINGUNA ENTREGA SALGA SIN DECIR A QUE AFECTA.

  python3 tools/alcance_check.py <CAMBIOS.md> [CADENA.md]

POR QUE EXISTE - 29/10/2026.

El corrector de imagen de banda ancha entro en la V5.11 para arreglar lo
que se veia en el espectro. Corre dentro de la interrupcion del ADC, sobre
todas las muestras de todos los modos, y eso no estaba escrito en ningun
sitio. Resultado: HFDL decodificaba la mitad de aviones durante tres
semanas sin que nada lo mirara, porque HFDL nunca entro en la lista de
cosas que repasar al tocar el corrector.

El fallo no fue de medida. Habia doscientos setenta y cinco bancos
corriendo. El fallo fue de ALCANCE: nadie escribio que aguas abajo de ese
escalon esta la radio entera, asi que no habia banco porque no habia
sospecha.

Por eso el dueño pidio dos cosas el 29/10/2026, y esta es la segunda:
que cada cambio venga con el estudio de a que componentes afecta de
principio a fin, y que eso no le obligue a el a probar a mano la radio
entera en cada version.

Las dos piden lo mismo: que el alcance se ESCRIBA. Si esta escrito, se
puede exigir un banco por cada cosa nombrada; y lo que quede sin banco
sale dicho en la entrega en vez de descubrirlo el con dos radios encima
de la mesa.

QUE EXIGE
---------
De la ULTIMA entrada de CAMBIOS.md -la de la version que se va a
entregar-:

  1. que lleve un apartado de alcance;
  2. que ese apartado se apoye en CADENA.md, que es el mapa de la cadena
     de principio a fin. Sin mirarlo no se puede saber que hay aguas
     abajo, y una lista escrita de memoria vuelve a ser el fallo de la
     V5.11;
  3. que lo que nombre venga en lista, una cosa por renglon;
  4. y que CADA renglon lleve veredicto: o el banco que lo mide, o decir
     con todas las letras que no hay banco.

LO QUE NO PUEDE HACER
---------------------
No sabe si la lista esta COMPLETA. Eso no lo puede decidir una maquina:
haria falta saber que se ha tocado y que hay debajo, y lo segundo vive en
prosa. Lo que si consigue es que la lista EXISTA y que ningun renglon se
quede a medias, que es justo por donde se escapo el corrector.
"""
import os
import re
import sys
import unicodedata


def sin_tildes(t):
    t = unicodedata.normalize('NFD', t)
    return ''.join(c for c in t if unicodedata.category(c) != 'Mn').lower()


# Como se reconoce el apartado. Se mira sin tildes y sin mayusculas para
# que valga tanto el rotulo gritado como el escrito normal.
TITULO = 'a que afecta'

# Un banco nombrado es uno de estos. Son las cuatro formas con las que en
# este arbol se nombra algo que se puede ejecutar.
BANCO = re.compile(
    r'(make\s+[a-z0-9_]+'
    r'|tools/[a-z0-9_]+\.py'
    r'|sim/[a-z0-9_]+\.c'
    r'|out/[a-z0-9_]+'
    r'|manual_check\.py)'
)

# Y la forma de decir que no lo hay. Tiene que ser explicita: el silencio
# no vale, porque el silencio es exactamente lo que paso en la V5.11.
SIN_BANCO = re.compile(r'sin banco')


def ultima_entrada(texto):
    """Devuelve (titulo, renglones) de la ultima entrada ## con version."""
    lineas = texto.splitlines()
    marca = None
    for i, l in enumerate(lineas):
        if l.startswith('## ') and re.search(r'\(V\d+\.\d+\)', l):
            marca = i
    if marca is None:
        return None, []
    fin = len(lineas)
    for j in range(marca + 1, len(lineas)):
        if lineas[j].startswith('## '):
            fin = j
            break
    return lineas[marca][3:].strip(), lineas[marca + 1:fin]


def apartado(renglones):
    """Saca los renglones del apartado de alcance dentro de una entrada."""
    dentro = False
    nivel = None
    salida = []
    for l in renglones:
        m = re.match(r'^(#{3,6})\s+(.*)$', l)
        if m:
            if dentro and len(m.group(1)) <= nivel:
                break
            if not dentro and TITULO in sin_tildes(m.group(2)):
                dentro = True
                nivel = len(m.group(1))
                continue
        if dentro:
            salida.append(l)
    return salida if dentro else None


def junta_puntos(cuerpo):
    """Junta cada punto de la lista con sus renglones de continuacion.

    Un punto de lista en markdown se parte en varios renglones en cuanto
    pasa de los ochenta caracteres, y el veredicto cae casi siempre en el
    ultimo. Mirar renglon a renglon daria por sin veredicto justo a los
    puntos bien escritos, que son los largos. Un renglon en blanco o un
    titulo cierran el punto: lo que venga detras ya es prosa.
    """
    puntos = []
    abierto = False
    for l in cuerpo:
        if re.match(r'^\s*[-*]\s+\S', l):
            puntos.append(l.strip())
            abierto = True
        elif not l.strip() or re.match(r'^\s*#{1,6}\s', l):
            abierto = False
        elif abierto:
            puntos[-1] += ' ' + l.strip()
    return puntos


def main():
    sueltos = [a for a in sys.argv[1:] if not a.startswith('-')]
    if not sueltos:
        print('uso: alcance_check.py <CAMBIOS.md> [CADENA.md]')
        return 2
    cambios = sueltos[0]
    cadena = sueltos[1] if len(sueltos) > 1 else None

    if not os.path.isfile(cambios):
        print('  (no esta %s: me salto el alcance)' % cambios)
        return 0

    with open(cambios, encoding='utf-8') as f:
        texto = f.read()

    print('ALCANCE DE LA ENTREGA')
    print('---------------------')

    titulo, renglones = ultima_entrada(texto)
    if titulo is None:
        print('MAL: no encuentro ninguna entrada con version en %s.' % cambios)
        return 1
    print('  entrada:  %s' % titulo)

    if cadena and not os.path.isfile(cadena):
        print('MAL: falta %s, que es el mapa de la cadena.' % cadena)
        print('     El apartado de alcance se escribe mirandolo.')
        return 1

    cuerpo = apartado(renglones)
    if cuerpo is None:
        print('MAL: esa entrada no lleva apartado de alcance.')
        print('')
        print('     Cada entrada tiene que decir que queda AGUAS ABAJO de lo')
        print('     que se ha tocado, con CADENA.md delante, y por cada cosa')
        print('     nombrada el banco que la mide o que no hay banco. Asi:')
        print('')
        print('       ### A que afecta')
        print('')
        print('       Segun CADENA.md, por debajo de esto queda:')
        print('')
        print('       - el demodulador de AM -- lo mide make wfmrev')
        print('       - la cascada -- sin banco, mirado a ojo en el simulador')
        return 1

    fallos = 0
    if not any(('CADENA.md' in l) for l in cuerpo):
        print('MAL: el apartado no nombra CADENA.md.')
        print('     Lo que hay aguas abajo se mira ahi, no de memoria: una')
        print('     lista escrita de cabeza es el fallo de la V5.11 otra vez.')
        fallos += 1

    puntos = junta_puntos(cuerpo)
    if not puntos:
        print('MAL: el apartado no nombra nada en lista.')
        print('     Una cosa por renglon, empezando por un guion.')
        fallos += 1

    medidos = 0
    for l in puntos:
        corto = l.strip()
        if len(corto) > 72:
            corto = corto[:69] + '...'
        banco = BANCO.search(l)
        if banco:
            medidos += 1
            print('  ok   %s' % corto)
        elif SIN_BANCO.search(sin_tildes(l)):
            print('  OJO  %s' % corto)
        else:
            print('  MAL  %s' % corto)
            print('       sin veredicto: nombra el banco que lo mide, o di')
            print('       sin banco con todas las letras.')
            fallos += 1

    if puntos:
        print('')
        print('  %d cosas nombradas, %d con banco, %d sin el'
              % (len(puntos), medidos, len(puntos) - medidos))
        if medidos == 0 and len(puntos) > 1:
            print('  OJO: ninguna medida. Si de verdad no hay nada que medir,')
            print('       que quede dicho en la entrega y no lo descubra el.')

    if fallos:
        print('')
        print('MAL: %d cosa%s que arreglar en el apartado de alcance.'
              % (fallos, '' if fallos == 1 else 's'))
        return 1

    print('')
    print('ALCANCE: todo correcto')
    return 0


if __name__ == '__main__':
    sys.exit(main())
