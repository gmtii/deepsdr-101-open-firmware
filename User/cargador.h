/*
 * EL CARGADOR DE ARRANQUE, EN CODIGO NUESTRO - 01/10/2026.
 *
 * *** El dueno: "un bootloader nuestro pero que hiciese lo mismo que hace
 * el que tenemos, con todos sus impedimentos". ***
 *
 * Y con todos quiere decir TODOS, a proposito. Este fichero no arregla
 * nada del gestor actual: no anade CRC, no anade ranuras A/B, no levanta
 * el techo de 0x50000 y no mueve la firma de 0x08060000. Hace exactamente
 * lo mismo, y por eso se puede comprobar: el criterio de aprobado es "se
 * comporta igual que el que ya funciona", con el update.bin de verdad
 * como vector de prueba.
 *
 * Lo que hace el gestor actual, desensamblado en 0x0800A7CA y anotado en
 * el GD32F450VE_FLASH.ld:
 *
 *   1. monta el USB y busca UPDATE.BIN en el volumen FAT12 del W25Q16
 *      -esto ANTES de comprobar nada, que es lo que salva la radio
 *      cuando se graba una imagen mala-;
 *   2. si lo encuentra y mide 0x50000 o menos: borra los sectores 5, 6 y
 *      7 (0x08020000..0x0807FFFF) y lo copia TAL CUAL a 0x08020000, sin
 *      mirar un solo byte de lo que copia;
 *   3. lee la pila inicial de la aplicacion en 0x08020000 y exige que
 *      este en el TCM (bits 17..28 == 0x800, o sea 0x10000000..0x1001FFFF);
 *   4. compara 8 bytes en 0x08060000 con 8F 25 C8 65 59 9C 55 31;
 *   5. si las dos pruebas pasan, salta. Si no, se queda colgado.
 *
 * No hay checksum, ni longitud, ni verificacion de lo escrito. La firma
 * son literalmente los ocho bytes que haya en esa direccion, y se miran
 * en CADA arranque, no solo al actualizar.
 *
 * DONDE ESTA LA LINEA. Todo lo que DECIDE esta en cargador.c y se prueba
 * en el PC contra el spi_flash.c de verdad, el W25Q16 simulado y un
 * volumen FAT12 de 1 MB de verdad. Lo que no se puede probar aqui -el
 * gadget USB, el FMC y el salto- no decide nada: mueve bytes. Por eso la
 * flash interna entra por este struct y no por llamadas directas.
 *
 * POR QUE NO REUSA EL LECTOR DE FAT DEL spi_flash.c: porque el del
 * firmware arrastra los caminos de ESCRITURA -reserva, recorte, anadido
 * por trozos, las dos copias de la FAT- y el cargador no escribe en el
 * volumen ni una vez. Aqui solo hay lectura, y eso quita de un plumazo
 * toda la familia de fallos de clusters que ya nos mordio una vez.
 */
#ifndef CARGADOR_H
#define CARGADOR_H

#include <stdint.h>

/*
 * EL MAPA DE LA RAMA "reload".  02/10/2026.
 *
 * Ya no lo impone el gestor de fabrica, lo imponemos nosotros, asi que es
 * de una pieza: el cargador abajo y TODO lo demas para la aplicacion.
 *
 *   0x08000000  cargador            48 kB   sectores 0, 1 y 2
 *   0x0800C000  aplicacion     475.136 B    sectores 3, 4, 5, 6 y 7
 *   0x08080000  fin de la flash
 *
 * La base tiene que ser frontera de sector (el borrado es por sectores y
 * el cargador no puede borrarse a si mismo) y estar alineada a 512 por lo
 * menos (el VTOR).  0x0800C000 cumple las dos: es el principio del S3.
 */
#define CARGA_APP_BASE      0x0800C000UL
#define CARGA_BORRA_HASTA   0x08080000UL
#define CARGA_TAM_MAX       (CARGA_BORRA_HASTA - CARGA_APP_BASE)   /* 475.136 */

/*
 * LA CABECERA DE LA IMAGEN.  Ver User/cabecera_app.c para el formato y
 * para por que sustituye a los ocho bytes de firma del gestor viejo.
 *
 * Va en un desplazamiento FIJO, detras del vector de interrupciones, para
 * que el cargador la encuentre sin tener que saber cuanto mide el vector.
 */
#define CARGA_CAB_OFF       0x200UL
#define CARGA_CAB_ADDR      (CARGA_APP_BASE + CARGA_CAB_OFF)
#define CARGA_CAB_BYTES     16U
#define CARGA_CAB_MAGIA     0x52445344UL   /* "DSDR" en un volcado */

typedef enum {
    CARGA_ARRANCA = 0,      /* la imagen de dentro pasa las dos pruebas */
    CARGA_GRABADA,          /* se grabo un UPDATE.BIN y ahora arranca */
    CARGA_SIN_APP,          /* la pila inicial no esta en el TCM */
    CARGA_SIN_CABECERA,     /* no hay magia: ahi no hay una imagen nuestra */
    CARGA_CAB_RARA,         /* magia buena pero longitud imposible */
    CARGA_CRC_MALO,         /* la imagen esta incompleta o corrompida */
    CARGA_NO_HAY_VOLUMEN,
    CARGA_FICHERO_GRANDE,   /* no cabe en la region de la aplicacion */
    CARGA_FICHERO_ROTO,     /* la cadena de clusters no tiene sentido */
    CARGA_ERROR_BORRAR,
    CARGA_ERROR_GRABAR
} carga_r_t;

/*
 * La flash interna. Es lo unico que el cargador no puede probar en el PC,
 * asi que entra por aqui y el banco la sustituye por una simulada con las
 * reglas de verdad del FMC (borrado a 0xFF, y la escritura solo pone
 * ceros: no se puede subir un bit sin borrar el sector entero).
 */
typedef struct {
    void    (*lee)(uint32_t addr, uint8_t *buf, uint32_t len);
    uint8_t (*borra)(uint32_t addr);       /* el sector que contiene addr */
    uint8_t (*escribe)(uint32_t addr, const uint8_t *datos, uint32_t len);
} carga_fmc_t;

/*
 * El arranque entero: buscar fichero, grabar si toca, comprobar y decidir.
 * NO salta -saltar es hardware-; devuelve si la aplicacion vale o no.
 */
carga_r_t cargador_arranca(const carga_fmc_t *fmc);

/* Para el mensaje de la pantalla y para el banco. */
const char *cargador_porque_txt(carga_r_t r);

/*
 * El CRC32 que usa la cabecera: CRC-32/MPEG-2, polinomio 0x04C11DB7, que
 * empieza en 0xFFFFFFFF y NO lleva reflejo ni xor final.  Se expone para
 * que el banco pueda compararlo contra la implementacion independiente de
 * tools/cabecera.py, que es quien escribe el numero que esto comprueba.
 *
 * Llamadas encadenadas: el valor devuelto se vuelve a pasar como `crc`.
 */
uint32_t cargador_crc32(uint32_t crc, const uint8_t *datos, uint32_t n);

/* Bytes copiados del volumen a la flash interna en la ultima pasada. */
uint32_t cargador_grabados(void);

/* Partes sueltas, expuestas para que el banco pueda probarlas una a una. */
uint8_t cargador_imagen_vale(const carga_fmc_t *fmc, carga_r_t *porque);
uint32_t cargador_busca_update(uint32_t *primer_cluster);

#endif /* CARGADOR_H */
