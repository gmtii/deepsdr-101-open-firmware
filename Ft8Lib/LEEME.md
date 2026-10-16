# `Ft8Lib/` no es código de este proyecto

Los trece ficheros de `ft8/` —`constants`, `crc`, `decode`, `ldpc`, `message`,
`text` y sus cabeceras— son **[ft8_lib](https://github.com/kgoba/ft8_lib)**, de
Kārlis Goba, bajo **licencia MIT**. Están copiados aquí tal cual, y el
`LICENSE` de al lado es el suyo.

La huella es inconfundible y se puede comprobar en un minuto: el prefijo
`ftx_`, `ftx_find_candidates()`, `ftx_message_decode_nonstd()`,
`ftx_callsign_hash_interface_t`.

**Por qué estaba este fichero sin escribir.** `Ft8Lib/` llegó heredado del
proyecto del que salió esta rama, no se trajo a mano, y los trece ficheros
vinieron **sin cabecera de licencia**. Nadie lo notó hasta el repaso del
13/10/2026, justo antes de publicar. La MIT pide una sola cosa a cambio de
dejarte hacer lo que quieras: que el aviso de copyright **viaje con el
código**. Durante meses no viajó.

## Las tablas de `constants.h` vienen de WSJT-X

El propio fichero lo dice en sus comentarios: *«From WSJT-X's
`ldpc_174_91_c_reordered_parity.f90`»* y *«Mn from WSJT-X's `bpdecode174.f90`»*.
**WSJT-X es GPL-3.0.** Son las constantes del protocolo FT8 —el mismo tipo de
cosa que el vector de sincronismo de 162 posiciones de WSPR, que en este
proyecto se trató como protocolo y no como código ajeno—, pero quien las
publica las publica bajo GPL, y conviene saberlo.

Para este repositorio no es un problema: está bajo **AGPL-3.0**, y la propia
GPLv3, en su artículo 13 («Use with the GNU Affero General Public License»),
da permiso expreso para combinar una obra suya con una obra bajo AGPLv3 y
distribuir el resultado. El inventario completo está en
[`PROCEDENCIA.md`](../PROCEDENCIA.md).

## Lo que SÍ es nuestro, y no está aquí

El pegamento que lo conecta con esta radio —`ft8_decoder.c`,
`ft8_decimator.c`, `ft8_waterfall_adapter.c`, `ft8_shared_ram.h`,
`ft8_modo.c`, `ft8_lista.c`— vive en `User/` y es trabajo de este proyecto.
Ahí está lo que hizo que FT8 entrara en 196 kB de RAM compartida con la
cascada, la ventana de 1.600 Hz y el panel.
