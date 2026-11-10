#!/usr/bin/env python3
"""
QUE LA CUENTA DE LA PROCEDENCIA SEA LA DE VERDAD.

  python3 tools/procedencia_check.py [raiz]

POR QUE EXISTE - 08/11/2026.

PROCEDENCIA.md es el documento que dice de donde sale lo que no es propio.
No es decoracion: el repositorio es AGPL-3.0, la cadena de HFDL esta escrita
contra dumphfdl -que es GPL-3.0-or-later- y lo primero que va a mirar
cualquiera que llegue de fuera es cuantos ficheros citan ese proyecto y que
dicen sus cabeceras. Esta escrito con todas las letras ahi dentro:

    "Aun asi, treinta y cuatro ficheros que citan un proyecto GPL en sus
     cabeceras es lo primero que va a mirar cualquiera"

Pues la cuenta decia TREINTA Y DOS, y los ficheros eran treinta y cuatro.

Se escribio una vez, se anadieron dos ficheros mas a la cadena, y el numero
se quedo donde estaba. Nadie lo vio porque ninguna comprobacion miraba
dentro de PROCEDENCIA.md: se miraban los enlaces de los .md, las cadenas sin
traducir, el manual, la version... y la procedencia no la miraba nadie.

Es EXACTAMENTE el fallo que este proyecto lleva meses persiguiendo con otro
nombre -algo que se escribio una vez, que dejo de ser verdad cuando el arbol
crecio, y que sigue ahi diciendo que si- solo que esta vez cae en el unico
fichero del repositorio que contesta una pregunta legal.

QUE COMPRUEBA
-------------
  - Cuantos ficheros de User/hfdl_* nombran dumphfdl, contados del arbol.
  - Que ese numero es el que dice PROCEDENCIA.md en sus TRES sitios (el
    titulo, el parrafo en letra y la tabla del final) y el que dice el
    README.
  - Y que el numero sale escrito tambien en letra, porque el parrafo lo
    dice asi y una de las dos formas se queda atras sola.

LO QUE NO COMPRUEBA, y hay que decirlo: si lo que cuenta cada cabecera es
verdad. Eso se lee con los ojos y ya esta leido, fichero a fichero. Esto
solo vigila que la CUENTA no mienta.
"""
import os
import re
import sys


# Del cero al cuarenta, que es de sobra para esto y no obliga a traerse una
# biblioteca entera para escribir un numero en castellano.
LETRA = {
    20: "veinte", 21: "veintiun", 22: "veintidos", 23: "veintitres",
    24: "veinticuatro", 25: "veinticinco", 26: "veintiseis",
    27: "veintisiete", 28: "veintiocho", 29: "veintinueve", 30: "treinta",
    40: "cuarenta",
}
UNID = {1: "uno", 2: "dos", 3: "tres", 4: "cuatro", 5: "cinco", 6: "seis",
        7: "siete", 8: "ocho", 9: "nueve"}


def en_letra(n):
    """'34' -> 'treinta y cuatro'. Sin tildes, como escribe este arbol."""
    if n in LETRA:
        return LETRA[n]
    if 30 < n < 40:
        return "treinta y " + UNID[n - 30]
    if 40 < n < 50:
        return "cuarenta y " + UNID[n - 40]
    return None


def main():
    raiz = sys.argv[1] if len(sys.argv) > 1 else '.'
    user = os.path.join(raiz, 'User')

    if not os.path.isdir(user):
        print('  no encuentro %s   MAL' % user)
        return 1

    # LOS FICHEROS DE LA CADENA QUE CITAN LA REFERENCIA.
    #
    # Solo User/hfdl_*: main.c tambien la nombra -cuenta en un comentario
    # que dumphfdl resuelve una cosa de otra manera- pero main.c no es de
    # la cadena de HFDL, es la pantalla. Meterlo subiria la cuenta sin que
    # el documento este hablando de el.
    nombres = sorted(f for f in os.listdir(user)
                     if f.startswith('hfdl_') and f.endswith(('.c', '.h')))
    citan = []
    for f in nombres:
        p = os.path.join(user, f)
        try:
            txt = open(p, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        if 'dumphfdl' in txt:
            citan.append(f)

    n = len(citan)
    print('  %d ficheros User/hfdl_* en el arbol, %d citan dumphfdl' %
          (len(nombres), n))

    #
    # Y SI NO HA MIRADO NADA, NO DICE "TODO BIEN".
    #
    # La misma leccion que copias_check.py, version_check.py y, desde la
    # revision a fondo del 08/11, citas_check.py, interruptores_check.py,
    # md_enlaces_check.py y sin_llamar.py. Se escribe de entrada y no
    # cuando muerda.
    #
    if not nombres:
        print('  *** NO HE ENCONTRADO NI UN User/hfdl_*: la ruta no es la '
              'que creia ***')
        return 1
    if n == 0:
        print('  *** NINGUNO CITA dumphfdl, y eso no puede ser: o se han '
              'reescrito todas las cabeceras, o estoy mirando donde no es ***')
        return 1

    letra = en_letra(n)
    fallos = []

    doc = os.path.join(raiz, 'PROCEDENCIA.md')
    rdm = os.path.join(raiz, 'README.md')
    for ruta in (doc, rdm):
        if not os.path.isfile(ruta):
            print('  no encuentro %s   MAL' % os.path.basename(ruta))
            return 1

    tp = open(doc, encoding='utf-8', errors='replace').read()
    tr = open(rdm, encoding='utf-8', errors='replace').read()

    # Las tres menciones con cifra de PROCEDENCIA.md y la del README. Se
    # buscan por su forma, no por la linea, para que muevan sin romper.
    sitios = [
        (doc, r'### La cadena de HFDL — (\d+) ficheros', tp),
        (doc, r'Los (\d+) ficheros de HFDL, escritos contra', tp),
        (rdm, r'\*\*La cadena de HFDL\*\* \((\d+) ficheros\)', tr),
    ]
    for ruta, pat, txt in sitios:
        m = re.search(pat, txt)
        if not m:
            fallos.append('%s: no encuentro la frase "%s"'
                          % (os.path.basename(ruta), pat))
            continue
        dice = int(m.group(1))
        if dice != n:
            fallos.append('%s: dice %d ficheros y en el arbol hay %d'
                          % (os.path.basename(ruta), dice, n))

    # Y la que va en letra, que es la que mas facil se queda atras porque
    # no se parece a un numero y no la encuentra un grep de cifras.
    m = re.search(r'Aun así, ([a-zá-ú ]+?) ficheros que citan un proyecto GPL',
                  tp)
    if not m:
        fallos.append('PROCEDENCIA.md: no encuentro el parrafo del "Aun asi, '
                      '<numero en letra> ficheros que citan un proyecto GPL"')
    elif letra is None:
        print('  (no se escribir %d en letra; me salto esa comprobacion)' % n)
    elif m.group(1).strip() != letra:
        fallos.append('PROCEDENCIA.md: en letra dice "%s" y son "%s"'
                      % (m.group(1).strip(), letra))

    if fallos:
        print('')
        for f in fallos:
            print('  %s' % f)
        print('')
        print('  PROCEDENCIA.md contesta una pregunta de licencia, y la')
        print('  contesta con una cuenta. Si la cuenta no es la del arbol,')
        print('  el documento esta diciendo que no a algo que no ha mirado.')
        print('  Los que citan dumphfdl hoy son:')
        for f in citan:
            print('      User/%s' % f)
        print('')
        print('  PROCEDENCIA: HAY FALLOS (%d)' % len(fallos))
        return 1

    print('  y PROCEDENCIA.md y README.md dicen %d (y "%s")  ok'
          % (n, letra if letra else n))
    print('PROCEDENCIA: todo correcto')
    return 0


if __name__ == '__main__':
    sys.exit(main())
