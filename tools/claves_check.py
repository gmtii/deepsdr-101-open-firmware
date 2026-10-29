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

Y DESDE EL 29/10/2026, TAMBIEN POR TABLA. Las ocho ganancias por modo
digital (SET_X_PGA_FT8..SET_X_PGA_IDENT, V6.15) se escriben y se leen
recorriendo una tabla de claves, no nombrandolas una a una:

    static const uint8_t k_clave[PGA_DIGI_N] = { SET_X_PGA_FT8, ... };
    for (i = ...) { e->v[k_clave[i]] = ...; }
    for (i = ...) { if (EX_HAY(k_clave[i])) { ... } }

Eso es exactamente igual de seguro -la clave sigue NOMBRADA, y un
_Static_assert ata la cuenta de la tabla con la del enum-, pero no cuadra
con los dos patrones de arriba. Este guion las daba por huerfanas, y por
eso `make comprueba` salia en rojo con el firmware correcto: un guardian
que grita sin motivo acaba leido en diagonal, que es la forma mas barata
de perder uno que grite con motivo.

Lo que se admite NO es "la clave aparece en algun sitio". Es la cadena
entera: la clave esta nombrada en una tabla, Y esa misma tabla se usa
indexada dentro de un `e->v[...] =` para la escritura, Y dentro de un
`EX_HAY(...)` para la lectura. Una clave que se quede en la tabla sin que
nadie recorra la tabla sigue saliendo señalada.

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

def tablas_con(clave):
    """Las tablas `static const uint8_t NOMBRE[..] = { .. clave .. }`.

    Devuelve los NOMBRE que nombran esa clave. Se exige `static const` a
    proposito: una tabla que se pueda reescribir en caliente no prueba
    nada sobre lo que acaba en CONFIG.CSV.
    """
    out = []
    for g in re.finditer(r'static\s+const\s+uint8_t\s+(\w+)\s*\[[^\]]*\]'
                         r'\s*=\s*\{([^}]*)\}', m, re.S):
        if re.search(r'\b' + clave + r'\b', g.group(2)):
            out.append(g.group(1))
    return out


def por_tabla(clave, patron):
    """Si alguna tabla que nombra la clave se recorre dentro de `patron`.

    `patron` lleva un %s donde va el nombre de la tabla indexada.
    """
    for t in tablas_con(clave):
        if re.search(patron % re.escape(t), m):
            return t
    return None


sin_escribir, sin_leer, por_tablas = [], [], []
for k in claves:
    esc = re.search(r'e->v\[\s*' + k + r'\s*\]\s*=', m) is not None
    lee = re.search(r'EX_HAY\(\s*' + k + r'\s*\)', m) is not None
    # El `= ` del final NO es adorno: `e->v[k_clave[i]]` aparece tambien en
    # la rama que LEE, como argumento de ex_rec(). Sin exigir la asignacion,
    # quitar la escritura entera dejaba el guardian en verde - probado.
    t_esc = None if esc else por_tabla(
        k, r'e->v\[\s*%s\s*\[[^\]]*\]\s*\]\s*=[^=]')
    t_lee = None if lee else por_tabla(k, r'EX_HAY\(\s*%s\s*\[')
    if not esc and t_esc is None:
        sin_escribir.append(k)
    if not lee and t_lee is None:
        sin_leer.append(k)
    if (t_esc is not None) and (t_esc == t_lee):
        por_tablas.append((k, t_esc))

if por_tablas:
    # Se dicen EN VOZ ALTA, no se pasan en silencio: quien lea la salida
    # tiene que poder ver cuales no se nombran una a una.
    print('  %d clave(s) por tabla (nombradas en %s, recorridas en los dos'
          ' sitios):' % (len(por_tablas),
                         ', '.join(sorted({t for _, t in por_tablas}))))
    for k, t in por_tablas:
        print('      %-20s via %s[]' % (k, t))

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
