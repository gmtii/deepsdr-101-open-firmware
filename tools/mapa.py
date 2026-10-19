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

ESPERADO_APP  = 0x08008000
ESPERADO_BOOT = 0x08000000
BOOT_LEN      = 32 * 1024       # sectores 0 y 1
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



def sin_comentarios(t):
    """
    El texto con los comentarios y las cadenas en blanco (conservando los
    saltos de linea, para que los numeros de linea sigan cuadrando).

    Hace falta porque el primer barrido salto sobre un comentario de
    boot/boot_main.c que CITA la linea mala para explicarla. Un guardian que
    se cree lo que lee en un comentario no sirve: el comentario puede decir
    cualquier cosa, y de hecho decir cosas es para lo que esta.
    """
    salida = []
    i, n = 0, len(t)
    while i < n:
        c = t[i]
        if c == '/' and i + 1 < n and t[i+1] == '*':
            j = t.find('*/', i + 2)
            j = n if j < 0 else j + 2
            salida.append(''.join(ch if ch == '\n' else ' ' for ch in t[i:j]))
            i = j
        elif c == '/' and i + 1 < n and t[i+1] == '/':
            j = t.find('\n', i)
            j = n if j < 0 else j
            salida.append(' ' * (j - i))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and t[j] != c:
                j += 2 if t[j] == '\\' else 1
            j = min(j + 1, n)
            salida.append(''.join(ch if ch == '\n' else ' ' for ch in t[i:j]))
            i = j
        else:
            salida.append(c)
            i += 1
    return ''.join(salida)


def vtor_sueltos():
    """
    TODOS los SCB->VTOR del arbol, no solo el que esperamos.

    *** 02/10/2026, y esto costo una radio colgada. *** La version anterior
    de este guion comprobaba UN SCB->VTOR: el de system_gd32f4xx.c, buscando
    el valor nuevo. Y daba el visto bueno... mientras en User/main.c habia un
    SEGUNDO "SCB->VTOR = 0x08020000" escrito a pelo, que corre DESPUES y
    deshacia el bueno.

    El sintoma fue exactamente el que este fichero decia evitar: el firmware
    enlaza, el cargador graba al 100 %, salta, y la radio se queda con la
    pantalla del cargador congelada porque se estrella en la primera
    interrupcion. Ni un aviso, ni un mensaje.

    La leccion no es que se olvide un sitio -eso pasa-, es que el guardian
    buscaba lo que sabia que existia en vez de buscar la CLASE de cosa
    peligrosa. Asi que ahora barre todo el arbol y exige que cada VTOR que
    encuentre sea uno de los tres valores que tienen sentido:

        0x08000000     el cargador: su tabla esta en la base
        0x08008000     la aplicacion (= CARGA_APP_BASE)
        0              el salto a la ROM de fabrica (modo DFU)

    Cualquier otro es un fallo, lo haya escrito quien lo haya escrito.
    """
    import os

    buenos = {ESPERADO_APP, ESPERADO_BOOT, 0}
    malos = []
    cuantos = 0

    for raiz, dirs, fich in os.walk('.'):
        # Firmware/ y CMSIS/DSP son codigo del fabricante que no tocamos.
        # gd32f4xx_misc.c trae nvic_vector_table_set(), un setter generico
        # con el valor por parametro: no se puede juzgar aqui, y ademas no
        # lo llama nadie en este proyecto (comprobado con grep). Si algun
        # dia alguien lo llamara, este guardian NO lo veria - por eso queda
        # dicho aqui y no solo pensado.
        dirs[:] = [d for d in dirs
                   if d not in ('build', '__pycache__', '.git',
                                'Firmware', 'Ft8Lib', 'usb', 'DSP')]
        for n in fich:
            if not n.endswith(('.c', '.h')):
                continue
            ruta = os.path.join(raiz, n)
            texto = sin_comentarios(lee(ruta))
            for m in re.finditer(r'SCB->VTOR\s*=\s*([^;]+);', texto):
                crudo = m.group(1).strip()
                cuantos += 1
                # CARGA_APP_BASE y los literales hex o decimales
                if crudo.startswith('CARGA_APP_BASE'):
                    continue
                v = None
                mm = re.fullmatch(r'0[xX]([0-9A-Fa-f]+)[uU]?[lL]*', crudo)
                if mm:
                    v = int(mm.group(1), 16)
                elif re.fullmatch(r'\d+[uU]?[lL]*', crudo):
                    v = int(re.sub(r'[uUlL]', '', crudo))
                if v is None or v not in buenos:
                    linea = texto[:m.start()].count('\n') + 1
                    malos.append('  %s:%d   SCB->VTOR = %s' % (ruta, linea, crudo))

    if malos:
        print('mapa: hay SCB->VTOR con un valor que no cuadra con este mapa.')
        print('      Los unicos que valen son CARGA_APP_BASE, 0x%08X y 0:'
              % ESPERADO_BOOT)
        print('\n'.join(malos))
        print('      Un VTOR mal puesto NO da error al compilar ni al grabar:')
        print('      la radio se cuelga en la primera interrupcion, en negro.')
        sys.exit(1)

    return cuantos


def main():
    sitios = [
        ('GD32F450VE_FLASH.ld',
         r'FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*0x([0-9A-Fa-f]+)',
         'el ORIGIN de FLASH'),
        ('User/cargador.h',
         r'#define\s+CARGA_APP_BASE\s+0x([0-9A-Fa-f]+)',
         'CARGA_APP_BASE'),
        # EL PATRON NO LLEVA LA DIRECCION DENTRO, Y ES A PROPOSITO.
        # Ponia 0x(0800C[0-9A-Fa-f]{3}), o sea que buscaba el valor que
        # esperaba encontrar. Al mover la base el 17/10/2026 este guion no
        # dijo "vale 0x08008000 y se espera 0x0800C000", que es su oficio:
        # dijo "no encuentro el SCB->VTOR", que es lo que dice tambien
        # cuando alguien lo borra. Un guardian que no distingue "ha
        # cambiado" de "ya no esta" obliga a leerle el codigo para saber
        # cual de las dos cosas pasa.
        #
        # Ahora coge el VTOR que sigue al #else -el de la aplicacion, que
        # el #ifdef CARGADOR_ARRANQUE separa del otro- sea cual sea su
        # valor, y lo compara aqui.
        ('CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c',
         r'#else[\s\S]{0,1200}?SCB->VTOR\s*=\s*0x([0-9A-Fa-f]{8})',
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

    #
    # Y LA VARIANTE DEL 470, QUE ESTE GUION DECIA VIGILAR Y NO VIGILABA -
    # 16/10/2026.
    #
    # *** User/cargador.h decia, con todas las letras: "Los tres sitios que
    # tienen que cuadrar los vigila tools/mapa.py". Y mapa.py no nombraba
    # GD32F470VG_FLASH.ld por ningun lado, ni CARGA_BORRA_HASTA. ***
    #
    # El 16/10 se subio el tope del 470 en cuatro sitios y se dejo sin subir
    # un quinto -la tabla k_sectores[] de cargador.c-, y esto, que era el
    # guardian nombrado, no podia verlo porque sus dos constantes eran fijas
    # y de un solo chip. Un vigilante que mira la mitad del mapa no avisa:
    # tranquiliza.
    #
    # Ahora los dos .ld, y el tope de cada variante contra lo que declara
    # su enlazador. (La tabla de sectores la ata tools/tope_check.py, que
    # es quien tiene delante la funcion que borra.)
    #
    cab_h = lee('User/cargador.h')
    topes = re.findall(r'#define\s+CARGA_BORRA_HASTA\s+0x([0-9A-Fa-f]+)', cab_h)
    if len(topes) != 2:
        sys.exit('mapa: esperaba dos CARGA_BORRA_HASTA en User/cargador.h '
                 '(uno por chip) y hay %d. Si el esquema ha cambiado, '
                 'arregla este guion.' % len(topes))
    tope_470, tope_450 = int(topes[0], 16), int(topes[1], 16)

    for ld, tope, nombre in (('GD32F450VE_FLASH.ld', tope_450, '450VET6'),
                             ('GD32F470VG_FLASH.ld', tope_470, '470VGT6')):
        txt = lee(ld)
        m = re.search(r'FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*0x([0-9A-Fa-f]+),'
                      r'\s*LENGTH\s*=\s*(\d+)', txt)
        if not m:
            sys.exit('mapa: no entiendo el MEMORY de FLASH en %s' % ld)
        ini, largo = int(m.group(1), 16), int(m.group(2))
        if ini != ESPERADO_APP:
            sys.exit('mapa: %s empieza la FLASH en 0x%08X y se espera 0x%08X.'
                     % (ld, ini, ESPERADO_APP))
        if ini + largo != tope:
            sys.exit('mapa: %s (%s) deja crecer la imagen hasta 0x%08X y el\n'
                     '      cargador borra hasta 0x%08X (CARGA_BORRA_HASTA).\n'
                     '      Si el enlazador va mas lejos que el borrado, se\n'
                     '      graba sobre flash sin borrar y sale basura.'
                     % (ld, nombre, ini + largo, tope))

    n = vtor_sueltos()

    print('mapa: cargador 0x%08X+%dkB, aplicacion 0x%08X..0x%08X (%d bytes). '
          'Los tres sitios cuadran, y los %d SCB->VTOR del arbol tambien.'
          % (ESPERADO_BOOT, BOOT_LEN // 1024, ESPERADO_APP, FIN_FLASH,
             FIN_FLASH - ESPERADO_APP, n))
    print('      y los dos enlazadores acaban donde borra su cargador: '
          '450 en 0x%08X, 470 en 0x%08X.' % (tope_450, tope_470))


if __name__ == '__main__':
    main()
