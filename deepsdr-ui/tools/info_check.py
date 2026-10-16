#!/usr/bin/env python3
"""
La frontera de la pantalla de Informacion, comprobada contra main.c.

POR QUE EXISTE
--------------
La pagina 1 de Informacion abre al instante por una razon concreta: ni una
de sus filas toca la flash SPI. Y la flash SPI de esta radio va a patadas
por GPIO -dos microsegundos de retardo por bit-, asi que la fila "Zona
alta", que recorre 256 bloques con dos lecturas cada uno, cuesta unos
200 ms bloqueando el hilo de la interfaz.

Eso no es una propiedad del codigo, es una propiedad del REPARTO: se
cumple mientras las filas caras esten todas del lado avanzado. Y un
reparto no se defiende solo. Basta con que alguien anada manana una fila
util en lo que le parece el sitio natural -arriba, con las demas
lecturas- para que la pantalla vuelva a tardar un quinto de segundo en
abrir, sin que nada se queje y sin que nadie ate una cosa con la otra.

Los _Static_assert de main.c cubren lo que cabe en cada pagina. Esto cubre
lo que CUESTA cada fila, que es lo que el compilador no puede ver.

Ademas comprueba que lo destapado no se guarde en ajustes: el usuario pidio
expresamente que la pagina 2 se vuelva a tapar al apagar la radio, y
detras de ella estan el volcado, el borrado y la restauracion de la zona
alta. Un dia alguien "arregla" que no se recuerde y se lo carga.
"""
import re
import sys

# Lo que delata a una fila cara: lee la flash SPI de fuera, de una forma o
# de otra. alta_mira() es la del escaneo de 256 bloques; las imgs_* y las
# spi_flash_* van por el mismo bus a la misma velocidad.
CARAS = ("alta_mira", "spi_flash_", "imgs_")


def lee(ruta):
    with open(ruta, encoding="utf-8") as f:
        return f.read()


def enum_info(src):
    """Las etiquetas INFO_* en orden, tal y como las numera el enum."""
    m = re.search(r"enum\s*\{\s*(INFO_VERSION\s*=\s*0.*?)\}\s*;", src, re.S)
    if not m:
        sys.exit("  no encuentro el enum de INFO_* en main.c   MAL")
    return [t for t in re.findall(r"INFO_[A-Z0-9_]+", m.group(1))
            if t != "INFO_COUNT"]


def basicas(src, orden):
    """A que fila apunta INFO_BASICAS."""
    m = re.search(r"#define\s+INFO_BASICAS\s+\(\(uint8_t\)(INFO_[A-Z0-9_]+)\)", src)
    if not m:
        sys.exit("  no encuentro #define INFO_BASICAS   MAL")
    corte = m.group(1)
    if corte not in orden:
        sys.exit(f"  INFO_BASICAS apunta a {corte}, que no esta en el enum   MAL")
    return orden.index(corte), corte


def cuerpos_del_switch(src):
    """Cada 'case INFO_X:' de info_valor() con el codigo que le cuelga."""
    ini = src.index("static const char *info_valor(")
    # hasta la siguiente funcion de primer nivel
    fin = src.index("\nstatic ", ini + 10)
    cuerpo = src[ini:fin]
    trozos = re.split(r"\n\s*case\s+(INFO_[A-Z0-9_]+)\s*:", cuerpo)
    out = {}
    for i in range(1, len(trozos), 2):
        out[trozos[i]] = trozos[i + 1]
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit("uso: info_check.py <ruta a main.c>")
    src = lee(sys.argv[1])
    fallos = 0

    orden = enum_info(src)
    corte, nombre_corte = basicas(src, orden)
    print(f"  {len(orden)} filas, corte en {nombre_corte} "
          f"({corte} basicas, {len(orden) - corte} avanzadas)")

    # 1. La tabla de nombres tiene que tener una entrada por fila.
    m = re.search(r"k_info_nombres\[INFO_COUNT\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        print("  no encuentro k_info_nombres   MAL")
        fallos += 1
    else:
        # Desde la traduccion (29/09/2026) cada nombre es un T("es","en"),
        # o sea DOS cadenas por fila. Se cuentan los T(...), no las comillas:
        # contando comillas esto decia "64 para 32 filas MAL" con la tabla
        # perfectamente bien, que es un banco gritando por su propia causa.
        # Y de paso se comprueba que ninguna fila se quede a medias.
        cadenas = len(re.findall(r'"(?:[^"\\]|\\.)*"', m.group(1)))
        n = len(re.findall(r'\bT\s*\(', m.group(1)))
        ok = (n == len(orden)) and (cadenas == 2 * n)
        print(f"  nombres: {n} T(es,en) = {cadenas} cadenas para {len(orden)} filas"
              f"   {'ok' if ok else 'MAL'}")
        if not ok:
            fallos += 1

    # 2. Ninguna fila basica puede tocar la flash SPI. Esta es la de verdad.
    cuerpos = cuerpos_del_switch(src)
    malas = []
    for i, fila in enumerate(orden[:corte]):
        cuerpo = cuerpos.get(fila, "")
        for c in CARAS:
            if c in cuerpo:
                malas.append((fila, c))
    if malas:
        for fila, c in malas:
            print(f"  {fila} es basica y llama a {c}()   MAL")
        print("  una fila que lee la flash SPI no puede estar en la pagina 1:")
        print("  son ~200 ms bloqueando el toque. Muevela detras de INFO_BASICAS.")
        fallos += len(malas)
    else:
        print(f"  ninguna de las {corte} filas basicas toca la flash SPI   ok")

    # 3. Y al menos una de las avanzadas si, o el corte no pinta nada.
    hay = any(any(c in cuerpos.get(f, "") for c in CARAS) for f in orden[corte:])
    print(f"  alguna avanzada si la toca   {'ok' if hay else 'MAL'}")
    if not hay:
        fallos += 1

    # 4. Lo destapado no se guarda: tiene que volver a taparse al apagar.
    usos = re.findall(r"^.*s_info_avanzado.*$", src, re.M)
    guardado = [u.strip() for u in usos
                if re.search(r"settings_|SET_[A-Z_]+|cfg_guarda", u)]
    if guardado:
        for u in guardado:
            print(f"  s_info_avanzado se guarda: {u}   MAL")
        fallos += len(guardado)
    else:
        print(f"  s_info_avanzado ({len(usos)} usos) no llega a ajustes   ok")

    print("  TODO CORRECTO" if fallos == 0 else f"  {fallos} FALLOS")
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
