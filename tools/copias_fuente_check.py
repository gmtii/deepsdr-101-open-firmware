#!/usr/bin/env python3
"""
LAS DOS COPIAS DE LA INTERFAZ, QUE TIENEN QUE SER LA MISMA.

POR QUE EXISTE - 24/10/2026
---------------------------
*** Avisado por el usuario, con una foto: "sobrecarga deberia de salir
centrado". Lo centre en User/ui_top.c, rehice el banco de la franja de
arriba para mirar como quedaba... y salia exactamente igual que antes. ***

Y no era el arreglo: era que el banco NO COMPILA ese fichero.

deepsdr-ui/gfx2/ lleva una COPIA de treinta y nueve ficheros de User/ -todo
el gfx2, las fuentes, la paleta y las pantallas-, y es esa copia la que
compilan los bancos de la interfaz. Hoy estaban todos iguales al byte menos
el que acababa de tocar, asi que la cosa llevaba funcionando por disciplina
y por suerte: al tocar uno de los treinta y nueve en User/ y no copiarlo, el
banco sigue en VERDE midiendo el codigo de ayer.

Eso es peor que no tener banco. Un banco que no prueba lo que se entrega da
una tranquilidad que no se ha ganado, y la da en silencio.

LO QUE HACE ESTO. Comparar byte a byte cada fichero de deepsdr-ui/gfx2/ con
el de igual nombre en User/, y ponerse rojo si alguno se ha separado. No
copia nada por su cuenta: copiar en silencio seria tapar la misma clase de
fallo desde el otro lado -el dia que alguien edite la copia a proposito,
queremos enterarnos, no perder su trabajo-.

LO QUE NO ARREGLA, y se dice: que existan dos copias. Lo correcto seria que
local.mk compilara ../User/<fichero> y no hubiera copia ninguna. Eso son
treinta y nueve reglas de compilacion y se hace con calma; esta red tarda un
segundo y cierra el agujero hoy.
"""

import os
import sys


def main():
    raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    dir_copia = os.path.join(raiz, 'deepsdr-ui', 'gfx2')
    dir_bueno = os.path.join(raiz, 'User')

    if not os.path.isdir(dir_copia):
        print('  no encuentro deepsdr-ui/gfx2   MAL')
        return 1

    mirados = 0
    solos = 0
    malos = []

    for nombre in sorted(os.listdir(dir_copia)):
        if not (nombre.endswith('.c') or nombre.endswith('.h')):
            continue
        copia = os.path.join(dir_copia, nombre)
        bueno = os.path.join(dir_bueno, nombre)
        if not os.path.isfile(bueno):
            # Hay ficheros que solo viven en el simulador. No es un fallo.
            solos += 1
            continue
        mirados += 1
        with open(copia, 'rb') as f:
            a = f.read()
        with open(bueno, 'rb') as f:
            b = f.read()
        if a != b:
            malos.append((nombre, len(b), len(a)))

    print('COPIAS FUENTE: deepsdr-ui/gfx2/ contra User/')
    print('  %d ficheros comparados, %d solo del simulador' % (mirados, solos))

    if not malos:
        print('  las dos copias dicen lo mismo  ok')
        return 0

    for nombre, n_bueno, n_copia in malos:
        print('  %-20s User/ %d bytes, la copia %d   MAL'
              % (nombre, n_bueno, n_copia))
    print('  Los bancos de la interfaz compilan LA COPIA, no User/.')
    print('  Mientras no cuadren, el banco mide el codigo de antes y sale')
    print('  verde igual. Copia el fichero:')
    for nombre, _, _ in malos:
        print('      cp User/%s deepsdr-ui/gfx2/%s' % (nombre, nombre))
    print('  *** %d FALLOS ***' % len(malos))
    return 1


if __name__ == '__main__':
    sys.exit(main())
