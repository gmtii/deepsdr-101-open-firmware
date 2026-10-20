#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE EL PIE DE LA REJILLA NO SE SALGA DE SU HUECO.

  python3 pie_check.py <User/main.c>

POR QUE EXISTE - 06/10/2026.

El pie de cada celda se arma byte a byte con aj_u2s() y punteros, sin
snprintf ni nada que recorte. Asi que el tamaño no lo comprueba el
compilador: lo tiene que contar el que añade un numero. Y hay que contarlo
en el PEOR caso, que para un contador uint32_t son diez cifras, no las dos
que uno ve mientras prueba.

Se desbordo el mismo dia que se escribio: "0dsc 0trd 1rep 75-255/1024" son
26 caracteres mas el fin de cadena, 27 en un hueco de 24. Lo que se ve es el
texto cortado; lo que NO se ve es que los tres bytes de mas van al pie de la
celda siguiente.

El arreglo fue dejar de contar: pie_txt() y pie_num() escriben lo que quepa
y nada mas. Esto comprueba que siguen siendo las unicas que escriben ahi.

LA REGLA: dentro de info_valor(), nada escribe en `pie` por su cuenta. Ni
`pie[j++] = ...`, ni aj_u2s(pie, ...), ni aj_u2s(&pie[j], ...). Solo
pie_txt() y pie_num(), que son las que respetan INFO_PIE_MAX.

Lo que NO comprueba: que el texto quepa en la CELDA, que son 192 px. Eso lo
mide el ojo y el banco de geometria.
"""
import io
import re
import sys


def cuerpo(src, firma):
    m = re.search(re.escape(firma), src)
    if not m:
        return None
    i = src.index('{', m.start())
    n = 0
    for j in range(i, len(src)):
        if src[j] == '{':
            n += 1
        elif src[j] == '}':
            n -= 1
            if n == 0:
                return src[i:j + 1], src[:i].count('\n') + 1
    return None


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    src = io.open(sys.argv[1], encoding='utf-8', errors='replace').read()
    # fuera comentarios, conservando los saltos de linea
    src = re.sub(r'/\*.*?\*/',
                 lambda m: '\n' * m.group(0).count('\n'), src, flags=re.S)
    src = re.sub(r'//[^\n]*', '', src)

    m = re.search(r'#define\s+INFO_PIE_MAX\s+(\d+)', src)
    if not m:
        print("  no encuentro INFO_PIE_MAX   MAL")
        return 1
    print("  INFO_PIE_MAX = %s" % m.group(1))

    r = cuerpo(src, 'static const char *info_valor(uint8_t id, char *buf, char *pie)')
    if r is None:
        print("  no encuentro info_valor()   MAL")
        return 1
    cue, ln0 = r

    # Las dos que SI valen, y cualquier otra escritura a `pie`.
    malos = []
    for mm in re.finditer(r'\bpie\s*\[', cue):
        # pie[j] = '\0' despues de pie_txt no existe ya; cualquier indexado
        # con asignacion es escritura a mano.
        resto = cue[mm.start():mm.start() + 60]
        if re.match(r"pie\s*\[[^\]]*\]\s*=", resto):
            malos.append((ln0 + cue[:mm.start()].count('\n'),
                          ' '.join(resto.split())[:48]))
    for mm in re.finditer(r'\baj_u2s\s*\(\s*&?\s*pie', cue):
        malos.append((ln0 + cue[:mm.start()].count('\n'),
                      ' '.join(cue[mm.start():mm.start() + 48].split())))

    usa = len(re.findall(r'\bpie_(?:txt|num)\s*\(', cue))
    print("  escrituras por pie_txt()/pie_num(): %d" % usa)
    if usa == 0:
        print("  nadie usa las funciones que recortan   MAL")
        return 1

    if malos:
        # LA QUEJA SE ROMPIA AL QUEJARSE - 20/10/2026. El formato pedia dos
        # huecos y le llegaba un solo numero, asi que en vez de decir que
        # linea era, reventaba con un TypeError. Un guardian que se cae
        # cuando por fin encuentra algo no esta guardando nada: el que lo
        # provoco ve una traza de Python y no su fallo.
        for ln, txt in malos:
            print("    linea %-6d %-50s *** ESCRIBE EN EL PIE A MANO ***"
                  % (ln, txt))
        print("  %d escritura(s) al pie sin recortar" % len(malos))
        print("  Usa pie_txt()/pie_num(): respetan INFO_PIE_MAX y lo de mas")
        print("  sale cortado en vez de pisar la celda siguiente.")
        print("  Ver la cabecera de este guion.")
        return 1
    print("  nadie escribe en el pie sin recortar  ok")
    return 0


sys.exit(main())
