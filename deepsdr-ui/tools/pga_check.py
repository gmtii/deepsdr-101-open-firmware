#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE NO SE GUARDE NI SE RESTAURE LA GANANCIA QUE NO ES.

  python3 pga_check.py <User/main.c>

POR QUE EXISTE - 05/10/2026.

De un video del arranque: la radio arrancaba en WFM con el espectro plano
y el S-metro en S6, y bastaba cambiar de modo y volver para que apareciera
la emisora entera en S9+7. Veintitres decibelios de diferencia, con la
misma frecuencia y la misma antena.

Hay DOS ganancias de entrada y es facil confundirlas:

  s_pga_gain_db_x2   la VIVA, la que tiene el codec puesta ahora mismo
  s_pga_hf_x2        la TUYA, la que elegiste con el mando

En WFM no son la misma. apply_demod_mode() sube la viva al tope (47,5 dB)
a proposito, porque la VHF entra por una antena de HF y hace falta toda la
ganancia que hay; la tuya se queda guardada aparte para devolvertela al
salir. O sea que en WFM la viva no es una eleccion de nadie, es un apano.

De ahi salen dos fallos simetricos, y los dos habian pasado:

  AL RESTAURAR. El bloque de main() que aplica lo guardado corre DESPUES
  del apply_demod_mode() del arranque. Si mete lo guardado en la VIVA, se
  carga el tope que se acababa de poner: arrancas en WFM con tus 20 dB de
  HF. Eso es lo que se veia en el video.

  AL GUARDAR. Si un settings_save_now() escribe la VIVA estando en WFM, te
  deja 47,5 dB clavados como ajuste de HF para siempre. Lo hacian los dos
  de las calibraciones -la tactil y la de ppm-, mientras el autoguardado
  del bucle principal si lo hacia bien. Mismo fallo que tenia el atenuador
  antes del suelo manual, y por eso aqui se mira tambien el atenuador:
  s_att_suelo es el tuyo, s_rf_agc_rin_level es el que el AGC escala solo.

Ninguna de las dos cosas se ve leyendo la linea: cada una, por separado,
parece razonable. Solo se ven mirando TODOS los sitios a la vez. Por eso
esto se mide y no se recuerda.

LAS REGLAS:

  1. En el bloque `if (s_loaded_settings.have_pga_gain_db_x2)`, lo guardado
     tiene que ir a s_pga_hf_x2, y tocar la viva solo bajo una condicion
     que nombre DEMOD_MODE_WFM.

  2. Ninguna llamada a settings_save_now()/settings_poll() puede pasar
     s_pga_gain_db_x2 ni s_rf_agc_rin_level como argumento.

Lo que NO comprueba: que el resto de la cadena del PGA sea correcta. Eso no
lo puede mirar un guion.
"""
import io
import re
import sys


def sin_comentarios(src):
    """Fuera comentarios, conservando los saltos de linea (ver mando_check)."""
    src = re.sub(r'/\*.*?\*/',
                 lambda m: '\n' * m.group(0).count('\n'), src, flags=re.S)
    return re.sub(r'//[^\n]*', '', src)


def bloque_desde(src, i):
    """El { ... } que empieza en o despues de i."""
    k = src.find('{', i)
    if k < 0:
        return None, -1
    n = 0
    for q in range(k, len(src)):
        if src[q] == '{':
            n += 1
        elif src[q] == '}':
            n -= 1
            if n == 0:
                return src[k:q + 1], q
    return None, -1


def argumentos(src, i):
    """Los argumentos de la llamada cuyo parentesis abre en o despues de i."""
    a = src.find('(', i)
    if a < 0:
        return None, -1
    n = 0
    for q in range(a, len(src)):
        if src[q] == '(':
            n += 1
        elif src[q] == ')':
            n -= 1
            if n == 0:
                crudo = src[a + 1:q]
                break
    else:
        return None, -1
    # Partir por comas de primer nivel.
    args, hondo, acc = [], 0, ''
    for c in crudo:
        if c in '([':
            hondo += 1
        elif c in ')]':
            hondo -= 1
        if c == ',' and hondo == 0:
            args.append(' '.join(acc.split()))
            acc = ''
        else:
            acc += c
    args.append(' '.join(acc.split()))
    return args, q


PROHIBIDOS = ('s_pga_gain_db_x2', 's_rf_agc_rin_level')


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    src = sin_comentarios(
        io.open(sys.argv[1], encoding='utf-8', errors='replace').read())
    mal = 0

    # REGLA 1 - el bloque que restaura lo guardado.
    m = re.search(r'if\s*\(\s*s_loaded_settings\.have_pga_gain_db_x2\s*\)', src)
    if m is None:
        print("  no encuentro el bloque que restaura el PGA   MAL")
        return 1
    bl, _ = bloque_desde(src, m.end())
    ln = src[:m.start()].count('\n') + 1
    if bl is None:
        print("  linea %-6d el bloque del PGA no cierra   MAL" % ln)
        return 1
    pone_tuya = re.search(r's_pga_hf_x2\s*=', bl) is not None
    toca_viva = re.search(r's_pga_gain_db_x2\s*=', bl) is not None
    mira_wfm = 'DEMOD_MODE_WFM' in bl
    print("  restaurar el PGA (linea %d):" % ln)
    print("    guarda lo tuyo en s_pga_hf_x2 .......... %s"
          % ("ok" if pone_tuya else "*** NO ***"))
    if not pone_tuya:
        mal += 1
    if toca_viva and not mira_wfm:
        print("    pisa la viva sin mirar el modo ......... *** PISA EL TOPE DE WFM ***")
        mal += 1
    else:
        print("    la viva, solo fuera de WFM ............. ok")

    # REGLA 2 - los sitios de guardado.
    sitios = []
    for mm in re.finditer(r'\bsettings_(?:save_now|poll)\s*\(', src):
        args, fin = argumentos(src, mm.end() - 1)
        if args is None:
            continue
        sitios.append((src[:mm.start()].count('\n') + 1,
                       src[mm.start():mm.end() - 1].strip(), args))
    print("  sitios que escriben CONFIG.CSV: %d" % len(sitios))
    if not sitios:
        print("    no encuentro ninguno   MAL")
        return 1
    for ln, nombre, args in sitios:
        sucios = [a for a in args if a in PROHIBIDOS]
        print("    linea %-6d %-20s %s" % (ln, nombre,
              "ok" if not sucios else
              "*** GUARDA EL VIVO: %s ***" % ', '.join(sucios)))
        if sucios:
            mal += 1

    if mal:
        print("  *** %d sitios con la ganancia equivocada ***" % mal)
        print("  Lo que va a CONFIG.CSV es TU ajuste (pga_para_guardar(),")
        print("  s_att_suelo), nunca el apano de WFM ni lo que escala el AGC.")
        print("  Ver la cabecera de este guion.")
        return 1
    print("  la ganancia que se guarda y se restaura es la tuya  ok")
    return 0


sys.exit(main())
