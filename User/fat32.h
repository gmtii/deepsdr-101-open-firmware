#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include "bloques.h"

/*
 * FAT32 DE SOLO LECTURA, PARA EL PINCHO.
 *
 * *** La peticion que llego al repositorio: grabar lo que se recibe a un
 * pendrive USB. ***
 *
 * EL NUMERO QUE MANDA EN TODO ESTE FICHERO
 * ----------------------------------------
 * En un pincho de los de verdad la tabla de asignacion mide esto:
 *
 *     1 GB  ->  1,0 MB de FAT   (clusters de 4 kB)
 *     8 GB  ->  8,0 MB de FAT   (clusters de 4 kB)
 *    32 GB  ->  8,4 MB de FAT   (clusters de 16 kB)
 *
 * Y en esta radio quedan 10,7 kB de RAM libres. O sea que **la FAT no se
 * puede cachear**, ni entera ni por la mitad ni por un octavo: no cabe por
 * tres ordenes de magnitud. Cada salto de cluster es una lectura de sector
 * al pincho, y el diseño entero sale de ahi.
 *
 * Por eso aqui no hay mas memoria que UN sector de FAT (512 bytes) y UN
 * sector de datos. Leer un fichero seguido de 3 MB con clusters de 4 kB son
 * 768 saltos, y cada entrada de FAT32 son 4 bytes, asi que 128 saltos caen
 * en el mismo sector cacheado: una lectura de FAT por cada 512 kB leidos.
 * Eso es barato. Lo que seria caro -y por eso no se hace- es buscar hueco
 * libre recorriendo la FAT entera.
 *
 * LO QUE HACE Y LO QUE NO
 * -----------------------
 * Lee. Montar, listar el directorio raiz, abrir por nombre y leer seguido.
 * Escribir viene despues y es otro problema, porque escribir obliga a
 * buscar clusters libres y a mantener la FAT y el directorio coherentes
 * ante un apagon.
 *
 * Nombres: se comparan los 8.3 de siempre. Los nombres largos ocupan
 * entradas extra que se saltan al listar; el fichero sigue teniendo su 8.3
 * ("NOMBRE~1 TXT") y por ese se abre. Para grabaciones que crea la propia
 * radio eso sobra.
 */

#define FAT32_TAM_SECTOR  512U

typedef struct {
    const bloques_t *dev;
    uint32_t vol0;          /* primer sector del VOLUMEN (0 si no hay MBR) */
    uint32_t fat0;          /* primer sector de la FAT #1, absoluto */
    uint32_t datos0;        /* sector del cluster 2, absoluto */
    uint32_t sec_por_fat;
    uint32_t raiz_cluster;
    uint32_t clusters;      /* cuantos clusters de datos hay */
    uint8_t  sec_por_cluster;
    uint8_t  n_fats;
    uint8_t  montado;

    uint32_t fat_cache_sec;             /* 0xFFFFFFFF = vacia */
    uint8_t  fat_cache[FAT32_TAM_SECTOR];

    /*
     * EL SECTOR DE DATOS, Y POR QUE VIVE AQUI Y NO EN LA PILA - 09/10/2026.
     *
     * La primera version declaraba `uint8_t s[512]` dentro de cada funcion.
     * Funcionaba en el ordenador y habria funcionado casi siempre en la
     * radio, que es lo peor que puede hacer: aqui la pila mide 7.408 bytes
     * y el maximo medido va por 5.296, o sea que quedan 2.112 de margen
     * CONTANDO el anidamiento de interrupciones. Montar gastaba 1 kB de
     * golpe -dos buffers vivos a la vez- y leer otro medio por encima.
     *
     * La mitad del margen de pila de la radio entera, para un buffer que es
     * siempre el mismo. Y pasarse de pila aqui no da un error: pisa lo que
     * haya debajo y la radio se cuelga mas tarde, en otro sitio, sin que
     * nada apunte hacia aqui.
     *
     * En .bss son 512 bytes fijos de los 10,7 kB libres, y la pila no se
     * entera. El precio es que esta estructura ya no se puede tener dos
     * veces, que es justo lo que queremos: un pincho hay uno.
     */
    uint8_t  buf[FAT32_TAM_SECTOR];
} fat32_t;

typedef struct {
    char     nombre[13];    /* "NOMBRE~1.TXT" + fin */
    uint32_t cluster;
    uint32_t tam;
    uint8_t  es_dir;
} fat32_entrada_t;

typedef struct {
    fat32_t *fs;
    uint32_t cluster_ini;
    uint32_t tam;
    uint32_t pos;           /* cuantos bytes se han leido ya */
    uint32_t cluster;       /* el cluster donde esta `pos` */
    uint32_t cluster_idx;   /* el indice de ese cluster dentro del fichero */
} fat32_fich_t;

/* Codigos de fat32_monta(). Negativos para no confundirlos con un 0. */
enum {
    FAT32_OK           =  0,
    FAT32_E_LEER       = -1,   /* el medio no contesta */
    FAT32_E_SECTOR     = -2,   /* el sector no mide 512 */
    FAT32_E_NO_FAT32   = -3,   /* hay volumen pero no es FAT32 */
    FAT32_E_GEOMETRIA  = -4,   /* numeros del arranque incoherentes */
    FAT32_E_FIN        = -5,   /* se acabo el fichero o el directorio */
    FAT32_E_CADENA     = -6    /* la cadena de clusters esta rota */
};

int fat32_monta(fat32_t *fs, const bloques_t *dev);

/* Recorre un directorio. `cl` es su primer cluster (fs->raiz_cluster para
 * la raiz). `i` es el indice de la entrada: se empieza en 0 y se sube de
 * uno en uno hasta que devuelve FAT32_E_FIN. */
int fat32_entrada(fat32_t *fs, uint32_t cl, uint32_t i, fat32_entrada_t *e);

int fat32_abre(fat32_t *fs, uint32_t dir_cl, const char *nombre,
               fat32_fich_t *f);

/* Lee hasta `n` bytes seguidos. Devuelve cuantos ha leido, 0 al final, o un
 * negativo de los de arriba si el medio falla. */
int32_t fat32_lee(fat32_fich_t *f, void *dst, uint32_t n);

/* Los bytes libres, contados recorriendo la FAT entera. Caro a proposito:
 * ver el comentario de arriba. Solo para enseñarlo cuando alguien lo pide. */
uint64_t fat32_libres(fat32_t *fs);

#endif /* FAT32_H */
