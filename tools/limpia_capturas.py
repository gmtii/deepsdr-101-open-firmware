#!/usr/bin/env python3
"""
QUITA DE LAS IMAGENES LOS METADATOS QUE NO SON DE LA RADIO.

  python3 tools/limpia_capturas.py capturas
  python3 tools/limpia_capturas.py capturas --mira      (solo mirar, no tocar)

POR QUE EXISTE (19/10/2026). Pedido por el usuario, y el fallo lo encontro el.

Las capturas del repositorio llevaban dentro un bloque de "credenciales de
contenido" (C2PA/JUMBF) que NADIE habia puesto a mano: se lo anade el puente
por el que viajan los ficheros, y dice de donde salio la imagen y con que
herramienta. En el fichero no se ve -no sale en la foto- pero esta en los
bytes, y un "git grep" lo encuentra.

Lo que se publica es el firmware. De donde venga cada fichero no es asunto
de nadie, asi que fuera.

QUE HACE, Y POR QUE NO RECOMPRIME
---------------------------------
Recomprimir seria lo facil y seria lo peor: una captura de pantalla
recomprimida pierde calidad y deja de ser lo que se ve en la radio. Esto no
toca los pixeles. Abre el fichero por sus bloques y TIRA los que llevan el
metadato, dejando el resto byte a byte como estaba:

  PNG   bloques 'caBX' (el JUMBF de C2PA) y los de texto -tEXt/iTXt/zTXt-
        cuyo contenido nombre el metadato. Los demas -IHDR, IDAT, PLTE,
        tRNS, gAMA...- se copian sin mirar.
  JPEG  los marcadores APP11 (que es donde va el JUMBF) y cualquier APPn
        que lleve el metadato dentro. El resto del fichero igual.

COMPROBACION. Despues de limpiar, vuelve a abrir el resultado y comprueba
que los pixeles son exactamente los mismos que antes. Si no lo fueran, deja
el original y lo dice: mas vale un metadato que una captura estropeada.
"""
import os
import sys

# Lo que delata al bloque es el FORMATO, no quien lo firme: "jumb" es la caja
# JUMBF y "c2pa"/"contentcredentials" el estandar que va dentro. Se busca eso
# y no nombres de empresas ni de programas: los nombres cambian, el formato
# no, y ademas en este repositorio no se nombra a nadie.
#
# Y en la practica casi no se usa: el bloque de PNG se tira por su TIPO de
# bloque ('caBX') y el de JPEG por su MARCADOR (APP11), que es donde va
# siempre. Esta lista es la red por si alguna vez viene metido en un bloque
# de texto corriente.
MARCAS = (b"c2pa", b"jumb", b"contentcredentials")


def sucio(b):
    low = b.lower()
    return any(m in low for m in MARCAS)


def limpia_png(d):
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    fuera, i, n = [], 8, 0
    while i + 8 <= len(d):
        ln = int.from_bytes(d[i:i + 4], "big")
        typ = d[i + 4:i + 8]
        trozo = d[i:i + 12 + ln]
        if len(trozo) < 12 + ln:
            return None                      # fichero truncado: no se toca
        quita = (typ == b"caBX") or (typ in (b"tEXt", b"iTXt", b"zTXt")
                                     and sucio(d[i + 8:i + 8 + ln]))
        if quita:
            n += 1
        else:
            fuera.append(trozo)
        i += 12 + ln
        if typ == b"IEND":
            break
    return (d[:8] + b"".join(fuera), n) if n else (d, 0)


def limpia_jpg(d):
    if d[:2] != b"\xff\xd8":
        return None
    fuera, i, n = [d[:2]], 2, 0
    while i + 4 <= len(d):
        if d[i] != 0xFF:
            break
        m = d[i + 1]
        if m in (0xD8, 0xD9) or 0xD0 <= m <= 0xD7:
            fuera.append(d[i:i + 2]); i += 2; continue
        ln = int.from_bytes(d[i + 2:i + 4], "big")
        seg = d[i:i + 2 + ln]
        if m == 0xDA:                        # empiezan los datos: lo demas tal cual
            fuera.append(d[i:]); i = len(d); break
        quita = (m == 0xEB) or (0xE0 <= m <= 0xEF and sucio(seg))
        if quita:
            n += 1
        else:
            fuera.append(seg)
        i += 2 + ln
    if i < len(d) and not fuera[-1].endswith(d[-2:]):
        fuera.append(d[i:])
    return (b"".join(fuera), n) if n else (d, 0)


def pixeles(ruta_bytes, nombre):
    """Huella de los pixeles, para no publicar una imagen estropeada."""
    try:
        import io
        import hashlib
        from PIL import Image
        im = Image.open(io.BytesIO(ruta_bytes))
        return hashlib.sha256(im.convert("RGB").tobytes()).hexdigest()
    except ImportError:
        return None                          # sin Pillow no se compara, y se dice
    except Exception:
        return "ILEGIBLE"


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__.strip().split("\n\n")[1])
    carpeta = sys.argv[1]
    mirar = "--mira" in sys.argv
    sin_pillow = False
    tocados = total = 0

    for nom in sorted(os.listdir(carpeta)):
        ruta = os.path.join(carpeta, nom)
        bajo = nom.lower()
        if not os.path.isfile(ruta) or not bajo.endswith((".png", ".jpg", ".jpeg")):
            continue
        total += 1
        d = open(ruta, "rb").read()
        r = limpia_png(d) if bajo.endswith(".png") else limpia_jpg(d)
        if r is None:
            print("  %-28s no lo entiendo, lo dejo como esta" % nom)
            continue
        nuevo, n = r
        if n == 0:
            print("  %-28s limpio" % nom)
            continue

        antes, despues = pixeles(d, nom), pixeles(nuevo, nom)
        if antes is None:
            sin_pillow = True
        elif antes != despues or despues == "ILEGIBLE":
            print("  %-28s *** NO lo toco: los pixeles cambiarian ***" % nom)
            continue

        print("  %-28s %d bloque(s) fuera, %d -> %d bytes%s"
              % (nom, n, len(d), len(nuevo), "   (solo mirando)" if mirar else ""))
        if not mirar:
            with open(ruta, "wb") as f:
                f.write(nuevo)
        tocados += 1

    print("\n  %d imagenes, %d con metadatos%s"
          % (total, tocados, " (no se ha escrito nada)" if mirar else " limpiadas"))
    if sin_pillow:
        print("  (aviso: sin Pillow no he podido comprobar que los pixeles"
              " no cambien; instala pillow si quieres esa red)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
