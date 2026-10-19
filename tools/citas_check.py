#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE NO SE PUBLIQUEN CITAS DE CONVERSACIONES.

  python3 tools/citas_check.py [raiz]      (por defecto, el directorio actual)

POR QUE EXISTE - 17/10/2026.

Los comentarios de este proyecto explican POR QUE existe cada cosa, y
durante meses esa explicacion se escribio reproduciendo entre comillas lo
que se pedia o lo que se veia fallar. Eso convierte el repositorio en un
registro de conversaciones privadas, que no es lo que nadie viene a leer.

La limpieza se hizo a mano y costo lo suyo. Lo que costo MAS fue que se
hizo tres veces: cada vez se buscaba la forma concreta que uno recordaba
-primero la atribucion en castellano, luego la misma partida en dos
lineas, luego su gemela inglesa- y cada vez quedaba dentro un tercio de
las que habia. Una busqueda que persigue la
forma que uno recuerda confirma lo que ya sabe y no enseña lo que falta.

Asi que esto NO busca frases: busca la FORMA.

  1. Pega las lineas del comentario, quitando el ' * ' de continuacion. Una
     cita partida en dos lineas es invisible para un grep y por ahi se
     colaron diecisiete.
  2. Marca lo entrecomillado que viva en un comentario con atribucion -en
     los dos idiomas- o que suene a alguien hablando en primera persona.
  3. Y NO MIRA los textos de la pantalla de la radio, que son literales del
     codigo y se citan en los comentarios con todo el derecho. Se sacan del
     propio arbol, no de una lista escrita a mano: una lista a mano se
     queda vieja y entonces este banco empieza a mentir en la direccion
     peor, dando por cita un rotulo y por rotulo una cita.

LO QUE NO PUEDE HACER: una frase de otro escrita SIN comillas, integrada en
la prosa del comentario, no se distingue de texto propio. No hay medida
para eso, y por eso esta escrito aqui en vez de dejarlo sobreentendido.
"""
import os
import re
import sys

EXT = ('.c', '.h', '.py', '.mk', '.ps1', '.js', '.md', '.txt', '.ld', '.S',
       '.inc', '.json', '.yml', '.cfg', '.ini', '.bat', '.sh')

# Lo que no es nuestro, lo que se genera, y lo que no se publica.
FUERA = ('/build/', '/build_470/', '/out/', '/.git/', '/__pycache__/',
         '/sim/muestras/', '/sim/aire/', '/Ft8Lib/', '/CMSIS/', '/usb/',
         '/vendor/', '/_archivo/', '/rescate/')

ATRIB = re.compile(
    r'(usuario del proyecto|project owner|the owner|owner\'s|'
    r'el usuario (dice|dijo|pide|pidio|conto|reporto|veia|vio)|'
    #
    # LA ATRIBUCION DE LOS .md, QUE SE NOS ESCAPO ENTERA - 18/10/2026.
    #
    # Este banco se colgo de "make comprueba", que mira SOLO la carpeta del
    # firmware. docs/ y manual/ son repositorios hermanos y quedaron fuera,
    # asi que en docs/CAMBIOS.md habia 94 citas. Un guardian que no mira
    # donde esta el problema no es una red: es un fichero, otra vez.
    #
    # Y ademas no las habria visto estando alli: la forma que usan los .md
    # es "*Por el usuario:*" / "*El usuario, con una foto:*", y arriba se
    # pedia el verbo detras ("el usuario dijo"). Dos agujeros distintos, el
    # de donde se mira y el de que se busca, y los dos daban verde.
    #
    # SIN espacio detras del asterisco, y hace falta: en un comentario de C
    # la linea empieza por " * " y entonces " * Pedido por el usuario" -que
    # es la forma correcta y limpia- se daba por atribucion. Lo marco en
    # sstvtest.c, donde lo entrecomillado es la DEFINICION de un campo. En
    # markdown el asterisco va pegado a la palabra: *Por el usuario:*.
    r'\*\*?(el|por el|del|pedido por el)\s+usuario\b|'
    r'de un issue|del issue|the issue|from an issue|de un informe|'
    r'del informe|de un video|por un informe|la otra mitad del|'
    r'nos reportaron|a peticion de|se quejo|transcripcion|por el canal|'
    r'el que lo abrio|la peticion que llego)', re.I)

HABLA = re.compile(
    r'\b(no me |me pone|me sale|se me |he borrado|he dejao|le doy|'
    r'esta radio va|la radio se (cuelga|queda|reinicia)|siempre que arranco|'
    r'cuando pongo|yo sepa|no tengo claro|coñazo|cutre|emm)', re.I)

CHARLA = re.compile(
    r'(la conversacion de|media conversacion|en el chat|nuestras? charlas?|'
    r'conversacion de porte|port conversation|chat log|'
    r'con su transcripcion)', re.I)

#
# DE DONDE VINO EL MATERIAL - 17/10/2026.
#
# No es una cita ni una referencia a una conversacion, asi que las dos de
# arriba lo dejaban pasar, pero cuenta lo mismo: "el video del usuario",
# "la grabacion que me paso", "la foto que mando el usuario" dicen COMO
# llego el dato, y eso es la conversacion. Salieron 45 sitios de una vez.
#
# La frase correcta es el HECHO: "la grabacion de referencia", "lo que se
# veia en pantalla", "los 214 segundos de la grabacion por CAT". El dato
# medido se queda entero; lo que se va es el recado.
#
# LO QUE NO MARCA, Y ES A PROPOSITO: "el disco del usuario", "el QTH del
# usuario", "el ajuste del usuario", "la radio del usuario". Ahi "el
# usuario" es quien usa la radio -o su equipo-, no quien escribio pidiendo
# algo, y son la mitad de los "del usuario" del arbol. La regla pide un
# SOPORTE (video, foto, grabacion, captura, audio) o un VERBO de traer
# (mandar, pasar, enseñar), que es lo que distingue una cosa de la otra.
#
# AFINADA una vez: pedia el soporte seguido del verbo, y "la fila de audio
# paso a llevar cuatro numeros" encaja con eso sin ser un recado. Ahora el
# verbo tiene que venir con "que" o con quien lo trajo: "el video QUE mando",
# "la grabacion que me paso". Un "paso a" detras de un sustantivo ya no
# cuenta.
PROCEDENCIA = re.compile(
    r'((?:v[ií]deo|foto|captura|grabaci[oó]n|audio|pantallazo)s?'
    r'\s+que\s+(?:me\s+)?(?:mand[oó]|pas[oó]|ense[nñ][oó]|envi[oó])|'
    r'(?:v[ií]deo|foto|captura|pantallazo)s?\s+del\s+usuario|'
    r'el\s+usuario\s+(?:mand[oó]|pas[oó]|ense[nñ][oó]|grab[oó]|envi[oó])|'
    r'que\s+me\s+(?:mand[oó]|pas[oó]|ense[nñ][oó]|envi[oó])|'
    r'(?:video|photo|screenshot|recording)\s+(?:the\s+)?(?:user|owner)\s+sent|'
    r'sent\s+by\s+the\s+(?:user|owner))', re.I)


def comentarios_c(t):
    """[(linea, texto)] de cada comentario. Las cadenas del CODIGO se saltan:
    un rotulo de pantalla no es un comentario y no se toca nunca."""
    out, i, n = [], 0, len(t)
    while i < n:
        if t.startswith('/*', i):
            j = t.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append((t[:i].count('\n') + 1, t[i:j]))
            i = j
        elif t.startswith('//', i):
            j = t.find('\n', i)
            j = n if j < 0 else j
            out.append((t[:i].count('\n') + 1, t[i:j]))
            i = j
        elif t[i] == '"':
            j = i + 1
            while j < n and t[j] != '"':
                j += 2 if t[j] == '\\' else 1
            i = j + 1
        elif t[i] == "'":
            j = i + 1
            while j < n and t[j] != "'":
                j += 2 if t[j] == '\\' else 1
            i = j + 1
        else:
            i += 1
    return out


def comentarios_script(t):
    """Los docstrings y las lineas de #."""
    out = []
    for m in re.finditer(r'"""(.*?)"""|\'\'\'(.*?)\'\'\'', t, re.S):
        out.append((t[:m.start()].count('\n') + 1, m.group(0)))
    for k, l in enumerate(t.split('\n'), 1):
        s = l.lstrip()
        if s.startswith('#') or s.startswith('<#'):
            out.append((k, l))
    return out


def literales_codigo(t):
    """Las cadenas del codigo, que son los textos de la pantalla."""
    out, i, n = [], 0, len(t)
    while i < n:
        if t.startswith('/*', i):
            j = t.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif t.startswith('//', i):
            j = t.find('\n', i)
            i = n if j < 0 else j
        elif t[i] == '"':
            j = i + 1
            while j < n and t[j] != '"':
                j += 2 if t[j] == '\\' else 1
            out.append(t[i + 1:j])
            i = j + 1
        elif t[i] == "'":
            j = i + 1
            while j < n and t[j] != "'":
                j += 2 if t[j] == '\\' else 1
            i = j + 1
        else:
            i += 1
    return out


def pega(tr):
    """Las lineas del comentario, juntas. Una linea EN BLANCO separa
    parrafos y se conserva: sin eso, el final de un parrafo y el principio
    del siguiente parecen una frase cortada."""
    out = []
    for l in tr.split('\n'):
        s = re.sub(r'^\s*(\*|#|//)\s?', '', l).strip()
        out.append('\n' if s == '' else s)
    return re.sub(r' *\n *', '\n', ' '.join(out))


def ficheros(raiz):
    for base, dirs, fs in os.walk(raiz):
        r = base.replace(os.sep, '/') + '/'
        if any(f in r for f in FUERA):
            dirs[:] = []
            continue
        for f in sorted(fs):
            p = os.path.join(base, f)
            # Los ficheros SIN EXTENSION tambien: .gitignore llevaba tres
            # citas dentro y la primera busqueda ni lo miraba.
            #
            # Y OJO CON COMO SE MIRA ESO: la primera version preguntaba
            # si el nombre lleva un punto, y '.gitignore' LLEVA punto -es
            # todo el nombre-, asi que se seguia saltando exactamente el
            # fichero por el que se escribio esta linea. Se ve con
            # splitext, que separa nombre de extension de verdad.
            ext = os.path.splitext(f)[1]
            if ext and ext not in EXT:
                continue
            try:
                if b'\x00' in open(p, 'rb').read(2048):
                    continue
            except OSError:
                continue
            yield p


def main():
    raiz = sys.argv[1] if len(sys.argv) > 1 else '.'

    pantalla = set()
    for p in ficheros(raiz):
        if not p.endswith(('.c', '.h', '.S', '.inc')):
            continue
        try:
            t = open(p, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        for s in literales_codigo(t):
            s = s.strip()
            if len(s) >= 3:
                pantalla.add(s)
            # y sus trozos: un comentario cita a veces media etiqueta
            for trozo in re.split(r'[\t\\n|/]', s):
                trozo = trozo.strip()
                if len(trozo) >= 3:
                    pantalla.add(trozo)

    malos = []
    mirados = 0
    for p in ficheros(raiz):
        try:
            t = open(p, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        # El propio banco no se mira: sus expresiones y su documentacion
        # hablan de lo que persigue, asi que se encontraria a si mismo y
        # sacaria dos fallos que no lo son. Es el unico fichero exento, y
        # lo es por ser el que define la regla.
        if os.path.basename(p) == os.path.basename(__file__):
            continue
        mirados += 1
        es_c = p.endswith(('.c', '.h', '.S', '.inc'))
        for lin, c in (comentarios_c(t) if es_c else comentarios_script(t)):
            pg = pega(c)
            m = CHARLA.search(pg)
            if m:
                malos.append((p, lin, 'referencia a una conversacion',
                              m.group(0)))
            m = PROCEDENCIA.search(pg)
            if m:
                malos.append((p, lin, 'de donde vino el material',
                              m.group(0)))
            if '"' not in c:
                continue
            for q in re.finditer(r'"([^"\n]{4,250})"', pg):
                s = q.group(1).strip()
                if s in pantalla:
                    continue
                # La atribucion tiene que estar PEGADA -80 caracteres por
                # delante-, no en cualquier parte del comentario. Y una
                # cita es una frase: un termino tecnico de una o dos
                # palabras entre comillas no lo es.
                cerca = pg[max(0, q.start() - 80):q.start()]
                frase = len(s.split()) >= 3
                if (ATRIB.search(cerca) and frase) or HABLA.search(s):
                    malos.append((p, lin, 'cita', s))

    rel = lambda x: os.path.relpath(x, raiz).replace(os.sep, '/')
    print('  %d ficheros mirados, %d textos de pantalla protegidos'
          % (mirados, len(pantalla)))
    if malos:
        print('\n  *** %d POSIBLES CITAS ***' % len(malos))
        for p, lin, que, s in malos:
            print('    %s:%d  %s' % (rel(p), lin, que))
            print('        %s' % s[:140])
        print('\n  Si alguna es un termino tecnico y no una cita, la forma')
        print('  de callarla NO es ampliar una lista blanca aqui: es quitarle')
        print('  las comillas en el comentario, que es lo que la hace')
        print('  parecer una cita.')
        return 1
    print('  ninguna cita de conversaciones  ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
