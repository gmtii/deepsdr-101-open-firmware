# DEEPSDR 101 — qué se ha cambiado

Firmware del DEEPSDR 101 (GD32F450VET6) con la interfaz rehecha, modos nuevos,
cargador de arranque propio y un simulador que compila **el código de verdad**
del firmware en el PC.

Esto es la lista corta. El porqué de cada cosa, con las medidas y los fallos
que hubo por el camino, está en **`CAMBIOS.md`** (6.600 líneas, ordenado por
fecha). Versión actual **V2.44**; cargador **v1.1**.

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
- Se unificaron los datos en **un solo fichero** `DATOS.BIN` con directorio, en
  vez de cinco repartos escritos a mano.
- Se añadió soporte de `DATOS.BIN` **troceado** por el FAT: se sigue la cadena,
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

## Cargador de arranque propio (`boot/`, v1.1)

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

## Rendimiento y memoria

- Se descubrió que el **muro de los 256 kB no era un muro**: el gestor de
  arranque original no comprueba lo que yo creía, y eso abrió **64 kB** más.
  Con ese sitio entró WSPR esa misma noche.
- Se añadió el "desván" (`.arriba`, 0x08060008, 65.520 bytes) para los datos
  que no son código.
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
- Hay **111 bancos**, de los que más de 80 corren en `make comprueba` sin placa.
- La regla del proyecto: **los bancos escriben la regla aparte**. Si el banco
  llamara a la función del firmware comprobaría que hace lo que hace, no lo que
  debe.
- Se añadieron `tools/pila.py` (pila), `tools/firma.py` (firma del update),
  `tools/gordos.py` (quién ocupa), `tools/panel_check.py` y `tools/icao24.py`.
- Se añadió `avisos.mk`: el firmware compila con **cero avisos**.
- Se arreglaron, en distintas tandas, bancos que llevaban días o semanas en
  verde sin comprobar nada.

## Cómo se entrega

- `C:\Users\PC\source\deepsdr\update4.bin` ← el firmware (raíz, donde lo
  escribe el `make`).
- `C:\Users\PC\source\deepsdr\rescate\boot_nuestro.bin` ← el cargador.

## Estado

```
  VERSION V2.44          CARGADOR v1.1
  FLASH   258.460 de 262.144   libre 3.684    <- region baja
  DESVAN   59.580 de  65.520   libre 5.940
  RAM     192.780 de 196.608   libre 3.828
  TCM      59.476 de  65.536   libre 6.060    <- y eso ES la pila
  PILA      5.464 peor caso    margen  592
  avisos del compilador  0     comprobaciones  todas en verde
```

## Lo que queda pendiente

- Eliminar el desván: rama nueva con cargador propio y mapa de flash contiguo
  (la aplicación bajaría a 0x08010000 y el tope de carga subiría a 0x60000).
- El suelo de negro del cargador nuestro sale ~4 puntos por encima del
  original. Descartados con datos: reloj, temporización, las tres secuencias,
  pines de placa, PMU y EXMC.
- Medir la carga de audio con el botón de Ruido apagado y encendido, que es lo
  que decide si hay que reescribir las dos transformadas de `nr_ss.c`.
- Nada de NAVTEX, WEFAX, SSTV, APRS ni el notch se ha probado en antena.
- El filtro adaptado de PSK31 va 3 dB por detrás de fldigi: es la mejora con
  mejor relación entre lo que cuesta y lo que da.
- Mensajes de tipo 2 y 3 de WSPR, texto ACARS de HFDL en crudo, giros 6 y 7 de
  HFDL (no caben en este chip), y el `ICAO24.BIN` montado pero vacío.

