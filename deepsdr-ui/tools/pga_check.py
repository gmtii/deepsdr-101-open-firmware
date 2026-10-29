#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE NO SE GUARDE NI SE RESTAURE LA GANANCIA QUE NO ES.

  python3 pga_check.py <User/main.c>

POR QUE EXISTE - 05/10/2026.

De un video del arranque: la radio arrancaba en WFM con el espectro plano y
el S-metro en S6, y bastaba cambiar de modo y volver para que apareciera la
emisora entera en S9+7. Veintitres decibelios, misma frecuencia y misma
antena.

El fallo era confundir la ganancia VIVA -la que el codec tiene puesta ahora
mismo- con la TUYA -la que elegiste con el mando-. En FM ancha no son la
misma, y de ahi salen dos fallos simetricos que ya habian pasado los dos:

  AL RESTAURAR. El bloque que aplica lo guardado corre DESPUES del
  apply_demod_mode() del arranque. Si mete lo guardado en la VIVA, se carga
  lo que se acababa de poner.

  AL GUARDAR. Si un settings_save_now() escribe la VIVA estando en FM ancha,
  te deja el apano clavado como ajuste de onda corta para siempre.

Ninguna de las dos se ve leyendo la linea: cada una, por separado, parece
razonable. Solo se ven mirando TODOS los sitios a la vez.

LO QUE CAMBIO EL 29/10/2026, Y POR QUE ESTE GUION SE REESCRIBIO ENTERO.

Pedido por el usuario, con la medida delante -"he entrado a hfdl con 20 dB y
parecia perder paquetes, la he subido a 35 y parece ir mejor"- y una por
modo: "no no uno para cada uno de los 8". Asi que las ganancias pasan de
tres a DIEZ:

  s_pga_gain_db_x2   la VIVA
  s_pga_hf_x2        la tuya escuchando
  s_pga_wfm_x2       la tuya en FM ancha
  s_pga_digi_x2[8]   la tuya en cada modo de panel entero

Con diez, la version anterior de este guion -que buscaba ramas con
DEMOD_MODE_WFM a cada lado- dejaba de servir: no hay dos lados, hay diez
zonas. Y diez ramas encadenadas eran justo lo que iba a traer el fallo
numero tres.

Asi que main.c se dio la vuelta: no hay ramas por transicion, hay ZONAS, y
UNA sola funcion que guarda lo vivo en la zona de la que vienes y carga lo de
la zona a la que vas. Lo que este guion vigila ahora es esa propiedad, que es
mas fuerte y mas facil de comprobar que las ramas de antes:

  1. El bloque que restaura `have_pga_gain_db_x2` pone lo guardado en
     s_pga_hf_x2, y toca la viva solo bajo una condicion.

  2. Ningun settings_save_now()/settings_poll() recibe la viva ni el
     atenuador que escala el AGC. Esos son apanos, no ajustes.

  3. EL CAMBIO DE ZONA TIENE UN SOLO DUEÑO. La linea que carga la viva
     desde una de las tuyas -`s_pga_gain_db_x2 = *pga_mio(...)`- aparece
     UNA vez en todo el fichero, y dentro de pga_zona_sync(). El dia que
     aparezca una segunda, hay dos sitios decidiendo la misma cosa y el
     fallo de 2026 vuelve con otro nombre.

  4. Y SE LLAMA DOS VECES. Una desde apply_demod_mode() y otra despues de
     arrancar los modos digitales: en la primera todavia no estan
     arrancados, asi que sin la segunda entrar en HFDL se queda con la
     ganancia de la zona anterior. Esto no se puede deducir leyendo, hay
     que contarlo.

  5. El mando NO escribe ninguna de las tuyas por su nombre: escribe a
     traves de pga_mio(), que es el mismo reparto que usa el cambio de
     zona. Si escribiera una por su nombre, girarlo en otra zona marcaria
     sucio y guardaria el valor de otra. Ya paso con s_pga_hf_x2 el
     26/10/2026.

  6. extras_leer() guarda las variables, nunca la viva; y nombra las ocho
     claves digitales a mano -eso lo cruza persist_check.py-.

Lo que NO comprueba: que el resto de la cadena del PGA sea correcta. Eso no
lo puede mirar un guion.
"""
import io
import re
import sys

PROHIBIDOS = ('s_pga_gain_db_x2', 's_rf_agc_rin_level')


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
    args, hondo, acc = [], 0, ''
    for c in crudo:
        if c in '([':
            hondo += 1
        elif c in ')]':
            hondo -= 1
        if c == ',' and hondo == 0:
            args.append(acc.strip())
            acc = ''
        else:
            acc += c
    if acc.strip():
        args.append(acc.strip())
    return args, q


def cuerpo_de(src, firma):
    """El cuerpo de una funcion por su firma exacta.

    LA DEFINICION, NO LA DECLARACION - 29/10/2026. Buscando la firma a
    secas, lo primero que se encuentra es el prototipo de arriba del
    fichero -`static void extras_leer(settings_extra_t *e);`- y
    bloque_desde() se traga el siguiente `{` que haya, que es de otra
    funcion. El guion cantaba entonces que extras_leer() no guardaba
    nada. Se pide la firma seguida de salto y llave, que es lo que
    distingue una definicion de un prototipo.
    """
    i = src.find(firma + '\n{')
    if i < 0:
        return None
    bl, _ = bloque_desde(src, i + len(firma))
    return bl


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    src = sin_comentarios(
        io.open(sys.argv[1], encoding='utf-8', errors='replace').read())
    mal = 0

    # ---- REGLA 1: el bloque que restaura lo guardado ---------------------
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
    guardado = ('DEMOD_MODE_WFM' in bl) or ('pga_zona' in bl) or ('if' in bl)
    print("  restaurar el PGA (linea %d):" % ln)
    print("    guarda lo tuyo en s_pga_hf_x2 .......... %s"
          % ("ok" if pone_tuya else "*** NO ***"))
    mal += 0 if pone_tuya else 1
    if toca_viva and not guardado:
        print("    pisa la viva sin condicion ............. *** PISA EL APANO ***")
        mal += 1
    else:
        print("    la viva, solo bajo condicion ........... ok")

    # ---- REGLA 2: los sitios de guardado ---------------------------------
    sitios = []
    for mm in re.finditer(r'\bsettings_(?:save_now|poll)\s*\(', src):
        args, _ = argumentos(src, mm.end() - 1)
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
        mal += 1 if sucios else 0

    # ---- REGLA 3: el cambio de zona tiene UN dueño -----------------------
    cargas = [src[:m.start()].count('\n') + 1
              for m in re.finditer(r's_pga_gain_db_x2\s*=\s*\*pga_mio\s*\(', src)]
    sync = cuerpo_de(src, 'static void pga_zona_sync(void)')
    print("  el cambio de zona:")
    if sync is None:
        print("    no encuentro pga_zona_sync()   *** MAL ***")
        mal += 1
    else:
        dentro = re.search(r's_pga_gain_db_x2\s*=\s*\*pga_mio\s*\(', sync) is not None
        print("    carga la viva desde pga_mio() .......... %s"
              % ("ok" if dentro else "*** NO: no cambia de zona ***"))
        mal += 0 if dentro else 1
    print("    y es el unico que lo hace ............. %s"
          % ("ok" if len(cargas) == 1 else
             "*** %d sitios: lineas %s ***" % (len(cargas), cargas)))
    mal += 0 if len(cargas) == 1 else 1

    # ---- REGLA 4: se llama dos veces -------------------------------------
    llamadas = [src[:m.start()].count('\n') + 1
                for m in re.finditer(r'\bpga_zona_sync\s*\(\s*\)\s*;', src)]
    print("    se llama %d vez(ces) ................... %s"
          % (len(llamadas),
             "ok" if len(llamadas) >= 2 else
             "*** hacen falta DOS: entrar en un digital no cargaria la suya ***"))
    mal += 0 if len(llamadas) >= 2 else 1

    # ---- REGLA 5: el mando escribe por pga_mio(), no por nombre ----------
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
        print("  linea %-6d el bloque del mando no cierra   MAL" % ln)
        return 1
    # El argumento puede llevar su propia llamada dentro
    # -pga_mio(pga_zona_ahora())-, asi que el parentesis no se puede
    # cerrar con [^)]*: se permite un nivel de anidamiento.
    por_mio = re.search(r'\*\s*pga_mio\s*\((?:[^()]|\([^()]*\))*\)\s*=',
                        bl) is not None
    por_nombre = [n for n in ('s_pga_hf_x2', 's_pga_wfm_x2', 's_pga_digi_x2')
                  if re.search(r'\b' + n + r'\s*(\[[^\]]*\])?\s*=', bl)]
    print("  el mando del PGA (linea %d):" % ln)
    print("    escribe por pga_mio() ................. %s"
          % ("ok" if por_mio else "*** NO: no guarda la de la zona ***"))
    mal += 0 if por_mio else 1
    print("    y ninguna por su nombre ............... %s"
          % ("ok" if not por_nombre else
             "*** escribe %s a mano ***" % ', '.join(por_nombre)))
    mal += 0 if not por_nombre else 1

    # ---- REGLA 6: extras_leer() guarda las variables, no la viva ---------
    leer = cuerpo_de(src, 'static void extras_leer(settings_extra_t *e)')
    if leer is None:
        leer = cuerpo_de(src, 'static void extras_leer(settings_extra_t *e)\n')
    print("  extras_leer():")
    if leer is None:
        print("    no la encuentro   *** MAL ***")
        mal += 1
    else:
        wfm_ok = re.search(r'e->v\[SET_X_PGA_WFM\]\s*=\s*\(int32_t\)s_pga_wfm_x2', leer)
        digi_ok = 's_pga_digi_x2[' in leer
        viva = re.search(r'e->v\[SET_X_PGA_[A-Z0-9_]+\]\s*=[^;]*s_pga_gain_db_x2', leer)
        print("    la de FM ancha es la variable ......... %s"
              % ("ok" if wfm_ok else "*** NO ***"))
        print("    las ocho digitales, del array ......... %s"
              % ("ok" if digi_ok else "*** NO ***"))
        print("    y ninguna guarda la viva .............. %s"
              % ("ok" if not viva else "*** GUARDA LA VIVA ***"))
        mal += (0 if wfm_ok else 1) + (0 if digi_ok else 1) + (0 if not viva else 1)

    # ---- REGLA 7: pga_zona_sync() va DESPUES de demod_am_set_mode() -----
    #
    # 29/10/2026, por la tarde, y esto no es teoria: la V6.15 se entrego
    # por la manyana con la llamada SIETE LINEAS ANTES de
    # demod_am_set_mode(). pga_zona_ahora() pregunta por el modo VIVO, asi
    # que puesta ahi veia siempre el anterior. Los ocho digitales se
    # salvaban por la segunda llamada; FM ancha no tiene segunda llamada, y
    # el resultado era que sus 25,0 dB no se cargaban nunca y que al salir
    # se escribia la ganancia de FM dentro de s_pga_hf_x2 -lo unico que
    # llega a CONFIG.CSV-.
    #
    # Las cuatro ramas viejas no tenian el problema porque usaban el
    # PARAMETRO. Al cambiarlas por una funcion que mira el modo vivo habia
    # que mover la llamada, y no se movio. Esta regla es esa.
    cuerpo = cuerpo_de(src, 'static void apply_demod_mode(demod_mode_t mode)')
    print("  el orden dentro de apply_demod_mode():")
    if cuerpo is None:
        print("    no encuentro apply_demod_mode()   *** MAL ***")
        mal += 1
    else:
        i_sync = cuerpo.find('pga_zona_sync();')
        i_modo = cuerpo.find('demod_am_set_mode(mode);')
        if i_sync < 0 or i_modo < 0:
            print("    falta pga_zona_sync() o demod_am_set_mode()  *** MAL ***")
            mal += 1
        else:
            bien = i_sync > i_modo
            print("    pga_zona_sync() despues del modo ...... %s"
                  % ("ok" if bien else
                     "*** NO: mira el modo VIEJO, ver la regla 7 ***"))
            mal += 0 if bien else 1

    # ---- REGLA 8: al cruzar de zona, el recorte del AGC a cero ----------
    #
    # rf_agc_apply_pga() solo recorta el recorte HACIA ABAJO, contra el
    # techo nuevo. Subir el techo -de 20,0 a 35,0 al entrar en HFDL- no lo
    # toca, asi que la ganancia efectiva se queda en techo menos un recorte
    # que se aprendio con el techo viejo: 15,0 dB en vez de 35,0, y a 2 dB
    # cada tres segundos para recuperar. La rama de FM ancha lleva este
    # mismo razonamiento escrito desde el 26/10; las ocho zonas nuevas se
    # quedaron sin el hasta el 29/10.
    sync = cuerpo_de(src, 'static void pga_zona_sync(void)')
    print("  pga_zona_sync():")
    if sync is None:
        print("    no la encuentro   *** MAL ***")
        mal += 1
    else:
        i_cruce = sync.find('s_pga_zona = z;')
        i_cero = sync.find('s_rf_agc_backoff_x2 = 0;')
        bien = (i_cruce >= 0 and i_cero > i_cruce)
        print("    al cruzar pone el recorte a cero ...... %s"
              % ("ok" if bien else
                 "*** NO: la zona nueva hereda el recorte de la vieja ***"))
        mal += 0 if bien else 1

    # ---- REGLA 9: quien apague un modo digital, que sincronice la zona --
    #
    # 29/10/2026, tarde. La salida de emergencia por pulsacion larga es el
    # unico sitio que llama a digi_todo_apagado() SIN un apply_demod_mode()
    # detras, y apply_demod_mode() es quien llama a pga_zona_sync(). Sin
    # esa llamada, s_pga_zona se queda apuntando a la zona digital con la
    # radio ya en la de escuchar, y la siguiente vez que se cruce guarda lo
    # vivo en la casilla equivocada.
    print("  digi_todo_apagado():")
    apaga = cuerpo_de(src, 'static void digi_todo_apagado(void)')
    if apaga is None:
        print("    no la encuentro   *** MAL ***")
        mal += 1
    else:
        tiene = re.search(r'pga_zona_sync\s*\(\s*\)\s*;', apaga) is not None
        print("    sincroniza la zona al apagar .......... %s"
              % ("ok" if tiene else
                 "*** NO: la zona se queda apuntando al digital ***"))
        mal += 0 if tiene else 1

    if mal:
        print("  *** %d sitio(s) con la ganancia equivocada ***" % mal)
        print("  Lo que va a CONFIG.CSV es TU ajuste, nunca el apano de una")
        print("  zona ni lo que escala el AGC. Ver la cabecera de este guion.")
        return 1
    print("  la ganancia que se guarda y se restaura es la de su zona  ok")
    return 0


if __name__ == '__main__':
    sys.exit(main())
