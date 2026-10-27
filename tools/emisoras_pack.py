#!/usr/bin/env python3
"""
Empaqueta las listas EiBi y Aoki en EMISORAS.BIN, que es lo que la radio
lee para decir quien emite en la frecuencia que estas oyendo.

  python3 emisoras_pack.py eibi.txt aoki.txt EMISORAS.BIN [horas_utc]
                           [--hfcc hfcc.txt] [--aoki-xy aoki_xy.txt]
                           [--qth JN97] [--radio 3000] [--pais HNG,D,I]

`horas_utc` es CUANTO HAY QUE RESTARLE AL RELOJ DE LA RADIO PARA TENER
UTC. Va aqui y no en el firmware por una razon concreta: el reloj de la
radio marca hora local española y las dos listas dan las horas en UTC, asi
que sin este numero la radio enseñaria la emisora de hace dos horas - un
fallo especialmente feo porque el resultado PARECE bueno.

Por defecto 2, y no es un numero al azar: la lista A26 vale del 29 de
marzo al 25 de octubre de 2026, que es EXACTAMENTE el periodo de horario
de verano europeo. Mientras esa lista vale, España esta en UTC+2 siempre.
Para la lista de invierno (B26) hay que pasar 1.

LAS LISTAS NO SON LA MISMA COSA, y por eso se meten las tres:

  EiBi   8.226 filas. Lleva radiodifusion Y utilitarias: balizas de
         tiempo, fax meteorologico, radio maritima, aviacion. Trae el
         AREA DE DESTINO (a quien apunta la antena).
  Aoki   5.352 filas. Solo radiodifusion, pero trae el SITIO DESDE EL QUE
         SE EMITE, que EiBi no da. Con --aoki-xy se lee ademas su version
         ancha, que da ese sitio entero y con coordenadas.
  HFCC   3.114 filas, con --hfcc. No es una recopilacion: es el REGISTRO
         donde las emisoras apuntan sus transmisiones antes de emitirlas.
         Trae el emplazamiento con coordenadas, la potencia y el azimut, y
         es la unica que esta en la temporada que corre. No lleva
         utilitarias: esas no se coordinan ahi.

EiBi y Aoki se fusionan por (frecuencia, hora, emisora), como siempre: lo
que esta en las dos sale una vez, con el sitio de Aoki y el destino de
EiBi.

HFCC NO se puede fusionar asi, porque el nombre es justo lo que las tres
escriben distinto -"CNR 1 Voice of China", "China National Radio", "CHINA
RADIO INTERNATIONAL"-. Lo que si identifica una transmision son tres cosas
que las tres escriben igual: la FRECUENCIA, el PAIS y que las HORAS SE
SOLAPEN. Donde el registro tiene algo que decir de un par (frecuencia,
pais), manda el registro y las filas de la temporada anterior se apartan;
las utilitarias nunca se apartan. El detalle esta en el comentario de la
pasada, en main().

QUIEN SALE PRIMERO, Y POR QUE HACE FALTA DECIRLO - 05/10/2026.

En 540 kHz hay DIEZ emisoras y todas emiten las veinticuatro horas. La
radio pide tres resultados y pinta uno, asi que de esas diez se ve UNA.
Hasta hoy las entradas de una frecuencia iban ordenadas por nombre de
emisora, o sea que esa una era siempre la misma para todo el mundo:

    540  CHN  CNR 1 Voice of China     Jagdaqi-Hei      <- por la C
    540  J    NHK AM Ishigaku          Okinawa-Ish
    ...
    540  HNG  P1-Kossuth Radio         Solt             <- la octava
    540  KWT  R.KUWAIT                 Sulaibiyah
    540  THA  Yaan Kraw haa-sii-suun   Bangkok

Lo conto alguien que escucha desde Hungria: esperaba Kossuth, que emite
desde Solt a setenta kilometros de su casa, y le salia una de Mongolia
Interior a seis mil setecientos. No es un fallo de la base - Kossuth
estaba dentro- sino del ORDEN.

Con --qth las entradas de cada frecuencia salen ordenadas por lo lejos
que esta el transmisor de ti, y entonces la que pinta la radio es la de
Solt. El firmware NO se toca: emisoras_busca() ya devuelve primero las
que estan emitiendo ahora y dentro de eso respeta el orden del fichero.

    --qth JN97          tu cuadricula Maidenhead (2, 4, 6 u 8 caracteres)
    --qth 47.5,19.05    o la latitud y la longitud en grados
    --radio 3000        tira lo que este a mas de 3.000 km. Para hacerse
                        una base pequeña y de la zona; sin esto no se tira
                        nada, solo se ordena
    --pais HNG,D,I      deja solo esos codigos ITU

El sitio sale de dos tablas que estan al lado de este guion:
sitios_xy.csv (el centro emisor, cuando se conoce; manda) y
paises_xy.csv (el pais, para todo lo demas). Las dos se pueden editar sin
tocar codigo, que es justo lo que hace falta para afinar una zona.

TRES COSAS HONRADAS SOBRE ESTE ORDEN:

  - Lo que ordena es la distancia al TRANSMISOR, no quien se oye mejor.
    En onda media son casi lo mismo. En onda corta NO: un relé de 500 kW
    apuntandote desde Issoudun te llega mejor que uno local de 1 kW, y la
    ionosfera tiene una zona de silencio donde lo cercano no se oye.
  - El pais es un centroide. Para Hungria da igual; para Rusia, Estados
    Unidos, China o Canada puede caer a miles de kilometros del
    transmisor. Por eso existe sitios_xy.csv.
  - Lo que no tiene sitio conocido -clandestinas, "estacion
    desconocida"- se va al final de su frecuencia, no se tira. Al acabar
    se dice cuantas son.

SIN --qth el fichero sale EXACTAMENTE igual que antes, byte a byte. Eso
lo comprueba el banco, no la buena voluntad.

FORMATO DEL FICHERO. Todo entero es de menor peso primero (little-endian).

  cabecera, 56 bytes
    0   'E','M','I','S'
    4   u8  version = 2   (esta linea decia 1 y el codigo empaqueta 2, igual
                          que User/emisoras.c exige 2 - corregido el
                          30/09/2026, que las dos especificaciones estaban
                          copiadas con el mismo error)
    5   u8  n_paises            cuantas cadenas del final son codigos ITU
    6   u16 n_textos
    8   u32 n_frecuencias
    12  u32 n_entradas
    16  u32 off_frecuencias
    20  u32 off_entradas
    24  u32 off_textos
    28  u32 off_cadenas         donde empiezan las cadenas (= off_textos + 2*n_textos)
    32  u32 tam_total           lo que mide el fichero entero
    36  u32 valido_hasta        aaaammdd, para que la radio pueda avisar
                                de que la lista ha caducado
    40  i8  horas_utc           lo que hay que restarle al reloj local
    41  7 bytes a cero
    48  u32 suma_cabecera       de los bytes 0..47
    52  u32 suma_datos          de los bytes 56..tam_total

  POR QUE HAY DOS SUMAS, Y NO SOLO CAMPOS REDUNDANTES. La primera version
  se apoyaba en que los desplazamientos cuadraran entre si, y el banco la
  midio cambiando un byte cada vez: se colaban SIETE de treinta y seis. La
  peor era la del byte 5, n_paises, porque desplaza el indice de pais y
  entonces en la columna del pais sale un NOMBRE DE EMISORA. Un dato
  inventado con pinta de bueno, que es exactamente lo que no puede pasar.

  Con la suma de cabecera no se cuela ninguna. La de datos no se mira al
  abrir -serian 206 kB por un bus lento- sino al cargar el fichero, una
  vez, que es cuando de verdad puede haberse copiado mal.

  frecuencias: n_frecuencias registros de 8 bytes, ORDENADOS por hz
    0   u32 hz
    4   u16 primera entrada
    6   u16 cuantas entradas

  entradas: n_entradas registros de 12 bytes
    0   u16 ini      minuto UTC, 0..1440
    2   u16 fin      minuto UTC; si fin <= ini, cruza la medianoche
    4   u8  dias     bit0=lunes .. bit6=domingo
    5   u8  pais     numero de pais; el indice de texto es
                                n_textos - n_paises + pais
    6   u16 nombre   indice de texto
    8   u16 sitio    indice de texto, 0xFFFF si no se sabe
    10  u16 idioma   indice de texto, 0xFFFF si no se sabe

  textos: n_textos u16 de desplazamiento, y detras las cadenas acabadas
          en cero. El desplazamiento es desde el principio de las cadenas,
          no desde el principio del fichero.

POR QUE ORDENADO POR FRECUENCIA Y NO POR NOMBRE: la radio pregunta
siempre lo mismo -"quien hay en estos kilohercios"- y con la tabla
ordenada eso es una busqueda binaria de once lecturas sobre un bus SPI
lento. Ordenarlo de otra forma obligaria a recorrerlo entero.
"""
import io
import math
import os
import re
import struct
import sys


def lee_eibi(ruta):
    filas = []
    for l in open(ruta, encoding='cp1252', errors='replace').read().split('\n'):
        if not re.match(r'^\d', l) or len(l) < 57:
            continue
        filas.append(dict(khz=l[0:14].strip(), t=l[14:23].strip(),
                          d=l[24:30].strip(), itu=l[30:34].strip(),
                          est=l[34:57].strip(), lang=l[57:63].strip(),
                          sitio=''))
    return filas


def lee_aoki(ruta):
    filas = []
    for l in open(ruta, encoding='cp1252', errors='replace').read().split('\n'):
        if not re.match(r'^\d', l) or len(l) < 60:
            continue
        # Las columnas salen de la linea de '+---+' de la cabecera del
        # fichero, no de contarlas a ojo: los '+' estan en 0, 14, 24, 30,
        # 34, 59, 63 y 75.
        filas.append(dict(khz=l[0:14].strip(), t=l[14:24].strip(),
                          d=l[75:].strip(), itu=l[30:34].strip(),
                          est=l[34:59].strip(), lang=l[59:63].strip(),
                          sitio=l[63:75].strip()))
    return filas



# ----------------------------------------------------------------------
# HFCC: EL REGISTRO OFICIAL - 26/10/2026, pedido por el usuario.
# ----------------------------------------------------------------------
#
# Las dos listas de siempre son RECOPILACIONES: alguien escucha, apunta y
# publica. La HFCC (High Frequency Coordination Conference) es otra cosa -
# es donde las emisoras REGISTRAN sus transmisiones antes de emitirlas,
# temporada por temporada, para repartirse las frecuencias. O sea que trae
# cosas que una recopilacion no puede traer:
#
#   - El EMPLAZAMIENTO del transmisor con sus coordenadas, en site.txt.
#     Aoki da el sitio recortado a doce caracteres y sin posicion; EiBi no
#     lo da. Esto lo da entero y situado.
#   - La POTENCIA y el AZIMUT, que de momento no se guardan pero estan.
#   - Y, sobre todo, LA TEMPORADA DE VERDAD. Cuando se escribio esto, EiBi
#     y Aoki seguian publicando la A26 -verano- cinco dias despues de que
#     empezara el invierno. HFCC ya tenia la B26 al dia.
#
# Lo que NO trae: utilitarias. Las balizas horarias, el fax meteorologico,
# el marítimo y la aviacion no se coordinan aqui porque no son
# radiodifusion. Esas siguen saliendo de EiBi, que las marca con un guion
# delante del idioma (-TS, -CW, -TY, -HF, -MX), y no caducan de temporada
# en temporada como los horarios de las emisoras.
#
# EL FORMATO es de ancho fijo y lo dice el propio fichero en su cabecera:
#
#   ;----+----+----+---...---+---+----+-------+---+---+-------+------+---...
#   ;FREQ STRT STOP CIRAF ZONES  LOC POWR AZIMUTH SLW ANT DAYS  FDATE  TDATE ...
#
# Las columnas de abajo salen de contar los '+' de esa regla, no de mirar
# el fichero a ojo. Si algun dia la cambian, lo que hay que cambiar es esta
# tabla y nada mas.
HFCC_COLS = ((1, 5, 'freq'), (6, 10, 'ini'), (11, 15, 'fin'),
             (47, 50, 'loc'), (51, 55, 'kw'), (56, 63, 'az'),
             (72, 79, 'dias'), (87, 93, 'hasta'), (102, 112, 'lang'),
             (113, 116, 'adm'), (117, 120, 'brc'))


def hfcc_grados(txt):
    """'43N17' o '077E00' -> grados decimales. None si no se entiende."""
    m = re.match(r'^(\d+)([NSEW])(\d+)$', txt.strip())
    if not m:
        return None
    v = float(m.group(1)) + float(m.group(3)) / 60.0
    return -v if m.group(2) in 'SW' else v


def lee_hfcc_sitios(ruta):
    """site.txt: codigo -> (nombre, (lat, lon)).

    Es la tabla que hace que esto valga la pena: 1.200 emplazamientos con
    su nombre entero y su posicion, que es lo que ni EiBi ni el Aoki corto
    dan.
    """
    tabla = {}
    try:
        f = io.open(ruta, encoding='cp1252', errors='replace')
    except IOError:
        return tabla
    with f:
        for l in f:
            l = l.rstrip('\n\r')
            if not l or l.startswith(';') or len(l) < 42:
                continue
            cod = l[0:3].strip()
            nom = l[4:34].strip()
            xy = (hfcc_grados(l[39:44]), hfcc_grados(l[45:51]))
            if not cod:
                continue
            tabla[cod] = (nom, xy if None not in xy else None)
    return tabla


def lee_hfcc_emisoras(ruta):
    """broadcas.txt: codigo -> nombre de la emisora."""
    tabla = {}
    try:
        f = io.open(ruta, encoding='cp1252', errors='replace')
    except IOError:
        return tabla
    with f:
        for l in f:
            l = l.rstrip('\n\r')
            if not l or l.startswith(';') or len(l) < 5:
                continue
            cod, nom = l[0:3].strip(), l[4:].strip()
            if cod and nom:
                tabla[cod] = nom
    return tabla


def lee_hfcc(ruta):
    """Las transmisiones registradas, ya con el sitio y la posicion puestos.

    Las tablas de referencia se buscan AL LADO del fichero y con el nombre
    que les pone este proyecto al guardarlas. Si no estan, se sigue: se
    pierde el nombre largo del sitio y la posicion, no la transmision.
    """
    aqui = os.path.dirname(os.path.abspath(ruta))
    sitios = lee_hfcc_sitios(os.path.join(aqui, 'hfcc_sitios.txt'))
    emis = lee_hfcc_emisoras(os.path.join(aqui, 'hfcc_emisoras.txt'))
    filas = []
    try:
        f = io.open(ruta, encoding='cp1252', errors='replace')
    except IOError:
        return None
    with f:
        for l in f:
            l = l.rstrip('\n\r')
            if not l or l.startswith(';') or len(l) < 120:
                continue
            v = dict((n, l[a:b].strip()) for a, b, n in HFCC_COLS)
            if not re.match(r'^\d+$', v['freq']):
                continue
            nom, xy = sitios.get(v['loc'], ('', None))
            filas.append(dict(khz=v['freq'],
                              t=v['ini'] + '-' + v['fin'],
                              d=v['dias'],
                              itu=v['adm'],
                              est=emis.get(v['brc'], v['brc']),
                              lang=v['lang'],
                              sitio=nom or v['loc'],
                              xy=xy,
                              hasta=v['hasta']))
    return filas


def dias_hfcc(d):
    """HFCC numera 1 = DOMINGO, igual que Aoki y al reves que EiBi.

    Lo dice su propia tabla publicada: "Days of operation (Sun=1)". Como el
    formato es siempre la lista de digitos -'1234567', '23456', '17'- y
    nunca un rango con guion, basta con esto.
    """
    d = d.strip()
    if d == '' or d == '1234567':
        return 0x7F
    if re.match(r'^[1-7]+$', d):
        return sum(1 << ((int(c) + 5) % 7) for c in d)
    return 0x7F


def hfcc_valido(filas):
    """La caducidad: la fecha TDATE mas tardia de todas las filas.

    En ddmmaa, que es como la escribe HFCC. Se coge la mayor y no la
    primera porque las transmisiones no acaban todas el mismo dia: la
    temporada acaba cuando acaba la ultima.
    """
    mejor = 0
    for v in filas:
        m = re.match(r'^(\d{2})(\d{2})(\d{2})$', v.get('hasta', '') or '')
        if not m:
            continue
        aaaammdd = (2000 + int(m.group(3))) * 10000 + int(m.group(2)) * 100 \
                   + int(m.group(1))
        if aaaammdd > mejor:
            mejor = aaaammdd
    return mejor or None


def lee_aoki_xy(ruta):
    """El Aoki ANCHO (xta26.txt): sitio entero -> (lat, lon).

    Es el mismo listado que aoki.txt fila por fila, pero sin recortar y con
    la posicion del transmisor en grados, minutos y segundos. Aqui no se
    usa como fuente de filas -eso rompería el "byte a byte igual que antes"
    del fichero de siempre- sino SOLO como tabla de posiciones, que es lo
    que le falta a la version corta.

    La clave se guarda recortada a doce caracteres ADEMAS de entera, porque
    eso es justo lo que aoki.txt trae en su columna de sitio y es por donde
    lo va a buscar donde().
    """
    tabla = {}
    try:
        f = io.open(ruta, encoding='cp1252', errors='replace')
    except IOError:
        return tabla
    with f:
        for l in f:
            l = l.rstrip('\n\r')
            if len(l) < 129 or l.startswith('FRE'):
                continue
            sitio = l[85:109].strip()
            m = re.match(r'^(\d{2})(\d{2})(\d{2})([NS])(\d{3})(\d{2})(\d{2})([EW])$',
                         l[113:129].strip())
            if not sitio or not m:
                continue
            lat = int(m.group(1)) + int(m.group(2)) / 60.0 + int(m.group(3)) / 3600.0
            lon = int(m.group(5)) + int(m.group(6)) / 60.0 + int(m.group(7)) / 3600.0
            if m.group(4) == 'S':
                lat = -lat
            if m.group(8) == 'W':
                lon = -lon
            tabla.setdefault(sitio.upper(), (lat, lon))
            tabla.setdefault(sitio.upper()[:12].strip(), (lat, lon))
    return tabla


def hz(khz):
    try:
        return int(round(float(khz) * 1000.0))
    except ValueError:
        return None


def minutos(t):
    m = re.match(r'^(\d{2})(\d{2})-(\d{2})(\d{2})$', t)
    if not m:
        return None
    a = int(m.group(1)) * 60 + int(m.group(2))
    b = int(m.group(3)) * 60 + int(m.group(4))
    return a, b


def solape_min(a1, a2, b1, b2):
    """Minutos en que dos ventanas horarias coinciden. 0 si no se tocan.

    Las ventanas pueden CRUZAR LA MEDIANOCHE -es el convenio del fichero:
    si el fin no es mayor que el principio, se pasa de las 24:00- asi que
    cada una se parte en uno o dos tramos antes de compararlas. Una vuelta
    entera, 0000-2400, es un solo tramo de 1.440 minutos y no un cruce.
    """
    def tramos(x, y):
        if y > x:
            return ((x, y),)
        if y == 0:
            return ((x, 1440),)
        return ((x, 1440), (0, y))

    n = 0
    for p, q in tramos(a1, a2):
        for r, t in tramos(b1, b2):
            lo, hi = max(p, r), min(q, t)
            if hi > lo:
                n += hi - lo
    return n


def dias(d):
    """Devuelve la mascara de dias. bit0 = lunes.

    EiBi numera 1=lunes..7=domingo y ademas usa 'Mo-Fr', 'Su', etc.
    Aoki usa '1-7' con 1=DOMINGO, que es un convenio DISTINTO - lo dice su
    propia cabecera: "Day 1 = Sunday". Confundirlos desplazaria toda la
    semana un dia, asi que cada lector pasa el suyo ya convertido.
    """
    return d


NOMDIA = {'Mo': 0, 'Tu': 1, 'We': 2, 'Th': 3, 'Fr': 4, 'Sa': 5, 'Su': 6}


def dias_eibi(d):
    d = d.strip()
    if d == '':
        return 0x7F
    m = re.match(r'^([A-Z][a-z])-([A-Z][a-z])$', d)
    if m and m.group(1) in NOMDIA and m.group(2) in NOMDIA:
        a, b = NOMDIA[m.group(1)], NOMDIA[m.group(2)]
        v = 0
        i = a
        while True:
            v |= 1 << i
            if i == b:
                break
            i = (i + 1) % 7
        return v
    if d in NOMDIA:
        return 1 << NOMDIA[d]
    if re.match(r'^[1-7]+$', d):
        return sum(1 << (int(c) - 1) for c in d)      # EiBi: 1 = lunes
    return 0x7F      # fechas sueltas ("10Oct") y demas: se toma como diario


def dias_aoki(d):
    d = d.strip()
    if d in ('', '1-7'):
        return 0x7F
    m = re.match(r'^(\d)-(\d)$', d)
    if m:
        a, b = int(m.group(1)), int(m.group(2))
        v = 0
        i = a
        while True:
            v |= 1 << ((i + 5) % 7)                   # Aoki: 1 = domingo
            if i == b:
                break
            i = i % 7 + 1
        return v
    if re.match(r'^\d+$', d):
        return sum(1 << ((int(c) + 5) % 7) for c in d)
    return 0x7F


# --- donde estas tu, y donde esta cada emisora --------------------------

# Lo que no tiene sitio conocido va al final. Un numero grande y no None
# para que la ordenacion no tenga que llevar un caso aparte; media vuelta
# al mundo es 20.000 km, asi que con esto no se confunde con nada real.
SIN_SITIO = 99999.0

MESES = ('january', 'february', 'march', 'april', 'may', 'june', 'july',
         'august', 'september', 'october', 'november', 'december')


def maidenhead(loc):
    """Latitud y longitud del CENTRO de una cuadricula Maidenhead.

    Acepta 2, 4, 6 u 8 caracteres (IN, IN80, IN80ej, IN80ej12). El centro
    y no la esquina porque con 2 caracteres la esquina se va 10 grados -mas
    de mil kilometros- y el orden por distancia saldria torcido para quien
    escriba solo el campo.

    Devuelve None si no es una cuadricula, que es distinto de 0,0 - ahi
    hay oceano, y un QTH silenciosamente puesto en el Golfo de Guinea es
    de los fallos que parecen buenos.
    """
    t = loc.strip().upper()
    if len(t) not in (2, 4, 6, 8):
        return None
    lon, lat = -180.0, -90.0
    plon, plat = 20.0, 10.0
    try:
        if not ('A' <= t[0] <= 'R' and 'A' <= t[1] <= 'R'):
            return None
        lon += (ord(t[0]) - 65) * plon
        lat += (ord(t[1]) - 65) * plat
        if len(t) >= 4:
            plon /= 10.0
            plat /= 10.0
            lon += int(t[2]) * plon
            lat += int(t[3]) * plat
        if len(t) >= 6:
            if not ('A' <= t[4] <= 'X' and 'A' <= t[5] <= 'X'):
                return None
            plon /= 24.0
            plat /= 24.0
            lon += (ord(t[4]) - 65) * plon
            lat += (ord(t[5]) - 65) * plat
        if len(t) >= 8:
            plon /= 10.0
            plat /= 10.0
            lon += int(t[6]) * plon
            lat += int(t[7]) * plat
    except ValueError:
        return None
    return (lat + plat / 2.0, lon + plon / 2.0)


def qth_de(txt):
    """El --qth: una cuadricula o `lat,lon` en grados. None si no vale."""
    if ',' in txt:
        try:
            a, b = txt.split(',', 1)
            la, lo = float(a), float(b)
        except ValueError:
            return None
        if -90.0 <= la <= 90.0 and -180.0 <= lo <= 180.0:
            return (la, lo)
        return None
    return maidenhead(txt)


def dist_km(a, b):
    """Circulo maximo entre dos (lat, lon) en grados.

    Haversine y no la formula del coseno: la del coseno pierde precision
    justo en las distancias cortas, que son las que aqui deciden el orden
    -quien tengo mas cerca de los dos de mi pais-.
    """
    la1, lo1 = math.radians(a[0]), math.radians(a[1])
    la2, lo2 = math.radians(b[0]), math.radians(b[1])
    h = (math.sin((la2 - la1) / 2.0) ** 2
         + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2.0) ** 2)
    return 2.0 * 6371.0 * math.asin(min(1.0, math.sqrt(h)))


def lee_xy(ruta):
    """Lee paises_xy.csv o sitios_xy.csv: clave -> (lat, lon) o None.

    Las filas sin coordenadas se guardan como None A PROPOSITO, y no se
    saltan: asi se puede distinguir "esto no tiene sitio y lo sabemos"
    -una clandestina- de "este codigo no esta en la tabla", que es un
    aviso que hay que dar.
    """
    tabla = {}
    try:
        f = io.open(ruta, encoding='utf-8')
    except IOError:
        return None
    with f:
        for l in f:
            l = l.strip()
            if not l or l.startswith('#'):
                continue
            c = l.split(',')
            if len(c) < 3 or c[0] in ('codigo', 'prefijo'):
                continue
            if c[1].strip() == '' or c[2].strip() == '':
                tabla[c[0].strip().upper()] = None
                continue
            try:
                tabla[c[0].strip().upper()] = (float(c[1]), float(c[2]))
            except ValueError:
                print("  AVISO: %s: no entiendo la fila %r" % (ruta, l))
    return tabla


def donde(v, paises, sitios, faltan):
    """Donde esta la emisora de la fila `v`: el sitio manda sobre el pais.

    Del sitio se busca el PREFIJO mas largo que case, porque Aoki recorta
    el nombre a doce caracteres: "Urumqi Hutu" tiene que ganarle a
    "Urumqi", que es otra ciudad a 90 km.
    """
    # LA POSICION EXACTA MANDA SOBRE CUALQUIER TABLA - 26/10/2026. Las
    # filas que vienen de HFCC traen las coordenadas del transmisor en el
    # propio registro, asi que no hay nada que adivinar: ni prefijo de
    # nombre ni centroide de pais.
    if v.get('xy') is not None:
        return v['xy']
    sit = v['sitio'].strip().upper()
    if sit:
        mejor = None
        for k in sitios:
            if sit.startswith(k) and (mejor is None or len(k) > len(mejor)):
                mejor = k
        if mejor is not None and sitios[mejor] is not None:
            return sitios[mejor]
    itu = v['itu'].strip().upper()
    if itu in paises:
        return paises[itu]
    if itu:
        faltan.add(itu)
    return None


def lee_valido(ruta):
    """La fecha de caducidad, sacada de la cabecera de EiBi.

    Estaba escrita a mano en el codigo (20261025) y eso es una bomba de
    relojeria de seis meses: la lista siguiente se empaquetaria con la
    caducidad de la anterior, la radio diria "caducada" el dia de
    estreno o, peor, se callaria medio año de mas. La cabecera de EiBi
    lo dice en claro:

        Valid March 29, 2026 - October 25, 2026
    """
    try:
        cab = io.open(ruta, encoding='cp1252', errors='replace').read(4000)
    except IOError:
        return None
    m = re.search(r'Valid\s+\w+\s+\d+,\s*\d{4}\s*-\s*(\w+)\s+(\d+),\s*(\d{4})',
                  cab)
    if not m:
        return None
    mes = m.group(1).lower()
    if mes not in MESES:
        return None
    return int(m.group(3)) * 10000 + (MESES.index(mes) + 1) * 100 + int(m.group(2))


def main():
    aqui = os.path.dirname(os.path.abspath(__file__))
    sueltos, qth, radio, solo = [], None, None, None
    xy_paises = os.path.join(aqui, 'paises_xy.csv')
    xy_sitios = os.path.join(aqui, 'sitios_xy.csv')
    hasta = None
    ruta_hfcc = None
    ruta_axy = None
    i = 1
    while i < len(sys.argv):
        a = sys.argv[i]
        if a.startswith('--'):
            if i + 1 >= len(sys.argv):
                print("  MAL: %s se ha quedado sin valor" % a)
                return 1
            val = sys.argv[i + 1]
            i += 2
            if a == '--qth':
                qth = qth_de(val)
                if qth is None:
                    print("  MAL: %r no es una cuadricula ni un lat,lon" % val)
                    return 1
            elif a == '--radio':
                radio = float(val)
            elif a == '--pais':
                solo = set(x.strip().upper() for x in val.split(',') if x.strip())
            elif a == '--paises-xy':
                xy_paises = val
            elif a == '--sitios-xy':
                xy_sitios = val
            elif a == '--hasta':
                hasta = int(val)
            elif a == '--hfcc':
                ruta_hfcc = val
            elif a == '--aoki-xy':
                ruta_axy = val
            else:
                print("  MAL: no conozco %s" % a)
                return 1
        else:
            sueltos.append(a)
            i += 1
    if len(sueltos) not in (3, 4):
        print(__doc__)
        return 2
    eibi, aoki, salida = sueltos[:3]
    utc_h = int(sueltos[3]) if len(sueltos) == 4 else 2
    if not -12 <= utc_h <= 14:
        print("  MAL: %d no es un desfase horario razonable" % utc_h)
        return 1
    if radio is not None and qth is None:
        print("  MAL: --radio necesita --qth, que si no no hay desde donde medir")
        return 1

    crudas = []
    for f in lee_eibi(eibi):
        f['fuente'] = 'E'
        crudas.append(f)
    n_e = len(crudas)
    for f in lee_aoki(aoki):
        f['fuente'] = 'A'
        crudas.append(f)
    print("  EiBi %d filas, Aoki %d filas" % (n_e, len(crudas) - n_e))

    ent = {}
    tirados = 0
    for f in crudas:
        h = hz(f['khz'])
        mm = minutos(f['t'])
        if h is None or mm is None or f['est'] == '':
            tirados += 1
            continue
        d = dias_eibi(f['d']) if f['fuente'] == 'E' else dias_aoki(f['d'])
        clave = (h, mm[0], mm[1], f['est'].upper())
        v = ent.get(clave)
        if v is None:
            ent[clave] = dict(hz=h, ini=mm[0], fin=mm[1], dias=d, est=f['est'],
                              itu=f['itu'], lang=f['lang'], sitio=f['sitio'])
        else:
            v['dias'] |= d
            if not v['sitio'] and f['sitio']:
                v['sitio'] = f['sitio']
            if not v['lang'] and f['lang']:
                v['lang'] = f['lang']
    print("  %d filas sin frecuencia/hora utilizable, tiradas" % tirados)
    print("  %d entradas despues de fusionar (de %d)" % (len(ent), len(crudas)))

    filas = list(ent.values())
    for v in filas:
        v.setdefault('xy', None)

    # ------------------------------------------------------------------
    # Y ENCIMA, EL REGISTRO OFICIAL - 26/10/2026.
    # ------------------------------------------------------------------
    #
    # Pedido por el usuario: "añade tambien hfcc, evita duplicados".
    #
    # LO DIFICIL ES LO SEGUNDO, y no es un detalle de implementacion: la
    # MISMA transmision esta en las tres listas con TRES NOMBRES DISTINTOS.
    # EiBi la llama por el programa -"CNR 1 Voice of China"-, HFCC por la
    # organizacion que la registro -"China National Radio"- y Aoki en
    # mayusculas y recortada. Asi que la clave de siempre, que lleva el
    # nombre dentro, aqui no sirve: fusionar por nombre dejaria las tres.
    #
    # LO QUE SI IDENTIFICA UNA TRANSMISION son cuatro cosas que las tres
    # listas escriben igual: la FRECUENCIA, el PAIS que emite, y que las
    # HORAS SE SOLAPEN. Dos emisiones del mismo pais en la misma frecuencia
    # a la misma hora no son dos emisiones: son la misma contada dos veces.
    #
    # Y EL EMPAREJAMIENTO ES UNO A UNO, a proposito. En una frecuencia puede
    # haber varias emisiones del mismo pais en horas que se solapan -idiomas
    # distintos, por ejemplo- y si se dejara que una fila de HFCC se comiera
    # TODAS las que casan, se perderian emisiones de verdad. Cada fila de
    # HFCC se queda con UNA, la que mas solapa, y las demas siguen vivas.
    #
    # QUE SE QUEDA DE CADA UNA. El NOMBRE, de EiBi: es el del programa, el
    # que la gente reconoce. Todo lo demas, de HFCC: las HORAS y los DIAS
    # porque son los de esta temporada y los de EiBi son los de la anterior,
    # y el SITIO con sus coordenadas porque es el unico que lo da entero.
    #
    # Es la misma regla que ya seguian EiBi y Aoki -"el sitio de Aoki y el
    # destino de EiBi"-, un escalon mas arriba.
    if ruta_hfcc is not None:
        hf = lee_hfcc(ruta_hfcc)
        if hf is None:
            print("  MAL: no encuentro %s" % ruta_hfcc)
            return 1

        # (1) QUE PARES (FRECUENCIA, PAIS) CUBRE EL REGISTRO ESTA TEMPORADA.
        cubre = set()
        for f in hf:
            h = hz(f['khz'])
            if h is not None:
                cubre.add((h, f['itu'].strip().upper()))

        # (2) LO QUE LAS LISTAS DE AFICIONADO DICEN DE ESOS MISMOS PARES SE
        #     APARTA. No porque este mal escrito, sino porque es DE LA
        #     TEMPORADA ANTERIOR: cuando se escribio esto, EiBi y Aoki
        #     seguian publicando la A26 cinco dias despues de empezar el
        #     invierno, y en el cambio de temporada media banda se mueve una
        #     hora. Dejar las dos versiones seria dejar la radio diciendo que
        #     una emisora esta en el aire cuando ya no lo esta - que es el
        #     mismo fallo que el de las horas UTC: PARECE bueno.
        #
        #     LAS UTILITARIAS NO SE APARTAN NUNCA. EiBi las marca con un
        #     guion delante del idioma (-TS balizas horarias, -CW, -TY
        #     teletipo, -HF datos, -MX). No son radiodifusion, no se
        #     coordinan en la HFCC y no cambian de temporada: son servicios
        #     fijos. Que una baliza comparta frecuencia y pais con una
        #     emisora registrada no la convierte en esa emisora.
        viejas, quedan = [], []
        for v in filas:
            if (v['hz'], v['itu'].strip().upper()) in cubre \
               and not v['lang'].strip().startswith('-'):
                viejas.append(v)
            else:
                quedan.append(v)

        # (3) EL NOMBRE SI SE HEREDA. HFCC registra la ORGANIZACION -"China
        #     National Radio"- y EiBi apunta el PROGRAMA -"CNR 1 Voice of
        #     China"-, que es el que la gente reconoce y el que esta radio
        #     lleva enseñando desde que existe. Asi que cada transmision
        #     registrada busca entre las apartadas la que mas horas solapa
        #     con ella, y se queda con su nombre.
        #
        #     UNO A UNO: en una frecuencia puede haber varias emisiones del
        #     mismo pais a horas que se solapan -idiomas distintos- y si una
        #     fila de HFCC se quedara con TODAS las que casan, dos
        #     transmisiones distintas acabarian con el mismo nombre.
        porfrec = {}
        for v in viejas:
            porfrec.setdefault(v['hz'], []).append(v)
        n_nom = n_tiradas = 0
        nuevas = []
        for f in hf:
            h = hz(f['khz'])
            mm = minutos(f['t'])
            if h is None or mm is None or f['est'] == '':
                n_tiradas += 1
                continue
            mejor, mejor_sol = None, 0
            for v in porfrec.get(h, ()):
                if v.get('usada') \
                   or v['itu'].strip().upper() != f['itu'].strip().upper():
                    continue
                sol = solape_min(mm[0], mm[1], v['ini'], v['fin'])
                if sol > mejor_sol:
                    mejor, mejor_sol = v, sol
            est, lang = f['est'], f['lang']
            if mejor is not None:
                mejor['usada'] = 1
                est = mejor['est']
                if not lang and mejor['lang']:
                    lang = mejor['lang']
                n_nom += 1
            nuevas.append(dict(hz=h, ini=mm[0], fin=mm[1], dias=dias_hfcc(f['d']),
                               est=est, itu=f['itu'], lang=lang,
                               sitio=f['sitio'], xy=f['xy']))
        print("  HFCC %d filas en %d pares (frecuencia, pais)"
              % (len(hf), len(cubre)))
        print("  %d entradas de EiBi/Aoki eran de esos mismos pares y se "
              "sustituyen por las del registro" % len(viejas))
        print("  %d transmisiones registradas se quedan con el nombre que les "
              "daba EiBi" % n_nom)
        if n_tiradas:
            print("  %d filas de HFCC sin frecuencia/hora utilizable, tiradas"
                  % n_tiradas)

        # (4) Y por si dos han acabado diciendo exactamente lo mismo.
        filas = quedan + nuevas
        vistas, limpias, n_rep = set(), [], 0
        for v in filas:
            k = (v['hz'], v['ini'], v['fin'], v['est'].upper(),
                 v['sitio'].upper())
            if k in vistas:
                n_rep += 1
                continue
            vistas.add(k)
            limpias.append(v)
        if n_rep:
            print("  %d entradas repetidas despues de fusionar, tiradas" % n_rep)
        filas = limpias
        print("  %d entradas en total" % len(filas))

    # ------------------------------------------------------------------
    # LA MISMA EMISION CONTADA DOS VECES CON DOS NOMBRES - 26/10/2026.
    # ------------------------------------------------------------------
    #
    # La fusion de arriba empareja por (frecuencia, hora, NOMBRE), y el
    # nombre es justo lo que las listas escriben distinto: EiBi pone "China
    # Radio Int." y Aoki "CHINA RADIO INTERNATIONAL", asi que las dos
    # entraban. Se veia en la radio: 603 kHz, la misma emision a la misma
    # hora, dos renglones, uno con sitio y otro sin el.
    #
    # LO QUE SE TIRA ES SOLO LO QUE NO PUEDE SER OTRA COSA: misma
    # frecuencia, mismo pais, MISMA HORA DE PRINCIPIO Y DE FIN, y un nombre
    # que es el prefijo del otro con ocho caracteres por lo menos. Si las
    # horas no coinciden EXACTAMENTE no se toca, porque entonces puede ser
    # otra emision: China Radio emite de verdad a horas distintas desde
    # sitios distintos, y tirar una por parecerse seria perder una emisora.
    #
    # Y SE QUEDA LA QUE TIENE SITIO. Es la que puede situarse en el mapa y
    # la que el orden por cercania sabe colocar; la otra no añade nada.
    porclave = {}
    for v in filas:
        porclave.setdefault((v['hz'], v['itu'].strip().upper(),
                             v['ini'], v['fin']), []).append(v)
    fuera = set()
    for grupo in porclave.values():
        if len(grupo) < 2:
            continue
        for a in grupo:
            if a['sitio'].strip() or id(a) in fuera:
                continue
            na = re.sub(r'[^A-Z0-9]', '', a['est'].upper())
            if len(na) < 8:
                continue
            for b in grupo:
                if b is a or not b['sitio'].strip():
                    continue
                nb = re.sub(r'[^A-Z0-9]', '', b['est'].upper())
                if len(nb) >= 8 and (nb.startswith(na) or na.startswith(nb)):
                    fuera.add(id(a))
                    break
    if fuera:
        filas = [v for v in filas if id(v) not in fuera]
        print("  %d entradas eran la misma emision con otro nombre y sin "
              "sitio, tiradas" % len(fuera))

    # EL ORDEN DENTRO DE CADA FRECUENCIA.
    #
    # Sin --qth, (hz, ini, est) - exactamente lo de siempre, para que el
    # fichero salga byte a byte igual que antes de que esto existiera.
    # Con --qth, la distancia se mete DELANTE de ini y est: dentro de una
    # frecuencia manda lo cerca que esta, y entre dos que estan igual de
    # lejos sigue decidiendo lo de antes, que asi el resultado no depende
    # del orden en que se leyeron las listas.
    if qth is not None:
        tabla_p = lee_xy(xy_paises)
        sitios = lee_xy(xy_sitios)
        if tabla_p is None:
            print("  MAL: no encuentro %s" % xy_paises)
            return 1
        if sitios is None:
            print("  AVISO: no encuentro %s, solo se usaran los paises"
                  % xy_sitios)
            sitios = {}
        # LAS POSICIONES QUE REGALAN LAS PROPIAS LISTAS - 26/10/2026.
        #
        # sitios_xy.csv esta escrito a mano y tiene 596 renglones. El Aoki
        # ANCHO (xta26.txt) trae la posicion del transmisor en grados,
        # minutos y segundos para casi mil emplazamientos, y site.txt de
        # HFCC para setecientos mas. Meterlos aqui es gratis y hace que el
        # orden por cercania deje de apoyarse en el centroide del pais para
        # media lista.
        #
        # VAN DETRAS de sitios_xy.csv y no delante: esa tabla esta escrita a
        # mano precisamente para corregir lo que las listas dicen mal, asi
        # que si alguien se tomo la molestia de apuntar un sitio ahi, gana.
        if ruta_axy is not None:
            extra = lee_aoki_xy(ruta_axy)
            n_ant = len(sitios)
            for k, xy in extra.items():
                sitios.setdefault(k, xy)
            print("  %d emplazamientos del Aoki ancho (%d ya estaban)"
                  % (len(extra), len(extra) - (len(sitios) - n_ant)))
        if ruta_hfcc is not None:
            n_ant = len(sitios)
            n_h = 0
            for nom, xy in lee_hfcc_sitios(
                    os.path.join(os.path.dirname(os.path.abspath(ruta_hfcc)),
                                 'hfcc_sitios.txt')).values():
                if nom and xy is not None:
                    n_h += 1
                    sitios.setdefault(nom.upper(), xy)
                    sitios.setdefault(nom.upper()[:12].strip(), xy)
            print("  %d emplazamientos de HFCC (%d nuevos)"
                  % (n_h, len(sitios) - n_ant))
        faltan = set()
        n_sitio = n_sin = 0
        for v in filas:
            xy = donde(v, tabla_p, sitios, faltan)
            if xy is None:
                v['km'] = SIN_SITIO
                n_sin += 1
            else:
                v['km'] = dist_km(qth, xy)
                n_sitio += 1
        print("  QTH %.3f, %.3f" % qth)
        print("  %d entradas situadas, %d sin sitio conocido (van al final)"
              % (n_sitio, n_sin))
        if faltan:
            print("  AVISO: %d codigos ITU no estan en %s: %s"
                  % (len(faltan), os.path.basename(xy_paises),
                     ' '.join(sorted(faltan))))
        if radio is not None:
            antes = len(filas)
            filas = [v for v in filas if v['km'] <= radio]
            print("  --radio %g km: quedan %d entradas de %d"
                  % (radio, len(filas), antes))
        filas.sort(key=lambda v: (v['hz'], v['km'], v['ini'], v['est']))
    else:
        filas.sort(key=lambda v: (v['hz'], v['ini'], v['est']))

    if solo is not None:
        antes = len(filas)
        filas = [v for v in filas if v['itu'].strip().upper() in solo]
        print("  --pais: quedan %d entradas de %d" % (len(filas), antes))

    if not filas:
        print("  MAL: no queda ni una entrada; afloja --radio o --pais")
        return 1

    textos = ['']
    idx = {'': 0}

    def t(s):
        s = s.strip()
        if s not in idx:
            idx[s] = len(textos)
            textos.append(s)
        return idx[s]

    for v in filas:
        v['i_est'] = t(v['est'])
        v['i_itu'] = t(v['itu'])
        v['i_lang'] = t(v['lang']) if v['lang'] else 0xFFFF
        v['i_sitio'] = t(v['sitio']) if v['sitio'] else 0xFFFF

    if len(textos) > 65535:
        print("  MAL: %d textos, no caben en un u16" % len(textos))
        return 1
    # el indice de pais va en un byte
    paises = sorted(set(v['itu'] for v in filas))
    if len(paises) > 255:
        print("  MAL: %d paises, no caben en un byte" % len(paises))
        return 1
    pidx = {p: i for i, p in enumerate(paises)}

    # tabla de frecuencias
    frec = []
    i = 0
    while i < len(filas):
        j = i
        while j < len(filas) and filas[j]['hz'] == filas[i]['hz']:
            j += 1
        frec.append((filas[i]['hz'], i, j - i))
        i = j
    if max(n for _, _, n in frec) > 65535 or len(filas) > 65535:
        print("  MAL: demasiadas entradas para un u16")
        return 1

    blob = bytearray()
    off = []
    for s in textos:
        off.append(len(blob))
        blob += s.encode('cp1252', errors='replace') + b'\0'
    # los paises van al final de la tabla de textos, por indice propio
    poff = []
    for p in paises:
        poff.append(len(blob))
        blob += p.encode('cp1252', errors='replace') + b'\0'
    if len(blob) > 65535:
        print("  AVISO: las cadenas ocupan %d bytes, mas de 64 kB" % len(blob))
        return 1

    n_txt = len(off) + len(poff)
    off_frec = 56
    off_ent = off_frec + 8 * len(frec)
    off_txt = off_ent + 12 * len(filas)
    off_cad = off_txt + 2 * n_txt
    total = off_cad + len(blob)

    def suma(b):
        """Suma de 32 bits con rotacion. No es un CRC y no pretende serlo:
        aqui solo hay que notar que algo se ha copiado mal o que la zona
        estaba a medio escribir, no defenderse de nadie. Lo que si tiene,
        y una suma llana no, es que el orden importa - dos bytes
        intercambiados dan resultados distintos."""
        v = 0x811C9DC5
        for x in b:
            v = ((v ^ x) * 16777619) & 0xFFFFFFFF
        return v

    if hasta is None and ruta_hfcc is not None:
        # LA CADUCIDAD LA PONE EL REGISTRO, no la lista de aficionado.
        # Cuando se escribio esto, la cabecera de EiBi decia que caducaba el
        # 25/10/2026 -o sea, ayer- mientras que las transmisiones de HFCC
        # llegan hasta el 28/03/2027. Lo que la radio tiene dentro es la
        # temporada de invierno, asi que lo que tiene que avisar es cuando
        # acaba el invierno.
        hasta = hfcc_valido(lee_hfcc(ruta_hfcc) or [])
        if hasta is not None:
            print("  caduca el %08d, que es cuando acaba la ultima "
                  "transmision registrada" % hasta)
    if hasta is None:
        hasta = lee_valido(eibi)
    if hasta is None:
        hasta = 20261025
        print("  AVISO: no he sabido leer la caducidad de %s;"
              " me quedo con %d. Se pone a mano con --hasta." % (eibi, hasta))

    cab = bytearray()
    cab += b'EMIS' + struct.pack('<BBHIIIIIIII', 2, len(paises), n_txt, len(frec),
                                 len(filas), off_frec, off_ent, off_txt, off_cad,
                                 total, hasta)
    cab += struct.pack('<b7x', utc_h)
    assert len(cab) == 48, len(cab)

    cuerpo = bytearray()
    for h, pri, n in frec:
        cuerpo += struct.pack('<IHH', h, pri, n)
    for v in filas:
        cuerpo += struct.pack('<HHBBHHH', v['ini'], v['fin'], v['dias'],
                              pidx[v['itu']], v['i_est'], v['i_sitio'], v['i_lang'])
    for o in off + poff:
        cuerpo += struct.pack('<H', o)
    cuerpo += blob

    out = bytearray(cab)
    out += struct.pack('<II', suma(cab), suma(cuerpo))
    out += cuerpo
    assert len(out) == total, (len(out), total)
    open(salida, 'wb').write(out)
    print("  %d frecuencias, %d entradas, %d textos (%d bytes de cadenas)"
          % (len(frec), len(filas), n_txt, len(blob)))
    print("  tabla de frecuencias %6d B" % (8 * len(frec)))
    print("  entradas             %6d B" % (12 * len(filas)))
    print("  textos               %6d B" % (2 * n_txt + len(blob)))
    print("  el reloj de la radio menos %d horas es UTC" % utc_h)
    print("  %s: %d bytes (%.1f kB)" % (salida, total, total / 1024.0))
    return 0


sys.exit(main())
