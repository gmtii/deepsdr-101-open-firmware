# Manual de instalación — correcciones y añadidos para la V2

*Escrito el 06/10/2026 contra `INSTALL_HOWTO_EN-ES.pdf` V1 (25 páginas).*

El manual V1 describe el firmware tal y como estaba a finales de septiembre.
Desde entonces han cambiado dos cosas que el manual da por ciertas, y falta
una tercera que es la respuesta al issue que más se ha repetido.

Este fichero lleva **el texto ya redactado en los dos idiomas**, listo para
pegar, indicando en qué página va cada cosa.

---

## 1. Página 7 · "1 Acceder al modo DFU" — los cinco toques ya no hacen falta

El manual dice:

> 1. Entra en Ajustes → Equipo → Información → Versión.
> 2. Toca Versión cinco veces para activar las opciones ocultas.
> 3. Toca Modo DFU dos veces. La radio pasará a modo DFU por USB.

**El paso 2 sobra desde el 02/10/2026.** La fila *Modo DFU* salió de la parte
oculta y está a la vista, y por un motivo que conviene no deshacer: el modo DFU
se busca justo cuando la radio no arranca, que es el peor momento para tener
que acordarse de que antes hay que dar cinco toques en otra fila.

### Texto nuevo · Español

> **Desde el firmware anterior**
>
> 1. Entra en Ajustes → Equipo → Información.
> 2. Toca **Modo DFU** dos veces. La radio pasará a modo DFU por USB.
>
> *En firmware anterior a octubre de 2026 esta fila estaba oculta y hacía falta
> tocar cinco veces la fila Versión para que apareciera.*

### New text · English

> **From the previous firmware**
>
> 1. Go to Settings → Device → Information.
> 2. Tap **DFU mode** twice. The radio will switch to USB DFU mode.
>
> *On firmware older than October 2026 this row was hidden, and you had to tap
> the Version row five times to reveal it.*

---

## 2. Página 12 · "4 Instalar el firmware" — qué hacer si el disco no se reconoce

Esto es lo que falta, y es la respuesta al issue *"the loader doesn't see
update.bin on the disk"*.

El manual dice *"copia update.bin dentro de esa unidad"* y da por hecho que la
unidad se ve bien. A veces no: el cargador dice **"Volumen: no se reconoce"** y
no encuentra el fichero por mucho que esté copiado.

**No es que el disco esté roto.** Es que el driver de la radio no admite
cualquier FAT: pide sectores de 512 bytes, cluster potencia de dos que divida a
4 kB, DOS tablas de asignación, y la zona de datos alineada a 4 kB. Windows no
tiene por qué elegir nada de eso, y según el tamaño del chip elige otra cosa.
Por eso **formatear el disco desde el PC no lo arregla**: deja un volumen
perfectamente válido para Windows y que la radio no puede escribir.

Desde la V3.60 la radio lo formatea ella misma, con los números que sabe leer.

### Texto nuevo · Español

> **Si el cargador dice "Volumen: no se reconoce"**
>
> La radio formatea su propio disco con el formato que ella sabe leer. No hace
> falta formatearlo desde el ordenador; de hecho, no sirve.
>
> 1. Arranca la radio con normalidad, sin pulsar nada.
> 2. Entra en Ajustes → Equipo → Información.
> 3. Busca la fila **Formatear disco**. A su derecha verás lo que va a quedar,
>    por ejemplo `4096 B/cluster, 253 clusters`.
> 4. Tócala una vez. Pondrá **BORRA TODO: tocar otra vez**.
> 5. Tócala otra vez. Tarda menos de un segundo.
> 6. Comprueba que pone **hecho: lee y graba**.
>
> Ese mensaje no es de cortesía: después de formatear, la radio escribe un
> fichero de prueba en el disco y lo vuelve a leer. Si lo que sale no es lo que
> entró, la fila lo dice en vez de decir que todo ha ido bien.
>
> Al enchufar la radio al ordenador verás un `PRUEBA.TXT` de 24 bytes. Es ese
> fichero; puedes borrarlo.
>
> **Esto borra el índice del disco**, así que lo que hubiera dentro deja de
> encontrarse. Guarda antes lo que te interese: las capturas de pantalla
> (`PANT*.BMP`) y, si la has tocado, la configuración (`CONFIG.CSV`).
>
> Después vuelve al paso 1 de esta sección y copia `update.bin` como siempre.

### New text · English

> **If the loader says "Volume: not recognised"**
>
> The radio formats its own disk the way it knows how to read it. You do not
> need to format it from the computer — in fact, that does not help.
>
> 1. Start the radio normally, without holding anything down.
> 2. Go to Settings → Device → Information.
> 3. Find the **Format disk** row. On its right you will see what it is about
>    to lay down, for example `4096 B/cluster, 253 clusters`.
> 4. Tap it once. It will read **ERASES ALL: tap again**.
> 5. Tap it again. It takes well under a second.
> 6. Check that it reads **done: reads and writes**.
>
> That message is not a pleasantry: after formatting, the radio writes a test
> file to the disk and reads it back. If what comes out is not what went in,
> the row says so instead of claiming success.
>
> When you plug the radio into the computer you will see a 24-byte
> `PRUEBA.TXT`. That is the test file; you can delete it.
>
> **This erases the disk's index**, so anything on it stops being findable.
> Save what you care about first: the screenshots (`PANT*.BMP`) and, if you
> have changed it, the settings file (`CONFIG.CSV`).
>
> Then go back to step 1 of this section and copy `update.bin` as usual.

---

## 3. Página 12 · nota al pie sobre el nombre del fichero

Merece un renglón porque ha pasado de verdad y el fallo es silencioso.

### Texto nuevo · Español

> El cargador busca el nombre **exacto** `UPDATE.BIN`. Si Windows lo guarda
> como `update-1.bin` o `update (1).bin` —cosa que hace cuando ya hay uno en la
> carpeta de destino— no lo encontrará y **la radio arrancará igual que estaba,
> sin decir nada**. Comprueba el nombre antes de reiniciar.

### New text · English

> The loader looks for the **exact** name `UPDATE.BIN`. If Windows saves it as
> `update-1.bin` or `update (1).bin` — which it does when one is already in the
> destination folder — it will not be found and **the radio will boot exactly as
> it was, without saying anything**. Check the name before restarting.

---

## Lo que NO cambia del manual V1

Por si acaso, y para que nadie rehaga trabajo: la sustitución de **R8** por
22 Ω (página 6), el puente **BOOT0–3V3** (página 7), los pasos de `dfu-util`
(página 8), los de GD32 All-In-One Programmer (páginas 9 a 11) y la carga de
`BD.BIN` (página 13) siguen siendo correctos palabra por palabra.
