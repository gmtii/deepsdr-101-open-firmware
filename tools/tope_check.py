#!/usr/bin/env python3
"""
EL BANCO DEL TOPE DE BORRADO.

Pedido por el usuario.

Desde hoy hay dos customboot en la misma carpeta y arrastrar el que no era
es un error de un segundo. El caro es el del 470 en una radio de 512 kB:
intenta borrar sectores que en ese chip NO EXISTEN, se queda ahi en mitad de
una grabacion y la radio no vuelve.

cargador_tope_segun() de boot/boot_main.c es lo que lo impide: compara lo
que el binario PRETENDE (el macro) con lo que el chip DICE tener (la palabra
de fabrica de 0x1FFF7A20) y se queda con la menor.

Esto la compila de verdad -no una copia de la logica, que no probaria nada-
y la llama con las cifras de los dos chips y con lecturas rotas. Un banco
que no se ha visto fallar no esta probado, asi que los casos "mal" tambien
estan: si alguien quita la guarda, aqui se pone rojo.
"""
import os
import re
import subprocess
import sys
import tempfile

AQUI = os.path.dirname(os.path.abspath(__file__))
FUENTE = os.path.join(AQUI, '..', 'boot', 'boot_main.c')

CARGADOR_H = os.path.join(AQUI, '..', 'User', 'cargador.h')
CARGADOR_C = os.path.join(AQUI, '..', 'User', 'cargador.c')


def topes_del_fuente():
    """
    Los dos CARGA_BORRA_HASTA, leidos de User/cargador.h.

    *** ESTO ESTABA A MANO Y ERA EL MISMO FALLO QUE VENIMOS A CAZAR -
    16/10/2026. *** El banco traia `V450 = 0x08080000` y `V470 = 0x08100000`
    copiados. O sea que si alguien movia el macro, el banco seguia probando
    las cifras viejas y seguia en verde: un banco que no mira el codigo no
    prueba el codigo, prueba su propia copia.
    """
    txt = open(CARGADOR_H, encoding='utf-8', errors='replace').read()
    m = re.findall(r'#define\s+CARGA_BORRA_HASTA\s+(0x[0-9A-Fa-f]+)', txt)
    if len(m) != 2:
        sys.exit('tope_check: esperaba DOS definiciones de CARGA_BORRA_HASTA en\n'
                 '  %s (una por chip) y he encontrado %d.\n'
                 '  Si el esquema ha cambiado, arregla este banco: no lo borres.'
                 % (CARGADOR_H, len(m)))
    # el #ifdef CHIP_F470VG va primero, el #else despues
    return int(m[1], 16), int(m[0], 16)


def fin_de_la_tabla():
    """
    Donde acaba el ultimo sector de k_sectores[] en cada variante.

    Es la cifra que de verdad decide cuanto se borra, y la que se quedo
    atras el 16/10 mientras las otras cuatro subian. Aqui se lee del
    fuente y se compara con el tope: si se vuelven a separar, rojo.
    """
    txt = open(CARGADOR_C, encoding='utf-8', errors='replace').read()
    m = re.search(r'static const uint32_t k_sectores\[\] = \{(.*?)\n\};', txt, re.S)
    if not m:
        sys.exit('tope_check: no encuentro k_sectores[] en %s' % CARGADOR_C)
    cuerpo = m.group(1)
    comun, extra = cuerpo, ''
    if '#ifdef CHIP_F470VG' in cuerpo:
        comun, extra = cuerpo.split('#ifdef CHIP_F470VG', 1)
        extra = extra.split('#endif', 1)[0]
    dirs450 = [int(x, 16) for x in re.findall(r'(0x[0-9A-Fa-f]+)UL', comun)]
    dirs470 = dirs450 + [int(x, 16) for x in re.findall(r'(0x[0-9A-Fa-f]+)UL', extra)]
    if not dirs450:
        sys.exit('tope_check: k_sectores[] parece vacia')

    #   S3 16 kB, S4 64 kB, S5..S11 128 kB
    def tam(a):
        if a < 0x08010000: return 0x4000
        if a < 0x08020000: return 0x10000
        return 0x20000

    return (dirs450[-1] + tam(dirs450[-1]),
            dirs470[-1] + tam(dirs470[-1]))


V450, V470 = topes_del_fuente()

#  kb del chip, tope del binario, lo que tiene que salir, por que
CASOS = [
    (512,  V450, 0x08080000, "el de siempre en su radio: 512 kB"),
    (1024, V470, 0x08100000, "el del 470 en su radio: 1024 kB enteros"),
    (1024, V450, 0x08080000, "el del 450 en un 470: media flash, pero vivo"),
    (512,  V470, 0x08080000, "EL CARO: el del 470 en un 450, manda el chip"),
    (2048, V470, 0x08100000, "un VIT6 de 2048: el macro sigue mandando"),
    (2048, V450, 0x08080000, "lo mismo con el del 450"),
    (0,    V470, 0x08080000, "palabra de fabrica a cero: conservador"),
    (0xFFFF, V470, 0x08080000, "palabra a 0xFFFF: conservador"),
    (128,  V470, 0x08080000, "menos que cualquier chip: no vale, conservador"),
    (255,  V470, 0x08080000, "justo por debajo del rango: conservador"),
    (256,  V470, 0x08040000, "el menor que se acepta; OJO: deja S6 y S7 sin borrar"),
    (4096, V470, 0x08100000, "el mayor que se acepta"),
    (4097, V470, 0x08080000, "uno mas: fuera de rango, conservador"),
]


def saca_la_funcion():
    """La funcion tal cual esta en boot_main.c, sin copiarla a mano."""
    txt = open(FUENTE, encoding='utf-8', errors='replace').read()
    m = re.search(r'^uint32_t cargador_tope_segun\(uint32_t kb, uint32_t tope_macro\)\n\{.*?\n\}',
                  txt, re.S | re.M)
    if not m:
        sys.exit('tope_check: no encuentro cargador_tope_segun() en boot/boot_main.c.\n'
                 '  Si se ha renombrado, este banco deja de probar nada: '
                 'arreglalo, no lo borres.')
    return m.group(0)


def main():
    cc = None
    for exe in ('cc', 'gcc', 'clang'):
        if subprocess.run(['which', exe], capture_output=True).returncode == 0:
            cc = exe
            break
    if cc is None:
        print('tope_check: no hay compilador de PC; no se puede probar')
        return 0

    with tempfile.TemporaryDirectory() as d:
        c = os.path.join(d, 't.c')
        with open(c, 'w', encoding='utf-8') as f:
            f.write('#include <stdint.h>\n#include <stdio.h>\n')
            f.write(saca_la_funcion())
            f.write('''
int main(void)
{
    uint32_t kb, tope;
    while (scanf("%u %u", &kb, &tope) == 2) {
        printf("%u\\n", (unsigned)cargador_tope_segun(kb, tope));
    }
    return 0;
}
''')
        exe = os.path.join(d, 't')
        r = subprocess.run([cc, '-O2', '-Wall', '-Wextra', c, '-o', exe],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print('tope_check: la funcion no compila sola:')
            print(r.stderr.strip()[:800])
            return 1

        entrada = '\n'.join('%d %d' % (kb, t) for kb, t, _e, _p in CASOS)
        sal = subprocess.run([exe], input=entrada, capture_output=True, text=True)
        salidas = [int(x) for x in sal.stdout.split()]

    print('')
    print('EL TOPE DE BORRADO DEL CARGADOR')
    print('')
    mal = 0
    for (kb, tope, esperado, porque), dio in zip(CASOS, salidas):
        ok = (dio == esperado)
        if not ok:
            mal += 1
        print('  %-4s chip %5d kB  binario 0x%08X  ->  0x%08X   %s'
              % ('ok' if ok else 'MAL', kb, tope, dio, porque))
        if not ok:
            print('       esperaba 0x%08X' % esperado)
    #
    # Y LO QUE DE VERDAD SE SOLTO EL 16/10: el tope contra la tabla.
    #
    # Los once casos de arriba pueden estar todos verdes y la variante no
    # borrar la mitad de la flash, porque miden la DECISION y no lo que se
    # borra. Esto mide las dos cifras que tienen que acabar en el mismo
    # sitio: CARGA_BORRA_HASTA (cargador.h) y el final del ultimo sector
    # de k_sectores[] (cargador.c).
    #
    fin450, fin470 = fin_de_la_tabla()
    print('  el tope contra la tabla de sectores:')
    for nombre, tope, fin in (('450', V450, fin450), ('470', V470, fin470)):
        ok = (tope == fin)
        if not ok:
            mal += 1
        print('    %-4s %s  tope 0x%08X   la tabla acaba en 0x%08X'
              % ('ok' if ok else 'MAL', nombre, tope, fin))
        if not ok:
            print('         *** se acepta una imagen de %d bytes y se borran %d.'
                  % (tope - 0x0800C000, fin - 0x0800C000))
            print('         Es el fallo del 16/10: funciona la primera vez y')
            print('         falla la segunda, con la aplicacion ya borrada.')

    print('')
    if mal:
        print('  *** %d MAL de %d casos + 2 comprobaciones de tabla.'
              % (mal, len(CASOS)))
        return 1
    print('  los %d casos y las dos tablas, correctos.' % len(CASOS))
    return 0


if __name__ == '__main__':
    sys.exit(main())
