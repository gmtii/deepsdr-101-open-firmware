# HFDL: lo que cuesta y de dónde sale el sitio

*25/09/2026. Escrito antes de portar una sola línea, porque la primera
pregunta no es cómo se porta sino dónde cabe.*

## El encargo

*"Deberes que me voy, HFDL, empieza a implementarlo y luego vemos de dónde
sacar hueco."*

Quedaban **4.236 bytes de flash** de 262.144. Así que "de dónde sacar hueco"
no es un detalle posterior: es el proyecto entero. Y la respuesta no puede ser
una opinión sobre lo que *parece* gordo — este proyecto ya se equivocó así una
vez, cuando se predijo que el cuello de botella del dibujado era el bus de la
pantalla y el número medido dijo que no.

## Lo que cuesta HFDL, compilado de verdad

No estimado: los 23 módulos de la rama `hfdl` del proyecto padre, compilados
con **nuestras** opciones (`-Os`, Cortex-M4, mismo `-ffunction-sections`).

```
  modulo                           FLASH       RAM
  hfdl_symsync                     3.812     4.800
  hfdl_payload_decode              3.060         5
  hfdl_soft_demod                  1.792         0
  hfdl_pdu                         1.354         0
  hfdl_framer                      1.134       504
  hfdl_viterbi                     1.028         0
  hfdl_rational_resampler          1.022         0
  hfdl_costas                        928         0
  hfdl_demod_chain                   880    10.752
  hfdl_equalizer                     868         0
  hfdl_deinterleaver                 828    21.330
  hfdl_preamble_sync                 820     4.573
  hfdl_iq_mixer                      760         0
  hfdl_crc                           701         0
  hfdl_preamble_seq                  559         0
  hfdl_agc                           540         0
  hfdl_matched_filter                414         0
  hfdl_data_segment                  364         0
  hfdl_hfnpdu                        228         0
  hfdl_descrambler                   207         0
  TOTAL                           21.299    41.964
```

Tres módulos más —`hfdl_aircraft`, `hfdl_systable` e `icao24_db`— salen a
cero porque están **detrás de un `#if`**: son extras opcionales, y la base de
datos de matrículas ni siquiera vive en la flash del programa, la lee del
almacén SPI. Bien pensado por ellos.

Falta `hfdl_scope.c` (21 kB de fuente), que es su pantalla y no compila aquí
porque habla con su cascada. Habrá que escribir la nuestra, como se hizo con
el panel de FT8. Calcúlale entre 1 y 2 kB más de flash.

### La conclusión, que no es la que parecía

**El problema es la flash, no la RAM.**

- **RAM: cabe.** Los 42 kB son casi todos tres buffers (deinterleaver 21 kB,
  cadena de demodulación 10,7 kB, sincronizador 4,8 kB) y HFDL está diseñado
  para pedirle prestada la memoria a la cascada, igual que FT8 — su unión ya
  tiene un miembro `hfdl_scratch`. Nuestra unión mide 54.432 bytes y FT8 usa
  52.484 de ellos. HFDL y FT8 no pueden estar a la vez, así que **entran en
  los mismos bytes**, con 12 kB de sobra.

- **Flash: faltan unos 18 kB** como mínimo (21,3 necesarios contra 4,2
  libres), y más bien 20 contando la pantalla.

## De dónde salió el sitio — y la corrección de este documento

**Este documento tenía un número falso y llevó a la conclusión equivocada.**

La tabla de abajo, que decía que `dcf77.o` costaba 12.461 bytes y lo ponía
como el tercer gasto de la radio, estaba mal. Lo midió `tools/gordos.py`
leyendo el mapa del enlazador, y el enlazador **funde las cadenas de texto de
todo el firmware en una sección y se la apunta al primer objeto que llegó**,
que resultó ser dcf77 — con sus 27 bytes propios. Los otros 8.379 son las
cadenas de `main.c`, de la interfaz y de todos los demás.

Lo que cuestan de verdad los decodificadores horarios:

```
             este documento decía      real
  dcf77.o              12.461         4.109
  hora_wwv.o            3.881         3.881
  hora_rbu.o            3.028         3.028
  TOTAL                 19.370        11.018
```

O sea que el "Candidato 1", el bloque limpio de 19,4 kB que yo recomendaba
quitar, eran **11**. Y no habría bastado. De haber seguido esta recomendación
se habría perdido DCF77, WWV y RBU **y aun así HFDL no habría entrado**.

`gordos.py` ya está arreglado (hay una fila `(cadenas fundidas)` y el total
cuadra con el binario), pero la lección es la de siempre aquí, y esta vez casi
cuesta tres funciones: **un instrumento que miente es peor que no tener
ninguno**, porque no te deja sin decisión, te da una decisión equivocada con
aire de estar medida.

### Lo que se hizo en su lugar: dos recortes que no cuestan nada

En vez de quitar modos, se buscó lo que sobraba:

```
  un printf() de depuración olvidado en spi_flash.c      5.684
    (arrastraba la familia printf entera de newlib, y
     no imprimía nada: con nosys.specs _write es un tapón)
  powf(10,x) -> expf(x*ln10) en nr_ss.c                  1.848
    (única llamada a powf del firmware; expf ya estaba
     enlazada. Error medido: 1,3e-6 en los 4.096 valores)
  TOTAL                                                  7.532
```

Más los 14.924 que ya habían dejado las dos tipografías del arranque y la
maquinaria de la lluvia.

### El resultado

```
  antes:  242.980 usados    19.164 libres
  ahora:  235.472 usados    26.672 libres
  HFDL:    21.299 + 1-2 kB de panel  ~=  23.300
```

**Entra, con unos 3,4 kB de margen, y sin quitar un solo modo.**

Los dos candidatos que proponía este documento —los decodificadores horarios
y las tipografías del arranque— se borran de aquí en vez de dejarlos "por si
acaso": uno estaba mal medido y el otro ya se hizo. Si algún día hace falta
más sitio, se vuelve a medir entonces, con la herramienta arreglada.

## Lo que NO propongo

- **Tocar `main.c` (42,8 kB).** Es grande porque tiene dentro toda la
  interfaz; no hay ahí un bloque que quitar, hay mil sitios de cincuenta
  bytes. Trabajo mucho, riesgo alto, premio disperso.
- **Quitar modos que funcionan** (SSTV 4,1 kB, CW 4,2 kB, RDS 3,7 kB). Cuesta
  poco y se pierde una función entera.
- **Bajar la calidad de las tipografías de la interfaz.** Las cuatro `ui_*`
  suman 34,7 kB pero las usan entre 8 y 11 ficheros cada una: se ven en todas
  las pantallas.

## Siguiente paso

Ya no hace falta decidir nada: hay sitio. El orden del port es:

1. **Que compilen dentro del firmware**, sin enchufarlos a ningún modo, y
   medir el coste real *in situ* — el de arriba son objetos sueltos, y el
   enlazador todavía tiene que decir la suya.
2. **La RAM, de la unión compartida.** Los 42 kB son casi todos tres buffers
   y HFDL ya está pensado para pedirlos prestados, igual que FT8. Los dos no
   pueden estar a la vez, así que caben en los mismos bytes.
3. **Un banco con una muestra conocida** antes de conectar nada al audio: que
   la cadena saque el PDU que tiene que sacar, en el PC, donde se puede mirar.
4. **La entrada de audio y el panel**, reutilizando `ui_digi` como hizo FT8.
