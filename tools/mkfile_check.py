#!/usr/bin/env python3
"""
QUE EL TRAMPOLIN SIGA SIENDO UN TRAMPOLIN.

LO QUE PASABA
-------------
El puente que escribe en el disco del usuario RECHAZA cualquier fichero que se
llame exactamente "Makefile":

    is a protected file and cannot be written via remote tools

O sea que cada vez que habia que tocar una regla -del firmware o del
cargador- yo no podia escribirla. Se la pasaba por el chat para que la pegara
a mano, y eso ya habia pasado dos veces: con el "Makefile.nuevo" de la raiz,
que lleva meses sin renombrar, y con las dos lineas que copian
customboot.bin a la raiz, que es de donde salio la averia del cargador
atrasado.

EL METODO
---------
GNU make busca su fichero en este orden:

    GNUmakefile    makefile    Makefile

Asi que GNUmakefile gana y "Makefile" deja de leerlo nadie. GNUmakefile no
lleva reglas: lleva UNA linea, `include reglas.mk`, y no va a cambiar nunca
mas. Todo lo que haya que tocar vive en reglas.mk, que no esta protegido.

Es el mismo reparto que el simulador ya usaba con su local.mk, asi que no es
un invento para esta piedra: es lo que ya funcionaba al lado.

QUE COMPRUEBA, Y POR QUE ESO
----------------------------
El reparto se rompe de una sola forma: que alguien escriba reglas DENTRO del
trampolin. Ese fichero es justo el que yo no voy a poder arreglar luego, asi
que lo que hay que defender no es que exista -eso se ve al primer `make`-
sino que SIGA VACIO DE REGLAS. Por eso esto exige que GNUmakefile sea
comentarios mas un unico include, y nada mas.

Y avisa, sin tumbar nada, de los "Makefile" que queden por ahi. GNU make no
los lee, asi que no estorban; pero son una copia congelada de como se
compilaba un dia, y este proyecto ya se ha tropezado seis veces con una copia
a mano que se separo sola. Se nombran para que se vean, no para dar la lata:
borrarlos es de un segundo y no corre prisa.
"""
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Donde tiene que haber trampolin, y a que apunta cada uno.
#
# deepsdr-ui ENTRA AQUI EL 16/10/2026, y llevaba meses fuera. Era el unico
# directorio con un "Makefile" a secas, o sea el unico donde seguia haciendo
# falta pegar a mano. Y de paso se vio lo que faltaba saber: GNUmakefile esta
# protegido IGUAL que Makefile -"is a protected file and cannot be written
# via remote tools"-, asi que cambiarle el nombre no quita la piedra. Lo que
# la quita es que el fichero protegido sea una linea que no cambia nunca y
# que todo lo demas viva en un .mk que si se puede escribir. Por eso esto
# exige las dos cosas: que el trampolin exista y que este VACIO DE REGLAS.
SITIOS = (("", "reglas.mk"), ("boot", "reglas.mk"),
          ("deepsdr-ui", "trampolin.mk"))

RE_INCLUDE = re.compile(r"^\s*include\s+(\S+)\s*$")


def mira(sitio, incluido):
    donde = os.path.join(RAIZ, sitio) if sitio else RAIZ
    nombre = os.path.join(sitio, "GNUmakefile") if sitio else "GNUmakefile"
    gnu = os.path.join(donde, "GNUmakefile")
    reglas = os.path.join(donde, incluido)
    malos = []

    if not os.path.isfile(gnu):
        print("  %-22s NO ESTA   <- sin el, make lee el Makefile protegido"
              % nombre)
        return 1
    if not os.path.isfile(reglas):
        print("  %-22s apunta a %s, que no existe" % (nombre, incluido))
        return 1

    incluye = None
    with open(gnu, encoding="utf-8") as f:
        for n, linea in enumerate(f, 1):
            t = linea.strip()
            if t == "" or t.startswith("#"):
                continue
            m = RE_INCLUDE.match(linea)
            if m is not None and incluye is None:
                incluye = m.group(1)
                continue
            malos.append((n, t))

    if incluye is None:
        print("  %-22s no incluye nada: el trampolin no salta" % nombre)
        return 1
    if incluye != incluido:
        print("  %-22s incluye %s y deberia incluir %s"
              % (nombre, incluye, incluido))
        return 1
    if malos:
        print("  %-22s TIENE REGLAS DENTRO, y ese fichero no se puede "
              "reescribir desde fuera:" % nombre)
        for n, t in malos[:5]:
            print("        linea %d:  %s" % (n, t))
        print("        muevelas a %s, que si se puede escribir."
              % (os.path.join(sitio, incluido) if sitio else incluido))
        return 1

    print("  %-22s una linea, include %s   ok" % (nombre, incluido))
    return 0



def requisitos_que_no_estan():
    """Requisitos literales de las reglas que no existen en el arbol."""
    import re

    mal = 0
    for sitio, incluido in SITIOS:
        donde = os.path.join(RAIZ, sitio) if sitio else RAIZ
        ruta = os.path.join(donde, incluido)
        if not os.path.isfile(ruta):
            continue
        txt = open(ruta, encoding="utf-8", errors="replace").read()
        # Los objetivos del propio fichero -y los .PHONY- NO son ficheros:
        # pedirlos es pedir que se haga algo, no que exista algo. Sin esto,
        # `update4: update` salia como fallo.
        propios = set()
        for ln in txt.split("\n"):
            if ln.startswith("\t") or ln.lstrip().startswith("#"):
                continue
            m = re.match(r"^([^\t=:]+):(?!=)([^#]*)", ln)
            if not m:
                continue
            d = m.group(1).strip()
            if d == ".PHONY":
                propios.update(m.group(2).split(";")[0].split())
            else:
                propios.update(d.split())
        faltan = []
        for ln in txt.split("\n"):
            if ln.startswith("\t") or ln.lstrip().startswith("#"):
                continue
            m = re.match(r"^([^\t=:]+):(?!=)([^#]*)", ln)
            if not m:
                continue
            destino = m.group(1).strip()
            # .PHONY y los demas objetivos especiales no piden ficheros:
            # lo que llevan detras son NOMBRES DE OBJETIVO. Y lo que va
            # despues de un `;` ya es la receta, no requisitos.
            if destino.startswith("."):
                continue
            for req in m.group(2).split(";")[0].replace("|", " ").split():
                if ("$" in req or "%" in req or req in (";",)
                        or req.endswith(":")):
                    continue
                if req in propios:
                    continue
                if not os.path.exists(os.path.join(donde, req)):
                    faltan.append((destino, req))
        if faltan:
            mal += 1
            print("\n  MAL: en %s hay reglas que piden ficheros que no estan:" % ruta)
            for destino, req in faltan:
                print("        %-28s pide  %s" % (destino, req))
            print("  Una regla de patron con un requisito que no existe NO SE")
            print("  APLICA, y make no lo dice: falla mas adelante y en otro")
            print("  sitio. Es lo que paso con `Makefile` el 10/10/2026.")
    return mal



def los_ficheros_que_pide_el_make(raiz):
    """Que los ficheros que nombran los makefiles sigan donde los nombran.

    POR QUE - 13/10/2026.

    Ordenando la carpeta para publicarla, GD32F450VE_FLASH.ld -el guion
    del enlazador, sin el cual no se compila NADA- acabo dentro de docs\.
    Y no lo dijo nadie: raiz_check.py solo mira las rutas que nombran los
    makefiles DEL SIMULADOR, y copias_check.py solo mira los ficheros que
    estan duplicados en los dos arboles. El .ld no es ninguna de las dos
    cosas, asi que se podia mover de sitio sin que saltara nada hasta el
    siguiente "make", que ademas falla con un error del enlazador y no con
    un "te falta un fichero".

    Un fichero que el make nombra y que no esta es un fallo que se tiene
    que ver AQUI, antes de compilar, y no en mitad de un enlazado.
    """
    import re as _re
    fallos = 0
    for mk in ("GNUmakefile", "reglas.mk", "avisos.mk"):
        ruta = os.path.join(raiz, mk)
        if not os.path.isfile(ruta):
            print("  falta el propio %s" % mk)
            fallos += 1
            continue
        txt = open(ruta, encoding="utf-8", errors="replace").read()
        # solo los nombres sueltos, sin carpeta: son los de la raiz
        for nom in sorted(set(_re.findall(r'(?<![\w./-])([A-Za-z0-9_-]+\.(?:ld|mk))(?![\w.])',
                                          txt))):
            if nom == "local.mk":      # es del simulador, no de aqui
                continue
            if not os.path.isfile(os.path.join(raiz, nom)):
                print("  %s nombra %s y no esta en la raiz" % (mk, nom))
                fallos += 1
    if fallos == 0:
        print("  los ficheros que nombran los makefiles estan en su sitio")
    return fallos


def main():
    fallos = 0

    print("TRAMPOLIN DEL MAKE: GNU make lee GNUmakefile antes que Makefile")
    for sitio, incluido in SITIOS:
        fallos += mira(sitio, incluido)

    """
    *** Y QUE NINGUNA REGLA PIDA UN FICHERO QUE NO EXISTE - 10/10/2026. ***

    El usuario, compilando en su maquina:

        ld: cannot open map file build/firmware.map: No such file
        make: *** [reglas.mk:177: build/firmware.elf] Error 1

    Las reglas de patron de reglas.mk pedian `Makefile` como requisito, y en
    este arbol ya no hay ningun fichero que se llame asi. Una regla de patron
    cuyo requisito no existe NO SE APLICA, asi que los .o no se construian y
    el enlace se encontraba sin directorio.

    Y en MI maquina seguia habiendo un Makefile viejo tirado, asi que
    compilaba. Un requisito que solo existe en la maquina de quien lo
    escribio es la peor clase de requisito: no falla donde se puede ver.

    Esto mira los requisitos literales de reglas.mk -los que no son
    variables ni patrones- y exige que existan.
    """
    fallos += requisitos_que_no_estan()

    #
    # *** "YA NO LO LEE NADIE" NO ERA VERDAD - 21/10/2026. ***
    #
    # Esto listaba como muerto CUALQUIER fichero llamado Makefile, razonando
    # solo con el orden en que GNU make elige el suyo: GNUmakefile gana, asi
    # que Makefile ya no se lee.
    #
    # Es falso en cuanto el trampolin lo INCLUYE. Y eso es justo lo que hace
    # deepsdr-ui/trampolin.mk en su ultima linea: `include Makefile`. Ahi el
    # Makefile no es una foto congelada, es EL makefile del simulador -sus
    # 192 lineas de reglas-, y el .mk de al lado solo decide si se lee tal
    # cual o con dos objetivos quitados cuando no hay vendor/.
    #
    # Lo que paso: este aviso se leyo como un permiso, el fichero se borro, y
    # con el se fueron todos los bancos. Un guardian que senala a un inocente
    # es peor que no tenerlo, porque viene firmado.
    #
    # Asi que ahora se mira quien lo incluye ANTES de llamarlo muerto. Se
    # recorren los .mk del sitio -no solo el que nombra SITIOS: un trampolin
    # puede incluir a otro- y se busca un include de ese nombre.
    #
    muertos = []
    vivos = []
    for sitio, _ in SITIOS:
        donde = os.path.join(RAIZ, sitio) if sitio else RAIZ
        incluidos = set()
        try:
            for f in os.listdir(donde):
                if not f.endswith(".mk"):
                    continue
                try:
                    txt = open(os.path.join(donde, f), encoding="utf-8",
                               errors="replace").read()
                except OSError:
                    continue
                for m in re.finditer(r"^\s*(?:-|s)?include\s+(\S+)\s*$",
                                     txt, re.M):
                    incluidos.add((m.group(1), f))
        except OSError:
            pass
        for n in ("Makefile", "Makefile.nuevo", "makefile"):
            ruta = os.path.join(donde, n)
            if not os.path.isfile(ruta):
                continue
            quien = [f for (obj, f) in incluidos if obj == n]
            rel = os.path.relpath(ruta, RAIZ)
            if quien:
                vivos.append((rel, sorted(set(quien))))
            else:
                muertos.append(rel)

    if vivos:
        print("\n  y esto SI lo lee alguien, aunque se llame Makefile:")
        for rel, quien in vivos:
            print("        %-24s lo incluye %s" % (rel, ", ".join(quien)))
        print("  NO SE BORRA. Lleva reglas de verdad; el trampolin solo")
        print("  decide como leerlo. Ver el comentario de esta funcion.")

    if muertos:
        print("\n  y esto ya no lo lee nadie (ni GNU make, que coge el")
        print("  GNUmakefile, ni ningun .mk de al lado por include):")
        for m in muertos:
            print("        %s" % m)
        print("  No estorba. Pero es una foto congelada de como se compilaba")
        print("  un dia, y aqui una copia a mano que se separa sola ya ha")
        print("  costado seis averias. Borrarlos cuando apetezca.")

    fallos += los_ficheros_que_pide_el_make(RAIZ)

    print("\n%s" % ("MAKE: todo correcto" if fallos == 0
                    else "MAKE: MAL, mira arriba"))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
