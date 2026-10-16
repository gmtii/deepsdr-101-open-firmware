#!/usr/bin/env python3
"""
Comprueba el reparto de prioridades de interrupcion del firmware.

POR QUE EXISTE
--------------
Hasta la etapa 29 no habia ningun reparto puesto, y eso no significaba "los
valores por defecto": significaba que la funcion del fabricante,
nvic_irq_enable(), al no reconocer el registro en cero, establecia uno en
SILENCIO -dos bits para expropiar y dos para desempatar- la primera vez que
alguien encendia una interrupcion.

Con dos bits, el maximo que se puede pedir es 3. El audio pedia 6. Se
desbordaba fuera del byte y acababa valiendo lo mismo que el tactil, que pedia
2. O sea que el codigo decia "el tactil manda sobre el audio" y la maquina
hacia "van empatados", durante meses, sin que nada lo dijera.

Y el tic de 1 ms se quedaba en la prioridad mas baja que existe, porque es lo
que hace SysTick_Config() de CMSIS. Debajo del procesado de audio, que dura
milisegundos.

Ninguna de las tres cosas produce un aviso del compilador. Las tres se ven
leyendo tres ficheros distintos a la vez y sabiendo que buscar.

LO QUE VIGILA
-------------
  1. Que el reparto se establezca, y ANTES de encender ninguna interrupcion.
  2. Que ninguna prioridad sea un numero suelto: todas de irq_prio.h.
  3. Que SysTick se suba DESPUES de SysTick_Config(), que si no, no sirve.
  4. Que las prioridades sean distintas entre si y quepan en cuatro bits.
"""
import re
import sys
import os


def main(base):
    fallos = 0
    prio_h = open(os.path.join(base, 'User/irq_prio.h'), encoding='utf-8').read()
    main_c = open(os.path.join(base, 'User/main.c'), encoding='utf-8').read()

    # --- 1. las constantes del plan ---
    prios = dict(re.findall(r'#define\s+(IRQ_PRIO_\w+)\s+(\d+)U', prio_h))
    print('  el plan: ' + ', '.join('%s=%s' % (k.replace('IRQ_PRIO_', '').lower(), v)
                                    for k, v in sorted(prios.items(), key=lambda x: int(x[1]))))
    if len(prios) < 3:
        print('  FALLA: irq_prio.h no define las prioridades esperadas')
        return 1

    vals = [int(v) for v in prios.values()]
    if len(set(vals)) != len(vals):
        print('  FALLA: hay dos interrupciones con la misma prioridad')
        fallos += 1
    else:
        print('    todas distintas                                        ok')
    if max(vals) > 15:
        print('  FALLA: una prioridad se sale de los cuatro bits del micro')
        fallos += 1
    else:
        print('    todas caben en los cuatro bits del micro               ok')

    # --- 2. el reparto se establece antes de encender nada ---
    pos_grupo = main_c.find('nvic_priority_group_set(')
    if pos_grupo < 0:
        print('  FALLA: nadie llama a nvic_priority_group_set()')
        return fallos + 1
    print('    el reparto se establece explicitamente                  ok')

    enciende = []
    for nombre in ('User/main.c', 'User/sdr_rx.c', 'User/touch.c'):
        src = open(os.path.join(base, nombre), encoding='utf-8').read()
        for m in re.finditer(r'nvic_irq_enable\(\s*(\w+)\s*,\s*([^,]+),', src):
            enciende.append((nombre, m.group(1), m.group(2).strip(), m.start()))

    if not enciende:
        print('  FALLA: no se encuentra ninguna nvic_irq_enable()')
        return fallos + 1

    for nombre, irq, prio, pos in enciende:
        # 3. ninguna prioridad puede ser un numero suelto
        if not prio.startswith('IRQ_PRIO_'):
            print('  FALLA: %s enciende %s con "%s", que no sale de irq_prio.h'
                  % (nombre, irq, prio))
            fallos += 1
        elif prio not in prios:
            print('  FALLA: %s usa %s, que irq_prio.h no define' % (nombre, prio))
            fallos += 1
        else:
            print('    %-28s %-22s %s' % (irq, prio, 'ok'))
        # y en main.c, despues del reparto
        if nombre == 'User/main.c' and pos < pos_grupo:
            print('  FALLA: %s enciende %s ANTES de establecer el reparto'
                  % (nombre, irq))
            fallos += 1

    # --- 4. SysTick se sube despues de SysTick_Config ---
    pos_cfg = main_c.find('SysTick_Config(')
    pos_set = main_c.find('NVIC_SetPriority(SysTick_IRQn')
    if pos_set < 0:
        print('  FALLA: nadie sube la prioridad de SysTick; CMSIS la deja en la mas baja')
        fallos += 1
    elif pos_set < pos_cfg:
        print('  FALLA: se sube la prioridad de SysTick ANTES de SysTick_Config(),')
        print('         que la vuelve a escribir y se lleva el cambio por delante')
        fallos += 1
    else:
        print('    SysTick se sube despues de SysTick_Config()             ok')

    # --- 5. y que el vigilante siga ahi ---
    if 'tics_poll' not in main_c:
        print('  FALLA: falta el contador de tics perdidos, que es lo unico que')
        print('         comprueba desde fuera que este reparto hace algo')
        fallos += 1
    else:
        print('    el contador de tics perdidos sigue en su sitio         ok')

    print('  %s' % ('HAY FALLOS' if fallos else 'TODO CORRECTO'))
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '../deepsdr'))
