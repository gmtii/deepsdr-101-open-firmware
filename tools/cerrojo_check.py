#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
EL CERROJO DE WEFAX, COMPROBADO - 21/10/2026.

User/wefax.c tiene dos lados: wefax_process() corre DENTRO de la
interrupcion de audio y el resto corre en el bucle principal. Su cabecera
lo explica entero y pone dos reglas; este guion las comprueba, porque las
dos se han roto ya y las dos se rompen en silencio:

  1. LO QUE ESCRIBEN LOS DOS LADOS ES `volatile`. Sin eso, a -O2 el
     compilador ve que nadie fuera del fichero toca la variable, se la
     guarda en un registro durante las N muestras del bloque y la vuelca al
     salir: el cero que acaba de escribir el bucle DESAPARECE. Asi se
     perdia el boton de Sincroniza.

  2. Y EL BUCLE LAS ESCRIBE DENTRO DE WFX_CIERRA()/WFX_ABRE(). `volatile`
     obliga a ir a memoria pero no hace atomico un `lista--`, que en un
     Cortex-M4 son tres instrucciones; y menos aun hace atomico mover tres
     indices que tienen un invariante entre ellos.

  3. Y LO QUE ESCRIBE LA INTERRUPCION Y SOLO LEE EL BUCLE, o no se puede
     partir al leerlo o se lee con el cerrojo echado. Ese sentido estaba
     FUERA del guion, con una nota que decia que ahi solo estaba el anillo
     de lineas y que se leia bajo cerrojo. Ni una cosa ni la otra: tambien
     estan el contador de lineas y la posicion del pulso, y las dos se leen
     sueltas. Estan bien -son palabras de 4 bytes, y una palabra alineada
     se lee de una instruccion en un Cortex-M4- pero no por el motivo que
     habia escrito. Un sentido entero dejado fuera con un motivo que no es
     el suyo se queda fuera para siempre: el dia que ahi aparezca un
     contador de 64 bits o una estructura, nadie se entera. Ahora se
     enumera y se dice de cada una CUAL de las tres cosas la tapa.

No comprueba el lado de la interrupcion: ahi no hace falta cerrojo, porque
el bucle principal no puede interrumpir a la interrupcion.

wefax_start() queda FUERA de la regla 2 a proposito y es la unica
excepcion: lo primero que hace es `s_on = 0`, o sea retirar el fichero del
servicio antes de tocar nada. Y s_on y s_estado quedan fuera porque son
banderas sueltas de un solo byte, sin invariante con nadie: una escritura
suelta no puede dejarlas a medias.
"""
import os, re, sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FUENTE = os.path.join(RAIZ, 'User', 'wefax.c')

# La raiz del lado de la interrupcion. demod_am.c la llama desde la ISR de
# audio; de ahi para abajo, todo lo que se alcance es "lado interrupcion".
RAIZ_ISR = ['wefax_process']

# Y las del bucle principal, SIN wefax_start() (ver la cabecera).
#
# ppm_fija() NO esta aqui, y hay que decir por que: el unico que la llama es
# fase_linea(), o sea la interrupcion. Por eso cuenta como lado interrupcion
# y puede escribir s_ppm y s_inc sin cerrojo. Desde el 21/10/2026 es
# ESTATICA, asi que ya no se le puede colgar un mando desde fuera sin
# tocar wefax.c; si alguien lo hace, hay que meterla en esta lista y
# entonces este guion dira que esas dos escrituras se han quedado fuera del
# cerrojo. Que es justo lo que habria que arreglar.
RAIZ_BUCLE = ['wefax_stop', 'wefax_set_lpm', 'wefax_set_lpm_idx',
              'wefax_sincroniza', 'wefax_fuerza_imagen', 'wefax_linea_en',
              'wefax_info', 'wefax_fase_dbg', 'wefax_get_ppm',
              'wefax_get_lpm', 'wefax_get_lpm_idx']

# Banderas sueltas: una escritura, ningun invariante. Ver la cabecera.
SIN_CERROJO = {'s_on', 's_estado'}


def sin_comentarios(src):
    """Comentarios y textos, a espacios, SIN mover un solo byte.

    Hace falta de verdad: este fichero cuenta en sus comentarios lo que
    hace el codigo -"`s_linea_lista--` son tres instrucciones"- y sin
    esto el guion lee esas frases como escrituras y da fallos donde no
    los hay. Un guardian que miente es peor que no tenerlo.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i + 1] == '*':
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(ch if ch == '\n' else ' ' for ch in src[i:j]))
            i = j
        elif c == '/' and i + 1 < n and src[i + 1] == '/':
            j = src.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif c in '"\'':
            q = c
            j = i + 1
            while j < n and src[j] != q:
                j += 2 if src[j] == '\\' else 1
            j = min(j + 1, n)
            out.append(''.join(ch if ch == '\n' else ' ' for ch in src[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def funciones(src):
    """nombre -> (ini_cuerpo, fin_cuerpo), por conteo de llaves."""
    out = {}
    pat = re.compile(r'(?m)^(?:static\s+)?[A-Za-z_][A-Za-z0-9_ \*]*?'
                     r'([A-Za-z_][A-Za-z0-9_]*)\s*\(([^;{)]*)\)\s*\{')
    for m in pat.finditer(src):
        i = src.index('{', m.end() - 1)
        n = 0
        j = i
        while j < len(src):
            if src[j] == '{':
                n += 1
            elif src[j] == '}':
                n -= 1
                if n == 0:
                    break
            j += 1
        out[m.group(1)] = (i + 1, j)
    return out


def alcanzables(raices, llamadas):
    vistos = set()
    pila = list(raices)
    while pila:
        f = pila.pop()
        if f in vistos:
            continue
        vistos.add(f)
        pila.extend(llamadas.get(f, ()))
    return vistos


def tramos_cerrados(cuerpo):
    """Los trozos de `cuerpo` entre WFX_CIERRA() y su WFX_ABRE().

    El WFX_ABRE() que cierra es el de la MISMA profundidad de llaves:
    wefax_linea_en() tiene dos -uno dentro del `if` de la cola vacia, que
    abre para volverse- y cogiendo el primero el tramo acabaria antes de
    las escrituras que el cerrojo existe para proteger.
    """
    prof = [0] * (len(cuerpo) + 1)
    d = 0
    for q, ch in enumerate(cuerpo):
        if ch == '{':
            d += 1
        elif ch == '}':
            d -= 1
        prof[q] = d
    tramos = []
    for mc in re.finditer(r'WFX_CIERRA\s*\(', cuerpo):
        d0 = prof[mc.start()]
        for ma in re.finditer(r'WFX_ABRE\s*\(', cuerpo[mc.end():]):
            q = mc.end() + ma.start()
            if prof[q] == d0:
                tramos.append((mc.start(), q + ma.end() - ma.start()))
                break
    return tramos


DESTINO = re.compile(r'\b(?:memset|memcpy)\s*\(\s*'
                     r'(?:\(\s*[A-Za-z_][A-Za-z0-9_ \*]*\)\s*)?')
# `sizeof x` no lee nada en tiempo de ejecucion: lo resuelve el compilador.
# Sin esto, el `sizeof s_linea` del propio memset que la borra contaba como
# una lectura suelta y tumbaba la regla por donde no es.
SIZEOF  = re.compile(r'\bsizeof\s*\(?\s*$')


def bajo_cerrojo(nombre, src, cuerpos):
    """Si TODAS las veces que el bucle LEE `nombre` caen dentro de un tramo
    cerrado. Una sola suelta y no vale: el cerrojo protege lo que cubre, no
    lo que se le parece.

    Y LEE, no toca. El destino de un memset() o de un memcpy() es una
    ESCRITURA, y de las escrituras se ocupa la regla 2. Hace falta
    distinguirlo de verdad: s_linea[][] se lee bajo cerrojo -el memcpy de
    wefax_linea_en()- y en cambio se BORRA fuera, en linea_reinicia() y en
    wefax_set_lpm(), a proposito y explicado en los dos sitios: son 6 kB,
    o sea decenas de microsegundos con todo cerrado, y no hacen falta
    porque acto seguido las ranuras se declaran vacias dentro del cerrojo.
    Contando ese borrado como lectura, la regla daba en rojo lo unico que
    de verdad esta protegido.
    """
    visto = False
    for f in RAIZ_BUCLE:
        if f not in cuerpos:
            continue
        a, b = cuerpos[f]
        cuerpo = src[a:b]
        tramos = tramos_cerrados(cuerpo)
        destinos = set(m.end() for m in DESTINO.finditer(cuerpo))
        for m in re.finditer(r'\b%s\b' % re.escape(nombre), cuerpo):
            if m.start() in destinos:
                continue                      # es el destino: es escritura
            if SIZEOF.search(cuerpo[:m.start()]):
                continue                      # sizeof: lo resuelve el gcc
            visto = True
            if not any(x <= m.start() < y for x, y in tramos):
                return False
    return visto


def main():
    bruto = open(FUENTE, encoding='utf-8').read()
    src = sin_comentarios(bruto)
    if len(src) != len(bruto):
        print('  el quitacomentarios ha movido bytes: los numeros de linea')
        print('  no valdrian.   MAL')
        return 1
    cuerpos = funciones(src)

    # Las estaticas del fichero y si son volatiles. Una sola linea puede
    # declarar varias -`static volatile float s_apt300, s_apt450;`- y
    # saltarselas era dejar fuera del guion justo las dos que el arreglo
    # de hoy ha tenido que volver volatiles.
    vol = {}
    tipo = {}      # el tipo escrito, para la regla 3
    vector = {}    # y si es un vector
    for m in re.finditer(r'(?m)^static\s+(volatile\s+)?([A-Za-z_][A-Za-z0-9_ ]*?)'
                         r'(\bs_[A-Za-z0-9_]+\s*(?:\[[^;]*?\])?'
                         r'(?:\s*,\s*s_[A-Za-z0-9_]+\s*(?:\[[^;]*?\])?)*)'
                         r'\s*(?:=[^;]*)?;', src):
        for nombre in re.findall(r's_[A-Za-z0-9_]+', m.group(3)):
            vol[nombre] = bool(m.group(1))
            tipo[nombre] = m.group(2).strip()
            vector[nombre] = ('[' in m.group(3).split(nombre, 1)[1][:2]
                              if nombre in m.group(3) else False)
        for d in re.finditer(r'(s_[A-Za-z0-9_]+)\s*(\[?)', m.group(3)):
            vector[d.group(1)] = (d.group(2) == '[')

    # Quien llama a quien, quien escribe que y quien lee que.
    llamadas = {}
    escribe = {}
    toca = {}
    for f, (a, b) in cuerpos.items():
        cuerpo = src[a:b]
        llamadas[f] = set(x for x in re.findall(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(', cuerpo)
                          if x in cuerpos and x != f)
        w = set()
        for m in re.finditer(r'\b(s_[A-Za-z0-9_]+)\s*(?:\[[^\]]*\]\s*)*'
                             r'(\+\+|--|[-+*/|&^]?=(?!=))', cuerpo):
            w.add(m.group(1))
        for m in re.finditer(r'(\+\+|--)\s*(s_[A-Za-z0-9_]+)', cuerpo):
            w.add(m.group(2))
        escribe[f] = w
        toca[f] = set(re.findall(r'\b(s_[A-Za-z0-9_]+)\b', cuerpo))

    isr = alcanzables(RAIZ_ISR, llamadas)
    bucle = alcanzables(RAIZ_BUCLE, llamadas)

    def junta(d, fs):
        r = set()
        for f in fs:
            r |= d.get(f, set())
        return r

    w_isr, w_bucle = junta(escribe, isr), junta(escribe, bucle)
    t_isr, t_bucle = junta(toca, isr), junta(toca, bucle)

    #
    # QUE ES "COMPARTIDA", Y POR QUE NO ES SIMETRICO.
    #
    #   - la escriben los dos lados, o
    #   - la escribe el BUCLE y la lee la interrupcion (s_lpm_i: el bucle
    #     la cambia con el mando y la interrupcion la lee en
    #     tope_lineas_fase() y reloj_calcula()).
    #
    # EL OTRO SENTIDO -la escribe la interrupcion y la lee el bucle- tiene
    # regla propia, la 3, y no una excusa. Aqui ponia que ese sentido era
    # s_linea[][] y nada mas, "que se lee con el cerrojo echado": falso.
    # Son TRES -s_linea[][], s_lineas y s_pulso_pos- y las dos ultimas se
    # leen sin cerrojo y sin volatile. Estan bien, pero no por el motivo
    # que habia escrito, y un guardian que deja algo fuera con un motivo
    # que no es el suyo lo deja fuera para siempre: el dia que ahi aparezca
    # un contador de 64 bits o una estructura, nadie se entera.
    #
    dos_lados = (w_isr & w_bucle) & set(vol)
    solo_bucle = ((w_bucle & t_isr) & set(vol)) - dos_lados
    compartidas = sorted(dos_lados | solo_bucle)
    malos = 0

    print('el cerrojo de User/wefax.c')
    print('  %d funciones, %d estaticas, %d compartidas entre los dos lados'
          % (len(cuerpos), len(vol), len(compartidas)))
    if not compartidas:
        print('  no encuentro ninguna variable compartida: el guion no mide')
        print('  nada.   MAL')
        return 1

    # --- regla 1: volatile ---
    print('\n1. lo que comparten los dos lados es volatile')
    faltan = [v for v in compartidas if not vol[v]]
    for v in faltan:
        print('  %s %s y NO es volatile'
              % (v, 'la escriben los dos lados' if v in dos_lados
                 else 'la escribe el bucle y la lee la interrupcion'))
    if faltan:
        malos += 1
        print('  %d sin volatile.   MAL' % len(faltan))
    else:
        print('  las %d lo son: %s   ok'
              % (len(compartidas), ', '.join(compartidas)))

    # --- regla 2: el bucle las escribe dentro del cerrojo ---
    print('\n2. y el bucle las escribe dentro de WFX_CIERRA()/WFX_ABRE()')
    # Las dos ayudantes sin cerrojo propio: escribirlas es llamarlas.
    ayudantes = [f for f in ('anillo_cero', 'fase_cero') if f in cuerpos]
    sueltas = 0
    for f in sorted(RAIZ_BUCLE):
        if f not in cuerpos:
            continue
        a, b = cuerpos[f]
        cuerpo = src[a:b]
        # Los tramos cerrados de esta funcion.
        #
        # Y EL WFX_ABRE() QUE CIERRA ES EL DE LA MISMA PROFUNDIDAD.
        #
        # wefax_linea_en() tiene DOS: uno dentro del `if` de la cola vacia,
        # que abre para volverse, y el de verdad al final. Cogiendo el
        # primero, el tramo cerrado acababa antes de las dos escrituras que
        # el cerrojo existe para proteger y el guion las daba por sueltas.
        #
        prof = [0] * (len(cuerpo) + 1)
        d = 0
        for q, ch in enumerate(cuerpo):
            if ch == '{':
                d += 1
            elif ch == '}':
                d -= 1
            prof[q] = d
        tramos = []
        for mc in re.finditer(r'WFX_CIERRA\s*\(', cuerpo):
            d0 = prof[mc.start()]
            for ma in re.finditer(r'WFX_ABRE\s*\(', cuerpo[mc.end():]):
                q = mc.end() + ma.start()
                if prof[q] == d0:
                    tramos.append((mc.start(), q + ma.end() - ma.start()))
                    break
        def dentro(pos):
            return any(x <= pos < y for x, y in tramos)

        toca = []
        for m in re.finditer(r'\b(s_[A-Za-z0-9_]+)\s*(?:\[[^\]]*\])?\s*'
                             r'(\+\+|--|[-+*/|&^]?=(?!=))', cuerpo):
            if m.group(1) in compartidas and m.group(1) not in SIN_CERROJO:
                toca.append((m.start(), m.group(1)))
        for h in ayudantes:
            for m in re.finditer(r'\b%s\s*\(' % h, cuerpo):
                toca.append((m.start(), h + '()'))
        for pos, que in sorted(toca):
            if not dentro(pos):
                ren = src[:a + pos].count('\n') + 1
                print('  linea %d: %s() escribe %s fuera del cerrojo'
                      % (ren, f, que))
                sueltas += 1
    if sueltas:
        malos += 1
        print('  %d escrituras sueltas.   MAL' % sueltas)
    else:
        print('  ninguna escritura suelta en las %d entradas del bucle   ok'
              % len([f for f in RAIZ_BUCLE if f in cuerpos]))

    # --- regla 3: lo que escribe la interrupcion y lee el bucle ---
    #
    # No hace falta cerrojo para LEER algo que solo escribe el otro lado si
    # la lectura no se puede partir. En un Cortex-M4 una palabra alineada
    # -hasta 4 bytes- se lee de una instruccion y no se parte; un vector o
    # una estructura, si. Asi que cada una de estas tiene que ser una de
    # tres cosas, y se dice CUAL:
    #
    #   volatile        .. el compilador va a memoria cada vez
    #   bajo cerrojo    .. el bucle la lee dentro de WFX_CIERRA()/ABRE()
    #   una palabra     .. 4 bytes o menos: la lectura no se puede partir
    #
    # Lo que no sea ninguna de las tres sale en rojo.
    PALABRA = ('uint8_t', 'int8_t', 'char', 'uint16_t', 'int16_t',
               'uint32_t', 'int32_t', 'float')
    print('\n3. y lo que escribe la interrupcion y lee el bucle, o no se'
          ' parte o va con cerrojo')
    un_sentido = sorted(((w_isr & t_bucle) - w_bucle) & set(vol))
    sin_tapar = 0
    for v in un_sentido:
        if vol[v]:
            print('  %-14s volatile' % v)
            continue
        if bajo_cerrojo(v, src, cuerpos):
            print('  %-14s se lee bajo cerrojo' % v)
            continue
        if not vector.get(v, False) and tipo.get(v, '') in PALABRA:
            print('  %-14s una palabra (%s): la lectura no se puede partir'
                  % (v, tipo[v]))
            continue
        print('  %s no es volatile, no se lee bajo cerrojo y no cabe en una'
              % v)
        print('    palabra (%s%s): la lectura se puede partir.   MAL'
              % (tipo.get(v, '?'), '[]' if vector.get(v, False) else ''))
        sin_tapar += 1
    if not un_sentido:
        print('  ninguna: ese sentido esta vacio')
    elif sin_tapar:
        malos += 1
    else:
        print('  las %d estan tapadas   ok' % len(un_sentido))

    print('\n  %s' % ('HAY FALLOS' if malos else 'el cerrojo esta entero'))
    return 1 if malos else 0


if __name__ == '__main__':
    sys.exit(main())
