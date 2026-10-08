#!/usr/bin/env python3
"""
COBERTURA DEL MANUAL.

*** El dueno, el 06/10/2026: "en el manual tienen que aparecer TODAS LAS
FUNCIONALIDADES, asi que te coges el codigo fuente y vas una a una". ***

POR QUE EXISTE
--------------
El manual se escribio mirando pantallas, y asi solo entra lo que esta
DIBUJADO: los seis botones de la barra, las celdas de ajustes, la rejilla de
modos. Lo que no tiene rotulo -tocar la frecuencia para teclearla, tocar la
hora, mantener la esquina cuatro segundos para una captura- no entraba, y el
agujero solo aparecia cuando el dueno se daba de bruces con el. Tres en una
tarde ya no son despistes: son un metodo que no sirve.

QUE MIDE, Y QUE NO
------------------
Saca del firmware todas las cadenas que pueden acabar en la pantalla -las que
pasan por tr() o por T()- y dice cuales NO se nombran en el manual.

No dice si estan bien explicadas. Dice si estan MENCIONADAS, que es lo unico
que una maquina puede decidir y es exactamente el fallo que teniamos.

LO QUE NO CUENTA
----------------
Los ficheros de DATOS -nombres de pais, de emisora, de avion-: ahi las cadenas
son contenido, no interfaz, y meterlas ahogaria la senal.

Y quedan fuera por diseno los textos de diagnostico que el manual explica EN
GRUPO y no uno a uno (los contadores del panel de STANAG, las lineas de medida
de Ident, los codigos de error del disco). Por eso este banco no exige el 100%:
exige que nadie pueda anadir una PANTALLA nueva sin que salga aqui.

Y COMPRUEBA ADEMAS QUE EL MANUAL SEA UNO - 08/10/2026
-----------------------------------------------------
*** El dueno: "DeepSDR_Reload_Manual_de_uso.docx y para que pollas tienes
esto joder". ***

Habia TRES MANUAL_V2_ES.md en el arbol, con tres contenidos distintos y los
tres de 48.527 bytes:

    manual/   07/10 08:26   V3.72 y 28 firmas   <- el que escribe build.js
    raiz      06/10 17:04   V3.62 y 28 firmas
    docs/     06/10 13:04   V3.62 y 27 firmas   <- Y ERA EL QUE MEDIA ESTE
                                                   SCRIPT

Y dos .docx, con el de la raiz 215 kB mas corto que el bueno: el dueno
llevaba dias abriendo el manual de anteayer.

O sea la misma enfermedad que copias_check.py, por cuarta vez: build.js
escribe en manual/ y alguien -yo- fue repartiendo copias. Un banco que mide
una copia miente, y este llevaba dos dias mintiendo sobre la tabla de
firmas.

Asi que aqui abajo hay dos comprobaciones y no una:

  1. la cobertura de siempre, ahora contra manual/MANUAL_V2_ES.md,
  2. que no exista ninguna segunda copia del .md ni del .docx fuera de
     manual/. Con eso no se puede volver a hacer.
"""
import os, re, sys, unicodedata

FW  = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "User")
MAN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "manual", "MANUAL_V2_ES.md")

# cadenas de datos, no de interfaz
SALTA = {'dxcc.c','rds_pi.c','icao24_db.c','dcf77.c','emisoras.c','mapa_datos.c',
         'paises.c','hfdl_aircraft.c','volmsg_tabla.c'}

pat = re.compile(r'\b(?:tr|T)\s*\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)')

def norm(s):
    s = s.replace('\\"','"')
    s = re.sub(r'\\x[0-9A-Fa-f]{2}', '', s)
    s = unicodedata.normalize('NFKD', s)
    s = ''.join(c for c in s if not unicodedata.combining(c))
    return re.sub(r'[^a-z0-9]+', ' ', s.lower()).strip()

man = norm(open(MAN, encoding='utf-8').read())

vistas = {}
for f in sorted(os.listdir(FW)):
    if not f.endswith('.c') or f in SALTA: continue
    txt = open(os.path.join(FW,f), encoding='utf-8', errors='replace').read()
    for m in pat.finditer(txt):
        es = m.group(1)
        if not es.strip(): continue
        vistas.setdefault(es, set()).add(f)

falta, hay = [], 0
for es, ficheros in sorted(vistas.items()):
    n = norm(es)
    if not n or len(n) < 3: continue
    if n in man: hay += 1
    else: falta.append((es, ','.join(sorted(ficheros))))

print(f"cadenas de interfaz: {len(vistas)}   mencionadas: {hay}   NO: {len(falta)}")

# La lista entera solo cuando hace falta: son 171 renglones y este banco
# corre en cada "make comprueba". Con --todo se ve siempre.
mal = 0
SUELO_PREVIO = None   # se fija abajo; aqui solo para leerlo en orden

def lista():
    print()
    for es, f in falta:
        print(f"  {es!r:48} {f}")

# ------------------------------------------------------------------
# EL TRINQUETE.
#
# Este banco era un INFORME: lo imprimia todo y salia con 0 pasara lo que
# pasara, o sea que nadie lo miraba y encima no lo corria nadie (no estaba
# en ningun Makefile). Un informe que no falla no es un banco.
#
# No se puede exigir el 100%: hay 171 cadenas sin mencionar y la mayoria son
# lineas de diagnostico que el manual explica en grupo. Lo que si se puede
# exigir es que NO SUBAN. Si alguien anade una pantalla y no la documenta,
# el numero crece y esto se pone rojo.
#
# Para bajar el suelo: documenta y baja el numero. Nunca al contrario.
# ------------------------------------------------------------------
SUELO = 171     # medido el 08/10/2026 contra manual/MANUAL_V2_ES.md

if '--todo' in sys.argv:
    lista()

if len(falta) > SUELO:
    if '--todo' not in sys.argv:
        lista()
    print(f"\nMAL: {len(falta)} cadenas sin mencionar y el suelo son {SUELO}.")
    print("     Alguien ha anadido pantalla sin tocar el manual. Las nuevas")
    print("     son las de la lista de arriba que no reconozcas.")
    mal += 1
elif len(falta) < SUELO:
    print(f"\nOJO: ahora faltan {len(falta)} y el suelo sigue en {SUELO}.")
    print(f"     Baja SUELO a {len(falta)} en este fichero para que no se")
    print("     pueda volver atras.")

# ------------------------------------------------------------------
# QUE EL MANUAL SEA UNO. Ver la cabecera.
# ------------------------------------------------------------------
RAIZ   = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
UNICOS = ("MANUAL_V2_ES.md", "DeepSDR_Reload_Manual_de_uso.docx",
          "DeepSDR_Reload_Manual_de_uso.pdf")
BUENO  = os.path.normpath(os.path.join(RAIZ, "manual"))

sobran = []
for dd, dirs, ficheros in os.walk(RAIZ):
    dirs[:] = [d for d in dirs if d not in ('build', '.git', '__pycache__')]
    if os.path.normpath(dd) == BUENO:
        continue
    for n in ficheros:
        if n in UNICOS:
            sobran.append(os.path.relpath(os.path.join(dd, n), RAIZ))

if sobran:
    print("\nMAL: el manual lo escribe manual/build.js en manual/ y nada mas.")
    print("     Estas copias sobran y se separan solas; borralas:")
    for n in sorted(sobran):
        print(f"       {n}")
    mal += 1

if mal:
    sys.exit(1)
print("\nMANUAL: todo correcto")
