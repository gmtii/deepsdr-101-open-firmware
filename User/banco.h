#ifndef BANCO_H_INCLUDED
#define BANCO_H_INCLUDED

#include <stdint.h>

/*
 * ======================================================================
 * EL BANCO DE MEMORIAS - 26/10/2026, pedido por el usuario
 * ======================================================================
 *
 * CUATRO BANCOS DE QUINCE MEMORIAS. La rejilla de cada banco tiene
 * dieciseis botones: las quince memorias y, SIEMPRE el ultimo, el barrido.
 * Esa es la forma que se pidio y es la que manda sobre todo lo de aqui.
 *
 * QUE SE GUARDA EN UNA MEMORIA. La frecuencia, el modo, el paso de
 * sintonia y los dos cortes del filtro. O sea lo que hace falta para que
 * recuperarla te devuelva EXACTAMENTE a como estabas escuchando, y no solo
 * a la frecuencia. Lo que NO se guarda es el volumen, la ganancia ni el
 * atenuador: esos son de como esta la antena hoy, no de la emision.
 *
 * Y NO SE GUARDA UN NOMBRE, a proposito. Un nombre por memoria serian
 * ocho o doce bytes por las sesenta, y la SRAM de este chip tiene 2.236
 * libres: no es el sitio donde gastarlos. Ademas el nombre ya existe:
 * EMISORAS.BIN sabe quien emite en esa frecuencia a esta hora, que es un
 * dato MEJOR que un rotulo escrito a mano hace meses, porque cambia con la
 * temporada. La celda enseña la frecuencia, el modo y, si la base lo sabe,
 * quien es.
 *
 * DONDE VIVE. En MEM.CSV, en el disco de la flash externa, por tres
 * razones y las tres cuentan:
 *
 *   1. la flash INTERNA va con 4 kB libres de 491.520, asi que meter ahi
 *      una tabla que crece no es una opcion;
 *   2. un CSV se abre con el bloc de notas del PC, se corrige, se copia de
 *      una radio a otra y se guarda en el correo. Un formato binario no;
 *   3. y es el mismo camino que CONFIG.CSV, que lleva meses funcionando:
 *      spi_flash_write_or_update_file() y read_file_by_name().
 *
 * EN RAM son 60 x 12 = 720 bytes, que es lo que cuesta poder barrer las
 * memorias sin ir al disco en cada salto.
 */

#define BANCO_N       4U    /* cuantos bancos */
#define BANCO_MEM_N  15U    /* memorias por banco; la 16a casilla es el barrido */

/*
 * ======================================================================
 * EL NOMBRE DE CADA BANCO - 26/10/2026, pedido por el usuario
 * ======================================================================
 *
 * De serie son "Banco 1".."Banco 4", y eso NO se guarda: lo pone la
 * pantalla, traducido, cuando el fichero no dice otra cosa. Lo que se
 * guarda es el que tu le pongas, en un renglon del propio MEM.CSV:
 *
 *     nombre,1,Rusos
 *
 * Y VA EN MEM.CSV Y NO EN CONFIG.CSV, aunque se pidio alli. La razon es
 * de una linea: MEM.CSV se copia de una radio a otra -es media razon de
 * que sea un CSV- y un nombre que viviera en CONFIG.CSV no viajaria con
 * sus memorias. Llegarias a la otra radio con las frecuencias de "Rusos"
 * dentro de un banco que se sigue llamando "Banco 1". Lo que es del banco,
 * en el fichero del banco.
 *
 * EL TOPE son trece caracteres mas el cero. No es un numero redondo: es lo
 * que cabe en la columna de la izquierda (UIC_SIDE_W menos los margenes),
 * y lo mide sim/banco_pant.c con la fuente de verdad. Si el nombre que
 * escribes es mas largo se corta al cargarlo, no al dibujarlo: cortar al
 * dibujar deja medio nombre pisando la celda de al lado.
 */
#define BANCO_NOM_MAX  14U

typedef struct {
    uint32_t hz;        /* 0 = la memoria esta vacia */
    uint8_t  modo;      /* demod_mode_t */
    uint8_t  paso;      /* indice de la tabla de pasos; el FICHERO lo guarda
                         * en hercios, ver banco_pasos_pon() */
    uint16_t fil_lo;    /* los dos cortes del filtro de audio, en Hz */
    uint16_t fil_hi;
    uint16_t cw;        /* ancho del paso banda de CW en Hz, 0 = no era CW */
} banco_mem_t;

/* Pone las sesenta a cero y los cuatro nombres vacios. No toca el disco. */
void banco_init(void);

/*
 * El nombre que tiene puesto el banco `b`, o "" si no le han puesto
 * ninguno -que es lo normal: entonces la pantalla pone "Banco N".
 */
const char *banco_nombre(uint8_t b);

/* Le pone nombre. Con `s` vacio o nulo vuelve al de serie. Corta a
 * BANCO_NOM_MAX-1 caracteres. */
void banco_nombre_pon(uint8_t b, const char *s);

/* Lee una memoria. Devuelve 0 si el indice no vale o si esta vacia. */
/*
 * Le presta a este modulo la tabla de pasos de sintonia, para que el
 * fichero pueda guardar el paso EN HERCIOS en vez de su indice. Se llama
 * una vez al arrancar. Ver el bloque largo de banco.c.
 *
 * Sin llamarla, el campo del paso sale 0 y se lee 0: el banco sigue
 * funcionando, sencillamente no recuerda el paso.
 */
void banco_pasos_pon(const uint32_t *hz, uint8_t n);

/*
 * LOS NOMBRES DE SERIE DE LOS BANCOS, PARA QUE SALGAN EN EL FICHERO.
 * 28/10/2026, pedido por el usuario: *los nombres por defecto tienen que
 * salir en el CSV, si no como va a saber el usuario que los tiene que
 * cambiar ahi.*
 *
 * Y tenia razon: hasta hoy solo se escribia un renglon `nombre,` para los
 * bancos a los que YA se les habia puesto uno, o sea que en un fichero
 * recien hecho no salia ninguno. Quien lo abriera no tenia como saber que
 * eso se toca ahi.
 *
 * AHORA SALEN LOS CUATRO SIEMPRE, y los que no ha tocado nadie salen
 * COMENTADOS con una almohadilla y con el de serie escrito. Asi se ven,
 * se entiende que se pueden cambiar, y no cambian nada mientras no se les
 * quite la almohadilla.
 *
 * Y COMENTADOS, NO COMO RENGLONES DE VERDAD, por una razon concreta: los
 * nombres de serie estan TRADUCIDOS -"Aviones" / "Aircraft"-. Escribirlos
 * como renglones vivos los congelaria en el idioma que hubiera puesto el
 * dia que se grabo el fichero, y quien cambiara la radio a ingles seguiria
 * viendo "Aviones" sin entender por que.
 *
 * Los nombres los sabe main.c, que es quien tiene la tabla y el idioma, no
 * este fichero. `pista` es el renglon de ayuda que va encima, tambien
 * traducido. Los dos punteros tienen que vivir mientras viva el programa:
 * aqui se guardan tal cual, no se copian.
 */
void banco_nombres_pon(const char *const *serie, uint8_t n, const char *pista);

uint8_t banco_lee(uint8_t banco, uint8_t i, banco_mem_t *out);

/* Guarda una memoria en su sitio. Devuelve 0 si el indice no vale. */
uint8_t banco_graba(uint8_t banco, uint8_t i, const banco_mem_t *m);

/* Vacia una memoria. */
void banco_borra(uint8_t banco, uint8_t i);

/* Cuantas memorias usadas tiene un banco. Lo necesita el barrido para
 * saber si hay algo que barrer antes de ponerse. */
uint8_t banco_usadas(uint8_t banco);

/*
 * La siguiente memoria USADA despues de 'desde', dando la vuelta. Devuelve
 * 0xFF si el banco esta vacio del todo. Es lo que mueve el barrido, y vive
 * aqui y no en main.c para que el banco pueda medirse solo.
 */
uint8_t banco_siguiente(uint8_t banco, uint8_t desde);

/*
 * EL FICHERO. Las dos se parecen a settings_load()/settings_save_now() a
 * proposito: el mismo camino, el mismo formato y los mismos fallos ya
 * resueltos (los finales de linea de Windows, el fichero que no existe
 * todavia, el que viene a medias).
 */
/*
 * El buffer lo pone QUIEN LLAMA. El porque esta en banco.c, delante de
 * banco_carga(): resumido, ni cabe uno propio ni vale el de la union
 * compartida, porque esa la reparten FT8 y HFDL y grabar una memoria tiene
 * que poder hacerse en cualquier modo.
 */
#define BANCO_CSV_MIN  2560U
uint8_t banco_carga(char *buf, uint32_t cap);   /* 0 si no habia o estaba roto */
uint8_t banco_guarda(char *buf, uint32_t cap);  /* 0 si no se pudo escribir */

/*
 * Para el banco de pruebas: el CSV que se escribiria, sin tocar la flash.
 * Devuelve cuantos bytes ha puesto. Asi sim/ puede comprobar el formato y
 * el ida y vuelta sin un disco de mentira.
 */
uint32_t banco_a_csv(char *buf, uint32_t cap);
uint8_t  banco_de_csv(const char *buf, uint32_t len);

#endif
