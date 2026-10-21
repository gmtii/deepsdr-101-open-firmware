#!/usr/bin/env python3
"""
Que nadie cierre el fichero de otro, y las demas cosas que no se pueden
perder en main.c cuando entra una carta de fax.

Nacio vigilando UNA sola: que nadie cerrara el fichero de otro. Hoy vigila
tambien que la reescritura no comparta el buffer, que nadie borre la zona
del panel con el cartel de recolocar puesto, y que entrar en el modo en el
que ya estas no tire la carta. Las cuatro son lo mismo visto desde cuatro
sitios: en main.c, el fax comparte sus cosas con quien pase, y lo que
comparte no se puede comprobar compilando.

QUE VIGILA, Y POR QUE EXISTE
----------------------------
gsv_* es UNA SOLA maquina de escribir ficheros en el pendrive, y la usan
tres: el SSTV, el WEFAX y la captura de pantalla. El bucle principal llama
a fax_poll() y a sstv_poll() EN LA MISMA VUELTA y SIEMPRE, este activo el
modo que este.

Durante semanas, sstv_poll() empezaba con "si el SSTV no esta activo,
cierra lo que haya". O sea: en WEFAX, fax_poll() abria la carta y escribia
la cabecera, y la linea siguiente del bucle la cerraba. Resultado: un
fichero de 8 kB -cabecera mas un bloque de renglones- cada pocos segundos,
y el usuario mandando capturas de su carpeta cuatro veces en un dia
mientras yo miraba el camino de escritura, que no tenia nada que ver.

Lo mas incomodo: el comentario de gsv_cierra_ajeno() DESCRIBIA este fallo
exacto. Se habia tapado solo para la captura, con una bandera. El fax y el
SSTV se quedaron cerrandose el fichero el uno al otro, y nada lo
comprobaba.

Esto lo comprueba. El fichero tiene dueño -s_gsv_pre: "WFAX", "SSTV",
"PANT"- y quien cierra tiene que decir de quien es lo que viene a cerrar.

LA REGLA
--------
gsv_cierra() a secas solo vale en tres sitios, y por una razon cada uno:

  gsv_cierra_si_es()  es quien comprueba el dueño; tiene que poder llamarla.
  gsv_linea()         es el propio fichero cerrandose al llegar a su altura.
  capt_poll()         la captura es la dueña mientras s_capt_on.

En cualquier otro sitio hay que usar gsv_cierra_si_es("XXXX") con un
prefijo literal de cuatro letras. Y gsv_cierra_ajeno() -el que cerraba "lo
que hubiera"- no puede volver a existir: ese nombre ES el fallo.
"""
import re
import sys

# Donde si vale cerrar a secas, y por que (sale impreso, para que quien lea
# el fallo sepa si su caso es uno de estos).
PERMITIDOS = {
    'gsv_cierra_si_es': 'es quien comprueba el dueño',
    'gsv_linea':        'el fichero se cierra solo al llegar a su altura',
    'capt_poll':        'la captura es la dueña mientras s_capt_on',
    'gsv_bloque':       'no se pudo escribir: cierra lo suyo en condiciones',
}

# Y una nota, porque esta lista es justo donde se cuela el proximo fallo:
# anadir un nombre aqui es DECIR que ahi el dueño es evidente. gsv_bloque()
# entro el 20/10/2026 y entro porque es la propia maquina cerrandose
# despues de que una escritura falle -sin eso, la carta se quedaba a medias
# con la cabecera prometiendo WFX_ALTO_MAX renglones-. Si manana hace falta
# anadir otro, que sea por una razon de ese tamaño y escrita al lado.

# Los prefijos que de verdad existen. Un literal que no sea uno de estos es
# casi seguro una errata, y una errata aqui no cierra nada nunca.
PREFIJOS = ('WFAX', 'SSTV', 'PANT')


def sin_comentarios(src):
    """El fuente con los comentarios en blanco, conservando los saltos de
    linea para que los numeros de linea sigan siendo los de verdad.

    Hace falta porque los comentarios de este fichero NOMBRAN estas
    funciones -"ver el comentario gordo de gsv_cierra_si_es()"- y sin esto
    la comprobacion acusa a una frase explicativa. Un guardian que da
    fallos falsos se desactiva a la semana.

    Las cadenas NO se tocan: el prefijo del dueño es justo una cadena y
    hay que poder leerlo.
    """
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif src.startswith('/*', i):
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', src[i:j]))
            i = j
        elif src.startswith('//', i):
            j = src.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def funcion_de(src, pos):
    """El nombre de la funcion de nivel superior en la que cae `pos`.

    Se busca hacia atras el ultimo comienzo de funcion. Sirve una regla
    tosca -una linea que empieza en la columna 1 y acaba en '{'- porque en
    este fichero todas las funciones se escriben asi.
    """
    ultimo = '(fuera de toda funcion)'
    for m in re.finditer(r'(?m)^[A-Za-z_][A-Za-z0-9_ \*]*?([A-Za-z0-9_]+)\s*\([^;]*?\)\s*\{',
                         src[:pos]):
        ultimo = m.group(1)
    return ultimo


def main():
    if len(sys.argv) != 2:
        sys.exit('uso: gsv_dueno_check.py <main.c>')
    src = open(sys.argv[1], encoding='utf-8', errors='replace').read()
    src = sin_comentarios(src)
    malos = 0

    # 1. El nombre que ES el fallo no puede volver.
    if re.search(r'\bgsv_cierra_ajeno\b', src):
        print('  gsv_cierra_ajeno() ha vuelto. Ese nombre significa "cierra lo')
        print('  que haya, sea de quien sea", que es justo el fallo de los')
        print('  ficheros de 8 kB. Usa gsv_cierra_si_es("XXXX").   MAL')
        malos += 1

    # 2. Cerrar a secas, solo donde toca.
    n_secas = 0
    for m in re.finditer(r'\bgsv_cierra\s*\(\s*\)', src):
        f = funcion_de(src, m.start())
        if f in PERMITIDOS:
            n_secas += 1
            continue
        linea = src[:m.start()].count('\n') + 1
        print('  linea %d: gsv_cierra() dentro de %s()' % (linea, f))
        print('    Ahi no se sabe de quien es el fichero. Si de verdad es tuyo,')
        print('    dilo: gsv_cierra_si_es("XXXX").   MAL')
        malos += 1

    # 3. Y los que dicen dueño, que lo digan bien.
    n_dueno = 0
    for m in re.finditer(r'\bgsv_cierra_si_es\s*\(\s*([^)]*?)\s*\)', src):
        arg = m.group(1)
        if arg.startswith('const char'):
            continue          # la definicion, no una llamada 
        linea = src[:m.start()].count('\n') + 1
        lit = re.fullmatch(r'"(....)"', arg)
        if not lit:
            print('  linea %d: gsv_cierra_si_es(%s)' % (linea, arg))
            print('    El dueño tiene que ser un literal de cuatro letras, para')
            print('    que esto pueda leerlo sin ejecutar nada.   MAL')
            malos += 1
            continue
        if lit.group(1) not in PREFIJOS:
            print('  linea %d: prefijo "%s", que no existe. Son: %s   MAL'
                  % (linea, lit.group(1), ', '.join(PREFIJOS)))
            malos += 1
            continue
        n_dueno += 1

    # 4. Y que el dueño se compare de verdad contra s_gsv_pre, no que la
    #    funcion exista y no mire nada.
    cuerpo = re.search(r'static void gsv_cierra_si_es\(const char pre\[4\]\)\s*\{(.*?)\n\}',
                       src, re.S)
    if not cuerpo:
        print('  no encuentro gsv_cierra_si_es(). Sin ella no hay dueño.   MAL')
        malos += 1
    elif 's_gsv_pre' not in cuerpo.group(1):
        print('  gsv_cierra_si_es() no mira s_gsv_pre: dice que comprueba el')
        print('  dueño y no lo comprueba.   MAL')
        malos += 1

    # 5. Y EL CERROJO, QUE ES LO QUE IMPIDE LA RIADA.
    #
    # El dueño tapa el cerrador que se conoce. Lo que impide que un
    # cerrador DESCONOCIDO haga una riada es el cerrojo de un minuto - y
    # solo si lo arma el CIERRE. Mientras lo armaba el que cerraba, bastaba
    # con que uno no se acordara: sstv_poll() no se acordaba, y de ahi los
    # veintiun ficheros en un minuto.
    #
    # Asi que s_wfx_hubo y s_wfx_ult solo se ARMAN dentro de gsv_cierra().
    # En cualquier otro sitio vuelve a depender de que alguien se acuerde.
    #
    # *** ARMAR Y SOLTAR NO SON LO MISMO - 21/10/2026. ***
    #
    # Esta regla miraba cualquier escritura, y por eso daba en rojo el
    # `s_wfx_hubo = 0U` del boton de guardar, que es lo contrario de lo que
    # la regla protege: un cerrojo que se ARMA fuera del cierre deja de
    # armarse el dia que un cerrador no se acuerde -de ahi los veintiun
    # ficheros en un minuto-, mientras que SOLTARLO no puede hacer una
    # riada, porque suelta una vez por dedo y un dedo no es una riada.
    #
    # Asi que lo que no se puede hacer fuera del cierre es ponerlo a 1 o
    # tocar la marca de tiempo. Ponerlo a 0 se permite, y solo ahi: es lo
    # que hace que el boton sea un interruptor de verdad. Ver el punto 9.
    n_cerrojo = 0
    for m in re.finditer(r'\b(s_wfx_hubo|s_wfx_ult)\s*=\s*([^=;]*);', src):
        f = funcion_de(src, m.start())
        # El cuerpo se llama gsv_cierra_interno() desde el 21/10/2026, y el
        # cerrojo sigue siendo suyo: lo que importa es que no lo arme quien
        # llama. Ver el punto 6.
        if f in ('gsv_cierra', 'gsv_cierra_interno'):
            n_cerrojo += 1
            continue
        if m.group(1) == 's_wfx_hubo' and m.group(2).strip() in ('0U', '0'):
            continue                      # soltarlo, no armarlo
        linea = src[:m.start()].count('\n') + 1
        print('  linea %d: %s se arma dentro de %s()' % (linea, m.group(1), f))
        print('    El cerrojo lo arma el CIERRE, no el que cierra. Si lo arma')
        print('    quien llama, basta un cerrador que no se acuerde para que no')
        print('    se arme nunca - que es justo lo que paso.   MAL')
        malos += 1
    if n_cerrojo < 2:
        print('  gsv_cierra() no arma el cerrojo (s_wfx_hubo/s_wfx_ult).')
        print('  Sin eso, un cerrador desconocido vuelve a poder hacer una')
        print('  riada de ficheros.   MAL')
        malos += 1

    #
    # 6. Y QUE EL ENVOLTORIO SIGA SIENDO UN ENVOLTORIO - 21/10/2026.
    #
    # LO QUE PASO: la pregunta de recolocar la carta se armaba en la ULTIMA
    # linea del cierre. El cuerpo tiene cinco `return`, y el camino de una
    # carta de fax sale por el PRIMERO, cientos de lineas antes del final.
    # O sea que corria para la captura de pantalla y para el SSTV, y justo
    # para el fax -el unico que la usa- no corria nunca. Se entrego asi.
    #
    # Lo de "pase lo que pase al cerrar" vive ahora en gsv_cierra(), que
    # llama al cuerpo y luego hace lo suyo. Esto exige que ese reparto siga
    # en pie: que gsv_cierra() llame al cuerpo y arme la pregunta, y que el
    # cuerpo NO la arme por su cuenta -si la armara ahi dentro, volveria a
    # depender de por que `return` se salga-.
    cuerpo_env = re.search(r'static void gsv_cierra\(void\)\s*\{(.*?)\n\}', src, re.S)
    if not cuerpo_env:
        print('  no encuentro gsv_cierra().   MAL')
        malos += 1
    else:
        env = cuerpo_env.group(1)
        if 'gsv_cierra_interno' not in env:
            print('  gsv_cierra() no llama al cuerpo (gsv_cierra_interno).')
            print('  Si el cuerpo vuelve a ser gsv_cierra() a secas, lo que se')
            print('  escriba al final no corre para una carta de fax.   MAL')
            malos += 1
        if 'wfxr_pide' not in env:
            print('  gsv_cierra() no arma la pregunta de recolocar.')
            print('  Es el unico sitio por el que pasan TODAS las salidas del')
            print('  cierre; en el cuerpo depende de por que return se salga.   MAL')
            malos += 1
    #
    # Y EL CUERPO ES PRIVADO: UN SOLO LLAMANTE - 21/10/2026.
    #
    # La regla 2 de aqui arriba busca `gsv_cierra()` con parentesis vacios,
    # que NO casa con `gsv_cierra_interno()`. O sea que cualquiera podia
    # llamar al cuerpo desde otro sitio y saltarse de golpe el envoltorio, el
    # cartel y la comprobacion de dueño, sin que nada se quejara. El
    # comentario del envoltorio prometia una vigilancia que no existia.
    llamadas = [m for m in re.finditer(r'\bgsv_cierra_interno\s*\(\s*\)', src)]
    # una es la declaracion adelantada, otra la definicion, y una la llamada
    fuera = [m for m in llamadas
             if funcion_de(src, m.start()) not in ('gsv_cierra', '(fuera de toda funcion)')]
    if fuera:
        for m in fuera:
            print('  linea %d: gsv_cierra_interno() desde %s()'
                  % (src[:m.start()].count('\n') + 1, funcion_de(src, m.start())))
        print('    El cuerpo es privado del envoltorio. Llamarlo de fuera se')
        print('    salta el cartel de recolocar y la comprobacion de dueño.   MAL')
        malos += 1

    interno = re.search(r'static void gsv_cierra_interno\(void\)\s*\{(.*?)\n\}', src, re.S)
    if interno and 'wfxr_pide' in interno.group(1):
        print('  gsv_cierra_interno() arma la pregunta por su cuenta. Ahi')
        print('  depende de por que `return` se salga, que es el fallo que')
        print('  esto viene a impedir.   MAL')
        malos += 1

    #
    # 7. Y LA REESCRITURA NO EMPIEZA CON UN FICHERO ABIERTO - 21/10/2026.
    #
    # wfx_recoloca usa s_gsv_buf entero como bloque de trabajo, y ese buffer
    # es del grabador. Empezar a girar con un fichero abierto pone a los dos
    # a escribir en el mismo sitio: la carta nueva sale con tiras de 4 kB de
    # la vieja dentro y la vieja con renglones de la nueva. Los dos
    # ficheros destrozados, y la vieja ademas medio girada y sin deshacer.
    #
    # Ya paso dos veces en el mismo dia por dos caminos distintos -el relevo
    # por altura y la reapertura durante la pregunta-, asi que lo que se
    # exige no es una guarda concreta sino el invariante: wfxr_acepta()
    # tiene que cerrar lo que haya antes de prestar el buffer, y negarse si
    # despues de cerrar sigue habiendo algo abierto.
    acepta = re.search(r'static void wfxr_acepta\(void\)\s*\{(.*?)\n\}', src, re.S)
    if not acepta:
        print('  no encuentro wfxr_acepta().   MAL')
        malos += 1
    else:
        cuerpo_a = acepta.group(1)
        # Y se exige el DUEÑO, no un cierre cualquiera: con "SSTV" aqui el
        # fichero del fax se quedaria abierto y la comprobacion pasaba.
        m_cierra = re.search(r'gsv_cierra_si_es\s*\(\s*"WFAX"\s*\)', cuerpo_a)
        m_mira   = re.search(r'\bif\s*\(\s*s_gsv_on\s*\)', cuerpo_a)
        i_presta = cuerpo_a.find('wfx_recoloca_empieza')
        if not m_cierra or not m_mira or i_presta < 0:
            print('  wfxr_acepta() tiene que cerrar el fichero del FAX, negarse')
            print('  si s_gsv_on sigue puesto, y solo entonces prestar')
            print('  s_gsv_buf.   MAL')
            malos += 1
        elif not (m_cierra.start() < m_mira.start() < i_presta):
            print('  wfxr_acepta() hace las cosas en otro orden: primero')
            print('  cerrar, luego comprobar, luego prestar el buffer.   MAL')
            malos += 1
        else:
            # Y que la comprobacion RECHACE de verdad: un `if (s_gsv_on)` que
            # no vuelve deja seguir con el fichero abierto. Se mira que haya
            # un return dentro de su bloque, antes de prestar el buffer.
            tramo = cuerpo_a[m_mira.start():i_presta]
            if 'return' not in tramo:
                print('  wfxr_acepta() mira s_gsv_on y no se vuelve: comprobar')
                print('  sin rechazar es no comprobar.   MAL')
                malos += 1

    #
    # Y LAS CUATRO GUARDAS QUE SON EL INVARIANTE DE VERDAD - 21/10/2026.
    #
    # Lo de arriba vigila el sitio donde el fallo ya esta arreglado. El
    # invariante -nadie abre un fichero mientras la reescritura tiene
    # prestado s_gsv_buf- vive en las llamadas a gsv_abre() y en
    # capt_pide(), que son por donde volveria a entrar. Se comprobo que sin
    # esto el guion daba verde con las cuatro guardas quitadas.
    #
    # No se exige una forma concreta: se exige que CADA gsv_abre() tenga un
    # !wfxr_obra() cerca -en su condicion- y que capt_pide() lo mire.
    # La FUNCION que llama tiene que mirar wfxr_obra(). No se exige que sea
    # en la condicion del mismo if -capt_pide() lo mira antes, al principio-
    # sino que quien abre se haya hecho la pregunta.
    cuerpos = {}
    for m in re.finditer(r'(?m)^static\s[A-Za-z0-9_ \*]*?([A-Za-z0-9_]+)\s*\([^;]*?\)\s*\{', src):
        cuerpos[m.group(1)] = m.start()
    sin_guarda = 0
    for m in re.finditer(r'\bgsv_abre\s*\(', src):
        # La definicion y su declaracion adelantada no son llamadas. Se
        # reconocen porque su renglon empieza por `static`; funcion_de() no
        # sirve aqui, porque en la cabecera de una funcion la llave todavia
        # no se ha abierto y contesta la funcion ANTERIOR.
        ren0 = src.rfind('\n', 0, m.start()) + 1
        if 'static' in src[ren0:m.start()]:
            continue
        f = funcion_de(src, m.start())
        ini = cuerpos.get(f, -1)
        cuerpo_f = src[ini:m.start()] if ini >= 0 else ''

        # Dos formas validas, y se exige UNA DE LAS DOS POR CADA LLAMADA:
        #
        #   a) la guarda esta en el `if` que manda sobre esta llamada, o
        #   b) la funcion se ha vuelto antes: `if (wfxr_obra()) { ... return`
        #
        # Mirar solo si el nombre aparece en la funcion no vale: con dos
        # gsv_abre() en la misma funcion, una guarda tapaba a las dos.
        pronto = re.search(r'if\s*\(\s*wfxr_obra\s*\(\s*\)\s*\)[^}]*?return', cuerpo_f, re.S)
        cerca = src.rfind('if (', max(ini, m.start() - 700), m.start())
        en_la_condicion = (cerca >= 0 and 'wfxr_obra' in src[cerca:m.start()])
        if not pronto and not en_la_condicion:
            print('  linea %d: gsv_abre() en %s() sin wfxr_obra() que lo mande'
                  % (src[:m.start()].count('\n') + 1, f))
            sin_guarda += 1
    if sin_guarda:
        print('    Mientras la reescritura tiene prestado s_gsv_buf, abrir un')
        print('    fichero pone a los dos a escribir en el mismo sitio y')
        print('    destroza los dos ficheros.   MAL')
        malos += sin_guarda
    #
    # Y QUE NADIE BORRE LA ZONA DEL PANEL CON EL CARTEL PUESTO - 21/10/2026.
    #
    # rtty_scope_panel_reset() borra el area ENTERA del panel con el fondo
    # del tema: cartel de recolocar y vista previa incluidos. Y nadie los
    # repinta hasta el siguiente cambio de estado, asi que lo que queda es la
    # radio que parece rota -el tactil comido por un cartel que no se ve-
    # hasta que caduca a los cinco minutos.
    #
    # Se llega ahi por el USB, no por el dedo: apply_demod_mode() no tiene
    # salida temprana si el modo no cambia, asi que cualquier orden de modo
    # por CAT vuelve a entrar por la rama del fax y llama a
    # fax_panel_reinicia(), que pone s_fax_limpio a cero. La guarda que
    # habia protegia solo el camino de RTTY.
    #
    # Se exige en el BORRADO, no en cada uno de los que ponen la bandera:
    # mientras el cartel se vea, la zona no se borra, venga de donde venga.
    cuerpo_fp = re.search(r'static void fax_poll\(void\)\s*\{(.*?)\n\}\n', src, re.S)
    if not cuerpo_fp:
        print('  no encuentro fax_poll().   MAL')
        malos += 1
    else:
        n_borra = 0
        for m in re.finditer(r'\brtty_scope_panel_reset\s*\(', cuerpo_fp.group(1)):
            n_borra += 1
            cerca = cuerpo_fp.group(1).rfind('if (', 0, m.start())
            if cerca < 0 or 'wfxr_tapando' not in cuerpo_fp.group(1)[cerca:m.start()]:
                print('  fax_poll() borra la zona del panel sin mirar')
                print('  wfxr_tapando(): con el cartel puesto se lo lleva por')
                print('  delante y el tactil se queda comido sin nada que lo')
                print('  explique.   MAL')
                malos += 1
        if n_borra == 0:
            print('  fax_poll() ya no borra la zona del panel: esta')
            print('  comprobacion ha dejado de medir nada.   MAL')
            malos += 1

    #
    # Y QUE ENTRAR EN EL MODO EN EL QUE YA ESTAS NO TIRE LA CARTA -
    # 21/10/2026.
    #
    # apply_demod_mode() no tiene salida temprana cuando el modo no cambia, y
    # su rama del fax llamaba a wefax_start() siempre: estado a BUSCA, anillo
    # borrado, reloj y seguidor de fase tirados. Y como fax_poll() cierra el
    # fichero en la transicion de imagen a no-imagen, la carta que se
    # estuviera grabando se PARTIA EN DOS FICHEROS.
    #
    # Lo dispara cualquiera: un WSJT-X o un fldigi ponen el modo al arrancar
    # -aunque sea el que ya esta-, un cambio de banda pasa por ahi, y elegir
    # WEFAX en la rejilla estando ya en WEFAX tambien.
    for m in re.finditer(r'\bwefax_start\s*\(', src):
        ren0 = src.rfind('\n', 0, m.start()) + 1
        if 'void' in src[ren0:m.start()]:
            continue                      # la declaracion, no una llamada
        cerca = src.rfind('if (', max(0, m.start() - 400), m.start())
        if cerca < 0 or 'wefax_activo' not in src[cerca:m.start()]:
            print('  linea %d: wefax_start() sin mirar si el fax ya estaba'
                  % (src[:m.start()].count('\n') + 1))
            print('  puesto: una orden de modo que no cambia el modo parte')
            print('  la carta en dos ficheros.   MAL')
            malos += 1

    #
    # Y QUE LA RECURSION DE gsv_cierra() SIGA ACOTADA - 21/10/2026.
    #
    # gsv_bloque() llama a gsv_cierra() cuando no puede escribir, y
    # gsv_cierra() -por su cuerpo- llama a gsv_bloque() para volcar lo que
    # quede. tools/pila.py ve el ciclo y lo dice: "estos caminos no quedan
    # acotados", y de paso cuenta ese camino como CERO, asi que el peor caso
    # de pila que publica el README se queda corto.
    #
    # Esta acotada por DOS lineas en DOS funciones distintas:
    #
    #   1. gsv_cierra_interno() pone s_gsv_on = 0 ANTES de cualquier
    #      gsv_bloque();
    #   2. gsv_cierra() empieza negandose si s_gsv_on ya es 0.
    #
    # Quien mueva la primera detras del primer volcado, o quite la segunda,
    # abre la recursion de verdad y nada chilla. Esto chilla.
    cuerpo_i = re.search(r'static void gsv_cierra_interno\(void\)\s*\{(.*?)\n\}\n',
                         src, re.S)
    cuerpo_w = re.search(r'static void gsv_cierra\(void\)\s*\{(.*?)\n\}\n',
                         src, re.S)
    if not cuerpo_i or not cuerpo_w:
        print('  no encuentro gsv_cierra() o gsv_cierra_interno().   MAL')
        malos += 1
    else:
        cero = re.search(r's_gsv_on\s*=\s*0U\s*;', cuerpo_i.group(1))
        volcado = re.search(r'\bgsv_bloque\s*\(', cuerpo_i.group(1))
        guarda = re.search(r'if\s*\(\s*!\s*s_gsv_on\s*\)[^;]*;', cuerpo_w.group(1))
        if not cero:
            print('  gsv_cierra_interno() ya no retira el fichero del servicio:')
            print('  sin ese s_gsv_on = 0, la recursion con gsv_bloque() deja')
            print('  de estar acotada.   MAL')
            malos += 1
        elif volcado and cero.start() > volcado.start():
            print('  gsv_cierra_interno() vuelca un bloque ANTES de poner')
            print('  s_gsv_on a cero: si ese volcado falla, gsv_bloque()')
            print('  vuelve a entrar en gsv_cierra() de verdad.   MAL')
            malos += 1
        elif not guarda or guarda.start() > 40:
            print('  gsv_cierra() ya no se niega a entrar con s_gsv_on a cero,')
            print('  o no lo hace lo primero: es la otra mitad del tope de la')
            print('  recursion.   MAL')
            malos += 1

    #
    # Y QUE EL AVISO NO SE PINTE ENCIMA DEL CARTEL - 21/10/2026.
    #
    # capt_avisa() y el cartel de recolocar viven en la misma franja de la
    # pantalla y se solapan en 32 renglones, 20 de ellos de los botones. Y
    # al caducar, capt_restaura() repinta la pantalla de la radio pero NO el
    # cartel -no sabe que existe- asi que se queda con la mitad de abajo en
    # blanco y los botones medio borrados respondiendo igual.
    #
    # Mientras el cartel se vea, el aviso no se pone.
    cuerpo_av = re.search(r'static void capt_avisa\(const char \*msg\)\s*\{(.*?)\n\}\n',
                          src, re.S)
    if not cuerpo_av:
        print('  no encuentro capt_avisa().   MAL')
        malos += 1
    else:
        g = re.search(r'if\s*\(\s*wfxr_tapando\s*\(\s*\)\s*\)[^;]*return', cuerpo_av.group(1))
        escribe = cuerpo_av.group(1).find('s_capt_msg[i] = msg[i]')
        if not g:
            print('  capt_avisa() no mira wfxr_tapando(): el aviso se pinta')
            print('  encima del cartel de recolocar y nadie lo repinta.   MAL')
            malos += 1
        elif escribe >= 0 and g.start() > escribe:
            print('  capt_avisa() mira wfxr_tapando() DESPUES de armar el')
            print('  aviso: comprobar tarde es no comprobar.   MAL')
            malos += 1

    #
    # Y QUE UN AVISO DEL CARTEL NO LO BORRE EL DEDO QUE LO PUSO - 21/10/2026.
    #
    # El despacho del tactil llama a esto en CADA vuelta mientras haya dedo,
    # porque la linea del recolocado tiene que seguir al dedo mientras se
    # arrastra. O sea que un toque de dos decimas son muchas llamadas.
    #
    # Los cuatro avisos del cartel -«La carta se deja como esta», «No se
    # puede leer la carta», «Ahora no se puede: se esta guardando» y «No se
    # ha podido recolocar»- salen de pulsar un boton, y lo primero que hace
    # wfxr_toca() con un aviso puesto es retirarlo. Con el dedo todavia
    # encima, el aviso vivia UNA vuelta del bucle.
    #
    # Dos cosas, y las dos se pueden mirar:
    #
    #   1. wfxr_toca() se llama desde wfxr_dedo() y desde ningun otro sitio.
    #      Un segundo llamante tendria que inventarse el `nuevo`, y lo
    #      natural es pasarle un 1;
    #   2. y la rama del aviso pide `nuevo`.
    llam = [m.start() for m in re.finditer(r'\bwfxr_toca\s*\(', src)]
    # La declaracion adelantada y la definicion no cuentan como llamadas: se
    # reconocen por el tipo que llevan delante, no por lo que llevan detras
    # -la llamada de verdad tambien acaba en `;`-.
    llam = [q for q in llam if not src[:q].rstrip().endswith('static uint8_t')]
    cuerpo_d = re.search(r'static uint8_t wfxr_dedo\(uint8_t pulsado[^)]*\)\s*\{(.*?)\n\}',
                         src, re.S)
    if not cuerpo_d:
        print('  no encuentro wfxr_dedo(): es la unica puerta del cartel al')
        print('  tactil, y sin ella el `nuevo` se lo inventa quien llame. MAL')
        malos += 1
    else:
        a0 = cuerpo_d.start(1)
        a1 = cuerpo_d.end(1)
        fuera = [q for q in llam if not (a0 <= q < a1)]
        if fuera:
            for q in fuera:
                print('  linea %d: wfxr_toca() llamada desde fuera de'
                      % (src[:q].count('\n') + 1))
                print('  wfxr_dedo(): el `nuevo` deja de significar "el dedo')
                print('  acaba de bajar".   MAL')
            malos += 1
        elif not llam:
            print('  nadie llama a wfxr_toca(): esta comprobacion ha dejado')
            print('  de medir nada.   MAL')
            malos += 1

    cuerpo_t = re.search(r'static uint8_t wfxr_toca\(uint16_t x, uint16_t y,'
                         r'\s*uint8_t nuevo\)\s*\{(.*?)\n\}\n', src, re.S)
    if not cuerpo_t:
        print('  no encuentro wfxr_toca(x, y, nuevo): sin el tercer argumento')
        print('  el aviso lo borra el dedo que lo puso.   MAL')
        malos += 1
    else:
        dicho = re.search(r'if\s*\(\s*s_wfxr\s*==\s*WFXR_DICHO\s*\)\s*\{(.*?)\n    \}',
                          cuerpo_t.group(1), re.S)
        if not dicho:
            print('  wfxr_toca() ya no retira el aviso: esta comprobacion ha')
            print('  dejado de medir nada.   MAL')
            malos += 1
        elif not re.search(r'if\s*\(\s*nuevo\s*\)\s*\{?\s*wfxr_cierra',
                           dicho.group(1)):
            print('  wfxr_toca() retira el aviso sin pedir dedo NUEVO: el')
            print('  mismo dedo que lo puso lo borra en la vuelta siguiente,')
            print('  o sea en unos milisegundos.   MAL')
            malos += 1

    #
    # Y QUE EL BOTON DE GUARDAR SEA UN INTERRUPTOR - 21/10/2026.
    #
    # wfx_guarda.h promete que el aviso de que no hay sitio se queda puesto
    # "hasta que el usuario toque el boton, QUE ES LO QUE LIMPIA ESE AVISO",
    # y el manual dice que el boton "lo pones y abre un fichero". Las dos
    # cosas eran falsas: nadie limpiaba s_gsv_fallo -con el disco lleno, el
    # boton del fax quedaba muerto hasta salirse del modo- y el cerrojo de
    # los 25 segundos, que esta para las riadas de cierres AUTOMATICOS,
    # paraba tambien al dedo.
    #
    # sim/fax_guarda.c llevaba desde el 20/10 modelando la primera mitad y
    # dando verde: el banco probaba una linea que el firmware no tenia.
    rama = re.search(r'if\s*\(wefax_activo\(\)\)\s*\{(.*?)\n        \}\s*else\s*\{'
                     r'\s*\n\s*s_sstv_guarda', src, re.S)
    if not rama:
        print('  no encuentro la rama del boton de guardar del fax.   MAL')
        malos += 1
    else:
        for v, por in (('s_gsv_fallo',
                        'con el disco lleno el boton queda muerto hasta'
                        ' salirse del modo'),
                       ('s_wfx_hubo',
                        'quitarlo y ponerlo deja 25 s sin grabar, con la'
                        ' chapa diciendo "listo"')):
            if not re.search(r'\b' + v + r'\s*=\s*0U\s*;', rama.group(1)):
                print('  el boton de guardar del fax no pone %s a cero:' % v)
                print('  %s.   MAL' % por)
                malos += 1

    cuerpo_c = re.search(r'static uint8_t capt_pide\(void\)\s*\{(.*?)\n\}', src, re.S)
    if not cuerpo_c or 'wfxr_obra' not in cuerpo_c.group(1):
        print('  capt_pide() no mira wfxr_obra(): la captura de pantalla usa')
        print('  el mismo s_gsv_buf.   MAL')
        malos += 1

    print('  %d cierres con dueño y %d a secas, los de la lista' % (n_dueno, n_secas))
    print('  y la pregunta de recolocar la arma el envoltorio, no el cuerpo')
    print('  y la reescritura se queda el buffer antes de usarlo')
    print('  y el cerrojo se arma en gsv_cierra() y en ningun otro sitio')
    print('  y nadie borra la zona del panel con el cartel puesto')
    print('  y entrar en el modo en el que ya estas no tira la carta')
    print('  y la recursion de gsv_cierra() sigue acotada en un marco')
    print('  y el aviso no se pinta encima del cartel de recolocar')
    print('  y un aviso del cartel no lo borra el dedo que lo puso')
    print('  y el boton de guardar del fax es un interruptor de verdad')
    for f, por in sorted(PERMITIDOS.items()):
        print('      %-18s %s' % (f + '()', por))
    if malos:
        print('  *** %d FALLOS ***' % malos)
        return 1
    print('  nadie cierra el fichero de otro  ok')
    return 0


sys.exit(main())
