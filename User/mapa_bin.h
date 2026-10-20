#ifndef MAPA_BIN_H
#define MAPA_BIN_H

#include <stdint.h>

/*
 * EL MAPA QUE TRAE EL DUEÑO EN UN FICHERO - 12/10/2026.
 *
 * NO ES UNA FOTO, Y ESO NO ES UN RECORTE: ES LO QUE LO HACE POSIBLE. Una
 * foto del mundo a la resolucion de esta pantalla son 460.800 bytes, y
 * leerlos del bus de la flash cuesta 370 ms por repintado -medido: unos
 * 100 ns por bit-. Arrastrar el mapa iria a tres fotogramas por segundo
 * cuando el presupuesto de uno son 22 ms. Ademas la foto no se puede
 * ampliar: a zoom 8 el mundo mediria 32 MB y el disco entero son 7.
 *
 * Lo que se guarda son TRAZOS, igual que el mapa de serie: asi el zoom 8
 * se dibuja igual de limpio que el 1, ocupa cien veces menos y el coste de
 * pintarlo no cambia.
 *
 * -------------------------------------------------------------------
 * EL FICHERO
 * -------------------------------------------------------------------
 *
 *   0   "DSDRMAP1"          8 bytes, la marca
 *   8   version             1       (hoy 1)
 *   9   niveles             1       cuantos niveles de detalle trae
 *  10   reservado           2       a cero
 *  12   colores            45       quince colores, RGB de tres bytes
 *  57   reservado           3       a cero
 *  60   tabla de niveles    n x 12
 *  ..   los trazos
 *  ..   suma FNV-1a         4       de todo lo anterior
 *
 * LOS QUINCE COLORES, EN ESTE ORDEN:
 *
 *    0 mar          5 ecuador      10 marca_viva
 *    1 tierra       6 boton        11 marca_apagada
 *    2 costa        7 borde        12 casa
 *    3 frontera     8 tinta        13 rotulo
 *    4 rejilla      9 tinta_off    14 linea
 *
 * Y LOS CINCO ULTIMOS SON LOS MISMOS EN TODOS LOS MODOS. Esto lo corrigio
 * el usuario, y era cierto.
 *
 * Yo defendia que los cinco colores que habia -verde para FT8, ambar para
 * los aviones, azul para los barcos, salmon para WSPR, lila para JTTY-
 * eran la leyenda. No lo eran. main.c pinta las marcas en una cadena de
 * else-if, asi que NUNCA hay dos modos poniendo puntos a la vez: el color
 * no distingue nada de nada, porque cuando ves aviones ya sabes que son
 * aviones. Y comprobado uno a uno, cada uno de esos diez colores aparecia
 * UNA sola vez en todo el firmware, en la linea que pintaba la marca. Diez
 * constantes escritas un modo cada dia y nunca miradas juntas.
 *
 * Ahora el color del punto lo dice el mapa, que es quien sabe de que color
 * es su mar. Y con eso sobra el byte de "mapa claro o mapa oscuro" que yo
 * iba a meter: si el fichero elige el color, no hay nada que adivinar.
 *
 * -------------------------------------------------------------------
 * LOS TRAZOS, Y POR QUE VAN POR INCREMENTOS
 * -------------------------------------------------------------------
 *
 * El mapa de serie guarda cada punto como dos enteros de 16 bits en
 * centesimas de grado: cuatro bytes. Y cuatro bytes son pagar por una
 * precision que la pantalla no puede enseñar: a zoom 8 el mundo mide
 * 6.400 px, o sea 0,056 grados por pixel, y una centesima es cinco veces
 * mas fina que eso.
 *
 * Asi que aqui se cuantiza a 1/32 de grado -0,03125, medio pixel al zoom
 * mas grande- y se guardan SALTOS. Entre dos puntos seguidos de una costa
 * simplificada el salto es corto, y en 1/32 de grado un byte con signo
 * llega a +-3,97 grados. Lo que no quepa lleva una marca y va a 16 bits.
 *
 *   trazo:  n (2 bytes)  +  primer punto (2 x int16)  +  (n-1) saltos
 *   salto:  dx dy, dos int8;  o 0x80 y luego dx dy en dos int16
 *
 * MEDIDO sobre el mapa de serie y sobre seis tamaños mas: salen 2,5 bytes
 * por punto contra los 4,2 de ahora. Un 40 %, no el 50 % que calcule a
 * ojo antes de medirlo.
 *
 * Y SE QUEDA CODIFICADO EN LA RAM. Descodificarlo al cargar lo dejaria en
 * cuatro bytes por punto otra vez, o sea que el ahorro se perderia justo
 * donde hace falta. Se descodifica al dibujar, que son dos sumas por
 * punto al lado de la multiplicacion y la division que ya hacen
 * px_de_lon() y py_de_lat().
 *
 * -------------------------------------------------------------------
 * DONDE VIVE
 * -------------------------------------------------------------------
 *
 * El fichero se copia a los ultimos MAPA_BIN_RESERVA bytes de la zona
 * alta, FUERA de lo que toca el empaquetador de BD.BIN. Eso no es manía:
 * si compartieran sitio, cargar una base de aviones nueva se llevaria el
 * mapa por delante sin avisar.
 *
 * En la RAM solo vive un trozo: MAPA_BIN_RAM bytes. Hoy es un nivel de
 * detalle entero; cuando entren los cuatro niveles sera la ventana que se
 * este mirando, que es lo que de verdad limita -la pantalla son 800x288
 * pixeles tengas el zoom que tengas-.
 */

/*
 * EL ESTILO, EN EL BYTE 57 DE LA CABECERA - 14/10/2026.
 *
 * Estaba a cero y reservado. Ahora dice COMO se pinta la tierra, que es
 * cosa del dibujante; QUE es tierra lo dicen los anillos.
 *
 * Un .map viejo trae cero ahi, que es MAPA_ESTILO_CONTORNO, o sea
 * exactamente lo que la radio hacia antes de esto. Por eso el formato no
 * sube de version: lo viejo sigue significando lo mismo.
 *
 * Los nombres salen de tipos.json, donde los cien mapas llevan el suyo
 * escrito desde el principio -"contorno / gris FMDX", "tierra rellena /
 * gris FMDX"- y hasta hoy no significaba nada, porque los seis estilos
 * generaban el MISMO fichero.
 */
#define MAPA_ESTILO_CONTORNO 0U
#define MAPA_ESTILO_RELLENO  1U
#define MAPA_ESTILO_PUNTOS   2U
#define MAPA_ESTILO_SILUETA  3U

/* El estilo del .map cargado, o MAPA_ESTILO_CONTORNO si no hay ninguno. */
uint8_t mapa_bin_estilo(void);

#define MAPA_BIN_MARCA   "DSDRMAP1"
#define MAPA_BIN_VERSION 2U      /* v2: tres niveles, los de arriba troceados */
#define MAPA_BIN_COLORES 15U
#define MAPA_BIN_CAB     60U      /* lo que ocupa la cabecera sin niveles */
#define MAPA_BIN_NIV_MAX 4U
#define MAPA_BIN_NIV_CAB 16U      /* una entrada de la tabla de niveles */
#define MAPA_BIN_CAS_CAB 16U      /* una entrada de la tabla de casillas */
#define MAPA_BIN_RESERVA (192UL * 1024UL)   /* lo suyo en la zona alta */
/*
 * LO QUE EL CUERPO DE TRAZOS PUEDE OCUPAR EN LA RAM.
 *
 * SUBE DE 8.192 A 10.240 EL 12/10/2026, y no por gusto: con 8 kB y el
 * criterio de empaquetado arreglado el mundo NO CABE. Medido, con el
 * nivel del zoom 1 a medio pixel de error y tirando solo islas de menos
 * de cinco pixeles:
 *
 *     376 trazos (164 de costa + 212 de frontera), 3.588 puntos, 9.487 B
 *
 * Con los 8.192 de antes habia que subir la simplificacion hasta tirar
 * TRES MIL SETECIENTOS trazos de costa de los 3.751, y eso es lo que el
 * usuario vio en su radio: una costa rota, con los continentes hechos
 * de cuatro rayas sueltas.
 *
 * Cuesta 2.048 bytes de .bss y deja la RAM libre en unos 3.900. Es caro y
 * es lo que vale el dibujo.
 */
#ifndef MAPA_BIN_RAM
#define MAPA_BIN_RAM     10240U
#endif              /* lo suyo en la SRAM */

/* El orden de los quince, para que nadie cuente a mano. */
enum {
    MAPA_COL_MAR = 0, MAPA_COL_TIERRA, MAPA_COL_COSTA, MAPA_COL_FRONTERA,
    MAPA_COL_REJILLA, MAPA_COL_ECUADOR, MAPA_COL_BOTON, MAPA_COL_BORDE,
    MAPA_COL_TINTA, MAPA_COL_TINTA_OFF, MAPA_COL_MARCA, MAPA_COL_MARCA_OFF,
    MAPA_COL_CASA, MAPA_COL_ROTULO, MAPA_COL_LINEA
};

/* Por que un fichero no vale. Lo pinta la fila de Informacion: "no vale"
 * a secas no deja arreglar nada. */
typedef enum {
    MAPA_BIN_NADA = 0,      /* no hay ninguno cargado, y no es un error */
    MAPA_BIN_OK,
    MAPA_BIN_MARCA_MAL,
    MAPA_BIN_VERSION_MAL,
    MAPA_BIN_SUMA_MAL,
    MAPA_BIN_GRANDE,        /* no cabe en MAPA_BIN_RAM */
    MAPA_BIN_VACIO,
    MAPA_BIN_SIN_CONTRASTE  /* el punto no se veria sobre su propio mar */
} mapa_bin_r_t;

/*
 * Mete un fichero .map ya leido. QUIEN LEE ES EL QUE LLAMA, y no es pereza:
 * para validar hace falta el fichero ENTERO -la suma esta al final- y eso
 * son ocho kilobytes. Si los pidiera este modulo serian ocho kilobytes de
 * .bss permanentes para algo que se usa una vez al arrancar.
 *
 * main.c se los pide prestados a hfdl_ram.h, que al arrancar esta libre
 * porque todavia no hay ningun modo puesto. Lo que se queda aqui es solo
 * el cuerpo de los trazos, ya codificado.
 */
mapa_bin_r_t mapa_bin_pon(const uint8_t *datos, uint32_t n);

/* El ultimo veredicto, y el texto para la pantalla. */
mapa_bin_r_t mapa_bin_estado(void);
const char  *mapa_bin_porque_txt(mapa_bin_r_t r);

/* 1 si hay un mapa del usuario puesto y se puede dibujar. */
/*
 * QUITA EL MAPA PUESTO y vuelve el de serie, sin tocar la flash. Lo llama
 * el boton de borrar de la fila Mapa: borrar de la flash y dejar el de la
 * RAM pintandose hasta el siguiente arranque dejaria la fila diciendo una
 * cosa y la pantalla ensenando otra.
 */
void    mapa_bin_quita(void);

uint8_t mapa_bin_hay(void);

/* Un color del mapa cargado. Si no hay mapa devuelve `porsi`, que es lo
 * que permite llamarlo sin preguntar antes. */
uint32_t mapa_bin_color(uint8_t cual, uint32_t porsi);

/*
 * LA VENTANA: QUE TROZO DEL MUNDO ESTA CARGADO - 12/10/2026.
 *
 * Ayudaba. Medido:
 *
 *   - CUATRO niveles piden 263.000 B de flash, obligan a subir el hueco
 *     de 192 a 256 kB -y por tanto a regenerar BD.BIN- y encima el zoom 2
 *     troceado sale PEOR que el nivel entero de hoy (1,83 px contra 1,00).
 *   - TRES -x1 entero, x4 y x8 troceados- caben en 70.665 B, o sea en el
 *     hueco que YA existe, y no obligan a tocar nada.
 *
 * COMO FUNCIONA. El x1 se carga entero al arrancar y se queda; es el que
 * marca el tamano del buffer (9.375 de 10.240). Para el x4 y el x8 se
 * carga solo la VENTANA: las casillas cuya caja toca lo que se ve. Medido
 * barriendo la pantalla por todo el mundo, la ventana peor son 8.063 B en
 * el x4 y 7.480 en el x8, o sea que caben en el mismo buffer.
 *
 * QUIEN LLAMA. mapa.c, antes de dibujar, dice que escala y que esquina
 * del mundo esta mirando. Si eso cae en el nivel y la ventana que ya hay
 * cargados, esto no hace nada; si no, lee de la flash lo que falte. Por
 * eso hace falta `lee`: en la radio es spi_flash_read, en el banco un
 * fichero.
 */
typedef void (*mapa_bin_lee_t)(uint32_t addr, uint8_t *buf, uint32_t len);

/*
 * De donde se leen los niveles que no estan en la RAM. `base` es donde
 * empieza el .map dentro de la flash. Sin esto, solo funciona el x1.
 */
void mapa_bin_fuente(mapa_bin_lee_t lee, uint32_t base);

/*
 * ABRE EL .map LEYENDO DE LA FLASH, sin tenerlo entero en la RAM. Es lo
 * que usa la radio al arrancar: el v2 mide 70 kB y prestados hay 45.
 * mapa_bin_pon() se queda para comprobar un fichero que ya esta entero en
 * memoria -el que acaba de llegar del disco- y para los bancos.
 */
mapa_bin_r_t mapa_bin_abre_flash(mapa_bin_lee_t lee, uint32_t base, uint32_t n);

/*
 * Pone la ventana. `esc` es 1, 2, 4 u 8; la esquina va en 1/100 de grado,
 * como el resto del modulo. Devuelve 1 si hubo que recargar algo (para
 * poder medir cuantas recargas dispara un arrastre).
 *
 * No falla: si el nivel que pide no esta, se queda con el que mejor le
 * venga de los que hay.
 */
uint8_t mapa_bin_ventana(uint8_t esc, int32_t lon0, int32_t lat0,
                         int32_t lon1, int32_t lat1);

/* Que nivel esta cargado ahora mismo (su zoom), y cuantas recargas van.
 * Las dos para la fila de Informacion y para los bancos. */
uint8_t  mapa_bin_nivel(void);
uint16_t mapa_bin_recargas(void);

/* Cuantos trazos y cuantos puntos trae, para la fila de Informacion. */
/*
 * LO QUE MIDE EL .map DE VERDAD, sacado de su tabla de niveles y no de
 * quien lo trajo. Util para guardarlo: un fichero que llega con relleno
 * detras -el sistema de ficheros redondea a bloque- no tiene por que
 * guardarse con el relleno.
 */
uint32_t mapa_bin_largo(void);

uint16_t mapa_bin_trazos(void);
uint16_t mapa_bin_puntos(void);

/*
 * EL RECORRIDO. Se descodifica al vuelo: ver el comentario de arriba.
 *
 *   mapa_bin_iter_t it;
 *   mapa_bin_primero(&it);
 *   while (mapa_bin_trazo(&it)) {
 *       while (mapa_bin_punto(&it, &lon, &lat)) { ... }
 *   }
 *
 * lon y lat salen en CENTESIMAS de grado, que es lo que usa todo lo demas
 * del mapa: la cuantizacion a 1/32 es cosa del fichero y no se escapa de
 * aqui.
 */
typedef struct {
    const uint8_t *p;      /* por donde va */
    const uint8_t *fin;
    uint16_t trazos_quedan;
    uint16_t puntos_quedan;
    int32_t  x, y;         /* el punto actual, en 1/32 de grado */
    uint8_t  primero;
    /*
     * 1 si el trazo que se esta recorriendo es una FRONTERA y no una
     * costa - 12/10/2026, pedido por el usuario.
     *
     * No eran del mismo grosor: eran del mismo COLOR, que en una linea
     * de un pixel es lo mismo visto de lejos. El .map traia un color
     * propio para las fronteras desde el principio y no lo miraba nadie.
     */
    uint8_t  frontera;
    /*
     * Y ESTE ES UN ANILLO DE RELLENO - 16/10/2026.
     *
     * En los niveles troceados, cada casilla lleva detras de sus costas y
     * sus fronteras los ANILLOS CERRADOS que le tocan, recortados contra
     * su propio rectangulo (ver tools/mapa_pack.py). El relleno se pinta
     * con ellos y solo con ellos: el par-impar sobre costas cortadas no
     * cierra, y eso era lo que sacaba rayas al acercarse.
     *
     * NO SE DIBUJAN: los lados que el recorte añade van por el borde de
     * la casilla y no son costa de nada.
     */
    uint8_t  relleno;
    /* 1 = trazo cerrado cuyo interior es tierra. NO sale de la cuenta de
     * puntos sino de un mapa de bits aparte; ver mapa_bin.c, que cuenta
     * por que. */
    uint8_t  anillo;
    /* Indice del trazo dentro del nivel, que es con lo que se consulta ese
     * mapa de bits. Lo lleva el iterador porque el fichero no lo trae. */
    uint16_t indice;
} mapa_bin_iter_t;

void    mapa_bin_primero(mapa_bin_iter_t *it);
uint8_t mapa_bin_trazo(mapa_bin_iter_t *it);
uint8_t mapa_bin_punto(mapa_bin_iter_t *it, int32_t *lon_c, int32_t *lat_c);

/*
 * LA PARIDAD POR CASILLA, que es lo que permite rellenar al acercarse.
 * El porque entero esta en mapa_bin.c, encima de mapa_bin_par_lee().
 *
 *   mapa_bin_par_hay()    1 si el nivel CARGADO trae el bloque
 *   mapa_bin_par_bytes()  bytes por casilla (hoy 45 = 360 filas)
 *   mapa_bin_nx()/ny()    el troceado del nivel cargado
 *   mapa_bin_par_lee()    los bytes de una casilla, leidos de la flash
 *
 * Un .map que no lo traiga contesta 0 a todo y la radio rellena solo al
 * zoom 1, que es lo que hacia antes del 15/10/2026.
 */
uint8_t mapa_bin_par_hay(void);
uint8_t mapa_bin_par_bytes(void);
uint16_t mapa_bin_relleno_n(void);
uint8_t mapa_bin_nx(void);
uint8_t mapa_bin_ny(void);
uint8_t mapa_bin_par_lee(uint16_t cas, uint8_t desde, uint8_t *buf, uint8_t n);

#endif /* MAPA_BIN_H */
