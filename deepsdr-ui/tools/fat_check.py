#!/usr/bin/env python3
"""
Le pasa a fsck.fat los volumenes que dejo el banco, y comprueba el
contenido de PRUEBA.BIN.

POR QUE fsck.fat Y NO UNA COMPROBACION NUESTRA
----------------------------------------------
Porque una comprobacion nuestra compartiria las suposiciones del codigo
que esta comprobando. Si el firmware encadena los clusters de una forma
que nos parece bien pero no es la que dice la norma, un comprobador
escrito por nosotros dira que todo esta perfecto - y Windows dira otra
cosa el dia que el usuario enchufe el cable.

fsck.fat viene de dosfstools, no sabe nada de este proyecto, y se queja
de clusters perdidos, cadenas cruzadas, las dos copias de la FAT
distintas y tamanos que no cuadran con la cadena. Es justo el tribunal
que hace falta.

Y el CONTENIDO se comprueba aparte: que el volumen sea valido no dice
nada de si cada trozo cayo en su sitio. El relleno depende de la posicion
absoluta dentro del fichero, asi que un trozo escrito en el offset
equivocado no se parece al que le tocaba. La formula se LEE de main.c en
vez de copiarse aqui: una copia a mano de algo que existe es una copia
que un dia deja de coincidir.
"""
import glob
import os
import re
import subprocess
import sys

TROZO = 4096


def formula_del_banco(ruta):
    """
    Saca el relleno de relleno() del banco y lo devuelve como funcion.

    SALIA DE main.c HASTA EL 02/10/2026, de prueba_byte(): el firmware tenia
    una fila de "Probar imagenes" que escribia ese mismo patron, y leerlo de
    alli era lo que impedia que las dos copias se separaran en silencio.

    Esa fila se quito -ver la poda de Informacion en CAMBIOS.md- y con ella
    el patron del firmware. El unico usuario que queda es el banco, asi que se
    lee de ahi. Lo que importa no cambia: este comprobador NO escribe el
    patron a mano, lo lee de quien lo genera, y sigue leyendo el volumen con
    su propia implementacion de FAT12 -que es donde estaria el fallo
    compartido si lo hubiera-.
    """
    src = open(ruta, encoding="utf-8").read()
    m = re.search(r"static uint8_t relleno\(uint32_t k\)\s*\{\s*"
                  r"return\s*\(uint8_t\)\((.*?)\);", src, re.S)
    if not m:
        sys.exit("  no encuentro relleno() en el banco   MAL")
    expr = m.group(1)
    # C -> Python: quitar los sufijos U de los literales
    py = re.sub(r"\b(0x[0-9A-Fa-f]+|\d+)U\b", r"\1", expr)
    if not re.fullmatch(r"[ \t()k*^>~&|0xX0-9A-Fa-f+\-]+", py):
        sys.exit(f"  relleno() tiene algo que no se traducir: {py}   MAL")
    return lambda k: eval(py, {"__builtins__": {}}, {"k": k}) & 0xFF, expr.strip()


def lee_fichero(img, n11):
    """
    Saca un fichero del volumen siguiendo su cadena FAT12 a mano.

    A mano y no con una biblioteca a proposito: si el firmware y el lector
    compartieran implementacion, compartirian tambien el fallo. Devuelve
    (datos, error, saltos, saltos_hacia_atras).
    """
    d = open(img, "rb").read()
    bps = int.from_bytes(d[11:13], "little")
    res = int.from_bytes(d[14:16], "little")
    nf = d[16]
    rde = int.from_bytes(d[17:19], "little")
    fsz = int.from_bytes(d[22:24], "little")
    raiz = (res + nf * fsz) * bps
    datos = raiz + rde * 32
    fat = d[res * bps: res * bps + fsz * bps]

    for off in range(raiz, datos, 32):
        e = d[off:off + 32]
        if e[0] in (0x00, 0xE5) or e[0:11] != n11:
            continue
        primero = int.from_bytes(e[26:28], "little")
        tam = int.from_bytes(e[28:32], "little")
        out = bytearray()
        cadena = []
        vistos = set()
        c = primero
        while True:
            if c in vistos:
                return None, "la cadena tiene un bucle", 0, 0
            vistos.add(c)
            cadena.append(c)
            ini = datos + (c - 2) * bps
            out += d[ini:ini + bps]
            o = c + (c // 2)
            v = int.from_bytes(fat[o:o + 2], "little")
            sig = (v >> 4) if (c & 1) else (v & 0x0FFF)
            if sig >= 0x0FF8:
                break
            if sig < 2:
                return None, f"la cadena salta a un cluster imposible ({sig})", 0, 0
            c = sig
        if len(out) < tam:
            return None, f"la cadena da {len(out)} bytes y la entrada dice {tam}", 0, 0
        saltos = sum(1 for a, b in zip(cadena, cadena[1:]) if b != a + 1)
        atras = sum(1 for a, b in zip(cadena, cadena[1:]) if b < a)
        return bytes(out[:tam]), None, saltos, atras
    return None, f"no esta {n11.decode('ascii', 'replace')}", 0, 0


def lee_prueba_bin(img):
    return lee_fichero(img, b"PRUEBA  BIN")


def main():
    if len(sys.argv) < 3:
        sys.exit("uso: fat_check.py <dir de los .img> <ruta a main.c>")
    d, ruta_banco = sys.argv[1], sys.argv[2]
    relleno, expr = formula_del_banco(ruta_banco)
    print(f"  relleno leido del banco: {expr}")

    imgs = sorted(glob.glob(os.path.join(d, "fatanade_*.img")))
    if not imgs:
        sys.exit("  no hay volumenes que comprobar   MAL")

    fallos = 0
    ultimo_atras = 0
    for img in imgs:
        n = os.path.basename(img)
        r = subprocess.run(["fsck.fat", "-n", "-V", img],
                           capture_output=True, text=True)
        limpio = r.returncode == 0
        datos, err, saltos, atras = lee_prueba_bin(img)
        if datos is None:
            print(f"  {n}  {err}   MAL")
            fallos += 1
            continue
        malos = sum(1 for k, b in enumerate(datos) if b != relleno(k))
        ok = limpio and malos == 0
        forma = "contigua" if saltos == 0 else f"{saltos} saltos, {atras} hacia atras"
        print(f"  {n}  fsck {'limpio' if limpio else 'SE QUEJA'}, "
              f"{len(datos) // TROZO} trozos, cadena {forma}, "
              f"{'relleno correcto' if malos == 0 else f'{malos} bytes mal'}"
              f"   {'ok' if ok else 'MAL'}")
        ultimo_atras = atras
        if not limpio:
            for l in (r.stdout + r.stderr).strip().splitlines()[:6]:
                print(f"       {l}")
        if not ok:
            fallos += 1

    # Un escenario que no llega a fragmentar nada no ha probado lo que
    # dice probar. Si el agujero no se uso, esto es un fallo del BANCO,
    # no del firmware - y hay que enterarse igual.
    if ultimo_atras == 0:
        print("  la cadena nunca salto hacia atras: el agujero no se uso,")
        print("  o sea que este banco no ha probado la fragmentacion   MAL")
        fallos += 1
    else:
        print(f"  la cadena acabo saltando {ultimo_atras} veces hacia atras: "
              f"fragmentacion de verdad   ok")

    # Y lo que dice el firmware de su propia cadena, contra lo que dice
    # este recorrido, escrito aparte. Dos implementaciones independientes.
    try:
        suyo = int(open(os.path.join(d, "fatanade_saltos.txt")).read().strip())
    except (OSError, ValueError):
        print("  el firmware no dejo su cuenta de saltos   MAL")
        fallos += 1
    else:
        ok = suyo == ultimo_atras
        print(f"  el firmware cuenta {suyo} saltos y este recorrido {ultimo_atras}"
              f"   {'ok' if ok else 'MAL'}")
        if not ok:
            fallos += 1

    # ------------------------------------------------------------------
    # Y LA FOTO, EXTRAIDA DEL VOLUMEN Y ABIERTA CON PILLOW
    # ------------------------------------------------------------------
    # El camino entero de una vez: el firmware reserva el fichero, escribe
    # la cabecera BMP y los renglones, este comprobador lo saca siguiendo
    # la cadena FAT12 a mano, y Pillow -que no sabe nada de este proyecto-
    # lo abre y dice si los puntos estan donde se pusieron.
    foto = os.path.join(d, "fatanade_foto.img")
    if os.path.exists(foto):
        datos, err, _, _ = lee_fichero(foto, b"SSTV001 BMP")
        if datos is None:
            print(f"  SSTV001.BMP  {err}   MAL")
            fallos += 1
        else:
            tmp = os.path.join(d, "sstv001.bmp")
            with open(tmp, "wb") as f:
                f.write(datos)
            try:
                from PIL import Image
                im = Image.open(tmp).convert("RGB")
                px = im.load()
                malos = 0
                for y in range(256):
                    for x in range(320):
                        if y == 0:        e = (255, 0, 0)
                        elif y == 255:    e = (0, 0, 255)
                        elif x == 0:      e = (0, 255, 0)
                        elif x == 319:    e = (255, 255, 255)
                        else: e = [(248, 0, 0), (0, 252, 0),
                                   (0, 0, 248), (248, 252, 0)][(x * 4) // 320]
                        if px[x, y] != e:
                            malos += 1
                ok = im.size == (320, 256) and malos == 0
                print(f"  SSTV001.BMP  {len(datos)} B del volumen, Pillow: "
                      f"{im.size[0]}x{im.size[1]}, {malos} puntos mal"
                      f"   {'ok' if ok else 'MAL'}")
                if not ok:
                    fallos += 1
            except Exception as e:                  # noqa: BLE001
                print(f"  SSTV001.BMP  Pillow no lo abre: {e}   MAL")
                fallos += 1

    print("  TODO CORRECTO" if fallos == 0 else f"  {fallos} FALLOS")
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
