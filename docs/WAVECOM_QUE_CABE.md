# Qué del catálogo de Wavecom cabe en esta radio

*06/10/2026. Repasado contra el índice completo de modos de la ayuda en línea
del decodificador Wavecom (unas 250 entradas) y contra lo que esta radio ya
tiene montado.*

> **De dónde sale cada cosa.** Los NOMBRES y la existencia de cada modo salen
> del índice de Wavecom, que leí entero. Los PARÁMETROS de cada modo
> (velocidades, desplazamientos, anchos) **no** los saqué de esa web: salen de
> lo que yo sé de cada norma, y por tanto hay que confirmarlos contra el
> documento correspondiente antes de escribir una línea de código. Hoy mismo
> me he equivocado dos veces por afirmar más de lo medido; aquí queda dicho
> cuál es cuál.

---

## 0. Lo primero: no cabe un modo más

Antes de elegir nada, hay un tope duro:

```
  MODO_FAM_ANA    6 de 16 modos
  MODO_FAM_DIG   16 de 16 modos      <- LLENO
```

La rejilla de la pantalla de modos es densa, 4×4, o sea `UIC_CELLS = 16`. Y
`cfg_fill()` recorre la tabla con `n < UIC_CELLS` y **para ahí**. El modo 17 de
la familia Digital no se dibujaría, no avisaría nadie y el compilador no puede
saberlo.

Es exactamente el mismo fallo que el décimo ajuste de la página Digital, que ya
vigila `paginas_ajustes()`. Así que hoy se ha añadido la comprobación hermana a
`tools/modos_check.py`: ahora imprime `16 de 16` y avisa de que el siguiente no
entra.

**Hay que resolver esto antes de añadir el primer modo nuevo.** La salida
natural es partir `MODO_FAM_DIG` en dos familias —la columna de categorías ya
aguanta varias: Bandas tiene cuatro— en vez de paginar la rejilla. Por ejemplo
`Digital` (lo que entrega texto o imagen de radioaficionado y servicio) y
`Datos` (lo de aviones, barcos y utilitarias). Es mover etiquetas, no tocar el
dibujante.

---

## 1. Lo que la radio ya hace

Para no proponer lo que ya está:

| | |
|---|---|
| **Analógicos (6)** | AM, LSB, USB, NFM, WFM, SAM |
| **Texto** | CW, RTTY (L/U), NAVTEX, PSK31, JTTY |
| **Imagen** | WEFAX, SSTV |
| **Datos** | APRS (AX.25), FT8, HFDL, WSPR, AIS, ALE |
| **Militar** | STANAG (4285 demodula; 4529/4481/4538 identifican) |
| **Medida** | IDENT, 28 firmas |

Y piezas sueltas reutilizables: `disc_fm.c` (discriminador FM), `navtex.c` (el
alfabeto CCIR 476 entero), `ax25.c` (tramas AX.25 + APRS), `hfdl_viterbi.c`
(K=7 tasa 1/2), `wspr_fano.c`, `ale_golay.c`, `jtty_fec.c`, `fft.c`, y el
camino de pintar renglones de `wefax.c` / `sstv.c`.

---

## 2. La selección, por orden de lo que dan por lo que cuestan

### A. GMDSS / DSC — **mi primera elección**

*HF: 100 Bd, desplazamiento 170 Hz, banda lateral. VHF: canal 70, 1200 Bd sobre
FM.*

- **Reutiliza**: el camino de FSK de 100 Bd y 170 Hz de `navtex.c` casi tal
  cual, incluido el medidor de centro por cruces por cero que arreglamos el
  05/10.
- **Hay que escribir**: la capa de símbolos, que NO es CCIR 476 sino símbolos
  de 10 bits con cuenta de unos, y el formato de llamada (formato, dirección,
  MMSI, categoría, comprobación de error).
- **Lo que da**: identidad del barco (MMSI), categoría de la llamada, posición
  cuando la lleva. **Va en claro**, hay tráfico todos los días y las
  frecuencias son fijas: 2187,5 · 4207,5 · 6312 · 8414,5 · 12577 · 16804,5 kHz.
- **Por qué primero**: es lo único de la lista que junta coste bajo, contenido
  legible de verdad y una frecuencia donde siempre hay algo que oír. Después de
  una tarde entera persiguiendo señales cifradas, eso vale.

### B. Paquete de HF a 300 Bd (+ Robust Packet)

*300 Bd FSK, 200 Hz de desplazamiento, sobre banda lateral.*

- **Reutiliza**: `ax25.c` entero. Las tramas y el formateo de APRS ya están.
- **Hay que escribir**: sólo el demodulador de FSK a 300 Bd que lo alimente.
- **Lo que da**: APRS de HF y correo de radioaficionado. En claro.
- **Coste**: el más bajo de toda la lista. Es enchufar un frente nuevo a un
  decodificador que ya funciona.

### C. FELDHELL

*122,5 caracteres por minuto, cada letra pintada como una columna de puntos.*

- **Reutiliza**: el camino de "pinta una columna en el panel" de `wefax.c`.
- **Hay que escribir**: casi nada. No lleva corrección de errores ni
  sincronismo de carácter: se pinta lo que llega y lo lee el ojo.
- **Lo que da**: poco contenido, pero es de los pocos modos donde se ve de un
  vistazo si la sintonía está bien. Buen banco de pruebas y barato.

### D. ACARS de VHF (131 MHz)

*2400 bps MSK sobre AM.*

- **Reutiliza**: la familia del HFDL (que es su primo de HF) y la maquinaria de
  GMSK del AIS.
- **Hay que escribir**: demodulador MSK y el formato ARINC-618.
- **Lo que da**: **esto encaja con lo que la radio ya tiene**. En la zona alta
  hay 101.432 aviones y 356 tipos cargados: un mensaje de ACARS trae la
  matrícula y el vuelo, y la radio ya sabe traducir eso a modelo y operador.
  La tabla de bandas ya tiene VHF/UHF.
- **Pega**: hace falta estar cerca de un aeropuerto.

### E. OLIVIA y MFSK-16

- **Reutiliza**: `fft.c` y el medidor de velocidad de símbolo que IDENT ya usa.
- **Hay que escribir**: la decisión por transformada de Hadamard (Olivia) o la
  de tono a bits (MFSK-16), más el entrelazado.
- **Lo que da**: modos de radioaficionado muy usados y en claro. Olivia aguanta
  señales por debajo del ruido, lo cual se nota.

### F. POCSAG

*512 / 1200 / 2400 bps, FSK directa sobre FM estrecha.*

- **Reutiliza**: `disc_fm.c`.
- **Hay que escribir**: el sincronismo de lote, BCH(31,21) y el reparto de
  direcciones. Todo sencillo.
- **Pega**: depende de que quede actividad de busca por la zona. Mirar antes de
  escribir.

### G. DGPS en onda larga (283,5–325 kHz)

*100 / 200 bps MSK, mensajes RTCM SC-104.*

- **La radio llega**: sintoniza desde 30 kHz.
- **Reutiliza**: poco; el demodulador MSK habría que escribirlo.
- **Lo que da**: las balizas emiten 24 horas y llegan fuertes. Como señal
  **siempre disponible para probar**, no tiene rival en esta lista.

### H. MIL-188-110A / 110B / STANAG 4539 — el proyecto largo

Ya analizado aparte. Comparte con el 4285 que ya funciona la portadora de 1800
Hz, los 2400 símbolos por segundo, el filtro adaptado y el Viterbi. Lo que falta
es el **ecualizador adaptativo**, y antes de eso hay que confirmar en antena los
caminos de QPSK y 8PSK del 4285, que están escritos a ciegas.

El muro es la RAM del entrelazador: el préstamo son 54.432 bytes, así que
entrelazado corto y medio a cualquier velocidad, y el largo sólo hasta unos
4800 bps.

---

## 3. Lo que descarto, y por qué

| Modo | Motivo |
|---|---|
| APCO-25, DMR, dPMR, NXDN, TETRA | Voz digital: hacen falta vocodificadores AMBE, patentados y pesados. |
| PACTOR-II / III / 4, CLOVER, G-TOR | Propietarios y sin especificación pública. PACTOR-I sí sería posible. |
| SAT-* (Inmarsat), ORBCOMM | Banda L y banda S: el receptor no llega. |
| V.21/V.22/V.32/V.34, FAX G3 | Módems de línea telefónica, no de radio. |
| CIS-*, la mayoría de los STANAG | El tráfico va cifrado. Se demodularía, no se leería. |
| LINK-11 | Complejo, y el aire está lleno de versiones. |
| FLEX, ERMES, MOBITEX | FLEX pide más ancho del que tenemos cómodo; ERMES está prácticamente muerto. |
| COQUELET, ARQ-E/M, SI-ARQ, SWED-ARQ, POL-ARQ, HNG-FEC, RUM-FEC | Telegrafía europea de los años 80: casi nadie emite ya. Coste bajo, valor nulo. |

---

## 4. El orden que propongo

1. **Partir `MODO_FAM_DIG` en dos familias.** Sin esto no entra nada. Es una
   tarde corta y además deja la pantalla más legible.
2. **GMDSS / DSC de HF.** Máximo valor por el coste, y contenido en claro.
3. **Paquete de HF a 300 Bd.** El más barato: aprovecha `ax25.c` entero.
4. **FELDHELL.** Casi gratis, y útil como prueba visual de sintonía.
5. A partir de ahí, según lo que se oiga por la zona: **ACARS** si hay
   aeropuerto cerca, **POCSAG** si queda busca, **Olivia** si se frecuenta HF
   de radioaficionado.
6. **110A / 110B**, cuando haya ganas de un proyecto de semanas y no de tardes.

---

## 5. Lo que hay que hacer antes de escribir código, en cualquiera de ellos

La regla de `ident.h` vale igual aquí: **una firma sólo entra con su número
medido**. Para un decodificador nuevo:

1. Confirmar los parámetros contra la norma, no contra mi memoria ni contra
   esta tabla.
2. Conseguir una grabación de aire ANTES de empezar. El SSTV de hoy demuestra
   lo que cuesta depurar un decodificador contra un banco que fabrica la señal
   con las mismas suposiciones que el decodificador la lee.
3. Montar el banco con la grabación, al estilo de `sim/sstvaire.c`.
