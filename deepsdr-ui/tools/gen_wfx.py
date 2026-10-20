#!/usr/bin/env python3
"""
Saca a una cabecera las constantes del guardado de WEFAX que vive en main.c.

POR QUE SE GENERA Y NO SE COPIA
-------------------------------
sim/fax_guarda.c no compila el codigo de verdad: el guardado -gsv_abre(),
gsv_bloque(), gsv_cierra() y fax_poll()- vive dentro de main.c, que ningun
banco puede enlazar. Lo que ese banco corre es un MODELO del poll con la
condicion de verdad -wfx_abre_toca()- dentro.

Un modelo escrito a mano se separa del original. Y se separo: el banco
decia

    #define ALTO 600U     /* WFX_ALTO_MAX */

cuando WFX_ALTO_MAX llevaba ya tiempo valiendo 1024. O sea que el banco
juraba que una carta se trocea a los cinco minutos mientras el firmware la
trocea a los ocho y medio, y la linea que lo decia era un comentario: nada
lo comprobaba. Es exactamente la familia de fallo que este proyecto lleva
barriendo todo el mes -algo declara un dato y nada comprueba que sea
verdad- metida en la propia red que tenia que cazarla.

Con esto, las cifras del modelo SON las del firmware. Si alguien cambia una
en main.c, el banco cambia con ella; y si alguien le quita el #define, esto
para la compilacion en vez de generar una cabecera a medias.

Lo que NO arregla: que el modelo sea un modelo. Eso solo se arregla
sacando gsv_* de main.c a su propio fichero para que un banco lo compile,
y eso toca tambien SSTV y la captura de pantalla. Apuntado, y con la
diferencia de que ahora el banco no puede MENTIR mientras tanto.
"""
import re
import sys

# Lo que el modelo del banco necesita saber del firmware, y nada mas.
QUIERO = (
    ('WFX_ALTO_MAX',      'renglones por fichero antes de pasar al siguiente'),
    ('WFX_CERROJO_MS',    'lo que tiene que pasar entre dos ficheros'),
    ('WFX_TRAMO_BLOQUES', 'bloques de 4 kB que se piden de una vez'),
    ('GSV_BLOQUE',        'el bloque de escritura, en bytes'),
)


def saca(src, nombre):
    """El valor de un #define de una sola cifra, con o sin sufijo U."""
    m = re.search(r'^\s*#define\s+%s\s+(\d+)[UL]*\b' % re.escape(nombre),
                  src, re.M)
    if not m:
        sys.exit('gen_wfx.py: no encuentro #define %s en main.c.\n'
                 '  Si lo has renombrado, renombralo tambien aqui: el banco\n'
                 '  sim/fax_guarda.c se queda sin esa cifra y prefiero parar\n'
                 '  a generar una cabecera a medias.' % nombre)
    return int(m.group(1))


def main():
    if len(sys.argv) != 3:
        sys.exit('uso: gen_wfx.py <main.c> <salida.h>')
    src = open(sys.argv[1], encoding='utf-8', errors='replace').read()

    out = ['/* GENERADO por tools/gen_wfx.py desde User/main.c. No se edita:',
           ' * lo que pongas aqui lo pisa el siguiente make. */',
           '#ifndef WFX_TABLA_H',
           '#define WFX_TABLA_H',
           '']
    for nombre, que in QUIERO:
        out.append('#define %-20s %6uU   /* %s */' % (nombre, saca(src, nombre), que))
    out += ['', '#endif', '']
    open(sys.argv[2], 'w', encoding='utf-8').write('\n'.join(out))
    print('  %s: %d cifras sacadas de main.c' % (sys.argv[2], len(QUIERO)))


main()
