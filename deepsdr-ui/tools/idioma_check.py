#!/usr/bin/env python3
"""
LAS CADENAS DE INTERFAZ QUE SE QUEDAN EN CASTELLANO.

Llevaba razon dos veces. El modo Ident era 44 cadenas sin traducir, y
cuando fui a mirar si habia mas resulto que si: el veredicto del cargador
-once frases, justo las que lee alguien cuando algo le ha fallado-, las
cuatro fases del enganche de HFDL, y media linea de la pantalla de hora
donde una rama del `?:` pasaba por tr() y la otra no.

POR QUE NO SE VE A OJO
----------------------
Son ~600 cadenas repartidas por veintitantos ficheros, y la que falta no
se distingue de las demas leyendo: se distingue PONIENDO LA RADIO EN
INGLES Y MIRANDO ESA PANTALLA. O sea que solo aparece cuando un usuario
ingles llega a ella, que es tarde.

QUE SE EXIME, Y POR QUE
-----------------------
No todo literal en castellano es un fallo:

  _Static_assert   el mensaje solo lo ve quien compila.
  debug_print*     va al puerto serie, no a la pantalla.
  pares es/en      la tabla de firmas del Ident y las macros X() guardan
                   las dos cadenas seguidas; la de arriba no lleva tr()
                   porque la de abajo ES su traduccion.
  datos            nombres de pais, de emisora y de protocolo. Lo dice
                   idioma.h: traducirlos seria un fallo distinto.

Todo lo demas tiene que pasar por tr() o por T(), o estar en BLANCA de
aqui abajo con su motivo escrito. Una excepcion sin motivo es una
excepcion que nadie puede revisar.

Y LAS PALABRAS ESCRITAS LETRA A LETRA (29/10/2026)
--------------------------------------------------
*** El usuario: que revise que TODAS las palabras cambien de castellano a
ingles al cambiar el idioma. ***

Y habia cinco que no. Este guion recorre los LITERALES del fichero, y en
la pantalla de hora por radio los rotulos se escribian asi:

    *p++ = 'm'; *p++ = 'a'; *p++ = 'r'; *p++ = 'c'; *p++ = 'a';

Eso no es un literal: son cinco caracteres sueltos. Guardian en verde y
"marca", "ciclo", "raras", "mal" y "codigo" en castellano en la radio
inglesa, en la pantalla que mas se mira cuando algo no sincroniza.

Asi que ahora tambien se juntan esas rachas y se miran como palabras. No
es una rareza que no vaya a repetirse: este fichero escribe sin snprintf
-ver el comentario de pega_txt()- y pegar letras a mano es el gesto
natural. Lo que hace falta es que el gesto no esconda una palabra.

Se eximen las rachas que NO son palabras: cifras, signos y los nombres de
bit de las normas -b0, b20, z1z2-, que no se traducen igual que no se
traduce "UTC". La lista esta en BLANCA_LETRAS.

Y LAS TABLAS GEMELAS
--------------------
Lo que no cabe en tr() -una tabla estatica- va en dos tablas, `X_es[]` y
`X_en[]`, como k_fase_es/k_fase_en o k_mot_es/k_mot_en. Ahi la averia
natural es copiar la de arriba y olvidarse de traducirla: compila, el
_Static_assert de tamanos cuadra, y la radio inglesa dice lo mismo que la
castellana.

Asi que se exige que las dos no sean IDENTICAS. No se exige mas -entradas
sueltas iguales son normales: "b0" y "z1z2" son nombres de bit en los dos
idiomas-, pero una copia entera sin tocar si se caza, y es justo la que se
hace sin querer.
"""
import os, re, sys

# ---------------------------------------------------------------- reglas
SIN_PANTALLA = ('debug_print', 'debug_print_dec', 'debug_print_hex',
                'debug_print_always', 'printf', 'fprintf', 'i24_fail')

# Ficheros que son TABLAS DE DATOS, no interfaz. Ver idioma.h: los nombres
# propios no se traducen.
DATOS = {
    'dxcc.c':      'nombres de pais del DXCC',
    'rds_pi.c':    'nombres de emisora de FM',
    'icao24_db.c': 'avisos del cargador de la base de aviones',
    'dcf77.c':     'nombres de las emisoras horarias',
    'splash.c':    'los creditos, que son nombres propios',
    'aic3204.c':   'comentario de formula en ingles',
}

# Lo que queda, una por una y con su motivo.
BLANCA = {
    ('spi_flash.c', 'respaldo: el directorio no cuadra - se repara el bloque'):
        'etiqueta de debug_print_hex32 por el puerto serie; no llega a '
        'ninguna pantalla. Ver spi_flash_respaldo_repara()',
    ('main.c', 'espectro: sin señal, Hz'):
        'texto de depuracion por serie, lo come debug_print_dec en dos ramas',
    ('main.c', 'Se ha perdido - reintentando'):
        'ya va dentro de tr(); aparece tambien suelto en el comentario',
    # Los cuatro motivos de stanag_rx_init(). Los devuelve
    # stanag_rx_porque(), y ESA FUNCION NO LA LLAMA NADIE: hoy no llegan a
    # ninguna pantalla. El dia que alguien los enseñe hay que envolverlos.
    ('stanag_rx.c', 'no hay memoria que prestar'):
        'stanag_rx_porque() no tiene quien lo llame: no se ve',
    ('stanag_rx.c', 'la memoria prestada no esta alineada'):
        'idem',
    ('stanag_rx.c', 'el reparto de memoria ha quedado desalineado'):
        'idem',
    ('stanag_rx.c', 'el entrelazado largo de 2400 bps no cabe en la memoria prestada'):
        'idem',
    # El contenido de PRUEBA.TXT, que formato_comprueba() escribe en el disco
    # y vuelve a leer. No sale en la pantalla: sale en el Bloc de notas de
    # quien enchufe la radio al PC. Y no se traduce por la misma razon que no
    # se traducen las claves de CONFIG.CSV: es un dato, no una frase.
    ('formato.c', 'DEEPSDR disco formateado'):
        'contenido del fichero de prueba en el disco, no es interfaz',
    # El volcado por serie del squitter, puesto el 14/10/2026 al arreglar
    # el CRC del SPDU. Sale por debug_print_dec_always, o sea por el cable,
    # y no llega a ninguna pantalla: es la misma razon que la de arriba.
    ('hfdl_payload_decode.c', '  SPDU (squitter) de la estacion'):
        'texto de depuracion por serie, no es interfaz',
    # El renglon de cabecera de BANCO.CSV. No sale en la pantalla: sale en
    # la hoja de calculo de quien abra el fichero en el PC, y ademas son
    # los NOMBRES DE LAS COLUMNAS, o sea que traducirlos partiria en dos el
    # formato del fichero -un BANCO.CSV escrito en ingles no se leeria en
    # castellano-. Misma razon que las claves de CONFIG.CSV: es un dato.
    ('banco.c', r'banco,memoria,hz,modo,paso_hz,fil_lo,fil_hi,cw\n'):
        'cabecera de columnas de BANCO.CSV, no es interfaz',
}

# Las rachas de letra a letra que NO son palabras que traducir. Como en
# BLANCA, cada una con su motivo: una excepcion sin motivo no se puede
# revisar.
BLANCA_LETRAS = {
    ('main.c', 'UTC'):  'no se traduce, es la escala de tiempo',
    ('main.c', 'lpm'):  'lineas por minuto, se dice igual en los dos idiomas',
}

ACENTO = r"[áéíóúñÁÉÍÓÚÑ¿¡]"
# OJO con las palabras que existen en los DOS idiomas -"no", "error", "media",
# "son"-: metidas aqui, la traduccion inglesa de al lado se marca a si misma
# como castellano. Solo entran palabras que en ingles no significan nada.
PALABRA = (r"(?:\b(?:el|la|los|las|un|una|del|para|con|sin|que|por|mas|menos|"
           r"hay|esta|estan|ya|aun|cada|desde|hasta|segun|sobre|"
           r"entre|ninguna|ninguno|todo|toda|pulsa|elige|busca|buscando|guardar|"
           r"guardado|borrar|borrado|activo|activa|apagado|encendido|fallo|aviso|"
           r"senal|señal|tonos|tono|ancho|centro|pico|trama|ciclo|simbolo|salto|"
           r"velocidad|calidad|llevo|ratos|escuchando|parece|conozca|medible|"
           r"cuadran|separa|repita|arriba|abajo|izquierda|derecha|pantalla|pagina|"
           r"ajustes|volumen|disco|memoria|version|cargador|rama|modo|banda|"
           r"frecuencia|antena|ruido|filtro|ganancia|grabada|arranca|imagen|"
           r"fichero|cabecera|media)\b)")

def parece_castellano(s):
    if len(s) < 4:                      return False
    if not re.search(r"[A-Za-z]{3}", s): return False
    if re.search(ACENTO, s):            return True
    return re.search(PALABRA, s, re.I) is not None

def parece_ingles(s):
    if len(s) < 3: return False
    if re.search(ACENTO, s): return False
    if re.search(PALABRA, s, re.I): return False
    return re.search(r"[A-Za-z]{3}", s) is not None

# ------------------------------------------------------------- analisis
def literales(src):
    """(linea, texto, inicio, fin) de cada literal, saltando comentarios."""
    out, i, n, line = [], 0, len(src), 1
    while i < n:
        c = src[i]
        if c == '\n':                 line += 1; i += 1; continue
        if src.startswith('//', i):
            j = src.find('\n', i);    i = n if j < 0 else j; continue
        if src.startswith('/*', i):
            j = src.find('*/', i + 2); j = n if j < 0 else j + 2
            line += src.count('\n', i, j); i = j; continue
        if c == "'":
            i += 1
            while i < n and src[i] != "'":
                if src[i] == '\\': i += 1
                i += 1
            i += 1; continue
        if c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                if src[j] == '\\': j += 1
                j += 1
            out.append((line, src[i + 1:j], i, j + 1))
            line += src.count('\n', i, j + 1)
            i = j + 1; continue
        i += 1
    return out

def quien_lo_llama(src, a):
    """La funcion cuyo parentesis abre justo antes del literal, o ''."""
    k, prof = a - 1, 0
    while k > 0:
        c = src[k]
        if c in ' \t\n':      k -= 1; continue
        if c == ')':  prof += 1; k -= 1; continue
        if c == '(':
            if prof == 0:
                # Se retrocede A MANO en vez de con re.search sobre src[:k].
                # Esa rebanada copiaba el prefijo entero del fichero -para
                # main.c, un megabyte- y el ancla $ obligaba al motor a
                # recorrerlo hasta el final para leer una palabra que esta
                # justo antes de k. Diez mil llamadas por tanda: 13,3 de los
                # 15 s que tardaba este guion. Medido: 14,93 -> 0,99 s, con
                # la misma salida byte a byte.
                j = k - 1
                while j >= 0 and src[j] in ' \t\n':
                    j -= 1
                fin = j + 1
                while j >= 0 and (src[j].isalnum() or src[j] == '_'):
                    j -= 1
                return src[j + 1:fin] if fin > j + 1 else ''
            prof -= 1; k -= 1; continue
        if c == ',':  k -= 1; continue
        if c == '"':
            j = k - 1
            while j > 0 and not (src[j] == '"' and src[j - 1] != '\\'): j -= 1
            k = j - 1; continue
        return ''
    return ''

def rangos_de(src, fn):
    """(a,b) de cada llamada a `fn`, con sus parentesis equilibrados."""
    out = []
    for m in re.finditer(r'\b' + fn + r'\s*\(', src):
        i = m.end() - 1; prof = 0; j = i
        while j < len(src):
            if src[j] == '(': prof += 1
            elif src[j] == ')':
                prof -= 1
                if prof == 0: break
            j += 1
        out.append((i, j))
    return out

CARACTER = re.compile(r"\*\s*\w+\+\+\s*=\s*'(\\?.)'\s*;")


def letra_a_letra(src, nom):
    """Palabras escritas caracter a caracter, que no son literales.

    Se juntan las rachas de `*p++ = 'x';` seguidas -saltando los
    comentarios, que ya vienen tapados- y se devuelve la racha entera.
    Una racha se corta en cuanto hay una linea que no pega caracteres:
    ahi es donde acaba la palabra y empieza un numero, un bucle o lo que
    sea.
    """
    out = []
    cur, ini = '', 0
    for n, linea in enumerate(src.split('\n'), 1):
        ms = CARACTER.findall(linea)
        if ms:
            if not cur:
                ini = n
            cur += ''.join(' ' if c == '\\\\' else c for c in ms)
            continue
        if cur:
            out.append((ini, cur))
            cur = ''
    if cur:
        out.append((ini, cur))
    res = []
    for n, w in out:
        w = w.strip()
        # Hace falta una PALABRA: tres letras seguidas. Una racha de
        # cifras, barras o signos no tiene nada que traducir.
        if len(w) < 3 or not re.search(r'[A-Za-z]{3}', w):
            continue
        if (nom, w) in BLANCA_LETRAS:
            continue
        res.append((n, w))
    return res


TABLA = re.compile(
    r"\b(\w+)_(es|en)\s*\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;", re.S)


def tablas_gemelas(src):
    """Los pares X_es[]/X_en[] cuyo contenido es identico."""
    tablas = {}
    for m in TABLA.finditer(src):
        tablas.setdefault(m.group(1), {})[m.group(2)] = (
            src.count('\n', 0, m.start()) + 1, m.group(3))
    malas = []
    for nombre, par in sorted(tablas.items()):
        if 'es' not in par or 'en' not in par:
            continue
        if par['es'][1].strip() == par['en'][1].strip():
            malas.append((par['en'][0], nombre))
    return malas


def revisa(path):
    nom = os.path.basename(path)
    if nom in DATOS: return []
    src = open(path, encoding='utf-8', errors='replace').read()
    # las comprobaciones de compilacion, enteras: su mensaje no lo ve nadie
    # que use la radio, y dentro llevan expresiones que despistan al
    # rastreo hacia atras de quien_lo_llama().
    fuera = []
    for fn in ('_Static_assert', 'static_assert') + SIN_PANTALLA:
        fuera += rangos_de(src, fn)
    # y las lineas de #include / atributos
    for m in re.finditer(r'(?m)^\s*#\s*include[^\n]*', src):
        fuera.append((m.start(), m.end()))
    for m in re.finditer(r'__attribute__[^;]*;', src):
        fuera.append((m.start(), m.end()))
    lits = literales(src)
    malos = []
    for idx, (line, s, a, b) in enumerate(lits):
        if not parece_castellano(s):              continue
        if (nom, s) in BLANCA:                    continue
        if any(x <= a <= y for x, y in fuera):    continue
        fn = quien_lo_llama(src, a)
        if fn in ('tr', 'T'):                     continue
        if fn in SIN_PANTALLA:                    continue
        # pareja es/en: el literal de al lado, en las dos lineas siguientes,
        # es su traduccion. Es como estan la tabla de firmas y las macros X().
        # La pareja puede estar DELANTE o DETRAS: en la tabla de firmas van
        # nombre_es, nota_es, nombre_en, nota_en, asi que entre el nombre
        # castellano y su traduccion hay otra cadena por medio.
        pareja = False
        for j in (idx - 3, idx - 2, idx - 1, idx + 1, idx + 2, idx + 3):
            if 0 <= j < len(lits) and abs(lits[j][0] - line) <= 4 \
               and parece_ingles(lits[j][1]):
                pareja = True
        if pareja:                                continue
        malos.append((line, fn or '-', s))

    # Y las palabras escritas letra a letra, que no son literales y por eso
    # no estan en `lits`. Se miran sobre el fichero SIN comentarios, que es
    # lo que hace el bucle de arriba al saltarselos.
    def _tapa(m):
        return ''.join(c if c == '\n' else ' ' for c in m.group(0))

    sin_com = re.sub(r'/\*.*?\*/', _tapa, src, flags=re.S)
    sin_com = re.sub(r'(?m)//[^\n]*', _tapa, sin_com)
    for line, w in letra_a_letra(sin_com, nom):
        malos.append((line, 'letra a letra', w))

    # Y las tablas gemelas que son la misma copiada dos veces.
    for line, nombre in tablas_gemelas(sin_com):
        malos.append((line, 'tabla gemela',
                      '%s_en[] es identica a %s_es[]' % (nombre, nombre)))

    return malos

def main():
    raiz = sys.argv[1] if len(sys.argv) > 1 else '.'
    fuentes = []
    # SOLO User/. El cargador (boot/) arranca ANTES que los ajustes y no
    # tiene idioma: sus textos -"MODO ACTUALIZACION", "Copia update.bin al
    # disco USB"- estan en castellano a proposito y cambiarlo es otro
    # trabajo, con su propio sitio donde guardar la eleccion. Apuntado el
    # 06/10/2026; que no salga aqui no quiere decir que este bien.
    for sub in ('User',):
        d = os.path.join(raiz, sub)
        if os.path.isdir(d):
            fuentes += [os.path.join(d, f) for f in sorted(os.listdir(d))
                        if f.endswith('.c')]
    if not fuentes:
        print('  no encuentro fuentes en', raiz); return 1

    total = 0
    for f in fuentes:
        m = revisa(f)
        if not m: continue
        print('\n  %s' % os.path.relpath(f, raiz))
        for line, fn, s in m:
            print('    %5d  %-16s %s' % (line, fn, s[:64]))
        total += len(m)

    print('\n  %d ficheros revisados' % len(fuentes))
    if total:
        print('  *** %d cadenas de interfaz sin tr() ***' % total)
        print('  Envuelvelas en tr("es","en") o apuntalas en BLANCA con su motivo.')
        print('  Las de "letra a letra" son palabras pegadas caracter a')
        print('  caracter: pasalas por tr() -hay hora_rot() en main.c- o')
        print('  apuntalas en BLANCA_LETRAS si no son una palabra.')
        return 1
    print('  todas las cadenas de interfaz pasan por tr() o por T()')
    return 0

if __name__ == '__main__':
    sys.exit(main())
