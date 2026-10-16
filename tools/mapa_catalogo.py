#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
EL CATALOGO DE LOS CIEN MAPAS - 14/10/2026.

Compone las hojas a partir de las miniaturas que pinta
deepsdr-ui/sim/mapa_catalogo.c, que usa el dibujante DE VERDAD del
firmware. Aqui solo se maqueta: tipografia, rejilla y PDF.

POR QUE SE REHACE. El catalogo anterior se genero con unos mockups que ya
no estan en el repositorio, y llevaba impreso al pie:

    "En gris, el estilo: HOY NO CAMBIA EL DIBUJO. La radio pinta contornos
     y no rellena la tierra, asi que los numeros que comparten paleta se
     ven igual. Hay 34 paletas distintas entre los 100."

Era verdad y ha dejado de serlo: desde V4.78 la radio rellena, el estilo
cambia el dibujo y los cien ficheros son 84 distintos. Un catalogo que
promete lo contrario de lo que hace la radio es peor que no tenerlo.

uso: mapa_catalogo.py <dir de los .ppm> <tipos.json> <salida.pdf>
"""
import json, os, sys
from PIL import Image, ImageDraw, ImageFont

COLS, FILAS = 3, 4
POR_HOJA = COLS * FILAS
ANCHO, ALTO = 2480, 1754          # A4 apaisado a 210 ppp


def fuente(px, negrita=False):
    for r in ('/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf'
              % ('-Bold' if negrita else ''),
              '/usr/share/fonts/truetype/liberation/LiberationSans%s.ttf'
              % ('-Bold' if negrita else '-Regular')):
        if os.path.exists(r):
            return ImageFont.truetype(r, px)
    return ImageFont.load_default()


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    dirppm, ruta_tipos, salida = sys.argv[1:4]
    tipos = {t['n']: t for t in json.load(open(ruta_tipos, encoding='utf-8'))}

    f_tit = fuente(52, True)
    f_sub = fuente(26)
    f_num = fuente(40, True)
    f_nom = fuente(30, True)
    f_est = fuente(26)
    f_pie = fuente(24)

    mw = (ANCHO - 2 * 120 - (COLS - 1) * 60) // COLS
    mh = int(mw * 288 / 800)

    hojas = []
    n_hojas = (100 + POR_HOJA - 1) // POR_HOJA
    for h in range(n_hojas):
        pag = Image.new('RGB', (ANCHO, ALTO), 'white')
        d = ImageDraw.Draw(pag)
        d.text((120, 70), 'DEEPSDR 101 · los 100 mapas', font=f_tit, fill='black')
        d.text((120, 140),
               'Copia el .map que elijas a la raiz del pendrive. '
               'La radio lo graba al arrancar y borra el fichero.',
               font=f_sub, fill='#444444')
        d.text((ANCHO - 420, 78), 'hoja %d de %d' % (h + 1, n_hojas),
               font=f_sub, fill='#666666')

        for k in range(POR_HOJA):
            n = h * POR_HOJA + k + 1
            if n > 100:
                break
            cx = 120 + (k % COLS) * (mw + 60)
            cy = 195 + (k // COLS) * (mh + 118)
            im = Image.open(os.path.join(dirppm, '%03d.ppm' % n))
            im = im.crop((0, 0, 800, 288)).resize((mw, mh), Image.LANCZOS)
            pag.paste(im, (cx, cy))

            t = tipos.get(n, {})
            nombre = t.get('nombre', '')
            estilo = nombre.split('/')[0].strip()
            paleta = nombre.split('/')[-1].strip()

            d.text((cx, cy + mh + 14), '%03d' % n, font=f_num, fill='black')
            d.text((cx + 92, cy + mh + 22), paleta, font=f_nom, fill='black')
            d.text((cx + 92, cy + mh + 62), estilo, font=f_est, fill='#666666')

        # El pie se pisaba la ultima fila de miniaturas en la primera
        # version: 4 filas de 254 px mas sus rotulos acaban en 1.665 y el
        # pie estaba en 1.604. Medido y bajado, no estrechado.
        d.line((120, ALTO - 96, ANCHO - 120, ALTO - 96), fill='#CCCCCC', width=2)
        d.text((120, ALTO - 78),
               'El nombre en negro es la PALETA y el de abajo el ESTILO. Desde '
               'la V4.78 el estilo SI cambia el dibujo: los tres de contorno '
               'dibujan la costa', font=f_pie, fill='#444444')
        d.text((120, ALTO - 44),
               'y los tres de relleno pintan la tierra. Los cien ficheros son '
               '84 distintos. El relleno se ve al zoom 1; al acercarse, '
               'contorno.', font=f_pie, fill='#444444')
        hojas.append(pag)

    hojas[0].save(salida, save_all=True, append_images=hojas[1:],
                  resolution=210.0)
    print('  %d hojas en %s (%d bytes)'
          % (len(hojas), salida, os.path.getsize(salida)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
