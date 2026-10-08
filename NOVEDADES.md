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

## V4.03 · 2026-10-08

### Qué cambia

- **La radio ya dice POR QUÉ se reinició.** Un usuario reporta en la V4.01 reinicios
  aleatorios, que al volver no recupera el modo ni la configuración de pantalla, y
  uno de ellos recibiendo FT8. Antes de tocar nada: en esta radio **un fallo de CPU
  cuelga, no reinicia** —no hay perro guardián en ningún sitio, el `HardFault_Handler`
  es un bucle infinito, no hay WFI ni standby, y el único reinicio por software de
  todo el árbol lo pide el dueño a mano desde la fila de DFU—. Así que un reinicio
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

- **La FSK cifrada de 50 Bd / 200 Hz ya se identifica.** El dueño mandó una
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

Revisión a fondo de todo el código fuente, a petición del dueño: **44 fallos
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

- **Botón "Aprende" en el panel de CW**, al lado de Borrar. Idea del dueño.
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
