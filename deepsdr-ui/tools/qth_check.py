#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Comprueba que EN EL CODIGO SOLO HAY UN LOCALIZADOR: el de arranque en frio.

Pedido por el usuario, 30/09/2026.

Las dos cosas a la vez, separando "de donde sale el valor que usa la radio"
de "con que arranca una radio virgen": el valor sale SIEMPRE de la clave
"grid" de CONFIG.CSV, y el por defecto existe solo para el arranque en frio,
se aplica UNA vez en main() antes de settings_load() y no se repone nunca.

POR QUE ESTE BANCO DICE AHORA LO CONTRARIO DE LO QUE DECIA
----------------------------------------------------------
Hasta hoy este fichero comprobaba que el localizador COMPILADO fuese el de la
radio: tenia escritas las coordenadas de la Puerta del Sol y exigia que el
#define cayera a menos de 25 km. Y la historia de ese #define es el argumento
para no tener ninguno:

  - valio "IL18" (Canarias) durante meses, porque era el EJEMPLO del
    comentario original y nadie lo cambio. El aspa de los mapas salia en
    Tenerife y las distancias se median desde 1.796 km de donde tocaba, sin
    un solo aviso.
  - el 29/09 se cambio a otro localizador, elegido suponiendo donde estaba
    la radio. Seguia siendo una suposicion: el QTH de verdad caia a 29 km
    de ahi, asi que el valor compilado volvio a ser el de otro sitio, y
    este banco lo aprobaba, porque su referencia tambien estaba inventada.

Dos veces el mismo fallo, y las dos veces este banco lo aprobo, porque su
referencia tambien estaba inventada. Asi que ya no comprueba SI el valor es
el correcto -eso no lo puede saber nadie de antemano- sino que hay UNO SOLO,
que esta donde tiene que estar y que los ajustes lo pisan.

Y ya no necesita saber donde vive nadie, que es otra cosa que estaba mal:
tenia las coordenadas de una casa escritas en un fichero del proyecto.

LO QUE MIDE
-----------
 1. Que el UNICO literal con forma de localizador Maidenhead en el codigo
    (User/*.c, *.h, fuera de comentarios) es el por defecto, y que vale
    exactamente lo que el usuario dijo.
 2. Que ese por defecto solo se aplica desde main(), ANTES de
    settings_load(), y que ft8_decoder_init() NO lo repone - ese fue un
    fallo de verdad: init lo llama ft8_modo.c cada vez que se entra en FT8 y
    se llevaba por delante el QTH del usuario.
 3. Que el localizador entra por la clave "grid" de settings.c.

Lo que este banco NO puede hacer, y conviene tenerlo escrito: no sabe si el
localizador que el usuario ha teclado es el correcto. Eso solo lo caza el
usuario leyendo las coordenadas en la pantalla del QTH, que es exactamente
para lo que existe ese renglon - y es como se descubrio el fallo de las
coordenadas el 30/09.
"""
import os, re, sys

# Forma de un localizador Maidenhead de 4 o 6: dos letras A-R, dos digitos y
# opcionalmente dos letras a-x. Se pide la cadena COMPLETA para no cazar
# cualquier texto que empiece asi.
LOC = re.compile(r'^[A-Ra-r]{2}[0-9]{2}([A-Xa-x]{2})?$')

# Cadenas que tienen esa forma por casualidad y no son localizadores.
# El por defecto, escrito aqui TAMBIEN: si alguien lo cambia en el firmware
# tiene que venir a cambiarlo aqui, y entonces se entera de que lo esta
# cambiando. Es el unico localizador que se admite en el codigo.
POR_DEFECTO = 'IN80dk'

PERMITIDAS = {
    POR_DEFECTO,
    'AA00aa',   # la semilla del editor: el primero de cada alfabeto, y no se
                # aplica hasta que el usuario le da a Aplicar (ver qth_show())
    'PD50',     # modos de SSTV, en sstv.c. "PD" son dos letras entre A y R y
    'PD90',     # "50"/"90" dos digitos: forma de localizador por casualidad.
}

def sin_comentarios(src):
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    return re.sub(r'//[^\n]*', '', src)

def main(raiz):
    user = os.path.join(raiz, 'User')
    fallos = 0
    print("EL LOCALIZADOR: UNO SOLO EN EL CODIGO, Y LOS AJUSTES MANDAN\n")

    sospechosos = []
    for n in sorted(os.listdir(user)):
        if not n.endswith(('.c', '.h')):
            continue
        src = sin_comentarios(open(os.path.join(user, n), encoding='utf-8',
                                   errors='replace').read())
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', src):
            if LOC.match(lit) and lit not in PERMITIDAS:
                sospechosos.append((n, lit))

    if sospechosos:
        for n, lit in sospechosos:
            print('    %-20s "%s"   *** MAL ***' % (n, lit))
        print('\n  El unico localizador que puede estar escrito en el codigo es el')
        print('  por defecto ("%s"). Lo demas sale de CONFIG.CSV.' % POR_DEFECTO)
        fallos += len(sospechosos)
    else:
        print('    %-56s %s'
              % ('el unico localizador del codigo es el por defecto', 'bien'))

    todo = ''
    for n in sorted(os.listdir(user)):
        if n.endswith(('.c', '.h')):
            todo += sin_comentarios(open(os.path.join(user, n), encoding='utf-8',
                                         errors='replace').read())

    # 2a. El por defecto existe y vale lo que dijo el usuario.
    dec = re.findall(r'#define\s+FT8_GRID_POR_DEFECTO\s+"([^"]*)"', todo)
    ok = dec == [POR_DEFECTO]
    print('    %-56s %s' % ('el por defecto es "%s"' % POR_DEFECTO,
                            'bien' if ok else '*** MAL: %s ***' % dec))
    if not ok:
        fallos += 1

    # 2b. Y se aplica UNA vez, desde main(), antes de settings_load().
    mc = sin_comentarios(open(os.path.join(user, 'main.c'), encoding='utf-8').read())
    quien = sorted(n for n in os.listdir(user) if n.endswith('.c')
                   and 'ft8_decoder_grid_por_defecto()' in sin_comentarios(
                       open(os.path.join(user, n), encoding='utf-8',
                            errors='replace').read()))
    # Solo main.c lo LLAMA. La definicion de ft8_decoder.c lleva "(void)"
    # y por eso no cuenta aqui, que es justo lo que interesa: el sitio
    # desde donde se aplica.
    ok = quien == ['main.c']
    print('    %-56s %s' % ('solo lo aplica main.c',
                            'bien' if ok else '*** MAL: %s ***' % quien))
    if not ok:
        fallos += 1

    i_def = mc.find('ft8_decoder_grid_por_defecto()')
    i_set = mc.find('settings_load(')
    ok = 0 <= i_def < i_set
    print('    %-56s %s' % ('y antes de settings_load(), para que el fichero mande',
                            'bien' if ok else '*** MAL ***'))
    if not ok:
        fallos += 1

    # 2c. ft8_decoder_init() no lo repone: eso pisaba el QTH del usuario cada
    # vez que se entraba en FT8.
    dc = sin_comentarios(open(os.path.join(user, 'ft8_decoder.c'), encoding='utf-8').read())
    k = dc.find('void ft8_decoder_init(')
    cuerpo = dc[k:dc.find('\n}', k)] if k >= 0 else ''
    ok = 'FT8_GRID_POR_DEFECTO' not in cuerpo
    print('    %-56s %s' % ('ft8_decoder_init() no repone el por defecto',
                            'bien' if ok else '*** MAL ***'))
    if not ok:
        fallos += 1

    # 3. El unico camino: settings.c con la clave "grid".
    st = open(os.path.join(user, 'settings.c'), encoding='utf-8').read()
    hay_clave = '"grid"' in st
    print('    %-56s %s' % ('settings.c lee la clave "grid"',
                            'bien' if hay_clave else '*** MAL ***'))
    if not hay_clave:
        fallos += 1

    quien = sorted(set(
        n for n in os.listdir(user) if n.endswith('.c')
        and 'ft8_decoder_set_own_grid' in sin_comentarios(
            open(os.path.join(user, n), encoding='utf-8', errors='replace').read())))
    esperado = ['ft8_decoder.c', 'main.c', 'settings.c']
    ok = quien == esperado
    print('    %-56s %s' % ('solo lo pone %s' % ', '.join(esperado),
                            'bien' if ok else '*** MAL: %s ***' % quien))
    if not ok:
        fallos += 1

    print('\n  %s' % ('HAY FALLOS' if fallos else 'TODO CORRECTO (0 fallos)'))
    return 1 if fallos else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '.'))
