#!/usr/bin/env python3
"""
QUE NADIE BAJE UNA BANDERA DE REPINTADO SIN MIRAR SI SE HA PINTADO.
13/10/2026.

*** Por el fallo conocido: "el boton Borrar del panel de CW desaparece
despues de mucho rato decodificando". ***

gfx2_render() puede darse la vuelta SIN DIBUJAR: si la banda compositora
esta cogida -la captura de pantalla se la queda veinticinco segundos- no
hay donde componer y vuelve con un cero. Para un repintado que se repite
cada fotograma eso da igual. Para uno de UNA SOLA VEZ, el que se hace
porque hay una bandera de "esto hay que repintarlo entero", no: la bandera
se baja, el dibujo no se hizo, y lo que quedo en pantalla es lo que hubiera
debajo. Que en el panel de texto es la cabecera borrada, con su boton.

Esto NO se puede comprobar con un banco del simulador, y por el motivo de
siempre: la logica de la bandera vive en main.c, que el simulador no
compila. Asi que se comprueba leyendo el fuente, igual que
tools/via_audio_check.py y tools/tapado_check.py.

LA REGLA: toda linea que ponga a cero una bandera cuyo nombre acabe en
`_full_redraw` o `_repinta` tiene que estar precedida -dentro de las ocho
lineas de arriba- por una llamada a un dibujo cuyo valor SE MIRE, o sea
por un `if (!...draw...(` o un `if (...draw...(`.

Si algun dia hace falta una excepcion de verdad, se pone el comentario
`/* repinta: sin mirar */` en la misma linea y se explica por que.
"""
import re
import sys

BANDERA = re.compile(r'\b(\w*(?:_full_redraw|_repinta))\s*=\s*0U?\s*;')
MIRA    = re.compile(r'if\s*\(\s*!?\s*\w*draw\w*\s*\(')
PERDON  = '/* repinta: sin mirar */'

def sin_comentarios(texto):
    """Quita /* */ y // dejando los saltos de linea, para que los numeros de
    linea del informe sigan siendo los del fichero. Un comentario que cita el
    codigo -y aqui los hay, porque este proyecto explica lo que cambia- no es
    codigo."""
    salida = []
    i, n = 0, len(texto)
    while i < n:
        if texto.startswith('/*', i):
            j = texto.find('*/', i + 2)
            j = n if j < 0 else j + 2
            salida.append(''.join(c for c in texto[i:j] if c == '\n'))
            i = j
        elif texto.startswith('//', i):
            j = texto.find('\n', i)
            j = n if j < 0 else j
            i = j
        elif texto[i] in '"\'':
            q = texto[i]
            j = i + 1
            while j < n and texto[j] != q:
                j += 2 if texto[j] == '\\' else 1
            salida.append(texto[i:min(j + 1, n)])
            i = min(j + 1, n)
        else:
            salida.append(texto[i])
            i += 1
    return ''.join(salida)


def main(rutas):
    fallos = 0
    total  = 0
    for ruta in rutas:
        try:
            crudo = open(ruta, encoding='utf-8', errors='replace').read()
            lineas = sin_comentarios(crudo).split('\n')
        except OSError as e:
            print('  no puedo abrir %s: %s' % (ruta, e))
            return 1
        for n, linea in enumerate(lineas):
            m = BANDERA.search(linea)
            if not m:
                continue
            total += 1
            if PERDON in crudo.split('\n')[n]:
                print('  %s:%d  %s  perdonado a mano' % (ruta, n + 1, m.group(1)))
                continue
            antes = '\n'.join(lineas[max(0, n - 8):n])
            if MIRA.search(antes):
                print('  %s:%d  %s  se mira si se ha pintado  ok'
                      % (ruta, n + 1, m.group(1)))
            else:
                print('  %s:%d  %s  *** MAL: se baja sin mirar si gfx2_render '
                      'ha pintado ***' % (ruta, n + 1, m.group(1)))
                fallos += 1
    if total == 0:
        print('  *** MAL: no he encontrado ninguna bandera de repintado.')
        print('      O han cambiado de nombre, o esta comprobacion ya no mide nada.')
        return 1
    print('  %d bandera(s) de repintado, %d sin comprobar' % (total, fallos))
    return 1 if fallos else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:] or ['User/main.c']))
