#!/usr/bin/env python3
"""
QUE NINGUN ENLACE NI NINGUNA IMAGEN DE LOS .MD APUNTE A DONDE NO HAY NADA.

  python3 md_enlaces_check.py <raiz del firmware>

POR QUE EXISTE - 13/10/2026.

Pedido por el usuario.

Preparando esa publicacion aparecio esto en la PRIMERA linea de HARDWARE.md:

    ![Texto alternativo](deepsdr_1.jpg)

Ese fichero no esta en el arbol. No estaba desde hacia meses. En un editor de
markdown local no se nota -muchos no cargan imagenes-, pero GitHub lo pinta
como un icono roto, y era lo primero que iba a ver cualquiera que abriese el
repositorio. Nadie lo vio porque NINGUNA comprobacion miraba dentro de los
.md: se miraba la version, las novedades, el manual, los textos sin traducir,
y los enlaces no los miraba nadie.

Un enlace roto en el README es exactamente el fallo que este proyecto lleva
meses persiguiendo con otro nombre: algo que se escribio una vez, que dejo de
ser verdad cuando se movio un fichero, y que sigue ahi diciendo que si.

QUE COMPRUEBA, Y POR QUE CADA COSA:

  - Que todo enlace RELATIVO de cualquier .md del arbol lleva a un fichero o
    a una carpeta que existe. Los http(s) no se tocan: comprobarlos querria
    red, y una comprobacion que depende de la red falla el dia que no hay.
  - Que toda IMAGEN existe. Es el caso de HARDWARE.md y el mas visible de
    todos, porque se ve roto sin tener que pulsar nada.
  - Que las anclas internas (#algo) del README corresponden a un titulo del
    propio fichero. Un ancla mal escrita no da error: no hace nada, que es
    peor.
  - Que las capturas que hay en capturas/ se usan todas. Una captura que ya
    no sale en ningun sitio es peso muerto en el repositorio, y casi siempre
    quiere decir que alguien cambio el README y se dejo la mitad.

Lo que NO comprueba: si la imagen que sale es la que toca. Eso hay que mirarlo
con los ojos.
"""
import os
import re
import sys
from urllib.parse import unquote


# Los UNICOS .md que se quedan en la raiz. El resto van en docs/.
#
# Pedido por el usuario, 13/10/2026.
#
# README.md es la portada, NOVEDADES.md lo que lee el que acaba de
# actualizar, y PROCEDENCIA.md el que dice de donde sale lo que no es
# propio: los tres sitios donde mira alguien que llega de fuera. El
# tercero ademas no es opcional, que la MIT de Ft8Lib pide que el aviso
# viaje CON el codigo y el repositorio es AGPL-3.0.
ARRIBA = ('README.md', 'NOVEDADES.md', 'PROCEDENCIA.md')


def sin_codigo(texto):
    """El markdown de dentro de un bloque de codigo NO es un enlace.

    Lo encontro el propio fichero el dia que nacio: la entrada de CAMBIOS.md
    que cuenta este arreglo ENSEÑA, dentro de un bloque con comillas, el
    ![](deepsdr_1.jpg) roto de HARDWARE.md - porque de eso va la entrada. Y
    esta comprobacion lo dio por un enlace de verdad y se puso roja contra
    un texto que estaba bien escrito.

    Un banco que no distingue un ejemplo de un uso obliga a escribir la
    documentacion de lado para no despertarlo, y entonces manda el el, no
    el que escribe.
    """
    fuera, dentro = [], False
    for linea in texto.split('\n'):
        if linea.lstrip().startswith('```') or linea.lstrip().startswith('~~~'):
            dentro = not dentro
            fuera.append('')
            continue
        if dentro:
            fuera.append('')
            continue
        # y el codigo en linea, entre comillas simples
        fuera.append(re.sub(r'`[^`]*`', '', linea))
    return '\n'.join(fuera)


def anclas_de(texto):
    """Los titulos de un markdown, pasados a ancla como los pasa GitHub."""
    salida = []
    for titulo in re.findall(r'^#+\s+(.+?)\s*$', texto, re.M):
        a = titulo.lower()
        a = re.sub(r'[^\w\s-]', '', a, flags=re.U)
        salida.append(a.replace(' ', '-'))
    return salida


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("uso: md_enlaces_check.py <raiz del firmware>\n")
        return 2
    raiz = sys.argv[1]

    mds = []
    for aqui, dirs, ficheros in os.walk(raiz):
        dirs[:] = [d for d in dirs
                   if d not in ('build', '.git', 'node_modules', 'CMSIS',
                                'Firmware', 'Ft8Lib')]
        for f in ficheros:
            if f.lower().endswith('.md'):
                mds.append(os.path.join(aqui, f))
    mds.sort()

    fallos = []
    usadas = set()
    enlaces = 0

    for md in mds:
        rel = os.path.relpath(md, raiz)
        crudo = open(md, encoding='utf-8', errors='replace').read()
        propias = anclas_de(crudo)
        texto = sin_codigo(crudo)

        for imagen, destino in re.findall(r'(!?)\[[^\]]*\]\(([^)\s]+)', texto):
            if destino.startswith(('http://', 'https://', 'mailto:')):
                continue
            enlaces += 1

            if destino.startswith('#'):
                if destino[1:] not in propias:
                    fallos.append("%s: el ancla %s no es ningun titulo del fichero"
                                  % (rel, destino))
                continue

            # un enlace relativo va contra la carpeta del .md que lo escribe
            # UN ENLACE CON ESPACIOS VIENE ESCAPADO - 13/10/2026.
            #
            # "Mod flash .pdf" se enlaza como "Mod%20flash%20.pdf" o entre
            # <...>, que es como markdown deja meter espacios. Sin
            # deshacer eso, este comprobador buscaria un fichero llamado
            # literalmente "Mod%20flash%20.pdf" y diria que falta: un
            # guardia que da la alarma por no saber leer.
            limpio = destino.split('#', 1)[0]
            if limpio.startswith('<') and limpio.endswith('>'):
                limpio = limpio[1:-1]
            limpio = unquote(limpio)
            if not limpio:
                continue
            ruta = os.path.normpath(os.path.join(os.path.dirname(md), limpio))
            if not os.path.exists(ruta):
                fallos.append("%s: %s %s no existe"
                              % (rel, "la imagen" if imagen else "el enlace",
                                 destino))
            elif os.path.relpath(ruta, raiz).startswith('capturas' + os.sep):
                usadas.add(os.path.relpath(ruta, raiz).replace(os.sep, '/'))

    # EN LA RAIZ, SOLO README.md - 13/10/2026.
    #
    # Pedido por el usuario.
    #
    # Lo escribo aqui y no en un comentario porque un orden que no comprueba
    # nadie dura hasta el siguiente fichero que a alguien le venga bien
    # dejar arriba, y la portada del repositorio es justo donde se nota.
    for f in sorted(os.listdir(raiz)):
        if (f.lower().endswith('.md') and f not in ARRIBA
                and os.path.isfile(os.path.join(raiz, f))):
            fallos.append("%s esta en la raiz: los .md van en docs/, "
                          "menos %s" % (f, " y ".join(sorted(ARRIBA))))

    cap = os.path.join(raiz, 'capturas')
    sueltas = []
    if os.path.isdir(cap):
        for f in sorted(os.listdir(cap)):
            if f.startswith('.'):
                continue
            if ('capturas/' + f) not in usadas:
                sueltas.append(f)

    print("  %d ficheros .md, %d enlaces relativos" % (len(mds), enlaces))
    if sueltas:
        print("  capturas que no sale en ningun .md: %s" % ", ".join(sueltas))
    for f in fallos:
        print("  %s" % f)

    if fallos:
        print("  ENLACES: HAY FALLOS (%d)" % len(fallos))
        return 1
    print("  ENLACES: todo correcto")
    return 0


if __name__ == '__main__':
    sys.exit(main())
