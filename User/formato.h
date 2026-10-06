#ifndef FORMATO_H
#define FORMATO_H

#include <stdint.h>

/*
 * FORMATEAR EL DISCO DESDE LA PROPIA RADIO - 06/10/2026.
 *
 * *** El dueño, con el issue de "no se lee el volumen" abierto y el
 * usuario del otro lado ya habiendo formateado el pendrive desde Windows
 * sin que sirviera: "haz un puto boton en el menu oculto que formatee la
 * flash al formato que a ti te gusta leer". ***
 *
 * Y es la respuesta correcta al issue. El driver de spi_flash.c no admite
 * cualquier FAT: pide sectores de 512 bytes, cluster potencia de dos que
 * divida a 4 kB -el borrado de la flash-, DOS tablas, y que la zona de
 * datos empiece alineada a 4 kB. Windows no tiene por que elegir nada de
 * eso, y segun el tamaño del chip y la version de Windows elige otra cosa.
 * De ahi que formatear desde el PC no arregle el issue: deja un volumen
 * perfectamente valido para Windows y que este driver no puede escribir.
 *
 * Pedirle al usuario que acierte con las opciones de formateo de Windows
 * es pedirle que adivine. Formatearlo desde aqui es poner exactamente lo
 * que este driver sabe leer, y se acabo.
 *
 * QUE SE ESCRIBE. El sector de arranque, las dos tablas y el directorio
 * raiz. La zona de datos NO se toca: la tabla dice que esta libre, y
 * borrar siete megas de flash para dejarlos a 0xFF seria un minuto largo
 * de espera para no cambiar nada que se pueda leer.
 */

typedef enum {
    FORMATO_OK = 0,
    FORMATO_NO_LEE,        /* formateado, pero el driver no lo reconoce */
    FORMATO_NO_GRABA,      /* lo lee, pero no lo puede escribir */
    FORMATO_PRUEBA_MAL,    /* escribio un fichero y no salio igual al leerlo */
    FORMATO_SIN_CHIP,      /* no se ve la flash */
    FORMATO_MUY_PEQUENO,   /* no caben ni las tablas */
    FORMATO_ERROR_ESCRITURA
} formato_r_t;

/*
 * Formatea el volumen entero en FAT12 con los numeros que este driver
 * sabe manejar. Tarda unas decimas de segundo: no hay barra de progreso
 * porque no da tiempo a verla.
 */
formato_r_t formato_haz(void);

/*
 * SE COMPRUEBA SOLO, Y NO SE FIA DE HABERLO ESCRITO - 06/10/2026.
 *
 * *** El dueño: "formatear disco despues de poner hecho deberia de testear
 * que puede leer y grabar". ***
 *
 * Y tiene razon, porque "hecho" a secas no dice nada: lo unico que
 * garantiza es que las escrituras no devolvieron error, y el issue que
 * trajo todo esto iba precisamente de un volumen escrito sin errores que
 * el driver no podia usar.
 *
 * Asi que despues de formatear se hacen tres cosas, de menos a mas:
 *
 *   1. que spi_flash_geometria_lectura() lo reconozca     -> se puede leer
 *   2. que spi_flash_geometria() tambien                  -> se puede grabar
 *   3. y la de verdad: ESCRIBIR UN FICHERO Y VOLVER A LEERLO. Las dos
 *      primeras miran el sector de arranque y echan cuentas; esta pasa por
 *      la tabla, el directorio y la zona de datos, que es donde de verdad
 *      se rompen las cosas.
 *
 * El fichero de prueba se llama PRUEBA.TXT y se queda ahi: 23 bytes y
 * sirve para que el dueño vea desde el PC que el disco monta.
 */
formato_r_t formato_comprueba(void);

/* El motivo, para la pantalla. */
const char *formato_porque_txt(formato_r_t r);

/*
 * Lo que VA A PONER, sin escribir nada. Para que la fila de la pantalla
 * pueda decir "4096 B/cluster, 250 clusters" antes de que nadie toque
 * nada, y para que el banco pueda comprobar las cuentas sin una flash.
 */
uint8_t formato_plan(uint32_t bloques, uint8_t *spc, uint16_t *reservados,
                     uint16_t *fat_sec, uint16_t *raiz_ent, uint16_t *clusters);

#endif /* FORMATO_H */
