#ifndef STANAG_MODO_H
#define STANAG_MODO_H

#include <stdint.h>

/*
 * EL MODO: APUNTAR A UNA SEÑAL Y QUE DIGA QUE ES - 02/10/2026.
 *
 * *** Por el dueño del proyecto: "implementa stanag" y, un rato despues,
 * "lo he clonado porque en un futuro quiero un modo Identificador de
 * señales". ***
 *
 * Pues resulta que son la misma cosa, y esta es. stanag_det.c mide cada
 * cuanto se repite la estructura de lo que haya en el filtro; esto le pone
 * NOMBRE a ese numero y lo enseña.
 *
 * POR QUE ESTO VA ANTES QUE EL DEMODULADOR DEL 4285
 * --------------------------------------------------
 * Porque se puede probar HOY contra el aire, y el demodulador no. El
 * detector esta verde contra señales que me he fabricado yo, y eso
 * demuestra que se leer lo que yo mismo escribo y poco mas. La primera
 * medida de verdad es apuntar a una señal real y ver si el numero que sale
 * es el que debe ser. Con esto se puede; sin esto habria que esperar a
 * tener el demodulador entero para enterarse de si la primera etapa
 * funcionaba.
 *
 * Y de paso: si al apuntar a un 4285 de verdad sale 106,67 ms, eso valida
 * la cadena entera -oscilador, diezmado, autocorrelacion, eleccion del
 * fundamental- de una sola vez y contra algo que no he escrito yo.
 */

/* Que forma de onda se busca. AUTO la decide el detector. */
enum {
    STANAG_AUTO = 0,
    STANAG_4285,
    STANAG_4529,
    STANAG_4481,
    STANAG_4538,
    STANAG_4415,
    STANAG_FORMAS
};

#define STANAG_MODO_LINEAS  16U
#define STANAG_MODO_LARGO   48U

/*
 * LA MEMORIA NO ES SUYA: se la presta la cascada mientras el modo esta
 * puesto, igual que a HFDL, WSPR, AIS, ALE y JTTY (ver hfdl_ram.h). Eso se
 * resuelve dentro de stanag_modo.c, como hace wspr_modo.c, y por eso no
 * aparece aqui.
 */
uint8_t     stanag_modo_start(void);
void        stanag_modo_stop(void);
uint8_t     stanag_modo_activo(void);

/* El audio de banda lateral a 12 kHz, el mismo que comen FT8, ALE y JTTY.
 * Va en la interrupcion: ver stanag_det_mete(). */
void        stanag_modo_mete(const float *audio, uint16_t n);

/* Del bucle principal: aqui dentro hay dos transformadas. */
void        stanag_modo_poll(void);

/* El boton que cicla entre AUTO y las cuatro. */
void        stanag_modo_forma_pon(uint8_t f);
uint8_t     stanag_modo_forma(void);
const char *stanag_modo_forma_txt(uint8_t f);

void        stanag_modo_borra(void);
uint8_t     stanag_modo_lineas(void);
const char *stanag_modo_linea(uint8_t i);
const char *stanag_modo_estado_txt(void);

/*
 * Lo ultimo medido, para la pantalla y para los bancos: el periodo en
 * decenas de microsegundos (10667 = 106,67 ms) y que forma le encaja.
 */
/*
 * Cuantas medidas lleva hechas. Es lo que mira el repintado del panel para
 * saber si hay algo nuevo que enseñar: sube en cada pasada del detector,
 * HAYA O NO trama a la vista, porque los contadores de diagnostico cambian
 * igual y son los que dicen si esto esta vivo.
 */
uint32_t    stanag_modo_medidas(void);

/*
 * EL LATIDO: un numero que SIEMPRE cambia mientras el modo esta puesto.
 *
 * El repintado del panel mira si hay algo nuevo, y hasta ahora miraba la
 * cuenta de medidas. Eso es un circulo: si el detector no llega a medir
 * -que es justo cuando hay que mirar la pantalla-, la cuenta no cambia, el
 * panel no se repinta, y lo que se ve son los contadores del primer
 * instante. Exactamente lo que le pasaba al dueño cuatro entregas
 * seguidas.
 *
 * Esto cambia cuatro veces por segundo pase lo que pase, asi que el estado
 * -y sus contadores de diagnostico- se refresca aunque no haya nada que
 * detectar. Cuatro veces por segundo y no cada vuelta porque repintar la
 * cabecera entera a la velocidad del bucle principal seria tirar ciclos en
 * algo que nadie puede leer tan rapido.
 */
uint32_t    stanag_modo_latido(void);

uint16_t    stanag_modo_periodo(void);
uint8_t     stanag_modo_encaja(void);

#endif

/*
 * EL DEMODULADOR, QUE VIVE DETRAS DEL MISMO BOTON.
 *
 * Con la forma de onda en "Auto" corre el detector e identifica; puesta
 * en "S4285" corre stanag_rx.c y saca TEXTO. Los dos quieren los mismos
 * 32 kB prestados, asi que nunca corren a la vez, y cambiar de velocidad
 * o de entrelazado obliga a repartir la memoria otra vez porque el
 * desentrelazador cambia de tamaño.
 */
uint8_t     stanag_modo_demod(void);

/*
 * Y EL 4415, QUE ES EL TERCER OFICIO DEL MISMO BOTON.
 *
 * Puesta la forma en "S4415" corre s4415.c: 75 bps, el unico modo de 110A
 * que ensancha cada dibit en 32 simbolos y pasa por donde no pasa nadie.
 * Pide 39 kB del mismo prestamo que los otros dos, asi que tampoco puede
 * correr a la vez.
 */
uint8_t     stanag_modo_4415(void);

/*
 * El boton de velocidad tiene una posicion MAS que velocidades: la
 * ultima es AUTO, que recorre las once combinaciones sola. Por eso el
 * que lo rueda tiene que usar stanag_modo_vel_n() y no
 * STANAG_RX_VELOCIDADES.
 */
void        stanag_modo_vel_pon(uint8_t v);
uint8_t     stanag_modo_vel(void);
uint8_t     stanag_modo_vel_n(void);
const char *stanag_modo_vel_txt(void);

void        stanag_modo_entre_pon(uint8_t largo);
uint8_t     stanag_modo_entre(void);
const char *stanag_modo_entre_txt(void);

/* Por donde va el llenado del entrelazador, para la barra del panel. */
void        stanag_modo_llenado(uint16_t *hechos, uint16_t *hacen);

void        stanag_modo_fmt_pon(uint8_t f);
uint8_t     stanag_modo_fmt(void);
const char *stanag_modo_fmt_txt(void);
uint8_t     stanag_modo_fmt_n(void);
