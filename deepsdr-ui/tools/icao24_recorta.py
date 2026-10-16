#!/usr/bin/env python3
"""
Recorta una base de modelos de avion para que quepa junto a la de emisoras.

  python3 icao24_recorta.py ICAO24.BIN ICAO24_RECORTADO.BIN [tope_bytes]

POR QUE HACE FALTA. La zona alta de la flash SPI son 520.192 bytes
(0x101000..0x180000) y ahi viven DOS bases: los modelos de avion, que
crecen hacia arriba desde 0x101000, y las emisoras, que crecen hacia abajo
desde 0x180000. La de emisoras mide 206 kB, asi que a los aviones les
quedan 258.048. Una base completa de OpenSky filtrada a reactores son
515.715 bytes y no cabe.

COMO SE RECORTA, Y POR QUE ASI. No por las primeras N direcciones -las
direcciones ICAO24 se reparten POR PAISES, asi que quedarse con las bajas
es quedarse con media Europa y ningun americano- ni al azar, sino por
TIPO DE AVION.

HFDL es un enlace de onda corta para rutas OCEANICAS. Lo usan los que
cruzan el Atlantico, el Pacifico y los polos: bimotores de largo radio,
cuadrimotores, y los reactores ejecutivos con equipo de HF. Un Boeing 737
de vuelos domesticos no lleva HF y no va a aparecer nunca en la pantalla,
asi que su modelo en la base es sitio gastado.

La lista de abajo es esa: las familias que vuelan oceanico. De 101.431
aviones se quedan unos 39.000 y el fichero baja a 197 kB.

QUE SE PIERDE, Y COMO SE NOTA. Si aparece un avion cuyo tipo no esta en la
lista, la columna del modelo sale VACIA - que es exactamente lo que ya
pasa hoy con los aviones que no estan en la base-. No sale un modelo
equivocado: el indicativo, la posicion y el vuelo siguen saliendo igual.

FORMATO (el mismo de entrada y de salida, ver icao24_db.h):
  16 bytes de cabecera: 'I24B', version, largo del nombre, u16 n_tipos,
                        u32 n_aviones, u32 desplazamiento de los tipos
  n_aviones x 5 bytes:  ICAO24 de mas peso primero, u16 indice de tipo
  n_tipos x largo:      el nombre, con ceros de relleno
"""
import struct
import sys

# Los que cruzan oceanos y por tanto llevan HF. Ver la cabecera.
OCEANICOS = set("""
B741 B742 B743 B744 B748 B74D B74F B74R B74S B74N
B752 B753 B762 B763 B764 B76F
B772 B77L B773 B77W B778 B779 B77F
B788 B789 B78X
A306 A30B A310 A3ST
A332 A333 A337 A338 A339 A33F A33X
A342 A343 A345 A346
A359 A35K A35X
A388
A20N A320 A321 A21N
MD11 DC10 L101 IL96 IL62 IL76 IL86 A124 A225 AN22 A140
GLEX GL5T GL7T GL8T BD70 BD7L
GLF3 GLF4 GLF5 GLF6 GLFG G150 G280
F900 F2TH FA7X FA8X F2TE
CL60 CL30 CL35
E35L E50P E55P E545 E550
C17 K35R P8 A400 C130 C30J E3TF E6 KC30
""".split())


def main():
    if len(sys.argv) not in (3, 4):
        print(__doc__)
        return 2
    ent, sal = sys.argv[1], sys.argv[2]
    tope = int(sys.argv[3]) if len(sys.argv) == 4 else 258048

    d = open(ent, 'rb').read()
    base = 0
    if d[:4] != b'I24B':
        if len(d) > 4096 and d[4096:4100] == b'I24B':
            base = 4096
        else:
            print("  MAL: no encuentro la cabecera I24B")
            return 1
    h = d[base:base + 16]
    ver, largo = h[4], h[5]
    n_tip = struct.unpack('<H', h[6:8])[0]
    n_av = struct.unpack('<I', h[8:12])[0]
    off = struct.unpack('<I', h[12:16])[0]
    print("  de partida: %d aviones, %d tipos de %d bytes, %d bytes"
          % (n_av, n_tip, largo, len(d)))

    tipos = []
    for i in range(n_tip):
        s = d[base + off + i * largo: base + off + (i + 1) * largo]
        tipos.append(s.split(b'\0')[0].decode('latin-1'))

    # El codigo de tipo es la primera palabra del nombre: "B788 Boeing 787-8"
    def codigo(n):
        return n.split(' ')[0] if n else ''

    quedan = [i for i in range(n_tip) if codigo(tipos[i]) in OCEANICOS]
    if not quedan:
        print("  MAL: ningun tipo de la lista aparece en este fichero")
        return 1
    nuevo_idx = {vi: k for k, vi in enumerate(quedan)}

    av = []
    ant = -1
    desordenado = 0
    for i in range(n_av):
        r = d[base + 16 + i * 5: base + 16 + i * 5 + 5]
        dir24 = (r[0] << 16) | (r[1] << 8) | r[2]      # de mas peso primero
        ti = struct.unpack('<H', r[3:5])[0]
        if dir24 < ant:
            desordenado += 1
        ant = dir24
        if ti in nuevo_idx:
            av.append((dir24, nuevo_idx[ti]))
    if desordenado:
        print("  AVISO: el fichero de entrada no venia ordenado (%d saltos)"
              % desordenado)
    av.sort()

    tam = 16 + 5 * len(av) + largo * len(quedan)
    print("  se quedan %d aviones (%.0f%%) y %d tipos"
          % (len(av), 100.0 * len(av) / n_av, len(quedan)))
    print("  el fichero mide %d bytes, el tope es %d  ->  %s"
          % (tam, tope, "cabe" if tam <= tope else "NO CABE"))
    if tam > tope:
        return 1

    off2 = 16 + 5 * len(av)
    out = bytearray()
    out += b'I24B' + struct.pack('<BBHII', ver, largo, len(quedan), len(av), off2)
    for dir24, ti in av:
        out += bytes(((dir24 >> 16) & 0xFF, (dir24 >> 8) & 0xFF, dir24 & 0xFF))
        out += struct.pack('<H', ti)
    for vi in quedan:
        s = tipos[vi].encode('latin-1')[:largo]
        out += s + b'\0' * (largo - len(s))
    assert len(out) == tam, (len(out), tam)
    open(sal, 'wb').write(out)
    print("  escrito %s" % sal)

    return comprueba(ent, sal)


def lee(ruta):
    """Devuelve {direccion: modelo} leyendo el fichero como lo lee la radio."""
    d = open(ruta, 'rb').read()
    base = 0 if d[:4] == b'I24B' else 4096
    h = d[base:base + 16]
    largo = h[5]
    n_tip = struct.unpack('<H', h[6:8])[0]
    n_av = struct.unpack('<I', h[8:12])[0]
    off = struct.unpack('<I', h[12:16])[0]
    if off != 16 + 5 * n_av:
        raise ValueError("el desplazamiento de los tipos no cuadra")
    if base + off + n_tip * largo != len(d):
        raise ValueError("el fichero no mide lo que dice su cabecera")
    tipos = [d[base + off + i * largo: base + off + (i + 1) * largo]
             .split(b'\0')[0].decode('latin-1') for i in range(n_tip)]
    fuera = {}
    ant = -1
    for i in range(n_av):
        r = d[base + 16 + i * 5: base + 16 + i * 5 + 5]
        a = (r[0] << 16) | (r[1] << 8) | r[2]
        if a < ant:
            raise ValueError("no esta ordenado en %d" % i)
        ant = a
        ti = struct.unpack('<H', r[3:5])[0]
        if ti >= n_tip:
            raise ValueError("indice de tipo fuera de rango en %d" % i)
        fuera[a] = tipos[ti]
    return fuera


def comprueba(ent, sal):
    """
    LO QUE IMPORTA NO ES QUE QUEPA, ES QUE NO MIENTA.

    Un recorte que se coma un byte de mas corre la tabla entera y a partir
    de ahi cada avion sale con el modelo del siguiente: la pantalla se
    llena de datos con toda la pinta de buenos. Asi que se comprueban los
    TREINTA Y OCHO MIL, no una muestra:

      - que cada avion que sobrevive tiene EL MISMO modelo que tenia;
      - que no se ha colado ninguno que no estuviera;
      - que la tabla sale ordenada, que es de lo que vive la busqueda
        binaria de la radio;
      - y que la cabecera cuadra con el tamaño del fichero.
    """
    print()
    print("  comprobando el recorte contra el original")
    a = lee(ent)
    b = lee(sal)
    malos = 0
    for dir24, modelo in b.items():
        if dir24 not in a:
            print("    %06X no estaba en el original  MAL" % dir24)
            malos += 1
        elif a[dir24] != modelo:
            print("    %06X decia %r y ahora dice %r  MAL"
                  % (dir24, a[dir24], modelo))
            malos += 1
    print("    %d aviones comprobados uno a uno, %d mal" % (len(b), malos))
    print("    la tabla sale ordenada y la cabecera cuadra con el tamaño")
    print("  %s" % ("TODO CORRECTO" if malos == 0 else "HAY FALLOS"))
    return 1 if malos else 0


sys.exit(main())
