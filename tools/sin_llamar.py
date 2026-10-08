#!/usr/bin/env python3
"""
FUNCIONES PUBLICAS QUE NO LLAMA NADIE.

*** 06/10/2026. rtty_auto.c -el modulo que pone los tonos del RTTY encima
de la señal, acierta el desplazamiento y mide la velocidad- se escribio
entero, se probo en su banco, paso a produccion... y rtty_auto_poll() NO
SE LLAMABA DESDE NINGUN SITIO. ***

Durante ocho versiones la radio llevaba el motor montado y el cable sin
enchufar. El manual decia que el RTTY se ajustaba solo; hacia una de las
cuatro cosas que decia.

POR QUE NO LO VIO NADIE
-----------------------
- El compilador no avisa: la funcion es publica, asi que no es "unused".
- El enlazador no avisa: --gc-sections la tira y tan contento.
- El banco del modulo pasa: llama el mismo a rtty_auto_poll() y funciona.
- Y el usuario tampoco: lo que hace esa funcion es ARREGLAR una sintonia
  que va mal. Cuando va mal, uno mueve el dial; no se para a pensar por que
  no se arreglo sola.

O sea que es un fallo que no da sintoma en ningun sitio donde miremos.

QUE MIRA, Y POR QUE SOLO ESO
----------------------------
La primera version miraba TODAS las funciones publicas y saco 227. La
mayoria son accesores que usa un banco o la depuracion por UART, asi que la
unica de verdad se perdia entre ellas. Un banco que avisa de 227 cosas no
avisa de ninguna.

Asi que mira solo la familia donde el silencio SIGNIFICA que algo no corre:
las que existen para que las llame el bucle principal o el arranque, y que
se conocen por el nombre -_poll, _tick, _paso, _arranca-. Un accesor sin
llamantes es codigo de mas; un _poll() sin llamantes es una FUNCION QUE NO
PASA. Son cosas distintas y solo la segunda rompe la radio.

NO FALLA POR SI SOLO: avisa. Hay motivos legitimos para una funcion sin
llamantes -la API de un modulo que todavia se esta montando, algo que solo
usa el simulador, un gancho de depuracion- y por eso lo que hay es una
BLANCA con el motivo escrito al lado. Una excepcion sin motivo es una
excepcion que nadie puede revisar, igual que en idioma_check.py.
"""
import os, re, sys

AQUI = os.path.dirname(os.path.abspath(__file__))
USER = os.path.join(AQUI, '..', 'User')

# Y TAMBIEN SE MIRA boot/, QUE SI NO ESTE BANCO MIENTE - 08/10/2026.
#
# *** El dueno: "borra lo que no se use". ***
#
# Y lo primero que dijo este banco fue "cargador_arranca: NADIE la llama".
# La llama boot/boot_main.c linea 1037, que es su unico cliente y siempre
# lo fue: el cargador vive en el arranque, no en el firmware. Este script
# solo leia User/, asi que marcaba como muerta justo la funcion que graba
# el update.bin.
#
# Eso es peor que no tener banco: uno que senala funciones vivas ensena a
# no hacerle caso, y el dia que senale una de verdad tampoco se la va a
# creer nadie. De las cuatro que salieron, TRES eran fallo del banco y no
# codigo muerto; la cuarta -el almacen de imagenes- se borro entera.
EXTRA = [os.path.join(AQUI, '..', 'boot')]

# Lo que no se llama desde el firmware a proposito, con su porque. La regla
# es que el motivo se ESCRIBE: una excepcion sin motivo es una excepcion que
# nadie se atreve a quitar.
BLANCA = {
    'ident_firmas':        'la usa el banco sim/identancho.c, no el firmware',
    'rtty_auto_puntos':    'solo para el banco de rtty_auto',
    'rtty_auto_tonos_veces': 'solo para el banco y la depuracion',
    'rtty_auto_baud_veces':  'solo para el banco y la depuracion',
    'touch_debug_raw':     'gancho de depuracion por UART',
    'splash_paso':         'un fotograma suelto, para sim/splashtest.c; la '
                           'radio entra por el bucle de dentro del modulo',
    'ecu_paso':            'ecualizador ciego CMA sin cablear todavia: hace '
                           'falta para el 4539/110B y tiene banco en '
                           'sim/ecualiztest.c. El enlazador lo tira entero, '
                           'asi que no cuesta ni un byte en la imagen',
}

# Declaraciones tipo "tipo nombre(args);" en un .h, en una sola linea.
DECL = re.compile(r'^\s*(?!typedef|return)[A-Za-z_][\w \t\*]*?\b(\w+)\s*\([^;{]*\)\s*;\s*$')

# Las que existen para que alguien las llame desde fuera, una y otra vez.
MOTOR = ('_poll', '_tick', '_paso', '_arranca')

def sin_comentarios(src):
    """Fuera /* */ y //, y tambien las cadenas: un nombre dentro de un
    printf tampoco es una llamada."""
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    src = re.sub(r'//[^\n]*', ' ', src)
    src = re.sub(r'"(?:[^"\\]|\\.)*"', '""', src)
    return src


def main():
    cabeceras = sorted(f for f in os.listdir(USER) if f.endswith('.h'))
    fuentes   = sorted(f for f in os.listdir(USER) if f.endswith('.c'))
    # SIN COMENTARIOS. La primera version no los quitaba de los .c, y al
    # probar que el banco cazaba el fallo original -comentando la llamada a
    # rtty_auto_poll()- siguio diciendo que todo bien: la llamada comentada
    # contaba como llamada. Un banco que no caza el fallo para el que se
    # escribio no vale nada, y este proyecto ya ha pisado esa piedra hoy
    # (ver sim/identancho.c y su prerequisito que faltaba).
    texto = {f: sin_comentarios(
                    open(os.path.join(USER, f), encoding='utf-8',
                         errors='replace').read())
             for f in fuentes}

    # Ver EXTRA arriba: boot/ tambien llama, y sin leerlo desde aqui este
    # banco dice que el cargador esta muerto.
    for d in EXTRA:
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if not f.endswith(('.c', '.h')):
                continue
            texto[os.path.basename(d) + '/' + f] = sin_comentarios(
                open(os.path.join(d, f), encoding='utf-8',
                     errors='replace').read())

    sueltas, exentas = [], 0
    for h in cabeceras:
        propio = h[:-2] + '.c'
        cont = open(os.path.join(USER, h), encoding='utf-8', errors='replace').read()
        cont = sin_comentarios(cont)
        for linea in cont.splitlines():
            m = DECL.match(linea)
            if not m:
                continue
            nom = m.group(1)
            if not nom.endswith(MOTOR):
                continue
            if nom in BLANCA:
                exentas += 1
                continue
            uso = re.compile(r'\b' + re.escape(nom) + r'\s*\(')
            # sobre TODO lo leido -User/ y boot/-, no solo sobre User/.
            quien = [f for f in texto
                     if f != propio and uso.search(texto[f])]
            if not quien:
                # ¿la llama al menos su propio .c, o esta muerta del todo?
                viva = propio in texto and len(uso.findall(texto[propio])) > 1
                sueltas.append((h, nom, 'solo dentro de su modulo' if viva else 'NADIE la llama'))

    print(f"funciones de motor (_poll/_tick/_paso/_arranca) sin llamante fuera"
          f" de su modulo: {len(sueltas)}   (exentas por la blanca: {exentas})\n")
    for h, nom, que in sueltas:
        print(f"  {h:22} {nom:34} {que}")
    if sueltas:
        print("\nRevisalas: o se llaman, o se borran, o entran en BLANCA con su motivo.")
        # Y SALE CON ERROR, que antes no - 08/10/2026. Este script SI se
        # corria en cada "make comprueba", pero imprimia y salia con 0, y lo
        # que imprimia era "cargador_arranca: NADIE la llama", que era
        # mentira. Un aviso falso que sale siempre se convierte en parte del
        # paisaje: dejas de leerlo, y el dia que diga algo verdadero tampoco
        # lo lees. Peor que no tener banco.
        return 1
    print("SIN LLAMAR: todo correcto")
    return 0

if __name__ == '__main__':
    sys.exit(main())
