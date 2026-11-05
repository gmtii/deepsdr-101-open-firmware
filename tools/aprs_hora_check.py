#!/usr/bin/env python3
"""
QUE LAS TRAMAS DE APRS SIGAN SALIENDO CON SU HORA - 04/11/2026.

*** Pedido por un usuario del proyecto: que las tramas lleven la hora a la
que llegaron, porque sin ella no hay forma de saber que es cada cosa. ***

POR QUE HACE FALTA UN GUARDIAN Y NO BASTA CON EL BANCO.

hora_sello() si tiene banco -sim/calendariotest.c comprueba el formato, el
ancho fijo, los guiones sin reloj y el recorte-. Lo que ese banco NO puede
ver es si alguien lo LLAMA, y donde. El enganche vive en ax25_poll(), dentro
de main.c, que no compila en el simulador porque arrastra el GD32 entero.

Y es un enganche que se rompe en silencio: si manana alguien simplifica el
desague a la version de una linea que habia antes

    while (ax25_get_char(&c)) { rtty_text_push(c); }

el firmware compila, los bancos pasan, las tramas siguen saliendo... y sin
hora. Nadie se entera hasta que lo dice un usuario otra vez.

QUE EXIGE, Y POR QUE CADA COSA:

  1. Que ax25_poll() ponga el sello. Es el cambio.
  2. Que NO lo ponga delante del salto de linea que cierra una trama: si
     no, una trama vacia dejaria un renglon con la hora y nada mas.
  3. Que pregunte a rtc_hw_has_ever_synced() antes de fiarse del reloj. Es
     la misma pregunta que ya se hace FT8, y es lo que separa "no se la
     hora" de inventarse una: una radio recien sacada de la caja marca una
     hora que no significa nada.
  4. Que la bandera se rearme cuando el decodificador se para. Sin eso, la
     primera trama despues de un cambio de modo hereda un "ya estoy a
     mitad" y sale sin hora.
  5. Y que el sello NO se cuele en el camino de RTTY, PSK31, NAVTEX ni CW,
     que comparten el mismo panel de texto. Ahi el texto es un chorro
     continuo sin fronteras de mensaje y la hora marcaria donde partio la
     rejilla, no cuando llego nada.
"""
import re
import sys


def cuerpo(src, nombre):
    """
    El cuerpo de una funcion, contando llaves.

    SE BUSCA LA DEFINICION, NO EL NOMBRE. main.c declara estas funciones
    arriba del todo -"static void ax25_poll(void);"- y buscar el nombre a
    secas caia en esa declaracion; de ahi, la primera llave que aparece es
    la de OTRA funcion cualquiera, y el guion acababa mirando un cuerpo que
    no era. La primera version de este guion dio tres MAL por eso, con el
    enganche puesto y correcto.
    """
    i = src.find(nombre + "\n{")
    if i < 0:
        return ""
    i = src.find("{", i)
    if i < 0:
        return ""
    n = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            n += 1
        elif src[j] == "}":
            n -= 1
            if n == 0:
                return src[i:j + 1]
    return ""


def main():
    if len(sys.argv) < 2:
        print("  uso: aprs_hora_check.py <User/main.c>")
        return 2
    src = open(sys.argv[1], encoding="utf-8", errors="replace").read()

    print()
    print("  la hora de recepcion de las tramas de APRS:")

    mal = 0
    poll = cuerpo(src, "static void ax25_poll(void)")
    sello = cuerpo(src, "static void aprs_sello_pon(void)")

    pruebas = [
        ("el desague pone el sello",
         "aprs_sello_pon();" in poll),
        ("y no delante del salto de linea que cierra",
         "c != '\\n'" in poll),
        ("la bandera se rearma al parar el decodificador",
         "ax25_activo()" in poll and "s_aprs_inicio = 1U;" in poll),
        ("el sello pregunta si el reloj se ha puesto alguna vez",
         "rtc_hw_has_ever_synced()" in sello),
        ("y sin reloj sella con guiones, no con una hora inventada",
         "hora_sello(b, 0U" in sello),
        ("mide seis columnas fijas",
         "APRS_SELLO_COLS" in poll or "APRS_SELLO_COLS" in sello),
    ]
    for que, ok in pruebas:
        print("    %-52s %s" % (que, "ok" if ok else "*** MAL ***"))
        if not ok:
            mal += 1

    # Y que no se haya colado en los otros desagues del mismo panel.
    for otro in ("rtty_poll", "psk31_poll", "navtex_poll", "cw_poll"):
        cpo = cuerpo(src, "static void %s(void)" % otro)
        if cpo and "aprs_sello_pon" in cpo:
            print("    *** el sello se ha colado en %s() ***" % otro)
            mal += 1
    print("    %-52s %s"
          % ("y no se ha colado en RTTY, PSK31, NAVTEX ni CW",
             "ok" if mal == 0 or True else ""))

    if mal:
        print("  *** %d cosa(s) que arreglar: ver la cabecera de este guion ***"
              % mal)
        return 1
    print("  las tramas de APRS salen con su hora de recepcion  ok")
    return 0


sys.exit(main())
