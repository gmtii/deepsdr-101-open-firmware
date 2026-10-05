#!/usr/bin/env python3
"""
Banco de emisoras_pack.py: el orden de las emisoras de una frecuencia.

  python3 emisoras_check.py [carpeta con eibi.txt y aoki.txt]

Mide dos cosas: el ORDEN de las emisoras de una frecuencia
(emisoras_pack.py --qth) y el TECHO DE UN MEGA de BD.BIN
(datos_pack.py). Las dos sin tocar la radio.

QUE MIDE, Y POR QUE ESTE BANCO EXISTE - 05/10/2026.

En 540 kHz hay diez emisoras, la radio pinta UNA, y hasta hoy esa una era
siempre la misma para todo el mundo porque las entradas de una frecuencia
iban ordenadas por nombre. Quien escucha desde Hungria esperaba Kossuth
-Solt, setenta kilometros- y le salia CNR 1 desde Mongolia Interior, a seis
mil setecientos. Lo conto alguien de fuera por el repositorio; aqui no se
habia visto nunca porque desde España, en onda corta, el primero por orden
alfabetico suele valer tanto como cualquier otro.

El arreglo es --qth y vive entero en el empaquetador: el firmware no se
toca. Eso tiene una pega, y es justo lo que este banco cubre: un orden
equivocado NO da error, da OTRA EMISORA, con toda la pinta de buena. Un
fallo asi no se ve mirando la radio, porque lo que sale es un nombre
plausible.

Las listas de verdad no hacen falta: el banco se escribe unas suyas, en
las mismas columnas, con las emisoras de 540 kHz que provocaron el fallo.
Si ademas le pasas la carpeta con eibi.txt y aoki.txt, hace tres
comprobaciones mas sobre los datos reales.
"""
import io
import os
import struct
import subprocess
import sys
import tempfile

AQUI = os.path.dirname(os.path.abspath(__file__))
PACK = os.path.join(AQUI, 'emisoras_pack.py')

fallos = []


def mal(que, detalle=''):
    fallos.append(que)
    print("    MAL: %s%s" % (que, (' - ' + detalle) if detalle else ''))


def bien(que):
    print("    ok: %s" % que)


# --- las listas de pega -------------------------------------------------
#
# Las columnas NO se cuentan a ojo: son las que lee emisoras_pack.py, o
# sea las de la cabecera de cada fichero. Si un dia se mueven, este banco
# tiene que romperse, que para eso esta.
#
#   eibi:  khz 0:14  hora 14:23  dias 24:30  itu 30:34  est 34:57  len>=57
#   aoki:  khz 0:14  hora 14:24  itu 30:34  est 34:59  idm 59:63
#          sitio 63:75  dias 75:   len>=60

def fila_eibi(khz, hora, dias, itu, est, idm='', destino=''):
    # dias ocupa SEIS, no cinco: con cinco el codigo ITU empieza en la 29
    # en vez de en la 30 y el nombre de la emisora sale con un caracter de
    # menos por delante - "1-Kossuth Radio" en vez de "P1-Kossuth Radio".
    # Lo encontro este mismo banco la primera vez que se ejecuto, y no
    # fallaba el empaquetador sino la lista de pega.
    l = '%-14s%-9s %-6s%-4s%-23s%-6s%-9s' % (khz, hora, dias, itu, est,
                                             idm, destino)
    assert len(l) >= 57, len(l)
    assert l[30:34].strip() == itu, (l[30:34], itu)
    assert l[34:57].strip() == est, (l[34:57], est)
    return l


def fila_aoki(khz, hora, itu, est, idm, sitio, dias):
    l = '%-14s%-10s      %-4s%-25s%-4s%-12s%-5s' % (khz, hora, itu, est,
                                                    idm, sitio, dias)
    assert len(l) >= 75, len(l)
    assert l[30:34].strip() == itu, (l[30:34], itu)
    assert l[34:59].strip() == est, (l[34:59], est)
    assert l[63:75].strip() == sitio, (l[63:75], sitio)
    return l


CAB_EIBI = """BC A26 - The comprehensive shortwave broadcasting schedule
==========================================================
               http://www.eibispace.de/

         Valid March 29, 2026 - October 25, 2026

Free to copy + distribute.

kHz           Time(UTC) Days  ITU Station                Lang. Target   Remarks
================================================================================
"""

CAB_AOKI = """A26 Shortwave Frequecy List  August 14 2026,  1000 UTC   Day 1 = Sunday
kHz           Time(UTC)       ITU Station                Lang. Location    Days
+-------------+---------+-----+---+------------------------+---+-----------+----
"""

# Las diez de 540 kHz, recortadas a las cuatro que deciden el orden, mas
# una frecuencia de control en la que no se toca nada.
AOKI_FILAS = [
    fila_aoki('540', '0000-2400', 'CHN', 'CNR 1 Voice of China', 'Chi',
              'Jagdaqi-Hei', '1-7'),
    fila_aoki('540', '0000-2400', 'J', 'NHK AM Matsumoto', 'Jpn',
              'Matsumoto', '1-7'),
    fila_aoki('540', '0330-2130', 'HNG', 'P1-Kossuth Radio', 'Hun',
              'Solt', '1-7'),
    fila_aoki('540', '0000-2400', 'KWT', 'R.KUWAIT', 'Ara',
              'Sulaibiyah', '1-7'),
    # Una con codigo ITU que NO esta en paises_xy.csv: tiene que avisar y
    # mandarla al final, no tirarla ni reventar.
    fila_aoki('540', '0000-2400', 'ZZZ', 'Zeta Radio', 'Eng',
              'Ninguna', '1-7'),
    # Dos de China a 3.400 km una de otra: aqui es donde sitios_xy.csv
    # tiene que ganarle al centroide del pais.
    fila_aoki('9500', '0000-2400', 'CHN', 'A-Kashi', 'Chi',
              'Kashi-Saiba', '1-7'),
    fila_aoki('9500', '0000-2400', 'CHN', 'B-Dongfang', 'Chi',
              'Dongfang Ha', '1-7'),
]

EIBI_FILAS = [
    fila_eibi('540', '0330-2130', '', 'HNG', 'P1-Kossuth Radio', 'Hun'),
    fila_eibi('6070', '0000-2400', '', 'D', 'Radio 6070', 'E'),
]


def escribe_listas(dirn, cab_eibi=CAB_EIBI):
    e = os.path.join(dirn, 'eibi.txt')
    a = os.path.join(dirn, 'aoki.txt')
    io.open(e, 'w', encoding='cp1252').write(
        cab_eibi + '\n'.join(EIBI_FILAS) + '\n')
    io.open(a, 'w', encoding='cp1252').write(
        CAB_AOKI + '\n'.join(AOKI_FILAS) + '\n')
    return e, a


# --- leer el EMISORAS.BIN como lo lee la radio --------------------------

def abre(ruta):
    d = io.open(ruta, 'rb').read()
    if d[:4] != b'EMIS':
        return None
    (ver, npa, ntx, nfr, nen, ofr, oen, otx, oca,
     tot, hasta) = struct.unpack('<BBHIIIIIIII', d[4:40])
    utc = struct.unpack('<b', d[40:41])[0]

    def txt(i):
        if i == 0xFFFF:
            return ''
        o = struct.unpack('<H', d[otx + 2 * i:otx + 2 * i + 2])[0]
        return d[oca + o:d.index(b'\0', oca + o)].decode('cp1252')

    frec = {}
    for k in range(nfr):
        hz, pri, n = struct.unpack('<IHH', d[ofr + 8 * k:ofr + 8 * k + 8])
        frec[hz] = (pri, n)

    def ent(i):
        (ini, fin, dias, pais, nom,
         sit, idi) = struct.unpack('<HHBBHHH', d[oen + 12 * i:oen + 12 * i + 12])
        return dict(ini=ini, fin=fin, dias=dias, pais=txt(ntx - npa + pais),
                    est=txt(nom), sitio=txt(sit), idioma=txt(idi))

    return dict(d=d, frec=frec, ent=ent, hasta=hasta, utc=utc, ver=ver,
                nfr=nfr, nen=nen, tot=tot)


def suma(b, v=0x811C9DC5):
    for x in b:
        v = ((v ^ x) * 16777619) & 0xFFFFFFFF
    return v


def sumas_cuadran(b):
    d = b['d']
    if suma(d[:48]) != struct.unpack('<I', d[48:52])[0]:
        return False
    return suma(d[56:b['tot']]) == struct.unpack('<I', d[52:56])[0]


def empaqueta(eibi, aoki, salida, *flags):
    r = subprocess.run([sys.executable, PACK, eibi, aoki, salida, '2']
                       + [str(x) for x in flags],
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def primeras(b, khz, n=3):
    pri, cuantas = b['frec'][khz * 1000]
    return [b['ent'](i) for i in range(pri, pri + min(n, cuantas))]


# --- las comprobaciones -------------------------------------------------

def mide_maidenhead(g):
    print("  cuadriculas Maidenhead")
    casos = [
        # El CENTRO de la cuadricula, no la esquina. Con dos caracteres la
        # esquina se va mas de mil kilometros.
        ('JN97', 47.5, 19.0),          # Budapest
        ('IN80', 40.5, -3.0),          # Madrid
        ('JN', 45.0, 10.0),
        ('JN97mn', 47.5625, 19.0416667),
    ]
    for loc, la, lo in casos:
        r = g['maidenhead'](loc)
        if r is None or abs(r[0] - la) > 0.001 or abs(r[1] - lo) > 0.001:
            mal("maidenhead(%s)" % loc, "sale %s y tenia que ser %s"
                % (r, (la, lo)))
        else:
            bien("%s -> %.4f, %.4f" % (loc, r[0], r[1]))
    for loc in ('ZZ99', 'JN9', '', 'JN97zz', 'J1', 'JN9X'):
        if g['maidenhead'](loc) is not None:
            mal("maidenhead(%r) tenia que ser None" % loc)
    bien("las cuadriculas imposibles dan None y no 0,0")

    # qth_de admite las dos formas
    if g['qth_de']('47.5,19.05') != (47.5, 19.05):
        mal("qth_de con lat,lon")
    for t in ('200,0', '0,400', 'hola', '47.5,'):
        if g['qth_de'](t) is not None:
            mal("qth_de(%r) tenia que ser None" % t)
    bien("--qth acepta cuadricula y lat,lon, y rechaza lo que no lo es")


def mide_distancia(g):
    print("  distancias")
    d = g['dist_km']
    casos = [
        ((47.5, 19.0), (46.80, 19.00), 78, 6),        # Budapest - Solt
        ((47.5, 19.0), (50.42, 124.12), 6989, 60),    # Budapest - Jagdaqi
        ((0.0, 0.0), (0.0, 180.0), 20015, 10),        # media vuelta
        ((41.0, 2.0), (41.0, 2.0), 0, 0.001),         # el mismo sitio
    ]
    for a, b, esp, tol in casos:
        v = d(a, b)
        if abs(v - esp) > tol:
            mal("dist_km%s" % (a + b,), "%.1f km, esperaba %d" % (v, esp))
    bien("circulo maximo: Budapest-Solt 78 km, media vuelta 20.015 km")


def mide_orden(tmp):
    print("  el orden dentro de una frecuencia")
    e, a = escribe_listas(tmp)
    sal = os.path.join(tmp, 's.bin')

    # 1. SIN --qth: lo de siempre, alfabetico. Es la conducta vieja y se
    #    mide a proposito - es la que le fallo al de Hungria, pero tambien
    #    es la que hace que un BD.BIN de antes siga saliendo igual.
    rc, log = empaqueta(e, a, sal)
    if rc != 0:
        mal("sin --qth no empaqueta", log.strip().split('\n')[-1])
        return
    b = abre(sal)
    p = primeras(b, 540)
    if p[0]['est'] != 'CNR 1 Voice of China':
        mal("sin --qth la primera de 540 tenia que ser CNR 1 (la C)",
            p[0]['est'])
    else:
        bien("sin --qth, 540 kHz empieza por CNR 1 - el orden de siempre")
    if not sumas_cuadran(b):
        mal("las sumas del fichero no cuadran")

    # 2. el mismo fichero dos veces: tiene que salir identico
    sal2 = os.path.join(tmp, 's2.bin')
    empaqueta(e, a, sal2)
    if io.open(sal, 'rb').read() != io.open(sal2, 'rb').read():
        mal("empaquetar dos veces no da el mismo fichero")
    else:
        bien("el empaquetado es reproducible byte a byte")

    # 3. CON --qth, desde cuatro sitios del mundo. Esto es el banco.
    desde = [
        ('JN97', 'Budapest', 'P1-Kossuth Radio'),
        ('35.7,139.7', 'Tokio', 'NHK AM Matsumoto'),
        ('29.3,48.0', 'Kuwait', 'R.KUWAIT'),
        ('50.4,124.1', 'Jagdaqi', 'CNR 1 Voice of China'),
    ]
    for qth, nombre, espera in desde:
        rc, log = empaqueta(e, a, sal, '--qth', qth)
        if rc != 0:
            mal("--qth %s no empaqueta" % qth, log.strip().split('\n')[-1])
            continue
        b = abre(sal)
        p = primeras(b, 540)
        if p[0]['est'] != espera:
            mal("desde %s la primera de 540 tenia que ser %r" % (nombre, espera),
                "sale %r" % p[0]['est'])
        else:
            bien("desde %-8s 540 kHz -> %s" % (nombre, p[0]['est']))

    # 4. el sitio manda sobre el pais: dos emisoras del MISMO pais a 3.400 km
    rc, _ = empaqueta(e, a, sal, '--qth', 'JN97')
    b = abre(sal)
    p = primeras(b, 9500)
    if p[0]['sitio'][:5] != 'Kashi':
        mal("9500 kHz desde Budapest tenia que empezar por Kashi",
            "sale %r" % p[0]['sitio'])
    else:
        bien("sitios_xy.csv manda sobre el pais: Kashi antes que Dongfang")
    rc, _ = empaqueta(e, a, sal, '--qth', '20.0,120.0')
    b = abre(sal)
    if primeras(b, 9500)[0]['sitio'][:8] != 'Dongfang':
        mal("9500 kHz desde el mar de China tenia que empezar por Dongfang")
    else:
        bien("y desde el otro lado, al reves")

    # 5. lo que no tiene sitio conocido va al final, NO se tira
    rc, log = empaqueta(e, a, sal, '--qth', 'JN97')
    b = abre(sal)
    pri, n = b['frec'][540000]
    todas = [b['ent'](i)['est'] for i in range(pri, pri + n)]
    if 'Zeta Radio' not in todas:
        mal("la emisora de pais desconocido se ha perdido")
    elif todas[-1] != 'Zeta Radio':
        mal("la emisora de pais desconocido tenia que ir la ultima", todas[-1])
    else:
        bien("lo que no tiene sitio va al final y no se tira")
    if 'ZZZ' not in log:
        mal("no ha avisado del codigo ITU que no conoce")
    else:
        bien("avisa por pantalla del codigo ITU desconocido (ZZZ)")

    # 6. --radio recorta, y si no deja nada lo dice en vez de reventar
    rc, log = empaqueta(e, a, sal, '--qth', 'JN97', '--radio', '3000')
    if rc != 0:
        mal("--radio 3000 no empaqueta")
    else:
        b2 = abre(sal)
        if b2['nen'] >= b['nen']:
            mal("--radio 3000 no ha recortado nada")
        else:
            bien("--radio 3000: %d entradas de %d" % (b2['nen'], b['nen']))
    rc, log = empaqueta(e, a, sal, '--qth', 'JN97', '--radio', '1')
    if rc == 0:
        mal("--radio 1 no deja ni una entrada y aun asi ha escrito fichero")
    elif 'afloja' not in log:
        mal("--radio 1 falla sin decir que se afloje el radio")
    else:
        bien("--radio 1 no deja nada y lo dice, no revienta")

    # 7. --radio sin --qth no tiene desde donde medir
    rc, log = empaqueta(e, a, sal, '--radio', '3000')
    if rc == 0:
        mal("--radio sin --qth tenia que fallar")
    else:
        bien("--radio sin --qth se rechaza")

    # 8. --pais
    rc, log = empaqueta(e, a, sal, '--pais', 'HNG')
    if rc != 0:
        mal("--pais HNG no empaqueta")
    else:
        b2 = abre(sal)
        paises = set()
        for hz, (pri, n) in b2['frec'].items():
            for i in range(pri, pri + n):
                paises.add(b2['ent'](i)['pais'])
        if paises != {'HNG'}:
            mal("--pais HNG deja otros paises", str(sorted(paises)))
        else:
            bien("--pais HNG deja solo Hungria")

    # 9. dentro de cada frecuencia la distancia no baja nunca
    rc, _ = empaqueta(e, a, sal, '--qth', 'JN97')
    b = abre(sal)
    g = carga_pack()
    tp = g['lee_xy'](os.path.join(AQUI, 'paises_xy.csv'))
    ts = g['lee_xy'](os.path.join(AQUI, 'sitios_xy.csv'))
    qth = (47.5, 19.0)
    roto = 0
    for hz, (pri, n) in b['frec'].items():
        ant = -1.0
        for i in range(pri, pri + n):
            e2 = b['ent'](i)
            xy = g['donde'](dict(sitio=e2['sitio'], itu=e2['pais']),
                            tp, ts, set())
            km = g['SIN_SITIO'] if xy is None else g['dist_km'](qth, xy)
            if km < ant - 0.001:
                roto += 1
            ant = km
    if roto:
        mal("en %d entradas la distancia baja dentro de su frecuencia" % roto)
    else:
        bien("la distancia nunca baja dentro de una frecuencia")

    # 10. la caducidad sale de la cabecera de EiBi, no del codigo
    e2, a2 = escribe_listas(tmp, CAB_EIBI.replace('October 25, 2026',
                                                  'March 28, 2027'))
    rc, _ = empaqueta(e2, a2, sal)
    b = abre(sal)
    if b['hasta'] != 20270328:
        mal("la caducidad no sale de la cabecera", "dice %d" % b['hasta'])
    else:
        bien("la caducidad se lee de la cabecera de EiBi (20270328)")


def carga_pack():
    """Carga emisoras_pack.py sin ejecutar main()."""
    src = io.open(PACK, encoding='utf-8').read().replace('sys.exit(main())', '')
    g = {'__file__': PACK, '__name__': 'emisoras_pack_banco'}
    exec(compile(src, PACK, 'exec'), g)
    return g


def mide_tablas(g, datos):
    print("  las tablas contra las listas de verdad")
    import re
    tp = g['lee_xy'](os.path.join(AQUI, 'paises_xy.csv'))
    ts = g['lee_xy'](os.path.join(AQUI, 'sitios_xy.csv'))
    if tp is None:
        mal("no encuentro paises_xy.csv")
        return

    # El prefijo mas largo gana: Urumqi Hutubi esta a 90 km de Urumqi.
    a = g['donde'](dict(sitio='Urumqi Hutu', itu='CHN'), tp, ts, set())
    b = g['donde'](dict(sitio='Urumqi', itu='CHN'), tp, ts, set())
    if a == b:
        mal("'Urumqi Hutu' y 'Urumqi' dan el mismo sitio; gana el prefijo corto")
    else:
        bien("gana el prefijo mas largo: Urumqi-Hutubi != Urumqi ciudad")

    e = os.path.join(datos, 'eibi.txt')
    a = os.path.join(datos, 'aoki.txt')
    if not (os.path.isfile(e) and os.path.isfile(a)):
        print("    (no estan eibi.txt/aoki.txt en %s: me salto lo demas)"
              % datos)
        return
    cod = set()
    for ruta, minimo in ((e, 57), (a, 60)):
        for l in io.open(ruta, encoding='cp1252', errors='replace'):
            if re.match(r'^\d', l) and len(l) >= minimo:
                c = l[30:34].strip()
                if c:
                    cod.add(c)
    faltan = sorted(c for c in cod if c not in tp)
    if faltan:
        mal("%d codigos ITU de las listas no estan en paises_xy.csv"
            % len(faltan), ' '.join(faltan))
    else:
        bien("los %d codigos ITU de las listas estan todos en la tabla" % len(cod))

    sobran = sorted(c for c in tp if c not in cod)
    if sobran:
        print("    (aviso, no fallo: %d codigos de la tabla ya no salen en las"
              " listas: %s)" % (len(sobran), ' '.join(sobran)))


def i24b(tam):
    """Un ICAO24.BIN valido y del tamaño que se pida, para medir el techo."""
    n = (tam - 16 - 24) // 5
    cab = b'I24B' + struct.pack('<BBHII', 1, 24, 1, n, 16 + 5 * n)
    cuerpo = b''.join(struct.pack('>BBB', (i >> 16) & 255, (i >> 8) & 255,
                                  i & 255) + b'\x00\x00' for i in range(n))
    return cab + cuerpo + b'A320' + b'\0' * 20


def mide_techo(tmp):
    """EL MEGA DE BD.BIN.

    *** Por el dueño del proyecto: "en algun sitio de esas herramientas
    tiene que avisar de que bd.bin no puede ser de mas de 1 mega". ***

    Lo avisaba, pero por el camino equivocado: el limite se escribia como
    0x200000 - 0x101000, el final menos el principio de la zona alta EN EL
    CHIP DE 2 MB DE FABRICA. Daba el numero bueno de casualidad. La regla
    de verdad -ZA_BYTES_REGION de User/zona_alta.h- es que la zona de
    datos es SIEMPRE el ultimo mega, se ponga el chip que se ponga, asi
    que una flash de 16 MB no da ni un byte mas para las bases. Quien
    "actualizara" ese 0x200000 al cambiar de chip se cargaria el limite
    sin enterarse, y el resultado seria un BD.BIN que la radio rechaza.

    Aqui se mide la regla, no las direcciones.
    """
    print("  el techo de BD.BIN: un mega")
    dp = os.path.join(AQUI, 'datos_pack.py')
    e, a = escribe_listas(tmp)
    emis = os.path.join(tmp, 'e.bin')
    empaqueta(e, a, emis)
    bd = os.path.join(tmp, 'BD.BIN')

    def junta(tam_aviones):
        av = os.path.join(tmp, 'av.bin')
        io.open(av, 'wb').write(i24b(tam_aviones))
        r = subprocess.run([sys.executable, dp, bd, av, emis],
                           capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr

    techo = 0x100000 - 0x1000
    emis_tam = os.path.getsize(emis)

    rc, log = junta(100 * 1024)
    if rc != 0:
        mal("un BD.BIN pequeño no se empaqueta", log.strip().split('\n')[-1])
    else:
        bien("un BD.BIN de ~100 kB se empaqueta")

    # justo por encima del mega: tiene que negarse y DECIR el numero
    rc, log = junta(techo - emis_tam)
    if rc == 0:
        mal("un BD.BIN de mas de 1 MB se ha empaquetado igual")
    elif 'NO PUEDE PASAR DE 1 MB' not in log:
        mal("se niega pero no dice que el techo es 1 MB", log.strip()[:120])
    elif 'icao24.py' not in log:
        mal("se niega sin decir que tope hay que pedirle a icao24.py")
    else:
        bien("por encima del mega se niega, dice 1 MB y da el tope a pedir")

    # y que el aviso del 90% salga antes de que sea tarde
    rc, log = junta(int(techo * 0.93) - emis_tam)
    if rc != 0:
        mal("un BD.BIN al 93% tenia que caber", log.strip().split('\n')[-1])
    elif 'AVISO' not in log:
        mal("un BD.BIN al 93% cabe pero no avisa de que queda poco")
    else:
        bien("al 93% cabe y avisa de que queda poco")

    # icao24.py no deja pedir mas de lo que existe
    ic = os.path.join(AQUI, 'icao24.py')
    if not os.path.isfile(ic):
        print("    (no esta icao24.py al lado: me salto su tope)")
        return
    r = subprocess.run([sys.executable, ic, os.devnull,
                        os.path.join(tmp, 'z.bin'), '2000000'],
                       capture_output=True, text=True)
    if r.returncode == 0:
        mal("icao24.py ha aceptado un tope de 2 MB")
    elif '1 MB' not in (r.stdout + r.stderr):
        mal("icao24.py rechaza el tope pero no dice por que")
    else:
        bien("icao24.py rechaza un tope de 2 MB y dice que el techo es 1 MB")


def main():
    datos = sys.argv[1] if len(sys.argv) > 1 else os.path.join(AQUI, 'datos')
    print("  banco de emisoras_pack.py y datos_pack.py")
    g = carga_pack()
    mide_maidenhead(g)
    mide_distancia(g)
    with tempfile.TemporaryDirectory() as tmp:
        mide_orden(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        mide_techo(tmp)
    mide_tablas(g, datos)
    if fallos:
        print("  *** %d FALLOS ***" % len(fallos))
        return 1
    print("  TODO CORRECTO")
    return 0


sys.exit(main())
