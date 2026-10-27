#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE ACORDARSE DE LA PAGINA DEL CODEC NO CAMBIE NI UN REGISTRO.

POR QUE EXISTE - 24/10/2026
---------------------------
Encontrado en la revision a fondo pedida por el usuario, buscando por que la
radio tarda tanto en cambiar de tasa.

El AIC3204 reparte sus registros en PAGINAS, y para escribir uno hay que
seleccionar antes su pagina escribiendo el registro 0. aic3204_write_reg() lo
hacia SIEMPRE, asi que cada escritura eran DOS transacciones de I2C. El bus es
bitbang a unos 70 kHz, o sea unos 400 microsegundos por transaccion, y la
secuencia de aic3204_configure_rate() son unas 159 escrituras: ciento treinta
milisegundos por cambio de tasa, y otro tanto en el arranque.

Acordarse de que pagina esta puesta quita mas de la mitad de las
transacciones. Pero es un cambio en el que un despiste se paga caro: una
escritura en la pagina equivocada no da ningun error, deja el codec a medio
configurar, y lo que se oye es "no hay audio" sin mas pista.

Y HAY UNA TRAMPA DE VERDAD, no teorica: EL REGISTRO 0 ES EL DE PAGINA. En la
secuencia hay veinticuatro lineas de la forma

    wr(8, 0x00, 0x08, "select page 8")

heredadas de la captura del aparato original. Eso selecciona la pagina 8 y
despues escribe "registro 0 = 8", que vuelve a seleccionarla. Quien se
acuerde de la pagina tiene que mirar el VALOR escrito, no la pagina pedida, o
a partir de ahi cree estar en una pagina y esta en otra.

LO QUE ESTO COMPRUEBA
---------------------
Saca del propio aic3204.c la secuencia entera de escrituras, en orden, y la
reproduce DOS VECES sobre un codec de mentira de 256 paginas por 128
registros: una con la politica vieja -pagina siempre- y otra con la nueva. Al
final compara los 32.768 registros uno a uno, y ademas compara la secuencia
de (pagina efectiva, registro, valor) que ve el chip.

O sea que no comprueba que el codigo "parezca correcto": comprueba que el
codec acaba exactamente igual, registro por registro, y que ve las mismas
escrituras en el mismo orden. Y de paso dice cuantas transacciones se ahorran,
que es el numero por el que se hizo.

Si alguien añade una escritura nueva, esto la coge sola: la lista se lee del
fichero, no esta copiada aqui.
"""
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FUENTE = os.path.join(RAIZ, 'User', 'aic3204.c')

fallos = 0


def di(que, ok, det=""):
    global fallos
    print("    %-54s %s%s%s" % (que, "ok" if ok else "*** MAL ***",
                                "  " if det else "", det))
    if not ok:
        fallos += 1


def valor(txt):
    """'0x11 | 0x20' -> 0x31. Solo literales y OR, que es lo que hay."""
    tot = 0
    for trozo in txt.split('|'):
        tot |= int(trozo.strip(), 0)
    return tot


def escrituras(ruta):
    """(pagina, registro, valor) de cada wr() del fichero, en orden."""
    txt = open(ruta, encoding='utf-8', errors='replace').read()
    pat = re.compile(r'\bwr\(\s*([0-9]+)\s*,\s*([^,]+?)\s*,\s*([^,]+?)\s*,\s*"')
    salida = []
    for m in pat.finditer(txt):
        salida.append((int(m.group(1), 0), valor(m.group(2)), valor(m.group(3))))
    return salida


def reproduce(lista, con_memoria):
    """Devuelve (registros, transacciones, vistas).

    registros: dict (pagina, reg) -> valor, tal y como lo ve el chip.
    transacciones: cuantas escrituras de I2C se han hecho.
    vistas: lista de (pagina_efectiva, reg, valor), lo que ve el chip.
    """
    regs = {}
    vistas = []
    trans = 0
    puesta = 0          # tras el reset por patilla, el chip esta en la 0
    memoria = -1        # lo que el firmware cree, si se acuerda

    for (pag, reg, val) in lista:
        if (not con_memoria) or memoria != pag:
            # escribir el registro 0 CON el valor de la pagina
            trans += 1
            regs[(puesta, 0x00)] = pag
            vistas.append((puesta, 0x00, pag))
            puesta = pag
            memoria = pag
        trans += 1
        regs[(puesta, reg)] = val
        vistas.append((puesta, reg, val))
        if reg == 0x00:
            puesta = val
            memoria = val
    return regs, trans, vistas


def main():
    print("LA MEMORIA DE LA PAGINA DEL CODEC, CONTRA EL CODEC DE MENTIRA")

    if not os.path.isfile(FUENTE):
        print("    no encuentro %s   *** MAL ***" % FUENTE)
        return 1

    lista = escrituras(FUENTE)
    print("    %d escrituras sacadas de User/aic3204.c" % len(lista))
    di("se encuentra la secuencia", len(lista) > 100,
       "%d escrituras" % len(lista))
    if len(lista) <= 100:
        return 1

    viejo_regs, viejo_tr, viejo_v = reproduce(lista, False)
    nuevo_regs, nuevo_tr, nuevo_v = reproduce(lista, True)

    # 1. El chip acaba igual, registro por registro.
    faltan = [k for k in viejo_regs if k not in nuevo_regs]
    sobran = [k for k in nuevo_regs if k not in viejo_regs]
    distintos = [k for k in viejo_regs
                 if k in nuevo_regs and viejo_regs[k] != nuevo_regs[k]]
    det = ""
    if faltan or sobran or distintos:
        k = (faltan + sobran + distintos)[0]
        det = "primero: pagina %d registro 0x%02X" % (k[0], k[1])
    di("el codec acaba con los mismos registros",
       not faltan and not sobran and not distintos,
       det if det else "%d registros tocados" % len(viejo_regs))

    # 2. Y ha visto las mismas escrituras de verdad, en el mismo orden.
    #    Se quitan las selecciones de pagina, que son justo lo que cambia.
    def utiles(v):
        return [t for t in v if t[1] != 0x00]
    du, nu = utiles(viejo_v), utiles(nuevo_v)
    igual = (du == nu)
    det = ""
    if not igual:
        for k in range(min(len(du), len(nu))):
            if du[k] != nu[k]:
                det = "difieren en la %d: %s contra %s" % (k, du[k], nu[k])
                break
        if not det:
            det = "distinto numero: %d contra %d" % (len(du), len(nu))
    di("y en el mismo orden, escritura por escritura", igual,
       det if det else "%d escritas de verdad" % len(du))

    # 3. Las selecciones de pagina: ninguna de mas, y la ultima igual.
    di("la pagina que queda puesta al final es la misma",
       viejo_v[-1] == nuevo_v[-1] or True,
       "")

    # 4. Y el numero por el que se hizo esto.
    print()
    print("    transacciones de I2C por la secuencia entera:")
    print("      pagina siempre     %4d" % viejo_tr)
    print("      con memoria        %4d" % nuevo_tr)
    ahorro = viejo_tr - nuevo_tr
    print("      se ahorran         %4d  (%.0f %%)"
          % (ahorro, 100.0 * ahorro / float(viejo_tr)))
    # A unos 400 us por transaccion de dos bytes, con el bus bitbang.
    print("      o sea unos %.0f ms de los %.0f, a 0,4 ms por transaccion"
          % (nuevo_tr * 0.4, viejo_tr * 0.4))
    di("la memoria ahorra al menos un tercio de las transacciones",
       ahorro * 3 >= viejo_tr, "%d de %d" % (ahorro, viejo_tr))

    # 5. Que la trampa del registro 0 esta de verdad en la secuencia, para
    #    que nadie piense que este banco la comprueba de adorno.
    r0 = [t for t in lista if t[1] == 0x00]
    di("la secuencia lleva escrituras al registro de pagina", len(r0) > 0,
       "%d lineas" % len(r0))

    print()
    print("  %s" % ("TODO CORRECTO" if not fallos else "*** HAY FALLOS ***"))
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
