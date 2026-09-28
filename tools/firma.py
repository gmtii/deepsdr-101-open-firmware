#!/usr/bin/env python3
"""Comprueba que el .bin sigue siendo arrancable por el gestor de fabrica.

QUE MIRA EL GESTOR, Y CUANDO
----------------------------
El gestor de arranque de la radio ("Bootloader v4.0.3 ... by BH5HNU") hace
esto en 0x0800A7CA, en CADA arranque, no solo al actualizar:

    ldr.w r0, [0x08020000]     ; la pila inicial de la app
    ubfx  r0, r0, #17, #12
    cmp   r0, #0x800           ; -> el SP tiene que caer en el TCM
    bne   -> "APP Not Programmed !" y cuelgue
    memcmp(0x0800be2c, 0x08060000, 8)
    ==0 -> "Running APP..." y salta
    !=0 -> "Running APP---" y cuelgue

De la imagen no mira un solo byte mas: ni checksum, ni CRC, ni longitud.
Los dos unicos requisitos son esos: SP en el TCM y ocho bytes concretos en
0x08060000.

Y el camino de actualizacion no comprueba NADA: acepta el fichero si mide
0x50000 bytes o menos ("cmp.w r4, #0x50000"), borra los sectores 5, 6 y 7
(0x08020000..0x0807FFFF) y lo copia tal cual a 0x08020000.

POR QUE HACE FALTA ESTE FICHERO
-------------------------------
Desde el 27/09/2026 la imagen pasa de 0x40000 bytes, asi que la firma va
DENTRO de ella (User/firma_app.c, clavada por el enlazador en 0x08060000).
Eso significa que ahora hay tres formas silenciosas de generar un
update4.bin que la radio rechaza sin decir por que:

  1. Que --gc-sections se lleve la firma por delante. No la referencia
     nadie; lo unico que la sujeta es un KEEP() en el enlazador.
  2. Que alguien mueva la region FIRMA o meta algo delante y la firma acabe
     en otro sitio.
  3. Que el fichero pase de 0x50000. El gestor no dice "demasiado grande":
     simplemente se salta la actualizacion y arranca la app vieja, que es
     justo lo que parece cuando la actualizacion "no ha hecho nada".

Las tres dan la misma pantalla en blanco y el mismo rato perdido. Aqui
fallan en la compilacion, con el numero delante.
"""

import sys

FIRMA   = bytes.fromhex('8f25c865599c5531')
BASE    = 0x08020000
SITIO   = 0x40000        # desplazamiento exacto donde el gestor la busca
TOPE    = 0x50000        # el "cmp.w r4, #0x50000" del gestor
COLETA  = 8              # los 8 bytes que la receta del Makefile pega al
                         # final aunque la firma ya vaya dentro (el relleno
                         # "0x40000 - len" sale negativo y no rellena nada)


def kb(n):
    """Los mismos kilobytes que ensena la radio.

    TRUNCA, no redondea, porque eso es lo que hace info_kb() en main.c:
    parte entera por un lado y (resto * 10) / 1024 por el otro, las dos en
    enteros. Con round() aqui, 42.052 bytes salian "41,1 kB" en el PC y
    "41,0" en la pantalla: el mismo numero contado por dos instrumentos que
    no se ponen de acuerdo. Son 0,1 kB y no le importan a nadie -hasta el
    dia que dos numeros que tenian que ser iguales no lo son y hay que
    averiguar cual de los dos miente-.
    """
    return '%u,%u kB' % (n // 1024, ((n % 1024) * 10) // 1024)


def main(ruta):
    with open(ruta, 'rb') as f:
        img = f.read()

    if len(img) < SITIO:
        # Imagen pequena: la firma la pega la receta detras del relleno de
        # ceros, como toda la vida. Nada que comprobar aqui.
        print('Firma: imagen de %s, por debajo de 0x40000; la pega la receta.'
              % kb(len(img)))
        return 0

    fallos = []

    tiene = img[SITIO:SITIO + 8]
    if tiene != FIRMA:
        fallos.append(
            'la firma NO esta en el desplazamiento 0x%05X.\n'
            '    esperado: %s\n'
            '    hay:      %s\n'
            '    La radio se quedaria colgada en "Running APP---".\n'
            '    Mira que User/firma_app.c se compile y que el KEEP(*(.firma))\n'
            '    del enlazador siga ahi.'
            % (SITIO, FIRMA.hex(' ').upper(), tiene.hex(' ').upper()))

    fichero = len(img) + COLETA
    if fichero > TOPE:
        fallos.append(
            'el update4.bin saldria de %d bytes y el gestor solo acepta\n'
            '    hasta %d (0x%X). No avisa: se salta la actualizacion y\n'
            '    arranca la app vieja. %s.'
            % (fichero, TOPE, TOPE,
               'Sobra 1 byte' if (fichero - TOPE) == 1
               else 'Sobran %d bytes' % (fichero - TOPE)))

    if fallos:
        sys.stderr.write('\nFIRMA: %d problema(s).\n\n' % len(fallos))
        for f in fallos:
            sys.stderr.write('  - %s\n\n' % f)
        return 1

    # Del .bin NO se puede deducir cuanto ocupa de verdad la region baja:
    # el hueco entre el final de la imagen y 0x08060000 viene relleno de
    # ceros por objcopy y desde aqui no se distingue del contenido. Esa
    # cuenta la da el enlazador y la repite tools/pila.py; aqui solo se
    # informa de lo que este fichero si sabe.
    desvan = len(img) - SITIO - 8
    print('Firma OK en 0x%05X. Desvan %s de 64,0 kB. '
          'update4.bin: %d de %d bytes (%d libres).'
          % (SITIO, kb(desvan), fichero, TOPE, TOPE - fichero))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
