#!/usr/bin/env python3
"""
Genera sim/cambios_generado.h: los instantes UTC en que Europe/Madrid cambia
de hora, sacados de la base de datos de zonas horarias del sistema (zoneinfo),
que es una fuente INDEPENDIENTE de la regla que implementa el firmware.

Se genera y no se escribe a mano a proposito: una tabla de 200 fechas copiada
a mano es justo donde se cuela la errata que el banco luego bendice.
"""
import sys
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo

MAD = ZoneInfo("Europe/Madrid")

def es_verano(dt_utc):
    """1 si a ese instante UTC Madrid va en horario de verano."""
    return int(dt_utc.astimezone(MAD).dst() != timedelta(0))

filas = []
# Se muestrea DENSO donde puede fallar y poco en el resto: el dia del cambio
# entero hora a hora (que es donde una regla mal escrita se equivoca por una
# hora), el dia anterior y el siguiente en sus extremos (donde se equivocaria
# por un dia), y un dia de cada mes al mediodia para cubrir el resto del año.
#
# Muestrear las 8.760 horas de cada año daria un fichero de 15 MB para
# comprobar lo mismo.
for anno in range(2000, 2100):
    for mes in (3, 10):
        # el ultimo domingo de ese mes
        d = datetime(anno, mes, 31, tzinfo=timezone.utc)
        while d.weekday() != 6:
            d -= timedelta(days=1)
        for h in range(24):
            filas.append((d + timedelta(hours=h), 0))
        for off in (-24, -1, 24, 47):
            filas.append((d + timedelta(hours=off), 0))
    for mes in range(1, 13):
        filas.append((datetime(anno, mes, 15, 12, tzinfo=timezone.utc), 0))

filas = [(d, es_verano(d)) for d, _ in filas]

sal = sys.argv[1] if len(sys.argv) > 1 else "sim/cambios_generado.h"
with open(sal, "w") as f:
    f.write("/* GENERADO por tools/gen_cambios.py - no editar a mano.\n"
            " * Fuente: zoneinfo Europe/Madrid del sistema. */\n")
    f.write("typedef struct { unsigned char d, m, a, h, v; } cambio_t;\n")
    f.write("static const cambio_t k_cambios[] = {\n")
    for d, v in filas:
        f.write("  {%2d,%2d,%2d,%2d,%d},\n" % (d.day, d.month, d.year - 2000, d.hour, v))
    f.write("};\n")
    f.write("#define N_CAMBIOS %d\n" % len(filas))
print("%s: %d instantes" % (sal, len(filas)))
