#!/usr/bin/env python3
"""Que digi_panel_active() sigue leyendo la tabla y no una lista a mano.

POR QUE EXISTE (28/09/2026)
--------------------------
digi_panel_active() contesta a "¿el espectro ha dejado su sitio al panel
digital?", y durante meses lo hizo con una lista escrita a mano:

    if (m == DEMOD_MODE_NFM) { return ax25_activo(); }
    return (m == USB || m == LSB) &&
           (rtty || cw || navtex || wefax || sstv || ft8 || hfdl);

La lista se quedo en HFDL. Despues entraron WSPR, AIS y ALE, y NINGUNO de
los tres se anadio aqui. Tres veces el mismo olvido, porque esta funcion
esta a dos mil lineas de donde se anade un modo y nada la senala.

El sintoma era el peor posible: el modo se selecciona, el demodulador
arranca, el decodificador corre y hasta decodifica - pero la pantalla no
cambia. El dueno lo dijo asi: "le doy a wspr y la ventana no cambia". Nada
falla, nada avisa.

Ahora la funcion lee k_demod_modes[] y devuelve si la entrada activa es de
la familia digital, con lo que un modo nuevo se lleva su panel solo. Esto
comprueba que sigue siendo asi: si alguien vuelve a escribir la lista a
mano - por optimizar, por "es mas claro", o copiando una version vieja - la
compilacion falla aqui en vez de fallar la radio en silencio.

No es un analizador de C: es un vigilante de una linea concreta. Con eso
basta para lo que tiene que impedir.
"""

import re
import sys


def main(ruta):
    with open(ruta, encoding='utf-8') as f:
        src = f.read()

    m = re.search(r'static uint8_t digi_panel_active\(void\)\s*\{', src)
    if not m:
        print('  no encuentro digi_panel_active() en %s  MAL' % ruta)
        return 1

    # El cuerpo, contando llaves desde la de apertura.
    i = m.end() - 1
    prof = 0
    for j in range(i, len(src)):
        if src[j] == '{':
            prof += 1
        elif src[j] == '}':
            prof -= 1
            if prof == 0:
                cuerpo = src[i + 1:j]
                break
    else:
        print('  digi_panel_active() no cierra  MAL')
        return 1

    # Fuera los comentarios: ahi SI se nombran los modos, y a proposito.
    limpio = re.sub(r'/\*.*?\*/', '', cuerpo, flags=re.S)
    limpio = re.sub(r'//[^\n]*', '', limpio)

    fallos = []

    if 'k_demod_modes' not in limpio:
        fallos.append('ya no lee k_demod_modes[]: ha vuelto a ser una lista '
                      'a mano')
    if 'MODO_FAM_DIG' not in limpio:
        fallos.append('ya no decide por familia (MODO_FAM_DIG)')

    # Cualquier pregunta directa a un decodificador es la lista volviendo.
    sueltos = sorted(set(re.findall(
        r'\b((?:\w+_)?(?:activo|get_enabled)\(\))', limpio)))
    if sueltos:
        fallos.append('pregunta a decodificadores uno a uno: ' +
                      ', '.join(sueltos))

    if fallos:
        sys.stderr.write('\nPANEL DIGITAL: %d problema(s) en '
                         'digi_panel_active().\n\n' % len(fallos))
        for f in fallos:
            sys.stderr.write('  - %s\n' % f)
        sys.stderr.write('\n  Tiene que salir de la tabla. Ver su propio\n'
                         '  comentario en main.c: una lista a mano ahi se\n'
                         '  quedo tres modos atras y nadie se entero.\n\n')
        return 1

    print('Panel digital: sale de la tabla, no de una lista a mano.')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'User/main.c'))
