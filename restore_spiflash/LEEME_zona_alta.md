# Qué es `zona_alta_0x100000.bin`

Volcado de la **flash SPI externa** (W25Q16, 2 MB) desde el byte
`0x100000` — justo donde acaba el sistema de ficheros FAT12 — y 512 kB
hacia arriba. Sacado de la radio el 24/09/2026 con la celda
«Volcar zona alta» de Información (firmware V1.22).

## Qué contiene

Una **fuente china de 24×24 píxeles**, 72 bytes por glifo:

| | |
|---|---|
| Datos | `0x101000` – `0x17AFFF` (122 bloques de 4 kB, seguidos) |
| Glifos | 6.940 de 72 bytes |
| En blanco | 5 de 6.940 |
| Densidad de tinta | 0,11 – 0,59 (media 0,46) |

6.763 hanzi es el juego GB2312 completo, y 6.763 × 72 = 486.936 bytes:
esto es una HZK24 de manual. Es la fuente con la que el firmware de
fábrica pintaba su interfaz en chino. El nuestro no la usa — lleva las
suyas en la flash del micro.

**No es** `update4.bin`: no está su firma `8f25c865599c5531`, no hay
ninguna tabla de vectores ARM plausible, ni calibración, ni restos de
sistema de ficheros.

## Lo que sigue sin saberse

El primer bloque (`0x100000`–`0x100FFF`) está **borrado**, y la mitad de
arriba del chip (`0x180000`–`0x1FFFFF`) también, entera. Ese medio
megabyte limpio son exactamente dos imágenes de 256 kB, así que sigue
siendo el candidato a área de preparación del gestor de arranque — y por
eso no se escribe ahí. Ver el comentario de `IMGS_SUELO_MIN` en
`User/img_store.h`.

Pregunta abierta: **¿el gestor de arranque lee esta fuente?** Si la
pantalla del modo USB enseña caracteres chinos, sí, y entonces estos
500 kB son intocables. Si no, la fuente está huérfana.

## Con esto, el chip está respaldado entero

- `restore_spiflash.img` — el megabyte de abajo (FAT12 con la geometría
  de fábrica), reconstruible además con `build_fat12_image.py`
- `zona_alta_0x100000.bin` — este fichero, los 512 kB siguientes
- `0x180000`–`0x1FFFFF` — borrado, nada que guardar

Lo que **no** está respaldado es la flash **interna del micro** (el
gestor de arranque, en `0x08000000`). Eso necesita una sonda SWD y el
`openocd/gd32f450.cfg` que ya está en el repositorio.
