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

Y DESDE EL 28/10/2026 HAY UNA TERCERA GANANCIA, con sus propias reglas.

Pedido por el usuario: 25 dB de serie en FM ancha, y que si se cambia se
guarde. Hasta ese dia la FM ancha no tenia ganancia propia -apply_demod_mode()
clavaba el tope del codec- y por eso este guion solo conocia dos variables.
Ahora son tres:

  s_pga_gain_db_x2   la VIVA, la que tiene el codec puesta ahora mismo
  s_pga_hf_x2        la TUYA fuera de FM ancha
  s_pga_wfm_x2       la TUYA dentro de FM ancha

El fallo que esto vigila es el mismo de siempre, con un sitio mas donde
cometerlo: que una de las dos tuyas acabe donde va la otra. Tres reglas:

  3. El mando (ENCODER_TARGET_PGA) tiene que escribir LAS DOS, cada una
     bajo su lado de una condicion que nombre DEMOD_MODE_WFM. Si solo
     escribe una, girar el mando en el otro modo marca sucio, escribe el
     fichero... y escribe el valor viejo. Peor que no guardar: parece
     guardado. Ya paso con s_pga_hf_x2 el 26/10/2026.

  4. extras_leer() guarda s_pga_wfm_x2, NO la viva. Estando en onda corta
     la viva es la de HF, y guardarla ahi te cambiaria la de FM sin
     haberla tocado.

  5. extras_aplicar() restaura s_pga_wfm_x2 y toca la viva solo bajo una
     condicion que nombre DEMOD_MODE_WFM. Es la regla 1 reflejada: al
     arrancar, apply_demod_mode() corre ANTES, asi que sin esto arrancar en
     FM ancha te da el valor de serie aunque tengas otro guardado.

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

    # REGLA 3 - el mando escribe las DOS, una por lado del modo.
    # Y SE BUSCA DENTRO DE tune_encoder_poll(), NO EN TODO EL FICHERO
    # - 28/10/2026. Buscandolo en todo el fichero, el primer
    # `if (s_encoder_target == ENCODER_TARGET_PGA)` podia ser cualquier
    # otro sitio que solo MIRA a donde apunta el mando -por ejemplo un
    # renglon de pantalla que dice "Ganancia 25,0 dB"-, y entonces esto
    # auditaba un bloque que no escribe ninguna ganancia y cantaba tres
    # fallos que no existen. Paso.
    desde = src.find('static void tune_encoder_poll(void)\n{')
    if desde < 0:
        print("  no encuentro tune_encoder_poll()   MAL")
        return 1
    m = re.compile(r'if\s*\(\s*s_encoder_target\s*==\s*ENCODER_TARGET_PGA\s*\)'
                   ).search(src, desde)
    if m is None:
        print("  no encuentro el mando del PGA   MAL")
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

    # REGLA 3 - el mando escribe las DOS, una por lado del modo.
    # Y SE BUSCA DENTRO DE tune_encoder_poll(), NO EN TODO EL FICHERO
    # - 28/10/2026. Buscandolo en todo el fichero, el primer
    # `if (s_encoder_target == ENCODER_TARGET_PGA)` podia ser cualquier
    # otro sitio que solo MIRA a donde apunta el mando -por ejemplo un
    # renglon de pantalla que dice "Ganancia 25,0 dB"-, y entonces esto
    # auditaba un bloque que no escribe ninguna ganancia y cantaba tres
    # fallos que no existen. Pasó.
    desde = src.find('static void tune_encoder_poll(void)\n{')
    if desde < 0:
        print("  no encuentro tune_encoder_poll()   MAL")
        return 1
    m = re.search(r'if\s*\(\s*s_encoder_target\s*==\s*ENCODER_TARGET_PGA\s*\)',
                  src[desde:])
    if m is None:
        print("  no encuentro el mando del PGA   MAL")
        return 1
    m = re.search(r'if\s*\(\s*s_encoder_target\s*==\s*ENCODER_TARGET_PGA\s*\)',
                  src, re.M) if False else m
    class _M(object):
        def __init__(self, mm, off):
            self._m = mm; self._o = off
        def start(self): return self._m.start() + self._o
        def end(self):   return self._m.end() + self._o
    m = _M(m, desde)
    bl, _ = bloque_desde(src, m.end())
    ln = src[:m.start()].count('\n') + 1
    if bl is None:
        print("  linea %-6d el bloque del mando no cierra   MAL" % ln)
        return 1
    hf = re.search(r's_pga_hf_x2\s*=', bl) is not None
    wfm = re.search(r's_pga_wfm_x2\s*=', bl) is not None
    mira = 'DEMOD_MODE_WFM' in bl
    print("  el mando del PGA (linea %d):" % ln)
    print("    escribe tu ganancia de HF .............. %s"
          % ("ok" if hf else "*** NO: girar fuera de WFM no se guarda ***"))
    print("    escribe tu ganancia de FM ancha ........ %s"
          % ("ok" if wfm else "*** NO: girar en WFM no se guarda ***"))
    print("    y reparte mirando el modo .............. %s"
          % ("ok" if mira else "*** NO mira DEMOD_MODE_WFM ***"))
    mal += (0 if hf else 1) + (0 if wfm else 1) + (0 if mira else 1)

    # REGLA 4 - lo que se guarda de FM ancha es la variable, no la viva.
    m = re.search(r'e->v\[SET_X_PGA_WFM\]\s*=([^;]*);', src)
    if m is None:
        print("  extras_leer() no guarda SET_X_PGA_WFM   MAL")
        mal += 1
    else:
        ln = src[:m.start()].count('\n') + 1
        bien = ('s_pga_wfm_x2' in m.group(1)
                and 's_pga_gain_db_x2' not in m.group(1))
        print("  guardar la de FM ancha (linea %d) ......... %s"
              % (ln, "ok" if bien else "*** GUARDA LA VIVA ***"))
        if not bien:
            mal += 1

    # REGLA 5 - al restaurarla, la viva solo bajo el modo.
    m = re.search(r'if\s*\(\s*EX_HAY\(SET_X_PGA_WFM\)\s*\)', src)
    if m is None:
        print("  extras_aplicar() no restaura SET_X_PGA_WFM   MAL")
        mal += 1
    else:
        bl, _ = bloque_desde(src, m.end())
        ln = src[:m.start()].count('\n') + 1
        if bl is None:
            print("  linea %-6d el bloque de SET_X_PGA_WFM no cierra   MAL" % ln)
            mal += 1
        else:
            pone = re.search(r's_pga_wfm_x2\s*=', bl) is not None
            viva = re.search(r's_pga_gain_db_x2\s*=', bl) is not None
            mira = 'DEMOD_MODE_WFM' in bl
            print("  restaurar la de FM ancha (linea %d):" % ln)
            print("    la deja en s_pga_wfm_x2 ................ %s"
                  % ("ok" if pone else "*** NO ***"))
            print("    y al vivo solo estando en WFM .......... %s"
                  % ("ok" if (viva and mira) else
                     "*** arrancar en WFM no la coge ***" if not viva else
                     "*** pisa la viva sin mirar el modo ***"))
            mal += (0 if pone else 1) + (0 if (viva and mira) else 1)

    # REGLA 5b - y que el tope clavado ya no este.
    if re.search(r's_pga_gain_db_x2\s*=\s*\(int16_t\)\s*PGA_MAX_X2', src):
        print("  apply_demod_mode() sigue clavando el tope en WFM  *** MAL ***")
        print("    desde el 28/10/2026 FM ancha tiene su ganancia: s_pga_wfm_x2")
        mal += 1
    else:
        print("  en WFM manda tu ganancia, no el tope ...... ok")

    if mal:
        print("  *** %d sitios con la ganancia equivocada ***" % mal)
        print("  Lo que va a CONFIG.CSV es TU ajuste (pga_para_guardar(),")
        print("  s_att_suelo), nunca el apano de WFM ni lo que escala el AGC.")
        print("  Ver la cabecera de este guion.")
        return 1
    print("  la ganancia que se guarda y se restaura es la tuya  ok")
    return 0


sys.exit(main())
