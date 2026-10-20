#!/usr/bin/env python3
"""
QUE NO HAYA UN CARGADOR VIEJO RONDANDO POR EL ARBOL.

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

Y NO BASTA CON MIRAR LOS QUE SE LLAMAN ASI - 19/10/2026
--------------------------------------------------------
*** El dueno: "SPLF.BIN y esto que". ***

Repasando lo que sobra en la carpeta aparecio esto en la raiz:

    customboot_f470vg.bin    41.224   <- de antes del 17/10
    boot/customboot_470.bin  32.700   <- el de hoy, de DOS sectores

Un cargador del 470 de antes de recortarlo a dos sectores, 8.524 bytes mas
grande, sin que lo nombre NADIE: ni el README, ni las reglas, ni la
documentacion. Y este banco lo dejaba pasar porque buscaba el nombre exacto
"customboot.bin" y aquel se llama de otra forma.

O sea que la guarda cubria el caso que ya habia pasado y no la familia. Es
el mismo fallo que ella misma persigue, un piso mas arriba: lo peligroso no
es la copia que sabes que existe, es la que se llama parecido.

Ahora se miran DOS nombres -el del 450 y el del 470, cada uno contra SU
original en boot/- y ademas se avisa de cualquier otro "customboot*.bin"
que aparezca por el arbol, aunque no se sepa contra que compararlo: un
cargador suelto sin dueno es justo lo que no debe haber, porque el dia que
alguien busque "el cargador del 470" lo va a encontrar a el.

Y UNO DE ESOS SUELTOS NO SE TOCA - 19/10/2026
----------------------------------------------
*** El dueño, y es su decision: customboot_f470vg.bin NO SE TOCA. ***

Lo propuse para borrar el mismo dia que amplie esto, razonando que era un
cargador del 470 de antes del 17/10 que no nombraba nadie. El razonamiento
estaba hecho desde el arbol, no desde la radio, y la decision no era mia.

Asi que entra en la MISMA categoria que update4.bin: esta ahi a proposito.
No se escribe, no se sincroniza, no se "pone al dia", no se borra y no se
vuelve a proponer borrarlo. Se nombra en la salida -con su tamaño- para que
se vea que esta y que esta bien que este, igual que se hace con rescate/.

Si no estuviera exento aqui, la guarda que acabo de ampliar lo marcaria en
rojo en cada "make comprueba", que es la forma mas rapida de que una guarda
deje de leerse.

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

# Los dos cargadores que salen de compilar, cada uno con su original. Lo que
# se compara es el nombre; lo que se mira es que no haya una copia vieja.
PAREJAS = (
    ("customboot.bin",     os.path.join(RAIZ, "boot", "customboot.bin")),
    ("customboot_470.bin", os.path.join(RAIZ, "boot", "customboot_470.bin")),
)
BUENO  = PAREJAS[0][1]
NOMBRE = PAREJAS[0][0]

# Cargadores sueltos que estan ahi A PROPOSITO y por decision del dueño. No
# se comparan con nada, no se tocan y no se avisa de ellos como si sobraran:
# se nombran, que es distinto. Ver la cabecera.
INTOCABLES = ("customboot_f470vg.bin",)

# Carpetas que no se miran. rescate/ por el motivo de arriba; las otras
# porque son obra del compilador o de otro proyecto.
EXENTAS = ("rescate", "build", ".git", "__pycache__", "out")


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for t in iter(lambda: f.read(65536), b""):
            h.update(t)
    return h.hexdigest()


def revisa(nombre, bueno):
    """Compara todas las copias de `nombre` contra `bueno`. Devuelve
    (malas, exentas)."""
    if not os.path.isfile(bueno):
        print("COPIAS BIN: no encuentro %s; compila el cargador antes"
              % os.path.relpath(bueno, RAIZ))
        return None, None

    bueno_sha = sha(bueno)
    bueno_n   = os.path.getsize(bueno)
    print("COPIAS BIN: el bueno es %s, %d bytes, %s"
          % (os.path.relpath(bueno, RAIZ).replace(os.sep, "/"),
             bueno_n, bueno_sha[:16]))

    malas, exentas = [], []
    for dd, dirs, ficheros in os.walk(RAIZ):
        quitar = [d for d in dirs if d in EXENTAS]
        for d in quitar:
            exentas.append(os.path.relpath(os.path.join(dd, d), RAIZ))
        dirs[:] = [d for d in dirs if d not in EXENTAS]

        if nombre not in ficheros:
            continue
        p = os.path.join(dd, nombre)
        if os.path.normpath(p) == os.path.normpath(bueno):
            continue
        rel = os.path.relpath(p, RAIZ)
        if sha(p) != bueno_sha:
            malas.append((rel, os.path.getsize(p), bueno_n))
        else:
            print("  %-34s igual  ok" % rel)

    for d in sorted(set(exentas)):
        q = os.path.join(RAIZ, d, nombre)
        if os.path.isfile(q):
            print("  %-34s EXENTA (congelada), %d bytes"
                  % (os.path.join(d, nombre), os.path.getsize(q)))
    return malas, exentas


def sueltos(conocidos):
    """Cualquier otro customboot*.bin del arbol. No se compara con nada
    -no se sabe contra que-, pero se dice que esta: un cargador suelto sin
    dueño es lo que de verdad hace dano, porque parece el bueno."""
    fuera = []
    for dd, dirs, ficheros in os.walk(RAIZ):
        dirs[:] = [d for d in dirs if d not in EXENTAS]
        for f in ficheros:
            if not (f.startswith("customboot") and f.endswith(".bin")):
                continue
            if f in conocidos:
                continue
            rel = os.path.relpath(os.path.join(dd, f), RAIZ)
            n   = os.path.getsize(os.path.join(dd, f))
            if f in INTOCABLES:
                print("  %-34s INTOCABLE (del dueño), %d bytes" % (rel, n))
                continue
            fuera.append((rel, n))
    return fuera


def main():
    fallo = 0
    for nombre, bueno in PAREJAS:
        malas, _ = revisa(nombre, bueno)
        if malas is None:
            return 1
        if malas:
            print("\nMAL: hay una copia del cargador que NO es la que sale "
                  "de compilar.")
            for rel, n, bn in malas:
                print("       %-32s %d bytes  (el bueno: %d)" % (rel, n, bn))
            print("     Un cargador atrasado no se lee raro: se GRABA. Copia")
            print("     boot/%s encima, o borra la copia." % nombre)
            fallo = 1

    otros = sueltos({n for n, _ in PAREJAS})
    if otros:
        print("\nMAL: hay cargadores sueltos que no salen de compilar nada.")
        for rel, n in otros:
            print("       %-32s %d bytes" % (rel, n))
        print("     Nadie los nombra y nadie los actualiza, pero el dia que")
        print("     alguien busque 'el cargador' los va a encontrar a ellos.")
        print("     Borralos, o dales un sitio y un nombre en boot/.")
        fallo = 1

    if fallo:
        return 1
    print("COPIAS BIN: todo correcto")
    return 0


if __name__ == "__main__":
    sys.exit(main())
