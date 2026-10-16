#!/usr/bin/env python3
"""
QUE NO HAYA UN CARGADOR VIEJO RONDANDO POR EL ARBOL.

Pedido por el usuario, 08/10/2026.

LO QUE PASO
-----------
customboot.bin vive en TRES sitios:

    boot/customboot.bin      41.172   <- lo que sale de compilar
    customboot.bin (raiz)    41.136   <- la copia comoda, de hace dos horas
    rescate/customboot.bin   40.544   <- la copia congelada, de hace dias

Y el apartado "Como se entrega" del README decia, al pie de la letra:

    C:\\...\\deepsdr-reload\\rescate\\customboot.bin  <- el cargador

O sea que siguiendo las propias instrucciones del proyecto se grababa en la
radio un cargador de DOS VERSIONES ATRAS. Y eso no es un despiste de redactar:
es la misma enfermedad de copias_check.py y de manual_check.py, por quinta vez
en el proyecto. Un fichero que se duplica a mano se separa solo y sin ruido, y
el dia que importe no avisa nadie.

Lo de la flash es peor que lo del manual, ademas. Un manual atrasado se lee
raro; un cargador atrasado se GRABA, y el cargador es la unica pieza que, si
sale mal, deja la radio sin forma de arreglarse por USB. Que es justo lo que
este proyecto entero esta evitando desde el primer dia: no se abre la radio.

QUE COMPRUEBA
-------------
Recorre el arbol buscando ficheros llamados customboot.bin. El que sale de
compilar -boot/customboot.bin- es el bueno; cualquier otra copia tiene que ser
IDENTICA byte a byte, o esto se pone rojo y dice cual y de cuanto.

Y NO exige que las copias existan. Una copia que no esta no engana a nadie; la
que engana es la que esta y esta vieja. Asi el arbol de quien no quiera copias
pasa igual.

LA EXCEPCION, QUE ES DE VERDAD
------------------------------
rescate/ esta exenta A PROPOSITO y no por comodidad: es la copia CONGELADA,
la que se sabe buena, la que se graba cuando lo nuevo ha salido mal. Si se
actualizara con cada entrega dejaria de ser un rescate y seria otra copia mas
del mismo binario. Por eso se nombra en la salida en vez de callarla: que se
vea que esta vieja y que esta bien que lo este.
"""
import os
import sys
import hashlib

RAIZ   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUENO  = os.path.join(RAIZ, "boot", "customboot.bin")
NOMBRE = "customboot.bin"

# Carpetas que no se miran. rescate/ por el motivo de arriba; las otras
# porque son obra del compilador o de otro proyecto.
EXENTAS = ("rescate", "build", ".git", "__pycache__", "out")


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for t in iter(lambda: f.read(65536), b""):
            h.update(t)
    return h.hexdigest()


def main():
    if not os.path.isfile(BUENO):
        print("COPIAS BIN: no encuentro boot/customboot.bin; compila el "
              "cargador antes")
        return 1

    bueno_sha = sha(BUENO)
    bueno_n   = os.path.getsize(BUENO)
    print("COPIAS BIN: el bueno es boot/customboot.bin, %d bytes, %s"
          % (bueno_n, bueno_sha[:16]))

    malas, exentas = [], []
    for dd, dirs, ficheros in os.walk(RAIZ):
        quitar = [d for d in dirs if d in EXENTAS]
        for d in quitar:
            exentas.append(os.path.relpath(os.path.join(dd, d), RAIZ))
        dirs[:] = [d for d in dirs if d not in EXENTAS]

        if NOMBRE not in ficheros:
            continue
        p = os.path.join(dd, NOMBRE)
        if os.path.normpath(p) == os.path.normpath(BUENO):
            continue
        rel = os.path.relpath(p, RAIZ)
        if sha(p) != bueno_sha:
            malas.append((rel, os.path.getsize(p)))
        else:
            print("  %-34s igual  ok" % rel)

    for d in sorted(set(exentas)):
        q = os.path.join(RAIZ, d, NOMBRE)
        if os.path.isfile(q):
            print("  %-34s EXENTA (congelada), %d bytes"
                  % (os.path.join(d, NOMBRE), os.path.getsize(q)))

    if malas:
        print("\nMAL: hay una copia del cargador que NO es la que sale de "
              "compilar.")
        for rel, n in malas:
            print("       %-32s %d bytes  (el bueno: %d)" % (rel, n, bueno_n))
        print("     Un cargador atrasado no se lee raro: se GRABA. Copia")
        print("     boot/customboot.bin encima, o borra la copia.")
        return 1

    print("COPIAS BIN: todo correcto")
    return 0


if __name__ == "__main__":
    sys.exit(main())
