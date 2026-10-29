#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
EL QUE COJA LA RAM PRESTADA, QUE PREGUNTE ANTES - 18/10/2026.

LO QUE PASO. Once sitios se reparten los 60.304 bytes que la cascada presta
cuando no se esta mirando (ver hfdl_ram.c y ft8_shared_ram.h), y el cerrojo
que los reparte es UN BIT: `s_cogida`. Sin cuenta de anidamiento, o sea que
el que suelta, suelta para todos. Y soltar no es solo bajar el bit:
hfdl_ram_suelta() llama a waterfall_init(), que es un memset de esos 60 kB.

Los que la cogen son de dos clases, y la diferencia es TODO:

  PRESTATARIOS. La cogen dentro de su poll, la usan y la sueltan en la misma
  llamada. Corren en TODAS las vueltas del bucle principal, pase lo que
  pase, tambien mientras otro la tiene. Son hfdl, wspr, ais, ale, jtty,
  stanag e ident. Estos TIENEN que preguntar:

      if (waterfall_prestada()) { return 0; }   /* otro modo la tiene */
      hfdl_ram_coge();

  DUEÑOS. La cogen al entrar en su modo y no la sueltan hasta salir. Solo se
  llega ahi desde el cambio de modo, con el modo anterior ya pasado por su
  stop(), que es quien suelta; y los modos son excluyentes. Son sstv, hell,
  dsc y la carga del mapa en el arranque. Estos NO preguntan, y es correcto:
  preguntar les haria no arrancar por una memoria que es suya.

EL FALLO. ident.c era un prestatario escrito como si fuera dueño: cogia sin
preguntar. Y es justo el que main.c llama PEGADO a stanag_modo_poll(), en la
misma vuelta, asi que con los dos modos andando los dos trabajaban sobre la
misma memoria y el primero que acababa se la borraba al otro - mas 60 kB de
memset por vuelta. Desde fuera: una radio que deja de coger las pulsaciones.

POR QUE UN GUION Y NO UN COMENTARIO. Porque el comentario ya estaba, seis
veces, en los seis prestatarios que si preguntaban - y el septimo se escribio
igual sin mirarlos. Lo que distingue "esta escrito" de "se cumple" es alguien
que lo compruebe en cada tanda.

QUE COMPRUEBA. Que toda llamada a hfdl_ram_coge() tenga, en las quince
lineas de ENCIMA, una de estas dos cosas:

  - una consulta a waterfall_prestada()   (prestatario), o
  - la marca "DUENO DEL PRESTAMO"         (dueño), que obliga a escribir el
    porque al lado.

Y QUE LA LISTA DE DUEÑOS NO CREZCA SOLA. Esto es lo que de verdad sujeta la
marca. Leyendo el fichero NO se puede saber si uno es dueño o prestatario:
los dos lo escriben igual, y stanag e ident ni siquiera cogen la memoria
dentro de su poll sino en un ram_coge() auxiliar, asi que mirar el nombre de
la funcion no distingue nada - se probo, y dejaba pasar justo los dos casos
que importan. Un banco que aprueba cuando no ha podido medir es peor que no
tenerlo. Asi que los cuatro dueños estan escritos aqui abajo con nombre y
apellido: poner la marca en un quinto sitio hace fallar la tanda, y quitarla
de uno de los cuatro, tambien.

Quince lineas porque los siete prestatarios de hoy lo tienen entre una y seis
antes, y no hay razon para apurar mas: si alguien necesita mas distancia, lo
que necesita es una funcion aparte.

MIDE EL CODIGO, NO LOS COMENTARIOS, y hubo que aprenderlo aqui mismo. La
primera version buscaba waterfall_prestada() en el texto crudo de esas quince
lineas, y los cuatro dueños llevan en su comentario la frase "aqui no se
pregunta waterfall_prestada()". O sea que los cuatro salian aprobados por
decir que NO lo hacen. Es el mismo fallo de medida de siempre: el banco leia
la promesa en vez del hecho. Ahora la consulta se busca en el fichero con los
comentarios tachados, y la marca de dueño solo en los comentarios.

NO MIRA hfdl_ram.c, que es quien define el cerrojo, ni los bancos del
simulador, que se fabrican su propia RAM y no tocan la cascada.

    python3 tools/prestamo_check.py .
"""
import os
import re
import sys

MIRA = ('.c',)
FUERA = ('/build/', '/build_470/', '/out/', '/.git/', '/__pycache__/',
         '/CMSIS/', '/Firmware/', '/usb/', '/Ft8Lib/', '/vendor/',
         '/_archivo/', '/rescate/', '/deepsdr-ui/')

# El fichero que DEFINE el cerrojo no puede preguntarse a si mismo.
SALTA = ('hfdl_ram.c',)

COGE = re.compile(r'\bhfdl_ram_coge\s*\(')
PREGUNTA = re.compile(r'\bwaterfall_prestada\s*\(')
DUENO = re.compile(r'DUE.?O DEL PRESTAMO')

# Una definicion de funcion empieza en la columna 0 y lleva el parentesis.
CABECERA = re.compile(r'^[A-Za-z_][A-Za-z0-9_ \*]*\b([A-Za-z_][A-Za-z0-9_]*)\s*\(')

# LOS CUATRO DUEÑOS, uno por uno. Ver la cabecera: la marca sola no basta.
DUENOS_CONOCIDOS = {
    ('User/sstv.c', 'sstv_start'),   # cambio de modo; suelta en sstv_stop()
    ('User/hell.c', 'hell_start'),   # cambio de modo; suelta en hell_stop()
    ('User/dsc.c', 'dsc_start'),     # cambio de modo; suelta en dsc_stop()
    ('User/main.c', 'mapa_al_arrancar'),   # arranque, antes del bucle
    #
    # IDENT ENTRA EN LA LISTA LA MISMA TARDE QUE SE ESCRIBIO ESTE GUION.
    # Por la mañana le puse la guardia de prestatario y rompi la radio: su
    # ram_coge() se llama desde ident_modo_start(), una vez, y la memoria no
    # se suelta hasta ident_modo_stop(). Es dueño, como los cuatro de
    # arriba. Lo mire para sstv, hell y dsc y no lo mire para este.
    #
    #
    # IDENT ENTRA, y el 18/10 me costo la tarde entenderlo: su ram_coge()
    # se llama UNA vez, desde ident_modo_start(), y la memoria no se suelta
    # hasta ident_modo_stop(). Es dueño. Esa mañana le puse la guardia de
    # prestatario y la radio dejo de casar ninguna fila de la tabla de
    # modos - el mismo fallo que ya habia pasado con el DSC-.
    ('User/ident.c', 'ram_coge'),          # ident_modo_start(); suelta en stop()
    #
    # EL FAX ENTRA EL 29/10/2026, Y LLEVABA UN DIA PONIENDO ESTO EN ROJO.
    #
    # wefax_start() lleva la marca de dueño desde que el fax entro en el
    # reparto de esta memoria -la V6.10- y nadie lo apunto aqui. Este guion
    # decia en rojo, con razon, que alguien se habia puesto la marca sin
    # pasar por la lista. Y como no cuelga de "comprueba", nadie lo leyo.
    #
    # Es dueño de verdad: su hfdl_ram_coge() se llama una vez, desde
    # wefax_start(), y no se suelta hasta wefax_stop(). Lo que SI estaba mal
    # era el sitio de ese arranque dentro de la rejilla de modos -cogia la
    # memoria antes de que el modo anterior la soltara-, y eso se arreglo
    # hoy bajandolo con los demas. Ser dueño y estar en el sitio correcto
    # son dos cosas distintas y las dos hacian falta.
    ('User/wefax.c', 'wefax_start'),       # wefax_start(); suelta en wefax_stop()
    #
    # STANAG NO ENTRA, y es util saber por que: su ram_coge() tambien se
    # llama solo desde stanag_modo_start(), o sea que tambien es dueño,
    # pero ADEMAS lleva la consulta puesta de antes. Con ella pasa este
    # guion por el camino del prestatario y se queda como esta: cambiarselo
    # hoy, con la radio recien arreglada, seria meter otra variable.
}

VENTANA = 15


def sin_comentarios(txt):
    """El fichero con los comentarios cambiados por espacios.

    Conserva los saltos de linea para que la numeracion siga valiendo, y no
    se come lo que haya dentro de una cadena: "/*" dentro de comillas no
    abre comentario.
    """
    out = []
    i, n = 0, len(txt)
    while i < n:
        c = txt[i]
        if c == '"' or c == "'":
            q = c
            out.append(c)
            i += 1
            while i < n:
                out.append(txt[i])
                if txt[i] == '\\' and i + 1 < n:
                    out.append(txt[i + 1])
                    i += 2
                    continue
                if txt[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if txt.startswith('/*', i):
            j = txt.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join('\n' if x == '\n' else ' ' for x in txt[i:j]))
            i = j
            continue
        if txt.startswith('//', i):
            j = txt.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def funcion_de(lineas, i):
    """El nombre de la funcion en la que cae la linea i, o '' si no se ve."""
    j = i
    while j >= 0:
        m = CABECERA.match(lineas[j])
        if m:
            return m.group(1)
        j -= 1
    return ''


def main(raiz):
    malos = []
    duenos = []
    sitios = 0
    for base, dirs, ficheros in os.walk(raiz):
        r = base.replace('\\', '/') + '/'
        if any(x in r for x in FUERA):
            continue
        for f in sorted(ficheros):
            if not f.endswith(MIRA) or f in SALTA:
                continue
            p = os.path.join(base, f)
            try:
                txt = open(p, encoding='utf-8', errors='replace').read()
            except OSError:
                continue
            lineas = txt.split('\n')
            codigo = sin_comentarios(txt).split('\n')
            for i in range(len(lineas)):
                # Una llamada, no el nombre dentro de un comentario.
                if not COGE.search(codigo[i]):
                    continue
                sitios += 1
                desde = max(0, i - VENTANA)
                if PREGUNTA.search('\n'.join(codigo[desde:i])):
                    continue
                if DUENO.search('\n'.join(lineas[desde:i])):
                    duenos.append((p, i + 1, funcion_de(lineas, i)))
                    continue
                malos.append((p, i + 1, 'ni pregunta ni se declara dueña'))

    rel = lambda x: os.path.relpath(x, raiz).replace(os.sep, '/')
    vistos = set((rel(p), fn) for p, lin, fn in duenos)
    nuevos = [(p, lin, fn) for p, lin, fn in duenos
              if (rel(p), fn) not in DUENOS_CONOCIDOS]
    idos = sorted(DUENOS_CONOCIDOS - vistos)

    print('  %d sitios cogen la RAM prestada de la cascada'
          ' (%d de ellos dueños)' % (sitios, len(duenos)))
    if sitios == 0:
        print('  MAL: no he encontrado ninguno, y hay once. Algo no cuadra')
        print('       en este guion o alguien ha renombrado hfdl_ram_coge().')
        return 1

    # Los tres se cuentan enteros y se dicen todos: quitarle la marca a un
    # dueño es a la vez "este coge sin decir nada" y "falta uno de la lista",
    # y las dos frases juntas se entienden mejor que cualquiera de las dos.
    mal = 0

    if malos:
        print('\n  COGEN SIN PREGUNTAR SI OTRO MODO LA TIENE:\n')
        for q, lin, por in malos:
            print('    %s:%d  %s' % (rel(q), lin, por))
        print('\n  El cerrojo de hfdl_ram.c es un bit, sin anidamiento: el')
        print('  que suelta, suelta para todos, y ademas borra los 60 kB.')
        print('  Si esto corre en un poll, antes de hfdl_ram_coge() va, como')
        print('  en los otros siete:')
        print('\n      if (waterfall_prestada()) { return 0; }   /* otro modo la tiene */\n')
        print('  Y si de verdad es dueña de la memoria -solo se llega desde')
        print('  el cambio de modo o desde el arranque-, se dice ahi mismo')
        print('  con "DUENO DEL PRESTAMO" y el porque, y ademas se apunta en')
        print('  la lista DUENOS_CONOCIDOS de este guion.')
        mal += len(malos)

    if nuevos:
        print('\n  SE DECLARAN DUEÑOS DE LA MEMORIA Y NO ESTAN EN LA LISTA:\n')
        for q, lin, fn in nuevos:
            print('    %s:%d  %s()' % (rel(q), lin, fn))
        print('\n  Dueño es el que la coge al ENTRAR en su modo y no la')
        print('  suelta hasta salir, y al que solo se llega desde el cambio')
        print('  de modo o desde el arranque. Si de verdad lo es, va a la')
        print('  lista DUENOS_CONOCIDOS de este guion, con el porque al')
        print('  lado. Si lo que hace es cogerla en cada vuelta, entonces es')
        print('  prestatario y lo que le falta es preguntar.')
        mal += len(nuevos)

    if idos:
        print('\n  ESTABAN EN LA LISTA DE DUEÑOS Y YA NO SE VEN:\n')
        for f, fn in idos:
            print('    %s  %s()' % (f, fn))
        print('\n  O se ha renombrado la funcion, o ese sitio ya no coge la')
        print('  memoria, o se le ha caido la marca. Las dos primeras estan')
        print('  bien; lo que no vale es que la lista de este guion siga')
        print('  diciendo lo de antes.')
        mal += len(idos)

    if mal:
        print('\n  *** %d ***' % mal)
        return 1
    print('  todos preguntan antes de coger, o dicen por que no  ok')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '.'))
