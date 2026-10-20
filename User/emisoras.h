#ifndef EMISORAS_H
#define EMISORAS_H

#include <stdint.h>

/*
 * QUIEN EMITE EN ESTA FRECUENCIA - 28/09/2026.
 *
 * Sintonizas 9.420 kHz a las siete de la tarde y la radio dice "Voice of
 * Greece, Grecia, griego". Eso es todo lo que hace este modulo.
 *
 * DE DONDE SALEN LOS DATOS. De dos listas publicas que NO son la misma
 * cosa y por eso se meten las dos:
 *
 *   EiBi   8.186 filas. Radiodifusion Y utilitarias: balizas de tiempo,
 *          fax meteorologico, radio maritima, aviacion.
 *   Aoki   5.325 filas. Solo radiodifusion, pero trae el SITIO desde el
 *          que se emite, que EiBi no da.
 *
 * Las empaqueta tools/emisoras_pack.py en EMISORAS.BIN: 1.898 frecuencias
 * y 12.612 entradas despues de fusionar lo que sale en las dos.
 *
 * LO QUE UNA LISTA ASI NO ES. No es la verdad, es un HORARIO PUBLICADO
 * con meses de antelacion. Las emisoras cambian, cierran y se mueven, y
 * la lista trae su fecha de caducidad para poder decirlo. Que aqui salga
 * un nombre significa "esto es lo que estaba previsto", no "esto es lo
 * que estas oyendo". Por eso la radio lo enseña como una pista y no como
 * un hecho, y por eso emisoras_caducada() existe.
 *
 * DONDE VIVE. En la zona alta de la flash SPI, la que el modo USB no
 * enseña, igual que la base de aviones. Pero CRECIENDO HACIA ABAJO desde
 * 0x180000, mientras los aviones crecen hacia arriba desde 0x101000: asi
 * las dos pueden cambiar de tamaño sin que haya que repartir el sitio a
 * mano ni que una decida cuanto puede ocupar la otra. Se tocarian solo si
 * entre las dos pasaran de medio megabyte, y eso el cargador lo comprueba
 * antes de escribir nada.
 *
 * NO DEPENDE DEL GD32. Lee por un puntero a funcion -como lo hacia el
 * desaparecido img_store.c, ver CAMBIOS.md del 08/10/2026-, y por eso el
 * banco del simulador puede darle el
 * EMISORAS.BIN de verdad -el de los 206 kB- y comprobar las busquedas
 * contra un recorrido completo del fichero.
 */

/*
 * Cuando la radio no sabe en que dia de la semana esta -el cristal del RTC
 * no arranco- se pasa esto como `dia` y no se filtra por dia. Es lo
 * honrado: unas pocas entradas solo valen de lunes a viernes, y enseñar
 * una de mas es mucho menos malo que esconder la que se esta oyendo.
 */
#define EMISORAS_DIA_CUALQUIERA 0xFFU

/*
 * El dia de la semana (0 = lunes) a partir de la fecha del RTC. Devuelve
 * EMISORAS_DIA_CUALQUIERA si la fecha no tiene sentido.
 */
uint8_t emisoras_dia_semana(uint16_t anno, uint8_t mes, uint8_t dia);

/*
 * Lo que devuelve una busqueda. Los textos NO vienen como cadenas sino
 * como INDICES, y quien quiera la cadena la pide con emisoras_texto().
 *
 * No es por elegancia. La primera version devolvia punteros a un buffer
 * de dentro del modulo: ocho resultados por cuatro cadenas por cuarenta
 * caracteres, 1.280 bytes de SRAM. En esta radio quedaban 516 libres y el
 * enlazador se planto -"region RAM overflowed by 976 bytes"-. Asi el
 * modulo no gasta memoria en cadenas que casi siempre se tiran: de los
 * tres resultados que se piden, la pantalla pinta uno.
 */
typedef struct {
    uint16_t nombre;        /* indice de texto */
    uint16_t sitio;         /* indice; EMISORAS_SIN_TEXTO si no se sabe */
    uint16_t idioma;        /* indice; EMISORAS_SIN_TEXTO si no se sabe */
    uint8_t  pais;          /* numero de pais, para emisoras_pais() */
    uint16_t ini;           /* minuto UTC en que empieza */
    uint16_t fin;           /* minuto UTC en que acaba */
    uint8_t  dias;          /* bit0 = lunes ... bit6 = domingo */
    uint8_t  ahora;         /* 1 si esta emitiendo en el momento preguntado */
} emisoras_r_t;

#define EMISORAS_SIN_TEXTO 0xFFFFU

/*
 * Copia un texto de la base en `dest`, acabado en cero y recortado a
 * `len`. Con EMISORAS_SIN_TEXTO deja la cadena vacia. Cuesta dos lecturas
 * del bus SPI, asi que se pide solo lo que se va a pintar.
 */
void emisoras_texto(uint16_t idx, char *dest, uint8_t len);

/* Lo mismo para el codigo ITU del pais, que se numera aparte. */
void emisoras_pais(uint8_t n, char *dest, uint8_t len);

/*
 * Abre la base. `lee` es quien sabe leer la flash; `base` donde empieza
 * el fichero. Devuelve 0 y deja el modulo apagado si la cabecera no
 * cuadra - que es lo que pasa si ahi no hay nada, que es el caso normal
 * hasta que el usuario carga el fichero.
 */
uint8_t emisoras_abre(uint32_t base,
                      void (*lee)(uint32_t addr, uint8_t *buf, uint32_t len));

uint8_t emisoras_hay(void);

/*
 * Lee la base ENTERA y comprueba su suma. Del orden de un segundo por
 * este bus, asi que esto NO se llama al arrancar ni al sintonizar: se
 * llama una vez, justo despues de copiar el fichero, que es cuando de
 * verdad puede haberse copiado mal.
 */
uint8_t emisoras_verifica(void);

/*
 * Cuanto hay que RESTARLE al reloj de la radio para tener UTC. Viene en el
 * fichero, no en el firmware: el reloj marca hora local española y las dos
 * listas van en UTC, asi que sin esto saldria la emisora de hace dos horas
 * - un fallo con muy mala pinta precisamente porque el resultado la tiene
 * buena. Ver el comentario de cabecera de emisoras.c.
 */
int8_t emisoras_horas_utc(void);

/*
 * De la hora local de la radio al minuto y al dia de la semana EN UTC.
 * Va aqui y no en quien llama porque la vuelta por medianoche cambia el
 * DIA: a las 00:30 del lunes en España son las 22:30 del DOMINGO en UTC, y
 * una emisora que solo emite entre semana no deberia salir.
 */
void emisoras_utc(uint16_t min_local, uint8_t dia_local,
                  uint16_t *min_utc, uint8_t *dia_utc);

/* Cuantas frecuencias y cuantas entradas tiene la base abierta, y hasta
 * cuando vale (aaaammdd). Para la pantalla de informacion. */
void emisoras_info(uint32_t *n_frec, uint32_t *n_ent, uint32_t *valido_hasta);

/* 1 si `aaaammdd` ya pasa de la fecha de caducidad de la lista. */
uint8_t emisoras_caducada(uint32_t aaaammdd);

/*
 * QUIEN HAY EN `hz`.
 *
 * `tol_hz` es cuanto se admite de desvio: las listas dan kilohercios
 * redondos y la radio puede estar sintonizada 300 Hz al lado. 500 es un
 * valor razonable.
 *
 * `minuto` es el minuto del dia EN UTC (0..1439) y `dia` el dia de la
 * semana con 0 = lunes. Las dos listas dan las horas en UTC, asi que
 * pasarle la hora local hace que salga la emisora equivocada - que es un
 * fallo especialmente feo porque el resultado PARECE bueno.
 *
 * Rellena hasta `max` resultados y devuelve cuantos. Los que estan
 * emitiendo ahora van PRIMERO (con `ahora` a 1); detras, los que usan esa
 * frecuencia a otras horas, que es justo lo que hace falta para saber si
 * merece la pena volver mas tarde.
 */
uint8_t emisoras_busca(uint32_t hz, uint32_t tol_hz,
                       uint16_t minuto, uint8_t dia,
                       emisoras_r_t *out, uint8_t max);

/*
 * ======================================================================
 * LOS NOMBRES SOBRE EL ESPECTRO - 15/10/2026
 * ======================================================================
 *
 * emisoras_busca() contesta "quien hay EN esta frecuencia". Esto contesta
 * "quien hay ENTRE estas dos", que es otra pregunta y se paga distinto.
 *
 * POR QUE NO VALE LLAMAR A emisoras_busca() KILOHERCIO A KILOHERCIO.
 * Medido sobre EMISORAS.BIN de verdad, una pantalla de 96 kHz:
 *
 *     a base de emisoras_busca()   1.401 lecturas   11,7 kB
 *     recorriendo la tabla            ~50 lecturas     ~0,8 kB
 *
 * Treinta veces menos, y por el mismo bus SPI lento de siempre. La tabla
 * de frecuencias YA esta ordenada por hz (ver el formato arriba), asi que
 * el rango es una busqueda binaria para el borde izquierdo y despues
 * andar hacia delante hasta pasarse. Lo demas es leer la entrada de cada
 * una para saber si emite AHORA.
 *
 * SOLO LAS QUE EMITEN AHORA, y lo eligio el usuario teniendo las cuatro
 * opciones delante. Tiene su razon: en la peor ventana de 96 kHz hay 32
 * frecuencias y en 800 pixeles caben unos once nombres legibles, asi que
 * hay que filtrar SI O SI. Filtrar por horario es lo unico que no se
 * inventa nada: la lista dice quien esta en antena a esta hora.
 *
 * LO QUE ESTO SIGUE SIN SER. Lo mismo que dice la cabecera de este
 * fichero: un horario publicado, no lo que se esta oyendo. Que salga un
 * nombre encima de un pico no quiere decir que ese pico sea esa emisora.
 */

/* Una emisora dentro del rango. Se devuelve el INDICE del texto, no la
 * cadena: quien llama decide cuantas resuelve, y resolver cuesta dos
 * lecturas mas por nombre. */
typedef struct {
    uint32_t hz;            /* la frecuencia de la lista, en Hz */
    uint16_t nombre;        /* indice de texto, para emisoras_texto() */
} emisoras_rango_r_t;

/*
 * Las que emiten AHORA entre `hz0` y `hz1`, los dos incluidos.
 *
 * `minuto` y `dia` como en emisoras_busca(): UTC, y
 * EMISORAS_DIA_CUALQUIERA si el reloj no es de fiar.
 *
 * Devuelve cuantas ha puesto, hasta `max`, EN ORDEN DE FRECUENCIA. Si hay
 * mas de `max` en el rango se queda con las `max` primeras y lo dice en
 * `hubo` (que puede ser 0): ese numero es el que permite saber si la
 * pantalla esta escondiendo emisoras, y sin el no se sabria.
 */
uint8_t emisoras_rango(uint32_t hz0, uint32_t hz1,
                       uint16_t minuto, uint8_t dia,
                       emisoras_rango_r_t *out, uint8_t max,
                       uint16_t *hubo);

/*
 * SALTAR A LA SIGUIENTE EMISORA EN ANTENA - 15/10/2026.
 *
 * `dir` positivo busca hacia arriba y negativo hacia abajo, siempre
 * ESTRICTAMENTE: estando encima de una emisora, un clic lleva a la
 * siguiente y no se queda en la misma.
 *
 * `minuto` y `dia` como en emisoras_busca(): solo salta a las que emiten
 * AHORA, y con EMISORAS_DIA_CUALQUIERA salta a todas (que es lo que hay
 * que hacer cuando el reloj no es de fiar).
 *
 * Devuelve 1 y deja la frecuencia en `hz_out` -y el indice del nombre en
 * `nombre`, si se pide-, o 0 si no hay ninguna en ese sentido dentro de
 * EMISORAS_SALTO_MAX casillas. Devolver 0 quiere decir "no la he
 * encontrado con el presupuesto que tenia", no "no existe": el porque
 * del tope esta en el .c.
 */
/*
 * EL PRESUPUESTO DE UN CLIC, EN LECTURAS DE FLASH.
 *
 * No es tacañeria: es lo que hace que un clic dure siempre mas o menos lo
 * mismo. Sin tope, una banda vacia de madrugada haria que un clic
 * recorriera las 1.898 frecuencias de la base.
 *
 * Y se cuentan LECTURAS y no casillas, porque el coste es lo que pasa por
 * el bus: una casilla con doce entradas cuesta trece lecturas y una con
 * una, dos. Con el presupuesto en casillas, medido, diez detentes seguian
 * costando 749 lecturas contra 184 de uno solo: el tope no topaba nada.
 *
 * Y es un PRESUPUESTO QUE SE PASA DE UN SALTO AL SIGUIENTE, no un tope por
 * llamada - 15/10/2026, pedido por el usuario. El mando entrega los detentes acumulados,
 * asi que un giro rapido pedia diez saltos y cada uno gastaba su tope
 * entero: diez veces el peor caso dentro del mismo paso del bucle. Con el
 * presupuesto repartido, girar deprisa cuesta lo mismo que girar despacio,
 * que es lo unico que la cascada puede aguantar.
 */
#define EMISORAS_SALTO_MAX 256U

/*
 * `gasto` lleva la cuenta de lecturas ya gastadas y se ACTUALIZA: se pone a
 * cero antes del primer salto de un sondeo del mando y se pasa tal cual a
 * los siguientes. Puede ser 0 si a quien llama no le importa.
 */
uint8_t emisoras_salto(uint32_t desde, int8_t dir, uint16_t minuto, uint8_t dia,
                       uint32_t *hz_out, uint16_t *nombre, uint16_t *gasto);

#endif /* EMISORAS_H */
