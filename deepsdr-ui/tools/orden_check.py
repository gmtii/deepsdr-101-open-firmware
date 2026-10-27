#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE EL TACTIL SE MIRE ANTES DE DECIDIR QUE SE DIBUJA.

  python3 orden_check.py <User/main.c>

POR QUE EXISTE - 23/10/2026.

*** Avisado por un probador: al salir del decodificador de CW con el boton
Modo, la radio enseñaba media pantalla de cada una -el espectro arriba, la
chapa de los ppm en medio, y dos pestañas sueltas de la rejilla de modos
por la izquierda- y se quedaba asi hasta volver a pulsar. ***

LA VUELTA DEL BUCLE PRINCIPAL TIENE TRES PARTES Y EL ORDEN IMPORTA:

  1. mirar el tactil      -> aqui es donde se abre o se cierra una pantalla
  2. decidir quien pinta  -> active_now / drawing_now, que miran s_menu_open
  3. dibujar

Estaban en el orden 2, 1, 3: las banderas se calculaban ANTES del toque,
asi que eran una foto de como estaba la radio un momento antes. El toque
abria la rejilla de modos y la pintaba entera, y acto seguido el paso 3
-con la foto vieja en la mano- pintaba el panel digital por encima.

Y NO SE CURABA SOLO, que es lo que lo hacia visible: en la vuelta
siguiente drawing_now ya vale 0 porque el menu esta abierto, y entonces no
repinta nadie la pantalla que quedo rota.

LO QUE MIDE, sobre el cuerpo de main():

  - que haya una llamada a demo_touch_poll() ANTES de
    `uint8_t active_now = digi_panel_active();`
  - que ENTRE esa linea y el reparto del dibujo
    (sdr_spectrum_waterfall_tick(), que es la ultima rama) no haya
    ninguna llamada a demo_touch_poll() ni a tune_encoder_poll().

LO QUE NO MIDE: que el dibujo sea el correcto. Eso lo miran los bancos del
simulador. Esto solo mira el ORDEN, que es lo que se rompio y lo que no se
ve leyendo, porque las tres partes estan a doscientas lineas unas de otras.

Probado roto: bajando el bloque del tactil por debajo de active_now, esto
se pone rojo y dice que la mirada al tactil llega tarde.
"""
import io
import re
import sys

GUARDA = 'uint8_t active_now = digi_panel_active();'
REPARTO = 'sdr_spectrum_waterfall_tick();'
TARDE = ('demo_touch_poll();', 'tune_encoder_poll();')


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    src = io.open(sys.argv[1], encoding='utf-8', errors='replace').read()
    lineas = src.split('\n')

    def busca(txt):
        return [i for i, l in enumerate(lineas) if txt in l
                and not l.lstrip().startswith('*')
                and not l.lstrip().startswith('/*')]

    g = busca(GUARDA)
    r = busca(REPARTO)
    if len(g) != 1 or len(r) != 1:
        print('  no encuentro el tramo del dibujo en %s   MAL' % sys.argv[1])
        print('  (esto no es que este bien: es que no se donde mirar)')
        return 1
    g, r = g[0], r[0]
    if r < g:
        print('  el reparto del dibujo sale antes de la guarda   MAL')
        return 1

    malos = []

    antes = [i for i in busca('demo_touch_poll();') if i < g]
    ok = len(antes) > 0
    print('  %-52s %s' % ('el tactil se mira antes de la guarda',
                          'ok' if ok else 'MAL'))
    if not ok:
        malos.append('no hay ninguna llamada a demo_touch_poll() por delante\n'
                     '  de "%s"' % GUARDA)

    for que in TARDE:
        dentro = [i for i in busca(que) if g < i < r]
        ok = not dentro
        print('  %-52s %s' % ('nada de %s entre medias' % que[:-2],
                              'ok' if ok else 'MAL'))
        if not ok:
            malos.append('hay un %s en la linea %d, entre la guarda y el\n'
                         '  reparto del dibujo' % (que, dentro[0] + 1))

    if malos:
        print('')
        for m in malos:
            print('  ' + m)
        print('')
        print('  Mirar el tactil DESPUES de decidir quien pinta es dibujar lo')
        print('  de antes: el toque que abre una pantalla la pinta, y el')
        print('  dibujo de esa misma vuelta la tapa a medias.')
        return 1

    print('  el tactil se mira antes de decidir el dibujo   ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
