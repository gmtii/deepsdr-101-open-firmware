# Las capturas que le faltan al manual

*Repasado el 06/10/2026 contra `manual/build.js`, sección por sección, contando
las llamadas a `FOTO()`. No es de memoria: sale del fichero que hace el manual.*

**Cómo capturar.** Desde la radio, no con el móvil: mantén el dedo cuatro
segundos en la esquina de arriba a la izquierda. Pita al empezar, tarda unos
veinticinco segundos y vuelve a pitar. Salen al disco USB como `PANT*.BMP`.

Caben unas veinte en un disco de 16 MB, así que conviene vaciarlo entre tandas.

---

## Lo que hay ahora

*Puesto al día el 06/10/2026 por la tarde, con las siete que mandó el dueño.*

| Sección | Fotos |
|---|---|
| 6 · La pantalla principal | 1 |
| 7 · Los controles | 4 |
| 8 a 12 · Ajustes (las cinco páginas) | 5 |
| 13 · Las vistas de detalle | 2 |
| 14 · Información | 3 |
| 15 · Los 22 modos | 3 (CW, RTTY y FT8) |
| 16 · Las pantallas que no son modos | 2 (QTH y RDS) |
| 17 · Las 49 bandas | 0 (es una tabla, no las necesita) |
| 18 · Capturas de pantalla | 0 (ver abajo: no se puede) |
| 19 · El mapa del mundo | 2 (×1 y ampliado) |
| 20 · Los ficheros del disco | 0 (es una tabla) |

Veintitrés capturas. **Ya no queda ninguna sección ciega.**

---

## 0 · Puesto al día el 07/10/2026 por la mañana

De la carpeta `screens\` salieron tres que ya están metidas:

| Nueva | Qué enseña | Dónde va |
|---|---|---|
| `28_rtty4582.jpg` | RTTY con **Auto** puesto en 4.582 kHz: ha sacado solo 450 Hz y 100 Bd y el boletín marítimo sale entero | §15, junto al párrafo del botón Auto |
| `29_ident7136.jpg` | IDENT en 7.136 kHz: lo reconoce como **STANAG 4539 / MIL-188-110B** por la trama de 120,01 ms y dice «No se demodula todavía» | §15, detrás de la de IDENT en 4.326 |
| `30_hora_radio.jpg` | **Hora por radio** sobre DCF77 en 77,5 kHz, con la barra de nivel llena | §16, en «Hora por radio» |
| `31_hfdl_aviones.jpg` | **HFDL en 5.720 kHz** sobre Reykjavik: dieciséis aviones en 42 tramas, con modelo, vuelo y posición | §15, detrás de la tabla de columnas |

Y la `31`, mandada aparte el mismo día, tacha **HFDL** de la lista de modos
sin foto: la que había (`26_hfdl_frec.jpg`) es la rejilla de canales, no el
panel con aviones dentro, que es lo que de verdad cuesta de imaginar.

Con eso, la `28` tacha de la lista de abajo la verificación de **RTTY Auto en
antena**, que llevaba pendiente desde la V3.69.

### Las dos de NAVTEX, pendientes de repetir

En `screens\5\` hay dos capturas de **NAVTEX en 8.421,50** con la XSQ de
Guangzhou, a las 23:18 y a las 23:21. Son las únicas de NAVTEX que hay y la
sección no tiene ninguna, pero **no se han metido**: las dos enseñan el fallo
de la marquesina —el nombre de la emisora pisando el reloj y los voltios— que
se arregló en la V3.84. Un manual no debe enseñar una foto de un fallo.

**Repítelas con la V3.84 o posterior** y entran las dos. La XSQ emite a esa
hora todos los días.

### Y lo que la carpeta enseñó de paso

`PANT00341.BMP` (HFDL en 8.942, Shannon) y `3/PANT003.BMP` (HFDL en 8.843, San
Francisco Radio CEP) llevan el **mismo fallo de marquesina**, con el horario
de la estación metiéndose en los voltios. Son tres capturas independientes del
mismo fallo, hechas en días distintos: confirma que no era una casualidad de
una emisora con nombre largo.

---

## 1 · Las que quedan de las imprescindibles — 3

De las ocho de la lista anterior ya están cinco. Faltan éstas:

1. **Ajustes → Digital → Hora por radio**, ya enganchada: con la hora en
   grande y el pie diciendo `pulsa Aplicar para ponerla`. Si pillas la de RDS
   anclada al segundo, que enseña los segundos, mejor todavía.
2. **SSTV recibiendo** una foto a medias, con el botón de guardar puesto en
   `BMP 16 bits`.
3. **WEFAX recibiendo**, con la chapa diciendo `Recibiendo` y las líneas por
   minuto.

Las tres son de la sección 16, que ya tiene dos, así que no es urgente: la
sección ya no está ciega.

### Y una que salió de la tanda anterior

La captura de la vista del filtro enseñó un fallo que no se ve leyendo el
código: **la pastilla naranja decía `Escala mín.` ajustando el filtro**.
Arreglado en la V3.64 — ahora dice `Filtro mín.` / `Filtro máx.` y además el
valor en hercios. La foto que está en el manual es la de ANTES del arreglo; si
repites esa captura con la V3.64, se cambia.

---

## 2 · Los modos sin foto — hasta 10 capturas

La sección 15 explica los dieciséis modos digitales y sólo enseña dos. Cada
uno con su panel y con algo decodificado, que es lo que vale:

NAVTEX · APRS · WSPR · AIS · ALE · PSK31 · JTTY

(HFDL sale desde el 07/10 en `31_hfdl_aviones.jpg`; STANAG en `25_stanag.jpg`
y IDENT en `24_ident.jpg` y `29_ident7136.jpg`.)

(FT8 ya está; WEFAX y SSTV están en la lista de arriba.)

**La sección funciona sin ellas** —la tabla de botones de cada modo está
escrita— pero un panel que no se ha visto nunca cuesta de imaginar.

De éstas, la que más aporta es **IDENT sobre una señal conocida**, porque es el
único modo cuyo panel no se parece a ningún otro.

---

## 3 · Las que vendrían bien, si sobra sitio — 3

9. **La rejilla de modos de SSTV**, tocando la chapa del panel. Son catorce y
   el manual los nombra, pero no se ve de dónde salen.
10. **La lista de frecuencias de HFDL**, con las estaciones y la cuenta de
    tramas buenas de cada canal.
11. **La página 1 de Información con la cuenta atrás puesta**: `V3.63 (2)`.
    Explica el gesto de los cinco toques mejor que el párrafo.

---

## 4 · Las que NO se pueden capturar, y por qué

Esto no es pereza: **la radio no puede fotografiarse a sí misma en estas cuatro
pantallas**, porque la captura vive en el bucle principal y estas cuatro no
pasan por él.

- **La pantalla de arranque** (`splash_run()` se queda dentro hasta que acaba).
- **La carga de `BD.BIN` al encender** (igual: bloquea el bucle a propósito).
- **El asistente de calibración del táctil**: el bucle toma su propia rama
  (`touch_calib_poll()`) y no llega ni al reparto de toques ni a la captura.
- **El cartel de una captura en marcha** (`GUARDANDO CAPTURA`, la barra de
  progreso): una captura no puede fotografiarse a sí misma.

Para estas cuatro, **o foto con el móvil o nada**. Una foto de móvil sale
torcida y con los colores cambiados, así que yo las dejaría fuera: el texto las
explica y ninguna de las cuatro es una pantalla en la que haya que decidir nada.

---

## Resumen

```
  imprescindibles      3   (Hora por radio, SSTV y WEFAX)
  los modos sin foto  10   (la seccion funciona sin ellas)
  si sobra sitio       3
  imposibles           4   (o movil, o nada)
```

Ninguna sección está ya sin foto. Lo que queda **mejora** el manual; no lo
arregla.
