# Qué es `flash_micro_0x08000000.bin`

Volcado de los **512 kB de flash interna del GD32F450VET6**, desde
`0x08000000`. Sacado de la radio el 24/09/2026 con la celda «Volcar flash
micro» de Información (firmware V1.24).

Es la copia que faltaba: aquí vive el **gestor de arranque**, que es el
único camino para meter firmware en esta radio. Todo lo demás (el FAT12
de la flash SPI y la fuente china) ya se podía reconstruir; esto no.

## El mapa

| Desde | Tamaño | Qué |
|---|---|---|
| `0x08000000` | ~57 kB | gestor de arranque (el resto de sus 128 kB, borrado) |
| `0x08020000` | 244.008 B | la aplicación |
| `0x0805B928` | 18.136 B | relleno de ceros de `update4.bin` |
| `0x08060000` | 8 B | la firma `8f25c865599c5531` |
| `0x08060008` | resto | borrado |

La aplicación volcada coincide **byte a byte** con el `build/firmware.bin`
de la V1.24. Eso valida el camino entero del volcado contra una
referencia conocida.

## El gestor de arranque

`** Bootloader v4.0.3 LCD:%04X by BH5HNU **`

Lleva **FatFs** (están todas sus cadenas de error, de `FR_OK` a
`FR_INVALID_PARAMETER`) y la pila USB de ST (`STM32 Mass Storage`,
`MSC Interface`). Y su propia fuente latina: `neep-iso8859-1-10x20`.

Sus mensajes cuentan el proceso entero:

```
FatFs mount OK.
Flash total = %d KB, free = %d KB
update4.bin
No update4.bin.
Target update4.bin find, updating...
Erase SECTOR 1... 2... 3...
Read update4.bin Error ! %d %d
Running APP...
APP Not Programmed !
Please use DeepSDR firmware !
```

O sea: monta el sistema de ficheros, busca `update4.bin`, borra tres
sectores y lo programa **directamente** en la flash interna. La firma de
`0x08060000` es el final del propio `update4.bin` y es la marca de "aquí
hay aplicación"; sin ella sale `APP Not Programmed !`.

Los tres sectores que borra son los de 128 kB que empiezan en
`0x08020000`, o sea hasta `0x0807FFFF`: nada puede sobrevivir en la
flash interna por encima de la aplicación.

## Dos cosas que esto aclara

**1. `update4.bin` NO se copia a la zona alta de la flash SPI.** Se lee
del sistema de ficheros con FatFs y se programa directo. Desaparece del
USB porque el gestor de arranque lo borra al terminar. La teoría de que
lo preparaba arriba queda descartada.

**2. El gestor de arranque NO usa la fuente china.** Lleva la suya
(`neep-iso8859-1-10x20`) y todos sus mensajes son en inglés.

Conclusión, con la cautela de que es deducción a partir de cadenas y no
demostración: el megabyte de arriba de la flash SPI —la fuente de 500 kB
y el medio mega borrado de encima— no lo usa nada que importe.

Un barrido buscando constantes de 32 bits que apunten a esa zona salió
**no concluyente**: en código Thumb cualquier pareja de instrucciones
puede parecer una dirección, y los aciertos eran pocos y dispersos.
