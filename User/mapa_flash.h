#ifndef MAPA_FLASH_H
#define MAPA_FLASH_H

#include <stdint.h>
#include "mapa_bin.h"

/*
 * EL MAPA GUARDADO EN LA FLASH - 12/10/2026.
 *
 * Pedido por el usuario.
 *
 * LA VERSION DE ANTES LEIA EL .map DEL DISCO EN CADA ARRANQUE y lo dejaba
 * alli. Funcionaba, pero no era lo que se habia pedido ni lo que hace
 * BD.BIN, y tenia dos pegas de verdad: el fichero se quedaba ocupando el
 * disco para siempre, y no habia ni un cartel que dijera si el mapa habia
 * entrado o por que no. Para enterarte de que tu .map no valia tenias que
 * ir a Informacion a mirar una fila, y eso solo lo haces si ya sospechas.
 *
 * AHORA ES EL MISMO RITUAL QUE BD.BIN:
 *
 *     buscar el fichero -> copiarlo al hueco -> comprobar la suma de lo
 *     copiado -> y SOLO ENTONCES borrarlo del disco
 *
 * El orden importa y es el mismo que esta escrito en datos_al_arrancar().
 * Mientras la copia no este verificada, el fichero del disco es el UNICO
 * sitio donde existe ese mapa: borrarlo antes y descubrir despues que lo
 * copiado no vale seria perderlo entero.
 *
 * DONDE. En el hueco de 192 kB que hay arriba del todo de la region de
 * datos, fuera del directorio de la zona alta. Ver ZA_MAPA_BYTES en
 * zona_alta.h para por que no es una seccion mas de ese directorio.
 *
 * LA CABECERA DEL HUECO ocupa un bloque de borrado entero, ella sola, y
 * el .map empieza en el siguiente:
 *
 *     ZA_MAPA + 0      'D','M','A','P'
 *            + 4       u8 version = 1
 *            + 5       u8 cero x3
 *            + 8       u32 cuantos bytes de .map vienen detras
 *            + 12      u32 suma FNV-1a de esos bytes
 *            + 16      sin usar hasta el final del bloque
 *     ZA_MAPA + 4096   el fichero .map tal cual, sin tocar
 *
 * LA CABECERA SOLA EN SU BLOQUE, Y ESCRITA LA ULTIMA. Las dos cosas van
 * juntas y las dos importan. Sola, porque escribirla despues del cuerpo
 * sin borrar el cuerpo exige que no compartan bloque de borrado. Y la
 * ultima, porque asi una copia que se queda a medias -se va la luz, se
 * tira del cable- deja un hueco SIN cabecera, y un hueco sin cabecera es
 * "no hay mapa": arranca con el de serie y no con medio mapa. Lo primero
 * que hace graba_abre() es borrar ese bloque, justo por eso.
 *
 * EL .map SE GUARDA LITERAL, no reempaquetado. Asi la unica cosa que sabe
 * leer ese formato sigue siendo mapa_bin.c, y no hay dos maneras de
 * entenderlo que puedan dejar de cuadrar. La cabecera de aqui solo dice
 * CUANTO hay; de si vale o no ya opina mapa_bin_pon().
 *
 * Y LA SUMA ESTA DOS VECES a proposito: la de aqui dice que los bytes han
 * llegado enteros desde el disco, la de dentro del .map dice que el
 * fichero era bueno de origen. Son preguntas distintas y las dos importan
 * justo despues de copiar.
 */

#define MAPA_FLASH_CAB   4096U   /* un bloque de borrado, la cabecera sola */

/* Lo mas grande que admite el hueco. */
uint32_t mapa_flash_sitio(void);

typedef enum {
    MAPA_FLASH_NADA = 0,   /* no hay mapa guardado */
    MAPA_FLASH_OK,         /* hecho */
    MAPA_FLASH_ESTORBA,    /* la base de datos llega hasta el hueco */
    MAPA_FLASH_GRANDE,     /* el .map no cabe en el hueco */
    MAPA_FLASH_NO_ESTA,    /* el fichero no esta en el disco */
    MAPA_FLASH_COPIA_MAL,  /* la copia fallo por el camino */
    MAPA_FLASH_SUMA_MAL,   /* copiado, pero lo que hay no cuadra */
    MAPA_FLASH_NO_BORRA    /* grabado, pero no se pudo borrar del disco */
} mapa_flash_r_t;

const char *mapa_flash_porque_txt(mapa_flash_r_t r);

/*
 * SI HAY UN MAPA GUARDADO. Cuesta una lectura de 16 bytes, asi que se
 * puede llamar al arrancar sin pensarlo. `n` (puede ser NULL) recibe lo
 * que mide.
 */
uint8_t mapa_flash_hay(uint32_t *n);

/*
 * CARGA EL MAPA GUARDADO y se lo da a mapa_bin_pon(). Necesita un sitio
 * donde leerlo: `pozo` de `cap` bytes. Devuelve el veredicto de
 * mapa_bin_pon(), o MAPA_BIN_NADA si no habia nada guardado.
 */
/* Donde empieza el .map dentro de la flash. Lo necesita mapa_bin.c para
 * poder leer de ahi los niveles troceados - ver mapa_bin_fuente()-. */
uint32_t mapa_flash_donde(void);

mapa_bin_r_t mapa_flash_carga(uint8_t *pozo, uint32_t cap);

/*
 * GRABA EL .map DEL DISCO AL HUECO. Va por pasos como BD.BIN para que la
 * pantalla siga viva: mapa_flash_graba_abre() prepara y devuelve el
 * veredicto de si se puede, mapa_flash_graba_paso() mueve un bloque.
 *
 * `name8` es el nombre del fichero en el disco, sin extension (la
 * extension es siempre "MAP").
 */
mapa_flash_r_t mapa_flash_graba_abre(const char name8[8]);
int8_t         mapa_flash_graba_paso(void);     /* 1 queda, 0 fin, -1 nada */
uint32_t       mapa_flash_graba_hechos(void);
uint32_t       mapa_flash_graba_total(void);

/*
 * CIERRA LA GRABACION: escribe la cabecera del hueco, comprueba la suma de
 * lo que ha quedado en la flash y borra el fichero del disco. Es lo ultimo
 * y es lo unico que borra nada.
 */
mapa_flash_r_t mapa_flash_graba_cierra(const char name8[8],
                                       uint8_t *pozo, uint32_t cap);

/*
 * BORRA EL MAPA GUARDADO y vuelve el de serie.
 *
 * Pedido por el usuario.
 *
 * Borra SOLO la cabecera del hueco, no los 192 kB enteros. Un mapa sin
 * cabecera no se carga, que es lo que se pedia, y borrar un bloque de
 * 4 kB es instantaneo mientras que borrar 192 kB son cuarenta y ocho
 * bloques y medio segundo largo de pantalla parada. Los bytes del mapa
 * viejo se quedan ahi hasta que entre otro encima, y no molestan a nadie:
 * ese hueco no lo lee nadie mas.
 */
uint8_t mapa_flash_borra(void);

#endif /* MAPA_FLASH_H */
