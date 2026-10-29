#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE EL CORRECTOR DE IMAGEN NO SE QUEDE PUESTO DONDE NADIE PUEDE MEDIRLO.

  python3 espejo_check.py <User/main.c>

POR QUE EXISTE - 28/10/2026.

El corrector de banda ancha de iqbal.c suma w[k]*conj(z[n-k]) a todas las
muestras de todos los modos, dentro de la interrupcion del ADC. Sus tomas
las calcula iqbal_resuelve(), que vive dentro de
sdr_spectrum_waterfall_tick(). En los modos de panel entero esa funcion no
pinta -se quedan la pantalla- asi que ahi dentro las tomas no se pueden
medir y las de otra banda siguen corrigiendo. Medido en
sim/hfdl_extremo.c, apartado 9, flujo largo con denominador: con la toma
en su tope, los enganches de preambulo pasan de 61 de 90 a 33 de 90, y las
tramas buenas de 50 a 12.

Y EL 29/10/2026 ESTE GUION SE REESCRIBE, PORQUE DEJO PASAR LA MITAD.

La version del 28/10 comprobaba solo que se llamara a iqbal_fir_olvida().
Tenia una tercera regla escrita a medias -un "if" seguido de "pass"- que
no comprobaba nada, y justo ahi estaba el agujero: la premisa de que
sdr_spectrum_waterfall_tick() no corre en los modos de panel entero ES
FALSA. El reparto del dibujo es

    } else if (drawing_now && modo_panel_entero()) { ...
    } else                                         { sdr_spectrum_waterfall_tick(); }

con drawing_now = activo && !s_menu_open. Con cualquier menu abierto se
cae al else y el tick corre, A PROPOSITO. Lo que se salta por dentro es
solo el DIBUJO; la FFT cruda que alimenta a iqbal_calibra() y la llamada a
iqbal_resuelve() quedan por delante de esa guarda.

O sea: entrar en HFDL borraba el FIR, abrir Ajustes lo volvia a instalar a
medio converger, y cerrar el menu lo dejaba congelado y aplicandose dentro
de la interrupcion el resto de la sesion.

LAS DOS REGLAS, Y LAS DOS HACEN FALTA:

  1. apply_demod_mode() llama a iqbal_fir_olvida() bajo
     modo_panel_entero(). Que la funcion exista no basta: el 08/10 de este
     proyecto ya paso que una funcion de reinicio no la llamara nadie.

  2. TODA llamada a iqbal_resuelve() esta bajo !modo_panel_entero(). Esta
     es la que faltaba. Sin ella, borrar el FIR al entrar no sirve de
     nada: se vuelve a poner solo.

Lo que NO comprueba: que el corrector plano siga puesto. Eso se mira en
iqbaltest.c, que es su banco.
"""
import io
import re
import sys


def cuerpo_de(src, firma):
    """El cuerpo de la DEFINICION, no el del prototipo."""
    desde = 0
    while True:
        k = src.find(firma, desde)
        if k < 0:
            return None
        if src[k + len(firma):k + len(firma) + 8].lstrip().startswith('{'):
            break
        desde = k + 1
    i = src.find('{', k)
    n = 0
    for j in range(i, len(src)):
        if src[j] == '{':
            n += 1
        elif src[j] == '}':
            n -= 1
            if n == 0:
                return src[i:j + 1]
    return None


def sin_comentarios(t):
    """Tapa /* ... */ y // ... con espacios, dejando los saltos de linea."""
    out = []
    i = 0
    n = len(t)
    while i < n:
        if t.startswith('/*', i):
            j = t.find('*/', i + 2)
            if j < 0:
                j = n
            else:
                j += 2
            out.append(''.join(c if c == '\n' else ' ' for c in t[i:j]))
            i = j
        elif t.startswith('//', i):
            j = t.find('\n', i)
            if j < 0:
                j = n
            out.append(' ' * (j - i))
            i = j
        else:
            out.append(t[i])
            i += 1
    return ''.join(out)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    src = io.open(sys.argv[1], encoding='utf-8', errors='replace').read()
    # Los comentarios fuera, pero CONSERVANDO las posiciones: se tapan con
    # espacios en vez de borrarse, asi los numeros de linea que se impriman
    # siguen siendo los del fichero de verdad. Hace falta porque este mismo
    # fichero EXPLICA el fallo en prosa y nombra a iqbal_resuelve() tres
    # veces dentro de comentarios; sin esto, el guion se denunciaba a si
    # mismo por los comentarios que documentan el arreglo.
    src = sin_comentarios(src)
    mal = 0

    print("  el FIR del espejo en los modos de panel entero:")

    # 0. la funcion que reparte los modos tiene que existir
    if 'static uint8_t modo_panel_entero(void)' not in src:
        print("    no encuentro modo_panel_entero()   MAL")
        return 1

    # 1. el olvido tiene que estar DENTRO de una guarda suya
    m = re.search(r'if\s*\(\s*modo_panel_entero\s*\(\s*\)\s*\)\s*\{'
                  r'[^}]*iqbal_fir_olvida\s*\(\s*\)\s*;', src, re.S)
    print("    se olvida al entrar .................... %s"
          % ("ok" if m else "*** NO: nadie lo olvida al entrar ***"))
    if not m:
        mal += 1

    # 2. y NINGUNA llamada a iqbal_resuelve() puede quedar fuera de una
    #    guarda !modo_panel_entero(). Se miran los 400 caracteres de
    #    delante de cada llamada, que es donde tiene que estar el "if".
    llamadas = [x.start() for x in re.finditer(r'iqbal_resuelve\s*\(', src)]
    sueltas = 0
    for p in llamadas:
        antes = src[max(0, p - 400):p]
        if not re.search(r'if\s*\(\s*!\s*modo_panel_entero\s*\(\s*\)\s*\)\s*\{'
                         r'[^{}]*$', antes, re.S):
            sueltas += 1
            linea = src.count('\n', 0, p) + 1
            print("    *** iqbal_resuelve() suelto en la linea %d ***" % linea)
    print("    el estimador no corre donde no mide ..... %s"
          % ("ok  (%d llamada(s))" % len(llamadas) if sueltas == 0
             else "*** %d suelta(s) ***" % sueltas))
    if not llamadas:
        print("    *** no encuentro ninguna llamada a iqbal_resuelve() ***")
        mal += 1
    mal += sueltas

    # 3. Y EL CORRECTOR PLANO NO SE OLVIDA AL CAMBIAR DE BANDA.
    #
    #    Esta regla exigia lo CONTRARIO durante unas horas del 29/10/2026, y
    #    la radio la desmintio: con iqbal_plano_olvida() en el guardian de
    #    banda, en 11.184 kHz con señales fuertes el embudo de HFDL se
    #    quedaba en 8>3>3 con 0 tramas buenas de 2 - engancha, sincroniza y
    #    ninguna pasa el CRC, que es la firma del espejo encima.
    #
    #    El plano es el corrector GRANDE (25 a 86 dB de rechazo). Olvidarlo
    #    en una banda donde su estimador no puede volver a medir -la guarda
    #    de la pareja simetrica- no lo deja corrigiendo de menos: lo deja
    #    SIN CORREGIR, y el theta de la banda de al lado quita mas espejo
    #    que cero.
    #
    #    Asi que la regla se da la vuelta y se queda escrita en los dos
    #    sentidos, para que a nadie se le ocurra volver a ponerlo "porque el
    #    razonamiento es el mismo que el del FIR". No lo es: el FIR si puede
    #    volver a medirse donde corre.
    cuerpo_lo = cuerpo_de(src, 'static void apply_lo_tune(uint32_t freq_hz)')
    if cuerpo_lo is None:
        print("    *** no encuentro apply_lo_tune() ***")
        mal += 1
    else:
        firs = len(re.findall(r'iqbal_fir_olvida\s*\(\s*\)\s*;', cuerpo_lo))
        planos = len(re.findall(r'iqbal_plano_olvida\s*\(\s*\)\s*;', cuerpo_lo))
        print("    al cambiar de banda se tira el FIR .... %s"
              % ("ok  (%d sitio(s))" % firs if firs else "*** NO ***"))
        if not firs:
            mal += 1
        print("    y el plano se queda .................. %s"
              % ("ok" if planos == 0 else
                 "*** %d iqbal_plano_olvida(): la antena dice que no ***"
                 % planos))
        mal += planos

    # 4. el corrector plano NO se apaga: que nadie haya colado un
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
