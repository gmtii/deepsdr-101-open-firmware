#!/usr/bin/env python3
"""
Empaqueta las listas EiBi y Aoki en EMISORAS.BIN, que es lo que la radio
lee para decir quien emite en la frecuencia que estas oyendo.

  python3 emisoras_pack.py eibi.txt aoki.txt EMISORAS.BIN [horas_utc]
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

LAS DOS LISTAS NO SON LA MISMA COSA, y por eso se meten las dos:

  EiBi   8.186 filas. Lleva radiodifusion Y utilitarias: balizas de
         tiempo, fax meteorologico, radio maritima, aviacion. Trae el
         AREA DE DESTINO (a quien apunta la antena).
  Aoki   5.325 filas. Solo radiodifusion, pero trae el SITIO DESDE EL QUE
         SE EMITE, que EiBi no da.

Se fusionan por (frecuencia, hora, emisora): lo que esta en las dos sale
una vez, con el sitio de Aoki y el destino de EiBi.

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
