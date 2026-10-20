#!/usr/bin/env python3
"""
LO QUE CUESTA EN LA RADIO EL CORRECTOR DE I/Q, CONTADO EN INSTRUCCIONES.

POR QUE EXISTE (19/10/2026). Ya habia un banco para esto, deepsdr-ui/sim/
iq_coste.c, y aun asi se colo una version del FIR del espejo que costaba
cinco veces el corrector plano dentro de la interrupcion del ADC. Dos
agujeros a la vez:

  1. Aquel banco no encendia nunca el FIR, asi que media la rama barata.
     Eso ya esta arreglado alli.
  2. Aquel banco mide con el reloj de un PC. El PC no tiene SSAT y convierte
     float a entero por un camino caro, asi que su numero no dice lo que
     cuesta en el Cortex-M4; de hecho dice lo contrario cuando se cambia la
     saturacion a mano por la instruccion del nucleo.

Lo que no depende de la maquina es el codigo que sale del compilador. Esto
compila User/iqbal.c PARA LA RADIO, con las mismas opciones con las que se
monta el firmware, busca los dos bucles que recorren muestra a muestra y
cuenta sus instrucciones.

QUE SE MIDE Y POR QUE ESOS TOPES. Por esta interrupcion pasan TODAS las
muestras de TODOS los modos, 96.000 parejas por segundo. Medido el 19/10:

    corrector plano solo        15 instrucciones por muestra
    plano + FIR, fundidos       43 instrucciones por muestra

La version que se entrego en V5.11 hacia dos pasadas y corria el historial
en memoria: unas 145 entre las dos. Los topes de aqui estan puestos con
holgura sobre lo medido, no al ras, para que avisen de un disparate y no de
que otra version del compilador ordene tres instrucciones de otra manera.

NO SE COMPARA UNO CON OTRO. Se probo y se quito: el FIR hace dieciseis
multiplicaciones-acumulacion por muestra y el plano una, asi que cualquier
proporcion entre los dos es un numero inventado que se pone rojo sin que
nada vaya mal. Lo que significa algo es cuanto cuesta cada uno.

SI ESTO NO ENCUENTRA LOS BUCLES se pone rojo en vez de pasar callando: un
banco que no sabe lo que esta mirando no vale nada, y aqui eso significa que
el compilador ha reorganizado el codigo y hay que volver a mirarlo a mano.
"""
import os
import re
import subprocess
import sys
import tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Los mismos que reglas.mk, en lo que afecta al codigo que sale.
OPCIONES = [
    "-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard",
    "-DGD32F450", "-DUSE_STDPERIPH_DRIVER", "-DHXTAL_VALUE=12288000U",
    "-DARM_MATH_DSP=1", "-DARM_MATH_CM4",
    "-ICMSIS/Include", "-ICMSIS/GD/GD32F4xx/Include", "-IFirmware/Include",
    "-IUser", "-ICMSIS/DSP/Include", "-ICMSIS/DSP/PrivateInclude",
    "-O2", "-ffunction-sections", "-fdata-sections", "-fno-common",
]

TOPE_PLANO = 24     # medido 15
TOPE_FIR   = 60     # medido 43

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
    """Lista de (direccion, mnemonico completo) del desensamblado."""
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


def cuenta(ins, filtro):
    cand = [b for b in bucles(ins) if filtro(b)]
    return len(min(cand, key=len)) if cand else None


def main():
    print("\nLO QUE CUESTA EL CORRECTOR DE I/Q EN LA RADIO\n")

    gcc = herramienta("gcc")
    objdump = herramienta("objdump")
    if not gcc or not objdump or not gcc.startswith("arm-"):
        print("    sin compilador de ARM: no se puede medir, no se inventa\n")
        return 0

    with tempfile.TemporaryDirectory() as tmp:
        obj = os.path.join(tmp, "iqbal.o")
        r = subprocess.run([gcc, "-c"] + OPCIONES +
                           [os.path.join("User", "iqbal.c"), "-o", obj],
                           cwd=RAIZ, capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stderr[-2000:])
            di("iqbal.c compila para la radio", False)
            return 1
        d = subprocess.run([objdump, "-d", obj], capture_output=True, text=True)
        texto = d.stdout

    trozo = re.search(r"<iqbal_bloque>:\n(.*?)(?=\n\n[0-9a-f]+ <|\Z)", texto, re.S)
    if not trozo:
        di("se encuentra iqbal_bloque() en el codigo compilado", False)
        return 1
    ins = instrucciones(trozo.group(1))

    def es_fir(b):
        return sum(1 for t in b if t.startswith(("vfma", "vmla", "vfms"))) >= 12

    def es_plano(b):
        n = sum(1 for t in b if t.startswith(("vfma", "vmla", "vfms", "vmul")))
        return 1 <= n <= 4 and any(t.startswith("strh") for t in b)

    n_fir = cuenta(ins, es_fir)
    n_plano = cuenta(ins, es_plano)

    di("se encuentra el bucle del corrector plano", n_plano is not None)
    di("se encuentra el bucle del plano + FIR", n_fir is not None)
    if n_plano is None or n_fir is None:
        print("\n    No estan donde deberian: o el compilador ha dejado de meter")
        print("    fir_aplica() dentro de iqbal_bloque(), o el bucle de muestras")
        print("    ya no es un bucle. En los dos casos hay que mirarlo a mano,")
        print("    porque el numero que daria este banco seria mentira.\n")
        print("    *** HAY FALLOS ***\n")
        return 1

    di("el corrector plano cabe en %d instrucciones por muestra" % TOPE_PLANO,
       n_plano <= TOPE_PLANO, "(%d)" % n_plano)
    di("el plano + FIR cabe en %d instrucciones por muestra" % TOPE_FIR,
       n_fir <= TOPE_FIR, "(%d)" % n_fir)
    print("\n    por aqui pasan 96.000 parejas por segundo, todas las de")
    print("    todos los modos: cada instruccion de mas la paga la radio\n")
    print("  %s\n" % ("*** HAY FALLOS ***" if fallos else "TODO CORRECTO"))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
