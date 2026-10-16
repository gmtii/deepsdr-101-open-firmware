#!/usr/bin/env python3
"""
QUE EL SIMULADOR NO COMPILE OTRO FIRMWARE. 10/10/2026.

El simulador no tiene el firmware dentro: lo compila desde fuera, con
$(FIRMWARE) apuntando a la raiz del repositorio. El Makefile trae un valor
por defecto -FIRMWARE ?= ../deepsdr- y ese valor es de cuando solo habia
una rama.

HOY HAY DOS, Y LAS DOS EXISTEN EN LA MISMA CARPETA:

    ...\\source\\deepsdr          <- la vieja, parada el 28/09
    ...\\source\\deepsdr-reload   <- la que corre en la radio
    ...\\source\\deepsdr-ui       <- este simulador

O sea que "make comprueba" a secas, sin FIRMWARE=, no fallaba: compilaba y
aprobaba EL FIRMWARE VIEJO. Y copias_check.py, que vigila que las copias de
vendor/ y gfx2/ esten al dia, las comparaba contra la rama vieja y decia
"ninguno se ha quedado atras" - con razon, porque estaban al dia... de la
otra. Cuatro ficheros de vendor/ coinciden al byte con deepsdr/User/ y no
con deepsdr-reload/User/:

    gfx.c       10.001  =  deepsdr/User/gfx.c        (hoy son 12.464)
    gfx.h        5.014  =  deepsdr/User/gfx.h        (hoy son  5.391)
    spectrum.c  64.064 ~=  deepsdr/User/spectrum.c   (hoy son 80.847)
    spectrum.h  16.459  =  deepsdr/User/spectrum.h   (hoy son 17.489)

Y lo peor no es que avisara de menos: es que "make copias" -la orden que
pone las copias al dia- habria copiado el firmware de septiembre ENCIMA de
las copias buenas. La herramienta de arreglarlo era la que mas dano hacia.

Es la misma familia de fallo que ya documentan ram_mapa.py (miraba .bss. y
se perdia COMMON), pila.py (miraba donde no era) y modos_check.py (dejo de
reconocer la tabla y dijo "no hay modos"): UN BANCO QUE MIDE OTRA COSA NO
FALLA, APRUEBA.

COMO SE COMPRUEBA, SIN INVENTARSE UNA MARCA
-------------------------------------------
No se mira si la carpeta "se llama" de una forma, ni se busca un fichero
centinela puesto a proposito: eso es una convencion y las convenciones se
olvidan. Se mira lo que el propio simulador DICE QUE NECESITA.

local.mk y el Makefile nombran 148 rutas bajo $(FIRMWARE). Si la raiz es la
correcta, estan todas. Si falta una -rapido.c, por ejemplo, que nacio en la
rama nueva- es que $(FIRMWARE) apunta a otro sitio, y se dice a cual
deberia apuntar mirando las carpetas hermanas.

Se queda corto a proposito en un caso: dos arboles de la misma rama, uno de
ayer y otro de hoy, tienen los mismos ficheros y este guion no los
distingue. Para eso esta copias_check.py, que compara el CONTENIDO. Los dos
hacen falta: este dice "no es ese arbol" y el otro dice "es ese arbol pero
las copias estan viejas".
"""
import os
import re
import sys

EXT = (".c", ".h", ".md", ".bin", ".py", ".csv", ".ld", ".mk", ".txt")


def sin_comentarios(ruta):
    """Las lineas del makefile con los comentarios quitados.

    Hace falta porque los comentarios de local.mk citan rutas de ejemplo
    -"$(FIRMWARE)/User/ecualiz.h." con el punto de la frase pegado- y una
    ruta citada en una explicacion no es un requisito de nadie.
    """
    out = []
    try:
        with open(ruta, encoding="utf-8", errors="replace") as f:
            for l in f:
                i = l.find("#")
                if i >= 0:
                    l = l[:i]
                out.append(l)
    except OSError:
        pass
    return out


def sin_comodines(l):
    """La linea con los $(wildcard ...) quitados.

    *** ESTE GUION SE PILLO A SI MISMO EN SU PRIMERA EJECUCION. ***

    Dijo que la raiz BUENA era la mala, por un solo fichero: update4.bin.
    Y update4.bin esta nombrado en local.mk:478 dentro de un $(wildcard):

        UPD = $(firstword $(wildcard $(FIRMWARE)/update.bin \
                                     $(FIRMWARE)/update4.bin))

    o sea que NO es un requisito: es una de dos alternativas, puesta ahi
    justamente porque cada rama llama al fichero de otra forma. Exigir las
    dos es exigir que un arbol sea las dos ramas a la vez.

    Lo mismo pasa con $(wildcard $(FIRMWARE)/User/hfdl_*.c) y con las
    cabeceras: un comodin que no casa con nada da la lista vacia y make
    sigue tan tranquilo. Un comodin dice "lo que haya", nunca "esto tiene
    que estar".
    """
    fuera, i = [], 0
    while True:
        j = l.find("$(wildcard", i)
        if j < 0:
            fuera.append(l[i:])
            return "".join(fuera)
        fuera.append(l[i:j])
        # Cerrar el parentesis contando, que dentro hay otros $(...).
        #
        # Empieza en j+1, que es el "(" de "$(wildcard": si empieza en j+2
        # ese parentesis no se cuenta, el primer ")" deja la cuenta en -1 y
        # no cierra nunca. Fallo mio de la primera version; lo saco el
        # propio banco, que siguio diciendo que faltaba update4.bin.
        k, hondo = j + 1, 0
        while k < len(l):
            if l[k] == "(":
                hondo += 1
            elif l[k] == ")":
                hondo -= 1
                if hondo == 0:
                    k += 1
                    break
            k += 1
        i = k


def rutas(raiz_sim):
    """Las rutas bajo $(FIRMWARE) que los makefiles exigen de verdad."""
    pat = re.compile(r"\$\(FIRMWARE\)/([A-Za-z0-9_./-]+)")
    fuera = set()
    for mk in ("local.mk", "Makefile", "GNUmakefile"):
        p = os.path.join(raiz_sim, mk)
        if not os.path.isfile(p):
            continue
        for l in sin_comentarios(p):
            for m in pat.finditer(sin_comodines(l)):
                r = m.group(1)
                if r.endswith(EXT):
                    fuera.add(r)
    return sorted(fuera)


def hermanas(raiz_fw, falta):
    """Carpetas hermanas de la raiz donde SI estan los ficheros que faltan.

    Sin esto el aviso dice "te falta rapido.c" y el usuario tiene que
    adivinar el resto. Con esto dice la orden entera.
    """
    padre = os.path.dirname(os.path.abspath(raiz_fw.rstrip("/\\")))
    buenas = []
    try:
        cand = sorted(os.listdir(padre))
    except OSError:
        return buenas
    for n in cand:
        d = os.path.join(padre, n)
        if not os.path.isdir(d):
            continue
        if os.path.abspath(d) == os.path.abspath(raiz_fw):
            continue
        if not os.path.isdir(os.path.join(d, "User")):
            continue
        if all(os.path.isfile(os.path.join(d, r)) for r in falta):
            buenas.append(d)
    return buenas


def main():
    raiz_fw = sys.argv[1] if len(sys.argv) > 1 else None
    if not raiz_fw:
        print("  uso: raiz_check.py <raiz del firmware>")
        return 2
    raiz_sim = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    r = rutas(raiz_sim)
    #
    # CERO RUTAS ES UN FALLO, NO UN APROBADO.
    #
    # Si el dia de mañana alguien renombra la variable y este guion deja de
    # encontrar nada, lo que NO puede hacer es decir "todo correcto": seria
    # exactamente el fallo que viene a tapar.
    #
    if not r:
        print("  *** raiz_check: no he encontrado NINGUNA ruta $(FIRMWARE)/ en")
        print("      los makefiles. O ha cambiado el nombre de la variable o")
        print("      este guion ya no sabe leerlos. No doy por bueno nada.")
        return 1

    if not os.path.isdir(raiz_fw):
        print("  *** raiz_check: FIRMWARE=%s no existe ***" % raiz_fw)
        return 1

    #
    # DOS CLASES DE RUTA, Y SOLO UNA TUMBA ESTO - 16/10/2026.
    #
    # Este guion existe para cazar una raiz EQUIVOCADA -apuntar el
    # simulador a otro arbol del firmware y medir el codigo que no es-, y
    # para eso le basta con las rutas que viven DENTRO del repositorio.
    #
    # Pero los makefiles nombran ademas dos cosas que en un repositorio
    # recien clonado no estan y no tienen por que estar:
    #
    #   - lo que cuelga de CARPETAS HERMANAS (../docs, ../manual): son
    #     carpetas privadas, no se publican, y los objetivos que las usan
    #     ya se saltan solos cuando faltan (ver local.mk);
    #   - lo que se GENERA al compilar (build/, build_470/).
    #
    # Contarlas como "falta un fichero" hacia que `make comprueba` se
    # pusiera rojo en cualquier clon -medido: cinco rutas, y la tanda
    # entera parada en el primer objetivo-, diciendo ademas que la raiz
    # era otra, que es falso. Un banco que se pone rojo cuando todo esta
    # bien enseña a no mirarlo.
    #
    # Asi que se separan: las de dentro tumban, las de fuera se avisan.
    #
    def de_fuera(x):
        y = x.replace("\\", "/")
        return (y.startswith("../") or y.startswith("build/")
                or y.startswith("build_470/"))

    falta = [x for x in r
             if not os.path.exists(os.path.join(raiz_fw, x)) and not de_fuera(x)]
    aparte = [x for x in r
              if not os.path.exists(os.path.join(raiz_fw, x)) and de_fuera(x)]
    if not falta:
        print("  raiz del firmware: %s (%d rutas, todas presentes)"
              % (raiz_fw, len(r) - len(aparte)))
        if aparte:
            print("  (y %d que no son del repositorio: carpetas hermanas o"
                  % len(aparte))
            print("   ficheros que se generan al compilar. Normal en un clon.)")
        return 0

    print("")
    print("  *** FIRMWARE=%s NO ES EL ARBOL DE ESTE SIMULADOR ***" % raiz_fw)
    print("")
    print("  Los makefiles nombran %d ficheros bajo $(FIRMWARE) y ahi faltan"
          % len(r))
    print("  %d. Los primeros:" % len(falta))
    for x in falta[:12]:
        print("      %s" % x)
    if len(falta) > 12:
        print("      ... y %d mas" % (len(falta) - 12))
    print("")
    b = hermanas(raiz_fw, falta)
    if b:
        print("  Esto si lo tiene todo:")
        for d in b:
            print("      make comprueba FIRMWARE=%s" % d)
    else:
        print("  Y no he encontrado ninguna carpeta hermana que los tenga.")
    print("")
    print("  NO sigas con esta raiz: `make copias` copiaria ESE firmware")
    print("  encima de las copias del simulador y perderias el trabajo.")
    print("")
    return 1


#
# Con el guardian de siempre, y no un `sys.exit(main())` a pelo: desde hoy
# copias_check.py IMPORTA este fichero para preguntarle si la raiz es la
# buena antes de copiar nada. Sin el guardian, el simple import lanzaba
# main() y terminaba con SystemExit - incluido el SystemExit(0) del caso
# bueno, con lo que el guardian habria dicho que no justo cuando tenia que
# decir que si.
if __name__ == "__main__":
    sys.exit(main())
