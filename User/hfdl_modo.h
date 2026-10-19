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

/* Copia de los simbolos que entran al decodificador, para que un banco
 * pueda compararlos con lo que se transmitio. Apagada en la radio. Ver el
 * comentario del .c. */
void     hfdl_modo_espia(float *i_out, float *q_out, uint32_t cap);
uint32_t hfdl_modo_espia_n(void);

/* Cuantas lineas hay y cual es cada una (las mas nuevas al final). */
uint32_t    hfdl_modo_lineas(void);
const char *hfdl_modo_linea(uint32_t i);

/* El volcado en crudo del ultimo LPDU de datos de ese avion, en
 * hexadecimal, o "" si no ha mandado ninguno. Empieza por 0x01, que para
 * ui_digi.c significa "este renglon va entero, sin columnas". Ver el
 * comentario de la funcion. */
const char *hfdl_modo_linea_bytes(uint32_t i);

/* Buenas/totales del CRC por velocidad de modulacion, para la cabecera de
 * la vista de bytes: "BPSK 12/14  8PSK 3/57". "" si aun no hay ninguna.
 * Ver el comentario de s_crc_ok[] en el .c. */
const char *hfdl_modo_linea_crc(void);

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

/*
 * Como se llama el avion `i` para ponerlo en el mapa: el distintivo de
 * vuelo ("DLH494") si se sabe, y si no la OACI en hexadecimal ("3C6545").
 * Devuelve 0 -y deja `out` vacio- si no se sabe ninguna de las dos, y
 * entonces ese avion se queda con su punto y sin etiqueta.
 *
 * `tope` incluye el cero final y tiene que ser 8 o mas.
 */
uint8_t hfdl_modo_ident(uint32_t i, char *out, uint8_t tope);
/* La OACI y el modelo del avion i, para la ficha del mapa: "781F2E A359
 * Airbus A350-941". No repite la OACI cuando hfdl_modo_ident() ya la ha
 * devuelto por no saberse el vuelo. Devuelve 0 si no hay nada que decir.
 * Ver su comentario en hfdl_modo.c. */
uint8_t hfdl_modo_ficha(uint32_t i, char *out, uint8_t tope);
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
/* ...y de esas, cuantas eran del giro 7 -el que no se arregla ensanchando
 * la RAM-. Ver s_no_cupo_7 en hfdl_modo.c: decide la LETRA de la chapa. */
uint32_t hfdl_modo_no_cupo_7(void);
/* ...y cuantas se descartaron porque el bucle principal estaba usando la
 * memoria prestada. Ver s_remate en hfdl_modo.c. */
uint32_t hfdl_modo_pisada(void);

/* Rafagas con el CRC bueno. Las malas cuentan en hfdl_modo_total() pero no
 * salen en la lista - ver hfdl_modo_poll(). */
uint32_t hfdl_modo_buenas(void);

/* Cuantos mensajes se han decodificado desde que arranco el modo. */
uint32_t hfdl_modo_total(void);

/*
 * Ráfagas tiradas por el portero del entrenamiento: enganches de
 * preambulo detras de los cuales NO venian los 135 simbolos de
 * HFDL_T_SEQ, o sea ruido que colo por la puerta. Ver el comentario
 * grande de simbolo_de_segmento() en hfdl_modo.c.
 *
 * Es un numero util de mirar en antena: si sube mucho mas deprisa que
 * hfdl_modo_total(), el umbral de A1 esta demasiado flojo para esa banda
 * y ese ruido.
 */
uint32_t hfdl_modo_rechazadas(void);

/* Posiciones recogidas de tramas cuyo LPDU no paso el CRC, aceptadas por
 * las pruebas independientes de posicion_de_confianza(). Se cuentan
 * aparte para poder vigilarlas en antena. */
uint32_t hfdl_modo_pos_sin_crc(void);

/* Cargas de usuario cuyo HFNPDU era un parte de posicion (tipo 0xD1),
 * se sacaran coordenadas o no. Ver s_d1_vistos en hfdl_modo.c. */
uint32_t hfdl_modo_d1_vistos(void);

/* Posiciones sacadas que no se pudieron atribuir a ningun avion. */
uint32_t hfdl_modo_pos_huerfanas(void);

/* Tramas con el CRC bueno que no llegaron a pintar renglon. */
/*
 * Tramas con el CRC bueno que venian ROTAS: la cabecera del MPDU no se
 * deja leer, o no trae ni un LPDU. Sale en la chapa como "/n".
 *
 * Desde el 15/10/2026 NO incluye las tramas buenas que no hablan de
 * ningun avion, que son normales y hacian ilegible el numero. Ver el
 * comentario de apunta_trama() en el .c.
 */
uint32_t hfdl_modo_sin_renglon(void);
#if HFDL_DIAG
/* De que es cada "/n": cabecera ilegible, sin LPDU, o buena pero sin
 * avion (trafico de tierra). Ver el comentario de s_sr_* en el .c. */
void hfdl_modo_dg_sin_renglon(uint32_t *cabecera, uint32_t *sin_lpdu,
                              uint32_t *sin_avion);
#endif

/* Enganches rechazados por no llevar pre-key delante. */
uint32_t hfdl_modo_sin_armar(void);

/* Pre-key separados por menos de media ranura: si son muchos, el
 * detector esta partiendo un mismo tono en dos. */
uint32_t hfdl_modo_prekeys_juntos(void);
uint32_t hfdl_modo_prekeys_con_rafaga(void);
#if HFDL_DIAG
uint32_t hfdl_modo_prekeys_mismo_tono(void);
uint32_t hfdl_modo_prekey_gap_min(void);
#endif
#if HFDL_DIAG
uint32_t hfdl_modo_dg_con_a1(void);
uint32_t hfdl_modo_dg_banda(uint32_t k);
uint32_t hfdl_modo_dg_ok(uint32_t k);
float    hfdl_modo_dg_coh(uint8_t si);
float    hfdl_modo_dg_pico(uint8_t si);
float    hfdl_modo_dg_hz(uint8_t si);
float    hfdl_modo_dg_picoh(uint8_t si);
uint32_t hfdl_modo_dg_gana_half(void);
void     hfdl_modo_dg_clases(uint32_t *sq, uint32_t *sub, uint32_t *baj);
void     hfdl_modo_dg_acars(uint32_t *n, uint32_t *con_pos);
uint32_t hfdl_modo_dg_lpdu(uint32_t k);
uint32_t hfdl_modo_dg_lista(uint32_t k, float *coh, float *hz, float *a1);
uint32_t hfdl_modo_dg_n_lista(void);
uint32_t hfdl_modo_dg_dur(uint32_t k);
float    hfdl_modo_dg_giro(uint32_t k, uint32_t *cual);
#endif

/* Edad del pre-key (en simbolos) en el instante de los enganches. */
void hfdl_modo_edad_prekey(uint32_t *min, uint32_t *max);

/*
 * Cuantos PRE-KEY se han visto: los 249 ms de tono puro a 1440 Hz con
 * que empieza cada ranura (ARINC 635-3, 2.2.2.1.1). Es el numero de
 * ráfagas que de verdad han pasado por la antena, independiente de que
 * el buscador de preambulo las enganchara o no. Comparado con el embudo
 * dice si el problema esta en detectar o en lo de despues.
 */
uint32_t hfdl_modo_prekeys(void);

/* El desvio de frecuencia, en Hz, que midio el ultimo pre-key. */
float hfdl_modo_prekey_desvio(void);

/* DIAGNOSTICO: los bytes que el Viterbi saco de la ultima ráfaga, haya
 * pasado el CRC o no. Para los bancos; la pantalla no lo usa. Devuelve
 * NULL si todavia no ha habido ninguna. Ver hfdl_modo_poll(). */
const uint8_t *hfdl_modo_ultimo_crudo(uint32_t *n);

/* DIAGNOSTICO: la ultima carga de usuario (detras del octeto de tipo del
 * LPDU), entera. Para los bancos. */
const uint8_t *hfdl_modo_ultima_carga(uint32_t *n);

/* La coherencia -de 0 a 1- de la ultima ventana de entrenamiento que se
 * juzgo. 1 es "identica a HFDL_T_SEQ", 0,086 es lo que da el ruido. */
float hfdl_modo_coherencia(void);

/* Estado del sincronismo, para la chapa del panel: 0=buscando A1, 1=A2,
 * 2=M1, 3=enganchado. */
uint8_t hfdl_modo_estado(void);

/*
 * Cuantos LPDU de datos de usuario han entrado y cuantos han dado una
 * posicion. En HFDL la posicion solo viaja ahi dentro, asi que estos dos
 * numeros separan "no ha llegado nada que pueda llevarla" de "esta llegando
 * y no se esta leyendo". Ver su comentario en hfdl_modo.c.
 */
void hfdl_modo_datos(uint32_t *ud, uint32_t *pos);

#endif /* HFDL_MODO_H */
