# Novedades · What's new

Lo que cambia en cada versión, en corto y en dos idiomas. El porqué, las
medidas y los fallos que hubo por el camino están en **`CAMBIOS.md`**, que va
por fecha, no se corta en los detalles y no se publica.

What changes in each release, briefly and in two languages. The reasoning, the
measurements and the bugs found along the way are in **`CAMBIOS.md`**, which is
ordered by date, does not spare the detail, and is not published.

> Una entrada por versión, la más nueva arriba. La versión de arriba tiene que
> ser la misma que `CONFIG_FW_VERSION` de `User/config.h`, y eso lo comprueba
> `tools/novedades_check.py` antes de entregar nada.
>
> One entry per release, newest first. The top version must match
> `CONFIG_FW_VERSION` in `User/config.h`; `tools/novedades_check.py` enforces
> that before anything ships.

---

## V5.08 · 2026-10-19

### Qué cambia

- **El atenuador de entrada sube a la barra de arriba.** La chapa que ponía
  **NR** ahora pone **ATT**, con su valor al lado y en color: `ATT OFF`,
  `ATT -06`, `ATT -12`. Las tres de tres caracteres y la chapa con ancho
  reservado, así que al cambiar de estado no se mueve nada de sitio. Se toca y va pasando por los tres, igual que la
  casilla de Ajustes → Radio, con el silencio de transición y el reajuste
  del borrador de ruido incluidos. El reductor de ruido sigue donde
  siempre: la casilla **Ruido** de la barra de abajo y su ajuste.
- **La sobrecarga avisa en la pastilla del mando.** Antes tapaba la lectura
  del S-metro entera, así que para enterarte de que la entrada se satura
  perdías justo el dato que estabas mirando. Ahora se pone roja la pastilla
  del centro de la franja —la que suele poner «Sintonía»— con la palabra
  entera. Mientras dura, no dice qué mueve el mando; en cuanto el control
  automático de radiofrecuencia se recupera, vuelve sola.
- **Fuera el cartel «AGC activo».** Ocupaba el sitio del dBm cuando el
  número no vale, y como el AGC está puesto casi siempre, el cartel estaba
  casi siempre: un aviso permanente deja de leerse. Ahora el hueco se queda
  vacío, que ya dice lo mismo, y la chapa **AGC** de la derecha sigue
  diciendo cuál tienes puesto.
- **La barra del S-metro suelta cuarenta píxeles** y la lectura y la pastilla
  se corren a la izquierda, que es lo que hace sitio para el valor del
  atenuador sin que ninguna chapa se caiga ni el nombre del ajuste
  enganchado se recorte más que antes: «Sintonía 1 kHz» sigue saliendo
  entero, igual que en la V5.06.

### What's new

- **The input attenuator moves up to the top strip.** The chip that read
  **NR** now reads **ATT**, with its value beside it and in colour:
  `ATT OFF`, `ATT -06`, `ATT -12`. All three are three characters wide and
  the chip reserves a fixed width, so nothing shifts when the state changes. Tapping it steps through the three, exactly
  like the Settings → Radio cell, transition mute and noise-blanker reset
  included. Noise reduction stays where it was: the **Noise** button on the
  bottom bar and its own setting.
- **Overload now warns on the knob pill.** It used to cover the whole S-meter
  reading, so finding out the input is clipping cost you the very number you
  were looking at. Now the pill in the middle of the strip — the one that
  usually reads "Tuning" — turns red with the whole word on it. While it
  lasts it does not say what the knob does; as soon as the RF AGC recovers,
  it comes back by itself.
- **The "AGC active" label is gone.** It sat where the dBm goes when the
  number means nothing, and since the AGC is on almost always, so was the
  label: a permanent warning stops being read. The slot is now simply empty,
  which says the same thing, and the **AGC** chip on the right still says
  which profile is set.
- **The S-meter bar gives up forty pixels** and the reading and the pill
  shift left, which is what makes room for the attenuator's value without
  dropping a chip or truncating the attached setting's name any more than
  before: «Tuning 1 kHz» still shows in full, just as in V5.06.

---

## V5.06 · 2026-10-19

### Qué cambia

- **En PKT300 el espectro se quedaba sin moverse.** En los modos digitales el
  espectro de radiofrecuencia deja su sitio al osciloscopio de sintonía, que
  es lo que enseña dónde caen los dos tonos. Pero a ese osciloscopio hay que
  darle audio, y la lista de quién se lo da estaba escrita a mano y no
  nombraba a PKT300: se quedaba sin una sola muestra, no llegaba a pintar
  nunca, y en pantalla quedaba congelado lo último que hubiera dibujado el
  espectro. El decodificador funcionaba perfectamente. Es el mismo fallo que
  el del APRS de hace unos días, una rama más allá: aquél era el camino de
  FM y éste el de banda lateral.
- **Y las dos rayas de PKT300 estaban donde no hay nada.** El osciloscopio
  pintaba siempre los tonos del APRS -1.200 y 2.200 Hz-, que en paquete de
  onda corta son 1.600 y 1.800. Ahora los da el propio decodificador, así
  que son los mismos con los que está afinado.
- **En WEFAX el estado salía recortado:** «Esperando el tono de arranque» se
  leía «ono de arranque». La chapa de estado guardaba sitio para un botón
  que en esa cabecera no existe, y lo que no cabía lo quitaba por delante,
  que es justo donde está la palabra que dice en qué fase va. Afecta igual a
  SSTV y a Feld-Hell.
- **Y esa misma cabecera podía salir con un botón de otro modo.** WEFAX,
  SSTV y Feld-Hell se arman la cabecera ellos, y los campos que no tocan se
  quedaban con lo que había dejado el modo anterior: viniendo de FT8 o de
  JTTY aparecía el botón «Mapa» y la chapa se encogía a su ancho reservado.
- **En STANAG e IDENT la radio se comía toques.** No era el táctil: era cada
  cuánto se miraba. Y el problema no estaba en la vuelta del bucle sino
  **dentro del repintado**: un panel digital son 253.940 accesos al bus de la
  pantalla, y mientras duran, el táctil no existía. Ahora se mira **cada 8 ms
  pase lo que pase**, desde dentro del propio dibujo —entre banda y banda, y
  justo mientras el DMA empuja píxeles y la CPU no tiene nada que hacer—.
  Medido con el código de verdad: antes se perdían 15 de cada 20 toques
  cortos; ahora salen todos, hasta los de 40 ms.
- **Y la firmeza del táctil era un tiempo disfrazado de cuenta de lecturas.**
  Lo que separa un toque de un roce siempre fue que el dedo DURE; lo que
  había escrito era un número de lecturas que daba ese tiempo al ritmo al que
  corría el bucle. Por eso la radio se volvía sorda cuando el bucle se
  alargaba. Ahora son 20 ms de dedo puesto por confirmación, y mirar más
  deprisa o más despacio ya no cambia lo sensible que es.
- **El identificador cuesta la mitad: 2,1 veces más rápido**, con los nueve
  aciertos de referencia idénticos. Y de paso se quitó un sesgo que tenía la
  medida de velocidad hacia los símbolos cortos.
- **Las dos transformadas —la del identificador y la del espectro— calculan
  el giro una vez por columna** en vez de una por mariposa: cinco veces y
  media menos. Mismo resultado bit a bit.
- **2.268 bytes de RAM** recuperados del guardado de imágenes.
- **El panel de los modos digitales sólo repinta lo que ha cambiado.** Antes
  repintaba los 800×317 enteros siempre —253.940 accesos al bus de la
  pantalla, varias veces por segundo—, incluidos 125 px de fondo vacío debajo
  del último renglón. Ahora: un renglón que cambia cuesta el **6 %**, la chapa
  el **5 %**, un botón pulsado el **1 %**, y si no cambia nada no se toca el
  bus ni una vez. Comprobado píxel a píxel contra el repintado entero.
- **En el mapa, la ficha de un avión ya dice su modelo y su OACI.** Antes
  ponía sólo el vuelo —«CCA845 · 828 km · AZ 56»— mientras que la lista del
  mismo modo sí enseñaba las dos cosas. Ahora: «CCA845 · 781F2E A359 Airbus
  A350-941 · 828 km · AZ 56».
- **Y al dejar de repintar de más salieron dos cosas que eso tapaba:** en
  STANAG e IDENT, cerrar un menú encima del panel no lo repintaba (la lista
  que lo decide no nombraba esos dos modos), y el cartel de la captura de
  pantalla tampoco se borraba del panel al quitarse.

### What's new

- **In PKT300 the spectrum stopped moving.** In digital modes the RF spectrum
  gives up its place to the tuning scope, which is what shows where the two
  tones land. That scope has to be fed audio, and the list of who feeds it was
  written by hand and did not name PKT300: it never received a single sample,
  never painted, and the screen kept whatever the spectrum had last drawn. The
  decoder was working perfectly. Same bug as the APRS one a few days ago, one
  branch further along: that was the FM path, this is the sideband one.
- **And PKT300's two tone markers pointed at nothing.** The scope always drew
  the APRS tones -1200 and 2200 Hz- which in HF packet are 1600 and 1800. The
  decoder now supplies them, so they are the ones it is actually tuned to.
- **In WEFAX the state was being clipped:** "Waiting for the start tone" read
  as "start tone". The status chip was reserving room for a button that
  header does not have, and it drops what does not fit from the FRONT, which
  is exactly where the word naming the phase lives. Same for SSTV and
  Feld-Hell.
- **And that header could show a button belonging to another mode.** WEFAX,
  SSTV and Feld-Hell build their own header, and the fields they do not touch
  kept whatever the previous mode left: coming from FT8 or JTTY the "Map"
  button appeared and the chip shrank to its reserved width.
- **In STANAG and IDENT the radio dropped taps.** Not the touch panel: how
  often it was sampled. And the problem was not the loop but **inside the
  repaint**: one digital panel is 253,940 display-bus writes, and while those
  run the touch did not exist. It is now sampled **every 8 ms no matter
  what**, from inside the drawing itself — between bands, and right while DMA
  is pushing pixels and the CPU has nothing to do. Measured against the real
  code: 15 in 20 quick taps used to be lost; now every tap lands, down to
  40 ms.
- **And touch firmness was a time disguised as a count of readings.** What
  tells a tap from a brush has always been that the finger LASTS; what was
  written was a number of readings that happened to equal that time at the
  rate the loop ran. That is why the radio went deaf when the loop got
  longer. It is now 20 ms of contact per confirmation, and sampling faster or
  slower no longer changes how sensitive it is.
- **The identifier costs half as much: 2.1x faster**, with the nine reference
  scores identical. It also removed a bias the symbol-rate measurement had
  toward short symbols.
- **Both FFTs — the identifier's and the spectrum's — compute the twiddle
  once per column** instead of once per butterfly: five and a half times
  fewer. Bit-identical results.
- **2,268 bytes of RAM** recovered from the image-saving path.
- **The digital-mode panel now repaints only what changed.** It used to
  repaint the whole 800×317 every time — 253,940 display-bus writes, several
  times a second — including 125 px of empty background below the last row.
  Now: one changed row costs **6 %**, the status chip **5 %**, a pressed
  button **1 %**, and when nothing changes the bus is not touched at all.
  Verified pixel for pixel against the full repaint.
- **On the map, an aircraft's label now shows its model and ICAO address.**
  It used to show only the flight — "CCA845 · 828 km · AZ 56" — while the list
  in the same mode showed both. Now: "CCA845 · 781F2E A359 Airbus A350-941 ·
  828 km · AZ 56".
- **And dropping the waste exposed two things it had been hiding:** in STANAG
  and IDENT, closing a menu over the panel did not repaint it (the list that
  decides did not name those two modes), and the screenshot notice was not
  cleared from the panel either.

---

## V5.05 · 2026-10-18

### Qué cambia

- **Con un chip de flash mas pequeño que 2 MB, cargar BD.BIN se llevaba el
  disco por delante.** `spi_flash_capacidad()` cruza dos fuentes y se queda
  con la pequeña, que es lo correcto; pero detras tenia un suelo de 2 MB que
  se aplicaba a TODA respuesta, no solo a la que no habia podido medirse. Un
  chip de 1 MB -uno mas pequeño soldado, o un clon que dice la verdad sobre
  un tamaño menor- se anunciaba como de 2, y entonces `za_base()` cae en
  `0x101000`, que en ese chip da la vuelta y aterriza en `0x001000`: encima
  de la primera copia de la FAT. Las dos guardias de `datos_arranca()`
  comparan contra ese mismo numero inflado, asi que las dos pasaban, y la
  copia escribia 204.076 bytes sobre la FAT, el directorio raiz y los datos.
  El sector de arranque sobrevive, asi que Windows sigue montando el volumen
  y lo ve destrozado; y como el fichero que se esta leyendo se machaca a si
  mismo por el camino, lo copiado no cuadra y la pantalla da el error
  DESPUES de haber roto el disco. El mapa, mientras tanto, parecia ir
  perfecto: son 8 kB que dan la vuelta a la zona de datos y se verifican
  leyendo de esa misma direccion. Ahora el suelo solo se aplica cuando
  ninguna fuente ha dicho nada, y un chip asi se anuncia como lo que es: el
  disco sale de cero bloques y la zona alta se niega. Medido en
  `sim/chip_chico.c`, que hace el ritual entero y cuenta los bytes del disco
  que cambian.

- **Y el cartel de BD.BIN decia la averia equivocada.** Cuando la carga se
  niega porque la zona alta no cabe hay DOS motivos que no se parecen en
  nada: que el chip sea mas pequeño de lo que hace falta -problema del
  equipo- o que el VOLUMEN diga medir mas que el disco, que es un formateo
  viejo y se arregla desde la propia radio en Informacion -> Formatear
  disco. El cartel culpaba al chip en los dos casos. Y el segundo se ve
  justo asi: el mapa se graba perfecto -grabarlo solo LEE el disco, y leer
  sigue permitido-, BD.BIN da un error, y el disco esta roto para Windows.
  Ahora el mensaje dice cual de las dos es.

- **STANAG e Ident juntos dejaban la radio congelada.** Los dos salen de la
  misma memoria prestada -los 60.304 bytes de la cascada- y el cerrojo que la
  reparte es un bit sin anidamiento: el que suelta, suelta para todos, y
  ademas borra los 60 kB. Seis de los siete que la cogen en cada vuelta
  preguntaban antes; el identificador no, y es justo el que el bucle llama
  pegado a STANAG. Con los dos andando se pisaban la memoria y el memset por
  vuelta arrastraba el bucle hasta que la radio dejaba de coger las
  pulsaciones. Dos lineas en `User/ident.c`, y `tools/prestamo_check.py` para
  que el octavo que llegue no se olvide.

- **Borrar un fichero del disco dejaba sucio el volumen.** Cuando Windows
  copia un fichero con nombre largo -o con mayusculas y minusculas
  mezcladas- pone delante de la entrada 8.3 una o varias entradas de nombre
  largo. El borrado marcaba la de 8.3 y dejaba aquellas colgando sin dueno,
  que es lo que `chkdsk` enseña como que el disco tiene errores. Ahora se
  lleva su grupo entero, comprobando la suma que las liga al nombre 8.3 para
  no tocar las de otro. Lo encontro `fsck.fat` juzgando el volumen que deja
  el banco nuevo `sim/bd_borra.c`.

- **El eje de nivel del espectro no era lo que decia su comentario.** Ni son
  dBFS ni son negativos: lo que se pinta es 10·log10 de la potencia de la FFT
  en cuentas al cuadrado, sin referencia. El rotulo ya estaba quitado; ahora
  el comentario dice lo que la radio hace. El S-metro si es dBFS y si da dBm,
  con su offset medido contra generador. Nada cambia en pantalla.

### What's new

- **With a flash chip smaller than 2 MB, loading BD.BIN destroyed the disk.**
  `spi_flash_capacidad()` crosses two sources and keeps the smaller one,
  which is right; but behind that sat a 2 MB floor applied to *every* answer,
  not only to the one that could not be measured. A 1 MB chip was announced
  as 2 MB, so `za_base()` landed at `0x101000`, which on that chip wraps
  around to `0x001000` — on top of the first copy of the FAT. Both guards in
  `datos_arranca()` compare against that same inflated number, so both
  passed, and the copy wrote 204,076 bytes over the FAT, the root directory
  and the data. The boot sector survives, so Windows still mounts the volume
  and sees it wrecked; and since the file being read overwrites itself along
  the way, the copy fails its checksum and the screen reports an error
  *after* the disk is already broken. The map looked perfect meanwhile: 8 KB
  that wrap into the data area and get verified by reading back that same
  address. The floor now applies only when no source said anything. Measured
  in `sim/chip_chico.c`.

- **And BD.BIN's message named the wrong fault.** When the load is refused
  because the upper area does not fit there are two very different reasons:
  the chip is smaller than needed —a hardware problem— or the *volume*
  claims to be larger than the disk, which is an old format and is fixed
  from the radio itself under Information -> Format disk. The message blamed
  the chip in both cases. The second one looks exactly like this from
  outside: the map flashes perfectly (flashing it only *reads* the disk, and
  reading is still allowed), BD.BIN errors out, and the disk is broken as
  far as Windows is concerned. The message now says which of the two it is.

- **STANAG and Ident together froze the radio.** Both borrow the same
  60,304-byte waterfall buffer, and the lock that hands it out is a single
  bit with no nesting: whoever releases it releases it for everyone, and
  releasing also wipes those 60 KB. Six of the seven pollers asked first; the
  identifier did not, and it is the one the main loop calls right next to
  STANAG. Two lines in `User/ident.c`, plus `tools/prestamo_check.py` so the
  next one cannot forget.

- **Deleting a file from the disk left the volume dirty.** Windows puts long
  file name entries in front of the 8.3 entry; only the 8.3 one was being
  marked, so the rest were left orphaned - which is what `chkdsk` reports as
  a damaged disk. The whole group is removed now, checked against the 8.3
  checksum so another file's entries are never touched. Found by `fsck.fat`
  judging the volume left by the new `sim/bd_borra.c` bench.

- **The spectrum level axis was not what its comment claimed.** It is neither
  dBFS nor negative: what is drawn is 10·log10 of FFT power in raw counts
  squared, with no reference. Comment fixed; nothing changes on screen.

---

## V5.04 · 2026-10-17

### Qué cambia

- **La aplicacion gana 22.036 bytes, y el cargador pasa a dos sectores.**
  A la aplicacion le quedaban 2.720 libres de 475.136. El cargador medía
  41.212 y 11.462 de esos eran UNA fuente: `font_ui_18b` entera, 117 glifos,
  de los que pinta 68. Recortada a los que usa y bajada a 11 px, el cargador
  cabe en dos sectores de flash en vez de tres, y el tercero -16.384 bytes-
  pasa a la aplicacion, que ahora empieza en `0x08008000`. La frontera vive
  en ocho sitios que no se pueden incluir entre ellos y los vigila
  `tools/mapa.py`; dos de los ocho los cazaron un `_Static_assert` y la
  tabla de sectores del cargador, no una revision.

  **El cargador y el `update.bin` se graban JUNTOS.** Un cargador v2.17 salta
  a `0x08008000` y una aplicacion anterior esta en `0x0800C000`: ninguna de
  las dos combinaciones cruzadas arranca.

- **La fuente de la lluvia del arranque se va a BD.BIN.** `font_rain_16` eran
  5.592 bytes de flash interna -45 katakana- para una pantalla que dura tres
  segundos. Ahora es una seccion mas de la zona alta y se carga al arrancar,
  en la misma memoria prestada que usan las columnas de la lluvia. Si el
  BD.BIN no la trae, la radio arranca igual con la pantalla fija, que es lo
  que ya hacia cuando no habia memoria que prestar. Se genera con
  `tools/fuente_pack.py` y la lee `User/splash_fuente.c`, que comprueba todo
  lo que va a usar como indice: `sim/splash_fuente_test.c` le mete dieciseis
  ficheros rotos distintos y los dieciseis se rechazan.

- **Dos diagnosticos de prueba que llevaban meses encendidos.**
  `TOUCH_EDGE_DEBUG` desde el 18/08 y `SPI_FLASH_PROBE_TEST` desde la puesta
  en marcha, los dos con un comentario encima que decia que habia que
  apagarlos. Escribian por un puerto serie que se compila apagado, asi que
  no se veian: el primero hacia dos lecturas del tactil cada 150 ms mientras
  hubiera un dedo en la pantalla, y el segundo volcaba el chip de flash y
  escaneaba la FAT en cada encendido. 364 bytes de flash, y trabajo en cada
  arranque y en cada toque. Lo vigila ahora `tools/interruptores_check.py`,
  que busca un `#define ... 1` cuyo propio comentario prometa apagarlo.

- **Diez temas claros nuevos, y el espectro que los acompaña.** De una
  petición: con un tema claro, el panel del espectro seguía siendo negro. Los
  diez nuevos —Lino, Marfil, Arena, Pergamino, Niebla, Almendra, Menta, Cielo,
  Trigo y Porcelana— pintan el espectro con los colores de su propio tema,
  llevan el mapa detrás y usan la cascada Humo, que es la única escala clara
  de las dieciséis. Los ocho de antes no se mueven: su espectro sigue siendo
  negro bit a bit, Oliva, Naranja y Chemistry incluidos, que son claros de
  interfaz pero se diseñaron con el espectro oscuro. Los diez van al final de
  la lista porque el tema se guarda por su posición: meter uno en medio le
  cambiaría el tema a quien ya tenga uno puesto.

- **Un banco que pinta los temas y mide los píxeles.** `sim/espectro_tema.c`
  renderiza el espectro con cada uno de los dieciocho temas y lee el marco:
  fondo, señal, trazo, rejilla y raya de la demodulación, cada uno contra el
  fondo que de verdad le sale. Comprueba las dos mitades: que los diez nuevos
  se vean y que los ocho de antes sigan pintando el mismo fondo. Mide píxeles
  y no constantes porque leer los `#define` contesta a otra pregunta.

- **El mapa vuelve a rellenar al acercarse, y sin rayas.** Desde el segundo
  aumento salían renglones horizontales del color que no tocaba. El relleno
  se pintaba por par‑impar sobre las costas de cada casilla, y las costas de
  un nivel troceado vienen **cortadas**: una polilínea abierta no tiene
  "dentro", así que el par‑impar no cerraba. Ahora cada casilla del `.map`
  trae sus propios **anillos cerrados**, recortados contra su rectángulo, y
  el par‑impar cierra dentro de ella por construcción: sin tabla de paridad,
  sin semilla y sin nada que pueda contradecirse. Medido sobre 8 mapas, 4
  escalas y 14 ventanas del mundo: **ninguna raya**. Hay que volver a grabar
  los mapas en la radio para verlo.
- **Un banco que cuenta rayas.** `sim/mapa_rayas.c` pinta los `.map` del
  disco y las busca píxel a píxel. El que había miraba sólo la tabla de serie
  y sólo a escala 1: aprobaba mientras la radio rayaba.
- **La ficha del mapa ya no se sale de su buffer.** Los números que pone
  —kilómetros, azimut, dB, veces, "hace N min"— se escribían sin mirar el
  tope, y con un indicativo y un localizador largos se iban **tres bytes por
  detrás** de un buffer de la pila. El banco no lo veía porque usaba un
  buffer de 96 bytes donde la radio tiene 80.
- **Y la línea de diagnóstico de HFDL tampoco.** Mismo fallo: los contadores
  de tramas se escribían sin tope sobre 58 bytes, y la guarda del bucle se
  comprobaba *después* de escribir.

### What's new

- **The application gains 22,036 bytes, and the bootloader drops to two sectors.**
  The application had 2,720 bytes left of 475,136. The bootloader was 41,212
  and 11,462 of those were ONE font: the whole of `font_ui_18b`, 117 glyphs,
  of which it draws 68. Trimmed to the ones it uses and dropped to 11 px, the
  bootloader fits in two flash sectors instead of three, and the third one
  -16,384 bytes- goes to the application, which now starts at `0x08008000`.
  That boundary lives in eight places that cannot include one another and
  `tools/mapa.py` watches them; two of the eight were caught by a
  `_Static_assert` and by the bootloader's sector table, not by review.

  **The bootloader and `update.bin` are flashed TOGETHER.** A v2.17 bootloader
  jumps to `0x08008000` and an earlier application sits at `0x0800C000`:
  neither crossed combination boots.

- **The boot rain's font moves to BD.BIN.** `font_rain_16` was 5,592 bytes of
  internal flash -45 katakana- for a screen that lasts three seconds. It is
  now one more section of the upper area, loaded at boot into the same
  borrowed memory the rain's columns use. If BD.BIN does not carry it, the
  radio boots just the same with the static screen, which is what it already
  did when there was no memory to borrow. `tools/fuente_pack.py` generates it
  and `User/splash_fuente.c` reads it, checking everything it will use as an
  index: `sim/splash_fuente_test.c` feeds it sixteen different broken files
  and all sixteen are rejected.

- **Two test switches that had been left on for months.**
  `TOUCH_EDGE_DEBUG` since 18/08 and `SPI_FLASH_PROBE_TEST` since bring-up,
  both with a comment above them saying they had to go back to 0. They wrote
  to a serial port that is compiled off, so nobody saw them: the first did
  two touch reads every 150 ms while a finger was on the screen, and the
  second dumped the flash chip and scanned the FAT on every boot. 364 bytes
  of flash, plus work on every boot and every tap. `tools/interruptores_check.py`
  now watches for a `#define ... 1` whose own comment promises to turn it off.

- **Ten new light themes, and a spectrum to match.** From a request: with a
  light theme, the spectrum panel was still black. The ten new ones — Linen,
  Ivory, Sand, Parchment, Mist, Almond, Mint, Sky, Wheat and Porcelain — draw
  the spectrum in their own theme's colours, take the map with them, and use
  the Smoke waterfall, the only light scale of the sixteen. The earlier eight
  do not move: their spectrum stays black bit for bit, Olive, Orange and
  Chemistry included, which are light in the interface but were designed with
  a dark spectrum. The ten go at the end of the list because the theme is
  stored by its position: slotting one into the middle would change the theme
  of anyone who already had one set.

- **A bench that draws the themes and measures the pixels.**
  `sim/espectro_tema.c` renders the spectrum with each of the eighteen themes
  and reads the framebuffer: background, signal, trace, grid and demodulation
  mark, each against the background that actually comes out. It checks both
  halves: that the ten new ones are visible, and that the earlier eight still
  draw the same background. It measures pixels rather than constants, because
  reading the `#define`s answers a different question.

- **The map fills again when you zoom in, with no stripes.** From the second
  zoom step on, horizontal lines of the wrong colour appeared. The fill was
  painted by even‑odd over each tile's coastlines, and a tiled level's
  coastlines are **cut**: an open polyline has no inside, so even‑odd never
  closed. Every tile in the `.map` now carries its own **closed rings**,
  clipped against its rectangle, and even‑odd closes inside it by
  construction: no parity table, no seed, nothing that can contradict
  anything. Measured over 8 maps, 4 scales and 14 windows of the world: **no
  stripes at all**. The maps must be written to the radio again to see it.
- **A bench that counts stripes.** `sim/mapa_rayas.c` draws the `.map` files
  from disk and looks for them pixel by pixel. The old one looked only at the
  built‑in table and only at scale 1: it passed while the radio striped.
- **The map's info card no longer runs past its buffer.** The numbers it
  prints — distance, azimuth, dB, count, "N min ago" — were written without
  checking the limit, and with a long callsign and locator they went **three
  bytes past** a stack buffer. The bench missed it because it used a 96‑byte
  buffer where the radio has 80.
- **Nor does HFDL's diagnostic line.** Same bug: frame counters written
  without a limit into 58 bytes, and the loop's guard checked *after*
  writing.

---

## V5.03 · 2026-10-16

### Qué cambia

- **Dos `update.bin` y dos `customboot.bin`, uno por chip.** El de siempre
  para el GD32F450VET6 de 512 kB y uno nuevo para el GD32F470VGT6 de 1024,
  que puede usar el doble de flash y 448 kB de RAM en vez de 192. Se piden
  con `make update` y `make update470`; los cargadores, con `make -C boot` y
  `make -C boot f470`.
- **El cargador del 470 no puede romper una radio de 512 kB.** Antes de
  borrar lee lo que el chip dice tener (la palabra de fábrica de
  `0x1FFF7A20`) y se queda con el menor de los dos topes. Si arrastras el
  que no era, usa media flash en vez de intentar borrar sectores que no
  existen y quedarse ahí en mitad de una grabación. El cargador enseña
  además cuál de los dos es: «v2.16 470» o «v2.16» a secas.
- **La celda «Micro» ya no miente.** Decía `GD32F450VET6` escrito a mano, y
  lo seguía diciendo en una radio con un 470 soldado. Ahora enseña la flash
  y la SRAM que declara el chip y, entre corchetes, para cuál de los dos se
  compiló el firmware que está corriendo. No deduce la familia: un F450 y un
  F470 existen con las mismas capacidades, así que del tamaño no se sigue el
  nombre.
- **Y el borrado llega de verdad hasta arriba.** La tabla desde la que el
  cargador borra vive en un sitio distinto del tope que acepta las imágenes,
  y se había quedado corta: el del 470 habría aceptado una imagen del doble
  y borrado la mitad, fallando no la primera vez sino la segunda. Ahora
  borra sólo los sectores que la imagen ocupa, un `_Static_assert` ata las
  dos cifras, y dos bancos las comparan.
- **Se acabaron las rayas del mapa al acercarse.** A partir del segundo
  aumento salían renglones horizontales del color que no tocaba. El relleno
  de los niveles de cerca se pinta por par-impar sobre costas **partidas por
  casillas**, y cada pieza cortada deja sus extremos clavados en el borde de
  su casilla: medido, tres cruces en un sitio donde la costa cruza una sola
  vez. Impar, así que la paridad se da la vuelta y todo lo que viene detrás
  sale del color contrario hasta el siguiente borde. De momento el relleno
  al acercarse queda apagado —el mapa se ve como antes de octubre, con las
  costas sobre el mar— y el de los dos primeros aumentos sigue igual. El
  arreglo de fondo, recortar los anillos contra cada casilla en vez de
  recortar las líneas, va aparte.
- **Y un banco nuevo que las cuenta.** `sim/mapa_rayas.c` pinta los `.map`
  del disco a cuatro escalas en seis ventanas del mundo y busca las rayas
  píxel a píxel. El banco que había miraba sólo la tabla de serie y sólo a
  escala 1: aprobaba mientras la radio rayaba.
- **`make comprueba` pasa los dieciséis bancos del firmware y los doscientos
  tres del simulador.** Estaban todos escritos y no había forma de correrlos
  de una; cuatro, además, necesitaban argumentos y llamados a secas sólo
  imprimían su ayuda.

### What's new

- **Two `update.bin` and two `customboot.bin`, one per chip.** The usual one
  for the 512 kB GD32F450VET6 and a new one for the 1024 kB GD32F470VGT6,
  which can use twice the flash and 448 kB of RAM instead of 192. Build them
  with `make update` and `make update470`; the loaders with `make -C boot`
  and `make -C boot f470`.
- **The 470 loader can no longer brick a 512 kB radio.** Before erasing it
  reads what the chip says it has (the factory word at `0x1FFF7A20`) and
  takes the lower of the two ceilings. Flash the wrong one and it uses half
  the flash instead of trying to erase sectors that do not exist and
  stalling mid-write. The loader also shows which one it is: "v2.16 470" or
  plain "v2.16".
- **The "Micro" cell no longer lies.** It read `GD32F450VET6`, hardcoded,
  and kept saying so on a radio with a 470 soldered in. It now shows the
  flash and SRAM the chip declares and, in brackets, which of the two builds
  is running. It does not infer the family: an F450 and an F470 both exist
  with the same capacities, so the size does not give you the name.
- **And the erase actually reaches the top.** The table the loader erases
  from lives somewhere other than the ceiling that accepts images, and it
  had been left short: the 470 build would have accepted a double-sized
  image and erased half of it, failing not the first time but the second.
  It now erases only the sectors the image occupies, a `_Static_assert` ties
  the two figures together, and two benches compare them.
- **No more stripes on the map when you zoom in.** From the second zoom step
  on, horizontal lines of the wrong colour appeared. The fill on the close-in
  levels is painted by even-odd over coastlines **split by tiles**, and every
  split piece leaves its ends pinned to its tile border: measured, three
  crossings at a spot where the coast crosses only once. Odd, so the parity
  flips and everything after it comes out the wrong colour until the next
  border. For now the close-in fill is off — the map looks as it did before
  October, coastlines over sea — and the first two zoom levels are unchanged.
  The real fix, clipping the rings against each tile instead of clipping the
  lines, is separate work.
- **And a new bench that counts them.** `sim/mapa_rayas.c` draws the `.map`
  files from disk at four scales in six windows of the world and looks for
  stripes pixel by pixel. The old bench looked only at the built-in table and
  only at scale 1: it passed while the radio striped.
- **`make comprueba` runs the firmware's sixteen benches and the
  simulator's two hundred and three.** They were all written and there was no
  way to run them together; four of them also needed arguments and, called
  bare, only printed their usage.

---

## V5.02 · 2026-10-15

### Qué cambia

- **Los carteles ya no bailan.** Con un clic de 1 kHz cambiaban la mitad de los
  nombres: el reparto partía *la pantalla* en trozos, y al moverse la pantalla
  se movían los trozos. Ahora la rejilla está anclada al **dial**, no a la
  pantalla: mover la sintonía sólo puede hacer que entre un nombre por un lado
  y salga otro por el otro.
- **Se acabó el trozo encima de la señal y el parpadeo.** Los nombres se
  pintaban con una pasada aparte que arrancaba en negro y le comía 36 píxeles
  al sombreado del filtro y a la rejilla. Ahora los pinta el mismo código que
  pinta la traza, en el mismo buffer.
- **Y por eso ya salen en las capturas de pantalla**, que antes iban por otro
  camino y no los llevaban.
- **Tres filas y letra más pequeña: trece carteles en vez de siete.** En una
  banda llena se nombran ahora diez emisoras donde antes salían seis. La fila
  de cada nombre sale de su frecuencia: **un cartel no cambia de fila mientras
  está en pantalla**, pase lo que pase con el dial.
- **Y ya no desaparece media fila** cuando la emisora sintonizada tiene nombre:
  su cartel se pinta el primero y borraba del reparto todo lo que tenía a su
  izquierda en esa fila.
- **En el espectro hay una sola regla y una sola excepción:** un cartel por
  casilla, en la fila de su casilla, y sólo cambia cuando su casilla entra o
  sale por un borde. La excepción es **la emisora que estás oyendo**: cuando el
  dial se posa encima de ella (a menos de 600 Hz, la misma distancia que usa el
  cartel de junto al reloj), su nombre toma el sitio de su casilla aunque le
  toque perderlo. Es la única cosa del espectro que depende del dial, cuesta un
  cartel —el vecino de su casilla, que vuelve en cuanto te apartas— y ocurre
  dentro del sombreado del filtro, que es donde estás mirando.
- **Y se acabó el baile al girar:** los carteles ya no se dibujan con la lista
  de hasta 120 ms antes, que es lo que hacía que un nombre pareciera cambiar de
  fila mientras movías y volviera a su sitio al parar.
- **Se repintan al momento.** Antes sólo se refrescaban cuando subía una señal
  hasta arriba, porque el espectro se salta las franjas que no han cambiado.
- **SAM-U y SAM-L ya se ven en el espectro.** La banda sombreada —la que te
  dice qué estás oyendo— salía simétrica en los dos, porque se decidía mirando
  el modo y SAM-U/SAM-L son un ajuste de SAM, no modos propios. Pintaba dos
  bandas laterales mientras la radio demodulaba una.
- **`JMP`: el mando salta de emisora en emisora.** Nueva pastilla al lado de
  `NCO`. Con ella puesta, cada clic del mando te lleva a la siguiente emisora
  **que esté emitiendo ahora** según `EMISORAS.BIN`, arriba o abajo según el
  sentido del giro, en vez de mover un paso de sintonía. Recorrer una banda de
  radiodifusión pasa de ser cientos de clics a unos pocos. Si no hay reloj
  fiable salta a todas; si no hay base, la pastilla no se enciende. Se guarda
  en `CONFIG.CSV` y arranca **apagada**. Girar rápido cuesta lo mismo que
  girar despacio: el trabajo de un giro está presupuestado entero, no por
  cada salto.
- **Los nombres de emisora se adaptan al tema.** En Oliva, Naranja y
  Chemistry no salían: la tinta de esos temas es negra —van sobre barras
  claras— y el panel del espectro es casi negro en todos. Ahora, cuando la
  tinta del tema no se vería ahí, el nombre toma el color de la traza de ese
  mismo tema, aclarado hasta que se lee. Los otros cinco temas no cambian.
- **La tierra se rellena también al acercarte.** Hasta ahora el mapa sólo
  salía con tierra y agua de distinto color al zoom 1; en cuanto te acercabas
  se quedaba todo del color del mar. Los mapas traen ahora un dato nuevo que
  le dice a la radio por dónde empezar a rellenar cada fila. **Hay que copiar
  los `.map` de nuevo al pendrive**: los de antes siguen valiendo, pero sin
  relleno al acercarte.
- **En HFDL, el contador `/n` ahora se puede leer.** Juntaba dos cosas
  opuestas: tramas rotas y tramas perfectamente buenas que no hablan de ningún
  avión (tráfico de tierra, que es lo normal). Ahora cuenta sólo lo raro: si
  ves un número ahí, hay algo que mirar.
- **Ya no pasa que el nombre salga junto al reloj y no en el espectro.** Le
  tocaba precisamente a las frecuencias redondas: 5.544 kHz es múltiplo exacto
  del paso de la rejilla, así que caía justo en el borde de su casilla —el peor
  sitio— y perdía contra cualquier vecina. Medido sobre la base entera, el 18 %
  de las emisoras en antena caen en ese borde.
- **La franja de nombres ocupa un solo hueco del espectro**: va del techo de la
  traza a la primera raya de la rejilla —la del valor de arriba— y ahí acaban
  las rayitas. Justo debajo arranca la barra del paso de banda.
- **El reparto ya no compite consigo mismo.** Un clic de 1 kHz cambia como
  mucho un cartel, y sólo cuando una casilla entra o sale por un borde.
- Los nombres largos ya no se quedan en el saludo: «Voice of Vietn» en vez de
  «Voice of».

### What's new

- **Labels stop dancing.** One 1 kHz click used to change half the names: the
  layout split *the screen* into slots, so moving the screen moved the slots.
  The grid is now anchored to the **dial**, not the screen: retuning can only
  bring one name in at one edge and drop one at the other.
- **No more leftover strip over the signal, and no more flicker.** The names
  were painted by a separate pass that started black and ate 36 pixels of the
  filter shading and the grid. The code that paints the trace now paints them,
  into the same buffer.
- **So they finally show up in screenshots**, which went through another path
  and never carried them.
- **Three rows and a smaller face: thirteen labels instead of seven.** On a
  busy band ten stations now get named where six did before. Each name's row
  comes from its frequency: **a label never swaps rows while it is on screen**,
  whatever you do with the dial.
- **And half a row no longer vanishes** when the tuned station has a name: its
  label is drawn first and used to wipe everything to its left on that row.
- **One rule on the spectrum and one exception:** one label per cell, on its
  cell's row, changing only when its cell enters or leaves at an edge. The
  exception is **the station you are listening to**: when the dial sits on it
  (within 600 Hz, the same distance the label next to the clock uses), its name
  takes its cell's slot even when it would otherwise lose it. It is the only
  thing on the spectrum that depends on the dial, it costs one label — its
  cell's neighbour, back the moment you tune away — and it happens inside the
  filter shading, which is where you are looking.
- **And no more shuffling while you turn:** labels are no longer drawn from a
  list up to 120 ms old, which is what made a name look like it changed rows
  while tuning and went back when you stopped.
- **They repaint immediately.** Until now they only refreshed when a signal rose
  to the top, because the spectrum skips strips that have not changed.
- **SAM-U and SAM-L now show up on the spectrum.** The shaded passband — the
  one that tells you what you are hearing — came out symmetric for both,
  because it was decided from the mode and SAM-U/SAM-L are a setting of SAM,
  not modes of their own. It drew two sidebands while the radio demodulated
  one.
- **`JMP`: the knob hops from station to station.** New chip next to `NCO`.
  With it on, each knob detent takes you to the next station **on air right
  now** according to `EMISORAS.BIN`, up or down with the direction of turn,
  instead of moving one tuning step. Sweeping a broadcast band goes from
  hundreds of clicks to a handful. With no trusted clock it hops to all of
  them; with no database the chip will not turn on. Saved in `CONFIG.CSV`,
  starts **off**. Spinning fast costs the same as spinning slowly: the
  work for one turn is budgeted as a whole, not per hop.
- **Station names follow the theme.** On Olive, Orange and Chemistry they
  did not show at all: those themes use black ink — it sits on light bars —
  and the spectrum panel is near-black on every theme. Now, when a theme's
  ink would not be visible there, the name takes that theme's own trace
  colour, lightened until it reads. The other five themes are unchanged.
- **Land is filled in when you zoom in too.** Until now the map only showed
  land and water in different colours at zoom 1; zoom in and everything went
  sea-coloured. The map files now carry a new piece of data telling the radio
  where to start filling each row. **Copy the `.map` files to the stick
  again**: the old ones still work, just without fill when zoomed in.
- **On HFDL, the `/n` counter can now be read.** It lumped two opposite things
  together: broken frames and perfectly good frames that are not about any
  aircraft (ground traffic, which is normal). It now counts only the odd ones:
  if you see a number there, something is worth looking at.
- **No more names that show next to the clock but not on the spectrum.** It hit
  round frequencies in particular: 5,544 kHz is an exact multiple of the grid
  step, so it fell right on its cell's boundary — the worst place — and lost to
  any neighbour. Measured over the whole database, 18% of on-air stations land
  on that boundary.
- **The name band takes a single grid gap**: from the top of the trace down to
  the first grid line — the top dB value — and the ticks end there. The
  passband bar starts right below it.
- **The layout no longer competes with itself.** One 1 kHz click changes at most
  one label, and only when a cell enters or leaves at an edge.
- Long names no longer stop at the greeting: "Voice of Vietn" instead of
  "Voice of".

---

## V5.01 · 2026-10-15

### Qué cambia

- **Los nombres sobre el espectro, bien repartidos.** En la V5.00 se le pedía
  a la base el doble de ventana de la que se ve y se cogían los primeros: la
  mitad de los nombres caía fuera de la pantalla, el lado derecho quedaba
  vacío y la emisora sintonizada se quedaba sin cartel. Ahora se pregunta sólo
  por lo que se ve y los carteles se reparten de borde a borde.
- **Siete en vez de seis, y todos distintos.** Una emisora que esté en tres
  canales pegados ya no gasta tres carteles; cuando sobran emisoras mandan los
  nombres que aún no están, y la repetida que se queda es la más cercana a la
  sintonía. Si aún quedan huecos, entonces sí entran las repetidas lejanas.
- **Los nombres largos se cortan por la palabra**, no por la mitad: «China
  National Radio 1» y no «China Nationa».

### What's new

- **Station names on the spectrum, properly spread out.** V5.00 asked the
  database for twice the window actually on screen and kept the first few, so
  half the names fell off-screen, the right-hand side was always empty and the
  tuned station had no label. It now asks only for what is visible and spreads
  the labels edge to edge.
- **Seven instead of six, and all different.** A station on three adjacent
  channels no longer eats three labels; when there are more stations than
  slots, names not yet shown win, and the repeat that survives is the one
  nearest the tuned frequency. Leftover slots then go to distant repeats.
- **Long names are cut at a word boundary**, not mid-word: "China National
  Radio 1" rather than "China Nationa".

---

## V5.00 · 2026-10-15

### Qué cambia

- **Los nombres de las emisoras, encima del espectro.** Sale de EMISORAS.BIN,
  cada nombre con una rayita hasta su frecuencia exacta, y se desplazan con el
  espectro al mover la sintonía. Hasta ahora el nombre sólo aparecía junto al
  reloj y sólo cuando estabas clavado en la frecuencia. Se enciende en
  **Ajustes → Pantalla → BD en espectro**, y nace apagado.
- Sólo salen **las que están en antena a esta hora**, y como máximo seis: en
  una banda llena hay más de treinta dentro de la pantalla, y amontonadas no se
  lee ninguna. Las que no caben se callan en vez de pintarse encima.
- Si no hay base cargada, la celda dice «Sin base» y no deja encenderlo.

### What's new

- **Station names on top of the spectrum.** Taken from EMISORAS.BIN, each with
  a tick down to its exact frequency, and they move with the spectrum as you
  retune. Until now the name only appeared next to the clock, and only when you
  were exactly on frequency. Switch it on at **Settings → Display → DB on
  spectrum**; it starts off.
- Only stations **on air at this hour** are shown, at most six: a crowded band
  has more than thirty on screen and piled up none of them can be read. The
  ones that do not fit stay silent rather than overprinting.
- With no database loaded the cell reads "No database" and cannot be switched on.

---

## V4.99 · 2026-10-15

### Qué cambia

- **Con el mapa en pantalla, el mando dejaba de hacer lo que decía la botonera.**
  Si lo tenías enganchado con el botón Func al ancho de banda, al squelch, al
  reductor de ruido, a la ganancia o a cualquier ajuste de la lista, girabas y
  lo único que pasaba era que el mapa hacía zoom. Sólo el volumen estaba a
  salvo. Ahora, si has enganchado el mando a algo, el mando es tuyo; el mapa se
  queda el giro únicamente cuando el mando está en su sitio por omisión, la
  sintonía. Los dos botones de zoom de la esquina no cambian.

### What's new

- **With the map on screen, the knob stopped doing what the button bar said.**
  If you had assigned it with the Func button to bandwidth, squelch, noise
  reduction, gain or any setting from the list, turning it only zoomed the map.
  Only volume was safe. Now, if you have assigned the knob to something, the
  knob is yours; the map takes the turn only when the knob is on its default
  target, tuning. The two zoom buttons in the corner are unchanged.

---

## V4.98 · 2026-10-14

### Qué cambia

- **El paso de adaptación del ecualizador de HFDL estaba a un paso de
  diverger.** Valía 0,1 porque es lo que usa dumphfdl, y eso nunca se había
  medido en este firmware: nuestro LMS no normaliza igual, así que el mismo
  número no es el mismo paso. Barrido sobre siete grabaciones reales: en 0,15 el
  ecualizador se va a cero y la radio queda muda; el óptimo es una meseta de
  0,035 a 0,05. Puesto en 0,04. Una grabación pasa de 9 a 12 tramas buenas, y
  otra gana dos renglones y dos coordenadas.

### What's new

- **HFDL's equalizer step size was one notch from diverging.** It was 0.1
  because that is what dumphfdl uses, and that had never been measured in this
  firmware: our LMS does not normalize the same way, so the same number is not
  the same step. Swept over seven real recordings: at 0.15 the equalizer
  collapses to zero and the radio goes deaf; the optimum is a plateau from 0.035
  to 0.05. Set to 0.04. One recording goes from 9 to 12 good frames, another
  gains two rows and two coordinates.

---

## V4.97 · 2026-10-14

### Qué cambia

- **El portero del entrenamiento de HFDL vuelve a estar conectado**, ahora con
  umbral 0,32. Estaba apagado desde que se midió que a 0,30 se comía ráfagas
  buenas, pero aquella medida se hizo cuando el buscador de preámbulo enganchaba
  catorce veces más ruido. Rehecho el barrido: de 0,30 a 0,35 hay una meseta que
  gana una ráfaga y un renglón, y por debajo y por encima se pierde. Cada
  segmento de ruido que ya no se abre deja de dejar la radio sorda entre 1,8 y
  4,2 segundos.

### What's new

- **HFDL's training-sequence gate is switched back on**, now at a 0.32
  threshold. It had been off since a measurement showed 0.30 was eating good
  bursts, but that measurement was taken when the preamble search was locking
  onto fourteen times more noise. Re-swept: 0.30 to 0.35 is a plateau that gains
  one burst and one row, and it loses either side of that. Every noise segment
  that no longer opens is 1.8 to 4.2 seconds the radio is no longer deaf.

---

## V4.96 · 2026-10-14

### Qué cambia

- **El contador de ráfagas de HFDL decía más del doble de las que hay.** La
  pantalla ponía `P46` contando los tonos de 249 ms que la norma pone delante
  de cada ranura, dando por hecho que detrás de cada tono viene una ráfaga.
  Medido sobre una grabación de 214 segundos: de 43 tonos, sólo 18 llevan
  preámbulo detrás. Ahora pone `P46/24` — tonos y, de ésos, los que sí traían
  ráfaga. En una grabación limpia los dos números coinciden (`P14/14`).
- **Los contadores de HFDL no se reiniciaban al entrar al modo.** Al salir y
  volver, la pantalla mezclaba los enganches de esta sesión con los pre-key, el
  CRC y los avisos de la anterior. Afecta a `P`, `D`, `/n`, `!` y a las
  fracciones de BPSK/4-PSK/8-PSK.
- **El banco de pruebas de aire real no recompilaba al cambiar una cabecera**, y
  por eso un barrido de umbrales daba ocho veces el mismo resultado.
- **Las coordenadas que venían dentro del logon y se tiraban enteras.** Un
  avión manda su posición al darse de alta en la estación, y ese mensaje —el
  más frecuente que hay en bajada— trae 55 bytes de los que el firmware sólo
  leía los 4 primeros. Medido sobre una grabación de 214 segundos: cinco
  posiciones perdidas (ETD123, QTR037, MS0241, VIR209, CPA382) sobre un total
  de cero decodificadas. Ahora salen las cinco.
- **La autoprueba de la demodulación blanda de 4-PSK** esperaba los valores de
  la decisión dura y llevaba un día en rojo sin que eso significara nada.
- **El banco del portón de HFDL** exigía que el ruido fuerte produjera enganches
  de preámbulo. Ahora se le mete una ráfaga de verdad.

### What's new

- **The HFDL burst counter was reporting more than twice the real number.** The
  screen showed `P46`, counting the 249 ms tones the standard puts ahead of each
  slot and assuming a burst follows every tone. Measured over a 214-second
  recording: of 43 tones, only 18 are followed by a preamble. It now reads
  `P46/24` — tones, and of those, the ones that did carry a burst. On a clean
  recording both numbers match (`P14/14`).
- **HFDL counters were not cleared when entering the mode.** Leaving and
  re-entering mixed this session's locks with the previous session's pre-key,
  CRC and warning counts. Affects `P`, `D`, `/n`, `!` and the BPSK/4-PSK/8-PSK
  fractions.
- **The real-air test bench did not rebuild when a header changed**, which made
  a threshold sweep return the same result eight times in a row.
- **The coordinates that came inside the logon and were thrown away.** An
  aircraft sends its position when it logs on to the ground station, and that
  message —the most frequent one on the downlink— carries 55 bytes of which the
  firmware read only the first 4. Measured over a 214-second recording: five
  positions lost (ETD123, QTR037, MS0241, VIR209, CPA382) against a total of
  zero decoded. All five now come out.
- **The 4-PSK soft-demodulation self-test** still expected the hard-decision
  values and had been red for a day without meaning anything.
- **The HFDL squelch bench** demanded that louder noise produce preamble locks.
  It is now fed a real burst.

---

## V4.84 · 2026-10-14

### Qué cambia

- **El giro absoluto de la constelación, que es lo que impedía decodificar
  4-PSK y 8-PSK.** La correlación contra la secuencia de entrenamiento usaba
  sólo la componente I y se quedaba con el signo: eso distingue 0 de π y
  nada más. En BPSK ésa es toda la ambigüedad y por eso BPSK funciona; en
  4-PSK sobra un giro de ±90° y en 8-PSK de ±45°, y si la constelación está
  girada un paso **todos** los símbolos dan bits equivocados. Ahora se
  correlan I y Q y el ángulo del resultado dice el giro completo. Medido en
  banco: los 4 giros de 4-PSK y los 8 de 8-PSK, 0 mal de 12; con la función
  devolviendo la identidad —lo que hacía el firmware— 10 mal de 12.

### What's new

- **The absolute constellation rotation, which was what blocked 4-PSK and
  8-PSK.** The correlation against the training sequence used only the I
  component and took its sign: that tells 0 from π and nothing else. In BPSK
  that is the whole ambiguity, which is why BPSK works; 4-PSK leaves ±90° and
  8-PSK ±45°, and one step of rotation makes **every** symbol carry the wrong
  bits. It now correlates I and Q and the angle of the result gives the full
  rotation. Measured on the bench: all 4 rotations of 4-PSK and all 8 of
  8-PSK, 0 wrong out of 12; with the function returning identity — what the
  firmware did — 10 wrong out of 12.

---

## V4.83 · 2026-10-14

### Qué cambia

- **El detector de fase deja de suponer BPSK.** Durante los datos, la
  corrección que corre en cada símbolo medía el error con `sign(I)·Q`, o sea
  contra una constelación de dos puntos. Con cuatro u ocho empuja la fase
  hacia un punto que no existe, símbolo tras símbolo. Ahora el error se mide
  contra el punto ideal más cercano de la constelación que toca, que el M1 ya
  dice al engancharse. **BPSK sale por el camino de antes, bit a bit**: ahí
  hay un 39 % que funciona y no se arriesga para arreglar un 0 %.

### What's new

- **The phase detector no longer assumes BPSK.** During data, the per-symbol
  correction measured its error as `sign(I)·Q`, i.e. against a two-point
  constellation. With four or eight points it pushes the phase towards a point
  that does not exist, symbol after symbol. The error is now measured against
  the nearest ideal point of the actual constellation, which M1 already states
  on lock. **BPSK goes through the old path, bit for bit**: there is a working
  39 % there and it is not risked to fix a 0 %.

---

## V4.82 · 2026-10-14

### Qué cambia

- **ACARS dentro de HFDL.** Las tramas que no daban posición resultaron ser
  de tipo `0xFF` —ACARS envuelto— y este firmware sólo entendía el `0xD1`.
  Ahora se descifran: matrícula, etiqueta, acuse y texto, con la paridad impar
  del bit 7 quitada. Si el texto trae coordenadas, entran en la tabla y en el
  mapa. El decodificador está verificado contra **dos tramas reales** del aire
  en 17.967 kHz, no contra un recuerdo.

- **El CRC, separado por velocidad de modulación**, de cabecera en la vista de
  bytes: `BPSK 14/36  4PSK 0/4  8PSK 0/2`. Es lo que permitió encontrar los
  dos fallos de arriba en vez de suponerlos.

- Y el volcado en crudo de cada trama de datos, debajo de la fila de su avión,
  que es de donde salió todo lo demás.

### What's new

- **ACARS inside HFDL.** The frames that produced no position turned out to be
  type `0xFF` — enveloped ACARS — and this firmware only understood `0xD1`.
  They are now decoded: registration, label, acknowledgement and text, with
  bit 7's odd parity stripped. If the text carries coordinates they go into the
  table and the map. The decoder is verified against **two real frames** off
  the air on 17.967 kHz, not against a memory.

- **CRC split by modulation speed**, as the header of the bytes view:
  `BPSK 14/36  4PSK 0/4  8PSK 0/2`. This is what made the two fixes above
  findable instead of guessable.

- And the raw dump of each data frame under its aircraft's row, which is where
  all of it came from.

---

## V4.81 · 2026-10-14

### Qué cambia

- **Se acabaron las rayas al arrastrar el mapa.** El relleno descartaba
  entera cualquier fila con más de 40 cruces de costa, y eso en pantalla es
  una raya horizontal del ancho del continente. Los 40 los elegí midiendo
  **una sola vista** —el mundo centrado— y escribiendo al lado que estaba
  medido. Barriendo el mundo entero en los zooms 1 y 2, 78 vistas contra un
  render de referencia: con 40 salen 114.076 píxeles mal, con 48 quedan
  21.019 y **con 56 ninguno**. Está en 56.

- **El volcado de bytes de HFDL.** El botón «Mapa» en HFDL ahora cicla
  **Lista → Bytes → Mapa**. En «Bytes», cada avión que haya mandado datos de
  usuario lleva debajo los primeros 20 bytes en crudo. Es para averiguar por
  qué no salen coordenadas: el decodificador busca el tipo 0xD1 en un sitio
  que sale de **una sola trama real** mirada a mano, y sin ver los bytes
  cualquier arreglo sería adivinar.

- Dos minas de las que no se ven: la ordenación de los cruces usaba un
  índice de 8 bits, que se da la vuelta en silencio con más de 128; y el
  contador de cruces es de 8 bits, así que un tope de 256 no se alcanza
  nunca y el barrido escribiría fuera. Las dos salieron al subir el tope
  para medir.

### What's new

- **No more stripes when dragging the map.** The fill dropped any row with
  more than 40 coastline crossings outright, which on screen is a horizontal
  stripe as wide as the continent. I picked 40 by measuring **a single
  view** — the centred world — and wrote next to it that it was measured.
  Sweeping the whole world at zooms 1 and 2, 78 views against a reference
  render: 40 leaves 114,076 pixels wrong, 48 leaves 21,019, and **56 leaves
  none**. It is now 56.

- **The HFDL byte dump.** In HFDL the "Map" button now cycles **List →
  Bytes → Map**. Under "Bytes", every aircraft that has sent user data shows
  its first 20 raw bytes underneath. It exists to find out why no positions
  come out: the decoder looks for type 0xD1 at an offset derived from **one
  real frame** checked by hand, and without seeing the bytes any fix would
  be guesswork.

- Two landmines of the invisible kind: the crossing sort used an 8-bit
  index, which silently wraps past 128; and the crossing counter is 8-bit,
  so a cap of 256 is never reached and the scan would write out of bounds.
  Both surfaced while raising the cap to measure.

---

## V4.80 · 2026-10-14

### Qué cambia

- **La radio ya rellena la tierra.** Hasta hoy no lo hacía con ningún mapa,
  aunque el catálogo y los bancos dijeran que sí. Un `.map` se carga de dos
  maneras —con el fichero entero en RAM, o a trozos desde la flash interna—
  y son dos funciones distintas. La radio usa siempre la segunda, porque el
  mapa se graba en su flash al arrancar, y **ésa era la única que no leía el
  byte del estilo**. Se quedaba en cero, que significa «contorno».

- Los cinco bancos del relleno entran todos por la puerta de RAM, así que
  los cien mapas salían en verde midiendo un camino que la radio no usa. El
  banco nuevo, **`mapados`**, carga el mismo fichero por las dos puertas y
  compara el dibujo **píxel a píxel**: 100 de 100 ficheros, 0 píxeles de 230.400.

- Y un segundo fallo que hacía invisible al primero: `mapa_bin_quita()` no
  borraba el estilo, así que el banco heredaba el del mapa anterior y pasaba
  con el fallo puesto. Ahora lo borra, y el banco caza el fallo original a la
  primera.

### What's new

- **The radio now fills the land.** Until today it did not, with any map,
  even though the catalogue and the benches said otherwise. A `.map` loads
  two ways — whole file in RAM, or in pieces from internal flash — and those
  are two different functions. The radio always uses the second, because the
  map is burned into its flash at boot, and **that was the only one not
  reading the style byte**. It stayed at zero, which means outline.

- All five fill benches go through the RAM door, so the hundred maps came up
  green while measuring a path the radio never takes. The new bench,
  **`mapados`**, loads the same file through both doors and compares the
  drawing **pixel by pixel**: 100 of 100 files, 0 pixels out of 230,400.

- And a second bug that hid the first: `mapa_bin_quita()` did not clear the
  style, so the bench inherited the previous map's and passed with the bug in
  place. It clears it now, and the bench catches the original bug first try.

---

## V4.79 · 2026-10-14

### Qué cambia

- **Los mapas nuevos vuelven a funcionar en las radios viejas.** La V4.78
  marcaba los anillos de tierra con un bit **dentro de la cuenta de puntos**
  de cada trazo, razonando que un `.map` antiguo no trae ese bit y por tanto
  no hacía falta subir la versión del formato. El razonamiento sólo miraba
  en una dirección. En la otra —un `.map` **nuevo** leído por un firmware
  **anterior**— el lector de entonces enmascaraba `0x7FFF`, el bit se le
  colaba en la cuenta y el primer anillo se tragaba la tabla entera: **51 de
  los cien ficheros** salían como un solo trazo de 4.309 puntos en vez de
  370 trazos. Sin reventar y sin avisar, que es el peor fallo que se puede
  entregar.

- Los anillos pasan a un **mapa de bits fuera de la tabla**, pegado detrás
  del cero que la cierra y contado en el `tam` del nivel; su longitud va en
  los dos bytes de relleno que la entrada de nivel ya tenía a cero. Un
  lector viejo para en ese cero y no lo ve jamás, así que **la tabla de
  trazos de un `.map` de la V4.79 es byte a byte la de un fichero de versión
  2**. Cuesta 47 bytes por mapa y no sube la versión del formato, esta vez
  de verdad.

- Un banco nuevo, **`mapacompat`**, recorre los cien ficheros con el lector
  de la radio y con una copia del lector de versión 2 y exige que vean los
  mismos trazos y los mismos puntos. Se le ha visto fallar: forzando el bit
  a mano, la copia pasa de 370 trazos a 1.

### What's new

- **New maps work on old radios again.** V4.78 marked land rings with a bit
  **inside each stroke's point count**, reasoning that an old `.map` does not
  carry that bit, so the format version need not change. The reasoning only
  looked one way. The other way — a **new** `.map` read by an **older**
  firmware — the reader of the day masked `0x7FFF`, the bit leaked into the
  count and the first ring swallowed the whole table: **51 of the hundred
  files** came out as a single 4,309-point stroke instead of 370 strokes.
  No crash, no warning, which is the worst thing one can ship.

- The rings move to a **bitmap outside the table**, tucked behind the zero
  that closes it and counted inside the level's `tam`; its length goes in the
  two padding bytes the level entry already held at zero. An old reader stops
  at that zero and never sees it, so **a V4.79 `.map`'s stroke table is byte
  for byte that of a version-2 file**. It costs 47 bytes per map and does not
  bump the format version — this time for real.

- A new bench, **`mapacompat`**, walks all hundred files with the radio's
  reader and with a copy of the version-2 reader and demands they see the
  same strokes and the same points. It has been seen to fail: forcing the bit
  by hand takes the copy from 370 strokes to 1.

---

## V4.78 · 2026-10-14

### Qué cambia

- **La tierra se rellena.** Hasta hoy el mapa pintaba el panel del color del
  mar y dibujaba las costas encima, y `MAPA_COL_TIERRA` —el hueco 1 de los
  quince colores que lleva cada uno de los cien mapas desde el primer día—
  no lo leía nadie. Por eso el catálogo tenía que avisar de que «el estilo
  HOY NO CAMBIA EL DIBUJO» y por eso 013, 014 y 015 eran **el mismo fichero
  byte a byte**.
- **Y el estilo del nombre pasa a significar algo.** Los seis estilos
  —contorno, contorno sin fronteras, contorno encendido, tierra rellena,
  tierra de puntos, silueta— generaban el mismo `.map`. Ahora los tres que
  rellenan traen los anillos de tierra y los tres de contorno no, y el
  fichero lleva un byte que dice cuál es. De los cien, **84 son distintos
  entre sí** en vez de 34.
- **Cuesta 22 bytes por mapa** y, medido antes de escribirlo, **la quinta
  parte de lo que ya costaba pintar el mapa** al zoom 1, menos al acercarse.
- Los mapas viejos siguen valiendo y se ven como siempre: traen cero en ese
  byte, que es «contorno».

### What's new

- **Land is filled in.** Until today the map painted the panel in the sea
  colour and drew the coastlines on top, and `MAPA_COL_TIERRA` — slot 1 of
  the fifteen colours every one of the hundred maps has carried from day
  one — was read by nobody. That is why the catalogue had to warn that "the
  style DOES NOT CHANGE THE DRAWING TODAY", and why 013, 014 and 015 were
  **the same file byte for byte**.
- **And the style in the name now means something.** All six styles produced
  the same `.map`. The three filling styles now carry land rings and the
  three contour ones do not, and the file carries a byte saying which. Of
  the hundred, **84 are now distinct** rather than 34.
- **It costs 22 bytes per map** and, measured before a line of firmware was
  written, **a fifth of what drawing the map already cost** at zoom 1, less
  when zoomed in.
- Old maps still work and look the same: they carry zero in that byte, which
  means "contour".

## V4.77 · 2026-10-13

### Qué cambia

- **El SNR de FT8 ya es un SNR.** Lo avisó un issue comparando con WSJT-X:
  "rarely drop below +5 dB, with an average reading around +11 dB". Y tenía
  razón, porque lo que se enseñaba era `score * 0,5`, o sea **la correlación
  del preámbulo**: una medida de cuánto me fío de haber encontrado una señal,
  positiva por construcción y sin ancho de banda de referencia. Era la fórmula
  de relleno del demo de `ft8_lib`, marcada allí como un TODO, y se arrastró
  tal cual.
- **Ahora se calcula de verdad**, referido a los 2.500 Hz de siempre: la
  potencia de los bins de tono de los 79 símbolos contra la mediana de los
  bins vecinos, en lineal y no en dB, con el suelo de −24 de WSJT-X.
- **Y las constantes están medidas, no ajustadas a ojo**: el ancho de ruido
  de un bin resultó ser 4,664 Hz y no sus 6,25 de espaciado, y el símbolo
  pierde 6,00 dB exactos por ocupar media ventana de análisis. Las dos salen
  de medirlas con un tono de amplitud conocida y ruido de varianza conocida.
- **Banco nuevo**: se fabrica una emisión FT8 con el SNR que se quiere, se
  mete por el adaptador de verdad del firmware y se barren siete niveles de
  −20 a +10 dB. Sale pendiente 0,98 y 1,0 dB de error en el peor punto. Un
  solo punto no habría distinguido un error de escala de uno de
  desplazamiento.

### What's new

- **The FT8 SNR is now actually an SNR.** Reported in an issue against
  WSJT-X: "rarely drop below +5 dB, with an average reading around +11 dB".
  Correct, because what was shown was `score * 0.5` — **the sync correlation**:
  a measure of how confident the decoder is that a signal is there, positive
  by construction and with no reference bandwidth. It was `ft8_lib`'s demo
  placeholder, marked there as a TODO, carried over as-is.
- **It is now computed properly**, against the usual 2500 Hz reference: the
  power of the tone bins across the 79 symbols against the median of the
  neighbouring bins, in linear units rather than dB, with WSJT-X's −24 floor.
- **And the constants are measured, not eyeballed**: a bin's noise bandwidth
  turned out to be 4.664 Hz rather than its 6.25 Hz spacing, and a symbol
  loses exactly 6.00 dB by occupying half the analysis window. Both come from
  measuring with a tone of known amplitude and noise of known variance.
- **New bench**: an FT8 transmission is synthesised at a chosen SNR, pushed
  through the firmware's real waterfall adapter, and swept across seven levels
  from −20 to +10 dB. Slope comes out 0.98 with 1.0 dB worst-case error. A
  single point could not have told a scale error from an offset.

## V4.76 · 2026-10-13

### Qué cambia

- **El teclado de la hora tiene ahora dos teclas de aceptar.** «Poner en hora»
  es la de siempre y dice *qué* hora es, sin decir nada del segundo. **«En
  punto»** dice *cuándo*: el toque es el cambio de minuto, o sea «ahora mismo
  son las HH:MM:00». Eso ancla el reloj igual que lo hace el RDS, que es lo que
  piden FT8 y WSPR.
- **Y hay segundero**, a la derecha de la lectura. Mientras pone dos rayas, la
  radio te está diciendo que no sabe en qué segundo va —y no lo sabe—; en
  cuanto tocas «En punto» empieza a contar, y ese cambio es la confirmación de
  que el anclaje ha entrado.
- **Una cadena de esa pantalla estaba sin traducir** y hablaba español en la
  versión inglesa. Arreglada.

### What's new

- **The time keypad now has two accept keys.** «Set the clock» is the old one
  and says *what* the time is, saying nothing about the second. **«On the
  minute»** says *when*: the tap is the minute change — "right now it is
  HH:MM:00". That anchors the clock the same way RDS does, which is what FT8
  and WSPR need.
- **And there is a seconds counter**, to the right of the readout. While it
  shows two dashes, the radio is telling you it does not know which second it
  is in — and it does not; as soon as you tap «On the minute» it starts
  counting, and that change is the confirmation the anchor took.
- **One string on that screen was untranslated** and spoke Spanish in the
  English build. Fixed.

## V4.75 · 2026-10-13

### Qué cambia

- **Al pasar de RTTY a SSTV ya no se queda el botón «50 Bd» en pantalla.** Lo
  avisó un probador, con vídeo. El panel de texto lo comparten RTTY, SSTV, el
  fax y Hell, y desde el 06/10 la estructura del panel ya dejaba a cero los dos
  botones de RTTY en cuanto arrancaba el SSTV — pero eso no borra los píxeles
  que ya estaban pintados, y nadie los borraba: el borrado de la zona entera es
  de una sola vez, y el repintado de cada medio segundo sólo toca lo suyo.
  Bastaba con que el panel se pintara una vez con los rótulos de RTTY *después*
  de ese borrado, y eso pasa porque apagar un decodificador y encender el otro
  no es el mismo instante. Ahora el borrado se rearma mientras el decodificador
  anterior siga en marcha, así que el último borrado cae siempre después del
  último repintado ajeno. Vale igual para el fax y para Hell.

### What's new

- **Switching from RTTY to SSTV no longer leaves RTTY's "50 Bd" button on
  screen.** Reported by a tester, with video. RTTY, SSTV, the fax and Hell all
  share the text panel, and since 06/10 the panel structure already cleared
  RTTY's two buttons as soon as SSTV started — but that does not erase pixels
  already drawn, and nothing erased them: the full-area clear happens once, and
  the half-second repaint only touches its own elements. All it took was one
  paint carrying RTTY's labels *after* that clear, which happens because
  switching one decoder off and the other on are not the same instant. The
  clear is now re-armed while the previous decoder is still running, so the
  last clear always lands after the last foreign repaint. Same for fax and
  Hell.

## V4.74 · 2026-10-13

### Qué cambia

- **El reloj de hardware está reescrito entero.** No cambia lo que ves: la
  hora se sigue tomando del cristal de 32.768 Hz de la placa, que es el que
  aguanta las ranuras de quince segundos de FT8 y el que sobrevive a un
  reinicio. Lo que cambia es de quién es el código. El que había venía de
  otro proyecto con una licencia que no encaja con la de éste, y en vez de
  borrar el aviso se ha escrito de nuevo.
- **Las cuentas del calendario salen a su propio fichero y por fin tienen
  banco.** Bisiestos, días de la semana y conversiones de BCD estaban donde
  ninguna prueba podía mirarlas; ahora se comprueban contra fechas conocidas
  en cada compilación. Un error ahí no se habría visto hasta un 1 de marzo.
- **El reloj ya no se traga una fecha imposible.** Un 31 de febrero se
  rechaza en vez de guardarse, y una lectura corrupta del periférico devuelve
  la fecha de arranque en lugar de un «mes 0» que acababa en pantalla.
- **Al actualizar, la radio dirá «nunca sincronizado» una vez** aunque lo
  estuviera, porque no reconoce lo que dejó la versión anterior en su
  registro de respaldo. Se arregla solo con el primer DCF77, el primer RDS o
  el primer toque en el teclado de la hora. **La hora no se pierde.**

### What's new

- **The hardware clock has been rewritten from scratch.** Nothing changes on
  screen: the time still comes from the board's 32.768 kHz crystal, the one
  that holds FT8's fifteen-second slots and survives a reset. What changes is
  whose code it is. The previous one came from another project under a licence
  that does not fit this one's, and rather than delete the notice it was
  written anew.
- **The calendar arithmetic moved to its own file and finally has a bench.**
  Leap years, weekdays and BCD conversions lived where no test could reach
  them; they are now checked against known dates on every build. A bug there
  would not have shown up until some 1st of March.
- **The clock no longer accepts an impossible date.** A 31st of February is
  rejected instead of stored, and a corrupt read of the peripheral returns the
  boot date rather than a "month 0" that used to reach the screen.
- **On updating, the radio will say "never synced" once** even if it was,
  because it does not recognise what the previous version left in its backup
  register. The first DCF77, RDS or clock-keypad entry fixes it. **The time
  itself is not lost.**

---

## V4.73 · 2026-10-13

### Qué cambia

- **El mapa de HFDL pone el nombre de los aviones.** Hasta ahora eran
  puntos sueltos; ahora los que se sabe cómo se llaman llevan su distintivo
  de vuelo escrito —«DLH494»—, su rayo desde tu QTH y su ficha al tocarlos,
  igual que las estaciones de FT8. Si no se sabe el vuelo se pone la
  matrícula OACI; si no se sabe ninguna de las dos, ese avión se queda con
  su punto y sin nombre.
- La ficha de un avión dice **quién es, a cuántos kilómetros está y en qué
  rumbo**, y nada más: de un avión no se sabe con cuánta señal llegó ni
  cuándo, así que no se inventa. Y **no se le pone país**: «UAL123» es
  United Airlines, pero su prefijo «UA» en la tabla de países es Ucrania.

### What's new

- **The HFDL map now labels the aircraft.** They used to be bare dots; the
  ones whose name is known now carry their flight number — "DLH494" — a ray
  from your QTH and an info card when tapped, just like the FT8 stations.
  If the flight number is unknown the ICAO address is used instead; if
  neither is known, that aircraft keeps its dot and stays unnamed.
- An aircraft's card gives **who it is, how far away and on what bearing**,
  and nothing else: nobody knows how strong it came in or when, so nothing
  is invented. And **no country is shown**: "UAL123" is United Airlines,
  but its "UA" prefix in the country table is Ukraine.

---

## V4.72 · 2026-10-13

### Qué cambia

- **Arreglado un fallo gordo de audio que estaba desde el arranque.** La
  radio tiraba bloques de audio BUENOS creyendo que venían corruptos: la
  regla que los juzga se decidió apagar el 08/10, pero la línea que la
  apaga vive en una rama que el arranque en frío no recorre. Mientras tanto
  se repetían 2,67 ms de audio ya sonado hasta 375 veces por segundo —eso
  es el siseo— y esos bloques **no le llegaban a ningún decodificador**:
  huecos en SSTV, en el fax, en RTTY. Y se curaba solo en cuanto entrabas
  una vez en WFM, para volver al siguiente reinicio.
- **El mapa va mucho más suelto.** Se dibujaba la costa entera dieciocho
  veces por fotograma; ahora los trazos que no tocan la franja que se está
  componiendo se saltan. De 24.822 trazos a 1.556, con el dibujo idéntico.
  Se nota sobre todo arrastrando el mapa con el dedo.
- **En FT8 sin reloj en hora, la barra ya no desaparece.** El mensaje "pon
  el reloj en hora" salía con la última palabra tapada y se borraba entero
  cada segundo.
- **En SSTV y en el fax ya no sale un botón "Auto" que no es suyo**, que
  además estrujaba la chapa: "Esperando una imagen" se leía "o una imagen".
- **La pantalla de la hora ya no corta el nombre de la emisora.** Cinco de
  los diez salían cortados, incluido el que sale al abrir ("DCF77 - 77,5
  kHz, Alem"). Ahora van en dos renglones.
- Por dentro: menos trabajo repetido en cada fotograma (el reloj se leía 18
  veces, el país del mapa se buscaba 18 veces entre 329, la tabla de bandas
  se recorría dos veces seguidas), el recorte de las columnas ya no es
  cuadrático **y ya no parte letras acentuadas por la mitad**, y el texto
  que cae fuera de la ventana ya no se descodifica para tirarlo.

### What's new

- **Fixed a serious audio fault that was there from power-on.** The radio
  was discarding GOOD audio blocks as if they were corrupt: the rule that
  judges them was decided to be switched off on 08/10, but the line that
  switches it off sits in a branch a cold boot never takes. Meanwhile 2.67
  ms of already-played audio was repeated up to 375 times a second — that
  is the hiss — and those blocks **never reached any decoder**: gaps in
  SSTV, in the fax, in RTTY. And it healed itself as soon as you visited
  WFM once, only to come back on the next reboot.
- **The map is much smoother.** The whole coastline was being drawn
  eighteen times per frame; strokes that do not touch the strip being
  composed are now skipped. From 24,822 strokes to 1,556, with an identical
  picture. Most noticeable when dragging the map.
- **In FT8 with no clock set, the bar no longer disappears.** The "set the
  clock first" message had its last word covered and was wiped every
  second.
- **SSTV and the fax no longer show an "Auto" button that is not theirs**,
  which also squeezed the status tag: "Waiting for a picture" read "for a
  picture".
- **The clock screen no longer clips the station name.** Five of the ten
  were cut, including the one shown on opening. They now use two lines.
- Under the hood: less repeated per-frame work (the clock was read 18
  times, the map's country looked up 18 times among 329, the band table
  walked twice in a row), column truncation is no longer quadratic **and no
  longer cuts accented letters in half**, and text outside the window is no
  longer decoded just to be thrown away.

---

## V4.71 · 2026-10-13

### Qué cambia

- **En el mapa ya no desaparecen estaciones que sí están en la lista.** Si
  dos indicativos comparten localizador —M7BSI y G8KHF son los dos IO92, o
  sea el mismo punto del mapa— la chapa del segundo chocaba con la del
  primero y el que llegaba tarde se callaba entero: sin chapa, sin punto y
  sin rayo. Y un cuadrado de localizador son más de cien kilómetros de
  lado, así que eso pasa a diario. Ahora se prueban seis colocaciones
  —derecha e izquierda, a la altura del punto y un renglón arriba y otro
  abajo— antes de rendirse. Dos en el mismo cuadrado salen las dos; cuatro,
  las cuatro.
- La etiqueta que no choca con nadie **no se mueve**: la primera
  colocación que se prueba es la de siempre.

### What's new

- **Stations that are in the list no longer vanish from the map.** When two
  callsigns share a locator — M7BSI and G8KHF are both IO92, i.e. the same
  map pixel — the second one's tag clashed with the first and the late
  arrival went silent entirely: no tag, no dot, no ray. And a locator
  square is over a hundred kilometres across, so this happens daily. The
  map now tries six placements — right and left, level with the dot and one
  row above and below — before giving up. Two in the same square both show;
  four, all four.
- A tag that clashes with nothing **does not move**: the first placement
  tried is the one it always used.

---

## V4.70 · 2026-10-13

### Qué cambia

- **Las dos celdas de SAM ya no salen con el texto cortado.** Decían «AM
  síncrona, banda al» y «AM síncrona, banda b» desde el 11/10: la
  descripción medía más que la celda. Ahora ponen «Sólo la banda alta» y
  «Sólo la banda baja».
- Por dentro: **tres entradas de la lista de modos eran invisibles para las
  comprobaciones** —las dos de SAM y la nueva SSTV-FM—, porque se leía la
  tabla línea a línea y esas van en dos. Por eso lo de arriba llevaba dos
  días sin que nada avisara, y por eso la descripción de SSTV-FM también
  nacía cortada. Arreglado, y con ello el recuento de modos por categoría,
  que decía 25 donde hay 28.
- Recordatorio de dónde está lo nuevo: **Modo → Imágenes, tercera celda**,
  entre SSTV y HELL.

### What's new

- **The two SAM cells no longer show clipped text.** They read «AM síncrona,
  banda al» and «AM síncrona, banda b» since 11/10: the description was
  wider than the cell. They now read «SAM, upper only» and «SAM, lower
  only».
- Under the hood: **three entries in the mode list were invisible to the
  checks** — the two SAM ones and the new SSTV-FM — because the table was
  read line by line and those span two. That is why the above went unnoticed
  for two days, and why SSTV-FM's description was born clipped too. Fixed,
  and with it the per-category mode count, which said 25 where there are 28.
- A reminder of where the new mode lives: **Mode → Images, third cell**,
  between SSTV and HELL.

---

## V4.69 · 2026-10-13

### Qué cambia

- **SSTV en FM: hay un modo nuevo, «SSTV-FM».** El mismo decodificador que
  SSTV, pero escuchando en FM estrecha. En onda corta la SSTV va por banda
  lateral; en dos metros va en FM, y **la ISS manda fotos en 145.800 MHz**,
  que la radio alcanza de sobra. El filtro de canal se abre a 9 kHz para que
  quepa la señal entera: con el de voz los tonos agudos salen apagados y la
  foto pierde los claros.
- **Arreglada la foto que se corrompía a trozos con la señal perfecta.** No
  se perdía señal, se perdían **muestras**: si la interrupción del audio
  llega tarde —y la radio va justa mientras pinta la foto— se tira medio
  bloque, y 5,3 ms son más que el pulso de sincronismo de Martin 1 (4,9 ms).
  Hasta ahora un solo hueco así descolocaba la rejilla de líneas un 20 % y
  el siguiente sincronismo bueno se rechazaba, y el siguiente, y el
  siguiente: **cuatro huecos de 5 ms estropeaban veintitrés renglones**.
  Ahora cuestan lo que pesan. Medido: de 23 renglones malos a 0.

### What's new

- **SSTV on FM: there is a new mode, «SSTV-FM».** The same decoder as SSTV,
  but listening on narrow FM. On shortwave SSTV runs on sideband; on two
  metres it runs on FM, and **the ISS sends pictures on 145.800 MHz**, well
  within the radio's reach. The channel filter opens to 9 kHz so the whole
  signal fits: with the voice filter the high tones come out muted and the
  picture loses its highlights.
- **Fixed the picture that broke up in patches with a perfect signal.** No
  signal was being lost — **samples** were: if the audio interrupt runs late
  (and the radio is tight on time while painting the picture) half a block
  is dropped, and 5.3 ms is longer than Martin 1's sync pulse (4.9 ms).
  Until now a single gap like that threw the line grid off by 20 %, and then
  the next good sync was rejected, and the next, and the next: **four 5 ms
  gaps ruined twenty-three rows**. Now they cost what they weigh. Measured:
  from 23 bad rows to 0.

---

## V4.68 · 2026-10-13

### Qué cambia

- **La ficha del mapa dice el país**, justo detrás del localizador y delante
  de los kilómetros: `DX F1BHB JN09 · Francia · 1047 km · AZ 18 · +9 dB`. Si
  el prefijo no está en la tabla no se inventa nada, se queda el hueco.
- **La batería de arriba lleva el porcentaje dentro del dibujo y ya no pone
  los voltios.** El relleno pasa a ser un tinte para que el número se lea
  tanto sobre la parte llena como sobre la vacía; comprobado en las ocho
  paletas. Los milivoltios siguen en Ajustes → Información → Batería, que es
  donde se miran cuando se duda de la medida.
- **El botón "Borrar" del panel de CW.** Un dibujo puede no llegar a hacerse
  —la pantalla se compone en una franja que la captura de pantalla ocupa
  veinticinco segundos— y la radio bajaba igualmente la marca de "panel
  repintado". Si justo antes se había borrado la zona, la cabecera con su
  botón se quedaba en blanco hasta cambiar de modo. Arreglado en los dos
  sitios donde pasaba. **No he podido reproducir el fallo, así que esto es
  un candidato con el mecanismo correcto, no una causa confirmada**: la fila
  nueva **Choques de banda** de Información dice si de verdad ocurre. Cero
  tras un buen rato en CW significa que hay que seguir buscando.

### What's new

- **The map card now names the country**, right after the locator and before
  the distance: `DX F1BHB JN09 · France · 1047 km · AZ 18 · +9 dB`. If the
  prefix is not in the table nothing is invented — the slot is simply left
  out.
- **The battery at the top shows its percentage inside the icon and no
  longer prints the voltage.** The fill became a tint so the number stays
  readable over both the full and the empty part; checked across all eight
  palettes. Millivolts are still in Settings → Information → Battery, which
  is where you look when you doubt the reading.
- **The CW panel's "Clear" button.** A drawing can silently fail to happen —
  the screen is composed through one strip that a screenshot holds for
  twenty-five seconds — and the radio cleared its "panel repainted" flag
  regardless. If the area had just been wiped, the header and its button
  stayed blank until you changed mode. Fixed in both places where this
  happened. **I could not reproduce the fault, so this is a candidate with
  the right mechanism, not a confirmed cause**: the new **Band clashes** row
  under Information says whether it happens at all. Zero after a long CW
  session means the hunt continues.

---

## V4.67 · 2026-10-13

### Qué cambia

- **La barra de FT8 ya no se ve como una persiana de rayas.** Era un fallo de
  verdad, no de gusto: la radio pedía repintar una franja de 450 píxeles
  empezando en el 458 de una pantalla de 800. El panel no tiene columna 908,
  así que cada fila se le derramaba en la siguiente. Venía de la cabecera
  nueva: la cuenta sumaba el botón de CQ *a la derecha* de la barra, y el
  botón se había mudado a la izquierda.
- **El "8/15" va ahora pegado al punto verde**, a la derecha del todo. El
  número y la lucecita cuentan lo mismo —cómo va la tanda— y estaban en
  extremos opuestos de la barra. Los números de diagnóstico se centran en lo
  que queda.
- Por dentro: `gfx2_render()` recorta la ventana contra los bordes de la
  pantalla, así que un error así vuelve a salir como "falta un trozo del
  dibujo" y no como rayas. Y se ha quitado el único aviso del compilador que
  quedaba vivo.

### What's new

- **The FT8 bar no longer renders as a blind of stripes.** This was a real
  bug, not a matter of taste: the radio was asking to repaint a 450-pixel
  strip starting at column 458 of an 800-pixel screen. The panel has no
  column 908, so every row spilled into the next one. It came from the new
  header layout: the width still added the CQ button *to the right* of the
  bar, and that button had moved to the left.
- **"8/15" now sits right next to the green dot**, at the far right. The
  number and the light tell the same story —how the slot is going— and they
  were at opposite ends of the bar. The diagnostic numbers are centred in
  what is left.
- Under the hood: `gfx2_render()` now clips the window against the screen
  edges, so a mistake like this shows up as "part of the drawing is missing"
  instead of stripes. And the one remaining compiler warning is gone.

---

## V4.66 · 2026-10-13

### Qué cambia

- **La AM síncrona ya no pita cuando no estás justo en la portadora.** Un
  detector de producto con el oscilador suelto saca la señal batida contra
  ese oscilador, y esa diferencia es un tono. Ahora, **sin enganche sale la
  envolvente** —AM de toda la vida—, y en cuanto el lazo engancha se pasa a
  la demodulación síncrona sin tocar nada. Se cruza en unos 50 ms, no se
  conmuta de golpe.
- Y además **se recupera el audio**: con el lazo suelto el detector no sólo
  añadía un tono, se comía la modulación. Medido con la portadora a 400 Hz:
  el tono de la emisora pasa de 723 a 16.499 y el pitido baja de +3 dB a
  −21 dB respecto de él.
- **Esto arregla algo que la V4.61 había empeorado**, y conviene saberlo: al
  bajar el margen de enganche de 4.000 a 100 Hz —para que el lazo no se fuera
  con la emisora de al lado— una portadora a 400 Hz dejó de alcanzarse, así
  que el lazo se quedaba contra el tope pitando. Ahora no hay que elegir
  entre las dos cosas.
- **En el mapa es mucho más fácil acertarle a un indicativo.** El blanco del
  dedo pasa de 24 x 24 px a 44 x 44 —este firmware se exige 47 px en los
  ajustes y aquí se conformaba con la mitad—, con dos vecinas pegadas **gana
  la más cercana al dedo** en vez de la más reciente, y el umbral que separa
  un toque de un arrastre pasa de 8 a 20 px, que es lo que se mueve un dedo
  en un panel resistivo mientras aprieta.

### What's new

- **Synchronous AM no longer whistles when you are not exactly on the
  carrier.** A product detector with an unlocked oscillator outputs the
  signal beating against that oscillator, and the difference is a tone. Now,
  **with no lock it outputs the envelope** —ordinary AM— and as soon as the
  loop locks it switches to synchronous demodulation with nothing to set. It
  crossfades over about 50 ms rather than switching abruptly.
- And it **recovers the audio** too: with the loop adrift the detector was
  not just adding a tone, it was eating the modulation. Measured with the
  carrier 400 Hz off: the station's tone goes from 723 to 16,499 and the
  whistle drops from +3 dB to −21 dB relative to it.
- **This fixes something V4.61 had made worse**, and that is worth stating:
  lowering the capture range from 4,000 to 100 Hz —so the loop would not run
  off to the neighbouring station— meant a carrier 400 Hz away could no
  longer be reached, so the loop sat against the limit and whistled. There is
  no longer a choice to make between the two.
- **Hitting a callsign on the map is far easier.** The finger target goes
  from 24 x 24 px to 44 x 44 —this firmware demands 47 px in the settings
  grid and here it settled for half— with two neighbours close together **the
  nearest to the finger wins** instead of the most recent, and the threshold
  separating a tap from a drag goes from 8 to 20 px, which is what a finger
  moves on a resistive panel while pressing.

---

## V4.65 · 2026-10-13

### Qué cambia

- **El DT de la barra de FT8 ya está en segundos.** Estaba escrito como si lo
  fuera y no lo era: 240 ms salían como «+2,4», que se lee como dos segundos
  y pico. La coma estaba un sitio a la izquierda. Ahora pone «+0,2 s», con su
  unidad detrás, igual que WSJT-X. **El número siempre fue bueno; lo que
  mentía era la unidad**, y es justo el número con el que se decide si tienes
  el reloj torcido.
- **La barra enseña dos números en vez de cuatro.** Se quedan los que se
  miran siempre: los **candidatos** («24c», sitios donde la radio creyó ver
  una emisión FT8 antes de descifrarla; si es 0, no llega nada con forma de
  FT8) y el **desfase**.
- **Los otros dos aparecen sólo cuando dicen algo**: «4p» si se está
  perdiendo audio —tendría que ser siempre 0— y «9140 ms» si la
  decodificación pasa de media ranura. Si los dos aplicaran, sale el de los
  perdidos, que es el que estropea la decodificación en silencio.
- Separador « · » en vez de dos espacios.
- **Y la barra se ha fusionado con la chapa de la derecha.** La chapa decía
  «8/15» —el segundo dentro de la ranura— que es exactamente lo que ya
  dibujaba el relleno de la barra: dos sitios contando lo mismo. Ahora la
  cabecera de FT8 es **Borrar · CQ · frecuencia · Mapa** a la izquierda y
  **la barra a la derecha del todo**, con 336 px.
- **Sin perder nada**: el «8/15» va dentro de la barra, la lucecita verde en
  su extremo derecho, «sin hora» ya lo decía la propia barra, y **tocar la
  barra sigue cuadrando la ranura** igual que antes se tocaba la chapa.
- Las cabeceras de los otros doce modos no cambian.

### What's new

- **The DT in the FT8 bar is now in seconds.** It was written as if it were
  and it was not: 240 ms came out as «+2,4», which reads as two and a bit
  seconds. The decimal point was one place to the left. It now reads
  «+0,2 s», with its unit, like WSJT-X. **The figure was always right; the
  unit was lying**, and this is precisely the figure you use to tell whether
  your clock is off.
- **The bar shows two numbers instead of four.** The ones you always look at
  stay: **candidates** («24c», places where the radio thought it saw an FT8
  transmission before decoding it; 0 means nothing FT8-shaped is arriving)
  and the **time offset**.
- **The other two appear only when they have something to say**: «4p» if
  audio is being lost —it should always be 0— and «9140 ms» if decoding takes
  more than half a slot. If both applied, the lost-audio one wins, because
  that is the one that quietly spoils decoding.
- « · » as the separator instead of two spaces.
- **And the bar has been merged with the status chip on the right.** The chip
  read «8/15» —the second within the slot— which is exactly what the bar's
  fill already draws: two places saying the same thing. The FT8 header is now
  **Clear · CQ · frequency · Map** on the left and **the bar on the far
  right**, 336 px wide.
- **Nothing is lost**: the «8/15» goes inside the bar, the green light at its
  right-hand end, «no clock» was already spelled out by the bar itself, and
  **tapping the bar still re-aligns the slot**, exactly as tapping the chip
  used to.
- The headers of the other twelve modes do not change.

---

## V4.64 · 2026-10-13

### Qué cambia

- **La barra de la ranura de FT8 ocupa ahora 200 px en vez de 300.** Lo que
  la hacía larga no era la cuenta de los quince segundos —ésa es el relleno,
  y se ve igual— sino los cuatro números de diagnóstico de dentro, que en su
  caso peor miden 185 px. Caben de sobra.
- **Botón CQ nuevo**, en el hueco que deja la barra. Pulsado, la lista
  enseña **sólo las llamadas CQ**, que son las únicas a las que puedes
  contestar: el resto son conversaciones entre otros dos.
- **Y filtra también los puntos del mapa.** Si te preguntabas por qué hay
  puntos sin indicativo, es porque el punto lo pone cualquier mensaje con
  localizador y la etiqueta sólo los CQ. Con el botón pulsado, en el mapa se
  queda exactamente lo que tiene nombre.
- El filtro mira **todos** los mensajes guardados, no los quince que caben en
  el panel: si las únicas CQ están entre los más viejos, salen igual.
- No se guarda al apagar, igual que el botón del mapa: es lo que estás
  mirando ahora, no cómo quieres que arranque la radio.

### What's new

- **The FT8 slot bar is now 200 px wide instead of 300.** What made it long
  was not the fifteen-second count —that is the fill, and it reads the same—
  but the four diagnostic numbers inside, which at worst measure 185 px. They
  fit comfortably.
- **New CQ button**, in the space the bar frees. Pressed, the list shows
  **only CQ calls**, which are the only ones you can answer: the rest are
  conversations between two other stations.
- **And it filters the map dots too.** If you were wondering why there are
  dots with no callsign, it is because any message carrying a locator gets a
  dot and only CQs get a label. With the button pressed, what stays on the
  map is exactly what has a name.
- The filter looks at **all** stored messages, not just the fifteen that fit
  on the panel: if the only CQs are among the oldest, they still show.
- It is not saved on power-off, like the map button: it is what you are
  looking at now, not how you want the radio to start.

---

## V4.63 · 2026-10-13

### Qué cambia

- **Arreglado: tocar un indicativo del mapa no hacía nada.** La V4.62 pintaba
  los indicativos y los rayos bien, pero el toque no elegía ninguno.
- La causa eran **las coordenadas del momento de soltar el dedo: valen
  (0,0)**. El panel no las escribe cuando ya no hay contacto, así que el
  toque se resolvía contra la esquina de arriba a la izquierda de la
  pantalla, donde no hay ningún indicativo. Ahora se usa el sitio donde
  empezó el gesto, que es el bueno.
- Y de paso, **el temblor del panel ya no cuenta como arrastre**: lo que
  separa un toque de un arrastre es dónde acaba el dedo respecto de dónde
  empezó, no si en algún momento se movió dos píxeles. Antes, un dedo que
  temblara un poco —o sea, cualquiera— no llegaba a tocar nunca.
- El reparto del gesto se ha sacado a donde el simulador lo puede medir, con
  seis comprobaciones nuevas. Lo de ayer no fue escribir dos líneas mal: fue
  escribirlas donde ningún banco llegaba.

### What's new

- **Fixed: tapping a callsign on the map did nothing.** V4.62 drew the
  callsigns and the great-circle lines correctly, but the tap selected none
  of them.
- The cause was **the coordinates at the moment the finger lifts: they are
  (0,0)**. The panel does not write them once there is no contact, so the tap
  was resolved against the top-left corner of the screen, where no callsign
  is or ever will be. It now uses where the gesture started, which is the
  right one.
- And while at it, **panel jitter no longer counts as a drag**: what tells a
  tap from a drag is where the finger ends relative to where it started, not
  whether it ever moved two pixels. Before, a finger that wavered a little
  —that is, anyone's— never managed to tap at all.
- The gesture handling has been moved to where the simulator can measure it,
  with six new checks. Yesterday's mistake was not writing two lines wrong:
  it was writing them where no bench could reach.

---

## V4.62 · 2026-10-12

### Qué cambia

- **Los indicativos de los CQ salen en el mapa.** Hasta ahora el mapa ponía
  el punto; ahora pone también el nombre de quien llama. Sólo los CQ: lo
  demás son respuestas a otros, que no son a quien puedes llamar tú.
- **Y una línea desde tu QTH hasta cada uno.** Es una **ortodrómica de
  verdad**, no una raya entre dos puntos: en un mapa plano la raya recta no
  es el camino —Madrid-Tokio por la raya pasa por el Himalaya y de verdad
  pasa por el Ártico— y una raya que no es el camino engaña sobre por dónde
  apuntar la antena.
- **Tocar un indicativo saca su ficha abajo**: `DX JA5AFW PM64 · 10537 km ·
  AZ 35 · -8 dB · x6 · hace 3 min`. Tocar el mar la cierra.
- **Cuántos caben sube con el zoom**: 5 a escala 1, 6 a escala 2, 8 a escala
  4 y 12 a escala 8. Y si dos chocan, **gana el más nuevo**: el que acaba de
  llamar siempre se lee.
- **La misma estación, una etiqueta.** En FT8 el que llama repite su CQ cada
  minuto; se cuenta y sale como "x6", pero rótulo hay uno.
- **El color del rótulo y el de la línea los trae tu `.map`.** Son dos de los
  quince colores que los cien tipos ya traían puestos desde agosto y que no
  leía nadie. Si en un tipo concreto el rótulo no contrastara con su chapa,
  la radio lo pinta con la tinta de siempre: un indicativo que no se lee no
  es un indicativo.
- Va en **FT8 y en WSPR**. No hace falta tocar nada: si estás en el mapa y
  hay CQ, salen.

### Lo que NO lleva, y por qué

- **JTTY se queda fuera.** Su mensaje son átomos pegados —`CQ K1ABC CQ FN42
  599 123`— donde el indicativo no está en un sitio fijo, así que sacarlo
  sería adivinar, y un indicativo adivinado en un mapa es peor que ninguno.
  Los puntos de JTTY siguen saliendo igual que hasta ahora.
- **HFDL y AIS tampoco**: un avión y un barco no llaman CQ.

### What's new

- **CQ callsigns now appear on the map.** Until now the map drew the dot;
  now it also writes the name of whoever is calling. CQ only: the rest are
  replies to someone else, not someone you can call.
- **And a line from your QTH to each of them.** It is a **real great
  circle**, not a straight line between two points: on a flat map the
  straight line is not the path —Madrid-Tokyo by the straight line crosses
  the Himalayas, and the real path crosses the Arctic— and a line that is
  not the path misleads you about where to point the antenna.
- **Tapping a callsign brings up its card at the bottom**: `DX JA5AFW PM64 ·
  10537 km · AZ 35 · -8 dB · x6 · hace 3 min`. Tapping the sea closes it.
- **How many fit grows with zoom**: 5 at scale 1, 6 at 2, 8 at 4 and 12 at 8.
  And when two collide, **the newest wins**: whoever has just called is
  always readable.
- **The same station, one label.** In FT8 a caller repeats their CQ every
  minute; it is counted and shown as "x6", but there is one label.
- **The label and line colours come from your `.map`.** They are two of the
  fifteen colours all hundred types have carried since August with nothing
  reading them. If in a given type the label would not contrast with its
  plate, the radio draws it in the usual ink: a callsign you cannot read is
  not a callsign.
- Works in **FT8 and WSPR**. Nothing to switch on: if you are on the map and
  there are CQs, they show.

### What it does not carry, and why

- **JTTY is left out.** Its message is a string of atoms —`CQ K1ABC CQ FN42
  599 123`— where the callsign is not in a fixed place, so extracting it
  would be guessing, and a guessed callsign on a map is worse than none.
  JTTY dots keep showing exactly as before.
- **HFDL and AIS either**: an aircraft and a ship do not call CQ.

---

## V4.61 · 2026-10-12

### Qué cambia

- **El detector síncrono ya no se engancha a la interferencia.** Lo avisó un
  probador: con la emisora floja y una fuerte al lado, el lazo se iba con la
  fuerte. El margen de enganche venía de la fuente original —4.000 Hz a cada
  lado— y eso es *exactamente* lo que deja pasar el filtro de canal, o sea
  que el lazo podía irse a cualquier sitio que entrara por el filtro. Ahora
  son **100 Hz** por omisión.
- **Y lo peor no era que se fuera, era que decía estar enganchado.** Con una
  intrusa 20 dB más fuerte a 1 kHz, el lazo se quedaba en -989,5 Hz y la
  radio daba por buena esa portadora: los PPM de la cabecera eran los de la
  emisora de al lado, y "Calibrar PPM" se habría dejado calibrar el cristal
  contra ella. Con 100 Hz se queda contra el tope y **dice que no está
  enganchado**, que es la verdad.
- **Información > última página > Margen SAM**, con cuatro valores: ±100 Hz
  (por omisión), 250, 500 y 4.000, y un toque pasa al siguiente. No es un
  gusto: lo que hay que cubrir es el error de sintonía, que sale del cristal y
  por tanto crece con la frecuencia. Con el cristal calibrado, 100 Hz sobran
  en toda la banda; sin calibrar, 10 ppm son 290 Hz a 29 MHz. La fila enseña
  además a cuántos hercios está enganchado el lazo.
- **El margen ancho es el modo con el que se calibra**, y por eso sigue
  estando: sin margen no hay enganche, sin enganche no hay calibración y sin
  calibración nunca habría enganche. La fila "Calibrar PPM" ahora dice **"abre
  el margen"** en vez de "sin enganche" cuando ése es el motivo.
- Se puede cambiar con la radio andando, sin reiniciar el modo.

### Lo que NO lleva, y por qué

- **El ancho del lazo se queda en 200 Hz.** Con 25 Hz el lazo ni se pega al
  tope —mejor todavía—, pero enganchar una portadora desplazada 90 Hz pasa de
  1,80 s a 3,25 s y, sobre todo, el alcance se hunde con él: a 300 Hz ya no
  engancha ni con el margen a 4.000, o sea que el modo ancho dejaría de servir
  para calibrar, que es para lo único que está.
- **Con una emisora 20 dB más fuerte dentro del filtro no hay demodulación que
  salve eso.** Lo que salva es SAM-U o SAM-L (V4.51), o estrechar el filtro.

### What's new

- **The synchronous detector no longer locks onto the interference.** A tester
  reported it: with a weak station and a strong one nearby, the loop went with
  the strong one. The capture range came from the original source —4,000 Hz
  either side— and that is *exactly* what the channel filter passes, so the
  loop could go anywhere that got through the filter. It is now **100 Hz** by
  default.
- **And the worst part was not that it drifted, it was that it claimed a
  lock.** With an interferer 20 dB stronger at 1 kHz, the loop settled at
  -989.5 Hz and the radio took that carrier as good: the PPM figure in the
  header belonged to the neighbouring station, and "PPM calib." would have let
  you calibrate the crystal against it. With 100 Hz it sits against the limit
  and **reports no lock**, which is the truth.
- **Information > last page > SAM range**, four values: ±100 Hz (default),
  250, 500 and 4,000, one tap moves to the next. This is not a taste: what the
  range has to cover is the tuning error, which comes from the crystal and
  therefore grows with frequency. With the crystal calibrated, 100 Hz is
  plenty across the band; uncalibrated, 10 ppm is 290 Hz at 29 MHz. The row
  also shows how many hertz off the loop is locked.
- **The wide range is the mode you calibrate in**, which is why it is still
  there: no range, no lock; no lock, no calibration; no calibration, never a
  lock. The "PPM calib." row now says **"widen range"** instead of "no lock"
  when that is the reason.
- It can be changed while the radio is running, without restarting the mode.

### What it does not carry, and why

- **The loop bandwidth stays at 200 Hz.** At 25 Hz the loop does not even hit
  the limit —better still— but acquiring a carrier 90 Hz off goes from 1.80 s
  to 3.25 s and, above all, the reach collapses with it: at 300 Hz it no
  longer acquires even with the range at 4,000, so the wide mode would stop
  being usable for calibration, which is all it is for.
- **With a station 20 dB stronger inside the filter there is no demodulation
  that saves it.** What saves it is SAM-U or SAM-L (V4.51), or a narrower
  filter.

---

## V4.60 · 2026-10-12

### Qué cambia

- **Las fronteras de países ya no se pintan con el color de la costa.** El
  dibujante no las distinguía: sacaba *todos* los trazos con el mismo color, y
  en una línea de un píxel eso es lo mismo que el mismo grosor visto de lejos.
  El `.map` traía un color propio para las fronteras desde el primer día —y
  los cien tipos lo tienen puesto— y no lo miraba nadie.
- **Y cuesta cero bytes.** Se marca en el bit 15 de la cuenta de puntos de
  cada trazo: el trazo más largo del mundo tiene 5.914 puntos, así que de
  32.768 para arriba no hay nada y ese bit estaba libre. Los ficheros miden
  exactamente lo mismo, 70.665 bytes.
- Hay que **volver a copiar el `.map`**: los cien están rehechos con la marca.

### Lo que NO lleva, y por qué

- **El relleno de tierra se queda fuera.** Se puede hacer —3.708 de las 3.751
  costas son anillos cerrados y el barrido por filas cuesta 1.600 bytes— pero
  **sólo funcionaría al zoom 1**: al ampliar, los trazos van partidos por
  casillas para que la ventana quepa en la RAM, y partidos no son anillos.
  Tener el x1 entero residente además de la ventana pide 10.975 bytes y libres
  hay 3.880. Decisión del usuario: dejarlo como está.

### What's new

- **Country borders are no longer drawn in the coastline's colour.** The
  renderer did not tell them apart: it drew *every* outline in the same
  colour, and on a one-pixel line that looks the same as the same thickness
  from a distance. The `.map` has carried a colour of its own for borders from
  day one —and all hundred types set it— and nothing looked at it.
- **And it costs zero bytes.** It is marked in bit 15 of each outline's point
  count: the longest outline in the world has 5,914 points, so nothing lives
  above 32,768 and that bit was free. The files are exactly the same size,
  70,665 bytes.
- You need to **copy the `.map` again**: all hundred are rebuilt with the mark.

### What it does not carry, and why

- **Land fill is left out.** It can be done —3,708 of the 3,751 coastlines are
  closed rings and the scanline fill costs 1,600 bytes— but it would **only
  work at zoom 1**: when zoomed, outlines are split by tile so the window fits
  in RAM, and split outlines are not rings. Keeping the whole x1 level
  resident as well as the window needs 10,975 bytes and 3,880 are free. The
  owner's call: leave it as it is.

---

## V4.59 · 2026-10-12

### Qué cambia

- **«No cabe en la flash» sobre una flash con 192 kB libres.** Quedaba un
  techo viejo que exigía que el `.map` entero cupiera en la RAM prestada
  (unos 45 kB) porque la comprobación de después lo necesitaba en memoria.
  Desde que esa comprobación lee de la flash a trozos, el pozo ya no tiene que
  caber el fichero — quité la guardia de dentro del módulo y **me dejé la de
  fuera**. El mapa de tres niveles mide 70.665 bytes, así que saltaba con
  todos los mapas nuevos.
- Y el mensaje era falso por los dos lados: ni era la flash, ni era el tamaño
  del fichero, era un buffer de RAM que ya no se usa. **Un límite que se queda
  después de que desaparezca su motivo no falla donde está: falla en el sitio
  equivocado y con el nombre equivocado.**

### What's new

- **"Too big for the flash" on a flash with 192 kB free.** An old ceiling was
  left in place requiring the whole `.map` to fit in the borrowed RAM (about
  45 kB), because the verification that followed needed it in memory. Since
  that verification reads from flash in chunks, the pool no longer has to hold
  the file — I removed the guard inside the module and **left the one
  outside**. The three-level map is 70,665 bytes, so it fired on every new
  map.
- And the message was wrong on both counts: it was neither the flash nor the
  size of the file, it was a RAM buffer that is no longer used. **A limit left
  behind after its reason disappears does not fail where it sits: it fails in
  the wrong place and under the wrong name.**

---

## V4.58 · 2026-10-12

### Qué cambia

- **Tres niveles de detalle: al ampliar ya no ves la misma costa estirada.**
  Hasta ahora el mapa tenía un solo nivel y el zoom 8 mostraba los datos del
  zoom 1 agrandados, con 8 píxeles de error. Ahora hay un nivel propio para el
  x4 y otro para el x8, y el error baja a **2,86 px**.
- **Tres y no cuatro, y la idea fue del usuario.** Medido: cuatro niveles piden
  263.000 B de flash —habría que subir el hueco de 192 a 256 kB y regenerar
  `BD.BIN`— y encima el zoom 2 troceado sale **peor** que el nivel entero
  (1,83 px contra 1,00). Con tres caben en 70.665 B, o sea en el hueco que ya
  existe, y el zoom 2 sale del x1 estirado con 2 px.
- **El x1 no se trocea a propósito**: al zoom 1 la ventana *es* el mundo, así
  que las casillas serían puro gasto.
- **De los niveles de arriba sólo se carga lo que se ve.** El mundo entero del
  x8 son 37.557 B y en la RAM caben 10.240; se leen de la flash las casillas
  que tocan la pantalla, con media pantalla de margen por cada lado.
- **Y arrastrar no recarga en cada fotograma.** Medido: un arrastre de punta a
  punta del mundo son 756 pasos y dispara **8 recargas, el 1,1%**. Sin el
  margen sería una por paso.
- **RAM: 72 bytes más.** 3.880 libres. El buffer no crece: el que manda sigue
  siendo el x1 entero.

### Tres fallos que encontró el banco

- **Los trazos había que partirlos por casilla, y el primer intento no lo
  hacía.** Metía cada trazo entero en la casilla de su primer punto. Medido:
  **seis trazos de 3.724 miden más de 45° de ancho** —el mayor, 189° con 5.914
  puntos— así que la caja de su casilla tapaba media pantalla y esa casilla se
  cargaba casi siempre. La ventana peor se llevaba el 83% del nivel.
- **Al volver del zoom 8 al 1, la fila de Información seguía enseñando los
  trazos de la ventana anterior** (373 en vez de 376) con los puntos ya
  buenos. Sólo se recontaba en el camino troceado. Una cuenta que no cambia
  cuando cambia lo que cuenta no está contando, está recordando.
- **El fichero ya no cabe en la RAM prestada para leerlo.** El v2 mide 70 kB y
  prestados hay 45, así que copiarlo entero dejó de ser posible. Ahora se abre
  leyendo de la flash en tres pasadas —la suma a trozos, la cabecera, y el x1
  al buffer— sin buffer grande.

### What's new

- **Three levels of detail: zooming in no longer shows the same coastline
  stretched.** The map had a single level, so zoom 8 showed zoom-1 data
  enlarged, with 8 pixels of error. There is now a level of its own for x4 and
  another for x8, and the error drops to **2.86 px**.
- **Three rather than four, and it was the owner's idea.** Measured: four
  levels need 263,000 B of flash —the slot would have to grow from 192 to
  256 kB and `BD.BIN` be regenerated— and zoom 2, tiled, comes out **worse**
  than the whole level (1.83 px against 1.00). Three fit in 70,665 B, in the
  slot that already exists, and zoom 2 comes from x1 stretched at 2 px.
- **x1 is deliberately not tiled**: at zoom 1 the window *is* the world, so
  tiles would be pure overhead.
- **Only what is on screen is loaded from the upper levels.** The whole x8
  world is 37,557 B and 10,240 fit in RAM; the tiles touching the screen are
  read from flash, with half a screen of margin on each side.
- **And dragging does not reload every frame.** Measured: a drag right across
  the world is 756 steps and triggers **8 reloads, 1.1%**. Without the margin
  it would be one per step.
- **RAM: 72 bytes more.** 3,880 free. The buffer does not grow: the x1 level,
  held whole, still sets its size.

### Three bugs the bench caught

- **Outlines had to be split by tile, and the first attempt did not.** It put
  each whole outline in the tile of its first point. Measured: **six outlines
  out of 3,724 are more than 45° wide** —the largest, 189° with 5,914 points—
  so their tile's bounding box covered half the screen and that tile was
  loaded almost always. The worst window took 83% of the level.
- **Coming back from zoom 8 to 1, the Information row still showed the
  previous window's outline count** (373 instead of 376) with the point count
  already correct. Only the tiled path recounted. A count that does not change
  when what it counts changes is not counting, it is remembering.
- **The file no longer fits in the RAM borrowed to read it.** The v2 is 70 kB
  and 45 are borrowed, so copying it whole stopped being possible. It is now
  opened by reading from flash in three passes —the checksum in chunks, the
  header, and x1 into the buffer— with no large buffer.

---

## V4.57 · 2026-10-12

### Qué cambia

- **Los mapas tenían 35 trazos de costa de los 3.751 que hay. Ahora 164, y
  212 de fronteras.** El empaquetador elegía mal: se quedaba siempre con la
  simplificación más fina y gastaba todo el presupuesto alisando costas
  mientras tiraba islas y países enteros. Medido, era tan malo que **con más
  memoria salían menos países**: a 16 kB daba 169 trazos y a 24 kB daba 107.
  Un criterio con el que dar más sitio empeora el mapa no mide lo que dice
  medir.
- **La regla nueva, en una frase:** la isla que se tira no puede ser mayor que
  el error que ya aceptas al simplificar. Si admites 0,3° de desvío en una
  costa, tirar una isla de 10° es incoherente. Con eso queda un solo
  parámetro y más presupuesto es siempre más mundo.
- **Y el nivel se elige en píxeles de la pantalla, no en bytes**: medio píxel
  de error, y no se tira nada que mida más de cinco píxeles. El presupuesto
  dice lo que cabe, no lo que hace falta.
- **Fronteras en los cien.** Cincuenta tipos las llevaban apagadas.
- **El buffer del mapa sube de 8.192 a 10.240 bytes.** Con 8 kB el mundo no
  cabe: habría que simplificar hasta tirar 3.700 trazos de costa otra vez.
  Cuesta 2 kB de RAM y deja 3.952 libres.
- Hay que **volver a copiar el `.map`** al pendrive: los cien están rehechos
  y los viejos tienen el mundo recortado.

### What's new

- **The maps carried 35 coastline outlines out of the 3,751 that exist. Now
  164, plus 212 of borders.** The packer was choosing badly: it always took
  the finest simplification, spending the whole budget smoothing coastlines
  while discarding entire islands and countries. Measured, it was bad enough
  that **more memory produced fewer countries**: 169 outlines at 16 kB and 107
  at 24 kB. A criterion where more room makes the map worse is not measuring
  what it claims to.
- **The new rule, in one sentence:** the island you discard cannot be larger
  than the error you already accept when simplifying. If you allow 0.3° of
  deviation on a coastline, dropping a 10° island is incoherent. That leaves
  a single parameter, and more budget always means more world.
- **And the level is chosen in screen pixels, not bytes**: half a pixel of
  error, and nothing smaller than five pixels is discarded. The budget says
  what fits, not what is needed.
- **Borders on all one hundred.** Fifty types had them switched off.
- **The map buffer grows from 8,192 to 10,240 bytes.** The world does not fit
  in 8 kB: it would mean discarding 3,700 coastline outlines again. It costs
  2 kB of RAM and leaves 3,952 free.
- You need to **copy the `.map` to the stick again**: all one hundred are
  rebuilt and the old ones carry a trimmed world.

---

## V4.56 · 2026-10-12

### Qué cambia

- **Arreglada la medida de la referencia interna, que en la radio no medía.**
  La fila «Calibrar voltaje» marcaba 3,00 V clavados, y esos 3,00 V no eran
  una medida: eran el valor de reserva. Faltaban tres cosas, las tres del
  periférico y ninguna de la cuenta — limpiar la bandera de fin de conversión
  **antes** de disparar (si quedaba puesta de la conversión anterior se leía
  el dato viejo, que es el de otro canal), esperar a que la referencia
  arranque tras encenderla, y tirar la primera conversión después de cambiar
  de canal.
- **Y ahora la fila lo dice cuando no mide.** Pone «sin ref.» y el número
  crudo del conversor en vez de un voltaje. Un valor de reserva enseñado como
  si fuera una medida es lo que escondió este fallo una versión entera: en
  pantalla, 3,00 V de reserva y 3,00 V medidos se escriben igual.
- **La fila «Mapa» explica también el paso de grabar.** Si lo que falla es
  antes del lector —no se encontró el fichero, no se pudo copiar, no hay nada
  guardado— el lector decía «el de serie», que es verdad y no dice nada. El
  cartel del arranque sí lo decía, pero dura tres segundos.

### What's new

- **Fixed the internal-reference measurement, which was not measuring on the
  radio.** The «Voltage calib.» row read exactly 3.00 V, and those 3.00 V were
  not a measurement: they were the fallback value. Three things were missing,
  all of them in the peripheral and none in the arithmetic — clearing the
  end-of-conversion flag **before** triggering (if it was left set from the
  previous conversion the old data was read, which belongs to another
  channel), waiting for the reference to start up after switching it on, and
  discarding the first conversion after a channel change.
- **And the row now says so when it cannot measure.** It shows «no ref.» and
  the raw converter count instead of a voltage. A fallback displayed as a
  measurement is what hid this bug for a whole release: on screen, a fallback
  3.00 V and a measured 3.00 V look identical.
- **The «Map» row now also explains the storing step.** When the failure is
  before the reader —no file found, the copy failed, nothing stored— the
  reader said "the built-in one", which is true and says nothing. The boot
  notice did say it, but it only lasts three seconds.

---

## V4.55 · 2026-10-12

### Qué cambia

- **El voltímetro ya no se fía del regulador de la placa.** Hasta ahora la
  medida de batería se hacía contra una constante de 3.300 mV... en una placa
  que lleva un regulador de **3 V**. O sea que al error de unidad a unidad se
  le sumaba un error sistemático de un diez por ciento en todas. Ahora se mide
  la referencia interna de 1,2 V del propio micro y de ahí sale lo que de
  verdad vale el raíl, que es contra lo que se mide todo lo demás.
- **Medido, no supuesto: el abanico entre unidades baja de 348 mV a 4 mV.**
  Diez radios con el regulador entre 2,85 y 3,15 V midiendo la *misma* batería
  de 3,90 V leían entre 3,75 y 4,10 V; ahora leen entre 3,896 y 3,900.
- **El termómetro del chip llevaba el mismo error** y se arregla de paso: eran
  unos 4 grados, no los 20 que yo me esperaba. Queda escrito lo que es.
- **La caída del diodo vuelve a su valor real (700 mV).** Estaba puesta en 400
  "a ojo", y ahora se entiende por qué: tapaba parte del error de la
  referencia. Eran dos errores que se restaban, que es peor que un error solo
  — al arreglar uno, el otro aparece entero.
- **Y una fila para calibrar contra un multímetro**, en Información ·
  *Calibrar voltaje*. Enseña los tres números que hacen falta para decidir —lo
  que mide el raíl, lo que cree que tiene la batería y el ajuste puesto— y
  cada toque sube 50 mV, volviendo abajo al llegar arriba. Se guarda en
  `CONFIG.CSV` como `vcalib_mv`.
- **La fila Batería enseña ahora los voltios**, no sólo el porcentaje: si el
  número está mal, un porcentaje no deja ver cuánto ni por dónde mirar.

### What's new

- **The voltmeter no longer trusts the board's regulator.** Until now the
  battery measurement used a hard-coded 3,300 mV reference... on a board
  fitted with a **3 V** regulator. On top of the unit-to-unit spread, that
  added a systematic ten per cent error to every radio. It now measures the
  MCU's own 1.2 V internal reference and derives the real rail voltage from
  it, and everything else is measured against that.
- **Measured, not assumed: the spread between units drops from 348 mV to
  4 mV.** Ten radios with regulators between 2.85 and 3.15 V reading the
  *same* 3.90 V battery used to report 3.75 to 4.10 V; they now report 3.896
  to 3.900.
- **The chip thermometer carried the same error** and is fixed along the way:
  about 4 degrees, not the 20 I expected. The real figure is written down.
- **The diode drop goes back to its real value (700 mV).** It had been set to
  400 "by eye", and now it is clear why: it was masking part of the reference
  error. Two errors that cancelled, which is worse than one — fix one and the
  other shows up whole.
- **And a row to calibrate against a multimeter**, under Information ·
  *Voltage calib.* It shows the three numbers you need to decide —the measured
  rail, the battery reading and the correction in force— and each tap adds
  50 mV, wrapping round at the top. Stored in `CONFIG.CSV` as `vcalib_mv`.
- **The Battery row now shows volts**, not just a percentage: if the number is
  wrong, a percentage does not show by how much or where to look.

---

## V4.54 · 2026-10-12

### Qué cambia

- **El `.map` ya se graba en la flash, con su cartel, igual que `BD.BIN`.**
  Antes se leía del pendrive en cada arranque y el fichero se quedaba ahí
  para siempre, sin que la pantalla dijera ni una palabra: para enterarte de
  que tu mapa no valía tenías que ir a Información y mirar una fila. Ahora
  sale el mismo cartel que la base de datos de aviones —buscando, copiando
  con su porcentaje, comprobando— y al terminar **el fichero se borra del
  disco**.
- **Y se borra el último de todo, nunca antes.** Primero se copia, luego se
  comprueba la suma de lo que ha quedado grabado, y sólo entonces se borra
  del pendrive. Mientras la copia no esté verificada, el fichero del disco es
  la única copia que existe de ese mapa.
- **Si algo sale mal, el cartel dice qué.** No es un `.map`, versión que no
  conozco, la suma no cuadra, no cabe, no tiene trazos, el punto no se vería
  sobre su mar, o la base de datos ocupa su sitio. Tres segundos en pantalla,
  en vez de un silencio.
- **Un botón para volver al mapa de siempre.** En Información, la fila *Mapa*
  se toca una vez para armarla y otra para borrar el mapa de la flash. Sin
  mapa guardado, sale el de serie. Dos toques, como el de DFU, porque borrar
  de un roce un mapa que copiaste a propósito sería perderlo sin querer.
- **Una copia cortada por la mitad no deja medio mapa.** Lo primero que se
  hace al grabar es borrar la cabecera del hueco, así que si se va la luz en
  mitad de la copia la radio arranca con el mapa de serie y no con basura en
  pantalla. Es la prueba que más trabajo costó dejar bien medida.
- **El mapa se queda con 192 kB de la zona alta**, pegados al techo y fuera
  del directorio —si fuera una sección más, el siguiente `BD.BIN` lo borraría
  sin avisar—. El techo de `BD.BIN` baja de 1.044.480 a **847.872 bytes**, que
  sigue siendo el doble de lo que ocupan hoy las dos bases juntas.
- **Un `BD.BIN` antiguo que llegue hasta ese hueco no se pierde.** La radio lo
  sigue cargando igual; lo que hace es negarse a grabar el mapa y decir por
  qué. Para poder grabarlo, se regenera `BD.BIN` con `tools/datos_pack.py`,
  que ya lleva el techo nuevo.

### Fallo corregido

- **Un `.map` con relleno detrás se daba por roto.** El lector ponía la suma
  en los últimos cuatro bytes de lo que le dieran, o sea se fiaba de que el
  tamaño que traía el sistema de ficheros fuera exactamente el del mapa. Un
  fichero de 7.195 bytes que ocupa dos bloques de 4 kB podía llegar como
  8.192, y entonces decía «la suma no cuadra» sobre un fichero perfecto —y
  encima mandaba a buscar el problema al sitio equivocado—. Ahora el final
  del fichero lo dice su propia tabla de niveles, no quien lo trae.

### What's new

- **The `.map` is now stored in flash, with an on-screen notice, just like
  `BD.BIN`.** It used to be read from the memory stick at every boot and the
  file stayed there forever, with the screen saying nothing: to find out your
  map had been refused you had to go to Information and read a row. Now you
  get the same notice as the aircraft database —searching, copying with a
  percentage, checking— and when it finishes **the file is deleted from the
  disk**.
- **And it is deleted last, never before.** Copy first, then verify the
  checksum of what was written, and only then delete from the stick. Until the
  copy is verified, the file on the disk is the only copy of that map.
- **If something goes wrong, the notice says what.** Not a `.map`, unknown
  version, checksum mismatch, too big, no outlines in it, markers would not
  show, or the database is in its place. Three seconds on screen instead of
  silence.
- **A button to go back to the usual map.** In Information, the *Map* row is
  tapped once to arm it and again to erase the stored map. With no stored map,
  the built-in one is used. Two taps, like the DFU row, because losing a map
  you copied on purpose to a stray touch would be a bad trade.
- **An interrupted copy does not leave half a map.** The first thing the write
  does is erase the slot header, so if the power goes during the copy the
  radio boots with the built-in map rather than garbage on screen. That is the
  case that took the most work to measure properly.
- **The map takes 192 kB of the high area**, at the very top and outside the
  directory —as one more section, the next `BD.BIN` would wipe it silently—.
  The `BD.BIN` ceiling drops from 1,044,480 to **847,872 bytes**, still twice
  what both databases take together today.
- **An older `BD.BIN` that reaches into that slot is not lost.** The radio
  still loads it; what it does is refuse to store the map and say why. To
  store one, regenerate `BD.BIN` with `tools/datos_pack.py`, which already
  carries the new ceiling.

### Bug fixed

- **A `.map` with padding after it was treated as broken.** The reader took
  the checksum from the last four bytes of whatever it was handed, i.e. it
  trusted that the size reported by the filesystem was exactly the size of the
  map. A 7,195-byte file occupying two 4 kB blocks could arrive as 8,192, and
  it then reported "checksum mismatch" about a perfectly good file —and sent
  you looking in the wrong place—. The end of the file is now derived from its
  own level table, not from whoever brought it.

---

## V4.53 · 2026-10-12

### Qué cambia

- **El mapa del mundo ya lo puede traer el usuario, en un fichero.** Dejas un
  `.map` en el pendrive y la radio lo coge al arrancar. Hay **cien** para
  elegir, cada uno con su color de mar, de costa, de fronteras y de los
  puntos. No es una foto: son trazos, igual que el mapa de serie, porque una
  foto del mundo a esta pantalla son 460.800 bytes y **370 ms por repintado**
  — arrastrar el mapa iría a tres fotogramas por segundo, y además no se
  podría ampliar.
- **El nombre da igual**: se busca por la extensión, así que `27.map` vale tal
  cual, sin renombrar nada.
- **Más mundo en la misma memoria.** Los puntos se guardan cuantizados a 1/32
  de grado —medio píxel al zoom más grande— y por incrementos de 8 bits.
  Medido: **2,5 bytes por punto contra los 4,2 de antes**. Donde cabían 1.762
  puntos caben ahora 3.112, con fronteras de países incluidas.
- **El color de los puntos lo dice el mapa, y es el mismo en los cinco modos.**
  Antes había diez colores a pelo —verde FT8, ámbar aviones, azul barcos,
  salmón WSPR, lila JTTY—. No eran una leyenda: el mapa se dibuja en una
  cadena de `else-if`, así que nunca hay dos modos poniendo puntos a la vez, y
  cada uno de esos diez valores aparecía **una sola vez** en todo el firmware.
  Ahora el punto va del color que diga el fichero, que es quien sabe de qué
  color es su propio mar.
- **Y un mapa cuyo punto no se vea sobre su mar se rechaza**, en el
  empaquetador y en la radio. Un mapa así parece una radio que no decodifica
  nada, que es el peor fallo posible: uno que miente sobre el receptor.
- Fila nueva en Información: **«Mapa»**, que dice si está puesto el del
  fichero o el de serie, y si no está, **por qué** — "la suma no cuadra" se
  arregla volviendo a copiarlo; "no cabe en la RAM", cogiendo otro.

### What's new

- **The world map can now come from the user, in a file.** Drop a `.map` on
  the memory stick and the radio picks it up at boot. There are **a hundred**
  to choose from, each with its own sea, coastline, border and marker colours.
  It is not a photo: it is outlines, like the built-in map, because a photo of
  the world at this screen size is 460,800 bytes and **370 ms per repaint** —
  dragging the map would run at three frames per second, and it could not be
  zoomed either.
- **The name does not matter**: the radio looks for the extension, so `27.map`
  works as it is.
- **More world in the same memory.** Points are quantised to 1/32 of a degree
  —half a pixel at the deepest zoom— and stored as 8-bit increments. Measured:
  **2.5 bytes per point against the old 4.2**. Where 1,762 points fitted,
  3,112 now do, country borders included.
- **The marker colour comes from the map, and it is the same in all five
  modes.** There used to be ten hardcoded colours. They were not a legend: the
  map is drawn in an `else-if` chain, so two modes never place markers at the
  same time, and each of those ten values appeared **exactly once** in the
  whole firmware. The marker now takes the colour the file gives it, since the
  file is what knows the colour of its own sea.
- **A map whose marker would not show against its sea is refused**, both by
  the packer and by the radio. Such a map looks like a receiver that decodes
  nothing, which is the worst kind of bug: one that lies about the radio.
- New Information row, **«Map»**: whether the file's map or the built-in one is
  in use and, when it is not, **why**.

---

## V4.52 · 2026-10-11

### Qué cambia

- **La radio adivina sola el modo de SSTV.** Hasta ahora, si te enganchabas a
  mitad de una foto la cabecera ya había pasado y no quedaba más remedio que
  elegir el modo a dedo entre catorce. Ahora lo mide del ritmo de la señal:
  cada línea empieza con un pitido, así que la señal dice cada cuánto llega
  una línea y cuánto dura esa marca, y con esos dos números los catorce modos
  se separan solos. **Acierta los catorce en cuatro líneas** —entre medio
  segundo y cuatro, según el modo— y la foto sale colocada desde la primera.
  La chapa pone «Buscando el modo» mientras tanto, y el nombre lleva un
  asterisco cuando lo ha medido ella en vez de leerlo de la cabecera.
  Viene puesto de fábrica; si prefieres decírselo tú, toca la chapa y elige
  uno de los catorce, y para devolvérselo elige «Auto», que es la primera
  celda de esa lista.
- **Y se corrige sola si el modo puesto no es el que viene.** Un modo que vino
  de la cabecera no se toca nunca —eso es un dato—, pero uno medido se vuelve
  a medir mientras pinta.
- **SSTV aguanta ahora mucho más ruido y mucha más sintonía corrida.** El
  corte del pulso de sincronismo era un número fijo; ahora se mide contra la
  propia señal. Medido: el modo se adivina con la emisora llegando **hasta
  500 Hz alta** (antes, 100) y con tres veces más ruido. Donde la cabecera no
  se puede leer de lo mala que viene la señal, ahora sale foto igual.
- **Arreglado un desvío de sintonía inventado.** Sin cabecera, el detector de
  VIS se dejaba llevar por la propia imagen y guardaba hasta 400 Hz de
  corrección que no había medido nadie. Con eso puesto, la foto salía oscura y
  los sincronismos se perdían. Ahora un desvío sólo vale si lo ganó una
  cabecera leída de verdad.
- **El disco USB ya no puede anunciar más flash de la que hay.** La radio mide
  el tamaño del chip buscando dónde da la vuelta la dirección; cuando no lo
  encontraba se quedaba con su tope —16 MB— y lo daba por medido. Un chip de
  8 MB que no dé la vuelta se anunciaba de 16, y entonces Windows escribe en
  bloques que no existen y lo escrito se come el principio del volumen. Ahora
  las dos fuentes —la medida y el código JEDEC del chip— se cruzan y se coge
  la pequeña.

### What's new

- **The radio now works out the SSTV mode by itself.** Until now, if you
  latched on mid-picture the header was long gone and the only way out was
  picking the mode by hand from fourteen. Now it measures it from the rhythm
  of the signal: every line starts with a beep, so the signal says how often a
  line arrives and how long that mark lasts, and those two numbers separate
  the fourteen modes on their own. **It gets all fourteen right in four
  lines** —between half a second and four, depending on the mode— and the
  picture comes out aligned from the first one. The chip says «Finding the
  mode» meanwhile, and the name carries an asterisk when the radio measured it
  instead of reading it from the header. It ships switched on; if you would
  rather tell it yourself, tap the chip and pick one of the fourteen, and to
  hand it back pick «Auto», the first cell in that list.
- **And it corrects itself if the mode in use is not the one arriving.** A mode
  that came from the header is never touched —that is a fact— but a measured
  one keeps being measured while it paints.
- **SSTV now takes far more noise and far more mistuning.** The sync-pulse
  threshold was a fixed number; now it is measured against the signal itself.
  Measured: the mode is found with the station arriving **up to 500 Hz high**
  (it was 100) and with three times the noise. Where the header is too noisy
  to read, a picture now comes out anyway.
- **Fixed an invented tuning offset.** With no header, the VIS detector was
  led along by the picture itself and stored up to 400 Hz of correction nobody
  had measured. With that in place the picture came out dark and the syncs got
  lost. An offset is now only kept if a header that was actually decoded
  earned it.
- **The USB disk can no longer claim more flash than the chip has.** The radio
  measures the chip by looking for where the address wraps around; when it
  could not find it, it kept its own ceiling —16 MB— and treated that as a
  measurement. An 8 MB chip that does not wrap was announced as 16, and then
  Windows writes into blocks that do not exist and what it writes eats the
  start of the volume. The measurement and the chip's own JEDEC code are now
  cross-checked and the smaller one wins.

---

## V4.51 · 2026-10-11

### Qué cambia

- **La reducción de ruido ya funciona en SAM.** Y hasta ahora era peor que
  "no funciona": el botón de Ruido no está limitado por modo, así que en SAM
  se encendía, se quedaba encendido y ponía **«activo»** en la barra… sin
  tocar el audio. Ahora hace exactamente lo mismo que en AM.
- **SAM con una sola banda: dos botones nuevos, SAM-U y SAM-L.** El detector
  síncrono puede quedarse sólo con la banda de arriba o sólo con la de abajo.
  Si tienes una emisora pisándote por un lado, **desaparece**: el rechazo va
  de 16 dB en los graves a 40 dB en los agudos. Pruébalos los dos y quédate
  con el que suene mejor — depende de por dónde te estén pisando, y no hay
  forma de saberlo de antemano. En AM normal esto no se puede hacer: el
  detector de envolvente ve las dos bandas sumadas.

### What's new

- **Noise reduction now works in SAM.** And until now it was worse than "it
  doesn't work": the Noise button is not gated by mode, so in SAM you could
  switch it on, it stayed on, and it read **"on"** in the bar… without
  touching the audio. It now does exactly what it does in AM.
- **Single-sideband SAM: two new buttons, SAM-U and SAM-L.** The synchronous
  detector can keep only the upper or only the lower sideband. If a station
  is sitting on top of you from one side, it **goes away**: rejection runs
  from 16 dB at the low end to 40 dB at the top. Try both and keep whichever
  sounds better — it depends on which side the interference is coming from,
  and there is no telling in advance. Plain AM cannot do this: an envelope
  detector sees both sidebands summed together.

---

## V4.50 · 2026-10-11

### Qué cambia

- **El mando del NR por fin sirve en todo su recorrido.** Hasta ahora el
  umbral era un número absoluto, así que el punto donde el reductor
  funcionaba **dependía del nivel que llegara** — y eso lo mueven el AGC, el
  desvanecimiento y la banda. Ponías el mando donde iba bien, la señal subía
  cuatro veces y el NR dejaba de hacer nada. Ahora el umbral se mide contra
  el **suelo de ruido**: el mando elige cuántas veces por encima del ruido
  hay que estar, y eso quiere decir lo mismo con cualquier señal. El punto
  bueno queda en el centro del mando y no se mueve. Y **ninguna posición
  empeora el audio**: antes media carrera o no hacía nada o se llevaba la voz
  por delante.
- **Y ya no se come el CW ni las portadoras.** El suelo se mide ahora también
  mirando los bins vecinos, porque un suelo de ruido es liso en frecuencia y
  un tono es un pico. Un tono sostenido sale entero, con 10 dB menos de ruido
  encima.

### What's new

- **The noise reduction knob is now useful across its whole travel.** Until
  now the threshold was an absolute number, so the point where the reducer
  actually worked **depended on the incoming level** — which the AGC, fading
  and the band all move. You set the knob where it sounded right, the signal
  came up four times, and the NR stopped doing anything. The threshold is now
  measured against the **noise floor**: the knob picks how far above the noise
  a bin must be, which means the same thing for any signal. The sweet spot
  sits in the middle of the travel and stays there. And **no knob position
  makes the audio worse**: before, half the travel either did nothing or ate
  the voice.
- **And it no longer eats CW or carriers.** The floor is now estimated by
  looking at neighbouring bins too, because a noise floor is smooth across
  frequency while a tone is a peak. A sustained tone comes through intact,
  with 10 dB less noise on top of it.

---

## V4.49 · 2026-10-11

### Qué cambia

- **HFDL decodifica un tipo de trama más.** De los ocho "giros" que usa HFDL
  (velocidad y ranura), dos no cabían en la memoria prestada y se perdían sin
  intentarlos siquiera: el contador `X` de la chapa los cuenta. Midiendo en
  13.264 salieron **20 de 94 ráfagas — el 21 %** — y la letra decía que eran
  del giro 6, que cabe pagando 5.872 bytes. Pagados: ahora entran **siete de
  los ocho**. El que queda fuera, el giro 7, pide 25.200 bytes más de los que
  hay libres en toda la radio, así que ése no entra con ningún arreglo
  razonable. El giro 6 es 1200 bps con doble entrelazado — las tramas largas,
  que es donde suelen venir las posiciones.

### What's new

- **HFDL now decodes one more frame type.** Of the eight "rates" HFDL uses
  (speed and slot), two did not fit in the borrowed memory and were dropped
  without even being attempted — the `X` counter on the badge counts them.
  Measured on 13.264, that was **20 bursts out of 94 — 21 %** — and the letter
  said they were rate 6, which fits for 5,872 bytes. Paid: **seven of the eight
  now fit**. The one still out, rate 7, needs 25,200 bytes more than the whole
  radio has free, so no reasonable fix gets it in. Rate 6 is 1200 bps with
  double interleaving — the long frames, which is where position reports
  usually travel.

---

## V4.48 · 2026-10-11

### Qué cambia

- **FM: vuelven las emisoras que no entraban.** La continua del convertidor
  se sumaba a la I y a la Q justo donde está la portadora, y el quitador de
  continua que había estaba **detrás** del discriminador, donde ya no puede
  hacer nada. El efecto no era una degradación suave: **cualquier emisora
  cuya portadora quedara por debajo de ese offset era imposible de
  demodular**, porque el vector deja de dar la vuelta al origen y no hay
  frecuencia que sacar. Ahora se quita delante, y una emisora con la
  portadora 2,5 veces por debajo del offset sale exactamente igual que si
  no hubiera offset. La corrección va acotada a lo que un offset de
  hardware puede ser, para que no pueda confundir una portadora poco
  modulada con continua y comérsela.
- **El NR rechaza las llamadas con la longitud equivocada** en vez de
  aceptarlas y devolver audio roto en silencio. El firmware ya llamaba
  bien; esto es para que no pueda volver a pasar sin que se note.

### What's new

- **FM: stations that would not come in now do.** The converter's DC offset
  was adding to I and Q exactly where the carrier sits, and the existing DC
  blocker was **after** the discriminator, where it can do nothing about it.
  The effect was not a gradual degradation: **any station whose carrier fell
  below that offset was impossible to demodulate at all**, because the
  vector stops encircling the origin and there is no frequency left to
  recover. It is now removed ahead of the discriminator, and a station with
  its carrier 2.5x below the offset comes out exactly as if there were no
  offset. The correction is capped at what a hardware offset can plausibly
  be, so it cannot mistake a lightly modulated carrier for DC and eat it.
- **The noise reduction now rejects calls with the wrong block length**
  instead of accepting them and quietly returning broken audio. The firmware
  already called it correctly; this makes sure it cannot happen unnoticed
  again.

---

## V4.47 · 2026-10-11

### Qué cambia

- **Arrastrar el mapa con el dedo ya no va a trompicones.** El táctil llevaba
  un filtro que, si una lectura se separaba más de 40 px de la anterior, la
  daba por mala y contestaba *"no hay dedo"* — y quien lee eso da el arrastre
  por terminado. Pero esos 40 px no son una velocidad: entre dos lecturas
  separadas 30 ms son sólo 257 mm/s, y un dedo arrastrando algo por la pantalla
  hace entre 100 y 600 mm/s. O sea que el filtro llamaba imposible a un gesto
  normal, y como el bucle no corre a ritmo fijo, el mismo gesto unas veces
  pasaba y otras no. Ahora el tope se mide en píxeles por milisegundo, y un
  salto grande ya no se contesta como una suelta: se espera a la lectura
  siguiente, que es quien dice si fue el dedo o un artefacto del panel.
- **De paso, un toque ya no puede contar dos veces.** La suelta inventada
  disparaba la acción del botón que estuviera pulsado y, al rearmarse, la
  volvía a disparar al soltar de verdad. Bastaba con deslizar el dedo 40 px sin
  levantarlo.

### What's new

- **Dragging the map with your finger no longer stutters.** The touch panel had
  a filter that, whenever a reading moved more than 40 px from the previous
  one, rejected it and answered *"no finger"* — and whatever reads that treats
  the drag as finished. But 40 px is not a speed: across two readings 30 ms
  apart it is only 257 mm/s, and a finger dragging something across the screen
  does 100 to 600 mm/s. The filter was calling a normal gesture impossible,
  and since the main loop does not run at a fixed rate, the same gesture passed
  sometimes and not others. The limit is now measured in pixels per
  millisecond, and a large jump is no longer answered as a release: it waits
  for the next reading, which decides whether it was the finger or a panel
  artefact.
- **And a single tap can no longer count twice.** The invented release fired
  the action of whatever button was held and, on re-arming, fired it again on
  the real release. Sliding your finger 40 px without lifting it was enough.

---

## V4.46 · 2026-10-11

### Qué cambia

- **El mando hace zoom en el mapa.** Con el mapa abierto —en FT8, HFDL, AIS,
  JTTY o WSPR— girar el mando acerca y aleja, en los mismos pasos que los
  botones de la esquina (x1, x2, x4, x8) y anclando en el centro de lo que
  estás mirando. Los botones siguen estando. Si tienes el mando enganchado al
  volumen, el giro sigue siendo del volumen: mirando el mapa se puede querer
  bajarlo igual que en cualquier otra pantalla.

### What's new

- **The encoder now zooms the map.** With the map open —in FT8, HFDL, AIS, JTTY
  or WSPR— turning the encoder zooms in and out, in the same steps as the
  corner buttons (x1, x2, x4, x8), anchored on the centre of what you are
  looking at. The buttons are still there. If the encoder is assigned to
  volume, turning it still changes the volume: looking at the map you may well
  want to turn it down, same as on any other screen.

---

## V4.45 · 2026-10-11

### Qué cambia

Una revisión a fondo de pintado y de refrescos. Lo que se nota:

- **La raya de color de la banda salía con cuatro muescas.** Las marcas de la
  regla se dibujaban encima, en su misma fila. Ahora bajan una fila: la raya
  sale entera del color que le toca.
- **El cuarto botón del panel digital ya no desaparece.** Sin botón de mapa,
  una chapa larga —como la del entrelazador de STANAG— le comía el sitio hasta
  dejarlo en ancho cero.
- **Cosas que no se refrescaban**, todas de la misma familia: el volumen y el
  paso de sintonía cambiaban arriba y la casilla de abajo se quedaba con lo
  anterior; el detalle del filtro decía un corte mientras el mando movía el
  otro; los presets del filtro no actualizaban el chip BW; el teclado de
  frecuencia no repintaba la barra al abrirse; y salir de Hora por radio
  devolvía la frecuencia sin repintar la cabecera.
- **El indicador de página** decía «1 de :» a partir de diez páginas.

### What's new

A thorough review of drawing and refreshes. What you will notice:

- **The band colour rule had four nicks in it.** The ruler ticks were drawn on
  top of it, on the same row. They now start one row lower: the rule comes out
  whole, in its own colour.
- **The fourth button on the digital panel no longer vanishes.** With no map
  button, a long chip — such as STANAG's interleaver status — ate its space
  until it was zero wide.
- **Things that were not refreshing**, all of the same family: volume and
  tuning step changed at the top while the tile at the bottom kept the old
  value; the filter detail named one cut-off while the encoder moved the other;
  the filter presets did not update the BW chip; the frequency keypad did not
  repaint the bottom bar on opening; and leaving the radio-time screen restored
  the frequency without repainting the header.
- **The page indicator** read «1 of :» from ten pages on.

---

## V4.44 · 2026-10-11

### Qué cambia

- **La pastilla de la banda ya no se corta al cambiar a un nombre más largo.**
  Poniendo SSTV 20 se leía «SSTV 2» con el borde partido a media letra, y se
  arreglaba solo al rato. El repintado rápido —el de cada clic del mando—
  recortaba usando el ancho de la etiqueta **anterior**, así que la nueva, más
  ancha, no cabía. Ahora, si la etiqueta ha crecido, se repinta en el acto.

### What's new

- **The band chip no longer gets clipped when switching to a longer name.**
  Selecting SSTV 20 showed «SSTV 2» with the border cut mid-letter, and it
  fixed itself a while later. The fast repaint —the one on every encoder
  click— was clipping to the **previous** label's width, so the new, wider one
  did not fit. Now, if the label has grown, it repaints right away.

---

## V4.43 · 2026-10-11

### Qué cambia

- **La raya dice de qué clase es la banda, no solo si estás dentro.** En 7.190
  estás en 40 m de aficionados y en 7.210 en 41 m de radiodifusión, y antes la
  raya era verde en las dos. Ahora: **verde** aficionados, **azul**
  radiodifusión, **ámbar** utilidades, **rojo** VHF/UHF y **gris** fuera de
  banda. El borde de la chapa de la banda usa el mismo código. Los cinco
  colores salen de la paleta —así manda el tema— y se eligieron midiendo la
  distancia entre ellos en las ocho paletas, porque en varias hay colores que
  se confunden.
- **Las ocho de Bahréin dejan de ir aparte** en las frecuencias de HFDL y se
  reparten por frecuencia como todas: las de menos de 10 MHz a la noche y las
  demás al día. 13.312 sale al lado de 13.303, que es donde tiene que estar.
- **Las páginas de las rejillas se reparten iguales**: un grupo de 16 sale como
  8+8 en vez de 12+4, que parecía un error.
- **El botón NR de arriba y la casilla Ruido de abajo ya se enteran el uno del
  otro.** Son el mismo interruptor en dos sitios; al tocar el de arriba, el de
  abajo se quedaba con lo de antes.

### What's new

- **The rule now says which kind of band you are in, not just whether you are
  in one.** 7,190 is the 40 m ham band and 7,210 is the 41 m broadcast band,
  and the rule used to be green for both. Now: **green** ham, **blue**
  broadcast, **amber** utility, **red** VHF/UHF and **grey** out of band. The
  band chip's border follows the same code. All five come from the palette —so
  the theme decides— and were picked by measuring the distance between them
  across all eight palettes, because several have colours that clash.
- **Bahrain's eight no longer sit apart** in the HFDL frequencies: they are
  spread by frequency like the rest, below 10 MHz at night and the others by
  day. 13,312 now sits next to 13,303, where it belongs.
- **Grid pages split evenly**: a group of 16 comes out as 8+8 instead of 12+4,
  which looked like a mistake.
- **The NR button at the top and the Noise tile at the bottom now agree.** They
  are the same switch shown twice; touching the top one left the bottom one
  showing the old state.

---

## V4.42 · 2026-10-11

### Qué cambia

- **El botón que salta de frecuencia en HFDL ahora barre de verdad.** Iba por
  el orden interno de la tabla —agrupada por noche, día y Bahréin— así que de
  13.303 saltaba a 15.025 y 13.312 quedaba diez toques más allá, al final.
  Ahora sube en frecuencia: de 13.303 el siguiente es **13.312**. Y si tecleas
  una frecuencia a mano, te lleva a la siguiente por encima en vez de al
  principio de la lista.
- **La rejilla de Frec. se abre en la página donde estás**, no siempre en la
  primera. Si estás en una frecuencia de Bahréin, abre en Bahréin.
- **El botón de frecuencia se enciende en ámbar** cuando la que tienes puesta
  es una de la lista. Antes enseñaba la frecuencia estuviera o no en la tabla,
  y era imposible distinguirlo.
- **La raya entre el espectro y la cascada es verde dentro de banda y gris
  fuera**, y la chapa de la banda lleva el borde ámbar cuando estás dentro.

### What's new

- **The frequency-stepping button in HFDL now really sweeps.** It followed the
  table's internal order —grouped by night, day and Bahrain— so 13.303 jumped
  to 15.025 and 13.312 sat ten taps away at the end. It now climbs in
  frequency: after 13.303 comes **13.312**. And if you key in a frequency by
  hand, it takes you to the next one above instead of back to the start.
- **The Freq. grid opens on the page you are on**, not always on the first one.
  On a Bahrain frequency, it opens on Bahrain.
- **The frequency button lights up amber** when the one you are on belongs to
  the list. It used to show the frequency whether or not it was in the table,
  with no way to tell.
- **The rule between spectrum and waterfall is green in-band and grey out**,
  and the band chip carries an amber border when you are inside.

---

## V4.41 · 2026-10-10

### Qué cambia

- **HFDL perdía ráfagas sin decirlo.** La interrupción de audio y el bucle
  principal escribían la misma memoria sin ningún cerrojo: si el bucle
  principal tardaba, la ráfaga siguiente se abría encima de la que se estaba
  rematando y se perdían las dos. Con el CRC no pasaba ninguna, así que no se
  veía un fallo, se veía una radio que decodifica menos. Puesto el cerrojo. Si
  llega a pasar, la chapa lo dice con una **`P`**.
- **Encender el notch pegaba un golpe de 42 dB.** Al encenderlo, la línea de
  retardo queda a cero y durante las primeras muestras el lazo calculaba su
  paso con una ventana casi vacía: el peso se disparaba y la salida saturaba.
  Ahora no adapta hasta que la línea está llena —unos 6 ms— y el audio sale
  tal cual mientras tanto. Medido: de +42 dB a 0 dB, sin tocar el
  funcionamiento normal.
- **1.040 bytes de RAM libres** y la radio baja del 90 %: el osciloscopio
  copiaba 1 kB para dividirlo por uno, y había dos contadores de CRC que no
  leía nadie.
- **El manual se puede volver a generar.** Subir la versión rompía el manual
  en inglés, porque las traducciones se buscaban por el texto español con la
  versión dentro. Arreglado, manual regenerado y atado a las comprobaciones.
- **Y una red en el decodificador de JTTY**: un índice al que le faltaba un
  byte para salirse del anillo de filas. No fallaba, pero bastaba con subir una
  constante sin mirar; ahora el compilador se planta antes de que llegue a la
  radio.

### What's new

- **HFDL was losing bursts silently.** The audio interrupt and the main loop
  wrote the same memory with no lock at all: if the main loop was slow, the
  next burst opened on top of the one being finished and both were lost.
  Neither passed CRC, so it did not look like a fault — it looked like a radio
  that decodes less. The lock is in. If it ever happens, the chip says so with
  a **`P`**.
- **Switching the notch on produced a 42 dB thump.** On switching on, the
  delay line is zeroed, and for the first samples the loop computed its step
  from a nearly empty window: the weight blew up and the output clipped. It
  now does not adapt until the line is full —about 6 ms— and the audio passes
  through meanwhile. Measured: from +42 dB down to 0 dB, with normal operation
  untouched.
- **1,040 bytes of RAM freed** and the radio drops below 90 %: the scope was
  copying 1 kB to divide it by one, and two CRC counters nobody read.
- **The manual can be regenerated again.** Bumping the version broke the
  English manual, because translations were keyed by the Spanish text with the
  version inside it. Fixed, manual regenerated, and tied into the checks.
- **And a guard in the JTTY decoder**: an index that was one byte away from
  running off the row ring. It did not fail, but raising a single constant
  without looking was enough; the compiler now stops it before it reaches the
  radio.

---

## V4.40 · 2026-10-10

### Qué cambia

- **Un banco nuevo vigila la memoria del decodificador de HFDL.** Cuánto pide
  cada una de las ocho velocidades estaba escrito a mano en un comentario
  desde el 28/09, y un número a mano es un número que nadie recalcula. Ahora
  se mide llamando a la función de verdad del firmware, con la capacidad real,
  y comprueba además que la frontera muerde: un byte menos del que pide una
  velocidad y se tiene que negar. Si alguien ensancha la memoria, el banco lo
  refleja solo.
- **Y los bancos ya no pueden medir otra rama sin enterarse.** Al cambiar de
  firmware, los binarios del simulador se rehacen en vez de reusarse. Sin
  esto, un banco construido contra otra rama no fallaba: **pasaba**, midiendo
  código que no era el que creías. Es la misma avería del 25/09, y hoy volvió
  a morder mientras se probaba una mutación.

### What's new

- **A new bench now guards the HFDL decoder's memory.** How much each of the
  eight bit rates asks for had been written by hand in a comment since 28/09,
  and a hand-written number is one nobody recomputes. It is now measured by
  calling the firmware's real function with the real capacity, and it also
  checks that the boundary bites: one byte less than a rate asks for and it
  must refuse. If anyone widens the memory, the bench reflects it on its own.
- **And benches can no longer measure another branch unnoticed.** On switching
  firmware root, the simulator's binaries are rebuilt instead of reused.
  Without this, a bench built against another branch did not fail: it
  **passed**, measuring code that was not the one you thought. Same fault as
  25/09, and it bit again today while testing a mutation.

---

## V4.39 · 2026-10-10

### Qué cambia

- **La barra de HFDL se cortaba y no se leía el final.** El embudo llevaba
  cuatro números y el cuarto era exactamente el mismo total que ya enseña la
  chapa de al lado: `325>80>66>59` con la chapa diciendo `.../59`. Medido con
  la fuente de verdad, la línea entera pedía 177 px y la barra mide 170.
  Ahora el embudo son tres números -enganches, confirmados, sincronizados- y
  el aviso de memoria `X` se muda a la chapa, que es donde viven los números
  de rendimiento. Peor caso medido: 160 px de 170 en la barra y 138 de 140 en
  la chapa.
- **El aviso `X` ahora dice si se puede arreglar.** Son las ráfagas que no
  caben en la memoria del decodificador, y hay dos motivos detrás: el giro 6
  (1200 bps, ranura doble) cabría con 5.872 bytes más —y hay 19.412 libres—,
  y el giro 7 (1800 bps, ranura doble) pide 76 kB y no cabrá nunca. La letra
  los distingue: **`X`** se arregla ensanchando la memoria, **`Z`** no.

### What's new

- **The HFDL bar was being clipped and its tail could not be read.** The
  funnel had four numbers and the fourth was exactly the same total the chip
  beside it already shows: `325>80>66>59` with the chip reading `.../59`.
  Measured with the real font, the whole line asked for 177 px and the bar is
  170. The funnel is now three numbers -hits, confirmed, synchronised- and the
  `X` out-of-memory warning moves to the chip, where the performance numbers
  live. Worst case measured: 160 px of 170 in the bar and 138 of 140 in the
  chip.
- **The `X` warning now says whether it can be fixed.** Those are the bursts
  that do not fit in the decoder's memory, and there are two reasons behind
  it: rate 6 (1200 bps, double slot) would fit with 5,872 bytes more —and
  19,412 are free— while rate 7 (1800 bps, double slot) asks for 76 kB and
  never will. The letter tells them apart: **`X`** is fixable by widening the
  memory, **`Z`** is not.

---

## V4.38 · 2026-10-10

### Qué cambia

- **La posición de los aviones en HFDL no se leía nunca.** El decodificador
  exigía la marca del mensaje de posición en los dos primeros bytes, y la
  única trama real contra la que se verificó esto la tenía en los bytes
  10-11. Ahora se busca en los primeros 24, y el resultado tiene que
  sostenerse por cuatro sitios a la vez: que quepa el mensaje entero, la
  latitud en rango, que la hora sea una hora y que la estación de tierra
  exista.
- **Y de paso se inventaba posiciones.** Medido: la versión anterior daba por
  buena una posición en el 2,1 % de los datos de usuario que no la llevaban.
  Ahora, el 0,19 %. Un avión en un sitio inventado se cree, porque es un
  punto en un mapa.
- **Dos contadores nuevos en la barra de HFDL**, `U3/0`: cuántos mensajes de
  datos de usuario han entrado y cuántos han dado posición. Sirven para
  distinguir "no ha llegado ninguna" de "están llegando y no se leen".

### What's new

- **Aircraft position in HFDL was never being read.** The decoder demanded the
  position-message marker in the first two bytes, and the only real frame this
  was ever verified against had it at bytes 10-11. It is now searched for in
  the first 24, and the result must hold up on four counts at once: the whole
  message fits, latitude in range, the time is a real time of day, and the
  ground station exists.
- **And it was inventing positions.** Measured: the previous version accepted
  a position from 2.1 % of user data that carried none. Now 0.19 %. An
  aircraft at an invented place gets believed — it is a dot on a map.
- **Two new counters on the HFDL bar**, `U3/0`: how many user-data messages
  arrived and how many yielded a position. They tell "none arrived" apart from
  "they are arriving and not being read".

---

## V4.37 · 2026-10-10

### Qué cambia

- **Bahréin en las presintonías de HFDL.** Las ocho frecuencias de Al
  Muharraq (2986, 5544, 6646, 8885, 10030, 13312, 17967 y 21982) entran como
  una página propia, al lado de las de noche y las de día del Atlántico
  norte. Ya no hay que teclearlas a mano.
- No hacía falta para decodificar —el nombre de la estación viaja dentro de
  la trama, no sale de ninguna lista— pero escuchar las ocho en vez de una
  multiplica las posibilidades de cazar un parte de posición de un avión.
- Banco nuevo que vigila esa tabla: los grupos seguidos, las frecuencias
  subiendo, ninguna repetida y cada página con su rótulo. Una fila mal puesta
  no daba error, abría una página de una sola celda.

### What's new

- **Bahrain added to the HFDL presets.** Al Muharraq's eight frequencies
  (2986, 5544, 6646, 8885, 10030, 13312, 17967 and 21982) get a page of their
  own, alongside the North Atlantic night and day pages. No more typing them
  in by hand.
- It was never needed for decoding — the ground station name travels inside
  the frame, not from any list — but listening on eight frequencies instead
  of one multiplies the chances of catching an aircraft position report.
- New bench watching that table: groups contiguous, frequencies ascending,
  none repeated and every page labelled. A misplaced row used to open a
  one-cell page without anyone noticing.

---

## V4.36 · 2026-10-10

### Qué cambia

- **Menos trabajo dentro de la interrupción de audio, y suena exactamente
  igual.** Tres cosas que se calculaban dos veces: el notch automático sumaba
  la energía de su ventana de 48 tomas entera en cada muestra cuando sólo
  cambia un valor (un 32 % menos de cuentas por muestra); el blanqueador de
  ruido recorría el bloque cinco veces y ahora dos; y el osciloscopio de APRS
  hacía 32 divisiones por bloque para repetir la misma cuenta.
- Comprobado comparando la salida contra la versión de antes: el blanqueador
  da un resultado **idéntico bit a bit**, y el notch se separa 102 dB por
  debajo de la señal, o sea por debajo del último bit de un `float`.

### What's new

- **Less work inside the audio interrupt, and it sounds exactly the same.**
  Three things that were being computed twice: the automatic notch summed the
  energy of its whole 48-tap window on every sample when only one value
  changes (32 % fewer operations per sample); the noise blanker walked the
  block five times and now walks it twice; and the APRS tuning scope did 32
  divisions per block to repeat the same calculation.
- Verified by comparing the output against the previous version: the blanker
  is **bit-for-bit identical**, and the notch differs by 102 dB below the
  signal, which is below the last bit of a `float`.

---

## V4.35 · 2026-10-10

### Qué cambia

- **Se acabaron los bloques de audio tirados por nada en AM y SSB.** La
  interrupción descartaba un bloque de I/Q si veía saltos grandes entre
  muestras seguidas, y cada descarte repite 2,67 ms de audio ya sonado. En WFM
  ya se había apagado por lo mismo; ahora está medido que en AM/SSB pasa
  igual, porque el oscilador se aparca entre 6 y 40 kHz de la señal y a esa
  distancia una señal **perfecta** salta en cada muestra.
- Medido: un tono limpio al 49 % del fondo de escala, a 20 kHz del oscilador,
  se tiraba. Ya no.
- Banco nuevo `make corrupto`, y la regla sacada de la interrupción a
  `User/rx_corrupto.c` para poder medirla.

### What's new

- **No more audio blocks thrown away for nothing in AM and SSB.** The DMA
  interrupt used to discard an I/Q block when it saw large jumps between
  consecutive samples, and every discard repeats 2.67 ms of already-played
  audio. It had already been turned off for WFM for the same reason; it is now
  measured that AM/SSB has the same problem, because the local oscillator is
  parked 6 to 40 kHz away from the signal and at that offset a **perfect**
  signal jumps on every sample.
- Measured: a clean tone at 49 % of full scale, 20 kHz from the oscillator,
  was being thrown away. Not any more.
- New bench `make corrupto`, and the rule moved out of the interrupt into
  `User/rx_corrupto.c` so it can be measured at all.

---

## V4.34 · 2026-10-10

### Qué cambia

- **El Ident ya no enseña la última ventana, sino lo que pasa.** Publicaba la
  bandera de "hay dos tonos" y la velocidad tal y como salían del último
  medio segundo, así que una ventana mala con la señal desvanecida borraba un
  nombre que llevaba un minuto acertado. Ahora las dos van suavizadas, igual
  que ya iba la separación: la bandera por mayoría de las últimas dieciséis
  ventanas y la velocidad por mediana de las últimas ocho medidas buenas, con
  caducidad para no arrastrar un número de otra señal.
- **Lo que se nota:** la grabación de STANAG 4481 FSK en 5339 kHz pasa de
  acertar **1 ventana de 380 a 139**, y deja de decir "CW (Morse)" delante de
  una FSK. El RTTY comercial sube del 70 % al 82 % de aciertos. El CW de
  verdad no se mueve: sigue en el 72 %.
- Banco nuevo `make identgrab`: nueve grabaciones de verdad, contando en
  cuántas ventanas la radio acierta y no cómo acaba el fichero.

### What's new

- **The Ident no longer shows the last window, it shows what is happening.**
  The "two tones" flag and the baud rate were published exactly as the last
  half second left them, so one bad window with the signal fading wiped out a
  name that had been right for a minute. Both are now smoothed, as the tone
  separation already was: the flag by majority of the last sixteen windows,
  the baud by median of the last eight good measurements, with an expiry so a
  stale number from another signal is never carried over.
- **What you notice:** the STANAG 4481 FSK recording on 5339 kHz goes from
  getting **1 window right out of 380 to 139**, and stops saying "CW (Morse)"
  in front of an FSK signal. Commercial RTTY goes from 70 % to 82 %. Real CW
  does not move: still 72 %.
- New bench `make identgrab`: nine real recordings, scored on how many windows
  the radio gets right rather than on how the file ends.

---

## V4.33 · 2026-10-10

### Qué cambia

- **El filtro de fase ahora va centrado en la portadora.** La V4.32 lo estrenó
  a 25 Hz centrado en continua, y el banco sintético enseñó el fallo: con error
  de oscilador —que en onda larga puede ser de casi 200 Hz— el filtro atenúa la
  portadora y no el ruido, así que deja la señal peor que sin filtro. Ahora se
  quita el desvío antes de filtrar, usando el que el propio decodificador ya
  estima. Resultado: decodifica a cualquier desvío de 0 a 200 Hz, y el número
  de marca sigue sin desbocarse.
- **Y el banco sintético pasa por donde pasa la radio**, desde I y Q, no por la
  fase ya hecha. Hasta ahora medía media cadena.

### What's new

- **The phase filter is now centred on the carrier.** V4.32 introduced it at
  25 Hz centred on DC, and the synthetic bench showed the flaw: with oscillator
  error —which on longwave can be close to 200 Hz— the filter attenuates the
  carrier and not the noise, leaving the signal worse off than with no filter
  at all. The offset is now removed before filtering, using the one the decoder
  already estimates. Result: it decodes at any offset from 0 to 200 Hz, and the
  mark figure still does not run away.
- **And the synthetic bench now goes where the radio goes**, from I and Q
  rather than from ready-made phase. Until now it measured half the chain.

---

## V4.32 · 2026-10-10

### Qué cambia

- **ALS162 ya se puede saber si se está recibiendo, y decodifica con ruido.**
  A la medida de fase le faltaba un filtro: la radio promediaba I y Q y llamaba
  a `atan2` con unos 750 Hz de ruido entrando en un número que luego se
  **integra**, así que el resultado se paseaba hasta 30 radianes tanto si había
  emisora como si no. El guion de PC que preparó la grabación con la que el
  banco salía verde **sí** filtraba, a 40 Hz — o sea que al banco le daban el
  trabajo hecho. Ahora el filtro está en la radio, dos polos a 25 Hz, medidos
  contra la grabación con ruido inyectado. Con eso el número de marca separa
  los dos casos: **0,7 y pico con ALS162 delante, medio punto o menos con
  portadora y nada más.**
- **Y un banco que mide la cadena de verdad**, desde I y Q en vez de desde la
  fase ya limpia (`als_iq.c`, dentro de `make comprueba`).

### What's new

- **With ALS162 you can now tell whether you are receiving it, and it decodes
  through noise.** The phase measurement was missing a filter: the radio
  averaged I and Q and called `atan2` with some 750 Hz of noise going into a
  number that is then **integrated**, so the result wandered up to 30 radians
  whether or not a station was there. The PC script that prepared the recording
  the bench passes on **did** filter, at 40 Hz — the bench was being handed the
  work already done. The filter now lives in the radio, two poles at 25 Hz,
  measured against the recording with injected noise. With it the mark figure
  separates the two cases: **around 0.7 with ALS162 present, half a point or
  less with a bare carrier.**
- **And a bench that measures the real chain**, from I and Q rather than from
  already-cleaned phase (`als_iq.c`, inside `make comprueba`).

---

## V4.31 · 2026-10-10

### Qué cambia

- **La pantalla de Hora por radio enseña el número de la marca.** La barra se
  recorta al 100 %, así que un contraste de 1,0 y uno de 5,0 pintaban la misma
  barra llena: «no hay señal» y «hay cinco veces más señal de la que esto sabe
  manejar» se veían igual. Ahora, mientras busca, debajo pone el número crudo
  («1,34 de marca»). Es un instrumento, no un arreglo: con ALS162 hay un fallo
  abierto —a partir de 1,00 no se decodifica— y este número es lo que hace
  falta para arreglarlo midiendo en vez de adivinando.

### What's new

- **The Radio clock screen now shows the mark figure.** The bar clips at 100 %,
  so a contrast of 1.0 and one of 5.0 painted the same full bar: «no signal»
  and «five times more signal than this knows how to handle» looked identical.
  While it searches, the raw number now appears below («1.34 mark»). This is an
  instrument, not a fix: ALS162 has an open fault —above 1.00 nothing decodes—
  and this number is what is needed to fix it by measuring instead of guessing.

---

## V4.30 · 2026-10-10

### Qué cambia

- **Tu escala del espectro ya no te la puede borrar la autoescala.** Si pones
  el suelo de la escala por encima del ruido, el ruido desaparece de la
  cascada: por debajo de ese número se pinta el color del fondo. Es una forma
  cómoda de recortar ruido, y mucha gente la usa así. El problema era que ese
  número y el de trabajo de la autoescala **eran la misma variable**: en cuanto
  la autoescala se encendía, tu número desaparecía sin copia, apagarla no te
  devolvía nada, y al guardar ajustes se escribía en tu `CONFIG.CSV` la ventana
  que la autoescala tuviera en ese instante. Ahora son dos: la autoescala mueve
  lo que se pinta y **tus números se quedan quietos**. Apagarla te los devuelve.
  Girar el mando sí los cambia, que para eso es tuyo.
- **Y un banco que lo vigila.** Esto no se puede comprobar compilando —escribir
  en la variable equivocada compila igual de bien—, así que `escala_check.py`
  comprueba quién puede escribir dónde, y entra en `make comprueba`.

### What's new

- **The auto-scale can no longer wipe your spectrum scale.** Raising the scale
  floor above the noise makes the noise vanish from the waterfall: anything
  below that number is painted in the panel's own colour. It is a handy way to
  gate noise, and plenty of people use it that way. The problem was that your
  number and the auto-scale's working value **were the same variable**: the
  moment auto-scale came on, your number was gone with no copy kept, turning it
  off gave you nothing back, and saving settings wrote whatever window the
  auto-scale happened to hold into your `CONFIG.CSV`. They are now two: the
  auto-scale moves what gets drawn and **your numbers stay put**. Turning it off
  hands them back. Turning the knob does change them, which is the point.
- **And a bench that watches it.** None of this can be caught by compiling —
  writing to the wrong variable compiles perfectly — so `escala_check.py`
  checks who is allowed to write where, and runs inside `make comprueba`.

---

## V4.29 · 2026-10-10

### Qué cambia

- **La posición de los aviones en HFDL.** Nos dijeron que no se decodificaba, y
  resultó que `hfdl_hfnpdu.c` —el único sitio del firmware que convierte bytes
  del aire en una latitud y una longitud— **no lo compilaba ningún banco**: no
  se había probado nunca. Exigía un marco concreto (`0xFF 0xD1`) y ahora acepta
  también el que entrega dumphfdl sin sobre (`0xD1`), que se diferencia en un
  byte. Y lo que sale se comprueba: una latitud imposible se rechaza en vez de
  pintar un avión en mitad de ninguna parte.
- **HFDL y FT8 entran en el banco de basura**, que ya cubría 25 decodificadores
  y dejaba fuera justo los dos más grandes. 27 decodificadores, 13 millones de
  muestras de entrada hostil, ni una escritura fuera de sitio.
- **El ecualizador del 4415 no entra, y queda dicho por qué**: su criterio deja
  la salida girada y este modo no tiene lazo de portadora detrás, así que
  estropeaba los casos que hoy funcionan. El camino bueno —entrenar con el
  preámbulo, que trae 11.520 símbolos conocidos— queda escrito donde iba.
- **El ecualizador del 4285 tampoco hace falta**: medido con dos rayos iguales,
  el decodificador de hoy sigue sacando el texto. Ya está ecualizado por el
  método que la propia forma de onda tiene previsto.

### What's new

- **Aircraft position in HFDL mode.** We were told it wasn't decoding, and it
  turned out `hfdl_hfnpdu.c` — the only place in the firmware that turns
  over-the-air bytes into a latitude and longitude — **was not compiled by any
  bench**: it had never been tested. It required one specific framing
  (`0xFF 0xD1`) and now also accepts the un-enveloped one dumphfdl delivers
  (`0xD1`), which differs by a single byte. And the result is checked: an
  impossible latitude is rejected rather than painting an aircraft in the middle
  of nowhere.
- **HFDL and FT8 join the hostile-input bench**, which covered 25 decoders and
  left out precisely the two biggest ones. 27 decoders, 13 million samples of
  deliberately hostile input, not one out-of-bounds write.
- **The 4415 equaliser does not go in, and why is written down**: its criterion
  leaves the output rotated and this mode has no carrier loop behind it, so it
  broke the cases that work today. The right path — training on the preamble,
  which carries 11,520 known symbols — is left where the code would have gone.
- **The 4285 equaliser isn't needed either**: measured with two equal rays, the
  current decoder still produces text. It is already equalised, by the method
  the waveform itself provides for.

---

## V4.28 · 2026-10-10

### Qué cambia

- **El Ident ya no dice APRS sobre tres teletipos.** Con varias señales en el
  paso de banda, el detector juntaba el tono de arriba de una con el de abajo de
  otra y se inventaba un salto de 1.019 Hz, que cae cerca del 1200/2200 del APRS.
  Ahora agrupa los tonos por cercanía, descarta los saltos que ninguna firma
  puede usar, y **dice cuántas señales hay**: *«3 señales a la vez: estrecha el
  filtro»*. Con tres encima no da el nombre — dice que hay tres, que es lo único
  honrado.
- **La RAM pasa del 97,1 % al 89,6 %**: 14.844 bytes recuperados sin perder
  ninguna función. Las cuatro tablas de la FFT de FT8 se reconstruían en RAM en
  cada entrada al modo y ahora viven en flash (mismos valores, bit a bit), con
  lo que seis buffers que estaban fuera de la memoria compartida han entrado
  dentro. Y los dos caminos de audio —WFM y el estrecho— comparten buffers,
  porque nunca están vivos a la vez.
- **Se fue el cuelgue de FT8**, no tapado sino desaparecido: existía un «sello»
  para detectar que las tablas estaban sin construir, porque la cascada las
  pisaba. En flash nadie puede borrarlas.
- **Menos CPU en el camino de audio.** La comprobación de recorte corría fuera
  de la ventana que mide ciclos —invisible para la propia instrumentación— y
  ahora va dentro del bucle que ya lee esas muestras; `I²+Q²` se calculaba tres
  veces por bloque y ahora una; y el bucle de fase del WEFAX pagaba hasta 756
  senos y cosenos **dentro de la interrupción** para tirar el resultado después.
- **CW: con el autoajuste apagado ya no calcula doce filtros que nadie lee.**

### What's new

- **Ident no longer says APRS over three teleprinters.** With several signals in
  the passband, the detector was pairing the upper tone of one with the lower
  tone of another and inventing a 1,019 Hz shift, which lands near APRS's
  1200/2200. It now groups tones by proximity, rejects shifts no signature can
  use, and **says how many signals are there**: *"3 signals at once: narrow the
  filter"*. With three on top of each other it doesn't give a name — it says
  there are three, which is the only honest answer.
- **RAM goes from 97.1% to 89.6%**: 14,844 bytes recovered with no loss of
  function. FT8's four FFT tables were rebuilt in RAM on every entry to the mode
  and now live in flash (same values, bit for bit), which let six buffers that
  sat outside the shared memory move inside it. And the two audio paths — WFM
  and the narrow one — now share buffers, because they are never live together.
- **An FT8 hang is gone**, not papered over but removed: there was a "seal" to
  detect that the tables were unbuilt, because the waterfall overwrote them. In
  flash nobody can erase them.
- **Less CPU on the audio path.** The clipping check ran outside the window that
  measures cycles — invisible to the file's own instrumentation — and now runs
  inside the loop that already reads those samples; `I²+Q²` was computed three
  times per block and now once; and WEFAX's phase loop was paying up to 756 sines
  and cosines **inside the interrupt** only to throw the result away afterwards.
- **CW no longer computes twelve filters nobody reads** when auto-tune is off.

---

## V4.27 · 2026-10-10

### Qué cambia

- **La radio ya no da la hora de Madrid en Australia.** España no era un ajuste mal
  puesto: estaba **escrita dentro del programa**, en cuatro sitios —`hora_wwv.c`,
  `hora_rbu.c`, el byte 40 del `EMISORAS.BIN` y, el que apareció el último, la
  conversión de MSF en `dcf77.c`—. Las emisoras que emiten UTC daban hora de pared
  española con la regla europea del horario de verano, estuvieras donde estuvieras.
- **El huso es ahora un número en minutos**, no en horas: la India está en +5:30,
  Nepal en +5:45 y parte de Australia en +9:30. Se guarda en `CONFIG.CSV`.
- **Botón nuevo en Hora por radio**, con el valor escrito dentro («Huso +2:00»).
  Abre una lista con **Medir** arriba y **cincuenta ciudades** debajo, ordenadas por
  cercanía a tu localizador. Cada ciudad enseña su huso de invierno **y el de hoy**,
  para que una regla caducada se note.
- **Medir** coge el UTC que acaba de dar la emisora, le resta tu reloj y eso es tu
  huso, redondeado al cuarto de hora. Funciona con MSF, WWV, BPM y RBU; no con DCF77
  ni ALS162, que emiten hora local alemana y francesa.
- **El RDS ya pone el huso**, no sólo la hora: su grupo 4A trae el desplazamiento
  local de donde estés, con el verano ya dentro. Llevaba desde septiembre usándose
  para sumar y tirándose. Lo que diga una emisora manda sobre cualquier tabla.
- **La fecha se aplica además de la hora, en las seis fuentes.** Las seis la traían
  y se estaba tirando; el reloj se quedaba con la fecha de fábrica del chip, y por
  eso ninguna regla de horario de verano podía funcionar.
- **Cinco reglas de horario de verano** (Europa, EEUU, Australia, Nueva Zelanda y
  Chile) comprobadas contra **24.804 instantes** sacados de la base de husos del
  sistema. El banco encontró dos errores míos antes de ponerse en verde.

### What's new

- **The radio no longer gives Madrid time in Australia.** Spain was not a badly-set
  option: it was **written into the program**, in four places — `hora_wwv.c`,
  `hora_rbu.c`, byte 40 of `EMISORAS.BIN`, and the one found last, the MSF
  conversion in `dcf77.c`. The stations that broadcast UTC produced Spanish wall
  time, with the European summer-time rule, wherever you were.
- **The time zone is now a number in minutes**, not hours: India is at +5:30, Nepal
  at +5:45 and part of Australia at +9:30. It is saved in `CONFIG.CSV`.
- **A new button on the Radio clock screen**, with the value written on it
  ("Zone +2:00"). It opens a list with **Measure** at the top and **fifty cities**
  below, sorted by distance from your locator. Each city shows its winter offset
  **and today's**, so a stale rule is visible.
- **Measure** takes the UTC the station just gave, subtracts your clock, and that is
  your offset, rounded to the quarter hour. It works with MSF, WWV, BPM and RBU; not
  with DCF77 or ALS162, which broadcast German and French local time.
- **RDS now sets the offset**, not just the time: its group 4A carries the local
  offset where you are, with summer time already in it. Since September it was being
  used as an addend and thrown away. What a station says overrides any table.
- **The date is applied as well as the time, from all six sources.** All six carried
  it and it was being discarded; the clock kept the chip's factory date, which is why
  no summer-time rule could work.
- **Five summer-time rules** (Europe, USA, Australia, New Zealand and Chile) checked
  against **24,804 instants** taken from the system time-zone database. The bench
  found two of my own errors before it went green.

---

## V4.26 · 2026-10-10

### Qué cambia

- **ALS162 decodifica.** Llevaba desde el 28/09 sin sacar una sola trama, y la causa
  estaba en una frase de la norma de Allouis que no estaba en el código: *«Additional
  non-timing data is sent by phase modulation during the rest of each second»*. Medido
  sobre una grabación real: **6,24 elementos por segundo**, de los cuales sólo 1,05 son
  horarios. El contador los sumaba todos y, con más de dos, declaraba ruido.
- **El detector del segundo se engancha ahora a la rejilla de 1 s** en vez de contar
  todo lo que suba. Lo que separa el elemento horario del canal de datos no es la forma
  (idéntica) ni la amplitud (1,809 rad contra 1,904): es que cae cada 1,000 s.
- **Comprobado contra un reloj que no es el nuestro**: una grabación de WebSDR que trae
  su hora UTC en el nombre del fichero sale como las **04:21 del sábado 10/10/26**, en
  horario de verano — que es exactamente lo que debía salir.

### What's new

- **ALS162 decodes.** It had not produced a single frame since 28/09, and the cause was
  one sentence of the Allouis spec that was missing from the code: *"Additional
  non-timing data is sent by phase modulation during the rest of each second."* Measured
  on a real recording: **6.24 elements per second**, of which only 1.05 are timing ones.
  The counter added them all up and, past two, called it noise.
- **The second detector now locks to the 1 s grid** instead of counting every rise. What
  separates the timing element from the data channel is not its shape (identical) nor its
  amplitude (1.809 rad against 1.904): it is that it lands every 1.000 s.
- **Checked against a clock that is not ours**: a WebSDR recording carrying its UTC time
  in the filename comes out as **04:21 on Saturday 10/10/26**, in summer time — which is
  exactly what it had to be.

---

## V4.25 · 2026-10-09

### Qué cambia

- **La pantalla de hora dice ahora POR QUÉ se tira cada trama.** Mientras no haya
  ninguna mala sigue enseñando «raras»; en cuanto hay una, ese hueco pasa a decir
  «mal N» y el motivo: `corta`, `b0`, `b20`, `z1z2`, `par-min`, `par-hor`, `par-fec`
  o `rango`. Eso separa «las tramas no se completan» —que es sincronismo— de «se
  completan y se rechazan» —que es formato o ruido—, y dentro del segundo dice por
  cuál de las ocho puertas. Antes las dos cosas se veían igual: el contador dando
  vueltas de 0 a 59 y «Aplicar» apagado.
- **Y en el barrido, un asterisco en las emisoras de fase.** ALS162 y RBU no miden
  su porcentaje con la misma vara que DCF77, MSF, WWV y BPM: unas dan una razón
  entre dos niveles y las otras un pico a pico en radianes escalado a ojo. Estaban
  en la misma columna, con el mismo `%`, ordenadas entre sí. Ahora llevan `*` y un
  renglón que lo dice. Calibrarlas de verdad es trabajo pendiente y pide una medida
  de antena, no una cuenta.

### What's new

- **The radio-clock screen now says WHY each frame is thrown away.** While no frame
  has failed it still shows "raras"; as soon as one does, that slot becomes "mal N"
  plus the reason: `corta`, `b0`, `b20`, `z1z2`, `par-min`, `par-hor`, `par-fec` or
  `rango`. That separates "frames never complete" — a sync problem — from "frames
  complete and get rejected" — format or noise — and within the second it says which
  of the eight gates did it. Before, both looked identical: the counter cycling 0 to
  59 and Apply greyed out.
- **And an asterisk on the phase stations in the scan.** ALS162 and RBU do not
  measure their percentage with the same yardstick as DCF77, MSF, WWV and BPM: one
  is a ratio between two levels, the other a peak-to-peak in radians scaled by eye.
  They sat in the same column, with the same `%`, ranked against each other. They
  now carry `*` and a line that says so. Calibrating them properly is pending work
  and needs an antenna measurement, not arithmetic.

---

## V4.24 · 2026-10-09

### Qué cambia

- **La fila «Mult. zona 7» dice también el margen del VCO** (issue #35, segunda
  vuelta). Enseñaba el multiplicador y el fVCO del dial de ese momento, que sirve para
  barrer pero no para **elegir**: lo que decide si un valor se puede dejar puesto de
  fábrica es lo cerca que pasa del borde en los **dos extremos de la zona entera**. Y ahí
  los dos valores que discute el issue están los dos pegados al borde, cada uno al suyo:
  168 pasa a 2 MHz del suelo y 189 a **0,8 MHz** del techo. Ahora se ve.

### What's new

- **The "Zone-7 mult." row now also shows the VCO margin** (issue #35, second round). It
  showed the multiplier and the fVCO for wherever the dial happened to be, which is fine
  for sweeping but not for **choosing**: what decides whether a value can ship is how
  close it passes to the edge at **both ends of the whole zone**. And there both values
  under discussion sit against an edge, each against its own: 168 passes 2 MHz from the
  floor and 189 **0.8 MHz** from the ceiling. Now you can see it.

---

## V4.23 · 2026-10-09

### Qué cambia

- **La pantalla del reloj ya no se queda los toques cuando se abre otro menú encima.** El
  menú de arriba se pintaba, pero los dedos seguían yendo al reloj. La causa no estaba en el
  reloj: cada pantalla apagaba a mano las demás y ninguna las apagaba todas. Ahora eso lo
  hace una sola función, que además suelta lo que la pantalla del reloj había encendido —el
  decodificador horario, el RDS y tu frecuencia—, cosa que por el camino del QTH no se hacía.
- **En WSPR el botón de banda ya no tira el minuto que llevas.** Con una captura abierta el
  cambio se apunta y se hace al cerrarse la ventana. El botón sigue girando por la lista en
  el acto y enseña a dónde vas; la chapa dice «luego 160 m» mientras espera.
- **JTTY entra en el Ident** con su fila: 4-FSK de 31,25 Hz a 31,25 baudios. **AIS no**, y a
  propósito: son 14 kHz de GMSK y el Ident mide en una ventana de 3 kHz, así que en vez de
  adivinar contesta por la frecuencia —en 161,975 y 162,025 MHz no vive otra cosa—.
- **El banco de basura por la puerta del aire pasa de 15 decodificadores a 25, y además
  llama a la mitad cara de cada uno.** Esto segundo era un agujero: el Viterbi, el Fano, las
  FFT y los ecualizadores viven en el bucle principal y el banco sólo llamaba a la mitad de
  la interrupción, así que el código con más índices de la radio no había visto nunca una
  entrada hostil mientras el banco decía «todo correcto».

### What's new

- **The clock screen no longer swallows touches when another menu opens on top of it.** The
  menu above was painted, but the taps still went to the clock. The cause was not in the
  clock: every screen cleared the others by hand and none of them cleared all. One function
  does it now, and it also releases what the clock screen had switched on —the time decoder,
  RDS and your frequency— which the QTH path was not doing.
- **In WSPR the band button no longer throws away the minute in progress.** With a capture
  open the change is noted and applied when the window closes. The button still steps
  through the list at once and shows where you are heading; the badge reads "then 160 m"
  while it waits.
- **JTTY gets a row in Ident**: 4-FSK of 31.25 Hz at 31.25 baud. **AIS does not**, on
  purpose: it is 14 kHz of GMSK and Ident measures inside a 3 kHz window, so instead of
  guessing it answers from the dial — nothing else lives on 161.975 and 162.025 MHz.
- **The hostile-input bench goes from 15 decoders to 25, and now calls each one's expensive
  half.** That second part was a hole: the Viterbi, the Fano, the FFTs and the equalisers
  live in the main loop and the bench only called the interrupt half, so the most
  index-heavy code in the radio had never seen hostile input while the bench printed "all
  correct".

---

## V4.22 · 2026-10-09

### Qué cambia

- **«Puerto serie USB» sube a la primera página de Información y «Pila» baja a las
  escondidas.** Es el criterio que ya rige esa pantalla: delante lo que se busca cuando algo
  no funciona, detrás lo que se mira por curiosidad. El puerto serie es lo que hace falta
  para usar la radio con WSJT-X y estaba escondido detrás de cinco toques — o sea escondido
  justo de quien lo necesita, el mismo error que ya se corrigió con «Formatear disco». La
  pila es de destripar: quien la mira es quien está añadiendo código.
- Intercambio exacto: la página 1 tenía doce celdas y sigue teniendo doce.

### What's new

- **"USB serial port" moves up to Information's first page and "Stack" moves down to the
  hidden ones.** That is the rule that already governs that screen: what you look for when
  something is broken goes in front, what you look at out of curiosity goes behind. The
  serial port is what you need to drive the radio from WSJT-X and it was hidden behind five
  taps — hidden from exactly the person who needs it, the same mistake already corrected for
  "Format disk". The stack is for digging: whoever reads it is adding code.
- An exact swap: page 1 had twelve cells and still has twelve.

---

## V4.21 · 2026-10-09

### Qué cambia

- **El contador de huecos de audio contaba uno que no lo era.** Al abrir la grabación se
  vacía el anillo y se manda la primera trama en el mismo instante, así que sale vacía *por
  construcción*. Hay que mandarla —en isócrono el flujo tiene que arrancar— pero contarla
  como corte es mentir, y la mentira importaba: con ese 1 ahí, el colchón parecía justo
  cuando en toda una sesión de WSJT-X decodificando no había habido **ni un corte**.
- Ahora sólo se cuentan los huecos después de que haya salido audio de verdad.
- **El dispositivo se llama «DeepSDR Audio»** en vez de «CAT de 101», a petición del usuario.

### What's new

- **The audio-gap counter counted one that was not a gap.** Opening the capture clears the
  ring and sends the first frame in the same breath, so it goes out empty *by construction*.
  It has to be sent — an isochronous stream has to start — but counting it as a dropout is a
  lie, and the lie mattered: with that 1 sitting there the cushion looked tight when a full
  WSJT-X decoding session had produced **no dropouts at all**.
- Gaps are now counted only once real audio has gone out.
- **The device is now called "DeepSDR Audio"** instead of "CAT de 101".

---

## V4.20 · 2026-10-09

### Qué cambia

- **El audio llegaba destrozado, y era medio sample.** `anillo_mete()` mete *lo que quepa*,
  que para el puerto serie es lo correcto pero aquí es veneno: si caben 100 bytes de 128,
  mete 100 — y 100 no es múltiplo de cuatro. A partir de ahí el canal izquierdo son los
  bytes altos de una muestra y los bajos de la siguiente, para siempre. Medido en una
  grabación de referencia: **los canales diferían hasta 33.226** cuando la radio manda el mismo
  valor a los dos, y **el 56,7% de la energía estaba por encima de 8 kHz** cuando este audio
  no pasa de 4. Ahora es todo o nada.
- **Y se manda lo que haya, no 192 bytes rellenos de silencio.** El endpoint está declarado
  asíncrono, que significa exactamente «el reloj lo pongo yo y mando lo que tenga». Rellenar
  era mentirle al ordenador y meterle silencio: en esa misma grabación salieron 20 rachas de
  ceros en nueve segundos, la mayor de 9,33 ms.
- Tres asertos nuevos exigen al compilar que todo lo que pasa por ese anillo sea múltiplo de
  una trama estéreo.

### What's new

- **The audio arrived destroyed, and it was half a sample.** `anillo_mete()` writes *what
  fits*, which is right for the serial port but poison here: if 100 bytes of 128 fit, it
  writes 100 — and 100 is not a multiple of four. From then on the left channel is the high
  byte of one sample and the low byte of the next, forever. Measured from the owner's
  recording: **the channels differed by up to 33,226** where the radio sends the same value
  to both, and **56.7% of the energy sat above 8 kHz** where this audio stops at 4. It is now
  all or nothing.
- **And it sends what it has, not 192 bytes padded with silence.** The endpoint is declared
  asynchronous, which means precisely "I keep my own clock and send what I have". Padding was
  lying to the host and injecting real silence: that same recording had 20 runs of zeros in
  nine seconds, the longest 9.33 ms.
- Three new static assertions demand at compile time that everything through that ring is a
  whole stereo frame.

---

## V4.19 · 2026-10-09

### Qué cambia

- **La radio contesta a lo que Windows le pregunta del audio.** El anfitrión pide la
  frecuencia de muestreo al endpoint isócrono aunque el descriptor diga que no se puede
  cambiar. Contestando STALL —que es lo que hacía— el driver de audio da el dispositivo por
  roto y **no sale como entrada de grabación**, mientras el COM sigue yendo: el síntoma
  habría sido «el puerto va y el micrófono no».
- Se contesta la que dice el descriptor, y cualquier intento de cambiarla se acepta y se
  tira: sólo hay una, y el reloj de aquí no se mueve.

### What's new

- **The radio now answers what Windows asks about the audio.** The host asks the isochronous
  endpoint for its sampling frequency even though the descriptor says it is not adjustable.
  STALLing it — which is what the previous version did — makes the audio driver give up and
  **the device never appears as a recording input**, while the COM port keeps working: the
  symptom would have been "the port works and the microphone does not".
- It answers with the rate the descriptor declares, and accepts and discards any attempt to
  change it: there is only one, and the clock here does not move.

---

## V4.18 · 2026-10-09

### Qué cambia

- **El COM vuelve: `USBD_ITF_MAX_NUM` valía 1 y hacen falta 4 interfaces.** El núcleo USB
  rechaza con STALL todo lo que vaya dirigido a una interfaz por encima de ese número.
  Con el puerto serie solo (interfaces 0 y 1) colaba; con el audio (2 y 3) Windows pedía la
  3, se llevaba un STALL, reiniciaba el equipo y volvía a empezar en bucle — que desde la
  radio se ve como «esperando» para siempre y desde el PC como ningún COM y ningún error.
- Va bajo `#ifndef` y lo sube sólo la aplicación desde su Makefile: **el cargador sigue
  compilando byte a byte idéntico**.

### What's new

- **The COM port is back: `USBD_ITF_MAX_NUM` was 1 and four interfaces are needed.** The USB
  core STALLs anything addressed to an interface above that number. With the serial port
  alone (interfaces 0 and 1) it passed; with audio (2 and 3) Windows asked for interface 3,
  got a STALL, reset the device and started over, in a loop — which looks like a permanent
  "waiting" on the radio and no COM port and no error at all on the PC.
- It sits behind `#ifndef` and only the application raises it from its Makefile: **the
  bootloader still builds byte for byte identical**.

---

## V4.17 · 2026-10-09

### Qué cambia

- **La V4.16 no montaba el COM, y era un byte.** El descriptor general de flujo de audio
  llevaba tipo `0x25` (clase de endpoint) donde tiene que llevar `0x24` (clase de interfaz).
  El anfitrión veía un descriptor de endpoint sin endpoint al que pegarse y abandonaba la
  configuración **sin dar ningún error**. El banco no lo cazaba; ahora lleva la regla
  general y se le ha visto fallar.
- **Y repasando lo nuevo salieron cinco más**: el cambio de modo por CAT entraba por
  `demod_am_set_mode()` en vez de `apply_demod_mode()`, saltándose la ganancia del PGA y el
  cambio de frecuencia de muestreo; `usb_cdc_para()` vaciaba los anillos desde el bucle
  mientras dos interrupciones los tocan; encendido sin cable, la fila decía «apagado» y el
  toque siguiente no lo paraba; una ráfaga de órdenes repintaba la cabecera una vez por
  orden; y el volumen cambiado por CAT no se repintaba.

### What's new

- **V4.16 did not create the COM port, and it was one byte.** The audio AS-general descriptor
  carried type `0x25` (class-specific endpoint) where it must carry `0x24` (class-specific
  interface). The host saw an endpoint descriptor with no endpoint to attach to and gave up
  on configuration **with no error at all**. The bench missed it; it now carries the general
  rule, and has been seen to fail on it.
- **Reviewing the new code turned up five more**: CAT mode changes went through
  `demod_am_set_mode()` instead of `apply_demod_mode()`, skipping the PGA bookkeeping and the
  sample-rate switch; `usb_cdc_para()` cleared the rings from the main loop while two
  interrupts use them; turned on with no cable the row said "off" and the next tap would not
  stop it; a burst of commands repainted the header once per command; and a CAT volume change
  did not repaint.

---

## V4.16 · 2026-10-09

### Qué cambia

- **La radio es ahora, por el mismo cable, un puerto serie Y una tarjeta de sonido.** En
  WSJT-X o fldigi se elige como entrada de audio y **no hace falta tarjeta de sonido, ni
  cable de audio, ni ajustar niveles**: lo que llega al PC son exactamente las mismas
  muestras que van al altavoz, ya con su AGC y su filtro.
- **Dos descriptores de asociación, y la clase del dispositivo pasa a 0xEF/0x02/0x01.** Con
  más de una función eso es obligatorio: sin ello Windows ve cuatro interfaces sueltas y
  **no monta el COM**, sin dar ningún error.
- **El audio sólo se activa cuando un programa abre la grabación.** La interfaz lleva un
  ajuste alternativo 0 sin endpoints, que es el que el anfitrión deja puesto mientras nadie
  grabe: así el ancho de banda isócrono no se reserva desde que se enchufa el cable.
- **El anillo pasó a llevar su sitio fuera**, porque el del CAT mueve órdenes de una docena
  de bytes y el del audio 192 **cada milisegundo**. Una sola implementación para los dos, y
  el banco prueba los dos tamaños.
- **El banco de descriptores se reescribió entero** para lo compuesto, y con dos cuentas
  cruzadas nuevas: que el total de la parte de control de audio cuadre, y que **el endpoint
  isócrono quepa el formato que se declara** tres descriptores más arriba.

### What's new

- **Over the same cable, the radio is now both a serial port and a sound card.** In WSJT-X
  or fldigi you pick it as the audio input and **no sound card, audio cable or level
  tweaking is needed**: what reaches the PC is exactly the samples going to the speaker.
- **Two interface association descriptors, and the device class becomes 0xEF/0x02/0x01.**
  With more than one function that is mandatory: without it Windows sees four loose
  interfaces and **does not create the COM port**, with no error of any kind.
- **Audio only starts when a program opens recording.** Alternate setting 0 carries no
  endpoints, so isochronous bandwidth is not reserved just by plugging the cable in.
- **The ring now takes its storage from outside**, because the CAT one carries a dozen bytes
  and the audio one 192 **every millisecond**. One implementation, and the bench covers both
  sizes.
- **The descriptor bench was rewritten** for the composite device, with two new cross-checks:
  that the audio-control total adds up, and that **the isochronous endpoint fits the format
  declared** three descriptors above it.

---

## V4.15 · 2026-10-09

### Qué cambia

- **La radio se ve como un Kenwood TS-2000 desde el ordenador.** Información → página 4 →
  **«Puerto serie USB»**, un toque, y al enchufar el cable sale un puerto serie (COMx /
  ttyACM0) por el que hablan WSJT-X, JTDX, fldigi y cualquier cosa que use hamlib. Sin
  drivers y sin tocar la placa. Frecuencia y modo de ida y vuelta; S, volumen y ancho de
  lectura. No transmite, y lo dice como lo diría un Kenwood.
- **Los 48 MHz del USB salen del PLLSAI, no del PLL principal.** De este PLL salen 49,92
  MHz, un **+4,0%** sobre una tolerancia del ±0,25%: el ordenador ve que se engancha algo y
  la enumeración no termina nunca. El cargador lo resolvió bajando la CPU a 192 MHz; la
  aplicación no puede, porque eso arrastra el oscilador de banda baja, el I2S, el reloj del
  códec y los tiempos del bus de pantalla. **El PLLSAI estaba libre** y da 48,000 MHz
  exactos con N=125 y P=4, con la CPU quieta en 199,68.
- **El pegamento de hardware está escrito aquí y no reutilizado**, porque el del fabricante
  se llevaría por delante TIMER2 —que es el reloj maestro del códec y el oscilador de banda
  baja— y cambiaría la agrupación de prioridades de toda la radio.
- **Se enciende a mano, a propósito.** El núcleo USB y el hardware son lo único de todo
  esto que no se ha podido medir en el ordenador. Detrás de un toque, si algo va mal la
  radio arranca normal y basta con no tocar la fila.
- **Dos bancos nuevos**: los descriptores recorridos como los recorre un anfitrión, y el
  anillo entre la interrupción y el bucle con dos hilos pasándose dos millones de bytes.

### What's new

- **The radio appears as a Kenwood TS-2000 to the computer.** Information → page 4 → **"USB
  serial port"**, one tap, and plugging the cable in gives a serial port (COMx / ttyACM0)
  that WSJT-X, JTDX, fldigi and anything using hamlib can drive. No drivers, no board
  changes. Frequency and mode both ways; S-meter, volume and filter width read-only. It
  does not transmit, and says so the way a Kenwood would.
- **The USB's 48 MHz comes from PLLSAI, not the main PLL.** The main PLL yields 49.92 MHz,
  **+4.0%** against a ±0.25% tolerance: the host sees something attach and enumeration never
  completes. The bootloader solved this by dropping to 192 MHz; the application cannot,
  because that drags the low-band oscillator, the I2S, the codec clock and the display bus
  timings with it. **PLLSAI was unused** and gives exactly 48.000 MHz with N=125, P=4, with
  the CPU untouched at 199.68.
- **The hardware glue is written here rather than reused**, because the vendor's would take
  out TIMER2 — the codec's master clock and the low-band oscillator — and change the
  interrupt priority grouping of the whole radio.
- **It is turned on by hand, deliberately.** The USB core and the hardware are the only part
  of this that could not be measured on the host.
- **Two new benches**: the descriptors walked the way a host walks them, and the ring
  between the interrupt and the main loop with two threads passing two million bytes.

---

## V4.14 · 2026-10-09

### Qué cambia

- **La fila «USB del pincho» ahora barre patillas, y la del conector se nombra sola.** La
  V4.13 sólo sabía decir sí o no, y dijo **no**: el conector grande no llega a PA11/PA12.
  Un toque pone como entrada con bajada las **26 patillas que el firmware no configura
  nunca** —sacadas del propio código, no de una foto—, las lee con el pincho dentro y
  escribe el nombre de la que se mueva. Cada una se deja exactamente como estaba: se
  guardan los registros `CTL` y `PUD` enteros de los cuatro puertos antes de tocar nada.
- **PA9 entra en el barrido aunque la use la consola serie.** Contando las pastillas de ese
  lado del chip —25 exactas, un lado entero de LQFP100— las dos que el usuario marcó en la
  foto son la 7ª y la 8ª; si las dos anteriores son las del conector de grabar (PA12 y
  PA11), las marcadas son **PA10 y PA9**. Barrer sólo PA10 dejaría la otra sin confirmar.
- **Y una corrección a lo que dije en la V4.13:** los dos cortes del mod **no dejan sin
  datos al conector de grabar**. Van del lado del conector grande, a soltar sus líneas de
  donde están hoy. **La grabación arrastrando el fichero sobrevive al mod.**

### What's new

- **The "Stick USB" row now sweeps pins, and the connector's pin names itself.** V4.13 could
  only say yes or no, and it said **no**: the big connector does not reach PA11/PA12. One
  tap sets the **26 pins the firmware never configures** —derived from the code itself, not
  from a photo— to input with pulldown, reads them with the stick in, and writes the name of
  whichever moves. Every pin is left exactly as it was: the whole `CTL` and `PUD` registers
  of all four ports are saved before anything is touched.
- **PA9 is in the sweep even though the serial console uses it.** Counting the pads on that
  side of the chip —25 exactly, a full LQFP100 side— the two the owner marked are the 7th
  and 8th; if the two before them are the flashing connector's (PA12 and PA11), the marked
  ones are **PA10 and PA9**. Sweeping only PA10 would leave the other unconfirmed.
- **And a correction to what V4.13 said:** the mod's two cuts **do not strip the flashing
  connector of its data lines**. They are on the big connector's side, freeing its lines
  from where they sit today. **Drag-and-drop flashing survives the mod.**

---

## V4.13 · 2026-10-09

### Qué cambia

- **«USB del pincho», una fila nueva en Información que contesta sola dónde está el
  conector grande.** Para grabar en un pincho el micro tiene que hacer de anfitrión, y en
  este encapsulado eso sólo cabe en **PA11/PA12 (USBFS)**: el otro par posible —PB14/PB15,
  USBHS— **son el bus I2S del códec**, con las muestras I/Q entrando por PB15 y el audio
  saliendo por PB14 (ver `gd32_i2s.c`). La fila se lee con un pincho enchufado y detecta la
  resistencia de 1,5 kΩ que todo dispositivo USB lleva dentro: si pone «PA12 · D+», el
  conector grande ya llega y **el anfitrión es sólo firmware**.
- **La página 4 de Información no estaba en el manual.** Ocho filas sin documentar, y el
  manual decía «veintinueve filas en tres páginas» cuando son treinta y cinco en cuatro. El
  suelo de `tools/manual_check.py` baja de 171 a 164.
- **El pie de la fila nueva no cabía:** 246 px de 192. Lo pilló `sim/pie_ancho.c`, al que se
  le han añadido los cuatro pies de esta fila para que no se vuelva a colar.

### What's new

- **"Stick USB", a new Information row that answers by itself where the big connector
  goes.** To write to a stick the MCU must act as host, and on this package that only fits
  on **PA11/PA12 (USBFS)**: the other possible pair —PB14/PB15, USBHS— **is the codec's I2S
  bus**, with I/Q samples coming in on PB15 and audio going out on PB14 (see
  `gd32_i2s.c`). The row is read with a stick plugged in and detects the 1.5 kΩ resistor
  every USB device carries: if it reads "PA12 · D+", the big connector already reaches it
  and **host mode is firmware only**.
- **Information's page 4 was undocumented.** Eight rows missing, and the manual said
  "twenty-nine rows in three pages" when there are thirty-five in four. The floor in
  `tools/manual_check.py` drops from 171 to 164.
- **The new row's footer did not fit:** 246 px of 192. Caught by `sim/pie_ancho.c`, which
  now carries all four of this row's footers so it cannot slip through again.

---

## V4.12 · 2026-10-09

### Qué cambia

- **Incidencia 12, los espurios por debajo de 300 kHz: era el filtro de entrada.**
  `rf_lpf_select()` sólo se llamaba desde dentro del driver del MS5351, y por debajo de
  300 kHz el oscilador no lo hace el MS5351. Así que el filtro se quedaba donde lo dejó la
  última sintonía de arriba: bajando desde FM se quedaba en el rango de 61–120 MHz, que
  **corta todo lo de abajo**, y lo que entraba era lo que se colaba por los lados. Ahora el
  camino del GD32 también lo selecciona.
- **Y la cuadratura de ese oscilador, medida sin osciloscopio.** La aritmética del
  temporizador es entera: con el período impar el desfase se quedaba en 90 − 90/cuentas
  grados — **46,6 dB de rechazo de imagen en el peor caso**, la mitad de las frecuencias
  afectadas. No era la causa de los espurios, pero **ahora el período se fuerza a par y la
  cuadratura es exacta por construcción**. Sale gratis: el escalón de frecuencia que eso
  dobla ya se lo comía el NCO desde V4.10.
- **Banco nuevo `make logen`**, que barre los 294.001 hercios de 6 a 300 kHz. Visto fallar:
  147.338 casos en rojo sin el ajuste a par.

### What's new

- **Issue 12, spurious signals below 300 kHz: it was the input filter.** `rf_lpf_select()`
  was only ever called from inside the MS5351 driver, and below 300 kHz the oscillator is
  not the MS5351. So the filter stayed wherever the last tune above had left it: coming
  down from FM it stayed on the 61–120 MHz range, which **cuts everything below**, and what
  reached the input was whatever leaked past the wrong filter. The GD32 path now selects it
  too.
- **And that oscillator's quadrature, measured without an oscilloscope.** The timer
  arithmetic is integer: with an odd period the phase landed at 90 − 90/counts degrees —
  **46.6 dB of image rejection worst case**, affecting half of all frequencies. Not the
  cause of the spurs, but **the period is now forced even and the quadrature is exact by
  construction**. It is free: the doubled frequency step it costs has been absorbed by the
  NCO since V4.10.
- **New bench `make logen`**, sweeping all 294,001 hertz from 6 to 300 kHz. Seen to fail:
  147,338 cases red without the even adjustment.

---

## V4.11 · 2026-10-09

### Qué cambia

- **Arreglado el aparcado del NCO en onda larga.** Por debajo de 44 kHz de dial la cuenta
  `freq - off_max` se iba por debajo de cero y, siendo sin signo, daba la vuelta: el
  oscilador salía a **4.294 MHz**. La sintonía se quedaba donde le daba la gana y el panel
  se dibujaba alrededor de un número inventado. La radio baja a 30 kHz desde que se
  añadieron las horarias, así que toda la franja de 30 a 44 kHz estaba rota con el NCO
  puesto.
- **Y el punto de reaparcado ya no cae fuera de la banda plana del códec.** El comentario
  decía «±40 kHz» y los `#define` decían 44.000: al reaparcar bajando, la emisora quedaba
  al 92% de Nyquist, dentro de la caída del antialias. Por eso después de un salto «no
  recibía nada».
- **El banco probaba el aparcado sólo en 7,15 MHz**, donde el fallo no existe. Ahora barre
  ocho diales de 30 a 91,3 kHz con los dos topes. Visto fallar: seis de ocho en rojo.

### What's new

- **Fixed the NCO parking in the longwave band.** Below 44 kHz of dial, `freq - off_max`
  went below zero and, being unsigned, wrapped: the oscillator came out at **4,294 MHz**.
  Tuning ended up wherever it liked and the panel was drawn around an invented number. The
  radio goes down to 30 kHz since the time stations were added, so the whole 30–44 kHz
  stretch was broken with the NCO on.
- **And the re-park point no longer lands outside the codec's flat band.** The comment said
  "±40 kHz" and the `#define`s said 44,000: re-parking downwards left the station at 92% of
  Nyquist, inside the antialias roll-off. That is why, after a jump, it "received nothing".
- **The bench only tested parking at 7.15 MHz**, where the bug cannot happen. It now sweeps
  eight dials from 30 to 91.3 kHz with both limits. Seen to fail: six of eight red.

---

## V4.10 · 2026-10-09

### Qué cambia

- **Por debajo de 300 kHz el dial podía estar hasta 900 Hz de donde decía.** Ahí el
  oscilador lo hace un temporizador del GD32, cuyo periodo es un número entero de cuentas:
  el escalón crece con el cuadrado de la frecuencia (58 Hz a 54 kHz, **1.803 Hz a
  300 kHz**), y el desplazamiento digital se calculaba contra la frecuencia **pedida**, no
  contra la que sale. En onda larga de radiodifusión eso es casi un canal. Ahora se guarda
  la real y el NCO se come el redondeo: el dial vuelve a ser exacto, y la regla del
  espectro con él.
- **Y la fila «Cuadratura I/Q» ya dice dónde está ese oscilador.** Ponía «GD32, PA6/PA7» y
  se acababa: en toda la banda de DCF77, ALS162, RBU y las horarias, la radio no decía
  dónde tenía el oscilador. Ahora pone la frecuencia que sale de verdad y su desvío contra
  la pedida, `54025 Hz (+25)`.
- **Sobre los 66 kHz: el oscilador queda descartado con números** — a esa frecuencia el
  desvío es de 25 Hz, no de medio kilohercio.

### What's new

- **Below 300 kHz the dial could be up to 900 Hz off from what it read.** There the local
  oscillator is a GD32 timer whose period is a whole number of counts: the step grows with
  the square of the frequency (58 Hz at 54 kHz, **1,803 Hz at 300 kHz**), and the digital
  offset was computed against the **requested** frequency, not the one that comes out. On
  the longwave broadcast band that is nearly a channel. The real value is now kept and the
  NCO absorbs the rounding: the dial is exact again, and the spectrum ruler with it.
- **And the "I/Q quadrature" row now says where that oscillator is.** It read "GD32,
  PA6/PA7" and stopped: across the whole DCF77 / ALS162 / RBU / time-station band the radio
  never said where its oscillator was. It now shows the frequency that actually comes out
  and its deviation from the requested one, `54025 Hz (+25)`.
- **On the 66 kHz report: the oscillator is ruled out with numbers** — the deviation there
  is 25 Hz, not half a kilohertz.

---

## V4.09 · 2026-10-09

### Qué cambia

- **AIS no decodificaba nada a 48 kHz** y parecía que no había tráfico: el receptor
  arrancaba con un `96000` escrito a pelo. A 48 kHz salen cinco muestras por bit y no diez,
  así que el reloj muestreaba un bit sí y otro no. Ahora pregunta la tasa, y el cambio de
  RATE también reinicia AIS.
- **La FM se quedaba sorda al pasar por el reloj por radio**, con 4 kHz de audio en vez de
  15, sin forma de arreglarlo desde la interfaz — y el autoguardado podía dejarlo escrito
  en `CONFIG.CSV` para siempre.
- **Entrar en el reloj por radio dejaba los decodificadores encendidos**: el botón de Modo
  se quedaba en `?`, y viniendo de CW los tics de la emisora horaria sonaban como un
  silbido, porque seguían pasando por el filtro de 250–1000 Hz.
- **El borrador de ruido dejaba de borrar medio segundo al tocar el atenuador**, que es
  justo cuando lo tocas porque la banda se ha puesto ruidosa. Lo pedía su propia cabecera.
- **El WEFAX dibujaba y guardaba la línea anterior, rota por la mitad**: la bandera se
  publicaba antes de cerrar el buffer.
- **El identificador de NAVTEX podía ser uno que no existió nunca** — mezcla de la cabecera
  anterior y la nueva durante medio segundo en cada mensaje.
- **ALE daba una palabra con la calidad de otra** y perdía la siguiente, y el contador de
  la pantalla se quedaba descuadrado para siempre. Ahora es un anillo, como los otros nueve.
- **Y cuatro más**: el puntero del gancho de audio se leía dos veces (ventana de salto a la
  dirección cero), el contador de bloques perdidos de FT8 marcaba cero justo cuando se
  perdían, el DSC podía volcar una llamada sin su veredicto, y conmutar a AIS reconfiguraba
  un filtro que la interrupción estaba usando.

### What's new

- **AIS decoded nothing at 48 kHz** and it looked like empty traffic: the receiver was
  started with a hard-coded `96000`. At 48 kHz there are five samples per bit, not ten, so
  the clock sampled every other bit. It now asks for the rate, and changing RATE restarts
  AIS too.
- **FM went dull after a trip through the radio-clock screen** — 4 kHz of audio instead of
  15, with no way to fix it from the UI, and autosave could write it into `CONFIG.CSV` for
  good.
- **Entering the radio-clock screen left the decoders running**: the Mode button stuck at
  `?`, and coming from CW the time station's ticks came out as a narrow whistle, still
  going through the 250–1000 Hz filter.
- **The noise blanker stopped blanking for half a second when you touched the
  attenuator** — exactly when you touch it because the band got noisy. Its own header asked
  for this.
- **WEFAX drew and saved the previous line, torn in half**: the flag was published before
  the buffer was closed.
- **The NAVTEX identifier could be one that never existed on the air** — a mixture of the
  previous header and the new one for half a second on every message.
- **ALE returned one word with another word's quality** and dropped the next one, leaving
  the on-screen counter permanently out of step. It is a ring now, like the other nine.
- **And four more**: the audio hook pointer was read twice (a window where it branches to
  address zero), FT8's dropped-block counter read zero exactly when blocks were dropped,
  DSC could dump a call without its verdict, and switching to AIS reconfigured a filter the
  interrupt was using.

---

## V4.08 · 2026-10-09

### Qué cambia

- **Arreglado: la cabecera de WEFAX escribía seis bytes fuera de su array.** El buffer
  medía 40 y el peor caso son 46, desde que se le añadió la corrección de inclinación
  (`+NNNN ppm`). Y no era un caso raro: en cuanto una carta deja aprendida cualquier
  corrección, el texto se repinta con ese campo **cada 500 ms** hasta salir del modo.
- **Dos buffers que cabían por un pelo**, ampliados y con la cuenta atada a un
  `_Static_assert`: el detalle de la hora por RDS (63 de 64) y la fila «Flash externa»
  (30 de 32, y es justo la fila que existe para detectar chips raros). De paso, un
  `", descartadas "` que salía en castellano dentro de la frase inglesa.
- **Una guarda que no cubría lo que venía detrás** en la pantalla de calibración del
  táctil: reservaba un carácter y se escriben siete.
- **Dos redes que faltaban en HFDL:** el bucle del sincronizador no cumplía el contrato
  que su propia cabecera declara (podía escribir más de 16 muestras en arrays de 16 de la
  pila, o no terminar), y el desentrelazador tenía la vuelta de columna en `push()` pero
  no en `pop()`.
- **Un desbordamiento con signo** en el reloj de bit de AIS y APRS, ahora hecho sin signo.
  Con honestidad: **ningún banco lo cazó** y 400 semillas siguen en verde — la ventana es
  de un solo valor del acumulador entre 2³². Salió leyendo.
- **Y una medida incómoda sobre nuestro propio banco de fuzzing:** sólo prueba la mitad
  delantera de cada decodificador. El «3.780 millones de muestras, limpio» cubre la mitad
  del ISR, no el decodificador entero. Queda escrito en los pendientes.

### What's new

- **Fixed: the WEFAX header wrote six bytes past the end of its array.** The buffer was 40
  and the worst case is 46, ever since the slant correction (`+NNNN ppm`) was added. Not a
  rare case either: once a chart leaves any correction learned, the text redraws with that
  field **every 500 ms** until you leave the mode.
- **Two buffers that fitted by one byte**, enlarged with the arithmetic bound to a
  `_Static_assert`: the RDS clock detail (63 of 64) and the "External flash" row (30 of 32
  — and that row exists precisely to catch odd chips). Also one Spanish literal that leaked
  into the English sentence.
- **A guard that did not cover what came after it** in the touch calibration screen: it
  reserved one character and seven bytes are written.
- **Two missing safety nets in HFDL:** the symbol-sync loop did not honour the contract its
  own header states (it could write more than 16 samples into 16-element stack arrays, or
  never terminate), and the deinterleaver had the column wrap in `push()` but not in
  `pop()`.
- **A signed-overflow UB** in the AIS and APRS bit clock, now done unsigned. Honestly:
  **no bench caught it** and 400 seeds stay green — the window is a single accumulator
  value out of 2³². It came from reading, not measuring.
- **And an uncomfortable measurement about our own fuzz bench:** it only exercises the
  front half of each decoder. The "3,780 M samples, clean" covers the ISR half, not the
  whole decoder. Written into the pending list.

---

## V4.07 · 2026-10-09

### Qué cambia

- **El margen de ruido del CW, en pantalla.** `cw.c` persigue dos medias —la baja sobre
  el ruido, la alta sobre la señal— y pone el umbral entre las dos. Las dos tenían
  función pública desde que se escribieron **y no las llamaba nadie**. Ahora la chapa del
  panel pone **la separación entre ellas en dB**, al lado de los ppm: por debajo de
  1,2 dB se han juntado y la radio calla a propósito, ~2 dB es el peor caso que intenta
  sacar, 10 o más es señal cómoda. Es un diagnóstico, **no** una condición nueva: lo que
  separa Morse de ruido sigue siendo la forma, no el nivel.
- **La velocidad ya se detectaba sola** entre 5 y 45 ppm, y sale en la chapa; en
  Ajustes → `CW PPM` salen las dos, la pista que le das y la que ha medido.
- **Un desbordamiento cazado antes de salir.** El buffer de la chapa medía 16 bytes y el
  peor caso de CW pasaba a 18. Ahora lleva la cuenta escrita al lado y un
  `_Static_assert` que la ata, y `sim/digi.c` mide que la chapa no invada el botón
  "Aprende".

### What's new

- **The CW noise margin, on screen.** `cw.c` tracks two running means — the low one on
  the noise, the high one on the signal — and puts the threshold between them. Both have
  had public accessors since the day they were written **and nothing called them**. The
  panel badge now shows **the gap between them in dB**, next to the wpm: under 1.2 dB
  they have collapsed and the radio stays quiet on purpose, ~2 dB is the worst case it
  tries to decode, 10 or more is a comfortable signal. It is a diagnostic, **not** a new
  condition: what separates Morse from noise is still shape, not level.
- **Speed was already detected automatically**, 5 to 45 wpm, and shows in the badge;
  Settings → `CW PPM` shows both the hint you give it and what it measured.
- **An overflow caught before it shipped.** The badge buffer was 16 bytes and CW's worst
  case grew to 18. It now carries the arithmetic beside it and a `_Static_assert` that
  binds the two, and `sim/digi.c` measures that the badge cannot invade the "Learn"
  button.

---

## V4.06 · 2026-10-09

### Qué cambia

- **El AGC estaba escrito dos veces, y una de las dos copias se quedó atrás.** El de FM
  ancha seguía haciendo una división de coma flotante **por muestra** —192.000 por
  segundo, el 1,3% del presupuesto de la interrupción más apretada que hay— porque la
  optimización del 05/10 se aplicó al otro bucle y nadie miraba los dos. Ahora el lazo
  está **una sola vez**, en `User/agc.c`, y los dos caminos lo llaman: la división
  desaparece por construcción. **160 bytes menos de flash.**
- **Y en los silencios ya no se divide nunca.** Quedaba viva una división por muestra
  para el caso sin señal —la mitad del tiempo que se escucha una radio—; ahora se calcula
  una vez por bloque.
- **Primer banco de pruebas de la cadena de audio.** El simulador no compilaba ni una
  línea del camino del sonido: todos los decodificadores tenían banco, lo que suena por
  el altavoz no tenía ninguno. `make agc` compila `agc.c` **de verdad** y lo compara
  muestra a muestra con un AGC de libro: 8 señales × 2 caminos × 3 relajaciones, y
  **1 LSB** de diferencia máxima. Visto fallar de dos maneras, hasta 28.414 LSB.
- **Revisado y limpio:** el notch adaptativo, el reductor de ruido, el volumen y la PGA.
- **Abierto a propósito:** el silenciador corta en seco y eso es un chasquido en cada
  apertura; y `sdr_rx.c` manda al audio los bloques que ha marcado como corruptos. Las
  dos piden una decisión, no un parche.

### What's new

- **The AGC existed twice, and one copy fell behind.** Wide-FM was still doing one
  floating-point division **per sample** — 192,000 a second, 1.3% of the tightest
  interrupt budget in the radio — because the 05/10 optimisation was applied to the other
  loop and nothing looked at both. The loop now lives **once**, in `User/agc.c`, and both
  paths call it: the division is gone by construction. **160 bytes of flash saved.**
- **And silence no longer divides at all.** One per-sample division survived for the
  below-floor case — half the time you listen to a radio; it is now computed once per
  block.
- **First bench for the audio chain.** The simulator compiled none of the sound path:
  every decoder had a bench, what comes out of the speaker had none. `make agc` compiles
  the **real** `agc.c` and compares it sample by sample against a textbook AGC: 8 signals
  × 2 paths × 3 release settings, **1 LSB** worst-case difference. Seen to fail two ways,
  up to 28,414 LSB.
- **Reviewed and clean:** the adaptive notch, the noise reducer, volume and PGA.
- **Left open on purpose:** the squelch cuts dead, which is a click on every open; and
  `sdr_rx.c` still feeds blocks it has marked corrupt to the audio. Both need a decision,
  not a patch.

---

## V4.05 · 2026-10-09

### Qué cambia

- **El multiplicador de la zona 7 se puede barrer desde la radio** (issue #35). Fila
  nueva en Información, **«Mult. zona 7»**: un toque pasa al siguiente valor, 168…189 y
  vuelta a empezar, rehaciendo la maniobra de los 90° **sin mover el dial**. Así el
  barrido entero se hace sobre la misma emisora en una sesión, leyendo la sensibilidad
  en el espectro y el rechazo de imagen en «Espejo I/Q», en vez de recompilar veintidós
  veces. No se guarda: al arrancar vuelve a 168.
- **Y la respuesta al issue, con números.** Los dos valores que discute —168 y 189— son
  justo **los dos extremos del mismo rango válido** (167 saca el VCO por abajo, 190 por
  arriba). El error de frecuencia es ≤ 0,15 Hz con los dos; el divisor del multisynth es
  el multiplicador exacto en los dos; la maniobra de los 90° no depende del
  multiplicador; el ruido de fase favorece a 189 por **1,0 dB**; y el tercer armónico se
  mueve **0,003 dB**. O sea que **el compromiso que describe el issue no sale de las
  cuentas** — por eso el número tiene que salir de la radio. Y la razón de que hubiera un
  168 es que **la sensibilidad nunca fue un criterio** al derivar la tabla: 168 es el
  mínimo que cabe y se cogió el mínimo.
- **Los asertos de la tabla ahora cubren todo el rango que se puede pedir**, no sólo el
  valor por omisión.

### What's new

- **Zone 7's multiplier can now be swept from the radio** (issue #35). New row in Info,
  **"Zone-7 mult."**: one tap moves to the next value, 168…189 and back around, redoing
  the 90° manoeuvre **without moving the dial**. The whole sweep happens on the same
  station in one session, reading sensitivity off the spectrum and image rejection off
  the "I/Q mirror" row next to it, instead of rebuilding twenty-two times. Not saved: it
  returns to 168 at boot.
- **And the answer to the issue, with numbers.** The two values under discussion — 168
  and 189 — are exactly **the two ends of the same valid range** (167 drops the VCO below
  600 MHz, 190 pushes it past 908). The synthesised-frequency error is ≤ 0.15 Hz for
  both; the Multisynth divider is the multiplier exactly for both; the 90° manoeuvre does
  not depend on the multiplier; phase noise favours 189 by **1.0 dB**; and the third
  harmonic moves **0.003 dB**. So **the trade-off the issue describes does not follow
  from the arithmetic** — which is why the number has to come off the radio. And the
  reason 168 is there at all: **sensitivity was never a criterion** when the table was
  derived. 168 is the minimum that fits, and the minimum is what was taken.
- **The table's static assertions now cover the whole range that can be requested**, not
  just the default value.

---

## V4.04 · 2026-10-08

### Qué cambia

- **El medidor de velocidad del Ident, arreglado de raíz.** Medía *la racha más
  corta que se repite*: con una alternancia limpia eso es el símbolo, pero con datos
  cifrados las rachas duran varios símbolos y se agarraba al ruido —610,8 Bd a 3 kHz
  son cinco muestras por racha—. Ahora se busca **el período que explica todos los
  grupos a la vez**: cada candidato se prueba como un peine con púas en T, 2T, 3T…,
  se le descuenta lo que recogería por puro azar (un peine corto tiene más púas) y se
  exige que la fundamental esté poblada. Contra la vara de medir: `sitorb` de 3% a
  **1%**, `navtex8424` de **511% a 1%**, `stanag4481_fsk` de **748% a 0%**. Y si
  ningún peine destaca, no dice nada en vez de inventarse un número.
- **`sim/det_velocidad.c` entra en `make comprueba`.** Llevaba fuera a propósito
  porque salía en rojo. Un banco que se deja fuera para que no cante es la versión
  educada de no tener banco.
- **El Ident dice cuánto encaja, en tanto por ciento.** Al lado del nombre. No es una
  probabilidad —no hay con qué calcularla— sino **lo lejos que está la señal de dejar
  de cuadrar con esa fila**: lo centrado que cae en cada rango, la calidad de la
  medida que manda, y si la fila se ha visto alguna vez en antena. Se queda con el
  peor de los tres, porque una cadena vale lo que su eslabón más flojo. Si cuadran
  varias filas, todas bajan. En el corpus: lo correcto sale entre 70% y 100%, y la
  identificación falsa de «CW (Morse)» sobre una grabación de 5339 kHz sale al 40%.
- **NAVTEX con los tonos poco claros, ya se identifica.** La grabación de 8424 kHz no
  cuadraba porque el motor **no le ve dos tonos** (205 Hz de ancho, demasiado juntos y
  desvanecidos), no por la velocidad. Pero el alfabeto sí se ve: 78% a 100 Bd. Fila
  nueva que va por ancho **más alfabeto**.
- **Un cargador viejo ya no puede quedarse rondando.** `customboot.bin` vivía en tres
  sitios con tres contenidos, y el README mandaba grabar el de `rescate\`, que llevaba
  dos versiones de retraso. Ahora la copia de la raíz la hace la compilación y
  `make comprueba` falla si alguna se separa. Un manual atrasado se lee raro; un
  cargador atrasado **se graba**.

### What's new

- **The Ident's baud meter, fixed at the root.** It measured *the shortest repeating
  run*: with clean alternation that is the symbol, but with encrypted data runs last
  several symbols and it latched onto noise — 610.8 Bd at 3 kHz is five samples per
  run. It now looks for **the period that explains all the groups at once**: each
  candidate is tried as a comb with teeth at T, 2T, 3T…, what it would catch by pure
  chance is subtracted (a short comb has more teeth), and the fundamental is required
  to carry weight. Against the measuring stick: `sitorb` from 3% to **1%**,
  `navtex8424` from **511% to 1%**, `stanag4481_fsk` from **748% to 0%**. And if no
  comb stands out, it says nothing rather than inventing a number.
- **`sim/det_velocidad.c` now runs in `make comprueba`.** It had been left out on
  purpose because it was red. A bench kept out so it won't complain is the polite
  version of having no bench.
- **The Ident now says how well it fits, as a percentage.** Next to the name. It is
  not a probability — there is nothing to compute one from — but **how far the signal
  is from no longer matching that row**: how centred it falls in each range, the
  quality of the measurement that rules that row, and whether the row has ever been
  seen on the air. It keeps the worst of the three, because a chain is worth what its
  weakest link is worth. If several rows match, all of them drop. Across the corpus:
  correct answers land between 70% and 100%, and the false "CW (Morse)" on a 5339 kHz
  recording comes out at 40%.
- **NAVTEX with unclear tones is now identified.** The 8424 kHz recording did not
  match because the engine **cannot see two tones** in it (205 Hz wide, too close and
  too faded), not because of the baud rate. But the alphabet is visible: 78% at
  100 Bd. New row that goes by width **plus alphabet**.
- **A stale bootloader can no longer linger.** `customboot.bin` lived in three places
  with three contents, and the README told you to flash the one in `rescate\`, two
  versions behind. The root copy is now made by the build, and `make comprueba` fails
  if any copy drifts. A stale manual just reads oddly; a stale bootloader **gets
  flashed**.

## V4.03 · 2026-10-08

### Qué cambia

- **La radio ya dice POR QUÉ se reinició.** Un usuario reporta en la V4.01 reinicios
  aleatorios, que al volver no recupera el modo ni la configuración de pantalla, y
  uno de ellos recibiendo FT8. Antes de tocar nada: en esta radio **un fallo de CPU
  cuelga, no reinicia** —no hay perro guardián en ningún sitio, el `HardFault_Handler`
  es un bucle infinito, no hay WFI ni standby, y el único reinicio por software de
  todo el árbol lo pide el usuario a mano desde la fila de DFU—. Así que un reinicio
  de verdad sólo puede ser tensión, la patilla de reset, software o un perro que
  nadie enciende. Y **el chip lo sabe**: `RCU_RSTSCK` guarda la causa y este firmware
  no la miraba nunca. Nueva fila **Reinicios** en Información: la causa de este
  arranque, cuántos arranques van desde el último en frío, y el historial de los
  ocho últimos. `T` tensión, `P` patilla, `S` software, `G` perro, `B` bajo consumo.
  `T T T` es una batería que se cae; `T P T` es otra cosa. No evita ni un reinicio:
  convierte "se reinicia random" en una letra.
- **Cinco esperas de hardware sin salida, ahora con tope.** Todas colgaban la radio
  (pantalla congelada con el audio sonando, que va por DMA), no la reiniciaban:
  - `sdr_rx_stop()` esperaba que el DMA de recepción soltara su bit de activo. Se
    recorre **cada cambio de modo** que cruce un cambio de tasa de muestreo. Y tenía
    además el orden mal: no apagaba la petición del lado del SPI antes del canal, que
    es justo lo que su gemela de audio documenta como la regla a seguir —dos funciones
    iguales y sólo una la cumplía—.
  - Las dos del DMA de audio, una de ellas **en el arranque en frío**: si se quedaba
    ahí, la radio no llegaba a pintar nada.
  - La transferencia por SPI de la flash en el **cargador**, que se recorre en
    **todos** los arranques buscando `update.bin`: colgaba con la pantalla negra
    antes de nada. Ahora un byte perdido es un `0xFF`, la cabecera no cuadra y el
    cargador arranca la aplicación, que es infinitamente mejor.
  - El retardo de USB del cargador, que esperaba una variable que sólo mueve una
    interrupción: colgaba el modo actualización en "USB: conectando…".

### What's new

- **The radio now says WHY it restarted.** A user reports random reboots on V4.01,
  coming back without the previous mode or the screen settings, one of them while
  receiving FT8. Before changing anything: on this radio **a CPU fault hangs, it does
  not reboot** — there is no watchdog anywhere, `HardFault_Handler` is an infinite
  loop, there is no WFI or standby, and the only software reset in the whole tree is
  the one the owner asks for by hand from the DFU row. So a real reboot can only be
  supply voltage, the reset pin, software, or a watchdog nobody enables. And **the
  chip knows**: `RCU_RSTSCK` holds the cause and this firmware never read it. New
  **Resets** row in Information: this boot's cause, how many boots since the last
  cold start, and the last eight. `T` voltage, `P` pin, `S` software, `G` watchdog,
  `B` low power. `T T T` is a sagging battery; `T P T` is something else. It prevents
  no reboot at all: it turns "random reboots" into a letter.
- **Five unbounded hardware waits now have a limit.** All of them froze the radio
  (screen locked with audio still playing, since audio runs on DMA) rather than
  rebooting it:
  - `sdr_rx_stop()` waited for the receive DMA channel to drop its enable bit. This
    runs on **every mode change** that crosses a sample-rate change. It also had the
    ordering wrong: it did not disable the SPI-side request before the channel, which
    is exactly the rule its own audio-side twin documents — two functions doing the
    same thing and only one following it.
  - Both audio DMA waits, one of them **during cold boot**: stuck there, the radio
    would never draw anything.
  - The **bootloader's** SPI flash byte transfer, which runs on **every** boot looking
    for `update.bin`: it hung with a black screen before anything appeared. Now a lost
    byte becomes `0xFF`, the header fails to match, and the bootloader boots the
    application instead.
  - The bootloader's USB delay, which waited on a variable only an interrupt moves: it
    hung update mode at "USB: connecting…".

## V4.02 · 2026-10-08

### Qué cambia

- **La FSK cifrada de 50 Bd / 200 Hz ya se identifica.** El usuario mandó una
  grabación de 16.911 kHz: el Ident medía bien el salto (201 Hz) y la velocidad
  (50,0 Bd), tenía la fila puesta desde hace dos días… y contestaba «MFSK
  multitono (Olivia)». La fila pedía velocidad de símbolo de 46 a 55 Bd y el
  medidor daba 25,0: la mitad exacta, porque en una FSK de dos tonos con bits al
  azar el tono cambia cada dos bits y ese medidor cuenta cambios de tono, no
  símbolos. Ahora la fila pide **el alfabeto**: de cada grupo de siete bits,
  cuántos tienen exactamente cuatro unos (el CCIR 476 del NAVTEX). **78% en esta
  señal contra 31% en una FSK con bits al azar del mismo salto y la misma
  velocidad** — lo que no distingue ningún espectro. Y el baudio sale de la
  propia medida en vez del medidor flojo.
- **El manual era tres manuales.** Había tres `MANUAL_V2_ES.md` con tres
  contenidos distintos y dos `.docx`, y el banco de cobertura medía la copia más
  vieja: llevaba dos días diciendo que la tabla del Ident tiene 27 firmas cuando
  tiene 28. Ahora sólo hay uno, en `manual/`, y un control nuevo tumba la entrega
  si aparece una segunda copia.
- **Fuera el almacén de imágenes en flash** (`img_store.c`, 26 funciones): nunca
  llegó a usarse —las capturas van al disco USB como BMP— y su banco llevaba
  meses midiendo código que el enlazador tiraba entero.
- **Dos bancos que no fallaban, ahora fallan.** `sin_llamar.py` llevaba dos días
  sacando en cada entrega que `cargador_arranca()` estaba muerta —mentira: la
  llama el arranque— porque sólo leía `User/` y no `boot/`; un aviso falso que
  sale siempre deja de leerse. Y `manual_check.py` ni siquiera estaba en ningún
  Makefile.

### What's new

- **The encrypted 50 Bd / 200 Hz FSK is now identified.** The owner sent a
  16.911 kHz recording: Ident measured the shift (201 Hz) and the rate (50.0 Bd)
  correctly, the signature row had been in the table for two days… and it
  answered "MFSK multitone (Olivia)". The row asked for a symbol rate of 46 to
  55 Bd and the meter said 25.0 — exactly half, because in a two-tone FSK with
  random bits the tone changes every two bits, so that meter counts tone
  changes, not symbols. The row now asks for **the alphabet**: out of every
  seven-bit group, how many have exactly four ones (NAVTEX's CCIR 476). **78% on
  this signal against 31% on a random-bit FSK with the same shift and the same
  rate** — something no spectrum measurement can tell apart. And the baud rate
  now comes out of that same measurement instead of the unreliable meter.
- **The manual was three manuals.** Three `MANUAL_V2_ES.md` files with three
  different contents and two `.docx`, and the coverage bench was measuring the
  oldest copy: for two days it claimed the Ident table has 27 signatures when it
  has 28. Now there is only one, in `manual/`, and a new check fails the build if
  a second copy appears.
- **The flash image store is gone** (`img_store.c`, 26 functions): it was never
  used — screenshots go to the USB disk as BMP — and its bench had been
  measuring code the linker discarded entirely.
- **Two benches that never failed now fail.** For two days `sin_llamar.py`
  reported on every build that `cargador_arranca()` was dead — untrue: the
  bootloader calls it — because it only read `User/`, not `boot/`; a false
  warning that always fires stops being read. And `manual_check.py` was not even
  in any Makefile.

## V4.01 · 2026-10-08

### Qué cambia

Revisión a fondo de todo el código fuente, a petición del usuario: **44 fallos
encontrados y arreglados**. Los que se notan:

- **El botón Aprende del CW no podía funcionar.** Había puesto el umbral de
  ataque por debajo del de suelta, o sea histéresis al revés: la tecla
  parpadeaba y la puerta de calidad no abría nunca. Corregido y vuelto a medir:
  de la grabación débil de 4XZ ahora salen **seis indicativos en vez de dos**.
- **Guardar los ajustes podía corromper la flash.** El guardado en segundo plano
  escribía 32 bytes fuera de su borrador cuando CONFIG.CSV no estaba en el
  primer sector del directorio, y grababa la entrada en el bloque equivocado.
  Además el camino rápido daba por hecho que el fichero no estaba troceado y
  podía escribir encima de otro.
- **El notch automático no funcionaba en AM ni en SAM.** Se podía poner "a saco",
  la etiqueta cambiaba, y el heterodino seguía igual: la llamada estaba sólo en
  el camino de banda lateral.
- **El modo DSC se quedaba sordo tras la primera llamada**, hasta salir y volver
  a entrar.
- **Elegir PSK31 no apagaba el DSC**, y eso dejaba el botón de Modo en `?`, la
  rejilla sin marcar nada y el panel de PSK31 sin pintarse.
- **Elegir una banda no apagaba los decodificadores digitales**: pasar de FT8 a
  FM comercial dejaba FT8 vivo con la memoria de la cascada prestada.
- **Mover el ancho o el tono en CW daba un chasquido a fondo de escala**, porque
  los coeficientes del filtro se reescribían con la interrupción de audio viva.
- **La chapa del ancho de filtro se salía un byte en cada fotograma** en cuanto
  el corte bajo dejaba de ser cero.
- **El RTTY arrancaba con la ventana equivocada** si CONFIG.CSV no traía la
  velocidad: funcionaba con señal fuerte y no enganchaba con señal floja.
- **Las chapas de la barra de estado se congelaban** mientras el mando estuviera
  enganchado a un ajuste: tocabas NR o MUDO y no cambiaban de aspecto.
- Y el resto: el STANAG perdiendo el enganche por leer muestras ya pisadas, el
  SSTV y el NAVTEX colgándose sin plazo de salida, el cargador borrando la
  aplicación antes de validar el fichero, esperas de hardware sin plazo que
  colgaban la radio, y una veintena de variables compartidas con la interrupción
  de audio sin `volatile`.

### What's new

A full source-code review, at the owner's request: **44 faults found and fixed**.
The ones you would notice:

- **The CW Learn button could not work.** The attack threshold had been set
  *below* the release threshold — inverted hysteresis — so the key chattered and
  the quality gate never opened. Fixed and re-measured: the weak 4XZ recording
  now yields **six callsigns instead of two**.
- **Saving settings could corrupt the flash.** The background save wrote 32 bytes
  past its scratch buffer whenever CONFIG.CSV was not in the first directory
  sector, and wrote the entry to the wrong block. The fast path also assumed the
  file was unfragmented and could overwrite another one.
- **The auto-notch did nothing in AM or SAM** — the call was only on the
  sideband path.
- **DSC went deaf after the first call**, until you left the mode and came back.
- **Selecting PSK31 did not stop DSC**, which left the Mode button showing `?`,
  nothing selected in the mode grid and the PSK31 panel never drawn.
- **Selecting a band did not stop the digital decoders**: going from FT8 to
  broadcast FM left FT8 running with the waterfall's borrowed memory.
- **Moving CW width or pitch produced a full-scale click**, because the filter
  coefficients were rewritten with the audio interrupt live.
- **The filter-width badge overran its buffer by one byte every frame** as soon
  as the low cutoff was non-zero.
- **RTTY started with the wrong window** when CONFIG.CSV lacked the speed key:
  it worked on strong signals and would not lock on weak ones.
- **The status-bar badges froze** while the knob was latched to a setting.
- Plus the rest: STANAG losing lock by reading samples already overwritten,
  SSTV and NAVTEX hanging with no escape timer, the bootloader erasing the
  application before validating the file, unbounded hardware waits that could
  hang the radio, and a score of variables shared with the audio interrupt
  missing `volatile`.

---

## V4.00 · 2026-10-08

### Qué cambia

- **El aprendizaje de CW ya lee de verdad.** Faltaba una pieza que tenía
  delante desde el principio: cuando decodifiqué la grabación a mano, yo cortaba
  la señal en el 40 % del recorrido y la radio lo hace en el 60 %. Con el umbral
  alto cada elemento pierde unos milisegundos por cada punta, y eso basta para
  que los puntos largos de una estación como 4XZ crucen la frontera y se
  conviertan en rayas.
- Con el botón encendido el corte baja al 35 %. De la grabación fuerte de 4XZ,
  que antes no soltaba **una sola letra**, ahora sale:

  `VVV DE 4XZ W QTC 1 NR 28 = =`

  y de la débil, cinco indicativos en vez de dos.
- Sigue sin escribir nada sobre ruido, y eso ahora se comprueba contra **ruido
  de banda** —chasquidos estáticos y desvanecimiento, que es lo que hay de
  verdad en 4 MHz— y no sólo contra ruido blanco, que era demasiado fácil.
- Con el botón apagado, nada cambia: comprobado letra por letra.

### What's new

- **CW learning now actually reads.** One piece was missing and it had been in
  front of me all along: when I decoded the recording by hand I sliced the
  signal at 40 % of the range and the radio does it at 60 %. A high threshold
  shaves a few milliseconds off each end of every element, and that is enough to
  push the long dots of a station like 4XZ across the boundary into dashes.
- With the button on, the slice drops to 35 %. The strong 4XZ recording, which
  previously produced **not one letter**, now gives:

  `VVV DE 4XZ W QTC 1 NR 28 = =`

  and the weak one yields five callsigns instead of two.
- It still writes nothing on noise, and that is now checked against **band
  noise** — static crashes and fading, which is what 4 MHz actually sounds like
  — not just white noise, which was far too easy.
- With the button off nothing changes: verified letter for letter.

---

## V3.99 · 2026-10-08

### Qué cambia

- **La chapa del panel de CW enseña, con el aprendizaje encendido, la relación
  raya/punto que ha medido.** Sin ese número no hay forma de ver desde fuera si
  está midiendo algo o está parado. `3,0` es que la estación teclea según la
  norma y el aprendizaje no va a cambiar nada; `3,8` o más es una de las raras
  —4XZ sale ahí—; `--` es que todavía no ha medido.
- Junto al número sigue el punto verde de enganchado, que es la puerta de
  calidad: si está apagado, la radio ve elementos pero no se fía de ellos.

### What's new

- **The CW panel chip now shows the measured dash/dot ratio while learning is
  on.** Without that number there is no way to tell from outside whether it is
  measuring anything or stalled. `3.0` means the station keys to spec and
  learning will change nothing; `3.8` or more is one of the awkward ones — 4XZ
  lands there; `--` means it has not measured yet.
- Next to it is the usual lock dot, which is the quality gate: dark means the
  radio sees elements but does not trust them.

---

## V3.98 · 2026-10-08

### Qué cambia

- **El botón "Aprende" del CW ya responde.** En la V3.97 se dibujaba pero el
  toque no le llegaba: el reparto de pulsaciones del segundo botón tenía una
  lista de modos escrita a mano (SSTV, WEFAX y STANAG) y el CW no estaba en
  ella. Se ha quitado la lista entera: ahora quien decide si hay segundo botón
  es el que lo dibuja, igual que ya se hizo con el quinto. Un modo nuevo con
  segundo botón entra solo.

### What's new

- **The CW "Learn" button now responds.** In V3.97 it was drawn but never
  received the tap: the second button's touch dispatch carried a hand-written
  list of modes (SSTV, WEFAX and STANAG) and CW was not on it. The list is gone
  entirely — what decides whether there is a second button is now whatever
  draws it, exactly as was already done for the fifth. A new mode with a second
  button is picked up automatically.

---

## V3.97 · 2026-10-08

### Qué cambia

- **Botón "Aprende" en el panel de CW**, al lado de Borrar. Idea del usuario.
- El Morse tiene una regla: la raya dura tres puntos. La radio la da por buena,
  y acierta, porque es lo que manda casi todo el mundo. Pero hay estaciones que
  teclean fuera de norma. La naval israelí **4XZ** en 4.330 kHz manda los puntos
  de dos tamaños (40 y 80 ms) y las rayas de dos (160 y 200): con la regla puesta
  la radio no saca ni una letra de ella.
- Con el botón **encendido**, la radio deja de suponer y **mide** cómo teclea esa
  estación: dónde caen los cortos, dónde los largos, y pone la frontera en medio.
  De esa grabación pasa a salir `VVV DE 4XZ` limpio.
- Con el botón **apagado** no cambia absolutamente nada — está comprobado letra
  por letra contra la versión anterior. Y **arranca siempre apagado**: medir en
  vez de suponer cuesta precisión con el Morse normal, así que es una herramienta
  para una estación concreta, no un ajuste que se queda puesto.
- Sigue callándose con ruido estando encendido. Eso es lo que más se ha medido:
  un minuto de ruido blanco, cero caracteres.

### What's new

- **A "Learn" button in the CW panel**, next to Clear. The owner's idea.
- Morse has one rule: a dash lasts three dots. The radio assumes it, and it is
  right to, because that is what nearly everyone sends. But some stations key
  out of spec. The Israeli naval station **4XZ** on 4,330 kHz sends dots in two
  lengths (40 and 80 ms) and dashes in two (160 and 200): with the rule assumed,
  the radio cannot read a single letter of it.
- With the button **on**, the radio stops assuming and **measures** how that
  station keys — where the short ones fall, where the long ones fall — and puts
  the boundary between them. That recording then yields a clean `VVV DE 4XZ`.
- With the button **off** nothing changes at all — verified letter for letter
  against the previous version. And it **always starts off**: measuring instead
  of assuming costs accuracy on ordinary Morse, so this is a tool for one
  awkward station, not a setting to leave on.
- It still stays silent on noise while on. That is the most heavily measured
  part: one minute of white noise, zero characters.

---

## V3.96 · 2026-10-08

### Qué cambia

- **Los STANAG 4285 ya sacan texto.** Había dos fallos encadenados, y los dos
  había que arreglarlos para que saliera una sola letra.
- **El primero, el gordo: la fase de byte.** El decodificador suelta bits
  seguidos, sin principio, y el demodulador los partía en bytes de ocho
  empezando por donde cayera el primero. Eso es acertar **una de ocho**. La
  señal se demodulaba perfecta y el texto salía ilegible siete veces de cada
  ocho. Ahora la fase sale de las marcas SOM/EOM de la norma cuando están, y
  cuando no están —que es lo normal si sintonizas con el mensaje ya empezado—
  se encuentra contando caracteres imprimibles en las ocho fases.
- **El segundo: la velocidad y el entrelazado no van en la señal.** Son once
  combinaciones y había que acertarlas a mano con dos botones. El botón de
  velocidad tiene ahora una posición más, **AUTO**, que las recorre sola y se
  queda con la que hace bajar el error de recodificación. Mientras busca, el
  estado dice `prueba 7/11`; cuando acierta, el botón pasa de `AUTO` a `A300`.
- Juntando las dos cosas eran **88 maneras de mirar un 4285 de las que una
  daba texto**. Por eso no salía nada en claro en ninguna frecuencia.
- Peor caso medido de la búsqueda: **24,6 segundos**, y casi todo es el
  relleno del entrelazado largo, que son diez segundos de física.

### What's new

- **STANAG 4285 actually prints text now.** Two faults, chained; both had to go
  before a single letter could come out.
- **The big one: byte phase.** The decoder emits a continuous bit stream with
  no start, and the demodulator cut it into bytes beginning wherever the first
  bit happened to land — a **one-in-eight** guess. The waveform demodulated
  perfectly and the text came out unreadable seven times out of eight. The
  phase now comes from the standard's SOM/EOM markers when present, and when
  they are not — the normal case if you tune in mid-message — from counting
  printable characters across all eight phases.
- **The second: speed and interleave are not carried in the signal.** Eleven
  combinations, previously set by hand on two buttons. The speed button now has
  one more position, **AUTO**, which walks them and keeps the one that drops
  the re-encoding error. While hunting the status reads `trying 7/11`; on a
  hit the button changes from `AUTO` to `A300`.
- Together that was **88 ways to look at a 4285, one of which produced text**.
  That is why nothing ever came out in the clear.
- Worst case measured for the hunt: **24.6 seconds**, nearly all of it the long
  interleaver fill, which is ten seconds of physics.

---

## V3.95 · 2026-10-08

### Qué cambia

- **Arreglado de verdad: la radio volvía lentos los botones.** Había que
  pulsar el botón de modo varias veces para que reaccionara. La causa era el
  **corrector ancho de I/Q** que metí el 7 de octubre dentro de la interrupción
  del ADC, por donde pasan todas las muestras de todos los modos.
- **El corrector estrecho se queda** — es el que arregla el espejo de verdad,
  de 25 a 86 dB de rechazo— y **el ancho sale**: cuesta nueve veces y media
  más y sólo añade algo en los bordes de la banda. Pagar eso dentro de una
  interrupción, y perder pulsaciones, es un mal cambio.
- Y ahora hay un banco que mide lo que cuesta esa interrupción y falla si
  alguien vuelve a meterle trabajo.

### What's new

- **Properly fixed: the radio had gone sluggish on the buttons.** The mode
  button needed several presses to react. The cause was the **wideband I/Q
  corrector** I put inside the ADC interrupt on 7 October — the one place every
  sample of every mode passes through.
- **The narrowband corrector stays** — it is the one that actually fixes the
  mirror, 25 to 86 dB of rejection — and **the wideband one goes**: it costs
  nine and a half times more and only adds anything at the band edges. Paying
  that inside an interrupt, and dropping button presses, is a bad trade.
- And there is now a bench that measures what that interrupt costs and fails
  if anyone puts work back into it.

---

## V3.93 · 2026-10-08

### Qué cambia

- **El panel de STANAG se repinta cuando cambia algo, no cuando pasa el
  tiempo.** La V3.92 bajó los repintados de 14,7 a 4,0 por segundo y no bastó:
  esos cuatro se dibujaban igual aunque en la pantalla no cambiara ni un
  píxel. Ahora son **0,4 por segundo** en reposo, e inmediatos cuando hay algo
  nuevo que leer.
- **Y el modo enseña lo que tarda la vuelta del bucle**, ahora y lo peor desde
  que se encendió: `bucle 45/12 ms peor`. También en la fila de Información.
  Si vuelve a ir lento, el número dice si se lo come un decodificador o el
  dibujo, en vez de haber que adivinarlo.

### What's new

- **The STANAG panel repaints when something changes, not on a timer.** V3.92
  cut repaints from 14.7 to 4.0 per second and that was not enough: those four
  were drawn even when not a single pixel changed. Now it is **0.4 per second**
  at rest, and immediate whenever there is something new to read.
- **And the mode shows how long the main loop takes**, now and the worst since
  power-on: `loop 45/12 ms worst`. Also on the Information row. If it goes slow
  again, the number says whether a decoder or the drawing is eating it, instead
  of having to guess.

---

## V3.92 · 2026-10-07

### Qué cambia

- **Arreglado: el modo STANAG dejaba la radio sin responder a los botones.**
  No estaba colgada: el panel se repintaba **dieciséis veces por segundo** y
  el bucle principal iba tan lento que se perdía las pulsaciones. Ahora son
  cuatro, más una cuando de verdad aparece algo nuevo.
- **Y por eso iba a peor con el tiempo:** el panel del identificador acumula
  renglones, así que cada repintado dibujaba más texto que el anterior.
- **El reductor de ruido burbujea la mitad.** La ganancia de cada bin ahora se
  suaviza en el tiempo —ataque rápido para no comerse el arranque de una
  sílaba, caída lenta para que una ráfaga de ruido no suene como un pitido—.
  Medido: el vaivén que añade pasa de +1,40 a +0,69 dB y **quita medio
  decibelio MÁS** de ruido. Ninguna medida peor.

### What's new

- **Fixed: STANAG mode left the radio not responding to buttons.** It was not
  frozen: the panel was repainting **sixteen times a second** and the main loop
  ran so slowly that button presses fell through the gaps. Now it is four, plus
  one whenever something genuinely new appears.
- **And that is why it got worse over time:** the identifier's panel
  accumulates rows, so each repaint was drawing more text than the last.
- **The noise reducer bubbles half as much.** Each bin's gain is now smoothed
  in time — fast attack so no syllable onset is lost, slow release so a
  one-frame noise burst does not come out as a whistle. Measured: the added
  flutter drops from +1.40 to +0.69 dB and it removes **half a decibel MORE**
  noise. No measurement got worse.

---

## V3.91 · 2026-10-07

### Qué cambia

- **Arreglado: la paleta de la cascada no se guardaba.** Si ponías el tema
  «Oliva» y le cambiabas la paleta a «Turbo», al reiniciar volvía «Humo»
  aunque en `CONFIG.CSV` pusiera `TURBO`. **La configuración no se estaba
  ignorando: se leía bien y el tema la pisaba justo después.**
- Cada tema propone una paleta de cascada, y desde que existen los temas
  claros esa propuesta se aplicaba al arrancar *después* de leer el fichero.
  Ahora la paleta guardada se aplica la última. **El tema propone; lo que
  guardaste manda.**
- Y para que no vuelva, `make comprueba` vigila ese orden: el fallo entró
  porque un comentario que decía «aquí no hay dependencia de orden» era cierto
  el día que se escribió y dejó de serlo dos semanas después.

### What's new

- **Fixed: the waterfall palette was not being saved.** Pick the "Olive" theme,
  change its palette to "Turbo", restart, and "Smoke" came back even though
  `CONFIG.CSV` said `TURBO`. **The config was not being ignored: it was read
  correctly and the theme overwrote it a moment later.**
- Every theme proposes a waterfall palette, and ever since the light themes
  existed that proposal was applied at boot *after* the file was read. Now the
  saved palette is applied last. **The theme proposes; what you saved wins.**
- And so it cannot come back, `make comprueba` now watches that ordering: the
  bug got in because a comment saying "there is no ordering hazard" was true
  the day it was written and stopped being true two weeks later.

---

## V3.90 · 2026-10-07

### Qué cambia

- **Arreglado: el modo STANAG colgaba la radio.** Era el 4415 que entregué en
  la V3.89. Dos fallos míos, los dos de coste y ninguno de acierto, que es
  justo lo que un banco que solo mira si acierta no ve.
- **La interrupción de audio volvía a hacer trabajo pesado.** Hacía dos
  llamadas a la librería de matemáticas y un filtro de 41 coeficientes **por
  muestra** — veinticuatro mil llamadas por segundo dentro de la interrupción.
  Ahora solo copia, y moler se hace en el bucle principal, que es la promesa
  que el 4285 ya tenía escrita y yo rompí. Además 1800 Hz a 12 kHz son
  exactamente 3/20: la fase se repite cada veinte muestras y eso es una tabla,
  no trigonometría.
- **Y el panel se repintaba entero setenta y cinco veces por segundo**, porque
  el módulo decía "hay noticia" en cada dibit. Ahora solo cuando hay un
  segmento de preámbulo leído o un bloque decodificado.
- **El botón Borrar ya no tira el enganche**, y el panel enseña cuántas
  muestras se ha tenido que tirar si el bucle principal va tarde.

### What's new

- **Fixed: STANAG mode froze the radio.** It was the 4415 shipped in V3.89. Two
  mistakes of mine, both about cost and neither about correctness — exactly
  what a bench that only checks correctness cannot see.
- **The audio interrupt was doing heavy work again.** Two maths-library calls
  and a 41-tap filter **per sample** — twenty-four thousand calls per second
  inside the interrupt. Now it only copies and the grinding happens in the main
  loop, which is the promise the 4285 already had written down and I broke.
  Also, 1800 Hz at 12 kHz is exactly 3/20: the phase repeats every twenty
  samples, so that is a table, not trigonometry.
- **And the panel repainted in full seventy-five times a second**, because the
  module reported "news" on every dibit. Now only on a preamble segment read or
  a block decoded.
- **The Clear button no longer drops the lock**, and the panel reports how many
  samples had to be dropped if the main loop runs late.

---

## V3.89 · 2026-10-07

### Qué cambia

- **STANAG 4415 / MIL-STD-188-110A a 75 bps**, una posición más en el botón de
  forma de onda del modo STANAG. Es el modo lento de 110A: cada dibit se
  ensancha en 32 símbolos, 15 dB de ganancia de proceso, y pasa por donde no
  pasa ningún otro.
- **Lee la velocidad y el entrelazado de la propia señal.** El preámbulo los
  manda en D1 y D2, así que aquí no hay que ponerlos a mano como en el 4285:
  sobre tu grabación salen 23 segmentos, la cuenta atrás entera y
  `D1=5 D2=5 → 75 bps, entrelazado largo`.
- **Saca texto**: desensanchado, desentrelazado 20×36, Viterbi y caracteres,
  con ASCII y HEX en el botón de formato. Sobre tu grabación aparece el
  **EOM 4B65A5B2** del estándar, que es la prueba de que la cadena entera está
  bien puesta — y detrás de él, el vaciado, con el que la emisión se da por
  terminada en vez de seguir masticando ruido.

### What's new

- **STANAG 4415 / MIL-STD-188-110A at 75 bps**, one more position on the STANAG
  mode's waveform button. This is 110A's slow mode: every dibit is spread over
  32 symbols, 15 dB of processing gain, and it gets through where nothing else
  does.
- **It reads the rate and the interleave off the signal itself.** The preamble
  carries them in D1 and D2, so unlike the 4285 there is nothing to set by
  hand: on your recording it reports 23 segments, the full countdown and
  `D1=5 D2=5 → 75 bps, long interleave`.
- **It produces text**: despreading, 20×36 deinterleaving, Viterbi and
  characters, with ASCII and HEX on the format button. Your recording yields
  the standard's **4B65A5B2 EOM**, which is the proof that the whole chain is
  right — and behind it the flush, at which point the transmission is declared
  over instead of chewing on noise.

---

## V3.88 · 2026-10-07

### Qué cambia

- **Fila nueva en Información: «Armónicos».** Dice qué bandas conocidas caen
  encima de los armónicos del oscilador local, del 2.º al 7.º. Con el dial en
  3.411 kHz pone `x3 30 m -30k`: el FT8 de 30 metros entrando por el tercer
  armónico, que es lo que estabas oyendo donde no hay nada.
- **Hace falta porque no se distingue mirando.** Una señal recibida por el
  tercer armónico y una imagen I/Q **se mueven exactamente igual** al girar el
  dial: las dos al revés y al doble de velocidad. Esta fila lo contesta en un
  segundo.
- **Página nueva en Información**, «La radio sobre sí misma», porque la de
  Pantalla se llenó. No es una decisión: saltó el `_Static_assert` que vigila
  justo eso.

### What's new

- **New Information row: "Harmonics".** It says which known bands fall on the
  local oscillator's harmonics, from the 2nd to the 7th. With the dial at
  3,411 kHz it reads `x3 30 m -30k`: 30-metre FT8 coming in through the third
  harmonic, which is what you were hearing where there is nothing.
- **It is needed because you cannot tell by looking.** A signal received on
  the third harmonic and an I/Q image **move exactly the same way** as you
  tune: both backwards and at twice the speed. This row answers it in a
  second.
- **New Information page**, "The radio about itself", because the Display one
  filled up. Not a decision: the `_Static_assert` that watches for exactly
  that went off.

---

## V3.87 · 2026-10-07

### Qué cambia

- **Corrector de espejo de banda ancha.** El de la V3.84 corrige con dos
  números —una ganancia y una fase— para los 96 kHz enteros, y eso sólo vale
  si lo único que separa a I de Q es un ángulo. Si entre las dos ramas hay un
  retardo o filtros que no casan, el desequilibrio es **distinto en cada
  frecuencia** y una constante no puede con él. Éste corrige con un filtro de
  seis coeficientes en vez de con un número. **Medido en banco, con medio
  microsegundo de diferencia entre las ramas: en el borde de la banda, el
  plano se queda en 5,8 dB de rechazo y éste llega a 24,5. Y no empeora al
  plano en ninguna frecuencia.**
- **La prueba A/B apaga y enciende los dos**, así que sigue contestando la
  misma pregunta: si el número sube al corregir, era espejo.

### What's new

- **Wideband mirror corrector.** The one from V3.84 corrects with two numbers
  — one gain and one phase — for the whole 96 kHz, which only works if the
  only thing separating I from Q is an angle. If the two branches differ by a
  delay or by filters that do not match, the imbalance is **different at every
  frequency** and a constant cannot fix it. This one corrects with a six-tap
  filter instead of a number. **Measured on the bench, with half a microsecond
  of difference between branches: at the band edge the flat corrector reaches
  5.8 dB of rejection and this one 24.5. And it is never worse than the flat
  one at any frequency.**
- **The A/B test now toggles both**, so it still answers the same question: if
  the figure rises when correcting, it was a mirror.

---

## V3.86 · 2026-10-07

### Qué cambia

- **La fila «Espejo I/Q» ahora enseña dos rechazos, no uno**, con el formato
  `global/espectro dB  fase`. El primero sale de promediar los 96 kHz enteros;
  el segundo, de comparar la señal que hay ahora mismo en el panadaptador con
  su bin espejo. Si no hay ninguna señal clara pone `--` en vez de inventarse
  un número.
- **La prueba la hace la radio: un toque en la fila y tres segundos.** Apaga
  el corrector, mide, lo enciende, mide, lo deja como estaba y enseña los dos
  números juntos: `A/B 14→38 dB`. Si el número sube al corregir, era espejo de
  verdad; si no se mueve, en ese bin hay una emisora y lo que se estaba
  mirando no era un reflejo. Antes había que tocar, apuntar, tocar y apuntar,
  acordándose de cuál era cuál.
- **Y comparar los dos es lo que da el diagnóstico.** Si el global sale alto
  —la cuadratura promedio está bien— pero el del espectro sale bajo —en la
  pantalla hay espejo—, entonces el desequilibrio **depende de la frecuencia**
  y un solo coeficiente no puede con él. Si los dos salen altos, lo que se
  está mirando no es un espejo y hay que buscar en otro sitio. Sale de que, al
  tocar la fila con un espejo en pantalla, no cambiaba nada: la fila marcaba
  0,1° de desfase, y con eso el corrector no tenía nada que corregir.

### What's new

- **The "I/Q mirror" row now shows two rejection figures, not one**, formatted
  `global/spectrum dB  phase`. The first comes from averaging the whole 96 kHz
  band; the second from comparing the signal currently on the panadapter with
  its mirror bin. With no clear signal it shows `--` rather than inventing a
  number.
- **The radio runs the test itself: one tap on the row and three seconds.**
  It turns the corrector off, measures, turns it on, measures, puts it back as
  it was and shows both figures together: `A/B 14→38 dB`. If the number rises
  when correcting, it really was a mirror; if it does not move, that bin holds
  a real station and what you were looking at was not a reflection. Before,
  this meant tap, write down, tap, write down, and remember which was which.
- **Comparing the two is what gives the diagnosis.** If the global figure is
  high — average quadrature is fine — but the spectrum one is low — there is a
  mirror on screen — then the imbalance **depends on frequency** and a single
  coefficient cannot fix it. If both are high, what you are looking at is not
  a mirror. This came from tapping the row with a mirror on screen and nothing
  changing: the row read 0,1° of phase error, and with that the corrector had
  nothing to correct.

---

## V3.85 · 2026-10-07

### Qué cambia

- **Once kilobytes y medio de RAM libres.** La cola de líneas de SSTV se
  reservaba siempre, estuvieras en SSTV o no, y la radio sólo está en un modo
  a la vez. Ahora se pide prestada al arrancar el modo y se devuelve al salir,
  como ya hacían AIS, ALE, DSC, Feld-Hell, HFDL, IDENT, JTTY, STANAG y WSPR.
  **Medido: de 937 bytes libres a 12.477.** SSTV funciona igual; si alguna vez
  no hubiera memoria, el modo no arranca y lo dice, en vez de pintar donde no
  debe.
- **Corrección de lo que decía la V3.84.** Decía que se recuperaban quince
  kilobytes quitando un controlador de Ethernet del build. **Era falso:** el
  enlazador ya lo descartaba entero, y compilar con él y sin él da exactamente
  el mismo tamaño. Los 28 bytes que hacían falta salieron de otro sitio.
  Queda dicho aquí porque una cifra inventada en una lista de novedades es
  peor que no poner nada.

### What's new

- **Eleven and a half kilobytes of RAM freed.** The SSTV line queue was
  allocated permanently whether you were in SSTV or not, and the radio is only
  ever in one mode at a time. It is now borrowed when the mode starts and
  returned when it ends, as AIS, ALE, DSC, Feld-Hell, HFDL, IDENT, JTTY,
  STANAG and WSPR already did. **Measured: from 937 bytes free to 12,477.**
  SSTV behaves identically; if the memory were ever unavailable the mode does
  not start and says so, rather than drawing where it should not.
- **Correction to what V3.84 claimed.** It said fifteen kilobytes of RAM were
  recovered by dropping an Ethernet driver from the build. **That was false:**
  the linker was already discarding it whole, and building with and without it
  gives exactly the same size. The 28 bytes that were needed came from
  somewhere else. It is stated here because an invented figure in a release
  note is worse than no figure at all.

---

## V3.84 · 2026-10-07

### Qué cambia

- **El espejo de las señales, corregido.** Girando el encoder hacia abajo todas
  las señales avanzan de izquierda a derecha; si una va al revés, es el reflejo
  de otra, y aparece porque las dos ramas I y Q no están exactamente a 90° ni
  tienen la misma ganancia. La radio ahora lo mide sola sobre las propias
  muestras y lo corrige antes de que el bloque llegue a nadie, así que se nota
  a la vez en el espectro, en la cascada y en los veintidós modos digitales.
  **Medido en banco: de 25 dB de rechazo de imagen a 86 dB.**
- **Fila nueva en Información: "Espejo I/Q".** Enseña el rechazo de imagen que
  se está midiendo ahora mismo y los grados de desfase de los que viene. Y
  tocándola se apaga y se enciende el corrector, que es la forma de comprobar
  en antena si un espejo concreto es de esto o viene de más adelante.
- **La marquesina ya no se sale en las capturas.** El nombre de la emisora se
  recortaba sólo por arriba y por abajo al montar la captura, nunca por los
  lados, así que un nombre largo pisaba el reloj y los voltios. En la pantalla
  de verdad no pasaba. El banco que compara captura y pantalla usaba un nombre
  que cabía; ahora usa uno de los que lo destapó.
- **Ecualizador adaptativo**, la pieza que faltaba para demodular el STANAG
  4539 y el 4415: a 2400 baudios en HF cada símbolo llega sumado a los dos o
  tres anteriores y la constelación es una nube. Va a ciegas primero y por
  decisión después. Todavía no está enganchado a ningún modo.

### What's new

- **Mirror images are now corrected.** Turning the encoder down, every real
  signal drifts left to right; one that drifts the other way is the reflection
  of another, and it appears because the I and Q branches are not exactly 90°
  apart and do not have the same gain. The radio now measures that from the
  samples themselves and corrects it before the block reaches anything, so it
  shows up at once in the spectrum, the waterfall and all twenty-two digital
  modes. **Measured on the bench: image rejection from 25 dB to 86 dB.**
- **New Information row: "I/Q mirror".** It shows the image rejection being
  measured right now and the phase error behind it. Tapping it turns the
  corrector off and on, which is how to tell on air whether a given mirror
  comes from this or from further up the chain.
- **The marquee no longer overflows in screenshots.** The station name was
  clipped only top and bottom when composing a capture, never at the sides, so
  a long name ran over the clock and the battery voltage. It never happened on
  the live screen. The bench that compares capture against screen used a name
  that fitted; it now uses one of the names that exposed this.
- **Adaptive equaliser**, the missing piece for demodulating STANAG 4539 and
  4415: at 2400 baud on HF each symbol arrives on top of the two or three
  before it and the constellation is a cloud. Blind first, decision-directed
  afterwards. Not wired to any mode yet.

---

## V3.83 · 2026-10-07

### Qué cambia

- **Modo nuevo: DSC marítimo** (`DSC`, familia Digital, USB). Es la llamada
  selectiva del GMDSS: dice quién llama, a quién, con qué urgencia y en qué
  frecuencia seguir. Enseña el MMSI de origen y destino, la categoría y el fin
  de secuencia, con su comprobación de errores. El formato está leído de la
  ITU-R M.493-16 y **comprobado contra cuatro grabaciones reales**: tres
  decodifican enteras con la comprobación correcta.
- **Encuentra los tonos solo.** El DSC no cae en un sitio fijo del audio: en
  las grabaciones de prueba los centros estaban en 995, 1700 y 2214 Hz. Los
  busca en el analizador de sintonía, por parejas separadas 170 Hz.
- **El formato de socorro NO está probado en antena**, y no lo estará mientras
  no haya una grabación de una alerta real. Está construido desde el estándar y
  respaldado sólo por el banco. Queda dicho aquí a propósito.

### What's new

- **New mode: maritime DSC** (`DSC`, Digital family, USB). This is GMDSS
  digital selective calling: who is calling, whom, how urgently, and on which
  frequency to continue. It shows the source and destination MMSI, the
  category and the end-of-sequence, with its error check. The format is read
  from ITU-R M.493-16 and **verified against four real recordings**: three
  decode in full with the check passing.
- **It finds the tones by itself.** DSC does not sit at a fixed audio
  frequency: in the test recordings the centres were at 995, 1700 and 2214 Hz.
  It looks for them in the tuning analyser, as pairs 170 Hz apart.
- **The distress format is NOT tested on air**, and will not be until a
  recording of a real alert exists. It is built from the standard and backed
  only by the bench. That is stated here deliberately.

---

## V3.82 · 2026-10-07

### Qué cambia

- **Repaso a fondo de todo el código.** Cuatro pasadas: avisos del compilador
  con un juego mucho más estricto (38 avisos, ninguno un fallo de verdad),
  análisis estático, los bancos de pruebas y los números escritos dos veces.
- **Cinco fallos reales, arreglados.** Cuatro sitios leían un array antes de
  comprobar si el índice cabía; en tres daba igual por un byte de margen, pero
  en el locutor de WSPR se leía de verdad fuera. Y en el espectro había una
  comprobación de puntero nulo escrita **detrás** de la operación que debía
  proteger.
- **Veintitrés bancos de pruebas existían y no se ejecutaban nunca.** Dos ni
  siquiera compilaban — uno de ellos el de Feld-Hell, roto desde V3.75 sin que
  nadie pudiera enterarse. Ocho quedan enganchados a la comprobación
  automática. Un banco que no corre no puede fallar.
- **La tasa del camino de banda lateral vive ahora en un solo sitio.** Estaba
  escrita a mano en una docena, y es la misma forma de avería que costó la
  tabla de zonas del oscilador la noche anterior: dos números que tienen que
  ser el mismo y nada que los compare.

### What's new

- **A deep pass over the whole source.** Four sweeps: compiler warnings with a
  much stricter set (38 warnings, none a real defect), static analysis, the
  test benches, and numbers written down twice.
- **Five real bugs, fixed.** Four places read an array before checking the
  index was in range; three got away with it thanks to a spare byte, but the
  WSPR locator genuinely read past the end. And in the spectrum a null-pointer
  check was written *after* the operation it was meant to guard.
- **Twenty-three test benches existed and never ran.** Two did not even
  compile — one of them the Feld-Hell bench, broken since V3.75 with no way for
  anyone to notice. Eight are now wired into the automatic check. A bench that
  does not run cannot fail.
- **The sideband path's sample rate now lives in one place.** It was written out
  by hand in a dozen, and that is the same shape of fault that cost the
  oscillator's zone table the night before: two numbers that must agree, with
  nothing comparing them.

---

## V3.81 · 2026-10-07

### Qué cambia

- **Modo nuevo: paquete de HF a 300 baudios** (`PKT300`, en la familia Digital).
  Es el mismo AX.25 del APRS por el otro camino: 300 baudios con 200 Hz de
  desplazamiento por banda lateral, en vez de 1200 y Bell 202 por FM. Va en LSB
  por convenio del dial; al decodificador la banda lateral le da igual. Cuesta
  unos 2,5 dB de sensibilidad frente al de VHF, porque a esa velocidad los dos
  tonos se pisan — y está medido, no supuesto.
- **El autoenganche de CW cubre ahora ±500 Hz** en vez de ±350. La lista decía
  ±250 y el comentario del código también: los dos eran falsos, y solo se vio
  al medirlo. Trece sondas en vez de nueve.
- **Y cabían gratis**: el cálculo del detector de tono guardaba dos tablas de
  senos y cosenos que no hacían ninguna falta — hay una identidad exacta que da
  el mismo número sin ellas. 156 bytes de RAM liberados y dos multiplicaciones
  menos por sonda.
- **La señal débil de CW sigue sin salir, pero ya se sabe por qué.** El detector
  de tecla funciona bien hasta 6 dB: saca los mismos elementos, sin reparaciones
  y con la envolvente sana. Lo que se degrada son sus longitudes. No es ningún
  umbral —se han descartado cinco, uno a uno, midiendo— sino la medida del borde
  del elemento. Está escrito con los números en `CAMBIOS.md`.

### What's new

- **New mode: 300 Bd HF packet** (`PKT300`, in the Digital family). It is the
  same AX.25 as APRS down the other path: 300 baud with a 200 Hz shift over
  sideband, instead of 1200 baud Bell 202 over FM. It sits on LSB by dial
  convention; the decoder itself does not care which sideband. It costs about
  2.5 dB of sensitivity against the VHF one, because at that rate the two tones
  overlap — measured, not assumed.
- **CW auto-lock now covers ±500 Hz** instead of ±350. The to-do list said ±250
  and so did the comment in the code; both were wrong, and it only showed up on
  measuring. Thirteen probes instead of nine.
- **And they fit for free**: the tone detector kept two tables of sines and
  cosines that were never needed — an exact identity gives the same number
  without them. 156 bytes of RAM freed and two multiplies fewer per probe.
- **Weak-signal CW still does not come out, but now we know why.** The key
  detector works fine down to 6 dB: same element count, no repairs, healthy
  envelope. What degrades is the element *lengths*. It is not any threshold —
  five were ruled out one by one, by measurement — but the measurement of the
  element's edge. Written up with the numbers in `CAMBIOS.md`.

---

## V3.80 · 2026-10-07

### Qué cambia

- **Un cambio que se queda sin hacer, a propósito.** Había propuesto bajar el
  techo del oscilador interno porque la hoja del fabricante del chip original
  marca un límite más bajo. Pero esta radio no lleva ese chip, lleva un clon, y
  la captura de la radio de referencia corre **esta misma placa** por encima de
  ese límite y con la cuadratura funcionando. Una hoja de otra pieza no manda
  sobre una medida propia, así que la tabla se queda como está y la hipótesis
  se cae. Lo que sí queda escrito es de dónde sale cada límite: el de arriba
  está medido aquí, el de abajo viene de la hoja y **nadie lo ha comprobado**.
- **El nombre de la otra radio desaparece del código**: quedaban tres
  menciones en comentarios sobre ideas de interfaz, y ahora dicen "la radio
  comercial de referencia".

### What's new

- **A change deliberately left unmade.** The internal oscillator's ceiling was
  about to be lowered because the original chip's datasheet sets a lower limit.
  But this radio does not use that chip, it uses a clone, and the reference
  radio's capture runs **this very board** above that limit with quadrature
  working. A datasheet for a different part does not override a measurement
  taken here, so the table stays and the hypothesis dies. What does get written
  down is where each limit comes from: the upper one is measured here, the
  lower one comes from the datasheet and **nobody has verified it**.
- **The other radio's name is gone from the source**: three comments about
  interface ideas now say "the commercial reference radio".

---

## V3.79 · 2026-10-07

### Qué cambia

- **De 300 a 401 kHz ya se sintoniza bien.** V3.78 se limitaba a negarse ahí
  porque la tabla de zonas del oscilador pedía un divisor más grande del que el
  chip tiene. Parecía que hacía falta portar el divisor R, que nunca se portó,
  pero no: el problema era que la tabla estaba derivada desde 100 kHz, y por
  debajo de 300 kHz este chip **no se usa** — manda el generador del GD32. Las
  tres primeras zonas eran código muerto y su único efecto era empujar a la
  cuarta a un divisor imposible. Rehecha desde los 300 kHz salen siete zonas y
  todas caben de sobra.
- **Y ahora hay una red.** Nada comprobaba que un divisor cupiera: por eso
  cuatro de las diez zonas no cabían y nadie se enteró. Un número que no cabe
  ya no compila, y el punto donde se relevan los dos generadores está atado
  para que no puedan volver a separarse.
- Esto **no cambia nada** de las imágenes por debajo de 4,8 MHz: las zonas
  nuevas son igual de anchas que las viejas, porque ese ancho lo fija el VCO.
  Eso sigue pendiente de lo que diga la prueba de "Cuadratura I/Q".

### What's new

- **300 to 401 kHz now tunes correctly.** V3.78 simply refused there, because
  the oscillator's zone table asked for a divider larger than the chip has. It
  looked like the R divider — never ported — was needed, but it wasn't: the
  table had been derived from 100 kHz, and below 300 kHz this chip **is not
  used** at all; the GD32 timer drives the mixer there. The first three zones
  were dead code whose only effect was pushing the fourth into an impossible
  divider. Rebuilt from 300 kHz it comes out as seven zones, all comfortably
  within range.
- **And there is now a safety net.** Nothing checked that a divider fit, which
  is why four of the ten did not and nobody noticed. A number that does not fit
  no longer compiles, and the handover point between the two generators is tied
  down so the two cannot drift apart again.
- This changes **nothing** about the images below 4.8 MHz: the new zones are
  exactly as wide as the old ones, because that width is set by the VCO. That
  still waits on what the "I/Q quadrature" test says.

---

## V3.78 · 2026-10-06

### Qué cambia

- **Fila nueva en Información → medidas: "Cuadratura I/Q".** Enseña de dónde
  sale el par en cuadratura que alimenta al mezclador y, si es del truco de
  banda baja, en qué zona y con qué fVCO. **Al tocarla rehace el enganche de
  los 90° sin mover el oscilador ni un hercio.** Es para las imágenes que
  aparecen por debajo de 4,8 MHz: con una emisora y su espejo en pantalla, se
  toca varias veces y se mira. Si la imagen baila entre toques, el enganche
  sale mal a veces; si no se inmuta, es que se pierde según el oscilador se
  aleja de donde se hizo. Son dos averías distintas con arreglos opuestos.
- **De 300 a 401 kHz la radio ya no miente.** Esas zonas pedían un divisor de
  salida más grande del que el chip tiene, y los bits de arriba se tiraban en
  silencio: se sintonizaba una frecuencia que no era la que ponía en pantalla.
  Ahora se niega y lo dice. El arreglo de verdad necesita el divisor R, que no
  se portó en su día y que no es una línea.

### What's new

- **New row in Information → measurements: "I/Q quadrature".** It shows where
  the quadrature pair feeding the mixer comes from and, for the low-band
  trick, which zone and what VCO frequency. **Tapping it redoes the 90° latch
  without moving the oscillator by a single hertz.** It is aimed at the images
  that appear below 4.8 MHz: with a station and its mirror on screen, tap a
  few times and watch. If the image jumps between taps, the latch is
  unreliable; if it does not move at all, the latch is lost as the oscillator
  drifts away from where it was made. Two different faults with opposite
  fixes.
- **From 300 to 401 kHz the radio no longer lies.** Those zones asked for an
  output divider larger than the chip has, and the top bits were silently
  dropped: it tuned a frequency other than the one on screen. It now refuses
  and says so. The real fix needs the R divider, which was never ported and is
  not a one-liner.

---

## V3.77 · 2026-10-06

### Qué cambia

- **El CW ya no parte los elementos.** Un bache de ruido en mitad de una raya
  la rompía en dos marcas de media raya, que salían como dos puntos: donde
  ponía `T` aparecía `I`, y la velocidad estimada se iba al doble —25 PPM sobre
  una señal de 12, que es lo que marcaba la pantalla—. Ahora un tramo demasiado
  corto para ser un elemento se **fusiona** con sus dos vecinos en vez de
  tirarse, y la regla vale igual para las marcas que para los huecos, que antes
  no la tenían nunca.
- **Y el ruido deja de parecer código.** Fusionar, por sí solo, hace que el
  ruido blanco también acabe teniendo forma de Morse. La regla que lo impide no
  es un umbral: un elemento que ha habido que reparar no cuenta como prueba.
  Un minuto de ruido pasa de 41 caracteres inventados a **cero**, y la FSK de
  8.565 kHz de 9 a cero, sin tocar la grabación de SVO, que sigue saliendo
  entera.
- **Banco nuevo** que mide justo esto: agujeros metidos a mano dentro de los
  elementos. Faltaba en el banco entero, y es el caso que se tenía delante. El
  error baja de 39 % a 9 %.
- **Lo que empeora:** una grabación concreta (IZ3WUW/P) da peor sus dos
  primeras letras. Está contado en `CAMBIOS.md` sin maquillar, con la variante
  que lo recuperaba y por qué no se usa.

### What's new

- **CW no longer breaks elements apart.** A noise dropout in the middle of a
  dash used to split it into two half-dashes that came out as two dots — a `T`
  printed as `I` — and the speed estimate doubled, reading 25 WPM on a 12 WPM
  signal, exactly what the screen showed. A run too short to be an element is
  now **merged** into its neighbours instead of being discarded, and the rule
  applies to gaps as well as marks, which never had it.
- **And noise stops looking like code.** Merging on its own makes white noise
  acquire Morse shape too. What prevents it is not a threshold: an element that
  had to be repaired does not count as evidence. A minute of noise goes from 41
  invented characters to **zero**, and the 8.565 kHz FSK from 9 to zero,
  without touching the SVO recording, which still decodes in full.
- **A new bench** measuring precisely this: holes punched inside elements. It
  was missing from the whole suite, and it is the case that was on screen. The
  error drops from 39 % to 9 %.
- **What gets worse:** one recording (IZ3WUW/P) returns its first two letters
  less well. It is written up in `CAMBIOS.md` without varnish, including the
  variant that recovered it and why it is not used.

---

## V3.76 · 2026-10-06

### Qué cambia

- **El CW deja de escribir ruido entre transmisiones.** La puerta de calidad
  abría exigiendo ocho elementos seguidos que encajan —bien pensado, es lo que
  el ruido no sabe hacer— pero **cerraba** sólo si el error medio subía de
  0,48, y esa media baja muy despacio. Resultado: enganchaba con una
  transmisión de verdad y se quedaba abierta durante los minutos siguientes
  escribiendo las letras de un solo punto o una sola raya (T, E, I), que es
  justo lo que el ruido fabrica. Ahora el cierre es **simétrico**: ocho
  elementos seguidos que no encajan y se cierra. Mismo número y mismo
  argumento que la apertura, y sin mirar el nivel — que es lo que este
  decodificador descarta con datos desde el principio.

### What's new

- **CW stops writing noise between transmissions.** The quality gate opened by
  demanding eight consecutive elements that fit — sound reasoning, that is what
  noise cannot do — but it **closed** only when the average error rose above
  0.48, and that average falls very slowly. So it would lock onto a real
  transmission and stay open for the following minutes, writing the letters
  made of a single dot or dash (T, E, I), which is exactly what noise
  produces. Closing is now **symmetric**: eight consecutive elements that do
  not fit and it shuts. Same number, same argument as the opening, and without
  looking at level — which this decoder rejects with measurements from the
  start.

---

## V3.75 · 2026-10-06

### Qué cambia

- **Modo nuevo: FELD-HELL**, telegrafía por imagen de 1929, en la categoría
  Imágenes. No tiene alfabeto, ni marco de carácter, ni corrección de errores,
  ni sincronismo: llegan puntos encendidos y apagados y las letras las forma el
  ojo. La pantalla pinta **cada columna dos veces**, una encima de otra — eso
  no es decoración, es lo que hace que funcione sin sincronismo: si el ritmo
  del emisor no coincide con el nuestro el texto sube o baja despacio, y con
  dos copias separadas justo una altura de carácter siempre hay una entera. La
  máquina original lo hacía con el paso de la hélice entintada, por el mismo
  motivo. Margen medido: ±1% de error de ritmo.
- **Y un fallo que me comí al partir las familias:** `digi_panel_active()`
  preguntaba «¿es de la familia Digital?» para decidir si el espectro cede su
  sitio. Con las familias nuevas eso habría dejado a **WEFAX, SSTV y HFDL sin
  panel**. Ahora pregunta lo que de verdad quiere saber: «¿no es analógico?».

### What's new

- **New mode: FELD-HELL**, 1929 picture telegraphy, in the Images category. It
  has no alphabet, no character framing, no error correction and no sync: dots
  arrive on and off and the eye forms the letters. The screen paints **each
  column twice**, one above the other — not decoration but the very thing that
  makes it work without sync: if the sender's rate does not match ours the text
  drifts slowly up or down, and with two copies exactly one character-height
  apart there is always a whole one. The original machine did this with the
  pitch of its inked helix, for the same reason. Measured margin: ±1% rate
  error.
- **And a bug I introduced when splitting the families:** `digi_panel_active()`
  asked "is this the Digital family?" to decide whether the spectrum gives up
  its place. With the new families that would have left **WEFAX, SSTV and HFDL
  with no panel**. It now asks what it actually means: "is this not analogue?".

---

## V3.74 · 2026-10-06

### Qué cambia

- **Categoría nueva: Aeronáutica.** De momento con el HFDL dentro. La división
  de esa pantalla es por lo que sale, no por cómo se demodula, y lo que sale
  del HFDL no es texto: es una posición, un vuelo y una matrícula, y la radio
  ya sabe traducir esa matrícula a modelo y operador con los 101.432 aviones de
  la zona alta. Ningún otro modo usa esa base de datos. El ACARS de VHF caerá
  aquí en cuanto se escriba.
- **Y un tope que no avisaba.** La columna de categorías tiene cinco sitios.
  `cfg_fill()` rellena hasta cinco nombres pero luego pasa la cuenta **real**
  al dibujante: con seis, el dibujante leería fuera del array. No es una celda
  invisible como la del modo 17, es salirse de la memoria. Ahora hay tres
  `_Static_assert` —modos, bandas y ajustes— que paran la compilación. Y de
  paso se ve que **ajustes está a 5 de 5**: una página más no cabe.

### What's new

- **New category: Aeronautical.** With HFDL in it for now. That screen is
  divided by what comes out, not by how it demodulates, and what comes out of
  HFDL is not text: it is a position, a flight and a registration — and the
  radio already turns that registration into model and operator using the
  101,432 aircraft in high flash. No other mode uses that database. VHF ACARS
  will land here once written.
- **And a ceiling that gave no warning.** The category column has five slots.
  `cfg_fill()` fills up to five names but then hands the drawer the **real**
  count: with six, the drawer would read past the end of the array. That is not
  an invisible cell like mode 17, it is reading out of bounds. Three
  `_Static_assert`s — modes, bands and settings — now stop the build. They also
  make visible that **settings sits at 5 of 5**: there is no room for another
  page.

---

## V3.73 · 2026-10-06

### Qué cambia

- **La pantalla de modos tiene una categoría nueva: Imágenes.** WEFAX y SSTV se
  mudan ahí. El motivo inmediato es que Digital estaba a **16 de 16** y la
  rejilla densa dibuja exactamente dieciséis: el siguiente modo que alguien
  añadiera habría desaparecido sin que nada avisara. Pero el corte tampoco es
  un apaño — de los dieciséis, catorce entregan texto o una tabla y dos
  entregan una foto, y quien busca «el modo que me saca el mapa» no está
  mirando la misma lista que quien busca el teletipo. Quedan 14 de 16 en
  Digital y 2 de 16 en Imágenes.
- **Y la comprobación que lo vigila.** `modos_check.py` contaba los modos por
  familia pero sólo fallaba si una familia se quedaba vacía: con diecisiete
  habría dicho «17 modos» y pasado en verde. Ahora avisa cuando una familia se
  llena y para la compilación si se pasa. Y saca la lista de familias del
  propio fuente, porque la tenía escrita a mano y no se habría enterado de la
  nueva.

### What's new

- **The mode screen has a new category: Images.** WEFAX and SSTV move there.
  The immediate reason is that Digital sat at **16 of 16** and the dense grid
  draws exactly sixteen: the next mode anyone added would have vanished with
  nothing to warn them. But the split is not a workaround either — of the
  sixteen, fourteen deliver text or a table and two deliver a picture, and
  someone looking for "the mode that gets me the weather chart" is not reading
  the same list as someone looking for teletype. Digital is now 14 of 16,
  Images 2 of 16.
- **And the check that watches it.** `modos_check.py` counted modes per family
  but only failed when a family was empty: with seventeen it would have printed
  "17 modes" and passed green. It now warns when a family fills up and stops
  the build if one overflows. It also reads the family list from the source,
  because it had that list hard-coded and would not have noticed the new one.

---

## V3.72 · 2026-10-06

### Qué cambia

- **Vuelve el rechazo de imagen por debajo de 4,8 MHz.** Desde la V3.5x, por
  debajo de esa frecuencia la imagen —la banda lateral contraria reflejada en
  el oscilador— se colaba como si fuera una emisora. El 27/09 acorté la
  maniobra que pone I y Q a 90 grados, de 62,5 ms a 6,25, subiendo diez veces
  el desplazamiento de frecuencia que la genera. Y eso multiplica por diez el
  error de ángulo, porque los dos extremos de esa ventana no los marca el
  cronómetro sino **dos escrituras I2C**: 1,44 grados por milisegundo de
  incertidumbre pasaban a 14,4, o sea de unos 38 dB de rechazo a unos 18.
  Veinte decibelios. Vuelve a 62,5 ms, que sólo se notan al cruzar una de las
  diez fronteras de zona, no girando el mando.

### What's new

- **Image rejection below 4.8 MHz is back.** Since V3.5x, below that frequency
  the image — the opposite sideband mirrored about the local oscillator — came
  through like a real station. On 27/09 I shortened the manoeuvre that sets I
  and Q 90 degrees apart, from 62.5 ms to 6.25, by raising tenfold the
  frequency offset that generates it. That multiplies the angle error by ten,
  because the two ends of that window are marked not by the timer but by **two
  I2C writes**: 1.44 degrees per millisecond of uncertainty became 14.4, so
  about 38 dB of rejection became about 18. Twenty decibels. It goes back to
  62.5 ms, which is only noticeable when crossing one of the ten zone
  boundaries, not while turning the knob.

---

## V3.71 · 2026-10-06

### Qué cambia

- **IDENT ya no llama «STANAG 4539» a cualquier cosa con trama de 120 ms.** La
  fila nació con un margen del ±1%, y las dos tramas que hay que distinguir se
  separan **tres décimas de porcentaje**: 287 símbolos a 2400 Bd son 119,583 ms
  y 288 son 120,000. El margen se las tragaba las dos. La grabación de
  referencia que ya estaba en el proyecto mide 119,61 ms; una señal de 7.135
  kHz medía 120,02 y la radio la nombraba igual. Ahora el margen es de 119,30 a
  119,80 ms: entra la buena con holgura y la otra se queda fuera. Cuando lo
  único que se mide es un número, ese número tiene que cuadrar de verdad.

### What's new

- **IDENT no longer calls anything with a 120 ms frame "STANAG 4539".** The row
  was born with a ±1% window, and the two frames that must be told apart are
  **three tenths of a percent** away from each other: 287 symbols at 2400 Bd is
  119.583 ms, 288 is 120.000. The window swallowed both. The reference
  recording already in the project measures 119.61 ms; a signal on 7.135 kHz
  measured 120.02 and the radio named it the same. The window is now 119.30 to
  119.80 ms: the real one fits with room to spare and the other falls outside.
  When a single number is all you measure, that number has to actually match.

---

## V3.70 · 2026-10-06

### Qué cambia

- **IDENT reconoce la FSK de 50 Bd y 200 Hz de 14.118 kHz.** Antes se callaba
  ante ella y había que adivinar. Ahora la nombra y dice lo que hay que saber:
  usa el alfabeto del NAVTEX (7 bits, cuatro unos de siete) pero **no es
  SITOR-B**, así que poner la radio en NAVTEX no sirve de nada — su reloj va a
  100 baudios, su desplazamiento a 170, y declara enganche por una repetición
  que esta señal no tiene. Y el contenido va cifrado: los 35 códigos posibles
  salen casi el mismo número de veces (entropía 4,97 de 5,13 máximo; en texto
  llano sería 4,34 con el espacio llevándose el 20%). Eso último no depende de
  acertar con la tabla, porque cambiarla sólo baraja las etiquetas.
  Confirmada sobre dos grabaciones separadas 18 minutos: 199,95 y 200,13 Hz,
  50,00 baudios las dos.
- **Y la firma se reconoce por el número que el medidor acierta.** La primera
  versión pedía la velocidad de dos tonos, y ese medidor da 594 Bd en una
  grabación y 53,2 en la otra para la misma señal. La de símbolo da 50,5 y
  50,4: las dos bien. Así que va por separación de tonos más velocidad de
  símbolo, como la fila de APRS y por el mismo motivo.
- **IDENT ya no dice «ni tonos» cuando hay tonos.** Decía «Sin estructura
  medible» en la cabecera mientras tres renglones más abajo enseñaba «tonos
  709 y 910 Hz, salto 200». Lo que pasaba no era que no hubiera medidas, sino
  que ninguna firma cuadraba con ellas — y para quien mira son dos cosas muy
  distintas. Ahora lo dice así: «No cuadra con ninguna firma / los números
  están medidos, la tabla no los tiene».

### What's new

- **IDENT now recognises the 50 Bd / 200 Hz FSK on 14.118 kHz.** It used to
  say nothing and leave you guessing. It now names it and says what matters:
  it uses NAVTEX's alphabet (7 bits, four marks of seven) but it is **not
  SITOR-B**, so switching the radio to NAVTEX achieves nothing — that decoder
  clocks at 100 baud, expects a 170 Hz shift, and declares lock on a
  repetition this signal does not carry. And the content is encrypted: all 35
  possible codes appear about equally often (entropy 4.97 out of a 5.13
  maximum; plain text would be 4.34 with the space alone taking 20%). That
  last point does not depend on getting the table right, because changing the
  table only shuffles the labels. Confirmed on two recordings 18 minutes
  apart: 199.95 and 200.13 Hz, 50.00 baud both.
- **And the signature keys off the number the meter gets right.** The first
  version asked for the two-tone speed, and that meter reads 594 Bd on one
  recording and 53.2 on the other for the same signal. The symbol-rate meter
  reads 50.5 and 50.4: both right. So the row keys off tone separation plus
  symbol rate, like the APRS row and for the same reason.
- **IDENT no longer says "no tones" when there are tones.** Its header read
  "No measurable structure" while three lines below it displayed "tones 709
  and 910 Hz, shift 200". The problem was never a lack of measurements but
  that no signature matched them — and to the person reading, those are very
  different things. It now says: "Matches no signature / the numbers are
  measured, the table lacks them".

---

## V3.69 · 2026-10-06

### Qué cambia

- **El SSTV reconoce la cabecera por lo que ES, no por dónde cae.** El líder
  se buscaba «por encima de 1600 Hz». Con la sintonía 190 Hz baja aguantaba
  por los pelos; con 390 no existía. Y había algo peor: con la señal baja, la
  imagen entera vive en esa misma banda, así que mientras entraba una foto el
  detector creía oír líder todo el rato y se quedaba clavado — 117 segundos
  seguidos en el mismo paso, medido sobre una grabación real. Ahora el líder
  se reconoce porque **no se mueve**: son 300 ms de un tono solo, y la imagen
  cambia en cada punto. El desvío sale de dónde está quieto. El margen pasa de
  −190…+200 Hz a **−300…+300 Hz**.
- **El corte de la cabecera se mide como una bajada, no como una banda.** Los
  10 ms a 1200 Hz no dan tiempo a que la frecuencia acabe de caer: con −190 se
  quedaba en 1255 Hz corregidos, cuatro hercios fuera de la banda, y la
  cabecera se perdía ahí. Entre los dos líderes no hay nada más que pueda
  bajar, así que ahora basta con que baje.
- **El ajuste automático del RTTY ya ve el espectro.** Promediaba ocho veces
  el **mismo** cuadro, porque el dibujo del osciloscopio se lo quitaba antes.
  Y la gracia de promediar es que en 43 ms sólo suena uno de los dos tonos:
  sin cuadros distintos encontraba un tono, nunca la pareja, y sin pareja no
  hay desplazamiento que medir.

### What's new

- **SSTV now recognises the header by what it is, not where it falls.** The
  leader was looked for "above 1600 Hz". With tuning 190 Hz low it barely
  made it; at 390 Hz low it did not exist. Worse: with the signal low, the
  whole picture lives in that same band, so while a picture was coming in the
  detector thought it heard a leader continuously and jammed — 117 seconds
  stuck on the same step, measured against a real recording. The leader is now
  recognised because it **does not move**: it is 300 ms of a single tone,
  while a picture changes at every pixel. The offset comes from wherever that
  steady tone sits. The working range goes from −190…+200 Hz to
  **−300…+300 Hz**.
- **The header's break is measured as a drop, not as a band.** Ten
  milliseconds at 1200 Hz is not enough for the frequency to finish falling:
  at −190 it stopped at 1255 Hz corrected, four hertz outside the band, and
  the header was lost right there. Nothing else between the two leaders can
  fall, so now falling is enough.
- **RTTY's auto-tune can finally see the spectrum.** It was averaging the
  **same** frame eight times, because the scope drawing took it first. The
  point of averaging is that in 43 ms only one of the two tones is sounding:
  without distinct frames it found one tone, never the pair, and without a
  pair there is no shift to measure.

---

## V3.68 · 2026-10-06

### Qué cambia

- **Se acabó la «pantalla a mitad» en SSTV y fax.** Al entrar en SSTV salía por
  el medio de la foto el panel de texto del RTTY: su raya, sus renglones vacíos
  y un botón «Borrar» que ahí no pinta nada. No era un botón que sobrara, era
  el panel entero dibujado encima —y dibujado *después* de que el SSTV hubiera
  limpiado la zona, así que ya no lo borraba nadie—. El SSTV y el fax quitan el
  sitio al espectro igual que el RTTY, y por eso compartían la rama que repinta
  el panel de texto. Ahora cada uno repinta lo suyo, y al volver de una imagen
  al RTTY el texto se redibuja en limpio en vez de quedar sobre los píxeles de
  la foto.

### What's new

- **No more half-screen in SSTV and fax.** Entering SSTV painted RTTY's text
  panel right across the middle of the picture: its rule, its empty rows and a
  "Clear" button with nothing to clear. It was not a stray button but the whole
  panel drawn on top — and drawn *after* SSTV had cleared the area, so nothing
  erased it afterwards. SSTV and fax take the spectrum's place just like RTTY
  does, so they shared the branch that repaints the text panel. Each now
  repaints its own, and coming back from a picture to RTTY redraws the text
  clean instead of leaving it over the photo's pixels.

---

## V3.67 · 2026-10-06

### Qué cambia

- **El SSTV ya engancha con la sintonía desviada.** La cabecera se clasificaba
  con bandas fijas en hercios y los bits del código de modo se comparaban
  contra 1200 clavado. Con la señal 190 Hz baja —13 ppm de cristal, lo normal—
  el bit de arranque y los ceros caían en la banda equivocada: los ocho bits
  salían unos, el modo no se reconocía nunca y la radio se quedaba en
  «Esperando una imagen» emisión tras emisión. Ahora mide el desvío sobre el
  líder de la cabecera y corrige con él. Aguanta hasta unos **±190 Hz**.
- **Los botones del RTTY ya no se cuelan en SSTV ni en WEFAX.** Salían «450 Hz»,
  «50 Bd» y «Auto» al lado de «Empezar». Culpa de la V3.66.
- La fila del contador enseña las líneas **del modo**, no 256 siempre: un
  Robot 72 ponía «000/256» cuando tiene 240.

### What's new

- **SSTV now locks on when the tuning is off.** The header was classified with
  fixed hertz bands and the mode-code bits were compared against a hardcoded
  1200. With the signal 190 Hz low — 13 ppm of crystal error, nothing unusual —
  the start bit and the zeros fell in the wrong band: all eight bits read as
  ones, the mode was never recognised, and the radio sat at "Waiting for a
  picture" transmission after transmission. It now measures the offset on the
  header's leader and corrects by it. Good for about **±190 Hz**.
- **RTTY's buttons no longer leak into SSTV and WEFAX.** "450 Hz", "50 Bd" and
  "Auto" were showing up next to "Start". Introduced in V3.66.
- The line counter shows the **mode's** height, not always 256: a Robot 72 read
  "000/256" when it has 240.

---

## V3.66 · 2026-10-06

### Qué cambia

- **El RTTY ya se ajusta solo de verdad.** El módulo que pone los tonos encima
  de la señal, acierta el desplazamiento y mide la velocidad estaba escrito y
  probado desde la V3.58… y **sin llamar desde ningún sitio**. De las cuatro
  cosas que el manual decía que hacía sola, sólo hacía la inversión. Ahora
  hace las cuatro.
- **Y se puede apagar**: Ajustes → Digital → *Automático*, que es lo que
  prometía el código y no existía. Se guarda.
- **Encender o apagar el Ruido ya se guarda.** La clave existía desde el 01/10
  pero ni el botón de la barra ni el chip de la cabecera marcaban los ajustes
  para escribir, así que sólo se guardaba si después tocabas otra cosa. Lo
  mismo con el perfil de AGC tocando su chip.
- Las notas de Ident **se pintan enteras** sin gastar más memoria: las líneas
  del panel pasan a ser punteros en vez de nueve copias.

### What's new

- **RTTY now really does tune itself.** The module that places the tones on the
  signal, gets the shift right and measures the baud rate had been written and
  tested since V3.58 — and **was never called from anywhere**. Of the four
  things the manual claimed it did by itself, only inversion actually ran. Now
  all four do.
- **And it can be switched off**: Settings → Digital → *Automatic*, which the
  code promised and did not exist. It is saved.
- **Turning noise reduction on or off is now saved.** The key had existed since
  1 Oct, but neither the bottom-bar button nor the header chip marked settings
  dirty, so it was only written if you happened to change something else
  afterwards. Same for the AGC profile from its chip.
- Ident's notes now **print in full** without using more memory: the panel's
  lines became pointers instead of nine copies.

---

## V3.65 · 2026-10-06

### Qué cambia

- **Las notas de IDENT salían cortadas a mitad de palabra.** En 4.326 kHz ponía
  `Ponlo en STANAG: a 600 bps y entrela` cuando lo que dice la tabla es
  `...y entrelazado largo hay texto` — justo los dos ajustes por los que esa
  nota existe. El renglón tenía 48 bytes y las notas llegan a 73.
- No era falta de sitio en pantalla: la más larga mide 510 px y el panel tiene
  792. Era el búfer, y `snprintf` recortaba sin decir nada.
- **Eran 41 textos cortados**, no uno, contando los dos idiomas.
- Banco nuevo `identancho`: mide los nombres y las notas de las 27 firmas
  contra las dos cosas que las pueden cortar —el búfer y los píxeles—, en
  castellano y en inglés, leyéndolas de la tabla del firmware.

### What's new

- **IDENT's notes were being cut off mid-word.** On 4,326 kHz it read
  `Ponlo en STANAG: a 600 bps y entrela` when the table says
  `...with long interleaving there is text` — precisely the two settings that
  note exists for. The line buffer was 48 bytes and the notes run to 73.
- It was not a screen-space problem: the longest note is 510 px wide and the
  panel is 792. It was the buffer, and `snprintf` truncated silently.
- **41 strings were affected**, not one, counting both languages.
- New test `identancho`: measures every name and note of all 27 signatures
  against both things that can cut them — the buffer and the pixels — in both
  languages, reading them from the firmware's own table.

---

## V3.64 · 2026-10-06

### Qué cambia

- **Ajustando el ancho del filtro, la pastilla naranja decía «Escala mín.»**,
  que es otro ajuste. Ahora dice `Filtro mín.` o `Filtro máx.` según el corte
  que estés moviendo, y además enseña el valor en hercios, que tampoco salía.
- Lo enseñó una captura de pantalla, no un banco: en el código no se ve, porque
  el fallo era apuntar a un rótulo que existe y que es de otra cosa.

### What's new

- **While adjusting the audio filter width, the orange knob pill read "Scale
  min."** — a different setting entirely. It now reads `Filter min.` or
  `Filter max.` depending on which cut you are moving, and it also shows the
  value in hertz, which it never did.
- A screenshot found this one, not a test: it is invisible in the code, because
  the bug was pointing at a label that exists and belongs to something else.

---

## V3.63 · 2026-10-06

### Qué cambia

- **Tres botones que no hacían nada ya hacen lo suyo.** El de **Mapa en WSPR**
  se pintaba y no respondía —y si el mapa venía puesto de otro modo, se quedaba
  puesto sin forma de quitarlo—; el de **formato de salida de STANAG**
  (ASCII / ITA2 / HEX) tampoco respondía, así que esa rueda no se había podido
  usar nunca.
- **La fila «Formatear disco» ya se desarma al salir de Información.** Si salías
  con «BORRA TODO: confirma» puesto, al volver el **primer** toque ya formateaba:
  la confirmación se la habías dado sin saberlo, un rato antes y en otra pantalla.
  Y el veredicto de la vez anterior ya no te recibe diciendo «hecho».
- Los tres salieron de repasar el firmware para escribir el manual, no de usar
  la radio. Un botón que no hace nada es lo último que se nota.

### What's new

- **Three buttons that did nothing now do their job.** The **Map button in
  WSPR** was drawn but ignored — and if the map was already on from another
  mode, it stayed on with no way to turn it off; the **STANAG output format**
  button (ASCII / ITA2 / HEX) was ignored too, so that cycle had never been
  usable.
- **The "Format disk" row now disarms when you leave the Information screen.**
  If you left it reading "ERASES ALL: tap again", the **first** tap on your way
  back already formatted: you had confirmed it without knowing, earlier and on
  another screen. And last time's verdict no longer greets you with "done".
- All three came out of going through the firmware to write the manual, not out
  of using the radio. A button that does nothing is the last thing you notice.

---

## V3.62 · 2026-10-06

### Qué cambia

- **La fila *Cargar datos* ya no dice "no encuentro el .BIN"** cuando la base
  está cargada. Ese era el estado normal —la radio borra `BD.BIN` del disco
  cuando termina de pasarla a la flash— pero parecía una avería. Ahora pone
  `cargada · N frec.`
- Y se han quitado cuatro avisos del compilador que venían de la V3.57.

### What's new

- **The *Load data* row no longer says "cannot find the .BIN"** when the
  database is already loaded. That was the normal state — the radio deletes
  `BD.BIN` from the disk once it has moved it into flash — but it looked like a
  fault. It now reads `loaded · N freq.`
- And four compiler warnings dating back to V3.57 are gone.

---

## V3.61 · 2026-10-06

### Qué cambia

- **La fila *Formatear disco* salía cortada** (`...1788 clus`). La celda mide
  184 píxeles, no un número de letras. Los siete textos que puede mostrar están
  ahora medidos en el banco de anchos, en los dos idiomas.
- Y los motivos de error del formateo **ya salen en inglés** con la radio en
  inglés.

### What's new

- **The *Format disk* row was being cut off** (`...1788 clus`). The cell is 184
  pixels wide, not a number of letters. All seven texts it can show are now
  measured by the width test, in both languages.
- And the format error messages **now appear in English** when the radio is set
  to English.

---

## V3.60 · 2026-10-06

### Qué cambia

- **Formatear disco se comprueba solo.** Ya no dice "hecho" por no haber dado
  error: escribe un fichero de prueba y lo vuelve a leer. La fila pone
  `hecho: lee y graba`, o el motivo concreto si algo falla.
- **Y deja de quedarse en "hecho" para siempre**: el resultado se ve una vez y
  al siguiente toque la fila vuelve a mostrar el plan, sin reiniciar.

### What's new

- **Format disk now verifies itself.** It no longer says "done" just because
  nothing returned an error: it writes a test file and reads it back. The row
  shows `done: reads and writes`, or the specific reason if something fails.
- **And it stops being stuck on "done"**: the result shows once, and the next
  tap returns the row to the plan — no reboot needed.

---

## V3.59 · 2026-10-06

### Qué cambia

- **Formatear el disco desde la radio.** Información → *Formatear disco*. Deja
  el volumen exactamente como la radio sabe leerlo y escribirlo, que es lo que
  Windows no tiene por qué acertar. La fila dice antes de tocar nada lo que va
  a quedar, y pide dos toques porque borra el índice del disco.
- **Y la fila de *Cascada* se va al menú oculto** para hacerle sitio: lo que se
  busca cuando algo no funciona va delante.
- **El RTTY se ajusta solo**: pone los tonos encima de la señal, acierta el
  desplazamiento y mide la velocidad. Con la inversión automática de la V3.58,
  el modo pasa a ser apuntar y listo.
- Y como siempre: **sólo se mueve cuando lo de ahora va mal**. Si estás
  decodificando bien, no te toca nada.

### What's new

- **Format the disk from the radio.** Information → *Format disk*. It lays the
  volume out exactly the way the radio can read and write it, which Windows has
  no reason to get right. The row tells you what it will do before you touch
  anything, and asks for two taps because it erases the disk's index.
- **The *Waterfall* row moves to the hidden menu** to make room: what you look
  for when something is broken goes first.
- **RTTY tunes itself**: it puts the tones on the signal, gets the shift right
  and measures the speed. With V3.58's automatic inversion, the mode becomes
  point-and-go.
- And as always: **it only moves when what you have is going badly**. If you
  are decoding cleanly, it leaves you alone.

---

## V3.58 · 2026-10-06

### Qué cambia

- **El RTTY detecta solo si el desplazamiento está invertido** y se da la
  vuelta él. Antes, una emisión con la marca en el tono alto salía como
  basura hasta que ibas a Ajustes a invertirla a mano. Ahora tarda unos 7
  segundos y se corrige. Y si ya estabas decodificando bien, no te lo toca.
- **El filtro de tonos era cuatro veces más ancho de lo que debía**: medía
  sobre 32 muestras, o sea 375 Hz de resolución para separar tonos que están
  a 450. Ahora mide sobre medio bit: 100 Hz y 6 dB menos ruido.
- **Y cada tono se mide contra sí mismo**, no contra el otro, que es lo que
  rompía la decodificación cuando la ionosfera desvanecía uno de los dos.
- **Ident, el cargador y HFDL ya hablan inglés.** Eran las últimas cadenas
  de pantalla que no pasaban por el traductor. Hay un banco nuevo que
  recorre los 130 ficheros y falla si vuelve a colarse alguna.

### What's new

- **RTTY now detects a reversed shift on its own** and flips itself. A
  station with mark on the high tone used to decode as garbage until you
  went into Settings and inverted it by hand. It now takes about 7 seconds
  and corrects itself — and if you were already decoding cleanly, it leaves
  you alone.
- **The tone filter was four times wider than it should be**: it measured
  over 32 samples, i.e. 375 Hz of resolution to separate tones 450 Hz apart.
  It now measures over half a bit: 100 Hz and 6 dB less noise.
- **And each tone is measured against itself**, not against the other, which
  is what broke decoding when the ionosphere faded one of the two.
- **Ident, the loader and HFDL now speak English.** They were the last
  on-screen strings not going through the translator. A new test sweeps all
  130 source files and fails if another one slips through.

---

## V3.57 · 2026-10-06

### Qué cambia

- **Ident avisa cuando en una frecuencia hay más de una señal.** En 5339 kHz
  conviven un STANAG 4481 FSK, que está casi todo el rato, y una red ALE que
  sólo emite ráfagas de dos o tres segundos cuando alguien llama. La pantalla
  enseñaba una sola. Ahora añade un renglón `y a ratos: ...` con la otra.
- **Y no se la inventa.** Sólo cuenta una señal que aguante el canal 1,4 s
  seguidos; un tono que se cae un momento por desvanecimiento no cuela. El
  banco de pruebas pilló esa falsa alarma antes de que saliera de aquí.
- **ALE dice cuánto hace de la última ráfaga.** Antes, con la tabla vacía, la
  pantalla era idéntica si la red estaba callada o si el decodificador estaba
  roto. Ahora pone `ultima hace 4 min` en cuanto ha oído algo.

### What's new

- **Ident now reports when a frequency carries more than one signal.** On
  5339 kHz a STANAG 4481 FSK runs almost continuously while an ALE net only
  transmits two- or three-second bursts when someone calls. The screen showed
  just one. It now adds a `y a ratos: ...` ("and now and then") line naming
  the other one.
- **And it does not make it up.** A signal only counts if it holds the channel
  for 1.4 s straight, so a tone briefly lost to fading does not qualify. The
  test bench caught that false alarm before it shipped.
- **ALE shows how long since the last burst.** Previously an empty table looked
  the same whether the net was silent or the decoder was broken. It now reads
  `ultima hace 4 min` once it has heard anything.

---

## V3.56 · 2026-10-06

### Qué cambia

- **El identificador ya reconoce las FSK con desvanecimiento.** Medía cada
  tono contra el otro, y en onda corta la ionosfera desvanece los dos por
  separado: uno puede llegar 11 dB más flojo y la radio decía "no es FSK de
  dos" delante de una FSK de libro. Ahora mide cada tono contra el ruido que
  tiene al lado, que es lo que el desvanecimiento no toca. Sobre las
  grabaciones de prueba: de reconocerla el 2 % de las veces a el 90 %, sin
  que ninguna señal de las que NO son FSK pase a serlo.
- **Y el nombre deja de bailar**: usa la mediana de las últimas medidas en
  vez de la última.

### What's new

- **The identifier now recognises fading FSK signals.** It measured each tone
  against the other, and on shortwave the ionosphere fades the two
  independently: one can arrive 11 dB weaker and the radio would say "not
  two-tone FSK" in front of a textbook FSK. It now measures each tone against
  the noise beside it, which fading does not affect. Across the test
  recordings: from recognising it 2 % of the time to 90 %, with no non-FSK
  signal starting to pass as one.
- **And the name stops flickering**: it uses the median of the last readings
  instead of the latest one.

## V3.55 · 2026-10-06

### Qué cambia

- **El siseo deja de ser un tono.** El repintado del espectro estaba
  enganchado al ritmo del audio: sólo podía empezar justo cuando llegaba un
  bloque, así que el tiempo entre cuadros era siempre un número entero de
  bloques. Con eso, todo el ruido del dibujo se apilaba en los mismos pocos
  tonos y el oído los juntaba en un siseo. Ahora el plazo baila un par de
  milisegundos y la misma energía se reparte: entre 20 y 25 dB menos de
  concentración, con cualquier ajuste de fps.

### What's new

- **The hiss stops being a tone.** The spectrum repaint was locked to the
  audio rhythm: it could only start right when an audio block arrived, so
  the time between frames was always a whole number of blocks. All the
  repaint's noise piled onto the same few tones and the ear merged them into
  a hiss. The interval now jitters by a couple of milliseconds and the same
  energy spreads out: 20 to 25 dB less concentration, at every frame-rate
  setting.

## V3.54 · 2026-10-06

### Qué cambia

- **La fila de audio de Información ya no se corta.** El límite no era el
  tamaño del texto sino el ancho de la celda, con una fuente proporcional,
  así que ahora se mide en vez de contarse. El margen sale en tanto por
  ciento, que además se lee mejor.

### What's new

- **The Info page's audio row no longer gets cut off.** The limit was not
  the text buffer but the cell width, with a proportional font, so it is now
  measured rather than counted. The margin is shown as a percentage, which
  also reads better.

## V3.53 · 2026-10-06

### Qué cambia

- **El cargador ya puede leer discos que no sabe escribir.** De un issue: el
  cargador no veía `update.bin` en una radio con el formateo de fábrica. No
  es que no lo viera: es que rechazaba el volumen entero y ni siquiera
  miraba el directorio. Las comprobaciones duras son para poder *escribir*
  sin romper nada; para *leer* un fichero no hacen falta.
- **Y la ficha del cargador dice ahora por qué** no reconoce un volumen, en
  vez de limitarse a "no se reconoce".
- **Arreglado un desbordamiento** que metí ayer en la fila de audio de
  Información: el texto se salía de su hueco y pisaba el de la celda
  siguiente.

### What's new

- **The loader can now read disks it cannot write.** From an issue: the
  loader could not see `update.bin` on a radio with the factory format. It
  was not that it could not see it — it rejected the whole volume and never
  looked at the directory. The strict checks are for *writing* safely; they
  are not needed to *read* a file.
- **And the loader's info screen now says why** it does not recognise a
  volume, instead of just "not recognised".
- **Fixed a buffer overrun** introduced yesterday in the Info page's audio
  row: the text ran past its slot and into the next cell's.

## V3.52 · 2026-10-06

### Qué cambia

- **Quitado el "gr gr gr" de Información.** Esa página repintaba la rejilla
  entera —254.400 píxeles— dos veces por segundo, aunque sólo cambiara un
  número. Ahora pinta sólo las celdas que cambian: un 89 % menos de tráfico
  cada medio segundo.
- **Los contadores de audio dicen ahora de qué lado está el margen**: en vez
  de la distancia al borde más cercano, que era ambigua, salen el mínimo y
  el máximo.

### What's new

- **Removed the "gr gr gr" on the Info page.** That page repainted the whole
  grid —254,400 pixels— twice a second even when a single number changed. It
  now paints only the cells that changed: 89 % less traffic every half
  second.
- **The audio counters now say which side the margin is on**: instead of the
  distance to the nearest edge, which was ambiguous, they report the minimum
  and the maximum.

## V3.51 · 2026-10-06

### Qué cambia

- **Menos distorsión en FM.** La arcotangente rápida del discriminador era
  mala justo donde más se usa: metía un 2 % de distorsión en FM estrecha y
  casi un 1 % en FM comercial con programa flojo, y cuanto más floja la
  modulación, peor sonaba. Ahora es 24 veces mejor y no cuesta más.
- **Arreglado un golpe de audio** cuando la señal se va a cero exacto
  (arranque, cambio de modo): la arcotangente devolvía π/2 en vez de 0 y
  eso salía por el altavoz como un "pum" que además dejaba el AGC agachado.
- **El reductor de ruido ya no baja el volumen** 1,76 dB al encenderlo: su
  normalización estaba mal calculada.
- **El supresor de impulsos vuelve a medir el suelo** al encenderlo, en vez
  de usar el de la última vez.
- **Menos crepitar al mover el brillo**: el PWM de la retroiluminación
  escribía su comparador en caliente y colaba un pulso espurio por detente.
- **Nuevos contadores de audio** en Información, segundo renglón de la fila
  de audio: bloques descartados, interrupciones que llegaron tarde y
  repeticiones del buffer de salida. Sirven para localizar de dónde sale el
  siseo que aparece al pintar el espectro.
- Y tres fallos latentes más: un bucle sin acotar dentro de la interrupción
  de audio, basura de la pila que se guardaba como ajuste de filtro, y una
  parada de audio que no paraba del todo.

### What's new

- **Less distortion on FM.** The fast arctangent in the discriminator was
  worst exactly where it is used most: 2 % THD on narrow FM and nearly 1 %
  on broadcast FM with quiet programme, and the weaker the modulation the
  worse it sounded. It is now 24 times better and costs no more.
- **Fixed an audio thump** when the signal goes to exactly zero (start-up,
  mode change): the arctangent returned π/2 instead of 0, which came out of
  the speaker as a thump and left the AGC pulled down afterwards.
- **The noise reducer no longer drops the volume** by 1.76 dB when switched
  on: its normalisation was miscalculated.
- **The impulse blanker re-measures its floor** when switched on, instead of
  reusing the one from last time.
- **Less crackle when adjusting brightness**: the backlight PWM wrote its
  compare register live and slipped a spurious pulse in per detent.
- **New audio counters** on the Info page, second line of the audio row:
  discarded blocks, late interrupts and output-buffer repeats. They are for
  pinning down where the hiss that appears while the spectrum repaints comes
  from.
- Plus three more latent bugs: an unbounded loop inside the audio interrupt,
  stack garbage being saved as a filter setting, and an audio stop that did
  not fully stop.

## V3.50 · 2026-10-05

### Qué cambia

- **Arreglada la ganancia al arrancar en FM comercial (WFM).** Si encendías la
  radio ya puesta en WFM, se quedaba con la ganancia de entrada de HF en vez
  de con el tope que necesita la VHF: el espectro salía plano y el S-metro en
  S6. Bastaba cambiar de modo y volver para que apareciera todo. Son 23 dB.
- **Y la de salir de WFM**: al volver a HF te devolvía la ganancia de fábrica
  en lugar de la tuya.
- **Calibrar la pantalla o el ppm estando en WFM** guardaba el tope de WFM
  como si fuera tu ajuste de HF, y se quedaba ahí para siempre. Ya no.
- **El espectro vuelca un 38% menos de píxeles**: sólo repinta las filas y las
  columnas que han cambiado desde el cuadro anterior. Se ve exactamente igual
  —el banco compara el marco entero contra la imagen patrón— y deja el bus más
  libre para el audio.

### What's new

- **Fixed the input gain when the radio boots into broadcast FM (WFM).** If
  you powered it on already in WFM it kept the HF input gain instead of the
  maximum that VHF needs: the spectrum came up flat and the S-meter read S6.
  Switching mode and back made everything appear. That is 23 dB.
- **And leaving WFM**: going back to HF restored the factory gain instead of
  yours.
- **Running the touch or ppm calibration while in WFM** saved the WFM maximum
  as if it were your HF setting, and it stayed there. Not any more.
- **The spectrum now pushes 38% fewer pixels**: it only repaints the rows and
  columns that changed since the previous frame. It looks exactly the same
  —the test bench compares the whole frame against a golden image— and it
  leaves the bus freer for audio.

## V3.49 · 2026-10-05

### Qué cambia

- **Arreglado un descarte de bloques indebido en FM comercial (WFM).** La
  radio daba por corruptos bloques de señal perfectamente buenos y los
  descartaba, y cada descarte repite 2,67 ms de audio ya sonado. *Corregido
  el 05/10 por la noche: esto es un fallo real y está arreglado, pero **no**
  es lo que causa el siseo. El siseo entra con el audio en el arranque y no
  cambia al pasar del splash a la pantalla principal.*
- Dos contadores nuevos en la pantalla de información: bloques descartados y
  veces que la interrupción llegó tarde.
- El cargador v2.10 deja el arranque del panel exactamente como estaba en la
  v2.2.

### What's new

- **Fixed an incorrect block discard on broadcast FM (WFM).** The radio was
  marking perfectly good signal blocks as corrupt and discarding them, and
  each discard replays 2.67 ms of audio you already heard. *Corrected later
  on 05/10: this is a real bug and it is fixed, but it is **not** what causes
  the hiss. The hiss starts with the audio at boot and does not change when
  the splash gives way to the main screen.*
- Two new counters on the info screen: discarded blocks, and times the
  interrupt arrived late.
- Bootloader v2.10 restores the panel start-up to exactly what v2.2 did.

## V3.48 · 2026-10-05

### Qué cambia

- El tema nuevo pasa a llamarse **"Chemistry"**. Sólo es el nombre: los colores
  y el sitio en la lista son los mismos.
- **Cargador v2.4: las tres secuencias de arranque del panel, sacadas del
  cargador de fábrica ejecutándolo.** Dos de las tres estaban mal copiadas, y
  una de ellas era la que usaban todas las radios. Si la tuya se veía lavada,
  apagada o con los grises oscuros pegados entre sí, esto es lo que hay que
  probar.
- **Pantalla de prueba del panel**: en modo actualización, pulsando el mando se
  cambia entre las tres secuencias y se pinta una escala de grises. Sirve para
  ver cuál le va bien a tu pantalla y mandar una foto.
- (Lo que decía la v2.3 sobre la tabla de gamma seguía siendo cierto, pero el
  arreglo estaba a medias: faltaba el orden.)
  El arranque del panel mandaba los ajustes de color antes de abrir el modo que
  permite cambiarlos, así que se perdían y cada cristal se quedaba con lo que
  trae de fábrica — de ahí que dos radios iguales se vieran distintas. Si la
  tuya se veía lavada o apagada, esto es lo primero que hay que probar. Se graba
  por DFU, como el cargador de siempre.
- El cargador además vuelve a reconocer el tipo de panel él solo, como el de
  fábrica, en vez de usar siempre el mismo arranque para todos.
- En la ficha del cargador, la columna de la izquierda ya no sale en gris
  oscuro: en algunas pantallas no se leía.

### What's new

- The new theme is now called **"Chemistry"**. Name only: the colours and its
  place in the list are unchanged.
- **Bootloader v2.4: all three panel start-up sequences, taken from the factory
  loader by running it.** Two of the three were copied wrongly, and one of those
  was the sequence every radio used. If yours looked washed out, dim, or with
  the dark greys stuck together, this is the one to try.
- **Panel test screen**: in update mode, pressing the knob cycles the three
  sequences and draws a grey ramp. Use it to see which one suits your display,
  and send a photo.
- (What v2.3 said about the gamma tables was true, but the fix was half done:
  the ordering was missing.) The
  panel start-up sent the colour settings before opening the mode that allows
  changing them, so they were dropped and every panel kept its factory curve —
  which is why two identical radios could look different. If yours looked washed
  out or dim, try this first. Flashed over DFU, as the loader always is.
- The loader also detects the panel type on its own again, like the factory one,
  instead of using the same start-up sequence for every radio.
- In the loader's info panel the left-hand column is no longer dark grey: on
  some displays it could not be read.

## V3.47 · 2026-10-05

### Qué cambia

- **Tema nuevo: "Chemistry"**, papel blanco con tinta negra. Está pensado
  para pantallas que no distinguen los tonos oscuros, donde los otros temas se
  ven como una mancha plana. La paleta la escribió quien reportó el problema;
  va tal cual, con dos colores de estado retocados para que la luz de enganche
  y el icono de batería no desaparezcan sobre blanco.
- **El mapa del mundo ya sigue al tema**, de momento sólo con ése puesto. Antes
  salía azul oscuro con el tema que fuera: llevaba sus propios nueve colores y
  no miraba la paleta.
- Los temas van por índice, así que el nuevo entra **al final de la lista** y
  ninguno de los que ya tenías se mueve.

### What's new

- **New theme: "Chemistry"**, paper white with black ink. It is meant
  for displays that cannot separate dark tones, where every other theme reads
  as one flat smudge. The palette was written by the person who reported the
  problem and ships as sent, with two status colours adjusted so the digital
  sync light and the battery icon do not vanish on white.
- **The world map now follows the theme**, for now only under that one. It used
  to stay dark blue whatever you picked: it carried its own nine colours and
  never looked at the palette.
- Themes are stored by index, so the new one is appended **at the end** and
  nothing you already had moves.

## V3.46 · 2026-10-05

### Qué cambia

- El **volumen ya gira con el menú de Ajustes abierto**. El botón se podía
  encender y sacaba su caja, pero el giro seguía moviendo el cursor del menú.
  Ahora los detentes son del volumen y la pulsación sigue siendo del menú, así
  que se puede bajar el volumen sin salir de donde estabas.
- Lo mismo con el **teclado de frecuencia** y con la **rejilla de bandas**.
- Y no se queda enganchado: a los 4 s sin tocarlo el mando vuelve solo a la
  sintonía, también con el menú abierto.

### What's new

- **Volume now responds with the settings menu open.** The button could be lit
  and showed its box, but turning the encoder still moved the menu cursor.
  Detents now go to the volume while the knob press still belongs to the menu,
  so you can turn the volume down without leaving what you were doing.
- Same for the **frequency keypad** and the **band grid**.
- It does not get stuck: after 4 s untouched the knob returns to tuning on its
  own, with the menu open too.

## V3.45 · 2026-10-05

### Qué cambia

- Arreglado que en RTTY **sólo saliera el botón de la velocidad**. El del
  desplazamiento estaba puesto pero sin un píxel donde dibujarse.

### What's new

- Fixed the RTTY panel **showing only the speed button**. The shift button was
  there but had no pixels to draw itself in.

## V3.44 · 2026-10-05

### Qué cambia

- **Dos botones nuevos en el panel de RTTY**: desplazamiento (170 / 425 / 450
  / 850 Hz) y velocidad (45,45 / 50 / 75 / 100 Bd), sin salir a Ajustes.
- Ident ya nombra la **FSK de 50 baudios con 850 Hz** de salto, la de tráfico
  naval y diplomático. Comprobada en antena con FUG en 13.418 kHz.
- Arreglado que Ident diera **velocidad 0,0 Bd** con los dos tonos limpios
  delante. Pasaba a 50 baudios y por poco; a 75 no pasaba nunca.
- Cuando no hay nada que medir, Ident dice **"Sin estructura medible"** en vez
  de soltar un nombre de protocolo sacado sólo del ancho.

### What's new

- **Two new buttons on the RTTY panel**: shift (170 / 425 / 450 / 850 Hz) and
  speed (45.45 / 50 / 75 / 100 Bd), without leaving for the Settings screen.
- Ident now names **50 baud FSK with an 850 Hz shift**, the naval and
  diplomatic one. Confirmed on air against FUG on 13.418 kHz.
- Fixed Ident reporting **0.0 Bd** with two clean tones in front of it. It
  happened at 50 baud, and only barely; at 75 baud it never did.
- When there is nothing to measure, Ident says **"no measurable structure"**
  instead of producing a protocol name derived from bandwidth alone.

## V3.43 · 2026-10-05

### Qué cambia

- El RTTY comercial de 50 baudios ya sale en Ident sin el (?): comprobado en antena.
- En tools/, el empaquetador de emisoras ordena cada frecuencia por cercanía a tu QTH.
- Las herramientas avisan de que BD.BIN no puede pasar de 1 MB.

### What's new

- Commercial 50 Bd RTTY is now shown in Ident without the (?): confirmed on air.
- In tools/, the station packer sorts each frequency by distance from your QTH.
- The tools now warn that BD.BIN cannot exceed 1 MB.

## V3.42 · 2026-10-05

### Qué cambia

- Ident mide la velocidad de símbolo aunque no vea tonos sueltos.
- Ya nombra el MFSK multitono (Olivia y parecidos) en vez de encogerse de hombros.

### What's new

- Ident measures the symbol rate even when it cannot see individual tones.
- It now names multi-tone MFSK (Olivia and friends) instead of giving up.

## V3.41 · 2026-10-05

### Qué cambia

- Ident aprende a medir la cadencia de una señal: cada cuánto arranca y cuánto ocupa.
- Con eso reconoció el FT8 en antena por primera vez.

### What's new

- Ident learns to measure a signal's cadence: how often it starts and how much it fills.
- That got FT8 recognised on air for the first time.

## V3.40 · 2026-10-05

### Qué cambia

- Arreglado que Ident no reconociera el FT8 en una banda real.

### What's new

- Fixed Ident failing to recognise FT8 on a real band.

## V3.39 · 2026-10-05

### Qué cambia

- Modo Ident nuevo: apúntale una señal y te dice qué puede ser, con veintitantas firmas.

### What's new

- New Ident mode: point it at a signal and it tells you what it might be, with twenty-odd signatures.

## V3.38 · 2026-10-05

### Qué cambia

- Los valores largos ya no se salen de su celda en Información.
- La barra de "Guardando" deja de parpadear.

### What's new

- Long values no longer spill out of their cell on the Information screen.
- The "Saving" progress bar stops flickering.

## V3.37 · 2026-10-05

### Qué cambia

- Arreglado el "NO CABE: directorio lleno" con el pendrive casi vacío.

### What's new

- Fixed "directory full" appearing with a nearly empty USB stick.

## V3.36 · 2026-10-05

### Qué cambia

- El cartel de "Guardando" ya no desaparece en un segundo.

### What's new

- The "Saving" notice no longer vanishes after a second.

## V3.35 · 2026-10-05

### Qué cambia

- Las capturas funcionan también en los modos digitales y en las pantallas de opciones.

### What's new

- Screenshots now work in the digital modes and the settings screens too.

## V3.34 · 2026-10-05

### Qué cambia

- Fuera la línea blanca del final de las capturas.
- Los pitidos suenan en todas las capturas, no sólo en la primera.
- Guardar una captura tarda ocho segundos menos.

### What's new

- The white line at the bottom of screenshots is gone.
- The beeps sound on every capture, not just the first.
- Saving a screenshot is eight seconds faster.

## V3.33 · 2026-10-05

### Qué cambia

- Las capturas ya se ven bien y se guardan 2,5 veces más rápido.

### What's new

- Screenshots now come out correct and save 2.5 times faster.

## V3.32 · 2026-10-05

### Qué cambia

- Capturas de pantalla al pendrive, en BMP de 800x480: cuatro segundos en la esquina superior izquierda.
- Pita al empezar y al acabar; tarda unos 25 s con los mandos congelados.
- Sólo desde la pantalla principal, no dentro de los menús.

### What's new

- Screenshots to the USB stick as 800x480 BMP: hold the top-left corner for four seconds.
- It beeps at the start and the end; it takes about 25 s with the controls frozen.
- Only from the main screen, not inside the menus.

## V3.31 · 2026-10-05

### Qué cambia

- Los filtros de audio pasan a su camino rápido: la carga de audio baja de forma notable.
- La radio ya puede avisar con un pitido por el altavoz.

### What's new

- The audio filters move to their fast path: audio load drops noticeably.
- The radio can now beep through the speaker.

## V3.30 · 2026-10-05

### Qué cambia

- Radio y espectro mucho más fluidos: de unos 14 a unos 38 fotogramas por segundo.
- Girar el mando ya no da tirones al sintonizar.
- Analizador de audio, notch automático y ALE consumen mucho menos.

### What's new

- Radio and spectrum are far smoother: from about 14 to about 38 frames per second.
- Turning the knob no longer stutters while tuning.
- The audio analyser, auto notch and ALE all use much less processing.

## V3.29 · 2026-10-05

### Qué cambia

- SAM ya casi no frena la radio ni la cascada.
- Los PPM ya no se inventan: pone "buscando" si no hay enganche.
- "Calibrar PPM" muestra su valor y dice qué ha hecho al pulsarlo.

### What's new

- SAM barely slows the radio or the waterfall any more.
- The PPM figure is no longer invented: it says "searching" when there is no lock.
- "Calibrate PPM" shows its value and says what it did when you press it.

## V3.28 · 2026-10-04

### Qué cambia

- SAM va más fluido: la lectura de PPM ya no repinta media pantalla en cada fotograma.
- FM de banda estrecha y ancha consumen bastante menos.

### What's new

- SAM is smoother: the PPM reading no longer repaints half the screen every frame.
- Narrow and wide FM use considerably less processing.

## V3.27 · 2026-10-04

### Qué cambia

- En SAM la radio ya no se arrastra: pantalla, táctil y menús vuelven a responder.
- Se libera memoria interna, que estaba al borde de agotarse.

### What's new

- In SAM the radio no longer crawls: screen, touch and menus respond again.
- Internal memory is freed up, having been on the edge of running out.

## V3.26 · 2026-10-03

### Qué cambia

- Un carácter perdido ya no deja el resto del mensaje NAVTEX en cifras ilegibles.

### What's new

- A single lost character no longer leaves the rest of a NAVTEX message as unreadable figures.

## V3.25 · 2026-10-02

### Qué cambia

- El NAVTEX por fin descodifica emisiones reales: antes no sacaba ni una letra del aire.
- La radio mide sola el centro de los tonos, así que engancha aunque el dial no esté exacto.

### What's new

- NAVTEX finally decodes real transmissions: it used not to get a single letter off air.
- The radio measures the tone centre itself, so it locks even when the dial is not exact.

## V3.24 · 2026-10-02

### Qué cambia

- El botón Borrar ya limpia de verdad el texto y los contadores.
- Borrar no pierde el enganche: no hay que esperar otra vez a que se llene el entrelazador.

### What's new

- The Clear button really does clear the text and the counters.
- Clearing does not lose the lock: no waiting again for the interleaver to fill.

## V3.23 · 2026-10-02

### Qué cambia

- El panel muestra el preámbulo junto al nivel de ruido y el umbral, y dice si ahí hay señal.

### What's new

- The panel shows the preamble next to the noise level and threshold, and says whether a signal is there.

## V3.22 · 2026-10-02

### Qué cambia

- Engancha con señales flojas o con interferencia al lado, no sólo con señales limpias.
- Un enganche falso ya no llega a soltar texto inventado.

### What's new

- It locks on weak signals or with interference alongside, not just on clean ones.
- A false lock no longer gets as far as emitting invented text.

## V3.21 · 2026-10-02

### Qué cambia

- Ya descodifica señales reales en el aire, también con la sintonía muy desviada.
- Si se pierde audio se reinicia el relleno, para que no salga texto falso.

### What's new

- It now decodes real signals off air, including when badly off tune.
- If audio is lost the fill restarts, so no false text comes out.

## V3.20 · 2026-10-02

### Qué cambia

- Panel de diagnóstico permanente bajo el texto: preámbulo, desvío, cola, tramas y ajustes.
- La búsqueda de portadora llega ahora hasta mil hercios.

### What's new

- A permanent diagnostics panel under the text: preamble, offset, queue, frames and settings.
- Carrier search now reaches a thousand hertz.

## V3.19 · 2026-10-02

### Qué cambia

- El descodificador ya no se come su propio audio mientras busca: más enganche.
- El panel indica cuántas pérdidas hay y cuánto audio aguanta la cola.

### What's new

- The decoder no longer eats its own audio while searching, so it locks more often.
- The panel shows how many dropouts there have been and how much audio the queue holds.

## V3.18 · 2026-10-02

### Qué cambia

- Mirar el panel ya no impide enganchar: la pantalla no se repinta sin motivo.
- Reenganche rápido tras un corte, útil con costeras que emiten a ráfagas.

### What's new

- Watching the panel no longer prevents a lock: the screen does not repaint for no reason.
- Fast re-lock after a dropout, useful with coast stations that transmit in bursts.

## V3.17 · 2026-10-02

### Qué cambia

- Se acabó el tirón general de la radio al buscar señal: la búsqueda va treinta veces más rápida.
- Ya no aparece texto inventado idéntico en frecuencias distintas.
- El estado ya no se pinta encima del botón de formato.

### What's new

- No more stutter across the whole radio while searching: the search is thirty times faster.
- Identical invented text no longer appears on different frequencies.
- The status no longer paints over the format button.

## V3.16 · 2026-10-02

### Qué cambia

- Ahora engancha aunque la sintonía esté desviada: busca la portadora en un margen amplio.
- El indicador de error ya no marca cero con una emisión callada.

### What's new

- It locks on even when you are off tune: it searches for the carrier over a wide range.
- The error indicator no longer reads zero on a silent transmission.

## V3.15 · 2026-10-02

### Qué cambia

- La posición S4481 ya descodifica texto, no sólo identifica.
- Arranca en 600 bps, la velocidad de las costeras en claro, y el panel dice qué está haciendo.

### What's new

- The S4481 setting now decodes text, rather than only identifying.
- It starts at 600 bps, the usual rate for coast stations in clear, and the panel says what it is doing.

## V3.14 · 2026-10-02

### Qué cambia

- Se acabó que la radio se quedase colgada y perdiese los ajustes al usar S4285.
- Si una combinación de velocidad y entrelazado no cabe, se vuelve a la anterior.

### What's new

- No more freezing and losing your settings when using S4285.
- If a speed and interleaving combination does not fit, it falls back to the previous one.

## V3.13 · 2026-10-02

### Qué cambia

- Nuevo descodificador STANAG 4285: las emisiones sin cifrar salen como texto en pantalla.
- Tres botones nuevos: velocidad, entrelazado y formato de salida.

### What's new

- New STANAG 4285 decoder: unencrypted transmissions appear as text on screen.
- Three new buttons: speed, interleaving and output format.

## V3.12 · 2026-10-02

### Qué cambia

- La pantalla del modo STANAG se refresca cuatro veces por segundo aunque no detecte nada.
- Un contador más para ver en qué punto se para la cadena.

### What's new

- The STANAG screen refreshes four times a second even when nothing is detected.
- One more counter, to see where in the chain it stops.

## V3.11 · 2026-10-02

### Qué cambia

- Ahora sí se repinta la pantalla del modo STANAG; antes los contadores nunca se movían.

### What's new

- The STANAG screen really does repaint now; the counters never used to move.

## V3.10 · 2026-10-02

### Qué cambia

- El panel del modo STANAG se refresca en vez de quedarse con la primera foto.
- El modo da señales de vida aunque no vea ninguna trama.

### What's new

- The STANAG panel refreshes instead of freezing on its first snapshot.
- The mode shows signs of life even when it sees no frames at all.

## V3.09 · 2026-10-02

### Qué cambia

- Mientras no hay medida, la pantalla enseña contadores que dicen en qué etapa se para.

### What's new

- Until there is a measurement, the screen shows counters telling you where it stalls.

## V3.08 · 2026-10-02

### Qué cambia

- Al elegir el modo ya no se queda en USB ni se pierde el panel.
- El modo pasa a llamarse STANAG.

### What's new

- Choosing the mode no longer leaves you stuck in USB with no panel.
- The mode is renamed STANAG.

## V3.07 · 2026-10-02

### Qué cambia

- Nuevo modo IDENT en Digital: mide el periodo y dice qué señal puede ser.
- Botón de forma de onda con Auto, S4285, S4529, S4481 y S4538.
- En Hora por radio la fecha ya no se pisa con los botones.

### What's new

- New IDENT mode under Digital: it measures the period and says what the signal might be.
- Waveform button with Auto, S4285, S4529, S4481 and S4538.
- In Radio clock the date no longer overlaps the buttons.

## V3.06 · 2026-10-02

### Qué cambia

- Los números del reparto del audio ya son correctos; antes una etapa se disparaba al 99 %.

### What's new

- The audio breakdown figures are correct now; one stage used to shoot up to 99 %.

## V3.05 · 2026-10-02

### Qué cambia

- La pantalla de Información se refresca sola: las medidas ya no se quedan pegadas.
- Nueva fila Reparto del audio con el coste de cada etapa del sonido.

### What's new

- The Information screen refreshes by itself: the measurements no longer freeze.
- New Audio breakdown row showing what each stage of the sound costs.

## V3.04 · 2026-10-02

### Qué cambia

- Admite discos de hasta 16 MB que antes rechazaba por la tabla de ficheros.
- El cargador enseña en pantalla su propia versión y su código de contenido.

### What's new

- Accepts disks of up to 16 MB that were previously rejected over the file table.
- The bootloader shows its own version and content code on screen.

## V3.03 · 2026-10-02

### Qué cambia

- Información se queda limpia: fuera nueve filas de herramientas viejas.
- El Modo DFU está ya en Información, sin buscarlo con cinco toques ocultos.
- Nueva fila con el número de serie del chip de flash externa.

### What's new

- Information is tidied up: nine rows of old tooling are gone.
- DFU mode now lives in Information, with no five-tap secret handshake.
- New row showing the external flash chip's serial number.

## V3.02 · 2026-10-02

### Qué cambia

- La radio ya acepta el disco formateado con las opciones por defecto de Windows.
- Funciona con chips de flash mayores sin tener que elegir un tamaño de unidad raro.

### What's new

- The radio now accepts a disk formatted with Windows' default options.
- It works with larger flash chips without picking an odd allocation unit size.

## V3.01 · 2026-10-02

### Qué cambia

- La versión en Información sale con un código que identifica exactamente el binario grabado.

### What's new

- The version in Information carries a code that identifies the exact binary flashed.

## V3.00 · 2026-10-02

### Qué cambia

- Aviso: rama nueva incompatible. Hay que grabar primero el cargador y después el update.bin.
- El arranque valida la imagen entera: una actualización a medias ya no deja la radio colgada.
- Los ficheros pasan a llamarse update.bin, customboot.bin y BD.BIN.

### What's new

- Warning: this is a new, incompatible branch. Flash the bootloader first, then update.bin.
- Startup validates the whole image, so a half-finished update no longer bricks the radio.
- The files are now called update.bin, customboot.bin and BD.BIN.

## V2.44 · 2026-10-01

### Qué cambia

- El botón "Modo DFU" ya funciona: el PC reconoce la radio como dispositivo DFU.
- Permite regrabar el arranque de fábrica por USB sin abrir la radio.

### What's new

- The "DFU mode" button works at last: the PC sees the radio as a DFU device.
- It makes reflashing the factory bootloader over USB possible without opening the radio.

## V2.43 · 2026-10-01

### Qué cambia

- Otro intento de entrar en DFU reiniciando el USB antes del salto; la pantalla se quedaba negra.

### What's new

- Another attempt at entering DFU by resetting USB first; the screen went black.

## V2.42 · 2026-10-01

### Qué cambia

- Otro intento de entrar en DFU, saltando nada más encender la radio; seguía sin entrar.

### What's new

- Another attempt at entering DFU, jumping as soon as the radio powers up; still no.

## V2.41 · 2026-10-01

### Qué cambia

- Otro intento de entrar en DFU marcando el modo de arranque; seguía sin entrar.

### What's new

- Another attempt at entering DFU by setting the boot mode; still would not enter.

## V2.40 · 2026-10-01

### Qué cambia

- Otro intento de entrar en DFU, ahora dejando el reloj como tras un reset; seguía sin entrar.

### What's new

- Another attempt at entering DFU, now leaving the clock as it is after a reset; still no.

## V2.39 · 2026-10-01

### Qué cambia

- Otro intento de entrar en DFU siguiendo una receta conocida; seguía sin pasar nada.

### What's new

- Another attempt at entering DFU following a published recipe; still nothing happened.

## V2.38 · 2026-10-01

### Qué cambia

- Otro intento de entrar en DFU apagando los periféricos antes del salto; seguía sin entrar.

### What's new

- Another attempt at entering DFU by shutting down the peripherals first; still no.

## V2.37 · 2026-10-01

### Qué cambia

- El reinicio ya conserva la orden de DFU, pero el PC seguía sin ver la radio.

### What's new

- The reboot now preserves the DFU request, but the PC still could not see the radio.

## V2.36 · 2026-10-01

### Qué cambia

- Otro intento de entrar en DFU, ahora reiniciando la radio al tocar la fila; seguía sin entrar.

### What's new

- Another attempt at entering DFU, this time rebooting on the tap; still would not enter.

## V2.35 · 2026-10-01

### Qué cambia

- La ROM de fábrica se vuelca al VOLCADO.BIN de siempre, sin pedir otro fichero.

### What's new

- The factory ROM is dumped to the usual VOLCADO.BIN, with no second file to supply.

## V2.34 · 2026-10-01

### Qué cambia

- Las filas de volcado ya muestran el porcentaje y el motivo exacto del fallo.
- Antes un volcado fallido ponía "tocar" y parecía un botón muerto.

### What's new

- The dump rows now show a percentage and the exact reason for a failure.
- A failed dump used to just say "tap" and look like a dead button.

## V2.33 · 2026-10-01

### Qué cambia

- Nueva fila "Volcar ROM de fábrica" en la página oculta de Información.
- Deja el volcado en el disco USB de la radio, sin abrir el aparato.

### What's new

- New "Dump factory ROM" row on the hidden Information page.
- It leaves the dump on the radio's USB disk, without opening the case.

## V2.32 · 2026-10-01

### Qué cambia

- Otro intento de arreglar el cuelgue al tocar "Modo DFU"; sin comprobar que funcione.

### What's new

- Another attempt at the freeze when tapping "DFU mode"; not verified to work.

## V2.31 · 2026-10-01

### Qué cambia

- Nueva fila "Modo DFU" en la página oculta de Información, para grabar por USB sin soldar.
- Pide dos toques, pero de momento la pantalla se queda quieta: se sale apagando y encendiendo.

### What's new

- New "DFU mode" row on the hidden Information page, to reflash over USB without soldering.
- It asks for two taps, but the screen freezes for now: power-cycle to get out.

## V2.30 · 2026-09-30

### Qué cambia

- Nueva fila Carga de audio en Información: cuánto cuesta el audio y la reducción de ruido.

### What's new

- New Audio load row in Information: what the audio and the noise reduction actually cost.

## V2.29 · 2026-09-30

### Qué cambia

- Con señal la pantalla ya no se ralentiza: el fotograma baja un 11 %.

### What's new

- The display no longer slows down when there is a signal: frame time drops by 11 %.

## V2.28 · 2026-09-30

### Qué cambia

- Guardar los ajustes ya no borra otros ficheros de la flash ni se pierde al arrancar.
- Con el suavizado alto la cascada vuelve a verse nítida y al ritmo real.
- Cambiar la pastilla RATE ya no deja APRS sordo ni el analizador con el eje mal.

### What's new

- Saving settings no longer wipes other files from flash or gets lost at startup.
- With heavy smoothing the waterfall is sharp again and runs at the real rate.
- Changing the RATE chip no longer leaves APRS deaf or the analyser's axis wrong.

## V2.27 · 2026-09-30

### Qué cambia

- Tocar bajo las pantallas de Volumen o Brillo ya no cambia el filtro de audio sin avisar.
- Al salir de HFDL, WSPR, AIS, ALE o JTTY la cascada ya no se llena de basura.

### What's new

- Tapping below the Volume or Brightness screens no longer changes the audio filter silently.
- Leaving HFDL, WSPR, AIS, ALE or JTTY no longer fills the waterfall with rubbish.

## V2.26 · 2026-09-30

### Qué cambia

- El mapa se arrastra y se amplía con la mitad de trabajo, o sea más suave.
- Las coordenadas del QTH salen con punto y con signo, listas para pegar en un mapa.
- Una radio sin configurar arranca con IN80dk.

### What's new

- The map pans and zooms with half the work, so it feels smoother.
- QTH coordinates are shown with a decimal point and a sign, ready to paste into a map.
- An unconfigured radio starts up at IN80dk.

## V2.25 · 2026-09-30

### Qué cambia

- En APRS ya se ve el osciloscopio de tono, con las rayas en 1.200 y 2.200 Hz.
- El localizador sale sólo de tus ajustes: sin él, no hay aspa ni distancias.
- Nueva familia VHF/UHF: las bandas que no cabían vuelven a verse.

### What's new

- APRS now shows the tone scope, with markers at 1,200 and 2,200 Hz.
- The locator comes only from your settings: without it, no cross and no distances.
- New VHF/UHF family: the bands that did not fit are visible again.

## V2.24 · 2026-09-30

### Qué cambia

- Fuera el rótulo del panel que el mapa tapaba siempre y que ya era falso.

### What's new

- Removed the panel label the map always covered and which was no longer true.

## V2.23 · 2026-09-30

### Qué cambia

- Zoom del mapa con - a la izquierda y + a la derecha.
- Táctil y Calibrar táctil pasan al menú Pantalla.
- Nombres de países sin letras comidas.

### What's new

- Map zoom with - on the left and + on the right.
- Touch and Calibrate touch move to the Display menu.
- Country names no longer lose letters.

## V2.22 · 2026-09-30

### Qué cambia

- Nueva pantalla Ajustes - Equipo - QTH para teclear tu localizador.
- FT8 y WSPR ya no piden poner el reloj en hora si ya lo está.
- Ajustes - Equipo - Mando invierte el sentido del encoder.

### What's new

- New Settings - Equipment - QTH screen for typing in your locator.
- FT8 and WSPR no longer ask you to set the clock when it is already set.
- Settings - Equipment - Knob reverses the encoder direction.

## V2.21 · 2026-09-30

### Qué cambia

- La pantalla de Información también sale en inglés, incluidos reloj y cristal.

### What's new

- The Information screen is in English too, clock and crystal included.

## V2.20 · 2026-09-30

### Qué cambia

- Traducidos una veintena de rótulos sueltos que seguían saliendo en castellano.

### What's new

- Translated a couple of dozen stray labels that were still appearing in Spanish.

## V2.19 · 2026-09-30

### Qué cambia

- Nueva opción Ajustes - Equipo - Idioma: toda la radio pasa a inglés sin reiniciar.

### What's new

- New Settings - Equipment - Language option: the whole radio switches to English without a restart.

## V2.18 · 2026-09-28

### Qué cambia

- El brillo al 100 % vuelve a alumbrar al máximo, como antes de septiembre.

### What's new

- Brightness at 100 % is genuinely full again, as it was before September.

## V2.17 · 2026-09-28

### Qué cambia

- JTTY muestra una columna de País.
- La hora y la calidad ocupan menos, así que el mensaje se ve más largo.
- Filas con fondo alterno en JTTY y WSPR.

### What's new

- JTTY shows a Country column.
- Time and quality take less room, so more of the message is visible.
- Alternating row backgrounds in JTTY and WSPR.

## V2.16 · 2026-09-28

### Qué cambia

- WSPR ya tiene mapa: se ve desde dónde se oyen las balizas.
- Arreglado el mapa de JTTY, que resaltaba el contacto más viejo en vez del más nuevo.

### What's new

- WSPR now has a map: you can see where the beacons are being heard from.
- Fixed the JTTY map, which highlighted the oldest contact instead of the newest.

## V2.15 · 2026-09-28

### Qué cambia

- Un mensaje largo de JTTY sale entero aunque se pierdan varias tramas por el camino.
- A cambio, un mensaje con el final perdido puede tardar hasta diez segundos en salir.

### What's new

- A long JTTY message comes out whole even if several frames are lost on the way.
- The trade-off: a message whose ending is lost can take up to ten seconds to appear.

## V2.14 · 2026-09-28

### Qué cambia

- JTTY ya no mezcla conversaciones de estaciones distintas en la misma frecuencia.
- Sigue hasta ocho conversaciones a la vez sin que se pisen.
- El botón Mapa se queda quieto y ya no se encoge al crecer el contador.

### What's new

- JTTY no longer mixes up conversations from different stations on one frequency.
- It follows up to eight conversations at once without them overlapping.
- The Map button stays put and no longer shrinks as the counter grows.

## V2.13 · 2026-09-28

### Qué cambia

- JTTY coge el doble de tramas: menos puntos suspensivos y mensajes más enteros.
- Cerca de 2 dB más de sensibilidad con desvanecimiento, sin indicativos inventados.

### What's new

- JTTY picks up twice as many frames: fewer gaps and more complete messages.
- About 2 dB more sensitivity under fading, with no invented callsigns.

## V2.12 · 2026-09-28

### Qué cambia

- Cuando falta un trozo del mensaje JTTY, sale con puntos suspensivos en medio.
- Con varias estaciones a la vez ya no se pierden tramas enteras.

### What's new

- When part of a JTTY message is missing, it is shown with an ellipsis in the gap.
- With several stations at once, whole frames are no longer dropped.

## V2.11 · 2026-09-28

### Qué cambia

- Las palabras de JTTY ya no se parten: sale "WE REALLY NEED", no "WE RE ALLY".
- Dos mensajes distintos ya no se pegan en un mismo renglón.

### What's new

- JTTY words no longer break apart: you get "WE REALLY NEED", not "WE RE ALLY".
- Two different messages no longer end up glued into one line.

## V2.10 · 2026-09-28

### Qué cambia

- JTTY oye mucho más: la ventana pasa de 500 a 1.000 Hz de audio.
- Cada emisión deja de salir duplicada en la lista.
- Desaparece el botón de grabar trozos, que ya había cumplido su función.

### What's new

- JTTY hears much more: the audio window goes from 500 to 1,000 Hz.
- Transmissions no longer appear twice in the list.
- The capture-snippets button is gone, having served its purpose.

## V2.09 · 2026-09-28

### Qué cambia

- JTTY estrena botón de frecuencias con las nueve de llamada habituales.
- JTTY ya pinta en el mapa con su propio color: cuatro modos con mapa.

### What's new

- JTTY gains a frequency button with the nine usual calling frequencies.
- JTTY now plots on the map in its own colour: four modes with a map.

## V2.08 · 2026-09-28

### Qué cambia

- Nuevo modo JTTY: la radio ya decodifica y enseña sus mensajes.
- JTTY escucha en continuo, sin esperar a ranuras del reloj.
- De momento JTTY va sin botón de frecuencias y sin mapa.

### What's new

- New JTTY mode: the radio decodes and displays its messages.
- JTTY listens continuously, with no need to wait for clock slots.
- For now JTTY has no frequency button and no map.

## V2.07 · 2026-09-28

### Qué cambia

- El botón Mapa ya se ve en FT8: antes se borraba solo una vez por segundo.

### What's new

- The Map button is now visible in FT8: it used to erase itself once a second.

## V2.06 · 2026-09-28

### Qué cambia

- Nuevo botón Mapa, debajo de BW, en vez de tener que tocar la lista.
- El mapa ocupa todo el ancho y se mueve arrastrando con el dedo.
- Zoom x1, x2, x4 y x8 con los mandos + y - del propio mapa.

### What's new

- New Map button below BW, instead of having to tap the list.
- The map now spans the full width and pans by dragging with a finger.
- Zoom x1, x2, x4 and x8 from the map's own + and - controls.

## V2.05 · 2026-09-28

### Qué cambia

- Vuelve la lluvia de katakana en la pantalla de arranque.
- Los caracteres ya no se quedan pegados encima de la línea de créditos.

### What's new

- The katakana rain is back on the splash screen.
- Characters no longer stick on top of the credits line.

## V2.04 · 2026-09-28

### Qué cambia

- Los tamaños en pantalla salen bien puntuados: 665.600 en vez de 665.60.
- Tras copiar aparece "Comprobando lo copiado" con su barra: ya no parece colgada.
- El nombre de la emisora y sus datos pasan en marquesina cuando no caben.

### What's new

- Sizes on screen are punctuated properly: 665,600 instead of 665.60.
- After copying, a "verifying" bar appears, so it no longer looks frozen.
- Station names and details scroll when they do not fit.

## V2.03 · 2026-09-28

### Qué cambia

- DATOS.BIN ya se carga aunque Windows lo deje partido en trozos por el pendrive.
- Se acabó el "falta DATOS.BIN" con el fichero puesto: ahora dice el motivo de verdad.

### What's new

- DATOS.BIN now loads even when Windows has scattered it in fragments across the stick.
- No more "DATOS.BIN missing" with the file right there: it now states the real reason.

## V2.02 · 2026-09-28

### Qué cambia

- Ahora basta con grabar un solo fichero, DATOS.BIN, en el disco USB de la radio.
- Caben más aviones en la base: de 50.396 a 57.769.
- Los avisos de error dicen de qué base hablan y cuál es el motivo real.

### What's new

- A single file, DATOS.BIN, is all you now copy to the radio's USB disk.
- More aircraft fit in the database: up from 50,396 to 57,769.
- Error messages name the database they refer to and the actual reason.

## V2.01 · 2026-09-28

### Qué cambia

- El mapa llega también a HFDL y AIS: aviones en naranja, barcos en azul.
- Se pintan todos los barcos y aviones de la tabla, no sólo los que caben en la lista.
- El mapa se borra al cambiar de modo y ya no enseña lo del modo anterior.

### What's new

- The map now covers HFDL and AIS too: aircraft in orange, ships in blue.
- Every ship and aircraft in the table is plotted, not just those that fit the list.
- The map clears when you change mode instead of keeping the previous one's traffic.

## V2.00 · 2026-09-28

### Qué cambia

- Nuevo mapa del mundo en el panel de FT8: se ve por dónde entran las señales.
- Se pasa de lista a mapa tocando la propia lista.
- Los RR73 ya no salen como estaciones falsas en mitad del Pacífico.

### What's new

- New world map on the FT8 panel: you can see where signals are coming from.
- Tap the list itself to switch between list and map.
- RR73 replies no longer show up as phantom stations in the middle of the Pacific.

---

## Antes de V2.00 · del 24/09/2026 al 28/09/2026

Las versiones no se numeraban todavía; esta época acaba en la V1.95. Es cuando
se rehízo la interfaz entera y entraron casi todos los modos digitales. Por
temas:

Releases were not numbered yet; this era ends at V1.95. It is when the whole
interface was rebuilt and most of the digital modes arrived. By theme:

### La interfaz, rehecha · The interface, rebuilt

- Pantallas nuevas con tipografías proporcionales, esquinas redondeadas y degradados.
- Temas de interfaz y quince paletas de espectro y cascada, a elegir.
- Teclado nuevo para meter la frecuencia y para poner el reloj en hora.
- Pantalla de Información con versión, fecha de compilación y memoria usada.
- New screens with proportional fonts, rounded corners and gradients.
- Interface themes and fifteen spectrum and waterfall palettes to choose from.
- A new keypad for entering a frequency and for setting the clock.
- An Information screen with version, build date and memory used.

### Modos digitales nuevos · New digital modes

- FT8: mensajes con hora, frecuencia, dB, desfase, indicativo y país.
- HFDL: tabla de aviones con dirección OACI, modelo, vuelo y posición.
- WSPR: balizas con indicativo, localizador, potencia y desvío de frecuencia.
- AIS y ALE: barcos por MMSI, y sondeos de estaciones en onda corta.
- PSK31: charla en texto, con osciloscopio y el error de sintonía en hercios.
- FT8: messages with time, frequency, dB, offset, callsign and country.
- HFDL: an aircraft table with ICAO address, model, flight and position.
- WSPR: beacons with callsign, locator, power and frequency drift.
- AIS and ALE: ships by MMSI, and shortwave station soundings.
- PSK31: text chat, with a scope and the tuning error in hertz.

### Bases de datos a bordo · Databases on board

- Base de 101.431 aviones: HFDL enseña el modelo, no sólo el código de tipo.
- Listas EiBi y Aoki: al sintonizar sale quién emite, desde dónde y en qué idioma.
- Tabla de 244 prefijos: FT8 muestra el país, y dice "no sé" cuando duda.
- Las bases se copian solas del pendrive al arrancar, y se comprueban antes de borrar.
- A database of 101,431 aircraft: HFDL shows the model, not just the type code.
- EiBi and Aoki lists: tune in and it tells you who is on, from where and in what language.
- A table of 244 prefixes: FT8 shows the country, and says "unknown" when unsure.
- The databases copy themselves off the stick at startup, and are verified before deletion.

### Imágenes al pendrive · Images to the USB stick

- SSTV y WEFAX guardan lo decodificado como BMP en el disco USB de la radio.
- En SSTV se elige formato: 24 bits (caben cuatro) o 16 bits (caben seis).
- Tocar el modo en SSTV abre los catorce, con duración y resolución de cada uno.
- SSTV and WEFAX save what they decode as BMP on the radio's USB disk.
- In SSTV you pick the format: 24-bit (four fit) or 16-bit (six fit).
- Tapping the mode in SSTV opens all fourteen, with each one's duration and resolution.

### Escuchar y sintonizar · Listening and tuning

- Decodificador de CW que engancha aun desafinado y que calla antes que inventar letras.
- Filtro de CW de cuatro etapas con ancho elegible: 1k0, 500 o 250 Hz.
- Tocar el espectro cae encima de la señal, con error medio de 21 a 34 Hz.
- Autoescala del espectro por percentiles: el ruido ya no sale aplastado.
- A CW decoder that locks even when off tune and stays quiet rather than inventing letters.
- A four-stage CW filter with selectable width: 1k0, 500 or 250 Hz.
- Tapping the spectrum lands on the signal, with a mean error of 21 to 34 Hz.
- Percentile auto-scaling of the spectrum: the noise floor is no longer squashed flat.

### Va más rápido · Speed

- El dibujo del espectro pasa de 69 a 20 milisegundos por fotograma.
- FT8 saca mensajes cada quince segundos; antes salía una tanda de cada cuatro.
- La pantalla de Información abre al instante, sin los 200 ms de espera.
- Spectrum drawing goes from 69 to 20 milliseconds per frame.
- FT8 produces messages every fifteen seconds; it used to manage one batch in four.
- The Information screen opens instantly, without the 200 ms wait.

### Fallos gordos que ya no pasan · Major bugs fixed

- La radio se colgaba con un pitido muy molesto al entrar en FT8.
- El reloj de arriba se quedaba parado al entrar en cualquier modo digital.
- Elegir WSPR, AIS o ALE no cambiaba la pantalla: seguía pintando el espectro.
- HFDL se llenaba de filas "ID 255" que echaban fuera a los aviones de verdad.
- WSPR daba localizadores equivocados: ponía IO70 donde de verdad era JO20.
- El brillo llevaba dos meses a medio gas por una polaridad al revés.
- The radio locked up with a loud beep when entering FT8.
- The clock at the top froze on entering any digital mode.
- Choosing WSPR, AIS or ALE did not change the screen: it kept drawing the spectrum.
- HFDL filled with "ID 255" rows that crowded out the real aircraft.
- WSPR gave wrong locators: IO70 where it was really JO20.
- Brightness had been at half power for two months because of a reversed polarity.

### Más sitio dentro de la radio · More room inside

- El firmware puede ocupar 320 kB en vez de 256: de 4 kB libres a 45 kB.
- La ventana de FT8 pasa de 400 a 1.600 Hz: se leen muchas más estaciones.
- Se quitó la fuente de fábrica que no se usaba y quedó medio mega para las bases.
- The firmware may now use 320 kB instead of 256: from 4 kB free to 45 kB.
- The FT8 window goes from 400 to 1,600 Hz, so many more stations are read.
- An unused factory font was removed, freeing half a megabyte for the databases.

---

*V2.15 no existe como binario: el diario numeró dos entradas seguidas como
V2.14 y aquí se ha separado lo que contaba cada una. / There is no V2.15
binary: the log numbered two consecutive entries V2.14, and what each one
covered has been split out here.*
