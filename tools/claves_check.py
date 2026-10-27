#!/usr/bin/env python3
"""
QUE TODA CLAVE DE CONFIG.CSV SE ESCRIBA Y SE LEA.

POR QUE EXISTE. 24/10/2026, avisado por el usuario: el ancho del paso banda
de CW no sobrevivia al apagado. El fallo no era del fichero -el fichero se
escribia, y hasta se marcaba sucio al girar el mando- sino que ESE DATO NO
ESTABA EN EL FICHERO. Es el mismo fallo que el 01/10 con el interruptor de
la reduccion de ruido: se guardaba la intensidad y no el encendido.

Las dos veces el sintoma fue el mismo y las dos veces costo encontrarlo,
porque por fuera todo cuadra: el ajuste se mueve, la radio responde, el
CONFIG.CSV se reescribe. Lo unico que falta es el numero.

QUE MIRA, Y LO QUE NO PUEDE MIRAR

Mira que cada SET_X_* del enum de settings.h tenga las tres patas:

  1. su nombre en k_extra_claves[] de settings.c -eso ya lo vigila un
     _Static_assert, pero se comprueba igual porque es barato-;
  2. un sitio en main.c que la ESCRIBA:  e->v[SET_X_LOQUESEA] = ...
  3. un sitio en main.c que la LEA:      if (EX_HAY(SET_X_LOQUESEA))

Una clave a la que le falte la 2 se guarda siempre a cero; una a la que le
falte la 3 se escribe y no se usa nunca. Las dos se ven igual desde fuera:
"no guarda".

LO QUE NO PUEDE MIRAR, y hay que decirlo: un ajuste que NO TENGA clave. Eso
es lo que paso con el ancho de CW, y ningun programa lo puede saber, porque
no hay nada escrito en ningun sitio que diga que ese ajuste deberia
guardarse. Para eso solo hay una red: que al añadir un ajuste que el usuario
pueda mover, se mire si tiene clave.
"""
import re, sys, os

raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
h   = open(os.path.join(raiz, 'User/settings.h'), encoding='utf-8', errors='replace').read()
c   = open(os.path.join(raiz, 'User/settings.c'), encoding='utf-8', errors='replace').read()
m   = open(os.path.join(raiz, 'User/main.c'),     encoding='utf-8', errors='replace').read()

# El enum QUE ACABA EN SET_X_N, y con dos trampas que me comi las dos:
#
#   - settings.h tiene VARIOS `typedef enum`, asi que hay que coger el
#     ultimo que empiece antes del final, no el primero del fichero;
#   - y "SET_X_N" es PREFIJO de "SET_X_NR", asi que buscarlo como texto
#     corta el enum en la tercera linea y deja dos claves. Hace falta el
#     \b del final.
fin    = re.search(r'\bSET_X_N\b', h).start()
cuerpo = h[h.rindex('typedef enum', 0, fin):fin]
claves = []
for linea in cuerpo.splitlines():
    # Admite `SET_X_LOQUESEA = 0,` ademas de `SET_X_LOQUESEA,`: la primera
    # clave lleva su valor escrito y sin esto se quedaba fuera de la cuenta.
    g = re.match(r'\s*(SET_X_[A-Z0-9_]+)\s*(?:=\s*\w+\s*)?,', linea)
    if g and g.group(1) != 'SET_X_N':
        claves.append(g.group(1))

if not claves:
    print('  no encuentro ninguna clave en settings.h   MAL')
    sys.exit(1)

# Los nombres de k_extra_claves[], para contarlos.
tabla = c[c.index('k_extra_claves[] = {'):]
tabla = tabla[:tabla.index('};')]
# Fuera los comentarios ANTES de sacar las cadenas: dentro de la tabla hay
# comentarios con texto entrecomillado, y contarlos como nombres daba una
# clave de mas y un descuadre que no existia.
tabla = re.sub(r'/\*.*?\*/', '', tabla, flags=re.S)
nombres = re.findall(r'"([^"]+)"', tabla)

fallos = 0
print('  %d claves en settings.h, %d nombres en k_extra_claves[]'
      % (len(claves), len(nombres)))
if len(claves) != len(nombres):
    print('  NO COINCIDEN: el enum y la tabla de nombres se han desincronizado   MAL')
    fallos += 1

sin_escribir, sin_leer = [], []
for k in claves:
    if not re.search(r'e->v\[\s*' + k + r'\s*\]\s*=', m):
        sin_escribir.append(k)
    if not re.search(r'EX_HAY\(\s*' + k + r'\s*\)', m):
        sin_leer.append(k)

if sin_escribir:
    print('  claves que NADIE escribe (se guardarian siempre a cero):')
    for k in sin_escribir:
        print('      %s' % k)
    fallos += 1
if sin_leer:
    print('  claves que NADIE lee (se escriben y no se usan):')
    for k in sin_leer:
        print('      %s' % k)
    fallos += 1

if fallos == 0:
    print('  todas se escriben y se leen  ok')
sys.exit(1 if fallos else 0)
