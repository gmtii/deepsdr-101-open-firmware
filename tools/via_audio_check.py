#!/usr/bin/env python3
"""
QUE LOS DOS CAMINOS DE AUDIO NO SE MIREN.

  python3 via_audio_check.py <User/demod_am.c>

POR QUE EXISTE - 10/10/2026.

demod_am.c tiene dos caminos que no pueden correr a la vez: el de WFM, a 192
kHz, y el estrecho -AM, SSB, NFM- a 96 kHz. Desde hoy COMPARTEN LOS MISMOS
BYTES (la union s_via), y eso ahorra 4.096 de los 196.608 que tiene la radio.

Compartirlos solo es correcto mientras se cumpla UNA cosa:

    cada juego de buffers se toca SOLO dentro de su propia funcion de
    bloque, y de ningun otro sitio.

Hoy se cumple -comprobado a mano antes de hacer la union- pero es justo la
clase de invariante que se rompe sin querer: basta con que alguien añada un
getter de diagnostico que lea s_env[] desde el bucle principal, o que mueva
un trozo de proceso a una funcion auxiliar. Y cuando se rompa, lo que se oira
es un ruido raro en un cambio de modo, una vez de cada muchas. No hay aviso
del compilador que lo cace.

Asi que se comprueba aqui, en cada `make comprueba`, leyendo el fichero.

LO QUE NO ES: esto NO sustituye a un banco. demod_am.c no lo compila ningun
banco -tiene 37 cabeceras propias- y eso sigue siendo la deuda de este
cambio. Esto vigila el invariante del que depende la union, que es otra cosa
y mas pequeña.
"""
import re
import sys

# buffer -> la UNICA funcion que puede tocarlo
DUENO = {
    's_wfm_i_buf':     'demod_wfm_process_raw',
    's_wfm_q_buf':     'demod_wfm_process_raw',
    's_wfm_env':       'demod_wfm_process_raw',
    's_wfm_audio_out': 'demod_wfm_process_raw',
    's_i_buf':         'demod_am_process_raw',
    's_q_buf':         'demod_am_process_raw',
    's_env':           'demod_am_process_raw',
    's_audio_out':     'demod_am_process_raw',
}


def sin_comentarios(txt):
    """Quita comentarios y cadenas: un nombre citado en un comentario no es
    un uso, y este fichero esta lleno de comentarios que los nombran."""
    out = []
    i, n = 0, len(txt)
    while i < n:
        if txt.startswith('/*', i):
            j = txt.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', txt[i:j]))
            i = j
        elif txt.startswith('//', i):
            j = txt.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif txt[i] == '"':
            j = i + 1
            while j < n and txt[j] != '"':
                j += 2 if txt[j] == '\\' else 1
            j = min(j + 1, n)
            out.append(' ' * (j - i))
            i = j
        else:
            out.append(txt[i])
            i += 1
    return ''.join(out)


def funciones(txt):
    """[(nombre, inicio, fin)] de las funciones de primer nivel."""
    out = []
    for m in re.finditer(r'^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\([^;{]*\)\s*\{',
                         txt, re.M):
        ini = m.start()
        # la llave de cierre a nivel 0
        d, i = 0, txt.index('{', m.start())
        while i < len(txt):
            if txt[i] == '{':
                d += 1
            elif txt[i] == '}':
                d -= 1
                if d == 0:
                    break
            i += 1
        out.append((m.group(1), ini, i))
    return out


def main():
    ruta = sys.argv[1] if len(sys.argv) > 1 else 'User/demod_am.c'
    crudo = open(ruta, encoding='utf-8', errors='replace').read()
    txt = sin_comentarios(crudo)
    fns = funciones(txt)

    print('LOS DOS CAMINOS DE AUDIO (%s)' % ruta)
    malos = []
    cuenta = {k: 0 for k in DUENO}

    for nom, pat in sorted(DUENO.items()):
        for m in re.finditer(r'\b%s\b' % re.escape(nom), txt):
            # la definicion (el #define) no cuenta
            linea = crudo[:m.start()].count('\n') + 1
            lin_txt = crudo.splitlines()[linea - 1] if linea <= len(crudo.splitlines()) else ''
            if lin_txt.lstrip().startswith('#define'):
                continue
            dentro = None
            for f, a, b in fns:
                if a <= m.start() <= b:
                    dentro = f
                    break
            if dentro is None:
                continue          # declaracion a nivel de fichero
            cuenta[nom] += 1
            if dentro != pat:
                malos.append((nom, dentro, linea))

    for nom in sorted(DUENO):
        print('  %-16s %3d usos en %s' % (nom, cuenta[nom], DUENO[nom]))

    if malos:
        print('\nMAL: hay buffers tocados fuera de su camino.')
        print('     La union s_via de demod_am.c deja de ser correcta: los dos')
        print('     caminos comparten bytes y esto significa que uno puede leer')
        print('     lo que el otro escribio.')
        for nom, dentro, linea in malos:
            print('       %s en %s()  (linea %d), deberia ser solo %s()'
                  % (nom, dentro, linea, DUENO[nom]))
        return 1

    sin_uso = [n for n, c in cuenta.items() if c == 0]
    if sin_uso:
        print('\nMAL: estos no se usan en ningun sitio: %s' % ', '.join(sin_uso))
        print('     O sobran, o se les ha cambiado el nombre y este guion se ha')
        print('     quedado mirando a un fantasma, que es peor.')
        return 1

    print('\n  cada camino toca solo lo suyo  ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
