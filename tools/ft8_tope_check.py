#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE EL TOPE DE LA DECODIFICACION DE FT8 SIGA SIENDO UN TOPE.

  python3 tools/ft8_tope_check.py <User/ft8_modo.c> <User/main.c>

POR QUE EXISTE - 03/11/2026.

La decodificacion de una ranura de FT8 no cabe siempre en el tiempo que
hay: con la banda llena hay mas candidatos que milisegundos. Lo que no
puede pasar es que por eso se pierda la ranura SIGUIENTE, y de eso se
encarga un tope que corta y reintegra.

Ese tope se ha roto de tres maneras distintas en un solo dia, y las tres
en silencio -la radio seguia decodificando, solo que peor-:

  1. mirando el reloj ANTES de cada candidato, cuando un candidato cuesta
     seis veces el trozo. El tope existia y no topaba.
  2. contando bloques del desvio en vez de mirar el reloj. El contador se
     quedaba corto por un motivo que sigue sin saberse, y hubo ranuras de
     2,4 s con el tope puesto en 1,6.
  3. y el AVISO de que todo eso pasaba estaba puesto en 7.500 ms, o sea
     por encima del propio tope: el numero que lo habria delatado no podia
     salir nunca.

Ninguna de las tres la caza un banco de decodificacion, porque las tres
decodifican bien. Se cazan leyendo que el codigo siga diciendo lo que
tiene que decir, que es lo que hace esto.
"""
import re
import sys


def mira(txt, patron, que, fallos, nombre):
    if re.search(patron, txt, re.S):
        print('  %-58s ok' % que)
        return fallos
    print('  %-58s *** MAL ***' % que)
    print('      no encuentro el patron en %s' % nombre)
    return fallos + 1


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    modo = open(sys.argv[1], encoding='utf-8', errors='replace').read()
    main_c = open(sys.argv[2], encoding='utf-8', errors='replace').read()
    mal = 0

    print('\nEL TOPE DE LA DECODIFICACION DE FT8\n')

    # 1. el tope mira el RELOJ, y no solo un contador de bloques
    mal = mira(modo, r'#define\s+FT8_TOPE_MS\s+\d+',
               'hay un tope en milisegundos', mal, sys.argv[1])
    mal = mira(modo, r'g_msticks\s*-\s*s_ms_ini\s*\)\s*>=\s*FT8_TOPE_MS',
               'y el corte lo mira contra el reloj de verdad', mal, sys.argv[1])

    # 2. el remuestreador respira durante la busqueda de candidatos
    mal = mira(modo, r'ft8_decoder_set_respira\s*\(\s*ft8_decimator_poll\s*\)',
               'la busqueda deja respirar al remuestreador', mal, sys.argv[1])

    # 3. el aviso de tiempo TIENE que poder dispararse antes del tope
    m_av = re.search(r'#define\s+FT8_BARRA_MS_AVISA\s+(\d+)', main_c)
    m_tp = re.search(r'#define\s+FT8_TOPE_MS\s+(\d+)', modo)
    if not m_av or not m_tp:
        print('  %-58s *** MAL ***' % 'el aviso de tiempo y el tope se pueden comparar')
        mal += 1
    else:
        av, tp = int(m_av.group(1)), int(m_tp.group(1))
        if av < tp:
            print('  %-58s ok   %d < %d' %
                  ('el aviso de tiempo salta ANTES que el tope', av, tp))
        else:
            print('  %-58s *** MAL ***' %
                  'el aviso de tiempo salta ANTES que el tope')
            print('      aviso %d ms, tope %d: el numero no puede salir nunca' % (av, tp))
            mal += 1

    # 4. y los perdidos no pueden tapar al resto sin decirlo
    mal = mira(main_c, r"s_ft8_barra\[i\+\+\]\s*=\s*'x';",
               'la barra distingue ranuras cortadas de audio perdido', mal,
               sys.argv[2])

    print('\n%s\n' % ('TOPE: todo correcto' if mal == 0
                      else '*** %d FALLOS ***' % mal))
    return 1 if mal else 0


if __name__ == '__main__':
    sys.exit(main())
