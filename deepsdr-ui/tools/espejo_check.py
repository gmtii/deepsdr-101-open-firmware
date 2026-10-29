#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE EL CORRECTOR DE IMAGEN NO SE QUEDE PUESTO DONDE NADIE PUEDE MEDIRLO.

  python3 espejo_check.py <User/main.c>

POR QUE EXISTE - 28/10/2026.

El corrector de banda ancha de iqbal.c suma w[k]*conj(z[n-k]) a todas las
muestras de todos los modos, dentro de la interrupcion del ADC. Sus tomas
las calcula iqbal_resuelve(), que vive dentro de
sdr_spectrum_waterfall_tick(), y esa funcion NO se llama en los modos de
panel entero: se quedan la pantalla y no pintan espectro.

O sea que ahi dentro las tomas no se pueden volver a medir, y las de otra
banda siguen corrigiendo. Medido en sim/hfdl_extremo.c, apartado 9, flujo
largo con denominador: con la toma en su tope, los enganches de preambulo
pasan de 61 de 90 a 33 de 90, y las tramas buenas de 50 a 12.

LA REGLA: apply_demod_mode() tiene que llamar a iqbal_fir_olvida() bajo
modo_panel_entero(). No vale que este en el fichero en cualquier sitio: el
08/10 de este proyecto ya paso que una funcion de reinicio existiera y no
la llamara nadie, y eso es lo mismo que no tenerla.

Lo que NO comprueba: que el corrector plano siga puesto. Eso se mira en
iqbaltest.c, que es su banco.
"""
import io
import re
import sys


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    src = io.open(sys.argv[1], encoding='utf-8', errors='replace').read()
    mal = 0

    print("  el FIR del espejo en los modos de panel entero:")

    # 1. la funcion que reparte los modos tiene que existir
    if 'static uint8_t modo_panel_entero(void)' not in src:
        print("    no encuentro modo_panel_entero()   MAL")
        return 1

    # 2. y el olvido tiene que estar DENTRO de una guarda suya
    m = re.search(r'if\s*\(\s*modo_panel_entero\s*\(\s*\)\s*\)\s*\{'
                  r'[^}]*iqbal_fir_olvida\s*\(\s*\)\s*;', src, re.S)
    print("    se olvida bajo modo_panel_entero() ..... %s"
          % ("ok" if m else "*** NO: nadie lo olvida al entrar ***"))
    if not m:
        mal += 1

    # 3. y el estimador no puede estar corriendo ahi: si algun dia
    #    sdr_spectrum_waterfall_tick() se llamara en esos modos, este
    #    olvido sobraria y habria que revisarlo. Se avisa, no se falla.
    if 'modo_panel_entero()' in src and 'sdr_spectrum_waterfall_tick();' in src:
        pass

    # 4. el corrector plano NO se toca: que nadie haya colado un
    #    iqbal_set_on(0) en el mismo sitio.
    if re.search(r'if\s*\(\s*modo_panel_entero\s*\(\s*\)\s*\)\s*\{'
                 r'[^}]*iqbal_set_on\s*\(\s*0', src, re.S):
        print("    *** y ademas apaga el corrector PLANO: eso no se pidio ***")
        mal += 1

    if mal:
        print("  *** %d fallo(s): ver la cabecera de este guion ***" % mal)
        return 1
    print("  el corrector que no se puede medir no se aplica  ok")
    return 0


if __name__ == '__main__':
    sys.exit(main())
