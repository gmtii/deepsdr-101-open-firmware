#!/usr/bin/env python3
"""
El sello del cargador de arranque: version + CRC del contenido.

*** Por el dueño, el 02/10/2026 y por segunda vez el mismo dia: "en el
custom boot tampoco estas cambiando la version mamon". ***

Esto es para customboot.bin lo que avisa_version() de tools/cabecera.py es
para update.bin, y existe porque aquel arreglo se hizo solo para el
firmware: el cargador siguio con su "v2.0" fijo mientras salian dos
binarios distintos el mismo dia.

Hace tres cosas:

  1. imprime el tamaño, la version y el CRC-32/MPEG-2 del binario, que es
     EXACTAMENTE lo que el cargador pinta en la esquina de su pantalla
     (ver cargador_sello() en boot/boot_main.c). Comparar los dos numeros
     dice sin ninguna duda que hay grabado en la radio;

  2. comprueba que el binario mide lo que el enlazador dice que mide. Si no
     cuadraran, el CRC de la pantalla y el de aqui se calcularian sobre
     tramos distintos y los dos numeros no se podrian comparar - o sea que
     la comprobacion de arriba dejaria de servir sin avisar;

  3. avisa si esa version ya salio con OTRO contenido, con el mismo
     registro que usa el firmware.

Avisa y no falla, por la misma razon que cabecera.py: entre entrega y
entrega se recompila decenas de veces, y fallar en cada una obliga a subir
la version por cada punto y coma hasta que el numero deja de significar
nada.
"""
import os
import re
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
REGISTRO = os.path.join(AQUI, 'entregas.csv')


def crc32_mpeg2(datos, crc=0xFFFFFFFF):
    """Bit a bit, que es la definicion. Poly 0x04C11DB7, sin reflejar y sin
    xor final: NO es el crc32 de zlib. El valor de control sobre
    "123456789" es 0x0376E6E7, y de eso se asegura sim/cargador.c."""
    for b in datos:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 \
                else (crc << 1) & 0xFFFFFFFF
    return crc


def version_de(ruta):
    m = re.search(r'#define\s+CARGADOR_VERSION\s+"([^"]+)"',
                  open(ruta, encoding='utf-8').read())
    if not m:
        sys.exit('  no encuentro CARGADOR_VERSION en %s' % ruta)
    return m.group(1)


def tam_del_enlazador(ruta_map):
    """El _eflash del mapa: donde acaba lo que se graba. Ver el script del
    enlazador del cargador."""
    if not os.path.exists(ruta_map):
        return None
    m = re.search(r'^\s*0x0*([0-9a-fA-F]+)\s+_eflash\s*$',
                  open(ruta_map, encoding='utf-8', errors='replace').read(),
                  re.M)
    return (int(m.group(1), 16) - 0x08000000) if m else None


def avisa(clave, crc):
    visto = {}
    if os.path.exists(REGISTRO):
        for linea in open(REGISTRO, encoding='utf-8'):
            linea = linea.strip()
            if not linea or linea.startswith('#'):
                continue
            partes = linea.split(',')
            if len(partes) >= 2:
                visto[partes[0]] = partes[1]

    antes = visto.get(clave)
    if antes is not None and antes != '%08X' % crc:
        print()
        print('  *** AVISO: el cargador %s ya se genero con otro contenido.'
              % clave)
        print('      antes crc %s, ahora crc %08X' % (antes, crc))
        print('      Si esto va a salir de aqui, sube CARGADOR_VERSION en')
        print('      boot/boot_main.c. Si no, ignoralo.')
        print()

    visto[clave] = '%08X' % crc
    with open(REGISTRO, 'w', encoding='utf-8') as f:
        f.write('# version,crc32 del ultimo binario generado con ese numero.\n')
        f.write('# Lo escriben tools/cabecera.py (firmware) y\n')
        f.write('# tools/bootsello.py (cargador). Ver avisa_version().\n')
        for v in sorted(visto):
            f.write('%s,%s\n' % (v, visto[v]))


def main():
    binario = sys.argv[1] if len(sys.argv) > 1 else 'customboot.bin'
    fuente = sys.argv[2] if len(sys.argv) > 2 else 'boot_main.c'
    mapa = sys.argv[3] if len(sys.argv) > 3 else 'customboot.map'

    datos = open(binario, 'rb').read()
    ver = version_de(fuente)
    crc = crc32_mpeg2(datos)

    esperado = tam_del_enlazador(mapa)
    if esperado is not None and esperado != len(datos):
        print('  *** el binario mide %d y el enlazador dice %d: el CRC de la'
              % (len(datos), esperado))
        print('      pantalla NO se podra comparar con este. Mirar _eflash.')
        sys.exit(1)

    print('%s: %d bytes  %s  crc %08X'
          % (os.path.basename(binario), len(datos), ver, crc))
    avisa('BOOT ' + ver, crc)


if __name__ == '__main__':
    main()
