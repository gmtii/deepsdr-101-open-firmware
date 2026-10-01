#ifndef JTTY_MODO_H
#define JTTY_MODO_H

#include <stdint.h>

/*
 * JTTY como modo de la radio. 28/09/2026.
 *
 * *** Por el dueño del proyecto: "al final no implementaste jtty?" ...
 * "atacalo ahora". ***
 *
 * jtty_rx.c saca ATOMOS del audio; esto los junta en MENSAJES y los
 * engancha a la pantalla y al boton de frecuencia.
 *
 * POR QUE HACE FALTA JUNTARLOS
 * -----------------------------
 * Una trama de JTTY lleva 34 bits, que no dan ni para un indicativo y un
 * localizador. Por eso el modo manda de una a dieciseis tramas seguidas y
 * solo la ULTIMA lleva el bit de fin (ver jtty_msg.h). Enseñar cada atomo
 * en su renglon daria "CQ K1ABC CQ" en uno y "FN42" en el de abajo como
 * si fueran dos estaciones, que es exactamente lo que no son.
 *
 * Y POR QUE SE JUNTAN POR FRECUENCIA. Porque en este modo no hay ranuras:
 * dos estaciones pueden estar emitiendo A LA VEZ en la misma banda, y sus
 * atomos llegan intercalados. Lo unico que los separa es donde estan. Se
 * agrupan en cubos de +-20 Hz, que es mas de lo que una emision se mueve y
 * menos de lo que dos emisiones se separan.
 *
 * SI UN MENSAJE SE QUEDA A MEDIAS -se pierde la trama del final, o el que
 * emite se para- se enseña igual pasados unos segundos, con puntos
 * suspensivos. Tirar lo que se ha entendido porque falta el final seria
 * perder el 90% de un mensaje por el 10% que no llego.
 */

#define JTTY_MODO_LINEAS  14U   /* renglones en pantalla */
/*
 * EL RENGLON YA MONTADO. Sube de 96 a 128 el 28/09 por la tarde, cuando
 * entro la columna del pais: los 32 de mas son para ella y para el
 * tabulador que la separa, no para el mensaje.
 *
 * *** Por el dueño del proyecto: "estaria bien poner una columna en jtty
 * con el pais". ***
 *
 * JTTY_MODO_PAIS es lo que se le reserva. Cuando se escribio esto el nombre
 * mas largo de dxcc.c eran trece letras ("Nueva Zelanda") y con dieciseis
 * cabian ese, el tabulador y dos de sobra.
 *
 * 30/09/2026: ya hay de CATORCE ("Neth. Antilles", "Dominican Rep."), y en
 * ingles tambien. Sigue cabiendo -el peor caso del renglon entero son 124 de
 * los 128 bytes, medido por sim/jtty_cols.c-, pero la cuenta de arriba ya no
 * es la que justifica la constante, y de "sobran dos" hemos pasado a "sobra
 * uno". Si entra un pais de quince letras, hay que volver a medir esto.
 *
 * Son 14 renglones, o sea 448 bytes mas de la memoria prestada - que no de
 * la SRAM libre, que son 3.944-.
 */
#define JTTY_MODO_LARGO   128U  /* lo que ocupa un renglon ya montado */
#define JTTY_MODO_PAIS     16U  /* lo que se le guarda al pais del final */

/*
 * CUANTOS MENSAJES A MEDIAS A LA VEZ. Eran cuatro, y cuatro eran pocos.
 *
 * *** 28/09/2026: en una foto de 14.090 hay NUEVE estaciones a la vez
 * -desvios de -437 a +406 Hz-. ***
 *
 * Y quedarse corto no se notaba como "se pierde un mensaje" sino como algo
 * peor: `cubo_de()` devolvia el cubo mas viejo cuando no habia libre, pero
 * el que llamaba solo lo inicializaba si estaba SIN USAR. O sea que el
 * atomo nuevo se pegaba al mensaje de otra estacion. Era la tercera manera
 * distinta de juntar mensajes que no van juntos, y la unica que no se veia
 * venir leyendo el codigo del cubo.
 *
 * Ahora reutilizar un cubo lo CIERRA primero -sale a pantalla con sus
 * puntos suspensivos- y ademas hay ocho, que es mas de las que se han
 * visto emitiendo a la vez.
 */
#define JTTY_MODO_CUBOS    8U

/*
 * DOS TRAMAS DEL MISMO MENSAJE ESTAN A 118 FILAS, NI UNA MAS NI UNA MENOS.
 *
 * Esta es la funcion que decide si un atomo continua un mensaje ya
 * empezado, y esta aqui suelta y sin estado para que el banco la pueda
 * probar sin montar el modo entero.
 *
 * `fila_ant` es la fila en que acabo la trama anterior de ese mensaje y
 * `fila` la del atomo que acaba de salir; las dos las da
 * jtty_rx_r_t.fila. Devuelve 1 si encaja, y entonces `saltos` dice
 * CUANTAS TRAMAS FALTAN en medio: 0 si viene pegada, 1 o 2 si se han
 * perdido por el camino.
 *
 * Que no encaje quiere decir que el atomo es de OTRA estacion, aunque
 * este en la misma frecuencia. Eso es lo que hay en 14.090: en un modo de
 * teclado se contesta en la frecuencia del otro, asi que dos QSO
 * distintos pueden compartir el mismo cubo de +-20 Hz.
 */
/*
 * LOS DOS NUMEROS, Y LOS DOS MEDIDOS - 28/09/2026 por la tarde.
 *
 * *** Por el dueño del proyecto, segunda foto de 14.090 con la V2.14
 * puesta: ya no junta lo que no va junto, pero PARTE lo que si va junto.
 * Renglones sueltos "5DNZ...", "A IN...", "E LUI...", "LZ1QZ..." donde
 * antes habia un mensaje. ***
 *
 * EL MARGEN: la primera vez lo puse en dos filas RAZONANDO -"el
 * sincronismo se prueba cada media fila y puede clavarse en la que toca o
 * en la de al lado, o sea una fila por trama y dos entre dos tramas"-. No
 * lo habia medido. Medido (mensaje de diez tramas seguidas, 40 repeticiones
 * por punto):
 *
 *     sin desvanecimiento, -6 a -13 dB   error 0 filas en el 100%
 *     con desvanecimiento, -6 a -11 dB   error 0 filas en el 99,4%
 *
 * O sea que el pico del sincronismo cae en la fila EXACTA, y el margen de
 * dos no hacia falta. Se queda en dos igual: lo que cuesta son 20 fases de
 * 600 -un 3,3%- y lo que compra es aguantar un transmisor con el reloj de
 * la tarjeta de sonido desviado, que a 1.000 ppm son 0,6 filas en cinco
 * tramas. Bajarlo a uno ahorraria un 1,3% de enganches falsos y no lo
 * merece.
 *
 * EL ALCANCE: ESTE ERA EL FALLO. Con dos saltos solo se podia recuperar
 * una cadena a la que le faltaran DOS tramas seguidas, y en antena falta
 * mucho mas. Medido en el mismo banco, cuantas tramas faltan entre un
 * atomo y el siguiente:
 *
 *                            -9 dB con desv.   -11 dB con desv.
 *     ninguna                     72,1%              58,3%
 *     una                         17,6%              18,0%
 *     dos                          6,3%              13,7%
 *     tres                         2,7%               4,3%
 *     cuatro o mas                 1,4%               5,7%
 *
 *     alcance con 2 saltos        95,9%              89,9%   <- lo que habia
 *     alcance con 4 saltos        98,7%              94,3%   <- ahora
 *     alcance con 6 saltos       100,0%              99,3%
 *
 * Con cuatro, uno de cada dieciocho enlaces se parte a -11 dB en vez de
 * uno de cada diez. Mas alcance sigue comprando, pero cada salto obliga a
 * subir el plazo de cerrar -son 1,888 s cada uno- y un renglon que tarda
 * doce segundos en salir tambien es peor. Cuatro es donde dejan de pagarse
 * el uno al otro.
 */
#define JTTY_FASE_TOL      2U   /* filas de margen; medido, ver arriba */
#define JTTY_SALTOS_MAX    4U   /* hasta tres tramas perdidas seguidas */

uint8_t  jtty_modo_encadena(uint32_t fila_ant, uint32_t fila, uint8_t *saltos);

uint8_t  jtty_modo_start(void);
void     jtty_modo_stop(void);
uint8_t  jtty_modo_activo(void);

/* Audio de banda lateral a 12 kHz, desde la interrupcion. Barato. */
void     jtty_modo_mete(const float *audio, uint16_t n);

/* Desde el bucle principal: aqui corre el Viterbi de 512 estados. */
void     jtty_modo_poll(void);

void        jtty_modo_borra(void);
uint8_t     jtty_modo_lineas(void);
const char *jtty_modo_linea(uint8_t i);

/* Para la chapa: cuantos atomos han salido y a que frecuencia va. */
uint32_t    jtty_modo_atomos(void);
const char *jtty_modo_estado_txt(void);

/*
 * EL BOTON DE FRECUENCIAS, Y LO QUE ESTE COMENTARIO DECIA ANTES.
 *
 * Aqui habia escrito "NO HAY BOTON DE FRECUENCIAS, Y ES A PROPOSITO",
 * razonado sobre que JTTY se estreno en la v3.2.0-rc1 de este mes y que
 * una tabla inventada se lee como un dato. El razonamiento era bueno; lo
 * que estaba mal era el HECHO, porque la lista SI se pudo comprobar: sale
 * de `models/FrequencyList.cpp` del propio WSJT-X, que es donde el
 * programa que define el modo pone sus frecuencias de llamada. Esta en
 * `k_jtty_canales[]` de main.c con esa procedencia escrita al lado.
 *
 * Se queda anotado porque el error era de los que importan: un comentario
 * que afirma algo que no ha comprobado -aqui, que no habia nada que
 * comprobar-.
 */

#endif /* JTTY_MODO_H */
