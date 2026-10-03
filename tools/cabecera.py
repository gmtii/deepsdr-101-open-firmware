#!/usr/bin/env python3
"""
cabecera.py - rellena la cabecera de la imagen y escribe update.bin.
Rama "reload", 02/10/2026.

QUE SUSTITUYE
-------------
A tools/firma.py, que pegaba ocho bytes constantes en el desplazamiento
0x40000 del fichero porque era lo unico que miraba el gestor de fabrica:

    memcmp(0x0800be2c, 0x08060000, 8)

Aquello no comprobaba NADA de la imagen -ni longitud, ni suma, ni CRC-, lo
que tenia una consecuencia fea: una imagen grabada a medias con esos ocho
bytes dentro pasaba la prueba y arrancaba.  Desde fuera se veia una radio
que se cuelga o se reinicia sola, sin ninguna pista de que lo que le pasa
es que le falta un trozo.

Con cargador propio la imagen lleva cabecera, y esta es la herramienta que
la rellena: es el unico momento del proceso en que se conocen la longitud y
el CRC, porque hasta que el enlazador no ha terminado no existen.

EL FORMATO, en CARGA_APP_BASE + 0x200 (ver User/cabecera_app.c):

    +0x00  magia     0x52445344, "DSDR" en un volcado
    +0x04  longitud  bytes de la imagen
    +0x08  crc32     de esos bytes, con ESTE campo contado como cero
    +0x0C  version   (mayor << 16) | menor, sacada de User/config.h

CUAL DE LOS CRC32, QUE NO ES DETALLE
------------------------------------
CRC-32/MPEG-2: polinomio 0x04C11DB7, empieza en 0xFFFFFFFF, SIN reflejar
entrada ni salida y SIN xor final.  NO es el de zlib.crc32() ni el de los
ZIP, que van reflejados y con xor final y dan otro numero.

Si alguien reescribe esto con zlib "porque es lo mismo", el cargador
rechazara todas las imagenes y el motivo no se vera por ningun lado: la
radio dira "imagen incompleta o corrompida" sobre una imagen perfecta.  Por
eso aqui esta el calculo BIT A BIT, que es la definicion, y no una copia de
la tabla que usa el firmware: asi los dos lados son implementaciones
independientes y el banco (sim/cargador.c) comprueba que coinciden.  Si
coincidieran por compartir codigo, no se estaria comprobando nada.
"""

import re
import sys

MAGIA   = 0x52445344
CAB_OFF = 0x200          # donde el cargador la busca, ver CARGA_CAB_OFF
POLI    = 0x04C11DB7

# 0x08080000 - 0x0800C000. Tiene que cuadrar con CARGA_TAM_MAX.
TOPE    = 475136


def crc32_mpeg2(datos, crc=0xFFFFFFFF):
    """Bit a bit, que es la definicion. Lento y da igual: son 300 kB una vez."""
    for b in datos:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ POLI) & 0xFFFFFFFF if crc & 0x80000000 \
                  else (crc << 1) & 0xFFFFFFFF
    return crc


def version_de_config(ruta='User/config.h'):
    """(mayor, menor) leidos de config.h. Si no se pueden leer, se para:
    una version equivocada en la cabecera es peor que no tenerla."""
    texto = open(ruta, encoding='utf-8', errors='replace').read()
    may = re.search(r'#define\s+CONFIG_FW_MAYOR\s+"(\d+)"', texto)
    men = re.search(r'#define\s+CONFIG_FW_MENOR\s+"(\d+)"', texto)
    if not may or not men:
        sys.exit('cabecera: no encuentro CONFIG_FW_MAYOR/MENOR en %s' % ruta)
    return int(may.group(1)), int(men.group(1))


def u32(v):
    return bytes((v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF))


REGISTRO = 'tools/entregas.csv'


def avisa_version(version, crc):
    """
    Avisa si esta version ya salio con OTRO contenido.

    *** Por el dueño, 02/10/2026: "no estas cambiando el numero de version
    en los updates". ***

    Y tenia razon: salieron SEIS update.bin distintos llamados todos V3.00.
    Eso es el mismo fallo que llevo a escribir tools/update4_check.py en su
    dia -un fichero con la cara del bueno- pero una capa mas arriba: aqui
    el fichero SI es el firmware recien compilado, lo que miente es su
    etiqueta.

    Esto es un AVISO y no un error a proposito. El bucle de trabajo
    recompila decenas de veces entre entrega y entrega y fallar en cada una
    obligaria a subir la version por cada punto y coma, que es peor: acabas
    subiendola sin mirar y el numero vuelve a no significar nada.

    Lo que SI es a prueba de despistes es el CRC, que sale del contenido y
    no de que nadie se acuerde: la radio lo enseña al lado de la version en
    Ajustes -> Equipo -> Informacion, asi que dos binarios distintos nunca
    pueden parecer el mismo aunque compartan etiqueta.
    """
    import os

    visto = {}
    if os.path.exists(REGISTRO):
        for linea in open(REGISTRO, encoding='utf-8'):
            linea = linea.strip()
            if not linea or linea.startswith('#'):
                continue
            partes = linea.split(',')
            if len(partes) >= 2:
                visto[partes[0]] = partes[1]

    antes = visto.get(version)
    if antes is not None and antes != '%08X' % crc:
        print()
        print('  *** AVISO: la %s ya se genero con otro contenido.' % version)
        print('      antes crc %s, ahora crc %08X' % (antes, crc))
        print('      Si esto va a salir de aqui, sube CONFIG_FW_MENOR en')
        print('      User/config.h. Si no, ignoralo.')
        print()

    visto[version] = '%08X' % crc
    with open(REGISTRO, 'w', encoding='utf-8') as f:
        f.write('# version,crc32 del ultimo update.bin generado con ese numero.\n')
        f.write('# Lo escribe tools/cabecera.py. Ver avisa_version().\n')
        for v in sorted(visto):
            f.write('%s,%s\n' % (v, visto[v]))


def main():
    if len(sys.argv) != 3:
        sys.exit('uso: cabecera.py <firmware.bin> <update.bin>')
    entrada, salida = sys.argv[1], sys.argv[2]

    img = bytearray(open(entrada, 'rb').read())

    # --- Las cuatro cosas que pueden ir mal, y cada una con su mensaje ---

    if len(img) % 4:
        # El enlazador alinea todas las secciones a 4, asi que esto solo
        # pasa si alguien ha tocado el .ld. Se rellena y se avisa.
        img += b'\x00' * (4 - len(img) % 4)
        print('cabecera: AVISO, la imagen no era multiplo de 4; rellenada.')

    if len(img) > TOPE:
        sys.exit('cabecera: la imagen mide %d bytes y la region es de %d. '
                 'Sobran %d.' % (len(img), TOPE, len(img) - TOPE))

    if len(img) < CAB_OFF + 16:
        sys.exit('cabecera: la imagen mide %d bytes, menos que la propia '
                 'cabecera. Algo ha salido muy mal en el enlazado.'
                 % len(img))

    hueco = img[CAB_OFF:CAB_OFF + 16]
    if hueco != bytes(16):
        # Si ahi no hay dieciseis ceros, la seccion .cabecera no ha caido
        # donde dice el enlazador -o --gc-sections se la ha llevado y lo
        # que estamos a punto de pisar es CODIGO-. Pararse aqui es
        # obligatorio: machacarlo daria un update.bin que se graba bien y
        # luego hace cualquier cosa.
        sys.exit('cabecera: en el desplazamiento 0x%X no hay dieciseis ceros '
                 'sino %s. La seccion .cabecera no esta donde deberia; '
                 'revisa el KEEP() del enlazador antes de seguir.'
                 % (CAB_OFF, hueco[:8].hex()))

    mayor, menor = version_de_config()

    img[CAB_OFF +  0:CAB_OFF +  4] = u32(MAGIA)
    img[CAB_OFF +  4:CAB_OFF +  8] = u32(len(img))
    img[CAB_OFF +  8:CAB_OFF + 12] = u32(0)          # el CRC, aun a cero
    img[CAB_OFF + 12:CAB_OFF + 16] = u32((mayor << 16) | menor)

    # El CRC se calcula con su propio campo a cero, que es como ya esta.
    crc = crc32_mpeg2(img)
    img[CAB_OFF + 8:CAB_OFF + 12] = u32(crc)

    open(salida, 'wb').write(img)

    libre = TOPE - len(img)
    print('%s: %d bytes  V%d.%02d  crc %08X  libre %d (%.1f kB)'
          % (salida, len(img), mayor, menor, crc, libre, libre / 1024.0))

    avisa_version('V%d.%02d' % (mayor, menor), crc)


if __name__ == '__main__':
    main()
