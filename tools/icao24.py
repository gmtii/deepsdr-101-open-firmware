#!/usr/bin/env python3
"""
LA BASE DE MODELOS DE AVION, PARA LA ZONA ALTA DE LA FLASH.

QUE HACE. Coge el CSV de aeronaves de OpenSky -medio millon de aviones, unos
90 MB- y saca un ICAO24.BIN de unos cientos de kilobytes con lo unico que la
radio necesita: direccion OACI -> modelo.

POR QUE NO CABE TAL CUAL. La zona alta libre son 520.192 bytes y el CSV trae
matricula, operador, fabricante, numero de serie y veinte campos mas. Aqui se
tira todo menos el codigo de tipo, y se agrupan los tipos en una tabla: hay
medio millon de aviones pero solo unos cientos de codigos de tipo, asi que
cada avion ocupa 3 bytes de direccion y 2 de indice, y el nombre del tipo
-"A320", "B77W"- se escribe UNA vez.

EL FORMATO lo fija el lector del firmware (User/icao24_db.c), no este script:

    cabecera, 16 bytes
        'I','2','4','B'
        u8   version (1)
        u8   largo del nombre de tipo (24)
        u16  cuantos tipos
        u32  cuantos aviones
        u32  donde empieza la tabla de tipos (= 16 + 5*aviones)
    luego, un registro de 5 bytes por avion, ORDENADOS por direccion:
        3 bytes  direccion OACI, primero el byte alto
        u16      indice en la tabla de tipos, primero el byte bajo
    y al final, la tabla: un nombre de `largo` bytes por tipo, con ceros
    detras.

El orden importa: el firmware busca por biseccion. Un fichero desordenado no
da error, da MODELOS EQUIVOCADOS, asi que aqui se ordena siempre y ademas se
comprueba antes de escribir.

DE DONDE SALE EL CSV. De OpenSky, carpeta de metadatos:

    https://opensky-network.org/datasets/metadata/
    (boton "metadata/"; el de abajo del todo es el mas nuevo)

    el mas nuevo comprobado el 25/09/2026, 108 MB:
    https://s3.opensky-network.org/data-samples/metadata/aircraft-database-complete-2025-08.csv

Hay DOS formatos y los dos valen, porque aqui se mira cual es antes de leer:

    aircraftDatabase-AAAA-MM.csv            campos entre comillas dobles
    aircraft-database-complete-AAAA-MM.csv  campos entre comillas SIMPLES

El segundo es el que mantienen ahora. Los unicos campos que se usan son
"icao24", "typecode" y, si esta, "model".

COMO SE USA:

    python3 icao24.py aircraft-database-complete-2025-08.csv ICAO24.BIN [tope]

y despues se copia el ICAO24.BIN al disco USB de la radio y se carga desde
Informacion. Una vez cargado se puede borrar del disco.

EL TOPE, Y DE DONDE SALE - 28/09/2026.

La zona alta ya no es toda para los aviones: la comparten con la base de
emisoras. Pero el reparto NO esta escrito en ningun sitio fijo - lo decide
tools/datos_pack.py al juntar las dos en DATOS.BIN, mirando lo que miden
de verdad-.

Asi que el numero que hay que pasar aqui lo dice datos_pack.py: si al
juntarlas no caben, imprime exactamente cuanto pueden ocupar los aviones y
la orden que hay que repetir. El valor de fabrica de abajo es lo que cabe
hoy dejando sitio a las emisoras de ahora.

El flujo entero son tres ordenes:

    python3 emisoras_pack.py eibi.txt aoki.txt EMISORAS.BIN 2
    python3 icao24.py <el CSV de OpenSky> ICAO24.BIN 827392
    python3 datos_pack.py BD.BIN ICAO24.BIN EMISORAS.BIN

y lo que se copia al disco USB de la radio es BD.BIN, uno solo.
"""
import collections, csv, os, struct, sys

MAGIC      = b'I24B'
VERSION    = 1
NAME_LEN   = 24          # lo que espera icao24_db.c: no se toca

# SOLO EL CODIGO DE TIPO, NO EL MODELO ENTERO. Antes aqui se guardaba
# "A320 Airbus A320-214". Mirando lo que hace de verdad hfdl_modo.c al
# pintar la fila:
#
#     for (c = 0U; c < 5U && r->model[c] > 32; c++)
#
# cinco caracteres y para en el primer espacio. O sea que de esos 24 bytes
# la pantalla ensena "A320" y tira 19 a la basura. Y no da igual: guardando
# el modelo salian 4.300 nombres distintos -103 kB, el 22% del fichero- y
# guardando solo el codigo salen unos 400. Esos 90 kB son los que hacen que
# quepan los turbohelices.
REC_LEN    = 5
HDR_LEN    = 16

# LA ZONA ALTA ENTERA, que es el techo absoluto de lo que se puede pedir.
# 0x101000 .. 0x200000 desde el 02/10/2026 (antes acababa en 0x180000, ver
# ZA_TOPE en User/zona_alta.h). Esto NO es lo que le toca a los aviones
# -eso lo reparte datos_pack.py mirando lo que miden las dos bases-, es
# solo el "mas que esto no existe".
ZA_BYTES   = 0x200000 - 0x101000
# Lo que le queda a los aviones despues del directorio de DATOS.BIN y de
# la base de emisoras de hoy. Ver la cabecera: el numero bueno lo dice
# datos_pack.py, y se pasa como tercer argumento.
TOPE       = 294912

# QUIEN ENTRA Y POR QUE. En la zona alta caben ahora 1.040.384 bytes -eran
# 520.192 hasta el 02/10/2026- o sea unos 200.000 aviones contando solo los
# registros. El CSV de OpenSky de 2025 trae 616.743 filas; con codigo de
# tipo son unos 359.000. SIGUEN SIN CABER, aunque ahora sobren por menos.
#
# Que el sitio se doble no cambia el criterio: no se recorta a ojo, se
# recorta por lo unico que importa aqui, quien lleva radio de onda corta.
# Un L1P -un motor, helice- es un Cessna de aeroclub que no va a salir en
# la pantalla jamas y gasta sus cinco bytes igual. Lo que el sitio nuevo
# permite es bajar mas escalones de la lista, no meterlos todos.
#
# La columna "icaoAircraftClass" lo dice en tres letras: L2J = avion de ala
# fija, 2 motores, reactor. Los de HFDL son los de linea y carga; un L1P
# -un motor, helice- es un Cessna de aeroclub y no va a salir en la pantalla
# jamas, pero gasta sus cinco bytes igual.
#
# Dos escalones por si un dia el CSV engorda: primero los reactores, y los
# turbohelices grandes solo si despues de los reactores queda sitio.
CLASES_1 = ('L2J', 'L3J', 'L4J')          # reactores: estos no se tocan
CLASES_2 = ('L2T', 'L4T')                 # turbohelices grandes: si caben

# Para CSV viejos que no traen la columna de clase. Entonces no hay mas
# remedio que ir por el codigo de tipo, que es peor.
PREFIJOS_FUERA = ('C1', 'C2', 'P2', 'PA', 'R2', 'DR', 'BALL', 'GLID')


def pela(v):
    """Quita espacios y, si quedaran, las comillas de los bordes.

    El csv ya pela las comillas que le hemos dicho que son comillas. Esto es
    por si el fichero mezcla los dos estilos en la misma linea, que pasa.
    """
    v = (v or '').strip()
    if len(v) >= 2 and v[0] == v[-1] and v[0] in '\'"':
        v = v[1:-1].strip()
    return v


def que_comilla(ruta):
    """Mira la primera linea y dice con que comilla esta escrito el CSV.

    OpenSky cambio de formato: los "aircraftDatabase-*" llevan comillas
    dobles y los "aircraft-database-complete-*" las llevan SIMPLES. Si se lee
    el segundo como si fuera el primero, las columnas se llaman "'icao24'"
    -con comillas dentro del nombre- y no sale ni un avion.
    """
    with open(ruta, newline='', encoding='utf-8', errors='replace') as f:
        primera = f.readline()
    return "'" if primera.lstrip().startswith("'") else '"'


def clase_de(fila):
    """Las tres letras de clase OACI. El campo cambio de nombre entre los
    dos formatos: "icaoaircrafttype" en el viejo, "icaoAircraftClass" en el
    nuevo."""
    return (pela(fila.get('icaoAircraftClass'))
            or pela(fila.get('icaoaircrafttype'))).upper()


def mete(fila):
    """Una fila del CSV -> (direccion, codigo, clase, fabricante, modelo).

    Aqui NO se decide si entra: eso se hace despues, cuando ya se ha leido
    el fichero entero y se sabe que codigos de tipo son de reactor. Ver
    reparte_escalones().
    """
    ico = pela(fila.get('icao24')).lower()
    tipo = pela(fila.get('typecode')).upper()

    if len(ico) != 6 or any(c not in '0123456789abcdef' for c in ico):
        return None
    try:
        dir24 = int(ico, 16)
    except ValueError:
        return None
    if dir24 <= 0 or dir24 > 0xFFFFFF:
        return None
    if not tipo:
        return None
    return (dir24, tipo, clase_de(fila),
            pela(fila.get('manufacturerName')) or pela(fila.get('manufacturername')),
            pela(fila.get('model')))


# Palabras del nombre del fabricante que no dicen quien es. "The Boeing
# Company" es Boeing; "Airbus Industrie" es Airbus. Sin esto salia
# literalmente "B738 The BOEING 737-8AS", que empieza por "The".
FAB_FUERA = ('THE', 'COMPANY', 'CO', 'CO.', 'CORP', 'CORP.', 'CORPORATION',
             'INC', 'INC.', 'INDUSTRIE', 'INDUSTRIES', 'AIRCRAFT', 'AVIATION',
             'AEROSPACE', 'GMBH', 'LTD', 'LTD.', 'AG', 'SAS', 'S.A.', 'SA',
             'DE', 'MEXICO', 'CANADA', 'AMERICA', 'GROUP')


def fabricante(nombre):
    """"The Boeing Company" -> "Boeing". Cadena vacia si no queda nada."""
    for w in nombre.replace(',', ' ').split():
        if w.upper().strip('.') in FAB_FUERA:
            continue
        if not w[0].isalpha():
            continue
        return w[0].upper() + w[1:].lower()
    return ''


def guiona(texto):
    """"787 8" -> "787-8".

    OpenSky escribe la misma variante de ocho maneras y una de ellas es
    poner un espacio donde va el guion: "787 8", "A320 214", "A321 231".
    Solo se toca el espacio que separa DOS CIFRAS, que es donde siempre es
    un guion; "737 MAX 8" no cumple eso y se queda como esta.
    """
    fuera = []
    i = 0
    while i < len(texto):
        if (texto[i] == ' ' and i > 0 and i + 1 < len(texto)
                and texto[i - 1].isdigit() and texto[i + 1].isdigit()):
            fuera.append('-')
        else:
            fuera.append(texto[i])
        i += 1
    return ''.join(fuera)


def nombre_bonito(tipo, votos):
    """"B788" -> "B788 Boeing 787-8".

    POR QUE NO SE GUARDA EL MODELO DE CADA AVION. *** El dueno: "el modelo
    del avion no lo pone". *** Y tenia razon: "B788" es el codigo, no el
    modelo.

    Pero el modelo NO es un dato del avion, es un dato del TIPO: los 3.000
    787-8 del fichero dicen todos lo mismo. Lo que pasa es que lo dicen de
    ocho maneras -"787 8", "787-8", "Boeing 787-8", "B787-8"-, y guardarlas
    todas daba 4.300 nombres distintos, 103 kB, el 22% del fichero.

    Asi que se vota: para cada codigo, la forma que mas se repite. Quedan
    unos 400 nombres, los mismos que guardando solo el codigo, porque la
    entrada de la tabla mide 24 bytes lo lleve escrito o no. O sea que el
    nombre entero sale GRATIS: ya lo estabamos pagando y usabamos cuatro
    letras de veinticuatro.

    El codigo va DELANTE a proposito: es lo que siempre cabe, y si algun
    dia la columna se estrecha lo que se pierde es la cola.
    """
    if not votos:
        return tipo
    fab_crudo, mod = votos.most_common(1)[0][0]
    fab = fabricante(fab_crudo)
    texto = guiona(' '.join(mod.split()))

    # Si el modelo ya empieza por el fabricante -a veces en mayusculas,
    # "BOEING 737-8AS"- se quita de ahi y se pone el nuestro, bien escrito.
    if fab and texto.upper().startswith(fab.upper() + ' '):
        texto = texto[len(fab) + 1:]
    if fab and fab.upper() not in texto.upper():
        texto = (fab + ' ' + texto).strip()

    if not texto:
        return tipo
    return (tipo + ' ' + texto)[:NAME_LEN - 1]


MIOS = 'icao24_mios.csv'


def lee_mios(entrada_csv):
    """Los que pongo yo a mano: direccion OACI -> codigo de tipo.

    POR QUE HACE FALTA. La base de OpenSky tiene huecos, y no pocos: de los
    aviones que la radio habia cazado de verdad, varios -39D2B2, 71C735,
    AB0970, 010283- no estan ni en el CSV de 2025, ni en el de 2021, ni en
    hexdb.io, que es otra base distinta. Ahi no hay nada que arreglar en el
    filtro: el dato no existe.

    Asi que se puede anadir a mano, en un CSV de dos columnas:

        icao24,typecode
        71c735,B77W        # lo que sea, escrito por quien lo sepa

    Estos no pasan por el filtro de clases y TAMPOCO se caen en el reparto
    si el fichero no cabe: son los que a uno le importan, que para eso los
    ha escrito. Manda este fichero sobre el de OpenSky.

    Se busca al lado del script y al lado del CSV de entrada.
    """
    mios = {}
    sitios = [os.path.join(os.path.dirname(os.path.abspath(__file__)), MIOS),
              os.path.join(os.path.dirname(os.path.abspath(entrada_csv)), MIOS)]
    ruta = next((x for x in sitios if os.path.exists(x)), None)
    if ruta is None:
        return mios

    # Se quitan comentarios y lineas en blanco ANTES de darselo al csv. Si
    # no, la primera almohadilla se toma por la cabecera y no sale nada: el
    # fichero lleva encima una explicacion de veinte lineas, que es
    # justamente lo que hay que poder escribir en un fichero como este.
    with open(ruta, newline='', encoding='utf-8', errors='replace') as f:
        lineas = [(n, L) for n, L in enumerate(f, 1)
                  if L.strip() and not L.lstrip().startswith('#')]
    if not lineas:
        return mios
    if lineas[0][1].lstrip().lower().startswith('icao24'):
        lineas = lineas[1:]          # cabecera, si la trae

    for n, L in lineas:
        # El comentario al final de la linea tambien fuera. La explicacion
        # de este fichero lo ensena escrito asi -"71c735,B77W  # lo que
        # sea"- y sin esto el codigo de tipo acababa siendo
        # "B77W        # LO QUE SEA": un nombre plausible y equivocado, que
        # es exactamente lo que no puede pasar aqui.
        L = L.split('#', 1)[0]
        campos = [pela(c) for c in L.rstrip('\r\n').split(',')]
        ico = campos[0].lower() if campos else ''
        tipo = campos[1].upper() if len(campos) > 1 else ''
        # Seis caracteres NO basta: "-00001" mide seis y int(...,16) lo
        # acepta como -1. Con el enmascarado de abajo se guarda como
        # FFFFFF pero sorted() lo pone el PRIMERO, y entonces el fichero
        # deja de estar ordenado y la biseccion del firmware empieza a no
        # encontrar aviones que si estan. El guardia de ordenacion tampoco
        # lo veia, porque compara los enteros de Python y -1 < todo.
        if len(ico) != 6 or not tipo or any(c not in '0123456789abcdef' for c in ico):
            print('%s linea %d: se salta "%s"' % (MIOS, n, L.strip()[:40]),
                  file=sys.stderr)
            continue
        try:
            mios[int(ico, 16)] = tipo
        except ValueError:
            print('%s linea %d: "%s" no es hexadecimal' % (MIOS, n, ico),
                  file=sys.stderr)
    return mios


def busca_en_csv(entrada, lista):
    """Ensena las filas crudas de unas direcciones. Para cuando un avion no
    sale en la radio y hay que saber de quien es la culpa: si no esta en el
    CSV, del CSV; si esta, del filtro de aqui.

        python3 icao24.py --busca fichero.csv 71c735,39d2b2
    """
    quiero = {x.strip().lower() for x in lista.split(',') if x.strip()}
    csv.field_size_limit(4 * 1024 * 1024)
    comilla = que_comilla(entrada)
    vistos = set()
    with open(entrada, newline='', encoding='utf-8', errors='replace') as f:
        for fila in csv.DictReader(f, quotechar=comilla):
            ico = pela(fila.get('icao24')).lower()
            if ico not in quiero:
                continue
            vistos.add(ico)
            print('%s  tipo=%-6s clase=%-4s modelo=%s'
                  % (ico.upper(),
                     pela(fila.get('typecode')) or '(vacio)',
                     clase_de(fila) or '(vacia)',
                     pela(fila.get('model')) or '(vacio)'))
    for ico in sorted(quiero - vistos):
        print('%s  NO ESTA EN EL CSV' % ico.upper())
    return 0


def reparte_escalones(crudo, hay_clase):
    """direccion -> escalon (1 reactores, 2 turbohelices, 0 fuera).

    LO QUE ME COSTO UN FICHERO ENTERO. Filtrar solo por la columna de clase
    parecia limpio y dejaba fuera la mitad de los aviones que la radio habia
    cazado de verdad: 39D2B1 -un A350 de Air France-, 71C735, 4BB0F3...
    Todos reactores de linea, todos con su codigo de tipo en su sitio, y
    todos con la columna de clase VACIA. Son 90.000 en el fichero de hoy,
    uno de cada seis.

    Asi que la clase se usa para APRENDER, no para decidir: primero se mira
    que codigos de tipo aparecen marcados como reactor en alguna fila, y
    luego se admite a todos los que lleven uno de esos codigos, tengan o no
    tengan la columna. Si un A321 es un reactor en la fila de al lado, lo es
    tambien en esta.
    """
    if not hay_clase:
        # CSV viejo sin columna: no hay nada que aprender, se va por la lista
        # de prefijos de siempre.
        return {d: (0 if t.startswith(PREFIJOS_FUERA) else 1)
                for d, (t, _c) in crudo.items()}

    de_reactor, de_turbo = set(), set()
    for _d, (tipo, cls) in crudo.items():
        if cls in CLASES_1:
            de_reactor.add(tipo)
        elif cls in CLASES_2:
            de_turbo.add(tipo)
    de_turbo -= de_reactor          # si es de las dos cosas, gana reactor

    fuera = set()
    for _d, (tipo, cls) in crudo.items():
        if cls and cls not in CLASES_1 and cls not in CLASES_2:
            fuera.add(tipo)         # un tipo marcado siempre como otra cosa
    de_reactor -= fuera
    de_turbo -= fuera

    esc = {}
    for d, (tipo, _cls) in crudo.items():
        esc[d] = 1 if tipo in de_reactor else (2 if tipo in de_turbo else 0)
    return esc


def main():
    if len(sys.argv) == 4 and sys.argv[1] == '--busca':
        return busca_en_csv(sys.argv[2], sys.argv[3])
    if len(sys.argv) not in (3, 4):
        print(__doc__)
        return 1
    entrada, salida = sys.argv[1], sys.argv[2]

    global TOPE
    if len(sys.argv) == 4:
        try:
            TOPE = int(sys.argv[3])
        except ValueError:
            print('el tope tiene que ser un numero de bytes', file=sys.stderr)
            return 1
        if TOPE < HDR_LEN + NAME_LEN or TOPE > ZA_BYTES:
            print('el tope tiene que estar entre %d y %d bytes'
                  % (HDR_LEN + NAME_LEN, ZA_BYTES), file=sys.stderr)
            return 1

    # Y ademas se comprueba que la entrada NO es ya un ICAO24.BIN. Pasarle
    # el .BIN en vez del CSV es el error facil de cometer -son los dos
    # ficheros de esta carpeta- y sin esto el csv lo lee como texto
    # binario, no encuentra la columna "icao24" y dice "0 aviones", que no
    # explica nada.
    try:
        with open(entrada, 'rb') as f:
            if f.read(4) == MAGIC:
                print('%s ya es un ICAO24.BIN, no el CSV de OpenSky.\n'
                      'Esto toma el CSV: aircraft-database-complete-AAAA-MM.csv'
                      % os.path.basename(entrada), file=sys.stderr)
                return 1
    except OSError as e:
        print('no puedo abrir %s: %s' % (entrada, e), file=sys.stderr)
        return 1

    # El CSV nuevo trae campos de 200 kB -notas enteras- y el csv de Python
    # se planta a los 128 kB con "field larger than field limit". No es un
    # fichero roto, es que el limite viene bajo de fabrica.
    csv.field_size_limit(4 * 1024 * 1024)

    crudo = {}                               # direccion -> (codigo, clase)
    votos = {}                               # codigo -> Counter de (fab, modelo)
    leidas = 0

    comilla = que_comilla(entrada)
    with open(entrada, newline='', encoding='utf-8', errors='replace') as f:
        lector = csv.DictReader(f, quotechar=comilla)
        cabeceras = [c.strip().strip('\'"') for c in (lector.fieldnames or [])]
        hay_clase = ('icaoAircraftClass' in cabeceras
                     or 'icaoaircrafttype' in cabeceras)
        for fila in lector:
            leidas += 1
            r = mete(fila)
            if r is None:
                continue
            dir24, tipo, cls, fab, mod = r
            # intern: hay 616.000 filas y unos 400 codigos distintos. Sin
            # esto son 616.000 cadenas iguales en memoria.
            crudo[dir24] = (sys.intern(tipo), sys.intern(cls))
            if mod:
                votos.setdefault(tipo, collections.Counter())[
                    (sys.intern(fab), sys.intern(mod))] += 1

    escalon_de = reparte_escalones(crudo, hay_clase)

    # Los mios entran por encima de todo: del filtro y del reparto.
    mios = lee_mios(entrada)
    for dir24, tipo in mios.items():
        crudo[dir24] = (sys.intern(tipo), 'MIO')
        escalon_de[dir24] = 1

    if not crudo:
        print('no ha salido ni un avion: mira que el CSV tenga columnas '
              '"icao24" y "typecode"', file=sys.stderr)
        return 1

    # LOS ESCALONES. Se arma con todo; si no cabe, se cae el escalon 2 -los
    # turbohelices- y se vuelve a medir. Antes esto era un error y te dejaba
    # sin fichero; ahora saca el que cabe y te dice que ha quitado.
    quitado = ''
    for tope_escalon in (2, 1):
        tipos, indice_de, aviones = [], {}, {}
        for dir24, (texto, _cls) in crudo.items():
            escalon = escalon_de[dir24]
            if escalon == 0 or escalon > tope_escalon:
                continue
            if texto not in indice_de:
                indice_de[texto] = len(tipos)
                tipos.append(nombre_bonito(texto, votos.get(texto)))
            aviones[dir24] = indice_de[texto]
        if len(tipos) > 0xFFFF:
            print('mas de 65.535 modelos distintos: el indice es de 16 bits',
                  file=sys.stderr)
            return 1
        total = HDR_LEN + REC_LEN * len(aviones) + NAME_LEN * len(tipos)
        if total <= TOPE:
            break
        quitado = ('  (no cabian los %d turbohelices, se han quitado)'
                   % sum(1 for e in escalon_de.values() if e == 2))

    if not aviones:
        # `crudo` no vacio no quiere decir que quede alguno DESPUES del
        # filtro. Sin esto se escribia un fichero de 16 bytes con exito, y
        # la radio lo rechazaba luego con un mensaje que hablaba de otra
        # cosa ("la cabecera se sale de la zona alta") y se quedaba sin
        # base hasta reiniciar.
        print('el filtro ha dejado fuera a TODOS los aviones: mira que el '
              'CSV traiga reactores (columna "icaoAircraftClass")',
              file=sys.stderr)
        return 1

    orden = sorted(aviones.items())          # por direccion, que es como busca

    # ULTIMO RECURSO. Si ni con los reactores solos cabe, antes esto se
    # rendia y te dejaba sin fichero. Es peor de lo que parece: un fichero a
    # medias funciona -el que no esta sale en blanco, que es lo mismo que
    # pasa hoy con todos-, y ninguno no funciona.
    #
    # Se quita repartido por todo el rango, no por el final. Las direcciones
    # OACI van por paises en bloques: cortar por arriba seria quedarse sin
    # Japon entero, y en pantalla eso parece una averia. Quitando uno de
    # cada tantos, lo que falta esta igual de repartido que lo que hay.
    if HDR_LEN + REC_LEN * len(orden) + NAME_LEN * len(tipos) > TOPE:
        caben = (TOPE - HDR_LEN - NAME_LEN * len(tipos)) // REC_LEN
        if caben < 1:
            print('no cabe ni la tabla de modelos: %d modelos a %d bytes'
                  % (len(tipos), NAME_LEN), file=sys.stderr)
            return 1
        n = len(orden)
        # Los de icao24_mios.csv se sacan del sorteo: se reparte entre los
        # demas y luego se vuelven a meter. Si uno se ha molestado en
        # escribir una direccion a mano es justo la que no puede faltar.
        fijos = [x for x in orden if x[0] in mios]
        sueltos = [x for x in orden if x[0] not in mios]
        sitio = caben - len(fijos)
        if sitio < 0:
            print('hay mas aviones a mano (%d) que sitio (%d)'
                  % (len(fijos), caben), file=sys.stderr)
            return 1
        m = len(sueltos)
        orden = sorted(fijos + [sueltos[(i * m) // sitio] for i in range(sitio)]) \
            if sitio > 0 else sorted(fijos)
        quitado = ('  (no cabian los %d, se ha guardado el %.0f%%, '
                   'repartido; los %d a mano intactos)'
                   % (n, 100.0 * len(orden) / n, len(fijos)))

    # Se vuelve a medir aqui y no antes: el reparto de arriba pudo quitar
    # aviones, y la cabecera lleva el numero. El assert de mas abajo esta
    # justo para esto.
    total = HDR_LEN + REC_LEN * len(orden) + NAME_LEN * len(tipos)
    tipos_off = HDR_LEN + REC_LEN * len(orden)
    out = bytearray()
    out += MAGIC
    out += struct.pack('<BBHII', VERSION, NAME_LEN, len(tipos), len(orden), tipos_off)
    for dir24, idx in orden:
        out += bytes(((dir24 >> 16) & 0xFF, (dir24 >> 8) & 0xFF, dir24 & 0xFF))
        out += struct.pack('<H', idx)
    for t in tipos:
        b = t.encode('ascii', 'replace')[:NAME_LEN - 1]
        out += b + b'\0' * (NAME_LEN - len(b))

    # Un `assert` aqui desaparece con python3 -O, y esta es la ultima
    # comprobacion de que la cabecera dice lo que el fichero trae.
    if len(out) != total:
        print('la cuenta de la cabecera (%d) no cuadra con lo escrito (%d)'
              % (total, len(out)), file=sys.stderr)
        return 1

    # COMPROBACION ANTES DE ESCRIBIR. El firmware busca por biseccion: si el
    # fichero no esta ordenado no falla, MIENTE. Se mira aqui porque aqui se
    # puede, y en la radio no.
    for i in range(1, len(orden)):
        if orden[i][0] <= orden[i - 1][0]:
            print('fichero desordenado o con repetidos en %06X' % orden[i][0],
                  file=sys.stderr)
            return 1

    with open(salida, 'wb') as f:
        f.write(out)

    print('  leidas del CSV ...... %d' % leidas)
    print('  aviones guardados ... %d%s' % (len(orden), quitado))
    print('  tipos distintos ..... %d' % len(tipos))
    if mios:
        puestos = sum(1 for d, _ in orden if d in mios)
        print('  puestos a mano ...... %d de %d (%s)'
              % (puestos, len(mios), MIOS))
    print('  %s ...... %d bytes (%.0f%% de los %d que caben)'
          % (os.path.basename(salida), total, 100.0 * total / TOPE, TOPE))
    return 0


if __name__ == '__main__':
    sys.exit(main())
