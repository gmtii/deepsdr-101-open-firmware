#ifndef MAPA_ETI_H
#define MAPA_ETI_H

#include <stdint.h>
#include "gfx2.h"
#include "mapa.h"

/*
 * ======================================================================
 * LOS INDICATIVOS EN EL MAPA, EL RAYO DESDE TU QTH Y LA FICHA DE ABAJO
 * ======================================================================
 *
 * Pedido por el usuario.
 *
 * El mapa ya ponia el PUNTO de cada estacion desde el 28/09. Lo que
 * faltaba es lo que convierte un punto en una estacion: COMO SE LLAMA, POR
 * DONDE VA EL SALTO, y -al tocarla- los cuatro numeros con los que se
 * decide si vale la pena llamarla.
 *
 * TRES DECISIONES, Y LAS TRES SE PUEDEN DISCUTIR MIRANDO EL BANCO:
 *
 * 1) SOLO LOS CQ. Un mapa con el indicativo de todo lo que entra se llena
 *    de texto en dos ranuras -en una banda abierta salen doce o quince
 *    mensajes cada quince segundos- y encima la mitad son respuestas a
 *    otros, que no son a quien puedes llamar tu. "CQ" es exactamente la
 *    lista de los que estan pidiendo contacto. Lo pidio asi el usuario y
 *    ademas es lo unico que cabe.
 *
 * 2) EL TOPE SUBE CON EL ZOOM: 4 + la escala, o sea 5 a escala 1 y 12 a
 *    escala 8. No es un numero redondo porque no es un gusto: a escala 1
 *    el mundo entero mide 800 px y cada etiqueta se lleva unos 60, asi que
 *    cinco ya son 300 px de texto encima del dibujo; a escala 8 el mismo
 *    trozo de mundo ocupa ocho veces mas y el texto cabe sin taparse.
 *    Cuantas cabian se midio dibujandolas, no estimando: ver
 *    sim/mapa_eti.c.
 *
 * 3) EL QUE LLEGA TARDE SE CALLA. Las etiquetas se ofrecen del MAS NUEVO
 *    al mas viejo y la que choca con una ya puesta no se pinta. Asi el que
 *    acaba de llamar siempre se lee, que es lo que uno esta mirando, y los
 *    choques se los come el de hace diez minutos. La alternativa -pintar
 *    todas y que se solapen- no es "mas informacion": dos indicativos
 *    encima el uno del otro no son dos indicativos, son un borron.
 *
 * Y LA MISMA ESTACION NO SALE DOS VECES. En FT8 el que llama repite su CQ
 * cada minuto, asi que en la lista de dieciseis mensajes el mismo
 * indicativo puede estar seis veces. Se cuenta -y ese numero sale en la
 * ficha, "x6", que es justo lo que dice si esta ahi de verdad o fue un
 * rebote- pero etiqueta hay una.
 *
 * EL RAYO ES UNA ORTODROMICA DE VERDAD, no una raya entre dos puntos. En
 * un mapa plano la raya recta NO es el camino: Madrid-Tokio por la raya
 * pasa por el Himalaya y de verdad pasa por el Artico. Una raya que no es
 * el camino es decoracion, y encima engaña sobre por donde hay que apuntar
 * la antena. Se parte en MAPA_ETI_PTS puntos sobre la esfera y se proyecta
 * punto a punto.
 *
 * LO QUE CUESTA, medido: MAPA_ETI_RAYOS arcos de MAPA_ETI_PTS puntos a
 * cuatro bytes son 408 bytes de SRAM, mas la lista de etiquetas. No se
 * guarda el arco en grados ademas de en pixeles: se recalcula entero
 * cuando el mapa se repinta, que es cuando algo ha cambiado -no hay
 * repintado continuo-, y asi la cuenta de la esfera vive en un sitio y no
 * en dos.
 */

#define MAPA_ETI_MAX    12U   /* el tope mas alto: 4 + MAPA_ESC_MAX */
#define MAPA_ETI_RAYOS   6U   /* cuantas llevan rayo desde el QTH */
#define MAPA_ETI_PTS    17U   /* puntos del arco: 16 tramos */
#define MAPA_ETI_CALL   12U   /* indicativo + el cero */
#define MAPA_ETI_LOC     8U

/* Lo que hace falta saber de una estacion para ponerla en el mapa. */
typedef struct {
    char     call[MAPA_ETI_CALL];
    char     loc[MAPA_ETI_LOC];
    int16_t  snr;        /* dB, como lo da el modo */
    uint16_t minuto;     /* la hora del mensaje en minutos UTC (h*60+m) */

    /*
     * Y LA POSICION A PELO, PARA QUIEN NO TIENE LOCALIZADOR - 13/10/2026.
     *
     * Pedido por el usuario.
     *
     * Los aficionados mandan localizador Maidenhead; un avion manda
     * latitud y longitud. Hasta hoy esto solo sabia de localizadores, asi
     * que un avion no podia tener etiqueta.
     *
     * Con `hay_pos` a 1 se usan `lon_c` y `lat_c` -centesimas de grado,
     * que es lo que maneja mapa.c- y `loc` pasa a ser SOLO para la ficha:
     * puede venir vacio y entonces no se escribe.
     *
     * Con `hay_pos` a 0 todo sigue exactamente como estaba: la posicion
     * sale del localizador. Por eso FT8, WSPR y JTTY no se enteran.
     */
    uint8_t  hay_pos;
    int32_t  lon_c, lat_c;

    /*
     * 1 = esto es un avion y no una estacion de radioaficionado.
     *
     * Cambia DOS cosas de la ficha y ninguna del dibujo: no se escribe
     * "DX" delante -un avion no es un DX- y NO SE BUSCA EL PAIS. Eso
     * ultimo no es cosmetico: dxcc_pais() mira el prefijo, y "UAL123"
     * -United Airlines- empieza por "UA", que en la tabla es Ucrania. Un
     * pais equivocado con cara de dato es peor que ningun pais.
     */
    uint8_t  avion;
} mapa_eti_dx_t;

/*
 * EL CICLO DE UN FOTOGRAMA, y el orden importa:
 *
 *   mapa_eti_empieza(caja, qth)   vacia la lista y apunta de donde salen
 *                                 los rayos ("" o 0 = no hay QTH, no hay
 *                                 rayos, pero las etiquetas siguen)
 *   mapa_eti_pon(dx)              una por mensaje, DEL MAS NUEVO AL MAS
 *                                 VIEJO. Devuelve 1 si ha entrado.
 *   mapa_eti_cierra()             lo que hay que hacer cuando ya estan
 *                                 todas: soltar la seleccion si el que
 *                                 estaba elegido ya no se oye.
 *
 * Se llama UNA VEZ por fotograma y FUERA de gfx2_render(), igual que
 * mapa_ventana(): dentro correria dieciocho veces, una por banda.
 */
void    mapa_eti_empieza(const mapa_caja_t *c, const char *qth);
uint8_t mapa_eti_pon(const mapa_eti_dx_t *dx);
void    mapa_eti_cierra(void);

/* Cuantas etiquetas han entrado, y el tope que les toca a esta escala. */
uint8_t mapa_eti_n(void);
uint8_t mapa_eti_tope(const mapa_caja_t *c);

/* Dibuja los rayos y las etiquetas. Va DENTRO de gfx2_render(), una vez
 * por banda, como todo lo demas del mapa. */
void    mapa_eti_dibuja(gfx2_surf_t *s);

/*
 * LA FICHA DE ABAJO, con los datos del que se haya tocado. No pinta nada
 * si no hay ninguno elegido. `ahora` es la hora de la radio en minutos
 * UTC, para poder decir hace cuanto se oyo.
 *
 * Va a la derecha de los dos mandos del zoom y no encima: el sitio de
 * abajo a la izquierda ya estaba cogido, y mover los mandos para hacerle
 * hueco a la ficha cambiaria donde responden al toque.
 */
void    mapa_eti_ficha(gfx2_surf_t *s, uint16_t ahora);

/*
 * Y EL TEXTO DE ESA FICHA, SIN PINTARLO. Devuelve cuantos caracteres ha
 * escrito, o 0 si no hay nadie elegido.
 *
 * Existe porque lo que hay que comprobar de la ficha son los NUMEROS -los
 * kilometros, el azimut, el "x4", la vuelta de la medianoche- y eso en una
 * pantalla pintada no se puede mirar sin ponerse a leer pixeles. Un banco
 * que lee pixeles para comprobar una resta mide el dibujo, no la cuenta.
 * La pinta mapa_eti_ficha() llamando a esta: una sola copia del texto.
 */
uint8_t mapa_eti_texto(char *buf, uint8_t tope, uint16_t ahora);

/* Los pixeles de ancho que tiene la ficha para la letra, por la misma
 * cuenta que usa el dibujo. Para el banco: ver sim/mapa_eti.c. */
int16_t mapa_eti_ficha_hueco_dbg(void);

/* La caja de la chapa de la etiqueta i, tal y como quedo colocada, y el
 * indicativo que lleva. Para el banco, que comprueba que dos estaciones
 * del MISMO cuadrado acaban con sus chapas en sitios distintos. */
void        mapa_eti_caja_dbg(uint8_t i, int16_t *x, int16_t *y, int16_t *w);
const char *mapa_eti_call_dbg(uint8_t i);

/* Lo que se guarda en el arco cuando un punto cae fuera del mapa (mas
 * alla de +-71,5 grados). El dibujo salta ese tramo; ver mapa_eti.c. */
#define MAPA_ETI_FUERA ((int16_t)-32768)

/*
 * EL TOQUE. Devuelve 1 si ha cambiado la seleccion (y por tanto hay que
 * repintar). Tocar encima de una etiqueta o de su marca la elige; tocar
 * en cualquier otro sitio del mapa la suelta.
 *
 * LA SELECCION SE GUARDA POR INDICATIVO Y NO POR INDICE. La lista se
 * rehace entera en cada fotograma y el orden cambia en cuanto llega un
 * mensaje nuevo: con un indice, el que tocaste se convertiria en otro
 * solo.
 */
uint8_t mapa_eti_toca(int16_t x, int16_t y);
const char *mapa_eti_sel(void);
void    mapa_eti_suelta(void);

#endif /* MAPA_ETI_H */
