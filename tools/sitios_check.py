#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE CADA CENTRO EMISOR ESTE EN SU PAIS - 10/10/2026.

*** Nace de una aportacion: alguien geolocalizo a mano unos cientos de
sitios de aoki.txt con OSM Nominatim, con un umbral del 70 % de confianza,
los metio en sitios_xy.csv -de 46 filas a 593- y avisó de que podia haber
errores de sitio, de que lo habia probado con el toolchain sin errores y de
que no sabia como validarlo contra el mapa. ***

Las dos ultimas cosas son las importantes. Que no de errores solo quiere
decir que el fichero PARSEA: emisoras_pack.py lee tres columnas y nunca ha
tenido forma de saber si una coordenada es la buena. Y no, no habia como
validarlo. Esto es ese como.

QUE ES LO QUE SALE MAL AL GEOCODIFICAR
--------------------------------------
Nominatim siempre contesta algo. Pedirle "Hino" devuelve un Hino: el de
Sudafrica. "Gobo" devuelve el de Camerun, "Iba" un sitio de Jerusalen,
"Dushanbe" una calle de Vilnius y "San Francis" la basilica de Asis. No es
un fallo del que aporta ni del geocodificador: es que el nombre de un sitio
de onda corta, suelto y recortado a doce caracteres, es ambiguo casi
siempre.

Lo que lo deshace es el dato que el geocodificador NO tiene y nosotros SI:
de que pais emite esa estacion. Aoki lo trae en la columna ITU.

LAS DOS CRIBAS
--------------
1. EL PAIS DEL NOMBRE. Nominatim deja el pais al final del display_name,
   entre parentesis, en su idioma: "(Cameroun)", "(中国)", "(España)". No
   hace falta una tabla de paises en veinte idiomas para usarlo: basta
   comparar cada sitio con LOS DEMAS SITIOS DEL MISMO ITU. Si diecisiete
   dicen "日本" y uno dice "Uganda", el raro se señala solo. Se calibra con
   los datos, no con una lista que haya que mantener.

2. LA DISTANCIA AL CENTROIDE DE SU PAIS. Coge lo que la primera no puede:
   los ITU con una sola entrada, los nombres sin pais al final, y los
   errores DENTRO del pais correcto -"Fuzhou/Song" cayo en la calle Fuzhou
   de Shanghai, a 1.589 km del Fuzhou de verdad-.

   La distancia sola no vale como veredicto y por eso no lo es: Hawai esta
   a 5.926 km del centroide de EEUU y esta BIEN. Es una criba para mirar a
   mano, y lo que se mira a mano se apunta en LEJOS_OK con su razon.

EL CRITERIO PARA TIRAR UNA FILA, QUE NO ES "SI ES EXACTA"
---------------------------------------------------------
Un sitio sobra cuando esta PEOR QUE NO PONER NADA. Sin fila, la emisora cae
en el centroide de su pais y sigue funcionando (ver la cabecera de
sitios_xy.csv). Asi que:

  - cae en otro pais  -> fuera. El centroide del pais de verdad siempre es
                         mejor que una ciudad del pais equivocado.
  - cae en el suyo, torcida -> SE QUEDA si esta mas cerca que el centroide.
                         "Hailar" apunta al rio Argun, 197 km al norte del
                         Hailar bueno; el centroide de China esta a 2.129.
                         Doscientos kilometros de error son mucho mejor que
                         dos mil.

Uso:
    python3 tools/sitios_check.py [sitios_xy.csv]
"""
import collections
import math
import os
import re
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------------
# Lo que se mira a mano y se da por bueno. Cada linea es un sitio que la
# criba de la distancia señala y que ESTA BIEN, con el porque: sin esto el
# informe pide revisar lo mismo cada vez y acaba sin leerse.
# ---------------------------------------------------------------------
LEJOS_OK = {
    'Hawaii Koko': 'Hawai esta a 5.900 km del centroide de EEUU, y es EEUU',
    'Nome AK':     'Alaska, por lo mismo',
    'Monticello':  'Maine (WBCQ); EEUU es muy grande',
    'Okeechobee':  'Florida (WRMI); idem',
    'Red Lion':    'Pensilvania; idem',
    'Rancho Simi': 'California; idem',
    'Los Angeles': 'California; idem',
    'Dongfang':    'Hainan, al sur del todo de China',
    'Jagdaqi':     'Heilongjiang, al noreste del todo',
    'Qiqihar':     'Heilongjiang; idem',
    'Hailar':      'apunta al rio Argun, 197 km al norte del Hailar bueno: '
                   'torcido, pero el centroide de China esta a 2.129',
    'Brisbane/Nu': 'Queensland; Australia es muy grande',
    'Belem PA':    'Para, al norte de Brasil',
    'Ottawa':      'Ontario; Canada es muy grande',
    'Greenville':  'Greenville, CAROLINA DEL NORTE: el sitio de la VOA '
                   '("Site B"), confirmado en la pagina de la VOA. Quien '
                   'aporto el fichero habia cogido el condado de Greenville '
                   'de CAROLINA DEL SUR, a 480 km',
    # Los del noreste y el sureste de China. China mide 5.200 km de ancho,
    # asi que su centroide esta lejos de casi todo y la criba los señala en
    # bloque. Los nombres los confirman: 吉林省 (Jilin), 辽宁省 (Liaoning),
    # 福建省 (Fujian), 浙江省 (Zhejiang), 广东省 (Guangdong).
    'Wangqing':    'Jilin, en la frontera con Corea',
    'Yanji':       'Jilin; idem',
    'Jilin-Yanji': 'Jilin; idem',
    'Longjing Ji': 'Jilin; idem',
    'Fuyu Jilin':  'Jilin; idem',
    'Nanguan':     'Changchun, Jilin',
    'Shenyang':    'Liaoning',
    'Fuzhou':      'Fujian, en la costa de enfrente de Taiwan',
    'Fuzhou 552':  'Fujian; idem',
    'Nanping':     'Fujian; idem',
    'Jinhua 831':  'Zhejiang',
    'Huadu-Guang': 'Guangzhou, Guangdong',
    'Nicosia':     'la estacion es griega (GRC) y emite DESDE Nicosia, '
                   'Chipre. El sitio manda sobre el ITU, que es para lo que '
                   'existe este fichero',
}

# ---------------------------------------------------------------------
# Las 46 de la primera tanda (05/10/2026), puestas a mano y comprobadas una
# a una entonces. Llevan nombre escrito en castellano -"Pekin (China)",
# "EE.UU., WBCQ"- y NO un display_name de Nominatim, asi que la criba del
# pais no las puede leer y las señalaria todas. No es que esten exentas:
# es que ya se miraron, y por otro camino.
#
# Quien añada filas nuevas NO las mete aqui. Esta lista solo crece cuando
# alguien mira una fila y responde por ella.
# ---------------------------------------------------------------------
REVISADO = {
    'Agat', 'Al Seela', 'Al-Dhabbiya', 'Baoji', 'Beijing', 'Bishkek',
    'Bocaue', 'Dongfang', 'Emirler', 'Galbeni', 'Grigoriopol', 'Hanoi',
    'Hohhot', 'Ibaragi', 'Issoudun', 'Jagdaqi', 'Kashi', 'Kimjae',
    'Kuanyin', 'Kujang', 'Kununurra', 'Kunming', 'Lhasa', 'Miaoli',
    'Monticello', 'Nanning', 'Nauen', 'Noblejas', 'Okeechobee',
    'Palau Medor', 'Paochung', 'Santa Maria', 'Shijiazhuan', 'Sirjan',
    'Solt', 'Sulaibiyah', 'Talata', 'Tashkent', 'Tiganesti', 'Tinang',
    'Tinian', 'Trincomalee', 'Urumqi', 'Urumqi Hutu', 'Woofferton',
    'Xianyang',
}

UMBRAL_KM = 1500.0     # a partir de aqui, a mirar
SUELO_SIN_PAIS = 60    # ITU con una sola entrada: no se puede cribar por pais


def lee_aoki(ruta):
    filas = []
    texto = open(ruta, encoding='cp1252', errors='replace').read()
    for l in texto.split('\n'):
        if not re.match(r'^\d', l) or len(l) < 60:
            continue
        filas.append((l[30:34].strip(), l[63:75].strip()))
    return filas


def lee_xy(ruta):
    """prefijo -> (lat, lon, nombre). Las filas sin coordenadas se saltan."""
    tabla = {}
    orden = []
    for l in open(ruta, encoding='utf-8'):
        l = l.strip()
        if not l or l.startswith('#'):
            continue
        c = l.split(',')
        if len(c) < 3 or c[0] in ('codigo', 'prefijo'):
            continue
        try:
            tabla[c[0]] = (float(c[1]), float(c[2]), ','.join(c[3:]))
            orden.append(c[0])
        except ValueError:
            continue
    return tabla, orden


def km(a, b):
    la1, lo1 = math.radians(a[0]), math.radians(a[1])
    la2, lo2 = math.radians(b[0]), math.radians(b[1])
    h = (math.sin((la2 - la1) / 2) ** 2
         + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2)
    return 2 * 6371.0 * math.asin(min(1.0, math.sqrt(h)))


def pais_del_nombre(n):
    """Nominatim deja el pais al final, entre parentesis y en su idioma."""
    m = re.search(r'\(([^()]+)\)\s*$', n)
    return m.group(1).strip() if m else None


def main():
    ruta = sys.argv[1] if len(sys.argv) > 1 else os.path.join(AQUI, 'sitios_xy.csv')
    sitios, orden = lee_xy(ruta)
    paises, _ = lee_xy(os.path.join(AQUI, 'paises_xy.csv'))
    aoki = lee_aoki(os.path.join(AQUI, 'datos', 'aoki.txt'))
    mal = 0

    print('sitios: %d    paises: %d    filas de aoki: %d'
          % (len(sitios), len(paises), len(aoki)))

    # --- 0. Lo barato: formato, rangos, repetidos ---
    vistos = collections.Counter(orden)
    rep = [k for k, n in vistos.items() if n > 1]
    if rep:
        print('\nMAL: prefijos repetidos: %s' % ', '.join(sorted(rep)))
        mal += 1
    fuera = [k for k, v in sitios.items()
             if not (-90.0 <= v[0] <= 90.0) or not (-180.0 <= v[1] <= 180.0)]
    if fuera:
        print('\nMAL: coordenadas imposibles: %s' % ', '.join(sorted(fuera)))
        mal += 1
    cero = [k for k, v in sitios.items() if v[0] == 0.0 and v[1] == 0.0]
    if cero:
        print('\nMAL: en la isla nula (0,0): %s' % ', '.join(sorted(cero)))
        mal += 1

    # --- 1. Que prefijo coge cada sitio de aoki: el mas largo que case,
    #        igual que donde() en emisoras_pack.py ---
    claves = sorted(sitios, key=len, reverse=True)

    def casa(s):
        sl = s.lower()
        for k in claves:
            if sl.startswith(k.lower()):
                return k
        return None

    itu_de = collections.defaultdict(collections.Counter)
    for itu, s in aoki:
        if s:
            k = casa(s)
            if k:
                itu_de[k][itu] += 1

    cubiertas = sum(sum(c.values()) for c in itu_de.values())
    print('prefijos usados por aoki: %d de %d    filas cubiertas: %d (%.0f%%)'
          % (len(itu_de), len(sitios), cubiertas, 100.0 * cubiertas / len(aoki)))

    sin_usar = [k for k in sitios if k not in itu_de]
    if sin_usar:
        print('  (sin usar, no estorban: %s)'
              % ', '.join(sorted(sin_usar)[:8]))

    # --- 2. CRIBA DEL PAIS: cada sitio contra los demas de su mismo ITU ---
    por_itu = collections.defaultdict(collections.Counter)
    for k in sitios:
        if k in itu_de:
            p = pais_del_nombre(sitios[k][2])
            if p:
                por_itu[itu_de[k].most_common(1)[0][0]][p] += 1

    raros = []
    for k in orden:
        if k not in itu_de:
            continue
        itu = itu_de[k].most_common(1)[0][0]
        p = pais_del_nombre(sitios[k][2])
        if p is None:
            continue
        com = por_itu[itu].most_common()
        if not com or com[0][1] < 2:
            continue
        if k in REVISADO or k in LEJOS_OK:
            continue
        if p != com[0][0] and por_itu[itu][p] == 1:
            raros.append((k, itu, p, com[0][0], sum(itu_de[k].values())))

    if raros:
        print('\nMAL: %d sitios en un pais que no es el de sus vecinos de ITU'
              % len(raros))
        print('  %-14s %-4s %-24s %-24s %s'
              % ('prefijo', 'ITU', 'pais del nombre', 'lo normal del ITU', 'usos'))
        for k, itu, p, m, n in raros:
            print('  %-14s %-4s %-24s %-24s %d' % (k, itu, p[:24], m[:24], n))
        mal += 1

    # --- 3. CRIBA DE LA DISTANCIA: para lo que la del pais no alcanza ---
    lejos = []
    for k in orden:
        if k not in itu_de:
            continue
        itu = itu_de[k].most_common(1)[0][0]
        if itu not in paises or paises[itu] is None:
            continue
        d = km(sitios[k][:2], paises[itu][:2])
        # OJO: aqui NO se exime a REVISADO. Esa lista existe porque la
        # criba del pais no sabe leer sus nombres escritos a mano, no para
        # darles bula de por vida: si alguien mueve una fila revisada a
        # Chile, esta criba tiene que cantarlo igual. Las revisadas que de
        # verdad estan lejos de su centroide -Monticello, Okeechobee,
        # Dongfang, Jagdaqi- estan en LEJOS_OK con su razon, como todas.
        if d > UMBRAL_KM and k not in LEJOS_OK:
            lejos.append((d, k, itu, sum(itu_de[k].values())))
    lejos.sort(reverse=True)

    if lejos:
        print('\nMAL: %d sitios a mas de %.0f km del centroide de su pais y '
              'sin mirar' % (len(lejos), UMBRAL_KM))
        print('     Miralos uno a uno. Si estan bien, a LEJOS_OK con su razon;')
        print('     si no, fuera del csv: el centroide del pais es mejor.')
        for d, k, itu, n in lejos:
            print('  %6.0f km  %-14s %-4s  %-40s (%d usos)'
                  % (d, k, itu, sitios[k][2][:40], n))
        mal += 1

    # --- 3 bis. Que el geocodificador no haya acertado un NEGOCIO ---
    #
    # *** 16/10/2026, repasando la carpeta para el push. ***
    #
    # La linea 110 decia:
    #
    #   Camino Rec,42.2050334,-8.7579077,Rec Fitness & Padel Club, Camiño
    #   do Caramuxo, ... Vigo, Pontevedra, Galicia, 36212, España
    #
    # "Camino Rec" es el emplazamiento de SER Radio Valencia (1179 kHz) en
    # aoki.txt. El geocodificador busco ese nombre y le encajo un gimnasio
    # de padel EN VIGO: 700 km de donde esta el transmisor. Y de paso metia
    # en el repositorio la direccion de portal de un negocio, con calle,
    # poligono, barrio y codigo postal, que no pinta nada aqui.
    #
    # La comprobacion 3 de arriba no lo veia porque mide la distancia al
    # centroide del PAIS, y Vigo y Valencia son los dos España.
    #
    # Esto mira otra cosa: si lo que devolvio el geocodificador es un
    # NEGOCIO en vez de un sitio. Un transmisor de onda media no esta en un
    # gimnasio ni en una cafeteria. Saco dos con esto -el de Vigo y un
    # "Maru Cafe" en Takamatsu-, y los dos estaban mal.
    #
    # No es una regla perfecta y no pretende serlo: es barata y pilla la
    # clase de error que de otro modo se queda años.
    #
    NEGOCIOS = ('fitness', 'padel', 'pádel', ' club', 'hotel', 'restaurant',
                'pizz', 'gimnas', 'supermercad', 'centro comercial',
                'shopping', 'cafe', 'café', 'museo', 'iglesia', 'church',
                'escuela', 'colegio', 'hospital', 'farmacia', 'bar ')
    pegados = []
    for k in sorted(sitios):
        texto = sitios[k][2].lower()
        for n in NEGOCIOS:
            if n in texto:
                pegados.append((k, n.strip(), sitios[k][2]))
                break
    if pegados:
        print('\nMAL: %d sitio(s) donde el geocodificador devolvio un negocio'
              % len(pegados))
        print('     Un transmisor no esta en un gimnasio ni en una cafeteria:')
        print('     el geocodificador acerto un comercio con ese nombre. Pon')
        print('     la localidad que toque, no el portal del negocio.')
        for k, n, t in pegados:
            print('  %-16s (%s)  %s' % (k, n, t[:60]))
        mal += 1

    # --- 4. Que los de LEJOS_OK sigan existiendo ---
    fantasmas = [k for k in list(LEJOS_OK) + sorted(REVISADO) if k not in sitios]
    if fantasmas:
        print('\nOJO: LEJOS_OK nombra sitios que ya no estan: %s'
              % ', '.join(sorted(fantasmas)))
        print('     Quitalos de la lista para que no tape a uno nuevo igual.')

    if mal:
        sys.exit(1)
    print('\nSITIOS: todo correcto')


if __name__ == '__main__':
    main()
