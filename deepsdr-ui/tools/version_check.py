#!/usr/bin/env python3
"""
QUE LA VERSION SUBA.

POR QUE EXISTE (25/09/2026). Pedido por el usuario. Y no lo estaba: llevabamos el dia entero sacando binarios
-HFDL, la base de aviones, el borrado del disco- y los cuatro decian
V1.80. Desde fuera eso es peor que no poner version: uno flashea, ve el
mismo numero y no sabe si ha flasheado o no.

Subirla es una linea en config.h y por eso se olvida, asi que la red no es
acordarse: es que el numero que dice el firmware y el que dice el diario
de cambios TIENEN QUE SER EL MISMO. Si se sube uno hay que tocar el otro,
y si no se toca ninguno esto salta antes de entregar.

No comprueba que la version sea mayor que la de ayer -no hay de donde
sacarlo sin fiarse de git- sino que las dos mitades cuenten lo mismo, que
es lo que se rompe en la practica.
"""
import re, sys


def mira_el_manual(firmware, manual_md):
    """El manual tambien dice una version, y tambien se queda atras.

    POR QUE SE AÑADE (10/10/2026, de una revision a fondo). El firmware iba
    por V4.40 y el manual -el .md, el .docx y el .pdf- decia V4.30: se
    genero once horas antes del bump y nadie lo regenero. manual_check.py
    comprueba que las capturas esten y que las secciones cuadren, pero NO
    mira la version; esta comprobacion ataba config.h con CAMBIOS.md y
    tampoco. El manual se quedaba en tierra de nadie.

    QUE MIRA, Y POR QUE JUSTO ESO. El manual lleva versiones escritas de
    dos clases, y confundirlas seria peor que no mirar:

      - las que INYECTA build.js desde User/config.h, en una frase con un
        formato fijo: «Vx.yy / A1B2C3D4». Esa es la que tiene que ir al dia.
      - y las de la NARRATIVA historica -"hasta la V4.26", "desde la
        V4.27"-, que hablan del pasado y NO se tocan nunca.

    Asi que se ancla en el CRC de ejemplo, que solo aparece en la frase
    generada. Buscar "la version mas alta del fichero" habria dado por
    bueno cualquier manual mientras alguien citara una version nueva en un
    parrafo de historia.
    """
    try:
        txt = open(manual_md, encoding='utf-8', errors='replace').read()
    except OSError as e:
        print('  no puedo leer el manual (%s)   MAL' % e)
        return 1

    vistas = re.findall(r'[«"]\s*(V\d+\.\d\d)\s*/\s*A1B2C3D4', txt)
    if not vistas:
        print('  no encuentro la frase de la version en %s   MAL' % manual_md)
        print('  (se busca «Vx.yy / A1B2C3D4», que es lo que inyecta build.js)')
        return 1

    malas = sorted(set(v for v in vistas if v != firmware))
    print('  el manual dice %s en %d sitio(s)'
          % (', '.join(sorted(set(vistas))), len(vistas)))
    if malas:
        print('  NO COINCIDE con el firmware (%s)   MAL' % firmware)
        print('  regenera el manual:  cd manual && node build.js')
        return 1
    print('  el manual esta al dia  ok')
    return 0


# ---------------------------------------------------------------------
# Y AHORA TODO LO DEMAS QUE DICE UNA VERSION - 11/10/2026
# ---------------------------------------------------------------------
#
# Pedido por el usuario.
#
# Y la respuesta honrada era "a medias", porque al ir a mirarlo aparecieron
# DOS ficheros que declaraban una version y que nadie comprobaba:
#
#   DeepSDR_Reload_Manual-ES-EN.pdf   decia V4.30   (y habia DOS copias,
#                                                    una en la raiz)
#   README.md, su bloque "Estado"     decia V4.12
#
# Dieciocho y treinta y seis versiones atras. El de la raiz es ademas el
# mas facil de encontrar, o sea seguramente el que abre el usuario.
#
# Lo incomodo: es EXACTAMENTE la familia de fallo que acabamos de barrer
# por los 181 bancos -algo declara un dato y nada comprueba que sea
# verdad-. Se encontraron seis ahi y habia dos en la propia entrega.
#
# Este barrido es la red que faltaba. Mira TODO fichero de la carpeta que
# pueda declarar una version, PDF incluidos -se les saca el texto-, en vez
# de los tres que se le pasaban por la linea de ordenes.
#
# LOS ANCLAJES SON GENERADOS, NUNCA UN "V4.xx" SUELTO. Esa disciplina ya
# estaba en mira_el_manual() y vale aqui igual: CAMBIOS.md tiene miles de
# versiones citadas en narrativa historica -"hasta la V4.26"- y buscarlas a
# pelo daria por bueno cualquier fichero mientras alguien nombre una
# version nueva en un parrafo del pasado.
import os
import subprocess
import zipfile

ANCLAJES = (
    # El pie que build.js pone en cada pagina del manual.
    (r'Manual de uso\s*[\u00b7|]\s*(V\d+\.\d\d)', 'el pie del manual'),
    # Y la frase de portada, tambien generada.
    (r'Escrito contra la versi[o\u00f3]n\s+(V\d+\.\d\d)', 'la portada del manual'),
    # El bloque "Estado" de CAMBIOS.md y de README.md.
    # Sin anclar el FINAL de la linea, y eso tiene historia: el primer
    # intento de este barrido pedia (V\d+\.\d\d)\s*$ y por eso NO cazo el
    # README, cuya linea sigue con "CARGADOR v2.15" detras. O sea que esta
    # comprobacion nacio con el mismo fallo que venia a arreglar, y solo se
    # vio al romper el README a proposito para ver si saltaba. Una
    # comprobacion que no se ha visto fallar no esta comprobada.
    (r'(?m)^\s*VERSION\s+(V\d+\.\d\d)\b', 'el bloque Estado'),
    # Y LA CABECERA DEL README, QUE CAMBIO DE FORMA Y DEJO AL BANCO
    # MIRANDO A UN FANTASMA - 17/10/2026. El README dejo de llevar una
    # linea "VERSION Vx.yy" y paso a abrir con «`V5.03` . cargador
    # `v2.16`». Ningun anclaje casaba, asi que barre_la_carpeta() leia
    # el fichero, no encontraba NINGUNA version declarada y lo daba por
    # bueno sin mirar: el README se quedo en V5.03 con el firmware en
    # V5.04 y nadie se entero. Un guardian que no encuentra nada no
    # dice "todo bien", dice "no se donde mirar" - por eso ademas de
    # este anclaje esta la cuenta de abajo.
    (r'(?m)^`(V\d+\.\d\d)`\s*[\u00b7|]\s*(?:cargador|bootloader)',
     'la cabecera del README'),
)

# Lo de terceros no se toca y no declara nuestra version.
AJENOS = ('build', 'CMSIS', 'Firmware', 'usb', 'Ft8Lib', 'rescate', '.git')


def _texto(ruta):
    """El texto del fichero, sea del tipo que sea. None si no se puede."""
    bajo = ruta.lower()
    if bajo.endswith('.pdf'):
        try:
            r = subprocess.run(['pdftotext', '-q', ruta, '-'],
                               capture_output=True, timeout=120)
            return r.stdout.decode('utf-8', 'replace')
        except (OSError, subprocess.SubprocessError):
            return None
    if bajo.endswith('.docx'):
        try:
            with zipfile.ZipFile(ruta) as z:
                return z.read('word/document.xml').decode('utf-8', 'replace')
        except (OSError, KeyError, zipfile.BadZipFile):
            return None
    try:
        return open(ruta, encoding='utf-8', errors='replace').read()
    except OSError:
        return None


def barre_la_carpeta(firmware, raiz):
    """Todo fichero que declare una version tiene que decir la misma."""
    malos = []
    mirados = 0
    sin_leer = []

    for base, dirs, ficheros in os.walk(raiz):
        dirs[:] = [d for d in dirs if d not in AJENOS]
        for f in sorted(ficheros):
            if not f.lower().endswith(('.md', '.pdf', '.docx')):
                continue
            ruta = os.path.join(base, f)
            txt = _texto(ruta)
            if txt is None:
                sin_leer.append(os.path.relpath(ruta, raiz))
                continue
            dichas = set()
            for patron, _ in ANCLAJES:
                dichas.update(re.findall(patron, txt))
            if f.lower() == 'readme.md' and base == raiz:
                # EL README NO PUEDE CALLARSE. Los demas .md declaran
                # version o no, y no pasa nada; el README es la primera
                # pantalla del repositorio y tiene DOS cabeceras, la
                # espanola y la inglesa. Si una pierde el formato y la
                # otra no, el conjunto sigue declarando V5.04 y el fallo
                # pasa de largo: por eso no basta con mirar el fichero
                # entero, hay que contar las cabeceras una por una.
                cab = re.findall(r'(?m)^(.*(?:cargador|bootloader)\s*`v[\d.]+`.*)$',
                                 txt)
                buenas = [c for c in cab
                          if re.match(r'^`V\d+\.\d\d`\s*[\u00b7|]', c)]
                if not cab:
                    malos.append((os.path.relpath(ruta, raiz),
                                  'no tiene cabecera de version'))
                elif len(buenas) != len(cab):
                    malos.append((os.path.relpath(ruta, raiz),
                                  '%d de %d cabeceras sin version delante'
                                  % (len(cab) - len(buenas), len(cab))))
            if not dichas:
                continue
            mirados += 1
            fuera = sorted(v for v in dichas if v != firmware)
            if fuera:
                malos.append((os.path.relpath(ruta, raiz), ', '.join(fuera)))

    print('  %d ficheros de la carpeta declaran una version' % mirados)
    for r in sin_leer:
        print('  no he podido leer %s (aviso, no fallo)' % r)
    if malos:
        for r, v in malos:
            print('  %s dice %s y el firmware %s   MAL' % (r, v, firmware))
        print('  (anclajes: el pie del manual, su portada, y la linea')
        print('   "VERSION Vx.yy" del bloque Estado. Un "V4.xx" suelto en')
        print('   un parrafo de historia NO cuenta y no hay que tocarlo.)')
        return 1
    print('  todos dicen %s  ok' % firmware)
    return 0


def mira_los_binarios(raiz):
    """Que los tamaños y los CRC que anuncia el README sean los de los .bin.

    POR QUE EXISTE (17/10/2026). El README se quedo en V5.03 y con el no se
    quedaron solo la version: tambien el tamaño -469.904 cuando el fichero
    media 470.328- y el CRC -3EED8CE8 cuando la cabecera decia 90681EDF-.
    Tres datos, los tres mal, en la primera pantalla de lo que ve quien
    llega al repositorio, y todos comprobables leyendo el fichero que esta
    tres carpetas mas abajo.

    Son ADEMAS los tres datos con los que alguien decide si el binario que
    se ha bajado es el bueno. Un CRC equivocado en el README no se lee como
    "el README esta viejo": se lee como "me he bajado algo que no es".

    El CRC que se publica es el de la CABECERA en +0x200, que es el que
    comprueba la radio, no el CRC-32 del fichero entero. Son numeros
    distintos y confundirlos ya costo una tarde.
    """
    import struct
    readme = os.path.join(raiz, 'README.md')
    if not os.path.isfile(readme):
        print('  no hay README.md en %s   MAL' % raiz)
        return 1
    txt = open(readme, encoding='utf-8', errors='replace').read()

    # «`update.bin` 470.328 B CRC `90681EDF`» y su version inglesa con coma.
    pares = re.findall(
        r'`(update(?:_470)?\.bin)`\s+([\d.,]+)\s*B\s+CRC\s+`([0-9A-Fa-f]{8})`',
        txt)
    if not pares:
        print('  el README no anuncia ningun binario con tamaño y CRC   MAL')
        print('  (formato esperado: `update.bin` 470.328 B CRC `90681EDF`)')
        return 1

    malos, vistos = [], set()
    for nombre, tam_txt, crc_txt in pares:
        ruta = os.path.join(raiz, nombre)
        if not os.path.isfile(ruta):
            malos.append('%s: el README lo anuncia y no esta en la carpeta'
                         % nombre)
            continue
        d = open(ruta, 'rb').read()
        dicho = int(tam_txt.replace('.', '').replace(',', ''))
        if dicho != len(d):
            malos.append('%s: el README dice %d B y mide %d'
                         % (nombre, dicho, len(d)))
        if len(d) < 0x210:
            malos.append('%s: no llega ni a la cabecera' % nombre)
            continue
        magia, _long, crc, _ver = struct.unpack('<IIII', d[0x200:0x210])
        if magia != 0x52445344:
            malos.append('%s: en +0x200 no hay cabecera "DSDR"' % nombre)
            continue
        if crc != int(crc_txt, 16):
            malos.append('%s: el README dice CRC %s y la cabecera %08X'
                         % (nombre, crc_txt.upper(), crc))
        vistos.add(nombre)

    print('  %d binarios anunciados en el README' % len(pares))
    if malos:
        for m in malos:
            print('  %s   MAL' % m)
        return 1
    print('  tamaños y CRC del README cuadran con los .bin  ok')
    return 0


def main():
    # Con DOS argumentos de mas -CAMBIOS.md y el manual- comprueba todo.
    # Con UNO solo comprueba lo que vive dentro del repositorio, que es
    # como corre en un clon: ahi docs/ y manual/ no estan, y hasta hoy
    # eso se saltaba el banco ENTERO, README incluido.
    if len(sys.argv) not in (2, 3, 4):
        print(__doc__)
        return 1
    config_h = sys.argv[1]
    cambios_md = sys.argv[2] if len(sys.argv) >= 3 else None
    manual_md = sys.argv[3] if len(sys.argv) == 4 else None

    txt = open(config_h, encoding='utf-8', errors='replace').read()
    may = re.search(r'#define\s+CONFIG_FW_MAYOR\s+"([^"]*)"', txt)
    men = re.search(r'#define\s+CONFIG_FW_MENOR\s+"([^"]*)"', txt)
    if not may or not men:
        print('  no encuentro CONFIG_FW_MAYOR/MENOR en %s   MAL' % config_h)
        return 1
    firmware = 'V%s.%s' % (may.group(1), men.group(1))

    if len(men.group(1)) != 2 or not men.group(1).isdigit():
        print('  el menor tiene que ser dos cifras, y es "%s"   MAL' % men.group(1))
        return 1

    if cambios_md is not None:
        md = open(cambios_md, encoding='utf-8', errors='replace').read()
        m = re.search(r'^\s*VERSION\s+(V\d+\.\d\d)\s*$', md, re.M)
        if not m:
            print('  falta la linea "VERSION Vx.yy" en el Estado de %s   MAL'
                  % cambios_md)
            return 1
        diario = m.group(1)
        print('  firmware dice %s, el diario dice %s' % (firmware, diario))
        if firmware != diario:
            print('  NO COINCIDEN: sube las dos o no subas ninguna   MAL')
            return 1
        print('  la version cuadra  ok')
    else:
        print('  firmware dice %s (docs/ no esta: no comparo el diario)'
              % firmware)

    rc = 0
    if manual_md is not None:
        rc |= mira_el_manual(firmware, manual_md)

    # Y el barrido de toda la carpeta, que es la red que faltaba. La raiz
    # sale de donde este config.h: User/config.h -> la carpeta del firmware.
    raiz = os.path.dirname(os.path.dirname(os.path.abspath(config_h)))
    rc |= barre_la_carpeta(firmware, raiz)
    rc |= mira_los_binarios(raiz)
    return rc


if __name__ == '__main__':
    sys.exit(main())
