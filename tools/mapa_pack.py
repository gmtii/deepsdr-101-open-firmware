#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mapa_pack.py - de unos trazos del mundo a un fichero .map de la radio.

EL PORQUE DEL FORMATO ESTA EN User/mapa_bin.h y no se repite aqui. Lo que
hace falta saber para usar esto:

  python3 mapa_pack.py datos/mundo.bin 014 --salida mapas/

El fichero de entrada -datos/mundo.bin- trae la costa y las fronteras del
mundo ya recortadas a +-71,5 grados, SIN simplificar. Lo genera una sola
vez tools/mundo_pack.py a partir de GSHHS y de las fronteras admin-0, que
son de dominio publico. Se guarda en el repositorio a proposito: un
empaquetador que necesita bajarse medio gigabyte de shapefiles para
funcionar no lo usa nadie, ni siquiera yo dentro de seis meses.

LOS TIPOS son los cien de mockups_mapa/tipos: cada uno es una paleta y un
tratamiento, y de aqui solo salen los QUINCE COLORES. El tratamiento
-relleno, silueta, punteado- lo decide la radio al dibujar, no el fichero.
"""
import argparse, os, struct, sys

MARCA    = b'DSDRMAP1'
VERSION  = 2          # v2: tres niveles, los de arriba troceados
N_COLOR  = 15
CAB      = 60
NIV_MAX  = 4
RAM      = 10240         # MAPA_BIN_RAM
PX_GRADO = 800.0/360.0   # la pantalla del mapa al zoom 1
CUANT    = 32            # 1/32 de grado
LAT_TOPE = 71.5

# El orden de los quince, igual que el enum de mapa_bin.h.
ORDEN = ('mar','tierra','costa','frontera','rejilla','ecuador','boton',
         'borde','tinta','tinta_off','marca','marca_off','casa','rotulo','linea')

CONTRASTE_MINIMO = 48    # el mismo numero que mapa_bin.c


def luz(c):
    return (299*((c >> 16) & 255) + 587*((c >> 8) & 255) + 114*(c & 255)) // 1000


def contrasta(marca, mar):
    return abs(luz(marca) - luz(mar)) >= CONTRASTE_MINIMO


# ---------------------------------------------------------------- entrada
def lee_mundo(ruta):
    """(costa, fronteras), cada una una lista de listas de (lon, lat)."""
    with open(ruta, 'rb') as f:
        if f.read(8) != b'DSDRWRL2':
            sys.exit('mapa_pack: %s no es un mundo.bin' % ruta)
        capas = []
        for _ in range(2):
            (n_tr,) = struct.unpack('<I', f.read(4))
            trazos = []
            for _ in range(n_tr):
                (n,) = struct.unpack('<H', f.read(2))
                # centesimas de grado en int16: ver tools/mundo_pack.py
                v = struct.unpack('<%dh' % (2 * n), f.read(4 * n))
                trazos.append([(v[2*i] / 100.0, v[2*i+1] / 100.0) for i in range(n)])
            capas.append(trazos)
    return capas[0], capas[1]



# =====================================================================
# LOS ANILLOS DE TIERRA - 14/10/2026
# =====================================================================
#
# LO QUE SE ENCONTRO AL MIRAR, antes de escribir una linea: de los 3.751
# trazos de costa de mundo.bin, 3.708 YA SON ANILLOS CERRADOS -el primer
# punto es el ultimo-, porque vienen de GSHHS, que son poligonos. Solo 43
# estan abiertos, y todos por la misma razon: tools/mundo_pack.py recorta
# el mundo a +-180 y +-71,5 grados, y eso parte por el borde la costa de
# Eurasia y unas cuantas islas articas.
#
# O sea que el trabajo no era "convertir lineas en poligonos" -que habria
# sido mucho- sino CERRAR CUARENTA Y TRES contra el borde por el que se
# cortaron. Medirlo primero ahorro el noventa y nueve por ciento del
# trabajo que yo tenia presupuestado.
LON_TOPE = 180.0
BORDE_EPS = 0.6

_ESQUINA = {1.0: ( LON_TOPE, -LAT_TOPE), 2.0: ( LON_TOPE,  LAT_TOPE),
            3.0: (-LON_TOPE,  LAT_TOPE), 4.0: (-LON_TOPE, -LAT_TOPE)}


def en_borde(p):
    x, y = p
    return abs(abs(x) - LON_TOPE) <= BORDE_EPS or abs(abs(y) - LAT_TOPE) <= BORDE_EPS


def t_perimetro(p):
    """0..4 recorriendo el rectangulo en ANTIHORARIO desde (-180,-71,5)."""
    x, y = p
    if abs(y + LAT_TOPE) <= BORDE_EPS:
        return 0.0 + (x + LON_TOPE) / (2 * LON_TOPE)
    if abs(x - LON_TOPE) <= BORDE_EPS:
        return 1.0 + (y + LAT_TOPE) / (2 * LAT_TOPE)
    if abs(y - LAT_TOPE) <= BORDE_EPS:
        return 2.0 + (LON_TOPE - x) / (2 * LON_TOPE)
    return 3.0 + (LAT_TOPE - y) / (2 * LAT_TOPE)


def _esquinas(ta, tb, sentido):
    """Las esquinas que se cruzan yendo de ta a tb por el perimetro."""
    import math
    out, t = [], ta
    for _ in range(5):
        if sentido > 0:
            sig = math.floor(t) + 1.0
            d_sig = (sig - t) % 4.0
            d_tb = (tb - t) % 4.0
        else:
            sig = math.ceil(t) - 1.0
            if sig < 0.0:
                sig += 4.0
            d_sig = (t - sig) % 4.0
            d_tb = (t - tb) % 4.0
        if d_tb <= d_sig + 1e-9:
            break
        out.append(_ESQUINA[sig if sig > 0.0 else 4.0])
        t = sig % 4.0
    return out


def area(anillo):
    """Area con signo en grados cuadrados (formula del zapatero). En un mapa
    equirectangular el grado cuadrado ES el area de pantalla, asi que este
    numero se puede comparar con lo que se ve."""
    s = 0.0
    for i in range(len(anillo) - 1):
        x0, y0 = anillo[i]
        x1, y1 = anillo[i + 1]
        s += x0 * y1 - x1 * y0
    return s * 0.5


def cierra(t):
    """Cierra por el perimetro, quedandose con el camino que encierra MENOS
    superficie.

    POR QUE EL MENOR Y NO "EL QUE DEJE AREA POSITIVA". Lo intente asi -GSHHS
    trae la tierra en antihorario, o sea area positiva- y salio el mundo
    relleno entero: el 1.076 % de la ventana. El motivo es que los dos
    caminos dan areas que SUMAN EL RECTANGULO COMPLETO, asi que si el trozo
    bueno mide -1, el malo mide +51.479 y la regla del signo se queda con el
    malo.

    Un trozo de costa cortado por el borde encierra, por definicion, un
    pedazo de tierra; el camino contrario encierra ese pedazo menos el
    mundo. El menor en valor absoluto es el bueno, y con la regla par-impar
    el signo no se usa para nada.
    """
    ta, tb = t_perimetro(t[-1]), t_perimetro(t[0])
    mejor = None
    for s in (+1, -1):
        n = list(t) + _esquinas(ta, tb, s) + [t[0]]
        if mejor is None or abs(area(n)) < abs(area(mejor)):
            mejor = n
    return mejor


def encadena(abiertos):
    """Junta los trazos abiertos que comparten un extremo.

    POR QUE HACE FALTA, Y COMO SALIO. La primera version cerraba cada trazo
    abierto por su cuenta y el banco de pantalla enseño el fallo en la foto:
    Africa, Eurasia, Suramerica y Australia rellenas, y NORTEAMERICA Y
    GROENLANDIA no.

    Mirando los extremos, el motivo:

        2403 pts  de (-92.95, 71.35) a (-79.98, 9.36)
        2152 pts  de (-79.98,  9.36) a (-95.50, 71.47)

    Es Norteamerica PARTIDA EN DOS en Panama. Cada mitad tiene un extremo en
    el borde y el otro DENTRO del mapa, asi que ninguna de las dos pasaba la
    prueba de "los dos extremos en el borde" y las dos se quedaban como
    linea. Groenlandia, lo mismo, partida en (-44.70, 59.99).

    Encadenadas, el trozo va de borde a borde y se cierra como los demas.

    Se encadena por COINCIDENCIA EXACTA del punto, no por cercania: estos
    ficheros vienen en centesimas de grado y el punto de union es el mismo
    numero en los dos trozos. Una tolerancia uniria cosas que no se tocan.
    """
    porini = {}
    for i, t in enumerate(abiertos):
        porini.setdefault(t[0], []).append(i)

    # SE EMPIEZA POR LAS CABEZAS, Y ESO NO ES UN DETALLE. La primera version
    # recorria la lista en orden y no encadeno NADA: la mitad de
    # Norteamerica que acaba en el borde venia antes en el fichero que la
    # que empieza en Panama, asi que se procesaba primero, se marcaba usada,
    # y cuando llegaba su pareja ya no habia con quien engancharse. Treinta
    # y seis trozos entraban y treinta y seis salian.
    #
    # Una cabeza es un trozo cuyo principio NO es el final de ningun otro.
    # Empezando por ellas, cada cadena se recorre entera y en su sentido.
    finales = set()
    for t in abiertos:
        finales.add(t[-1])
    cabezas = [i for i, t in enumerate(abiertos) if t[0] not in finales]
    orden = cabezas + [i for i in range(len(abiertos)) if i not in set(cabezas)]

    usados = [False] * len(abiertos)
    fuera = []
    for i in orden:
        t = abiertos[i]
        if usados[i]:
            continue
        usados[i] = True
        cad = list(t)
        # ir enganchando por el final mientras haya quien empiece ahi
        for _ in range(len(abiertos)):
            sig = None
            for j in porini.get(cad[-1], ()):
                if not usados[j]:
                    sig = j
                    break
            if sig is None:
                break
            usados[sig] = True
            cad.extend(abiertos[sig][1:])
            if cad[0] == cad[-1]:
                break
        fuera.append(cad)
    return fuera


def anillos_y_lineas(trazos):
    """(anillos, lineas, cerrados, no_cerrables).

    Un trazo que no se puede cerrar NO se cierra: se queda como linea y se
    dibujara como hoy, sin relleno. Inventarle un cierre seria pintar tierra
    donde no la hay, que es peor que no pintarla.
    """
    anillos, lineas = [], []
    cerrados = no_cerrables = 0

    # Primero los que ya cierran; los abiertos se encadenan entre si antes
    # de intentar nada, porque un continente puede venir partido.
    abiertos = []
    for t in trazos:
        if len(t) >= 3 and t[0] == t[-1]:
            anillos.append(t)
        elif len(t) < 3:
            lineas.append(t)
        else:
            abiertos.append(t)

    for t in encadena(abiertos):
        if t[0] == t[-1]:
            anillos.append(t)
            cerrados += 1
            continue
        if not (en_borde(t[0]) and en_borde(t[-1])):
            lineas.append(t)
            no_cerrables += 1
            continue
        n = cierra(t)
        if n is None:
            lineas.append(t)
            no_cerrables += 1
        else:
            anillos.append(n)
            cerrados += 1
    return anillos, lineas, cerrados, no_cerrables


# ------------------------------------------------------------ simplificar
def _dp(p, eps):
    """Douglas-Peucker sin recursion: con costas de miles de puntos la
    recursiva se come la pila del interprete y revienta sin decir donde."""
    if len(p) < 3:
        return p
    guarda = [False] * len(p)
    guarda[0] = guarda[-1] = True
    pila = [(0, len(p) - 1)]
    while pila:
        a, b = pila.pop()
        if b <= a + 1:
            continue
        x0, y0 = p[a]; x1, y1 = p[b]
        dx, dy = x1 - x0, y1 - y0
        n = (dx*dx + dy*dy) ** 0.5
        peor, k = -1.0, a
        for i in range(a + 1, b):
            x, y = p[i]
            if n < 1e-12:
                d = ((x-x0)**2 + (y-y0)**2) ** 0.5
            else:
                d = abs(dx*(y0-y) - (x0-x)*dy) / n
            if d > peor:
                peor, k = d, i
        if peor > eps:
            guarda[k] = True
            pila.append((a, k)); pila.append((k, b))
    return [p[i] for i in range(len(p)) if guarda[i]]


def diagonal(t):
    xs = [q[0] for q in t]; ys = [q[1] for q in t]
    return ((max(xs)-min(xs))**2 + (max(ys)-min(ys))**2) ** 0.5


def recorta(trazos, minimo, eps):
    fuera = []
    for t in trazos:
        if minimo > 0 and diagonal(t) < minimo:
            continue
        s = _dp(t, eps) if eps > 0 else t
        if len(s) >= 2:
            fuera.append(s)
    return fuera


# -------------------------------------------------------------- codificar
def cuantiza(trazos):
    fuera = []
    for t in trazos:
        q = []
        for lon, lat in t:
            x = int(round(lon * CUANT)); y = int(round(lat * CUANT))
            if not q or q[-1] != (x, y):
                q.append((x, y))
        if len(q) >= 2:
            fuera.append(q)
    return fuera


# EL BIT QUE DICE "ESTO ES UNA FRONTERA" - 12/10/2026.
#
# Porque el dibujante no las distinguia: pintaba TODOS los trazos con el
# color de la costa. El .map traia desde el principio un color propio
# para las fronteras -MAPA_COL_FRONTERA, y cada uno de los cien tipos lo
# tiene puesto- y no lo usaba nadie.
#
# Se marca en el bit alto de la cuenta de puntos. El trazo mas largo del
# mundo tiene 5.914 puntos, asi que de 32.768 para arriba no hay nada y
# ese bit esta libre. Cuesta CERO bytes: no crece el fichero.
FRONTERA_BIT = 0x8000
RELLENO_BIT  = 0x4000   # anillos de relleno: no se dibujan

# EL BIT QUE DICE "ESTO ES UN ANILLO DE TIERRA" - 14/10/2026.
#
# MAPA_COL_TIERRA es el hueco 1 de los quince colores que lleva cada .map
# desde el primer dia, y no lo lee nadie: mapa.c rellena el panel con el
# color del mar y dibuja las costas encima. Por eso el catalogo dice "el
# estilo HOY NO CAMBIA EL DIBUJO" y por eso 013, 014 y 015 son el mismo
# fichero byte a byte.
#
# Para rellenar hace falta saber QUE LADO ES TIERRA, y eso se decide AQUI,
# no en la radio: un Cortex-M4 no tiene que averiguar topologia cincuenta
# veces por segundo.
#
# El trazo marcado con este bit es un ANILLO CERRADO cuyo interior es
# tierra. La radio lo rellena con la regla par-impar, que no necesita saber
# el sentido de giro ni quien esta dentro de quien: un lago dentro de un
# continente sale hueco solo, y una isla dentro del lago vuelve a salir
# llena.
#
# NO VA EN LA CUENTA DE PUNTOS - 14/10/2026, por la tarde.
#
# La primera version lo puso en el bit 14, al lado del de frontera, con el
# argumento de que "un .map viejo no lo trae y entonces la radio dibuja como
# hoy". Eso es verdad en una direccion y MENTIRA en la otra, que es la que
# importa: un .map NUEVO leido por un firmware VIEJO. El lector de antes
# enmascaraba 0x7FFF, asi que el bit 14 se le colaba dentro de la cuenta y
# el primer anillo se tragaba la tabla entera. Medido: 51 de los cien mapas
# salian como UN solo trazo de 4.309 puntos en vez de 370 trazos. No
# reventaba -el lector comprueba limites- pero el dibujo quedaba destruido
# en silencio, que es el peor fallo que se puede entregar.
#
# Asi que los anillos NO tocan la tabla de trazos. Van en un mapa de bits
# DETRAS DEL CERO QUE CIERRA la tabla, dentro del `tam` del nivel, y su
# longitud va en los dos bytes de relleno de la entrada de nivel. Un lector
# viejo para en el cero y no lo ve nunca; la tabla de trazos vuelve a ser
# byte a byte la de un fichero de version 2.
#
# Cuesta 47 bytes por mapa, y el banco mapa_compat_check.py lo vigila.
ANILLO_BIT   = 0  # ya no existe; se deja el nombre para que nadie lo reviva

# EL ESTILO, QUE HASTA HOY ERA UNA MENTIRA EN EL NOMBRE - 14/10/2026.
#
# tipos.json trae seis estilos repartidos entre las cien paletas:
#
#   contorno                17      tierra rellena      17
#   contorno sin fronteras  16      tierra de puntos    17
#   contorno encendido      16      silueta             17
#
# ...y el fichero salia IGUAL para los seis, porque la radio nunca relleno
# nada. Por eso 013 (tierra de puntos), 014 (contorno) y 015 (tierra
# rellena) son hoy el mismo fichero byte a byte, y por eso el catalogo tiene
# que avisar de que "el estilo HOY NO CAMBIA EL DIBUJO".
#
# Con los anillos, el estilo pasa a decidir dos cosas distintas:
#
#   - si los trazos van MARCADOS como anillo, que es lo que permite
#     rellenar. Los tres "contorno" no lo llevan: un contorno no tiene
#     interior que pintar, y marcarselo seria dejar que la radio decida
#     rellenar un mapa cuyo nombre promete lo contrario.
#   - COMO se rellena, que es cosa del dibujante y no del fichero. Va en un
#     byte de la cabecera -el 57, que estaba a cero y reservado- para que la
#     radio no tenga que leer el nombre del tipo, que no viaja en el .map.
ESTILO_CONTORNO = 0
ESTILO_RELLENO  = 1
ESTILO_PUNTOS   = 2
ESTILO_SILUETA  = 3

ESTILOS = {
    'contorno':               ESTILO_CONTORNO,
    'contorno sin fronteras': ESTILO_CONTORNO,
    'contorno encendido':     ESTILO_CONTORNO,
    'tierra rellena':         ESTILO_RELLENO,
    'tierra de puntos':       ESTILO_PUNTOS,
    'silueta':                ESTILO_SILUETA,
}


def estilo_de(nombre):
    """El estilo sale del nombre del tipo, que es donde esta escrito. Si
    aparece uno que no esta en la tabla se PARA: un estilo nuevo que caiga
    en silencio en 'contorno' seria otra promesa que el fichero no cumple,
    y de esas ya hubo una."""
    cabeza = (nombre or '').split('/')[0].strip()
    if cabeza not in ESTILOS:
        sys.exit('mapa_pack: estilo desconocido "%s". Si es nuevo, ponlo en '
                 'ESTILOS y di como se dibuja.' % cabeza)
    return ESTILOS[cabeza]


def codifica(trazos, frontera=False, relleno=False):
    """n, primer punto absoluto, y los saltos. Ver mapa_bin.h.

    `frontera` marca el bit alto de la cuenta para que el dibujante sepa
    pintarlos con su color y no con el de la costa. Y NADA MAS: la cuenta de
    puntos de un .map tiene que seguir siendo la que un lector de version 2
    entiende. Los anillos van aparte (ver ANILLO_BIT, arriba).
    """
    out = bytearray()
    for t in trazos:
        if len(t) >= RELLENO_BIT:
            sys.exit('mapa_pack: un trazo de %d puntos no cabe con los bits '
                     'de marca' % len(t))
        marca = (FRONTERA_BIT if frontera else 0)
        if relleno:
            marca |= RELLENO_BIT
        out += struct.pack('<H', len(t) | marca)
        out += struct.pack('<hh', t[0][0], t[0][1])
        ax, ay = t[0]
        for x, y in t[1:]:
            dx, dy = x - ax, y - ay
            if -127 <= dx <= 127 and -127 <= dy <= 127:
                out += struct.pack('<bb', dx, dy)
            else:
                out += b'\x80' + struct.pack('<hh', dx, dy)
            ax, ay = x, y
    out += struct.pack('<H', 0)      # el cero que cierra
    return bytes(out)


def por_pixeles(costa, fronteras, tope, con_fronteras=True,
                px_eps=1.0, px_isla=5.0, zoom=1):
    """El nivel, elegido en PIXELES de la pantalla y no en bytes.

    px_eps   el desvio que se admite en una costa, en pixeles
    px_isla  lo mas pequeño que se conserva, en pixeles

    Al zoom 1 la pantalla da 2,22 pixeles por grado, asi que un pixel son
    0,45 grados. Medido con esos numeros: 376 trazos (164 costa + 212
    frontera), 3.588 puntos, 9.487 bytes.

    Si no cabe en `tope` se cae a a_presupuesto() y se avisa por pantalla.
    """
    pxg = PX_GRADO * zoom
    eps = px_eps / pxg
    minimo = px_isla / pxg
    c = recorta(costa, minimo, eps)
    f = recorta(fronteras, minimo, eps * 1.2) if con_fronteras else []
    b = codifica(cuantiza(c + f))
    if len(b) <= tope:
        return b, len(c) + len(f), sum(len(t) for t in c + f), minimo, eps
    print('  AVISO: a %.2f px no cabe (%d B > %d). Se baja el detalle.'
          % (px_eps, len(b), tope))
    return a_presupuesto(costa, fronteras, tope, con_fronteras)


def a_presupuesto(costa, fronteras, tope, con_fronteras=True):
    """Lo que mas se parezca al mundo y quepa en `tope` bytes.

    *** EL CRITERIO DE ANTES ESTABA ROTO, Y LO CAZO UNA MEDIDA - 12/10/2026.

    Pedido por el usuario. Y
    era cierto: de 3.751 trazos de costa sobrevivian TREINTA Y CINCO.

    La version anterior probaba combinaciones de (minimo, eps) y se
    quedaba con la de eps MAS PEQUEÑO. Suena a "la mas fiel" y no lo es,
    porque eps y minimo miden cosas distintas y el criterio solo miraba
    una. Medido:

        tope  8.128 B -> islas>10.0  eps 0.344 : 107 trazos
        tope 16.384 B -> islas> 7.0  eps 0.177 : 169 trazos
        tope 24.576 B -> islas>10.0  eps 0.108 : 107 trazos   <-- MENOS

    Con MAS sitio salian MENOS islas, porque gastaba todo el presupuesto
    en alisar costas en vez de recuperar islas. Un criterio con el que dar
    mas memoria empeora el mapa no esta midiendo lo que dice medir.

    EL CRITERIO DE AHORA, en una frase: **la isla que se tira no puede ser
    mayor que el error que ya se acepta al simplificar**. Si admites 0,3
    grados de desvio en una costa, tirar una isla de 10 grados es
    incoherente - te estas comiendo algo treinta veces mayor que tu propia
    tolerancia-. Asi que minimo = 2*eps y queda UN solo parametro: se
    busca el eps mas pequeño que quepa.

    Y eso hace que mas presupuesto sea SIEMPRE mas mundo:

        tope  12.288 B ->  719 trazos ( 418 costa + 301 frontera),  4.052 pt
        tope  24.576 B -> 1277 trazos ( 930 costa + 347 frontera),  8.357 pt
        tope  49.152 B -> 2400 trazos (2018 costa + 382 frontera), 17.445 pt
        tope  98.304 B -> 4168 trazos (3751 costa + 417 frontera), 37.758 pt
                          ^^^^ el mundo entero, sin tirar un solo trazo
    """
    eps = 0.02
    while eps <= 0.90:
        minimo = 2.0 * eps
        c = recorta(costa, minimo, eps)
        f = recorta(fronteras, minimo, eps * 1.2) if con_fronteras else []
        b = codifica(cuantiza(c + f))
        if len(b) <= tope:
            return b, len(c) + len(f), sum(len(t) for t in c + f), minimo, eps
        eps *= 1.12
    sys.exit('mapa_pack: no cabe en %d bytes ni simplificando a 0,9 grados,\n'
             '           que ya es un mundo de palitos. Sube el presupuesto.'
             % tope)


# ------------------------------------------------------- niveles y casillas
#
# EL FORMATO v2: TRES NIVELES, Y LOS DE ARRIBA TROCEADOS - 12/10/2026.
#
# Ayudaba, y mucho. Lo dijo la medida y la decision salio de su pregunta:
#
#   - CUATRO niveles piden 263.000 B de flash -hay que subir el hueco de
#     192 a 256 kB y el usuario tiene que regenerar BD.BIN- y ademas el
#     zoom 2 troceado sale PEOR que el nivel entero de hoy.
#   - TRES -x1 entero, x4 y x8 troceados- caben en 70.605 B, o sea en el
#     hueco que ya existe, y no obligan a tocar nada mas.
#
# POR QUE EL x1 NO SE TROCEA. Porque al zoom 1 la ventana ES el mundo:
# las casillas serian puro gasto. Medido, el x1 troceado da 1,83 px de
# error donde el entero da 1,00. El zoom 2 sale del x1 estirado, con 2 px,
# que se ve bien.
#
# LOS TRAZOS SE PARTEN POR CASILLA, y eso NO es opcional. El primer
# intento metia cada trazo entero en la casilla de su primer punto y
# guardaba la caja de lo que ocupaba. Fallo medido: SEIS trazos de 3.724
# miden mas de 45 grados de ancho -el mayor, 189 grados con 5.914
# puntos-, asi que la caja de su casilla tapa media pantalla y esa
# casilla se carga casi siempre. La ventana peor se llevaba el 83 % del
# nivel. Partir cuesta un punto repetido por cruce y baja la ventana a la
# cuarta parte.
#
# EL FICHERO:
#
#     0   'DSDRMAP1'            8
#     8   version = 2           1
#     9   niveles               1
#    10   reservado             2
#    12   15 colores x 3       45
#    57   reservado             3
#    60   tabla de niveles, 16 B cada uno:
#           +0  u16 zoom        (1, 4, 8)
#           +2  u8  nx          casillas en X (0 = sin trocear)
#           +3  u8  ny
#           +4  u32 off         desde el principio del fichero
#           +8  u32 tam
#          +12  u16 piezas      (informativo)
#          +14  u16 reservado
#    ...  por nivel troceado: tabla de casillas, 16 B cada una:
#           +0  u32 off         desde el principio del fichero
#           +4  u32 tam
#           +8  i16 x0, y0, x1, y1   la caja, en 1/32 de grado
#         y detras los cuerpos, cada uno con el mismo formato de trazos
#         de siempre y su u16 cero de cierre.
#    fin  u32 suma FNV-1a de todo lo anterior

NIV_CAB = 16        # lo que mide una entrada de la tabla de niveles
CAS_CAB = 16        # lo que mide una entrada de la tabla de casillas


# --------------------------------------------- la paridad de cada casilla
#
# PARA QUE LA TIERRA SE RELLENE TAMBIEN AL ACERCARSE - 15/10/2026.
#
# El relleno de la radio es par-impar sobre los cruces de cada fila. Al
# zoom 1 eso basta porque la ventana ES el mundo: toda fila empieza en el
# borde del mapa, que es mar. Al acercarse no: una ventana metida dentro
# de un continente no tiene ninguna costa que cruzar y el par-impar la
# dejaria toda de mar.
#
# Lo que falta, entonces, no es el relleno: es LA PARIDAD DE PARTIDA. Un
# bit por fila que diga si el borde izquierdo de la casilla cae en tierra
# o en agua. Eso es lo que se escribe aqui.
#
# POR QUE UN BIT POR CASILLA Y NO UNO POR VENTANA. Porque sembrar solo en
# el borde izquierdo de lo que se ve deja que un error se propague hasta
# el otro lado de la pantalla: basta una isla que la simplificacion se
# haya comido para que todo lo que tenga a la derecha salga invertido. Con
# un bit en CADA casilla, la radio vuelve a sincronizar la paridad en cada
# borde de casilla y el daño de una isla perdida no sale de su casilla.
#
# POR QUE SE MIDE CONTRA LOS ANILLOS CERRADOS Y NO CONTRA LOS TRAZOS DEL
# NIVEL. Porque los anillos son la verdad -son los mismos que la radio usa
# para rellenar al zoom 1- y asi los dos rellenos no pueden contradecirse.
# Si el nivel troceado trae una costa simplificada, la diferencia es de un
# pixel de costa; si se midiera contra el propio nivel, un fallo del nivel
# se escribiria tambien en la semilla y nunca se notaria.
#
# EL RAYO VA HACIA EL NORTE, no hacia el este. Hacia el este habria que
# decidir donde empieza la cuenta en un cilindro, y la longitud -180 cae
# justo encima de las Aleutianas y de Taveuni. Hacia el norte la longitud
# es fija, no hay cilindro que valga, y los anillos estan cerrados contra
# el borde de +-71,5 grados, asi que la cuenta es exacta.
PAR_SUB   = 16           # muestras por grado de latitud
PAR_LAT_S = 1.0 / 16.0


def _cruces_meridiano(anillos, lon0):
    """Latitudes en las que los anillos cruzan el meridiano `lon0`."""
    ys = []
    for t in anillos:
        for i in range(len(t) - 1):
            x1, y1 = t[i]
            x2, y2 = t[i + 1]
            dx = x2 - x1
            # El antimeridiano: la arista de verdad va por el lado corto.
            if dx > 180.0:
                dx -= 360.0
            elif dx < -180.0:
                dx += 360.0
            if dx == 0.0:
                continue
            xb = x1 + dx
            # Y se prueba el meridiano tambien un mundo a cada lado, que es
            # lo que hace que una arista desenrollada cuente donde debe.
            for v in (-360.0, 0.0, 360.0):
                x = lon0 + v
                # Regla medio abierta en X: un vertice lo cuenta una sola
                # de las dos aristas que se juntan en el.
                if (x1 <= x < xb) or (xb <= x < x1):
                    ys.append(y1 + (y2 - y1) * (x - x1) / dx)
    ys.sort()
    return ys


def paridad_casillas(anillos, nx, ny):
    """Un bit por fila y casilla: 1 = el borde IZQUIERDO cae en tierra.

    Devuelve (bytes, bits_por_casilla). Las casillas van en el mismo orden
    que la tabla: cy * nx + cx.
    """
    import bisect

    alto = 180.0 / ny                      # grados de latitud por casilla
    nbit = int(round(alto * PAR_SUB))      # 360 con ny=8
    nbyte = (nbit + 7) // 8
    # Los cruces solo dependen de la COLUMNA, asi que se calculan nx veces
    # y no nx*ny: con dieciseis columnas y 300.000 aristas, la diferencia
    # son segundos contra minutos.
    por_col = [_cruces_meridiano(anillos, -180.0 + cx * (360.0 / nx))
               for cx in range(nx)]

    out = bytearray()
    for cy in range(ny):
        arriba = 90.0 - cy * alto
        for cx in range(nx):
            ys = por_col[cx]
            bits = bytearray(nbyte)
            for k in range(nbit):
                lat = arriba - (k + 0.5) * PAR_LAT_S
                # Cuantos cruces quedan POR ENCIMA: impar = tierra.
                if (len(ys) - bisect.bisect_right(ys, lat)) & 1:
                    bits[k >> 3] |= 1 << (k & 7)
            out += bits
    return bytes(out), nbit


def casilla_de(x, y, nx, ny):
    """En que casilla cae un punto en 1/32 de grado."""
    lon = x / float(CUANT)
    lat = y / float(CUANT)
    cx = int((lon + 180.0) / (360.0 / nx))
    cy = int((90.0 - lat) / (180.0 / ny))
    return max(0, min(ny - 1, cy)) * nx + max(0, min(nx - 1, cx))


# ------------------------------------------- los anillos de cada casilla
#
# POR QUE HACE FALTA ESTO - 16/10/2026, Y ES EL FALLO DE LAS RAYAS.
#
# El relleno de los niveles de cerca se pinta por par-impar sobre los
# trazos de la casilla. Pero los trazos de una casilla son COSTAS
# CORTADAS: cada pieza empieza y acaba en el borde, y una polilinea
# abierta no tiene "dentro". El par-impar sobre ellas no cierra, y lo que
# sale son tramos del color contrario que siempre acaban en un borde de
# casilla. Eso es la raya horizontal que se veia en la radio.
#
# Cortar una LINEA contra un rectangulo da trozos de linea. Lo que hace
# falta es cortar el POLIGONO, que da otro poligono: Sutherland-Hodgman.
# Cada casilla se lleva asi sus propios anillos cerrados, y el par-impar
# dentro de ella es exacto por construccion, sin tabla de paridad, sin
# semilla y sin nada que pueda contradecirse.
#
# Los lados que el recorte añade van por el borde de la casilla y no son
# costa, asi que estos anillos NO SE DIBUJAN: van marcados con
# RELLENO_BIT y el dibujante los usa solo para rellenar.

def _corta(a, b, v, eje):
    """Donde el segmento a-b cruza la recta eje=v."""
    if eje == 0:
        t = (v - a[0]) / float(b[0] - a[0])
        return (v, a[1] + (b[1] - a[1]) * t)
    t = (v - a[1]) / float(b[1] - a[1])
    return (a[0] + (b[0] - a[0]) * t, v)


def _lado(poli, dentro, v, eje):
    out = []
    n = len(poli)
    for i in range(n):
        a, b = poli[i - 1], poli[i]
        da, db = dentro(a), dentro(b)
        if db:
            if not da:
                out.append(_corta(a, b, v, eje))
            out.append(b)
        elif da:
            out.append(_corta(a, b, v, eje))
    return out


def recorta_poligono(poli, x0, y0, x1, y1):
    """Sutherland-Hodgman: la parte del anillo que cae en el rectangulo.

    Devuelve una lista de puntos SIN repetir el primero al final, o [] si
    el anillo no toca el rectangulo. Para un anillo concavo puede salir un
    poligono con lados pegados al borde; para rellenar por par-impar eso
    es correcto, que es justo para lo que se usa.
    """
    p = poli[:-1] if len(poli) > 1 and poli[0] == poli[-1] else list(poli)
    for dentro, v, eje in ((lambda q: q[0] >= x0, x0, 0),
                           (lambda q: q[0] <= x1, x1, 0),
                           (lambda q: q[1] >= y0, y0, 1),
                           (lambda q: q[1] <= y1, y1, 1)):
        if len(p) < 3:
            return []
        p = _lado(p, dentro, v, eje)
    return p if len(p) >= 3 else []


def anillos_por_casillas(anillos, nx, ny):
    """Para cada casilla, sus anillos recortados y cuantizados."""
    cubo = [[] for _ in range(nx * ny)]
    anchox = 360.0 / nx
    altoy = 180.0 / ny
    for cy in range(ny):
        y1 = 90.0 - cy * altoy
        y0 = y1 - altoy
        for cx in range(nx):
            x0 = -180.0 + cx * anchox
            x1 = x0 + anchox
            dest = cubo[cy * nx + cx]
            for an in anillos:
                xs = [q[0] for q in an]
                ys = [q[1] for q in an]
                if max(xs) < x0 or min(xs) > x1:
                    continue
                if max(ys) < y0 or min(ys) > y1:
                    continue
                r = recorta_poligono(an, x0, y0, x1, y1)
                if not r:
                    continue
                # a 1/32 de grado, cerrado, y sin puntos repetidos seguidos
                q = []
                for x, y in r:
                    pt = (int(round(x * CUANT)), int(round(y * CUANT)))
                    if not q or q[-1] != pt:
                        q.append(pt)
                while len(q) > 1 and q[0] == q[-1]:
                    q.pop()
                if len(q) >= 3:
                    q.append(q[0])          # cerrado: el par-impar lo exige
                    dest.append(q)
    return cubo


def parte_por_casillas(trazos, nx, ny):
    """Parte cada trazo donde cambia de casilla.

    El punto del cruce va en las DOS piezas: si no, la linea se corta en
    pantalla justo en el borde de la casilla y se ve una costura.
    """
    cubo = [[] for _ in range(nx * ny)]
    for t in trazos:
        act = casilla_de(t[0][0], t[0][1], nx, ny)
        cur = [t[0]]
        for p in t[1:]:
            c = casilla_de(p[0], p[1], nx, ny)
            if c != act:
                cur.append(p)
                if len(cur) >= 2:
                    cubo[act].append(cur)
                cur = [cur[-2], p] if len(cur) >= 2 else [p]
                act = c
            else:
                cur.append(p)
        if len(cur) >= 2:
            cubo[act].append(cur)
    return cubo


def nivel_entero(costa, fronteras, zoom, px_eps, px_isla):
    """Un nivel sin trocear: el cuerpo tal cual. Para el x1.

    AQUI ES DONDE VAN LOS ANILLOS, y solo aqui. Este nivel guarda los trazos
    ENTEROS, asi que un anillo sigue siendo un anillo y la radio lo puede
    rellenar. Los niveles troceados (nivel_troceado) parten cada trazo por
    las casillas, y un anillo partido ya no cierra: alli no hay mapa de bits
    y el mapa se dibuja como hoy.

    Devuelve (cuerpo, trazos, puntos, bits), donde `bits` es un bit por
    trazo diciendo cuales son anillos. VA APARTE DEL CUERPO a proposito: ver
    ANILLO_BIT arriba para el fallo que costo aprenderlo.

    Eso es deliberado y tiene consecuencia visible: el relleno se ve al
    zoom 1 y no al acercarse.

    LO QUE DECIA AQUI ESTABA MAL - 14/10/2026. Decia que haria falta
    "rellenar casilla por casilla, y eso es otro trabajo y otra medida".
    Se hizo la medida (deepsdr-ui/sim/mapa_troceado.c) y dice que no: el
    par-impar cuenta CRUCES, no anillos, y dos piezas de un trazo cortado
    comparten el punto del corte, asi que el juego de aristas es el mismo.
    Medido sobre la geometria de verdad, el 100% de los extremos sueltos
    estan en el borde de la VENTANA y ninguno en medio del mapa.

    Lo que falta de verdad es la PARIDAD de partida de cada fila, y esa
    sale del x1 -que lleva sus anillos y vive siempre en RAM-, no de aqui.
    O sea que el empaquetador no tiene que cambiar nada. Ver mapa.c.

    SE SIMPLIFICA ANTES DE CERRAR, no despues. Douglas-Peucker conserva los
    extremos, asi que un anillo simplificado sigue cerrando; pero los trazos
    que hay que cerrar contra el borde necesitan sus extremos EN el borde, y
    simplificar despues podria moverlos.
    """
    pxg = PX_GRADO * zoom
    an, li, cerrados, no_cerrables = anillos_y_lineas(costa)

    a = cuantiza(recorta(an, px_isla / pxg, px_eps / pxg))
    # Cuantizar puede dejar el ultimo punto igual al primero y quitarlo (el
    # bucle de cuantiza() salta los repetidos): hay que volver a cerrarlo, o
    # el anillo se queda abierto por un punto y el relleno se derrama.
    for t in a:
        if t[0] != t[-1]:
            t.append(t[0])
    l = cuantiza(recorta(li, px_isla / pxg, px_eps / pxg))
    f = cuantiza(recorta(fronteras, px_isla / pxg, (px_eps / pxg) * 1.2))

    # Anillos primero, despues las costas que no cierran, y al final las
    # fronteras. El cero de cierre lo pone SOLO el ultimo bloque. El orden
    # importa: es el que indexa el mapa de bits de aqui abajo.
    cuerpo = b''
    if a:
        cuerpo += codifica(a)[:-2]
    if l:
        cuerpo += codifica(l)[:-2]
    cuerpo += codifica(f, True)

    # EL MAPA DE BITS DE LOS ANILLOS.
    #
    # Un bit por trazo, en el MISMO orden en que van en el cuerpo: primero
    # los anillos, despues las costas que no cierran, y al final las
    # fronteras. Asi que los unos son los primeros len(a) bits y el resto
    # son ceros; se escribe el mapa entero de todas formas, porque el dia
    # que el orden cambie un mapa de bits sigue valiendo y un "los primeros
    # N" se convierte en un fallo mudo.
    n_trazos = len(a) + len(l) + len(f)
    bits = bytearray((n_trazos + 7) // 8)
    for i in range(len(a)):
        bits[i >> 3] |= 1 << (i & 7)

    nivel_entero.ultimo = (len(a), len(l), cerrados, no_cerrables,
                           sum(abs(area(t)) for t in an))
    return (cuerpo, n_trazos, sum(len(t) for t in a + l + f), bytes(bits))


def nivel_troceado(costa, fronteras, zoom, px_eps, px_isla, nx, ny,
                   viejo=False):
    """Un nivel con su tabla de casillas. Devuelve (cuerpo, piezas)."""
    pxg = PX_GRADO * zoom
    c = recorta(costa, px_isla / pxg, px_eps / pxg)
    f = recorta(fronteras, px_isla / pxg, (px_eps / pxg) * 1.2)
    # Dos cubos: costa y fronteras. Se parten por separado para no perder
    # cual es cual, y cada casilla lleva primero sus costas y detras sus
    # fronteras, marcadas.
    cubo_c = parte_por_casillas(cuantiza(c), nx, ny)
    cubo_f = parte_por_casillas(cuantiza(f), nx, ny)
    #
    # Y LOS ANILLOS DE RELLENO, RECORTADOS A CADA CASILLA - 16/10/2026.
    # Ver anillos_por_casillas() para el porque; aqui solo se pegan detras
    # de las fronteras, marcados, para que el dibujante los distinga.
    #
    cubo_r = ([[] for _ in range(nx * ny)] if viejo
              else anillos_por_casillas(anillos_y_lineas(c)[0], nx, ny))
    cubo = [cubo_c[i] + cubo_f[i] + cubo_r[i] for i in range(nx * ny)]

    cuerpos = []
    for i in range(nx * ny):
        if not cubo[i]:
            cuerpos.append(b'')
            continue
        tr = b''
        if cubo_c[i]:
            tr += codifica(cubo_c[i])[:-2]
        if cubo_f[i]:
            tr += codifica(cubo_f[i], True)[:-2]
        if cubo_r[i]:
            tr += codifica(cubo_r[i], False, True)[:-2]
        tr += struct.pack('<H', 0)
        cuerpos.append(tr)
    tabla = bytearray()
    off = nx * ny * CAS_CAB          # relativo al principio del NIVEL
    for i, cc in enumerate(cubo):
        if cc:
            xs = [p[0] for t in cc for p in t]
            ys = [p[1] for t in cc for p in t]
            caja = (min(xs), min(ys), max(xs), max(ys))
        else:
            caja = (0, 0, -1, -1)    # vacia: x1<x0, no toca nada
        tabla += struct.pack('<IIhhhh', off if cc else 0, len(cuerpos[i]),
                             caja[0], caja[1], caja[2], caja[3])
        off += len(cuerpos[i])
    return bytes(tabla) + b''.join(cuerpos), sum(len(cc) for cc in cubo)


# ----------------------------------------------------------------- suma
def fnv(b, v=0x811C9DC5):
    for x in b:
        v = ((v ^ x) * 16777619) & 0xFFFFFFFF
    return v


def escribe(ruta, colores, niveles, estilo=0, con_anillos=False):
    """Escribe el .map v2.

    `niveles` es una lista de (zoom, nx, ny, cuerpo, piezas, bits, par,
    nbit). nx=0 quiere decir "sin trocear": el cuerpo es la tira de trazos
    tal cual. `bits` es el mapa de bits de los anillos, o b'' si ese nivel
    no tiene. `par` es el mapa de paridad de las casillas, o b''.

    LA PARIDAD NO SUBE LA VERSION DEL FORMATO - 15/10/2026, y eso es a
    proposito. Va DENTRO del `tam` del nivel, detras de todo, y se anuncia
    en dos bytes que hasta hoy eran relleno a cero:

        byte 58   mascara de niveles que la traen (bit i = nivel i)
        byte 59   bytes por casilla

    Un firmware viejo no mira ninguno de los dos, lee la tabla de trazos
    igual que siempre y se para en el cero que la cierra, asi que un .map
    nuevo le sigue valiendo. Y un .map viejo trae los dos bytes a cero, o
    sea "ninguno la trae", que es el comportamiento de hoy: relleno solo al
    zoom 1. Los cien ficheros que ya estan por ahi no obligan a nada.

    Con `con_anillos`, el mapa de bits se pega DETRAS del cero que cierra la
    tabla de trazos y su longitud va en los dos bytes de relleno de la
    entrada de nivel. Sin el, no se escribe y el fichero es exactamente el
    que salia antes. En los dos casos la TABLA DE TRAZOS es identica, que es
    justo lo que hace que un firmware viejo siga dibujando bien.
    """
    for nombre in ORDEN:
        if nombre not in colores:
            sys.exit('mapa_pack: falta el color "%s"' % nombre)
    if not contrasta(colores['marca'], colores['mar']):
        sys.exit('mapa_pack: el punto (%06X) no se veria sobre el mar (%06X).\n'
                 '           Esto no se avisa, se corta: un mapa asi parece una\n'
                 '           radio que no decodifica nada.'
                 % (colores['marca'], colores['mar']))
    if not niveles:
        sys.exit('mapa_pack: un mapa sin niveles no es un mapa')
    if len(niveles) > NIV_MAX:
        sys.exit('mapa_pack: %d niveles, el formato admite %d'
                 % (len(niveles), NIV_MAX))

    cab = bytearray()
    cab += MARCA
    cab += bytes([VERSION, len(niveles), 0, 0])
    for nombre in ORDEN:
        c = colores[nombre]
        cab += bytes([(c >> 16) & 255, (c >> 8) & 255, c & 255])
    # El byte 57 era relleno a cero y reservado: ahora dice el estilo. Un
    # .map viejo trae cero ahi, que es ESTILO_CONTORNO, o sea lo que la
    # radio hace hoy. Por eso no hay que subir la version del formato.
    cab += bytes([estilo & 255, 0, 0])   # 58 y 59 se rellenan abajo
    assert len(cab) == CAB, len(cab)

    # La tabla de niveles va detras de la cabecera, y los cuerpos detras
    # de la tabla. Los desplazamientos son DESDE EL PRINCIPIO DEL FICHERO
    # para que el lector no tenga que sumar nada.
    off = CAB + NIV_MAX * NIV_CAB
    tabla = bytearray()
    cuerpos = []
    par_mascara = 0
    par_bytes = 0
    for i, (zoom, nx, ny, cuerpo, piezas, bits, par, nbit) in enumerate(niveles):
        anillos = bits if (con_anillos and bits) else b''
        if len(anillos) > 65535:
            sys.exit('mapa_pack: el mapa de bits de los anillos mide %d B y '
                     'el relleno de la entrada de nivel son dos bytes'
                     % len(anillos))
        par = par if (con_anillos and par) else b''
        if par:
            # La paridad va LA ULTIMA y los anillos siguen siendo los
            # ultimos del trozo que el lector viejo conoce. Hoy no se da el
            # caso -los niveles troceados no llevan anillos- y si algun dia
            # se diera, esto lo corta en vez de mover el mapa de bits bajo
            # los pies de un firmware que lo busca al final.
            if anillos:
                sys.exit('mapa_pack: un nivel con anillos Y paridad moveria '
                         'el mapa de bits de los anillos; no esta previsto')
            nb = (nbit + 7) // 8
            if nx == 0 or nx * ny == 0:
                sys.exit('mapa_pack: la paridad solo tiene sentido troceando')
            if len(par) != nx * ny * nb:
                sys.exit('mapa_pack: la paridad mide %d B y deberia medir %d'
                         % (len(par), nx * ny * nb))
            if nb > 255:
                sys.exit('mapa_pack: %d bytes por casilla no caben en el byte 59'
                         % nb)
            if par_bytes and par_bytes != nb:
                sys.exit('mapa_pack: dos niveles con distintos bytes por '
                         'casilla y en la cabecera solo cabe un numero')
            par_bytes = nb
            par_mascara |= 1 << i
        cuerpo = cuerpo + anillos + par
        cuerpos.append(cuerpo)
        tabla += struct.pack('<HBBIIHH', zoom, nx, ny, off, len(cuerpo),
                             min(piezas, 65535), len(anillos))
        off += len(cuerpo)
    tabla += b'\x00' * (NIV_MAX * NIV_CAB - len(tabla))

    cab[58] = par_mascara & 255
    cab[59] = par_bytes & 255

    cuerpo_todo = b''.join(cuerpos)
    entero = bytes(cab) + bytes(tabla) + cuerpo_todo
    todo = entero + struct.pack('<I', fnv(entero))
    with open(ruta, 'wb') as f:
        f.write(todo)
    return len(todo)


# LOS TRES NIVELES Y SU DETALLE, medido contra el buffer de la radio.
#
# El tope de la ventana es MAPA_BIN_RAM: lo que el dibujante puede tener
# cargado de una vez. Estos numeros salieron de barrer la pantalla por
# todo el mundo y quedarse con la ventana PEOR, no con una a ojo.
#
#   zoom  eps(px)  isla(px)   nivel      ventana peor
#    x1     1.00     5.00      9.487 B   (entero, residente)
#    x4     2.29    11.44     23.579 B      8.063 B
#    x8     2.86    14.31     37.539 B      7.480 B
#
# El zoom 2 sale del x1 estirado: 2 px de error, y troceado saldria peor
# (1,83 px propios contra 1,00 estirados del x1 entero).
NIVELES = (
    (1, 0,  0, 1.00,  5.00),    # nx=0: sin trocear
    (4, 16, 8, 2.29, 11.44),
    (8, 16, 8, 2.86, 14.31),
)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mundo')
    ap.add_argument('tipos', help='fichero con los colores de cada tipo (json)')
    ap.add_argument('--salida', default='mapas')
    ap.add_argument('--solo', default=None, help='un tipo, por su numero')
    #
    # EL FORMATO DE ANTES DEL 16/10/2026, SOLO PARA UN BANCO.
    #
    # Hasta ese dia los niveles troceados llevaban una TABLA DE PARIDAD y
    # ningun anillo propio: el relleno se arrancaba con la tabla en cada
    # borde de casilla. Eso sacaba rayas (ver User/mapa.c), y por eso hoy
    # cada casilla trae sus anillos cerrados y la tabla no se escribe.
    #
    # Pero los .map que ya estan grabados en las radios SI son asi, y la
    # radio tiene que seguir pintandolos. Lo vigila
    # deepsdr-ui/sim/mapa_tierra_zoom.c, y para eso necesita un fichero de
    # los de antes. Esta opcion se lo da. Para mapas de verdad no se usa:
    # los de verdad salen rayados.
    ap.add_argument('--viejo', action='store_true',
                    help='formato de antes del 16/10/2026: con tabla de '
                         'paridad y sin anillos por casilla (solo bancos)')
    a = ap.parse_args()

    import json
    costa, fronteras = lee_mundo(a.mundo)
    tipos = json.load(open(a.tipos, encoding='utf-8'))
    os.makedirs(a.salida, exist_ok=True)

    # LOS NIVELES SE CONSTRUYEN UNA VEZ, no uno por tipo: los cien
    # comparten la misma geometria y solo se diferencian en los colores.
    # Construirlos cien veces serian diez minutos de nada.
    print('  construyendo los %d niveles...' % len(NIVELES))
    # UNA SOLA VERSION DE CADA NIVEL. Antes se construian dos del nivel
    # entero -con los anillos marcados y sin marcar- porque la marca iba
    # DENTRO de la tabla de trazos. Ahora va fuera, asi que la geometria es
    # la misma para los cien tipos y lo unico que cambia es si se le pega el
    # mapa de bits o no. Media la mitad de trabajo y, lo que importa mas,
    # garantiza que los cien ficheros compartan tabla de trazos byte a byte.
    # LA PARIDAD SE CALCULA UNA SOLA VEZ PARA TODAS LAS CASILLAS, porque
    # no depende del nivel: es la verdad del mundo medida contra los
    # anillos cerrados. Lo unico que cambia entre niveles es cuantas
    # casillas hay, y hoy los dos troceados tienen las mismas.
    anillos_mundo, _l, _c, _nc = anillos_y_lineas(costa)
    par_cache = {}

    niveles = []
    for zoom, nx, ny, pe, pi in NIVELES:
        par, nbit = b'', 0
        if nx == 0:
            cuerpo, piezas, puntos, bits = nivel_entero(costa, fronteras,
                                                        zoom, pe, pi)
            if len(cuerpo) + len(bits) > RAM - 64:
                sys.exit('mapa_pack: el nivel x%d mide %d B y el buffer de la\n'
                         '           radio son %d. Ese nivel es el que vive\n'
                         '           ENTERO en la RAM, asi que no cabe.'
                         % (zoom, len(cuerpo) + len(bits), RAM))
        else:
            cuerpo, piezas = nivel_troceado(costa, fronteras, zoom, pe, pi,
                                            nx, ny, a.viejo)
            puntos, bits = 0, b''
            #
            # LA TABLA DE PARIDAD YA NO SE ESCRIBE - 16/10/2026.
            #
            # Servia para ARRANCAR el par-impar en el borde de cada
            # casilla, porque lo que habia dentro eran costas cortadas y
            # una polilinea abierta no tiene "dentro". Desde que cada
            # casilla lleva sus propios ANILLOS CERRADOS (ver
            # anillos_por_casillas) el par-impar se arranca solo y la
            # tabla no solo sobra: estorbaba, porque se sacaba de otra
            # geometria y podia contradecir a la que se dibuja.
            #
            # Son 5.760 bytes menos por nivel troceado. Se deja la funcion
            # paridad_casillas() porque la radio sigue sabiendo leer la
            # tabla, y un .map de los que ya estan por ahi la trae.
            #
            if a.viejo:
                if (nx, ny) not in par_cache:
                    par_cache[(nx, ny)] = paridad_casillas(anillos_mundo, nx, ny)
                par, nbit = par_cache[(nx, ny)]
            else:
                par, nbit = b'', 0
        niveles.append((zoom, nx, ny, cuerpo, piezas, bits, par, nbit))
        print('    x%-2d  %s  %7d B  %5d piezas%s%s'
              % (zoom, 'entero  ' if nx == 0 else 'troceado', len(cuerpo),
                 piezas, ('  + %d B de anillos' % len(bits)) if bits else '',
                 ('  + %d B de paridad (%d bits/casilla)' % (len(par), nbit))
                 if par else ''))
    if hasattr(nivel_entero, 'ultimo'):
        na, nl, cer, noc, ar = nivel_entero.ultimo
        print('    anillos de tierra %d, costas sin cerrar %d'
              ' (cerrados contra el borde %d, no cerrables %d)'
              % (na, nl, cer, noc))
        print('    encierran %.0f grados^2 de %.0f  ->  %.1f %% de la ventana'
              % (ar, 360.0 * 2 * LAT_TOPE, 100.0 * ar / (360.0 * 2 * LAT_TOPE)))

    hechos = 0
    for t in tipos:
        if a.solo and str(t['n']) != str(a.solo).lstrip('0'):
            continue
        ruta = os.path.join(a.salida, '%03d.map' % t['n'])
        est = estilo_de(t.get('nombre'))
        n = escribe(ruta, t['colores'], niveles, est,
                    con_anillos=(est != ESTILO_CONTORNO))
        hechos += 1
        if hechos <= 3 or a.solo:
            print('  %s  %6d B   %s'
                  % (os.path.basename(ruta), n, t.get('nombre', '')))
    print('\n  %d ficheros en %s' % (hechos, a.salida))


if __name__ == '__main__':
    main()
