# DEEPSDR 101 — rama `reload`

Firmware del DEEPSDR 101 (GD32F450VET6) con la interfaz rehecha, modos nuevos,
cargador de arranque propio y un simulador que compila **el código de verdad**
del firmware en el PC.

Esto es la lista corta. Lo que trae **cada versión**, en dos idiomas y en
pocas líneas, está en **`NOVEDADES.md`**. El porqué de cada cosa, con las
medidas y los fallos que hubo por el camino, está en **`CAMBIOS.md`**
(ordenado por fecha).
Versión actual **V3.72**; cargador **v2.15**. Los tres ficheros del sistema son
`update.bin` (firmware), `customboot.bin` (cargador) y `BD.BIN` (bases de
datos): el cargador mira al arrancar si hay un `update.bin` en el disco USB, y
el firmware mira si hay un `BD.BIN`.

> **Esta rama no es compatible con la anterior.** La aplicación empieza en
> `0x0800C000` y la valida una cabecera con CRC32, así que hace falta grabar
> **primero** el cargador `v2.0` y **después** el `update.bin`. La rama de
> antes, con la aplicación en `0x08020000` y los nombres viejos, sigue en
> `..\deepsdr`.

---

## Interfaz

- Se cambió el motor de dibujo entero: `gfx.c` abría una ventana del panel por
  píxel; `gfx2` compone una banda en RAM y la vuelca de una vez (**una ventana,
  un volcado**).
- Se añadieron tipografías proporcionales con antialias, esquinas redondeadas y
  degradados, que con el camino anterior no eran viables.
- Se partió la interfaz en módulos que no dependen del firmware salvo de
  `gfx2`: `ui_top`, `ui_grid`, `ui_cfg`, `ui_det`, `ui_act`, `ui_kbd`,
  `ui_digi`, `spec_chrome`, `splash`. Por eso el simulador puede compilar
  **los mismos ficheros** y sacar un PNG.
- Se borró el sistema de interfaz viejo: de `ui.c` queda **una** función,
  `ui_panel_draw()`. Comprobado con `--print-gc-sections`, no a ojo.
- Se añadieron temas de interfaz y **15 paletas** de espectro/cascada.
- Se añadió pantalla de Información: versión, fecha de compilación, flash y RAM
  usadas, calculadas desde los símbolos del enlazador.
- Se añadieron reloj ajustable y teclado numérico de frecuencia.
- Se reordenó el menú (Equipo el último, Brillo a Pantalla) y se arreglaron
  textos que se salían de su celda, tres rayas negras invisibles en tema
  oscuro y el renglón vivo, que estaba en dos sitios.
- Se añadió marquesina al campo de texto de los modos digitales, que se cortaba.
- Se quitaron **26,7 kB de fuentes** que no usaba nadie.
- Se arregló que el mapa mandaba el **47,5 % de los píxeles dos veces**.
- Se arregló el bucle de renglones, que no miraba dónde acaba el panel.
- Se arregló que el **100 % de brillo no era el 100 %**: las dos centésimas que
  faltaban eran de ciclo de trabajo, no de luz.

## Espectro y cascada

- Se cambió la autoescala: antes mínimo y máximo absolutos (el pico de continua
  se los comía, 121 dB de escala); ahora **percentiles 5 % y 99,5 %** sobre un
  histograma de 128 cubos (50 dB útiles).
- Se añadió **tocar el espectro para caer en la señal** (`spec_snap.c`), con
  interpolación parabólica sobre tres bins: error medio 21–34 Hz, peor 64 Hz,
  contra los 187 Hz que deja caer al bin entero.
- Se cambió dónde cae según el modo: encima de la portadora en AM/SAM/NFM/WFM,
  un pitch por debajo en CW, en el borde en USB/LSB. Si no hay nada 8 dB sobre
  el ruido, no inventa señal.
- Se bajó el espectro de ser el **94 % del fotograma** a lo que ocupa ahora.
- Se arregló que el suavizado le rompía el eje del tiempo a la cascada.
- Se arregló la basura que aparecía en la cascada al salir de cinco modos.
- Se documentó por qué el panadaptador está descentrado (75 % del ancho): es la
  arquitectura, no un fallo.

## Modos y decodificadores nuevos

- **CW** (`cw.c`, ~1.500 líneas): Goertzel solapado, banco de 9 sondas para
  engancharse ±250 Hz desafinado, clasificador de dos medias y reproducción
  retrospectiva del historial. Filtro de audio paso banda de **4 etapas**
  (1k0 / 500 / 250 Hz) elegidas midiendo faldas, no a ojo.
- **FT8**: portado, enganchado y metido en la unión de RAM de la cascada.
  Ventana ensanchada de 400 a **1.600 Hz** (256 bins, toda la banda tras el
  diezmado). Panel en columnas, botón Borrar, botón de frecuencias.
- **WSPR**: receptor completo del audio al indicativo, con mapa.
- **HFDL**: datos de aviones en onda corta, con tabla de estaciones, portero de
  ráfaga, base de modelos de avión y mapa.
- **AIS**: barcos, con tabla y filtro por MMSI.
- **ALE**: con Golay demostrado en banco, no muestreado.
- **PSK31**: varicode, recuperación de reloj por histograma, sintonía
  automática y umbral medido.
- **JTTY**: receptor propio, frecuencias, mapa, FEC y unión de trozos.
- **NAVTEX, WEFAX, SSTV, APRS, RTTY, RDS, DCF77, ALS162**: en el firmware, con
  sus bancos.
- **IDENT**: "¿qué señal es esta?". Mide ancho, tonos, separación, velocidad de
  símbolo, periodo de trama y cadencia, y los convierte en un nombre con una
  tabla de 28 firmas. Doce confirmadas en antena. Desde la V3.57 avisa además
  cuando por la frecuencia pasa **más de una señal turnándose** (`y a ratos:`).
- **El RTTY se ajusta solo** desde la V3.59: pone los tonos encima de la señal,
  acierta el desplazamiento, mide la velocidad por la racha más corta y detecta
  si el desplazamiento está invertido. Sólo se mueve cuando lo que hay puesto
  va mal — la salud del decodificador (qué parte de los caracteres cierran con
  su bit de parada) es la que manda — y se puede apagar.
- Se añadió **mapa del mundo** (costa en 7,1 kB y **cero** bytes de SRAM), con
  botón, zoom y de borde a borde, y puntos de FT8, WSPR, JTTY, HFDL y AIS.
- Se añadió **"quién emite en esta frecuencia"**: lista de emisoras cargada de
  fichero, con el desfase horario en el fichero y no en el firmware.
- Se añadió botón para recorrer las frecuencias conocidas de cada modo.
- Se añadió **atenuador manual** que el AGC de RF ya no pisa: lo elegido es un
  suelo, el automático puede atenuar más pero nunca menos.
- Se puso **WFM** sola al tope de ganancia de entrada (la pérdida del mezclador
  en VHF se come ~20 dB).

## Ficheros, USB y ajustes

- Se añadió **guardado de imágenes** de SSTV y WEFAX como BMP en el volumen FAT
  que la radio enseña por USB, escribiendo en el FAT sin romperlo.
- Se unificaron los datos en **un solo fichero** (`BD.BIN`) con directorio, en
  vez de cinco repartos escritos a mano.
- Se añadió soporte de `BD.BIN` **troceado** por el FAT: se sigue la cadena,
  sin tabla.
- Se arregló que guardar los ajustes **borraba los ficheros vecinos**.
- Se arregló que si `CONFIG.CSV` no empezaba en frontera de bloque, los ajustes
  se perdían.
- Se arregló que el borrado del `ICAO24.BIN` sólo miraba 16 entradas del
  directorio, y que un fichero demasiado grande se truncaba, se daba por bueno
  y se borraba.
- Se pasó el **QTH** y las bandas de VHF a los ajustes: ningún QTH en el código.
- Se añadió **idioma** (español/inglés) en los ajustes.
- Se añadió persistencia del interruptor de **reducción de ruido** (`nr_on`),
  que tenía `nr_strength` guardado pero no el on/off.
- Se validan los valores que vienen del fichero antes de aplicarlos.
- Se añadió **volcado de la ROM de fábrica a `VOLCADO.BIN` sin abrir la radio**.
- Se añadió botón de **DFU** en Información, y se dejó funcionando tras once
  intentos documentados (V2.31 a V2.44): el testigo en `.noinit`, el reset de
  periféricos, el `BOOT_MODE`, los vectores a cero y el DP abajo 300 ms.
- **Formatear el disco desde la radio** (Información → *Formatear disco*, desde
  la V3.59). El driver no admite cualquier FAT —512 B/sector, cluster potencia
  de dos que divida al borrado de 4 kB, dos tablas, datos alineados a 4 kB— y
  Windows no tiene por qué elegir eso. La fila dice antes de tocar nada lo que
  va a quedar, y pide dos toques.

## Cargador de arranque propio (`boot/`, v2.15)

- Se escribió un **cargador de arranque nuestro** que reemplaza al de fábrica,
  en 0x08000000, sin tocar la aplicación ni abrir la radio.
- Se sacaron del binario original, desensamblando y **sin adivinar**: los siete
  pines de placa, las tres secuencias de arranque del panel RM68120 (A/B/C,
  forzada la B), el PWM de retroiluminación (TIMER4, PA3 en AF2) y la
  temporización del EXMC.
- Se corrigió el reloj: el original va a **144,384 MHz** con USB a 48,128, no a
  192 como yo había afirmado. Con el PLL mal, el USB no enumeraba.
- Se arregló que el disco USB no montaba: las llamadas del MSC reciben el
  desplazamiento **en bytes**, no en bloques (`usbd_msc_scsi.c:509`).
- Se añadió una **salida de emergencia a DFU**: encender con el mando pulsado y
  girándolo. Ha rescatado la radio dos veces.
- Se añadió pantalla de actualización con fuente propia, versión del cargador a
  la derecha, dos columnas y **barra de progreso** mientras graba.
- Se hizo el **arranque normal totalmente silencioso**: no se ve nada si no hay
  `UPDATE4.BIN` y no se pulsa nada.
- Se pasó la flash SPI del cargador a **SPI0 por hardware** (12 MHz); la
  aplicación conserva el bit-bang porque comparte el bus con el táctil.
- Se dejaron la pila del cargador por debajo del testigo de DFU y el salto a la
  aplicación limpiando SysTick y las 240 líneas del NVIC.

## El mapa de flash (rama `reload`, V3.00)

- Se cambió el mapa entero: **una sola región** de `0x0800C000` a `0x08080000`
  en vez de 256 kB abajo + 8 bytes de firma + un desván de 64 kB.
- Se pasó de **3.272 bytes libres para código** a **156.612**. El desván tenía
  5.940 libres y no servían para una línea de código: el enlazador no derrama
  de una región a otra.
- Se le reservan al cargador **48 kB** (sectores 0 a 2); usa 34.172.
- Se cambiaron los 8 bytes de firma por una **cabecera de 16 bytes** en
  `base+0x200` con magia, longitud y **CRC32**, comprobada en cada arranque.
  Con la firma vieja, una imagen grabada a medias arrancaba; ahora no.
- Se cambió el borrado a **tabla explícita de sectores**: el bucle `+= 0x20000`
  de antes se habría saltado el sector 4 en silencio.
- Se añadió `tools/mapa.py`, que comprueba en cada compilación que los **tres**
  sitios donde está escrita la dirección base dicen lo mismo (enlazador,
  `cargador.h` y el `SCB->VTOR` de `system_gd32f4xx.c`, que no puede incluir a
  los otros dos).
- Se quitaron `.arriba`, `FLASH2`, `FIRMA`, `_sarriba`, `_earriba`,
  `User/firma_app.c`, `tools/firma.py` y la fila "Flash alta" de Información.

## Rendimiento y memoria

- Se descubrió que el **muro de los 256 kB no era un muro**: el gestor de
  arranque original no comprueba lo que yo creía, y eso abrió **64 kB** más.
  Con ese sitio entró WSPR esa misma noche.
- Se arregló que la **pila se salía por 6,3 kB**, y se puso `tools/pila.py` a
  vigilarlo en cada compilación, ahora contando el anidamiento de interrupciones
  (peor caso 5.464 bytes, margen 592).
- Se bajó la banda de dibujo (`GFX2_BAND_H`) de 24 a **16 filas**: 25 kB en vez
  de 38.
- Se pasó el historial de la cascada a un byte por píxel (índice de paleta):
  **56 kB ahorrados**.
- Se comparte esa RAM entre cascada, FT8, HFDL, WSPR, AIS y ALE mediante una
  unión, con cerrojo de préstamo.
- Se quitaron 4 ms de trabajo **dentro de la interrupción de audio** en WSPR, y
  24.000 llamadas a trigonometría por segundo.
- Se arregló el periodo de 62,5 ms que debía ser 6,25.
- Se resolvió, midiendo en la propia radio, que **el cargador nuestro no
  ralentiza el espectro**: 19 ms contra 20 ms, idénticos. Lo que cambiaba eran
  las condiciones de banda.

## Herramientas y comprobaciones

- Se montó un **simulador** (`deepsdr-ui/`) que compila los ficheros reales del
  firmware en el PC y saca PNG.
- Hay más de **115 bancos**, de los que la mayoría corren en `make comprueba`
  sin placa.
- La regla del proyecto: **los bancos escriben la regla aparte**. Si el banco
  llamara a la función del firmware comprobaría que hace lo que hace, no lo que
  debe.
- Se añadieron `tools/pila.py` (pila), `tools/cabecera.py` (cabecera y CRC del
  update), `tools/gordos.py` (quién ocupa), `tools/mapa.py` (que los tres
  mapas de flash dicen lo mismo), `tools/panel_check.py` y el utillaje de
  datos que tiene su apartado aquí abajo.
- Se añadió `avisos.mk`: el firmware compila con **cero avisos**.
- Se añadió `tools/idioma_check.py`: recorre los 130 fuentes de `User/` y falla
  si una cadena de pantalla no pasa por `tr()` o por `T()`. Nació el 06/10/2026
  porque el modo Ident, el veredicto del cargador y las fases de HFDL seguían
  contestando en castellano con la radio en inglés, y eso no se ve leyendo: se
  ve poniendo la radio en inglés y llegando a esa pantalla.
- Bancos nuevos del 06/10/2026: `rttyaire` (una grabación por el `rtty.c` de
  verdad), `rttysolo` (el ajuste automático, arrancando mal **y** arrancando
  bien), `identturnos` (dos señales turnándose) y `formato` (que lo que escribe
  el formateador lo acepte el mismo `spi_flash.c` que rechazaba el volumen del
  issue).
- Se añadió `tools/manual_check.py`: saca del firmware las **488 cadenas de
  interfaz** -las que pasan por `tr()` o `T()`, o sea las que alguien puede
  llegar a leer en la pantalla- y dice cuáles no se nombran en el manual. Nació
  el 06/10/2026 porque el manual se había escrito mirando pantallas, y así sólo
  entra lo que está dibujado: tocar la frecuencia para teclearla, tocar la hora
  y mantener la esquina para una captura no tienen rótulo, y por eso faltaban
  las tres. Pasó de 230 cadenas mencionadas a 357.
- Se añadió `tools/sin_llamar.py`: las funciones de motor -`_poll`, `_tick`,
  `_paso`, `_arranca`- que no llama nadie. Nació el 06/10/2026 porque
  `rtty_auto_poll()` llevaba ocho versiones **sin llamarse**: el módulo del
  ajuste automático del RTTY estaba montado y sin enchufar, y no lo vio ni el
  compilador -es pública, no es "unused"-, ni el enlazador -`--gc-sections` la
  tira y tan contento-, ni su propio banco -que la llama él-, ni el dueño
  usando la radio, porque lo que hace esa función es arreglar una sintonía que
  va mal y cuando va mal uno mueve el dial. La primera versión miraba todas las
  funciones públicas y sacaba 227, con la única de verdad perdida entre
  accesores de depuración; afilada a la familia donde el silencio significa que
  algo no corre, saca 3.
- Se añadió el banco `identancho`: los nombres y las notas de las 27 firmas de
  Ident, contra el ancho del renglón en píxeles, en los dos idiomas y leídos de
  `ident_firmas()`. Nació porque la nota del STANAG 4285 salía cortada a mitad
  de palabra en la radio.
- Se arreglaron, en distintas tandas, bancos que llevaban días o semanas en
  verde sin comprobar nada.
- **08/10/2026 — los dos bancos que no fallaban.** `tools/sin_llamar.py` y
  `tools/manual_check.py` imprimían y salían con 0, o sea que ninguno podía
  tumbar una entrega. Y cada uno fallaba a su manera:
  - `sin_llamar.py` **sí** corría en `comprueba`, y llevaba dos días sacando
    *"cargador_arranca: NADIE la llama"*. Era mentira: la llama
    `boot/boot_main.c:1037`, que es justo quien graba el `update.bin`. Sólo
    leía `User/`, no `boot/`. Un aviso falso que sale siempre se convierte en
    parte del paisaje, y el día que diga algo verdadero tampoco se lee.
  - `manual_check.py` **no estaba en ningún Makefile**, y encima medía la copia
    vieja del manual (había tres `MANUAL_V2_ES.md` distintos, uno con 27 firmas
    cuando la tabla tiene 28). Ahora mide la que genera `build.js`, exige que
    no exista una segunda copia fuera de `manual/`, y lleva trinquete: 171
    cadenas sin mencionar de 559, y si suben, rojo.
- **Banco `cr7`** (`sim/cr7_test.c`): el alfabeto de siete bits del CCIR 476,
  medido por el motor de la radio. El par que importa es CCIR contra bits al
  azar **con el mismo salto y la misma velocidad** —78% contra 31%—, que es lo
  único que no se puede separar mirando el espectro. Exige 20 puntos de margen
  entre el peor que sí y el mejor que no.
- **Banco `basura`** (`sim/aire_basura.c`): siete clases de señal hostil
  —ruido, al límite, cuadrada a ±1, silencio, nivel continuo, FSK enloquecida y
  ráfagas— por la **misma puerta** que el audio de la radio, en los quince
  decodificadores, con `-fsanitize=address,undefined`. Los bloques van de
  tamaño aleatorio entre 1 y 512, porque la radio llama siempre con el mismo y
  un fallo que dependa de dónde se parta el bloque no sale nunca en uso normal.
  Barrido profundo del 08/10/2026: **3.780 millones de muestras, 87 horas de
  audio, ni una escritura fuera de sitio ni un comportamiento indefinido.** En
  `comprueba` va con 20 semillas; el profundo se repite con
  `make basura FIRMWARE=... SEMILLAS=500`.

### Dos bancos que mintieron en verde el mismo día

Vale la pena dejarlo escrito, porque es un fallo de banco y no de firmware:

- `identancho` no llevaba `ident.h` de prerequisito. Al bajar el tope para
  comprobar que la prueba fallaba, `make` no rehízo nada y contestó el binario
  viejo: **todo correcto**.
- `sin_llamar.py` no quitaba los comentarios de los `.c`. Al comentar la
  llamada para comprobar que la cazaba, la llamada comentada contó como
  llamada: **todo correcto** otra vez.

Los dos están comprobados ahora **en los dos sentidos**: que pasan con el
código bueno y que fallan con el malo. Un banco que no se ha visto fallar no
se ha probado.

## Cómo se hace el `BD.BIN` (`tools/`)

`BD.BIN` son las dos bases de datos en un solo fichero: los modelos de avión
para HFDL y las emisoras de onda corta y media. Se copia al disco USB de la
radio y se carga desde Información; después se puede borrar del disco.

```sh
cd tools/datos

# 1. las emisoras. --qth ordena cada frecuencia por cercanía al oyente
python3 ../emisoras_pack.py eibi.txt aoki.txt EMISORAS.BIN 2 --qth IN80

# 2. los modelos de avión, del CSV de OpenSky
python3 ../icao24.py aircraft-database-complete-2025-08.csv ICAO24.BIN 827392

# 3. y se juntan
python3 ../datos_pack.py BD.BIN ICAO24.BIN EMISORAS.BIN
```

- `tools/datos/` trae las dos listas de origen y un `LEEME.md` con de dónde se
  bajan, las temporadas y por qué el `2` del final (es cuánto hay que restarle
  al reloj de la radio para tener UTC: **2 en A26, 1 en B26**).
- `--qth` admite cuadrícula Maidenhead o `lat,lon`. Sin él, el fichero sale
  byte a byte como siempre. También hay `--radio 3000` y `--pais HNG,D,I`
  para hacerse una base pequeña y de la zona.
- El sitio de cada emisora sale de `tools/paises_xy.csv` (129 códigos ITU) y
  `tools/sitios_xy.csv` (centros emisores, que mandan sobre el país). Son CSV
  normales y están para editarlos.
- **`BD.BIN` no puede pasar de 1 MB**, ponga el chip de flash que se ponga: la
  zona de datos es siempre el último mega, y una flash más grande sólo agranda
  el disco del usuario. `datos_pack.py` se niega y dice qué tope pedirle a
  `icao24.py`.
- Lo comprueba `tools/emisoras_check.py`, en `make comprueba` como `emispack`.

## El manual de uso (`manual/`)

El manual de uso -las secciones 6 a 20, las que empiezan cuando la radio ya
arranca- **se escribe una sola vez**, en `manual/build.js`, y de ahí salen los
dos formatos:

```
node build.js        ->  DeepSDR_Reload_Manual_de_uso.docx   (24 páginas)
node build.js md     ->  MANUAL_V2_ES.md                     (docs/)
```

Los dos llevaban el mismo texto escrito por separado hasta el 06/10/2026, que
es la misma clase de fallo que la tabla de modos duplicada y la lista de
paletas copiada a mano: dos copias de lo mismo son dos copias que un día dejan
de coincidir. El `.md` lleva escrita la nota de que está generado.

Las capturas van en `manual/img/`, numeradas por el orden en que salen.

`tools/manual_check.py` dice qué cadenas de la radio siguen sin nombrarse.

## Cómo se entrega

- `C:\Users\PC\source\deepsdr-reload\update.bin` ← el firmware (raíz, donde
  lo escribe el `make`).
- `C:\Users\PC\source\deepsdr-reload\boot\customboot.bin` ← el cargador,
  **donde lo escribe el `make`**, igual que el `update.bin`.

  *Aqui ponia `rescate\customboot.bin` hasta el 08/10/2026, y era una trampa:*
  `rescate\` es la copia CONGELADA de rescate, la que se sabe buena, y por eso
  no se actualiza nunca. Siguiendo esta linea al pie se flashea un cargador de
  hace dos versiones. Hay tres copias del fichero -`boot\` (la que sale de
  compilar), la raiz (la comoda para arrastrar) y `rescate\` (la congelada)-,
  que es la misma enfermedad de los tres manuales con un numero mas bajo. La
  que vale es la de `boot\`; la de la raiz se pone al dia al entregar.
- **El orden importa**: primero el cargador por DFU, después el `update.bin`.
  Entre los dos pasos la radio se queda en la pantalla de actualización
  esperando el fichero, y eso es lo normal.
- **Nunca por el chat**: Windows renombra a `update-1.bin` y el cargador busca
  exactamente `UPDATE.BIN`. Un guión y un fallo silencioso.
- Después de escribir, **releer y comparar el sha256** con el de la compilación.

### `update4.bin` no se tira

En la raíz hay además un `update4.bin`, de una compilación anterior. **No es
basura y no se borra**: *por el dueño, 05/10/2026,* "el update4.bin no se tira
porque se necesita para pasar de la version vieja a la nueva".

El "4" es del **gestor de fábrica**, que abre `update4.bin` por FatFs; nuestro
cargador abre `UPDATE.BIN`. O sea que son dos nombres para dos programas
distintos, y una radio que todavía lleve el de fábrica **sólo** mira el
primero. Ése es el puente: sin él no hay forma de llegar desde el firmware
viejo hasta esta rama sin abrir la radio, y abrir la radio es justo lo que no
se hace aquí.

Por eso parece atrasado —lo está, es de antes— y por eso tiene que seguir
estándolo. Lo que no hay que hacer es **grabar ése** en una radio que ya está
en la rama `reload`: para el día a día el fichero es `update.bin`.

### `CONFIG.CSV` tampoco

*Por el dueño, 08/10/2026:* "config.csv y update4.bin tienen que estar
siempre". Así que los dos se quedan en la raíz y **no se proponen para
borrar**.

Y queda escrito aquí por un motivo concreto: ese mismo día le pasé una lista
de ficheros a borrar en la que iban los dos, **sin haber leído este README**,
que ya decía desde el 05/10 que el `update4.bin` no se tira. Una regla que
está escrita y que aun así se incumple es una regla que hay que poner donde se
mire, y el sitio donde se mira al limpiar la carpeta es éste.

## Estado

```
  VERSION V4.03          CARGADOR v2.15
  update.bin             customboot.bin          BD.BIN
  ZONA ALTA  733.184 de 1.040.384   libre 311.296  (101.432 aviones)
  DISCO USB  capacidad del chip - 1 MB  (hoy 7 MB con el W25Q64 soldado)
  FLASH   397.484 de 475.136   libre  77.652   <- UNA region, sin desvan
  CARGADOR 41.172 de  49.152   libre   7.980
  RAM     185.696 de 196.608   libre  10.912   <- .bss + .data, 94,4%
  TCM      59.476 de  65.536   libre   6.060   <- y eso ES la pila
  PILA      5.296 de  7.408    margen  2.112 (29%)  <- CON anidamiento
  avisos del compilador  0     comprobaciones  todas en verde
```

## Lo que queda pendiente

*Repasado contra el código y el diario el 05/10/2026. La lista anterior daba
por pendientes cinco cosas que ya estaban hechas, y una de ellas al revés del
todo.*

### Lo que ya NO está pendiente, aunque esta lista lo dijera

- **FT8 sí ha decodificado en antena.** Lo dice el diario del 25/09/2026:
  7.074,0 kHz, siete mensajes en dos tandas, uno a 7.116 km, con los desvíos
  anotados (153, 263, 266 y 350 Hz). La lista llevaba desde entonces diciendo
  que no, que es el peor error que puede tener un "pendiente": manda a repetir
  algo que ya salió.
- **La carga de audio con el Ruido encendido está medida**, y en la radio, no
  en el banco. De ahí salió la fila "Reparto del audio" y la conclusión de la
  V3.05: *el NR no era el problema*. Las dos transformadas de `nr_ss.c` se
  quedan como están.
- **El NAVTEX está probado en antena** desde la V3.25, y además arreglado con
  señal de verdad: antes no sacaba una letra del aire.
- **`ICAO24.BIN` ya no está vacío**: la zona alta lleva 101.432 aviones y 356
  tipos, cargados y comprobados en la radio.
- **RTTY y CW sí guardan sus ajustes**: desplazamiento, baudios e inversión del
  RTTY, y tono, velocidad y autoenganche del CW. Todo persiste.

### En antena, que es donde se decide

- **WEFAX, APRS y el notch no se han probado nunca con señal real.** Lo dicho
  de FT8 vale igual aquí: banco en verde no es lo mismo.
- **SSTV sí se ha probado en antena, el 06/10/2026, y no funcionaba.** Una
  emisión de 14.230 kHz no enganchó ni una vez en tres pasadas. De ahí salieron
  tres fallos reales —el panel de texto colándose por mitad de la foto, el
  líder de la cabecera reconocido por su frecuencia en vez de por estar quieto,
  y el corte de 10 ms medido contra una banda de cien hercios que un
  transitorio no alcanza— arreglados en las V3.68 y V3.69 y comprobados contra
  las grabaciones del dueño y contra señal sintética de −300 a +300 Hz de
  desvío. **Lo que falta es volver a verlo entrar con una emisión de verdad**:
  la estación dejó de emitir antes de poder confirmarlo.
- **La mayoría de las 28 firmas del Ident siguen sin comprobar en antena.**
  Van doce confirmadas, la última la FSK de 50 Bd y 200 Hz de 14.118 kHz,
  medida sobre dos grabaciones del dueño separadas 18 minutos. Antes de esa,
  el STANAG 4481 FSK de 5339 kHz medido dos
  veces por caminos que no comparten nada (la radio y numpy sobre una
  grabación, los cuatro números por debajo del 0,5%). Confirmadas: FT8 por
  cadencia, RTTY comercial de 50 Bd (el DWD) y la FSK naval de 850 Hz a 50 Bd
  (FUG en 13.418). Las que caerían con una grabación de treinta segundos:
  NAVTEX, WSPR, una portadora sola y PSK31.

### Fallos conocidos

- **ABIERTO: un usuario reporta reinicios aleatorios (V4.01).** Al volver no
  recupera el modo ni la configuración de pantalla, una de las veces estaba
  recibiendo FT8, y está **conectado al PC con el cable USB**. Lo que se sabe,
  del barrido del 08/10:
  - **Aquí un fallo de CPU cuelga, no reinicia.** No hay perro guardián en
    ningún sitio, el `HardFault_Handler` es un bucle infinito, no hay `WFI` ni
    standby, y el único `NVIC_SystemReset()` del árbol lo pide el dueño a mano
    desde la fila de DFU. Así que un reinicio sólo puede ser tensión, la
    patilla de reset, software, o un perro que nadie enciende.
  - **La aplicación no monta USB** (no hay un solo `usbd_` ni `msc` en
    `User/`): el disco de Windows lo presenta el cargador. Con el cable puesto
    la radio sólo toma corriente, así que ningún evento del bus puede
    reiniciarla.
  - **Los dos síntomas caben en una sola causa.** El autoguardado es retardado
    3 s y el guardado asíncrono **borra un bloque de 4 kB antes de escribir**:
    un reinicio dentro de ese borrado no pierde el último cambio, pierde el
    fichero entero y la radio arranca de fábrica. Y un borrado de bloque es el
    mayor pico de consumo del chip de flash. **Es una hipótesis, no un
    hallazgo.**
  - **Descartado por medida:** la pila (5.296 de 7.408, 29% de margen, y el
    único agujero del análisis que caía en FT8 mide 56 bytes) y las escrituras
    fuera de sitio en los decodificadores (3.780 millones de muestras con los
    detectores puestos, limpio).
  - **Instrumentado en V4.03:** fila **Reinicios** en Información, que lee
    `RCU_RSTSCK` —que este firmware no miraba nunca— y dice la causa con una
    letra, más el historial de los ocho últimos. Sin ese dato lo único que
    queda es adivinar. **Lo que falta es la letra.**
- **El botón "Borrar" del panel de CW desaparece** después de mucho rato
  decodificando. Descartados por inspección: `solo_chip()` no llega a su
  franja, el repintado por desplazamiento sí redibuja la cabecera, y
  `ui_digi_draw_fila()` no puede alcanzarla. Queda un cursor de renglón que
  puede desincronizarse.
- **El suelo de negro del cargador** sale unos 4 puntos por encima del
  original. Descartados con datos: reloj, temporización, las tres secuencias,
  pines de placa, PMU y EXMC.
- **La paleta `DEEPSDR` sigue fuera**: rompía el audio de WFM por un mecanismo
  que nunca llegué a entender, y prefiero no meter código cuyo fallo no
  entiendo.
- **Arreglado el 06/10/2026 (V3.72): el rechazo de imagen por debajo de 4,8
  MHz.** Lo reportó la incidencia 32 del repositorio con una captura
  inequívoca: emisora real en 4.780, réplica en 4.732, y el oscilador local en
  el centro de las dos. Causa: el 27/09 subí `LOWF_PHASE_DF_HZ` de 4 a 40 Hz
  para acortar de 62,5 a 6,25 ms la maniobra que pone I y Q a 90 grados. Los
  extremos de esa ventana no los marca el cronómetro sino dos escrituras I2C,
  así que el error de ángulo va con `df · Δt`: de 1,44 grados por milisegundo
  a 14,4, o sea de unos 38 dB de rechazo a unos 18. El comentario que dejé
  entonces decía que no estaba probado en aire y nombraba este síntoma exacto
  como lo que había que vigilar. **Un comentario que dice "esto no está
  probado" no es una prueba.**

### Decodificadores, por orden de lo que dan

- **Los ~2 dB de PSK31 contra fldigi siguen sin explicación, y el culpable que
  se le achacaba está descartado.** Medido el 05/10/2026: el 50 % de las letras
  cae en **−7,97 dB en 2,5 kHz** (amplitud 0,258; el banco lo da en amplitud y
  la conversión es 10·log10(a²·2,4)). A fldigi se le atribuyen −10 dB, pero en
  **3 kHz**, que pasados a 2,5 kHz son −9,2: la diferencia real son **~1,2 dB**,
  no tres, y encima los criterios no son el mismo ("50 % de las letras" aquí,
  "copia legible" allí).

  Lo que esta lista decía —que la culpa era el filtro adaptado y que era la
  mejora de mejor relación coste/beneficio— **ya se había medido el 30/09 y es
  que no**: el coseno de 64, que es el adaptado de verdad, gana **0,12 dB** por
  64 multiplicaciones por muestra y 256 bytes de RAM. Está escrito con las
  cuatro tandas en la cabecera de `psk31.c`. Donde sí hay sitio es en el
  **rechazo al vecino**: aguanta uno de hasta **+4,74 dB** y en 14.070 hay una
  emisión cada 50 Hz.
- Mensajes de **tipo 2 y 3 de WSPR**, y el **texto ACARS de HFDL en crudo**.
- Los **giros 6 y 7 de HFDL** no caben en este chip.
- **Separar SSTV de WEFAX por el periodo de línea**: hacen falta 500 ms y la
  autocorrelación del Ident llega a 256.
- **AIS y JTTY los decodifica la radio pero el Ident no los nombra**: les falta
  su fila en `k_firmas[]`, y una fila sólo entra con su número medido.
- **El cargador (`boot/`) sigue sólo en castellano.** Arranca antes que los
  ajustes y no tiene dónde leer el idioma, así que `tools/idioma_check.py` no
  lo mira. Que no salga ahí no quiere decir que esté bien: el que abrió el
  issue del volumen escribe en inglés y lo que lee es *"no se reconoce"*.

### Herramientas

- **`tools/sitios_xy.csv` está a medias a propósito**: 46 centros emisores de
  los 824 que aparecen en las listas. Añadir uno es una línea, y es justo lo
  que mejora el orden por cercanía de una zona concreta.
- Renombrar `Makefile.nuevo` a `Makefile`, en la raíz y en `boot/`. No es
  manía: el puente que escribe en el disco se niega a crear un fichero que se
  llame exactamente `Makefile`.
