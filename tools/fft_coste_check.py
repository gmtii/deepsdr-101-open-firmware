#!/usr/bin/env python3
"""
LO QUE COSTARIA UNA FFT DE 512 PUNTOS, CONTADO EN INSTRUCCIONES DEL BRAZO.

POR QUE EXISTE (24/10/2026)
---------------------------
*** Avisado por el usuario: "creo que en 192 estoy viendo señales que no
terminan en pico". Y despues, cuando le dije lo que costaria arreglarlo:
"se podrian encontrar esos 8kb que hacen falta?" y "empieza a medir". ***

Y tenia razon en lo que veia. La FFT del panadaptador son 256 bins SIEMPRE,
asi que:

      96 kHz / 256 = 375 Hz por bin
     192 kHz / 256 = 750 Hz por bin     <- el doble de gordo

La traza son 756 pixeles para 256 bins, o sea que cada bin se pinta en ~3
columnas con la misma altura. Una emisora de fonia ocupa unos 2,5 kHz:

      a  96 kHz  2.500/375 = 6,7 bins = ~20 px  -> se le ve la forma
      a 192 kHz  2.500/750 = 3,3 bins = ~10 px  -> tres escalones y meseta

No es una averia: es que a 750 Hz por bin no quedan puntos para dibujarla.
Lo que lo arreglaria es una FFT de 512 a 192 kHz, que devuelve los 375 Hz.

LA PREGUNTA QUE ESTO CONTESTA, Y LA QUE NO
------------------------------------------
La RAM ya esta contada en docs/CAMBIOS.md. Lo que faltaba es lo otro, que es
lo que de verdad decide: CUANTO TIEMPO. A 192 kHz la radio ya va al 59 % de
carga de audio y con 45 ms de vuelta de bucle contra los 22 de objetivo.
Doblar el coste de la transformada justo ahi es la receta para que la
cascada se vuelva a arrastrar, que es una queja que ya hubo.

COMO SE MIDE, Y POR QUE ASI
---------------------------
Igual que tools/espectro_coste_check.py, y por el mismo motivo: el PC tiene
cache y el microcontrolador no, asi que cronometrar en el PC no dice
milisegundos de radio. Lo que no depende de la maquina es el codigo que sale
del compilador.

Esto compila User/fft.c PARA LA RADIO -las mismas opciones del firmware- una
vez con 256 y otra con 512, busca en fft_run() el bucle mas interno (la
mariposa) y cuenta sus instrucciones. Las vueltas no se estiman: salen de la
propia estructura del algoritmo y se cuentan exactas.

    etapa 0      N/2 mariposas sin giro (va en su propio bucle)
    etapas 1..   (N/2) * (log2(N) - 1) mariposas con giro
    giros        N - 2 veces (una por columna, no una por mariposa)

Y de ahi el total, en instrucciones. Para pasarlo a microsegundos se usa el
reloj de la radio (200 MHz) y UNA instruccion por ciclo, que para este bucle
-coma flotante de precision simple en el FPU del M4, sin divisiones- es una
aproximacion razonable y OPTIMISTA: lo que salga es un suelo, no un techo.

EL NUMERO DE VERDAD LO TIENE LA RADIO. La fila "Fotograma" de Informacion
enseña FFT + marco + traza + cascada en milisegundos, medidos con el
contador de ciclos. Si algun dia se compara con lo de aqui y no cuadra, el
que manda es el de la radio y esta cuenta esta mal.

LO QUE ESTE BANCO EXIGE
-----------------------
Que la mariposa no engorde. El tope esta puesto con holgura sobre lo medido
-23 instrucciones, y lo dice el propio comentario de fft.c- para avisar de
que alguien ha metido trabajo dentro del bucle mas repetido de la radio, no
de que otra version del compilador ordene dos instrucciones distinto.

El informe de 512 no se exige: se imprime. Es una decision de proyecto, no
una regla.
"""
import os
import re
import subprocess
import sys
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

RELOJ_HZ = 200000000        # ver system_gd32f4xx.c
TOPE_MARIPOSA = 28          # medido 23; ver la cabecera

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


def solo_funcion(texto, nombre):
    """El trozo del volcado que pertenece a una funcion."""
    dentro = False
    filas = []
    for linea in texto.split("\n"):
        if re.match(r"^[0-9a-f]+ <%s>:" % re.escape(nombre), linea):
            dentro = True
            continue
        if dentro and re.match(r"^[0-9a-f]+ <", linea):
            break
        if dentro:
            filas.append(linea)
    return "\n".join(filas)


def elige(bs, con_mul):
    """De los bucles encontrados, el MAS CORTO que lleve (o no lleve)
    multiplicacion en coma flotante.

    Y hace falta elegir asi, no por el mas corto a secas: fft_run() tiene
    TRES bucles anidados, y el mas corto de todos es el de la etapa 0 -la
    que no lleva giro-, no la mariposa. Cogiendo el minimo sin mirar salian
    18 instrucciones en vez de 23, un 22 % de menos, y el comentario de
    fft.c dice 23 con todas sus letras. Un banco que se equivoca de bucle
    da un numero perfectamente formado que no significa nada."""
    cand = []
    for b in bs:
        hay = any(t.startswith("vmul") or t.startswith("vfma") or
                  t.startswith("vfms") or t.startswith("vmla")
                  for t in b)
        if hay == con_mul:
            cand.append(b)
    return min(cand, key=len) if cand else None


def compila(gcc, objdump, n, tmp):
    obj = os.path.join(tmp, "fft%d.o" % n)
    orden = [gcc] + OPCIONES + ["-DFFT_SIZE_BANCO=%dU" % n,
                                "-c", os.path.join(RAIZ, "User", "fft.c"),
                                "-o", obj]
    r = subprocess.run(orden, cwd=RAIZ, capture_output=True, text=True)
    if r.returncode != 0:
        print("  no compila con %d puntos:" % n)
        print(r.stderr[:800])
        return None
    d = subprocess.run([objdump, "-d", obj], capture_output=True, text=True)
    return d.stdout


def mariposas(n):
    """Las mariposas que hace una transformada de n puntos, exactas."""
    etapas = n.bit_length() - 1          # log2(n)
    primera = n // 2                     # la etapa 0, sin giro
    resto = (n // 2) * (etapas - 1)      # las demas
    return primera, resto, n - 2         # y los giros, uno por columna


def main():
    gcc = herramienta("gcc")
    objdump = herramienta("objdump")
    if gcc is None or objdump is None:
        print("  no encuentro el compilador del brazo   MAL")
        return 1

    print("LA FFT, CONTADA EN INSTRUCCIONES DEL BRAZO")

    med = {}
    with tempfile.TemporaryDirectory() as tmp:
        for n in (256, 512):
            dump = compila(gcc, objdump, n, tmp)
            if dump is None:
                return 1

            run = bucles(instrucciones(solo_funcion(dump, "fft_run")))
            iq = bucles(instrucciones(
                solo_funcion(dump, "fft_compute_db_iq_ex")))
            if not run or not iq:
                print("  no encuentro los bucles   MAL")
                return 1

            mar = elige(run, True)      # la mariposa: lleva el giro
            et0 = elige(run, False)     # la etapa 0: sin giro
            # En fft_compute_db_iq_ex() hay dos pasadas de N: la de cargar
            # con ventana de Hann y la de pasar a dB. La de dB lleva la
            # aproximacion de log2, que multiplica; la de carga tambien
            # -la ventana-, asi que se cogen los dos mas cortos distintos.
            cortos = sorted((len(b) for b in iq))
            if mar is None or et0 is None or len(cortos) < 2:
                print("  no encuentro los bucles   MAL")
                return 1
            med[n] = {"mar": len(mar), "et0": len(et0),
                      "pas": cortos[0] + cortos[1]}

    im = med[256]["mar"]
    print("  la mariposa son %d instrucciones, la etapa 0 son %d"
          % (im, med[256]["et0"]))
    di("la mariposa no ha engordado", im <= TOPE_MARIPOSA,
       "%d de un tope de %d" % (im, TOPE_MARIPOSA))

    print()
    print("  lo que cuesta una transformada ENTERA, por tamaño:")
    print("    puntos  Hz/bin a 192k  mariposas  instrucciones  us a 200 MHz")
    coste = {}
    for n in (256, 512):
        p, r, g = mariposas(n)
        total = (r * med[n]["mar"] + p * med[n]["et0"] + g * 4
                 + n * med[n]["pas"])
        us = total * 1000000.0 / RELOJ_HZ
        coste[n] = us
        print("    %4d    %6.1f         %6d       %8d      %7.1f"
              % (n, 192000.0 / n, p + r, total, us))

    print()
    print("  y en el presupuesto de la radio:")
    print("    hasta 6 transformadas por cuadro de 33 ms")
    for n in (256, 512):
        por_cuadro = coste[n] * 6.0
        print("      %3d puntos   %6.2f ms de los 33   (%4.1f %% del cuadro)"
              % (n, por_cuadro / 1000.0, por_cuadro / 1000.0 / 33.0 * 100.0))
    print("    el salto: +%.2f ms por cuadro, o sea %.2f veces"
          % ((coste[512] - coste[256]) * 6.0 / 1000.0, coste[512] / coste[256]))

    print()
    print("  UNA INSTRUCCION POR CICLO, que para este bucle -coma flotante")
    print("  de precision simple, sin divisiones- es optimista pero no")
    print("  disparatado. Lo que esto NO modela es la memoria: a 512 los")
    print("  arrays son el doble y la radio no tiene cache. Asi que es un")
    print("  SUELO. El numero de verdad lo da la fila \"Fotograma\" de")
    print("  Informacion, medida con el contador de ciclos de la radio.")

    print()
    print("  %s" % ("TODO CORRECTO" if not fallos else "*** HAY FALLOS ***"))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
