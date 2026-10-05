#!/usr/bin/env python3
"""Que digi_panel_active() sigue leyendo la tabla y no una lista a mano.

POR QUE EXISTE (28/09/2026)
--------------------------
digi_panel_active() contesta a "¿el espectro ha dejado su sitio al panel
digital?", y durante meses lo hizo con una lista escrita a mano:

    if (m == DEMOD_MODE_NFM) { return ax25_activo(); }
    return (m == USB || m == LSB) &&
           (rtty || cw || navtex || wefax || sstv || ft8 || hfdl);

La lista se quedo en HFDL. Despues entraron WSPR, AIS y ALE, y NINGUNO de
los tres se anadio aqui. Tres veces el mismo olvido, porque esta funcion
esta a dos mil lineas de donde se anade un modo y nada la senala.

El sintoma era el peor posible: el modo se selecciona, el demodulador
arranca, el decodificador corre y hasta decodifica - pero la pantalla no
cambia. El dueno lo dijo asi: "le doy a wspr y la ventana no cambia". Nada
falla, nada avisa.

Ahora la funcion lee k_demod_modes[] y devuelve si la entrada activa es de
la familia digital, con lo que un modo nuevo se lleva su panel solo. Esto
comprueba que sigue siendo asi: si alguien vuelve a escribir la lista a
mano - por optimizar, por "es mas claro", o copiando una version vieja - la
compilacion falla aqui en vez de fallar la radio en silencio.

No es un analizador de C: es un vigilante de una linea concreta. Con eso
basta para lo que tiene que impedir.
"""

import re
import sys


def main(ruta):
    with open(ruta, encoding='utf-8') as f:
        src = f.read()

    m = re.search(r'static uint8_t digi_panel_active\(void\)\s*\{', src)
    if not m:
        print('  no encuentro digi_panel_active() en %s  MAL' % ruta)
        return 1

    # El cuerpo, contando llaves desde la de apertura.
    i = m.end() - 1
    prof = 0
    for j in range(i, len(src)):
        if src[j] == '{':
            prof += 1
        elif src[j] == '}':
            prof -= 1
            if prof == 0:
                cuerpo = src[i + 1:j]
                break
    else:
        print('  digi_panel_active() no cierra  MAL')
        return 1

    # Fuera los comentarios: ahi SI se nombran los modos, y a proposito.
    limpio = re.sub(r'/\*.*?\*/', '', cuerpo, flags=re.S)
    limpio = re.sub(r'//[^\n]*', '', limpio)

    fallos = []

    if 'k_demod_modes' not in limpio:
        fallos.append('ya no lee k_demod_modes[]: ha vuelto a ser una lista '
                      'a mano')
    if 'MODO_FAM_DIG' not in limpio:
        fallos.append('ya no decide por familia (MODO_FAM_DIG)')

    # Cualquier pregunta directa a un decodificador es la lista volviendo.
    sueltos = sorted(set(re.findall(
        r'\b((?:\w+_)?(?:activo|get_enabled)\(\))', limpio)))
    if sueltos:
        fallos.append('pregunta a decodificadores uno a uno: ' +
                      ', '.join(sueltos))

    if fallos:
        sys.stderr.write('\nPANEL DIGITAL: %d problema(s) en '
                         'digi_panel_active().\n\n' % len(fallos))
        for f in fallos:
            sys.stderr.write('  - %s\n' % f)
        sys.stderr.write('\n  Tiene que salir de la tabla. Ver su propio\n'
                         '  comentario en main.c: una lista a mano ahi se\n'
                         '  quedo tres modos atras y nadie se entero.\n\n')
        return 1

    if comprueba_entry_active(src):
        return 1

    print('Panel digital: sale de la tabla, no de una lista a mano.')
    return 0


def comprueba_entry_active(src):
    """
    Y QUE demod_mode_entry_active() MIRE TODOS LOS INTERRUPTORES.

    POR QUE SE AÑADE ESTO (02/10/2026)
    ----------------------------------
    *** El dueño, estrenando el modo IDENT: "le doy al boton ident y no
    hace nada" ... "me quita el espectro" ... "y me pone abajo modo usb".
    ***

    Las tres frases juntas son el sintoma exacto de este fallo, y es el
    MISMO que esta funcion ya vigilaba un piso mas arriba, pero en otra
    funcion que nadie miraba.

    demod_mode_entry_active(i) contesta "¿es ESTA fila de k_demod_modes[]
    la que esta puesta?", y lo hace comparando los interruptores de la fila
    con los decodificadores de verdad, uno por uno y A MANO. Al añadir el
    modo nuevo se añadio su columna a la tabla y NO se añadio su linea
    aqui. Resultado: con IDENT puesto, la primera fila que casaba era la de
    "USB" a secas -porque todos los interruptores que esta funcion mira
    estaban apagados en las dos-, asi que la radio decia "Modo USB" abajo y
    el panel se pintaba con lo que le tocara a USB: nada.

    Nada falla, nada avisa, y lo que hay roto es una linea que no menciona
    el modo por ningun lado. Igual que la vez anterior, y la anterior, y la
    anterior.

    EL ARREGLO DE VERDAD NO ES AÑADIR LA LINEA: es que no se pueda olvidar.
    La tabla declara un uint8_t por decodificador; esto comprueba que
    TODOS esos campos aparecen en la funcion. Si alguien añade una columna
    y no la compara, la compilacion se para aqui.
    """
    import re as _re

    m = _re.search(r'typedef struct \{(.*?)\} demod_mode_entry_t;', src, _re.S)
    if not m:
        print('  no encuentro demod_mode_entry_t  MAL')
        return 1
    cuerpo_struct = _re.sub(r'/\*.*?\*/', '', m.group(1), flags=_re.S)

    # Los interruptores son los uint8_t de la tabla. `fam` es la categoria
    # de la columna, no un decodificador, y por eso se queda fuera.
    campos = [c for c in _re.findall(r'uint8_t\s+(\w+)\s*;', cuerpo_struct)
              if c != 'fam']
    if not campos:
        print('  demod_mode_entry_t no declara interruptores  MAL')
        return 1

    m = _re.search(r'static uint8_t demod_mode_entry_active\(uint8_t i\)'
                   r'\s*\{(.*?)\n\}', src, _re.S)
    if not m:
        print('  no encuentro demod_mode_entry_active()  MAL')
        return 1
    fn = _re.sub(r'/\*.*?\*/', '', m.group(1), flags=_re.S)

    faltan = [c for c in campos if ('.' + c) not in fn]
    if faltan:
        sys.stderr.write('\nLA TABLA DE MODOS Y SU COMPARADOR NO CUADRAN.\n\n')
        for c in faltan:
            sys.stderr.write('  - demod_mode_entry_t declara "%s" y\n'
                             '    demod_mode_entry_active() no lo mira.\n' % c)
        sys.stderr.write('\n  Sin esa linea, el modo nuevo se confunde con\n'
                         '  otra fila: la radio dice el nombre equivocado\n'
                         '  abajo y pinta el panel que no es. Ver el\n'
                         '  comentario de comprueba_entry_active().\n\n')
        return 1

    #
    # Y EL SEGUNDO SITIO: el que ENCIENDE el decodificador.
    #
    # Añadir un modo obliga a tocar dos sitios que estan a ocho mil lineas
    # uno de otro: el comparador de arriba y el bloque que aplica el modo.
    # Se comprueba que cada interruptor aparece en los DOS, porque olvidar
    # el segundo da el otro medio sintoma: la radio dice el nombre
    # correcto, cede el espectro... y el decodificador nunca arranca.
    #
    # No se exige mas que eso -ni el panel ni el boton de borrar- porque no
    # todos los modos los tienen: el fax no pinta tabla y el CW no tiene
    # que borrar nada. Estos dos si los necesita cualquier modo, y son los
    # dos que se han olvidado cuatro veces.
    #
    # El comparador recorre la tabla con `i`; el que aplica el modo usa
    # `idx`. Esa diferencia es lo que permite distinguirlos sin tener que
    # localizar funciones por su nombre. Y se exige la version SIN negar:
    # `if (k_demod_modes[idx].x)` es encender, `if (!...)` es apagar, y un
    # modo que solo sabe apagarse no sirve de nada.
    sueltos = [c for c in campos
               if not re.search(r'(?<!!)k_demod_modes\[idx\]\.' + c + r'\b', src)]
    if sueltos:
        sys.stderr.write('\nHAY INTERRUPTORES QUE NO ENCIENDEN NADA.\n\n')
        for c in sueltos:
            sys.stderr.write('  - la tabla declara "%s" y no hay ningun\n'
                             '    "if (k_demod_modes[idx].%s)" que lo\n'
                             '    encienda al aplicar el modo.\n' % (c, c))
        sys.stderr.write('\n  Asi el modo se selecciona, la pantalla cambia\n'
                         '  y el decodificador no arranca nunca.\n\n')
        return 1

    if comprueba_panel_pareja(src):
        return 1

    print('Tabla de modos: los %d interruptores se comparan y se encienden.'
          % len(campos))
    return 0


def _cuerpo_de(src, firma):
    """El cuerpo de una funcion, buscandola por su firma entera."""
    import re as _re
    m = _re.search(_re.escape(firma) + r'\s*\{', src)
    if not m:
        return ''
    i = m.end()
    prof = 1
    while i < len(src) and prof > 0:
        if src[i] == '{':
            prof += 1
        elif src[i] == '}':
            prof -= 1
        i += 1
    return src[m.end():i]


def comprueba_panel_pareja(src):
    """
    EL TERCER SITIO: quien RELLENA el panel y quien lo REPINTA van en pareja.

    *** El dueño, con el modo STANAG estrenado: "audio 4544 anillo 1024 0
    pasadas" ... "esta parado". *** No estaba parado: estaba midiendo y
    nadie repintaba. Lo que veia era una foto del instante en que el panel
    se dibujo por ultima vez.

    Añadir un modo con tabla obliga a tocar DOS funciones que estan a
    ochocientas lineas una de otra:

        digi_sync()       rellena s_digi: los renglones, los titulos, los
                          botones. Si falta, el panel sale vacio.
        ft8_panel_draw()  decide CUANDO repintar, mirando si el modo tiene
                          algo nuevo. Si falta, el panel se pinta una vez
                          y se queda congelado para siempre.

    El segundo da el sintoma peor, porque desde fuera no se distingue de
    "el decodificador no funciona": la pantalla enseña datos, parecen
    reales, y son de hace un minuto.

    La regla es exacta y no hay que mantenerla a mano: TODO modo que
    aparezca en el relleno tiene que aparecer en el repintado. No al reves
    -el repintado puede tener ramas de mas- pero si en este sentido.
    """
    import re as _re

    def cuerpo(nombre):
        m = _re.search(r'static void ' + nombre + r'\(void\)\s*\{', src)
        if not m:
            return None
        i = m.end()
        prof = 1
        while i < len(src) and prof > 0:
            if src[i] == '{':
                prof += 1
            elif src[i] == '}':
                prof -= 1
            i += 1
        return src[m.end():i]

    rellena = cuerpo('digi_sync')
    pinta   = cuerpo('ft8_panel_draw')
    if rellena is None or pinta is None:
        print('  no encuentro digi_sync() o ft8_panel_draw()  MAL')
        return 1

    def modos(txt):
        txt = _re.sub(r'/\*.*?\*/', '', txt, flags=_re.S)
        return set(_re.findall(r'\b(\w+)_modo_activo\(\)', txt))

    # FT8 es el caso por DEFECTO de ft8_panel_draw() -de ahi su nombre-:
    # su rama es lo que queda cuando ninguna de las demas se lleva la
    # funcion, asi que no se nombra a si mismo y no puede faltar.
    #
    # Y EL CUARTO SITIO: la condicion que decide si LLAMAR al repintado.
    #
    # Es otra lista escrita a mano, en el bucle principal, y tampoco
    # nombraba al modo nuevo: ft8_panel_draw() no llegaba a ejecutarse
    # nunca, asi que daba igual que tuviera su rama dentro. El sintoma es
    # identico al del tercer sitio -pantalla congelada- y por eso costo
    # encontrarlo: arreglar uno no cambiaba nada mientras el otro siguiera
    # roto.
    #
    # Se busca la llamada y la condicion que la envuelve, y se exige lo
    # mismo: todo modo que rellene el panel tiene que estar ahi.
    #
    llama = ''
    k = src.find('ft8_panel_draw();')
    if k > 0:
        ini = src.rfind('if (', max(0, k - 600), k)
        if ini > 0:
            llama = src[ini:k]

    # 05/10/2026: esa condicion ya no lleva la lista escrita dentro. La
    # captura de pantalla necesita la MISMA lista -para saber si pintar el
    # osciloscopio encima del panel o no- y dos copias de una lista de siete
    # modos es justo lo que este comprobador existe para evitar, asi que la
    # lista vive en modo_panel_entero() y la condicion la llama.
    #
    # Se sigue a la funcion en vez de exigir la lista literal: lo que hay
    # que comprobar es que el modo este en la condicion EFECTIVA, no en que
    # sitio del fichero esta escrita.
    if 'modo_panel_entero()' in llama:
        llama += _cuerpo_de(src, 'static uint8_t modo_panel_entero(void)')

    faltan = sorted(modos(rellena) - modos(pinta) - {'ft8'})
    sin_llamada = sorted(modos(rellena) - modos(llama) - {'ft8'})
    if sin_llamada:
        sys.stderr.write('\nHAY PANELES A LOS QUE NO SE LLAMA A PINTAR.\n\n')
        for m in sin_llamada:
            sys.stderr.write('  - "%s" rellena el panel en digi_sync() y no\n'
                             '    esta en la condicion que llama a\n'
                             '    ft8_panel_draw() desde el bucle principal.\n' % m)
        sys.stderr.write('\n  La pantalla se queda con la primera foto.\n'
                         '  Ver comprueba_panel_pareja().\n\n')
        return 1
    if faltan:
        sys.stderr.write('\nHAY PANELES QUE SE PINTAN UNA VEZ Y SE CONGELAN.\n\n')
        for m in faltan:
            sys.stderr.write('  - "%s" rellena el panel en digi_sync() y no\n'
                             '    tiene rama en ft8_panel_draw(), asi que\n'
                             '    nadie lo vuelve a pintar.\n' % m)
        sys.stderr.write('\n  Se ve como un decodificador que no funciona:\n'
                         '  la pantalla enseña datos, parecen reales, y son\n'
                         '  de hace un minuto. Ver comprueba_panel_pareja().\n\n')
        return 1

    print('Panel: los %d modos con tabla se rellenan y se repintan.'
          % len(modos(rellena)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'User/main.c'))
