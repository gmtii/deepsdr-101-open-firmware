#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QUE NADIE SE QUEDE CON EL MANDO ANTES QUE EL VOLUMEN.

  python3 mando_check.py <User/main.c>

POR QUE EXISTE - 05/10/2026.

El mando no cambiaba el volumen
mientras el menu de ajustes estaba abierto. El cuadro del volumen salia,
pero girar el mando movia las opciones del menu en vez del volumen.

El reparto del mando vive en tune_encoder_poll() y es una ESCALERA: una
pantalla tras otra mira si es suya, y la que se lo queda hace `return`. El
volumen esta al final de esa escalera, asi que cualquier pantalla que coja
los detentes y vuelva ANTES le deja sin giro - y el boton, que esta en la
barra de abajo y se puede pulsar siempre, ya ha encendido la luz y sacado la
caja. La radio promete una cosa y hace otra.

No es un fallo que se vea leyendo: cada rama, por separado, es razonable.
Solo se ve mirando el ORDEN, y el orden cambia cada vez que se añade una
pantalla nueva a la escalera. Por eso esto se mide y no se recuerda.

LA REGLA: toda rama de tune_encoder_poll() que se quede con los detentes y
vuelva antes de la rama del volumen tiene que nombrar
ENCODER_TARGET_VOLUME - o sea, tiene que haberse preguntado si el mando
estaba enganchado al volumen. Como se conteste es cosa suya; lo que no vale
es no preguntarlo.

Lo que NO comprueba: que la respuesta sea la correcta. Eso no lo puede mirar
un guion.
"""
import io
import re
import sys


def cuerpo(src, nombre):
    """El texto de una funcion, contando llaves."""
    m = re.search(r'^static\s+void\s+' + nombre + r'\s*\(void\)\s*\n\{', src, re.M)
    if not m:
        return None
    i = src.index('{', m.start())
    n = 0
    for j in range(i, len(src)):
        if src[j] == '{':
            n += 1
        elif src[j] == '}':
            n -= 1
            if n == 0:
                return src[i:j + 1]
    return None


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    src = io.open(sys.argv[1], encoding='utf-8', errors='replace').read()
    # Fuera comentarios: un ENCODER_TARGET_VOLUME nombrado solo en un
    # comentario no es una pregunta, es una mencion.
    #
    # PERO CONSERVANDO LOS SALTOS DE LINEA, y esto no es un detalle de
    # estilo. La primera version los sustituia por UN espacio, asi que un
    # comentario de veinte lineas delante de un `if` dejaba ese `if`
    # pegado al codigo anterior - y el buscador de ramas, que los reconoce
    # por "salto de linea y cuatro espacios", dejaba de verlo. Se comia
    # justo la rama de la rejilla de Ajustes, que es DONDE ESTABA EL
    # FALLO: el guion decia "todo correcto" sin haber mirado al culpable.
    src = re.sub(r'/\*.*?\*/',
                 lambda m: '\n' * m.group(0).count('\n'), src, flags=re.S)
    src = re.sub(r'//[^\n]*', '', src)

    cue = cuerpo(src, 'tune_encoder_poll')
    if cue is None:
        print("  no encuentro tune_encoder_poll()   MAL")
        return 1

    # Donde empieza la rama del volumen: a partir de ahi ya no importa.
    #
    # CUAL ES "LA RAMA DEL VOLUMEN", dicho con cuidado.
    #
    # No vale el primer `if (s_encoder_target == ENCODER_TARGET_VOLUME)` que
    # aparezca: desde que las ramas de arriba preguntan por el volumen -que
    # es justo lo que este guion exige- hay varios, y quedarse con el
    # primero corta la escalera demasiado pronto y deja fuera a las ramas de
    # abajo. Paso de verdad: el arreglo del teclado creo un `if` asi antes
    # que el bueno, y la rejilla de Ajustes -donde estaba el fallo- se quedo
    # sin mirar.
    #
    # La de verdad es la que AJUSTA el volumen, o sea la que usa
    # VOLUME_STEP_X2. Eso no lo tiene ninguna otra.
    #
    ini = None
    for m in re.finditer(r'\n    if\s*\(\s*s_encoder_target\s*==\s*ENCODER_TARGET_VOLUME\s*\)',
                         cue):
        k = cue.find('{', m.start())
        if k < 0:
            continue
        p2 = 0
        fin = len(cue)
        for q in range(k, len(cue)):
            if cue[q] == '{':
                p2 += 1
            elif cue[q] == '}':
                p2 -= 1
                if p2 == 0:
                    fin = q
                    break
        # VOLUME_STEP_X2 DENTRO DE SU PROPIO BLOQUE, no "cerca": medir por
        # cercania daba por buena la del teclado, que esta setenta lineas
        # antes que la de verdad.
        if 'VOLUME_STEP_X2' in cue[k:fin]:
            ini = m.start()
            break
    if ini is None:
        print("  no encuentro la rama que ajusta el volumen (VOLUME_STEP_X2)   MAL")
        return 1
    antes = cue[:ini]

    # Las ramas de primer nivel: "if (...) { ... }"
    ramas = []
    for mm in re.finditer(r'\n    if\s*\(', antes):
        i = antes.index('(', mm.start())
        n = 0
        for j in range(i, len(antes)):
            if antes[j] == '(':
                n += 1
            elif antes[j] == ')':
                n -= 1
                if n == 0:
                    cond = antes[i + 1:j]
                    break
        else:
            continue
        k = antes.find('{', j)
        if k < 0:
            continue
        n = 0
        for q in range(k, len(antes)):
            if antes[q] == '{':
                n += 1
            elif antes[q] == '}':
                n -= 1
                if n == 0:
                    # La condicion ENTERA, no recortada: el recorte es solo
                    # para imprimir. Recortarla aqui hacia que la regla se
                    # mirara sobre media condicion - y dos ramas que SI
                    # preguntaban salian como que no.
                    ramas.append((antes[:i].count('\n') + 1,
                                  ' '.join(cond.split()), antes[k:q + 1]))
                    break

    mal = 0
    print("  ramas que corren ANTES del volumen: %d" % len(ramas))
    for ln, cond, bloque in ramas:
        coge = ('detents' in bloque) and ('return' in bloque)
        if not coge:
            continue
        # Vale preguntarlo de las dos formas: mirando si el destino ES el
        # volumen, o acotando el destino a otra cosa -"solo en SINTONIA"-,
        # que deja el volumen fuera igual de bien.
        pregunta = ('ENCODER_TARGET_VOLUME' in bloque
                    or 'ENCODER_TARGET_VOLUME' in cond
                    or 's_encoder_target' in cond)
        print("    linea %-6d %-54s %s" % (ln, cond[:52],
              "ok" if pregunta else "*** SE QUEDA EL MANDO SIN PREGUNTAR ***"))
        if not pregunta:
            mal += 1

    if mal:
        print("  *** %d ramas se quedan los detentes sin mirar si son del volumen ***" % mal)
        print("  El boton de Volumen se enciende igual, asi que la radio promete")
        print("  algo que no hace. Ver la cabecera de este guion.")
        return 1
    print("  todas preguntan por el volumen antes de quedarse el mando  ok")

    return pinta(src)


def pinta(txt):
    """
    Y QUE GIRAR EL MANDO NO REPINTE MEDIA PANTALLA - 31/10/2026.

    EL SINTOMA: moviendo el mando -volumen o frecuencia- el waterfall y el
    espectro se ralentizan, casi se paran, y vuelven en cuanto el mando se
    queda quieto.

    Y era eso, con los pixeles contados:

      aux_row_display_draw()  = ui_top_draw() entera
                              = cabecera 800x64  = 51.200 px
                              + franja estado 800x40 = 32.000 px
                              + RDS y dos botones
                              -------------------------------
                                mas de 83.000 px POR DETENTE

      spec_span_labels_draw() = la regla, 800x26 = 20.800 px POR PASADA

    Un cuadro entero de espectro son 106.564 px -medido el 30/10 al
    perseguir el siseo del panel-, y el bucle principal hace todo esto
    DESPUES de dibujar, asi que lo que retrasa es el cuadro siguiente. Y
    el bucle pasa mas a menudo que los cuadros: de 10 a 35 ms contra 22.

    Esto vigila las dos:

      - que el detente de volumen NO llame a la cabecera entera. El numero
        de dB vive en el boton "Volumen" de la barra de abajo; la cabecera
        no lo enseña y la franja de estado solo cambia al CAMBIAR DE
        DESTINO, que es pulsar el mando, no girarlo.
      - que la regla no se pinte a pelo en el camino del mando, sino
        aplazada y soltada como mucho una vez por cuadro.
    """
    print()
    print("  girar el mando no repinta media pantalla:")

    mal = 0

    i = txt.find('if ((int16_t)v != s_volume_db_x2)')
    if i < 0:
        print("    no encuentro el detente del volumen           *** MAL ***")
        return 1
    bloque = txt[i:i + 2500]
    fin = bloque.find('volume_activity = 1U;')
    bloque = bloque[:fin if fin > 0 else len(bloque)]

    # Fuera del menu tiene que pintar la casilla, no la cabecera.
    casilla = 'act_una(&s_btn_vol)' in bloque
    entera  = ('aux_row_display_draw()' in bloque)
    suelto  = ('settings_value_redraw();' in bloque
               and 's_menu_open' not in bloque)
    print("    el detente pinta el boton de Volumen          %s"
          % ("ok" if casilla else "*** MAL ***"))
    if not casilla:
        mal += 1
    print("    y NO la cabecera entera                       %s"
          % ("*** MAL ***" if (entera or suelto) else "ok"))
    if entera or suelto:
        mal += 1

    # La regla: aplazada, no a pelo, y soltada cuando el mando para.
    j = txt.find('static void tune_encoder_poll(void)\n{')
    cuerpo = txt[j:j + 60000] if j >= 0 else ''
    marca   = 's_regla_debe = 1U;' in cuerpo
    desague = ('s_regla_debe &&' in cuerpo
               and 'spec_span_labels_draw();' in cuerpo
               and 'detents == 0 || s_regla_saltos' in cuerpo)
    print("    la regla se marca, no se pinta al momento     %s"
          % ("ok" if marca else "*** MAL ***"))
    if not marca:
        mal += 1
    print("    y se suelta cuando el mando para              %s"
          % ("ok" if desague else "*** MAL ***"))
    if not desague:
        mal += 1

    #
    # Y LA CHAPA DEL NCO IGUAL QUE LA REGLA - 01/11/2026.
    #
    # El numero del NCO vive en la franja de estado, que son 287 us de
    # composicion -make topfranjas- y unos 12 ms en la radio. Girando, ese
    # numero cambia en CADA detente, asi que repintarlo al momento es
    # pagar la franja entera por un numero que no se lee hasta que se
    # suelta. Tiene que marcarse y soltarse cuando dejan de llegar
    # detentes; el plazo de NCO_CHAPA_MS es el seguro de los giros largos,
    # no el camino normal.
    #
    #
    # Y "CUANDO EL MANDO PARA" SE MIDE CON EL RELOJ - 02/11/2026.
    #
    # Pedia `s_chapa_debe && detents == 0` y que en algun sitio del cuerpo
    # apareciera ui_top_draw_status(). Las dos cosas estaban mal:
    #
    #   - esa funcion ya no se llama ahi -es ui_top_draw_chips()- y esta
    #     comprobacion seguia en verde porque el nombre salia en OTRO
    #     sitio de los 60.000 caracteres que mira. Un banco que pasa por
    #     el motivo equivocado es peor que uno que falla;
    #   - y `detents == 0` no es "el mando ha parado", es "esta vuelta no
    #     ha traido clics", que girando pasa a cada rato. Medido por el
    #     cable: 3.428 us por vuelta repintando la chapa mientras se gira.
    #
    # Ahora se exige el reloj: CHAPA_PARADO_MS desde el ultimo detente.
    #
    chapa_marca   = 's_chapa_debe = 1U;' in cuerpo
    chapa_desague = ('s_chapa_debe && detents == 0' in cuerpo
                     and 'CHAPA_PARADO_MS' in cuerpo
                     and 'ui_top_draw_chips(&s_top);' in cuerpo)
    print("    la chapa del NCO se marca, no se pinta        %s"
          % ("ok" if chapa_marca else "*** MAL ***"))
    if not chapa_marca:
        mal += 1
    print("    y tambien se suelta cuando el mando para      %s"
          % ("ok" if chapa_desague else "*** MAL ***"))
    if not chapa_desague:
        mal += 1

    #
    # Y EL NUMERO DE LA FRECUENCIA, QUE ERA EL QUE FALTABA - 02/11/2026.
    #
    # La regla y la chapa ya se marcaban; el numero se pintaba en CADA
    # vuelta que moviera la frecuencia. Medido por el cable con "DG;": el
    # tramo `frq` marca 3.331 us por vuelta girando, el segundo mas gordo
    # despues del dibujo, y para nada: a treinta vueltas por segundo un
    # numero que cambia treinta veces no se lee.
    #
    # Ahora va como los otros dos: se marca aqui y se pinta en el desague,
    # diez veces por segundo girando (FREQ_GIRO_MS) y en cuanto para.
    #
    freq_marca   = 's_freq_debe = 1U;' in cuerpo
    freq_desague = ('s_freq_debe' in cuerpo
                    and 'FREQ_GIRO_MS' in cuerpo
                    and 'freq_display_draw();' in cuerpo)
    print("    el numero de la frecuencia se marca           %s"
          % ("ok" if freq_marca else "*** MAL ***"))
    if not freq_marca:
        mal += 1
    print("    y sale a diez por segundo, no a treinta       %s"
          % ("ok" if freq_desague else "*** MAL ***"))
    if not freq_desague:
        mal += 1

    if mal:
        print("  *** %d cosa(s) que arreglar: ver la cabecera de pinta() ***" % mal)
        return 1
    print("  el mando ya no compite con el espectro por el bus  ok")
    return 0


sys.exit(main())
