# Las listas con las que se hace BD.BIN

Aquí van los ficheros de entrada de `tools/emisoras_pack.py`. Son **listas
de terceros**, no las mantiene este proyecto, y **caducan cada seis meses**.

| fichero     | qué es                                              | está aquí |
|-------------|-----------------------------------------------------|-----------|
| `eibi.txt`  | EiBi, versión por frecuencia. 8.186 filas.          | sí        |
| `aoki.txt`  | Lista de Aoki (Nagoya DXers Circle). 5.325 filas.   | sí        |

## De dónde se bajan

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
UTC**. El reloj marca hora local española y las dos listas dan las horas en
UTC; si se pone mal, la radio enseña la emisora de hace una o dos horas, que
es un fallo especialmente feo porque el resultado *parece* bueno.

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
python3 ../emisoras_pack.py eibi.txt aoki.txt EMISORAS.BIN 2 --qth JN97

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
