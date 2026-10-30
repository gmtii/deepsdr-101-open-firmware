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

## V6.25 · 2026-10-30

### Qué cambia

- **La pantalla principal mete 2,4 veces menos ruido en el audio.** Avisado
  por el usuario por tercera vez, y con razón: *«no puede ser que en el menú
  se escuche mejor y sin interferencias»*.

  Medido sobre el audio de su vídeo, comparando los tramos con el menú
  abierto contra los que tienen el espectro en pantalla: **hasta 10 dB más
  de siseo**, y latiendo a **374,8 Hz** —que es 96.000/256, el ritmo del
  bloque de audio— y a 42-44 Hz, el del cuadro. Con el menú abierto esas dos
  rayas no existen.

  El acoplo es **eléctrico**: sube con la ganancia de entrada —o sea que
  entra antes del PGA— y **no** baja con el brillo, así que tampoco es la
  retroiluminación. Es el bus del panel conmutando, y su energía es
  proporcional a los píxeles que se empujan.

  Y se empujaban de más. El panel se manda por bandas de 16 filas, y de cada
  banda se enviaba el tramo que va **del primer segmento que cambia al
  último**, de una tirada: con una traza de ruido, la banda entera. Ahora se
  manda **cada racha de segmentos que pinta por separado**.

  | | antes | ahora |
  |---|---|---|
  | píxeles por cuadro | 124.939 (79,5 % del panel) | **52.132 (33,2 %)** |
  | a 45 fps | 5,62 Mpixel/s | **2,35 Mpixel/s** |

  **Y no cambia ni un píxel de lo que se ve**: lo compara `sim/espectro_oro.c`
  contra una imagen patrón, y salta si se mueve uno solo.

- **Y el panadaptador se puede frenar o parar, y ahora se guarda.** La fila de
  los milisegundos de fotograma baja hasta 100 ms y llega a **0, que lo para
  del todo**: la pantalla principal se calla exactamente igual que con el menú
  abierto, y el dial, la ese, el audio y los decodificadores siguen vivos.
  Antes no se guardaba y había que volver a ponerlo en cada arranque.

### What's new

- **The main screen now injects 2.4 times less noise into the audio.**
  Reported by the user for the third time, and rightly: *"it cannot be that
  it sounds better and interference-free in the menu"*.

  Measured on the audio of his video, comparing the stretches with the menu
  open against those with the spectrum on screen: **up to 10 dB more hiss**,
  beating at **374.8 Hz** —which is 96,000/256, the audio block rate— and at
  42-44 Hz, the frame rate. With the menu open neither line exists.

  The coupling is **electrical**: it rises with input gain —so it enters
  before the PGA— and does **not** drop with the backlight, so it is not the
  backlight either. It is the panel bus switching, and its energy is
  proportional to the pixels pushed.

  And too many were being pushed. The panel is sent in bands of 16 rows, and
  each band was sent as the span from **the first changed segment to the
  last**, in one go: with a noisy trace, the whole band. Now **each run of
  painting segments is sent separately**.

  | | before | now |
  |---|---|---|
  | pixels per frame | 124,939 (79.5 % of the panel) | **52,132 (33.2 %)** |
  | at 45 fps | 5.62 Mpixel/s | **2.35 Mpixel/s** |

  **And not one pixel of what you see changes**: `sim/espectro_oro.c` compares
  against a golden image and trips if a single one moves.

- **And the panadapter can be slowed down or stopped, and it is now saved.**
  The frame-milliseconds row goes down to 100 ms and reaches **0, which stops
  it altogether**: the main screen goes as quiet as it does with the menu
  open, while the dial, the S-meter, the audio and the decoders stay alive.
  It used not to be saved, so it had to be set again on every boot.

---

## V6.24 · 2026-10-30

### Qué cambia

- **Apagar la radio mientras guarda ya no puede llevarse los ficheros del
  disco.** Avisado por el usuario, y con la imagen de su disco delante: tras
  apagarla y encenderla tenía los ajustes de fábrica y **un solo fichero**,
  con 2,26 MB marcados como ocupados. Sus ficheros seguían dentro; lo que se
  había perdido era **la lista de nombres**.

  El motivo: para cambiar una entrada del directorio hay que **borrar los
  4 kB en los que vive**, y en esos 4 kB caben **128 entradas de otros
  ficheros**. El corte cayó en esa ventana. En su disco, el primer bloque
  del directorio apareció a `0xFF` —flash recién borrada y nunca
  regrabada— y el segundo, intacto.

  Son dos arreglos:

  **Guardar los ajustes ya no toca el directorio.** Si la entrada va a decir
  lo mismo —mismo nombre, mismo cluster, mismo tamaño— no se reescribe. Lo
  único que cambiaba era la hora de la entrada. Medido: diez guardados, diez
  borrados de 4 kB en vez de veinte, y ninguno del directorio. **Ése es el
  camino que se lo hizo.**

  **Y para los que sí tienen que tocarlo** —crear un fichero, borrarlo, o el
  tamaño de una carta de fax que crece cada dos segundos y medio— ahora hay
  red: antes de borrar el bloque se guarda una copia de lo que va a quedar
  en él, y al arrancar, si la copia sigue viva y el bloque no dice lo mismo,
  se regraba.

  **Y ahora el banco sabe apagar la radio**, que es lo que faltaba para que
  esto no lo cazara nadie. Se corta la corriente en cada operación de
  escritura del chip, una por una, y se cuentan los nombres al arrancar:

  | | sin red | con red |
  |---|---|---|
  | lo peor que se pierde | **62 entradas de 63** | **0** |
  | cortes dentro de la ventana | — | 18, los 18 reparados |

  Sin el arreglo, el banco reproduce la avería clavada: una entrada y un
  bloque del directorio en blanco.

### What's new

- **Switching the radio off mid-save can no longer take the disk's files
  with it.** Reported by the user, with his disk image in hand: after an
  off/on he had factory settings and **a single file**, with 2.26 MB still
  marked as used. His files were still there; what had been lost was **the
  list of names**.

  Why: changing one directory entry means **erasing the 4 kB it lives in**,
  and **128 entries belonging to other files** fit in those 4 kB. The cut
  landed in that window. On his disk the directory's first block came back
  as `0xFF` —freshly erased flash, never reprogrammed— and the second one
  intact.

  Two fixes:

  **Saving settings no longer touches the directory.** If the entry would
  say the same thing —same name, same cluster, same size— it is not
  rewritten. The only thing that changed was the entry's timestamp.
  Measured: ten saves, ten 4 kB erases instead of twenty, and none of them
  the directory. **That is the path that did it to him.**

  **And for the ones that must touch it** —creating a file, deleting one, or
  the size of a fax growing every two and a half seconds— there is now a
  safety net: before erasing the block, a copy of what it is about to hold
  is saved, and at boot, if that copy is still live and the block does not
  match it, the block is rewritten.

  **And the bench now knows how to switch the radio off**, which is what was
  missing for anyone to catch this. Power is cut at every single chip write
  operation, one by one, and the names are counted at the next boot:

  | | without the net | with it |
  |---|---|---|
  | worst case lost | **62 entries out of 63** | **0** |
  | cuts inside the window | — | 18, all 18 repaired |

  Without the fix the bench reproduces the failure exactly: one entry and a
  blank directory block.

---

## V6.23 · 2026-10-30

### Qué cambia

- **Con el NCO puesto, la pantalla ya no pega el salto al cruzar el centro.**
  Avisado por el usuario con un vídeo: en 40 m, al pasar de 7.103 a 7.106 kHz,
  el eje entero se corría once kilohercios. Había un **agujero de ±6 kHz**
  alrededor del oscilador y cruzarlo obligaba a reaparcar.

  Ese agujero existía porque la fuga del oscilador se automezcla a 0 Hz y se
  sentaba encima de la emisora. **Eso dejó de ser cierto el 25/10**, cuando el
  receptor empezó a restar la continua de I y de Q antes de bajar en
  frecuencia. El agujero era deuda.

  Quitado, y además el reaparcado aterriza ahora en el extremo **contrario**
  en vez de a +6 kHz, que es lo que deja toda la ventana por delante. Medido:
  **0 reaparcados y 181 kHz seguidos** de borde a borde. Antes eran 84 kHz y
  un salto en el centro.

  **Lo que se pierde al cruzar, y se dice:** dentro de esos doce kilohercios
  la emisora cae encima de su propia imagen. Eso es un receptor de frecuencia
  intermedia cero y no lo arregla ningún corrector: son doce kilohercios de
  **paso**, no un sitio donde quedarse a escuchar.

- **Y el corrector de espejo ya no se envenena al pasar por ahí**, que es lo
  que habría costado abrir el centro sin mirar nada más. Su estimador se apoya
  en que la señal esté desplazada del oscilador; con la señal en continua se
  inventa un desequilibrio y se lo lleva puesto a la banda siguiente. La
  puerta que lo evitaba miraba sólo «¿es FM ancha?», que valía mientras FM
  ancha fuese el único sitio donde eso pasaba.

  Medido en el camino de verdad, el de la interrupción: una calibración de
  **59,9 dB** se quedaba en **28,8** después de cuatro segundos cruzando el
  centro. Con la puerta puesta, **59,3**. Ahora la puerta mira las dos cosas
  —modo y distancia al oscilador— y se refresca al sintonizar, no sólo al
  cambiar de modo.

- **El lazo de continua de radiofrecuencia se congela al cruzar** en vez de
  medir la emisora y restársela. Conserva lo último que midió, que es lo
  correcto: la fuga sigue ahí mientras se pasa.

- **Dos valores de fábrica, pedidos:** el **suavizado** del espectro pasa de
  75 a **25 %** y el **contorno** de 3 pasadas a **1**. Con tres cuartas
  partes de peso en lo viejo la traza dejaba estela; con tres pasadas de
  contorno, una portadora de un solo bin salía más baja y más ancha de lo que
  es. Una radio con su `CONFIG.CSV` guardado se queda con lo que tenga puesto.

### What's new

- **With the NCO on, the display no longer jumps when crossing the centre.**
  Reported by the user with a video: on 40 m, going from 7.103 to 7.106 kHz
  the whole axis shifted by eleven kilohertz. There was a **±6 kHz hole**
  around the oscillator, and crossing it forced a re-park.

  That hole existed because the local oscillator's leakage self-mixes to 0 Hz
  and sat on top of the station. **That stopped being true on 25/10**, when
  the receiver started subtracting the DC of I and Q before the downconversion.
  The hole was debt.

  Gone — and the re-park now lands at the **opposite** end instead of at
  +6 kHz, which is what leaves the whole window ahead. Measured: **0 re-parks
  and 181 kHz in a row**, edge to edge. It was 84 kHz and a jump in the middle.

  **What is lost when crossing, said plainly:** inside those twelve kilohertz
  the station lands on top of its own image. That is what a zero-IF receiver
  is, and no corrector fixes it: twelve kilohertz to **pass through**, not a
  place to sit and listen.

- **And the image corrector no longer poisons itself going through there**,
  which is what opening the centre would have cost. Its estimator relies on
  the signal being offset from the oscillator; with the signal at DC it
  invents an imbalance and carries it into the next band. The gate that
  prevented this only asked "is this wide FM?", which held while wide FM was
  the only place that happened.

  Measured on the real path, the one inside the interrupt: a **59.9 dB**
  calibration dropped to **28.8** after four seconds crossing the centre. With
  the gate, **59.3**. The gate now looks at both things — mode and distance to
  the oscillator — and is refreshed on tuning, not only on a mode change.

- **The RF DC loop freezes while crossing** instead of measuring the station
  and subtracting it. It keeps its last measurement, which is the right thing:
  the leakage is still there while you pass.

- **Two factory defaults, as requested:** spectrum **smoothing** goes from 75
  to **25 %** and **contour** from 3 passes to **1**. With three quarters of
  the weight on history the trace smeared; with three contour passes a
  single-bin carrier came out lower and wider than it is. A radio with a saved
  `CONFIG.CSV` keeps whatever it has.

---

## V6.22 · 2026-10-29

### Qué cambia

- **La lista de FT8 ya no pierde mensajes en una ranura llena.** La cola del
  decodificador admitía 40 y la lista guarda 48: una ranura de banda abierta
  con 45 mensajes perdía cinco **antes** de llegar a la lista, que tenía sitio
  de sobra, y los perdía en silencio. Ahora la cola es tan larga como la
  lista, y un `_Static_assert` impide que se vuelvan a separar.
- **Y con el dedo subido la lista ya no se descoloca.** Al entrar mensajes
  nuevos, la ventana subía tantos renglones como había visto el
  decodificador, no los que de verdad entraron en la lista. Si la cola se
  desbordaba, lo que estabas leyendo se te iba hacia abajo.

*Recordatorio de cómo es la lista: guarda **48** mensajes y el panel enseña
**15**. A los otros 33 se llega arrastrando con el dedo hacia abajo sobre la
lista; cuando vuelves al final se queda otra vez pegada a lo nuevo.*

### What's new

- **The FT8 list no longer loses messages on a busy slot.** The decoder queue
  held 40 while the list holds 48, so a wide-open slot with 45 messages lost
  five **before** they ever reached the list, which had room for them — and it
  lost them silently. The queue is now as long as the list, and a
  `_Static_assert` stops the two drifting apart again.
- **And the list no longer slips when you have scrolled it up.** As new
  messages arrived the window advanced by however many the *decoder* had seen
  rather than however many actually went into the list, so a queue overflow
  dragged what you were reading off the bottom.

*As a reminder: the list holds **48** messages and the panel shows **15**.
The other 33 are reached by dragging down over the list; back at the end it
sticks to the newest again.*

---

## V6.21 · 2026-10-29

### Qué cambia

- **Doce palabras que se quedaban en castellano con la radio en inglés.** En
  la pantalla de hora por radio: los rótulos «marca», «ciclo», «raras»,
  «mal» y «código», y los siete motivos por los que se descarta una trama de
  MSF («corta», «rango», «patrón», «par-min»…). No los veía el guardián de
  idioma porque no eran cadenas: se escribían **letra a letra**, carácter a
  carácter, y eso no es un texto que se pueda buscar. Ahora pasan por el
  traductor como todo lo demás, y el guardián junta esas rachas y las mira
  como palabras.
- Y cambiar de idioma repinta la cabecera entera, no tres cuartos de ella.

### What's new

- **Twelve words that stayed in Spanish with the radio set to English.** On
  the radio-clock screen: the labels "marca", "ciclo", "raras", "mal" and
  "código", and the seven reasons an MSF frame gets rejected ("corta",
  "rango", "patrón", "par-min"…). The language guard never saw them because
  they were not strings: they were written **letter by letter**, character by
  character, which is not text anything can search for. They now go through
  the translator like everything else, and the guard joins those runs up and
  reads them as words.
- And switching languages now repaints the whole header, not three quarters
  of it.

---

## V6.20 · 2026-10-29

### Qué cambia

- **Cambiar de tema ya no deja un trozo del tema anterior.** Al cambiar la
  paleta se repintaba «la cabecera entera»… que no lo era: desde el 05/10 esa
  función sólo pinta la frecuencia y la pastilla de banda. La esquina del
  reloj y de la batería se quedaba con los colores viejos —un rectángulo gris
  con el tema claro puesto— hasta el siguiente cambio de minuto, que es quien
  la repinta. Ahora se repinta la cabecera de verdad entera.

### What's new

- **Switching themes no longer leaves a patch of the old one.** Changing the
  palette repainted "the whole header" — except it didn't: since 5 Oct that
  function only paints the frequency and the band pill. The clock and battery
  corner kept the old colours — a grey rectangle under a light theme — until
  the next minute change, which is what repaints it. The header is now
  repainted in full.

---

## V6.19 · 2026-10-29

### Qué cambia

- **El cartel de la emisora ya no se queda cortado al abrir un menú.** Con
  Ajustes —o la rejilla, o el teclado, o el localizador— abiertos, el nombre
  y el horario de la emisora dejaban de correr y se veía sólo el principio:
  «China National Rad», «CHN - UI - 11:00-14:0». Ninguna de esas pantallas
  tapa la cabecera —todas empiezan por debajo—, así que el texto seguía a la
  vista, recortado y quieto. Y al cambiar de minuto volvía a empezar por el
  principio y ahí se quedaba. Ahora corre siempre que se ve. Lo mismo para la
  marquesina del RDS, que tenía la misma parada.

### What's new

- **The station caption no longer freezes half-read when a menu is open.**
  With Settings — or the mode grid, the keypad or the locator — open, the
  station name and schedule stopped scrolling and you only ever saw the
  start: "China National Rad", "CHN - UI - 11:00-14:0". None of those screens
  covers the header — they all start below it — so the text stayed visible,
  clipped and still. And on each minute change it restarted from the
  beginning and stayed there. It now scrolls whenever it is visible. Same for
  the RDS marquee, which had the identical stop.

---

## V6.18 · 2026-10-29

### Qué cambia

- **La hora por radio se pone con su segundo.** Avisado por un probador: tras
  sincronizar con ALS162 se enseñaba HH.MM, se encendía «Aplicar» y los
  segundos no avanzaban, así que al pulsar el reloj se quedaba con los
  segundos a cero. La hora de estas emisoras vale en el instante de su borde
  de minuto, no cuando la miras: entre ese instante y el dedo se tiraban los
  veinte o treinta segundos que cuesta leer la pantalla y decidirse. Ahora la
  pantalla enseña **HH:MM:SS corriendo** —que es exactamente lo que se va a
  poner— y «Aplicar» ancla el reloj a ese borde. Vale para las cinco
  emisoras: DCF77, MSF, ALS162, RBU y WWV/BPM. El RDS ya lo hacía así.

### What's new

- **The radio clock now sets the seconds too.** Reported by a tester: after
  syncing to ALS162 the screen showed HH.MM with "Apply" lit, but the seconds
  never advanced, so pressing Apply left the clock at zero seconds. The time
  these stations send is valid **at its minute edge**, not when you look at
  it, and the twenty or thirty seconds it takes to read the screen and decide
  were being thrown away. The screen now shows **a running HH:MM:SS** — which
  is exactly what Apply will set — and Apply anchors the clock to that edge.
  This covers all five stations: DCF77, MSF, ALS162, RBU and WWV/BPM. RDS
  already worked this way.

---

## V6.17 · 2026-10-29

### Qué cambia

- **HFDL vuelve a enganchar.** La V6.16 traía un cambio en el corrector de
  imagen —tirar también la corrección «plana» al cambiar de banda— que en
  antena resultó ser malo: en 11.184 kHz con señales fuertes el embudo se
  quedaba en 8>3>3 y ninguna trama pasaba el CRC. Revertido. El corrector
  plano es el grande, y olvidarlo en una banda donde no puede volver a
  medirse no lo deja corrigiendo de menos: lo deja sin corregir.

### What's new

- **HFDL locks again.** V6.16 carried a change to the image corrector —also
  dropping the "flat" correction on a band change— which turned out to be
  wrong on air: at 11,184 kHz with strong signals the funnel stalled at
  8>3>3 and no frame passed its CRC. Reverted. The flat corrector is the big
  one, and forgetting it in a band where it cannot measure itself again does
  not leave it under-correcting: it leaves it not correcting at all.

---

## V6.16 · 2026-10-29

### Qué cambia

- **El táctil de STANAG y de IDENT ya no se hace de rogar.** En esos dos
  modos había que insistir para que entrara un toque, con el mando
  funcionando bien. El motivo: un toque necesita dos lecturas con el dedo
  puesto, y el bucle principal mira el táctil sólo entre decodificador y
  decodificador. La propia radio lo midió —«181 ms poll» en el panel de
  STANAG con señal—, y 181 ms es una ventana de 181 ms sin mirar el dedo.
  Ahora los decodificadores largos miran el táctil **desde dentro**, con el
  mismo mecanismo que ya usaban los repintados largos.

- **Y si aun así no responde, se sale con el mando.** Una pulsación larga
  dentro de uno de los ocho modos de panel entero apaga el modo digital y
  devuelve el espectro. Hasta hoy, sin táctil no se salía de ahí más que
  apagando la radio.

- **El panel ya no se queda en blanco al pasar de un modo digital a otro.**
  De DSC a FT8, de PSK31 a JTTY y cualquier otra pareja de distinto tipo: la
  mitad de los botones no aparecían.

- **HFDL: once cosas arregladas**, de cinco pasadas de revisión. Las que se
  notan en antena: el corrector de imagen dejaba de medirse al abrir un menú
  y se quedaba congelado corrigiendo mal; el corrector plano seguía aplicando
  lo aprendido en otra banda; y al entrar en HFDL el silenciador del modo se
  abría de par en par durante tres segundos y medio por el salto de ganancia
  de la versión anterior.

- **Y seis que se notan en la pantalla**: las estaciones de tierra salían
  como un avión fantasma «ID 255»; una trama dirigida a varios aviones ponía
  el vuelo y la posición de uno en la ficha de otro —y el mapa lo pintaba
  donde no estaba—; el contador de posiciones no se veía en las frecuencias
  de puros LOGON; los contadores no se reiniciaban al entrar; la vista de
  bytes mezclaba mensajes de sesiones anteriores; y de las tramas con más de
  cuatro mensajes dentro sólo se miraban los cuatro primeros.

- **El CAT ya no deja la radio a medias.** Si un programa del ordenador
  cambiaba el modo estando en un modo digital, el decodificador se quedaba
  vivo, la cascada congelada y el botón Modo en «?».

- **Las memorias guardadas con firmwares anteriores vuelven con su paso
  correcto.** Desde la V6.05 se leían corridas: la de marina de 12,5 kHz
  volvía en 10 kHz.

- **El barrido de memorias se para solo** al entrar en un modo de panel
  entero, en vez de seguir moviéndole el dial al decodificador.

### What's new

- **Touch in STANAG and IDENT no longer needs coaxing.** In those two modes
  you had to insist before a tap registered, while the knob worked fine. The
  reason: a tap needs two readings with the finger down, and the main loop
  only looked at the touch panel between decoders. The radio measured it
  itself —«181 ms poll» on the STANAG panel with a signal— and 181 ms is a
  181 ms window with nobody watching the finger. The long decoders now check
  the touch panel **from the inside**, with the same mechanism long repaints
  already used.

- **And if it still does not answer, the knob gets you out.** A long press
  inside any of the eight full-panel modes turns the digital mode off and
  brings the spectrum back. Until today, no touch meant no way out short of
  powering the radio down.

- **The panel no longer goes blank when switching between two digital
  modes.** DSC to FT8, PSK31 to JTTY and any other pair of different type:
  half the buttons simply did not appear.

- **HFDL: eleven fixes**, from five review passes. The ones that show on air:
  the image corrector stopped being measured as soon as a menu opened and
  froze while still correcting wrongly; the flat corrector kept applying what
  it had learnt in another band; and entering HFDL left the mode's own gate
  wide open for three and a half seconds because of the previous release's
  gain step.

- **And six that show on screen**: ground stations appeared as a phantom
  aircraft «ID 255»; a frame addressed to several aircraft put one's flight
  and position on another's row —and the map drew it in the wrong place—; the
  position counter was hidden on LOGON-only frequencies; counters were not
  reset on entry; the byte view mixed messages from earlier sessions; and
  frames carrying more than four messages only had the first four read.

- **CAT no longer leaves the radio half-switched.** A program on the computer
  changing the mode while a digital mode was running left the decoder alive,
  the waterfall frozen and the Mode button showing «?».

- **Memories stored by earlier firmware come back with the right tuning
  step.** Since V6.05 they were read one position off: the 12.5 kHz marine
  memory came back as 10 kHz.

- **The memory scan now stops by itself** when a full-panel mode is selected,
  instead of retuning the radio under the decoder.

---

## V6.15 · 2026-10-29

### Qué cambia

- **Cada modo digital tiene ya su propia ganancia de entrada, y HFDL arranca
  en 35 dB.** Avisado por el usuario con la medida hecha en antena: «he
  entrado a HFDL con 20 dB y parecía perder paquetes, la he subido a 35 y
  parece ir mejor».

  Los 20 dB eran el valor de fábrica, así que quien entraba en HFDL sin haber
  tocado nunca el mando entraba con ellos. Y el AGC interno de HFDL —que
  tiene 100 dB de margen— no lo tapa, porque actúa **después del
  convertidor**: un AGC digital arregla la amplitud, no la relación
  señal/ruido. Con la ganancia analógica baja, una ráfaga floja mueve pocos
  códigos del conversor y el ruido de cuantización ya está dentro;
  multiplicar por mil no devuelve los bits que el conversor no llegó a usar.

  Ahora **FT8, HFDL, WSPR, AIS, ALE, JTTY, STANAG e IDENT tienen cada uno la
  suya**, guardada en `CONFIG.CSV`. Lo que gires dentro de un modo se queda
  en ese modo y no te mueve la de escuchar. El pie del ajuste lo dice: «sólo
  para este modo».

  **De serie, HFDL en 35,0 dB y los otros siete en los 20,0 de siempre.** El
  35 es un número medido; en los otros siete no hay medida, y cambiar el
  arranque de un modo que hoy funciona apoyándose en la observación de otro
  sería justo lo que no se hace aquí.

- **Y por dentro, diez ganancias dejan de ser diez ramas.** Había cuatro
  condiciones encadenadas para repartir dos ganancias, y meter ocho más
  habría multiplicado los casos. En su lugar hay **zonas**: escuchar, FM
  ancha y cada uno de los ocho digitales, con **una sola función** que guarda
  lo vivo donde estabas y carga lo de donde estás.

  No es cosmética: este fichero ya tenía dos averías escritas con esa misma
  variable, las dos por ramas que se miraban entre ellas. El guardián
  `pga_check.py` se ha reescrito para vigilar la propiedad nueva —que el
  cambio de zona tenga **un solo dueño**— y se ha comprobado que se pone rojo
  con tres averías inventadas a propósito.

### What's new

- **Every digital mode now has its own input gain, and HFDL starts at
  35 dB.** Reported by the user, measured on air: "I went into HFDL at 20 dB
  and it seemed to drop packets, I raised it to 35 and it seems better".

  20 dB was the factory value, so anyone entering HFDL without ever having
  touched the knob entered with it. And HFDL's internal AGC — which has
  100 dB of range — does not cover for it, because it acts **after the
  converter**: a digital AGC fixes amplitude, not signal-to-noise. With the
  analogue gain low, a weak burst moves only a few converter codes and the
  quantisation noise is already inside; multiplying by a thousand does not
  give back the bits the converter never used.

  Now **FT8, HFDL, WSPR, AIS, ALE, JTTY, STANAG and IDENT each have their
  own**, saved in `CONFIG.CSV`. What you turn inside a mode stays in that
  mode and does not move your listening gain. The setting's footer says so:
  "this mode only".

  **Factory: HFDL at 35.0 dB and the other seven at the usual 20.0.** The 35
  is a measured number; for the other seven there is no measurement, and
  changing the start value of a mode that works today on the strength of an
  observation about another one is exactly what is not done here.

- **And inside, ten gains stop being ten branches.** There were four chained
  conditions sharing two gains, and adding eight more would have multiplied
  the cases. Instead there are **zones**: listening, wide FM and each of the
  eight digital modes, with **one single function** that saves the live value
  where you were and loads the one where you are.

  This is not cosmetic: this file already had two faults written with that
  same variable, both of them caused by branches looking at each other. The
  `pga_check.py` guard has been rewritten to watch the new property — that
  the zone change has **one single owner** — and checked to go red against
  three faults introduced on purpose.

---

## V6.14 · 2026-10-28

### Qué cambia

- **La fila del mapa dice si se ha quedado sin sitio.** Preguntado por el
  usuario: si se puede optimizar el mapa.

  El mapa no cabe entero en la RAM. Lo que se guarda son **trazos** y no una
  foto —una foto del mundo a esta pantalla son 460.800 bytes y leerla costaría
  370 ms por repintado, cuando el presupuesto de un fotograma son 22—, y aun
  así sólo vive en memoria un trozo: 10.240 bytes. Al máximo zoom, la radio
  carga las casillas que tocan lo que ves, y **si no caben, deja fuera las que
  sobran**. Cuando la que falta es una costa, el relleno —que va por
  par-impar— descuadra la cuenta de cruces y lo que viene detrás sale del
  color contrario hasta el siguiente borde: eso es una raya horizontal.

  La radio contaba esas casillas desde hace doce días y **no las leía nadie**:
  ni se podían consultar ni salían en ninguna pantalla. Ahora sale el peor
  valor de la sesión en **Ajustes → Equipo → Información → Mapa**, y sólo si
  no es cero.

  **Medido sobre los cien mapas, recorriendo todas las ventanas posibles:**

  | | |
  |---|---|
  | peor ventana, sólo lo que se ve | 10.195 B |
  | el buffer | 10.240 B |
  | **margen** | **45 B** |

  O sea que **hoy no falta ninguna** — pero 45 bytes de 10.240 no son un
  margen, son suerte. Un `.map` con algo más de detalle en una sola ventana ya
  empezaría a perder costa, y hasta ahora habría pasado sin que se supiera.

### What's new

- **The map row now says when it has run out of room.** Asked by the user:
  whether the map can be optimised.

  The map does not fit in RAM. What is stored are **outlines**, not a picture
  — a picture of the world at this screen's resolution is 460,800 bytes and
  reading it would cost 370 ms per repaint, against a frame budget of 22 — and
  even so only a slice lives in memory: 10,240 bytes. At maximum zoom the
  radio loads the tiles covering what you see, and **if they do not fit, the
  rest are left out**. When the missing one is a coastline, the fill — which
  works by odd-even — loses its crossing count and everything after it comes
  out in the opposite colour until the next tile edge: that is a horizontal
  streak.

  The radio had been counting those tiles for twelve days and **nobody read
  them**: no way to query them, not shown anywhere. The worst value of the
  session now appears in **Settings → Device → Information → Map**, and only
  when it is not zero.

  **Measured across the hundred maps, sweeping every possible window:**

  | | |
  |---|---|
  | worst window, visible tiles only | 10,195 B |
  | the buffer | 10,240 B |
  | **headroom** | **45 B** |

  So **nothing is missing today** — but 45 bytes out of 10,240 is not headroom,
  it is luck. A `.map` with slightly more detail in a single window would start
  losing coastline, and until now it would have done so silently.

---

## V6.13 · 2026-10-28

### Qué cambia

- **Los modos digitales dejan de aplicar una corrección de imagen que no
  pueden comprobar, y HFDL lo nota.** Avisado por un usuario con dos radios
  en la misma mesa, la misma antena y seis minutos de reloj: la versión
  anterior sacaba el doble de aviones.

  El corrector de imagen de banda ancha corrige sumando una copia
  **conjugada** de la banda encima de la banda. Con sus tomas bien medidas
  eso cancela el espejo del aparato; con las tomas equivocadas no cancela
  nada: **lo fabrica**, encima de lo que se está decodificando. Y corre
  dentro de la interrupción del convertidor, sobre todas las muestras de
  todos los modos.

  El problema es que en los **ocho modos de panel entero** —FT8, HFDL, WSPR,
  AIS, ALE, JTTY, STANAG e IDENT— no se puede volver a medir: el estimador
  vive dentro del dibujo del espectro, y esos modos se quedan la pantalla y
  no pintan espectro. Las tomas se quedaban congeladas en lo que aprendieron
  **en otro modo y en otra banda** y allí seguían corrigiendo, sin que nada
  pudiera comprobar si lo que hacían era bueno.

  Medido de dos formas. Con una ráfaga entera por la cadena de verdad,
  30 ráfagas por punto, BPSK 600 y la señal justa:

  | toma del corrector | 0 | 0,05 | 0,10 | 0,20 | 0,35 | 0,50 |
  |---|---|---|---|---|---|---|
  | ráfagas buenas de 30 | 17 | 13 | 8 | 10 | 9 | **4** |

  Y con un banco nuevo de **flujo largo** —ruido continuo con noventa
  ráfagas metidas dentro, una sola sesión del modo— que es el único que
  puede contar los enganches **perdidos**:

  | toma | A1 | A2 | M1 | buenas |
  |---|---|---|---|---|
  | 0 | 61/90 | 13/90 | 9/90 | 3/3 |
  | 0,50 | **33/90** | 6/90 | **3/90** | 2/2 |

  De 17 a 4, de 61 enganches a 33, y 0,50 es exactamente el tope que el
  propio corrector permite.
  Ahora, al entrar en esos ocho modos, esa parte se olvida. **El corrector
  plano no se toca** —es el que arregló el espejo que se veía, de 25 a 86 dB
  de rechazo— y además sigue midiendo dentro del modo; lo que se deja fuera
  es sólo la parte fina, la que depende de la frecuencia. Al volver a un modo
  con espectro se aprende sola en segundos.

- **Con el RDS puesto, el mando ya llega a la ganancia de entrada sin salir
  de la pantalla del reloj.** Avisado por un usuario. Esa pantalla ocupa la
  barra de abajo con sus propios botones, así que el botón con el que se
  elige qué mueve el mando no está, y la ganancia quedaba fuera de alcance
  justo donde más falta hace. Ahora, **con el RDS elegido**, pulsar el mando
  lo pasa de sintonía a ganancia y al revés, y el renglón de detalle dice en
  todo momento a cuál de las dos apunta y cuántos decibelios hay puestos.

  Sólo con el RDS: las seis emisoras horarias de onda larga tienen frecuencia
  fija y allí ni se sintoniza ni sobra ganancia.

- **ALS162 ya pasa de la mitad de la trama.** Avisado por el usuario con una
  foto: `04/59`, `10/59` y vuelta a empezar. La radio exigía 60 ms de fase
  plana para dar por buena una marca de segundo, y con la portadora sucia esa
  condición no se cumple nunca aunque la marca esté exactamente donde toca.

  **Sólo ALS162**, que es la única que se decodifica por fase. DCF77 y MSF van
  por amplitud —portadora que se apaga y se enciende— y no pasan por esta
  puerta: a esas dos no les cambia nada. Ahora, **con la rejilla de segundos ya enganchada**,
  caer dentro de la ventana de ±70 ms vale igual que el silencio; y un hueco
  sólo se toma por el segundo 59 si antes han entrado 55 bits, en vez de
  tirar la trama entera por una marca perdida.

  Medido en el banco contra la misma señal, seis minutos por caso:

  | marcadores sin silencio | antes | ahora |
  |---|---|---|
  | uno de cada 8 | no engancha (47 raras) | decodifica |
  | uno de cada 4 | no engancha (88 raras) | decodifica |
  | uno de cada 2 | no engancha | no engancha |

- **El mapa ya escribe el nombre de todos los aviones que caben, no de
  cinco.** Avisado por el usuario: doce en la lista, cinco en el mapa.

  Había un tope de etiquetas puesto a ojo —`4 + nivel de zoom`—, así que con
  el mapamundi entero eran **cinco** por mucho que hubiera. Los puntos salían
  todos; lo que faltaba eran los nombres escritos.

  Y ese tope sobraba desde que, en octubre, se le enseñó a cada etiqueta a
  probar **seis colocaciones** antes de rendirse —derecha, izquierda, un
  renglón arriba y otro abajo—. Eso ya resuelve el apelotonamiento midiendo
  píxeles de verdad; el `4 + zoom` se disparaba antes de mirar si la etiqueta
  cabía y tiraba nombres que sí entraban.

  Ahora el único tope es el del buffer, 12, y quien reparte es el choque. En
  el mapamundi se seguirán viendo pocas —ahí Europa entera mide treinta
  píxeles— pero porque no caben, no porque un número lo decidiera antes de
  medir. Vale igual para el mapa de FT8.

### What's new

- **The digital modes stop applying an image correction they cannot check,
  and HFDL notices.** Reported by a user with two radios on the same desk,
  the same antenna and six minutes on the clock: the older version pulled
  twice as many aircraft.

  The wideband image corrector works by adding a **conjugate** copy of the
  band on top of the band. With its taps correctly measured that cancels the
  radio's own mirror; with the taps wrong it cancels nothing — it **creates**
  mirror, on top of whatever is being decoded. And it runs inside the
  converter interrupt, over every sample of every mode.

  The trouble is that in the **eight full-panel modes** — FT8, HFDL, WSPR,
  AIS, ALE, JTTY, STANAG and IDENT — it can never be re-measured: the
  estimator lives inside the spectrum drawing, and those modes take the whole
  screen and draw no spectrum. The taps stayed frozen at whatever they learnt
  **in another mode and another band** and went on correcting there, with
  nothing able to check whether what they did was any good.

  Measured two ways. With a whole burst through the real chain, 30 bursts
  per point, BPSK 600 and the signal marginal:

  | corrector tap | 0 | 0.05 | 0.10 | 0.20 | 0.35 | 0.50 |
  |---|---|---|---|---|---|---|
  | good bursts out of 30 | 17 | 13 | 8 | 10 | 9 | **4** |

  And with a new **long-flow** bench — continuous noise with ninety bursts
  inside it, one single session of the mode — which is the only one that
  can count the detections that were **missed**:

  | tap | A1 | A2 | M1 | good |
  |---|---|---|---|---|
  | 0 | 61/90 | 13/90 | 9/90 | 3/3 |
  | 0.50 | **33/90** | 6/90 | **3/90** | 2/2 |

  From 17 to 4, from 61 detections to 33, and 0.50 is exactly the ceiling
  the corrector itself allows.
  Entering any of those eight modes now forgets that part. **The flat
  corrector is untouched** — it is the one that fixed the mirror you could
  actually see, from 25 to 86 dB of rejection — and it keeps measuring inside
  the mode; what is left out is only the fine, frequency-dependent part. Back
  in a mode with a spectrum it is learnt again by itself in seconds.

- **With RDS selected, the knob now reaches the input gain without leaving
  the clock screen.** Reported by a user. That screen fills the bottom bar
  with its own buttons, so the button that picks what the knob moves is not
  there, and the gain was out of reach exactly where it is most needed. Now,
  **with RDS selected**, pressing the knob swaps it between tuning and gain,
  and the detail line says at all times which of the two it is on and how
  many decibels are set.

  RDS only: the six longwave time stations sit on fixed frequencies, where
  there is nothing to tune and no gain to spare.

- **ALS162 now gets past the middle of the frame.** Reported by the user with
  a photo: `04/59`, `10/59` and back to the start. The radio demanded 60 ms of
  flat phase before accepting a second marker, and with a dirty carrier that
  condition is never met even when the marker is exactly where it should be.

  **ALS162 only**, the one station decoded from phase. DCF77 and MSF are
  decoded from amplitude — carrier switched off and on — and do not go through
  this gate: nothing changes for those two. Now, **once the second grid is locked**, falling inside
  the ±70 ms window counts as much as the silence; and a gap is only taken
  for second 59 if 55 bits have arrived first, instead of throwing the whole
  frame away over one lost marker.

  Measured on the bench against the same signal, six minutes per case:

  | markers with no silence | before | now |
  |---|---|---|
  | one in 8 | no lock (47 odd) | decodes |
  | one in 4 | no lock (88 odd) | decodes |
  | one in 2 | no lock | no lock |

- **The map now writes the name of every aircraft that fits, not of five.**
  Reported by the user: twelve on the list, five on the map.

  There was a label budget set by eye — `4 + zoom level` — so with the whole
  world on screen it was **five**, however many there were. The dots were all
  drawn; what was missing were the written names.

  And that budget had been redundant since October, when each label learnt to
  try **six placements** before giving up — right, left, one row up, one row
  down. That already resolves crowding by measuring real pixels; the
  `4 + zoom` fired before looking at whether the label fitted, and threw away
  names that did.

  Now the only limit is the buffer, 12, and the collision test does the
  sharing. On the world view you will still see few — Europe is thirty pixels
  wide there — but because they do not fit, not because a number decided it
  before measuring. The same applies to FT8's map.

---

## V6.12 · 2026-10-28

### Qué cambia

- **La lista de aviones de HFDL pasa de 20 a 48, como la de FT8.** El número
  que importaba era el de aviones: esa tabla no la lee sólo el panel, **la
  lee el mapa**, así que el mapa no podía enseñar más de veinte por llena
  que estuviera la banda. Es la misma razón por la que FT8 pasó de 16 a 48
  en su día; a HFDL no se le aplicó. Los renglones de la lista suben también
  a 48.

  **El precio: la lista se borra al salir del modo**, igual que ya le pasa a
  la de FT8. A cambio, las dos tablas se mudan a la memoria compartida y
  esto **no cuesta ni un byte**: la ocupación de RAM baja 2.048 bytes.

- **Cuando MSF rechaza una trama, la radio dice por qué.** El decodificador
  tiene nueve puertas de rechazo y ninguna ponía el motivo, así que la
  pantalla enseñaba «mal 7» y se callaba cuál —con DCF77 sí lo dice desde
  siempre—. Ahora salen `patron`, `par-anno`, `par-fec`, `par-hor` y
  `rango`, que son cinco averías distintas con cinco arreglos distintos.

  Esto **no** arregla por sí solo una recepción que no engancha: es lo que
  hacía falta para poder mirarla.

### What's new

- **HFDL's aircraft list goes from 20 to 48, like FT8's.** The number that
  mattered was the aircraft count: that table is not read only by the panel,
  **the map reads it**, so the map could never show more than twenty however
  busy the band was. It is the same reason FT8 went from 16 to 48; HFDL
  never got the same treatment. The list rows go to 48 as well.

  **The price: the list is cleared when you leave the mode**, exactly as
  FT8's already is. In exchange both tables move to the shared memory and
  this **costs nothing**: RAM use drops by 2,048 bytes.

- **When MSF rejects a frame, the radio now says why.** The decoder has nine
  rejection paths and not one of them recorded the reason, so the screen
  showed "mal 7" and stayed quiet about which — with DCF77 it has always
  said. Now it shows `patron`, `par-anno`, `par-fec`, `par-hor` and `rango`:
  five different faults with five different fixes.

  This does **not** by itself fix a reception that will not lock: it is what
  was missing in order to look at it.

---

## V6.11 · 2026-10-28

### Qué cambia

- **La FM ancha ya tiene su propia ganancia de entrada, y son 25 dB.** Hasta
  ahora, entrar en FM ancha ponía el amplificador de entrada **al tope**
  —47,5 dB— y el mando no servía de nada dentro del modo: lo que girases se
  tiraba al salir. El razonamiento era que a 100 MHz el mezclador pierde unos
  25 dB y «hace falta todo lo que hay». Eso vale con una antena de hilo y una
  emisora lejana; con una antena decente y una local, satura.

- **Y si la cambias, se guarda.** Son dos ajustes separados, uno por familia
  de modo:

  | | dónde manda | se guarda como |
  |---|---|---|
  | tu ganancia de siempre | fuera de FM ancha | `pga_gain_db_x2` |
  | tu ganancia de FM ancha | dentro de FM ancha | `pga_wfm_db_x2` |

  Entrar en FM ancha pone la segunda, salir repone la primera, y el mando
  escribe la que corresponda al modo en el que estés. Ninguna pisa a la otra.
  Un `CONFIG.CSV` de una versión anterior no trae la nueva clave: entonces la
  FM ancha arranca con los 25 dB de serie.

- **La hora por radio ya no se pone en hora alemana.** DCF77 y ALS162 **no
  emiten UTC**: emiten hora local alemana y francesa (CET en invierno, CEST
  en verano). El decodificador guardaba la trama tal cual, así que el reloj
  se ponía en hora de Europa central pasara lo que pasara y **el ajuste de
  huso no lo miraba nadie**. En España no se nota, porque la península *es*
  CET; fuera, la hora salía adelantada y cambiar el huso no servía de nada.

  La misma trama, 20:33 CEST, con el huso puesto en cada sitio:

  | huso | antes | ahora |
  |---|---|---|
  | España/Alemania (+120) | 20:33 | 20:33 |
  | Dublín, verano (+60) | 20:33 | **19:33** |
  | UTC (+0) | 20:33 | **18:33** |

  De paso, el botón que **mide** el huso ya funciona también con estas dos
  emisoras; antes sólo con MSF.

- **Los indicativos franceses ya sacan país.** La tabla tenía para Francia
  un solo prefijo, `F`, mientras los demás países grandes llevaban su bloque
  entero. En FT8 eso dejaba sin país a `TM8R` y a cualquier indicativo
  especial francés, que son constantes. Añadidos `HW`, `HX`, `HY`, `TH`,
  `TM`, `TP`, `TQ` y `TV`.

- **El mapa de FT8 se arrastra con fluidez.** Mover el mapa con el dedo
  repintaba los 800×288 enteros **en cada muestra del táctil**, y el táctil
  se mira dos veces por vuelta: hasta dos repintados completos por vuelta
  de los que sólo se veía el último. Ahora el arrastre acumula y se pinta
  una sola vez por vuelta.

### What's new

- **Wide FM now has its own input gain, and it is 25 dB.** Until now,
  entering wide FM set the input amplifier to **maximum** —47.5 dB— and the
  knob did nothing inside the mode: whatever you turned was discarded on the
  way out. The reasoning was that the mixer loses about 25 dB at 100 MHz and
  "all of it is needed". That holds with a wire antenna and a distant
  station; with a decent antenna and a local one, it clips.

- **And if you change it, it is kept.** They are two separate settings, one
  per mode family:

  | | where it applies | stored as |
  |---|---|---|
  | your usual gain | outside wide FM | `pga_gain_db_x2` |
  | your wide-FM gain | inside wide FM | `pga_wfm_db_x2` |

  Entering wide FM applies the second, leaving restores the first, and the
  knob writes whichever matches the mode you are in. Neither overwrites the
  other. A `CONFIG.CSV` from an earlier release does not carry the new key:
  wide FM then starts at the default 25 dB.

- **Radio time no longer comes out on German time.** DCF77 and ALS162 do
  **not** transmit UTC: they transmit German and French local time (CET in
  winter, CEST in summer). The decoder stored the frame as received, so the
  clock was set to Central European time whatever you did, and **the
  timezone setting was never looked at**. It does not show in Spain, which
  *is* CET; elsewhere the time ran ahead and changing the timezone did
  nothing.

  The same frame, 20:33 CEST, with the timezone set to each place:

  | timezone | before | now |
  |---|---|---|
  | Spain/Germany (+120) | 20:33 | 20:33 |
  | Dublin, summer (+60) | 20:33 | **19:33** |
  | UTC (+0) | 20:33 | **18:33** |

  The button that *measures* the timezone now works with these two stations
  as well; before, only with MSF.

- **French callsigns now show a country.** The table held a single prefix
  for France, `F`, while every other large country carried its whole block.
  In FT8 that left `TM8R` — and every French special-event callsign, which
  are constant — with an empty country column. Added `HW`, `HX`, `HY`,
  `TH`, `TM`, `TP`, `TQ` and `TV`.

- **The FT8 map drags smoothly.** Dragging the map repainted the full
  800×288 **on every touch sample**, and the touch is read twice per loop:
  up to two full repaints per pass, of which only the last was ever seen.
  The drag now accumulates and paints once per pass.

---

## V6.10 · 2026-10-28

### Qué cambia

- **FT8 mira ahora la banda entera: de 0 a 3.200 Hz, no hasta 1.600.** Hasta
  esta versión el decodificador analizaba la mitad de lo que entra por el
  audio, y lo de arriba no es que se decodificara peor: es que no se miraba.
  Con veinticinco emisiones repartidas por la banda, el banco de pruebas que
  compila el código de la radio leía trece; ahora lee veinte, y la última
  que se lee pasa de 1.500 Hz a 3.000.

  Esto es lo que había detrás de «oigo pitidos de FT8 y no sale nada»: si la
  estación estaba por encima de 1.600 Hz de audio, no había nada que hacer
  más que sintonizar alrededor.

- **Sin perder sensibilidad.** Medido contra la versión anterior con las
  mismas señales y las mismas semillas:

  | | antes | ahora |
  |---|---|---|
  | última emisión que se lee | 1.500 Hz | **3.000 Hz** |
  | de 25 repartidas por la banda | 13 | **20** |
  | rejilla de medio bin | 8 8 8 5 0 7 8 8 | 8 8 8 5 0 7 8 8 |
  | sensibilidad (−18 dB, 8 semillas) | 6 de 8 | 6 de 8 |
  | relación S/R: error peor | 4,0 dB | **1,0 dB** |

  La relación señal/ruido que sale en la lista es además más fiel que antes,
  porque ya no se queda en el escalón más cercano: interpola dentro de él.

- **Y sin pedirle más memoria a la radio.** La RAM libre pasa de 1.316 a
  1.500 bytes y la flash libre de 1.720 a 4.632. Para que cupiera, el fax, el
  SSTV y WSPR pasan a usar la misma memoria prestada que ya usaban FT8, HFDL,
  AIS, ALE, DSC, HELL, IDENT, JTTY y STANAG: son modos que no pueden estar
  encendidos a la vez, así que no tiene sentido que cada uno tenga su trozo
  reservado todo el día.

### What's new

- **FT8 now looks at the whole band: 0 to 3,200 Hz, not just up to 1,600.**
  Until this release the decoder analysed half of what comes in through the
  audio, and everything above that was not decoded badly — it was not looked
  at. With twenty-five signals spread across the band, the test bench that
  compiles the radio's own code read thirteen; it now reads twenty, and the
  highest one read goes from 1,500 Hz to 3,000.

  This is what was behind "I can hear FT8 tones and nothing comes out": if the
  station sat above 1,600 Hz of audio there was nothing to be done but tune
  around it.

- **With no loss of sensitivity.** Measured against the previous release with
  the same signals and the same seeds:

  | | before | now |
  |---|---|---|
  | highest signal read | 1,500 Hz | **3,000 Hz** |
  | of 25 spread across the band | 13 | **20** |
  | half-bin grid | 8 8 8 5 0 7 8 8 | 8 8 8 5 0 7 8 8 |
  | sensitivity (−18 dB, 8 seeds) | 6 of 8 | 6 of 8 |
  | SNR readout: worst error | 4.0 dB | **1.0 dB** |

  The signal-to-noise figure shown in the list is also more faithful than
  before, because it no longer settles for the nearest step: it interpolates
  within it.

- **And without asking the radio for more memory.** Free RAM goes from 1,316
  to 1,500 bytes and free flash from 1,720 to 4,632. To make it fit, WEFAX,
  SSTV and WSPR now use the same borrowed memory that FT8, HFDL, AIS, ALE,
  DSC, HELL, IDENT, JTTY and STANAG already shared: these modes cannot be on
  at the same time, so there is no point in each keeping its own slice
  reserved all day.

---

## V6.09 · 2026-10-28

### Qué cambia

- **Los nombres de serie de los bancos ya salen en MEM.CSV.** Antes sólo se
  escribía un renglón `nombre,` para los bancos a los que ya les habías
  puesto uno, así que en un fichero recién hecho no salía ninguno y quien
  lo abría no tenía forma de saber que eso se cambia ahí.

  Ahora salen los cuatro siempre, con su renglón de ayuda encima:

  ```
  banco,memoria,hz,modo,paso_hz,fil_lo,fil_hi,cw
  # Para ponerle nombre a un banco, quita la almohadilla y escribe el tuyo:
  # nombre,1,HF
  # nombre,2,Aviones
  # nombre,3,Marina
  # nombre,4,Repetidores
  1,1,9690000,0,1000,100,4000,0
  ```

  **Comentados a propósito.** Los nombres de serie están traducidos
  —«Aviones» / «Aircraft»—, así que escribirlos como renglones vivos los
  congelaría en el idioma que hubiera puesto el día que se grabó el
  fichero: cambiabas la radio a inglés y seguías viendo «Aviones». Con la
  almohadilla se ven, se entiende que se tocan, y no cambian nada mientras
  no se la quites. El que tú pongas se escribe vivo, sin almohadilla.

### What's new

- **The banks' default names now appear in MEM.CSV.** Until now a `nombre,`
  row was only written for banks you had already renamed, so a freshly
  written file had none at all and whoever opened it had no way of knowing
  that is where you change them.

  Now all four are always written, with a hint line above them:

  ```
  banco,memoria,hz,modo,paso_hz,fil_lo,fil_hi,cw
  # To name a bank, remove the hash and write your own:
  # nombre,1,HF
  # nombre,2,Aircraft
  # nombre,3,Marine
  # nombre,4,Repeaters
  1,1,9690000,0,1000,100,4000,0
  ```

  **Commented out on purpose.** The default names are translated —«Aviones»
  / «Aircraft»— so writing them as live rows would freeze them in whatever
  language was set the day the file was written: switch the radio to
  English and you would still see «Aviones». Hashed out they are visible,
  it is obvious they can be changed, and they change nothing until you
  remove the hash. A name you set is written live, without the hash.

---

## V6.08 · 2026-10-28

### Qué cambia

- **FT8: arreglado que un parón del bucle principal tirara el ciclo
  entero.** Era lo de "oigo los pitidos de la ranura y no sale nada".

  La captura de una ranura son **93 bloques de 160 ms = 14,880 s** dentro
  de una ranura de **15 s**: **120 ms de margen, menos de un bloque**. Y el
  remuestreador tiene un colchón de 320 ms (dos buffers), así que cualquier
  parón del bucle principal más largo que eso se come 160 ms de audio, la
  ranura se queda en 92 bloques de 93 y **se tiraba entera**, en silencio.

  Ahora, si la ranura que se acaba tiene bloques de sobra para que quepa un
  mensaje, **se decodifica con los que tenga** en vez de tirarla. Ft8Lib ya
  lo admite: pone a cero las verosimilitudes de los símbolos que falten y
  el LDPC se los come como borrados. Una emisión ocupa 79 bloques de los
  93. Medido en el banco nuevo: con un parón de 600 ms, antes salían **0
  líneas** y ahora salen **3**.

  Y la chapa lleva ahora la cuenta de las ranuras que se decodificaron
  cortas, que antes no la decía nadie.

### What's new

- **FT8: fixed a main-loop stall throwing away the whole cycle.** This was
  the "I hear the slot's tones and nothing comes out".

  A slot's capture is **93 blocks of 160 ms = 14.880 s** inside a **15 s**
  slot: **120 ms of margin, less than one block**. The resampler's cushion
  is 320 ms (two buffers), so any main-loop stall longer than that eats
  160 ms of audio, the slot ends at 92 blocks out of 93, and **it was
  thrown away whole**, silently.

  Now, if the slot that is ending has enough blocks for a message to fit,
  **it is decoded with the blocks it has** instead of being discarded.
  Ft8Lib already supports this: it zeroes the likelihoods of the missing
  symbols and the LDPC treats them as erasures. A transmission occupies 79
  of the 93 blocks. Measured in the new bench: with a 600 ms stall, before
  it produced **0 lines** and now it produces **3**.

  The badge now also counts the slots decoded short, which nothing used to
  report.

---

## V6.07 · 2026-10-27

### Qué cambia

- **Arreglado: por debajo de 300 kHz la raya del dial iba hacia delante y
  hacia atrás dando saltos** mientras subías de frecuencia, sin llegar a
  ninguna parte. Visto en onda larga con paso de 1 kHz.

  No era un fallo de dibujo. Ahí abajo el oscilador no lo hace el
  sintetizador sino **un temporizador del micro**, que sólo puede dar
  `99,84 MHz / (2·N)` con N entero y par: en onda larga su escalón son
  **1.421 Hz** —3.668 pegado a los 300 kHz— mientras el dial va de 1.000
  en 1.000. Pidiéndole «el dial menos 24 kHz» en cada clic, **no puede
  obedecer**: redondea. El audio no se entera, porque el desplazamiento
  digital se calcula contra el oscilador real y el dial sale exacto. La
  pantalla sí: el panel se centra en el oscilador, así que el redondeo se
  colaba en el panel y en la marca. Medido: avanzaba 3,9 píxeles,
  retrocedía 1,7, retrocedía 1,8, avanzaba 3,9.

  **Se arregla en la pantalla, no en el oscilador.** El panel pasa a
  centrarse en lo que se *pidió* —que sigue al dial exactamente— y los
  datos del espectro y la cascada se giran los pocos bins del redondeo
  para que sigan cayendo en su frecuencia. Resultado: la raya se queda
  **clavada**, que es lo correcto con el oscilador siguiendo al dial, y lo
  que se desplaza es el espectro por debajo. En toda la banda, con paso de
  1 kHz, de **119 retrocesos de 239 clics a cero**.

  **La radio sintoniza exactamente igual que siempre** y el NCO sigue
  siendo tuyo: no se toca ni el oscilador ni el ajuste.

  Lo que queda sin corregir es el redondeo a bin entero: menos de medio
  bin, **0,37 píxeles de los 756**. Sin el giro, la emisora se habría ido
  hasta 2,9 píxeles de debajo de la raya, que es peor que el temblor.

### What's new

- **Fixed: below 300 kHz the dial marker lurched forwards and backwards**
  as you tuned up, never getting anywhere. Seen on longwave with a 1 kHz
  step.

  It was not a drawing bug. Down there the oscillator is not made by the
  synthesiser but by **a timer inside the micro**, which can only produce
  `99.84 MHz / (2·N)` with N an even integer: on longwave its step is
  **1,421 Hz** —3,668 right at 300 kHz— while the dial moves in steps of
  1,000. Asked for "the dial minus 24 kHz" on every click, it **cannot
  comply**: it rounds. The audio never notices, because the digital shift
  is computed against the real oscillator and the dial comes out exact.
  The screen did: the panel centres on the oscillator, so the rounding
  leaked into both the panel and the marker. Measured: forward 3.9 pixels,
  back 1.7, back 1.8, forward 3.9.

  **The fix is in the screen, not in the oscillator.** The panel now
  centres on what was *asked for* — which follows the dial exactly — and
  the spectrum and waterfall data are rotated by the few bins of the
  rounding so they keep landing on their real frequency. The result: the
  marker sits **still**, which is what it should do with the oscillator
  following the dial, and what moves is the spectrum underneath. Across
  the whole band at a 1 kHz step, from **119 backward steps out of 239
  clicks to none**.

  **The radio tunes exactly as it always did** and the NCO is still yours:
  neither the oscillator nor the setting is touched.

  What is left uncorrected is the rounding to a whole bin: less than half
  a bin, **0.37 pixels out of 756**. Without the rotation the station
  would have drifted up to 2.9 pixels out from under the marker, which is
  worse than the twitch.

---

## V6.06 · 2026-10-27

### Qué cambia

- **El barrido de memorias sigue solo cuando el canal se queda libre.**
  Antes se paraba del todo donde encontraba a alguien y había que volver a
  arrancarlo a mano. Ahora se queda escuchando mientras haya alguien y
  sigue por donde iba cuando el canal lleva **dos segundos y medio**
  callado. Una pausa entre dos frases de la misma conversación no le mueve
  el dial. Y para quedarte donde estás sigue estando el gesto de siempre:
  **tocar MEM lo para del todo**.

- **Y da la vuelta tres veces más rápido: 400 ms por memoria en vez de
  1.250.** Era lo que hacía que en los canales de servicio se perdiera las
  frases cortas, porque lo que decide si cazas una frase no es lo que mira
  cada memoria sino **lo que tarda la vuelta entera**: con quince memorias
  eran 18,7 segundos y ahora son 6. Los dos tiempos —lo que espera a que la
  cadena se asiente y lo que mira después— estaban puestos a ojo y ahora
  están **medidos**: la lectura se asienta en 109 ms en el peor modo, y
  mirando un cuarto de segundo se caza a alguien hablando las 24 veces de
  24 sin inventarse ni una emisora. El segundo entero de antes no compraba
  nada.

- **Arreglado: un banco que mezcle FM ancha con cualquier otro modo se
  paraba en todas las memorias que cambiaban de modo**, estuvieran vacías o
  no. En FM ancha el barrido busca el ruido más bajo y en los demás el
  nivel más alto, y la medida se arrancaba mirando el modo *anterior*. No
  se había visto nunca en la radio.

### What's new

- **Memory scan resumes on its own once the channel goes free.** It used to
  stop for good wherever it found someone, and you had to start it again by
  hand. Now it stays and listens while anyone is there, and carries on from
  where it was once the channel has been quiet for **two and a half
  seconds**. A pause between two overs of the same exchange does not move
  the dial. To stay put there is still the gesture there has always been:
  **tapping MEM stops it for good**.

- **And a full cycle is three times faster: 400 ms per memory instead of
  1,250.** This is what made it miss short phrases on service channels,
  because what decides whether you catch a phrase is not the dwell on each
  memory but **how long the whole cycle takes**: with fifteen memories that
  was 18.7 seconds and it is now 6. Both times — the settle after a hop and
  the dwell after it — were set by eye and are now **measured**: the reading
  settles in 109 ms in the worst mode, and a quarter-second dwell catches
  someone talking 24 times out of 24 without inventing a single station.
  The full second bought nothing.

- **Fixed: a bank mixing wide FM with any other mode stopped on every
  memory that changed mode**, empty or not. In wide FM the scan looks for
  the *lowest* noise and in the other modes for the *highest* level, and the
  measurement was being armed against the *previous* mode. This had never
  been seen on the radio.

---

## V6.05 · 2026-10-27

### Qué cambia

- **Paso de sintonía de 9 kHz**, que es el canalizado de onda media en
  Europa. Entra en la lista entre los 5 y los 10 kHz, donde le toca, y sale
  en la pantalla de Paso como una más.

- **Y de paso, MEM.CSV guarda el paso en hercios y no por su número de
  orden.** Esto no se ve pero es lo que hace que lo de arriba no te rompa
  nada: metiendo los 9 kHz en medio de la lista, todos los pasos por
  encima se corren un puesto, y tus memorias ya grabadas habrían vuelto
  con **otro paso** — una de marina guardada en 12,5 kHz habría vuelto en
  10. CONFIG.CSV ya lo hacía así desde hace tiempo y lo explicaba con
  todas las letras; MEM.CSV se había quedado sin esa lección.

  Los ficheros que ya tengas **se siguen leyendo bien**: un número pequeño
  se entiende como el orden de antes y uno grande como hercios, y no hay
  ambigüedad posible porque el paso más pequeño son 10 Hz. La columna
  pasa a llamarse `paso_hz`, así que editando el fichero a mano ahora se
  ve lo que pone: `9000` en vez de `5`.

### What's new

- **A 9 kHz tuning step**, the mediumwave channel spacing in Europe. It
  goes into the list between 5 and 10 kHz, where it belongs, and shows up
  on the Step screen like any other.

- **And MEM.CSV now stores the step in hertz rather than by its position
  in the list.** You do not see this, but it is what keeps the above from
  breaking anything: inserting 9 kHz in the middle of the list shifts
  every coarser step up by one, and your stored memories would have come
  back with **a different step** — a marine memory saved at 12.5 kHz would
  have returned at 10. CONFIG.CSV has worked that way for a while and says
  so explicitly; MEM.CSV had missed the lesson.

  Files you already have **still read correctly**: a small number is taken
  as the old position and a large one as hertz, and there is no ambiguity
  because the smallest step is 10 Hz. The column is now called `paso_hz`,
  so hand-editing the file shows what it means: `9000` instead of `5`.

---

## V6.04 · 2026-10-27

### Qué cambia

- **136 bytes más de flash interna**, de 2.668 a 2.804 libres. `aic3204.c`
  estaba en la lista de ficheros que se compilan con `-O2` en vez de
  `-Os`, y no pintaba nada ahí: no toca ni una muestra de audio ni un
  píxel, configura el códec por I²C y se calla. Corre al arrancar y al
  cambiar de tasa, y los dos ya pagan un reset por patilla con 100 ms de
  espera por delante. No cambia nada de lo que oyes ni de lo que ves.

### What's new

- **136 more bytes of internal flash**, from 2,668 to 2,804 free.
  `aic3204.c` was on the list of files built with `-O2` instead of `-Os`,
  and it had no business being there: it touches neither an audio sample
  nor a pixel, it configures the codec over I²C and goes quiet. It runs at
  boot and on a rate change, and both already pay a pin reset with a 100 ms
  wait in front of them. Nothing you hear or see changes.

---

## V6.03 · 2026-10-27

### Qué cambia

- **El avisador de armónicos ya sabe nombrar lo que de verdad te entra.**
  La fila de Armónicos de Información leía la tabla de bandas, y esa tabla
  no tenía **ni una sola** banda aérea ni marítima: entre 2.495 y 3.200,
  entre 5.367 y 5.800 y entre 7.878 y 9.400 kHz no había nada que nombrar.
  Y duele justo ahí, porque esos servicios emiten **las veinticuatro
  horas** y son fuertes —son los que producen los fantasmas que uno se
  encuentra de verdad, mientras que las de radiodifusión van y vienen con
  la propagación—. Con el dial en 1.848,5 kHz entra Shannon Volmet por el
  tercer armónico, y la radio contestaba «ninguno».

  Ahora conoce **quince rangos más**: aeronáutico y marítimo de onda corta,
  de 2 a 18 MHz. No son presets de banda —la rejilla no pagina y Utilidades
  está casi llena—, y no hace falta: para nombrar un armónico bastan un
  rango y un nombre.

### What's new

- **The harmonic warning can now name what actually reaches you.** The
  Harmonics row in Information read the band table, and that table held
  **not one** aeronautical or maritime band: between 2,495 and 3,200,
  between 5,367 and 5,800 and between 7,878 and 9,400 kHz there was nothing
  to name. And that is exactly where it hurts, because those services
  transmit **around the clock** and are strong — they are the ones that
  produce the ghosts you actually run into, while the broadcast bands come
  and go with propagation. With the dial on 1,848.5 kHz Shannon Volmet
  comes in through the third harmonic, and the radio answered «none».

  It now knows **fifteen more ranges**: HF aeronautical and maritime, from
  2 to 18 MHz. They are not band presets — the grid does not paginate and
  Utility is nearly full — and they do not need to be: naming a harmonic
  takes a range and a name.

---

## V6.02 · 2026-10-27

### Qué cambia

Una revisión a fondo de las señales espejo y los armónicos, en cuatro
pasadas y unos cuarenta arreglos. Lo que más se nota:

- **Tocar la fila de «Cuadratura I/Q» podía encender los dos osciladores a
  la vez.** Por debajo de 300 kHz el oscilador lo hace un temporizador del
  GD32, y se guarda su frecuencia *real* para que el dial no mienta; pero
  esa frecuencia va en cuentas enteras forzadas a par y **puede pasarse del
  cruce**. En el dial entre 322,9 y 324,0 kHz, las pantallas de Información
  creían que mandaba el MS5351 y lo encendían encima del temporizador: dos
  excitadores en contrafase sobre el mismo nodo, la cuadratura destruida y
  espurias por toda la pantalla, sin que nadie volviera a apagarlo.

- **La maniobra de 90 grados de la banda baja salía corta, siempre, por dos
  motivos distintos.** Uno: `SystemCoreClock / 1000000` truncaba 199,68 a
  199, así que los 62,5 ms eran 62,287. Otro, mayor: el cronómetro
  arrancaba *después* de la escritura por I²C y se detenía *antes* de la
  siguiente, así que al cuarto de vuelta se le sumaba una transacción
  entera. Los dos iban siempre en el mismo sentido y ponían techo al
  rechazo de imagen en toda la banda de 300 kHz a 4,8 MHz.

- **El avisador de armónicos nombraba al sospechoso equivocado, y en el eje
  equivocado.** Recortaba la lista por número de armónico y la ordenaba por
  nivel *después*, así que cuatro bandas en el x2 dejaban fuera al x3 —que
  está 20 dB más fuerte—. Y daba el desvío en radiofrecuencia cuando lo que
  se lee es la pantalla: el 3.º y el 7.º salen con la banda lateral dada la
  vuelta. Ahora acierta, y un armónico **par** sale con **±** porque aparece
  por los dos lados a la vez. De regalo: con el eje bien puesto, **la prueba
  del movimiento sí distingue un armónico de una imagen** —la imagen va al
  doble y el 3.er armónico al cuádruple—, justo al revés de lo que decía el
  manual de la propia radio.

- **El corrector de espejo podía acabar fabricando espejo.** Su lazo se
  abría con el corrector apagado y seguía sumando; la frecuencia de cada
  ranura se iba al infinito a los cinco minutos y medio de escuchar una
  emisora fija; no se tiraba nada al cambiar de banda ni al cambiar de
  oscilador; y con el analizador de audio puesto medía el espejo sobre el
  espectro de **audio**. Todo eso está cerrado, y de paso dejó de correr en
  cada cuadro de pantalla cuando no hace falta.

- **El notch automático ya no se mete en CW ni en los digitales.** Busca un
  tono sostenido y lo mata, que es exactamente lo que es una señal de CW o
  la marca de un RTTY: medido, dejaba la marca 12 dB por debajo. Y como los
  decodificadores comen el audio *antes* del notch, la pantalla seguía
  decodificando mientras el altavoz ya no tenía señal. Tu ajuste no se
  toca: al volver a la voz, el notch sigue como lo dejaste.

- **SAM rechaza la banda contraria entre 4 y 7 dB mejor.** El espejo nacía
  dentro del PLL —el lazo seguía en parte la modulación y la reinyectaba al
  otro lado—, así que se estrecha de 200 a 100 Hz. El número está medido,
  no elegido: con 50 Hz se gana más pero el lazo deja de engancharse a una
  portadora descolocada, y eso sí se nota.

- **Banda lateral arranca con el corte bajo en 300 Hz** y no en cero. Por
  debajo de 400 Hz el método de fases apenas rechaza la banda contraria
  —27 dB a 300 Hz, 12 a 150—, así que una emisora pegada al dial *por el
  lado malo* entraba como retumbe. Sigue siendo ajustable; lo que cambia es
  de dónde parte. AM y FM se quedan como estaban.

- **El interpolador de media banda** recalculaba una trama por gusto y, con
  un bloque corto, escribía fuera del buffer.

- **Y una treintena de números y comentarios que mentían**, varios de ellos
  justo donde alguien iría a mirar para arreglar otra cosa: el escalón del
  oscilador estaba a la mitad, el error del arcotangente rápido era el del
  estimador anterior (dieciséis veces peor), la constante de tiempo del
  corrector decía un segundo donde son once, y un comentario del códec
  afirmaba que un canal estaba muerto cuando es por donde entra la señal.

### What's new

A deep review of image signals and harmonics, over four passes and some
forty fixes. The ones you will notice:

- **Tapping the «I/Q quadrature» row could switch both oscillators on at
  once.** Below 300 kHz the oscillator is a GD32 timer, and its *real*
  frequency is stored so the dial does not lie; but that frequency comes in
  whole counts forced even and **can overshoot the crossover**. With the
  dial between 322.9 and 324.0 kHz the Information screens believed the
  MS5351 was in charge and switched it on top of the running timer: two
  push-pull drivers on one node, quadrature destroyed, spurs across the
  display, and nothing ever switched it back off.

- **The low band's 90-degree manoeuvre always came up short**, for two
  separate reasons. One: `SystemCoreClock / 1000000` truncated 199.68 to
  199, so 62.5 ms were really 62.287. The other, larger: the stopwatch
  started *after* the I²C write and stopped *before* the next one, so a
  whole transaction was added to the quarter turn. Both leaned the same
  way, capping image rejection across 300 kHz to 4.8 MHz.

- **The harmonic warning named the wrong suspect, on the wrong axis.** It
  truncated the list by harmonic number and sorted by level *afterwards*,
  so four bands on the 2nd pushed out the 3rd — which is 20 dB stronger.
  And it gave the offset in RF when what you read is the screen: the 3rd
  and 7th come out with the sideband flipped. It is right now, and an
  **even** harmonic is shown with **±** because it appears on both sides at
  once. A bonus: with the axis right, **the movement test does tell a
  harmonic from an image** — the image moves at twice the dial, the 3rd
  harmonic at four times — the exact opposite of what the radio's own notes
  claimed.

- **The image corrector could end up manufacturing image.** Its loop opened
  when the corrector was off and kept accumulating; each slot's frequency
  ran away after five and a half minutes on one station; nothing was
  discarded on a band change or an oscillator change; and with the audio
  analyser on it measured image rejection against the **audio** spectrum.
  All closed, and it no longer runs every display frame when it is not
  needed.

- **The auto-notch stays out of CW and the digital modes.** It hunts a
  sustained tone and kills it, which is exactly what CW or an RTTY mark is:
  measured, it left the mark 12 dB down. And since the decoders are fed
  *before* the notch, the screen kept decoding while the speaker had
  nothing. Your setting is untouched: back on voice, the notch is as you
  left it.

- **SAM rejects the opposite sideband 4 to 7 dB better.** The image was
  born inside the PLL — the loop partly tracked the modulation and fed it
  back to the other side — so it narrows from 200 to 100 Hz. The figure is
  measured, not picked: 50 Hz gains more but the loop stops acquiring an
  offset carrier, and that you would notice.

- **Sideband now starts with the low cut at 300 Hz** instead of zero. Below
  400 Hz the phasing method barely rejects the opposite sideband — 27 dB at
  300 Hz, 12 at 150 — so a station sitting next to the dial *on the wrong
  side* came through as rumble. Still adjustable; only the starting point
  changes. AM and FM are untouched.

- **The half-band interpolator** recomputed one frame for nothing and, with
  a short block, wrote past the end of its buffer.

- **And thirty-odd numbers and comments that lied**, several of them
  exactly where someone would look to fix something else: the oscillator's
  step was stated at half its real value, the fast arctangent's published
  error was the previous estimator's (sixteen times worse), the corrector's
  time constant said one second where it is eleven, and a codec comment
  claimed a channel was dead when it is the one the signal comes in on.

---

## V6.01 · 2026-10-26

### Qué cambia

Dos fallos de la V6.00, los dos vistos en la radio.

- **La radio se saturaba y dejaba de responder al entrar en STANAG** con
  una señal enganchada. Era la medida de nivel que la V6.00 puso para el
  barrido: 256 multiplicaciones y un logaritmo por bloque, **375 veces por
  segundo y en todos los modos, estuvieras barriendo o no**. Por sí sola
  no es mucha cuenta; el problema es dónde caía, porque los
  decodificadores pesados van ya justos dentro de ese mismo bloque y lo
  que sobra se come su margen. Ahora **sólo se mide mientras el barrido
  anda**: se enciende al tocar SCAN y se apaga al parar, y fuera de ahí no
  cuesta ni una instrucción más que mirar una bandera.

- **El barrido se paraba al llegar a la segunda vuelta.** Había **un solo
  suelo de ruido para el banco entero**, el más bajo de sus quince
  memorias: la primera vuelta lo aprendía y la segunda decidía con él, por
  eso caía justo ahí. Y no podía funcionar, por un motivo físico: **el
  ruido de banda no vale lo mismo en 3 MHz que en 28**. La diferencia
  entre la banda más tranquila del banco y la más ruidosa pasa de veinte
  decibelios con facilidad, más del doble del margen, así que la memoria
  más tranquila fijaba el suelo y **cualquier memoria de una banda más
  ruidosa sobresalía diez decibelios sin que allí hubiera nadie**. Medido:
  ruido contra ruido, 300 cuentas frente a 1.800, **15,1 dB de diferencia**
  — el barrido se paraba en eso.

  Ahora hay **un suelo por memoria**, que es la única referencia que
  significa algo: el ruido de *esa* frecuencia. Se aprende de la parte
  callada de cada visita, no del pico, y eso trae dos cosas: **ya se puede
  parar en la primera vuelta** —en la primera visita la cuenta es «cuánto
  sobresale el pico de las pausas», que es justo la prueba de si alguien
  está hablando— y una memoria que estuviera ocupada al empezar **se cura
  sola**, porque el suelo baja en el acto en cuanto la pilla callada. Sube
  despacio, medio decibelio por visita, para que una portadora permanente
  no deje la memoria marcada para siempre. Medido en una visita de un
  segundo: el ruido se mueve **1,0 a 1,6 dB** y alguien hablando, **27 a
  32**.

### What's new

Two bugs from V6.00, both found on the radio.

- **The radio bogged down and stopped responding when entering STANAG**
  with a signal locked. It was the level measurement V6.00 added for the
  scan: 256 multiplies and a logarithm per block, **375 times a second and
  in every mode, whether you were scanning or not**. On its own that is
  not much arithmetic; the problem is where it landed, because the heavy
  decoders are already tight inside that same block and anything spare
  eats their margin. It is now **only measured while the scan is running**:
  on when you tap SCAN, off when it stops, and outside that it costs no
  more than testing a flag.

- **The scan stopped once it reached the second round.** There was **a
  single noise floor for the whole bank**, the lowest of its fifteen
  memories: the first round learned it and the second decided with it,
  which is exactly when it failed. And it could not work, for a physical
  reason: **band noise is not the same at 3 MHz as at 28**. The gap
  between the quietest and the noisiest band in a bank easily exceeds
  twenty decibels, more than twice the margin, so the quietest memory set
  the floor and **any memory in a noisier band stood ten decibels clear
  with nobody there**. Measured: noise against noise, 300 counts versus
  1,800, **15.1 dB apart** — that is what the scan was stopping on.

  There is now **one floor per memory**, the only reference that means
  anything: the noise of *that* frequency. It is learned from the quiet
  part of each visit rather than from the peak, and that brings two
  things: **it can now stop on the first round** —on the first visit the
  sum becomes "how far the peak stands above the pauses", which is exactly
  the test for someone talking— and a memory that was busy when the scan
  started **heals itself**, because the floor drops at once as soon as it
  catches it quiet. It rises slowly, half a decibel per visit, so a
  permanent carrier does not mark a memory as busy forever. Measured over
  a one-second visit: noise moves **1.0 to 1.6 dB**, someone talking,
  **27 to 32**.

---

## V6.00 · 2026-10-26

### Qué cambia

El barrido de memorias de V5.99 **no se paraba donde había alguien
hablando**, y esta versión es eso: que lo haga.

- **El barrido ya distingue una emisora del ruido.** Lo que miraba estaba
  mal medido en tres de los cinco modos, y de dos maneras distintas. En
  **AM** miraba el audio, y una portadora sin modular no da audio —la come
  el bloqueador de continua—, así que una emisora callada leía igual que
  el ruido: 4,3 decibelios de diferencia cuando hacen falta diez. En **FM
  estrecha** era peor, era **al revés**: el ruido de un canal vacío sale
  por el discriminador como un siseo fuerte y una portadora limpia lo
  calla, así que la emisora leía **30,7 decibelios por debajo** del ruido
  y el barrido se habría parado justo en los canales vacíos. Ahora en AM y
  en FM estrecha mira el **nivel de radiofrecuencia del canal**, que sube
  con la portadora module o no module, y en banda lateral y CW —donde ese
  nivel no existe— mira el audio, ya medido **dentro del paso de audio** y
  no sobre un ancho mucho mayor, que era lo que dejaba una voz a ras del
  umbral. Medido modo por modo en `deepsdr-ui/sim/scan_nivel.c`: una
  emisora floja —600 cuentas sobre ruido de 300— sobresale entre **12,7 y
  17,6 decibelios** en los cinco modos, y dos sitios con sólo ruido leen
  igual con menos de **medio decibelio** de diferencia.

- **Tocar una memoria que ya tiene frecuencia te devuelve a la pantalla
  principal**, como ya hacían las bandas y los modos: has elegido dónde
  escuchar, lo que quieres ver es el espectro. La casilla **vacía** no
  cierra —no hay nada que recuperar— y hace el pip de «no», para que no
  parezca que la pantalla no responde.

- **Y la versión sube a V6.00 por una razón concreta:** seis binarios
  distintos salieron llamándose todos V5.99 y no había forma de saber
  cuál tenía cargado la radio. El número de versión sirve exactamente
  para eso.

### What's new

The memory scan in V5.99 **did not stop where someone was talking**, and
this release is about making it do that.

- **The scan can now tell a station from noise.** What it measured was
  wrong in three of the five modes, in two different ways. In **AM** it
  looked at the audio, and an unmodulated carrier produces no audio —the
  DC blocker removes it— so a silent station read the same as noise: 4.3
  dB apart where ten are needed. In **narrow FM** it was worse, it was
  **backwards**: the noise of an empty channel comes out of the
  discriminator as loud hiss and a clean carrier quiets it, so the station
  read **30.7 dB below** the noise and the scan would have stopped
  precisely on the empty channels. AM and narrow FM now look at the
  **in-channel RF level**, which rises with the carrier whether it is
  modulated or not, and sideband and CW —where that level does not
  exist— look at the audio, now measured **inside the audio passband**
  rather than over a far wider one, which is what left a voice sitting on
  the threshold. Measured mode by mode in
  `deepsdr-ui/sim/scan_nivel.c`: a weak station —600 counts over noise of
  300— stands **12.7 to 17.6 dB** clear in all five modes, and two
  noise-only spots read the same to within **half a decibel**.

- **Tapping a memory that already holds a frequency returns you to the
  main screen**, as bands and modes already did: you have chosen where to
  listen, what you want to see is the spectrum. An **empty** slot does not
  close —there is nothing to recall— and gives the "no" beep, so it does
  not read as an unresponsive screen.

- **And the version goes to V6.00 for a specific reason:** six different
  binaries all shipped calling themselves V5.99, with no way to tell which
  one the radio had loaded. That is exactly what a version number is for.

---

## V5.99 · 2026-10-26

### Qué cambia

Un banco de memorias con escaneo, y una revisión a fondo de FM ancha de
catorce pasadas en la que lo que de verdad le pasaba al audio no estaba en
el demodulador.

- **Banco de memorias (nuevo).** Botón **MEM** arriba a la derecha, en la
  misma fila que las pastillas de modo y banda. La ventana es **la misma
  que la de bandas**: los cuatro bancos —**HF, Aviones, Marina y
  Repetidores**, y les puedes cambiar el nombre— siempre a la vista en la
  columna de la izquierda y dieciséis botones a la derecha, quince
  memorias y **SCAN**
  siempre el último. **Se graba dejando el dedo dos segundos** sobre el
  botón, y
  suena el pip de la captura de pantalla; un toque corto recupera. Y no
  recupera sólo la frecuencia: vuelve al **modo, al paso y al filtro** que
  tenías. Se guardan en **MEM.CSV**, un fichero de texto del disco que
  puedes abrir en el PC, corregir y **copiar de una radio a otra**.

- **Barrido.** El botón SCAN recorre las memorias usadas del banco **en el
  que lo toques**: se planta en cada una, mira **un segundo** si hay
  emisión y si no sigue; al llegar a la última **vuelve a empezar**. Lo que
  decide que hay señal es una medida propia del barrido, así
  que **no usa el silenciador**: un umbral fijo en decibelios no distingue
  una emisora del ruido de banda. Se compara **consigo mismo** y se para
  donde algo sobresale diez decibelios del más bajo del banco. **La
  primera vuelta las recorre todas** —hasta haberlas visto no hay con qué
  comparar—, así que no hay que ajustar nada antes de usarlo. Para del todo y hace pip; no
  sigue solo cuando la señal se va. **Al tocar SCAN la ventana se cierra y
  vuelves a la principal**, con el espectro y la cascada delante; el botón
  **MEM** de arriba se enciende y va cantando el **número de la memoria**
  que escucha —01, 02, 03…—, y **tocarlo para el barrido**.

- **Silenciador en FM ancha (nuevo).** FM ancha no lo había tenido nunca, y
  por eso una frecuencia sin emisora te comía con siseo. Va **por ruido de
  alta frecuencia**, como las radios comerciales, y no por nivel: en FM el
  nivel no dice si hay emisora —una floja y una local dan la misma salida—,
  pero una emisora no pone nada por encima de los 60 kHz y una frecuencia
  vacía sí. Medido: 47,3 dB con cualquier emisora de 500 a 30.000 cuentas,
  75,1 dB con el canal vacío. **Diecinueve decibelios de margen**, y por eso
  no hay que reajustarlo al cambiar de emisora. Es el mando «Silenciador»
  de siempre, que en FM no hacía nada; en su suelo sigue apagado.

- **Una emisora floja encima de un desplazamiento podía desaparecer del
  todo.** El estimador de continua tenía un atajo que, cuando la continua
  es mayor que la portadora, devolvía la amplitud de la emisora como si
  fuera la fuga: el tono caía de 12.518 a 20 y el 99,9 % de lo que salía
  era basura. No era una degradación suave, era un acantilado.

- **Menos distorsión en una radio sin fuga ninguna.** Dos fallos hacían que
  el quitador de continua restara el residuo de la propia emisora: con el
  offset a cero, la distorsión baja de **0,32 % a 0,08 %**. Un escalón de
  nivel de un decibelio, o un desvanecimiento normal, dejaban restando
  2.048 cuentas de una emisora limpia.

- **El ancho de audio de FM ancha vuelve a tener mando.** Se había quedado
  sin uno solo: lo que hubiera en CONFIG.CSV se quedaba ahí para siempre, y
  un fichero de una versión anterior podía dejar la FM comercial cortada en
  4 kHz sin forma de arreglarlo desde la radio. Ahora la chapa BW, la celda
  de Ajustes y el mando enganchado lo cambian entre 15, 8 y 4 kHz.

- **Y veinte fallos más**, entre ellos: la pantalla de hora escribía su
  frecuencia y su modo en CONFIG.CSV, un detente hacia atrás en «Muestreo»
  hacía dos reconfiguraciones completas del códec —medio segundo de radio
  muerta—, el aviso de SOBRECARGA no se pintaba nunca, y cinco instrumentos
  de la ventana de Información medían algo distinto de lo que decían medir.

### What's new

A memory bank with scanning, and a deep review of wide FM over fourteen
passes in which what was really happening to the audio turned out not to be
in the demodulator.

- **Memory bank (new).** A **MEM** button at the top right, on the same row
  as the mode and band pills. The window is **the same one the band list
  uses**: the four banks —**HF, Aircraft, Marine and Repeaters**, and you
  can rename them— always in view down the left-hand column and sixteen
  buttons on the right, fifteen memories and **SCAN** always the
  last. **You store by holding your finger for two seconds** on the
  button, and the screenshot beep sounds; a short tap recalls. And it does
  not just recall the frequency: it returns to the **mode, the step and the
  filter** you had. They are kept in **MEM.CSV**, a text file on the disk
  you can open on a PC, correct and **copy from one radio to another**.

- **Scanning.** The SCAN button runs through the used memories of
  **whichever bank you tap it in**: it settles on each one, looks for **one
  second**, and moves on if there is nothing; after the last one it
  **starts again**. What decides that there is a signal is not a new
  setting and it is **not the squelch**: a fixed threshold in decibels
  cannot tell a station from band noise. It compares **against itself** and
  stops where something stands ten decibels above the lowest in the bank.
  **The first lap runs through them all** —until it has seen them there is
  nothing to compare against—, so nothing needs adjusting first. It stops for good and beeps; it does not resume by itself when the
  signal goes. **It keeps scanning with the window closed**, so you can
  **Tapping SCAN closes the window and takes you
  back to the main one**, spectrum and waterfall in front of you; the
  **MEM** button up top lights and shows the **number of the memory** it
  is listening to —01, 02, 03…—, and **tapping it stops the scan**.

- **Squelch in wide FM (new).** Wide FM had never had one, which is why a
  frequency with no station drowned you in hiss. It works **by
  high-frequency noise**, like commercial radios, not by level: in FM the
  level does not tell you whether there is a station —a weak one and a local
  one give the same output— but a station puts nothing above 60 kHz and an
  empty frequency does. Measured: 47.3 dB with any station from 500 to
  30,000 counts, 75.1 dB with an empty channel. **Nineteen decibels of
  margin**, which is why it never needs readjusting between stations. It is
  the usual «Squelch» control, which did nothing in FM; at its floor it is
  still off.

- **A weak station on top of a DC offset could vanish entirely.** The DC
  estimator had a shortcut that, when the offset is larger than the carrier,
  returned the station's own amplitude as if it were the leak: the tone fell
  from 12,518 to 20 and 99.9 % of the output was rubbish. Not a gentle
  degradation — a cliff.

- **Less distortion on a radio with no leak at all.** Two bugs made the DC
  remover subtract the station's own residue: with zero offset, distortion
  drops from **0.32 % to 0.08 %**. A one-decibel level step, or ordinary
  fading, left it subtracting 2,048 counts from a clean station.

- **The wide-FM audio bandwidth has a control again.** It had been left with
  none at all: whatever was in CONFIG.CSV stayed there forever, and a file
  from an earlier version could leave commercial FM cut at 4 kHz with no way
  to fix it from the radio. The BW chip, the Settings cell and the knob now
  cycle it between 15, 8 and 4 kHz.

- **And twenty more bugs**, among them: the clock screen wrote its own
  frequency and mode into CONFIG.CSV, one detent backwards on «Sample rate»
  did two full codec reconfigurations —half a second of dead radio—, the
  OVERLOAD warning was never painted, and five instruments in the
  Information window measured something other than what they claimed.

---

## V5.98 · 2026-10-25

### Qué cambia

Los informes de un probador, uno por uno. Cinco cosas, y la primera no era
la que parecía.

- **Los artefactos en los pasos pegados a la frecuencia intermedia cero.**
  El oscilador local se auto-mezcla y deja una **continua encima de la I y
  de la Q**. Esta radio la apartaba sintonizando siempre a 6 kHz o más del
  oscilador y confiando en que el filtro de canal la matara — con un
  comentario que decía, palabra por palabra, que «el filtro de canal es de
  ±4 kHz, así que a 6 kHz la fuga ya está fuera con holgura». Eso era verdad
  para AM el día que se escribió. Medido hoy sobre los coeficientes que hay
  en el firmware, con el desplazamiento en ese mínimo de 6 kHz: **AM −14,5
  dB, NFM −2,3 dB, AIS −0,1 dB**. O sea que en NFM el filtro de canal **no
  hacía nada** con la fuga, y en AIS ni eso: llegaron los dos filtros anchos
  y nadie volvió a mirar el número.

  No se arregla ensanchando el agujero del NCO, que es lo primero que se
  piensa y lo que costaría ventana de paseo: la continua **se resta de la I
  y de la Q antes de la bajada en frecuencia**, que es donde vive, y
  entonces deja de existir para todo lo que viene detrás. La cadena de FM
  ancha ya lo hacía desde el 11/10; ésta no, y es la que lo tenía más fácil
  —allí la portadora vive en continua y la corrección necesita tope, aquí
  hay mezclador y la señal buena nunca está en el centro—. Banco nuevo
  `sim/rf_dc.c`, que mete muestras crudas por la entrada de verdad. NFM,
  emisora floja, 300 cuentas de fuga: **45,7 % de basura a 6 kHz, 11,1 % a
  8 kHz, 0,1 % a 12** — antes; **0,0 % en los tres** — ahora.

- **El recuadro de la rejilla de bandas señalaba otra banda.** Comparaba la
  frecuencia sintonizada con la de **entrada** del preset en vez de mirar en
  qué banda estás, así que sólo acertaba justo en la frecuencia de entrada;
  en 7.188 kHz enmarcaba la casilla de 2 m.

- **El valor del NCO no salía solo después de sintonizar.** Se recalcula en
  cada cuadro, pero quien lo pinta es la franja de estado y el clic del
  mando no la repinta —se dejó así a propósito para no gastar 32.000
  accesos al bus por clic—. Con el oscilador aparcado, la sintonía se mueve
  y el oscilador no: el número cambia en cada clic y la chapa se quedaba con
  el del arranque. Ahora se repinta **sólo cuando ese número cambia de
  verdad**, así que el coste se paga cuando hay algo nuevo que enseñar.

- **En NFM la línea de sintonía del espectro no tenía ancho.** AM, SAM y
  banda lateral sombrean su paso y NFM era la única rama sin él. Ya sale:
  ±6,25 kHz, o ±9 kHz con AIS puesto.

- **Saliendo de FT8 en 80 m la radio se quedaba en USB.** La tabla de «cómo
  dejaste cada banda» guardaba la **portadora**, y en FT8 esa portadora es
  USB siempre — también en los 3.573 kHz, que caen dentro de 3.500-3.800 y
  pisan la casilla de 80 m. A partir de ahí esa casilla daba USB para
  siempre. Un modo digital no elige banda lateral: la hereda de su norma.
  Ahora, cuando lo que suena no es analógico, se apunta la del preset — LSB
  por debajo de 10 MHz, USB por encima —. La frecuencia y el paso se siguen
  apuntando tal cual.

**Y el RDS dice dos cosas más**, escritas desde cero: **TRÁFICO** mientras
dura el aviso de carretera —no la marca permanente de «esta emisora da
tráfico», sino el bit que se levanta al entrar el boletín— y el **tipo de
programa** en el idioma de la radio. El tipo hace falta leerlo **dos grupos
seguidos iguales** antes de darlo por bueno: el campo viaja en todos los
grupos y un bloque mal recibido cambiaría el rótulo sin avisar.

**Antes de entregar, un revisor adversarial repasó estos cinco cambios y
encontró tres fallos, los tres míos y de hoy**: que TRÁFICO se encendía con
avisos que la norma dice que son de *otra* emisora; que la memoria de la
chapa del NCO sólo la actualizaban cinco de los trece sitios que repintan
esa franja, así que podía volver a mentir; y que lo apuntado al salir de una
banda dejaba un par incoherente —la frecuencia de un canal digital con la
banda lateral contraria—. Los tres están arreglados y contados enteros en
`CAMBIOS.md`.

### What's new

A tester's reports, one by one. Five things, and the first was not what it
looked like.

- **The artefacts on the tuning steps next to zero IF.** The local
  oscillator self-mixes and leaves a **DC term on I and Q**. The radio kept
  it away by always tuning 6 kHz or more off the oscillator and trusting the
  channel filter to kill it — with a comment stating, word for word, that
  «the channel filter is ±4 kHz, so at 6 kHz the leak is well clear». True
  for AM the day it was written. Measured today against the coefficients in
  the firmware, with the offset at that 6 kHz minimum: **AM −14.5 dB, NFM
  −2.3 dB, AIS −0.1 dB**. In NFM the channel filter was doing **nothing** to
  the leak, and in AIS less than nothing: the two wide filters arrived and
  nobody revisited the number.

  The fix is not to widen the NCO's hole, which is the first thing you think
  of and would cost tuning range: the DC is **subtracted from I and Q before
  the down-mix**, where it lives, and then it does not exist for anything
  downstream. The wide-FM chain has done this since 11/10; this one did not,
  and it is the one with the easier job — there the carrier lives at DC and
  the correction needs a cap, here there is a mixer and the wanted signal is
  never at the centre. New bench `sim/rf_dc.c`, feeding raw samples through
  the real entry point. NFM, weak station, 300 counts of leak: **45.7 % junk
  at 6 kHz, 11.1 % at 8 kHz, 0.1 % at 12** — before; **0.0 % at all three** —
  now.

- **The band grid boxed the wrong band.** It compared the tuned frequency
  against the preset's **entry** frequency instead of asking which band you
  are in, so it was only right exactly at that entry point; on 7,188 kHz it
  boxed the 2 m cell.

- **The NCO value did not appear on its own after tuning.** It is recomputed
  every frame, but the status strip paints it and a knob click does not
  repaint that strip — deliberately, to avoid 32,000 bus accesses per click.
  With the oscillator parked, tuning moves and the oscillator does not: the
  number changes on every click and the badge kept the one from boot. It is
  now repainted **only when that number really changes**, so the cost is
  paid when there is something new to show.

- **In NFM the spectrum's tuning line had no width.** AM, SAM and sideband
  shade their passband and NFM was the only branch without it. It is there
  now: ±6.25 kHz, or ±9 kHz with AIS on.

- **Leaving FT8 on 80 m left the radio in USB.** The «how you left each
  band» table stored the **carrier**, and in FT8 that carrier is always USB —
  including on 3,573 kHz, which falls inside 3,500-3,800 and overwrites the
  80 m cell. From then on that cell gave USB forever. A digital mode does not
  choose a sideband: it inherits one from its standard. Now, when what is
  running is not analogue, the preset's sideband is stored instead — LSB
  below 10 MHz, USB above. Frequency and step are still stored as they were.

**And RDS says two more things**, written from scratch: **TRAFFIC** while
the road announcement lasts — not the permanent «this station carries
traffic» mark, but the bit raised as the bulletin starts — and the
**programme type** in the radio's own language. The type has to be read
**two consecutive groups alike** before it is trusted: the field travels in
every group and one badly received block would swap the label without
warning.

**Before shipping, an adversarial reviewer went over these five changes and
found three bugs, all three mine and all three from today**: TRAFFIC lit up
for announcements the standard says belong to *another* station; the NCO
badge's memory was updated by only five of the thirteen places that repaint
that strip, so it could go back to lying; and what was stored on leaving a
band left an incoherent pair — a digital channel's frequency with the wrong
sideband. All three are fixed and written up in full in `CAMBIOS.md`.

---

## V5.97 · 2026-10-25

### Qué cambia

Una **revisión a fondo de todo el firmware**, pedida por ti, buscando lo que
hace ir lento, lo que corrompe el audio y lo que está duplicado. Ocho
pasadas, hasta dos seguidas sin encontrar nada. **Treinta y cinco fallos**
encontrados y arreglados, tres de ellos míos de la propia revisión —el
último, de los gordos, lo cazó un banco que ya existía.

**Lo que explicaba que la radio tardara tanto en arrancar y en cambiar de
tasa:**

- **Un bucle de espera de DOS MILLONES de vueltas** en `sdr_rx.c` que se
  compilaba en la versión de producción. Los `debug_print` de alrededor se
  evaporan con el puerto serie apagado; la espera no, porque su contador es
  `volatile`. Son **70-90 ms con el bucle principal parado**, en el arranque
  **y en cada cambio de tasa**.
- **El codec seleccionaba su página antes de CADA registro**, o sea dos
  transacciones de I²C por escritura sobre un bus que va a mano a ~70 kHz.
  La secuencia de configuración son 334 transacciones: **134 ms cada vez que
  cambia la tasa**. Ahora se acuerda de cuál está puesta: 189. Con banco que
  lo comprueba contra un codec de mentira, registro por registro, incluida la
  trampa de que el registro 0 **es** el de página.

**Lo que hacía ir lento el espectro:**

- **Cada clic del dial forzaba el repintado completo del panel**, aunque
  ningún cartel se moviera. Con el oscilador de sintonía el centro está
  aparcado y no cambia, así que se puede girar un rato sin que se mueva un
  píxel —y se repintaban igual las 52 bandas—. Son unos 9 ms por detente,
  justo mientras sintonizas.
- **Los carteles de emisora se recorrían enteros en las 26 bandas del
  cuadro**, con una división de 64 bits y una medida de texto por cartel,
  pintando sólo en 5.
- **El texto no se cortaba en vertical**: un glifo entero fuera de la banda
  entraba igual en su bucle de filas. Y el bucle de glifos estaba escrito
  **dos veces**, así que los dos arreglos de octubre entraron sólo en uno.
- Una **división en coma flotante por píxel** en la cascada: 1.500 por cuadro
  para obtener 1.500 veces el mismo divisor.
- Un **recorrido de 800 columnas por cuadro** que existía sólo para deshacer
  una mezcla de 100 bytes.
- El **chivato del repintado parcial no veía nada de lo que pinta el
  compositor**, que es donde se escribe toda la interfaz nueva.

**Lo que corrompía o cortaba:**

- **El préstamo de los 60 kB de la cascada se soltaba debajo del que acababa
  de cogerlo.** SSTV, HELL y DSC arrancaban *antes* de que pararan los ocho
  modos que comparten esa memoria, así que ir de AIS a SSTV-FM —que están
  pegados en la rejilla— hacía: SSTV coge la memoria, AIS la suelta, y el
  `waterfall_init()` borra los 60 kB encima. Foto que no baja y cascada con
  basura, hasta cambiar de modo.
- **`spi_flash_read()` leía el chip mientras programaba.** El guardado de
  `CONFIG.CSV` deja la flash ocupada entre llamadas, y durante ese rato la
  ventana del mapa y los nombres de las emisoras leían lo que diera.
- **El guardado de los ajustes tenía dos dueños.** La base de aviones usaba
  como pregunta una función que **avanza** la máquina de estados, así que se
  comía el "terminado" y los ajustes se quedaban "guardando" **para
  siempre**: ni frecuencia, ni modo, ni volumen, ni calibración del táctil, y
  sin un aviso.
- **El reductor de ruido no tenía forma de reiniciarse.** Al encenderlo
  soltaba unos 10 ms de audio de otro momento y 130 ms de ganancias medidas
  contra un suelo de otra banda. Y el notch automático no se limpiaba al
  cambiar de modo, siendo uno solo para banda lateral y para AM.
- **El olvido de la cadena y el silenciador de entrada vivían dentro del `if`
  del cambio de tasa**, así que un cambio de modo que no cruza tasa no
  reiniciaba nada.
- **AIS:** la reparación del 09/10 no podía funcionar. La guarda que dejaba
  entrar era la misma bandera que hacía volver a la función en su primera
  línea, así que tocar la tasa con AIS puesto lo dejaba mudo para siempre.
- **La fase del oscilador** en el camino rápido de Fs/4 se "avanzaba" con una
  suma que vale exactamente cero.
- **Entrar en SSTV estando ya en SSTV** tiraba la imagen a medias.

**Duplicados fusionados:** el estimador del corrector de imagen estaba
escrito dos veces y ya se habían separado; el recorte a 16 bits tenía una
tercera copia; el bucle de glifos, dos. Y se han quitado **cuatro filtros de
audio que se inicializaban en cada cambio de tasa sin que nadie los usara
nunca**, con un comentario que describía un reparto que llevaba meses sin
existir.

**Tres bancos nuevos** y 255 comprobaciones en verde.

### What's new

A **deep review of the whole firmware**, as you asked, looking for what makes
things slow, what corrupts audio and what is duplicated. Eight passes, until
two in a row found nothing. **Thirty-five faults** found and fixed, three of
them mine from the review itself — the last one, a serious one, was caught by
a bench that already existed.

**What explained the slow boot and the slow rate changes:**

- **A TWO-MILLION-iteration busy wait** in `sdr_rx.c` that was compiled into
  the production build. The `debug_print`s around it vanish with the serial
  port off; the wait does not, because its counter is `volatile`. That is
  **70-90 ms with the main loop stopped**, at boot **and on every rate
  change**.
- **The codec selected its page before EVERY register**, i.e. two I²C
  transactions per write on a bit-banged ~70 kHz bus. The configuration
  sequence is 334 transactions: **134 ms every time the rate changes**. It
  now remembers which page is selected: 189. With a bench that checks it
  against a fake codec, register by register, including the trap that
  register 0 **is** the page register.

**What made the spectrum slow:**

- **Every dial detent forced a full panel repaint**, even when no label
  moved. With the tuning oscillator the centre is parked and does not change,
  so you can turn for a while without a pixel moving — and all 52 bands were
  repainted anyway. That is about 9 ms per detent, exactly while tuning.
- **Station labels were walked in full across all 26 bands of the frame**,
  with a 64-bit division and a text measurement per label, painting in only
  5.
- **Text was not clipped vertically**: a glyph entirely outside the band
  still entered its row loop. And the glyph loop was written **twice**, so
  both October fixes went into only one of them.
- A **floating-point division per pixel** in the waterfall: 1,500 per frame
  to obtain the same divisor 1,500 times.
- An **800-column sweep per frame** that existed only to undo a 100-byte
  merge.
- The **partial-repaint watchdog saw nothing the compositor painted**, which
  is where all the new interface is written.

**What corrupted or cut:**

- **The 60 kB waterfall loan was released underneath whoever had just taken
  it.** SSTV, HELL and DSC started *before* the eight modes that share that
  memory were stopped, so going from AIS to SSTV-FM — adjacent in the grid —
  did: SSTV takes the memory, AIS releases it, and `waterfall_init()` wipes
  the 60 kB on top. No picture and a garbage waterfall until you change mode.
- **`spi_flash_read()` read the chip while it was programming.** Saving
  `CONFIG.CSV` leaves the flash busy between calls, and during that window
  the map window and the station names read whatever came out.
- **The settings save had two owners.** The aircraft database used as a
  question a function that **advances** the state machine, so it swallowed
  the "done" and the settings stayed "saving" **forever**: no frequency, no
  mode, no volume, no touch calibration, and no warning.
- **The noise reducer had no way to reset.** Switching it on released about
  10 ms of audio from another moment and 130 ms of gains measured against
  another band's noise floor. And the auto-notch was not cleared on a mode
  change, being a single one for sideband and AM.
- **The chain reset and the input mute lived inside the rate-change `if`**,
  so a mode change that does not cross a rate reset nothing.
- **AIS:** the 09/10 fix could not work. The guard that let it in was the
  same flag that made the function return on its first line, so touching the
  rate with AIS on left it mute forever.
- **The oscillator phase** on the fast Fs/4 path was "advanced" by a sum that
  is exactly zero.
- **Entering SSTV while already in SSTV** threw away the half-received image.

**Duplicates merged:** the image-corrector estimator was written twice and
had already diverged; the 16-bit saturation had a third copy; the glyph loop,
two. And **four audio filters that were initialised on every rate change
without anyone ever using them** have been removed, along with a comment
describing a routing that had not existed for months.

**Three new benches** and 255 checks green.

---

## V5.96 · 2026-10-24

### Qué cambia

- **Reparada la V5.95, que dejó el espectro inservible.** Fue mío y fue
  gordo: el panadaptador pasó a hacer la FFT de **512 puntos siempre**, y a
  96 y a 48 kHz el bloque de la captura son **256 muestras**, así que había
  que juntar dos bloques por ventana. Dos averías de una vez:

  - juntar bloques obligaba a volver del tick del espectro **a la mitad de
    las pasadas**, antes de la puerta que decide si toca dibujar: la
    cadencia de cuadros partida por dos, y a 48 kHz por cuatro;
  - y los dos bloques **no son seguidos**. El bloque que llega pisa al
    anterior, y entre dos pasadas del bucle principal pasan de 10 a 35 ms,
    así que la ventana se montaba con dos trozos separados unos 20 ms: un
    salto de fase en medio. No era más resolución, era la traza emborronada.

- **Ahora una ventana es un bloque, y la transformada se adapta a él.**

  | | bloque | ahora |
  |---|---|---|
  | 192 kHz | 512 | **375 Hz por bin** |
  | 96 kHz | 256 | **375 Hz por bin** |
  | 48 kHz | 256 | **187 Hz por bin** |

  Los 375 Hz de 192 kHz —que es de donde salió todo esto, las emisoras con
  la punta plana— se quedan. A 96 kHz se vuelve exactamente a lo de la
  V5.94, sin juntar nada y sin pagar la transformada doble. Y a 48 kHz sale
  gratis el doble de resolución, porque ahí el bloque ya daba para ello.

- **Y no hacen falta dos juegos de tablas.** Las de 512 sirven para 256: los
  giros tal cual —el índice de la columna sale del tamaño de la *tabla*, no
  del de la transformada—, la ventana de Hann cogiendo las muestras pares, y
  la tabla de inversión de bits desplazada uno. Cero bytes de memoria
  añadidos.

- **Con su banco, porque son tres afirmaciones sobre índices.** Se comparan
  contra `User/fft.c` **compilado otra vez, nativo a 256 puntos y con sus
  propias tablas**: el único patrón que no puede equivocarse en lo mismo.
  Donde hay señal las dos salidas coinciden en **0,017 dB**.

- **El osciloscopio de los nueve modos no se entera:** sigue siendo de 512
  pase lo que pase, y el banco lo comprueba cambiando el tamaño del
  panadaptador por debajo.

### What's new

- **Fixes V5.95, which left the spectrum unusable.** It was mine and it was
  bad: the panadapter moved to a **512-point FFT always**, and at 96 and
  48 kHz the capture block is **256 samples**, so two blocks had to be
  joined per window. Two faults at once:

  - joining blocks meant returning from the spectrum tick **on half the
    passes**, before the gate that decides whether to draw: frame cadence
    halved, and quartered at 48 kHz;
  - and the two blocks **are not consecutive**. An arriving block overwrites
    the previous one, and 10 to 35 ms pass between two main-loop passes, so
    the window was assembled from two chunks about 20 ms apart: a phase step
    in the middle. Not more resolution — a smeared trace.

- **Now one window is one block, and the transform follows it.**

  | | block | now |
  |---|---|---|
  | 192 kHz | 512 | **375 Hz per bin** |
  | 96 kHz | 256 | **375 Hz per bin** |
  | 48 kHz | 256 | **187 Hz per bin** |

  The 375 Hz at 192 kHz — where all this started, the flat-topped stations —
  stay. At 96 kHz it is exactly V5.94 again, joining nothing and not paying
  for the double transform. And at 48 kHz twice the resolution comes free,
  because the block was already big enough there.

- **And two sets of tables are not needed.** The 512 ones serve 256: the
  twiddles as they are — the column index comes from the size of the
  *table*, not of the transform —, the Hann window taking the even samples,
  and the bit-reversal table shifted by one. Zero bytes of memory added.

- **With its own bench, because that is three claims about indices.** They
  are compared against `User/fft.c` **compiled again, native at 256 points
  with its own tables**: the only reference that cannot be wrong in the same
  way. Where there is signal the two outputs agree to **0.017 dB**.

- **The nine-mode scope does not notice:** it stays at 512 whatever happens,
  and the bench checks it by changing the panadapter size underneath it.

---

## V5.95 · 2026-10-24

### Qué cambia

- **El doble de resolución en el panadaptador, en las dos tasas.**

  | | antes | ahora |
  |---|---|---|
  | 96 kHz | 375 Hz por bin | **187** |
  | 192 kHz | 750 Hz por bin | **375** |

  Eso es lo que hacía que a 192 kHz las emisoras salieran con la punta
  plana: a 750 Hz por bin una emisora de fonía de 2,5 kHz cabe en tres
  muestras y no queda con qué dibujarla.
- **Y la memoria salió de una FFT duplicada.** `rtty_scope.c` llevaba una
  **transformada de 512 puntos entera** —sus propias tablas de giro, ventana
  de Hann y tabla de bits, 9.216 bytes— para el osciloscopio de RTTY. O sea
  que la radio ya tenía dentro la FFT de 512 que hacía falta; sólo que era
  del módulo equivocado. Ahora hay **una sola**, y el osciloscopio se lleva
  de regalo la versión optimizada de la mariposa.
- **La deuda estaba escrita desde el 05/08/2026**, en un comentario que
  decía que el panadaptador en FM ancha enseñaba «sólo las primeras 256 de
  las 512 muestras por bloque… hasta que se redimensione». Ésta es esa
  pieza.
- **Y una guarda que miraba la constante equivocada:** exigía que la ventana
  de la FFT fuera igual al bloque, pero comparaba contra el bloque de 96
  kHz. A 192 la invariante que defendía llevaba rota desde ayer y la guarda
  ni se enteraba.
- **Y un banco nuevo para el osciloscopio**, que no es de RTTY: lo
  alimentan **nueve modos** (RTTY, CW, NAVTEX, WEFAX, SSTV, FT8, PSK31, DSC
  y APRS) y hasta hoy sólo lo tocaba el banco de uno de ellos, de rebote.
  Ahora se mira de frente: un tono conocido entra y el pico tiene que salir
  en su bin.
- **Lo que no me gusta, y se dice:** el margen de pila baja de 3.792 a 2.256
  bytes (del 46 % al 34 %). No es alarmante, pero es menos colchón. La
  vuelta está identificada: SSTV, NAVTEX y APRS ocupan 8.448 bytes de TCM
  los tres a la vez aunque sólo pueda vivir uno, y una unión entre ellos
  devuelve 4.608 — más de lo que había antes. Es el siguiente paso.

### What's new

- **Twice the panadapter resolution, at both rates.**

  | | before | now |
  |---|---|---|
  | 96 kHz | 375 Hz per bin | **187** |
  | 192 kHz | 750 Hz per bin | **375** |

  That is what made stations come out flat-topped at 192 kHz: at 750 Hz per
  bin a 2.5 kHz voice signal fits in three samples and there is nothing left
  to draw it with.
- **And the memory came out of a duplicated FFT.** `rtty_scope.c` carried a
  **complete 512-point transform** — its own twiddle tables, Hann window and
  bit-reversal table, 9,216 bytes — for the RTTY scope. The radio already
  had inside it the 512-point FFT that was needed; it just belonged to the
  wrong module. There is **one** now, and the scope gets the optimised
  butterfly for free.
- **The debt had been written down since 05/08/2026**, in a comment saying
  the panadapter in wide FM showed "only the FIRST 256 of the 512 samples
  per block… until that's resized too". This is that piece.
- **And a guard that checked the wrong constant:** it required the FFT
  window to equal the block, but compared against the 96 kHz block. At 192
  the invariant it defends had been broken since yesterday and the guard
  never noticed.
- **And a new bench for the scope**, which is not RTTY's: **nine modes**
  feed it (RTTY, CW, NAVTEX, WEFAX, SSTV, FT8, PSK31, DSC and APRS) and
  until today only one of them's bench touched it, indirectly. Now it is
  checked head-on: a known tone goes in and the peak has to land in its bin.
- **What I am not happy about, said plainly:** stack margin drops from 3,792
  to 2,256 bytes (46 % to 34 %). Not alarming, but less cushion. The way
  back is identified: SSTV, NAVTEX and APRS take 8,448 bytes of TCM all at
  once although only one can ever live, and a union between them returns
  4,608 — more than there was before. That is the next step.

---

## V5.94 · 2026-10-24

### Qué cambia

Cuatro de la revisión que arrancó con lo de «señales por donde no toca».
Ninguno lo habías visto todavía: estaban escritos en el código esperando.

- **El zoom seguía permitido en FM ancha.** El comentario explicaba muy bien
  por qué el zoom no puede convivir con bloques de 512 muestras… y la guarda
  preguntaba por la casilla de 192 kHz, que vale **cero** en FM ancha aunque
  ahí la tasa sean 192 igualmente. Lo que decide es la tasa, no la casilla.
  Y faltaba un cuarto sitio que no existía: **entrar en FM comercial con el
  zoom puesto lo dejaba puesto**.
- **Con zoom, el borrado de la raya de continua tapaba la emisora que estás
  escuchando.** El zoom remezcla al dial antes de diezmar, así que el centro
  de esa ventana es el dial y no el oscilador: se borraban 234 Hz justo
  encima del punto de demodulación —la portadora de AM o el tono de CW— y a
  la vez **quedaba destapada la fuga del oscilador**, que aparecía como una
  portadora fija que no es ninguna emisora.
- **El automático de radiofrecuencia se pasaba 250 ms creyendo que
  actuaba.** Su tope de recorte eran 30 dB y el techo del que baja son 20:
  por debajo de cero el codec ya no cambia, así que seguía dando cinco pasos
  de 50 ms sin tocar nada antes de pasar al atenuador. Un cuarto de segundo
  de intermodulación de regalo cada vez que entra una señal fuerte, que es
  justo cuando duele.
- **Los 84 dB del filtro del zoom no existen.** El comentario decía que
  encadenar etapas multiplica el rechazo. No: cada etapa trabaja sobre lo ya
  diezmado por la anterior, así que en el punto crítico la anterior está en
  su banda de paso y no aporta. Medido: **28 dB en el borde, sea cual sea el
  zoom** (2x, 4x y 8x dan lo mismo). No se arregla en esta versión —haría
  falta un filtro más largo y eso cuesta interrupción—, pero el número
  medido queda escrito para que nadie vuelva a creerse los 84.

### What's new

Four from the review that started with "signals where they shouldn't be".
None of these had shown up yet: they were sitting in the code, waiting.

- **Zoom was still allowed in wide FM.** The comment explained perfectly why
  zoom cannot live with 512-sample blocks… and the guard asked about the
  192 kHz checkbox, which is **zero** in wide FM even though the rate there
  is 192 anyway. What decides is the rate, not the checkbox. And a fourth
  place was missing entirely: **entering commercial FM with zoom on left it
  on**.
- **With zoom, the DC-line blanking erased the station you are listening
  to.** Zoom re-mixes to the dial before decimating, so that window's centre
  is the dial and not the oscillator: it blanked 234 Hz right on the
  demodulation point — an AM carrier or a CW tone — while **leaving the
  oscillator's own leakage uncovered**, showing up as a fixed carrier that
  is not a station at all.
- **The RF auto-gain spent 250 ms believing it was acting.** Its backoff
  limit was 30 dB and the ceiling it backs off from is 20: below zero the
  codec stops changing, so it kept taking five 50 ms steps that did nothing
  before escalating to the attenuator. A quarter second of intermodulation
  free of charge every time a strong signal arrives — exactly when it hurts.
- **The zoom filter's 84 dB do not exist.** The comment claimed cascading
  stages multiplies the rejection. It does not: each stage works on what the
  previous one already decimated, so at the critical point the previous one
  is in its passband and contributes nothing. Measured: **28 dB at the edge,
  whatever the zoom** (2x, 4x and 8x all the same). Not fixed in this
  release — that needs a longer filter and interrupt time we do not have —
  but the measured number is now written down so nobody believes the 84
  again.

---

## V5.93 · 2026-10-24

### Qué cambia

- **«SOBRECARGA» sale centrado en su pastilla.** Iba pegado a la izquierda
  porque el ancho de la pastilla se calcula para lo que pone el mando, que
  es más largo. Ahora se centra con lo que mide el texto de verdad — porque
  «SOBRECARGA» y «OVERLOAD» no miden lo mismo.
- **Y al comprobarlo me equivoqué yo.** Centré el texto, rehice el banco de
  la franja de arriba, y salió **idéntico**: `deepsdr-ui/gfx2/` lleva una
  copia de los ficheros de interfaz y es esa copia la que compilan los
  bancos. Hasta ahí, cierto. Pero escribí que eso no lo vigilaba nadie y me
  puse a hacer una red nueva — **y sí lo vigila**, desde hace meses y mejor
  (54 ficheros, no 39). No saltó porque yo mismo había copiado el fichero a
  mano antes de correr las comprobaciones. La red que hice se ha quitado:
  era un duplicado peor del que ya había.
- **Y el aviso de sobrecarga, que no se refrescaba.** Dos cosas, y la
  primera era mía de esta misma tarde: el repintado de la franja sólo se
  dispara «si la barra de S se ha movido», y eso valía mientras el aviso
  dependiera del automático de radiofrecuencia. Al soltarlo de ahí, podía
  encenderse y apagarse sin que nada lo pintara. La segunda: el plazo de
  permanencia era de segundo y medio, y el recorte va **por sílabas** —
  entre dos picos hay más de eso y el aviso se apagaba. Ahora **tres
  segundos**, que es lo que hace falta para que no parpadee robándole la
  pastilla a la lectura del mando.

### What's new

- **"OVERLOAD" is now centred in its pill.** It sat to the left because the
  pill's width is sized for whatever the knob is showing, which is longer.
  It is now centred on the text's measured width — because "SOBRECARGA" and
  "OVERLOAD" are not the same width.
- **And checking it, I was the one who got it wrong.** I centred the text,
  rebuilt the top-strip bench, and it came out **identical**:
  `deepsdr-ui/gfx2/` holds a copy of the interface files and it is that copy
  the benches compile. True so far. But I wrote that nothing was guarding
  it and set about writing a new net — **and something is**, for months, and
  better (54 files, not 39). It did not fire because I had copied the file
  by hand myself before running the checks. The net I wrote has been
  removed: it was a worse duplicate of the one already there.
- **And the overload warning, which did not refresh.** Two things, the first
  of them mine from this same afternoon: the strip only repaints "if the S
  bar moved", which held while the warning depended on the RF auto-gain.
  Once it was cut loose from that, it could come and go with nothing
  painting it. The second: the hold time was a second and a half, and
  clipping goes **by syllables** — the gap between two peaks is longer than
  that, so the warning kept dropping out. Now **three seconds**, which is
  what it takes to stop it flickering and stealing the knob readout's pill.

---

## V5.92 · 2026-10-24

### Qué cambia

- **Las señales de otra banda que se te meten: ya se pueden identificar de
  un vistazo.** Un armónico del oscilador y una imagen de I/Q se movían
  igual en la pantalla y no había forma de distinguirlos. Sí la hay, y la
  cuenta es de una línea: los dos osciladores van separados 90°, así que en
  el armónico **n** esa separación vale **n × 90°**.

  | lo que ves y oyes | lo que es |
  |---|---|
  | una copia, en su modo | emisora de verdad |
  | una copia, en el modo contrario | imagen de I/Q, o armónico **impar** |
  | **dos copias simétricas, una en USB y otra en LSB** | armónico **par**: otra banda metiéndose |

  Con 180° (2.º armónico) I y Q quedan en contrafase: eso ya no es una señal
  compleja sino **real**, y una señal real sale **dos veces**, una a cada
  lado del centro, y la de abajo **con el espectro invertido**. Por eso una
  de 20 m en USB se oye en 40 m en LSB sin tocar el modo.

- **El avisador de armónicos tenía cuatro fallos y por eso no te lo decía.**
  Buscaba en una ventana que **se encogía con el zoom de pantalla** (un
  armónico entra por el mezclador y le da igual cómo mires tú); **ordenaba
  al revés** —decía que el 2.º es el sospechoso principal, cuando el 3.º
  está 20 dB por encima porque sale de la propia onda cuadrada y el 2.º sólo
  existe por el error de ciclo de trabajo—; **no sabía que hay un filtro de
  entrada**, así que nombraba bandas que el filtro rechaza; y calculaba el
  desvío dos veces.

- **La insignia de entrada saturada no se encendía nunca de fábrica.** El
  detector de recorte funcionaba y el dato se tiraba: su único lector salía
  por la puerta antes de leerlo cuando el automático de radiofrecuencia está
  apagado, que es como viene. Con señales fuertes el convertidor recortaba,
  aparecían productos por toda la pantalla y la radio no decía nada. Ahora
  se avisa con el automático o sin él.

- **Lo que no se puede, y se dice:** quitar el armónico. Entra por el
  mezclador, delante del convertidor, y el filtro de entrada de 7 MHz es un
  paso-bajo de 36 MHz: a 14 y a 21 MHz no les hace nada. Haría falta un
  filtro de banda a la entrada, y eso es hardware.

### What's new

- **Signals from another band leaking in: now you can tell which is which at
  a glance.** An oscillator harmonic and an I/Q image moved the same way on
  screen and could not be told apart. They can, and it is one line of
  arithmetic: the two oscillators sit 90° apart, so at harmonic **n** that
  separation is **n × 90°**.

  | what you see and hear | what it is |
  |---|---|
  | one copy, in its own mode | a real station |
  | one copy, in the opposite mode | I/Q image, or an **odd** harmonic |
  | **two symmetric copies, one USB and one LSB** | **even** harmonic: another band leaking in |

  At 180° (2nd harmonic) I and Q are in antiphase: that is no longer a
  complex signal but a **real** one, and a real signal appears **twice**,
  one each side of centre, the lower one **spectrum-inverted**. That is why
  a 20 m USB station is heard on 40 m in LSB without touching the mode.

- **The harmonic warner had four faults, which is why it never said so.** It
  searched a window that **shrank with the display zoom** (a harmonic comes
  in through the mixer and does not care how closely you are looking); it
  **ranked them backwards** — it called the 2nd the prime suspect, when the
  3rd is 20 dB above it because it comes from the square wave itself while
  the 2nd exists only through duty-cycle error; it **did not know there is
  an input filter**, so it named bands the filter rejects; and it computed
  the offset twice.

- **The input-overload badge never lit with the factory settings.** The clip
  detector worked and the measurement was thrown away: its only reader
  returned early when the RF auto-gain is off, which is how it ships. With
  strong signals the converter clipped, products appeared all over the
  screen, and the radio said nothing. Now it warns either way.

- **What is not possible, said plainly:** removing the harmonic. It enters
  through the mixer, ahead of the converter, and the input filter for 7 MHz
  is a 36 MHz low-pass: it does nothing at 14 or 21 MHz. That needs a
  band-pass filter at the input, and that is hardware.

---

## V5.91 · 2026-10-24

### Qué cambia

- **El corrector de espejo era quien metía el espejo.** Medido, con un
  desequilibrio de hardware conocido y sólo cambiando lo que hay en el
  aire:

  | lo que hay en el aire | corrector apagado | corrector puesto |
  |---|---|---|
  | dos emisoras, una enfrente de otra | 20,2 dB | **10,3 dB** |
  | una banda a medias (una pareja) | 20,2 dB | **13,5 dB** |
  | la banda llena (tres parejas) | 20,2 dB | **12,3 dB** |

- **Por qué.** El estimador se apoya en que `E[I·Q]` y `E[I²]−E[Q²]` valen
  cero si I y Q están bien. Eso es cierto con una emisora, pero **dos
  emisoras de verdad colocadas simétricamente respecto al oscilador dejan
  exactamente la misma huella que un desequilibrio**: el producto cruzado
  de las dos no gira y deja un término constante. El corrector se lo creía
  y aplicaba una ganancia y una fase que no existen.
- **No es de los 40 metros ni de los 192 kHz.** Dos emisoras bastan, y son
  el caso peor. En 40 m se ve siempre —aficionados de un lado,
  radiodifusión del otro, el oscilador en medio— pero en 20 m, en 31 m o
  en onda media de noche pasa igual en cuanto se junten dos.
- **Lo que se ha puesto.** Una guarda que reconoce un número imposible: si
  lo medido implica un espejo peor de 21 dB, eso no puede ser el
  desequilibrio de un receptor en cuadratura, así que **no se actualiza
  nada y el corrector se queda con lo último bueno**. Donde sí hay
  desequilibrio de verdad sigue trabajando igual (78 dB en el caso plano,
  22 → 38 en el de pendiente).
- **Tres más de la misma zona.** El calibrador del filtro daba por buena
  una pareja con sólo 6 dB de diferencia —imposible en este aparato, que
  en lo peor da 22—; miraba también la ventana del **zoom**, que va
  remezclada al dial y diezmada, o sea que emparejaba emisoras que no
  tienen nada que ver; y el reinicio de sus coeficientes al cambiar de
  tasa **estaba escrito y no lo llamaba nadie**, con lo que al pasar de 96
  a 192 seguían puestos en la frecuencia equivocada.

### What's new

- **The image corrector was the one putting the image there.** Measured,
  with a known hardware imbalance and only changing what is on the air:

  | what is on the air | corrector off | corrector on |
  |---|---|---|
  | two stations, one facing the other | 20.2 dB | **10.3 dB** |
  | a half-full band (one pair) | 20.2 dB | **13.5 dB** |
  | the full band (three pairs) | 20.2 dB | **12.3 dB** |

- **Why.** The estimator relies on `E[I·Q]` and `E[I²]−E[Q²]` being zero
  when I and Q are right. That holds for one carrier, but **two real
  stations placed symmetrically about the oscillator leave exactly the
  same signature as an imbalance**: their cross product does not rotate
  and leaves a constant term. The corrector believed it and applied a gain
  and a phase that do not exist.
- **It is not a 40 m thing, nor a 192 kHz thing.** Two stations are
  enough, and they are the worst case. On 40 m it shows all the time — ham
  band one side, broadcast the other, the oscillator in between — but on
  20 m, on 31 m or on medium wave at night it happens the moment two line
  up.
- **What went in.** A guard that recognises an impossible number: if what
  is measured implies an image worse than 21 dB, that cannot be the
  imbalance of a quadrature receiver, so **nothing is updated and the
  corrector keeps the last good value**. Where there really is an
  imbalance it works exactly as before (78 dB in the flat case, 22 → 38 in
  the sloped one).
- **Three more in the same area.** The filter's calibrator accepted a pair
  only 6 dB apart — impossible in this radio, whose worst is 22; it also
  looked at the **zoom** window, which is re-mixed to the dial and
  decimated, so it paired up stations with nothing to do with each other;
  and the reset of its coefficients on a rate change **was written and
  nobody called it**, so going from 96 to 192 left them placed at the
  wrong frequency.

---

## V5.90 · 2026-10-24

### Qué cambia

- **Revisión a fondo de los 192 kHz: ocho fallos, todos arreglados.** Tres
  pasadas sobre todo lo que toca la tasa doble; la primera sacó ocho, la
  segunda y la tercera ninguno.
- **Lo que se nota al usarla.** La cascada iba lenta porque los rótulos de
  emisora preguntaban a la flash en **cada clic del mando** —unas catorce
  mil lecturas por segundo a 192 kHz—; ahora la lista de candidatas se
  guarda y girar dentro del mismo rango no pregunta nada. Arrancar con el
  zoom guardado en 2x o 4x y la tasa doble dejaba el zoom puesto sobre
  bloques que no son los suyos: basura con forma de espectro. Y la pantalla
  copiaba 512 muestras para una FFT que lee 256.
- **Lo que no se oía pero estaba.** El reparto del desplazamiento se
  escribía desde el bucle principal y se leía desde la interrupción: si caía
  entre las dos escrituras, un bloque entero sonaba 48 kHz movido —un
  chasquido—. El audio por USB anunciaba 96 kHz mientras entregaba 192, así
  que lo grabado salía a mitad de velocidad. Y el vigilante que baja sola la
  tasa si la radio se ahoga contaba vueltas del bucle en vez de
  milisegundos: tardaba nueve segundos en vez de medio, y cuanto más se
  ahogaba, más tardaba.
- **Dos más, de arranque.** Un fichero de ajustes con la tasa guardada pero
  sin el modo dejaba el codec a 96 kHz con todo lo demás calculado para 192.
  Y cambiar de modo borraba la memoria del diezmador aunque la tasa no
  cambiara.

### What's new

- **Deep review of the 192 kHz path: eight faults, all fixed.** Three passes
  over everything the double rate touches; the first found eight, the second
  and third none.
- **What you notice while using it.** The waterfall was slow because the
  station labels queried the flash on **every click of the knob** — around
  fourteen thousand reads a second at 192 kHz; the candidate list is now
  kept, and turning within the same range asks nothing. Booting with the
  zoom saved at 2x or 4x and the double rate left the zoom engaged over
  blocks that are not its own: rubbish shaped like a spectrum. And the
  display copied 512 samples for an FFT that reads 256.
- **What you could not hear but was there.** The offset split was written
  from the main loop and read from the interrupt: if it landed between the
  two writes, a whole block played 48 kHz off — a click. USB audio announced
  96 kHz while delivering 192, so recordings came out at half speed. And the
  watchdog that drops the rate if the radio chokes counted loop passes
  instead of milliseconds: nine seconds instead of half a one, and the more
  it choked, the longer it took.
- **Two more, at boot.** A settings file with the rate saved but not the mode
  left the codec at 96 kHz with everything else computed for 192. And
  changing mode wiped the decimator's history even when the rate did not
  change.

---

## V5.89 · 2026-10-24

### Qué cambia

- **El NCO ya alcanza los dos lados del oscilador.** Hasta ahora sólo
  admitía distancias POSITIVAS: como el oscilador se queda en el centro
  de lo que se ve, el NCO cubría del centro al borde derecho y **la mitad
  izquierda de la pantalla no era suya**. Bajando, saltaba al llegar al
  centro. Una distancia negativa no tiene nada de malo —significa que el
  oscilador está por encima de lo que escuchas, y a un receptor en
  cuadratura eso le da igual—, así que lo que se exige ahora es el valor
  absoluto.
- **Lo que se gana, medido:** **84 kHz seguidos sin que la imagen se
  mueva, a cada lado del centro**. Antes eran 34 y sólo por la derecha.
- **Lo que no se puede, y se dice:** que los 192 kHz sean un solo tramo.
  La señal no puede sentarse encima del propio oscilador —ahí se le pega
  su fuga— así que cruzar el centro obliga a moverlo una vez. Es un salto
  pequeño, de once kilohercios, y en un sitio predecible. Y quedan seis
  kilohercios en cada borde de la pantalla por el mismo motivo.

### What's new

- **The NCO now reaches both sides of the oscillator.** Until now it only
  accepted POSITIVE offsets: since the oscillator sits at the centre of
  what you see, the NCO covered centre to right edge and **the left half
  of the screen was never its own**. Tuning down, it jumped on reaching
  the centre. A negative offset is not wrong — it means the oscillator is
  above what you are listening to, and a quadrature receiver does not
  care — so what is required now is the absolute value.
- **What that buys, measured:** **84 kHz in a row with the picture
  standing still, on each side of the centre**. It was 34, and only to
  the right.
- **What is not possible, said plainly:** making all 192 kHz one single
  stretch. The signal cannot sit on top of the oscillator itself — its
  leakage lands right there — so crossing the centre forces one move. It
  is a small jump, eleven kilohertz, in a predictable place. And six
  kilohertz remain at each edge of the screen for the same reason.

---

## V5.88 · 2026-10-24

### Qué cambia

- **La FM comercial pintaba las señales y no daba audio.** Y era mío. El
  conmutador que decide qué camino corre en la interrupción —el de FM
  ancha o el de todo lo demás— sólo se tocaba **si cambiaba la tasa de
  muestreo**. Durante meses eso bastó, porque la FM ancha era el único
  modo a 192 kHz. En cuanto dejé elegir 192 kHz para los demás, pasar de
  banda lateral a FM dejó de cambiar la tasa, el conmutador no se tocaba,
  y la radio seguía corriendo el camino de banda lateral sobre una señal
  de FM. El espectro se pinta aparte, así que seguía viéndose: por eso el
  fallo no se parecía a lo que era. Ahora el conmutador sigue al modo y a
  nada más.
- **El NCO ya recorre toda la pantalla a 192 kHz.** Alcanzaba 40 kHz de
  los 96 que hay a cada lado, así que a mitad de camino había que mover el
  oscilador y saltaba la imagen entera. El desplazamiento se parte ahora
  en un giro grueso —en cuartos de vuelta, sin una sola multiplicación, y
  antes del diezmador— más el fino de siempre. El alcance pasa de 40 a
  90 kHz y la pantalla se queda quieta.
- **Tres redes nuevas**, porque los fallos de estos dos días los ha
  encontrado el usuario y no las pruebas: una recorre **todos los modos a
  todas las tasas** exigiendo que cada uno entregue audio y las muestras
  que el altavoz pide; otra comprueba que el conmutador del demodulador no
  esté dentro de ninguna condición; y la tercera, que toda clave de ajustes
  tenga quien la escriba y quien la lea.

### What's new

- **Commercial FM drew the signals and gave no audio.** And it was mine.
  The switch that picks which path runs in the interrupt — wide FM or
  everything else — was only touched **if the sample rate changed**. For
  months that was enough, because wide FM was the only mode at 192 kHz.
  Once I let the other modes pick 192 kHz, going from sideband to FM
  stopped changing the rate, the switch was never touched, and the radio
  kept running the sideband path over an FM signal. The spectrum is drawn
  separately, so it still appeared: that is why the fault did not look
  like what it was. The switch now follows the mode and nothing else.
- **The NCO now covers the whole screen at 192 kHz.** It reached 40 kHz of
  the 96 on each side, so halfway across you had to move the oscillator
  and the whole picture jumped. The offset is now split into a coarse
  rotation — in quarter turns, without a single multiply, ahead of the
  decimator — plus the usual fine one. Reach goes from 40 to 90 kHz and
  the picture stays still.
- **Three new nets**, because the faults of these two days were found by
  the user and not by the tests: one runs **every mode at every rate**,
  requiring each to deliver audio and exactly the samples the speaker
  asks for; another checks the demodulator switch is not inside any
  condition; the third, that every settings key has someone writing it
  and someone reading it.

---

## V5.87 · 2026-10-24

### Qué cambia

- **El ancho del filtro de CW ya se guarda al apagar.** El mando lo afina
  de 100 a 800 Hz de cincuenta en cincuenta, pero ese número **no estaba
  en `CONFIG.CSV`**: al encender se sacaba del selector de ancho de audio,
  que sólo tiene tres posiciones, así que cualquier valor afinado a mano
  se perdía. Y el fichero sí se reescribía —la rama del mando marcaba los
  ajustes como sucios—, que es justo lo que hacía que no se notara: por
  fuera todo cuadraba.
- **Y una red para que no vuelva a pasar.** Hay una comprobación nueva que
  verifica, en cada compilación, que toda clave de `CONFIG.CSV` tenga
  quien la escriba y quien la lea. Es el mismo fallo que el interruptor
  de la reducción de ruido del 01/10, y van dos.
- **Revisado el NCO de sintonía a 192 kHz**, pedido. Funciona igual que a
  96: medido en el simulador, idéntico hasta su tope dentro de 0,01 dB.
  Lo que conviene saber es que ese tope son 40 kHz en las dos tasas, así
  que a 192 kHz cubre el 42 % de lo que se ve; más allá, sintonizar mueve
  el oscilador, como sin NCO. No es un fallo: es el filtro que hay delante
  de la mezcla.
- **Medido también el reductor de ruido a las dos tasas**, por el aviso de
  que a 192 parecía reducir menos: la diferencia es de 0,05 dB. El camino
  de 192 kHz no le cambia nada de lo que ve.

### What's new

- **The CW filter width is now saved on power-off.** The knob tunes it
  from 100 to 800 Hz in 50 Hz steps, but that number **was not in
  `CONFIG.CSV`**: at boot it came from the audio-width selector, which has
  only three positions, so any hand-tuned value was lost. And the file was
  being rewritten — the knob's branch did mark the settings dirty — which
  is exactly what kept this hidden: from the outside everything added up.
- **And a net so it does not happen again.** A new check verifies, on
  every build, that every `CONFIG.CSV` key has someone writing it and
  someone reading it. This is the same fault as the noise-reduction
  switch on 01/10, and that makes two.
- **The tuning NCO reviewed at 192 kHz**, as asked. It behaves exactly as
  at 96: measured in the simulator, identical up to its limit within
  0.01 dB. What is worth knowing is that the limit is 40 kHz at both
  rates, so at 192 kHz it covers 42 % of what you see; beyond that,
  tuning moves the oscillator, as it does without the NCO. Not a fault:
  it is the filter sitting ahead of the mixer.
- **The noise reduction measured at both rates too**, after the report
  that it seemed to reduce less at 192: the difference is 0.05 dB. The
  192 kHz path changes nothing about what it sees.

---

## V5.86 · 2026-10-24

### Qué cambia

- **Menos trabajo en el camino de 192 kHz.** El filtro que vuelve a subir
  el audio tenía 35 tomas y le sobraban 28: lo que sube es audio de
  6 kHz, no radiofrecuencia, así que con 7 tomas se queda la copia no
  deseada 78 dB abajo. Medido en el ordenador, ese filtro pasa de 5.280 a
  1.853 ns por bloque. El que baja la señal también adelgaza: de 4.005 a
  3.507. Entre los dos, **la mitad del trabajo que añadían los 192 kHz**.
- **Los nombres de las emisoras ya no se amontonan a la izquierda.** La
  consulta a la base se quedaba con las 28 de menos frecuencia cuando no
  cabían todas. En 96 kHz casi nunca pasa; en 192 la ventana es el doble
  y sí: barriendo la base entera y las 24 horas, la ventana más llena
  tiene 39 emisoras en antena, y había 48 ventanas donde el tercio
  derecho de la pantalla se quedaba sin un solo nombre. Ahora se pregunta
  por mitades cuando hace falta.
- **Y los 192 kHz siguen costando lo que cuestan.** Medido en la radio:
  61 % de carga de audio contra 46 % a 96 kHz, con la vuelta del bucle en
  45 ms en vez de 22. Esta versión recorta parte de esa diferencia, no
  toda: procesar el doble de muestras cuesta el doble, y lo que se lleva
  la interrupción se lo quita a quien pinta la cascada. **El reductor de
  ruido son 10 de esos puntos**: apagarlo a 192 kHz devuelve más que
  todo lo que optimiza esta versión.

### What's new

- **Less work on the 192 kHz path.** The filter that brings the audio
  back up had 35 taps and needed 7: what goes up is 6 kHz of audio, not
  radio frequency, so 7 taps already leave the unwanted copy 78 dB down.
  Measured on the host, that filter drops from 5,280 to 1,853 ns per
  block. The one on the way down gets leaner too: 4,005 to 3,507.
  Together, **half the work 192 kHz was adding**.
- **Station names no longer bunch up on the left.** The database query
  kept the 28 lowest in frequency when they did not all fit. At 96 kHz
  that almost never happens; at 192 the window is twice as wide and it
  does: sweeping the whole database over 24 hours, the busiest window has
  39 stations on air, and there were 48 windows where the right third of
  the screen got no name at all. It now asks in halves when needed.
- **And 192 kHz still costs what it costs.** Measured on the radio: 61 %
  audio load against 46 % at 96 kHz, with the main loop at 45 ms instead
  of 22. This release claws back part of that gap, not all of it:
  processing twice the samples costs twice as much, and what the
  interrupt takes comes out of whoever draws the waterfall. **The noise
  reduction is 10 of those points**: turning it off at 192 kHz gives back
  more than everything this release optimises.

---

## V5.85 · 2026-10-24

### Qué cambia

- **Arreglado lo de ayer: a 192 kHz el audio salía roto y la radio iba a
  rastras.** El convertidor de entrada y el de salida cuelgan del mismo
  reloj, así que al poner la entrada a 192 kHz el altavoz pasa a pedir el
  doble de muestras por bloque —y el demodulador seguía dando la mitad,
  porque detrás del nuevo filtro todo sigue a 96. La mitad de cada bloque
  de audio era memoria de más allá del final del buffer. Ahora el audio
  vuelve a subir a 192 kHz antes de salir, con el mismo filtro al revés.
- **El diezmador iba tres veces más lento de lo que debía.** Estaba
  escrito con una comprobación de límites por cada muestra que leía
  —diecinueve por cada muestra de salida—, y lo que costaba no eran las
  multiplicaciones sino los saltos. Reescrito: el 87 % de las muestras
  salen ahora de un bucle sin una sola bifurcación.
- **Y una red, por si acaso:** si a 192 kHz el bloque de audio se pasa
  del 90 % de su tiempo durante medio segundo seguido, la radio vuelve
  sola a 96 kHz y lo dice en pantalla. Una tasa que no quepa ya no puede
  estropearte la sesión.
- **El altavoz ya no acepta un bloque de longitud desconocida.** La
  función que entrega el audio al conversor copiaba lo que ella creía que
  medía un bloque, sin que nadie le dijera lo que traía de verdad. Esa
  pareja se ha desalineado dos veces en la vida de este firmware, y las
  dos el síntoma fue audio sucio. Ahora quien entrega dice cuántas
  muestras trae, y si no cuadran sale silencio y un aviso por el puerto
  serie en vez de lo que hubiera detrás.

### What's new

- **Fix for yesterday: at 192 kHz the audio came out broken and the radio
  crawled.** The input and output converters hang off the same clock, so
  setting the input to 192 kHz makes the speaker ask for twice as many
  samples per block — and the demodulator was still producing half,
  because behind the new filter everything still runs at 96. Half of
  every audio block was memory past the end of the buffer. The audio is
  now brought back up to 192 kHz before it leaves, with the same filter
  run backwards.
- **The decimator was three times slower than it had any right to be.**
  It was written with a bounds check on every sample it read — nineteen
  per output sample — and the cost was not the multiplies, it was the
  branches. Rewritten: 87 % of the samples now come out of a loop with no
  branch in it at all.
- **And a safety net:** if at 192 kHz the audio block goes over 90 % of
  its time budget for half a second straight, the radio drops back to
  96 kHz by itself and says so on screen. A rate that does not fit can no
  longer ruin your session.
- **The speaker no longer accepts a block of unknown length.** The
  function that hands audio to the converter used to copy whatever it
  believed a block was, with no word from the caller about what had
  actually been handed over. That pair has drifted apart twice in this
  firmware's life, and both times the symptom was dirty audio. Now the
  caller states how many samples it brings, and if the two disagree the
  output is silence plus a warning on the serial port instead of whatever
  sat behind the buffer.

---

## V5.84 · 2026-10-23

### Qué cambia

- **El espectro puede verse al doble de ancho: 192 kHz.** La casilla
  **Ajustes → Equipo → Muestreo** tenía dos posiciones —48 y 96 kHz— y
  ahora tiene tres. Con 192 kHz puestos el panadaptador y la cascada
  enseñan 192 kHz de banda de una vez, y se sigue sintonizando en todo lo
  que se ve: tocar el espectro mueve el oscilador igual que siempre.
- **El audio sigue trabajando a 96 kHz, a propósito.** Lo que entra al
  doble lo diezma a la mitad un filtro justo al principio de la cadena,
  así que el filtro de canal, el Hilbert, la reducción de ruido y los
  quince decodificadores siguen siendo exactamente los de antes. Correr
  toda la cadena al doble no cabía en el procesador: lo último medido en
  la radio fueron 46 % de carga de audio.
- **Qué cuesta, y cómo verlo tú mismo** en vez de creerte una
  estimación: **Ajustes → Equipo → Información → «Carga de audio»**, una
  vez a 96 kHz y otra a 192. La cuenta de esa fila ya sabe que a 192 kHz
  el bloque es el doble de largo, así que el tanto por ciento sigue
  significando lo mismo.
- **Lo que NO hace, dicho aquí y no escondido:**
  - el **zoom se queda en 1×** mientras estén puestos los 192 kHz. Su
    camino lleva sus propios diezmadores montados para el bloque corto, y
    darles medio bloque ensuciaría la ventana en vez de estrecharla. Para
    acercarse, la casilla Muestreo vuelve a 96.
  - la **resolución es la mitad**: los mismos 256 puntos de FFT repartidos
    entre el doble de banda. Se ve el doble de ancho con el doble de
    grano.
  - y el ancho que llega al **demodulador** sigue siendo de 96 kHz: se ve
    el doble, se oye lo de siempre.
- No se queda puesto de fábrica, y se guarda en `CONFIG.CSV` en el mismo
  campo de siempre: un fichero de una versión anterior se lee igual que
  antes.

### What's new

- **The spectrum can now be twice as wide: 192 kHz.** The **Settings →
  Device → Sampling** tile had two positions — 48 and 96 kHz — and now has
  three. With 192 kHz selected the panadapter and the waterfall show
  192 kHz of band at once, and you still tune anywhere you can see:
  tapping the spectrum moves the oscillator exactly as before.
- **The audio still runs at 96 kHz, on purpose.** What comes in at double
  rate is halved by a filter right at the head of the chain, so the
  channel filter, the Hilbert, the noise reduction and the fifteen
  decoders are exactly the ones you already had. Running the whole chain
  at double rate did not fit in the processor: the last figure measured on
  the radio was 46 % audio load.
- **What it costs, and how to see it yourself** instead of taking an
  estimate on trust: **Settings → Device → Information → "Audio load"**,
  once at 96 kHz and once at 192. That row's arithmetic already knows the
  block is twice as long at 192 kHz, so the percentage still means the
  same thing.
- **What it does NOT do, said here and not buried:**
  - **zoom stays at 1×** while 192 kHz is selected. Its path carries its
    own decimators built for the short block, and feeding them half a
    block would dirty the window rather than narrow it. To zoom in, set
    Sampling back to 96.
  - **resolution is halved**: the same 256 FFT points spread over twice
    the band. Twice as wide, twice as coarse.
  - and the width reaching the **demodulator** is still 96 kHz: you see
    twice as much, you hear the same as before.
- It is not on by default, and it is stored in `CONFIG.CSV` in the same
  field as always: a file from an earlier version reads exactly as it did.

---

## V5.83 · 2026-10-23

### Qué cambia

- **La lista de FT8 se sube con el dedo.** El panel enseña quince
  renglones y la lista guardaba dieciséis: lo que entraba por abajo
  empujaba lo de arriba y no había forma de volver a mirarlo. Ahora se
  arrastra el cuerpo del panel hacia abajo y aparecen los mensajes
  viejos, igual que una lista de teléfono. Soltar no hace falta: en
  cuanto el dedo vuelve abajo del todo, la lista se queda otra vez
  pegada a lo nuevo.
- **Y guarda cuarenta y ocho en vez de dieciséis.** Son unas tres
  pantallas de historial: entre cuarenta segundos con la banda abierta y
  dos minutos y medio con la banda floja. El mapa se llena del triple,
  porque los puntos y los indicativos salen de esta misma lista.
- **Con la lista subida, lo que estás leyendo no se mueve.** Los mensajes
  nuevos siguen entrando por abajo, pero la ventana se queda sobre los
  mismos renglones en vez de resbalar uno por cada mensaje. Con el filtro
  de «sólo CQ» puesto sólo empujan los que se enseñan.
- Tocar el filtro de CQ o «Borrar» devuelve la lista al final, que es
  donde se espera encontrarla después de pedir ver otra cosa.

### What's new

- **The FT8 list now scrolls with your finger.** The panel shows fifteen
  rows and the list only kept sixteen: whatever came in at the bottom
  pushed the top out and there was no way to look at it again. Drag the
  body of the panel downwards and the older messages appear, the same as
  any phone list. There is nothing to release: as soon as your finger
  brings it back to the bottom, the list sticks to the newest again.
- **And it keeps forty-eight instead of sixteen.** That is about three
  screens of history — from forty seconds on a busy band to two and a
  half minutes on a quiet one. The map fills up three times as much,
  because its dots and callsigns come from this same list.
- **While scrolled up, what you are reading stays put.** New messages
  keep arriving at the bottom, but the window stays on the same rows
  instead of sliding one row per message. With the "CQ only" filter on,
  only the messages actually shown push it.
- Tapping the CQ filter or "Clear" takes the list back to the end, which
  is where you expect it after asking to see something else.

---

## V5.82 · 2026-10-23

### Qué cambia

- **FT8 dejaba de decodificar para siempre si salías del modo mientras
  estaba decodificando.** Decodificar ocupa entre uno y cinco segundos de
  cada quince, así que cambiar de modo justo ahí no es raro. La captura se
  quedaba «desviada» —un estado interno que sobrevive al cambio de modo— y
  al volver a entrar en FT8 la ranura no se llenaba nunca: ni un mensaje
  más hasta apagar la radio. **Éste es el gordo de esta revisión.**
- **Dos mensajes distintos podían contar como uno.** El quitado de
  repetidos usaba el CRC de catorce bits que trae la biblioteca —ella misma
  lo marca como provisional— y con veinte mensajes en una ranura hay un uno
  por ciento de que dos coincidan: el segundo se tiraba. Ahora la huella
  sale de los 77 bits del mensaje. Hay dos CQ corrientes que lo
  demuestran, y están en el banco.
- **Con el bucle principal atrasado, dos trozos de audio entraban
  cambiados de orden.** El remuestreador usa dos buffers y se vaciaban por
  número, no por antigüedad.
- **La cola de mensajes pasa de 20 a 40 y la lista de 16 a 32.** Una ranura
  de banda abierta da hasta 21 mensajes —medido—, y lo que no cabía en la
  cola se contaba en el total pero no aparecía en pantalla. Las 32 líneas
  además **llenan el mapa**: los puntos y los indicativos salen de esa misma
  lista.
- **El margen de dB ya sale en pantalla.** Dos ficheros mandaban mirarlo
  «en la chapa» y no se pintaba en ningún sitio. Aparece cuando no hay
  ningún candidato, que es cuando dice algo: dB planos es que no llega
  nada o cae fuera de la ventana; dB vivos es que hay señal pero no es FT8.
- **Y la respuesta a «decodifica menos de lo que entra», con número.**
  Hay un banco nuevo que mete una ranura de FT8 completa por un lado y
  cuenta los mensajes por el otro. Con veinticinco estaciones repartidas de
  300 a 2.700 Hz se leen **trece**: todas las de debajo de 1.500 Hz y
  ninguna por encima. No es un fallo escondido —la ventana son 1.600 Hz por
  memoria, y la pantalla pinta la raya— pero ahora está medido. Dentro de
  la ventana se lee el **84 %** de lo que entra.

### What's new

- **FT8 stopped decoding for good if you left the mode while it was
  decoding.** Decoding takes one to five seconds out of every fifteen, so
  switching modes right then is not unusual. The capture stayed "diverted"
  — an internal state that survives a mode change — and on re-entering FT8
  the slot never filled again: not one more message until a power cycle.
  **This is the big one in this revision.**
- **Two different messages could count as one.** Duplicate removal used the
  library's 14-bit CRC — which the library itself marks as provisional —
  and with twenty messages in a slot there is a one percent chance two
  collide: the second was thrown away. The fingerprint now comes from the
  message's own 77 bits. Two ordinary CQs that prove it are in the bench.
- **With the main loop running behind, two chunks of audio went in out of
  order.** The resampler uses two buffers and they were drained by index,
  not by age.
- **The message queue goes from 20 to 40 and the list from 16 to 32.** One
  busy slot yields up to 21 messages — measured — and anything beyond the
  queue was counted in the total but never shown. The 32 lines also **fill
  the map**: dots and callsigns come from that same list.
- **The dB range is now on screen.** Two files told you to check it "on the
  badge" and it was painted nowhere. It appears when there are no
  candidates, which is when it says something: flat dB means nothing is
  arriving or it falls outside the window; live dB means there is signal
  but it is not FT8.
- **And the answer to "it decodes less than what comes in", with a
  number.** A new bench feeds a whole FT8 slot in one end and counts
  messages at the other. With twenty-five stations spread from 300 to
  2,700 Hz it reads **thirteen**: every one below 1,500 Hz and none above.
  It is not a hidden fault — the window is 1,600 Hz for memory reasons, and
  the screen draws the line — but now it is measured. Inside the window,
  **84 %** of what comes in is read.

---

## V5.81 · 2026-10-23

### Qué cambia

- **Al salir del decodificador de CW con el botón «Modo» ya no sale media
  pantalla de cada una.** Avisado por un probador: el espectro arriba, la
  chapa de los ppm en medio, dos pestañas sueltas de la rejilla de modos
  por la izquierda, y así se quedaba hasta volver a pulsar.
- **Era un problema de orden, no de dibujo.** La vuelta del bucle miraba
  el táctil *después* de decidir qué iba a pintar, así que el toque abría
  la rejilla de modos —y la pintaba entera— y acto seguido el reparto de
  esa misma vuelta, con la foto vieja en la mano, pintaba el panel digital
  por encima. Ahora se toca, se mira cómo ha quedado la radio, y entonces
  se dibuja.
- **Y no se curaba solo**, que es lo que lo hacía visible: en la vuelta
  siguiente la radio ya sabe que hay un menú abierto y entonces no repinta
  nadie. Pasaba en cualquier modo digital; en CW es donde se vio.
- **Una comprobación nueva que vigila ese orden** (`tools/orden_check.py`),
  porque las tres partes de la vuelta están a doscientas líneas unas de
  otras y volver a cruzarlas no se ve leyendo.

### What's new

- **Leaving the CW decoder with the "Mode" button no longer leaves half of
  each screen on display.** Reported by a tester: the spectrum on top, the
  ppm badge in the middle, two stray tabs of the mode grid on the left —
  and it stayed that way until the button was pressed again.
- **It was an ordering problem, not a drawing one.** The main loop read the
  touch panel *after* deciding what to paint, so the tap opened the mode
  grid, painted it in full, and then that same pass — working from the
  stale snapshot — painted the digital panel over it. Now it reads the
  touch, looks at how the radio ended up, and only then draws.
- **And it did not heal by itself**, which is what made it visible: on the
  next pass the radio knows a menu is open, so nobody repaints. It affected
  every digital mode; CW is where it showed up.
- **A new check guards that order** (`tools/orden_check.py`), because the
  three parts of the pass sit two hundred lines apart and crossing them
  again is not something you catch by reading.

---

## V5.80 · 2026-10-23

### Qué cambia

- **El mapa de FT8 ya enseña el indicativo de todas las estaciones con
  localizador, no sólo de las que llaman CQ.** Avisado por un usuario del
  repositorio con una foto que no deja lugar a dudas: la lista traía
  **siete** estaciones con localizador y el mapa enseñaba **un** nombre.
  El botón de CQ manda ahora también sobre las etiquetas: encendido, sólo
  los CQ; apagado, todas las que tengan dónde pintarse.
- **Y no, una respuesta no es rara: es lo normal.** El segundo mensaje de
  un ciclo de FT8 es «LLAMADO QUIENCONTESTA LOCALIZADOR», así que trae
  localizador igual que un CQ. De las siete de aquella foto, seis eran
  respuestas. El nombre que sale es el del que **transmite** —el segundo
  indicativo—, no el del llamado.
- **Y ahora hay una sola pregunta y no tres.** El botón decide lo que se
  ve en tres sitios —la lista, los puntos y las etiquetas— y cada uno se
  lo preguntaba por su cuenta: los puntos se quedaron fuera el 13/10 y las
  etiquetas hasta hoy. Las tres pasan por la misma función.

### What's new

- **The FT8 map now labels every station that has a locator, not just
  those calling CQ.** Reported by a user of the repository with a photo
  that leaves no room for doubt: the list held **seven** stations with a
  locator and the map showed **one** name. The CQ button now governs the
  labels too: on, CQ only; off, everything that has somewhere to be drawn.
- **And no, a reply carrying a locator is not rare — it is the norm.** The
  second message of an FT8 cycle is "CALLED ANSWERING LOCATOR", so it
  carries a grid just like a CQ. Six of the seven in that photo were
  replies. The name shown is the **transmitting** station — the second
  callsign — not the one being called.
- **And there is now one question instead of three.** The button decides
  what is shown in three places — list, dots and labels — and each asked
  on its own: the dots were left out on 13/10 and the labels until today.
  All three now go through the same function.

## V5.79 · 2026-10-23

### Qué cambia

- **El botón Func sale del modo «elegir» tocándolo otra vez.** Avisado por
  un usuario del repositorio: entrar sin querer te dejaba dentro, porque la
  única salida era elegir un ajuste —o sea enganchar algo que no querías
  para después soltarlo—. Func tiene tres estados y sólo dos tenían salida:
  suelto abre, con algo enganchado suelta, y el de en medio no hacía nada.
  Ahora cierra, y acaba exactamente igual que salir por el botón Ajustes.
- **Y un guardián nuevo para que no vuelva a pasar.** Que los dos estados
  simétricos estuvieran hechos es lo que hacía que el código pareciera
  completo leyéndolo. `tools/func_check.py` lee el manejador y exige los
  tres.

### What's new

- **The Func button now leaves "pick a setting" mode when you tap it
  again.** Reported by a user of the repository: entering it by accident
  left you stuck, because the only way out was to pick a setting — that
  is, to hook something you did not want just to unhook it. Func has three
  states and only two had an exit: idle opens, hooked releases, and the
  middle one did nothing. It now closes, ending exactly where leaving via
  the Settings button does.
- **And a new guardian so it cannot happen again.** The two symmetric
  states being present is what made the code look complete when read.
  `tools/func_check.py` reads the handler and demands all three.

## V5.78 · 2026-10-22

### Qué cambia

- **Las cartas de fax llevan la frecuencia en el nombre.** Pedido por un
  usuario del repositorio. `W0385301.BMP` es la primera carta de 3.853
  kHz, `W1388201.BMP` la de 13.882. La cuenta va por frecuencia, así que
  cada emisora numera las suyas.
- **Y todos los ficheros del disco llevan ya su fecha y su hora de
  verdad.** Pedía también la hora en el nombre, y en ocho caracteres no
  caben las dos cosas — pero el FAT tiene un sitio propio para la fecha y
  este driver lo rellenaba con una **constante**: los tres sitios que
  crean un fichero escribían el 1 de enero de 2026 y los campos de hora
  no los escribía nadie. O sea que en el ordenador todas las capturas y
  todas las cartas salían con la misma fecha y no se podían ordenar.
  Ahora llevan la hora a la que se abrieron —para una carta, cuando
  empezó a entrar—, y el nombre se queda entero para la frecuencia.
- **Sin reloj en hora no se inventa nada**: vuelve al relleno de siempre,
  que al menos se ve que es mentira.

### What's new

- **Fax charts now carry the frequency in the file name.** Requested by a
  user of the repository. `W0385301.BMP` is the first chart from 3,853
  kHz, `W1388201.BMP` the one from 13,882. The counter runs per
  frequency, so each station numbers its own.
- **And every file on the disk now carries its real date and time.** The
  request was for the time in the name too, and eight characters will not
  hold both — but FAT has a field of its own for the date, and this
  driver was filling it with a **constant**: all three places that create
  a file wrote 1 January 2026, and nobody ever wrote the time fields. So
  on a computer every screenshot and every chart showed the same date and
  could not be sorted. They now carry the time they were opened —for a
  chart, when it started coming in— and the name is free for the
  frequency.
- **With no clock set nothing is invented**: it falls back to the old
  placeholder, which at least looks like the lie it is.

## V5.77 · 2026-10-22

### Qué cambia

- **La fila «Margen SAM» ya dice POR QUÉ no engancha.** Avisado por un
  usuario con un vídeo: una emisora floja que no sincronizaba, y la
  sospecha razonable de que el lazo filtrara mal. Medido: **no filtra
  mal**. El detector engancha con la portadora por debajo del ruido
  —hasta −6 dB, por la cadena entera— si está dentro del margen, y no
  engancha nunca si está fuera, ni con 40 dB. Lo que decide es el desvío,
  no el nivel. Y estrechar el lazo lo empeora: con uno cuatro veces más
  estrecho deja de enganchar incluso con portadora limpia a 120 Hz.
- **Lo que faltaba era decirlo, y la radio lo sabía.** Con la portadora
  fuera de alcance el lazo se queda clavado contra el tope —el 100 % del
  tiempo, de 300 a 1.000 Hz de desvío— y con sólo ruido lo toca el 3 %.
  Ahora la fila distingue «sin enganche» de **«fuera: sube»** y **«fuera:
  baja»**, que además dicen hacia dónde girar el mando.
- **Y el texto de esa fila ya se salía de la celda.** Con el margen de
  4.000 Hz, «+- 4000 Hz · sin enganche» medía 200 px en una celda de 184:
  el que abre el margen —justo el que tiene un problema de enganche— era
  el que no podía leer el motivo. La fila no estaba en el banco de anchos;
  ahora sí, con sus nueve textos.

### What's new

- **The "SAM range" row now says WHY it is not locking.** Reported by a
  user with a video: a weak station that would not sync, and the
  reasonable suspicion that the loop filtering was at fault. Measured:
  **it is not**. The detector locks with the carrier below the noise
  —down to −6 dB, through the whole chain— as long as it is inside the
  range, and never locks outside it, not even with 40 dB. What decides is
  the offset, not the level. And narrowing the loop makes it worse: four
  times narrower and it stops locking even on a clean carrier 120 Hz off.
- **What was missing was saying so, and the radio knew.** With a carrier
  out of reach the loop sits pinned against the edge —100 % of the time,
  from 300 to 1,000 Hz of offset— while on noise alone it touches it 3 %.
  The row now tells «no lock» apart from **«out: higher»** and **«out:
  lower»**, which also say which way to turn the knob.
- **And that row's text was already overflowing its cell.** With the
  4,000 Hz range, "+- 4000 Hz · no lock" measured 200 px in a 184 px
  cell: whoever widens the range —precisely the person with a lock
  problem— was the one who could not read the reason. The row was not in
  the width bench; it is now, with its nine texts.

## V5.76 · 2026-10-22

### Qué cambia

- **En SAM-U y SAM-L, perder el enganche ya no devuelve a AM.** Avisado por
  un usuario del repositorio. A una sola banda no se llega por gusto: se
  llega porque hay otra emisora pisando por un lado, y el detector de
  envolvente ve las dos bandas sumadas, así que volver a AM devolvía entera
  la intrusa de la que se huía. Medido antes de tocarlo: sin enganche,
  SAM-U y SAM daban **exactamente el mismo audio**, con la intrusa 7,7 dB
  **por encima** del tono que se quería oír. Elegir banda no hacía nada.
  Ahora sale banda lateral de frecuencia fija —la misma banda, el mismo
  rechazo, el oscilador quieto en la sintonía— y la intrusa se queda unos
  50 dB por debajo.
- **SAM con las dos bandas no cambia**: ahí la vuelta a la envolvente sigue
  siendo lo correcto, y el banco nuevo lo exige para que nadie lo
  «arregle» de paso.
- **El manual decía que el margen del SAM por omisión era ±100 Hz.** Son 250
  desde el 19/10. Corregido en los dos idiomas.
- **`make comprueba` moría en un clon recién hecho**, en el único banco de
  catorce que usa `vendor/` y se había quedado fuera de la guarda que los
  descuelga. Arreglado, y con un guardián nuevo que lo vigila leyendo el
  grafo.

### What's new

- **In SAM-U and SAM-L, losing lock no longer falls back to AM.** Reported
  by a user of the repository. Nobody picks a single sideband on a whim:
  they pick it because another station is sitting on one side, and the
  envelope detector sees both sidebands summed, so falling back to AM
  handed back the interfering station whole. Measured before touching it:
  unlocked, SAM-U and SAM produced **exactly the same audio**, with the
  interferer 7.7 dB **above** the tone you were trying to hear. Picking a
  sideband did nothing. It now outputs fixed-frequency sideband —same
  sideband, same rejection, oscillator parked at the dial— and the
  interferer sits some 50 dB below.
- **SAM with both sidebands is unchanged**: there the envelope fallback is
  still the right answer, and the new bench demands it so nobody "fixes"
  it in passing.
- **The manual said the SAM margin defaulted to ±100 Hz.** It has been 250
  since 19/10. Corrected in both languages.
- **`make comprueba` died on a fresh clone**, on the one bench of fourteen
  using `vendor/` that had been left outside the guard that unhooks them.
  Fixed, with a new guardian that watches for it by walking the graph.

## V5.75 · 2026-10-21

### Qué cambia

- **Se puede dejar la flash externa entera en blanco desde la propia radio.**
  «Formatear disco» rehace el índice y nada más: lo escrito sigue ahí y la
  zona alta —la base de datos cargada y el mapa— no se toca. La fila nueva
  **«Borrar el chip»**, en la página oculta de Información al lado de
  «Cargar datos», borra de verdad de punta a punta y vuelve a formatear
  después. Dos toques para confirmar, el porcentaje mientras va, y se niega
  a empezar si hay una captura o un volcado en marcha.
- **Y se borra sin bloquear la radio.** Un bloque por vuelta del bucle
  principal, el más grande que quepa, en vez del borrado de chip entero del
  propio chip, que tarda entre 25 y 50 segundos sin contestar y se lleva por
  delante la espera de 10 s del driver. Así la radio sigue recibiendo
  mientras tanto.
- **El manual lo cuenta, con sus dos fallos y la advertencia de que esto no
  toca el firmware**, que vive en la flash interna del micro: la radio
  arranca igual con el chip en blanco.

### What's new

- **The whole external flash can now be wiped from the radio itself.**
  "Format disk" rebuilds the index and nothing more: what was written is
  still there and the upper area —the loaded database and the map— is left
  alone. The new **"Erase whole chip"** row, on Information's hidden page
  next to "Load data", really does erase end to end and formats afterwards.
  Two taps to confirm, a percentage while it runs, and it refuses to start
  if a screenshot or a database copy is in progress.
- **And it erases without locking the radio up.** One block per pass of the
  main loop, the largest that fits, instead of the chip's own whole-chip
  erase, which takes 25 to 50 seconds without answering and outlives the
  driver's 10 s wait. The radio keeps receiving throughout.
- **The manual covers it, with its two failure messages and the warning that
  this does not touch the firmware**, which lives in the micro's internal
  flash: the radio starts up just the same with a blank chip.

## V5.74 · 2026-10-21

### Qué cambia

- **La fila «Cargar datos» ya cabe en su celda, y uno de sus avisos decía lo
  contrario de lo que pasaba.** Avisado por un usuario del repositorio, que
  después de formatear veía «BD.bin esta en el disco p…» y no podía leer
  más. Dos cosas a la vez: los siete motivos de esa fila medían entre 255 y
  385 píxeles en una celda de 184 —salían cortados **los siete**— y el que
  le tocó afirmaba que el fichero estaba en el disco cuando la función que
  lo mira devuelve cero precisamente cuando **no está**. Él había
  formateado, que es lo que borra BD.BIN. Ahora los motivos son cortos y
  medidos, y cuando no hay fichero la fila deja hablar a quien sabe el
  motivo de verdad: «no encuentro el .BIN».
- **Y el banco que mide esa pantalla no medía esa fila.** Tenía apuntado como
  peor caso el texto del camino bueno —el único que cabe— y ninguno de los
  malos. Ahora están los veintiséis textos que la fila puede enseñar, y al
  añadirlos salieron dos más que tampoco cabían.

### What's new

- **The "Load data" row now fits in its cell, and one of its messages said
  the opposite of what had happened.** Reported by a user of the repository
  who, after formatting, saw "BD.bin esta en el disco p…" and could read no
  further. Two things at once: that row's seven reasons measured between 255
  and 385 pixels in a 184-pixel cell —**all seven** were cut off— and the one
  he got claimed the file was on the disk, when the function that looks for
  it returns zero precisely when it is **not** there. He had formatted, which
  is what deletes BD.BIN. The reasons are now short and measured, and when
  there is no file the row defers to the one that knows the real reason:
  "cannot find the .BIN".
- **And the bench that measures that screen was not measuring that row.** Its
  recorded worst case was the happy-path text —the only one that fits— and
  none of the failures. All twenty-six texts the row can show are in it now,
  and adding them turned up two more that did not fit either.

## V5.73 · 2026-10-21

### Qué cambia

- **Nada en la radio; lo que cambia es que los dos umbrales del detector de
  tonos ya están medidos y no calculados en una servilleta.** La cuenta que
  había daba un fondo de escala de 26.000 para un parpadeo de negro a blanco
  a fondo. Medido con una sonda en ese mismo punto, lo que llega son 15.009
  a 300 Hz y 7.241 a 450: el discriminador se come la mitad, y se come más a
  450 que a 300, así que no hay un fondo de escala sino uno por tono. Con
  los números buenos sale algo que conviene saber: la salida de emergencia
  que cierra una carta cuando el tono de fin se perdió necesita que el tono
  de arranque de la siguiente llegue al 63 % del vaivén, no al 15 % que
  decía el código. Se deja igual —bajarlo es lo que parte una carta buena
  por la mitad con una trama de la propia carta— y queda escrito por qué,
  con las tres medidas.
- Y el margen del anillo de renglones eran «tres órdenes de magnitud»: son
  setenta veces.

### What's new

- **Nothing in the radio; what changes is that the tone detector's two
  thresholds are now measured instead of worked out on a napkin.** The sum
  that was there gave a full scale of 26,000 for a full black-to-white
  flicker. Measured with a probe at that very point, what arrives is 15,009
  at 300 Hz and 7,241 at 450: the discriminator eats half of it, and more at
  450 than at 300, so there is no single full scale but one per tone. With
  the right numbers something worth knowing falls out: the escape hatch that
  closes a chart when its stop tone was lost needs the next chart's start
  tone to arrive at 63 % of the swing, not the 15 % the code claimed. It is
  left as it is —lowering it is what splits a good chart in half on the
  chart's own halftone— and why is now written down, with all three
  measurements.
- And the row ring's margin was "three orders of magnitude": it is seventy
  times.

## V5.72 · 2026-10-21

### Qué cambia

- **Nada en la radio. Lo que se corrige es una frase del código que decía
  que algo estaba protegido y no lo estaba.** El detector de los tonos de
  arranque y de fin promedia medio segundo «para que no lo dispare un
  golpe». No es eso lo que lo impide: un solo bloque de 20 ms a fondo ya
  mete en la media cinco veces el umbral, y medido, un golpe de dos décimas
  entra igual. Lo que de verdad filtra es la otra guarda —que el tono lleve
  al menos tres veces lo que lleve el otro—, que es la que deja fuera el
  ruido. Queda escrito lo que cuesta un falso positivo (35 segundos sin
  pintar nada y después un fichero de ruido que se ve y se borra) y por qué
  no sale a cuenta apretarlo: exigir que el tono se sostenga retrasaría el
  reconocimiento, y eso se cobra en renglones de la cabecera de todas las
  cartas.

### What's new

- **Nothing in the radio. What gets fixed is a line of the code that said
  something was protected when it was not.** The start- and stop-tone
  detector averages over half a second "so that a knock does not set it
  off". That is not what prevents it: a single 20 ms block at full swing
  already puts five times the threshold into the average, and, measured, a
  two-tenths-of-a-second knock sets it off just the same. What actually
  filters is the other guard —the tone's bin must carry at least three
  times what the other one carries—, which is what keeps noise out. What a
  false positive costs is now written down (35 seconds painting nothing and
  then a file of noise you can see and delete), and so is why tightening it
  is not worth it: requiring a sustained tone would delay recognition, and
  that is paid for in rows of every chart's header.

## V5.71 · 2026-10-21

### Qué cambia

- **La carta recién recolocada ya no sale con una raya negra en el borde.**
  Al acabar el giro se enseña la carta para que se vea que salió bien, y
  para quitar la línea vertical se ponía en la columna cero. Pero cero es
  una columna como las demás, así que la línea no se quitaba: se pintaba
  pegada al borde izquierdo. Y justo ahí, donde acabas de pedir que empiece
  la carta, una raya negra se lee como «el giro ha dejado una costura».
- **Y el giro del fichero también pasa por el banco de mutaciones.** Se
  rompieron a propósito quince de sus decisiones: nueve las caza el banco.
  De las seis que no, cuatro son la misma cosa —el final del giro lo
  deciden tres condiciones que en un fichero de verdad caen en el mismo
  bloque, porque al cerrarlo se recorta a lo que ocupa—, y eso no estaba
  escrito en ningún sitio.

### What's new

- **A freshly re-aligned chart no longer shows a black line at its edge.**
  When the rotation finishes the chart is shown so you can see it worked,
  and the vertical line was removed by moving it to column zero. But zero is
  a column like any other, so the line was not removed: it was painted
  against the left edge. And right there, where you have just asked the
  chart to begin, a black line reads as "the rotation left a seam".
- **And the file rotation now goes through the mutation bench too.** Fifteen
  of its decisions were deliberately broken: the bench catches nine. Of the
  six it does not, four are the same thing —the end of the rotation is
  decided by three conditions that in a real file all fall on the same
  block, because closing it trims it to what it occupies— and that was
  written down nowhere.

## V5.70 · 2026-10-21

### Qué cambia

- **Tampoco cambia nada en la radio: cambia lo que el banco de pruebas es
  capaz de ver.** Se rompieron a propósito quince decisiones del
  decodificador de fax —umbrales, topes, el tamaño del anillo de
  renglones— para ver cuántas caza el banco. Cazaba cinco. Entre las diez
  que no: el umbral que impide que una trama de semitono de la propia carta
  parta el fichero en dos; el que hace que un tono de fin desvanecido se
  oiga igual, y sin el cual dos cartas se cosen en un fichero; y la
  capacidad útil del anillo, que es la que evita el renglón partido por la
  mitad. Seis se han cerrado con casos nuevos, y la cuenta se queda en el
  árbol: `make comprueba` vuelve a romperlas en cada pasada y para si alguna
  deja de estar vigilada.

### What's new

- **Again nothing changes in the radio: what changes is what the test bench
  can see.** Fifteen decoder decisions —thresholds, caps, the size of the
  row ring— were deliberately broken to see how many the bench catches. It
  caught five. Among the ten it did not: the threshold that stops a
  halftone pattern in the chart itself splitting the file in two; the one
  that keeps a faded stop tone audible, without which two charts get sewn
  into one file; and the ring's usable capacity, which is what prevents a
  row torn down the middle. Six are now closed with new cases, and the count
  stays in the tree: `make comprueba` breaks them again on every run and
  stops if any stops being watched.

## V5.69 · 2026-10-21

### Qué cambia

- **Otra vez nada en el comportamiento: los binarios hacen exactamente lo
  mismo.** Lo que se quita son dos números escritos dos veces. Lo que dura
  la emisión —cinco segundos de tono de arranque y treinta de señal de
  fase— lo pone la emisora, no esta radio, y de ahí salen dos cuentas del
  decodificador que no tienen nada que ver entre sí. Estaban codificadas de
  dos maneras distintas en dos funciones que nadie lee juntas: el día que
  haya que admitir otra emisora se corregiría una y la otra se quedaría, y
  lo que sale de eso es una carta con franja negra o una carta sin cabecera,
  según cuál se quede. Lo mismo con el ancho del pulso de la señal de fase,
  que estaba en prosa y a medias en el código.
- **Y el fuzzer del fax medía con una banda de veintiún renglones.** Exigía
  entre 30 y 50 renglones donde las cuarenta rondas entregan siempre 38.
  Ahora se admite uno arriba y uno abajo, y los dos sentidos salen en rojo
  al tocar el número que decide dónde empieza la carta.

### What's new

- **Again nothing changes in behaviour: the binaries do exactly the same.**
  What goes away is two numbers written twice. How long the transmission
  lasts —five seconds of start tone and thirty of phasing signal— is set by
  the station, not by this radio, and two unrelated decoder calculations come
  from it. They were encoded two different ways in two functions nobody reads
  together: the day another station has to be supported, one would be fixed
  and the other left behind, and what comes out of that is either a chart
  with a black band or a chart with no header, depending which one stayed.
  Same for the width of the phasing pulse, which was in prose and half in the
  code.
- **And the fax fuzzer measured with a twenty-one-row band.** It required
  between 30 and 50 rows where all forty rounds deliver exactly 38. One row
  either way is now allowed, and both directions go red when the number that
  decides where a chart starts is touched.

## V5.68 · 2026-10-21

### Qué cambia

- **Nada en el comportamiento de la radio: se corrige lo que estaba mal
  escrito sobre él.** El número que decide dónde empieza la carta —los 35
  segundos que tarda el fax en dejar la señal de fase y ponerse a pintar— se
  explicaba como «los 30 de la fase más un margen», y no es eso: la cuenta
  empieza cuando se reconoce el tono de arranque, así que son los 5 del tono
  más los 30 de la fase, justos. Bajarlo mete franja negra en la cabecera de
  la carta; subirlo se come tres renglones de esa misma cabecera, que es
  donde el DWD pone la hora y el nombre. El banco no lo notaba: su margen era
  de dieciséis renglones y ahora es de tres, probado en rojo en los dos
  sentidos.
- **Y el manual cuenta ya lo que hace el cartel de recolocar cuando no le
  contestas:** se quita solo a los cinco minutos sin tocar el fichero, y se
  quita también si abres un menú o la pantalla se duerme. Lo único que no se
  interrumpe es la reescritura en sí.

### What's new

- **Nothing changes in how the radio behaves: what was written about it is
  what gets fixed.** The number that decides where a chart starts —the 35
  seconds after which the fax leaves the phasing signal and starts
  painting— was explained as "the 30 of the phasing plus some margin", and it
  is not: the count starts when the start tone is recognised, so it is the 5
  of the tone plus the 30 of the phasing, exactly. Lowering it puts a black
  band into the chart's header; raising it eats three rows of that same
  header, which is where DWD puts the time and the name. The bench could not
  see it: its margin was sixteen rows and is now three, proved red in both
  directions.
- **And the manual now says what the re-align card does when you don't
  answer it:** it goes away by itself after five minutes without touching the
  file, and it also goes away if you open a menu or the screen sleeps. The
  one thing that is not interrupted is the rewrite itself.

## V5.67 · 2026-10-21

### Qué cambia

- **El botón de guardar del fax vuelve a ser un interruptor.** Dos cosas lo
  callaban y ninguna estaba escrita en ningún sitio como que debiera: con el
  disco lleno, el botón quedaba muerto hasta salirse del modo y volver a
  entrar —el aviso de «sin sitio» no lo limpiaba nadie, aunque el código que
  lo pone dice con todas las letras que lo limpia el botón—; y el freno de
  veinticinco segundos que se pone al cerrar una carta, que está para que una
  tanda de cierres automáticos no haga una tanda de ficheros, paraba también
  al dedo: quitar el botón y volver a ponerlo dejaba veinticinco segundos sin
  grabar nada, con la chapa diciendo «listo». Ahora tocar el botón borra el
  aviso, reintenta una vez y suelta el freno. El freno sigue entero para lo
  suyo.
- **Y el manual dice ya las cuatro cosas que cierran una carta.** Listaba
  tres y faltaba la que más importa en onda corta: el tono de arranque de la
  carta SIGUIENTE. Es lo que impide que, si el tono de fin llega enterrado en
  un desvanecimiento, dos cartas se cosan dentro del mismo fichero.

### What's new

- **The fax save button is a switch again.** Two things silenced it and
  neither was supposed to: with a full disk the button was dead until you
  left the mode and came back —nothing cleared the «no room» notice, even
  though the code that raises it states in so many words that the button
  clears it—; and the twenty-five-second brake applied when a chart closes,
  which exists so that a run of automatic closes cannot produce a run of
  files, also stopped the user's finger: switching the button off and on left
  twenty-five seconds recording nothing, with the badge reading «ready».
  Touching the button now clears the notice, retries once, and releases the
  brake. The brake is untouched for what it is actually for.
- **And the manual now lists all four things that close a chart.** It listed
  three and was missing the one that matters most on shortwave: the start
  tone of the NEXT chart. That is what stops two charts being sewn into one
  file when the stop tone arrives buried in a fade.

## V5.66 · 2026-10-21

### Qué cambia

- **Los avisos del cartel de recolocar ya se pueden leer.** «La carta se deja
  como esta», «No se puede leer la carta», «Ahora no se puede: se esta
  guardando» y «No se ha podido recolocar» salen al pulsar un botón, y el
  mismo dedo que los ponía los borraba en la vuelta siguiente del bucle: unos
  milisegundos. Ahora hace falta un dedo nuevo para quitarlos.
- **Y la pregunta de recolocar ya no se contesta sola.** El cartel lo arma el
  tono de fin de la carta, que llega cuando llega; si en ese momento había un
  dedo apoyado donde salen los botones —arrastrando el espectro, por
  ejemplo—, la pregunta se respondía con ese dedo. Los botones también piden
  dedo nuevo. El arrastre de la línea no, que es lo único que necesita todas
  las vueltas.

### What's new

- **The re-align card's messages can now be read.** "The chart is left as it
  is", "The chart cannot be read", "Not now: something is being saved" and
  "Could not re-align" appear when you press a button, and the very finger
  that raised them erased them on the next pass of the main loop — a few
  milliseconds. Dismissing a message now requires a fresh touch.
- **And the re-align question no longer answers itself.** The card is raised
  by the chart's end tone, which arrives when it arrives; if a finger happened
  to be resting where the buttons appear — dragging the spectrum, say — that
  finger answered the question. The buttons now require a fresh touch too.
  Dragging the line does not, since that is the one thing that needs every
  pass.

## V5.65 · 2026-10-21

### Qué cambia

- **«La carta se deja como esta» vuelve a salir.** Al recolocar una carta, si
  se deja la línea vertical en el borde izquierdo y se pulsa OK no hay nada
  que girar, y la radio tiene que decirlo. Lo decía, pero el aviso estaba
  escrito debajo de una comprobación igual que ya se volvía unas líneas más
  arriba, así que no se ejecutaba nunca: el cartel se cerraba en silencio.
  Ahora se cierra diciendo qué ha pasado, y un guardián nuevo busca esa misma
  forma —una condición que ya se decidió antes en la misma función— en los
  403 ficheros del firmware y de los bancos.

### What's new

- **"The chart is left as it is" is shown again.** When re-aligning a chart,
  leaving the vertical line at the left edge and pressing OK means there is
  nothing to rotate, and the radio has to say so. It did say so, but the
  message sat below an identical check that already returned a few lines
  above, so it never ran: the card just closed in silence. Now it closes
  saying what happened, and a new guard looks for that same shape — a
  condition already decided earlier in the same function — across all 403
  firmware and bench source files.

## V5.64 · 2026-10-21

### Qué cambia

- **El aviso de «no se guarda» ya no se pinta encima del cartel de
  recolocar.** Los dos ocupan la misma franja de la pantalla: el aviso
  tapaba veinte de los treinta y ocho píxeles de alto de los botones «Sí» y
  «No», y al caducar nadie repintaba el cartel, que se quedaba con la mitad
  de abajo en blanco y los botones medio borrados respondiendo igual.
  Mientras el cartel se vea, el aviso no se pone: lo que diría ya lo dice la
  chapa del fax.

### What's new

- **The "not saving" notice no longer paints over the re-align card.** Both
  occupy the same band of the screen: the notice covered twenty of the
  thirty-eight pixels of the «Yes» and «No» buttons, and when it expired
  nothing repainted the card, which was left with its bottom half blank and
  its buttons half-erased but still responding. While the card is showing,
  the notice is not raised: what it would say is already on the fax badge.

---

## V5.63 · 2026-10-21

### Qué cambia

- **940 bytes más de RAM libre**, que es un 20% más de margen del que había.
  El detector de los tonos de arranque y parada guardaba las 240 muestras de
  su ventana —960 bytes ocupados las veinticuatro horas— para pasárselas
  enteras al filtro. No hacían falta: ese filtro es recursivo y su estado
  son dos números. Cuesta lo mismo, reparte mejor el trabajo dentro de la
  interrupción, y la salida del banco de pruebas es idéntica byte por byte.

### What's new

- **940 more bytes of free RAM**, 20% more headroom than there was. The
  start- and stop-tone detector stored all 240 samples of its window —960
  bytes held around the clock— to hand them to the filter whole. They were
  not needed: that filter is recursive and its state is two numbers. It
  costs the same, spreads the work better inside the interrupt, and the test
  bench output is identical byte for byte.

---

## V5.62 · 2026-10-21

### Qué cambia

- **Nada en la radio.** La compilación avisaba en cada `make` de una
  recursión sin acotar en el cierre del fichero de una carta, y además el
  cálculo del peor caso de pila contaba ese camino como cero. Está acotada
  —vuelve enseguida— pero lo que la acota eran dos líneas en dos funciones
  que no se ven la una a la otra. Ahora está escrito donde está el bucle y
  hay quien lo vigile.

### What's new

- **Nothing in the radio.** Every build warned about an unbounded recursion
  in the chart-file close path, and the worst-case stack figure counted that
  path as zero. It is bounded —it returns at once— but what bounds it was
  two lines in two functions that cannot see each other. It is now written
  where the loop is, and something checks it.

---

## V5.61 · 2026-10-21

### Qué cambia

- **Nada en la radio.** El banco de pruebas del fax mandaba una señal de
  fase de sesenta líneas a las cuatro velocidades, que son treinta segundos
  a 120 líneas por minuto pero sesenta a 60: a esa velocidad y a 90 no
  estaba probando el enganche sino el tope de seguridad. Se veía en su
  propia salida —228 renglones de los 200 emitidos— y nada lo miraba,
  porque la única comprobación de esa cuenta era por abajo.

### What's new

- **Nothing in the radio.** The fax test bench sent a sixty-line phasing
  signal at all four speeds, which is thirty seconds at 120 lines per minute
  but sixty at 60: at that speed and at 90 it was not testing the lock but
  the safety timeout. It showed in the bench's own output —228 rows out of
  the 200 sent— and nothing looked, because the only check on that count was
  a lower bound.

---

## V5.60 · 2026-10-21

### Qué cambia

- **Nada en la radio.** Los tres bancos de pruebas del fax llevaban la lista
  de velocidades y un par de medidas escritas a mano, copiadas del firmware.
  El día que una cambiara, los tres habrían seguido probando las de antes y
  dando verde. Ahora las preguntan.

### What's new

- **Nothing in the radio.** The three fax test benches carried the speed
  list and a couple of sizes written out by hand, copied from the firmware.
  The day one of them changed, all three would have carried on testing the
  old ones and showing green. They now ask for them.

---

## V5.59 · 2026-10-21

### Qué cambia

- **Nada en la radio.** Uno de los bancos del guardado modelaba el firmware
  de anteayer: ignoraba el estado del decodificador a propósito, y eso dejó
  de ser cierto cuando el tono de fin empezó a cerrar el fichero. Daba verde
  comprobando que el botón no puede llenar el disco de ficheros, y no decía
  nada del camino nuevo — que es justo el que puede repetirse deprisa.

### What's new

- **Nothing in the radio.** One of the recording benches was modelling the
  firmware of two days ago: it ignored the decoder's state on purpose, and
  that stopped being true when the stop tone began closing the file. It gave
  a green light checking that the button cannot flood the disk with files,
  and said nothing about the new path — which is precisely the one that can
  repeat quickly.

---

## V5.58 · 2026-10-21

### Qué cambia

- **Nada en la radio.** Un banco de pruebas nuevo que aporrea el
  decodificador de fax: ruido, tonos, señal de fase e imagen en el orden que
  salga, con los botones pulsados al azar, y después una carta limpia para
  comprobar que sigue entrando. Existe porque los cuatro fallos que salieron
  en las revisiones de los últimos días eran todos de la misma familia
  —estado que sobrevive a una transición que nadie probó— y ninguno lo
  podía ver un banco que sólo manda señales bien formadas.

### What's new

- **Nothing in the radio.** A new test bench that hammers the fax decoder:
  noise, tones, phasing signal and picture in whatever order comes up, with
  the buttons pressed at random, and then a clean chart to check it still
  comes through. It exists because the four bugs found in the last few days'
  reviews were all of the same family —state surviving a transition nobody
  tested— and none of them could be seen by a bench that only sends
  well-formed signals.

---

## V5.57 · 2026-10-21

### Qué cambia

- **Nada en la radio.** Los dos bancos de pruebas del fax se corren ahora
  también con los detectores de tiempo de ejecución del compilador, que
  miran si por el camino se hace algo que el lenguaje no define. Los dos
  pasan limpios; lo que faltaba era que alguien lo comprobara, porque el
  peor fallo que encontró esta revisión era exactamente de esa clase.

### What's new

- **Nothing in the radio.** The two fax test benches now also run under the
  compiler's runtime checks, which watch for anything the language leaves
  undefined. Both pass clean; what was missing was anything checking it,
  because the worst bug this review found was exactly of that kind.

---

## V5.56 · 2026-10-21

### Qué cambia

- **Cuando el recolocado se corta, el cartel dice además por qué.** La
  radio ya sabía cuál de las tres causas había sido —«el fichero se acaba
  antes de tiempo», «la cadena del fichero se rompe», «el giro se ha
  descolocado»— y el manual prometía que las nombra, pero en pantalla sólo
  salía «Cortado: la carta queda a medias». Ahora la causa va en el renglón
  de abajo.
- **Por dentro:** el tamaño del bloque que el recolocado toma prestado
  estaba escrito en tres sitios y atado en ninguno; ahora lo comprueba el
  compilador.

### What's new

- **When a re-align is cut short, the card now also says why.** The radio
  already knew which of the three causes it was —the file ends too soon, the
  file's chain is broken, the rotation lost its place— and the manual
  promised it names them, but the screen only said «Cut short: the chart is
  half done». The cause now goes on the line below.
- **Under the hood:** the size of the block the re-align borrows was written
  in three places and tied in none; the compiler now checks it.

---

## V5.55 · 2026-10-21

### Qué cambia

- **Nada que se vea.** Limpieza por dentro del fax: una macro definida dos
  veces y usada cero, dos funciones públicas que no llamaba nadie —una de
  ellas, una segunda forma de preguntar algo que ya se podía preguntar— y
  una tercera que estaba abierta a todo el mundo con un aviso al lado
  diciendo que sólo era segura mientras nadie la usara. Esa se ha cerrado
  en vez de seguir avisando.

### What's new

- **Nothing visible.** Housekeeping inside the fax: a macro defined twice
  and used zero times, two public functions nobody called —one of them a
  second way to ask something that could already be asked— and a third that
  was open to everyone with a note beside it saying it was only safe while
  nobody used it. That one has been closed instead of carrying on warning.

---

## V5.54 · 2026-10-21

### Qué cambia

- **Una orden de modo por USB ya no parte la carta en dos ficheros.**
  Cualquier programa que ponga el modo por CAT —un WSJT-X o un fldigi lo
  hacen al arrancar, aunque sea el modo que ya está puesto— reiniciaba el
  decodificador del fax de cero: la carta que se estaba grabando se cerraba
  a medias, lo que venía detrás iba a un fichero nuevo, se perdían los 25
  segundos del cerrojo entre los dos y encima salía el cartel ofreciendo
  recolocar media carta. También pasaba al cambiar de banda y al elegir
  WEFAX en la rejilla estando ya en WEFAX.
- **Y ya no deja el cartel de recolocar invisible pero puesto.** Ese mismo
  camino borraba la zona entera del panel —cartel y vista previa incluidos—
  sin que nadie la repintara: la pantalla quedaba limpia y el táctil del
  panel seguía comido hasta que el cartel caducaba a los cinco minutos.

### What's new

- **A mode command over USB no longer splits a chart into two files.** Any
  program that sets the mode over CAT —a WSJT-X or a fldigi does it at
  startup, even to the mode already selected— restarted the fax decoder from
  scratch: the chart being recorded was closed half done, what followed went
  into a new file, the 25-second lock was lost between the two, and the
  re-align card appeared offering to rotate half a chart. It also happened
  on a band change and on picking WEFAX from the grid while already in
  WEFAX.
- **And it no longer leaves the re-align card invisible but active.** That
  same path wiped the whole panel area —card and preview included— with
  nothing to repaint it: the screen went clean while the panel's touch input
  stayed swallowed until the card timed out five minutes later.

---

## V5.53 · 2026-10-21

### Qué cambia

- **Los tres botones de banda del fax ya reciben fax.** FAX 4, FAX 8 y FAX
  14 sintonizaban 3.855, 7.880 y 13.882,5 kHz, que son las frecuencias que
  publica el DWD — y lo que publica es el centro de la emisión, no la
  portadora. En banda lateral alta el dial va 1,9 kHz por debajo; con el
  dial en la publicada, los tonos de la imagen caen en la banda lateral que
  la USB suprime y no entra nada. Ahora son 3.853,1 / 7.878,1 / 13.880,6
  kHz, y el manual da las dos cifras al lado de cada banda.
- **El manual contaba otra radio en cuatro sitios:** una velocidad de 180
  líneas por minuto que no existe; «cada fichero son 128 renglones, poco más
  de un minuto» cuando son 1.536 y doce minutos y medio; «el fax reserva 600
  líneas» cuando hace un mes que no reserva nada por delante; y que una
  carta a medias se cierra sola a los diez segundos de silencio, que es del
  SSTV y no del fax.

### What's new

- **The three fax band buttons now receive fax.** FAX 4, FAX 8 and FAX 14
  tuned 3,855, 7,880 and 13,882.5 kHz, which are the frequencies the DWD
  publishes — and what it publishes is the centre of the emission, not the
  carrier. On upper sideband the dial sits 1.9 kHz below; with the dial on
  the published figure, the picture tones land in the sideband that USB
  suppresses and nothing comes through. They are now 3,853.1 / 7,878.1 /
  13,880.6 kHz, and the manual gives both figures beside each band.
- **The manual described a different radio in four places:** a 180 lines per
  minute speed that does not exist; "each file is 128 rows, a little over a
  minute" when it is 1,536 and twelve and a half minutes; "the fax reserves
  600 lines" when it has not reserved anything up front for a month; and
  that a half-done chart closes itself after ten seconds of silence, which
  is SSTV's behaviour and not the fax's.

---

## V5.52 · 2026-10-21

### Qué cambia

- **Ya no se pierden los primeros segundos de cada carta cuando llegan
  seguidas.** Entre cerrar un fichero y abrir el siguiente había un cerrojo
  de un minuto. Se puso cuando lo único que cerraba era el botón; desde que
  cierra también el tono de fin, ese minuto se comía los primeros 17 a 40
  segundos de la carta siguiente — justo la franja de arriba, que es donde
  va la cabecera con la hora y el nombre de la estación. Ahora son 25
  segundos, que siguen siendo más de lo que tarda ninguna carta legítima en
  empezar a pintar.
- **Después de recolocar, la grabación vuelve a los 25 segundos** en vez de
  hasta un minuto.
- **Por dentro:** fuera otra constante muerta cuyo comentario describía un
  mecanismo que ya no existe, y el banco del cerrojo mide ahora el
  invariante en vez de una copia del número.

### What's new

- **Back-to-back charts no longer lose their first seconds.** There was a
  one-minute lock between closing one file and opening the next. It was
  added when only the button closed files; since the stop tone closes them
  too, that minute ate the first 17 to 40 seconds of the next chart — the
  top band, which is where the header with the time and station name goes.
  It is now 25 seconds, still longer than any legitimate chart takes to
  start painting.
- **After a re-align, recording resumes in 25 seconds** instead of up to a
  minute.
- **Under the hood:** another dead constant whose comment described a
  mechanism that no longer exists is gone, and the lock's test bench now
  measures the invariant instead of a copy of the number.

---

## V5.51 · 2026-10-21

### Qué cambia

- **Tocar el cartel de recolocar ya no mueve la línea de corte.** El cartel
  se dibuja encima de la imagen, así que un toque en el mensaje se colaba
  como un toque en la carta y la línea saltaba a esa columna — a un sitio
  que además tapa el propio cartel, así que lo único que se veía cambiar era
  el número de píxeles de al lado del OK.
- **Por dentro:** se quita una bandera que el arreglo de la versión anterior
  dejó muerta, y con ella un `if` que no se ejecutaba nunca y cuyo
  comentario decía que protegía la primera fila de todas las cartas.
- **Y el banco del fax prueba ahora tres caminos que no probaba nadie:** el
  tono de fin que cierra el fichero, la carta siguiente que se engancha
  sola, y el botón de Sincroniza para cuando se llega tarde a una carta.

### What's new

- **Tapping the re-align card no longer moves the cut line.** The card is
  drawn on top of the picture, so a tap on its message fell through as a tap
  on the chart and the line jumped to that column — to a spot hidden by the
  card itself, so the only visible change was the pixel count next to OK.
- **Under the hood:** a flag left dead by the previous release is gone, and
  with it an `if` that never ran whose comment claimed it protected the first
  row of every chart.
- **And the fax test bench now covers three paths nobody tested:** the stop
  tone that closes the file, the next chart locking on by itself, and the
  Sync button for when you tune in mid-chart.

---

## V5.50 · 2026-10-21

### Qué cambia

- **Se acabó la franja negra encima de todas las cartas.** El decodificador
  entraba en imagen en cuanto terminaba de engancharse, y lo que quedaba de
  señal de fase se pintaba como carta: 13 renglones a 60 líneas por minuto,
  26 a 120 y **86 a 240**, negros y rayados, encima de cada mapa y dentro
  del fichero guardado. Ahora, hechas las pasadas del enganche, sigue en
  fase —donde no se guarda nada— hasta que la señal se acaba de verdad. A
  las cuatro velocidades llegan 59 de cada 60 líneas de imagen y ninguna de
  la fase.
- **El botón de Sincroniza ya no puede rebotar solo.** Al forzar la imagen
  no se limpiaba el detector de tonos, así que si la media del tono de fin
  venía alta —que es justo lo que pasa cuando el decodificador no engancha,
  o sea cuando se usa el botón— la radio volvía a «buscando» en 20 ms. El
  botón parecía no hacer nada.
- **Cambiar de velocidad a mitad del enganche ya no deja la medida a
  medias.** El mando reiniciaba el seguidor de fase sin cerrar las
  interrupciones y publicaba la velocidad nueva antes del reloj viejo: entre
  una cosa y otra cabía una línea entera, y el tope de seguridad podía saltar
  justo ahí.
- **Dejar la línea de recolocar en el borde izquierdo ya no da error.** Es
  la forma de decir «déjala como está», y contestaba «No se ha podido
  recolocar» —y antes cerraba la carta que se estuviera grabando para nada—.
  Ahora dice «La carta se deja como esta» y no toca nada.

### What's new

- **No more black band at the top of every chart.** The decoder switched to
  image as soon as it finished locking on, so the rest of the phasing signal
  was painted as chart: 13 rows at 60 lines per minute, 26 at 120 and **86 at
  240**, black and striped, on top of every map and inside the saved file. It
  now stays in phasing — where nothing is recorded — until the signal really
  ends. At all four speeds 59 out of every 60 image lines arrive, and none
  from the phasing signal.
- **The Sync button can no longer bounce straight back.** Forcing image did
  not clear the tone detector, so if the stop-tone average happened to be
  high — which is exactly the case when the decoder will not lock on, i.e.
  when you reach for the button — the radio returned to "searching" within
  20 ms. The button looked dead.
- **Changing speed mid-lock no longer leaves half a measurement.** The knob
  reset the phase tracker without masking interrupts, and published the new
  speed before the new clock: a whole line fitted in between, and the safety
  timeout could fire right there.
- **Leaving the re-align line at the left edge is no longer an error.** That
  is how you say "leave it as it is", and it answered "Could not re-align" —
  after closing whatever chart was being recorded, for nothing. It now says
  "The chart is left as it is" and touches nothing.

---

## V5.49 · 2026-10-21

### Qué cambia

- **La carta ya no sale con renglones cosidos.** El anillo de ocho líneas
  entre el decodificador y el bucle tenía capacidad para siete, no para
  ocho: con el anillo lleno, lo que se entregaba era el renglón que se
  estaba escribiendo en ese momento —media línea de ahora y media de hace
  ocho—. Pasa cuando el bucle se entretiene, que es justo cuando se está
  grabando en la flash.
- **Y ya no quedan renglones de basura al pie.** Al llegar el tono de fin
  quedaban líneas sin recoger, y a partir de ahí se entregaba el buffer
  vivo: uno a tres renglones de ruido al final de la carta guardada, y a
  veces al principio de la siguiente.
- **A 60 líneas por minuto faltaba la franja de arriba de todas las
  cartas.** El enganche pedía 33 líneas de señal de fase y a esa velocidad
  solo caben 30 en los 30 segundos que dura; el decodificador se quedaba
  esperando una segunda pasada imposible, y en ese estado no se guarda
  nada. Se perdían 36 segundos de carta, que es donde va la cabecera con la
  hora. Ahora las pasadas se calculan desde la velocidad.
- **El tono de fin cierra la carta**, que era lo que faltaba: con el botón
  de guardar puesto, el fichero seguía abierto después del final y se
  tragaba el silencio y el principio de la carta siguiente.
- **La primera línea de todas las cartas salía a medias** —el salto del
  último ajuste de fase parte la línea en curso y esa línea se entregaba
  igual—, y el ajuste de fase **desperdiciaba una de sus dos pasadas**
  siempre que el pulso caía en la mitad derecha de la línea.
- **El botón «Sincroniza» podía no hacer nada**, sin dejar rastro.
- **Cambiar la velocidad a mitad de la señal de fase** podía sacar la carta
  con una inclinación disparatada.
- **El cartel de recolocar ya no aparece cuando la carta solo cambia de
  fichero** al llegar a su altura máxima, ni con la pantalla dormida o un
  menú delante. Y si la reescritura se corta por el camino lo dice tal cual:
  «Cortado: la carta queda a medias».
- **Un tono de fin perdido ya no cose dos cartas en un fichero.** De
  «imagen» solo se salía con el tono de parada, y en onda corta el final de
  una carta es justo donde se desvanece: si ese tono llegaba enterrado, el
  decodificador se quedaba en imagen para siempre y la carta siguiente se
  pintaba encima, a la velocidad de la anterior. Ahora un tono de arranque
  sostenido también cierra.
- **El renglón ya no se presta, se copia.** El decodificador entregaba un
  puntero a su anillo y el bucle se iba con él a escribir en la flash, que
  puede tardar cientos de milisegundos; en ese rato la interrupción podía
  volver a esa posición y escribir encima. Ahora la copia se hace con las
  interrupciones cerradas y el problema no puede existir.
- **Mientras decides si recolocar, la radio sigue grabando.** Si entra la
  carta siguiente, se guarda. Al aceptar se cierra ese fichero —queda entero
  y válido— porque la reescritura necesita para ella sola el bloque de
  trabajo del grabador, así que después de recolocar la grabación tarda
  hasta un minuto en volver a arrancar.
- **Y el cartel ya no deja la radio sin táctil.** Si no se puede ver —menú
  delante, pantalla dormida, o un mando CAT que ha cambiado de modo— deja de
  comerse los toques en vez de seguir siendo invisible y modal.

### What's new

- **Charts no longer come out with stitched lines.** The eight-line ring
  between decoder and main loop could only hold seven: with the ring full,
  what was handed over was the line being written at that moment — half of
  it from now and half from eight lines ago. It happens when the loop is
  busy, which is exactly when it is writing to flash.
- **And no more rubbish lines at the foot.** When the stop tone arrived,
  lines were left uncollected and from then on the live buffer was handed
  out: one to three lines of noise at the end of the saved chart, and
  sometimes at the start of the next one.
- **At 60 lines per minute the top strip of every chart was missing.** The
  lock-on needed 33 lines of phasing signal and only 30 fit in the 30
  seconds it lasts; the decoder waited for a second pass that could never
  come, and in that state nothing is saved. 36 seconds of chart were lost —
  where the header with the time goes. The number of passes is now derived
  from the line rate.
- **The stop tone now closes the chart**, which was missing: with the save
  button on, the file stayed open past the end and swallowed the silence
  and the beginning of the next chart.
- **The first line of every chart came out half-written** — the jump from
  the last phasing adjustment splits the line in progress, and that line was
  handed over anyway — and the phasing adjustment **wasted one of its two
  passes** whenever the pulse fell in the right-hand half of the line.
- **The «Sync» button could silently do nothing.**
- **Changing the line rate mid-phasing** could produce a wildly skewed
  chart.
- **The re-align card no longer appears when the chart is merely rolling
  over** to a new file at maximum height, nor with the screen asleep or a
  menu in front. And if the rewrite is cut short it says so plainly: «Cut
  short: the chart is half done».
- **A missed stop tone no longer stitches two charts into one file.** The
  only way out of «image» was the stop tone, and on shortwave the end of a
  chart is exactly where it fades: if that tone arrived buried, the decoder
  stayed in image for ever and the next chart was painted on top at the
  previous chart's speed. A sustained start tone now closes it too.
- **The line is copied, not lent.** The decoder handed out a pointer into
  its ring and the loop went off to write to flash with it, which can take
  hundreds of milliseconds; in that time the interrupt could come back round
  and overwrite it. The copy is now made with interrupts off, so the problem
  cannot exist.
- **While you decide whether to re-align, the radio keeps recording.** If
  the next chart arrives, it is saved. Accepting closes that file — whole
  and valid — because the rewrite needs the recorder's working block all to
  itself, so after a re-align recording takes up to a minute to start again.
- **And the card no longer leaves the radio without a touchscreen.** If it
  cannot be seen — menu in front, screen asleep, or a CAT command that
  changed mode — it stops swallowing touches instead of staying invisible
  and modal.

---

## V5.48 · 2026-10-21

### Qué cambia

- **El centrado de la carta se elige al final, con una línea, y no con un
  botón mientras entra.** El botón que corría la imagen 21 puntos por toque
  ha desaparecido. Al acabar una carta —porque llega el tono de fin o porque
  se deja de guardar— sale un cartel: **¿Recolocar la imagen?**. Diciendo que
  sí, el mismo cartel se convierte en un **OK** y aparece una **línea negra
  vertical** sobre la carta, que se mueve con el mando y tocando la pantalla.
  Donde se deje la línea pasa a ser el borde izquierdo: lo que estaba a su
  derecha se pega a la izquierda y lo que estaba a su izquierda se va al
  final. No se pierde ni un punto, y **se reescribe el fichero de la flash**,
  no solo lo que se ve.
- **Y por eso el fichero se graba sin centrar.** Con el botón de antes, una
  carta en la que se tocaba a mitad quedaba grabada con dos centrados dentro:
  una costura que ninguna recolocación posterior puede deshacer.
- **La chapa de estado del fax recupera 73 puntos de ancho**, que es el sitio
  que ocupaba aquel botón.

### What's new

- **The chart's alignment is now chosen at the end, with a line, instead of
  with a button while it comes in.** The button that shifted the picture by
  21 pixels per tap is gone. When a chart finishes — the stop tone arrives,
  or saving is turned off — a card asks: **Re-align the picture?**. Say yes
  and the same card turns into an **OK** button while a **vertical black
  line** appears over the chart; it moves with the knob and by touch. Wherever
  the line is left becomes the new left edge: what was to its right is moved
  against the left margin and what was to its left wraps round to the end.
  Not one pixel is lost, and **the file in flash is rewritten**, not just
  what is on screen.
- **That is why the file is now written un-centred.** With the old button, a
  chart you adjusted halfway through was stored with two different alignments
  inside it — a seam that no later re-alignment can undo.
- **The fax status chip gets 73 pixels of width back**, the room that button
  used to take.

---

## V5.47 · 2026-10-20

### Qué cambia

- **La distancia al oscilador se ve en la pantalla principal**, pegada al
  chip «NCO» de la barra de estado («NCO +12k»). Estaba en Información, y ahí
  no sirve: la pregunta que contesta —si el panorama se queda sordo a una
  frecuencia fija o a una distancia fija— hay que mirarla **en la cascada**, y
  no se pueden ver las dos pantallas a la vez.
- **La cabecera del BMP declara los renglones que hay escritos, no los que
  entraron.** Si la grabación se corta —disco lleno, o un bloque que no se
  pudo escribir— el contador incluía renglones que no llegaron al fichero: el
  BMP prometía más imagen de la que tiene dentro, que es justo como un visor
  dice que el fichero está dañado.
- **El cargador de arranque vuelve a caber en sus 32 kB** (`v2.19`). Se había
  pasado por 136 bytes. Lo que lo sacaba eran seis funciones de la biblioteca
  del fabricante que el cargador llamaba para encender la luz de la pantalla:
  cada una lleva dentro un `switch` con los catorce temporizadores o con los
  cuatro canales, y el enlazador se las trae enteras aunque aquí solo se use
  **un** canal de **un** temporizador. Ahora esas escrituras están puestas a
  mano, y **no por deducción**: un banco compila el fichero del fabricante tal
  cual en el PC y exige que los dos caminos dejen los mismos registros con los
  mismos valores. **896 bytes recuperados, 760 libres**, y ni un solo texto de
  pantalla tocado.

### What's new

- **The offset from the local oscillator is now on the main screen**, next to
  the «NCO» chip in the status bar («NCO +12k»). It used to be in the
  Information page, which is no use: the question it answers — whether the
  band goes deaf at a fixed frequency or at a fixed offset — has to be watched
  **on the waterfall**, and you cannot see both screens at once.
- **The BMP header now declares the rows actually written, not the rows that
  came in.** If recording is cut short — disk full, or a block that could not
  be written — the counter included rows that never reached the file, so the
  BMP promised more image than it holds. That is exactly how a viewer decides
  a file is damaged.
- **The bootloader fits in its 32 kB again** (`v2.19`). It had overflowed by
  136 bytes. What pushed it out were six vendor-library functions the
  bootloader called to turn the backlight on: each one carries a `switch` over
  all fourteen timers or all four channels, and the linker pulls them in whole
  even though only **one** channel of **one** timer is used here. Those
  register writes are now done by hand, and **not from guesswork**: a bench
  compiles the vendor file unmodified on the PC and requires both paths to
  leave the same registers holding the same values. **896 bytes recovered, 760
  free**, and not one on-screen string touched.

---

## V5.46 · 2026-10-20

### Qué cambia

- **La fila «Cuadratura I/Q» de Información dice ahora a qué distancia ha
  quedado la emisora del oscilador.** Es el número que faltaba para atacar el
  hueco de 78–100 kHz: con el NCO puesto el oscilador se queda quieto y es la
  sintonía la que se pasea entre 6 y 40 kHz de él. Si una zona del dial se
  queda sorda **siempre a la misma frecuencia**, el problema está delante del
  convertidor; si se queda sorda **siempre que ese número es grande**, el
  problema es dónde se aparca el oscilador. Son dos averías con el mismo
  aspecto y hasta ahora no había forma de distinguirlas desde la radio.

### What's new

- **The «I/Q quadrature» row in Information now shows how far the signal has
  ended up from the local oscillator.** It is the number that was missing to
  attack the 78–100 kHz gap: with the NCO on, the oscillator stays put and
  the tuning walks between 6 and 40 kHz from it. If a stretch of the dial
  goes deaf **always at the same frequency**, the problem is ahead of the
  converter; if it goes deaf **whenever that number is large**, the problem
  is where the oscillator parks. Two faults that look alike, with no way to
  tell them apart from the radio until now.

---

## V5.45 · 2026-10-20

### Qué cambia

- **Una carta de fax cabe entera, de verdad.** El límite estaba en 1280
  renglones (10 min 40 s) y la carta que recibiste hoy medía **1352** — se
  habría partido igual, por 72 renglones. Ahora son **1536 (12 min 48 s)**,
  que es un 14 % por encima de la carta más larga medida. Es el tercer número
  de hoy y el primero que sale de un dato en vez de una impresión.
- En una flash de 8 MB caben seis cartas enteras en vez de ocho.

### What's new

- **A fax chart really does fit in one file now.** The limit was 1280 rows
  (10 min 40 s) and the chart received today was **1352** — it would still
  have split, by 72 rows. It is now **1536 (12 min 48 s)**, 14 % above the
  longest chart actually measured. It is today's third value for this number
  and the first taken from data rather than an impression.
- An 8 MB drive holds six full charts instead of eight.

---

## V5.44 · 2026-10-20

### Qué cambia

- **El centrado da la vuelta entera a la línea.** Salió con ±49 puntos porque
  la primera carta estaba corrida 21; la segunda estaba corrida **269**, así
  que el desfase no es de la emisora, depende de dónde se enganche. Ahora el
  paso es de 21 puntos, el recorrido es la línea completa (36 posiciones) y
  **la mitad izquierda del botón corre hacia la izquierda y la derecha hacia
  la derecha**: 18 toques en el peor caso, dos o tres en el normal.
- **El botón de centrado del fax ahora se ve.** En V5.43 existía y respondía
  al dedo, pero no se dibujaba: en esa cabecera el tercer botón no tiene un
  ancho fijo, es lo que queda entre la zona de la barra y la chapa, y sin
  acortar esa zona el hueco salía negativo. Ya está, 130 px al lado de
  «Guardando».

### What's new

- **Centring now wraps the whole line.** It shipped with ±49 points because
  the first chart was 21 points out; the second was **269**, so the offset is
  not a property of the station — it depends on where the decoder latched. The
  step is now 21 points, the range is the full line (36 positions), and **the
  left half of the button goes left, the right half goes right**: 18 taps
  worst case, two or three normally.
- **The fax centring button is now visible.** In V5.43 it existed and
  responded to touch, but was never drawn: in that header the third button
  has no fixed width — it is whatever is left between the bar area and the
  badge — and without shortening that area the gap came out negative. Fixed,
  130 px next to the save button.

---

## V5.43 · 2026-10-20

### Qué cambia

- **Centrado del fax, a mano.** En el panel del fax, al lado de «Sincroniza» y del botón de guardar. Si la
  carta sale con un trozo del margen pegado al borde derecho, es que el pulso
  de sincronismo de esa emisora no cae donde el decodificador supone. Se corre
  la imagen de 7 en 7 puntos (±49, o sea ±6,5 % de la línea) hasta que el
  margen vuelve a su sitio. **No se pierde nada**: lo que sale por un lado
  entra por el otro. Se guarda en CONFIG.CSV, así que se ajusta una vez.
- Se aplica al renglón **ya terminado**, justo antes de entregarlo: no toca el
  enganche, ni el ritmo, ni el pulso de fase.

### What's new

- **Manual fax centring**, on the fax panel, next to «Sync» and the save button. If a
  chart comes out with a strip of the margin stuck to the right edge, that
  station's sync pulse does not fall where the decoder assumes. Shift the
  image 7 points at a time (±49, i.e. ±6.5 % of a line) until the margin is
  back in place. **Nothing is lost**: what leaves one side comes in the other.
  Saved in CONFIG.CSV, so it is set once.
- It is applied to the **finished** row, just before handing it over: it does
  not touch the sync lock, the line rate or the phasing pulse.

---

## V5.42 · 2026-10-20

### Qué cambia

- **Una carta de diez minutos cabe entera en un fichero.** El troceo por
  altura estaba en 1024 renglones, que a 120 lpm son **8 min 32 s** — menos
  que una carta del DWD, así que iba a partir en dos justo lo que tiene que
  salir de una pieza. Ahora son **1280 renglones (10 min 40 s, 949 kB)**. En
  una flash de 8 MB caben ocho cartas enteras en vez de diez: barato al lado
  de partir una carta buena por la mitad.

### What's new

- **A ten-minute chart now fits in a single file.** The height split was set
  at 1024 rows, which at 120 lpm is **8 min 32 s** — shorter than a DWD
  chart, so it would have cut in two exactly what is meant to come out whole.
  It is now **1280 rows (10 min 40 s, 949 kB)**. An 8 MB drive holds eight
  full charts instead of ten: cheap next to splitting a good chart in half.

---

## V5.41 · 2026-10-20

Lo que hacía falta para poder prometer que la riada de ficheros no vuelve.

### Qué cambia

- **El cerrojo lo arma el cierre, no el que cierra.** Para que no se abra un
  fichero detrás de otro hay un cerrojo de un minuto, y lo armaba *quien
  cerraba*. Bastaba un cerrador que no se acordara —y lo hubo— para que no se
  armara nunca. Ahora lo arma el propio cierre: aunque mañana aparezca otro
  cerrador que no he visto, lo peor posible es **un fichero por minuto**, no
  veinte.
- **Si el disco se llena a mitad de carta, el fichero se cierra en
  condiciones.** Antes se abandonaba: quedaba con la cabecera prometiendo
  1024 renglones —el visor pinta basura hasta abajo— y con el tramo entero
  reservado, hasta 64 kB desperdiciados justo cuando menos sobra. Ahora se
  rehace la cabecera con la altura real y se devuelve lo que no se usó.
- **Y se deja de reintentar lo que acaba de fallar.** Volcar la cola del
  buffer tras un fallo de escritura eran otros 300 ms de radio parada sin
  añadir un renglón.

### What's new

- **The lock is armed by the close, not by the closer.** To stop one file
  opening right after another there is a one-minute lock, and it used to be
  armed by *whoever closed*. One closer that forgot — and there was one — and
  it never armed. The close now arms it itself: even if another closer I have
  not found turns up, the worst possible case is **one file per minute**, not
  twenty.
- **If the drive fills mid-chart, the file is closed properly.** It used to be
  abandoned, leaving the BMP header promising 1024 rows — viewers paint
  garbage to the bottom — and the whole extent still reserved, up to 64 kB
  wasted exactly when there is none to spare. The header is now rewritten
  with the real height and the unused part returned.
- **And what just failed is not retried.** Flushing the buffer tail after a
  write failure was another 300 ms of stalled radio for no extra row.

---

## V5.40 · 2026-10-20

El fallo de los ficheros sueltos. No estaba donde lo busqué todo el día.

### Qué cambia

- **Se acabaron los ficheros de 8 kB.** `gsv_*` —el que escribe en el
  pendrive— es **uno solo**, compartido por SSTV, WEFAX y la captura de
  pantalla. Y el bucle principal llama a `fax_poll()` y a `sstv_poll()` en la
  **misma vuelta y siempre**, esté activo el modo que esté. `sstv_poll()`
  empezaba con «si el SSTV no está activo, cierra lo que haya»: en WEFAX, el
  fax abría la carta y escribía la cabecera, y **la línea siguiente del bucle
  la cerraba**. Como el cerrojo de un minuto sólo se arma cuando cierra el
  propio `fax_poll()`, no se armaba nunca y a la vuelta siguiente se abría
  otro fichero. 8 kB = la cabecera más el primer bloque de renglones.
- **Ahora el fichero tiene dueño.** El prefijo con el que se abrió —«WFAX»,
  «SSTV», «PANT»— ya estaba guardado; quien cierra tiene que decir de quién
  es lo que viene a cerrar, y si no coincide no toca nada.
- **Y la carta de fax ya no se cierra por silencio.** Ese cierre a los diez
  segundos es del SSTV. El fax se cierra con el botón, que es lo pedido.

### What's new

- **No more 8 kB files.** `gsv_*` — the thing that writes to the drive — is a
  **single** machine shared by SSTV, WEFAX and the screen capture. The main
  loop calls `fax_poll()` and `sstv_poll()` in the **same pass, always**,
  whichever mode is active. `sstv_poll()` opened with "if SSTV is not active,
  close whatever is open": in WEFAX the fax opened the chart and wrote the
  header, and **the very next line of the loop closed it**. Since the
  one-minute lock only arms when `fax_poll()` itself closes, it never armed,
  and the next pass opened another file. 8 kB = the header plus the first
  block of rows.
- **The file now has an owner.** The prefix it was opened with — "WFAX",
  "SSTV", "PANT" — was already stored; whoever closes must say whose file
  they came to close, and leaves it alone if it does not match.
- **And the fax chart no longer closes on silence.** That ten-second close
  belongs to SSTV. The fax closes with the button, as asked.

---

## V5.39 · 2026-10-20

Dos fallos de la versión de ayer, que el usuario encontró al primer intento.

### Qué cambia

- **Los botones vuelven a responder mientras graba.** La carta crecía
  llamando a `spi_flash_anade_trozo()` una vez por bloque de 4 kB, y eso
  cuesta **303 ms medidos** de chip parado: cada dos segundos y medio la
  radio dejaba de mirar la pantalla táctil. Ahora el fichero se alarga a
  **tramos de 64 kB** y dentro escribe a direcciones ya conocidas: **51 ms
  por bloque**, y la parada larga pasa de cada 2,5 s a cada 40 s.
- **Una carta es UN fichero.** Con el camino de antes, el primer fallo
  cerraba el fichero y el siguiente intento abría otro: lo que quedaba en el
  pendrive era `WFAX001..WFAX020` con 4 kB cada uno, la cabecera y nada más.
  Ahora el fichero se crea una vez y todas las demás escrituras lo alargan.
- **Y se devuelve lo que sobra.** Al cerrar, los bloques no usados del último
  tramo —hasta 60 kB— vuelven al pendrive, siguiendo la cadena del fichero.
- **Si el disco está repartido, sigue grabando.** Hacen falta 64 kB seguidos,
  no 774; y si tampoco los hay, se piden la mitad, y la mitad de la mitad,
  hasta un solo bloque.
- **Entrar en WEFAX ya no empieza a grabar solo.** El botón se guardaba en
  los ajustes, así que al entrar en el modo la radio abría fichero sin que
  nadie tocara nada. Ahora arranca siempre en «No guardar».

### What's new

- **Buttons respond again while recording.** The chart file grew by calling
  `spi_flash_anade_trozo()` once per 4 kB block, and that costs a **measured
  303 ms** with the chip busy: every 2.5 seconds the radio stopped looking at
  the touch panel. The file now grows in **64 kB extents** and writes to
  already-known addresses inside them: **51 ms per block**, and the long
  stall drops from every 2.5 s to every 40 s.
- **One chart is ONE file.** Previously the first failure closed the file and
  the next attempt opened another one, leaving `WFAX001..WFAX020` of 4 kB
  each on the drive — the header and nothing else. The file is now created
  once and every later write extends it.
- **And the leftovers come back.** On close, the unused blocks of the last
  extent — up to 60 kB — are returned to the drive, following the file's
  cluster chain.
- **A fragmented drive still records.** It needs 64 kB contiguous, not 774;
  and if even that is missing it asks for half, and half of that, down to a
  single block.
- **Entering WEFAX no longer starts recording by itself.** The save button
  was stored in the settings, so entering the mode opened a file with nobody
  touching anything. It now always starts at "Do not save".

---

## V5.38 · 2026-10-20

Revisión a fondo del WEFAX entero, pasada tras pasada hasta que dos
seguidas no encontraran nada. Esto es lo que salió.

### Qué cambia

- **La carta ya no se reserva de una pieza: el fichero crece.** Se monta con
  `spi_flash_anade_trozo()`, de 4 kB en 4 kB y cada trozo donde quepa, así
  que **no hace falta ni un byte de espacio seguido**. Una carta entera, un
  fichero — se acabó trocearla para que entrara. Y se acabó el «no cabe» con
  el disco medio vacío.
- **GRAVE, y llevaba ahí desde siempre: el decodificador sólo tenía DOS
  buffers de línea.** El bucle recibía el puntero a la línea cerrada y se
  iba a escribirla en la flash —decenas de milisegundos—, y dos líneas
  después la interrupción volvía a ese mismo buffer y escribía encima de lo
  que el bucle aún estaba leyendo. Eso son renglones partidos por la mitad.
  Y con una sola bandera, si se cerraban dos líneas antes de que el bucle
  pasara, la primera desaparecía. Ahora hay un anillo de ocho, que es lo que
  ya se hizo en SSTV por este mismo motivo.
- **GRAVE: el escritor de bloques borraba lo que no era suyo.**
  `spi_flash_write_block_4k()` redondeaba la dirección **hacia abajo** y
  borraba y reescribía ese sector entero — con una dirección no alineada se
  llevaba hasta 4 kB del vecino y además dejaba los datos corridos. Es el
  mismo fallo que borró un `CONFIG.CSV` esta tarde, y estaba también en el
  escritor principal. No saltaba porque las direcciones de fichero están
  alineadas **sólo si Windows formateó el volumen así**.
- **El aviso dice el motivo de verdad.** Antes decía siempre «pide n kB
  seguidos» —que desde hoy ni siquiera es cierto— y sólo salía al abrir. Si
  el disco se llenaba a mitad de carta, la grabación se paraba en silencio.
  Ahora el motivo lo da quien lo sabe y sale en los dos sitios.
- **Y el cartel ya no se recortaba:** cabían 39 caracteres y el motivo se
  cortaba sin decirlo.

### What's new

- **A chart is no longer reserved in one piece: the file grows.** It is
  built with `spi_flash_anade_trozo()`, 4 kB at a time, each chunk wherever
  it fits — so **not one byte of contiguous space is needed**. One chart,
  one file. No more splitting it up to make it fit, and no more «no room»
  with a half-empty disk.
- **SERIOUS, and it had always been there: the decoder had only TWO line
  buffers.** The loop took the pointer to the closed line and went off to
  write it to flash — tens of milliseconds — and two lines later the
  interrupt came back to that same buffer and wrote over what the loop was
  still reading. That is torn rows. And with a single flag, if two lines
  closed before the loop came round, the first vanished. There is now a ring
  of eight, which is what SSTV already got for the same reason.
- **SERIOUS: the block writer erased what was not its own.**
  `spi_flash_write_block_4k()` rounded the address **down** and erased and
  rewrote that whole sector — with an unaligned address it took up to 4 kB
  of the neighbour and wrote the data at the wrong offset. Same fault that
  erased a `CONFIG.CSV` this afternoon, and it was in the main writer too.
- **The notice states the real reason**, and appears mid-chart too, not just
  when opening.
- **And it no longer truncated:** 39 characters fit, and the reason was cut
  off without saying so.

## V5.37 · 2026-10-20

### Qué cambia

- **Encontrado lo que llenaba el disco: los ficheros vacíos se quedaban con
  su reserva entera.** Una carta se reserva de una pieza —448 kB— y la
  entrada de directorio se escribe ya con el tamaño final. Al cerrar, un
  fichero con cero renglones **se salía sin recortar**, así que esos 448 kB
  seguían marcados como suyos en la tabla de asignación. Catorce de esos son
  seis megas y medio. Y desde fuera no se ve: Windows enseña el tamaño de la
  entrada de directorio mientras la tabla dice otra cosa — de ahí «tengo
  espacio de sobra» con un disco que no admite una carta más. Ahora se
  recorta a un bloque.
- **Y una carta pide 100 kB seguidos, no 448.** De 600 renglones a 128. Lo
  que manda para poder abrir un fichero no es el hueco libre sino **el hueco
  seguido más grande**, y ese se parte en cuanto hay unos cuantos ficheros
  repartidos. 100 kB entran casi en cualquier sitio. El precio son más
  ficheros —a 120 lpm, 64 segundos cada uno— y no hay hueco entre ellos:
  cuando uno se llena, el siguiente se abre en la misma vuelta.
- **Y el cartel dice el número de verdad**, calculado y no escrito a mano:
  si cambia el tamaño del fichero, cambia el cartel.

### What's new

- **Found what was filling the disk: empty files kept their whole
  reservation.** A chart is reserved in one piece — 448 kB — and the
  directory entry is written with the final size up front. On close, a file
  with zero lines **returned without trimming**, so those 448 kB stayed
  marked as its own in the allocation table. Fourteen of those is six and a
  half megabytes. And it is invisible from outside: Windows shows the
  directory entry's size while the table says otherwise — hence «I have
  plenty of space» with a disk that will not take one more chart. It is now
  trimmed to one block.
- **And a chart asks for 100 kB in a row, not 448.** From 600 lines to 128.
  What decides whether a file can be opened is not free space but **the
  largest contiguous hole**, and that breaks up as soon as a few files are
  scattered about. 100 kB fits almost anywhere. The price is more files — at
  120 lpm, 64 seconds each — with no gap between them.
- **And the notice states the real number**, computed rather than written by
  hand.

## V5.36 · 2026-10-20

### Qué cambia

- **El botón de guardar del WEFAX es ahora un interruptor, y nada más.** Lo
  pones y abre un fichero; todo lo que decodifica se escribe ahí; lo quitas
  —o te sales del modo— y lo cierra. Punto.
- **Por qué hacía falta.** Hasta ahora el fichero se abría y se cerraba
  siguiendo al **decodificador**: al entrar en imagen y al salir. Y el
  decodificador entra y sale constantemente con señal justa. De ahí salieron
  en un mismo día, por tres causas distintas, veintisiete ficheros de una
  sesión, luego diez minutos de carta sin grabar nada, y luego veintiséis
  ficheros de 8 kB. Eran tres intentos de hacer bien una máquina de estados
  **que no tenía que existir**.
- **Y si no hay sitio, sale un cartel grande encima del espectro**, no un
  rótulo en otra pantalla: «NO CABE: PIDE 448 kB SEGUIDOS, HAY nnn kB». Con
  el número, porque «no cabe» con medio disco libre no se entiende hasta que
  ves que lo que falta es sitio **seguido**.
- **Un cerrojo de un minuto entre fichero y fichero.** No protege de nada
  conocido; está porque el mismo síntoma volvió tres veces por tres causas
  distintas, y un tope que no depende de haber entendido la causa es lo
  único que cubre la cuarta. Un fichero que se llena —600 renglones, cinco
  minutos— continúa en el siguiente sin esperar.
- **Arreglado: una regla del comprobador se había perdido.** Al quitar un
  banco, el corte se llevó por delante la línea que compila el del WEFAX, y
  durante unas horas `make comprueba` ejecutó un **binario viejo** que ya
  nadie reconstruía. Un banco que no se recompila es un banco que miente.

### What's new

- **The WEFAX save button is now a switch, and nothing else.** Turn it on
  and it opens a file; everything decoded goes in there; turn it off — or
  leave the mode — and it closes. Full stop.
- **Why it was needed.** Until now the file opened and closed following the
  **decoder**: on entering image and on leaving it. And the decoder enters
  and leaves constantly on a marginal signal. That produced, in one day and
  from three different causes, twenty-seven files in one session, then ten
  minutes of chart with nothing saved, then twenty-six 8 kB files. Three
  attempts at getting right a state machine **that should not have existed**.
- **And if there is no room, a large notice appears over the spectrum**, not
  a label on another screen: «NO ROOM: NEEDS 448 kB IN A ROW, THERE IS nnn
  kB». With the number, because «no room» with half the disk free makes no
  sense until you see that what is missing is room **in a row**.
- **A one-minute lock between files.** It guards against nothing known; it
  is there because the same symptom came back three times from three
  different causes, and a cap that does not depend on having understood the
  cause is the only thing that covers the fourth. A file that fills — 600
  lines, five minutes — continues into the next without waiting.
- **Fixed: a rule in the checker had been lost.** When a bench was removed,
  the cut took the WEFAX bench's compile line with it, and for a few hours
  `make comprueba` ran an **old binary** that nobody rebuilt. A bench that
  is not recompiled is a bench that lies.

## V5.35 · 2026-10-20

### Qué cambia

- **GRAVE, y era mío: el firmware podía borrar ficheros ajenos del disco.**
  Un usuario mandó su `CONFIG.CSV`: **1.536 bytes a 0xFF con la entrada de
  directorio intacta**, que es la firma de un borrado de sector por
  dirección de chip. La causa: `spi_flash_borrado_paso()` redondeaba el
  cursor **hacia abajo** al límite de sector, o sea podía empezar a borrar
  **por debajo** de donde le decían. No saltaba a la vista porque todo lo
  que lo llamaba hasta la V5.33 trabajaba sobre zonas alineadas a 4 kB —
  pero una reserva de fichero empieza donde empiece su *cluster*, y la zona
  de datos de un volumen formateado por Windows no tiene por qué caer en un
  límite de 4 kB. Con la reserva desalineada se salía por el principio, y si
  ese límite caía en un múltiplo de 64 kB se llevaba **64 kB del vecino de
  un golpe**. Ahora redondea hacia arriba, que no puede salirse.
- **Y el «borrado por delante» de la V5.33 se retira entero.** Era quien
  llamaba a eso con una zona desalineada. Ganaba 45 ms por bloque, uno cada
  dos minutos, y se jugaba los ficheros del usuario — y encima, medido
  después, **no era la causa del atasco** que iba a arreglar: 51 ms de cada
  2.700 son el 2% del tiempo, no un tercio.
- **Quitado también lo de borrar las cartas cortas**, que metí en la V5.34.
  Era lo único de todo ese camino que *destruye* algo que ya existía. Un
  fichero feo molesta; tirar una carta de diez minutos no se arregla.
- **Una carta ya no pide 448 kB seguidos, sino 193.** Baja de 600 a 256
  renglones por fichero, y **cuando uno se llena se abre el siguiente
  enseguida**, así que una carta larga sigue grabándose en WFAX002,
  WFAX003… Antes se grababan los primeros cinco minutos y el resto se perdía
  en silencio.
- **Y la cabecera del fax dice lo que está haciendo el guardado**, mientras
  recibes y sin ir a otra pantalla: «no guarda», «listo», «WFAX003 128» o
  «sin sitio». Cuatro palabras que separan «no estaba puesto» de «está
  roto».

### What's new

- **SERIOUS, and it was mine: the firmware could erase other files off the
  disk.** A user sent his `CONFIG.CSV`: **1,536 bytes of 0xFF with the
  directory entry intact**, which is the signature of a sector erase by chip
  address. The cause: `spi_flash_borrado_paso()` rounded the cursor **down**
  to the sector boundary, so it could start erasing **below** where it was
  told to. It did not show because everything that called it before V5.33
  worked on 4 kB-aligned areas — but a file reservation starts wherever its
  cluster starts, and the data area of a Windows-formatted volume need not
  land on a 4 kB boundary. Misaligned, it ran off the front, and if that
  boundary fell on a 64 kB multiple it took **64 kB of the neighbour at
  once**. It now rounds up, which cannot run off.
- **And V5.33's «erase ahead» is withdrawn entirely.** It was what called
  that with a misaligned area. It gained 45 ms per block, one every two
  minutes, and gambled the user's files — and, measured afterwards, **it was
  not the cause of the stall** it was meant to fix: 51 ms out of every 2,700
  is 2% of the time, not a third.
- **Also removed: deleting short charts**, added in V5.34. It was the only
  thing in that whole path that *destroys* something already there.
- **A chart no longer needs 448 kB in a row, but 193.** Down from 600 to 256
  lines per file, and **when one fills the next opens straight away**, so a
  long chart keeps recording into WFAX002, WFAX003… Before, the first five
  minutes were saved and the rest was lost in silence.
- **And the fax header states what saving is doing**, while you receive and
  without going to another screen: «not saving», «ready», «WFAX003 128» or
  «no room».

## V5.34 · 2026-10-20

### Qué cambia

- **Arreglado: una carta de fax dejaba veintisiete ficheros en el disco.** El
  fichero se cerraba en cuanto el decodificador se salía de imagen **un
  instante**, y con señal justa el enganche se pierde medio segundo y vuelve
  cada poco. Cada parpadeo cerraba la carta y abría otra — y cada apertura
  reserva 448 kB y recorre la tabla de asignación entera. Ahora el estado
  tiene que llevar **cinco segundos** fuera de imagen para dar la carta por
  acabada. El tono de parada de verdad la cierra igual, cinco segundos más
  tarde, sin perder un renglón.
- **Y lo que no llega a dieciséis renglones se borra al cerrar.** Antes, una
  carta de cero renglones se dejaba en el directorio con sus 448 kB
  reservados y su cabecera dentro. Dieciséis renglones a 120 lpm son ocho
  segundos: por debajo de eso no hay nada que mirar.

### What's new

- **Fixed: one fax chart left twenty-seven files on the disk.** The file was
  closed the moment the decoder dropped out of image **for an instant**, and
  with a marginal signal the lock drops for half a second every so often.
  Every flicker closed the chart and opened another — and every open reserves
  448 kB and walks the whole allocation table. The state must now be out of
  image for **five seconds** before the chart is called finished. The real
  stop tone still closes it, five seconds later, without losing a line.
- **And anything under sixteen lines is deleted on close.** A zero-line chart
  used to stay in the directory with its 448 kB reserved and its header
  inside. Sixteen lines at 120 lpm is eight seconds: below that there is
  nothing to look at.

## V5.33 · 2026-10-20

### Qué cambia

- **Fuera el bus automático de la V5.31. Era mío y era malo.** Lo monté sobre
  una suposición —que ASET valía 3— y la radio dice que vale 2: con el valor
  de verdad, la cuenta predice lo contrario de lo que se midió. Y de paso
  movía el bus de los 45 ns del cargador a 50. **Los 45 ns de serie ya
  estaban en la zona silenciosa**, así que lo único que hacía el automático
  era meter ruido. La fila «Bus pantalla» vuelve a ser la de la V5.30.
- **Guardar una carta de fax ya no para el bucle cada dos segundos.** Escribir
  4 kB sobre flash con datos viejos obliga a borrar el sector y reprogramar:
  **51 ms** medidos contra el chip simulado con los tiempos del fabricante, y
  eso cada 2,7 segundos durante los diez minutos que dura una carta. Ahora la
  zona se borra **por delante**, un borrado por vuelta del bucle, y cada
  bloque se programa sobre flash limpia: **6 ms**. Preparar cuesta 1,5 s una
  sola vez, repartidos en 21 vueltas de 45 a 150 ms.
- **Y una carta abre un fichero, no diez.** Desde que el fichero se abre sin
  esperar al renglón cero, un parpadeo del enganche lo cerraba y lo volvía a
  abrir — y cada apertura reserva 448 kB. Ahora se abre una vez por carta.

- **Y un chip de 2 MB que dice ser de 1 ya no pierde la mitad.** Un usuario
  del repositorio mandó la foto de su flash: pone **W25Q16JVSIQ**, que son
  2 MB. Y el identificador que contesta es **EF 40 14**, que como código de
  capacidad significa 1 MB. La etiqueta de fábrica y lo que el chip dice de
  sí mismo no coinciden, y nosotros creíamos al código. Ahora **manda la
  medida**: leer en 1 MB y que salga algo distinto de lo que hay en 0 sólo
  es posible si hay más chip —en uno de 1 MB esa dirección ES físicamente la
  0— así que eso no es una estimación, es una demostración. Se le cree al
  chip, no a su etiqueta.
- **Y la medida ya funciona con el chip casi vacío.** Miraba tres ventanas
  repartidas por el chip (en 0, a un cuarto y a la mitad), y en un disco
  recién formateado dos de las tres están en blanco: sin datos no se
  distingue nada, así que no medía y mandaba el código. Ahora las tres van
  pegadas al principio, donde cualquier volumen FAT tiene el sector de
  arranque, la tabla de asignación y el directorio raíz.

### What's new

- **The V5.31 automatic bus is gone. It was mine and it was bad.** I built it
  on an assumption — that ASET was 3 — and the radio says it is 2: with the
  real value the arithmetic predicts the opposite of what was measured. And
  it moved the bus from the bootloader's 45 ns to 50. **The stock 45 ns was
  already in the quiet zone**, so all the automatic did was add noise. The
  «Display bus» row goes back to what it was in V5.30.
- **Saving a fax chart no longer stalls the loop every two seconds.** Writing
  4 kB over flash that holds old data forces a sector erase and a
  reprogram: **51 ms** measured against the simulated chip with the
  manufacturer's timings, and that every 2.7 seconds for the ten minutes a
  chart lasts. The area is now erased **ahead**, one erase per loop pass, and
  each block is programmed onto clean flash: **6 ms**. Preparing costs 1.5 s
  once, spread over 21 passes of 45 to 150 ms.
- **And one chart opens one file, not ten.** Since the file opens without
  waiting for line zero, a flicker of the lock closed it and reopened it —
  and every open reserves 448 kB. Now it opens once per chart.
- **And a 2 MB chip that claims to be 1 MB no longer loses half of itself.**
  A user of the repository sent a photo of his flash: it reads
  **W25Q16JVSIQ**, which is 2 MB. And the id it answers is **EF 40 14**,
  which as a capacity code means 1 MB. The factory marking and what the chip
  says about itself disagree, and we believed the code. Now **the
  measurement wins**: reading at 1 MB and getting something different from
  what is at 0 is only possible if there is more chip — on a 1 MB part that
  address IS physically 0 — so that is not an estimate, it is a proof.
- **And the measurement now works on a nearly empty chip.** It looked at
  three windows spread across the chip (at 0, a quarter in and half way in),
  and on a freshly formatted disk two of the three are blank: with no data
  nothing can be told apart, so it did not measure and the code took over.
  The three windows now sit near the start, where any FAT volume keeps its
  boot sector, allocation table and root directory.

## V5.32 · 2026-10-20

### Qué cambia

- **Arreglado: el WEFAX podía estar recibiendo con «BMP gris» puesto y no
  guardar nada.** El fichero sólo se abría si el contador de renglones valía
  cero, y eso únicamente es cierto en el instante en que el decodificador
  entra en imagen. Si encendías el botón con la carta ya empezada —que es lo
  normal, porque la radio arranca en «No guardar»— esa condición no se volvía
  a cumplir hasta la carta siguiente. **Y no avisaba**: el botón seguía
  diciendo «BMP gris» como si todo fuera bien.
- **Ahora se abre en cuanto hay imagen y el botón lo pide.** Si te enganchas
  a mitad de carta, el fichero empieza ahí. Media carta guardada es
  infinitamente más que ninguna, y lo que ya pasó no se puede recuperar.
- **Y si no hay sitio, se intenta una vez y no más.** Reservar son 448 kB
  seguidos y un barrido de la FAT entera; sin freno, a 120 lpm serían dos
  barridos por segundo durante toda la carta. Tocar el botón vuelve a darle
  una oportunidad, que es lo que ese botón ya hacía.

### What's new

- **Fixed: WEFAX could be receiving with «Grey BMP» set and save nothing.**
  The file was only opened if the line counter was zero, and that is only
  true at the instant the decoder enters image. If you turned the button on
  with the chart already running — which is the normal case, since the radio
  starts at «Do not save» — that condition did not come true again until the
  next chart. **And it said nothing**: the button went on reading «Grey BMP»
  as if all were well.
- **It now opens as soon as there is an image and the button asks for it.**
  If you join mid-chart, the file starts there. Half a chart saved is
  infinitely more than none, and what already went by cannot be recovered.
- **And if there is no room, it tries once and no more.** Reserving means
  448 kB in a row and a full sweep of the FAT; with no brake, at 120 lpm that
  would be two sweeps a second for the whole chart. Tapping the button gives
  it another go, which is what that button already did.

## V5.31 · 2026-10-20

### Qué cambia

- **El bus de la pantalla se elige solo, según dónde estés escuchando.** Lo
  que radia ese bus no son armónicos sueltos: son los dieciséis hilos de
  datos cambiando en cada escritura, o sea ruido de banda ancha con una
  envolvente que vale **cero en los múltiplos de 1/T**, siendo T el ciclo de
  una escritura. Y T lo pone el firmware, así que se puede poner un cero
  justo encima de la emisora. Ahora se calcula en cada clic del mando.
- **La cuenta coincide con lo que marcó el S-metro.** Con el bus de esta
  radio, en 97,8 MHz elige 50 ns — que es exactamente el ajuste que se había
  medido a mano como el más silencioso, sin que nadie se lo dijera. En toda
  la banda de FM baja la envolvente **7,2 dB de media** y en ninguna emisora
  la sube. El precio es un bus **un 11% más lento de media**, nunca más de un
  tercio.
- **Nunca acelera y nunca se pasa.** El suelo es el ciclo que dejó el gestor
  de arranque — el único que sabemos que esta pantalla aguanta — y el techo
  son doce ciclos. Lo peor que puede hacer es pintar algo más despacio. En
  onda corta no hay ningún cero al alcance, así que ahí no toca nada.
- **«Bus pantalla» empieza ahora en «auto».** Un toque lo apaga y recorre la
  tabla a mano como antes; al acabarla vuelve a automático. No es fila nueva
  porque la página de Pantalla ya está llena, y porque automático y manual
  son el mismo ajuste.
- **Arreglado: «Pendiente del bus» no enseñaba su valor.** La fila se añadió
  en V5.30 con etiqueta y sin número, así que tocarla cambiaba algo que no se
  veía. Ahora dice el nivel y los MHz.

### What's new

- **The display bus picks itself, based on where you are listening.** What
  that bus radiates is not a few harmonics: it is sixteen data lines changing
  on every write, that is, wideband noise with an envelope that is **zero at
  multiples of 1/T**, T being one write cycle. And T is set by the firmware,
  so a zero can be placed right on top of the station. It is now computed on
  every click of the knob.
- **The arithmetic agrees with what the S-meter read.** With this radio's
  bus, at 97.8 MHz it picks 50 ns — exactly the setting that had been
  measured by hand as the quietest, without being told. Across the FM band it
  lowers the envelope by **7.2 dB on average** and raises it at no station.
  The price is a bus **11% slower on average**, never more than a third.
- **It never speeds up and never overshoots.** The floor is the cycle the
  bootloader left — the only one we know this panel tolerates — and the
  ceiling is twelve cycles. The worst it can do is paint a little slower. On
  shortwave there is no zero within reach, so there it changes nothing.
- **«Display bus» now starts at «auto».** One tap turns it off and walks the
  manual table as before; at the end of the table it returns to auto. It is
  not a new row because the Display page is already full, and because auto
  and manual are the same setting.
- **Fixed: «Bus slew rate» did not show its value.** The row shipped in V5.30
  with a label and no number, so tapping it changed something you could not
  see. It now states the level and the MHz.

## V5.30 · 2026-10-20

### Qué cambia

- **La radio se mete ruido a sí misma al pintar el espectro.** Medido sin
  antena en 97,8 MHz: el S-metro marca **S9+21** con la pantalla de radio
  dibujando y **S9+7** con el menú abierto, que es la misma radio sin
  dibujar. Catorce decibelios que no vienen de fuera: los hacen los 211.000
  píxeles por segundo que salen por las dieciséis líneas del bus de la
  pantalla.
- **«Bus pantalla» ya puede ir también a más lento.** Sólo sabía bajar
  ciclos —o sea ir más rápido—, porque se puso para ganar milisegundos.
  Ahora recorre 4, 5, 6, 8, 10, 12 y 15 y vuelve a empezar. Hace falta
  porque lo que ese ajuste mueve es un **peine de armónicos**: con 5 ciclos
  sale S9+7, con 4 sale +14, con 3 parpadea entre los dos y con 2 vuelve a
  +14. No sube y baja poco a poco — cae dentro de la emisora o cae fuera, y
  para sacarlo hay que poder moverlo en las dos direcciones.
- **Fila nueva: «Pendiente del bus».** 50 MHz al arrancar, y cada toque baja
  a 25 y a 2. Es lo único de este bus que cambia la **energía** de cada
  flanco en vez de moverla de sitio, así que es lo que puede bajar el ruido
  entero en lugar de cambiarlo de frecuencia. Tampoco se guarda: si un nivel
  deja la pantalla con basura, apagar y encender lo deshace.
- **Y la medida del tamaño del chip ya no se decide con 64 bytes.** Declaraba
  el alias —y por tanto el tamaño— comparando UNA ventana de 64 bytes, así
  que una copia casual del principio del chip un megabyte más arriba lo
  habría hecho anunciar la mitad. Ahora compara tres ventanas repartidas y
  tienen que coincidir las tres; una ventana toda igual no cuenta, porque en
  una zona borrada todo coincide con todo.

### What's new

- **The radio feeds noise to itself when it paints the spectrum.** Measured
  with no antenna at 97.8 MHz: the S-meter reads **S9+21** with the radio
  screen drawing and **S9+7** with the menu open, which is the same radio not
  drawing. Fourteen decibels that do not come from outside: they come from
  the 211,000 pixels a second going out over the display bus's sixteen
  lines.
- **«Display bus» can now also go slower.** It only knew how to drop cycles
  — that is, go faster — because it was added to gain milliseconds. It now
  walks 4, 5, 6, 8, 10, 12, 15 and starts over. That matters because what
  the setting moves is a **comb of harmonics**: at 5 cycles it reads S9+7, at
  4 it reads +14, at 3 it flickers between the two and at 2 it is back to
  +14. It does not rise and fall gradually — it lands on the station or it
  does not, and to get it off you need to move it both ways.
- **New row: «Bus slew rate».** 50 MHz at startup, and each tap drops to 25
  and to 2. It is the only thing about this bus that changes the **energy**
  of each edge instead of moving it around, so it is what can lower the noise
  altogether rather than shift its frequency. Not saved either: if a level
  leaves the screen full of garbage, a power cycle undoes it.
- **And the chip-size measurement is no longer decided by 64 bytes.** It
  declared the alias — and therefore the size — by comparing ONE 64-byte
  window, so a chance copy of the chip's start one megabyte higher up would
  have made it announce half the chip. It now compares three spread-out
  windows and all three must match; a window that is all one value does not
  count, because in an erased area everything matches everything.

---

## V5.29 · 2026-10-20

### Qué cambia

- **El S-metro pasa de 31 ms a 3.** Era lo que quedaba después del reloj, y
  es peor que el reloj porque no corre una vez por minuto: corre **cada vez
  que la barra cambia de segmento**, o sea constantemente mientras entra
  señal.
- Dos cosas a la vez. Su ventana llegaba hasta la pastilla del mando
  («Sintonía 1 kHz»), que **no se mueve con la señal**: ahora acaba justo
  donde esa pastilla empieza, y de alto pinta las 26 filas donde vive la
  barra en vez de las 40 de la franja. De 16.805 accesos al bus a 5.476.
- Y lo gordo: **lo que no entra en la ventana ya no se calcula**. El
  recorte vivía dentro de cada primitiva, así que la pastilla y los cinco
  chips de la derecha se medían enteros —el texto de cada uno, su ancho, el
  sitio que queda— aunque la ventana acabara mucho antes de llegar a ellos.
  No se pintaban, pero se calculaban. Y una vez por banda.
- **Comprobado pixel a pixel** en cinco escenas más: la barra bajando, S9
  justo donde cambia de color, por encima de S9 con el +21, de S9+60 a cero
  —el que más borra— y con el mando en volumen, que mueve la pastilla y por
  tanto el borde de la ventana. Ni un píxel distinto en los 384.000 del
  marco.

### What's new

- **The S-meter goes from 31 ms to 3.** It was what remained after the
  clock, and it is worse than the clock because it does not run once a
  minute: it runs **every time the bar changes segment**, which is
  constantly while a signal is coming in.
- Two things at once. Its window reached as far as the knob pill («Tuning
  1 kHz»), which **does not move with the signal**: it now stops right where
  that pill begins, and vertically it paints the 26 rows where the bar lives
  instead of the strip's 40. From 16,805 bus accesses down to 5,476.
- And the big one: **what falls outside the window is no longer computed**.
  The clipping lived inside each primitive, so the pill and the five chips
  on the right were measured in full — each one's text, its width, the room
  left — even when the window ended long before reaching them. They were not
  painted, but they were computed. Once per band.
- **Checked pixel by pixel** in five more scenes: the bar dropping, S9 right
  where it changes colour, above S9 with the +21, from S9+60 to zero — the
  one that erases most — and with the knob on volume, which moves the pill
  and therefore the window's edge. Not one pixel differs out of the frame's
  384,000.

---

## V5.28 · 2026-10-20

### Qué cambia

- **ARREGLADO: el reloj repintaba la cabecera entera.** La radio lo dijo con
  todas las letras —«max 164 **cab** 99»—: componer la cabecera, no
  empujarla al bus. `time_display_draw()` llamaba a `ui_top_draw()`, que es
  la cabecera (800×64) más la marquesina más la franja de estado (800×40):
  **91.003 accesos al bus para cambiar cinco caracteres**, una vez por
  minuto.
- Ahora repinta **sólo su esquina**, con el mismo dibujo de siempre y la
  ventana recortada: **5.512 accesos**, dieciséis veces menos. La pastilla
  de la batería vive en esa misma esquina y va en la misma ventana.
- Es la cuarta vez que aparece este mismo fallo —el S-metro el 30/09, el PPM
  del SAM el 04/10 y la frecuencia el 05/10— y se arregla igual, sin una
  segunda función de dibujo que mantener al día.
- **Comprobado pixel a pixel**: el banco nuevo pinta la cabecera entera y la
  esquina sola y compara los 384.000 píxeles del marco en seis escenas —el
  minuto, medianoche, el reloj más ancho, la batería, las dos a la vez y con
  el PPM del SAM pegado al lado—. Ni uno distinto.

### What's new

- **FIXED: the clock was repainting the whole header.** The radio said it in
  plain terms — «max 164 **cab** 99»: composing the header, not pushing it to
  the bus. `time_display_draw()` called `ui_top_draw()`, which is the header
  (800×64) plus the marquee plus the status strip (800×40): **91,003 bus
  accesses to change five characters**, once a minute.
- It now repaints **only its corner**, with the same drawing code and a
  clipped window: **5,512 accesses**, sixteen times less. The battery pill
  lives in that same corner and rides along in the same window.
- This is the fourth time this same bug has shown up — the S-meter on 30/09,
  the SAM PPM on 04/10 and the frequency on 05/10 — and it is fixed the same
  way, with no second drawing function to keep in sync.
- **Checked pixel by pixel**: the new bench draws the whole header and the
  corner alone and compares all 384,000 pixels across six scenes — the
  minute, midnight, the widest clock, the battery, both at once, and with the
  SAM PPM right next to it. Not one differs.

---

## V5.27 · 2026-10-20

### Qué cambia

- **El reloj se lleva 109 ms de una vuelta de 168.** Con la resta ya
  arreglada y los tramos sumando la vuelta entera, la radio señaló
  `time_display_draw()`: el repintado de la cabecera que se hace una vez
  por minuto. Esta versión lo parte en las tres cosas que hace, que se
  arreglan de maneras completamente distintas: `syn` rellenar la ficha de
  la cabecera, `cab` componer sus 91.003 píxeles, y `bus` empujarlos al
  panel —este último sale del mismo contador que la fila «Fotograma» usa
  para separar las dos mitades del espectro—.
- Medido en el simulador contra ese mismo `ui_top.c`: 91.003 accesos al
  bus, que a los 45 ns que dice la propia radio son **4,1 ms**. Así que
  `bus` tiene que salir pequeño; si sale grande, lo que miente es el número
  de nanosegundos, y entonces lo que hay que hacer es pasar la cabecera al
  DMA que el espectro ya usa.

### What's new

- **The clock takes 109 ms out of a 168 ms pass.** With the subtraction
  fixed and the stretches adding up to the whole pass, the radio pointed at
  `time_display_draw()`: the header repaint done once a minute. This release
  splits it into the three things it does, which are fixed in completely
  different ways: `syn` filling in the header's data, `cab` composing its
  91,003 pixels, and `bus` pushing them to the panel — that last one comes
  from the same counter the «Frame» row uses to separate the spectrum's two
  halves.
- Measured in the simulator against that same `ui_top.c`: 91,003 bus
  accesses, which at the 45 ns the radio itself reports are **4.1 ms**. So
  `bus` has to come out small; if it comes out large, what is lying is the
  nanosecond figure, and then the fix is to move the header onto the DMA the
  spectrum already uses.

---

## V5.26 · 2026-10-20

### Qué cambia

- **La resta del tramo `mod` se dejaba media franja de arriba.** Al partir
  la franja en `smt`, `hra` y `emi`, el tramo `fra` pasó a ser sólo lo que
  sobra de ella — pero la resta que calcula `mod` seguía descontando
  únicamente `fra`, así que `mod` se quedaba con los otros tres. Un tramo
  que contiene a otros gana el reparto siempre, diga lo que diga la
  realidad: en antena salió «max 170 mod 141» con la radio en LSB, donde lo
  que `mod` mide de verdad no hace nada.
- **Y ahora los tramos suman la vuelta entera.** Entre tramo y tramo
  quedaban huecos que no contaba nadie; el tramo nuevo `res` es la resta —
  la vuelta menos la suma de los doce—, así que si el tiempo se va por uno
  de esos huecos la fila lo dice en vez de culpar al mayor de los pequeños.
- **Y la media de la vuelta, que llevaba días diciendo cero.** Marcaba la
  pasada como «ha dibujado» sólo en los modos digitales, así que escuchando
  en LSB no se marcaba nunca y el contador se quedaba a cero con la radio
  pintando cuarenta y cinco veces por segundo. Ahora se marca por lo que de
  verdad significa: que el tramo del dibujo haya costado un milisegundo o
  más.
- **Medido de paso, y descartado:** la cabecera entera (`ui_top_draw()`,
  que es lo que repinta el reloj una vez por minuto) son **91.003 accesos
  al bus, 4,1 ms**; la franja del nombre, 0,34; la del medidor de S, 0,76.
  Ninguna de las tres explica 141 ms.

### What's new

- **The `mod` stretch's subtraction was missing half the top strip.** When
  the strip was split into `smt`, `hra` and `emi`, `fra` became just what is
  left over from it — but the subtraction that computes `mod` still
  discounted `fra` alone, so `mod` kept the other three. A stretch that
  contains others always wins the share-out, whatever the truth: on air it
  read «max 170 mod 141» with the radio in LSB, where what `mod` actually
  measures does nothing.
- **And the stretches now add up to the whole pass.** There were gaps
  between them that nobody counted; the new `res` stretch is the
  subtraction — the pass minus the sum of the twelve — so if the time goes
  into one of those gaps the row says so instead of blaming the largest of
  the small ones.
- **And the pass average, which had been reading zero for days.** It
  flagged a pass as «drew» only in the digital modes, so while listening in
  LSB it never did and the counter stayed at zero with the radio painting
  forty-five times a second. It is now flagged by what it actually means:
  that the drawing stretch cost a millisecond or more.
- **Measured along the way, and ruled out:** the whole header
  (`ui_top_draw()`, which is what the clock repaints once a minute) is
  **91,003 bus accesses, 4.1 ms**; the name strip, 0.34; the S-meter strip,
  0.76. None of the three explains 141 ms.

---

## V5.25 · 2026-10-20

### Qué cambia

- **El nombre del tramo sin su número señalaba al inocente.** La fila decía
  «max 167 fra», pero el ganador es el tramo **más grande** de esa vuelta,
  no el que explica el pico: uno de cuatro milisegundos gana igual si los
  demás son de uno. Ahora sale el número al lado — «max 167 fra 160» dice
  que fra es el culpable; «max 167 fra 4» dice que el tiempo se fue por un
  sitio que no mide nadie, que es un diagnóstico distinto y igual de útil.
- **Y la franja de arriba se parte en tres**, que es donde apuntaba la
  última lectura: `smt` el medidor de S y el dBm, `hra` el reloj —que
  repinta la cabecera entera una vez por minuto— y `emi` el cartel de quién
  emite.
- **El peor dibujo se va de esa fila.** Medido en el simulador con la fuente
  de verdad: con el nombre y el número la fila ocupaba 188 px de los 192 del
  hueco, y a la primera cifra de más se recorta sin avisar. Quitándolo
  quedan 165 y aguanta cuatro cifras en cada número. No se pierde nada: si
  el dibujo es quien hace el pico, el tramo se llama `dib`.

### What's new

- **The stretch's name without its number was blaming the innocent.** The
  row read «max 167 fra», but the winner is the **largest** stretch of that
  pass, not the one that explains the peak: a four-millisecond one wins just
  the same if the others are one millisecond each. The number now sits next
  to it — «max 167 fra 160» says fra is the culprit; «max 167 fra 4» says
  the time went somewhere nobody measures, which is a different diagnosis
  and just as useful.
- **And the top strip is split in three**, which is where the last reading
  pointed: `smt` the S-meter and dBm readout, `hra` the clock —which
  repaints the whole header once a minute— and `emi` the station label.
- **The worst drawing leaves that row.** Measured in the simulator with the
  real font: with the name and the number the row took 188 px of the 192
  available, and one more digit would clip silently. Without it the row is
  165 px and survives four digits in each number. Nothing is lost: if the
  drawing is what made the peak, the stretch is called `dib`.

---

## V5.24 · 2026-10-20

### Qué cambia

- **El tramo «pre» era demasiado gordo.** La V5.22 puso nombre al pico de
  la vuelta y en antena contestó `pre` —todo lo de antes del dibujo— con
  167 ms. Tres sospechosos y ninguna forma de elegir, así que ahora son
  tres tramos: `fra` la franja de arriba, `mod` la traza y los repintados
  de cambio de modo, y `tc3` la tercera mirada al táctil, que estaba ahí
  dentro sin que el nombre lo dijera y es la única de las tres que despacha
  toques, o sea que puede hacer cualquier cosa.
- **Dos sospechosos descartados antes de tocar nada.** La base de emisoras
  no es: el banco nuevo la mide en **104–420 bytes** por cartel, tres
  décimas de milisegundo. Y pintar tampoco: la pantalla entera son 384.000
  accesos a 45 ns, **17 ms**.
- **Y queda de guardián**: si algún día el cartel de la cabecera vuelve a
  recorrer la tabla entera de emisoras —206 kB por ese bus son 170 ms—, la
  compilación se para.

### What's new

- **The «pre» stretch was too coarse.** V5.22 put a name on the loop peak
  and on air it answered `pre` — everything before the drawing — at 167 ms.
  Three suspects and no way to choose between them, so it is now three:
  `fra` the top strip, `mod` the trace and the mode-change repaints, and
  `tc3` the third look at the touchscreen, which was in there without the
  name saying so and is the only one of the three that dispatches touches,
  so it can do anything.
- **Two suspects ruled out before touching anything.** The station database
  is not it: the new bench measures **104–420 bytes** per header label,
  three tenths of a millisecond. And neither is painting: the whole screen
  is 384,000 bus accesses at 45 ns, **17 ms**.
- **And it stays as a guard**: if the header label ever walks the whole
  station table again — 206 kB over that bus is 170 ms — the build stops.

---

## V5.23 · 2026-10-20

### Qué cambia

- **Una radio con un chip de flash de 1 MB se quedaba sin disco.** El
  firmware reserva el último megabyte para las bases de datos, y en un chip
  de 1 MB eso es el chip entero: el disco USB salía de **cero bloques**. De
  ahí, en cadena: BD.BIN no se podía copiar porque no había dónde dejarlo,
  el sitio para fotos no se podía leer, el volumen de fábrica decía medir
  más que el disco y la pantalla mandaba formatear… y formatear contestaba
  **«no se ve la flash»** con el chip delante y su número de serie en
  pantalla. Un callejón sin salida.
- **La regla nueva es de una línea:** la zona alta se queda con el último
  mega **o con la mitad del chip, lo que sea menos**. Con 2 MB o más no se
  mueve ni un byte —la mitad de 2 MB es justo un mega—, así que ninguna
  radio de las que funcionan hoy se entera. Con 1 MB salen 512 kB de disco
  y 512 kB de zona alta, que es sitio para el mapa y para la lista de
  emisoras.
- **El techo de BD.BIN miraba el final del chip** en vez del hueco del
  mapa. Un .BIN de entre 847.872 y 1.044.480 bytes se habría escrito encima
  del mapa; no ha pasado porque el empaquetador del PC ya usa el número
  bueno, pero la red estaba mal puesta.
- **«No cabe» ya no sale como «destino no permitido»**, que mandaba a mirar
  el disco. Ahora dice **«no cabe, hay sitio para N kB»**.

### What's new

- **A radio with a 1 MB flash chip ended up with no disk at all.** The
  firmware reserves the last megabyte for the databases, and on a 1 MB chip
  that is the whole chip: the USB disk came out at **zero blocks**. Then, in
  a chain: BD.BIN could not be copied because there was nowhere to put it,
  the photo area could not be read, the factory volume claimed to be larger
  than the disk and the screen said to format it… and formatting answered
  **«no flash found»** with the chip right there and its serial number on
  screen. A dead end.
- **The new rule is one line:** the upper area takes the last megabyte **or
  half the chip, whichever is smaller**. At 2 MB and above not one byte
  moves — half of 2 MB is exactly one megabyte — so no radio in use today
  notices. At 1 MB it comes out as 512 kB of disk and 512 kB of upper area,
  which is room for the map and the station list.
- **BD.BIN's ceiling looked at the end of the chip** instead of the map
  hole. A .BIN between 847,872 and 1,044,480 bytes would have been written
  over the map; it never happened because the PC packer already uses the
  right number, but the safety net was in the wrong place.
- **«Does not fit» no longer comes out as «target not allowed»**, which sent
  you to look at the disk. It now says **«does not fit, room for N kB»**.

---

## V5.22 · 2026-10-20

### Qué cambia

- **Cinco instrucciones por píxel de espectro, en vez de siete.** El bucle
  que rellena la barra y pinta la traza es el más repetido de toda la radio
  —hasta 157.248 vueltas por fotograma—, y llevaba un índice de 16 bits:
  eso obliga al compilador a recortar a 16 bits en cada vuelta y a comparar
  contra el final. Contando hacia atrás las dos instrucciones sobran.
  Comprobado pixel a pixel contra la imagen patrón: no cambia ni uno.
- **La fila «Vuelta del bucle» ya dice de quién es el pico.** Decía un
  número sin dueño: en antena salieron 343 ms con el dibujo en 55 y los dos
  poll en 1 y 0, o sea 290 ms de nadie. La vuelta está partida en siete
  tramos con nombre y, junto al máximo, salen tres letras diciendo cuál lo
  hizo: `cat`, `pre`, `dib`, `tac`, `man`, `dec` o `pol`.
- **Y el pico ya no se lo lleva salir del menú.** La vuelta que cierra el
  menú repinta la pantalla entera y se contaba como si fuera una vuelta
  normal. Ahora sólo cuentan las que empiezan y acaban en la radio.
- **El máximo del segundo bloque de poll se borra con los demás.** Se
  añadió con su máximo y nadie lo metió en el botón de poner a cero, así que
  un pico de hacía media hora salía al lado de tres números recién puestos a
  cero.

### What's new

- **Five instructions per spectrum pixel instead of seven.** The loop that
  fills the bar and draws the trace is the most repeated one in the whole
  radio — up to 157,248 iterations per frame — and it used a 16-bit index,
  which forces the compiler to truncate to 16 bits every time round and to
  compare against the end. Counting down makes both instructions unnecessary.
  Checked pixel by pixel against the reference image: not one changes.
- **The «Loop pass» row now says whose the peak was.** It used to give an
  ownerless number: on air it read 343 ms with drawing at 55 and both poll
  blocks at 1 and 0 — 290 ms belonging to nobody. The pass is now split into
  seven named stretches and, next to the maximum, three letters say which one
  did it: `cat`, `pre`, `dib`, `tac`, `man`, `dec` or `pol`.
- **And leaving the menu no longer owns the peak.** The pass that closes the
  menu repaints the whole screen and was counted as an ordinary pass. Only
  passes that begin and end on the radio screen count now.
- **The second poll block's maximum is cleared with the rest.** It was added
  with its own maximum and nobody put it in the reset button, so a peak from
  half an hour earlier sat next to three freshly zeroed numbers.

---

## V5.21 · 2026-10-20

### Qué cambia

- **La franja de los carteles de emisora ya no es intocable.** El espectro la
  trataba como una zona que no se salta nunca y que se monta de ancho
  completo en cada fotograma —el 16 % del dibujo, según su propio
  comentario—. Medido en antena, en onda larga con la lista llena: **nueve
  milisegundos de los 38 que tardaba la traza**.
- Ya no hace falta, porque quien pone los carteles invalida el cuadro entero
  cada vez que la lista o la posición cambian, que es lo único que puede
  moverlos. Si no ha cambiado nada, esas bandas se saltan como las demás.
  Además cubre un caso que al forzado le faltaba: la lista igual con las
  posiciones movidas, que es lo que pasa girando el dial.
- **Y la media de «Vuelta del bucle» por fin sale.** Marcaba la pasada como
  «ha dibujado» en un sitio por el que se pasa siempre, dibuje o no, así que
  seguía promediando todas y seguía saliendo cero.

### What's new

- **The station-label strip is no longer untouchable.** The spectrum treated
  it as a zone that is never skipped and is composed full width on every
  frame — 16 % of the drawing, by its own comment. Measured on air, on
  longwave with a full list: **nine milliseconds of the 38 the trace was
  taking**.
- It is no longer needed, because whatever places the labels invalidates the
  whole frame whenever the list or the positions change, which is the only
  thing that can move them. If nothing changed, those bands are skipped like
  any other. It also covers a case the forcing missed: same list, moved
  positions, which is what happens while turning the dial.
- **And the «Loop pass» average finally shows up.** It flagged a pass as
  «drew» at a point the code goes through every time, drawing or not, so it
  was still averaging all of them and still reading zero.

---

## V5.20 · 2026-10-20

### Qué cambia

- **Los nombres de emisora del espectro se leían de la flash en CADA
  fotograma.** Hasta trece carteles, dos lecturas de la flash SPI cada uno,
  cuarenta y dos veces por segundo, dentro del camino de dibujo. Ahora se
  leen una sola vez, cuando la lista cambia —al cambiar de minuto o de
  frecuencia—, que es cuando pueden cambiar.
- Lo que lo destapó: midiendo en onda larga de noche, con la lista de
  emisoras llena, la traza tardaba **41 ms de un tope de 22** y el bus de la
  pantalla marcaba **cero**. O sea que lo caro no era pintar: era ir a buscar
  los nombres.

### What's new

- **The spectrum's station labels were read from flash on EVERY frame.** Up
  to thirteen labels, two SPI flash reads each, forty-two times a second,
  inside the drawing path. They are now read once, when the list changes —
  on a minute or frequency change — which is when they can change.
- What exposed it: measured on longwave at night with a full station list,
  the trace was taking **41 ms against a 22 ms budget** while the display bus
  read **zero**. So the expensive part was not painting: it was fetching the
  names.

---

## V5.19 · 2026-10-19

### Qué cambia

- **La media de «Vuelta del bucle» salía 0 y ahora sale el número de
  verdad.** Promediaba TODAS las pasadas del bucle, y la mayoría no dibuja
  —el limitador de fotograma las devuelve enseguida—, así que la media caía
  por debajo del milisegundo mientras las que sí dibujan tardaban 24 ms.
  Ahora sólo entran en la media las pasadas que han dibujado, que son las
  que importan.

### What's new

- **The «Loop pass» average read 0 and now reads the real number.** It
  averaged EVERY pass of the loop, and most passes do not draw — the frame
  limiter returns from them right away — so the average fell below a
  millisecond while the passes that do draw were taking 24 ms. Only passes
  that drew now count towards the average, which are the ones that matter.

---

## V5.18 · 2026-10-19

### Qué cambia

- **«Vuelta del bucle» enseñaba la última pasada, que es una lotería.** El
  valor se congela al abrir el menú, así que es la pasada que pille: del
  mismo sitio salieron 53, 47 y 16 ms, y las dos primeras mandaron a buscar
  un problema de 37 ms que no existe. Ahora enseña la **media de las últimas
  64** y, entre paréntesis, la última suelta.
- **Y el peor caso se puede poner a cero tocando la celda.** La función para
  hacerlo existía desde hace tiempo y **no la llamaba nadie**, así que el
  máximo venía acumulando desde el arranque e incluía la carga de mapas y
  listas desde la flash: 173 ms de pico con una vuelta típica de 16. Un pico
  que no se puede borrar no dice si acaba de pasar o si fue al encender.
- También vuelve el peor caso del dibujo al lado del de la vuelta, que es lo
  que parte en dos el caso «la vuelta es grande y los poll no».

### What's new

- **«Loop pass» showed the last pass, which is a lottery.** The value freezes
  when the menu opens, so it is whichever pass it happens to catch: the same
  spot gave 53, 47 and 16 ms, and the first two sent us chasing a 37 ms
  problem that does not exist. It now shows the **average of the last 64**
  with the latest one in brackets.
- **And the peak can be zeroed by tapping the cell.** The function to do that
  had existed for a while and **nobody called it**, so the peak accumulated
  from boot and included loading maps and lists from flash: a 173 ms spike
  against a typical pass of 16. A peak you cannot clear does not tell you
  whether it just happened or happened at power-on.
- The drawing peak is back alongside the loop one, which is what splits the
  «the loop is big and the polls are not» case in two.

---

## V5.17 · 2026-10-19

### Qué cambia

- **Dos celdas nuevas en Información, que había media página vacía.**
  «Vuelta del bucle» y «Poll de pantalla». La fila «Fotograma» se queda sólo
  con el reparto del dibujo, que es lo que cabe en ella.
- **Y lo importante: había un bloque entero del bucle que no medía nadie.**
  El cronómetro de los `poll` se cerraba justo después de los
  decodificadores, y todo lo que viene detrás —la captura, el volcado de
  4 kB por vuelta, el fax, el SSTV, el Hell, el RDS, la marquesina y el
  autoguardado— quedaba fuera de todos los relojes. Escuchando en 80 m la
  vuelta tardaba 47 ms con 9 de dibujo, 1 de táctil y 0 de `poll`: **37 ms
  sin dueño**, y la fila que debía señalarlos miraba el bloque equivocado.
  Ahora se miden los dos bloques por separado.

### What's new

- **Two new cells on the Information page, which had half a page spare.**
  «Loop pass» and «Screen polls». The «Frame» row keeps only the drawing
  breakdown, which is what fits in it.
- **And the point: a whole block of the loop was measured by nobody.** The
  poll timer closed right after the decoders, and everything after it — the
  screen capture, the 4 kB-per-pass dump, fax, SSTV, Hell, RDS, the marquee
  and the settings autosave — fell outside every clock. Listening on 80 m
  the loop took 47 ms with 9 of drawing, 1 of touch and 0 of polls: **37 ms
  with no owner**, and the row meant to point at them was watching the wrong
  block. Both blocks are now timed separately.

---

## V5.16 · 2026-10-19

### Qué cambia

- **La fila «Fotograma» de Información enseñaba menos de lo que medía.**
  Escribía ocho números en un renglón que sólo da para cinco, y la celda los
  recortaba **sin avisar**: lo que se perdía era justo la parte que dice
  dónde se van los milisegundos cuando la vuelta del bucle tarda más que el
  dibujo. Ahora el reparto del dibujo va en un renglón y la vuelta entera
  —con el táctil, los decodificadores y los máximos— en el de debajo.
- **Y se cuenta lo que no cabe.** Que un texto no quepa en una celda no daba
  error en ninguna parte: se dibujaba lo que entraba y el resto desaparecía.
  Desde hoy hay un contador, al lado de «Choques», que **tiene que ser
  cero**: si alguna celda de cualquier pantalla pierde un dato, se ve.

### What's new

- **The Information page's «Frame» row showed less than it measured.** It
  wrote eight numbers into a line that fits five, and the cell clipped them
  **with no warning** — and what got lost was exactly the part that says
  where the milliseconds go when the loop takes longer than the drawing. The
  drawing breakdown now has its own line and the whole loop — touch,
  decoders and peaks — goes on the one below.
- **And what does not fit is now counted.** Text overflowing a cell raised no
  error anywhere: whatever fitted was drawn and the rest vanished. There is
  now a counter next to «Collisions» that **must stay zero**: if any cell on
  any screen loses a value, it shows.

---

## V5.15 · 2026-10-19

### Qué cambia

- **La tira con la que se compone la pantalla pasa de 16 filas a 8**, y con
  eso la radio recupera **13.704 bytes de memoria**. La SRAM libre pasa de
  1.436 bytes a 15.140: de un 0,7 % a un 7,7 %.
- **Esto ya se probó el 25/09 y se deshizo** porque el par de milisegundos
  de más se notaba en la pantalla. Vuelve porque el motivo es otro: entonces
  era para que cupiera FT8, ahora es que con 1.436 bytes libres no cabe
  ninguna de las mejoras que quedan pendientes. Y porque el fotograma de hoy
  no es el de entonces: por el camino se han quitado la regla repintándose
  sin motivo, la cabecera pintada dos veces y el color del degradado
  calculado por píxel.
- Medido: el coste en accesos al panel es **+0,17 %**, bastante menos que el
  +0,3 % que se midió la vez anterior. Lo que no se puede medir sin la radio
  es si el doble de tiras se ve al repintar. **Si se ve, se deshace en una
  línea y no hay que devolver nada a cambio.**

### What's new

- **The strip the screen is composed in goes from 16 rows to 8**, handing
  back **13,704 bytes of memory**. Free SRAM goes from 1,436 bytes to
  15,140: from 0.7 % to 7.7 %.
- **This was tried on 25/09 and reverted** because the extra couple of
  milliseconds showed on screen. It is back for a different reason: then it
  was to make FT8 fit, now it is that 1,436 free bytes are not enough for any
  of the remaining improvements. And today's frame is not that frame: since
  then the ruler repainting for nothing, the header painted twice and the
  gradient colour computed per pixel have all gone.
- Measured: the cost in panel accesses is **+0.17 %**, well under the +0.3 %
  measured last time. What cannot be measured without the radio is whether
  twice as many strips shows up when repainting. **If it does, this reverts
  in one line and nothing has to be given back.**

---

## V5.14 · 2026-10-19

### Qué cambia

Revisión a fondo de todo el proyecto buscando cosas hechas de forma más cara
de lo necesario. Nada de esto cambia lo que la radio hace: cambia lo que le
cuesta hacerlo.

- **FT8 deja de gastar más en mover memoria que en filtrar.** El decimador
  corría 119 números de sitio por **cada muestra** que entraba, 12.000 veces
  por segundo. Eso costaba dos veces y media más que el filtro al que
  alimentaba. Ahora es un anillo: la muestra nueva se escribe y lo que gira
  es el índice. **Un 2,9 % de la CPU devuelto** mientras FT8 está activo, sin
  gastar ni un byte más de memoria.
- **La reducción de ruido y el panadaptador, más baratos.** Las dos
  transformadas tenían cada una un defecto que la otra no: una leía la tabla
  de giros una vez por mariposa en vez de una por columna, y la otra releía
  de memoria valores que ya tenía. Corregido en las dos. El resultado es
  **idéntico bit a bit**.
- **El recorte de audio a 16 bits se hace con la instrucción del núcleo**, no
  con dos comparaciones y dos saltos. Son 96.000 muestras por segundo.
- **La regla de frecuencia deja de repintarse sin motivo.** Comparaba
  hercios y la regla imprime kilohercios: cualquier clic de 1, 10 o 100 Hz
  repintaba 20.834 píxeles para dejar la pantalla exactamente igual, con la
  CPU parada esperando al bus. Ahora compara el texto que se lee.
- **La cabecera deja de pintarse dos veces** al cambiar de banda o al cruzar
  los 9.999 kHz, y el degradado calcula su color una vez por fila en vez de
  una por píxel.
- **1.600 bytes de pila devueltos**: los ajustes reservaban dos copias de un
  buffer de 1.600 bytes en la pila teniendo ya uno estático al lado.
- **La tanda de comprobaciones tarda 14 segundos menos** por una sola línea
  de uno de los guiones, y dos bancos que no podían ponerse rojos ahora sí
  pueden.

### What's new

A deep review of the whole project looking for work done more expensively
than it needs to be. None of this changes what the radio does; it changes
what it costs.

- **FT8 no longer spends more on moving memory than on filtering.** The
  decimator shifted 119 numbers along for **every** incoming sample, 12,000
  times a second — two and a half times the cost of the filter it fed. It is
  now a ring buffer. **2.9 % of the CPU handed back** while FT8 is running,
  at no extra memory cost.
- **Noise reduction and the panadapter got cheaper.** Each of the two
  transforms had a flaw the other didn't: one read the twiddle table once per
  butterfly instead of once per column, the other re-read values from memory
  it already held. Both fixed. The result is **bit-for-bit identical**.
- **16-bit audio clipping now uses the core's own instruction** instead of
  two compares and two branches. That is 96,000 samples a second.
- **The frequency ruler stops repainting for nothing.** It compared hertz
  while the ruler prints kilohertz: any 1, 10 or 100 Hz click repainted
  20,834 pixels to leave the screen exactly as it was, with the CPU stalled
  on the bus. It now compares the text you read.
- **The header stops being painted twice** when the band changes or the
  reading crosses 9,999 kHz, and the gradient computes its colour once per
  row instead of once per pixel.
- **1,600 bytes of stack handed back**: the settings code reserved two
  1,600-byte stack copies while a static one sat right beside it.
- **The check suite runs 14 seconds faster** thanks to one line in one of the
  scripts, and two benches that could never go red now can.

---

## V5.13 · 2026-10-19

### Qué cambia

- **Mejor recepción entre 3,6 y 4,8 MHz.** El oscilador local reparte esa
  banda en zonas, y cada zona lleva un multiplicador que decide dónde
  trabaja el VCO del sintetizador. La zona de 3,6 a 4,8 MHz era **la única
  de las siete que no llegaba al techo del VCO**: vivía pegada al suelo,
  con 2,9 MHz de margen, mientras las otras seis barren el rango entero. El
  número se había elegido por ser el mínimo que cabía, y la sensibilidad
  nunca fue un criterio al elegirlo.
- Ahora el multiplicador se ha medido en vez de suponerse: de los 22
  valores posibles se toma el que deja el VCO a más de 20 MHz de sus dos
  topes y, entre ésos, el de mejor ángulo de cuadratura. El margen pasa de
  2,9 MHz a 38,8 por abajo y 53,6 por arriba.
- **El oscilador tiene banco por primera vez.** Genera la señal de toda la
  radio, de 300 kHz a 180 MHz, y no lo tenía: ahora se comprueba la
  frecuencia sintetizada en toda la banda, que el VCO no se salga, que los
  dos canales lleven el mismo divisor y el ángulo de los 90 grados.

### What's new

- **Better reception between 3.6 and 4.8 MHz.** The local oscillator splits
  that band into zones, and each zone carries a multiplier that decides
  where the synthesiser's VCO runs. The 3.6-to-4.8 MHz zone was **the only
  one of the seven that never reached the top of the VCO**: it sat against
  the floor with 2.9 MHz of headroom while the other six sweep the whole
  range. That number had been picked as the smallest that fit, and
  sensitivity was never a criterion when it was chosen.
- The multiplier is now measured rather than assumed: of the 22 possible
  values, the one that keeps the VCO more than 20 MHz away from both of its
  limits and, among those, has the best quadrature angle. Headroom goes
  from 2.9 MHz to 38.8 below and 53.6 above.
- **The oscillator has a bench for the first time.** It generates the
  signal for the whole radio, 300 kHz to 180 MHz, and had none: the
  synthesised frequency across the band, the VCO staying in range, both
  channels carrying the same divider and the 90-degree angle are all
  checked now.

---

## V5.12 · 2026-10-19

### Qué cambia

- **El corrector de I/Q cuesta tres veces y media menos dentro de la
  interrupción del ADC.** El filtro que entró en V5.11 hacía dos pasadas
  sobre cada bloque —una el corrector plano y otra el filtro— y además movía
  su historial en memoria para cada muestra. Ahora es **una sola pasada**,
  con el historial en registros y el recorte a 16 bits hecho con la
  instrucción del núcleo: de unas 145 instrucciones por muestra a **43**. El
  rechazo de imagen no cambia: los mismos 38,3 dB.
- **Por qué importa:** por esa interrupción pasan todas las muestras de
  todos los modos, 96.000 parejas por segundo. Ya hubo una vez en que meter
  trabajo ahí hizo que la radio empezara a perder pulsaciones de botón.
- **Dos sitios donde se leía fuera de su sitio.** La respuesta de "¿llega el
  conector grande a las patillas de USB?" copiaba hasta 30 caracteres de un
  texto de 24, y la comprobación del tope iba después de leer. Y dos relojes
  —el ruso y el americano— arrastraban una variable muerta.

### What's new

- **The I/Q corrector is 3.4x cheaper inside the ADC interrupt.** The filter
  added in V5.11 made two passes over every block —one for the flat
  corrector, one for the filter— and shuffled its history through memory for
  every sample. It is now a **single pass**, with the history in registers
  and the 16-bit clamp done by the core's own instruction: from about 145
  instructions per sample down to **43**. Image rejection is unchanged: the
  same 38.3 dB.
- **Why it matters:** every sample of every mode goes through that
  interrupt, 96,000 pairs per second. Putting work there once already made
  the radio start dropping button presses.
- **Two out-of-bounds reads.** The "does the big connector reach the USB
  pins?" answer copied up to 30 characters out of a 24-character string, with
  the bounds check placed after the read. And two of the time-signal decoders
  carried a dead variable.

---

## V5.11 · 2026-10-19

### Qué cambia

- **El rechazo de imagen sube donde antes no podía subir.** El corrector de
  I/Q llevaba un solo coeficiente, y eso sólo sirve cuando el desequilibrio
  es el mismo en toda la banda. Cuando varía con la frecuencia —y varía,
  porque lo que descuadra las dos ramas es un retardo— ese coeficiente sólo
  puede cancelar su media, y lo que queda son los 22-24 dB que estaba midiendo
  un usuario. Ahora se le añade un **filtro de cuatro tomas** que corrige
  cada frecuencia por separado.
- **Y se calibra solo, con lo que entra por la antena.** Cuando en el
  panorama hay una emisora fuerte, su imagen está en el punto simétrico y el
  cociente entre las dos es el desequilibrio a esa frecuencia. La radio lo
  mide en la FFT que ya hace para el panadaptador, junta puntos en dieciséis
  casillas repartidas por la banda, y cada dos segundos ajusta el filtro.
  Moviendo el dial, la calibración se completa sola.
- **Medido de punta a punta:** con el desequilibrio que reproduce el caso
  real, el rechazo pasa de **22,6 a 38,3 dB**. En el caso extremo, de 17,9 a
  37,3. Donde el corrector de siempre ya era bueno (78 dB) se pierden 3,6,
  que es el ruido del propio estimador y queda muy por encima de los 45-50 dB
  a partir de los cuales la imagen no se ve.
- **Mientras no haya calibración, la radio se comporta exactamente como
  antes.** El filtro no entra hasta tener medidas suficientes, y sus
  coeficientes están acotados.

### What's new

- **Image rejection now improves where it previously could not.** The I/Q
  corrector used a single coefficient, which only works when the imbalance is
  the same across the band. When it varies with frequency — and it does,
  because what unbalances the two branches is a delay — that coefficient can
  only cancel its average, and what is left is the 22-24 dB a user was
  measuring. A **four-tap filter** now corrects each frequency separately.
- **And it calibrates itself from whatever is on the air.** When a strong
  station is on the panadapter, its image sits at the mirror point and the
  ratio between the two is the imbalance at that frequency. The radio
  measures it in the FFT it already computes for the display, collects points
  into sixteen slots across the band, and fits the filter every two seconds.
  Tuning around completes the calibration by itself.
- **Measured end to end:** with the imbalance that reproduces the real case,
  rejection goes from **22.6 to 38.3 dB**. In the extreme case, from 17.9 to
  37.3. Where the old corrector was already good (78 dB) it loses 3.6, which
  is the estimator's own noise and still far above the 45-50 dB beyond which
  the image is invisible.
- **Until it has calibrated, the radio behaves exactly as before.** The
  filter does not engage without enough measurements, and its coefficients
  are bounded.

---

## V5.10 · 2026-10-19

### Qué cambia

- **El detector síncrono (SAM) ya no suelta las emisoras que se mueven.** El
  margen de enganche por omisión pasa de **100 a 250 Hz**. Una portadora de
  onda corta real no está quieta: en una grabación sobre China Radio en
  15.560 kHz la traza serpentea del orden de ±150 Hz, y con 100 Hz el lazo se
  quedaba contra el tope y la pantalla ponía «buscando» cada pocos segundos.
  Medido: con ±150 Hz de vaivén, 100 Hz engancha el 81 % del tiempo y 250 Hz
  el 100 %.
- **Y tiene un precio, que conviene saber:** con 250 Hz una portadora intrusa
  a menos de esa distancia puede llevarse el lazo; con 100 Hz no podía. A
  300 Hz y más sigue protegido, que es donde estaban los casos que motivaron
  el margen estrecho. Quien prefiera el comportamiento de antes tiene **100**
  en la lista de Ajustes → Radio → Margen SAM.
- **El indicador de enganche ya no parpadea:** entra por encima de 0,60 y no
  sale hasta bajar de 0,45, en vez de un solo umbral que basta rozar.

### What's new

- **Synchronous AM no longer drops stations that move.** The default lock
  margin goes from **100 to 250 Hz**. A real shortwave carrier does not sit
  still: on a recording of China Radio on 15,560 kHz the trace wanders by
  something like ±150 Hz, and at 100 Hz the loop sat against its limit and
  the screen read "searching" every few seconds. Measured: with ±150 Hz of
  wander, 100 Hz holds lock 81 % of the time and 250 Hz holds it 100 %.
- **It has a price, worth knowing:** at 250 Hz an interfering carrier closer
  than that can capture the loop, which it could not at 100 Hz. At 300 Hz and
  beyond it is still protected, which is where the cases that motivated the
  narrow margin were. Anyone who prefers the old behaviour still has **100**
  in Settings → Radio → SAM range.
- **The lock indicator no longer flickers:** it engages above 0.60 and does
  not disengage until it falls below 0.45, instead of a single threshold that
  only had to be grazed.

---

## V5.09 · 2026-10-19

### Qué cambia

- **La barra del S-metro recupera su tamaño.** En la V5.08 se había encogido
  de 120 a 80 píxeles para hacerle sitio al valor del atenuador. Era el pago
  equivocado: el sitio ya estaba hecho.
- **La pastilla del mando se mete en el hueco que dejó «AGC activo»,** tiene
  **ancho fijo** y ya no recorta su rótulo. «Sintonía 1 kHz» —y «Sintonía
  12,5 kHz», que es el paso más ancho que existe— salen enteros siempre, con
  el mando enganchado a lo que sea.
- **La lectura en dBm sale de la franja de estado.** Sólo valía con el AGC en
  OFF y obligaba a reservarle setenta y nueve píxeles los otros cinco
  perfiles. Sigue estando, con los dos rechazos de imagen, en Ajustes →
  Equipo → Información.
- **Con eso la franja deja de bailar:** ni la barra, ni la pastilla, ni las
  chapas cambian de sitio o de tamaño con la señal ni con el perfil del AGC.

### What's new

- **The S-meter bar gets its size back.** V5.08 had shrunk it from 120 to 80
  pixels to make room for the attenuator's value. That was the wrong thing to
  charge: the room was already there.
- **The knob pill moves into the space "AGC active" used to occupy,** has a
  **fixed width** and no longer truncates its label. "Tuning 1 kHz" — and
  "Tuning 12.5 kHz", the widest step there is — always show in full, whatever
  the knob is attached to.
- **The dBm reading leaves the status strip.** It only meant anything with
  the AGC in OFF, and it was reserving seventy-nine pixels for the other five
  profiles. It is still there, with both image-rejection figures, under
  Settings → Equipment → Information.
- **With that the strip stops moving:** neither the bar, nor the pill, nor the
  chips change place or size with the signal or with the AGC profile.

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
