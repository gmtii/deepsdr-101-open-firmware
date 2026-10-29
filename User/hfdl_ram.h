#ifndef HFDL_RAM_H
#define HFDL_RAM_H

#include <stdint.h>

/*
 * LA RAM PRESTADA DE HFDL. 25/09/2026.
 *
 * QUE SUSTITUYE. En el proyecto padre esta funcion vive dentro de
 * hfdl_scope.c, que es su pantalla de HFDL: el modulo que dibuja tambien
 * era el usuario del buffer prestado. Nosotros no portamos su pantalla -habla
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
 *
 * Y POR ESO EL NOMBRE SIGUE DICIENDO "scope" - 30/09/2026. En nuestro arbol
 * ya no hay ningun hfdl_scope.c: teniamos uno a medio integrar, de la fase 1
 * del plan por etapas (un detector de rafagas con FFT propia, escrito cuando
 * todavia no habia demodulador), y se ha borrado porque el demodulador
 * terminado contesta mejor lo que aquel venia a contestar -hfdl_modo_senal()
 * y hfdl_modo_embudo(), que miran el preambulo de HFDL y no "energia en banda
 * ancha", que salta igual con un chasquido de estatica-.
 *
 * El nombre NO se renombra a hfdl_ram_get(): se mantiene el del padre para
 * que sus ficheros y sus comentarios sigan valiendo sin tocar. Si buscas
 * hfdl_scope.c en este arbol, no esta y no falta; la funcion esta aqui.
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
 * *** Y LOS NUMEROS DE ABAJO SON LOS DE ENTONCES - 28/10/2026. Hoy la
 * cabecera mide HFDL_RAM_CABECERA (15.168: los 9.216 de siempre mas la
 * tabla de aviones y los renglones) y la region de pago queda en 54.684,
 * que sigue tragando el giro 6. Y el margen de los estados ya no son 396
 * sino 240: sizeof(hfdl_estado_t) son 4.368 medidos hoy, no los 4.212 que
 * dice el parrafo de abajo. Lo vigila el _Static_assert de hfdl_modo.c,
 * que desde hoy se mide contra HFDL_OFF_AVIONES y no contra el final. ***
 *
 * Con 9.216 de cabecera quedan 45.216. Los seis giros que cabian siguen
 * cabiendo -el mayor de ellos, el 3, pide 33.088- y los giros 6 y 7 no
 * caben, igual que no cabian antes: el 7 pide 76 kB y no entrara nunca en
 * una union de 54.
 *
 * El margen que queda son 396 bytes, y no es "por si acaso": lo vigila el
 * _Static_assert de hfdl_modo.c, que ya cazo una vez el ecualizador cuando
 * se paso por 208.
 *
 * (Decia 296 hasta el 28/09/2026. El numero importa porque este mismo
 * parrafo lo usa como argumento: quien dimensione algo nuevo contra 296 se
 * deja 100 bytes sin usar, y quien decida que 296 es poco y suba
 * HFDL_RAM_CABECERA se los quita al decodificador para nada. Comprobado con
 * una sonda de compilacion: sizeof(hfdl_estado_t) == 4212, y
 * 9216 - 4608 - 4212 == 396.)
 */
/*
 * La cabecera guarda, por este orden:
 *   - las tablas de referencia del preambulo (4.572 bytes, ver
 *     build_refs_once() en hfdl_preamble_sync.c), que antes eran estaticas
 *     y se comian el 5 por 1 de la SRAM libre de la radio;
 *   - los estados del modo (4.212 medidos, ver hfdl_modo.c).
 * 9.216 para 8.784, y el margen lo vigila un _Static_assert.
 */
#define HFDL_RAM_PREAMBULO 4608U

/*
 * ======================================================================
 * Y LA TABLA DE AVIONES Y SUS RENGLONES, TAMBIEN AQUI - 28/10/2026
 * ======================================================================
 *
 * *** Pedido por el usuario: "amplia a 48 aceptando que se borre al
 * salir". ***
 *
 * LA PREGUNTA ERA: ¿por que la lista de aviones de HFDL se quedo en 14
 * renglones y 20 aviones cuando la de FT8 paso a 48?
 *
 * Y no hay respuesta buena. Cuando FT8 paso de 16 a 48 (ver
 * FT8_MODO_LINEAS) fueron dos razones, y UNA VALE IGUAL AQUI: esa lista no
 * la lee solo el panel, LA LEE EL MAPA. Los puntos y las etiquetas de HFDL
 * salen de recorrer la tabla de aviones, asi que el mapa no podia enseñar
 * mas de veinte por llena que estuviera la banda. La otra razon -que el
 * panel se sube con el dedo- HFDL no la tiene, y sigue sin tenerla.
 *
 * LO QUE LO FRENABA ERA UN COMENTARIO QUE SE QUEDO VIEJO. Las dos tablas
 * eran estaticas con este argumento escrito en hfdl_modo.c: "tienen que
 * sobrevivir a hfdl_modo_stop(): si vivieran en la RAM prestada, salir del
 * modo para mirar otra cosa borraria lo que acababa de salir. Lo mismo
 * hace FT8 con las suyas".
 *
 * La ultima frase ERA VERDAD CUANDO SE ESCRIBIO y hoy no lo es: las lineas
 * de FT8 viven dentro de la union desde hace tiempo (ver `lineas` en
 * ft8_shared_ram.h) y se borran al salir del modo, igual que todo lo
 * demas. O sea que HFDL estaba pagando 2.048 bytes de SRAM permanente por
 * una garantia que el modo con el que se comparaba ya no da.
 *
 * LO QUE CUESTA, que es lo bonito: NADA. La union la dimensiona FT8
 * (69.852 bytes) y el miembro de HFDL anda por 60.304, asi que estos
 * 5.952 caben en el hueco que ya habia. Y se devuelven 2.048 bytes de .bss
 * a la radio.
 *
 *     antes   cabecera  9.216 + giro6 51.088 = 60.304   +2.048 de .bss
 *     ahora   cabecera 15.168 + giro6 51.088 = 66.256   +0
 *
 * El giro 6 sigue cabiendo -la region de pago queda en 54.684 y el pide
 * 51.088- y el 7 sigue sin caber, igual que antes.
 *
 * EL PRECIO, que el usuario acepto: la lista se borra al salir del modo.
 * Es exactamente lo que ya le pasa a la de FT8.
 *
 * *** Y NO HAY VUELTA ATRAS POR .bss - 28/10/2026, por la tarde. ***
 *
 * Se intento devolverlas a memoria estatica para que la lista sobreviviera
 * al salir del modo, y el enlazador lo rechaza: los 5.952 bytes no estan.
 * La radio va al 98,2 % de su SRAM -193.064 de 196.608- y quedan 3.544
 * libres. Queda escrito con el numero delante para que nadie lo intente
 * otra vez a ciegas.
 *
 * Los tamaños se escriben aqui porque es aqui donde se reparte la
 * cabecera, pero quien manda son HFDL_MODO_AVIONES y HFDL_MODO_LINEAS en
 * hfdl_modo.c: alli hay un _Static_assert que no deja compilar si lo que
 * de verdad se declara no cabe en lo que aqui se reserva.
 */
#define HFDL_RAM_AVIONES   2496U   /* 48 x sizeof(hfdl_ac_rec_t), 52 bytes */
#define HFDL_RAM_LINEAS    3456U   /* 48 x HFDL_MODO_LARGO (72) */

#define HFDL_RAM_CABECERA  (9216U + HFDL_RAM_AVIONES + HFDL_RAM_LINEAS)

/*
 * ======================================================================
 * Y EL GIRO 6 SI CABE, PORQUE SE MIDIO - 11/10/2026
 * ======================================================================
 *
 * Esa X vale mas que toda la tabla de arriba, porque es la medida que
 * faltaba. La chapa pone X o Z segun cual de los dos giros que no caben
 * manda, y esta puesta ahi justo para decidir ESTO:
 *
 *   X = los que se caen son del giro 6 -> faltan 5.872 bytes, hay sitio
 *   Z = son del giro 7, que pide 76 kB en una union de 54 -> no hay nada
 *       que hacer con una cantidad razonable de RAM
 *
 * Salio X, y con 94 rafagas de muestra, no con cuatro. Veinte de esas 94
 * -el 21%- NI SE ABRIERON. No es que se intentaran y fallaran: la radio no
 * tenia donde meterlas.
 *
 * ASI QUE SE PAGA. La union media lo que su miembro mayor, la cascada:
 * 54.432. Ahora el mayor es este, 60.304, y la union crece 5.872 bytes.
 *
 *     antes   SRAM 176.240 de 196.608   libre 20.368
 *     ahora   SRAM 182.112 de 196.608   libre 14.496
 *
 * EL GIRO 7 SE QUEDA FUERA Y ESO NO ES UN FALLO: pide 76.288 + 9.216 =
 * 85.504, o sea 25.200 mas de los que hay libres EN TODA LA RADIO. No es
 * "no se ha hecho", es que no se puede, y por eso la chapa distingue las
 * dos letras en vez de contar "las que no cupieron" a secas.
 *
 * El margen que queda sobre el giro 6 es CERO a proposito: esto se
 * dimensiona contra la tabla de los ocho giros, que es un dato del
 * protocolo y no una estimacion. Un "por si acaso" aqui serian bytes
 * robados a la radio para nada. Si un dia hiciera falta mas, el numero que
 * hay que cambiar es este y el banco lo dira.
 */
#define HFDL_RAM_GIRO6     51088U   /* 1200 bps, ranura doble - ver la tabla */
#define HFDL_RAM_TOTAL     (HFDL_RAM_CABECERA + HFDL_RAM_GIRO6)   /* 66.256 desde el 28/10/2026: ver HFDL_RAM_AVIONES */

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
