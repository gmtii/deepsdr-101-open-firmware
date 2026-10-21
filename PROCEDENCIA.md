# Procedencia del código

*Repasado el 13/10/2026, fichero por fichero, antes de publicar el repositorio.
Lo de aquí sale de leer las cabeceras del propio árbol y de comprobar las
licencias en origen, no de memoria.*

Este documento existe por una razón concreta: **el repositorio está bajo la
AGPL-3.0** —el fichero `LICENSE` de la raíz, desde que se abrió— y eso obliga a
saber qué hay dentro que no sea propio y si encaja. Esto es el inventario, y la
respuesta corta es que sí encaja; el apartado 4 dice por qué y qué queda.

*Corrección del 13/10/2026, por la tarde:* la primera versión de este fichero
decía que la licencia estaba por elegir y que sin `LICENSE` esto era «todos los
derechos reservados». **Era falso**, y lo era por no haber mirado el
repositorio antes de escribirlo: lleva dieciocho commits, cinco estrellas y su
`LICENSE` AGPL-3.0 desde el principio. El inventario de abajo vale igual —
mirar qué hay dentro era lo correcto—, pero la conclusión estaba escrita contra
un hueco que no existía.

---

## 1. Lo que es código de terceros, copiado tal cual

### `Ft8Lib/ft8/` — el decodificador de FT8

Trece ficheros (`constants`, `crc`, `decode`, `ldpc`, `message`, `text`, con sus
cabeceras). La huella es inconfundible: el prefijo `ftx_`, `ftx_find_candidates()`,
`ftx_message_decode_nonstd()`, `ftx_callsign_hash_interface_t`. Es
**[ft8_lib](https://github.com/kgoba/ft8_lib)**, de Kārlis Goba, **licencia MIT**
(`Copyright (c) 2018 Kārlis Goba`, comprobado en el `LICENSE` de origen).

**Dos cosas que hay que arreglar antes de publicar:**

1. ~~**Los ficheros no llevan cabecera de licencia.**~~ **Hecho el 13/10/2026:**
   `Ft8Lib/LICENSE` lleva la MIT con el copyright de Kārlis Goba, y
   [`Ft8Lib/LEEME.md`](Ft8Lib/LEEME.md) dice qué es cada cosa, de dónde sale y
   qué parte de FT8 sí es de este proyecto. Los trece ficheros siguen sin
   cabecera individual —no se tocan, para que se vean idénticos a los de
   origen— pero el aviso ya viaja con el código, que es lo que pide la MIT.
2. **`constants.h` trae tablas sacadas de WSJT-X.** Lo dice el propio fichero:
   *«From WSJT-X's `ldpc_174_91_c_reordered_parity.f90`»* y *«Mn from WSJT-X's
   `bpdecode174.f90`»*. **WSJT-X es GPL-3.0.** Esas tablas son las constantes del
   protocolo FT8 —el mismo tipo de cosa que el vector de sincronismo de 162
   posiciones de WSPR, que en este proyecto se trató como protocolo y no como
   código ajeno—, pero quien las publica las publica bajo GPL, y eso es un
   argumento que alguien puede querer discutir.

Llegó al proyecto por herencia, no por decisión: `Ft8Lib/` y cinco módulos de
pegamento vinieron del proyecto del que salió esta rama.

### `Firmware/` y `CMSIS/` — la biblioteca del fabricante

La biblioteca de periféricos estándar de GigaDevice para el GD32F4xx y el CMSIS
de ARM, con sus propias licencias. Se usan tal cual y están fuera de los
barridos de estilo del proyecto a propósito.

---

### `User/rm68120_exmc.c` — la secuencia del panel, de Espressif (Apache-2.0)

*Añadido el 16/10/2026, al repasar la carpeta para el push: el aviso estaba
en el código desde el principio y este inventario no lo recogía. Un
inventario que se presenta como completo y no lo es engaña más que no
tenerlo.*

La tabla de registros que arranca el panel RM68120 de 480x800 (líneas 427 en
adelante) viene **adaptada del driver oficial de Espressif Systems** para el
ESP32-S2-HMI-DevKit, que monta ese mismo panel:

- `espressif/esp-dev-kits`, `…/controller_driver/rm68120/rm68120.c`
- `Copyright 2020 Espressif Systems (Shanghai) Co. Ltd.`
- **Apache License 2.0**

El propio fichero lo dice y no se esconde: *«se ha traducido mecánicamente
`LCD_WRITE_REG(cmd,data)`/`LCD_WRITE_CMD(cmd)` a las funciones equivalentes
de este proyecto, sin alterar ningún valor de registro»*. O sea: **obra
derivada**, no reimplementación. Y es lo correcto — una secuencia de
inicialización de un panel son los valores que el fabricante del cristal
exige; cambiarlos no es originalidad, es romperlo.

La Apache-2.0 pide que el aviso viaje con el código. Viaja, en la cabecera de
la propia tabla, con enlace y copyright. Y es compatible con la AGPL-3.0 en
esta dirección (Apache-2.0 → GPLv3/AGPLv3 sí; al revés no).

### `User/hfdl_viterbi.h/.c` — port de libfec (LGPL, Phil Karn KA9Q)

*Añadido el 16/10/2026, por lo mismo.*

Esto NO es una reimplementación como el resto de la cadena de HFDL: la
cabecera del fichero lo dice con sus palabras — *«a from-scratch,
**line-by-line PORT** of libfec's `viterbi27_port.c`»* (Phil Karn, KA9Q,
**LGPL**, Copyright Feb 2004, *«K=7 r=1/2 Viterbi decoder in portable C»*).
Un port línea a línea es derivado, y conviene que esté dicho aquí y no sólo
dentro del fichero.

El aviso está en la cabecera. La LGPL es compatible con la AGPL-3.0 (la
LGPLv3 permite expresamente combinar con GPLv3/AGPLv3, artículo 2b y 4).

### `User/hfdl_descrambler.c` — tres constantes de liquid-dsp (MIT)

*Añadido el 16/10/2026, por lo mismo.*

Los tres números del descramblador (`GENPOLY 0x4001`, `INIT 0x4d4b`,
`MASK 0x7FFF`) se confirmaron contra `liquid-dsp`
(`jgaeddert/liquid-dsp`, **MIT**), que es lo que usa `dumphfdl`. Son
constantes de un polinomio, no código, pero el fichero cita la fuente y el
inventario debe recogerlo.

MIT es permisiva y encaja sin fricción.

### `User/rtc_hw.c/.h` — lo que había antes, y por qué ya no está

*Añadido el 16/10/2026. **Esto es lo más importante de esta tanda**, porque
`rtc_hw.h` lleva desde el 13/10 mandando al lector a este documento —«venía
portado tal cual de otro proyecto, con su licencia, y eso chocaba con la de
este repositorio; ver PROCEDENCIA.md»— y este documento no contaba la
historia. Un fichero que apunta a un sitio donde no hay nada deja la peor
lectura posible: que se borró un aviso de licencia para que encajara.*

Lo que pasó, en orden:

1. El `rtc_hw.c` original venía **portado tal cual de otro proyecto**, con su
   licencia **CC BY-NC** (Creative Commons Atribución-NoComercial).
2. Se pidió quitar la atribución para que no chocara con la AGPL. **Eso no se
   hizo**, y no se podía hacer: quitar el aviso no cambia de quién es el
   código, sólo oculta de quién es.
3. El problema de fondo tampoco se arreglaba con atribución. **CC BY-NC no es
   una licencia de software libre**: la cláusula NonCommercial prohíbe el uso
   comercial, y eso es incompatible con la AGPL-3.0, que no admite
   restricciones añadidas (artículo 7). Con ese fichero dentro, este
   repositorio no se podía publicar bajo AGPL ni con aviso ni sin él.
4. El **13/10/2026 se reescribió entero**, partiendo de otra base: el driver
   de GigaDevice que ya venía en `Firmware/Source/gd32f4xx_rtc.c` (y que
   hasta entonces se enlazaba sin que lo llamara nadie) y la hoja de datos
   del GD32F4xx. La aritmética del calendario se sacó a `rtc_cal.c`, aparte,
   para que el simulador la compile y un banco la mire.

El `rtc_hw.c` de hoy **no deriva del original**: habla con el periférico a
través del driver del fabricante, que ya está inventariado arriba. Por eso no
lleva atribución CC BY-NC — no porque se quitara, sino porque no queda nada
que atribuir.

Lo que no se puede hacer nunca, y queda escrito por si alguien tiene la
tentación: borrar un aviso de licencia de un fichero que se conserva. Si la
licencia no encaja, se reescribe el fichero o se queda fuera. Las dos cosas
son trabajo; quitar la línea no lo es, y además no funciona.

---

### `capturas/` — fotos de la pantalla, y qué sale en ellas

Dieciséis capturas de la radio funcionando, hechas con la propia función de
captura. Son nuestras. Lo que se ve dentro son indicativos de estaciones
**recibidas en antena** (FT8, HFDL, RTTY): datos que esas estaciones emiten
en abierto, del mismo tipo que publica cualquier captura de WSJT-X. No
aparece el indicativo ni el localizador exacto de nadie de este proyecto; el
localizador que se ve es el de serie, IN80dk, Madrid capital.

---

## 2. Lo que se escribió mirando una referencia ajena

Esto es la parte larga, y está documentada **dentro de cada fichero**, no aquí.
El patrón del proyecto siempre fue el mismo: leer la referencia para entender el
algoritmo, escribir el código propio, y dejar escrito en la cabecera qué se miró
y qué no se copió.

### La cadena de HFDL — 32 ficheros, referencia `dumphfdl`

[dumphfdl](https://github.com/szpajder/dumphfdl), de Tomasz Lemiech, es
**GPL-3.0-or-later**. Los ficheros `User/hfdl_*.c/.h` citan su `src/hfdl.c`,
`src/crc.c` y `src/pdu.c` para el filtro adaptado, el desentrelazado, el
descrambler, el Viterbi, el preámbulo y el formato de las PDU.

Lo que dicen las cabeceras, y hay que leerlo entero antes de opinar:

- Lo que se confirmó contra dumphfdl son **parámetros del protocolo y
  estructura del algoritmo** (el retardo de grupo de 3 símbolos, las 8
  combinaciones de velocidad y ranura, el orden de reinicio del
  desentrelazador), no líneas de código.
- La única coincidencia literal es la **tabla CRC-16/X-25** de
  `hfdl_crc.c`, y el propio fichero dice por qué eso no significa nada: es la
  tabla reflejada estándar del polinomio 0x1021, *«y la tabla de referencia
  CRC-16/X-25 publicada en general»*. Sale igual en cualquier proyecto que
  use ese CRC.
- Varias piezas se resolvieron **distinto** a propósito, con el porqué medido:
  el preámbulo, el ecualizador y el AGC están escritos con constantes propias.

Aun así, treinta y dos ficheros que citan un proyecto GPL en sus cabeceras es
lo primero que va a mirar cualquiera que llegue al repositorio.

### `User/ecualiz.h` — referencia `sigutils` (LGPLv3)

El caso mejor documentado del árbol. La cabecera dice, literalmente, que se leyó
`sigutils/equalizer.h` de Gonzalo J. Carracedo (BatchDrake) para entender el
montaje, **que el permiso que da su LGPLv3 no se ha usado** y que no hay ni una
línea copiada: aquí es de longitud fija, sin reservar memoria, con los dos
algoritmos en vez de uno y en coma flotante de 32 bits.

### `User/wspr_msg.c`, `User/wspr_fano.c` — referencia GPL, código propio

Escrito limpio **a propósito**, anotado en el diario del 29/09:
*«Las constantes son el protocolo, comprobadas contra una implementación de
referencia; el código es nuestro, porque la referencia es GPL y este proyecto
no lo es.»* El decodificador secuencial es de pila, no de Fano, y se eligió así
para poder demostrarlo en banco.

(Aquella frase —«este proyecto no lo es»— se escribió sin mirar el `LICENSE`
del repositorio, que es AGPL-3.0, o sea copyleft fuerte. No cambia nada de lo
hecho: el código es propio igualmente, y haberlo escrito limpio sólo deja el
proyecto con **menos** ataduras, no con más.)

### `User/psk31.c`, `User/jtty_*`, `User/ident.c`

Citan **fldigi** y **WSJT-X** como referencia de medida —contra qué se comparó
el umbral, de dónde sale el nombre de una firma—, no como origen de código.

---

### Las listas de emisoras — `tools/datos/`

`eibi.txt` (EiBi, 8.186 filas) y `aoki.txt` (Nagoya DXers Circle, 5.325 filas)
son **listas de terceros copiadas tal cual** para que el `BD.BIN` se pueda
rehacer byte a byte sin depender de que la web siga publicando esa temporada.
No es un descuido y está documentado donde toca, en
[`tools/datos/LEEME.md`](tools/datos/LEEME.md), con la fuente de cada una:
EiBi trae en su propia cabecera un **«Free to copy + distribute»**; la de Aoki
**no trae ninguna nota de permiso**, así que es una copia de cortesía, y si
alguna vez piden que no se redistribuya se borra y se baja — nada más depende
de que esté ahí.

### Los datos del simulador — `deepsdr-ui/datos/`

- **`ne_110m_coastline.json`** es la línea de costa de **Natural Earth**, de
  donde salen los 7,1 kB de costa que dibuja la radio. Natural Earth se
  publica **en dominio público**: su propia página dice que nadie necesita
  permiso para usarla, de ninguna manera.
- **`jtty_vectores_fortran.txt`** son vectores de prueba —carga, tonos y
  bits— sacados de una implementación de referencia en Fortran para
  comprobar el FEC de JTTY contra algo que no es nuestro propio código. Son
  salidas medidas, no código.
- **`ICAO24.BIN`** lo genera `tools/icao24.py` desde el CSV de **OpenSky
  Network**. El fichero que se publica es el resultado, no el CSV.

---

## 3. Lo que parece procedencia y no lo es

`User/cat.c/.h`, `User/usb_cdc*.c/.h` citan WSJT-X, JTDX y fldigi, pero por
**interoperar**: la radio saca un puerto serie que habla el CAT de Kenwood para
que esos programas la manejen sin driver. Nada de ahí viene de su código.

---

## 4. Cómo encaja todo con la AGPL-3.0, y lo único que queda

La AGPL-3.0 es copyleft fuerte, y resulta ser **la licencia que menos problemas
da con este inventario**, porque es la más estricta de todas las de arriba:

| Lo que hay | Su licencia | Cómo encaja |
|---|---|---|
| `Ft8Lib/` | MIT | Permisiva: se puede meter en una obra AGPL sin pedir nada. Lo único que pide es que **el aviso viaje con el código**, y eso falta. |
| Tablas de `constants.h`, de WSJT-X | GPL-3.0 | La propia GPLv3, en su **artículo 13 («Use with the GNU Affero General Public License»)**, da permiso expreso para combinar una obra suya con una obra bajo AGPLv3 y distribuir el resultado. |
| Los 32 ficheros de HFDL, escritos contra `dumphfdl` | GPL-3.0-**or-later** | Igual que arriba, y además el «or later» deja la puerta abierta de par en par. Son reimplementación documentada, así que la pregunta ni siquiera debería llegar a plantearse; si alguien la plantea, la respuesta ya está escrita y además la licencia la cubre. |
| `sigutils`, de `ecualiz.h` | LGPL-3.0 | No se usó ni una línea, y está escrito en la cabecera. No hay nada que encajar. |
| `Firmware/`, `CMSIS/` | GigaDevice / ARM | Se usan tal cual, con sus avisos. |
| `rm68120_exmc.c`, la tabla del panel | Apache-2.0 (Espressif) | Permisiva y **compatible en esta dirección**: Apache-2.0 se puede incorporar a una obra GPLv3/AGPLv3; al revés no. Pide que el aviso viaje con el código, y viaja. |
| `hfdl_viterbi.h/.c`, port de libfec | LGPL (Phil Karn, KA9Q) | La LGPLv3 permite expresamente combinar con GPLv3/AGPLv3. Es un port línea a línea, o sea derivado, y está dicho en la cabecera. |
| Las tres constantes de `hfdl_descrambler.c` | MIT (liquid-dsp) | Permisiva. Son constantes de un polinomio, con la fuente citada. |
| `rtc_hw.c` | — (propio) | Reescrito entero el 13/10/2026 sobre el driver de GigaDevice. El que había antes era CC BY-NC, **incompatible con la AGPL** (NonCommercial = no es software libre), y por eso se reescribió en vez de atribuirlo. Ver el apartado 1. |

**Lo único que queda de verdad, y es de trámite:**

- ~~**Inventariar Espressif, libfec y liquid-dsp.**~~ **Hecho el 16/10/2026**,
  al repasar la carpeta para el push. Los tres avisos llevaban desde siempre
  en la cabecera de su fichero; lo que faltaba era que este documento los
  recogiera, porque se presenta como inventario completo.
- ~~**Contar aquí la historia de `rtc_hw.c`.**~~ **Hecho el 16/10/2026.** El
  fichero apuntaba a este documento desde el 13/10 y aquí no había nada.

- ~~**Meter el `LICENSE` de `ft8_lib` dentro de `Ft8Lib/`.**~~ **Hecho el
  13/10/2026.** Era el único punto de este documento que describía un
  incumplimiento real, y ya no lo es.
- **Decidir si la AGPL es lo que se quiere de verdad**, que es distinto de si
  vale. Su artículo 13 —el que la distingue de la GPL— obliga a ofrecer el
  fuente a quien use la obra **a través de una red**. Una radio no es un
  servidor: lo que esta saca por el USB-C es un puerto serie y un disco, no un
  servicio de red, así que en la práctica se comporta como la GPL-3.0. Pero si
  algún día alguien monta un receptor web encima de esto, el artículo 13 sí
  entra, y conviene saberlo antes y no después. **Cambiar de licencia ahora
  sería más difícil que dejarla**: hay cinco estrellas y un fork, o sea gente
  que ya recibió el código bajo AGPL.

*Esto no es asesoramiento legal, que no soy abogado: es lo que dicen los
ficheros de licencia leídos, con el artículo 13 de la GPLv3 comprobado contra
el texto de SPDX.*
