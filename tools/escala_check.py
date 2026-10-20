#!/usr/bin/env python3
"""
QUIEN PUEDE TOCAR TU ESCALA DEL ESPECTRO.

Eso funciona porque por debajo de s_db_min la cascada pinta el indice 0 del
mapa de color, que es el color del panel. Y funcionaba mal porque
s_db_min/s_db_max tenian DOS dueños: la escala que pone el usuario -que se
guarda en CONFIG.CSV- y el estado de trabajo de la autoescala, que las
machaca cada cuadro. Encender la autoescala le borraba sus numeros sin
copia, y guardar ajustes le escribia encima la ventana transitoria.

Desde el 10/10/2026 son dos pares:

  s_db_min     / s_db_max       lo que se PINTA. La autoescala escribe aqui.
  s_db_min_usr / s_db_max_usr   lo que es TUYO. Solo el mando escribe aqui,
                                y es lo que va a CONFIG.CSV.

Esto es lo que no se puede comprobar compilando, porque las cuatro reglas
son de QUIEN escribe DONDE, y escribir en la variable equivocada compila
igual de bien. Es la misma clase de banco que persist_check.py.
"""
import re
import sys

LIVE = ('s_db_min', 's_db_max')
USR  = ('s_db_min_usr', 's_db_max_usr')


def trozo(txt, desde, n=2600):
    i = txt.find(desde)
    return '' if i < 0 else txt[i:i + n]


def main(base):
    m = open(base + '/User/main.c', encoding='utf-8').read()
    # sin comentarios: aqui tambien se habla de las variables en prosa
    cod = re.sub(r'/\*.*?\*/', '', m, flags=re.S)
    cod = re.sub(r'//[^\n]*', '', cod)
    mal = 0

    # 1. la autoescala escribe SOLO en el par vivo
    for mm in re.finditer(r'spec_agc_step\s*\(([^;]*)\)', cod):
        arg = mm.group(1)
        if any(u in arg for u in USR):
            print("  MAL: spec_agc_step() recibe el par del usuario: %s" % arg.strip())
            print("       La autoescala solo puede mover lo que se pinta.")
            mal += 1
        elif not all(('&' + v) in arg.replace(' ', '') for v in LIVE):
            print("  MAL: spec_agc_step() no recibe &s_db_min y &s_db_max: %s" % arg.strip())
            mal += 1
    else:
        if 'spec_agc_step' not in cod:
            print("  MAL: no encuentro ninguna llamada a spec_agc_step()")
            mal += 1
    if not mal:
        print("  ok: la autoescala solo mueve la ventana que se pinta")

    # 2. a CONFIG.CSV van los numeros del usuario
    guarda = re.findall(r'SET_X_ESCALA_(?:LO|HI)\]\s*=\s*\(int32_t\)\s*(\w+)', cod)
    if sorted(guarda) != sorted(USR):
        print("  MAL: SET_X_ESCALA_LO/HI se guardan desde %s" % (guarda or "nada"))
        print("       Tienen que salir de %s, o CONFIG.CSV acaba con la" % (USR,))
        print("       ventana que la autoescala tuviera en ese instante.")
        mal += 1
    else:
        print("  ok: a CONFIG.CSV van tus numeros, no los de la autoescala")

    # 3. apagar la autoescala devuelve los del usuario
    t = trozo(cod, 'menu_tile_specagc_callback')
    if not (re.search(r's_db_min\s*=\s*s_db_min_usr', t) and
            re.search(r's_db_max\s*=\s*s_db_max_usr', t)):
        print("  MAL: apagar la autoescala no devuelve s_db_min_usr/s_db_max_usr.")
        print("       Sin eso, 'apagarla para que vuelva lo mio' no vuelve a nada.")
        mal += 1
    else:
        print("  ok: apagar la autoescala te devuelve tu escala")

    # 4. el mando escribe en LOS DOS pares, siempre
    # El anclaje es la FUNCION, no la constante: 'ENCODER_TARGET_SCALE'
    # sale primero en el enum, 20.000 lineas antes, y una ventana puesta
    # ahi no llega nunca al manejador. Lo descubrio este mismo banco la
    # primera vez que se ejecuto, diciendo que el mando no escribia en
    # ningun sitio cuando escribia en los dos.
    # ...y el anclaje es el CUERPO, no el prototipo: 'void
    # tune_encoder_poll' sale antes en la declaracion adelantada de la
    # linea 226, y una ventana puesta ahi tampoco llega. Dos anclajes
    # equivocados seguidos, los dos cazados por el propio banco.
    _mm = re.search(r'void\s+tune_encoder_poll\s*\(\s*void\s*\)\s*\{', cod)
    t = cod[_mm.start():_mm.start() + 40000] if _mm else ''
    if 'ENCODER_TARGET_SCALE' not in t:
        t = ''
    if not t:
        print("  MAL: no encuentro el manejador del mando de escala")
        mal += 1
    else:
        for vivo, usr in zip(LIVE, USR):
            n_v = len(re.findall(r'(?<!_)\b%s\s*=\s*v\s*;' % vivo, t))
            n_u = len(re.findall(r'\b%s\s*=\s*v\s*;' % usr, t))
            if n_v == 0 or n_v != n_u:
                print("  MAL: en el mando, %s se asigna %d vez/veces y %s %d."
                      % (vivo, n_v, usr, n_u))
                print("       Mover el mando tiene que escribir en los dos pares:")
                print("       si no, giras, lo ves cambiar y al guardar no se guarda.")
                mal += 1
        if not mal:
            print("  ok: el mando escribe en los dos pares")

    # 5. nadie mas escribe en el par del usuario
    fuera = []
    for mm in re.finditer(r'\b(s_db_(?:min|max)_usr)\s*=', cod):
        ini = cod.rfind('\nstatic ', 0, mm.start())
        cab = cod[ini:ini + 200].strip().split('\n')[0] if ini > 0 else '?'
        fuera.append(cab[:70])
    permitido = ('menu_tile_specagc_callback', 'tune_encoder_poll',
                 'extras_aplicar', 'static float s_db_min_usr',
                 'static float s_db_max_usr')
    sueltos = [f for f in fuera if not any(p in f for p in permitido)]
    if sueltos:
        print("  MAL: algo mas escribe en tu par de la escala:")
        for f in sorted(set(sueltos)):
            print("       %s" % f)
        mal += 1
    else:
        print("  ok: solo el mando, el boton y la carga de ajustes tocan tu escala")

    if mal:
        print("\nESCALA: %d MAL" % mal)
        return 1
    print("\nESCALA: todo correcto")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '.'))
