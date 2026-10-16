#!/usr/bin/env python3
"""
Saca a una cabecera los textos de spi_flash_volcado_porque_txt().

POR QUE SE GENERA. El banco de anchos (sim/infotest.c) tiene que medir
esos textos, porque una celda que se sale se pinta encima de la de al
lado. Pero spi_flash.c no se puede compilar para el ordenador: toca los
registros GPIO del GD32 directamente, que es lo suyo para un controlador
de bus a mano.

La alternativa era copiar los seis textos al banco. Una copia a mano de
algo que existe es una copia que un dia deja de coincidir - ha pasado
tres veces en este proyecto-, y ademas justo aqui: si alguien alarga un
mensaje y el banco mide el viejo, el banco dice que cabe y no cabe.
"""
import re
import sys


def main(fuente, destino):
    src = open(fuente, encoding='utf-8').read()

    m = re.search(r'const char \*spi_flash_volcado_porque_txt\(void\)\s*\{(.*?)\n\}',
                  src, re.S)
    if not m:
        print('FALLA: no se encuentra spi_flash_volcado_porque_txt()')
        return 1

    # 30/09/2026: los textos pasaron a tr("es", "en"). Se sacan LOS DOS: el
    # banco mide si caben en la celda, y el ingles no siempre es mas corto.
    textos = re.findall(r'return\s+tr\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)', m.group(1))
    textos = [t for par in textos for t in par]
    textos += re.findall(r'return\s+"([^"]*)"\s*;', m.group(1))
    if not textos:
        print('FALLA: esa funcion no devuelve ningun texto reconocible')
        return 1

    with open(destino, 'w', encoding='utf-8') as o:
        o.write('/* GENERADO por tools/gen_volmsg.py desde User/spi_flash.c'
                ' - no editar. */\n')
        o.write('#define VOLMSG_N %d\n' % len(textos))
        o.write('static const char *const k_volmsg[VOLMSG_N] = {\n')
        for t in textos:
            o.write('    "%s",\n' % t)
        o.write('};\n')

    print('  %s: %d textos' % (destino, len(textos)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
