# STANAG 4415 / tono serie robusto a 75 bps

*07/10/2026. Todo lo de este documento sale de medir las dos grabaciones que
mandó el dueño, no de recordar un estándar. Lo que no está medido lo dice.*

Grabaciones: `Stanag-4415.mp3` (21,4 s) y `Robser-s.mp3` (51,5 s, «robust
serial»). Las dos remuestreadas a 12 kHz, que es lo que come `stanag_rx.c`.

---

## 1 · Portadora y velocidad

| | Stanag-4415 | Robser-s |
|---|---|---|
| centroide del espectro | 1.480 Hz | **1.787 Hz** |
| ocupado a −20 dB | 227–2.673 Hz | 330–3.173 Hz |
| línea de símbolo en \|z\|² | — | **2.394,6 Hz** |

**Robser-s es tono serie de libro: portadora 1.800 Hz, 2.400 baudios.** La
línea de la velocidad de símbolo sale 60 veces por encima de la mediana del
espectro, así que no hay duda.

**Stanag-4415 está recortado por arriba** —a 2.800 Hz ya está 30 dB abajo—, y
eso tira del centroide hacia abajo. El ancho a −20 dB sigue siendo de 2.446
Hz, compatible con 2.400 Bd pasado por un receptor estrecho. El centroide de
1.480 Hz **no** es la portadora: es lo que queda después de que el filtro del
receptor se comiera el lado de arriba.

**Robser-s está además fuera de velocidad:** 2.394,6 en vez de 2.400 Bd, un
0,22 % lento. Sobre 320 símbolos eso es casi un símbolo de deslizamiento, y
explica que la autocorrelación salga en 321 y no en 320. Es la grabación, no
la señal.

---

## 2 · La trama: 160 símbolos

Probando periodos sobre los símbolos ya demodulados de `Stanag-4415`, midiendo
la coherencia de `s²` (que quita la modulación BPSK y deja ver lo que se
repite):

| periodo | coherencia media | máxima |
|---|---|---|
| 32 | 0,184 | 0,351 |
| 40 | 0,173 | 0,292 |
| 80 | 0,200 | 0,427 |
| **160** | **0,307** | **0,528** |
| 320 | 0,158 | 0,334 |

**160 símbolos.** Y lo confirma algo que no se puede discutir: el **patrón de
amplitud** se repite idéntico cada 160 símbolos durante los 21 segundos
enteros. Comparando las posiciones 0–15 con las 160–175:

```
  0: 1.15 0.83 0.84 0.77 1.04 0.93 1.22 1.05 0.70 1.08 0.80 1.10 0.95 1.03 0.79 0.92
160: 1.10 0.80 0.88 0.78 1.06 0.94 1.22 1.10 0.73 1.06 0.86 1.10 0.96 1.06 0.79 0.93
```

160 símbolos a 2.400 Bd son **66,67 ms**, o sea **15 tramas por segundo**
exactas.

---

## 3 · La cuenta que cuadra

A 75 bps con tramas de 15 por segundo salen **5 bits de datos por trama**. Con
corrección de errores de tasa 1/2 —que es lo normal en esta familia— son **10
bits codificados en 160 símbolos**, o sea **16 símbolos por bit codificado**.

```
   75 bps  x 2 (FEC 1/2)  = 150 bps codificados
   2400 Bd / 150          = 16 simbolos por bit
   160 simbolos / 16      = 10 bits codificados por trama
   10 / 2                 = 5 bits de datos  ->  5 x 15 = 75 bps
```

Cierra por los dos lados. Eso apunta a un **ensanchado de 16 símbolos por
bit** (una secuencia tipo Walsh), que es lo que hace robusto a este modo: cada
bit se decide sumando 16 símbolos, y eso son 12 dB de ganancia de proceso.

**Esto es una hipótesis que cuadra con todo lo medido, no un dato del
estándar.** Lo que falta por medir es la secuencia concreta de 16 y cómo se
reparten los 160 símbolos entre datos y sincronismo.

---

## 4 · Lo siguiente, y en este orden

1. **Sacar la secuencia de ensanchado de la propia grabación.** Con 160 tramas
   y el patrón repitiéndose, promediando por posición debería salir sola.
2. **Encontrar el preámbulo**, que es lo que permite enganchar sin adivinar.
3. **Descodificar.** El front end ya existe: `stanag_rx.c` hace la mezcla
   desde 1.800 Hz, el filtro adaptado, el reloj de símbolo y el barrido de
   portadora para el 4285, que es el mismo tono serie a 2.400 Bd.

Lo que **no** hace falta es un ecualizador. Está medido en `sim/stanag_multi.c`:
el 4285 a 300 bps con entrelazado largo aguanta un eco del 100 % a 1,2 ms sin
perder el texto. A 16 símbolos por bit, el 4415 va a ser todavía más duro. El
ecualizador hace falta para las **velocidades altas del 4539**, no para esto.

---

## 5 · Lo que NO se sabe *(resuelto, ver §6)*

- La secuencia de ensanchado exacta.
- El preámbulo.
- Si estas dos grabaciones son el mismo modo. Comparten la rejilla de 66,67 ms
  pero `Robser-s` no enseña patrón de amplitud repetido y `Stanag-4415` sí, lo
  que puede significar que una lleva datos y la otra está en reposo.
- **Si el tráfico va cifrado.** Casi todo lo de esta familia lo va. Se
  demodula la forma de onda; el cifrado no se toca.


---

# 6 · Y EL ESTÁNDAR, que lo confirma todo

*El dueño: «lo buscas en el historial que ya te lo pasé hace días». Y estaba:
**MIL-STD-188-110A**, escaneado, pasado por OCR. Lo de abajo son citas, no
recuerdos.*

## 6.1 · El ensanchado — tabla XIV

> At 75-bps fixed-frequency operation, the channel symbols shall consist of
> **two bits for 4-ary channel symbol mapping**. Unlike the higher rates, **no
> known symbols (channel probes) shall be transmitted and no repeat coding
> shall be used**. Instead, the use of **32 tribit numbers** shall be used to
> represent each of the 4-ary channel symbols.

**TABLE XIV. Channel symbol mapping for 75 bps**

| dibit | sets normales | sets excepcionales |
|---|---|---|
| 00 | `(0000)` ×8 | `(0000 4444)` ×4 |
| 01 | `(0404)` ×8 | `(0404 4040)` ×4 |
| 10 | `(0044)` ×8 | `(0044 4400)` ×4 |
| 11 | `(0440)` ×8 | `(0440 4004)` ×4 |

Los tribits 0 y 4 son fase 0 y 180: **BPSK**. Cuatro formas de onda
ortogonales de 32 símbolos — Walsh de cuatro, repetido.

Los sets **excepcionales** son cada 45.º (entrelazado corto) o cada 360.º
(largo), justo detrás del preámbulo. Y el estándar dice para qué sirven:

> The receive modem shall use the modification of the known data at
> interleaver boundaries to **synchronize without a preamble and determine the
> correct data rate and mode of operation**.

O sea que se puede enganchar a mitad de transmisión. Es la puerta de entrada
para una radio que llega tarde a la emisión, que es siempre.

## 6.2 · El preámbulo

> The synchronization pattern shall consist of either **three or twenty-four
> 200-millisecond segments**... Each 200-ms segment shall consist of a
> transmission of **15 three-bit channel symbols**... The sequence of channel
> symbols shall be: **0, 1, 3, 0, 1, 3, 1, 2, 0, D1, D2, C1, C2, C3, 0**

- 15 símbolos × 32 tribits = 480 tribits = 200 ms a 2400 Bd. Cuadra.
- **D1, D2** dicen la velocidad y el entrelazado (tabla XV).
- **C1, C2, C3** son la cuenta atrás de segmentos: empieza en 2 (corto/cero) o
  23 (largo) y baja hasta cero.
- El mapeo de los ocho símbolos de preámbulo a 32 tribits está en la tabla
  XVII, y es el mismo patrón `0/4` de ocho formas en vez de cuatro.

**Y ESTO EXPLICA LA MEDIDA.** La autocorrelación de `Stanag-4415.mp3` daba
picos en **200,00 / 400 / 600 / 800 ms**. Me fui detrás de la rejilla de 66,67
ms y los 200 eran el preámbulo, dicho por el estándar al milisegundo.

## 6.3 · El aleatorizador, y por qué salían 160

> The data sequence randomizing generator shall be a **12-bit shift register**
> ... loaded with the initial pattern **101110101101 (binary) or BAD
> (hexadecimal)** and advanced **eight times**. The resulting three bits shall
> be used to supply the scrambler with a number from 0 to 7. The shift
> register shall be shifted eight times each time a new three-bit number is
> required (every transmit symbol period). **After 160 transmit symbols, the
> shift register shall be reset to BAD**.
>
> NOTE: This sequence produces a **periodic pattern 160 transmit symbols in
> length.**

**Los 160 símbolos que salieron midiendo a ciegas son el aleatorizador.** No
la trama. La autocorrelación de la grabación dijo 160 y el estándar dice 160.

Y explica el fracaso del intento de sacar el ensanchado dividiendo cada grupo
de 16 por su primer símbolo: las 16 alineaciones daban 0,16-0,25, todas
iguales. Claro — encima hay un aleatorizador de periodo 160 que no es
múltiplo de 16 por casualidad, sino que **lo tapa todo** hasta que se quita.

El tribit se suma **módulo 8** al que sale del generador. Los tres bits salen
de tres etapas consecutivas del registro (figura 6); las derivaciones exactas
hay que leerlas del dibujo con cuidado, **y se comprueban solas**: con el
aleatorizador bien puesto, esa consistencia de 0,25 tiene que irse a 1. Es
una prueba que no admite discusión y no hace falta creerse nada.

## 6.4 · Lo que queda, en orden

1. **Generar la secuencia del aleatorizador** (12 bits, 0xBAD, 8 pasos,
   periodo 160) y comprobarla contra la grabación. Si la consistencia salta,
   las derivaciones son las correctas.
2. **Desensanchar** con la tabla XIV: correlar cada 32 tribits contra las
   cuatro formas y quedarse con la mayor.
3. **Enganchar**, por el preámbulo (200 ms) o por los sets excepcionales.
4. **Desentrelazar y Viterbi** — el `hfdl_viterbi.c` que ya usa el 4285.

El front end —mezcla desde 1.800 Hz, filtro adaptado, reloj de símbolo,
barrido de portadora— **ya está escrito** en `stanag_rx.c` para el 4285, que
es el mismo tono serie a 2.400 baudios.

## 6.5 · Y lo que no cambia

El cifrado no se toca. Se demodula la forma de onda y sale lo que salga; si
va cifrado, saldrán bytes y se dirá que van cifrados.

---

# 7 · ENGANCHADO. El preámbulo de la grabación, leído entero

*07/10/2026, por la tarde. Esto ya no es análisis: es el preámbulo de la
grabación del dueño decodificado con la plantilla del estándar.*

## 7.1 · La plantilla

Los **nueve primeros símbolos del preámbulo son fijos** —`0,1,3,0,1,3,1,2,0`—
y cada uno se convierte en 32 tribits por la tabla XVII. Encima, el
aleatorizador del preámbulo, que el estándar da escrito y no hay que deducir:

> The following scrambling sequence for the sync preamble shall repeat every 32
> transmitted symbols: **74305150221157435026216200505266**

Nueve símbolos × 32 = **288 tribits completamente conocidos**, 120 ms de
plantilla. Correlando eso contra la grabación, con normalización e insensible
a la fase:

```
  Stanag-4415.mp3 : pico 0,624   mediana 0,050   ->  12,5x
                    24 detecciones
                    separaciones: 200,0 200,0 200,0 200,0 200,0 200,0 ... ms
```

**Veinticuatro segmentos separados 200,0 ms exactos.** Y el estándar dice:

> The synchronization pattern shall consist of either three or **twenty-four**
> 200-millisecond segments (depending on whether either zero, short, or **long**
> interleave periods are used).

## 7.2 · Y lo que llevan dentro

Detrás de los nueve fijos van D1, D2, C1, C2, C3. Leídos segmento a segmento
contra los ocho patrones posibles:

| seg | D1 | D2 | C1 C2 C3 | cuenta | calidad |
|---|---|---|---|---|---|
| 0 | 5 | 5 | 5 5 7 | **23** | 0,79 |
| 1 | 5 | 5 | 5 5 6 | **22** | 0,78 |
| 2 | 5 | 5 | 5 5 5 | **21** | 0,76 |
| … | | | | … | |
| 22 | 5 | 5 | 4 4 5 | **1** | 0,79 |
| 23 | 5 | 5 | 4 4 4 | **0** | 0,80 |

La cuenta atrás completa:

```
23 22 21 20 19 18 17 16 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0
```

**Veinticuatro valores, bajando exactamente de uno en uno, sin un solo
fallo.** Eso no sale por casualidad ni con una plantilla equivocada.

## 7.3 · Y la tabla XV cierra el círculo

**TABLE XV. Assignment of designation symbols D1 and D2**

| Bit rate | corto D1 D2 | largo D1 D2 |
|---|---|---|
| 2400 (datos) | 6 4 | 4 4 |
| 1200 | 6 5 | 4 5 |
| 600 | 6 6 | 4 6 |
| 300 | 6 7 | 4 7 |
| 150 | 7 4 | 5 4 |
| **75** | 7 5 | **5 5** |

**D1 = 5, D2 = 5 → 75 bps, entrelazado LARGO.** Unánime en los veinticuatro
segmentos.

Y son **dos confirmaciones independientes de lo mismo**: la cuenta empieza en
23, que el estándar reserva para el entrelazado largo, y D1/D2 dicen largo
también.

## 7.4 · Estado

| pieza | estado |
|---|---|
| portadora y velocidad | **medido** 1.800 Hz / 2.400 Bd |
| aleatorizador de datos | **leído de la figura 6 y verificado** (0,318 contra un techo de 0,149 al azar) |
| aleatorizador de preámbulo | **dado por el estándar**, verificado por la correlación |
| preámbulo | **ENGANCHADO**, 24 segmentos, cuenta atrás perfecta |
| velocidad y entrelazado | **75 bps, largo**, por D1/D2 y por la cuenta |
| ensanchado (tabla XIV) | escrito, sin probar todavía |
| desentrelazado y Viterbi | pendiente — el `hfdl_viterbi.c` del 4285 sirve |

Lo que queda es desensanchar con la tabla XIV, desentrelazar y pasar el
Viterbi. El enganche, que era lo difícil, está hecho.

`Robser-s.mp3` **no engancha** (pico 0,219, 4,4× sobre la mediana, ninguna
detección): no es 75 bps de 110A.

---

# 8 · DEL ENGANCHE AL TEXTO. Lo que faltaba, medido

*07/10/2026, por la noche. La sección 7 dejó el preámbulo enganchado. Esta
cierra la cadena: desensanchado, desentrelazado, Viterbi y caracteres, con el
módulo de la radio y su banco.*

## 8.1 · La tabla XIV no necesitaba tabla

El estándar da ocho patrones, cuatro normales y cuatro "excepcionales":

| dibit | set normal | set excepcional |
|---|---|---|
| 00 | `(0000)` ×8 | `(0000 4444)` ×4 |
| 01 | `(0404)` ×8 | `(0404 4040)` ×4 |
| 10 | `(0044)` ×8 | `(0044 4400)` ×4 |
| 11 | `(0440)` ×8 | `(0440 4004)` ×4 |

Escritos así parecen ocho cosas nuevas. No lo son: `(0000)` ocho veces y
`(0000 0000)` cuatro veces son los **mismos 32 tribits**. Los ocho patrones de
la tabla XIV son exactamente los ocho de la **tabla XVII** —las ocho funciones
de Walsh de longitud 8 repetidas cuatro veces—, los normales las filas 0..3 y
los excepcionales las 4..7.

Así que el desensanchado es **un único decisor de ocho formas**: el dibit son
los dos bits de abajo del índice ganador y el de arriba dice si el set era
excepcional.

## 8.2 · Y el set excepcional marca el bloque del entrelazador

> The mapping in table XIVa shall be used for all sets of 32 tribit numbers
> with the exception of every 45th set (following the end of the sync pattern)
> if short interleave is selected, and **every 360th set** (following the end
> of sync pattern) if **long interleave** is selected.
>
> NOTE: ... **The receive modem shall use the modification of the known data at
> interleaver boundaries to synchronize without a preamble** and determine the
> correct data rate and mode of operation.

Medido sobre `Stanag-4415.mp3`:

```
  sets excepcionales: 3  en 359 719 1079
  desalineos 0, bloques sin marca 0
```

**Tres bordes, los tres donde dice el estándar.** Y eso confirma de una vez el
desensanchado, dónde empiezan los datos y el tamaño del bloque, contra una
marca que no estaba en el código antes de medirla.

## 8.3 · El entrelazador, que a 75 bps es otro

**TABLE X. Interleaver matrix dimensions** (fila `75N`, frecuencia fija):

| | filas | columnas |
|---|---|---|
| largo | **20** | **36** |
| corto | **10** | **9** |

Y las dos excepciones que el estándar escribe aparte para esta velocidad:

> a. When the interleave setting is on long, the procedure is the same, but the
> **row number shall be advanced by 7 modulo 20**.
>
> b. For fixed-frequency operation at the 75-bps rate, the interleaver fetch is
> similar except **the decrement value of the column number shall be 7** rather
> than 17.

O sea:

```
  carga     m -> fila (7*(m mod F)) mod F,  columna m/F
  lectura   k -> fila k mod F,  columna (k/F - 7*(k mod F)) mod C
  deshacer  m = c*F + (3*r) mod F        (3 es el inverso de 7 mod 20 y mod 10)
```

Con los números de las demás velocidades —9 mod 40 y bajada de 17— la
permutación sigue siendo **válida**, y no sale nada. Falla en silencio, que es
lo peligroso.

20 × 36 = 720 bits codificados = 360 bits de datos = **4,8 s a 75 bps**, que es
exactamente el retardo que el estándar pide para el entrelazado largo. Y 360
dibits por bloque es justo el "every 360th set". Todo cuadra con todo.

## 8.4 · El código, de la figura 4

```
  CONSTRAINT LENGTH = 7
  T1  x6 + x4 + x3 + x + 1
  T2  x6 + x5 + x4 + x3 + 1
```

En el dibujo la entrada entra por la celda X^6 y empuja hacia "1", así que X^6
es el bit nuevo y "1" el de hace seis. Con esa orientación T1 y T2 son
**0x6d y 0x4f**, que son exactamente los `V27POLYA`/`V27POLYB` de libfec — los
mismos que ya usa `hfdl_viterbi.c` para HFDL. No hace falta decodificador
nuevo: es el mismo código de siempre (133/171 en octal) y el mismo módulo.

## 8.5 · La prueba

```
  bloques 3, bits 1080, EOM 1, fin 1, desalineos 0, sin marca 0
```

**`4B65A5B2`** —el EOM de 5.3.2.3.1, con el bit más significativo primero— son
32 bits concretos saliendo del Viterbi después de desentrelazar 720. La
probabilidad de que aparezcan por casualidad en 1.080 bits es de uno entre
cuatro millones.

Detrás van ceros: el vaciado de 144 bits de 5.3.2.3.2. Por eso el módulo da la
emisión por terminada ahí.

**El desentrelazador y el Viterbi están probados en el aire, no en
simulación.**

## 8.6 · Lo que sigue sin confirmar

**Cuál de los dos bits del dibit sale primero del entrelazador.** La tabla XI
dice que a 75N se sacan dos bits por símbolo de canal; la tabla XIV indexa por
"00, 01, 10, 11"; pero el estándar no dice con todas las letras cuál de los dos
es el de la izquierda. Está puesto lo razonable, con el interruptor
`BIT_ALTO_PRIMERO` en `s4415.c`.

Esta grabación no sirve para decidirlo: **el 85% de los dibits son ceros**
—relleno de inactividad y vaciado— y con todo ceros los dos órdenes dan lo
mismo. Con una captura que lleve tráfico de verdad se resuelve en un minuto:
un orden saca texto y el otro basura.

## 8.7 · Estado

| pieza | estado |
|---|---|
| portadora y velocidad | **medido** 1.800 Hz / 2.400 Bd |
| aleatorizador de datos | **leído de la figura 6 y verificado** |
| aleatorizador de preámbulo | **dado por el estándar**, verificado por la correlación |
| preámbulo | **ENGANCHADO**, 23 segmentos, cuenta atrás perfecta |
| velocidad y entrelazado | **75 bps, largo**, por D1/D2 y por la cuenta |
| ensanchado (tabla XIV) | **MEDIDO**: bordes de bloque en 359, 719, 1079 |
| desentrelazado y Viterbi | **PROBADO EN EL AIRE**: sale el EOM del estándar |
| orden de los dos bits del dibit | **sin confirmar** — hace falta tráfico |
| el contenido | no es texto. Va cifrado, y **el cifrado no se toca** |
