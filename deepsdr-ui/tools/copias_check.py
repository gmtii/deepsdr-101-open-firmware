#!/usr/bin/env python3
"""
Comprueba que las copias que el simulador tiene del firmware no se han
quedado atras. Con --sincronizar, las pone al dia.

POR QUE HACE FALTA, Y POR QUE ESTA ES LA TERCERA VEZ
----------------------------------------------------
El simulador compila su propia copia de una parte del firmware: gfx2/*.c y
*.h, vendor/*.c. Son cuarenta ficheros. Ninguno esta obligado a estar al dia
con el original, asi que se separan solos y sin ruido.

Ya ha pasado tres veces:
  - vendor/spectrum.c llevaba meses divergido; se detecto por casualidad al
    preguntarse como se dibuja una punta, y la respuesta salia de un codigo
    que ya no era el que corria.
  - gfx2/gfx2.h se quedo sin el "#ifndef GFX2_BAND_H" que el firmware si
    tiene, o sea que el simulador no puede forzar la altura de banda aunque
    el firmware si.
  - Y el 23/09/2026, al ensanchar la rejilla de ajustes de 9 a 12 celdas: se
    edito User/ui_cfg.h, el banco de geometria dijo "TODO CORRECTO" y lo que
    habia comprobado era la copia vieja de 3 x 200 px. Un banco que mide una
    copia es un banco que miente, y eso es peor que no tener banco.

Este script no quita la duplicacion -no se pueden borrar ficheros en la
maquina donde vive el proyecto- pero la hace imposible de ignorar: si una
copia se ha quedado atras, "make comprueba" para y dice cual.
"""
import sys, os, filecmp, shutil

def pares(raiz_fw, raiz_sim):
    """(copia_en_el_simulador, original_en_el_firmware) de todo lo duplicado."""
    fuera = []
    for d in ("gfx2", "vendor"):
        dd = os.path.join(raiz_sim, d)
        if not os.path.isdir(dd):
            continue
        for n in sorted(os.listdir(dd)):
            if not n.endswith((".c", ".h")):
                continue
            orig = os.path.join(raiz_fw, "User", n)
            if os.path.isfile(orig):
                fuera.append((os.path.join(dd, n), orig))
    # Y EL UTILLAJE DE DATOS - 05/10/2026, CUARTA VEZ.
    #
    # emisoras_pack.py, datos_pack.py e icao24_recorta.py vivian SOLO aqui,
    # en el simulador. En el repositorio que se publica estaba icao24.py, y
    # su comentario de cabecera documenta un flujo de TRES ordenes:
    #
    #     python3 emisoras_pack.py eibi.txt aoki.txt EMISORAS.BIN 2
    #     python3 icao24.py <el CSV de OpenSky> ICAO24.BIN 827392
    #     python3 datos_pack.py BD.BIN ICAO24.BIN EMISORAS.BIN
    #
    # o sea que el repositorio explicaba como hacerse el BD.BIN y faltaban
    # dos de los tres guiones. Lo encontro alguien de fuera, desde Hungria,
    # intentando precisamente eso. Ahora el original vive en el firmware
    # -que es lo que se publica- y la copia de aqui la vigila este banco,
    # igual que las de gfx2/.
    for n in ("emisoras_pack.py", "datos_pack.py", "icao24_recorta.py",
              "emisoras_check.py", "icao24.py", "novedades_check.py",
              "mando_check.py", "paises_xy.csv", "sitios_xy.csv"):
        orig = os.path.join(raiz_fw, "tools", n)
        if os.path.isfile(orig):
            fuera.append((os.path.join(raiz_sim, "tools", n), orig))
    return fuera

def main():
    raiz_fw = None
    sincronizar = False
    for a in sys.argv[1:]:
        if a == "--sincronizar":
            sincronizar = True
        else:
            raiz_fw = a
    if not raiz_fw:
        print("  uso: copias_check.py <raiz del firmware> [--sincronizar]")
        return 2

    raiz_sim = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    p = pares(raiz_fw, raiz_sim)

    #
    # *** CERO PAREJAS ES UN FALLO, NO UN APROBADO - 10/10/2026. ***
    #
    # pares() solo apunta una pareja si EL ORIGINAL EXISTE. Asi que con una
    # raiz equivocada -FIRMWARE=/donde/sea- no encuentra ningun original, la
    # lista sale vacia, no hay ninguna "mala" y esto imprimia:
    #
    #     0 ficheros del firmware duplicados en el simulador
    #     ninguno se ha quedado atras
    #
    # y devolvia 0. Verde. Habiendo comprobado NADA.
    #
    # Es la quinta vez de la misma familia en este proyecto -ram_mapa.py con
    # COMMON, pila.py mirando donde no era, modos_check.py diciendo "no hay
    # modos", el banco de geometria midiendo la copia vieja- y siempre es el
    # mismo descuido: confundir "no he encontrado nada malo" con "no he
    # encontrado nada". Un banco que no ha medido nada tiene que decirlo a
    # gritos.
    #
    if not p:
        print("")
        print("  *** copias_check: NO HE COMPARADO NADA ***")
        print("")
        print("  No he encontrado en %s ni uno solo de los originales"
              % raiz_fw)
        print("  que el simulador duplica. O esa no es la raiz del firmware")
        print("  o ha cambiado de sitio. Lo que NO voy a hacer es decir que")
        print("  todo esta bien sin haber mirado un solo fichero.")
        print("")
        print("  Y sobre todo: NO lances `make copias` con esta raiz.")
        print("")
        return 1

    malas = [(c, o) for c, o in p
             if not (os.path.isfile(c) and filecmp.cmp(c, o, shallow=False))]

    print("  %d ficheros del firmware duplicados en el simulador" % len(p))
    if not malas:
        print("  ninguno se ha quedado atras")
        return 0

    for c, o in malas:
        print("    %-24s distinto de %s" % (os.path.relpath(c, raiz_sim), o))
    if sincronizar:
        #
        # *** LA ORDEN DE ARREGLARLO ERA LA QUE MAS DANO HACIA. ***
        #
        # `make copias` copia del firmware AL simulador. Con la raiz
        # equivocada -y el valor por defecto del Makefile, ../deepsdr, es
        # la equivocada desde que existe deepsdr-reload- esto no avisa de
        # nada: copia el firmware de septiembre encima de las copias buenas
        # y se lleva el trabajo por delante en silencio.
        #
        # El aviso de "cero parejas" de arriba no cubre este caso, porque
        # la rama vieja SI tiene gfx.c y spectrum.c: encuentra parejas, las
        # ve distintas, y machaca. Asi que antes de escribir un solo byte
        # se pregunta a raiz_check.py, que es quien sabe distinguir un
        # arbol de otro.
        #
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        try:
            import raiz_check
        except ImportError:
            print("  *** no encuentro raiz_check.py: no copio nada ***")
            return 1
        falta = [x for x in raiz_check.rutas(raiz_sim)
                 if not os.path.exists(os.path.join(raiz_fw, x))]
        if falta:
            print("")
            print("  *** NO COPIO NADA: %s no es el arbol de este" % raiz_fw)
            print("      simulador (le faltan %d ficheros que los makefiles"
                  % len(falta))
            print("      nombran, el primero %s). ***" % falta[0])
            print("")
            print("  Copiar de ahi se llevaria por delante las copias")
            print("  buenas. Lanza antes:  python3 tools/raiz_check.py %s"
                  % raiz_fw)
            print("")
            return 1
        for c, o in malas:
            shutil.copyfile(o, c)
        print("  %d copias puestas al dia desde el firmware" % len(malas))
        return 0

    print("  *** %d COPIAS SE HAN QUEDADO ATRAS ***" % len(malas))
    print("  El simulador esta midiendo codigo que ya no es el que corre.")
    print("  Se arregla con:  make copias FIRMWARE=%s" % raiz_fw)
    return 1

sys.exit(main())
