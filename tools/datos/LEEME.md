# Las listas con las que se hace BD.BIN

Aquí van los ficheros de entrada de `tools/emisoras_pack.py`. Son **listas
de terceros**, no las mantiene este proyecto, y **caducan cada seis meses**.

| fichero             | qué es                                                      | está aquí |
|---------------------|-------------------------------------------------------------|-----------|
| `hfcc.txt`          | **HFCC**, horario operativo de la temporada. 3.114 filas.    | sí        |
| `hfcc_sitios.txt`   | Su tabla de emplazamientos, con coordenadas. 701 entradas.   | sí        |
| `hfcc_emisoras.txt` | Su tabla de emisoras: código → nombre.                      | sí        |
| `eibi.txt`          | EiBi, versión por frecuencia. 8.226 filas.                   | sí        |
| `aoki.txt`          | Aoki (Nagoya DXers Circle), versión corta. 5.352 filas.      | sí        |
| `aoki_xy.txt`       | El mismo Aoki en versión ancha: sitio entero y coordenadas.  | sí        |

## Las tres no son la misma cosa

**HFCC manda, y es nuevo desde el 26/10/2026.** Las otras dos son
**recopilaciones**: alguien escucha, apunta y publica. La HFCC (*High
Frequency Coordination Conference*) es donde las emisoras **registran** sus
transmisiones antes de emitirlas, para repartirse las frecuencias. Trae
cosas que una recopilación no puede traer: el emplazamiento del transmisor
**con coordenadas**, la potencia, el azimut, y sobre todo **la temporada de
verdad** — cuando esto se escribió, EiBi y Aoki seguían publicando la A26
cinco días después de empezar el invierno, y HFCC ya tenía la B26 al día.

Lo que HFCC **no** trae: utilitarias. Las balizas horarias, el fax
meteorológico, el marítimo y la aviación no se coordinan ahí porque no son
radiodifusión. Ésas salen de EiBi, que las marca con un guion delante del
idioma (`-TS`, `-CW`, `-TY`, `-HF`, `-MX`), y **no caducan de temporada en
temporada**: son servicios fijos.

Así que el reparto es:

- **radiodifusión** → HFCC donde llegue, y donde no, EiBi y Aoki;
- **utilitarias** → EiBi, siempre, y no se tocan aunque la lista sea de la
  temporada anterior;
- **emplazamientos** → `sitios_xy.csv` manda, y detrás el Aoki ancho y la
  tabla de HFCC, que entre los dos sitúan casi dos mil transmisores.

Y **cómo se evitan los duplicados**, que es lo difícil: la misma emisión
está en las tres listas con tres nombres distintos — EiBi la llama por el
programa («CNR 1 Voice of China»), HFCC por la organización que la registró
(«China National Radio») y Aoki en mayúsculas y recortada —. Lo que sí
identifica una emisión son tres cosas que las tres escriben igual: la
**frecuencia**, el **país** y que las **horas se solapen**. El detalle está
en el comentario de `emisoras_pack.py`.

## De dónde se bajan

**HFCC** — <https://new.hfcc.org/data/>

De la tabla *Public Data Files*, el `.zip` de la temporada
(`b26allx2.zip`, `a27allx2.zip`…). Dentro vienen `Bnnall00.TXT` con el
horario y las tablas de referencia; aquí se guardan como `hfcc.txt`,
`hfcc_sitios.txt` (que es `site.txt`) y `hfcc_emisoras.txt` (que es
`broadcas.txt`). Es un registro público y lo publica la propia HFCC.

**EiBi** — <http://www.eibispace.de/>

Se baja el fichero de la temporada, en la *versión por frecuencia* (el que
empieza por `BC A26 - The comprehensive shortwave broadcasting schedule` y
tiene debajo `FREQUENCY VERSION`), y se guarda aquí como `eibi.txt`. En su
propia cabecera dice **«Free to copy + distribute»**, y por eso la copia
que generó el `BD.BIN` de hoy está en el repositorio: así cualquiera puede
rehacer el mismo fichero byte a byte sin depender de que la web siga
publicando esa temporada.

**Aoki** — lista del Nagoya DXers Circle, publicada en
<https://www4.plala.or.jp/nagoya/> (fichero de texto de la temporada).
Se guarda aquí como `aoki.txt`.

Está copiada aquí, igual que la de EiBi, para que el `BD.BIN` se pueda
rehacer tal cual. A diferencia de EiBi, **no trae ninguna nota de permiso
de copia**, así que es una copia de cortesía de una lista ajena: la fuente
es la de arriba, el trabajo es del Nagoya DXers Circle, y si alguna vez
piden que no se redistribuya, se borra el fichero y se baja — nada más
depende de que esté aquí (`emisoras_check.py` trae sus propias listas de
pega).

Las dos aportan cosas distintas y por eso se meten las dos: EiBi lleva
radiodifusión **y** utilitarias —balizas de tiempo, fax meteorológico,
radio marítima, aviación— y el área de destino; Aoki sólo radiodifusión,
pero trae el **sitio desde el que se emite**, que EiBi no da y que es de
donde sale `sitios_xy.csv`.

## Las temporadas, y por qué importa el `horas_utc`

Las dos listas van por temporada de radiodifusión, como los horarios de
tren:

| temporada | vale desde        | hasta             | `horas_utc` en España |
|-----------|-------------------|-------------------|-----------------------|
| A26       | 29 marzo 2026     | 25 octubre 2026   | **2**                 |
| B26       | 25 octubre 2026   | marzo 2027        | **1**                 |

`horas_utc` es **cuánto hay que restarle al reloj de la radio para tener
UTC**. Las dos listas dan las horas en UTC y el reloj de la radio marca hora
local; si se pone mal, la radio enseña la emisora de hace una o dos horas, que
es un fallo especialmente feo porque el resultado *parece* bueno.

**Desde la V4.27 este byte es el último recurso, no la norma.** El huso vive en
`huso_min` dentro de `CONFIG.CSV`, se pone desde la radio —Ajustes → Equipo,
con una lista de cincuenta ciudades ordenadas por tu QTH y un botón para
medirlo contra una emisora de UTC— y **gana siempre**. `horas_utc` sólo manda
cuando `CONFIG.CSV` no trae `huso_min`, que es el caso de una radio recién
formateada. La tabla de abajo dice España porque es desde donde se armó el
`BD.BIN` que se distribuye; si rehaces el tuyo en otro sitio, el número es el
de tu huso, no el de aquí.

Los periodos de las temporadas coinciden exactamente con el horario de
verano europeo, así que mientras vale A26 España está en UTC+2 **siempre**,
y mientras vale B26 en UTC+1 siempre. No hay que pensarlo cada vez.

La fecha de caducidad ya **no** se escribe a mano: `emisoras_pack.py` la lee
de la línea `Valid March 29, 2026 - October 25, 2026` de la cabecera de
EiBi. Si la lista que se use no la trae, lo dice y se pone con `--hasta
20270328`.

## El flujo entero

```sh
# 1. las emisoras (añade --qth si quieres que salgan ordenadas por cercanía)
python3 ../emisoras_pack.py eibi.txt aoki.txt EMISORAS.BIN 1 \
        --hfcc hfcc.txt --aoki-xy aoki_xy.txt

# 2. los modelos de avión, del CSV de OpenSky
python3 ../icao24.py aircraft-database-complete-2025-08.csv ICAO24.BIN 827392

# 3. y se juntan en el único fichero que se copia al disco USB de la radio
python3 ../datos_pack.py BD.BIN ICAO24.BIN EMISORAS.BIN
```

El CSV de OpenSky se baja de
<https://opensky-network.org/datasets/metadata/>; la cabecera de
`icao24.py` cuenta qué dos formatos valen y por qué.

**`BD.BIN` no puede pasar de 1 MB**, ponga el chip de flash que se ponga —
la zona de datos es siempre el último mega. `datos_pack.py` se niega y dice
qué tope hay que pedirle a `icao24.py`.

## Si no están las listas

No pasa nada: sólo hacen falta para **rehacer** el `BD.BIN`. El banco
`tools/emisoras_check.py` se escribe sus propias listas de pega y pasa
igual; si las de verdad están, hace además una comprobación más
(que todos los códigos ITU que salen en ellas estén en `paises_xy.csv`).

Eso vale también para cuando caduquen: se bajan las dos de la temporada
nueva, se sustituyen aquí y se repite el flujo. No hay que tocar código.
