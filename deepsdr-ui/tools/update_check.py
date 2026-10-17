#!/usr/bin/env python3
"""
Comprueba que update4.bin ES, byte a byte, el firmware que hay compilado.

POR QUE EXISTE ESTO (23/09/2026)
--------------------------------
Se entrego un update4.bin VIEJO creyendo que era el nuevo. La receta
`update4` del Makefile lee de build/firmware.bin pero escribe update4.bin
en el directorio del proyecto, no en build/. En build/ habia quedado un
update4.bin de una compilacion anterior, y al empaquetar la entrega se
copio ese: tenia el tamano correcto (262.152), la firma correcta en su
sitio y el puntero de pila correcto, asi que paso todas las
comprobaciones que se le hacian. Lo unico que no tenia era el cambio.

Eso son tres horas de informes de que no funciona por un fichero con la cara
del bueno. Comprobar la FECHA no vale -copiar conserva fechas raras y el
reloj del contenedor no es el de nadie-; lo que vale es comprobar el
CONTENIDO:

    update4.bin == firmware.bin + ceros hasta 0x40000 + la firma

Y de paso lo que la receta del Makefile no puede avisar: si el firmware
pasara de 256K, el relleno sale negativo y el fichero se genera igual con
la firma en el sitio equivocado. Aqui eso es un fallo con su nombre.

LA IMAGEN GRANDE (27/09/2026)
-----------------------------
Desde que se desensamblo el gestor de arranque hay un segundo formato
valido. El gestor acepta ficheros de hasta 0x50000 (327.680 bytes) y lo
unico que comprueba de la imagen son ocho bytes en 0x08060000, o sea en
el desplazamiento 0x40000 del fichero. Asi que una imagen de mas de
0x40000 tambien arranca, siempre que lleve la firma DENTRO, en su sitio.
El firmware la pone ahi con User/firma_app.c y un KEEP() en el enlazador.

En ese caso el relleno de la receta del Makefile sale negativo y no
rellena nada -correcto, la firma ya va dentro- pero los ocho bytes se
pegan igual al final. Son inofensivos: caen en zona ya borrada y el
gestor los copia sin mirarlos. Cuentan, eso si, para el limite de
0x50000, y por eso el firmware reserva sitio para ellos.

O sea que hay dos formas correctas y aqui se comprueban las dos:

    imagen <= 0x40000:  update4 == firmware + ceros hasta 0x40000 + firma
    imagen >  0x40000:  update4 == firmware + firma,
                        y ademas firmware[0x40000:0x40008] == firma

LA RAMA "reload" (02/10/2026)
-----------------------------
Con cargador propio desaparece todo lo de arriba: la imagen va de una
pieza desde 0x0800C000 y lo que la valida es una cabecera de 16 bytes en
el desplazamiento 0x200 con magia, longitud y CRC32. Ahi update4.bin ES
el firmware salvo esos 16 bytes, que el firmware trae a cero y rellena
tools/cabecera.py.

Este script se entera solo de cual de los dos formatos tiene delante
-mira si update4.bin lleva la magia "DSDR" en 0x200- para que el mismo
simulador valga para las dos ramas. Y en el formato nuevo recalcula el
CRC por su cuenta, que es justo la pregunta que importa: "este fichero,
tal y como esta, lo aceptaria el cargador?".
"""
import sys, os

FIRMA = bytes.fromhex('8f25c865599c5531')
SITIO = 0x40000    # donde el gestor busca la firma, en flash y en el fichero
TOPE  = 0x50000    # el "cmp.w r4, #0x50000" del gestor: mas grande, lo ignora

# --- rama "reload" ---
MAGIA    = b'DSDR'          # 0x52445344 little-endian
CAB_OFF  = 0x200
TOPE_NUE = 475136           # 0x08080000 - 0x0800C000
POLI     = 0x04C11DB7


def crc32_mpeg2(datos):
    """Bit a bit otra vez, a proposito: si esto importara la tabla del
    firmware o la de cabecera.py, comprobaria que son iguales entre si y
    no que el numero es el correcto."""
    crc = 0xFFFFFFFF
    for b in datos:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ POLI) & 0xFFFFFFFF if crc & 0x80000000 \
                  else (crc << 1) & 0xFFFFFFFF
    return crc


def u32(d, o):
    return d[o] | (d[o+1] << 8) | (d[o+2] << 16) | (d[o+3] << 24)


def comprueba_reload(fw, up):
    """El formato nuevo: update4 == firmware salvo los 16 bytes de cabecera."""
    fallos = 0
    print('  formato "reload": cabecera con longitud y CRC32')
    print('  firmware  %7d bytes  (%.1f%% de los %d que caben)'
          % (len(fw), 100.0 * len(fw) / TOPE_NUE, TOPE_NUE))

    if len(up) > TOPE_NUE:
        print('  NO CABE: %d bytes y la region es de %d  MAL'
              % (len(up), TOPE_NUE))
        return 1

    if len(up) != len(fw):
        print('  tamano distinto del firmware (%d vs %d)  MAL' % (len(up), len(fw)))
        return 1

    fuera = [i for i in range(len(up))
             if up[i] != fw[i] and not (CAB_OFF <= i < CAB_OFF + 16)]
    if fuera:
        print('  el contenido NO es este firmware: primer byte distinto en 0x%X'
              % fuera[0])
        print('  (casi seguro es un update4.bin de una compilacion anterior)  MAL')
        return 1
    print('  update4.bin es exactamente este firmware  ok')

    if fw[CAB_OFF:CAB_OFF + 16] != bytes(16):
        print('  el firmware NO trae la cabecera a cero en 0x%X: la seccion'
              ' .cabecera no esta donde deberia  MAL' % CAB_OFF)
        fallos += 1

    largo = u32(up, CAB_OFF + 4)
    if largo != len(up):
        print('  la longitud de la cabecera dice %d y el fichero mide %d  MAL'
              % (largo, len(up)))
        fallos += 1

    copia = bytearray(up)
    copia[CAB_OFF + 8:CAB_OFF + 12] = b'\x00\x00\x00\x00'
    crc = crc32_mpeg2(copia)
    dicho = u32(up, CAB_OFF + 8)
    if crc != dicho:
        print('  el CRC de la cabecera es %08X y el que sale de aqui es %08X'
              % (dicho, crc))
        print('  el cargador lo rechazaria  MAL')
        fallos += 1
    else:
        may, men = u32(up, CAB_OFF + 12) >> 16, u32(up, CAB_OFF + 12) & 0xFFFF
        print('  cabecera V%d.%02d, crc %08X, %d bytes libres  ok'
              % (may, men, crc, TOPE_NUE - len(up)))

    print('  %s' % ('HAY FALLOS' if fallos else 'TODO CORRECTO'))
    return 1 if fallos else 0


def main(fw_bin, up_bin):
    if not os.path.exists(fw_bin):
        print('  no existe %s  MAL' % fw_bin); return 1
    if not os.path.exists(up_bin):
        print('  no existe %s  MAL' % up_bin); return 1
    fw = open(fw_bin, 'rb').read()
    up = open(up_bin, 'rb').read()
    fallos = 0

    if len(up) > CAB_OFF + 16 and up[CAB_OFF:CAB_OFF + 4] == MAGIA:
        return comprueba_reload(fw, up)

    print('  firmware  %7d bytes  (%.1f%% de los %d que caben)'
          % (len(fw), 100.0 * len(fw) / (TOPE - 8), TOPE - 8))
    if len(fw) + 8 > TOPE:
        print('  el firmware NO CABE: el update4.bin saldria de %d bytes y el'
              % (len(fw) + 8))
        print('  gestor solo acepta hasta %d. Sobran %d  MAL'
              % (TOPE, len(fw) + 8 - TOPE))
        return 1

    if len(fw) > SITIO:
        # Imagen grande: la firma tiene que ir DENTRO, en 0x40000.
        dentro = fw[SITIO:SITIO + 8]
        if dentro != FIRMA:
            print('  la imagen pasa de 0x40000 y la firma NO esta en 0x%05X:'
                  % SITIO)
            print('    esperado %s' % FIRMA.hex(' ').upper())
            print('    hay      %s' % dentro.hex(' ').upper())
            print('  la radio se quedaria en "Running APP---"  MAL')
            return 1
        print('  firma dentro de la imagen, en 0x%05X  ok' % SITIO)
        esperado = fw + FIRMA
    else:
        esperado = fw + b'\x00' * (SITIO - len(fw)) + FIRMA
    print('  update4   %7d bytes  (esperado %d)' % (len(up), len(esperado)))
    if len(up) != len(esperado):
        print('  tamano distinto  MAL'); fallos += 1
    elif up != esperado:
        # decir DONDE, que es lo que distingue "viejo" de "corrupto"
        n = next(i for i in range(len(up)) if up[i] != esperado[i])
        print('  el contenido NO es este firmware: primer byte distinto en 0x%X' % n)
        print('  (casi seguro es un update4.bin de una compilacion anterior)  MAL')
        fallos += 1
    else:
        print('  update4.bin es exactamente este firmware  ok')

    if up[-8:] != FIRMA:
        print('  la firma final no esta  MAL'); fallos += 1

    print('  %s' % ('HAY FALLOS' if fallos else 'TODO CORRECTO'))
    return 1 if fallos else 0


if __name__ == '__main__':
    fw = sys.argv[1] if len(sys.argv) > 1 else '../deepsdr/build/firmware.bin'
    up = sys.argv[2] if len(sys.argv) > 2 else '../deepsdr/update4.bin'
    sys.exit(main(fw, up))
