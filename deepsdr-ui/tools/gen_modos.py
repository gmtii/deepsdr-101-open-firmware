#!/usr/bin/env python3
"""
Saca la tabla de modos de main.c a una cabecera que el simulador compila.

POR QUE SE GENERA Y NO SE COPIA
-------------------------------
Es la misma razon que en tools/gen_ajustes.py: una copia a mano de una
tabla del firmware no esta obligada a estar al dia, y de esas ya se han
separado tres en este proyecto. Con la tabla generada, anadir un modo
-o cambiarlo de categoria- se ve en el banco sin tocar nada mas.

Lo que le importa al banco de la tabla son tres cosas: la etiqueta, la
descripcion y la categoria de la columna, porque de eso dependen los
anchos y cuantas celdas lleva cada categoria.
"""
import re
import sys

def familias_del_fuente(src):
    """Los numeros de familia salen de los #define del propio main.c.

    AQUI HABIA UNA LISTA A MANO - 06/10/2026:

        FAM = {'MODO_FAM_ANA': 0, 'MODO_FAM_DIG': 1}

    El dia que nacio MODO_FAM_IMG esto paro la compilacion con "categoria
    desconocida", que esta bien -mejor parar que generar una tabla a medias-
    pero obligaba a tocar DOS sitios para anadir una familia, y el segundo no
    lo menciona ningun comentario del primero. Es la misma piedra que este
    proyecto lleva pisando todo el mes: una lista escrita a mano que no se
    entera de lo nuevo.

    Ahora el numero sale del propio #define, que es la fuente de verdad: si
    main.c dice `#define MODO_FAM_IMG 2U`, aqui vale 2 sin que nadie lo
    escriba dos veces."""
    fam = {}
    for nom, val in re.findall(r'#define\s+(MODO_FAM_[A-Z]+)\s+(\d+)U', src):
        if nom == 'MODO_FAM_COUNT':
            continue
        fam[nom] = int(val)
    return fam


def main(fuente, destino):
    src = open(fuente, encoding='utf-8').read()

    FAM = familias_del_fuente(src)
    if not FAM:
        print('FALLA: no encuentro ningun #define MODO_FAM_*')
        return 1

    # 30/09/2026: las dos tablas llevan los DOS idiomas, T("es", "en").
    m = re.search(r'static const texto_t k_modo_familias\[MODO_FAM_COUNT\]\s*=\s*\{(.*?)\};',
                  src, re.S)
    if not m:
        print('FALLA: no se encuentra k_modo_familias[]')
        return 1
    familias = re.findall(r'T\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)', m.group(1))
    if not familias:
        print('FALLA: k_modo_familias[] ya no trae los dos idiomas')
        return 1
    # Y que haya tantos nombres como #define. Un #define sin su nombre deja
    # una categoria sin rotulo en la columna; un nombre de mas se queda sin
    # modos que ensenar. Ninguna de las dos avisa sola.
    if len(familias) != len(FAM):
        print('FALLA: hay %d #define MODO_FAM_* y %d nombres en '
              'k_modo_familias[]' % (len(FAM), len(familias)))
        return 1

    m = re.search(r'static const demod_mode_entry_t k_demod_modes\[\]\s*=\s*\{(.*?)\n\};',
                  src, re.S)
    if not m:
        print('FALLA: no se encuentra k_demod_modes[]')
        return 1

    # POR ENTRADAS, NO POR LINEAS - 13/10/2026.
    #
    # Esto leia el cuerpo linea a linea, asi que una entrada escrita en DOS
    # -porque su descripcion es larga- no se generaba. Habia tres: SAM-U,
    # SAM-L y SSTV-FM. La rejilla de modos del SIMULADOR salia sin ellas
    # mientras la de la radio las tenia, o sea que la foto del banco
    # enseñaba una pantalla que no existe. El mismo fallo, el mismo dia,
    # que en tools/modos_check.py.
    #
    # Y LA LLAVE DE CIERRE TAMPOCO VA PEGADA AL FINAL: las dos filas de SAM
    # llevan un campo mas detras de MODO_FAM_* (sam_banda), asi que el
    # `(MODO_FAM_\w+)\s*\}` de antes tampoco las habria cogido aunque
    # estuvieran en una sola linea.
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

    filas = []
    for linea in por_entradas(m.group(1)):
        f = re.match(r'\s*\{\s*"([^"]*)"\s*,\s*T\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)\s*,'
                     r'\s*(DEMOD_MODE_\w+).*?(MODO_FAM_\w+)\s*(?:,[^}]*)?\}', linea)
        if f:
            if f.group(5) not in FAM:
                print('FALLA: categoria desconocida %s en "%s"' % (f.group(5), f.group(1)))
                return 1
            filas.append((f.group(1), f.group(2), f.group(3), FAM[f.group(5)], f.group(4)))

    if not filas:
        print('FALLA: k_demod_modes[] no ha dado ninguna entrada reconocible')
        return 1

    with open(destino, 'w', encoding='utf-8') as o:
        o.write('/* GENERADO por tools/gen_modos.py desde User/main.c - no editar. */\n')
        o.write('#define MODO_TABLA_FAMS %d\n' % len(familias))
        o.write('#define MODO_TABLA_IDIOMAS 2   /* 0 = espanol, 1 = ingles */\n')
        o.write('static const char *const k_modo_fam_nombres[MODO_TABLA_FAMS][MODO_TABLA_IDIOMAS] = {\n')
        for es, en in familias:
            o.write('    { "%s", "%s" },\n' % (es, en))
        o.write('};\n\n')
        o.write('#define MODO_TABLA_N %d\n' % len(filas))
        o.write('static const struct { const char *label;\n')
        o.write('                     const char *desc[MODO_TABLA_IDIOMAS]; int fam;\n')
        o.write('                     const char *demod; }\n')
        o.write('    k_modo_tabla[MODO_TABLA_N] = {\n')
        for lab, des_es, des_en, fam, dem in filas:
            o.write('    { "%s", { "%s", "%s" }, %d, "%s" },\n' % (lab, des_es, des_en, fam, dem))
        o.write('};\n')

    print('  %s: %d modos en %d categorias' % (destino, len(filas), len(familias)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
