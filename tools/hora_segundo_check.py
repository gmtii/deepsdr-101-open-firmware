#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE LA HORA POR RADIO SE PONGA CON SU SEGUNDO, NO CON UN CERO.

  python3 tools/hora_segundo_check.py <User/main.c>

POR QUE EXISTE - 29/10/2026.

*** Un probador: "tras sincronizar con ALS162 se ensena HH.MM y se
enciende Aplicar, pero los segundos no avanzan, asi que al pulsar el reloj
se queda con los segundos a cero". ***

Y tenia razon dos veces. La hora de estas emisoras vale EN EL INSTANTE de
su borde de minuto -lo dice la cabecera de dcf77.h desde que existe-, no
cuando uno mira la pantalla. Entre ese instante y el dedo pasan los
segundos que cuesta leer y decidirse, y se tiraban todos: el reloj quedaba
atrasado justo eso. El RDS ya lo hacia bien desde el 24/09 con
reloj_anclar(); las cinco emisoras horarias, no.

LAS CINCO REGLAS:

  1. Existe hora_atraso_ms() y acota con HORA_FRESCA_MS. Sin el tope, una
     trama vieja se aplicaria como si fuera de este minuto, y encima la
     cuenta podria cruzar la medianoche con la fecha de ayer puesta.
  2. hora_poll() arma el cronometro cuando el estado es DCF_LISTO.
  3. Y lo REARMA en cada trama nueva, no solo la primera: el decodificador
     sigue corriendo y cada minuto trae una hora nueva. Sin esto, a los
     cinco minutos el atraso serian cinco minutos y el arreglo seria peor
     que el fallo. La trama nueva se reconoce porque cambia hora o minuto.
  4. hora_sintoniza() lo pone a cero: cambiar de emisora reinicia el
     decodificador y el borde anterior ya no describe nada.
  5. En hora_touch(), la rama de Aplicar escribe la fecha ANTES de anclar
     y ancla con hora_atraso_ms(), no con un cero. El orden no es estilo:
     rtc_pon_fecha_hora() escribe la hora entera con los segundos a cero,
     asi que si fuera detras desharia el anclaje.

LO QUE NO COMPRUEBA: que la hora que trae la emisora sea correcta. De eso
se encargan los bancos de dcf77.c -make dcftest, make msf, make wwvtest- y
las dos tramas seguidas que exige el decodificador.
"""
import io
import re
import sys


def sin_comentarios(t):
    """Tapa /* ... */ y // ... con espacios, dejando los saltos de linea."""
    out = []
    i = 0
    n = len(t)
    while i < n:
        if t.startswith('/*', i):
            j = t.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(c if c == '\n' else ' ' for c in t[i:j]))
            i = j
        elif t.startswith('//', i):
            j = t.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        else:
            out.append(t[i])
            i += 1
    return ''.join(out)


def cuerpo(src, firma):
    """El cuerpo de la DEFINICION, no el del prototipo.

    main.c declara casi todo dos veces -el prototipo arriba y la definicion
    donde toca-, asi que buscar la firma a secas y despues la primera llave
    encuentra el cuerpo de OTRA funcion. Se exige que detras de la firma
    venga la llave, con espacios o un salto de linea en medio y nada mas.
    """
    desde = 0
    i = -1
    while True:
        k = src.find(firma, desde)
        if k < 0:
            return None
        if src[k + len(firma):k + len(firma) + 8].lstrip().startswith('{'):
            i = k
            break
        desde = k + 1
    i = src.find('{', i)
    if i < 0:
        return None
    n = 0
    for j in range(i, len(src)):
        if src[j] == '{':
            n += 1
        elif src[j] == '}':
            n -= 1
            if n == 0:
                return src[i:j + 1]
    return None


def di(texto, bien, mal):
    print("    %s %s" % (texto.ljust(40, '.'), "ok" if bien else mal))
    return 0 if bien else 1


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    src = sin_comentarios(
        io.open(sys.argv[1], encoding='utf-8', errors='replace').read())
    mal = 0

    print("  la hora por radio, con su segundo:")

    # 1. El cronometro y su tope.
    c = cuerpo(src, 'static uint32_t hora_atraso_ms(void)')
    mal += di("hay hora_atraso_ms()", c is not None,
              "*** NO: el atraso no se mide en ningun sitio ***")
    if c is None:
        return 1
    mal += di("y acota con HORA_FRESCA_MS", 'HORA_FRESCA_MS' in c,
              "*** NO: una trama vieja se aplicaria como de este minuto ***")

    # 2 y 3. Armado y rearmado.
    c = cuerpo(src, 'static void hora_poll(void)')
    mal += di("hay hora_poll()", c is not None, "*** NO ***")
    if c is None:
        return 1
    arma = re.search(r's_hora_listo_ms\s*=\s*g_msticks\s*;', c)
    mal += di("hora_poll() arma el cronometro", arma is not None,
              "*** NO: nadie apunta el borde de minuto ***")
    if arma is not None:
        antes = c[:arma.start()]
        # La condicion que lo arma tiene que mirar el estado Y la trama.
        trozo = antes[-400:]
        mal += di("mirando DCF_LISTO", 'DCF_LISTO' in trozo,
                  "*** NO: se armaria sin hora buena ***")
        rearma = (re.search(r'inf\.hora\s*!=', trozo) is not None
                  and re.search(r'inf\.minuto\s*!=', trozo) is not None)
        mal += di("y rearmando en cada trama nueva", rearma,
                  "*** NO: a los cinco minutos atrasaria cinco minutos ***")

    # 4. Cambiar de emisora lo borra.
    c = cuerpo(src, 'static void hora_sintoniza(void)')
    mal += di("hay hora_sintoniza()", c is not None, "*** NO ***")
    if c is not None:
        mal += di("y borra el cronometro al cambiar",
                  re.search(r's_hora_listo_ms\s*=\s*0U?\s*;', c) is not None,
                  "*** NO: se aplicaria el borde de la emisora anterior ***")

    # 5. La rama de Aplicar: orden y atraso de verdad.
    c = cuerpo(src, 'static void hora_touch(uint16_t x, uint16_t y, '
                    'uint8_t pressed)')
    mal += di("hay hora_touch()", c is not None, "*** NO ***")
    if c is not None:
        # La rama de las emisoras horarias es la del `else` de dcf_info_t.
        k = c.find('dcf_info_t inf;')
        rama = c[k:] if k >= 0 else ''
        mal += di("la rama de Aplicar lee dcf_info_t", k >= 0, "*** NO ***")
        if k >= 0:
            mf = re.search(r'rtc_pon_fecha_hora\s*\(', rama)
            ma = re.search(r'reloj_anclar\s*\(', rama)
            mal += di("escribe la fecha y ancla",
                      mf is not None and ma is not None,
                      "*** NO: o no pone la fecha o no ancla ***")
            if mf is not None and ma is not None:
                mal += di("la fecha ANTES del anclaje", mf.start() < ma.start(),
                          "*** NO: rtc_pon_fecha_hora() desharia el anclaje ***")
                # Y el segundo argumento del anclaje sale del cronometro.
                j = ma.end()
                n = 1
                while j < len(rama) and n > 0:
                    if rama[j] == '(':
                        n += 1
                    elif rama[j] == ')':
                        n -= 1
                    j += 1
                args = rama[ma.end():j - 1]
                coma = args.rfind(',')
                atraso = args[coma + 1:].strip() if coma >= 0 else ''
                bien = (atraso != '' and re.fullmatch(r'0U?|\(uint32_t\)0U?',
                                                      atraso) is None)
                mal += di("y ancla con el atraso, no con cero", bien,
                          "*** NO: vuelve el fallo del probador ***")

    if mal:
        print("  *** %d fallo(s): ver la cabecera de este guion ***" % mal)
        return 1
    print("  la hora por radio se pone con su segundo  ok")
    return 0


if __name__ == '__main__':
    sys.exit(main())
