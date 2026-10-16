#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ATA LA TABLA DE LA RADIO A LAS MEDIDAS DEL BANCO.

Hay dos listas que tienen que decir lo mismo y viven en ficheros
distintos:

  User/stanag_modo.c   k_conocidas[]  periodo -> nombre, lo que ensena la
                                      pantalla cuando el detector engancha
  sim/stanag_aire.c    k_espera[]     periodo que da CADA grabacion de
                                      verdad, medido con el detector

Si manana mido otra grabacion y meto su fila en el banco pero se me olvida
la de la tabla, la radio medira bien y dira "sin identificar". Eso no lo
ve ningun banco de los que hay: el del modo usa senales que me fabrico yo,
y el de aire no sabe nada de la tabla. Por eso esto.

Y de paso comprueba dos cosas que el comentario de la tabla AFIRMA, que es
donde mas facil es mentirse:

  - que esta ordenada por periodo (el bucle se queda con la primera que
    encaje, asi que el orden decide los empates)
  - que ninguna pareja tiene las ventanas del 1 % solapadas, o sea que
    ninguna medida puede caer legitimamente en dos filas a la vez
"""
import re, sys, os

def lee(ruta):
    with open(ruta, encoding='utf-8') as f:
        return f.read()

def saca_bloque(txt, nombre):
    i = txt.index(nombre + '[] = {')
    j = txt.index('};', i)
    return txt[i:j]

def main():
    fw  = sys.argv[1] if len(sys.argv) > 1 else '../deepsdr'
    aqui = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    modo = lee(os.path.join(fw, 'User', 'stanag_modo.c'))
    aire = lee(os.path.join(aqui, 'sim', 'stanag_aire.c'))

    fallos = []

    # --- k_conocidas: { 10667U, STANAG_4285, "...", "..." }
    b = saca_bloque(modo, 'k_conocidas')
    con = []
    for m in re.finditer(r'\{\s*(\d+)U\s*,\s*([A-Za-z0-9_]+)\s*,\s*"([^"]*)"', b):
        con.append((int(m.group(1)), m.group(2), m.group(3)))
    if len(con) < 5:
        fallos.append('no encuentro k_conocidas en User/stanag_modo.c')
        print('\n'.join(fallos)); return 1
    print('  k_conocidas: %d filas' % len(con))

    # ordenada
    for a, s in zip(con, con[1:]):
        if s[0] <= a[0]:
            fallos.append('k_conocidas no esta ordenada: %u (%s) va detras de %u (%s)'
                          % (s[0], s[2], a[0], a[2]))

    # ventanas del 1 % sin solaparse
    for i in range(len(con)):
        for j in range(i + 1, len(con)):
            # ordenadas aparte, para que este fallo no dependa del de
            # arriba: desordenar la tabla no debe disparar tambien este
            (pa, na), (pb, nb) = sorted(((con[i][0], con[i][2]),
                                         (con[j][0], con[j][2])))
            ta = max(1, pa // 100)
            tb = max(1, pb // 100)
            if pa + ta >= pb - tb:
                fallos.append('se solapan al 1 %%: %u (%s) y %u (%s)'
                              % (pa, na, pb, nb))

    # --- k_espera: { "4285", 10667U, 80U, 0U }
    b = saca_bloque(aire, 'k_espera')
    esp = []
    for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*(\d+)U\s*,\s*(\d+)U\s*,\s*(\d+)U\s*\}', b):
        esp.append((m.group(1), int(m.group(2)), int(m.group(4))))
    if len(esp) < 5:
        fallos.append('no encuentro k_espera en sim/stanag_aire.c')
        print('\n'.join(fallos)); return 1
    print('  k_espera:    %d grabaciones' % len(esp))

    # cada grabacion con periodo fijo tiene que tener nombre en la radio
    for clave, periodo, rejilla in esp:
        if periodo == 0:
            # sin periodo fijo: entonces la rejilla es la que tiene que
            # estar en la tabla, que es lo unico que la radio puede nombrar
            objetivo, que = rejilla, 'la rejilla del %s' % clave
        else:
            objetivo, que = periodo, 'el periodo del %s' % clave
        enc = None
        for p, forma, nombre in con:
            t = max(1, p // 100)
            if objetivo >= p - t and objetivo <= p + t:
                enc = (p, nombre); break
        if enc is None:
            fallos.append('%s (%u,%02u ms) no lo nombra k_conocidas: la radio diria '
                          '"sin identificar" con una grabacion que el banco mide bien'
                          % (que, objetivo // 100, objetivo % 100))
        else:
            print('    %-10s %6.2f ms  ->  %s' % (clave, objetivo / 100.0, enc[1]))

    if fallos:
        print('\n  MAL:')
        for f in fallos: print('    - ' + f)
        return 1
    print('\n  la tabla de la radio nombra las %d grabaciones del banco  ok' % len(esp))
    return 0

sys.exit(main())
