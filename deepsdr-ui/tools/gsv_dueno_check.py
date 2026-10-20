#!/usr/bin/env python3
"""
Que nadie cierre el fichero de otro.

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
    # Asi que s_wfx_hubo y s_wfx_ult solo se escriben dentro de
    # gsv_cierra(). En cualquier otro sitio vuelve a depender de que
    # alguien se acuerde.
    n_cerrojo = 0
    for m in re.finditer(r'\b(s_wfx_hubo|s_wfx_ult)\s*=[^=]', src):
        f = funcion_de(src, m.start())
        if f == 'gsv_cierra':
            n_cerrojo += 1
            continue
        linea = src[:m.start()].count('\n') + 1
        print('  linea %d: %s se escribe dentro de %s()' % (linea, m.group(1), f))
        print('    El cerrojo lo arma el CIERRE, no el que cierra. Si lo arma')
        print('    quien llama, basta un cerrador que no se acuerde para que no')
        print('    se arme nunca - que es justo lo que paso.   MAL')
        malos += 1
    if n_cerrojo < 2:
        print('  gsv_cierra() no arma el cerrojo (s_wfx_hubo/s_wfx_ult).')
        print('  Sin eso, un cerrador desconocido vuelve a poder hacer una')
        print('  riada de ficheros.   MAL')
        malos += 1

    print('  %d cierres con dueño y %d a secas, los de la lista' % (n_dueno, n_secas))
    print('  y el cerrojo se arma en gsv_cierra() y en ningun otro sitio')
    for f, por in sorted(PERMITIDOS.items()):
        print('      %-18s %s' % (f + '()', por))
    if malos:
        print('  *** %d FALLOS ***' % malos)
        return 1
    print('  nadie cierra el fichero de otro  ok')
    return 0


sys.exit(main())
