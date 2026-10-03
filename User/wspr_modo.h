#ifndef WSPR_MODO_H
#define WSPR_MODO_H

/*
 * WSPR como modo de la radio. 27/09/2026.
 *
 * Lo de siempre en este proyecto: el decodificador no sabe nada de la
 * radio -wspr_rx.c come audio y suelta mensajes- y este fichero es el que
 * lo engancha al reloj, a la memoria prestada y a la pantalla.
 *
 * LO QUE TIENE DE PARTICULAR FRENTE A FT8 Y HFDL. Una emision dura 110,6
 * segundos y solo empieza en los minutos PARES. O sea que esto no
 * escucha: espera. Y mientras espera no hay nada que ensenar salvo la
 * cuenta atras, que por eso es la barra.
 *
 * DE DONDE SALE LA HORA. Del RTC, igual que FT8. Si el reloj esta mal, la
 * captura empieza donde no toca y no sale nada - por eso la busqueda
 * aguanta un segundo largo de desfase en cada sentido (ver
 * WSPR_RX_ANTES_MS), pero un reloj a minutos de distancia no hay quien lo
 * salve.
 */

#include <stdint.h>

/*
 * APAGADO DE FABRICA, Y YA NO POR SITIO. 27/09/2026.
 *
 * El decodificador entero esta hecho y probado -ver sim/wspr_pruebas.c y
 * sim/wspr_aire.c, que lo llevan del audio al indicativo-. Cuesta 4.156
 * bytes de flash, medidos compilando estos cuatro .o con WSPR_MODO=1.
 *
 * HASTA HOY el problema era que no cabian: la app usaba 258.156 de 262.144
 * bytes y quedaban 3.988. Eso se acabo. Desensamblando el gestor de
 * arranque resulto que el techo de 256 kB era una conclusion a medias: el
 * gestor acepta ficheros de hasta 320 kB y de la imagen solo comprueba
 * ocho bytes en 0x08060000. Metiendo esos ocho bytes DENTRO del binario
 * (User/firma_app.c) y subiendo las tablas de fuentes por encima de ellos
 * (seccion .arriba del enlazador), la region baja bajo a 216.504 bytes:
 *
 *     region baja   0x08020000..0x0805FFFF   216.504 / 262.144   45.640 libres
 *     firma          0x08060000..0x08060007          8 bytes
 *     desvan        0x08060008..0x0806FFFF    42.052 /  65.520   23.468 libres
 *
 * O sea que WSPR cabe once veces. El razonamiento entero, con el
 * desensamblado, esta en el comentario de MEMORY de GD32F450VE_FLASH.ld.
 *
 * ENCENDIDO EL 27/09/2026, una vez cableado a main.c: esta en
 * k_demod_modes[], come audio en demod_am.c, corre en el bucle principal
 * y tiene su panel. El interruptor se queda porque sigue sirviendo para
 * lo mismo que el del DMA de la pantalla: si en antena hiciera algo raro,
 * "make WSPR=0" devuelve un firmware sin el y sin tocar una linea.
 */
#ifndef WSPR_MODO
#define WSPR_MODO 1
#endif

/* El tamano de la tabla vive FUERA del interruptor: main.c lo necesita
 * para dimensionar el panel tanto si el modo esta como si no. */
#define WSPR_MODO_LINEAS   12U
#define WSPR_MODO_LARGO    72U

#if WSPR_MODO

uint8_t  wspr_modo_start(void);
void     wspr_modo_stop(void);
uint8_t  wspr_modo_activo(void);

/* Audio de banda lateral, desde el demodulador. */
void     wspr_modo_mete(const float *audio, uint16_t n);

/* Desde el bucle principal: mira el reloj, arranca y cierra capturas y
 * decodifica cuando toca. Nunca desde una interrupcion: decodificar se
 * lleva su rato. */
void     wspr_modo_poll(void);

void     wspr_modo_borra(void);
uint32_t wspr_modo_lineas(void);
const char *wspr_modo_linea(uint32_t i);

/* Para la barra y la chapa. */
uint8_t  wspr_modo_capturando(void);
uint8_t  wspr_modo_por_ciento(void);
uint16_t wspr_modo_buenas(void);
/* 0 si el reloj no se ha puesto nunca en hora: sin el, la captura
 * arranca donde no toca y no sale nada. */
uint8_t  wspr_modo_hay_hora(void);
/* Segundos hasta que abra la siguiente captura (0 mientras captura). */
uint16_t wspr_modo_espera_s(void);

/*
 * La corroboracion del ultimo intento, 0..100 (0 = todavia no ha habido
 * ninguno). Hacen falta 88 para dar una baliza por buena. Es el numero que
 * contesta a "¿por que no sale nada?" - ver el comentario de donde se
 * guarda, en wspr_modo.c.
 */
uint8_t  wspr_modo_q(void);

#else /* !WSPR_MODO */

/*
 * APAGADO: tapones que no hacen nada, para que main.c y demod_am.c no
 * tengan que llenarse de #if. Son "static inline" y todos devuelven
 * constantes, asi que el compilador se los come enteros: con WSPR_MODO a
 * 0 el binario no lleva ni una instruccion de esto, y --gc-sections se
 * lleva ademas wspr_rx.o, wspr_fano.o y wspr_msg.o, que ya no referencia
 * nadie.
 *
 * Lo unico que SI hace falta envolver en un #if es la fila de
 * k_demod_modes[]: un tapon puede no hacer nada, pero una entrada en la
 * lista de modos se ve en la pantalla y se puede seleccionar.
 */
static inline uint8_t  wspr_modo_start(void)        { return 0U; }
static inline void     wspr_modo_stop(void)         { }
static inline uint8_t  wspr_modo_activo(void)       { return 0U; }
static inline void     wspr_modo_mete(const float *a, uint16_t n) { (void)a; (void)n; }
static inline void     wspr_modo_poll(void)         { }
static inline void     wspr_modo_borra(void)        { }
static inline uint32_t wspr_modo_lineas(void)       { return 0UL; }
static inline const char *wspr_modo_linea(uint32_t i) { (void)i; return ""; }
static inline uint8_t  wspr_modo_capturando(void)   { return 0U; }
static inline uint8_t  wspr_modo_por_ciento(void)   { return 0U; }
static inline uint16_t wspr_modo_buenas(void)       { return 0U; }
static inline uint8_t  wspr_modo_hay_hora(void)     { return 0U; }
static inline uint16_t wspr_modo_espera_s(void)     { return 0U; }
static inline uint8_t  wspr_modo_q(void)            { return 0U; }

#endif /* WSPR_MODO */
#endif /* WSPR_MODO_H */
