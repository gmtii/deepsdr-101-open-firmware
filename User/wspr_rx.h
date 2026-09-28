#ifndef WSPR_RX_H
#define WSPR_RX_H

/*
 * WSPR: del audio a los 162 simbolos. 27/09/2026.
 *
 * La otra mitad del modo. wspr_msg.c y wspr_fano.c saben deshacer el
 * mensaje una vez se tienen los simbolos; esto es lo que los saca del
 * audio, que es donde estan las decisiones que importan.
 *
 * LO QUE HAY QUE ENCONTRAR. Una emision de WSPR dura 110,6 segundos, ocupa
 * 6 Hz y puede estar en cualquier punto de una ventana de 200. Ni se sabe
 * en que frecuencia exacta esta -el que transmite elige- ni cuando empieza
 * exactamente, porque el reloj de aqui y el de alla no son el mismo. Asi
 * que hay que buscar en las dos cosas a la vez.
 *
 * COMO, Y POR QUE ASI:
 *
 *   1. El audio de banda lateral llega a 12 kHz. Se mezcla a banda base
 *      alrededor de 1.500 Hz y se diezma por 32 hasta 375 Hz complejos.
 *      375 no es un numero redondo elegido por gusto: a 375 Hz un simbolo
 *      de WSPR son 256 muestras EXACTAS (8192/12000 * 375 = 256), y por
 *      tanto los tonos caen en bins exactos. Cualquier otra tasa reparte
 *      cada tono entre dos bins y tira señal a la basura.
 *
 *   2. Cada medio simbolo -128 muestras- se mide la potencia de los 137
 *      bins de la ventana con Goertzel, y se guarda en un espectrograma
 *      de 336 x 137 bytes.
 *
 *      GOERTZEL Y NO FFT, y no por vaguería: una FFT de 256 puntos gana
 *      en operaciones, pero aqui sobran operaciones -son 170 ms de CPU
 *      repartidos en 110 SEGUNDOS- y lo que no sobra es flash. Goertzel
 *      son veinte lineas y ninguna tabla de giros.
 *
 *   3. Con el espectrograma hecho se busca: para cada frecuencia candidata
 *      y cada instante de arranque, se correla contra el vector de
 *      sincronismo, que es la parte del mensaje que YA se conoce.
 *
 *   4. Y del mejor candidato salen los 162 valores blandos que come el
 *      decodificador.
 *
 * LA MEMORIA sale de fuera, igual que en HFDL: el espectrograma son 46 kB
 * y solo existen mientras el modo esta puesto. Despues el decodificador
 * reutiliza ese mismo sitio, que para entonces ya no hace falta.
 */

#include <stdint.h>
#include "wspr_msg.h"

/*
 * LA REJILLA DE FRECUENCIA VA A MEDIA CASILLA, Y NO ES UN LUJO.
 *
 * Los cuatro tonos de WSPR estan separados EXACTAMENTE 1/T, o sea una
 * casilla de la transformada. Eso los hace ortogonales - cada tono cae
 * justo en el cero de los otros tres - pero solo si la portadora esta en
 * el centro de una casilla. Y el que transmite elige donde ponerse.
 *
 * A media casilla de distancia la ortogonalidad desaparece del todo: cada
 * tono se reparte entre dos casillas y se mete de lleno en la del tono de
 * al lado. Medido en el banco, con una rejilla de casilla entera:
 *
 *     desvio de 0 casillas .....  sale siempre, a cualquier amplitud
 *     desvio de 0,25 casillas ..  sale siempre
 *     desvio de 0,5 casillas ...  NO SALE NUNCA, a ninguna amplitud
 *
 * Con la rejilla a media casilla, lo peor que puede pasar es quedarse a
 * un cuarto de casilla de un punto de la rejilla - y un cuarto ya se vio
 * que va bien.
 *
 * LO QUE CUESTA: el doble de puntos, y eso no cabe a un byte por punto.
 * Asi que el espectrograma pasa a MEDIO byte por punto. Se pierde
 * resolucion en la potencia -1,6 dB por escalon en vez de 0,35- y se gana
 * la mitad de la ventana, que es el cambio bueno: una emision fuera de la
 * ventana no se decodifica mal, no se decodifica en absoluto.
 */
#define WSPR_RX_BINS     274U   /* puntos de rejilla, a media casilla */
#define WSPR_RX_PASOS    336U   /* 162 simbolos x2, mas margen para buscar */
#define WSPR_RX_RANGO_DB  24    /* lo que abarcan los 16 niveles */

/*
 * CUANDO HAY QUE EMPEZAR A CAPTURAR, y no es cuando empieza la emision.
 *
 * Una emision de WSPR arranca un segundo despues del minuto par, pero el
 * reloj de aqui y el del que transmite no son el mismo: puede llegar
 * antes o despues. La busqueda prueba desplazamientos hacia DELANTE -el
 * espectrograma tiene 336 pasos para 324 de señal, o sea doce de
 * sobra-, y hacia atras no puede probar nada: lo que llego antes de
 * empezar a capturar no esta.
 *
 * Asi que se empieza WSPR_RX_ANTES_MS antes de la hora, y con eso una
 * emision adelantada sigue cayendo dentro. Los doce pasos de margen son
 * 4,1 segundos; empezando dos antes quedan dos por delante y dos por
 * detras.
 *
 * Lo enseño el banco: con la captura empezando a la hora clavada,
 * cualquier emision ADELANTADA mas de medio segundo no salia, y la
 * busqueda se quedaba pegada al paso 0 sin poder ir mas atras.
 */
#define WSPR_RX_ANTES_MS  2000U
/* Medio byte por punto: ver el comentario de arriba. */
#define WSPR_RX_RAM  (((uint32_t)WSPR_RX_BINS * WSPR_RX_PASOS) / 2U + 1024U)

/* Centro de la ventana en el audio, en Hz. Es donde cae el 1.500 de WSPR
 * si se sintoniza la frecuencia de dial que toca. */
#define WSPR_RX_CENTRO_HZ  1500

/* Prepara el receptor sobre `ram` (>= WSPR_RX_RAM bytes, alineado a 4). */
void wspr_rx_init(void *ram, uint32_t bytes);

/* Tira lo capturado y empieza de cero: se llama al empezar cada emision. */
void wspr_rx_reinicia(void);

/* Audio de banda lateral a 12 kHz. Se le puede dar en trozos del tamaño
 * que sea. */
void wspr_rx_mete(const float *audio, uint16_t n);

/*
 * EL TRABAJO CARO, DESDE EL BUCLE PRINCIPAL.
 *
 * wspr_rx_mete() solo mezcla y diezma, que es barato y proporcional al
 * bloque. Cada medio simbolo deja apuntado un paso del espectrograma -274
 * Goertzel de 256 muestras, unos CUATRO milisegundos- y esto es quien lo
 * hace. Devuelve cuantos ha hecho.
 *
 * Tiene que llamarse a menudo: la ventana de muestras se reescribe a la
 * mitad cada 341 ms y un paso que llegue tarde se queda sin sus datos. El
 * bucle principal pasa cada 16 ms, asi que hay veinte veces de margen.
 *
 * NUNCA desde una interrupcion. Meter esto en el manejador del audio es
 * exactamente el fallo que dejaba un tono de 375 Hz sonando durante toda
 * la captura: ver el comentario de un_paso() en wspr_rx.c.
 */
uint8_t wspr_rx_pasos_pendientes(void);

/* Pasos que no dio tiempo a procesar. Cero en una radio sana. */
uint16_t wspr_rx_perdidos(void);

/* 1 cuando ya hay espectrograma entero, o sea cuando la emision acabo. */
uint8_t wspr_rx_listo(void);

/*
 * Busca, decodifica y devuelve 1 si sale algo creible. `hz_out` da el
 * desvio en centesimas de Hz respecto del centro de la ventana -que es lo
 * que se ensena, porque identifica la emision- y `calidad_out` cuanto
 * cuadro lo decodificado con lo recibido (0..100).
 *
 * Es cara: se llama UNA vez, desde el bucle principal, al acabar la
 * emision. Nunca desde una interrupcion.
 */
uint8_t wspr_rx_decodifica(wspr_msg_t *m, int16_t *hz_out, uint8_t *calidad_out);

/* Que eligio la ultima busqueda: bin del tono 0, paso de arranque y
 * puntuacion. Para saber, cuando algo no sale, si fallo la busqueda o
 * fallo el decodificador. */
void wspr_rx_ultimo(uint16_t *bin, uint16_t *paso, int32_t *punt);

/*
 * El diagnostico de la ultima captura, decodificara o no: `q` es la
 * corroboracion del mejor candidato (0..100, hacen falta 88) y `hz_e2` su
 * desvio respecto del centro de la ventana, en centesimas de Hz.
 *
 * Cuando no sale nada, esos dos numeros son lo unico que distingue "no hay
 * senal" (q por los 70) de "la hay y llega justa" (q por los 80) de "el
 * reloj esta corrido" (q bajo y el desvio saltando de una captura a otra).
 * Sin ellos las tres se ven igual: la tabla vacia.
 */
void wspr_rx_diag(uint8_t *q, int16_t *hz_e2);

/* Cuantos pasos del espectrograma llevan hechos, para la barra. */
uint16_t wspr_rx_pasos(void);

#endif /* WSPR_RX_H */
