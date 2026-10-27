#!/usr/bin/env python3
"""
LO QUE CUESTA EL DECODIFICADOR DE RDS EN LA RADIO, EN INSTRUCCIONES.

POR QUE EXISTE (26/10/2026). deepsdr-ui/sim/rds_coste.c cuenta SUCESOS por
bloque de audio -cuantas veces se llama a cada trozo del decodificador- y
los convierte en ciclos del Cortex-M4 multiplicandolos por un coste por
suceso. Esos costes estaban escritos a mano en cinco #define, y el
comentario del banco prometia que "se vuelven a sacar del binario en cada
make comprueba y se pone rojo si se mueven". No habia nada que lo hiciera:
la promesa era falsa y los cinco numeros podian envejecer sin que nadie se
enterara. Esto la cumple para los que se pueden sacar del binario, y dice
cuales no, en vez de fingir que los comprueba todos.

QUE SE MIDE Y QUE NO. Lo que no depende de la maquina que compile es el
codigo que sale del compilador (ver tools/isr_coste_check.py, que ya explica
por que el reloj de un PC no sirve para esto). Asi que se compila User/rds.c
y User/nco.c PARA LA RADIO, con las opciones de reglas.mk, y se cuentan
instrucciones sobre el desensamblado:

    I_FIR        SI. Es el bucle del producto escalar, y es exacto: las
                 instrucciones del cuerpo por K_RDS_DEC_TAPS tomas.
    I_RESTO      SI. El bucle de division de rds_resto() por sus 16 vueltas,
                 contando el cuerpo ENTERO -o sea suponiendo que en las 16
                 toca restar-. Un presupuesto se hace con el peor caso, no
                 con la media: la media depende de los datos y el peor caso
                 no.
    I_CORRIGE    SI, derivado. Una busqueda entera que falla son, con
                 RDS_RAFAGA_MAX bits, las posiciones de cada patron impar
                 mas el resto inicial del bloque recibido, todas por
                 I_RESTO. La cuenta se hace aqui a partir de la constante
                 que hay en rds.c, no a mano.
    I_MUESTRA    NO se puede separar: -O2 funde banda_base(), simbolo() y
                 mete_bit() dentro de rds_feed(). Lo que SI se vigila es que
                 rds_feed() entera no engorde, que es lo que de verdad
                 muerde en la interrupcion.
    I_BANDABASE  NO, por lo mismo. Mismo centinela.

LOS TOPES DE CRECIMIENTO estan puestos con holgura sobre lo medido, no al
ras, para que avisen de un disparate -alguien que mete un FIR cinco veces
mas caro dentro de la interrupcion, que es el fallo contra el que se
escribio isr_coste_check.py- y no de que otra version del compilador ordene
tres instrucciones de otra manera.

SI ESTO NO ENCUENTRA LO QUE BUSCA SE PONE ROJO en vez de pasar callando: un
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

BANCO = os.path.join("deepsdr-ui", "sim", "rds_coste.c")

VUELTAS_RESTO = 16      # el bucle de rds_resto(): bits 25..10

TOPE_POR_TOMA   = 12    # medido 7:  3 cargas, 2 multiplica-acumula, fin de bucle
TOPE_POR_VUELTA = 14    # medido 9:  prueba de bit, desplazamiento, xor, fin
TOPE_NCO        = 45    # medido 27: nco_paso() entera
TOPE_FEED      = 1400   # medido 873: rds_feed() entera, con todo lo fundido

fallos = 0


def di(que, ok, det=""):
    global fallos
    print("    %-44s %s%s%s" % (que, "ok" if ok else "*** MAL ***",
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


def numero(ruta, patron):
    """Un numero escrito en un fichero de C, para no repetirlo aqui."""
    with open(os.path.join(RAIZ, ruta), encoding="utf-8", errors="replace") as f:
        m = re.search(patron, f.read())
    return float(m.group(1)) if m else None


def instrucciones(texto):
    """Lista de (direccion, mnemonico completo) del desensamblado."""
    salida = []
    for linea in texto.split("\n"):
        m = re.match(r"\s*([0-9a-f]+):\s+[0-9a-f ]+\t(.*)", linea)
        if m:
            salida.append((int(m.group(1), 16), m.group(2).strip()))
    return salida


def cuerpo(texto, nombre):
    """Las instrucciones de una funcion del desensamblado."""
    m = re.search(r"<%s>:\n(.*?)(?=\n\n[0-9a-f]+ <|\Z)" % re.escape(nombre),
                  texto, re.S)
    return instrucciones(m.group(1)) if m else None


def bucles(ins):
    """Trozos delimitados por un salto hacia atras, de mas corto a mas largo."""
    salida = []
    for direc, texto in ins:
        m = re.match(r"b[a-z.]*\s+([0-9a-f]+)", texto)
        if m:
            destino = int(m.group(1), 16)
            if destino < direc:
                salida.append([t for d, t in ins if destino <= d <= direc])
    return sorted(salida, key=len)


def el_mas_corto(ins, filtro):
    cand = [b for b in bucles(ins) if filtro(b)]
    return min(cand, key=len) if cand else None


def igual(nombre, medido, escrito, holgura=0.10):
    """El numero del banco tiene que ser el que sale del binario."""
    if escrito is None:
        di(nombre, False, "no esta el #define en el banco")
        return
    if medido is None:
        di(nombre, False, "no se encuentra en el desensamblado")
        return
    ok = abs(medido - escrito) <= holgura * max(medido, escrito)
    di(nombre, ok, "binario %.0f, banco %.0f" % (medido, escrito))


def tope(nombre, medido, maximo):
    if medido is None:
        di(nombre, False, "no se encuentra en el desensamblado")
        return
    di(nombre, medido <= maximo, "%d, el tope %d" % (medido, maximo))


def divisiones(max_raf):
    """Las llamadas a rds_resto() de una busqueda ENTERA que falla, contadas
    como las cuenta rds_corrige(): patrones impares, anchura del patron,
    26-anchura+1 posiciones. Mas el resto del bloque recibido."""
    n = 1
    m = 1
    while m < (1 << int(max_raf)):
        anchura = m.bit_length()
        n += 26 - anchura + 1
        m += 2
    return float(n)


def main():
    print("\nLO QUE CUESTA EL RDS EN LA RADIO, CONTADO EN INSTRUCCIONES\n")

    gcc = herramienta("gcc")
    objdump = herramienta("objdump")
    if not gcc or not objdump or not gcc.startswith("arm-"):
        print("    sin compilador de ARM: no se puede medir, no se inventa\n")
        return 0

    tomas = numero(os.path.join("User", "rds_fir.h"),
                   r"#define\s+K_RDS_DEC_TAPS\s+(\d+)")
    rafaga = numero(os.path.join("User", "rds.c"),
                    r"#define\s+RDS_RAFAGA_MAX\s+(\d+)")
    if tomas is None or rafaga is None:
        di("se leen K_RDS_DEC_TAPS y RDS_RAFAGA_MAX", False)
        return 1

    texto = ""
    with tempfile.TemporaryDirectory() as tmp:
        for fuente in ("rds.c", "nco.c"):
            obj = os.path.join(tmp, fuente.replace(".c", ".o"))
            r = subprocess.run([gcc, "-c"] + OPCIONES +
                               [os.path.join("User", fuente), "-o", obj],
                               cwd=RAIZ, capture_output=True, text=True)
            if r.returncode != 0:
                print(r.stderr[-2000:])
                di("%s compila para la radio" % fuente, False)
                return 1
            texto += subprocess.run([objdump, "-d", obj],
                                    capture_output=True, text=True).stdout + "\n"

    ins_feed = cuerpo(texto, "rds_feed")
    ins_resto = cuerpo(texto, "rds_resto")
    ins_nco = cuerpo(texto, "nco_paso")

    # El FIR: el bucle mas corto que multiplica y acumula en coma flotante.
    fir = None
    if ins_feed:
        fir = el_mas_corto(ins_feed,
                           lambda b: any(re.match(r"v(fma|mla|mul)", t)
                                         for t in b))
    por_toma = len(fir) if fir else None

    # La division: el unico bucle de rds_resto().
    por_vuelta = None
    if ins_resto:
        b = bucles(ins_resto)
        if b:
            por_vuelta = len(b[0])

    n_div = divisiones(rafaga)
    print("    K_RDS_DEC_TAPS %d, RDS_RAFAGA_MAX %d -> %.0f divisiones por"
          " busqueda fallida\n" % (tomas, rafaga, n_div))

    print("  1. los que el banco dice y el binario confirma\n")
    igual("I_FIR       producto escalar entero",
          por_toma * tomas if por_toma else None,
          numero(BANCO, r"#define\s+I_FIR\s+([0-9.]+)"))
    i_resto_bin = por_vuelta * VUELTAS_RESTO if por_vuelta else None
    igual("I_RESTO     division polinomica", i_resto_bin,
          numero(BANCO, r"#define\s+I_RESTO\s+([0-9.]+)"))
    igual("I_CORRIGE   busqueda entera que falla",
          i_resto_bin * n_div if i_resto_bin else None,
          numero(BANCO, r"#define\s+I_CORRIGE\s+([0-9.]+)"))

    print("\n  2. los que -O2 funde y no se pueden separar: que no engorden\n")
    tope("rds_feed() entera, con todo dentro",
         len(ins_feed) if ins_feed else None, TOPE_FEED)
    tope("nco_paso() entera, una por muestra",
         len(ins_nco) if ins_nco else None, TOPE_NCO)
    tope("el producto escalar, por toma", por_toma, TOPE_POR_TOMA)
    tope("la division, por vuelta", por_vuelta, TOPE_POR_VUELTA)

    print("\n%s\n" % ("TODO CORRECTO (0 fallos)" if fallos == 0
                      else "*** %d MAL ***" % fallos))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
