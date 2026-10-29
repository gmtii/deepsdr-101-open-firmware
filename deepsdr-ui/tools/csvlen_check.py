#!/usr/bin/env python3
"""
Mide el tamano PEOR CASO de CONFIG.CSV a partir de User/settings.c.

Por que existe: el buffer del CSV se ha subido a mano tres veces (256 -> 384
-> 512), cada vez con una cuenta hecha a ojo escrita en el comentario, y cada
vez porque la anterior se habia quedado corta. append_str()/append_u32()/
append_i32() recortan en vez de pisar memoria, asi que quedarse corto no
rompe nada de golpe: escribe un CONFIG.CSV al que le faltan las ultimas
lineas y se pierden ajustes en silencio. Esto lo convierte en un numero.

No lleva ninguna copia de la lista de claves: las saca del propio settings.c,
tanto las de append_str(...,"clave,") como las de la tabla k_extra_claves[].
Si manana se anade una clave, este banco la ve sola.
"""
import re
import sys

# Un entero con signo de 32 bits, lo mas largo que puede escribir append_i32.
ANCHO_ENTERO = len("-2147483648")


def main(ruta):
    src = open(ruta, encoding='utf-8').read()

    # SIN COMENTARIOS - 28/10/2026, y por lo mismo que persist_check.py.
    #
    # El punto 4 de aqui abajo busca el tamaño del buffer con un
    # `(?:s_async_csv_buf|csv|buf)\[(\d+)\]` y se queda con el MENOR. En
    # settings.c hay un comentario que cuenta la historia del fichero y
    # dice "Aqui habia un uint8_t buf[1600] local": ese 1600 no es un
    # buffer, es una frase, y era el menor de los dos.
    #
    # Resultado: con el buffer de verdad en 2.112 bytes y el peor caso en
    # 1.618, este banco decia "NO CABE: el buffer necesita al menos 1619" y
    # dos renglones mas abajo "el peor caso ocupa 1618 y sobran 430 OK".
    # Las dos frases en la misma pantalla, contradiciendose.
    #
    # Un banco al que se le puede cambiar el veredicto escribiendo prosa no
    # mide el codigo. Se miran los numeros que COMPILAN y nada mas.
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    src = re.sub(r'//[^\n]*', '', src)

    # 1) Las claves con nombre propio: append_str(buf, p, buf_size, "clave,")
    con_nombre = re.findall(r'append_str\(buf,\s*p,\s*buf_size,\s*"([a-z_0-9]+),"\)', src)

    # 2) Los valores que son texto y no numero, para no contarlos como enteros:
    #    mode, audio_bw, spectrum_style, spectrum_palette. Se busca el literal
    #    mas largo que el fichero sepa escribir para cada uno.
    textuales = {}
    for nombre, fn in (('mode', 'mode_to_str'), ('audio_bw', 'audio_bw_to_str')):
        cuerpo = re.search(r'\b' + fn + r'\([^)]*\)\s*\n\{(.*?)\n\}', src, re.S)
        if cuerpo:
            textuales[nombre] = max((len(x) for x in re.findall(r'"([^"]*)"', cuerpo.group(1))), default=0)
    # spectrum_style / spectrum_palette: los escribe un getter de otro fichero.
    # El peor caso conocido se declara aqui porque NO esta en settings.c.
    textuales['spectrum_style'] = len("OUTLINE")
    textuales['spectrum_palette'] = len("TEMPER_COLORS")   # ver spectrum.c

    # 3) Las claves de tabla.
    tabla = re.search(r'k_extra_claves\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    de_tabla = re.findall(r'"([a-z_0-9]+)"', tabla.group(1)) if tabla else []

    # 4) El tamano del buffer que declara el propio fichero.
    buffers = [int(x) for x in re.findall(r'(?:s_async_csv_buf|csv|buf)\[(\d+)\]', src)]
    buf_size = min(buffers) if buffers else 0

    total = len("key,value\n")
    detalle = []
    for k in con_nombre:
        v = textuales.get(k, ANCHO_ENTERO)
        n = len(k) + 1 + v + 1
        total += n
        detalle.append((k, v, n))
    for k in de_tabla:
        n = len(k) + 1 + ANCHO_ENTERO + 1
        total += n
        detalle.append((k, ANCHO_ENTERO, n))

    print("CONFIG.CSV: %d claves con nombre + %d de tabla" % (len(con_nombre), len(de_tabla)))
    print("  peor caso  %d bytes   buffer declarado %d" % (total, buf_size))
    print("  clusters de 512 que ocupa: %d" % ((total + 511) // 512))
    print("  las cinco lineas mas largas:")
    for k, v, n in sorted(detalle, key=lambda t: -t[2])[:5]:
        print("    %-20s valor hasta %2d car -> %3d bytes" % (k, v, n))

    fallos = 0
    if buf_size < total + 1:
        print("  *** NO CABE: el buffer necesita al menos %d ***" % (total + 1))
        fallos += 1
    else:
        print("  holgura: %d bytes  OK" % (buf_size - total))

    # El guardado rapido (spi_flash_async_save_start) exige que el fichero
    # siga ocupando los MISMOS bloques de 512 que ya tenia. Antes eso
    # dependia de cuanto ocuparan los valores, y el banco tenia que
    # comprobar que el minimo y el maximo no cayeran a lados distintos de un
    # multiplo de 512 - una condicion que habia que volver a ganarse cada vez
    # que se anadia un ajuste, y que el 23/09/2026 se perdio al anadir la
    # velocidad de la cascada.
    #
    # Ahora build_csv() rellena el fichero hasta CSV_TAMANO con lineas en
    # blanco, asi que el tamano es fijo por construccion. Lo unico que queda
    # por comprobar es que el peor caso quepa dentro.
    m = re.search(r'#define\s+CSV_TAMANO\s+(\d+)', src)
    if not m:
        print("  *** no encuentro CSV_TAMANO en settings.c ***")
        fallos += 1
    else:
        fijo = int(m.group(1))
        if fijo % 512 != 0:
            print("  *** CSV_TAMANO (%d) no es multiplo de 512 ***" % fijo)
            fallos += 1
        if total > fijo:
            print("  *** El peor caso (%d) no cabe en CSV_TAMANO (%d):" % (total, fijo))
            print("      el fichero dejaria de tener tamano fijo ***")
            fallos += 1
        else:
            print("  tamano fijo %d bytes = %d bloques de 512, siempre" % (fijo, fijo // 512))
            print("  el peor caso ocupa %d y sobran %d  OK" % (total, fijo - total))

    print("  %s" % ("HAY FALLOS" if fallos else "todo correcto"))
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '../deepsdr/User/settings.c'))
