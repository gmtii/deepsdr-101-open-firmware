#!/usr/bin/env python3
"""
Genera User/ft8_tablas.c: las tablas de la FFT de FT8, en FLASH.

POR QUE
-------
Vivian dentro de g_ft8_shared_ram, la memoria que FT8 le toma prestada a la
cascada, y ft8_fft1024_init() las reconstruia EN CADA ENTRADA a FT8 porque
cada salida las borraba. Eran 10.240 bytes:

    seno y coseno (512 floats cada uno) ...  4.096
    ventana de Hann (1024 floats) .........  4.096
    orden de bits invertido (1024 u16) ....  2.048

Y son funcion pura de FT8_FFT_SIZE, que es una constante de compilacion. O
sea 10 kB de la memoria mas disputada de la radio gastados en unos numeros
que no cambian nunca.

LO QUE SE GANA NO ES RAM DIRECTAMENTE, y conviene entenderlo: la union mide
lo que mida su miembro MAYOR, que es la cascada con 54.432 bytes, y el
miembro de FT8 medía 52.484. Quitarle 10.240 no baja la union ni un byte.

Lo que se gana es SITIO DENTRO de la union, y ahi es donde se cobra: los seis
buffers que FT8 tenia FUERA -s_toc, s_tov, s_out_block, s_queue, s_lin y
candidate_list, 10.748 bytes de SRAM- se meten dentro. 42.244 + 10.748 =
52.992, que sigue cabiendo bajo los 54.432. Esos 10.748 son el ahorro.

Y DE REGALO SE VA EL SELLO. fft_sello existia porque estas tablas vivian en
memoria que la cascada pisaba: sin construir, fft_bitrev contenia indices de
paleta y se usaba COMO INDICE, asi que la radio se colgaba al entrar en FT8
(ver sim/ft8_tablas.c, que nacio de ese cuelgue). En flash no hay nada que
sellar: no se pueden borrar.

LOS VALORES SON LOS MISMOS, BIT A BIT. Se calculan con la MISMA
aproximacion de Bhaskara-I que usaba ft8_fft1024_init() -no con math.sin-
para que el decodificador vea exactamente los numeros que veia antes. Si se
usara el seno exacto las tablas serian mas precisas y el resultado podria
cambiar, y "mas preciso" no es "igual": eso seria otro cambio, y se haria
aparte y midiendolo.

SE GENERA, NO SE ESCRIBE A MANO, y sim/ft8_tablas.c comprueba en cada
`make comprueba` que lo generado es exactamente lo que producia la funcion.

Y DE CUATRO TABLAS SE QUEDAN EN TRES - 26/10/2026
-------------------------------------------------
Haciendo hueco en la flash interna, que se habia quedado en 1.346 bytes
libres de 491.520. La del orden de bits invertido se va, y es la unica de
las cuatro que se puede quitar sin cambiar un solo bit del resultado:

  - las otras tres son de coma flotante y salen de una aproximacion, asi
    que recalcularlas o deducir una de otra cambia valores -medido: la
    ventana de Hann NO es simetrica bit a bit (349 de 512 pares difieren)
    y el seno no se saca del coseno (152 de 512)-. Tocar eso seria cambiar
    los numeros que ve el decodificador, que es justo lo que esta cabecera
    dice que no se hace de pasada;

  - la del orden de bits es de ENTEROS, y es exactamente invertir diez
    bits: comprobado, 0 diferencias en 1.024 valores. En un Cortex-M4 eso
    es UNA instruccion, `rbit`, de un ciclo. O sea que la tabla costaba
    2.048 bytes de flash por ahorrar lo que vale menos que leerla.

Se calcula en ft8_bitrev10() (User/ft8_fft1024.h). 2.048 bytes de flash
interna, sin tocar un valor.
"""
import sys

N = 1024
PI = 3.14159265358979


def f32(x):
    """Redondea a float de 32 bits, que es lo que hace el ARM."""
    import struct
    return struct.unpack('f', struct.pack('f', x))[0]


def sin_bhaskara(x):
    """La misma sinf_approx_local() de User/ft8_fft1024.c, paso a paso y en
    float de 32 bits en cada operacion, para que el resultado coincida."""
    pi = f32(PI)
    negate = 0
    while x > pi:
        x = f32(x - f32(2.0 * pi))
    while x < -pi:
        x = f32(x + f32(2.0 * pi))
    if x < 0.0:
        x = f32(-x)
        negate = 1
    num = f32(f32(16.0 * x) * f32(pi - x))
    den = f32(f32(5.0 * f32(pi * pi)) - f32(f32(4.0 * x) * f32(pi - x)))
    x2 = f32(num / den)
    return f32(-x2) if negate else x2


def cos_bhaskara(x):
    return sin_bhaskara(f32(x + f32(PI / 2.0)))


def tablas():
    tc, ts, hann = [], [], []
    for n in range(N // 2):
        ang = f32(f32(f32(-2.0 * f32(PI)) * f32(n)) / f32(N))
        tc.append(cos_bhaskara(ang))
        ts.append(sin_bhaskara(ang))
    for n in range(N):
        ang = f32(f32(f32(2.0 * f32(PI)) * f32(n)) / f32(N - 1))
        hann.append(f32(0.5 - f32(0.5 * cos_bhaskara(ang))))
    return tc, ts, hann


def flt(x):
    t = '%.9g' % x
    if 'e' in t or 'E' in t:
        m, e = t.split('e') if 'e' in t else t.split('E')
        if '.' not in m:
            m += '.0'
        return m + 'e' + e + 'f'
    if '.' not in t:
        t += '.0'
    return t + 'f'


def bloque(nombre, tipo, vals, conv, por_fila, dim):
    out = ['const %s %s[%s] = {' % (tipo, nombre, dim)]
    for i in range(0, len(vals), por_fila):
        out.append('    ' + ', '.join(conv(x) for x in vals[i:i + por_fila]) + ',')
    out.append('};')
    return out


def texto():
    tc, ts, hann = tablas()
    out = ['/*',
           ' * GENERADO POR tools/gen_ft8_tablas.py - NO EDITAR A MANO.',
           ' *',
           ' * Las tres tablas de coma flotante de la FFT de FT8, en FLASH y no',
           ' * dentro de la memoria que FT8 le toma prestada a la cascada. Eran',
           ' * 10.240 bytes de SRAM liberados DENTRO de la union, que es donde',
           ' * hacian falta: ver la cabecera del generador y ft8_shared_ram.h.',
           ' *',
           ' * La cuarta -el orden de bits invertido- se fue el 26/10/2026: es de',
           ' * enteros, la hace `rbit` en un ciclo y costaba 2.048 bytes de flash.',
           ' * Ver ft8_bitrev10() en ft8_fft1024.h.',
           ' *',
           ' * Los valores salen de la MISMA aproximacion de Bhaskara-I que usaba',
           ' * ft8_fft1024_init(), bit a bit, para que el decodificador vea',
           ' * exactamente los numeros de antes. sim/ft8_tablas.c lo comprueba.',
           ' */',
           '#include "ft8_fft1024.h"',
           '']
    out += bloque('k_ft8_twiddle_cos', 'float', tc, flt, 6, 'FT8_FFT_SIZE / 2U')
    out.append('')
    out += bloque('k_ft8_twiddle_sin', 'float', ts, flt, 6, 'FT8_FFT_SIZE / 2U')
    out.append('')
    out += bloque('k_ft8_hann', 'float', hann, flt, 6, 'FT8_FFT_SIZE')
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    destino = sys.argv[1] if len(sys.argv) > 1 else 'User/ft8_tablas.c'
    with open(destino, 'w', encoding='utf-8') as f:
        f.write(texto())
    print('  %s: %d+%d+%d floats' % (destino, N // 2, N // 2, N))
