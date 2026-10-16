#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
De un mp3/wav de ALS162 a la FASE que come dcf77_feed().

Saca lo que la radio le da al decodificador en esta emisora: la fase de la
portadora, desenrollada, sin rampa, a 1.500 muestras/s (cuatro por bloque;
ver el comentario del desenrollado en User/dcf77.c).

    python3 tools/als_fase.py TDF.mp3 sim/muestras/als162_tdf_fase1500.f32

La portadora se mide por interpolacion parabolica sobre la FFT en vez de
cogerse del bin mas alto: con 20 s de grabacion un bin son 0,05 Hz y el
error de frecuencia se convierte en una rampa de fase que luego hay que
quitar a ojo. Asi se quita sola.
"""
import sys, subprocess, tempfile, os
import numpy as np
import scipy.io.wavfile as wav
import scipy.signal as sg

FS_SALIDA = 1500.0


def carga(ruta, fs_trabajo=8000):
    """Devuelve (fs, x) en mono float64. Pasa por ffmpeg si hace falta."""
    if ruta.lower().endswith('.wav'):
        fs, x = wav.read(ruta)
        return fs, x.astype(np.float64)
    tmp = tempfile.mktemp(suffix='.wav')
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y',
                    '-i', ruta, '-ac', '1', '-ar', str(fs_trabajo),
                    '-c:a', 'pcm_f32le', tmp], check=True)
    fs, x = wav.read(tmp)
    os.unlink(tmp)
    return fs, x.astype(np.float64)


def fase(ruta):
    fs, x = carga(ruta)
    n = len(x)
    t = np.arange(n) / fs

    X = np.abs(np.fft.rfft(x * np.hanning(n)))
    f = np.fft.rfftfreq(n, 1 / fs)
    i = int(np.argmax(X))
    a, b, c = X[i - 1], X[i], X[i + 1]
    fc = f[i] + 0.5 * (a - c) / (a - 2 * b + c) * (f[1] - f[0])

    z = x * np.exp(-2j * np.pi * fc * t)
    bb, aa = sg.butter(4, 40 / (fs / 2))
    z = sg.filtfilt(bb, aa, z)

    amp = np.abs(z)
    ph = np.unwrap(np.angle(z))
    p = np.polyfit(t, ph, 1)
    ph = ph - np.polyval(p, t)

    m = int(round(n * FS_SALIDA / fs))
    ph15 = np.interp(np.arange(m) / FS_SALIDA, t, ph)
    return fc, p[0] / (2 * np.pi), amp, ph15


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        raise SystemExit(2)
    fc, deriva, amp, ph15 = fase(sys.argv[1])
    ph15.astype(np.float32).tofile(sys.argv[2])
    print('portadora        %.3f Hz' % fc)
    print('deriva residual  %.4f Hz' % deriva)
    print('amplitud         media %.4f, variacion %.1f%%  '
          '(en una portadora pura seria ~0: lo que sube de ahi es el codec)'
          % (amp.mean(), 100 * amp.std() / amp.mean()))
    print('escritas         %d muestras a %.0f/s (%.2f s) en %s'
          % (len(ph15), FS_SALIDA, len(ph15) / FS_SALIDA, sys.argv[2]))
