#!/usr/bin/env python3
"""
LAS CADENAS DE INTERFAZ QUE SE QUEDAN EN CASTELLANO.

Pedido por el usuario, 06/10/2026.

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
                m = re.search(r"(\w+)\s*$", src[:k])
                return m.group(1) if m else ''
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
        return 1
    print('  todas las cadenas de interfaz pasan por tr() o por T()')
    return 0

if __name__ == '__main__':
    sys.exit(main())
