#!/usr/bin/env python3
"""
Comprueba la tabla de modos de main.c.

POR QUE EXISTE
--------------
Al anadir el modo CW, la pantalla de modos dejo de aparecer entera. La
causa era un array paralelo de descripciones, k_modo_desc[], indexado por
el mismo indice que k_demod_modes[] y con una entrada menos: la ultima
posicion leia fuera del array y el dibujador de texto se iba a recorrer
memoria arbitraria. El compilador no podia avisar, porque el array
paralelo estaba perfectamente bien formado; solo era mas corto.

La descripcion se metio dentro de la propia tabla, asi que ese fallo
concreto ya no puede repetirse. Lo que esta comprobacion vigila es el
OTRO fallo de la misma familia, que sigue siendo posible: que dos
entradas describan el mismo estado.

Ninguna entrada identifica su estado solo por el demodulador -USB, CW y
RTTY-U son las tres USB por debajo, LSB y RTTY-L las dos LSB-, asi que lo
que tiene que ser unico es la terna (modo, variante de RTTY, CW). Si dos
entradas la comparten, "cual esta activa" deja de tener una sola
respuesta: la etiqueta del boton de modo ensena una y la pantalla de
modos marca otra, y las dos son defendibles. Eso no se ve mirando el
codigo y no lo dice ningun aviso del compilador.
"""
import re
import sys

# La rejilla densa de la pantalla de modos y de bandas: 4x4. Sale de
# UIC_DCOLS * UIC_DROWS en ui_cfg.h, y cfg_fill() de main.c para de
# rellenar en cuanto llega a este numero.
UIC_CELLS = 16


def paginas_ajustes(src):
    """Cada categoria de ajustes tiene que caber en la rejilla de 3x3 de la
    pantalla de configuracion. Pasarse no rompe nada visible al instante
    -la pantalla pagina- pero deja ajustes escondidos en una segunda
    pagina a la que casi nadie va a llegar, que es una forma silenciosa
    de perderlos."""
    m = re.search(r'static const ajuste_t k_ajustes\[\]\s*=\s*\{(.*?)\n\};',
                  src, re.S)
    if not m:
        print('  FALLA: no se encuentra k_ajustes[]')
        return 1

    # EL NOMBRE ES UN T("es","en") - 30/09/2026.
    #
    # Esta expresion pedia una cadena suelta y desde la traduccion las filas
    # son `{ T("AGC", "AGC"), AJ_PAG_RADIO, AJ_AGC }`. Casaba CERO veces
    # sobre 139 lineas: `cuenta` quedaba vacio, el bucle de abajo no imprimia
    # nada y fallos salia 0. O sea, una comprobacion muerta que decia "TODO
    # CORRECTO" pasara lo que pasara - probado metiendo catorce ajustes en
    # una pagina de nueve: seguia diciendo que si.
    #
    # Es exactamente lo que este mismo fichero cuenta que paso con
    # k_demod_modes[] (ver su comentario, mas abajo). Se arreglo aquella el
    # 30/09 por la mañana y esta se quedo atras el mismo dia.
    #
    # Y una comprobacion de que la expresion sigue viva, porque lo que
    # convierte esto en una trampa no es equivocarse: es callarse.
    cuenta = {}
    filas = 0
    for linea in m.group(1).split('\n'):
        if re.match(r'\s*\{', linea):
            filas += 1
        f = re.match(r'\s*\{\s*(?:T\s*\(\s*"[^"]*"\s*,\s*"[^"]*"\s*\)|"[^"]*")'
                     r'\s*,\s*(AJ_PAG_\w+)', linea)
        if f:
            cuenta[f.group(1)] = cuenta.get(f.group(1), 0) + 1

    fallos = 0
    if not cuenta:
        print('  FALLA: k_ajustes[] tiene %d filas y no he entendido NINGUNA.'
              % filas)
        print('         La expresion se ha quedado atras respecto a la tabla.')
        return 1
    if sum(cuenta.values()) != filas:
        print('  FALLA: k_ajustes[] tiene %d filas y solo he entendido %d.'
              % (filas, sum(cuenta.values())))
        fallos += 1
    for pag, n in sorted(cuenta.items()):
        marca = ''
        if n > 9:
            marca = "  *** SE PASA DE 9 ***"
            fallos += 1
        print('    %-16s %d de 9%s' % (pag, n, marca))
    return fallos


def main(ruta):
    src = open(ruta, encoding='utf-8').read()

    m = re.search(r'static const demod_mode_entry_t k_demod_modes\[\]\s*=\s*\{(.*?)\n\};',
                  src, re.S)
    if not m:
        print('FALLA: no se encuentra k_demod_modes[] en', ruta)
        return 1

    # LOS INTERRUPTORES NO SE CUENTAN AQUI - 25/09/2026.
    #
    # Esto llevaba nueve grupos clavados en la expresion, uno por columna.
    # Al anadir FT8 la tabla paso a diez y esta comprobacion dejo de
    # reconocer NINGUNA entrada: no dijo "hay un modo raro", dijo "no hay
    # modos", que es el peor de los dos fallos posibles -una comprobacion
    # que se apaga sola cuando cambia justo lo que vigila-.
    #
    # Ahora los interruptores se leen como una lista de largo libre entre
    # la variante de RTTY y MODO_FAM_*. Anadir el septimo no toca nada de
    # aqui, y la clave de unicidad de mas abajo lo incluye sola.
    # Y LA DESCRIPCION YA NO ES UNA CADENA - 30/09/2026.
    #
    # Desde la traduccion es un T("es","en"), y esta expresion seguia
    # pidiendo dos cadenas sueltas: volvio a pasar exactamente lo de arriba,
    # "no hay modos" en vez de "hay un modo raro", y encima callado desde el
    # 29/09 porque nadie corrio la comprobacion entera. Ahora se admiten las
    # dos formas y se comprueban LAS DOS cadenas del par: una traduccion a
    # medias -T("Amplitud modulada", "")- es un hueco en la pantalla igual
    # que lo era una descripcion vacia.
    # Y POR ENTRADAS, NO POR LINEAS - 13/10/2026. LA TERCERA VEZ.
    #
    # Esto leia el cuerpo de la tabla linea a linea, asi que una entrada
    # escrita en DOS lineas -porque su descripcion es larga- era invisible.
    # Y habia tres: SAM-U, SAM-L y, desde hoy, SSTV-FM. La cuenta de
    # familias decia "MODO_FAM_IMG 3 de 16" con cuatro dentro.
    #
    # Es el mismo fallo que ya esta contado dos veces aqui arriba y con la
    # misma forma: la comprobacion se apaga sola justo donde vigila, y
    # calla. Lo que cambia es que las dos veces anteriores se apago DEL
    # TODO -"no hay modos"- y se noto; esta se apago A MEDIAS, que es peor,
    # porque siguio dando un numero y el numero era mentira.
    #
    # Ahora el cuerpo se parte por entradas: se acumula hasta que las
    # llaves se cierran, y la expresion corre sobre el texto entero con los
    # saltos de linea convertidos en espacios. De paso admite un campo
    # suelto DETRAS de MODO_FAM_* -el sam_banda de las dos filas de SAM-,
    # que era el segundo motivo por el que esas dos no entraban.
    def por_entradas(cuerpo):
        trozo, hondo = '', 0
        for ch in cuerpo:
            if ch == '{':
                hondo += 1
            elif ch == '}':
                hondo -= 1
                trozo += ch
                if hondo == 0:
                    yield ' '.join(trozo.split())
                    trozo = ''
                continue
            if hondo > 0:
                trozo += ch

    entradas = []
    TXT = r'(?:T\s*\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)|"([^"]*)")'
    for linea in por_entradas(m.group(1)):
        f = re.match(r'\s*\{\s*' + TXT + r'\s*,\s*' + TXT + r'\s*,'
                     r'\s*(\w+)\s*,\s*(\w+)\s*,'
                     r'\s*((?:\w+\s*,\s*)+?)(MODO_FAM_\w+)\s*(?:,\s*([^},]*))?\s*\}',
                     linea)
        if f:
            (e_es, e_en, e_1, d_es, d_en, d_1,
             modo, var, flags, fam, cola) = f.groups()
            etiqueta = [x for x in (e_es, e_en, e_1) if x is not None]
            desc     = [x for x in (d_es, d_en, d_1) if x is not None]
            flags = tuple(x.strip().rstrip('U') for x in flags.split(',') if x.strip())
            # LA COLA ENTRA EN LA CLAVE, y no es un detalle: es el campo
            # sam_banda, que es LO UNICO que distingue "SAM", "SAM-U" y
            # "SAM-L" -las tres llevan DEMOD_MODE_SAM y los dieciseis
            # interruptores a cero-. Sin el, en cuanto esta comprobacion
            # empezo a ver esas dos filas (13/10/2026) dijo que eran la
            # misma. Y era cierto. con lo que miraba: lo que estaba
            # incompleto era lo que miraba.
            flags = flags + ((cola or '').strip().rstrip('U') or '-',)
            entradas.append((etiqueta, desc, modo, var, flags, fam))

    if not entradas:
        print('FALLA: k_demod_modes[] no ha dado ninguna entrada reconocible')
        return 1

    fallos = 0
    print('  %d modos en la tabla' % len(entradas))

    # Ningun texto vacio: una celda sin descripcion se ve como un hueco y
    # parece que falta algo, que es como se colo el fallo original.
    for etiqueta, desc, _modo, _var, _flags, _fam in entradas:
        if not all(etiqueta) or not all(desc):
            print('  FALLA: %s no tiene etiqueta o descripcion en algun idioma'
                  % (etiqueta or desc))
            fallos += 1

    # El estado completo -demodulador, variante de RTTY y TODOS los
    # interruptores- tiene que ser unico.
    vistos = {}
    for etiqueta, _desc, modo, var, flags, _fam in entradas:
        etiqueta = etiqueta[0]
        clave = (modo, var) + flags
        if clave in vistos:
            print('  FALLA: "%s" y "%s" describen el mismo estado %s'
                  % (vistos[clave], etiqueta, clave))
            fallos += 1
        vistos[clave] = etiqueta

    # Y todas tienen que llevar los MISMOS interruptores: una fila a la que
    # se le olvide una columna la lee el compilador como un cero y no
    # avisa, pero aqui se ve.
    anchos = set(len(e[4]) for e in entradas)
    if len(anchos) != 1:
        print('  FALLA: no todas las filas tienen los mismos interruptores: %s'
              % sorted(anchos))
        fallos += 1

    if not fallos:
        for etiqueta, desc, modo, var, flags, fam in entradas:
            print('    %-8s %-40s %-16s rtty=%-20s interruptores=%s %s'
                  % (etiqueta[0], ' / '.join(desc), modo, var,
                     ''.join(flags), fam))

    # Cada categoria de la columna tiene que llevar algo. Una categoria
    # vacia se pinta igual que las demas y al tocarla no aparece nada, que
    # se vive como que la pantalla se ha roto.
    porfam = {}
    for e in entradas:
        porfam[e[5]] = porfam.get(e[5], 0) + 1
    # LAS FAMILIAS SE SACAN DEL FUENTE, NO DE UNA LISTA DE AQUI - 06/10/2026.
    #
    # Aqui ponia ('MODO_FAM_ANA', 'MODO_FAM_DIG') escrito a mano. El dia que
    # nacio MODO_FAM_IMG, esta comprobacion siguio en verde sin haberla
    # mirado: ni habria avisado de que estuviera vacia, ni de que se pasara
    # de 16. Es el mismo fallo que este proyecto lleva cazando todo el mes
    # -una lista a mano que no menciona lo nuevo- y estaba dentro del guion
    # que existe para cazarlo.
    familias = re.findall(r'#define\s+(MODO_FAM_[A-Z]+)\s+\d+U', src)
    familias = [f for f in familias if f != 'MODO_FAM_COUNT']
    if not familias:
        print('  FALLA: no encuentro ningun #define MODO_FAM_*')
        fallos += 1
    sobran = set(porfam) - set(familias)
    for f in sorted(sobran):
        print('  FALLA: la tabla usa %s y no hay #define para esa familia' % f)
        fallos += 1
    for fam in familias:
        n = porfam.get(fam, 0)
        marca = ''
        if n > UIC_CELLS:
            marca = '   *** %d NO SE DIBUJAN ***' % (n - UIC_CELLS)
        print('    %-14s %d de %d modos%s' % (fam, n, UIC_CELLS, marca))
        if n == 0:
            print('  FALLA: la categoria %s no tiene ningun modo' % fam)
            fallos += 1
        if n > UIC_CELLS:
            # cfg_fill() recorre la tabla con `n < UIC_CELLS` y PARA ahi.
            # El modo 17 no sale por ningun lado: ni se dibuja, ni avisa,
            # ni el compilador puede saberlo. Es exactamente el mismo
            # fallo que el del decimo ajuste de la pagina Digital, que ya
            # vigila paginas_ajustes() unas lineas mas abajo, y por eso
            # esta comprobacion nace el dia que la familia Digital se
            # queda con 16 de 16: el siguiente modo que alguien anada
            # desaparece en silencio.
            print('  FALLA: la categoria %s tiene %d modos y la rejilla '
                  'densa solo dibuja %d' % (fam, n, UIC_CELLS))
            fallos += 1
        if n == UIC_CELLS:
            print('      (lleno: el siguiente modo de %s NO cabe)' % fam)

    print('  paginas de ajustes:')
    fallos += paginas_ajustes(src)

    print('  TODO CORRECTO' if not fallos else '  %d FALLOS' % fallos)
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '../deepsdr/User/main.c'))
