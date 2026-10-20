#!/usr/bin/env python3
"""
EL REPARTO DE LA RAM, LEIDO DEL MAPA Y NO DEL CODIGO FUENTE.

Y la respuesta es que no podia salir. Un repaso del codigo fuente mira
ficheros .c; la RAM no se decide en un .c, se decide en el ENLAZADO. El mapa
es el unico sitio del proyecto donde pone que acaba de verdad en los 192 kB, y
hasta hoy no lo leia nadie. Los quince kilobytes de gd32f4xx_enet.c llevaban
ahi desde el primer dia y salieron por accidente: porque el enlazador se nego
a enlazar al meter 28 bytes mas.

Esto lo convierte en algo que se ve todos los dias:

  - saca el reparto de .bss y .data ordenado de mayor a menor, con el fichero
    del que viene cada cosa;
  - agrupa por fichero objeto, que es como se ve de un vistazo que un modulo
    entero pesa mas de lo que deberia;
  - y avisa de los ficheros de Firmware/Source -la libreria del fabricante-
    que estan ocupando RAM, porque esos son los que nadie repasa nunca.

No falla la compilacion por si solo: no todo lo grande sobra -g_ft8_shared_ram
son 54 kB y son a proposito-. Avisa, que es lo que hace falta cuando lo que se
quiere es que un humano lo mire.
"""
import re
import sys
import os

#
# *** LA RAM TOTAL NO ES UNA CONSTANTE - 16/10/2026. ***
#
# Aqui ponia `RAM_TOTAL = 192 * 1024` a mano. Era cierto mientras solo hubo
# un chip. Con el GD32F470VGT6 el enlazado usa GD32F470VG_FLASH.ld, que
# declara 448 kB, y este informe seguia diciendo "de 196608": anunciaba un
# 99 % de ocupacion donde hay un 42, y -peor- habria dicho "no cabe" de
# algo que cabe de sobra.
#
# El dato correcto esta en el propio mapa: el enlazador escribe arriba una
# tabla "Memory Configuration" con las regiones DE VERDAD usadas en ese
# enlazado. Leerla de ahi es gratis y no se puede quedar desfasada.
#
RAM_TOTAL_POR_DEFECTO = 192 * 1024


def ram_total(mapa):
    """
    Los bytes de la region RAM, leidos de la tabla de regiones del mapa.

    Devuelve (bytes, de_donde). Si la tabla no esta o no se entiende, cae
    al valor de siempre Y LO DICE, que es la diferencia entre un dato y
    una suposicion.
    """
    try:
        txt = open(mapa, encoding='utf-8', errors='replace').read()
    except OSError:
        return RAM_TOTAL_POR_DEFECTO, 'por defecto'
    i = txt.find('Memory Configuration')
    if i < 0:
        return RAM_TOTAL_POR_DEFECTO, 'por defecto'
    for ln in txt[i:i + 4000].split('\n'):
        c = ln.split()
        #   RAM              0x20000000         0x00030000         xrw
        if len(c) >= 3 and c[0] == 'RAM':
            try:
                n = int(c[2], 0)
            except ValueError:
                break
            if n > 0:
                return n, 'del mapa'
            break
    return RAM_TOTAL_POR_DEFECTO, 'por defecto'


def regiones(mapa):
    """Todas las regiones del mapa, para el pie del informe."""
    try:
        txt = open(mapa, encoding='utf-8', errors='replace').read()
    except OSError:
        return []
    i = txt.find('Memory Configuration')
    if i < 0:
        return []
    fuera = []
    for ln in txt[i:i + 4000].split('\n'):
        c = ln.split()
        if len(c) >= 3 and c[0] not in ('Name', '*default*') and c[1].startswith('0x'):
            try:
                fuera.append((c[0], int(c[1], 0), int(c[2], 0)))
            except ValueError:
                pass
    return fuera


def secciones(mapa, cual):
    """Las entradas de la seccion de salida `cual` (.bss o .data)."""
    lineas = open(mapa, encoding='utf-8', errors='replace').read().split('\n')
    i = 0
    while i < len(lineas) and not lineas[i].startswith(cual + ' '):
        i += 1
    if i >= len(lineas):
        return []
    i += 1
    filas = []
    pend = None
    while i < len(lineas):
        l = lineas[i]
        if l and not l[0].isspace():
            break
        #
        # *** Y COMMON, QUE ES POR DONDE SE COLABAN 55 kB - 10/10/2026. ***
        #
        # El usuario compilo con GCC 9.2.1 y este inforse pidio 120.672 bytes
        # cuando `size -A` decia 176.492. Cincuenta y cinco mil ochocientos
        # veinte sin contar, o sea el buffer de la cascada entero.
        #
        # GCC 9 compila con -fcommon por defecto y GCC 10 en adelante con
        # -fno-common. Con -fcommon, una global sin inicializar NO va a
        # `.bss.nombre`: va a COMMON. Comprobado con nm sobre el mismo
        # fichero:
        #
        #     -fcommon     C  g_ft8_shared_ram  0xd4a0 = 54.432
        #     -fno-common  B  g_ft8_shared_ram  0xd4a0
        #
        # Y esto solo buscaba `.bss.`, asi que no las veia. Un informe que
        # te dice que te sobran 75 kB cuando te quedan 19 es peor que no
        # tener informe.
        #
        # El firmware lleva ya -fno-common puesto a mano (ver reglas.mk),
        # pero esto cuenta COMMON igual: el guion tiene que ser capaz de
        # leer un mapa que no haya hecho nuestro Makefile.
        #
        #
        # Y el RELLENO DE ALINEACION, que tambien ocupa RAM de verdad. Sin
        # el, el contraste contra `size -A` salia 944 bytes corto en mi
        # propia compilacion. Se apunta a parte y no a un fichero objeto,
        # porque no es de nadie: es el hueco entre dos.
        #
        m = re.match(r'^ \*fill\*\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)', l)
        if m:
            filas.append((int(m.group(2), 16), '(relleno de alineacion)',
                          '-'))
            i += 1
            continue
        if cual == '.bss':
            m = re.match(r'^ COMMON\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)$', l)
            if m:
                filas.append((int(m.group(2), 16), 'COMMON', m.group(3)))
                i += 1
                continue
            if re.match(r'^ COMMON\s*$', l):
                pend = 'COMMON'
                i += 1
                continue
        # "' .bss.algo'" solo, con la direccion en la linea siguiente
        m = re.match(r'^ (' + re.escape(cual) + r'\.\S+)\s*$', l)
        if m:
            pend = m.group(1)
            i += 1
            continue
        if pend:
            m = re.match(r'^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)$', l)
            if m:
                filas.append((int(m.group(2), 16), pend, m.group(3)))
            pend = None
            i += 1
            continue
        #
        # Y las entradas que se llaman EXACTAMENTE como la seccion -` .data`
        # a secas, sin punto detras-, que es como las deja la libreria de C.
        # Eran los 440 bytes que faltaban despues de meter el relleno.
        #
        m = re.match(r'^ (' + re.escape(cual) + r')\s+(0x[0-9a-f]+)\s+'
                     r'(0x[0-9a-f]+)\s+(\S+)$', l)
        if m:
            filas.append((int(m.group(3), 16), cual, m.group(4)))
            i += 1
            continue
        # todo en una linea
        m = re.match(r'^ (' + re.escape(cual) + r'\.\S+)\s+(0x[0-9a-f]+)\s+'
                     r'(0x[0-9a-f]+)\s+(\S+)$', l)
        if m:
            filas.append((int(m.group(3), 16), m.group(1), m.group(4)))
        i += 1
    return filas



def contrasta(mapa, total):
    """
    *** QUE ESTE INFORME NO PUEDA MENTIR EN SILENCIO - 10/10/2026. ***

    El reparto de arriba sale de leer el `.map` a mano, y leer un `.map` a
    mano es adivinar el formato de una herramienta que no promete ninguno.
    Ya fallo una vez -COMMON, 55 kB- y fallo CALLANDO, que es lo caro.

    Asi que se contrasta contra `size -A` sobre el .elf, que es la cuenta
    que da el propio binutils y no una interpretacion nuestra. Si no
    cuadran, lo dice con las dos cifras delante. No tumba la compilacion:
    el binario puede estar perfectamente bien y ser el guion el que no sabe
    leer; lo que no puede pasar es que nadie se entere.
    """
    import subprocess

    elf = mapa[:-4] + '.elf' if mapa.endswith('.map') else None
    if not elf or not os.path.exists(elf):
        return None
    for exe in ('arm-none-eabi-size', 'size'):
        try:
            sal = subprocess.run([exe, '-A', elf], capture_output=True,
                                 text=True, timeout=30)
        except (OSError, subprocess.SubprocessError):
            continue
        if sal.returncode != 0:
            continue
        real = 0
        for ln in sal.stdout.split('\n'):
            c = ln.split()
            if len(c) >= 2 and c[0] in ('.bss', '.data'):
                try:
                    real += int(c[1])
                except ValueError:
                    pass
        if real == 0:
            return None
        if abs(real - total) <= 64:
            return None
        return ['  *** OJO: `%s -A` dice %d bytes en .bss + .data y aqui'
                % (exe, real),
                '      salen %d. Falta%s %d por contar: el reparto de abajo'
                % (total, 'n' if abs(real - total) != 1 else '',
                   abs(real - total)),
                '      esta INCOMPLETO y el porcentaje de arriba no vale.',
                '      Manda el `.map`: el guion no sabe leer algo que hay',
                '      dentro. Paso el 10/10/2026 con COMMON y 55 kB.']
    return None


def main():
    mapa = sys.argv[1] if len(sys.argv) > 1 else 'build/firmware.map'
    if not os.path.exists(mapa):
        print('ram_mapa: no hay mapa en %s (compila primero)' % mapa)
        return 0

    #
    # *** Y .noinit, QUE ME COSTO UNA TARDE - 15/10/2026. ***
    #
    # Este informe decia "libres 36" y el enlazador, al meter 24 bytes,
    # contesto "overflowed by 8". No se contradecian: .noinit tambien ocupa
    # RAM -el testigo del DFU y el historial de reinicios, 20 bytes- y aqui
    # no se sumaba. O sea que el numero que uno mira para decidir si cabe
    # una funcion nueva estaba SOBRANDO veinte bytes, que es justo el error
    # que hace que te lo diga el enlazador y no el informe.
    #
    # Un informe de RAM que no cuenta toda la RAM no es un informe optimista:
    # es uno que miente en la direccion mala.
    #
    filas = (secciones(mapa, '.bss') + secciones(mapa, '.data')
             + secciones(mapa, '.noinit'))
    if not filas:
        print('ram_mapa: el mapa no tiene secciones .bss/.data reconocibles')
        return 0

    total = sum(f[0] for f in filas)
    filas.sort(reverse=True)
    aviso = contrasta(mapa, total)

    tope, de_donde = ram_total(mapa)

    print('')
    print('EL REPARTO DE LA RAM (de %s, no del codigo)' % os.path.basename(mapa))
    print('')
    print('  %d bytes en .bss + .data + .noinit, de %d (%.1f%%); libres %d' %
          (total, tope, 100.0 * total / tope, tope - total))
    print('  (la pila son otros %d bytes, reservados aparte en el .ld)' % 0x1600)
    if de_donde == 'por defecto':
        print('  *** OJO: el mapa no trae tabla de regiones; los %d son el valor' % tope)
        print('      de siempre, NO lo que dice el .ld de este enlazado.')
    else:
        otras = [r for r in regiones(mapa) if r[0] != 'RAM']
        if otras:
            print('  (%s)' % ', '.join(
                '%s %d k en 0x%08X' % (n, l // 1024, o) for n, o, l in otras))
    if aviso:
        print('')
        for ln in aviso:
            print(ln)
    print('')

    print('  los quince mayores')
    for sz, nom, obj in filas[:15]:
        print('    %8d  %-44s %s' % (sz, nom[:44], os.path.basename(obj)))

    porobj = {}
    for sz, _nom, obj in filas:
        porobj[os.path.basename(obj)] = porobj.get(os.path.basename(obj), 0) + sz
    print('')
    print('  por fichero objeto, los diez mayores')
    for obj, sz in sorted(porobj.items(), key=lambda x: -x[1])[:10]:
        print('    %8d  %s' % (sz, obj))

    # Lo que de verdad hay que mirar: la libreria del fabricante ocupando RAM.
    # Esos ficheros no los repasa nadie y entran en la compilacion por el
    # comodin del Makefile, no porque alguien los haya pedido.
    vendor = sorted(
        [(sz, nom, os.path.basename(obj)) for sz, nom, obj in filas
         if re.match(r'^gd32f4xx_', os.path.basename(obj))],
        reverse=True)
    print('')
    if vendor:
        tot_v = sum(v[0] for v in vendor)
        print('  *** AVISO: %d bytes de RAM vienen de la libreria del fabricante' % tot_v)
        print('      (Firmware/Source/*.c). Entran por el comodin del Makefile, no')
        print('      porque alguien los haya pedido. Mira si el periferico se usa:')
        for sz, nom, obj in vendor[:10]:
            print('        %8d  %-40s %s' % (sz, nom[:40], obj))
        print('      Para sacar uno: añadelo a FW_EXCLUIR en el Makefile. Si hacia')
        print('      falta, el enlazador lo dira a gritos y no en silencio.')
    else:
        print('  la libreria del fabricante no ocupa RAM: correcto.')
    print('')
    return 0


if __name__ == '__main__':
    sys.exit(main())
