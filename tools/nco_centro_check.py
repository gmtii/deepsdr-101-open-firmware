#!/usr/bin/env python3
"""
EL PASO POR EL CENTRO DEL NCO: QUE NO SE DESATE.

El 30/10/2026 se quito el agujero de +-NCO_CENTRO_HZ que habia alrededor
del oscilador, porque cruzarlo obligaba a reaparcar y el panorama pegaba un
salto de once kilohercios. A cambio, el dial PUEDE pasar por encima del
oscilador, y eso son tres ficheros que tienen que estar de acuerdo:

  - nco.c       no reaparca dentro de la ventana del centro;
  - demod_am.c  CONGELA el lazo de continua de radiofrecuencia mientras se
                cruza, en vez de medir la emisora y restarsela;
  - main.c      apaga ahi el aprendizaje del corrector de espejo, y lo
                refresca TAMBIEN al sintonizar, no solo al cambiar de modo.

Si alguien deshace uno de los tres, los otros dos se quedan con una premisa
falsa y el fallo no se parece a su causa: la emisora desapareciendo al
cruzar, o el rechazo de imagen de toda la radio cayendose treinta decibelios
sin que nada lo diga. Medido en sim/vhf_espejo.c: 59,9 -> 28,8 dB.

Esto no sustituye al banco; cierra lo que el banco no puede ver, que es que
los tres sigan hablando del mismo numero.
"""
import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
USUARIO = RAIZ / "User"
fallos = []


def lee(nombre):
    return (USUARIO / nombre).read_text(encoding="utf-8")


def sin_comentarios(texto):
    """Quita comentarios conservando los saltos de linea, para que los
    numeros de linea sigan valiendo. Misma regla que idioma_check.py."""
    salida = []
    i = 0
    n = len(texto)
    while i < n:
        if texto.startswith("/*", i):
            fin = texto.find("*/", i + 2)
            fin = n if fin < 0 else fin + 2
            salida.append("".join(c if c == "\n" else " " for c in texto[i:fin]))
            i = fin
        elif texto.startswith("//", i):
            fin = texto.find("\n", i)
            fin = n if fin < 0 else fin
            salida.append(" " * (fin - i))
            i = fin
        else:
            salida.append(texto[i])
            i += 1
    return "".join(salida)


# --- 1. el numero vive en UN solo sitio -------------------------------
definiciones = []
for f in sorted(USUARIO.glob("*.h")) + sorted(USUARIO.glob("*.c")):
    for m in re.finditer(r"^\s*#define\s+NCO_CENTRO_HZ\b", f.read_text(encoding="utf-8"), re.M):
        definiciones.append(f.name)

if definiciones != ["nco.h"]:
    fallos.append(
        "NCO_CENTRO_HZ tiene que estar definido una sola vez y en nco.h; "
        "encontrado en: %s" % (", ".join(definiciones) or "ningun sitio")
    )

# --- 2. nco.c no reaparca dentro de la ventana del centro -------------
nco_c = sin_comentarios(lee("nco.c"))
m = re.search(r"uint32_t nco_lo_aparcado\(.*?\n\}", nco_c, re.S)
if not m:
    fallos.append("no encuentro nco_lo_aparcado() en nco.c")
else:
    cuerpo = m.group(0)
    # La rama del agujero comparaba |off| CONTRA off_min_hz. Las unicas
    # comparaciones legitimas que quedan son contra off_max_hz y la guarda
    # de onda larga (freq_hz <= off_min_hz).
    for linea in cuerpo.splitlines():
        if "off_min_hz" not in linea:
            continue
        if re.search(r"freq_hz\s*<=\s*off_min_hz", linea):
            continue            # la guarda de onda larga, que se queda
        if re.search(r"off_max_hz\s*>\s*off_min_hz", linea):
            continue            # el calculo del salto
        if re.search(r"(off|salto)\s*[<>]=?\s*\(?\s*-?\(?int64_t\)?\s*off_min_hz", linea):
            fallos.append(
                "nco_lo_aparcado() vuelve a comparar la distancia contra "
                "off_min_hz: eso es el agujero del centro, y con el vuelve "
                "el salto del panorama. Linea: %s" % linea.strip()
            )

# --- 3. demod_am.c congela el lazo de continua al cruzar --------------
dem = sin_comentarios(lee("demod_am.c"))
if "NCO_CENTRO_HZ" not in dem:
    fallos.append(
        "demod_am.c ya no mira NCO_CENTRO_HZ: el lazo de continua de "
        "radiofrecuencia mediria la emisora al cruzar el centro y se la "
        "restaria"
    )
if re.search(r"s_rf_dc_i\s*=\s*0\.0f\s*;", dem[dem.find("mide_dc"):] if "mide_dc" in dem else ""):
    fallos.append(
        "demod_am.c vuelve a poner a cero s_rf_dc_i en el camino del "
        "bloque: al cruzar el centro hay que CONGELAR lo medido, no "
        "olvidarlo"
    )

# --- 4. main.c: la puerta del espejo, y refrescada en los dos sitios --
mn = sin_comentarios(lee("main.c"))
if "espejo_aprende_refresca" not in mn:
    fallos.append("main.c ya no tiene espejo_aprende_refresca()")
else:
    g = re.search(r"static void espejo_aprende_refresca\(void\)\s*\{.*?\n\}", mn, re.S)
    if not g:
        fallos.append("no encuentro el cuerpo de espejo_aprende_refresca()")
    elif "NCO_CENTRO_HZ" not in g.group(0):
        fallos.append(
            "espejo_aprende_refresca() ya no mira NCO_CENTRO_HZ: volveria a "
            "aprender con la señal en continua, que es lo que cuesta 31 dB "
            "de rechazo (sim/vhf_espejo.c)"
        )
    llamadas = len(re.findall(r"\bespejo_aprende_refresca\s*\(\s*\)\s*;", mn))
    if llamadas < 2:
        fallos.append(
            "espejo_aprende_refresca() se llama %d vez: hacen falta las dos "
            "-apply_demod_mode() cuando cambia el modo y apply_lo_tune() "
            "cuando cambia la sintonia-, porque las dos entradas de la "
            "puerta cambian en sitios distintos" % llamadas
        )

# Y que nadie vuelva a llamar a iqbal_aprende() por su cuenta, que es como
# se pierde la mitad de la puerta.
cuerpo_puerta = g.group(0) if (("espejo_aprende_refresca" in mn) and g) else ""
sueltas = [
    l.strip()
    for l in mn.splitlines()
    if re.search(r"\biqbal_aprende\s*\(", l) and l.strip() not in cuerpo_puerta
]
if sueltas:
    fallos.append(
        "main.c llama a iqbal_aprende() directamente; tiene que pasar por "
        "espejo_aprende_refresca(), que es quien junta las dos condiciones. "
        "Linea(s): %s" % " | ".join(sueltas)
    )

if fallos:
    print("nco_centro_check: MAL")
    for f in fallos:
        print("  - %s" % f)
    sys.exit(1)

print("nco_centro_check: bien (el centro se cruza y los tres ficheros lo saben)")
