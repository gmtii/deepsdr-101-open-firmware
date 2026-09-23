# Qué se le ha hecho a este firmware

Esteban: esto es tu DEEPSDR 101 con bastante trabajo encima. El grueso son dos
cosas: **la interfaz está rehecha entera** y **hay modos nuevos**. Pero lo que
más te va a interesar probablemente no es ninguna de las dos, sino la tercera:
**hay un simulador que compila el código de verdad del firmware en el PC**, y
casi todo lo que hay aquí se decidió midiendo con él en vez de a ojo.

Este fichero es el mapa. Está escrito para que puedas ir directo a lo que te
interese sin leerte 98 ficheros.

---

## Lo primero: cómo compilarlo y cómo probarlo sin placa

```
make                 # firmware.bin / .hex, igual que siempre
```

Y en la carpeta de al lado, `deepsdr-ui/`:

```
make comprueba       # 14 bancos de pruebas, ~10 segundos, sin placa
make spec3b && ...   # cada banco saca un PNG de lo que va a salir en el panel
```

`make comprueba` es lo que hay que correr antes de grabar nada. Si sale limpio
no garantiza que funcione, pero sí que no se ha roto ninguna de las cosas que
se rompieron alguna vez.

---

## La interfaz: `gfx2`

El dibujo viejo (`gfx.c`) abría una ventana del panel por píxel. `gfx2` compone
una banda de 24 filas en RAM y la vuelca de una vez: **una ventana, un volcado**.
De ahí salen las tipografías proporcionales con antialias, las esquinas
redondeadas y los degradados, que con el camino anterior no eran viables.

Los módulos de interfaz viven en `User/` pero están escritos para no depender
de nada del firmware salvo `gfx2`: reciben una estructura con el estado y
pintan. Esa es la regla que permite que el simulador compile **los mismos
ficheros** con valores inventados y saque un PNG.

| Módulo | Qué dibuja |
|---|---|
| `ui_top.c` | Cabecera (frecuencia, modo, reloj, batería) y barra de estado |
| `ui_grid.c` | Rejilla paginada: modos, pasos, información |
| `ui_cfg.c` | Ajustes y bandas, con la columna de categorías a la izquierda |
| `ui_det.c` | Pantallas de detalle (volumen, escala, silenciador…) |
| `ui_act.c` | Barra de acciones de abajo |
| `ui_kbd.c` | Teclado numérico (frecuencia y hora) |
| `ui_digi.c` | Panel de modos digitales: texto decodificado, velocidad, borrar |
| `spec_chrome.c` | Regla de frecuencias, canaleta de dB y leyenda de color |
| `splash.c` | Pantalla de arranque |

De `ui.c`, el sistema de interfaz original, queda **una función**:
`ui_panel_draw()`. El resto —botones, etiquetas, registro de widgets, reparto
de toques— se fue cuando dejó de llamarlo nadie. Está comprobado con
`--print-gc-sections`, no a ojo.

---

## Modos y funciones nuevas

### CW (`cw.c`, ~1.500 líneas)

Decodificador de Morse. Goertzel solapado, banco de 9 sondas para engancharse
al tono aunque estés desafinado ±250 Hz, clasificador de dos medias con zona
muerta para separar puntos de rayas, y reproducción retrospectiva: cuando se
engancha, vuelve atrás sobre el historial de duraciones y saca lo que ya había
llegado.

Se desarrolló contra `sim/cwtest.c`, que le mete señal sintética con ruido a
distintas SNR y velocidades. **Ese banco encontró ocho fallos reales** que a
oído habrían pasado por "es que la señal era mala":

- El seguidor del suelo de ruido se quedaba clavado.
- El clasificador se bloqueaba con una raya absurda.
- El resembrado periódico destruía lo aprendido.
- El silenciador por nivel era imposible — se demostró midiendo, y se sustituyó
  por un criterio de forma.

El criterio del banco no es "acierta": es **"no puede MENTIR"**. Ante
interferencia o ruido tiene que decodificar bien o callarse, nunca inventar
letras plausibles. Un decodificador de CW sacando texto verosímil a partir de
ruido es peor que uno mudo, porque desde fuera no se distingue.

El filtro de audio de CW es un paso banda de **4 etapas** con ancho elegible
(1k0 / 500 / 250 Hz). Cuatro y no dos porque lo que decide cuántas señales oyes
a la vez es la falda, no el ancho a −3 dB:

```
  etapas   -3 dB     -20 dB    -40 dB
     2     250 Hz    1167 Hz   3851 Hz
     4     250 Hz     846 Hz   1725 Hz     <- elegido
     6     250 Hz     769 Hz   1364 Hz     <- ya casi no se gana
```

La Q sale de una medida (`sim/cwbw.c`), no de la fórmula de libro: con varias
etapas en serie `Q = f0/ancho` es falso. Resulta `Q = 0,434 · f0/ancho`,
constante entre 150 y 1000 Hz de ancho y entre 400 y 1000 Hz de tono.

### Tocar el espectro y caer en la señal

`spec_snap.c`. Busca la señal alrededor del dedo en los mismos datos que estás
viendo dibujados e interpola el pico con una parábola sobre los tres bins de
alrededor. Los bins miden 375 Hz, así que caer al entero deja hasta 187 Hz de
error; interpolando, medido sobre 32 realizaciones de ruido: **error medio
21–34 Hz, peor 64 Hz**.

Cae donde hay que caer para **oírla**, que no es donde está:

- AM/SAM/NFM/WFM → encima de la portadora.
- CW → un pitch por debajo, para que el tono salga al pitch ajustado en vez de
  a cero batido, que es inaudible.
- USB/LSB → en el borde de la señal, porque no hay portadora y el pico está en
  medio de la voz.

Si bajo el dedo no hay nada que destaque 8 dB sobre el ruido de alrededor, no
se inventa una señal: se comporta como antes.

### Autoescala del espectro (`spec_agc.c`)

La anterior usaba el mínimo y el máximo absolutos, y el pico de continua se los
comía: la escala se quedaba en 121 dB y el ruido en una franja de nada. Ahora
usa percentiles (5% y 99,5%) sobre un histograma de 128 cubos.

```
  con pico de continua y bordes:   antes  -1..120 (121 dB)   ahora  64..114 (50 dB)
```

`sim/autoesc.c` reproduce el caso y compara las dos.

### Lo demás

- **Temas** de interfaz y 15 paletas de espectro/cascada, elegibles.
- **Pantalla de información**: versión, fecha de compilación, flash y RAM
  usadas (calculadas desde los símbolos del linker, no escritas a mano).
- **Reloj** ajustable y teclado de frecuencia, los dos con el teclado nuevo.
- **Atenuador manual** que el AGC de RF ya no pisa: lo que eliges es un *suelo*,
  el automático puede atenuar más pero nunca menos.
- **WFM** se pone sola al tope de ganancia de entrada, porque la pérdida del
  mezclador en VHF se come unos 20 dB y si no hay que subirlo a mano cada vez.

---

## Una cosa que te va a pasar si tocas esto

El fallo más repetido de todo el proyecto, tres veces, siempre igual:

**la misma lista escrita en varios sitios.**

- La tabla de modos tenía una tabla de descripciones en paralelo a la que le
  faltaba una entrada. Resultado: puntero fuera de rango y la pantalla de modos
  entera rota.
- La lista de paletas estaba escrita **siete veces**. Tres paletas se mostraban
  con el nombre de otra.
- El ancho del filtro se formateaba en cuatro sitios. Uno se quedó devolviendo
  un texto fijo cuando los otros ya decían el valor real.

Ninguno daba error de compilación. Por eso ahora:

- Las listas viven en **una** tabla y todo lo demás la recorre.
- Hay `_Static_assert` en las tablas emparejadas: si añades una entrada a una y
  te olvidas de la otra, **no compila**.
- `tools/modos_check.py` y `tools/paleta_check.py` leen el fuente y comprueban
  que no hay estados duplicados ni textos vacíos.

Si añades algo y tienes que acordarte de tocar dos sitios, es que falta una
tabla.

---

## Los bancos de pruebas

En `deepsdr-ui/sim/`. Los que corren en `make comprueba`:

| Banco | Qué comprueba |
|---|---|
| `cwtest` | El decodificador de CW contra ruido, interferencia y clics |
| `autoesc` | Que la autoescala no se la come el pico de continua |
| `snaptest` | Que tocar el espectro engancha donde debe y no inventa señales |
| `rejilla` | Que el mando vuelve a la rejilla del paso |
| `paletachk` | Tabla de paletas coherente, claves de ida y vuelta, ciclo completo |
| `freqtap` | Que las zonas táctiles de la cabecera no invaden lo de al lado |
| `kbd`, `digi`, `config`, `bandas` | Que ningún texto se sale de su celda |
| `layout`, `guardtest`, `acttest`, `spec3blayout` | Geometría y recorte |

Y fuera del `comprueba`, para mirar en vez de para pasar: `cwbw` (respuesta del
filtro de CW), `paletas` (las 15 paletas con los mismos datos), `spec3b` (el
panel de espectro completo), `chromecost` (coste en ventanas EXMC).

El patrón que hace que esto funcione: **los bancos escriben la regla aparte**.
`sim/cwtest.c` tiene su propia tabla de Morse escrita a mano, y `sim/rejilla.c`
su propia versión de la cuenta. Si el banco llamara a la función del firmware
comprobaría que hace lo que hace, no lo que debe.

---

## Estado

```
  FLASH   229.228 de 262.144      libre 32.916
  RAM     140.744 BSS + 49.348 datos
  avisos del compilador                    0
  comprobaciones                       14/14
```

De la RAM, 54 KB son el historial de la cascada (un byte por píxel, índice de
paleta en vez de color — eso ahorró 56 KB) y 38 KB la banda de dibujo de gfx2
(`GFX2_BAND_H`, ajustable en compilación si alguna vez hace falta RAM).

## Lo que queda pendiente

- **WFM está limitada por diseño**: una emisora comercial con ±75 kHz de
  desviación ocupa ~200 kHz y la radio captura 192. Está documentado en
  `demod_am.c` junto a `WFM_DISC_GAIN`, con el plan de doble tasa que lo
  arreglaría de verdad.
- **Una paleta (`DEEPSDR`) está fuera** porque añadirla rompía el audio de WFM
  por un mecanismo que nunca llegué a entender. Se descartaron varias hipótesis
  (bucle de guardado bloqueante, desbordamiento del memcpy, tamaño del enum) y
  ninguna encajó. Preferí dejarla fuera antes que meter código cuyo fallo no
  entiendo.
- Los seis `s_btn_*` de la barra de abajo siguen existiendo solo como fichas de
  identidad para saber qué botón se ha pulsado. Debería ser un enum.
- RTTY y CW no guardan su velocidad ni su tono en `CONFIG.CSV`.
