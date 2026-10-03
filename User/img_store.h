#ifndef IMG_STORE_H
#define IMG_STORE_H

#include <stdint.h>

/*
 * ALMACEN DE IMAGENES EN LA FLASH EXTERNA. Etapa 33, 24/09/2026.
 *
 * QUE PROBLEMA RESUELVE
 * ---------------------
 * WEFAX y SSTV pintan cada linea DIRECTAMENTE en el panel segun llega y no
 * guardan nada (ver el comentario de cabecera de wefax.h). Eso es lo que
 * hizo que cupieran -una carta de superficie son 2 MB y aqui no hay- pero
 * tiene dos consecuencias que se viven mal: de una carta solo se ven las
 * 288 lineas que caben en el panel de las ~1200 que manda la emisora, y si
 * abres el menu encima de una imagen, la imagen se ha ido para siempre.
 *
 * DONDE SE GUARDA, Y POR QUE AHI
 * ------------------------------
 * En la COLA de bloques borrados de la flash externa, por encima del
 * sistema de ficheros. Medido en la radio el 24/09/2026 con
 * spi_flash_mira_alta():
 *
 *     chip                2,0 MB (medido por alias, no por el identificador)
 *     sistema de ficheros 1,0 MB (de su propio BPB)
 *     zona alta           ####....   122 de 256 bloques con datos
 *     firma de update4    no aparece
 *
 * O sea: en el primer medio megabyte por encima del sistema de ficheros hay
 * ALGO -unos 500 kB, que no es update4.bin porque su firma no esta- y el
 * medio megabyte de arriba esta entero borrado. Ese es el sitio.
 *
 * DONDE SE GUARDA, Y COMO SE LLEGO A ESE SITIO - 24/09/2026
 * ---------------------------------------------------------
 * Esto se decidio dos veces, y la primera estaba mal. Merece la pena
 * dejar escrito el camino, porque la conclusion no se deduce del codigo.
 *
 * LA PRIMERA DECISION. El dueño del proyecto noto que update4.bin
 * DESAPARECE del sistema de ficheros despues de flashear, y dedujo que el
 * gestor de arranque lo copiaba a la zona alta de la flash SPI antes de
 * programarlo. La forma cuadraba: medio megabyte borrado ahi arriba son
 * exactamente dos imagenes de 256 kB. Si eso fuese asi, "borrado" no
 * significaria "libre" sino "recien usado y soltado", y guardar imagenes
 * ahi tendria las dos caras malas - la siguiente actualizacion se las
 * llevaria por delante, y peor, podria quedarse sin sitio seguido y dejar
 * la radio sin poder actualizarse-. Asi que el suelo se puso en 2 MB, o
 * sea: no hay almacen hasta que se cambie el chip por uno mayor.
 *
 * LO QUE SE MIDIO DESPUES. Se volco la zona alta y luego la flash interna
 * entera, y las dos dijeron otra cosa:
 *
 *   - la zona alta son 500 kB de FUENTE CHINA (6.940 glifos de 24x24,
 *     72 bytes cada uno) del firmware de fabrica, y medio mega borrado
 *     encima. Ni rastro de update4.bin ni de su firma;
 *   - el gestor de arranque (v4.0.3, con FatFs y pila USB de ST) lee
 *     update4.bin DEL SISTEMA DE FICHEROS y lo programa DIRECTO en la
 *     flash interna. Sus propios mensajes lo cuentan: "Target update4.bin
 *     find, updating...", "Erase SECTOR 1... 2... 3...". Desaparece del
 *     disco porque lo BORRA al terminar, no porque lo mueva;
 *   - y no usa la fuente china: lleva la suya, "neep-iso8859-1-10x20",
 *     y todos sus mensajes son en ingles.
 *
 * O sea que la zona de arriba no la usa nada. La primera decision estaba
 * tomada sobre una hipotesis razonable que resulto ser falsa.
 *
 * POR ESO EL SUELO ESTA EN 1,5 MB. Por debajo queda la fuente -intocable
 * por si acaso, que no cuesta nada- y por encima el medio megabyte
 * borrado, que es lo que se usa.
 *
 * Y SE COMPROBO, LAS DOS COSAS. Primero se escribio una imagen de prueba
 * de 256 kB en la zona de arriba y se flasheo una version encima: siguio
 * entera, o sea que el gestor de arranque no ESCRIBE ahi. Despues se
 * borro la fuente china entera y se reinicio: la pantalla del gestor de
 * arranque salio igual y el modo USB tambien, o sea que tampoco la LEE.
 *
 * Esa segunda prueba hacia falta porque la primera no la cubria: que
 * nadie escriba encima no dice nada de quien lee. Y demostrar que NADIE
 * LEE algo no se puede hacer desde dentro - solo se puede quitar y
 * mirar-. Por eso el suelo bajo de 2 MB a 1,5 y despues a 1: cada bajada
 * vino detras de su medida, no de un razonamiento.
 *
 * El invariante de mas abajo sigue en pie igual: si algo de esto fuera
 * falso, lo peor que puede pasar es que el almacen se quede sin sitio, no
 * que se lleve nada por delante.
 *
 * Y CON UN CHIP MAS GRANDE sigue siendo mejor: un W25Q64 de 8 MB o un
 * W25Q128 de 16 dejan arriba una zona que NINGUNA de las dos cosas de
 * abajo puede siquiera direccionar, porque cuando se escribieron ese
 * sitio no existia. Esa es una garantia de otra clase - no depende de
 * medir bien, depende de la aritmetica- y ademas son 14 MB en vez de
 * medio.
 *
 * EL INVARIANTE DE SEGURIDAD
 * --------------------------
 * Lo que hay en ese medio megabyte de abajo no se sabe que es. Podria ser
 * cualquier cosa, incluido algo sin lo que la radio no arranque. Asi que
 * este modulo no decide "donde escribir" mirando un mapa: decide por una
 * regla que no puede fallar aunque el mapa este mal.
 *
 *     NO SE BORRA NI SE GRABA NUNCA UN BLOQUE QUE NO ESTE O BIEN ENTERO
 *     A 0xFF, O BIEN MARCADO CON LA MAGIA DE ESTE MODULO.
 *
 * "Entero a 0xFF" quiere decir los 4096 bytes leidos y comprobados, no una
 * muestra: para decidir si se puede destruir algo, un muestreo no vale.
 * Y la magia es lo que distingue "esto lo escribimos nosotros y se puede
 * reciclar" de "esto estaba aqui antes y es intocable".
 *
 * Con esa regla, lo peor que puede pasar si las medidas fallan es que el
 * almacen se quede sin sitio. No que se lleve nada por delante.
 *
 * NO DEPENDE DEL GD32
 * -------------------
 * El medio -leer, borrar, grabar- entra por punteros a funcion, igual que
 * nb.c o dcf77.c no dependen de nada de la placa. Asi el banco del
 * simulador puede poner debajo un chip NOR SIMULADO que hace cumplir las
 * reglas de verdad (grabar solo puede bajar bits de 1 a 0; borrar solo
 * bloques enteros) y que ademas apunta cada borrado, que es como se
 * comprueba que el invariante de arriba se cumple de verdad y no solo en
 * el comentario. Ver sim/imgstest.c.
 */

/*
 * El suelo absoluto. Por debajo de aqui no se escribe NUNCA, venga de
 * donde venga el suelo que le pasen a imgs_init().
 *
 * 1 MB: justo donde acaba el sistema de ficheros de fabrica. Por debajo
 * esta el volumen FAT del que arranca la actualizacion, y eso no se toca
 * jamas. Por encima ya no hay nada de nadie - ver el bloque de arriba,
 * donde estan las dos medidas que lo dicen-.
 *
 * Esto NO sustituye al suelo que pasa quien llama (el final del sistema
 * de ficheros, MEDIDO de su propio BPB). Es una segunda valla por si esa
 * medida fallara algun dia: dos vallas independientes en el mismo sitio.
 */
#define IMGS_SUELO_MIN (1024U * 1024U)

#define IMGS_BLOQUE 4096U     /* el bloque de borrado del chip */
#define IMGS_PAGINA  256U     /* la pagina de grabacion del chip */
#define IMGS_CAB      32U     /* lo que ocupa la cabecera de cada imagen */

/* Los tipos de imagen que se guardan. El formato va DENTRO del registro,
 * asi que una version futura que anada otro puede leer los viejos. */
#define IMGS_TIPO_WEFAX 1U    /* gris, 4 bits por punto */
#define IMGS_TIPO_SSTV  2U    /* color, RGB565 */

typedef struct {
    /* Lee `n` bytes desde `addr`. */
    void (*lee)(uint32_t addr, uint8_t *buf, uint32_t n);
    /* Borra el bloque de 4 kB que contiene `addr` (lo alinea el propio
     * medio). Deja los 4096 bytes a 0xFF. */
    void (*borra)(uint32_t addr);
    /* Graba hasta IMGS_PAGINA bytes sin cruzar un limite de pagina. NO
     * borra: los bytes de destino tienen que estar ya a 0xFF. */
    void (*graba)(uint32_t addr, const uint8_t *buf, uint32_t n);
} imgs_medio_t;

/*
 * POR QUE HAY ALMACEN O NO LO HAY.
 *
 * imgs_init() ya devolvia 0 en todos los casos malos, pero "0" no dice
 * cual de ellos: en la pantalla salia "sin sitio" tanto si el chip es el
 * de fabrica -que es lo normal y no es un fallo- como si el ultimo bloque
 * lo ocupa algo desconocido, que si lo es. Son situaciones distintas y
 * piden cosas distintas de quien lee, asi que se distinguen.
 */
typedef enum {
    IMGS_OK = 0,
    IMGS_NO_MEDIDO,    /* no se pudo medir el chip: no se arriesga nada */
    IMGS_CHIP_CHICO,   /* el de fabrica: no hay sitio propio donde escribir */
    IMGS_ZONA_AJENA,   /* el ultimo bloque del chip es de otro */
    IMGS_POCO_SITIO    /* libre, pero tan poco que no merece la pena */
} imgs_veredicto_t;

uint8_t     imgs_veredicto(void);
const char *imgs_veredicto_texto(void);

/* Solo para el banco de anchos del simulador: pone el veredicto a mano
 * para poder medir el texto de TODOS sin tener que montar un chip falso
 * por cada uno. Ver sim/infotest.c. */
void imgs_fuerza_veredicto(uint8_t v);

typedef struct {
    uint8_t  tipo;
    uint8_t  bpp;
    uint16_t ancho, alto;
    uint32_t bytes;      /* lo que ocupan los datos */
    uint32_t fecha_ms;   /* ms del dia en que se guardo */
    uint32_t addr;       /* donde empieza su cabecera */
} imgs_info_t;

/*
 * Arranque. `suelo` y `tope` acotan donde PUEDE mirar (normalmente, desde
 * donde acaba el sistema de ficheros hasta donde acaba el chip, los dos
 * medidos). Dentro de ese rango busca la cola de bloques utilizables
 * -borrados o nuestros- y se queda con ella.
 *
 * Devuelve cuantos bytes ha encontrado utilizables. Cero es un resultado
 * legitimo y quiere decir "aqui no se guarda nada", no un error.
 */
uint32_t imgs_init(const imgs_medio_t *medio, uint32_t suelo, uint32_t tope);

/* Lo encontrado en el arranque, para enseñarlo. */
uint32_t imgs_libre(void);    /* bytes libres para imagenes nuevas */
uint32_t imgs_total(void);    /* bytes de la zona entera */
uint8_t  imgs_cuantas(void);  /* imagenes guardadas */

/*
 * Guardar una imagen. Va en dos tiempos porque una carta de fax llega
 * linea a linea durante diez minutos y no cabe en la RAM:
 *
 *   imgs_abre()   reserva sitio y escribe la cabecera
 *   imgs_escribe() añade datos, tantas veces como haga falta
 *   imgs_cierra() da la imagen por buena
 *
 * Una imagen que se queda a medias -se corta la señal, se apaga la radio-
 * no queda "medio guardada": su cabecera no lleva la marca de cerrada, y
 * al arrancar se la trata como sitio reciclable.
 *
 * imgs_abre() devuelve 0 si no cabe o si ya hay una abierta.
 */
uint8_t imgs_abre(uint8_t tipo, uint16_t ancho, uint16_t alto, uint8_t bpp,
                  uint32_t bytes, uint32_t fecha_ms);
uint8_t imgs_escribe(const uint8_t *datos, uint32_t n);
uint8_t imgs_cierra(void);
void    imgs_cancela(void);
uint8_t imgs_abierta(void);

/* Leer. `i` va de 0 (la mas antigua) a imgs_cuantas()-1. */
uint8_t imgs_lee_info(uint8_t i, imgs_info_t *out);
uint8_t imgs_lee_datos(uint8_t i, uint32_t desde, uint8_t *buf, uint32_t n);

/*
 * Comprueba que una imagen guardada sigue entera: relee sus datos y los
 * compara con la suma que se guardo al cerrarla. Devuelve 1 si cuadra.
 *
 * No es adorno. El sitio donde se guardan se eligio por deduccion (ver
 * arriba), y esta es la forma de enterarse si la deduccion falla: una
 * imagen que estaba bien y ahora no cuadra dice que algo mas escribio
 * encima. Es tambien lo que convierte "creo que es seguro" en una
 * comprobacion que se puede repetir despues de cada actualizacion.
 */
uint8_t imgs_verifica(uint8_t i);

/*
 * Lo mismo, A PASOS. Releer un cuarto de megabyte por un bus bit a bit no
 * se puede hacer dentro del toque que lo pide: la radio se queda parada y
 * desde fuera eso no es "va lento", es "se ha colgado". Misma cuenta, misma
 * formula -por eso vive aqui y no en quien lo llama-, repartida en trozos.
 *
 *   imgs_verif_inicia(i)   prepara
 *   imgs_verif_paso()      un trozo: 1 = sigue, 0 = cuadra, -1 = NO cuadra
 */
void    imgs_verif_inicia(uint8_t i);
int8_t  imgs_verif_paso(void);
uint32_t imgs_verif_hechos(void);
uint32_t imgs_verif_total(void);

/* Borrar TODAS las imagenes. Solo toca bloques con nuestra magia; lo que
 * no la lleve no se toca ni aunque caiga dentro de la zona. */
void imgs_borra_todo(void);

#endif /* IMG_STORE_H */
