#!/usr/bin/env python3
"""
EL ORDEN DE ARRANQUE, VIGILADO - 07/10/2026, de un issue.

QUE SE ROMPIO. settings.c aplicaba `spectrum_palette` DENTRO de la lectura
del fichero, con este comentario encima:

    "applied DIRECTLY here ... there is no ordering hazard"

Era CIERTO el 08/09/2026. Dejo de serlo el 23/09/2026, cuando los temas
pasaron a llevar su propia paleta de cascada y tema_aplicar() empezo a llamar
a spectrum_set_palette(). Desde ese dia el arranque hacia esto:

    1. leer CONFIG.CSV   -> poner TURBO        (lo que el usuario guardo)
    2. aplicar el tema   -> poner SMOKE        (lo que el tema propone)

y el usuario veia "la configuracion se ignora". No se ignoraba: se leia y se
machacaba. Nadie lo vio porque el comentario que lo autorizaba seguia ahi,
diciendo que no habia peligro, dos semanas despues de que lo hubiera.

QUE VIGILA ESTO, y son las dos mitades del arreglo:

  1. settings.c NO aplica la paleta: la guarda con su par have_/valor.
  2. main.c la aplica DESPUES de tema_aplicar(), no antes.

Un comentario no puede comprobar nada. Esto si.
"""
import re, sys, pathlib

def main(raiz):
    u = pathlib.Path(raiz) / "User"
    fallos = []

    s = (u / "settings.c").read_text(encoding="utf-8", errors="replace")
    # fuera de comentarios: buscar llamadas reales
    sin_com = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    sin_com = re.sub(r"//[^\n]*", "", sin_com)
    if "spectrum_set_palette(" in sin_com:
        fallos.append("settings.c vuelve a aplicar la paleta durante la lectura "
                      "del fichero: el tema la pisara despues")
    if "out->have_spectrum_palette" not in sin_com:
        fallos.append("settings.c ya no guarda have_spectrum_palette: "
                      "la paleta del fichero no llegaria a main.c")

    m = (u / "main.c").read_text(encoding="utf-8", errors="replace")
    m_sin = re.sub(r"/\*.*?\*/", "", m, flags=re.S)
    i_tema = m_sin.find("tema_aplicar(s_loaded_settings.tema_idx)")
    i_pal  = m_sin.find("s_loaded_settings.spectrum_palette")
    if i_tema < 0:
        fallos.append("no encuentro tema_aplicar(s_loaded_settings.tema_idx) en main.c")
    if i_pal < 0:
        fallos.append("main.c no aplica s_loaded_settings.spectrum_palette: "
                      "la paleta guardada no se restaura")
    if i_tema >= 0 and i_pal >= 0 and i_pal < i_tema:
        fallos.append("main.c aplica la paleta ANTES del tema: el tema la pisa. "
                      "Es exactamente el fallo del issue.")

    print("  orden de arranque: tema -> paleta de la cascada")
    if not fallos:
        print("    ok: settings.c la guarda y no la aplica")
        print("    ok: main.c la aplica DESPUES del tema")
        print("  TODO CORRECTO")
        return 0
    for f in fallos:
        print("    *** MAL *** " + f)
    print("  *** %d FALLOS ***" % len(fallos))
    return 1

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
