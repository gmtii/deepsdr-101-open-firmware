#!/usr/bin/env python3
"""
LA TABLA DE CANALES DE HFDL, Y LO QUE LA REJILLA DA POR HECHO.

Pedido por el usuario.

De ahi salio que Bahrein no estaba en las presintonias -no hace falta que
este para decodificar, el nombre de la estacion viaja dentro de la trama,
pero teclear la frecuencia a mano es un estorbo- y se añadio como un tercer
grupo. Y al añadirlo quedo una suposicion sin vigilar.

LO QUE LA REJILLA DA POR HECHO, Y NADIE COMPROBABA
--------------------------------------------------
grid_page_info() reparte las celdas asi: recorre la tabla acumulando TRAMOS
DE FILAS DEL MISMO GRUPO y parte cada tramo en paginas de doce. O sea que
una fila colocada fuera de su tramo no da un error: abre una pagina nueva
de UNA celda, y la siguiente del mismo grupo abre otra. Nadie se entera
hasta que alguien mira la pantalla y ve tres paginas donde deberia haber
una.

Un _Static_assert no puede con esto -no cabe un bucle- asi que lo mira esto.

QUE SE COMPRUEBA, Y POR QUE CADA COSA

  - LOS GRUPOS VAN SEGUIDOS. Es lo que la rejilla necesita, y lo unico que
    puede romperse al añadir una frecuencia en el sitio "logico" por
    numero en vez de en el sitio de su grupo.
  - DENTRO DE CADA GRUPO, LAS FRECUENCIAS SUBEN. El boton "Freq." salta a
    la siguiente de la tabla, asi que un orden roto se nota al usarlo: dos
    toques y estas mas abajo que antes.
  - NO HAY FRECUENCIAS REPETIDAS. Una repetida parte en dos el contador de
    aciertos de esa frecuencia -s_hfdl_aciertos va por indice- y entonces
    el "que frecuencia me funciona mejor" miente.
  - LA NOCHE ES DE NOCHE Y EL DIA ES DE DIA. Las dos paginas del Atlantico
    norte se llaman "bajo 10 MHz" y "sobre 10 MHz" EN LA PANTALLA. Si una
    fila se cuela en el grupo que no es, el rotulo pasa a ser mentira.
  - Y QUE CADA GRUPO TENGA NOMBRE. Un grupo sin rotulo sale como una
    pagina sin titulo, que es de las cosas que se cuelan porque el que
    añade la fila no mira grid_grupo_nombre().

Lo que NO comprueba: si las frecuencias son las de verdad. Eso sale de la
tabla de dumphfdl y no de aqui; aqui solo se mira que la tabla sea
coherente consigo misma y con lo que la pantalla promete.
"""
import re
import sys

GRUPOS = {"HFDL_GRUPO_NOCHE": 0, "HFDL_GRUPO_DIA": 1, "HFDL_GRUPO_BAHREIN": 2}


def main():
    raiz = sys.argv[1] if len(sys.argv) > 1 else "."
    ruta = raiz + "/User/main.c"
    try:
        txt = open(ruta, encoding="utf-8", errors="replace").read()
    except OSError:
        print("  hfdl_canales: no puedo abrir %s" % ruta)
        return 2

    m = re.search(r"static const hfdl_canal_t k_hfdl_canales\[\]\s*=\s*\{(.*?)\n\};",
                  txt, re.S)
    if not m:
        print("  *** hfdl_canales: no encuentro k_hfdl_canales[] en main.c ***")
        print("      O ha cambiado de nombre o este guion ya no sabe leerlo.")
        print("      Lo que NO voy a hacer es dar por buena una tabla que no he visto.")
        return 1

    filas = re.findall(r"\{\s*(\d+)\s*,\s*\"([^\"]*)\"\s*,\s*([A-Z_0-9]+)\s*\}",
                       m.group(1))
    if not filas:
        print("  *** hfdl_canales: la tabla esta, pero no he sacado ni una fila ***")
        return 1

    fallos = 0
    print("  %d canales de HFDL" % len(filas))

    # 1. los grupos van seguidos
    orden = []
    for _k, _e, g in filas:
        if not orden or orden[-1] != g:
            orden.append(g)
    if len(orden) != len(set(orden)):
        print("    *** MAL *** los grupos NO van seguidos: %s" % " ".join(orden))
        print("        grid_page_info() parte la tabla en tramos del mismo grupo:")
        print("        una fila suelta abre una pagina de una celda.")
        fallos += 1
    else:
        print("    los %d grupos van seguidos                     ok" % len(orden))

    # 2. dentro de cada grupo, suben
    mal = []
    for g in orden:
        khz = [int(k) for k, _e, gg in filas if gg == g]
        if khz != sorted(khz):
            mal.append(g)
    if mal:
        print("    *** MAL *** no suben dentro de: %s" % " ".join(mal))
        fallos += 1
    else:
        print("    dentro de cada grupo las frecuencias suben    ok")

    # 3. sin repetidas
    todas = [int(k) for k, _e, _g in filas]
    rep = sorted(set(x for x in todas if todas.count(x) > 1))
    if rep:
        print("    *** MAL *** frecuencias repetidas: %s"
              % " ".join(str(x) for x in rep))
        fallos += 1
    else:
        print("    ninguna frecuencia repetida                   ok")

    # 4. la noche es de noche y el dia es de dia
    noche = [int(k) for k, _e, g in filas if g == "HFDL_GRUPO_NOCHE"]
    dia = [int(k) for k, _e, g in filas if g == "HFDL_GRUPO_DIA"]
    malas = [x for x in noche if x >= 10000] + [x for x in dia if x < 10000]
    if malas:
        print("    *** MAL *** el rotulo de la pagina mentiria en: %s"
              % " ".join(str(x) for x in malas))
        fallos += 1
    else:
        print("    noche < 10 MHz y dia >= 10 MHz                 ok")

    # 5. cada grupo con nombre
    #
    # *** Y AQUI ME CAZO A MI MISMO EN LA PRIMERA EJECUCION. ***
    #
    # La primera version buscaba "case GRID_HFDL:" y se paraba en el primer
    # "default:". Hay TRES "case GRID_HFDL:" en main.c -en grid_total(), en
    # grid_grupo() y en grid_grupo_nombre()- y la busqueda caia en el
    # primero, que no tiene ni un rotulo. Salia rojo con la tabla bien.
    #
    # Es el mismo fallo que ya tuvo escala_check.py dos veces: anclar en la
    # primera aparicion de algo que aparece varias veces. Se ancla en LA
    # FUNCION, que solo hay una.
    #
    # Y no se busca el nombre de cada constante, porque el ultimo grupo es
    # el `else` de la cadena y ahi no aparece ninguna: se cuentan los tr()
    # de ese tramo, que son los rotulos de verdad.
    cuerpo = re.search(r"static const char \*grid_grupo_nombre\(.*?\n\}",
                       txt, re.S)
    rot = 0
    if cuerpo:
        tramo = re.search(r"case GRID_HFDL:(.*?)(?:\n    case |\n    default:)",
                          cuerpo.group(0), re.S)
        if tramo:
            rot = len(re.findall(r"\btr\(", tramo.group(1)))
    desconocidos = [g for g in orden if g not in GRUPOS]
    if desconocidos:
        print("    *** MAL *** grupos que este guion no conoce: %s"
              % " ".join(desconocidos))
        print("        Añadelos a GRUPOS aqui arriba, o el rotulo no se vigila.")
        fallos += 1
    elif rot < len(orden):
        print("    *** MAL *** %d grupos y solo %d rotulos en grid_grupo_nombre()"
              % (len(orden), rot))
        print("        Una pagina sin titulo sale en blanco y nadie la reclama.")
        fallos += 1
    else:
        print("    los %d grupos tienen rotulo                   ok" % len(orden))

    if fallos:
        print("  *** %d FALLOS ***" % fallos)
        return 1
    return 0


sys.exit(main())
