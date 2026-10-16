#!/usr/bin/env python3
"""
De una grabacion de audio de HFDL a la pareja I/Q que espera el firmware.
14/10/2026.

La radio le da a HFDL la I/Q a 12 kHz justo despues del diezmado, con la
subportadora de HFDL en +1440 Hz (ver demod_am.c, "HFDL SE ENGANCHA
AQUI"). Una grabacion de audio en USB es REAL, asi que la pareja
equivalente es su señal analitica: I = la grabacion, Q = su Hilbert.

Esa conversion se hace aqui y no dentro del banco a proposito: el banco
sim/hfdl_aire.c tiene que ser lo mas tonto posible -leer y meter- para que
lo que mida sea el firmware y no la preparacion de la señal.

    python3 tools/aire_iq.py grabacion.mp3 [mas.mp3 ...]

Deja los .iq.wav en deepsdr-ui/sim/aire/, que es donde los busca el banco.
Hace falta ffmpeg, numpy y scipy.
"""
import os, subprocess, sys, tempfile
import numpy as np
import scipy.io.wavfile as wav
from scipy.signal import hilbert

DESTINO = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       '..', 'deepsdr-ui', 'sim', 'aire')


def convierte(ruta):
    nombre = os.path.splitext(os.path.basename(ruta))[0]
    with tempfile.TemporaryDirectory() as tmp:
        wv = os.path.join(tmp, 'm.wav')
        subprocess.run(['ffmpeg', '-y', '-i', ruta, '-ac', '1', '-ar', '12000',
                        '-f', 'wav', wv], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        fs, x = wav.read(wv)
    x = x.astype(np.float64) / 32768.0
    if x.ndim > 1:
        x = x.mean(axis=1)
    z = hilbert(x)
    z = z / np.max(np.abs(z)) * 0.7      # sitio de sobra, sin recortar
    iq = np.empty((len(z), 2), dtype=np.int16)
    iq[:, 0] = (z.real * 32767).astype(np.int16)
    iq[:, 1] = (z.imag * 32767).astype(np.int16)
    os.makedirs(DESTINO, exist_ok=True)
    salida = os.path.join(DESTINO, nombre + '.iq.wav')
    wav.write(salida, 12000, iq)

    # Y de paso el dato que mas vale de una grabacion: donde cae el
    # pre-key. La norma dice 1440 Hz; lo que mida de mas o de menos es el
    # desvio con el que el decodificador va a tener que pelearse.
    tono = mide_prekey(x, fs)
    print('%-28s %6d muestras   pre-key %s' %
          (os.path.basename(salida), len(z),
           ('%.1f Hz (%+.1f)' % (tono, tono - 1440.0)) if tono else 'no visto'))


def mide_prekey(x, fs):
    """Cada ranura empieza con 249 ms de tono puro a 1440 Hz (ARINC 635-3).
    Se busca el primer trozo con energia y se mide su pico espectral."""
    n = int(0.02 * fs)
    e = np.array([np.sum(x[i:i + n] ** 2) for i in range(0, len(x) - n, n)])
    if len(e) == 0 or e.max() <= 0:
        return None
    ini = next((i for i, v in enumerate(e) if v > 0.35 * e.max()), None)
    if ini is None:
        return None
    seg = x[ini * n: ini * n + int(0.06 * fs)]
    if len(seg) < 256:
        return None
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 16))
    f = np.fft.rfftfreq(1 << 16, 1.0 / fs)
    m = (f > 200) & (f < 3500)
    k = int(np.argmax(X[m]) + np.where(m)[0][0])
    a, b, c = X[k - 1], X[k], X[k + 1]
    d = 0.5 * (a - c) / (a - 2 * b + c + 1e-20)
    return float((k + d) * fs / (1 << 16))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    for r in sys.argv[1:]:
        convierte(r)
