#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
LA GUARDA QUE YA SE DECIDIO MAS ARRIBA - 21/10/2026.

Busca, dentro de una misma funcion, un `if` cuya condicion YA se decidio
antes -misma condicion, misma profundidad de llaves- en un `if` del que se
sale con return/goto/break/continue. Si por el camino nada ha podido
cambiar esa condicion, el segundo `if` no se ejecuta nunca.

POR QUE EXISTE. En la pasada 21 de la revision del fax aparecio esto en
wfxr_acepta():

    if (s_wfxr_x == 0) { wfxr_cierra(); return; }   /* el guarda de antes */
    ... treinta lineas de comentario ...
    if (s_wfxr_x == 0) { ...el aviso nuevo... }     /* el arreglo */

El arreglo se escribio DEBAJO del guarda viejo y no se ejecuto ni una vez.
No es un fallo que se vea leyendo lo que se acaba de escribir -que esta
bien- sino leyendo la funcion entera, que es justo lo que no se hace al
añadir algo a una funcion que ya existia. Y no da ni un aviso del
compilador: las dos ramas son alcanzables para el, que no sabe que la
condicion no ha cambiado.

COMO SE EVITAN LOS FALSOS POSITIVOS. Se pide que la condicion sea una
cuenta y no una pregunta al mundo:

  - ninguna llamada a funcion DENTRO de la condicion. `if (touch_pulsado())`
    dos veces no es un fallo: la segunda puede dar otra cosa;
  - ninguna llamada a funcion ENTRE las dos. Cualquiera puede escribir una
    estatica del fichero;
  - ninguna escritura, ningun ++/-- y ningun `&variable` entre las dos
    sobre los nombres que salen en la condicion;
  - y el `else` de un `else if` no cuenta como "entre las dos".

Con esas cuatro, el arbol entero -User/ y los bancos- da UN solo aviso, que
es el fallo de arriba. Un guardian que avisa de treinta cosas es un
guardian que se apaga.
"""
import os, re, sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

LLAMADA = re.compile(r'[A-Za-z_][A-Za-z0-9_]*\s*\(')
SALE    = re.compile(r'\b(return|goto|break|continue)\b')
ESPACIO = re.compile(r'\s+')


def sin_comentarios(src):
    """Comentarios y textos a espacios, SIN mover un solo byte: los numeros
    de linea que se imprimen tienen que ser los del fichero de verdad."""
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
    """nombre -> [(ini_cuerpo, fin_cuerpo)], por conteo de llaves. Una lista
    y no un par: hay nombres que se repiten entre ficheros."""
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
        out.setdefault(m.group(1), []).append((i + 1, j))
    return out


def parentesis(txt, p):
    """p apunta al '(' de un if. Devuelve (condicion, posicion tras el ')')."""
    d = 0
    j = p
    while j < len(txt):
        if txt[j] == '(':
            d += 1
        elif txt[j] == ')':
            d -= 1
            if d == 0:
                return txt[p + 1:j], j + 1
        j += 1
    return None, len(txt)


def rama(txt, q):
    """q = tras el ')'. Devuelve (cuerpo del if, posicion tras el)."""
    while q < len(txt) and txt[q] in ' \t\n':
        q += 1
    if q >= len(txt):
        return '', q
    if txt[q] == '{':
        d = 0
        j = q
        while j < len(txt):
            if txt[j] == '{':
                d += 1
            elif txt[j] == '}':
                d -= 1
                if d == 0:
                    return txt[q + 1:j], j + 1
            j += 1
        return txt[q + 1:], len(txt)
    j = txt.find(';', q)
    j = len(txt) if j < 0 else j + 1
    return txt[q:j], j


def toca(nombre, txt):
    """Si `txt` escribe `nombre`, o se lleva su direccion."""
    n = re.escape(nombre)
    return bool(re.search(r'\b' + n + r'\s*(?:\[[^\]]*\])?\s*'
                          r'(\+\+|--|[-+*/|&^]?=(?!=))', txt)
                or re.search(r'(\+\+|--)\s*\b' + n + r'\b', txt)
                or re.search(r'&\s*' + n + r'\b', txt))


def revisa(path):
    bruto = open(path, encoding='utf-8').read()
    src = sin_comentarios(bruto)
    if len(src) != len(bruto):
        return None
    avisos = []
    for nom, trozos in funciones(src).items():
        for (a, b) in trozos:
            cuerpo = src[a:b]
            prof = [0] * (len(cuerpo) + 1)
            d = 0
            for q, ch in enumerate(cuerpo):
                if ch == '{':
                    d += 1
                elif ch == '}':
                    d -= 1
                prof[q] = d
            vistos = {}
            for m in re.finditer(r'\bif\s*\(', cuerpo):
                cond, fin = parentesis(cuerpo, m.end() - 1)
                if cond is None:
                    continue
                cn = ESPACIO.sub(' ', cond).strip()
                cuerpo_if, fin_rama = rama(cuerpo, fin)
                clave = (prof[m.start()], cn)
                # Una condicion que llama a algo puede dar otra cosa cada vez.
                if LLAMADA.search(cn):
                    continue
                if clave in vistos:
                    _, ant_fin = vistos[clave]
                    entre = cuerpo[ant_fin:m.start()]
                    cambia = (LLAMADA.search(entre) is not None
                              or entre.strip() == 'else'
                              or any(toca(x, entre) for x in
                                     re.findall(r'[A-Za-z_][A-Za-z0-9_]*', cn)))
                    if not cambia:
                        avisos.append((
                            bruto[:a + m.start()].count('\n') + 1, nom, cn,
                            bruto[:a + vistos[clave][0]].count('\n') + 1))
                if SALE.search(cuerpo_if):
                    vistos[clave] = (m.start(), fin_rama)
    return avisos


def main():
    fuentes = sys.argv[1:]
    if not fuentes:
        import glob
        fuentes = (sorted(glob.glob(os.path.join(RAIZ, 'User', '*.c')))
                   + sorted(glob.glob(os.path.join(RAIZ, 'deepsdr-ui',
                                                   'sim', '*.c'))))
    print('guardas que ya se decidieron mas arriba')
    n = 0
    mudos = 0
    for f in fuentes:
        r = revisa(f)
        if r is None:
            print('  %s: el quitacomentarios ha movido bytes.   MAL'
                  % os.path.relpath(f, RAIZ))
            return 1
        mudos += 1
        for (ln, nom, cn, ant) in r:
            n += 1
            print('  %s:%d  %s(): `%s` ya se decidio en la linea %d y nada'
                  % (os.path.relpath(f, RAIZ), ln, nom, cn, ant))
            print('    ha podido cambiarla por el camino: esta rama no se'
                  ' ejecuta.   MAL')
    if mudos == 0:
        print('  no he mirado ni un fichero: el guion no mide nada.   MAL')
        return 1
    if n:
        print('  %d rama%s muerta%s.   MAL' % (n, '' if n == 1 else 's',
                                               '' if n == 1 else 's'))
        return 1
    print('  %d ficheros, ninguna rama muerta   ok' % mudos)
    return 0


if __name__ == '__main__':
    sys.exit(main())
