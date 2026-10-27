#!/usr/bin/env python3
"""
QUE EL GANCHO DEL DEMODULADOR SIGA AL MODO Y A NADA MAS.

POR QUE EXISTE. 24/10/2026, avisado por el usuario: la FM comercial pintaba
las señales y no daba audio.

sdr_rx_set_block_hook() decide cual de los dos caminos corre en la
interrupcion de audio: el de FM ancha o el de todo lo demas. Esa eleccion
vivia DENTRO del `if (desired_rate != s_current_rate)` de apply_demod_mode(),
y durante meses fue correcta por una casualidad: la FM ancha era el UNICO
modo a 192 kHz, asi que entrar o salir de ella cambiaba la tasa siempre.

Al dejar elegir 192 kHz para los demas modos, la casualidad se rompio: pasar
de banda lateral a FM comercial ya no cambia la tasa, el bloque no entraba, y
la radio seguia corriendo el camino de banda lateral sobre una señal de FM.
El espectro se pinta igual -sale del bucle principal, que no depende del
gancho- asi que el fallo no se parecia a lo que era.

QUE MIRA. Que las dos llamadas que eligen camino esten al NIVEL DE ARRIBA de
apply_demod_mode() -sangria de cuatro- y no dentro de ningun `if`. No es una
comprobacion de estilo: una condicion ahi es, exactamente, el fallo.

La de `sdr_rx_set_block_hook(0)` -soltar el gancho antes de reconfigurar- si
puede estar anidada, y lo esta: soltarlo de mas no rompe nada, porque la de
abajo lo vuelve a poner sin condicion.
"""
import re, sys, os

raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src  = open(os.path.join(raiz, 'User/main.c'), encoding='utf-8', errors='replace').read()

ini = src.index('static void apply_demod_mode(demod_mode_t mode)\n{')
# El final: la primera linea que empieza por } en la columna cero.
fin = src.index('\n}\n', ini) + 3
cuerpo = src[ini:fin]

lineas = cuerpo.splitlines()
puestas = []
for n, l in enumerate(lineas):
    if 'sdr_rx_set_block_hook(' not in l:
        continue
    if re.search(r'sdr_rx_set_block_hook\(\s*0\s*\)', l):
        continue          # soltarlo puede ir donde sea
    sangria = len(l) - len(l.lstrip())
    puestas.append((n + 1, sangria, l.strip()))

fallos = 0
if not puestas:
    print('  no encuentro donde se elige el camino del demodulador   MAL')
    fallos += 1
else:
    print('  %d sitio(s) eligen el camino del demodulador' % len(puestas))
    for n, sangria, txt in puestas:
        if sangria != 4:
            print('      sangria %d (deberia ser 4): %s' % (sangria, txt[:60]))
            fallos += 1

if fallos:
    print('  el gancho esta dentro de un `if`: volveria a pasar lo de la FM   MAL')
else:
    print('  el gancho se pone sin condicion, siguiendo al modo  ok')
sys.exit(1 if fallos else 0)
