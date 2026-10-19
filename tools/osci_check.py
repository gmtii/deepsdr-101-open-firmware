#!/usr/bin/env python3
"""Que todo modo que ensena el osciloscopio de sintonia reciba audio.

  python3 osci_check.py [raiz]

POR QUE EXISTE - 19/10/2026
---------------------------
En los modos digitales el espectro de radiofrecuencia CEDE SU SITIO al
osciloscopio de sintonia (digi_panel_active()). Ese osciloscopio no se
alimenta solo: alguien tiene que llamar a rtty_scope_feed() desde
demod_am.c con el audio del camino que corresponda. Y quien lo decidia
era una lista escrita a mano:

    if (rtty || cw || navtex || wefax || sstv || ft8 || psk31 || dsc)

PKT300 no estaba en ella. O sea que al elegir PKT300 el espectro se
apartaba, el osciloscopio se quedaba sin una sola muestra,
rtty_scope_frame_ready() nunca decia que si y rtty_scope_draw() se volvia
sin pintar: en pantalla quedaba congelado lo ultimo que hubiera dibujado
el espectro. El decodificador funcionaba perfectamente. Avisado por el
usuario.

Es EXACTAMENTE el mismo fallo que el del APRS del 13/10 -cuyo arreglo vive
en aprs_scope_feed(), con su comentario- una rama mas alla: aquel era el
camino de FM, este el de banda lateral. Y es la quinta vez que anadir un
modo se cae por una lista que no lo nombra: digi_panel_active() (tres
veces), modo_panel_entero(), y ahora esta.

LO QUE COMPRUEBA
----------------
Deriva de k_demod_modes[] que modos ensenan el osciloscopio:

    familia distinta de ANA        -> el espectro cede el sitio
    y NO esta en modo_panel_entero() -> el panel no se lo come entero
    y no es WEFAX / SSTV / HELL    -> esos pintan ellos su propia imagen

y exige que el interruptor de cada uno aparezca en alguna de las llamadas
a rtty_scope_feed() de demod_am.c. Si manana entra un modo digital nuevo
y nadie le da de comer, la compilacion se para aqui en vez de pararse la
pantalla en silencio.

No es un analizador de C: lee las dos listas y las compara. Con eso basta
para lo que tiene que impedir.
"""
import os
import re
import sys


# columna de la tabla -> la funcion que dice si ese decodificador esta
# encendido. El orden es el de demod_mode_entry_t; los nombres son los que
# se usan en demod_am.c.
INTERRUPTORES = [
    ('cw',     'cw_get_enabled'),
    ('navtex', 'navtex_activo'),
    ('fax',    'wefax_activo'),
    ('sstv',   'sstv_activo'),
    ('ax25',   'ax25_activo'),
    ('ft8',    'ft8_modo_activo'),
    ('hfdl',   'hfdl_modo_activo'),
    ('wspr',   'wspr_modo_activo'),
    ('ais',    'ais_modo_activo'),
    ('ale',    'ale_modo_activo'),
    ('psk31',  'psk31_activo'),
    ('jtty',   'jtty_modo_activo'),
    ('stanag', 'stanag_modo_activo'),
    ('ident',  'ident_modo_activo'),
    ('hell',   'hell_activo'),
    ('dsc',    'dsc_activo'),
]

# Los que pintan su propia imagen en el hueco y no ensenan osciloscopio.
# Esto NO sale de la tabla porque la tabla no lo sabe: es el reparto del
# bucle principal. Si cambia alli, cambia aqui.
PINTAN_SOLOS = ('wefax_activo', 'sstv_activo', 'hell_activo')


def sin_comentarios(txt):
    """Un nombre citado en un comentario no es una llamada, y estos dos
    ficheros estan llenos de comentarios que los nombran."""
    out = []
    i, n = 0, len(txt)
    while i < n:
        if txt.startswith('/*', i):
            j = txt.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', txt[i:j]))
            i = j
        elif txt.startswith('//', i):
            j = txt.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        else:
            out.append(txt[i])
            i += 1
    return ''.join(out)


def cuerpo(src, cabecera):
    """El cuerpo de una funcion, contando llaves desde la de apertura."""
    m = re.search(cabecera, src)
    if not m:
        return None
    i = src.index('{', m.start())
    prof = 0
    for j in range(i, len(src)):
        if src[j] == '{':
            prof += 1
        elif src[j] == '}':
            prof -= 1
            if prof == 0:
                return src[i + 1:j]
    return None


def filas(src):
    """[(sigla, modo, {interruptor: 1}, familia)] de k_demod_modes[]."""
    m = re.search(r'k_demod_modes\[\]\s*=\s*\{', src)
    if not m:
        return None
    i = src.index('{', m.start())
    prof = 0
    for j in range(i, len(src)):
        if src[j] == '{':
            prof += 1
        elif src[j] == '}':
            prof -= 1
            if prof == 0:
                tabla = sin_comentarios(src[i + 1:j])
                break
    else:
        return None

    # Los renglones, por profundidad de llaves y no por una expresion
    # regular: hay renglones partidos en dos lineas y descripciones con
    # comas dentro.
    crudos, prof, ini = [], 0, 0
    for k, c in enumerate(tabla):
        if c == '{':
            if prof == 0:
                ini = k + 1
            prof += 1
        elif c == '}':
            prof -= 1
            if prof == 0:
                crudos.append(tabla[ini:k])

    out = []
    for fila in crudos:
        mm = re.search(r'"([^"]*)"', fila)
        if not mm:
            continue
        sigla = mm.group(1)
        resto = fila[mm.end():]
        # Fuera lo que va entre parentesis -T("..","..")- para que sus comas
        # no cuenten como separadores de campo.
        plano, p2 = [], 0
        for c in resto:
            if c == '(':
                p2 += 1
            elif c == ')':
                p2 -= 1
            elif p2 == 0:
                plano.append(c)
        campos = [c.strip() for c in ''.join(plano).split(',')]
        campos = [c for c in campos if c != '']
        # El primer campo reconocible es el modo del demodulador; detras va
        # la variante de RTTY y detras los interruptores, por posicion.
        pos = next((k for k, c in enumerate(campos)
                    if c.startswith('DEMOD_MODE_')), -1)
        if pos < 0 or len(campos) < pos + 2 + len(INTERRUPTORES):
            continue
        modo = campos[pos]
        variante = campos[pos + 1]
        banderas = campos[pos + 2:]
        on = {}
        if variante != 'RTTY_VARIANT_NONE':
            on['rtty_get_enabled'] = 1
        for k, (_, fn) in enumerate(INTERRUPTORES):
            if k < len(banderas) and banderas[k].rstrip('U') != '0':
                on[fn] = 1
        fam = next((c for c in banderas if c.startswith('MODO_FAM_')), '')
        out.append((sigla, modo, on, fam))
    return out


def main(raiz):
    mainc = os.path.join(raiz, 'User', 'main.c')
    demodc = os.path.join(raiz, 'User', 'demod_am.c')
    for p in (mainc, demodc):
        if not os.path.isfile(p):
            print('  no encuentro %s  MAL' % p)
            return 1

    with open(mainc, encoding='utf-8') as f:
        src_main = f.read()
    with open(demodc, encoding='utf-8') as f:
        src_demod = f.read()

    tabla = filas(src_main)
    if not tabla:
        print('  no se deja leer k_demod_modes[] en main.c  MAL')
        return 1

    entero = cuerpo(src_main,
                    r'static uint8_t modo_panel_entero\(void\)\s*\{')
    if entero is None:
        print('  no encuentro modo_panel_entero() en main.c  MAL')
        return 1
    entero = sin_comentarios(entero)
    sin_osci = set(re.findall(r'(\w+)\s*\(\s*\)', entero))
    sin_osci.update(PINTAN_SOLOS)

    # Donde se le da de comer al osciloscopio, y POR QUE CAMINO. Son dos
    # ramas distintas del demodulador y no valen la una por la otra: el de
    # banda lateral sale de s_ssb_dec y el de FM del discriminador, via
    # aprs_scope_feed(). PKT300 fallaba justo por esto - ax25_activo()
    # estaba nombrado, pero en la rama de FM, que en LSB no corre.
    #
    # Se coge la condicion del `if` que envuelve cada llamada; mirar el
    # fichero entero no bastaria, porque los nombres salen tambien en los
    # comentarios y en otras ramas.
    limpio = sin_comentarios(src_demod)
    comen = {'SSB': set(), 'FM': set()}
    for mm in re.finditer(r'\b(rtty_scope_feed|aprs_scope_feed)\s*\(\s*(\w+)',
                          limpio):
        if mm.group(1) == 'aprs_scope_feed':
            via = 'FM'
        elif mm.group(2) == 's_ssb_dec':
            via = 'SSB'
        else:
            continue        # el rtty_scope_feed(lote,..) de dentro de
                            # aprs_scope_feed(): fontaneria, no una puerta
        tramo = limpio[max(0, mm.start() - 1200):mm.start()]
        ifs = list(re.finditer(r'\bif\s*\(', tramo))
        if not ifs:
            continue
        ini = ifs[-1].end()
        prof, j = 1, ini
        while j < len(tramo) and prof > 0:
            if tramo[j] == '(':
                prof += 1
            elif tramo[j] == ')':
                prof -= 1
            j += 1
        comen[via].update(re.findall(r'(\w+)\s*\(\s*\)', tramo[ini:j]))

    fallos = []
    osci = []
    for sigla, modo, on, fam in tabla:
        if fam in ('MODO_FAM_ANA', ''):
            continue            # el espectro no cede el sitio
        if not on:
            continue            # digital sin interruptor propio: no lo hay
        if any(fn in sin_osci for fn in on):
            continue            # se queda con el panel entero, o pinta solo
        osci.append(sigla)
        via = 'FM' if modo == 'DEMOD_MODE_NFM' else 'SSB'
        if not any(fn in comen[via] for fn in on):
            fallos.append((sigla, via, sorted(on)))

    print('  %d modos ensenan el osciloscopio de sintonia: %s'
          % (len(osci), ' '.join(osci)))

    if fallos:
        print('')
        print('  ENSENAN EL OSCILOSCOPIO Y NADIE LES DA DE COMER:')
        print('')
        for sigla, via, fns in fallos:
            print('    %-10s  %-4s  %s'
                  % (sigla, via, ', '.join(f + '()' for f in fns)))
        print('')
        print('  El espectro ha cedido su sitio pero rtty_scope_feed() no se')
        print('  llama nunca para este modo, asi que el osciloscopio no')
        print('  recibe ni una muestra y la pantalla se queda CONGELADA con')
        print('  lo ultimo que pinto el espectro. El decodificador funciona:')
        print('  desde fuera parece que se ha colgado la radio.')
        print('')
        print('  Se arregla en demod_am.c, nombrando el interruptor en la')
        print('  rama de audio que corresponda - la de banda lateral o la')
        print('  del discriminador de FM.')
        print('')
        print('  *** %d ***' % len(fallos))
        return 1

    print('  todos reciben audio  ok')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '.'))
