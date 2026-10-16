#!/usr/bin/env python3
"""
Disena los dos filtros del RDS y saca User/rds_fir.h.

POR QUE GENERADO Y NO ESCRITO A MANO
------------------------------------
Son 96 numeros. Escritos a mano son 96 sitios donde equivocarse en silencio:
un filtro con un coeficiente mal no falla, solo decodifica peor, y eso se
confunde con "hay poca senal". Generandolos, el fichero .h lleva escrito el
diseno del que salen y se puede volver a sacar identico.

LOS DOS FILTROS

1. DIEZMADO. La subportadora de RDS esta a 57 kHz del centro y ocupa +/-2,4
   kHz. Despues de mezclar por 57 kHz hay que quedarse con esos +/-2,4 kHz y
   bajar de 192 kHz a 12 kHz (diezmado de 16). Lo que decide el numero de
   coeficientes es el ALIASING: al diezmar por 16, todo lo que este por
   encima de 6 kHz se pliega encima de la senal. Como la senal util llega a
   2,4 kHz, la banda eliminada tiene que empezar en 12000-2400 = 9,6 kHz...
   pero en realidad basta con empezarla en 6 kHz, que es mas exigente y
   ademas quita el piloto de 19 kHz (que cae a -38 kHz despues de mezclar) y
   el audio (que cae a -57).

2. FILTRO ADAPTADO (coseno alzado). El RDS transmite con un conformado
   concreto; el filtro que mejor se lleva con el es un coseno alzado de 2,4
   kHz. Va ya en banda base a 12 kHz.
"""
import sys
import numpy as np
from scipy import signal

FS_MPX = 192000.0
DEC = 16
FS_BB = FS_MPX / DEC          # 12000
BAUD = 1187.5


def emite(f, nombre, h, comentario):
    f.write("/* %s */\n" % comentario)
    f.write("#define %s_TAPS %d\n" % (nombre.upper(), len(h)))
    f.write("static const float %s[%s_TAPS] = {\n" % (nombre, nombre.upper()))
    for i in range(0, len(h), 4):
        f.write("    " + ", ".join("%+.9ef" % v for v in h[i:i + 4]) + ",\n")
    f.write("};\n\n")


def main(destino):
    # --- 1. diezmado 192k -> 12k ---
    taps_dec = 64
    h_dec = signal.firwin(taps_dec, 2600.0, window=('kaiser', 6.0), fs=FS_MPX)
    # ganancia 1 en continua
    h_dec = h_dec / np.sum(h_dec)

    # --- 2. coseno alzado a 12 kHz ---
    # El conformado del RDS en el transmisor es un coseno alzado de 100% de
    # exceso de banda con corte a 2,4 kHz. En recepcion se usa el mismo como
    # filtro adaptado.
    n = 33
    t = (np.arange(n) - (n - 1) / 2.0) / FS_BB
    # respuesta impulsional de un coseno alzado con beta=1, T = 1/1187.5
    T = 1.0 / BAUD
    beta = 1.0
    x = t / T
    num = np.sinc(x) * np.cos(np.pi * beta * x)
    den = 1.0 - (2.0 * beta * x) ** 2
    h_rc = np.where(np.abs(den) < 1e-9, np.pi / 4.0 * np.sinc(1.0 / (2.0 * beta)), num / den)
    h_rc = h_rc / np.sum(np.abs(h_rc))

    with open(destino, 'w') as f:
        f.write("/* GENERADO por deepsdr-ui/tools/gen_rds_fir.py - no editar a mano.\n"
                " * Ver ese fichero para el diseno del que salen estos numeros. */\n"
                "#ifndef RDS_FIR_H\n#define RDS_FIR_H\n\n")
        emite(f, "k_rds_dec",
              h_dec,
              "Diezmado 192 kHz -> 12 kHz (16), corte 2,6 kHz, Kaiser beta 6")
        emite(f, "k_rds_rc",
              h_rc,
              "Coseno alzado (beta=1) a 12 kHz, filtro adaptado del RDS")
        f.write("#endif /* RDS_FIR_H */\n")

    # --- informe ---
    w, H = signal.freqz(h_dec, worN=8192, fs=FS_MPX)
    def db_en(fq):
        return 20 * np.log10(abs(H[np.argmin(abs(w - fq))]) + 1e-12)
    print("  diezmado: %d coeficientes" % taps_dec)
    print("    en  0 Hz  %6.2f dB   (la senal util)" % db_en(0))
    print("    en 2,4 k  %6.2f dB   (borde de la senal)" % db_en(2400))
    print("    en 6,0 k  %6.2f dB   (primer alias al diezmar por 16)" % db_en(6000))
    print("    en 9,6 k  %6.2f dB" % db_en(9600))
    print("    en 38  k  %6.2f dB   (el piloto de 19 kHz, ya mezclado)" % db_en(38000))
    print("    en 57  k  %6.2f dB   (el audio, ya mezclado)" % db_en(57000))
    # Lo que de verdad importa: lo que se PLIEGA encima de la senal util.
    # Al diezmar por 16 (salida a 12 kHz), lo que cae sobre +/-2,4 kHz viene
    # de 12000+/-2400, 24000+/-2400, etc. El 6 kHz de arriba es el borde de
    # Nyquist de la salida, no una frecuencia que se pliegue encima.
    peor, dondef = -999.0, 0.0
    for k in range(1, 9):
        for f in np.arange(k * 12000 - 2400, k * 12000 + 2400, 50):
            if f < FS_MPX / 2 and db_en(f) > peor:
                peor, dondef = db_en(f), f
    print("    peor de lo que SE PLIEGA sobre la senal: %.2f dB (en %.0f Hz)"
          % (peor, dondef))
    print("  coseno alzado: %d coeficientes" % n)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '../deepsdr/User/rds_fir.h')
