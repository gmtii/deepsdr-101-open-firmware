#!/usr/bin/env python3
"""
mapa.py - que los tres sitios que dicen donde empieza la aplicacion digan
lo mismo.  Rama "reload", 02/10/2026.

El por que esta en avisos.mk, encima del target "mapa".  En corto: la
direccion base vive en tres ficheros que no se pueden incluir entre ellos,
y el tercero -el VTOR en system_gd32f4xx.c- es el que, si se queda
desparejado, enlaza bien, arranca bien y cuelga la radio en la primera
interrupcion sin un solo aviso por ningun lado.

Tambien comprueba que el cargador y la aplicacion no se solapen, que es la
otra forma de romper esto: si el cargador creciera por encima de su region
se comeria el principio de la aplicacion, y el enlazador del cargador no
tiene ni idea de que la aplicacion existe.
"""

import re
import sys

ESPERADO_APP  = 0x0800C000
ESPERADO_BOOT = 0x08000000
BOOT_LEN      = 48 * 1024       # sectores 0, 1 y 2
FIN_FLASH     = 0x08080000


def lee(ruta):
    return open(ruta, encoding='utf-8', errors='replace').read()


def saca(ruta, patron, que):
    m = re.search(patron, lee(ruta))
    if not m:
        sys.exit('mapa: no encuentro %s en %s.\n'
                 '      Si lo has movido, actualiza tools/mapa.py tambien: '
                 'esta comprobacion existe justo para que mover la base '
                 'duela aqui y no en la radio.' % (que, ruta))
    return int(m.group(1), 16)


def main():
    sitios = [
        ('GD32F450VE_FLASH.ld',
         r'FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*0x([0-9A-Fa-f]+)',
         'el ORIGIN de FLASH'),
        ('User/cargador.h',
         r'#define\s+CARGA_APP_BASE\s+0x([0-9A-Fa-f]+)',
         'CARGA_APP_BASE'),
        ('CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c',
         r'SCB->VTOR\s*=\s*0x(0800C[0-9A-Fa-f]{3})',
         'el SCB->VTOR de la aplicacion'),
    ]

    malos = []
    for ruta, patron, que in sitios:
        v = saca(ruta, patron, que)
        if v != ESPERADO_APP:
            malos.append('  %-46s %s = 0x%08X' % (ruta, que, v))

    if malos:
        print('mapa: la base de la aplicacion no coincide en todos los sitios.')
        print('      Esperada 0x%08X y encontrada:' % ESPERADO_APP)
        print('\n'.join(malos))
        sys.exit(1)

    # Y que el cargador quepa debajo sin tocar a la aplicacion.
    tope_boot = ESPERADO_BOOT + BOOT_LEN
    if tope_boot > ESPERADO_APP:
        sys.exit('mapa: el cargador llega hasta 0x%08X y la aplicacion '
                 'empieza en 0x%08X. Se solapan.' % (tope_boot, ESPERADO_APP))

    boot_ld = lee('boot/GD32F450VE_BOOT.ld')
    m = re.search(r'FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*0x([0-9A-Fa-f]+),\s*'
                  r'LENGTH\s*=\s*(\d+)K', boot_ld)
    if not m:
        sys.exit('mapa: no entiendo el MEMORY de boot/GD32F450VE_BOOT.ld')
    if int(m.group(1), 16) != ESPERADO_BOOT or int(m.group(2)) * 1024 != BOOT_LEN:
        sys.exit('mapa: el enlazador del cargador dice 0x%s + %sK y aqui se '
                 'espera 0x%08X + %dK.'
                 % (m.group(1), m.group(2), ESPERADO_BOOT, BOOT_LEN // 1024))

    print('mapa: cargador 0x%08X+%dkB, aplicacion 0x%08X..0x%08X (%d bytes). '
          'Los tres sitios cuadran.'
          % (ESPERADO_BOOT, BOOT_LEN // 1024, ESPERADO_APP, FIN_FLASH,
             FIN_FLASH - ESPERADO_APP))


if __name__ == '__main__':
    main()
