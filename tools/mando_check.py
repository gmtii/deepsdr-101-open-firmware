#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE NADIE SE QUEDE CON EL MANDO ANTES QUE EL VOLUMEN.

  python3 mando_check.py <User/main.c>

POR QUE EXISTE - 05/10/2026.

*** De un issue: "volume doesn't work when in settings menu... the 'volume'
text box appears as expected, but moving the encoder goes through the
settings options instead of changing the volume". ***

El reparto del mando vive en tune_encoder_poll() y es una ESCALERA: una
pantalla tras otra mira si es suya, y la que se lo queda hace `return`. El
volumen esta al final de esa escalera, asi que cualquier pantalla que coja
los detentes y vuelva ANTES le deja sin giro - y el boton, que esta en la
barra de abajo y se puede pulsar siempre, ya ha encendido la luz y sacado la
caja. La radio promete una cosa y hace otra.

No es un fallo que se vea leyendo: cada rama, por separado, es razonable.
Solo se ve mirando el ORDEN, y el orden cambia cada vez que se añade una
pantalla nueva a la escalera. Por eso esto se mide y no se recuerda.

LA REGLA: toda rama de tune_encoder_poll() que se quede con los detentes y
vuelva antes de la rama del volumen tiene que nombrar
ENCODER_TARGET_VOLUME - o sea, tiene que haberse preguntado si el mando
estaba enganchado al volumen. Como se conteste es cosa suya; lo que no vale
es no preguntarlo.

Lo que NO comprueba: que la respuesta sea la correcta. Eso no lo puede mirar
un guion.
"""
import io
import re
import sys


def cuerpo(src, nombre):
    """El texto de una funcion, contando llaves."""
    m = re.search(r'^static\s+void\s+' + nombre + r'\s*\(void\)\s*\n\{', src, re.M)
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
                return src[i:j + 1]
    return None


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    src = io.open(sys.argv[1], encoding='utf-8', errors='replace').read()
    # Fuera comentarios: un ENCODER_TARGET_VOLUME nombrado solo en un
    # comentario no es una pregunta, es una mencion.
    #
    # PERO CONSERVANDO LOS SALTOS DE LINEA, y esto no es un detalle de
    # estilo. La primera version los sustituia por UN espacio, asi que un
    # comentario de veinte lineas delante de un `if` dejaba ese `if`
    # pegado al codigo anterior - y el buscador de ramas, que los reconoce
    # por "salto de linea y cuatro espacios", dejaba de verlo. Se comia
    # justo la rama de la rejilla de Ajustes, que es DONDE ESTABA EL
    # FALLO: el guion decia "todo correcto" sin haber mirado al culpable.
    src = re.sub(r'/\*.*?\*/',
                 lambda m: '\n' * m.group(0).count('\n'), src, flags=re.S)
    src = re.sub(r'//[^\n]*', '', src)

    cue = cuerpo(src, 'tune_encoder_poll')
    if cue is None:
        print("  no encuentro tune_encoder_poll()   MAL")
        return 1

    # Donde empieza la rama del volumen: a partir de ahi ya no importa.
    #
    # CUAL ES "LA RAMA DEL VOLUMEN", dicho con cuidado.
    #
    # No vale el primer `if (s_encoder_target == ENCODER_TARGET_VOLUME)` que
    # aparezca: desde que las ramas de arriba preguntan por el volumen -que
    # es justo lo que este guion exige- hay varios, y quedarse con el
    # primero corta la escalera demasiado pronto y deja fuera a las ramas de
    # abajo. Paso de verdad: el arreglo del teclado creo un `if` asi antes
    # que el bueno, y la rejilla de Ajustes -donde estaba el fallo- se quedo
    # sin mirar.
    #
    # La de verdad es la que AJUSTA el volumen, o sea la que usa
    # VOLUME_STEP_X2. Eso no lo tiene ninguna otra.
    #
    ini = None
    for m in re.finditer(r'\n    if\s*\(\s*s_encoder_target\s*==\s*ENCODER_TARGET_VOLUME\s*\)',
                         cue):
        k = cue.find('{', m.start())
        if k < 0:
            continue
        p2 = 0
        fin = len(cue)
        for q in range(k, len(cue)):
            if cue[q] == '{':
                p2 += 1
            elif cue[q] == '}':
                p2 -= 1
                if p2 == 0:
                    fin = q
                    break
        # VOLUME_STEP_X2 DENTRO DE SU PROPIO BLOQUE, no "cerca": medir por
        # cercania daba por buena la del teclado, que esta setenta lineas
        # antes que la de verdad.
        if 'VOLUME_STEP_X2' in cue[k:fin]:
            ini = m.start()
            break
    if ini is None:
        print("  no encuentro la rama que ajusta el volumen (VOLUME_STEP_X2)   MAL")
        return 1
    antes = cue[:ini]

    # Las ramas de primer nivel: "if (...) { ... }"
    ramas = []
    for mm in re.finditer(r'\n    if\s*\(', antes):
        i = antes.index('(', mm.start())
        n = 0
        for j in range(i, len(antes)):
            if antes[j] == '(':
                n += 1
            elif antes[j] == ')':
                n -= 1
                if n == 0:
                    cond = antes[i + 1:j]
                    break
        else:
            continue
        k = antes.find('{', j)
        if k < 0:
            continue
        n = 0
        for q in range(k, len(antes)):
            if antes[q] == '{':
                n += 1
            elif antes[q] == '}':
                n -= 1
                if n == 0:
                    # La condicion ENTERA, no recortada: el recorte es solo
                    # para imprimir. Recortarla aqui hacia que la regla se
                    # mirara sobre media condicion - y dos ramas que SI
                    # preguntaban salian como que no.
                    ramas.append((antes[:i].count('\n') + 1,
                                  ' '.join(cond.split()), antes[k:q + 1]))
                    break

    mal = 0
    print("  ramas que corren ANTES del volumen: %d" % len(ramas))
    for ln, cond, bloque in ramas:
        coge = ('detents' in bloque) and ('return' in bloque)
        if not coge:
            continue
        # Vale preguntarlo de las dos formas: mirando si el destino ES el
        # volumen, o acotando el destino a otra cosa -"solo en SINTONIA"-,
        # que deja el volumen fuera igual de bien.
        pregunta = ('ENCODER_TARGET_VOLUME' in bloque
                    or 'ENCODER_TARGET_VOLUME' in cond
                    or 's_encoder_target' in cond)
        print("    linea %-6d %-54s %s" % (ln, cond[:52],
              "ok" if pregunta else "*** SE QUEDA EL MANDO SIN PREGUNTAR ***"))
        if not pregunta:
            mal += 1

    if mal:
        print("  *** %d ramas se quedan los detentes sin mirar si son del volumen ***" % mal)
        print("  El boton de Volumen se enciende igual, asi que la radio promete")
        print("  algo que no hace. Ver la cabecera de este guion.")
        return 1
    print("  todas preguntan por el volumen antes de quedarse el mando  ok")
    return 0


sys.exit(main())
