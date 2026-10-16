#!/usr/bin/env python3
"""
LA LISTA DE TEXTOS DEL CARTEL DE LA CAPTURA, SACADA DEL CODIGO.

El cartel dice por que no se ha podido guardar una captura, y la mitad del
texto no lo escribe main.c: lo escribe spi_flash_anade_porque_txt(), que es
quien sabe por que no cabe. Pegados detras de "NO CABE: " y del tamano que
hace falta, salen frases largas.

Un cartel que no cabe no da error: gfx2_text_in() lo RECORTA, asi que el
motivo se queda a medias justo cuando hace falta leerlo entero.

Esto saca los textos a una cabecera y sim/avisocabe.c los mide con la fuente
de verdad -font_ui_18b, proporcional- y la caja de verdad. Escribir la lista
a mano era garantizar que el dia que alguien anada un motivo nuevo, la
comprobacion siga midiendo los de ayer.
"""
import re
import sys


def main(main_c, spi_c, salida):
    src = open(main_c, encoding='utf-8', errors='replace').read()
    flash = open(spi_c, encoding='utf-8', errors='replace').read()

    textos = []
    for m in re.finditer(r'capt_avisa\(\s*tr\(\s*"([^"]*)"\s*,\s*"([^"]*)"', src):
        textos.append(m.group(1))
        textos.append(m.group(2))

    motivos = []
    m = re.search(r'const char \*spi_flash_anade_porque_txt\(spi_anade_r_t r\)'
                  r'\s*\{(.*?)\n\}', flash, re.S)
    if m:
        for a, b in re.findall(r'tr\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)', m.group(1)):
            motivos += [a, b]

    cabs = re.findall(r'tr\(\s*"(NO CABE: |NO ROOM: )"', src) or ['NO CABE: ']
    colas = [c for par in re.findall(
        r'tr\(\s*"( \(756 kB [^"]*)"\s*,\s*"( \(756 kB [^"]*)"\s*\)', src)
        for c in par] or ['']

    for cab in cabs:
        for mo in motivos:
            for co in colas:
                textos.append(cab + mo + co)

    vistos = []
    for t in textos:
        if t not in vistos:
            vistos.append(t)

    with open(salida, 'w', encoding='utf-8') as f:
        f.write('/* GENERADO por tools/gen_avisos.py - no editar. */\n')
        f.write('#ifndef AVISOS_TABLA_H\n#define AVISOS_TABLA_H\n\n')
        f.write('static const char *const k_avisos[] = {\n')
        for t in vistos:
            f.write('    "%s",\n' % t.replace('"', '\\"'))
        f.write('};\n\n')

        # Y los renglones de la fila "Sitio para fotos", que van en una celda
        # de rejilla -192 px, mucho mas estrecha que el cartel- y se componen
        # igual: numeros variables mas texto fijo. Se ponen con los numeros
        # mas largos que pueden salir (un chip de 8 MB, 512 entradas de
        # raiz), que es el caso que hay que medir.
        celdas = []
        for a, b in re.findall(
                r'tr\(\s*"( kB seguidos)"\s*,\s*"([^"]*)"\s*\)', src):
            celdas.append('8188' + a)
            celdas.append('8188' + b)
        for a, b in re.findall(r'tr\(\s*"(dir )"\s*,\s*"(dir )"\s*\)', src):
            celdas.append(a + '512/512, 8188 kB')
        if not celdas:
            celdas = ['8188 kB seguidos', 'dir 512/512, 8188 kB']
        f.write('static const char *const k_celdas[] = {\n')
        for t in celdas:
            f.write('    "%s",\n' % t.replace('"', '\\"'))
        f.write('};\n\n#endif\n')
    print('  %d textos de cartel -> %s' % (len(vistos), salida))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2], sys.argv[3]))
