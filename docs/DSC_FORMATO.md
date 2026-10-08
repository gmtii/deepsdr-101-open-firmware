# DSC de onda corta — el formato, confirmado contra el estándar

**Fuente:** Recomendación **ITU-R M.493-16 (12/2023)**, *Digital selective-calling
system for use in the maritime mobile service*. Es la versión en vigor.
Descargada y leída el 07/10/2026 de
`itu.int/dms_pubrec/itu-r/rec/m/R-REC-M.493-16-202312-I!!PDF-E.pdf`.

Esto **no** sale de mi memoria. Lo digo porque el compromiso era ése: los
parámetros se confirman contra el documento y no de cabeza. Lo que aquí no
tenga número confirmado va marcado como pendiente.

---

## 1. La modulación — ya la tenemos

```
  FSK, 170 Hz de desplazamiento, 100 bit/s ±30 ppm
  la frecuencia ALTA  = estado B = cero binario
  la frecuencia BAJA  = estado Y = uno  binario
```

**Es exactamente la del NAVTEX** (SITOR-B, 100 Bd, 170 Hz), que esta radio ya
decodifica en `navtex.c`. O sea que el detector de tonos y el reloj de bit no
hay que inventarlos: se parte de ahí.

## 2. El carácter: diez bits

```
  bits 1..7   información, símbolos 0..127
  bits 8..10  comprobación: la CUENTA de elementos B (ceros) que hay en los
              siete de información, en binario
```

**El orden importa y es asimétrico** — éste es el detalle que no habría
acertado de memoria:

- los siete de información van con el **bit menos significativo primero**
- los tres de comprobación van con el **más significativo primero**

Un carácter con una cuenta de B que no cuadre con sus siete bits está mal
recibido y se descarta.

## 3. Patrón de puntos y enganche

```
  patrón de puntos (HF/MF)   200 bits  en alertas de socorro y casi todo
                              20 bits  en acuses a llamadas individuales y
                                       comunicaciones de estación costera
  carácter DX (primera emisión)   símbolo 125
  caracteres RX (repetición)      111, 110, 109, 108, 107, 106, 105, 104
                                  en ese orden
```

## 4. Diversidad temporal

Cada carácter **que no sea de enganche** se emite **dos veces**: una en
posición DX y otra en posición RX, separadas por **cuatro caracteres**. El
intervalo de diversidad es de **400 ms** en HF/MF (y 33⅓ ms en VHF).

Esto es lo que da la robustez del modo: una ráfaga de ruido que se come un
carácter no se come su copia, que va 400 ms más allá.

## 5. Especificadores de formato

```
  112  socorro
  116  todas las estaciones
  120  estación individual
  114  grupo de interés común
  102  zona geográfica
  123  individual, servicio automático
```

## 6. Categoría de la llamada

```
  100  rutina
  106  ACS (sistema de conexión automática)
  108  seguridad
  110  urgencia
  112  socorro
```

El 112 aparece en las dos tablas, y es correcto: son campos distintos.

## 7. Fin de secuencia

```
  117  acuse pedido (RQ)  - llamadas individuales y automáticas que lo piden
  122  acuse dado   (BQ)  - respuesta a las anteriores
  127  todas las demás, sin acuse
```

## 8. Naturaleza del socorro

```
  100  incendio o explosión        106  sin gobierno, a la deriva
  101  vía de agua                 107  socorro sin especificar
  102  abordaje                    108  abandono del buque
  103  varada                      109  piratería o robo a mano armada
  104  escora, peligro de zozobrar 110  hombre al agua
  105  hundimiento
```

## 9. La dirección (MMSI)

El MMSI de nueve cifras va en **cinco caracteres**, dos dígitos por carácter,
con un cero de relleno al final:

```
  carácter 1   (M, I)        carácter 4   (X7, X8)
  carácter 2   (D, X4)       carácter 5   (X9, 0)
  carácter 3   (X5, X6)
```

Cada pareja de dígitos es directamente el número de símbolo.

## 10. Carácter de comprobación (ECC)

Los siete bits de información del ECC son el **bit menos significativo de las
sumas módulo 2** de los bits correspondientes de **todos** los caracteres de
información — o sea, paridad vertical par, columna a columna.

- **Cuentan** como caracteres de información el especificador de formato y el
  de fin de secuencia.
- **No cuentan** los de enganche ni los RX.
- El ECC se emite **en las dos posiciones**, DX y RX.

---

## CONFIRMADO CONTRA SEÑAL REAL — 07/10/2026

El dueño mandó cinco grabaciones. **Cuatro son DSC de onda corta a 100 Bd**
(la quinta es de VHF, 1200 Bd, que es otro modo). Medido por el criterio que
no se puede falsear: cuánto se acercan las duraciones de racha a múltiplos
enteros del periodo de bit.

```
  fichero          velocidad   error al entero
  Dsc_examples       100 Bd        0,000
  GMDSS_3            100 Bd        0,008
  GMDSS_1            100 Bd        0,033
  GMDSS_2            100 Bd        0,100
  Gmdss_vhf_...      (no es 100)   0,267   <- VHF, 1200 Bd
```

Con un prototipo en Python (`tools/dsc_proto.py`, `tools/dsc_parse.py`), que
aplica literalmente lo escrito arriba —alta = cero, siete bits con el menos
significativo primero, tres de comprobación con el más significativo primero y
la cuenta de ceros— sale esto en **tres ficheros independientes**:

```
  125 111 125 110 125 109 125 108 125 107 125 106 120 105 120 104 ...
```

El enganche entero: DX = 125 alternando con RX = 111 a 104 en orden, y el
especificador de formato entrando en las ranuras DX mientras el RX todavía va
por 105 y 104 — **la diversidad de cuatro caracteres, vista en el aire**.

Y las llamadas, completas:

```
  GMDSS_1       formato 120 individual -> MMSI 004634060
                categoria 108 seguridad,  de MMSI 215322000

  GMDSS_3       formato 120 individual -> MMSI 312724000
                categoria 108 seguridad,  de MMSI 312714000,  fin 117 (RQ)

  Dsc_examples  formato 120 individual -> MMSI 002320001
                categoria 100 rutina,     de MMSI 005030001,  fin 117 (RQ)
```

**Lo que no se programó y salió solo**, que es la mejor señal de que la lectura
del formato es correcta: en el resto de la trama de `Dsc_examples` aparece
`8 29 10` dos veces, y en `GMDSS_3` `12 36 0` dos veces. Leídos como seis
cifras en unidades de 100 Hz son **8.291,0 kHz** y **12.360,0 kHz**:
frecuencias marítimas de onda corta reales, repetidas dos veces como
corresponde a un par de frecuencias de trabajo.

Esa interpretación concreta todavía NO está confirmada contra el documento
—falta leer §8— pero el hecho de que caiga sola en frecuencias que existen es
lo bastante fuerte como para dejarlo escrito.

### UNA CORRECCIÓN QUE SOLO DIERON LAS GRABACIONES

El §10 dice que el especificador de formato **cuenta** como carácter de
información para el ECC. Lo que no dice con esas palabras es qué pasa con
que se emita **dos veces** (§4). La primera versión del decodificador metió
las dos copias y el ECC falló siempre.

Con las dos secuencias reales delante salió solo:

```
  GMDSS_1   XOR de los 22 caracteres = 45     45 ⊕ 120 = 85   = ECC recibido
  GMDSS_3   XOR de los 22 caracteres = 28     28 ⊕ 120 = 100  = ECC recibido
```

Las dos, con el mismo 120 de sobra. O sea: **el especificador cuenta UNA vez,
no dos**. Corregido, y con eso el ECC pasa en las cuatro llamadas.

Esto es justo lo que no se saca leyendo y hay que sacar midiendo.

### Estado del decodificador en C (07/10/2026)

`User/dsc.c` ya decodifica, y se comprueba con `sim/dscaire.c`:

```
  GMDSS_1       individual -> 004634060, de 215322000, seguridad   ECC correcto
  GMDSS_3       individual -> 312724000, de 312714000, seguridad   ECC correcto
  Dsc_examples  dos llamadas completas                             ECC correcto
  GMDSS_2       no sale
```

**GMDSS_2 no decodifica a ningún centro.** Es el que peor ajuste de tiempos
daba de los cuatro (0,100 contra 0,000-0,033), así que lo más probable es que
sea la grabación y no el decodificador — pero eso NO está demostrado y queda
escrito como lo que es: uno de cuatro que no sale.

### Lo que falta

- **Meterlo en la radio**: fila de modo, panel, y el ajuste del centro. El
  decodificador necesita saber dónde están los tonos y eso depende del dial:
  en estas cuatro grabaciones los centros fueron 995, 1700 y 2214 Hz. La
  búsqueda automática por cruces por cero (la del NAVTEX) NO vale aquí
  —se equivocaba en 500 y 700 Hz— porque estas grabaciones llevan silencio y
  otras cosas dentro. Hay que usar algo como el `rtty_scope`/`rtty_auto` que
  ya existe, o dejarlo como ajuste.
- **El banco** con trama sintética, incluido el formato de SOCORRO, que es el
  único que no tiene grabación.

### Lo que esto cierra

El compromiso era no escribir el decodificador con parámetros de memoria. Ya no
hace falta: están leídos del estándar **y** comprobados contra cuatro
grabaciones reales. La radio ya tiene el cañón de delante (NAVTEX, misma
modulación), así que lo que falta es portar este prototipo a C.

---

## Lo que queda por confirmar antes de escribir el decodificador

- La **tabla A1-2** completa, símbolo 0..127 → dígitos y caracteres, para los
  campos de mensaje. Aquí sólo está confirmado el empaquetado de dos dígitos
  por símbolo del MMSI.
- Las tablas de **telemando 1 y 2** (`§8`), que son las que dicen qué pide la
  llamada.
- La **frecuencia o canal de trabajo** que viaja en el mensaje, y cómo se
  codifica.
- Las **frecuencias de llamada de HF** en las que merece la pena escuchar — eso
  no es de M.493 sino de la práctica; hay una lista en
  `docs/DSC_FRECUENCIAS.txt` que habrá que contrastar.

## Y la línea de siempre

Esto es demodular una forma de onda y leer un entramado público. El DSC va en
claro por definición —es un sistema de llamada, no de contenido— así que aquí
no hay nada que descifrar ni se descifrará.
