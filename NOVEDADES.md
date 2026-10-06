# Novedades · What's new

Lo que cambia en cada versión, en corto y en dos idiomas. El porqué, las
medidas y los fallos que hubo por el camino están en **`CAMBIOS.md`**, que va
por fecha y no se corta en los detalles.

What changes in each release, briefly and in two languages. The reasoning, the
measurements and the bugs found along the way are in **`CAMBIOS.md`**, which is
ordered by date and does not spare the detail.

> Una entrada por versión, la más nueva arriba. La versión de arriba tiene que
> ser la misma que `CONFIG_FW_VERSION` de `User/config.h`, y eso lo comprueba
> `tools/novedades_check.py` antes de entregar nada.
>
> One entry per release, newest first. The top version must match
> `CONFIG_FW_VERSION` in `User/config.h`; `tools/novedades_check.py` enforces
> that before anything ships.

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
