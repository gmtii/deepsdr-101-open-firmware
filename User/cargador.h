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
 * comporta igual que el que ya funciona", con el update4.bin de verdad
 * como vector de prueba.
 *
 * Lo que hace el gestor actual, desensamblado en 0x0800A7CA y anotado en
 * el GD32F450VE_FLASH.ld:
 *
 *   1. monta el USB y busca UPDATE4.BIN en el volumen FAT12 del W25Q16
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

/* El mapa que impone el gestor actual y que aqui se respeta tal cual. */
#define CARGA_APP_BASE      0x08020000UL
#define CARGA_FIRMA_ADDR    0x08060000UL
#define CARGA_TAM_MAX       0x50000UL      /* "cmp.w r4, #0x50000" */
#define CARGA_BORRA_HASTA   0x08080000UL   /* sectores 5, 6 y 7 */
#define CARGA_FIRMA_BYTES   8U

typedef enum {
    CARGA_ARRANCA = 0,      /* la imagen de dentro pasa las dos pruebas */
    CARGA_GRABADA,          /* se grabo un UPDATE4.BIN y ahora arranca */
    CARGA_SIN_APP,          /* "APP Not Programmed !": la pila no esta en el TCM */
    CARGA_SIN_FIRMA,        /* "Running APP---": los 8 bytes no cuadran */
    CARGA_NO_HAY_VOLUMEN,
    CARGA_FICHERO_GRANDE,   /* mas de 0x50000: el gestor lo ignora */
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

/* Bytes copiados del volumen a la flash interna en la ultima pasada. */
uint32_t cargador_grabados(void);

/* Partes sueltas, expuestas para que el banco pueda probarlas una a una. */
uint8_t cargador_imagen_vale(const carga_fmc_t *fmc, carga_r_t *porque);
uint32_t cargador_busca_update4(uint32_t *primer_cluster);

#endif /* CARGADOR_H */
