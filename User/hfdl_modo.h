#ifndef HFDL_MODO_H
#define HFDL_MODO_H

#include <stdint.h>

/*
 * HFDL COMO MODO DE LA RADIO. 25/09/2026.
 *
 * Mismo papel que ft8_modo.c hace para FT8: los modulos del proyecto padre
 * saben de senales y no saben nada de esta radio, y este fichero es la
 * costura. Arranca y para, presta la RAM, mete el audio y guarda las lineas
 * que salen para que el panel las pinte.
 *
 * EXCLUSION CON FT8 Y CON LA CASCADA. Los tres se reparten los mismos bytes
 * (ver ft8_shared_ram.h). Quien entra, presta; quien sale, devuelve. No hay
 * forma de estar en dos a la vez porque el modo es uno.
 *
 * ESTE PARRAFO DECIA QUE NO LO LLAMABA NADIE, Y YA NO ES VERDAD.
 *
 * Decia: "no lo llama nadie. Esta puesto para poder MEDIR lo que ocupa
 * HFDL de verdad una vez enlazado - hasta que algo lo referencia,
 * --gc-sections tira los 23 modulos enteros y el binario dice que HFDL es
 * gratis, que es mentira". Eso fue cierto el dia que se porto el modulo.
 *
 * HOY HFDL ES UN MODO DE LA RADIO, cableado entero: lo arranca
 * main.c:11818, lo para main.c:11790, le mete audio demod_am.c:3008 y
 * hfdl_modo_activo() se consulta desde diecisiete sitios. Son 24 ficheros
 * hfdl_*.c, no 23.
 *
 * Se corrige porque el parrafo viejo INVITA A ROMPER LA RADIO: quien lo
 * lea pensara que la llamada de main.c es un andamio de medicion y que
 * puede quitarla, y lo que apagaria es un modo que funciona.
 */

/* Arranca el modo: presta la RAM de la cascada y deja la cadena en cero.
 * Devuelve 0 si no pudo (la cascada ya estaba prestada a FT8). */
uint8_t hfdl_modo_start(void);

/* Para el modo y devuelve la RAM. Idempotente. */
void hfdl_modo_stop(void);

/* 1 si el modo esta activo. */
uint8_t hfdl_modo_activo(void);

/* Mete un bloque de audio ya diezmado. Se llama desde la cadena de audio. */
void hfdl_modo_mete(const float *i_in, const float *q_in, uint32_t n);

/*
 * Un simbolo ya demodulado, con sus DOS muestras: la de la mitad (T/2, que
 * es anterior en el tiempo) y la principal. El ecualizador va a T/2 y las
 * necesita las dos - ver simbolo_de_segmento() en hfdl_modo.c.
 *
 * Un simbolo ya demodulado, directo. Es por donde pasa TODO lo que decide
 * esta capa -preambulo, entrenamiento, polaridad, datos- y por eso es
 * publica: hfdl_modo_mete() la llama simbolo a simbolo, y el banco del
 * simulador tambien. Sin esto, probar la polaridad obligaria a reescribir
 * aqui la maquina de estados, o sea a probar una copia.
 */
void hfdl_modo_simbolo(float ih, float qh, float i_sym, float q_sym);

/* La polaridad global decidida en la ultima rafaga (0 o 1), y si ya esta
 * decidida. Para el banco y para la ventana de informacion. */
uint8_t hfdl_modo_polaridad(void);
uint8_t hfdl_modo_polaridad_lista(void);

/*
 * EL TRABAJO PESADO, FUERA DE LA INTERRUPCION.
 *
 * hfdl_modo_mete() se llama desde el manejador del DMA de audio. Lo que hace
 * por simbolo -mezclar, remuestrear, filtrar, sincronizar- es barato y cabe
 * ahi. Lo que NO cabe es el Viterbi: se ejecuta una vez al acabar la rafaga
 * y es con diferencia lo mas caro de todo HFDL.
 *
 * Asi que al acabar la rafaga solo se levanta una bandera, y el Viterbi lo
 * corre esto, desde el bucle principal. Es la misma leccion que costo cara
 * con FT8: alli la decodificacion bloqueante se comia el doble buffer del
 * diezmador y salian "cinco lineas y luego nada". Aqui lo que se comeria
 * seria el audio.
 *
 * Llamar a menudo; si no hay nada pendiente no hace nada.
 */
void hfdl_modo_poll(void);

/* Cuantos simbolos se le han metido al decodificador en la ultima rafaga.
 * Tiene que ser data_segment_cnt * 30 exactos. Para el banco. */
uint32_t hfdl_modo_simbolos_datos(void);

/* Cuantas lineas hay y cual es cada una (las mas nuevas al final). */
uint32_t    hfdl_modo_lineas(void);
const char *hfdl_modo_linea(uint32_t i);

/*
 * La posicion del avion `i` (mismo orden que hfdl_modo_linea), en grados
 * por diez mil. Devuelve 0 si de ese avion todavia no se sabe donde esta:
 * HFDL lleva muchos tipos de mensaje y solo unos cuantos traen posicion,
 * asi que un avion puede estar en la tabla -se le ha oido- sin que se
 * sepa por donde vuela.
 *
 * Existe para el mapa (ver mapa.h). La tabla es privada; esto es lo unico
 * que se saca de ella.
 */
uint8_t hfdl_modo_pos(uint32_t i, int32_t *lat_e4, int32_t *lon_e4);

/* Cuantos aviones hay en la TABLA, que son mas que los renglones que
 * caben en la pantalla. Para recorrerlos en el mapa. */
uint32_t hfdl_modo_aviones(void);
void        hfdl_modo_borra(void);

/*
 * La senal en tantos por ciento (0-99), sin calibrar: la energia que llega
 * comparada con el suelo de ruido que el modo va aprendiendo. Para comparar
 * una frecuencia con otra. Ver porton() en hfdl_modo.c.
 */
uint8_t hfdl_modo_senal(void);

/* 1 si el portero se abrio de puro hartazgo -mucho rato sin dejar pasar a
 * nadie- en vez de por haber visto senal. Si esto se queda encendido, el
 * umbral no vale para esta antena. */
uint8_t hfdl_modo_porton_forzado(void);

/*
 * El embudo del sincronismo: cuantas veces se ha pasado cada escalon del
 * preambulo desde que arranco el modo. La forma dice donde esta el problema
 * - ver el comentario en hfdl_modo.c. Cualquiera de los tres puede ser NULL.
 */
void hfdl_modo_embudo(uint32_t *a1, uint32_t *a2, uint32_t *m1);

/* Nombre de la ultima estacion de tierra oida, o "" si todavia ninguna. */
const char *hfdl_modo_estacion(void);

/* Tramas que no cupieron en la RAM prestada - ver hfdl_modo.c. No es lo
 * mismo que un CRC malo y por eso se cuenta aparte. */
uint32_t hfdl_modo_no_cupo(void);

/* Rafagas con el CRC bueno. Las malas cuentan en hfdl_modo_total() pero no
 * salen en la lista - ver hfdl_modo_poll(). */
uint32_t hfdl_modo_buenas(void);

/* Cuantos mensajes se han decodificado desde que arranco el modo. */
uint32_t hfdl_modo_total(void);

/* Estado del sincronismo, para la chapa del panel: 0=buscando A1, 1=A2,
 * 2=M1, 3=enganchado. */
uint8_t hfdl_modo_estado(void);

#endif /* HFDL_MODO_H */
