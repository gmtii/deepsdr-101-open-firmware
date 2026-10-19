#!/usr/bin/env python3
"""
LOS INTERRUPTORES DE PRUEBA QUE SE QUEDAN ENCENDIDOS - 17/10/2026.

EL PATRON, QUE YA SE HA DADO DOS VECES. Alguien necesita un diagnostico
para cazar algo, lo pone detras de un #define, escribe en el comentario
"a 0 cuando esto este arreglado", lo arregla... y el define se queda a 1.
No falla nada, no avisa nadie, y el codigo pasa a decir una cosa mientras
hace la contraria.

Los dos que habia el dia que se escribio esto:

    TOUCH_EDGE_DEBUG       a 1 desde el 18/08/2026. Su comentario decia
                           "back to 0 once that's tracked down". Llamaba
                           a touch_debug_stream_poll() en cada vuelta del
                           bucle principal y, con un dedo puesto, hacia
                           dos lecturas del tactil cada 150 ms para
                           escribirlas por un puerto serie apagado.

    SPI_FLASH_PROBE_TEST   a 1 desde la puesta en marcha. Su comentario
                           decia "kept here, GATED OFF BY DEFAULT".
                           Volcaba el chip y escaneaba la FAT en cada
                           encendido, tambien para nadie.

Entre los dos eran 364 bytes de flash, que es lo de menos: lo que costaba
de verdad era trabajo en cada arranque y en cada toque.

COMO LOS ENCUENTRA. No por el nombre -hay defines con TEST en el nombre
que son de verdad- sino por lo que dice su propio comentario. Si encima
de un #define ... 1 hay una frase que promete apagarlo ("back to 0",
"once", "temporary", "bring-up", "ONE-TIME", "solo para"), el define y su
comentario no dicen lo mismo, y eso es el fallo.

QUE NO MARCA. Un define a 0 -ese ya esta como dice-, ni uno a 1 sin esa
promesa en su comentario: LCD_DMA_ON_DEF, ARM_MATH_LOOPUNROLL y los demas
son decisiones, no diagnosticos pendientes de apagar.

    python3 tools/interruptores_check.py .
"""
import os
import re
import sys

EXT = ('.c', '.h')
FUERA = ('/build/', '/build_470/', '/out/', '/.git/', '/__pycache__/',
         '/CMSIS/', '/Firmware/', '/usb/', '/Ft8Lib/', '/vendor/',
         '/_archivo/', '/rescate/')

# Un #define de una sola cifra, que es la forma que tienen estos.
DEFINE = re.compile(r'^[ \t]*#[ \t]*define[ \t]+([A-Z_][A-Z0-9_]*)[ \t]+1[ \t]*$')

# Lo que convierte un define en una promesa sin cumplir.
PROMESA = re.compile(
    r'(back to 0|set (?:it )?(?:back )?to 0|a 0 (?:cuando|una vez|en cuanto)|'
    r'once that|once it|one-?time|bring-?up(?: only)?|'
    r'temporary diagnostic|diagnostico temporal|'
    r'gated off by default|apagado por defecto|off by default|'
    r'quitar cuando|borrar cuando|volver a 0)', re.I)

# Las lineas de comentario que van justo encima del #define.
#
# PARA EN EL "/*" DEL BLOQUE, Y HACE FALTA. Sin eso subia hasta la primera
# linea en blanco, y cuando un #define cae justo debajo del comentario
# largo de OTRO #define -sin un renglon en medio-, heredaba sus promesas y
# salia marcado sin tener la culpa. Se vio probando el banco con un define
# inventado que no prometia nada: lo marcaba igual.
def comentario_de_encima(lineas, i):
    out = []
    j = i - 1
    while j >= 0:
        t = lineas[j].strip()
        if t == '':
            break
        if t.startswith('/*'):          # el principio del bloque: esta y basta
            out.append(t)
            break
        if t.startswith('*') or t.startswith('//') or t.endswith('*/'):
            out.append(t)
            j -= 1
            continue
        break
    return '\n'.join(reversed(out))


def main(raiz):
    malos = []
    mirados = 0
    for base, dirs, ficheros in os.walk(raiz):
        r = base.replace('\\', '/') + '/'
        if any(x in r for x in FUERA):
            continue
        for f in sorted(ficheros):
            if not f.endswith(EXT):
                continue
            p = os.path.join(base, f)
            try:
                lineas = open(p, encoding='utf-8', errors='replace').read().split('\n')
            except OSError:
                continue
            mirados += 1
            for i, lin in enumerate(lineas):
                m = DEFINE.match(lin)
                if not m:
                    continue
                com = comentario_de_encima(lineas, i)
                pr = PROMESA.search(com)
                if pr:
                    malos.append((p, i + 1, m.group(1), pr.group(0)))

    rel = lambda x: os.path.relpath(x, raiz).replace(os.sep, '/')
    print('  %d ficheros mirados' % mirados)
    if malos:
        print('\n  INTERRUPTORES DE PRUEBA ENCENDIDOS:\n')
        for p, lin, nombre, frase in malos:
            print('    %s:%d  %s' % (rel(p), lin, nombre))
            print('        su comentario dice "%s"' % frase)
        print('\n  Un define a 1 cuyo comentario promete apagarlo es un')
        print('  diagnostico que se quedo puesto. O se apaga, o se le quita')
        print('  la promesa al comentario: lo que no vale es que digan')
        print('  cosas distintas.')
        print('\n  *** %d ***' % len(malos))
        return 1
    print('  ningun interruptor de prueba encendido  ok')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '.'))
