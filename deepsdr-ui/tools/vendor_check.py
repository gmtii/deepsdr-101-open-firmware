#!/usr/bin/env python3
"""
QUE 'make comprueba' SIGA SALIENDO EN VERDE EN UN CLON.

POR QUE EXISTE
--------------
vendor/ son gfx.c y spectrum.c del firmware ORIGINAL de la radio. No son
nuestros, no se publican, y .gitignore los deja fuera a proposito. Los
bancos que comparan el dibujado nuevo contra el viejo los necesitan, asi
que local.mk los descuelga de `comprueba` cuando la carpeta no esta:

    ifneq ($(wildcard vendor/gfx.c),)
    comprueba: espectroviejo
    endif

Eso funciona mientras alguien se acuerde de escribir las tres lineas. El
22/10/2026 se clono el repositorio y se lanzo la tanda -o sea lo que hace
quien llega nuevo, y lo que aqui no se hacia nunca porque vendor/ siempre
esta- y la tanda MURIO: de las catorce lineas que cuelgan un banco de
vendor/ de `comprueba`, trece estaban guardadas y UNA no.

Y la de `espectrotema` llevaba ahi desde que se escribio.

LO QUE MIDE
-----------
Para cada `comprueba: X` de local.mk, recorre las dependencias de X dentro
del propio fichero. Si en ese recorrido aparece algo de vendor/, esa linea
TIENE que estar dentro de un `ifneq ($(wildcard vendor/gfx.c),)`. Si no lo
esta, lo dice con su numero de linea.

Y AL REVES TAMBIEN, que es la mitad que se olvida: una linea guardada cuyo
banco ya NO usa vendor/ esta escondiendo un banco que podria correr en un
clon. Tambien se avisa, aunque eso no rompe nada: es trabajo perdido, no
una tanda muerta.

POR QUE SE LEE EL FICHERO Y NO SE PREGUNTA A make
-------------------------------------------------
Porque `make -n comprueba` con vendor/ PUESTO no distingue: los catorce
bancos salen igual, guardados o no. Lo que se quiere saber es que pasaria
SIN la carpeta, y para eso o se borra -que no se va a hacer en mitad de una
tanda- o se lee el grafo. Se lee el grafo.
"""
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MK   = os.path.join(RAIZ, 'local.mk')

GUARDA = 'ifneq ($(wildcard vendor/gfx.c),)'


def lee_lineas(ruta):
    """Las lineas del makefile con las continuaciones ya unidas.

    Devuelve (texto, numero de la PRIMERA linea fisica), que es el que hay
    que enseñar: el que abre la regla.
    """
    fuera = []
    with open(ruta, encoding='utf-8', errors='replace') as f:
        crudas = f.read().split('\n')
    i = 0
    while i < len(crudas):
        n0 = i + 1
        l = crudas[i]
        while l.endswith('\\') and i + 1 < len(crudas):
            i += 1
            l = l[:-1] + ' ' + crudas[i].strip()
        fuera.append((l, n0))
        i += 1
    return fuera


def main():
    if not os.path.isfile(MK):
        print('  no encuentro local.mk   MAL')
        return 1

    lineas = lee_lineas(MK)

    # 1. El mapa objetivo -> dependencias, y donde esta cada "comprueba: X".
    deps = {}
    cuelgan = []     # (objetivo, numero de linea, guardado si/no)
    hondo = 0        # dentro de cuantos ifneq de vendor estamos

    for texto, num in lineas:
        pelado = texto.strip()

        if pelado.startswith(GUARDA):
            hondo += 1
            continue
        if pelado.startswith('endif') and hondo > 0:
            hondo -= 1
            continue
        # Cualquier otro if/endif se cuenta aparte para no desparejar.
        if re.match(r'^(ifeq|ifneq|ifdef|ifndef)\b', pelado) and hondo > 0:
            hondo += 1
            continue

        if texto.startswith('\t') or pelado.startswith('#') or not pelado:
            continue

        m = re.match(r'^([^:=]+?)\s*:(?!=)\s*(.*)$', pelado)
        if not m:
            continue
        objetivos = m.group(1).split()
        resto = m.group(2)
        # Lo que venga detras de ';' es receta, no dependencias.
        resto = resto.split(';')[0]
        pre = resto.split()

        for o in objetivos:
            if o == 'comprueba':
                for d in pre:
                    cuelgan.append((d, num, hondo > 0))
            else:
                deps.setdefault(o, [])
                deps[o].extend(pre)

    if not cuelgan:
        print('  no he encontrado ningun "comprueba: X" en local.mk   MAL')
        print('  (esto no es que este bien: es que no se donde mirar)')
        return 1

    # 2. ¿Llega alguno a vendor/?
    def usa_vendor(obj, vistos):
        if obj in vistos:
            return False
        vistos.add(obj)
        for d in deps.get(obj, []):
            if d.startswith('vendor/'):
                return True
            if usa_vendor(d, vistos):
                return True
        return False

    malos, sobran = [], []
    con_vendor = 0
    for obj, num, guardado in cuelgan:
        v = usa_vendor(obj, set())
        if v:
            con_vendor += 1
            if not guardado:
                malos.append((obj, num))
        elif guardado:
            sobran.append((obj, num))

    print('  %d objetivos cuelgan de comprueba; %d usan vendor/' %
          (len(cuelgan), con_vendor))

    if malos:
        print('')
        for obj, num in malos:
            print('  local.mk:%d  "comprueba: %s" usa vendor/ y NO esta guardado'
                  % (num, obj))
        print('')
        print('  O sea que "make comprueba" muere en un clon, que es donde')
        print('  vendor/ no esta. Metelo entre:')
        print('')
        print('      %s' % GUARDA)
        print('      comprueba: %s' % malos[0][0])
        print('      endif')
        print('')
        return 1

    for obj, num in sobran:
        print('  local.mk:%d  "comprueba: %s" esta guardado y ya no usa vendor/'
              % (num, obj))
        print('    (no rompe nada; es un banco que podria correr en un clon)')

    print('  los %d guardados, guardados   ok' % con_vendor)
    print('')
    print('VENDOR: todo correcto')
    return 0


if __name__ == '__main__':
    sys.exit(main())
