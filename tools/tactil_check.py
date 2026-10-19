#!/usr/bin/env python3
"""Que el tactil se mire MAS DE UNA VEZ por vuelta del bucle principal.

  python3 tactil_check.py [User/main.c]

POR QUE EXISTE - 19/10/2026
---------------------------
Avisado por el usuario: en STANAG y en IDENT la radio responde al dedo mas
despacio que en los demas modos.

Y lo que falla no es el tactil, es cada cuanto se mira. Un toque solo sale
si, mientras el dedo esta puesto, el bucle pasa por demo_touch_poll() TRES
veces: dos para que touch_read() confirme el contacto -la firmeza, que
pidio el propio usuario para que un roce no contara- y una mas, ya sin
dedo, para ver la suelta.

sim/tactil_arrastre.c lo mide con el touch.c de verdad:

    mirado cada  20 ms  ->  salen todos los toques
    mirado cada  65 ms  ->  se pierden 3 de cada 20 toques de 120 ms
                            y 15 de cada 20 de los de 80 ms

La vuelta tarda lo que tarda; lo que se arregla es mirar el tactil dos
veces en cada una, con lo que el periodo baja a la mitad. Eso es UNA LINEA
en main.c, a mil lineas de la otra, y nada la sujeta: quien limpie el
bucle un dia la quita por duplicada y la radio vuelve a comerse toques sin
que ningun banco se entere, porque ningun banco compila main.c.

Esto es lo que la sujeta.

LO QUE NO VALE como arreglo, y por eso tambien se dice aqui: aflojar la
firmeza. Confirmar con dos lecturas SEPARADAS EN EL TIEMPO es lo que
distingue un toque de un roce; dos lecturas seguidas dentro de la misma
llamada confirmarian igual de rapido y volverian a dar por bueno
cualquier roce.
"""
import re
import sys


def sin_comentarios(txt):
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
        else:
            out.append(txt[i])
            i += 1
    return ''.join(out)


def main(ruta):
    try:
        with open(ruta, encoding='utf-8') as f:
            src = f.read()
    except OSError:
        print('  no encuentro %s  MAL' % ruta)
        return 1

    limpio = sin_comentarios(src)

    m = re.search(r'\n    while \(1\) \{', limpio)
    if not m:
        print('  no encuentro el bucle principal en %s  MAL' % ruta)
        return 1
    bucle = limpio[m.end():]

    miradas = [k for k in re.finditer(r'\bdemo_touch_poll\s*\(\s*\)\s*;', bucle)]
    n = len(miradas)
    print('  el tactil se mira %d %s por vuelta'
          % (n, 'vez' if n == 1 else 'veces'))

    if n >= 2:
        print('  mas de una vez por vuelta  ok')
        return 0

    print('')
    print('  EL TACTIL SE MIRA UNA SOLA VEZ POR VUELTA.')
    print('')
    print('  Un toque necesita TRES miradas dentro de lo que dura el dedo')
    print('  puesto: dos para confirmarlo y una para ver la suelta. Con una')
    print('  sola mirada por vuelta, en los modos con trabajo pesado')
    print('  -STANAG, IDENT- la vuelta se alarga y la radio se come toques.')
    print('  Desde fuera parece que no responde; el encoder, mientras tanto,')
    print('  sigue funcionando, que es lo que despista.')
    print('')
    print('  Se arregla volviendo a llamar a demo_touch_poll() detras de la')
    print('  cadena de polls. NO se arregla aflojando la firmeza del tactil.')
    print('')
    print('  Lo mide sim/tactil_arrastre.c, apartado (5).')
    print('')
    print('  *** 1 ***')
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'User/main.c'))
