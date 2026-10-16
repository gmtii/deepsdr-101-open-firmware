#!/usr/bin/env python3
"""
Genera sim/husos_generado.h: para cada una de las cinco reglas de horario de
verano de User/husos.c, los instantes UTC de una zona REAL que la sigue, con
lo que dice zoneinfo que vale el horario de verano en cada uno.

Igual que tools/gen_cambios.py, y por lo mismo: la fuente es la base de datos
de husos del sistema, que es INDEPENDIENTE de la regla que lleva escrita el
firmware, y la tabla se genera porque doscientas fechas copiadas a mano son
justo donde se cuela la errata que luego el banco bendice.

EL RANGO DE AÑOS DE CADA REGLA NO ES EL MISMO, y eso no es un detalle: las
reglas de verano se cambian. Estados Unidos movio las suyas en 2007, Chile
las ha movido media docena de veces y estuvo dos años sin invierno. Lo que
hay en husos.c es LA REGLA DE HOY, asi que compararla con 1998 solo diria
que en 1998 era otra. Aqui se declara desde cuando vale cada una y ademas se
COMPRUEBA, viendo que en todo ese rango zoneinfo cambia siempre el mismo
domingo que predice la regla. Si algun pais vuelve a moverla, esto deja de
generar y hay que mirarlo.
"""
import sys
from datetime import date, datetime, timedelta, timezone
from zoneinfo import ZoneInfo

# regla C, nombre, zona real, primer año en que vale la regla de hoy
REGLAS = [
    (1, "EU",  "Europe/Madrid",      2000),
    (2, "USA", "America/New_York",   2007),
    (3, "AU",  "Australia/Sydney",   2008),
    (4, "NZ",  "Pacific/Auckland",   2008),
    (5, "CL",  "America/Santiago",   2023),
]
ANNO_FIN = 2099


def es_verano(z, dt_utc):
    return int(dt_utc.astimezone(z).dst() != timedelta(0))


def transiciones(z, anno):
    """Los instantes UTC en que esa zona cambia de regimen ese año."""
    d = datetime(anno, 1, 1, tzinfo=timezone.utc)
    fin = datetime(anno + 1, 1, 1, tzinfo=timezone.utc)
    v = es_verano(z, d)
    out = []
    while d < fin:
        d2 = d + timedelta(hours=1)
        v2 = es_verano(z, d2)
        if v2 != v:
            out.append(d2)
            v = v2
        d = d2
    return out


def domingo(d0, mes, anno):
    """El primer domingo que cae el dia d0 de ese mes o despues."""
    d = date(anno, mes, d0)
    while d.weekday() != 6:
        d += timedelta(days=1)
    return d.day


# Las mismas cuatro cifras que k_reglas[] en User/husos.c. Estan aqui OTRA VEZ,
# a proposito: si alguien cambia la tabla del firmware y no esta, el banco
# seguiria verde comparando contra si mismo.
CIFRAS = {
    "EU":  (3, 25, 10, 25),
    "USA": (3,  8, 11,  1),
    "AU": (10,  1,  4,  1),
    "NZ":  (9, 24,  4,  1),
    "CL":  (9,  2,  4,  2),
}

filas = []
for num, nom, zona, anno0 in REGLAS:
    z = ZoneInfo(zona)
    m_ini, n_ini, m_fin, n_fin = CIFRAS[nom]

    # 1. Que la regla de hoy explique TODO el rango que decimos que explica.
    for anno in range(anno0, 2041):          # 2041: hasta donde llega la base
        tr = transiciones(z, anno)
        if len(tr) != 2:
            sys.exit("%s %d: zoneinfo da %d cambios, no 2" % (nom, anno, len(tr)))
        esperados = sorted([date(anno, m_ini, domingo(n_ini, m_ini, anno)),
                            date(anno, m_fin, domingo(n_fin, m_fin, anno))])
        vistos = sorted([t.date() for t in tr])
        # El cambio cae de madrugada LOCAL, asi que el instante en UTC puede
        # estar en el dia de al lado -en Sidney son catorce horas antes-. Lo
        # que se comprueba aqui es que la regla acierta EL DOMINGO; que el
        # firmware convierta bien la hora lo comprueba el banco en C, contra
        # estas mismas muestras hora a hora.
        for de, dv in zip(esperados, vistos):
            if abs((de - dv).days) > 1:
                sys.exit("%s %d: la regla dice %s y zoneinfo %s"
                         % (nom, anno, de, dv))
        for t in tr:
            d0 = t.astimezone(z).dst()
            if d0 not in (timedelta(0), timedelta(hours=1)):
                sys.exit("%s %d: el salto no es de una hora (%s)" % (nom, anno, d0))

    # 2. Las muestras. Denso en los dos dias del cambio -que es donde una
    #    regla mal escrita se equivoca por un dia- y un dia de cada mes para
    #    el resto del año.
    for anno in range(anno0, 2041):
        for t in transiciones(z, anno):
            base = t.replace(hour=0, minute=0)
            for h in range(-24, 48):
                filas.append((num, base + timedelta(hours=h)))
        for mes in range(1, 13):
            filas.append((num, datetime(anno, mes, 15, 12, tzinfo=timezone.utc)))

zs = {num: ZoneInfo(z) for num, _, z, _ in REGLAS}
bases = {}
for num, nom, zona, anno0 in REGLAS:
    z = ZoneInfo(zona)
    # el huso de INVIERNO: el 1 de enero para el norte, el 1 de julio para el
    # sur. Se coge el MENOR de los dos, que es el de invierno sea cual sea el
    # hemisferio, y asi no hay que saber en cual esta la zona.
    o1 = datetime(2030, 1, 1, tzinfo=timezone.utc).astimezone(z).utcoffset()
    o7 = datetime(2030, 7, 1, tzinfo=timezone.utc).astimezone(z).utcoffset()
    bases[num] = int(min(o1, o7).total_seconds() // 60)
sal = sys.argv[1] if len(sys.argv) > 1 else "sim/husos_generado.h"
with open(sal, "w") as f:
    f.write("/* GENERADO por tools/gen_husos.py - no editar a mano.\n"
            " * Fuente: zoneinfo del sistema. Ver el .py para el rango de\n"
            " * años que cubre cada regla y por que no son el mismo. */\n")
    f.write("typedef struct { unsigned char r, d, m, a, h, v; } huso_caso_t;\n")
    f.write("static const huso_caso_t k_husos[] = {\n")
    for num, d in filas:
        f.write("  {%d,%2d,%2d,%3d,%2d,%d},\n"
                % (num, d.day, d.month, d.year - 2000, d.hour,
                   es_verano(zs[num], d)))
    f.write("};\n")
    f.write("#define N_HUSOS %d\n" % len(filas))
    f.write("/* El huso de invierno de la zona de cada regla, en minutos:\n"
            " * la regla sola no basta para decidir, hay que convertir. */\n")
    f.write("static const short k_huso_base[] = { 0")
    for num in sorted(bases):
        f.write(", %d" % bases[num])
    f.write(" };\n")
print("%s: %d instantes" % (sal, len(filas)))
