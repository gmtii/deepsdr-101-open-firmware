#!/usr/bin/env python3
"""
LO QUE CUESTA PINTAR UN PIXEL DE ESPECTRO, CONTADO EN INSTRUCCIONES.

POR QUE EXISTE (20/10/2026). La fila "Fotograma" de la radio decia 22 ms de
traza con el bus de la pantalla parado, o sea que el tiempo no se va en
empujar pixeles al panel sino en decidirlos. El banco del simulador
(deepsdr-ui/sim/espectro_coste.c) mide el REPARTO -el 92 % del cuadro va en
el termino de pixel, no en el de columna- pero no puede decir cuantos
milisegundos son en la radio: el PC tiene cache y el microcontrolador no, y
el relleno escribe en vertical, que es justo lo que una cache castiga de
otra manera.

Lo que no depende de la maquina es el codigo que sale del compilador. Esto
compila User/spectrum.c PARA LA RADIO, con las opciones del firmware, busca
los bucles que montan la banda pixel a pixel dentro de compon() y cuenta sus
instrucciones.

QUE SE MIDE. Hay tres bucles de la misma forma -el relleno de la barra con
degradado, la traza con degradado y la traza de un solo color- y por ellos
pasa un pixel cada uno. En un cuadro de 756 x 208 son hasta 157.248 vueltas,
que es el bucle mas repetido de toda la radio.

    20/10/2026, antes:  7 instrucciones por pixel (indice de 16 bits, con
                        su `uxth` obligatorio y su `cmp` contra el final)
    20/10/2026, ahora:  5 (cuenta atras: el `subs` ya deja la bandera)

El tope esta puesto con holgura sobre lo medido, no al ras: avisa de que
alguien ha vuelto a meter trabajo por pixel, no de que otra version del
compilador ordene dos instrucciones de otra manera.

SI ESTO NO ENCUENTRA LOS BUCLES se pone rojo en vez de pasar callando: un
banco que no sabe lo que esta mirando no vale nada.
"""
import os
import re
import subprocess
import tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

OPCIONES = [
    "-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard",
    "-DGD32F450", "-DUSE_STDPERIPH_DRIVER", "-DHXTAL_VALUE=12288000U",
    "-DARM_MATH_DSP=1", "-DARM_MATH_CM4",
    "-ICMSIS/Include", "-ICMSIS/GD/GD32F4xx/Include", "-IFirmware/Include",
    "-IUser", "-ICMSIS/DSP/Include", "-ICMSIS/DSP/PrivateInclude",
    "-O2", "-ffunction-sections", "-fdata-sections", "-fno-common",
]

TOPE_PIXEL = 7       # medido 5
BUCLES_MIN = 3       # relleno, traza con degradado y traza de un color

fallos = 0


def di(que, ok, det=""):
    global fallos
    print("    %-52s %s%s%s" % (que, "ok" if ok else "*** MAL ***",
                                "  " if det else "", det))
    if not ok:
        fallos += 1


def herramienta(nombre):
    for pre in ("arm-none-eabi-", ""):
        try:
            subprocess.run([pre + nombre, "--version"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           check=True)
            return pre + nombre
        except (OSError, subprocess.CalledProcessError):
            pass
    return None


def instrucciones(texto):
    salida = []
    for linea in texto.split("\n"):
        m = re.match(r"\s*([0-9a-f]+):\s+[0-9a-f ]+\t(.*)", linea)
        if m:
            salida.append((int(m.group(1), 16), m.group(2).strip()))
    return salida


def bucles(ins):
    """Trozos delimitados por un salto hacia atras."""
    salida = []
    for direc, texto in ins:
        m = re.match(r"b[a-z.]*\s+([0-9a-f]+)", texto)
        if m:
            destino = int(m.group(1), 16)
            if destino < direc:
                salida.append([t for d, t in ins if destino <= d <= direc])
    return salida


def main():
    print("\nLO QUE CUESTA UN PIXEL DE ESPECTRO EN LA RADIO\n")

    gcc = herramienta("gcc")
    objdump = herramienta("objdump")
    if not gcc or not objdump or not gcc.startswith("arm-"):
        print("    sin compilador de ARM: no se puede medir, no se inventa\n")
        return 0

    with tempfile.TemporaryDirectory() as tmp:
        obj = os.path.join(tmp, "spectrum.o")
        r = subprocess.run([gcc, "-c"] + OPCIONES +
                           [os.path.join("User", "spectrum.c"), "-o", obj],
                           cwd=RAIZ, capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stderr[-2000:])
            di("spectrum.c compila para la radio", False)
            return 1
        d = subprocess.run([objdump, "-d", obj], capture_output=True, text=True)
        texto = d.stdout

    trozo = re.search(r"<compon>:\n(.*?)(?=\n\n[0-9a-f]+ <|\Z)", texto, re.S)
    if not trozo:
        di("se encuentra compon() en el codigo compilado", False)
        return 1
    ins = instrucciones(trozo.group(1))

    # Los bucles de pixel son los que escriben una media palabra y avanzan
    # el puntero de fila. Se reconocen por el `strh` y por ser cortos.
    cand = []
    for b in bucles(ins):
        if len(b) <= 12 and any(t.startswith("strh") for t in b):
            cand.append(b)
    if len(cand) < BUCLES_MIN:
        di("se encuentran los bucles que montan la banda", False,
           "encontrados %d, esperados %d" % (len(cand), BUCLES_MIN))
        return 1

    peor = max(len(b) for b in cand)
    for b in sorted(cand, key=len):
        print("      bucle de %d instrucciones:  %s" % (len(b), " ; ".join(b[:3])))
    di("el bucle mas caro de los que montan la banda", peor <= TOPE_PIXEL,
       "%d instrucciones por pixel (tope %d)" % (peor, TOPE_PIXEL))

    print("")
    return 1 if fallos else 0


if __name__ == "__main__":
    raise SystemExit(main())
