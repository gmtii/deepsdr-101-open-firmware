#ifndef HFDL_RAM_H
#define HFDL_RAM_H

#include <stdint.h>

/*
 * LA RAM PRESTADA DE HFDL. 25/09/2026.
 *
 * QUE SUSTITUYE. En el proyecto padre esta funcion vive dentro de
 * hfdl_scope.c, que es su pantalla de HFDL: el modulo que dibuja tambien
 * era el dueno del buffer prestado. Nosotros no portamos su pantalla -habla
 * con su cascada, que no es la nuestra, igual que paso con FT8- asi que el
 * prestamo se saca a su propio fichero y la pantalla se escribira aparte.
 *
 * Es ademas mas honesto: que la memoria de un decodificador dependa de si su
 * pantalla esta compilada es un acoplamiento que no hace falta copiar.
 *
 * DE DONDE SALE. De la misma union que usa FT8 (ver ft8_shared_ram.h): el
 * buffer de la cascada, prestado entero mientras dura el modo. HFDL y FT8 no
 * pueden estar a la vez -ninguno de los dos puede estar con la cascada- asi
 * que los tres se reparten los mismos bytes en vez de sumarse.
 *
 * SOLO HAY UN CAMBIO en el codigo del proyecto padre: el #include de
 * hfdl_payload_decode.c apunta aqui en vez de a hfdl_scope.h. La funcion se
 * llama igual a proposito, para que el resto de sus ficheros y de sus
 * comentarios sigan valiendo tal cual.
 */

/*
 * Devuelve la region prestada, o NULL si el modo HFDL no esta activo.
 * `capacity_out` (puede ser NULL) recibe cuantos bytes hay.
 *
 * QUIEN LLAMA TIENE QUE MIRAR EL NULL. Devolver NULL fuera del modo no es
 * una comodidad: es lo que impide que HFDL escriba sobre la cascada cuando
 * la cascada la esta usando otro. El codigo del padre ya lo comprueba en los
 * tres sitios donde llama.
 */
uint8_t *hfdl_scope_get_hfdl_ram(uint32_t *capacity_out);

/*
 * LA CABECERA RESERVADA.
 *
 * hfdl_payload_decode reparte la region prestada a su gusto empezando por el
 * byte 0, asi que los estados del modo -la cadena de demodulacion, el
 * sincronismo de preambulo y el segmento- no pueden vivir tambien ahi: se
 * pisarian en cuanto empezara un segmento.
 *
 * Se reservan los primeros HFDL_RAM_CABECERA bytes para ellos y a
 * hfdl_scope_get_hfdl_ram() se le dice que la region empieza DESPUES. Asi el
 * codigo del padre sigue creyendo que le dan una region entera para el solo,
 * que es lo que cree en su proyecto, y aqui no hace falta tocarle nada.
 *
 * Estuvo en 8.192 "por si acaso" hasta que se midio lo que el decodificador
 * necesita de verdad y resulto que cada byte de aqui se lo quita a el:
 *
 *     giro 0  300 bps  sencilla ...  7.168      giro 4  300  doble ... 15.808
 *     giro 1  600 bps  sencilla ... 11.488      giro 5  600  doble ... 25.888
 *     giro 2 1200 bps  sencilla ... 22.288      giro 6 1200  doble ... 51.088
 *     giro 3 1800 bps  sencilla ... 33.088      giro 7 1800  doble ... 76.288
 *
 * Con 9.216 de cabecera quedan 45.216. Los seis giros que cabian siguen
 * cabiendo -el mayor de ellos, el 3, pide 33.088- y los giros 6 y 7 no
 * caben, igual que no cabian antes: el 7 pide 76 kB y no entrara nunca en
 * una union de 54.
 *
 * El margen que queda son 296 bytes, y no es "por si acaso": lo vigila el
 * _Static_assert de hfdl_modo.c, que ya cazo una vez el ecualizador cuando
 * se paso por 208.
 */
/*
 * La cabecera guarda, por este orden:
 *   - las tablas de referencia del preambulo (4.572 bytes, ver
 *     build_refs_once() en hfdl_preamble_sync.c), que antes eran estaticas
 *     y se comian el 5 por 1 de la SRAM libre de la radio;
 *   - los estados del modo (4.312 medidos, ver hfdl_modo.c).
 * 9.216 para 8.884, y el margen lo vigila un _Static_assert.
 */
#define HFDL_RAM_PREAMBULO 4608U
#define HFDL_RAM_CABECERA  9216U

/* La cabecera reservada, o NULL si el modo no esta activo. Es de hfdl_modo.c
 * y de nadie mas. */
uint8_t *hfdl_ram_cabecera(void);

/*
 * LA REGION ENTERA, CABECERA INCLUIDA. 27/09/2026.
 *
 * Para WSPR, y solo para WSPR. Las dos funciones de arriba parten el
 * prestamo en dos porque HFDL necesita las dos piezas a la vez: la cabecera
 * para sus estados y el resto para el decodificador del padre, que reparte
 * desde el byte 0 a su gusto. WSPR no tiene esa division - lo que guarda es
 * UN espectrograma de 46 kB, de una pieza- y los tres modos son exclusivos,
 * asi que mientras WSPR esta puesto no hay ni tablas de preambulo ni
 * estados de HFDL que respetar: en la cabecera no vive nadie.
 *
 * Y no es un lujo: el espectrograma pide 47.056 bytes y despues de la
 * cabecera solo quedan 45.216. Sin esto, wspr_modo_start() devolveria 0
 * siempre y el modo se pondria sin arrancar - una pantalla que no falla,
 * simplemente no hace nada. Ver el _Static_assert de wspr_modo.c, que
 * convierte ese silencio en un error de compilacion.
 */
uint8_t *hfdl_ram_todo(uint32_t *capacity_out);

/* Coge y suelta el prestamo. Mismo par que waterfall_presta() usa FT8. */
void hfdl_ram_coge(void);
void hfdl_ram_suelta(void);

#endif /* HFDL_RAM_H */
