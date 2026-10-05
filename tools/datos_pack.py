#!/usr/bin/env python3
"""
Junta las bases de datos de la radio en un solo BD.BIN.

  python3 datos_pack.py BD.BIN ICAO24.BIN EMISORAS.BIN

*** Por el dueño del proyecto: "creo que para futuras versiones estaria
bien fusionarlos, para solo tener que grabar un .bin cada vez". ***

Y no es solo comodidad. Antes cada base tenia su direccion FIJA en el
firmware:

    0x101000 .. 0x140000   aviones
    0x140000 .. 0x180000   emisoras   (reparto viejo; ahora lo decide este guion)

Ese 0x140000 era un numero a ojo, y era una decision del FIRMWARE sobre
cuanto puede ocupar cada una: para cambiarlo habia que recompilar. Peor
aun, habia DOS cargadores escribiendo en la misma zona y uno tenia que
mirar donde acababa el otro; cuando ese calculo falla, la segunda base no
se carga y el mensaje no dice por que.

Con un solo fichero y un directorio delante, el reparto lo decide ESTE
script mirando lo que miden las dos de verdad, y no puede haber solape
porque no hay dos cosas que solapar.

EL FORMATO. Todo entero de menor peso primero.

  directorio, 4.096 bytes (un bloque de borrado entero):
     0   'D','S','D','R'
     4   u8  version = 1
     5   u8  cuantas secciones
     6   u16 cero
     8   u32 lo que mide el fichero entero
    12   u32 suma de los bytes 0..11 y de las secciones
    16   secciones, 16 bytes cada una:
           +0   u32 tipo: las cuatro letras con las que empieza el
                fichero de esa seccion ('I24B', 'EMIS'), leidas como
                entero. Asi no hay una segunda lista de tipos que cuadrar
           +4   u32 donde empieza, desde el principio del fichero
           +8   u32 lo que mide
           +12  u32 su suma

  y despues las secciones, cada una alineada a 4.096.

POR QUE HAY DOS NIVELES DE SUMA. La del directorio son 144 bytes y la
radio la comprueba en cada arranque: es lo que impide que unos bytes
cualesquiera que hubiera ahi se lean como desplazamientos y manden a leer
a cualquier parte. Las de las secciones son casi medio megabyte por un bus
lento -del orden de dos segundos- y por eso se comprueban UNA vez, al
cargar, que es cuando de verdad puede haberse copiado mal.
"""
import os
import struct
import sys

# EL TECHO DE BD.BIN: UN MEGA. SIEMPRE, PONGA EL CHIP QUE SE PONGA.
#
# Esto eran dos direcciones, BASE = 0x101000 y TOPE = 0x200000, o sea el
# principio y el final de la zona alta EN EL CHIP DE 2 MB DE FABRICA. La
# resta daba el numero bueno por casualidad, y era una trampa: el dia que
# alguien ponga un chip de 16 MB y "actualice" TOPE a 0x1000000, el limite
# se va a quince megas y este guion bendice un BD.BIN que la radio no
# puede cargar.
#
# La regla de verdad esta en User/zona_alta.h y no habla de direcciones:
#
#     ZA_BYTES_REGION  0x100000   la region de datos es SIEMPRE el ultimo
#                                 mega del chip, sea de 2 MB o de 16
#     ZA_HUECO         0x1000     un bloque de borrado de margen entre el
#                                 final del disco USB y el principio de
#                                 los datos, para que ninguno de los dos
#                                 pueda borrar al otro
#
# o sea que lo que puede medir BD.BIN son 0xFF000 = 1.044.480 bytes, y
# comprarse una flash mas grande NO da ni un byte mas para las bases: todo
# lo que crece el chip va al disco del usuario. Esta escrito asi a
# proposito, porque es lo que hace que un mismo BD.BIN valga en cualquier
# chip - el directorio guarda desplazamientos, no direcciones.
ZA_BYTES_REGION = 0x100000
ZA_HUECO = 0x1000
TECHO = ZA_BYTES_REGION - ZA_HUECO        # 1.044.480 bytes

# Donde cae todo esto en el chip de 2 MB de fabrica. Solo para poder
# imprimir direcciones reconocibles: aqui NO se decide nada con ellas.
BASE = 0x101000

CAB = 4096
SEC_MAX = 8

# A partir de aqui se avisa aunque quepa. Un BD.BIN al 95% cabe hoy y no
# cabra en cuanto EiBi publique la lista de la temporada siguiente, y mas
# vale enterarse ahora que delante de la radio.
AVISO = 0.90


def suma(b, v=0x811C9DC5):
    for x in b:
        v = ((v ^ x) * 16777619) & 0xFFFFFFFF
    return v


def sube(n):
    return (n + 4095) & ~4095


def main():
    if len(sys.argv) < 3 or len(sys.argv) > 2 + SEC_MAX:
        print(__doc__)
        return 2
    salida = sys.argv[1]
    entradas = sys.argv[2:]

    secs = []
    for ruta in entradas:
        d = open(ruta, 'rb').read()
        if len(d) < 4:
            print("  MAL: %s esta vacio" % ruta)
            return 1
        tipo = struct.unpack('<I', d[:4])[0]
        nombre = d[:4].decode('latin-1')
        if not all(32 <= c < 127 for c in d[:4]):
            print("  MAL: %s no empieza por cuatro letras, no se de que es"
                  % ruta)
            return 1
        secs.append((nombre, tipo, d, os.path.basename(ruta)))

    vistos = set()
    for nombre, tipo, _d, f in secs:
        if tipo in vistos:
            print("  MAL: dos secciones del mismo tipo '%s'" % nombre)
            return 1
        vistos.add(tipo)

    # Se colocan a partir del directorio, cada una en su bloque.
    off = CAB
    colocadas = []
    for nombre, tipo, d, f in secs:
        colocadas.append((nombre, tipo, off, d, f))
        off += sube(len(d))
    total = off

    print("  %d secciones:" % len(secs))
    for nombre, tipo, o, d, f in colocadas:
        print("    %-10s %-16s 0x%06X  %8d bytes"
              % (nombre, f, BASE + o, len(d)))
    print("    %-10s %-16s 0x%06X" % ("(fin)", "", BASE + total))

    if total > TECHO:
        print("  MAL: BD.BIN NO PUEDE PASAR DE 1 MB.")
        print("  mide %d bytes (%.1f kB) y el techo son %d (%.1f kB):"
              " se pasa en %d bytes."
              % (total, total / 1024.0, TECHO, TECHO / 1024.0, total - TECHO))
        print("  No es el tamaño del chip: la zona de datos es siempre el")
        print("  ultimo mega, y una flash mas grande solo agranda el disco.")
        # Lo mas util que se puede decir aqui: cuanto tendria que medir la
        # base de aviones, que es la unica que se puede recortar sola.
        otras = sum(sube(len(d)) for n, t, d, f in secs if n != 'I24B')
        cabe = TECHO - CAB - otras
        if cabe > 0:
            print("  los aviones tendrian que caber en %d bytes:" % cabe)
            print("    python3 icao24.py <el CSV> ICAO24.BIN %d" % cabe)
        else:
            print("  y no es culpa de los aviones: las demas secciones ya")
            print("  ocupan %d bytes de los %d." % (otras + CAB, TECHO))
        return 1

    cab = bytearray()
    cab += b'DSDR' + struct.pack('<BBHI', 1, len(secs), 0, total)
    cuerpo_sec = bytearray()
    for nombre, tipo, o, d, f in colocadas:
        cuerpo_sec += struct.pack('<IIII', tipo, o, len(d), suma(d))
    cab += struct.pack('<I', suma(bytes(cab) + bytes(cuerpo_sec)))
    cab += cuerpo_sec
    assert len(cab) == 16 + 16 * len(secs), len(cab)

    out = bytearray(cab)
    out += b'\xFF' * (CAB - len(out))
    for nombre, tipo, o, d, f in colocadas:
        assert len(out) == o, (len(out), o)
        out += d
        out += b'\xFF' * (sube(len(d)) - len(d))
    assert len(out) == total, (len(out), total)
    open(salida, 'wb').write(out)

    print("  %s: %d bytes (%.1f kB), %.0f%% del mega que cabe, sobran %d"
          % (salida, total, total / 1024.0, 100.0 * total / TECHO,
             TECHO - total))
    if total > TECHO * AVISO:
        print("  AVISO: queda menos del %d%% libre. BD.BIN no puede pasar de"
              " 1 MB (%d bytes)" % (int((1.0 - AVISO) * 100), TECHO))
        print("  en ningun chip, asi que la lista siguiente puede no caber.")
    return comprueba(salida, colocadas)


def comprueba(ruta, colocadas):
    """
    Se vuelve a leer el fichero escrito y se comprueba como lo lee la
    radio: la suma del directorio, y despues la de cada seccion sobre los
    bytes que hay de verdad en el sitio que dice el directorio.

    No es ceremonia. Un desplazamiento mal calculado da un fichero con
    todas las sumas buenas por separado y las secciones corridas, y eso en
    la radio sale como modelos de avion equivocados - datos con toda la
    pinta de buenos-.
    """
    d = open(ruta, 'rb').read()
    n = d[5]
    total = struct.unpack('<I', d[8:12])[0]
    mal = 0
    if d[:4] != b'DSDR' or d[4] != 1:
        print("  MAL: la cabecera no es la que se acaba de escribir")
        return 1
    if total != len(d):
        print("  MAL: dice medir %d y mide %d" % (total, len(d)))
        mal += 1
    v = suma(d[:12])
    v = suma(d[16:16 + 16 * n], v)
    if v != struct.unpack('<I', d[12:16])[0]:
        print("  MAL: la suma del directorio no cuadra")
        mal += 1
    for i in range(n):
        p = d[16 + 16 * i: 32 + 16 * i]
        tipo, off, tam, s = struct.unpack('<IIII', p)
        nom = struct.pack('<I', tipo).decode('latin-1')
        if off + tam > len(d):
            print("  MAL: la seccion %s se sale del fichero" % nom)
            mal += 1
            continue
        if d[off:off + 4] != struct.pack('<I', tipo):
            print("  MAL: en 0x%X no empieza '%s'" % (off, nom))
            mal += 1
        if suma(d[off:off + tam]) != s:
            print("  MAL: la suma de %s no cuadra" % nom)
            mal += 1
    print("  comprobado: %s" % ("TODO CORRECTO" if mal == 0 else "HAY FALLOS"))
    return 1 if mal else 0


sys.exit(main())
