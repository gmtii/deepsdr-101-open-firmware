#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DECODIFICADOR DE STANAG 4285, COMPLETO Y EN PYTHON (02/10/2026).

Esto NO va en la radio: es la referencia contra la que se escribira el C.
Saca TEXTO de una grabacion de verdad, de principio a fin:

    audio 12 kHz -> banda base -> filtro adaptado -> sincronismo de trama
    -> desvio de frecuencia -> canal -> simbolos -> bits blandos
    -> desentrelazado -> combinacion de repeticiones -> Viterbi -> ASCII

Probado con sim/muestras/stanag4481_psk.mp3, que es un 4285 a 300 bps. De
sus 25,9 segundos salen seis mensajes limpios:

    (001) This is a Stanag4481 test message: 0123456789/*+-
    (002) This is a Stanag4481 test message: 0123456789/*+-
    ...
    (006) This is a Stanag4481 test message: 0123456789/*+-

con una BER de recodificacion del 0,46 % (el ruido da 13 %). A partir del
segundo 20 la grabacion se desvanece y el texto se rompe; eso es la
grabacion, no la cadena.

-----------------------------------------------------------------------
DE DONDE SALE CADA NUMERO
-----------------------------------------------------------------------

LA FORMA DE ONDA (STANAG 4285, anexo principal). Portadora 1800 Hz,
2400 baudios siempre. Trama de 256 simbolos = 106,67 ms:

    80 de preambulo + [32 datos, 16 sondeo] x3 + 32 datos

o sea 128 simbolos de datos y 48 de sondeo por trama. Los conocidos son
los que cumplen (i mod 48) >= 32 contando desde el primer simbolo de
datos.

EL PREAMBULO. LFSR de 5 bits, arranque 11010, tomas en 2 y 4, se saca
state[-1] y el bit nuevo entra por la izquierda; 1 bit -> BPSK. Es una
secuencia m de periodo 31, asi que los 80 simbolos son 31+31+18. Es
IDENTICO en todas las tramas: no lleva ni la velocidad ni el modo.

EL SCRAMBLER DE DATOS. LFSR de 9 bits, todo unos, tomas 4 y 8, tres bits
por simbolo -> indice 8PSK. Se multiplica al simbolo. Los simbolos de
sondeo son EXACTAMENTE ese valor, y por eso sirven de referencia.

LA FASE DE MUESTREO: +2 muestras respecto al pico de correlacion. Barrida
de -3 a +8, el error cuadratico del preambulo hace minimo justo ahi
(0,12 contra 0,71 en el +0). Con la fase mal los sondeos caen de 44 a 18
de 48. Fue el fallo gordo de la primera version.

LA CODIFICACION (anexo E del 4285). Convolucional K=7, tasa 1/2,
generadores 133 y 171 en octal, y T1 se manda PRIMERO. Son los mismos
0x6d / 0x4f de libfec que ya usa User/hfdl_viterbi.c, en su convencion de
bits al reves. Las velocidades:

    2400 bps  8PSK  2/3   tasa 1/2 perforada A LA SALIDA del entrelazador
    1200 bps  QPSK  1/2   tal cual
     600 bps  BPSK  1/2   tal cual
     300 bps  BPSK  1/4   tasa 1/2 repetida 2 veces
     150 bps  BPSK  1/8   repetida 4
      75 bps  BPSK  1/16  repetida 8
    y 3600/2400/1200 SIN codificar, con entrelazador CERO

Y una trampa que me costo la tarde: "the bits are repeated in PAIRS
rather than repetitions of the first followed by the second". O sea
T1 T2 T1 T2, no T1 T1 T2 T2. Probando bit a bit la paridad daba 0,5
siempre; probando por parejas dio 0,0000 a la primera.

EL ENTRELAZADOR (anexo E, pagina E-4 y E-5). Convolucional, 32 filas.
Incremento de retardo por fila:

                        retardo total
                   10,24 s        0,853 s
    2400 bps          48             4
    1200 bps          24             2
    600 - 75 bps      12             1

Conmutador de SALIDA del entrelazador: la secuencia normal 0,1,2,...,31.
Conmutador de ENTRADA: la secuencia nonsequential, que la norma define
como "los resultados modulo 32 de multiplicar por 9 cada numero de la
secuencia normal":

    0, 9, 18, 27, 4, 13, 22, 31, 8, 17, 26, 3, 12, 21, 30, 7,
    16, 25, 2, 11, 20, 29, 6, 15, 24, 1, 10, 19, 28, 5, 14, 23

En el desentrelazador se intercambian los papeles: entrada secuencial,
salida por (9*i) mod 32, y la fila r retrasa j*(31-r).

Y ANTES DE TENER LA NORMA YO HABIA MEDIDO ESE 12. La grabacion empieza
con el transmisor recien arrancado, y eso deja ver el entrelazador
llenandose: mirando los bits en grupos de 32, cada posicion se enciende
en un grupo distinto, y el grupo en que lo hace ES su retardo.

    posicion  0 ->   3      posicion 16 -> 197
    posicion  4 ->  53      posicion 20 -> 241
    posicion  8 ->  98      posicion 24 -> 288
    posicion 12 -> 146      posicion 28 -> 336
                            posicion 31 -> 373

Pendiente 11,98 grupos por posicion, y la ultima en 373 = 12*31 + 1.
Cuando aparecio la norma decia 12 y 32 filas. Lo dejo escrito porque la
proxima vez que no haya documento, el truco sirve.

SINCRONISMO DEL ENTRELAZADOR. La norma lo resuelve sola: "the top (0th)
row always be the starting position for the 1st bit sent or received in
every data frame". Cada trama lleva un multiplo entero de 32 bits de
datos, asi que basta empezar cada trama en la fila 0. No hay nada que
buscar.

ARRANQUE Y FIN DE MENSAJE. Los registros del codificador y del
entrelazador empiezan a cero. Delante del mensaje va SOM = 0x03873C3C y
detras EOM = 0x4B65A5B2, el bit de mas a la izquierda primero, seguidos
de ceros de vaciado (6246 para 600 bps con entrelazado largo).

UNA PRUEBA QUE ME PARECIO ESTUPENDA Y ERA FALSA. Lo dejo escrito porque
casi me cuesta el decodificado.

Como los registros empiezan a cero, razone que el desentrelazador bien
alineado tenia que sacar ceros en sus primeros grupos. Barri los 32
desplazamientos y uno daba 6 unos de 320 mientras el resto daban 157.
Parecia clavado. Pues no: el que decodifica es el otro, el 0, y con el
los primeros grupos dan 14 unos de 32.

El razonamiento estaba mal. El primer grupo de SALIDA no es el principio
del mensaje tal como yo lo pensaba: recoge la fila 0 de un grupo recibido
muy viejo y la fila 31 del grupo recibido de hace un instante, que ya
lleva datos. O sea que la mitad de sus bits vienen de la zona con datos y
NO tiene por que ser cero. La prueba no prueba nada.

Lo que decide es el texto: con desplazamiento 0 salen seis mensajes
legibles con una BER de recodificacion del 0,46 %; con el 1, la paridad
del codigo da 0,33 y no sale nada. Una prueba que pinta bien no vale
contra un resultado que se lee.

EL TEXTO. ASCII sincrono, 8 bits, LSB PRIMERO. La norma admite ademas
ITA2 con trama 5N1 (es lo que usan las costeras francesas en las
frecuencias barco-costera, que emiten en claro) y ASCII asincrono.

-----------------------------------------------------------------------
LO QUE FALTA PARA LAS DEMAS VELOCIDADES
-----------------------------------------------------------------------

Esta escrito el camino de BPSK con repeticion (300, 150, 75) y el de
1/2 directo (600). Para 1200 hay que mapear QPSK a dos bits -con el Gray
[0,1,3,2], que da b1 = (Re+Im < 0) y b0 = (Im-Re > 0)- y para 2400 hay
que meter el 8PSK y el perforado, que segun la norma se hace SALTANDO UNA
FILA DE CADA CUATRO a la salida del entrelazador. Ninguna de las dos
tiene incognitas: es escribirlas.

Uso:  python3 tools/stanag4285_proto.py out/muestras/stanag4481_psk.wav
"""
import sys
import wave

import numpy as np

FS, BD = 12000.0, 2400.0
SPS = int(FS / BD)           # 5 exactas
OFF = 2                      # la fase de muestreo medida


# --- las dos secuencias de la norma ------------------------------------
def preambulo():
    est = [1, 1, 0, 1, 0]; taps = [0, 0, 1, 0, 1]; b = []
    for _ in range(80):
        b.append(est[-1])
        n = 0
        for i, t in enumerate(taps):
            if t: n ^= est[i]
        est = [n] + est[:-1]
    return np.exp(1j * np.pi * np.array(b))


def scrambler():
    est = [1] * 9; taps = [0, 0, 0, 0, 1, 0, 0, 0, 1]; p = np.zeros(176, int)
    for i in range(176):
        p[i] = est[-3] * 4 + est[-2] * 2 + est[-1]
        for _ in range(3):
            n = 0
            for k, t in enumerate(taps):
                if t: n ^= est[k]
            est = [n] + est[:-1]
    return np.exp(2j * np.pi * p / 8)


def rrc(beta, sps, span):
    n = np.arange(-span * sps, span * sps + 1, dtype=float) / sps
    h = np.zeros_like(n)
    for i, t in enumerate(n):
        if abs(t) < 1e-9:
            h[i] = 1 - beta + 4 * beta / np.pi
        elif abs(abs(4 * beta * t) - 1) < 1e-9:
            h[i] = beta / np.sqrt(2) * ((1 + 2 / np.pi) * np.sin(np.pi / (4 * beta))
                                        + (1 - 2 / np.pi) * np.cos(np.pi / (4 * beta)))
        else:
            h[i] = ((np.sin(np.pi * t * (1 - beta))
                     + 4 * beta * t * np.cos(np.pi * t * (1 + beta)))
                    / (np.pi * t * (1 - (4 * beta * t) ** 2)))
    return h / np.sqrt(np.sum(h ** 2))


# --- Viterbi K=7 tasa 1/2, 0x6d / 0x4f (el de User/hfdl_viterbi.c) -----
POLYA, POLYB = 0x6d, 0x4f


def _par(v):
    p = 0
    while v:
        p ^= v & 1; v >>= 1
    return p


SA = np.array([_par(s & POLYA) for s in range(128)], np.int8)
SB = np.array([_par(s & POLYB) for s in range(128)], np.int8)
NS = np.arange(64)
PRE = np.stack([NS >> 1, (NS >> 1) | 32], 1)
EA = np.stack([SA[NS], SA[NS | 64]], 1).astype(float) * 2 - 1
EB = np.stack([SB[NS], SB[NS | 64]], 1).astype(float) * 2 - 1


def viterbi(met):
    """met: dos metricas blandas por bit de informacion, >0 = el bit es 1."""
    n = len(met) // 2
    g = np.full(64, -1e18); g[0] = 0.0
    hist = np.zeros((n, 64), np.uint8)
    a = met[0:2 * n:2]; b = met[1:2 * n:2]
    for t in range(n):
        c = g[PRE] + EA * a[t] + EB * b[t]
        k = np.argmax(c, 1); g = c[NS, k]; hist[t] = k
        g -= g.max()
    s = int(np.argmax(g)); out = np.zeros(n, np.uint8)
    for t in range(n - 1, -1, -1):
        out[t] = s & 1; s = PRE[s, hist[t, s]]
    return out


def recodifica(bits):
    reg = 0; o = np.zeros(2 * len(bits), np.uint8)
    for i, u in enumerate(bits):
        r7 = ((reg << 1) | int(u)) & 0x7f
        o[2 * i] = SA[r7]; o[2 * i + 1] = SB[r7]; reg = r7 & 0x3f
    return o


def desentrelaza(x, j):
    """Anexo E: 32 filas, la fila r retrasa j*(31-r), salida por (9*i)%32."""
    ng = len(x) // 32
    G = x[:ng * 32].reshape(ng, 32)
    r = (9 * np.arange(32)) % 32
    d = j * (31 - r); gm = int(d.max())
    if ng <= gm:
        # el entrelazado largo se come gm grupos antes de dar el primero
        return np.empty((0, 32), x.dtype)
    o = np.empty((ng - gm, 32), x.dtype)
    for i in range(32):
        o[:, i] = G[gm - d[i]:ng - d[i], r[i]]
    return o


def main():
    ruta = sys.argv[1] if len(sys.argv) > 1 else 'out/muestras/stanag4481_psk.wav'
    w = wave.open(ruta, 'rb'); fs = w.getframerate()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float)
    if w.getnchannels() == 2: x = x[0::2]
    print('%s: %.1f s a %d Hz' % (ruta, len(x) / fs, fs))

    t = np.arange(len(x)) / fs
    z = np.convolve(x * np.exp(-2j * np.pi * 1800.0 * t), rrc(0.35, SPS, 6), mode='same')

    P = preambulo(); SCR = scrambler()
    conocido = (np.arange(176) % 48) >= 32

    # --- sincronismo de trama
    pl = np.repeat(P, SPS); pl /= np.linalg.norm(pl)
    c = np.abs(np.correlate(z, pl, mode='valid'))
    e = np.sqrt(np.convolve(np.abs(z) ** 2, np.ones(len(pl)), mode='valid'))
    cn = c / (e + 1e-9)
    print('  correlacion del preambulo: max %.3f  fondo %.3f' % (cn.max(), cn.mean()))
    L = 256 * SPS
    i0 = int(np.argmax(cn[:L]))
    ini = []
    for k in range((len(z) - i0 - L) // L):
        a = i0 + k * L
        ini.append(max(0, a - 3) + int(np.argmax(cn[max(0, a - 3):a + 4])))
    ini = np.array(ini)
    print('  tramas: %d' % len(ini))

    # --- desvio de frecuencia, del periodo 31 del preambulo
    dws = []
    for a in ini:
        r = z[a + OFF:a + OFF + 80 * SPS:SPS]
        if len(r) < 80: continue
        rp = r * np.conj(P)
        dws.append(np.angle(np.sum(rp[31:62] * np.conj(rp[0:31]))) / 31.0)
    dw = float(np.median(dws))
    print('  desvio de frecuencia: %+.2f Hz' % (dw * BD / (2 * np.pi)))
    zg = z * np.exp(-1j * dw * np.arange(len(z)) / SPS)

    # --- canal, con cuatro anclas por trama
    anclas = [(np.arange(0, 80), P, 40.0)]
    for s in (32, 80, 128):
        anclas.append((80 + np.arange(s, s + 16), SCR[s:s + 16], 80.0 + s + 7.5))

    U = []; buenos = 0; tot = 0
    for a in ini:
        r = zg[a + OFF:a + OFF + 256 * SPS:SPS]
        if len(r) < 256: continue
        cen = []; gan = []
        for (ix, ref, cc) in anclas:
            gan.append(np.sum(r[ix] * np.conj(ref)) / len(ix)); cen.append(cc)
        cen = np.array(cen); fas = np.unwrap(np.angle(gan)); amp = np.abs(np.array(gan))
        n = np.arange(256)
        d = r / (np.interp(n, cen, amp) * np.exp(1j * np.interp(n, cen, fas)) + 1e-12)
        pr = d[80:][conocido] * np.conj(SCR[conocido])
        buenos += int(np.sum(np.abs(np.angle(pr)) < np.pi / 8)); tot += 48
        U.append(d[80:][~conocido] * np.conj(SCR[~conocido]))
    U = np.concatenate(U)
    print('  sondeos en su sitio: %d de %d = %.0f%%' % (buenos, tot, 100.0 * buenos / tot))

    # --- BPSK: decision blanda, desentrelazado, repeticiones, Viterbi
    m = U.real / (np.abs(U) + 1e-9)
    o = desentrelaza(m, 12)          # entrelazado largo, 600-75 bps
    print('  grupos desentrelazados: %d' % len(o))
    if len(o) < 60:
        # El entrelazado largo se come 372 grupos antes de dar el primero.
        # Con menos de 60 utiles no hay con que decidir nada, y el Viterbi
        # devolveria algo con buena cara igualmente: se dice y se para.
        print('  la grabacion no da para el entrelazado largo: necesita '
              '%d grupos mas de señal' % (60 - len(o)))
        return

    mejor = None
    for fin in range(40, len(o), 20):
        lim = o[6:fin].reshape(-1)
        v = lim[:len(lim) // 4 * 4].reshape(-1, 4)
        # tasa 1/4: T1 T2 T1 T2, las dos copias se suman en blando
        sm = np.stack([v[:, 0] + v[:, 2], v[:, 1] + v[:, 3]], 1).reshape(-1)
        bits = viterbi(-sm)
        ber = float(np.mean(recodifica(bits) != (-sm > 0).astype(np.uint8)))
        if ber > 0.03: break
        mejor = (fin, bits, ber)
    if mejor is None:
        print('  no engancha: la grabacion no es un 4285 a 300 bps o va muy mal')
        return
    fin, bits, ber = mejor
    B = ''.join(map(str, bits))
    print('  decodificado hasta el grupo %d: %d bits, BER de recodificacion %.4f'
          % (fin, len(B), ber))

    print('\n--- TEXTO (ASCII sincrono, 8 bits, LSB primero) ---')
    txt = ''.join(chr(int(B[i:i + 8][::-1], 2)) if 32 <= int(B[i:i + 8][::-1], 2) < 127 else '.'
                  for i in range(0, len(B) - 7, 8))
    for i in range(0, len(txt), 76):
        print('  ' + txt[i:i + 76])


main()
