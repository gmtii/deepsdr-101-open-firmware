#ifndef MAPA_H
#define MAPA_H

#include <stdint.h>
#include "gfx2.h"

/*
 * EL MAPA DEL MUNDO, Y DONDE ESTA LO QUE SE HA CAZADO - 28/09/2026.
 *
 * Pedido por el usuario.
 *
 * Una lista de indicativos con su localizador contesta "quien" pero no
 * contesta "por donde se oye hoy", que es la pregunta de verdad cuando
 * uno mira una banda: si lo que entra es Europa, si ha abierto el
 * Atlantico, si asoma Japon por el paso gris. Eso en una tabla no se ve y
 * en un mapa se ve de un vistazo.
 *
 * LA PROYECCION ES PLANA (equirectangular) y no es una decision estetica:
 * el localizador Maidenhead ES una rejilla de longitud y latitud, asi que
 * en un mapa plano un cuadrado de localizador es un rectangulo de
 * pixeles, y pasar de uno a otro son dos multiplicaciones. Cualquier otra
 * proyeccion pediria trigonometria por punto para no ganar nada: a esta
 * escala, lo que se mira es "por donde", no "a cuantos kilometros".
 *
 * RECORTADO A +-71,5 GRADOS DE LATITUD en la TABLA. Lo que se pierde es
 * la Antartida y el alto Artico, que en HF no tienen a nadie.
 *
 * Y LO QUE SE VE ES MENOS QUE ESO - 28/09/2026.
 *
 * Pedido por el usuario.
 *
 * Las dejaba porque la proporcion manda: 360 grados por 143 son 2,517 a 1,
 * y en el hueco de 800x288 el mapa mas grande que cabe entero mide 724x288
 * - de ahi los 38 px de margen a cada lado-.
 *
 * Ahora el mapa mide 800 de ancho SIEMPRE y lo que sobra por abajo y por
 * arriba se sale del hueco: a escala 1 el mundo mide 800x317 y se ven 288,
 * o sea +-64,8 grados centrados. No se ha estirado nada -Groenlandia sigue
 * donde esta- y los 29 px que faltan no se han tirado: estan ahi y se
 * llega a ellos arrastrando. El norte de Escandinavia no desaparece, hay
 * que ir a buscarlo.
 *
 * NO GASTA NI UN BYTE DE SRAM. La costa vive en la flash (7,1 kB) y se
 * dibuja directamente sobre la banda que gfx2 esta componiendo. En esta
 * radio eso no era una virtud abstracta: cuando se escribio esto quedaban
 * 308 bytes libres.
 */

/*
 * La tabla de la costa. La genera tools/mapa_pack.py; ver su cabecera.
 *
 * EL NOMBRE "MAPA_DESVAN" YA NO QUIERE DECIR NADA - 02/10/2026.
 *
 * Aqui habia tres parrafos sobre el desvan: la region de 64 kB por encima
 * de la firma del gestor de fabrica, a la que las tablas gordas subian una
 * a una porque el enlazador no derrama de una region a otra. Esta tabla
 * llego a estar MAL puesta -en ".arriba", que era el nombre de la seccion
 * de salida y no de entrada- y sus 7.262 bytes se quedaban fuera de la
 * cuenta que enseñaba Informacion.
 *
 * En la rama "reload" no hay desvan: la flash es una sola region y todo el
 * .rodata cae en el mismo sitio sin que nadie elija nada. La macro se
 * queda -quitarla obligaria a tocar mapa_datos.c, que lo genera un guion-
 * pero no hace nada especial: ".rodata.k_mapa_pts" lo recoge el
 * "*(.rodata*)" de siempre.
 *
 * Si algun dia hubiera que volver a partir la flash, el historial de esto
 * esta en la rama "deepsdr".
 */
#ifndef MAPA_DESVAN
#define MAPA_DESVAN __attribute__((section(".rodata.k_mapa_pts")))
#endif

extern const int16_t  k_mapa_pts[];
extern const uint16_t k_mapa_n_lineas;
extern const uint16_t k_mapa_n_puntos;
extern const int16_t  k_mapa_lat_tope_c;

/*
 * EL HUECO, Y QUE TROZO DEL MUNDO SE VE EN EL.
 *
 * Pedido por el usuario.
 *
 * `x,y,w,h` son el hueco en pantalla y no cambian. Lo que cambia es el
 * MUNDO que se mete dentro: a escala `esc` mide `w*esc` de ancho -y el
 * alto sale de la proporcion, no se elige-, y de ese mundo se ve el trozo
 * que empieza en `off_x, off_y`.
 *
 * Asi el zoom y el arrastre son la misma cuenta y no hay dos caminos de
 * proyeccion que puedan discrepar: todo lo que se pinta en el mapa pasa
 * por px_de_lon/py_de_lat, y esos dos ya saben de escala y de
 * desplazamiento.
 */
typedef struct {
    int16_t x, y, w, h;    /* el hueco en pantalla */
    uint8_t esc;           /* 1, 2, 4, 8 - ver MAPA_ESC_MAX */
    int16_t off_x, off_y;  /* que esquina del mundo cae en (x,y) */
} mapa_caja_t;

#define MAPA_ESC_MAX 8U

/*
 * LOS COLORES DEL MAPA, EN UN SOLO SITIO.
 *
 * Estaba dentro de mapa.c porque solo lo usaba mapa.c. Sale aqui desde que
 * mapa_eti.c pinta encima del mismo mapa: dos tablas de colores del mismo
 * dibujo es exactamente como se consigue que el rotulo sea de un tono y la
 * costa de otro que no pega, y ademas hay que decidir UNA vez si manda el
 * tema o el .map (ver mapa_tinte()).
 */
typedef struct {
    uint32_t mar, rejilla, ecuador, costa;
    /* EL COLOR DE LA TIERRA - 14/10/2026. Llevaba en la paleta de los cien
     * mapas desde el primer dia (MAPA_COL_TIERRA, hueco 1) y no lo leia
     * nadie, porque la radio no rellenaba. Ver el relleno en mapa.c. */
    uint32_t tierra;
    /* LAS FRONTERAS, APARTE DE LA COSTA - 12/10/2026. Pedido por el
     * usuario, que veia las fronteras de los paises igual de gruesas que
     * la costa. No eran del mismo grosor: eran del mismo COLOR, que en
     * una linea de un pixel es lo mismo de lejos. */
    uint32_t frontera;
    uint32_t boton, borde, tinta, tinta_off, casa;
    /* El indicativo y el rayo desde el QTH - 12/10/2026. Ver mapa_eti.h. */
    uint32_t rotulo, linea;
} mapa_tinte_t;

void mapa_tinte(mapa_tinte_t *t);

/* El mundo entero a la escala que lleve la caja, en pixeles. El alto NO se
 * elige: sale de los 360x143 grados. */
/*
 * PONE LA VENTANA DEL MAPA PARA ESTE FOTOGRAMA. Se llama UNA VEZ, antes
 * de gfx2_render(), NUNCA desde mapa_dibuja() - que corre una vez por
 * banda, dieciocho por fotograma-. Ver el comentario en mapa.c.
 */
uint8_t mapa_ventana(const mapa_caja_t *c);

int32_t mapa_mundo_w(const mapa_caja_t *c);
int32_t mapa_mundo_h(const mapa_caja_t *c);

/*
 * De un localizador Maidenhead al pixel de su CENTRO. Admite 4 o 6
 * caracteres ("IN80", "JO20HI") y devuelve 0 si no lo entiende - que es
 * lo que hay que hacer con un localizador que vino de un decodificado
 * dudoso, en vez de pintar un punto en mitad del oceano.
 */
uint8_t mapa_loc_a_px(const mapa_caja_t *c, const char *loc,
                      int16_t *px, int16_t *py);

/*
 * Y EL MISMO LOCALIZADOR EN GRADOS, sin pantalla de por medio: en
 * centesimas de grado, el centro del cuadrado, sin recortar la latitud.
 * Lo usa mapa_eti.c para la distancia y el azimut, que no son dibujo.
 * Un solo parser de Maidenhead en todo el firmware; ver mapa.c.
 */
uint8_t mapa_loc_a_cent(const char *loc, int32_t *lon_c, int32_t *lat_c);

/* Y de grados a pixel, para lo que ya viene en grados. Devuelve 0 si cae
 * fuera del recorte. */
uint8_t mapa_grados_a_px(const mapa_caja_t *c, float lon, float lat,
                         int16_t *px, int16_t *py);

/*
 * Lo mismo pero en grados por DIEZ MIL, que es como dan la posicion los
 * aviones de HFDL y los barcos de AIS. Va aparte y con enteros a
 * proposito: es un camino de dibujo que puede recorrer decenas de
 * posiciones por cuadro, y ahi la coma flotante no pinta nada.
 */
uint8_t mapa_e4_a_px(const mapa_caja_t *c, int32_t lon_e4, int32_t lat_e4,
                     int16_t *px, int16_t *py);

/*
 * PONE EL MAPA EN EL HUECO, a escala 1 y centrado.
 *
 * Antes esto se llamaba mapa_encaja() y hacia lo contrario: encogia el
 * mapa hasta que cabia entero, de donde salian las franjas laterales. Se
 * ha cambiado de nombre a proposito - lo que hace ya no es encajar- para
 * que nadie lo llame esperando lo de antes.
 *
 * La proporcion sigue sin ser negociable: 360 grados de longitud por 143
 * de latitud son 2,517 a 1, y estirar el mapa para llenar el hueco pondria
 * Groenlandia donde no esta. Lo que ha cambiado es por donde se recorta:
 * antes sobraba ancho, ahora sobra alto y el alto que sobra se puede ir a
 * ver arrastrando.
 */
void mapa_pon(mapa_caja_t *c, int16_t x, int16_t y, int16_t w, int16_t h);

/*
 * ZOOM, DEJANDO QUIETO EL PUNTO (ax, ay) DE LA PANTALLA.
 *
 * El ancla importa: si el zoom entrara siempre por el centro, acercarse a
 * una marca que esta en una esquina la echaria fuera y habria que ir a
 * buscarla. Con ancla, lo que estabas mirando se queda donde estaba.
 *
 * `esc` se recorta a [1, MAPA_ESC_MAX]. Devuelve la escala que ha quedado.
 */
uint8_t mapa_zoom(mapa_caja_t *c, uint8_t esc, int16_t ax, int16_t ay);

/*
 * UN PASO DE ZOOM, EN POTENCIAS DE DOS. 11/10/2026.
 *
 * Pedido por el usuario.
 *
 * Nace de ahi, pero vive AQUI y no en main.c a proposito: con el mando y
 * los botones de la esquina hay ya DOS caminos al mismo zoom, y una cuenta
 * de "multiplica por dos y topa" copiada en los dos es como se consigue que
 * acaben llegando a escalas distintas. La escala es del mapa, asi que
 * moverla un paso tambien.
 *
 * `pasos` positivo acerca y negativo aleja; se topa en [1, MAPA_ESC_MAX] y
 * lo que sobre NO se acumula, para que el primer giro hacia el otro lado se
 * note en seguida. El ancla es la misma que en mapa_zoom(). Devuelve la
 * escala que ha quedado.
 */
uint8_t mapa_zoom_paso(mapa_caja_t *c, int32_t pasos, int16_t ax, int16_t ay);

/*
 * ARRASTRA el trozo visible `dx, dy` pixeles de pantalla. El sentido es el
 * del dedo: arrastrar a la derecha trae lo que habia a la izquierda.
 *
 * Se recorta a los bordes del mundo y NO se da la vuelta por el
 * antimeridiano. Darla seria mas bonito -la Tierra es un cilindro- pero
 * obligaria a pintar cada trazo de costa dos veces para que cruzara el
 * borde, y eso es bastante codigo para algo que se hace con el dedo.
 */
void mapa_arrastra(mapa_caja_t *c, int16_t dx, int16_t dy);

/*
 * ======================================================================
 * ARRASTRAR O TOCAR, QUE NO ES LO MISMO - 13/10/2026
 * ======================================================================
 *
 * Pedido por el usuario.
 *
 * Y no pasaba. El reparto del gesto vivia suelto en main.c y tenia DOS
 * fallos que un banco habria cogido en diez segundos, si hubiera habido
 * donde ponerlo:
 *
 * 1) LAS COORDENADAS DE LA SUELTA SON (0,0). touch_read() devuelve 0
 *    cuando no hay dedo y NO ESCRIBE x ni y; quien llama las tiene
 *    puestas a cero al entrar. O sea que el toque se resolvia contra la
 *    esquina de arriba a la izquierda de la pantalla, donde no hay
 *    ningun indicativo ni lo va a haber. El sitio bueno es el ULTIMO en
 *    el que hubo dedo, que es cosa de quien sigue el gesto.
 *
 * 2) DOS PIXELES NO SEPARAN UN TOQUE DE UN ARRASTRE. El umbral de dos
 *    pixeles esta para que el mapa no vibre con el dedo quieto, y vale
 *    para eso; usarlo ademas para decidir "esto ha sido un arrastre"
 *    convierte cualquier temblor del panel resistivo en un arrastre, y
 *    entonces el toque no llega nunca.
 *
 * Asi que el reparto se saca aqui, donde el simulador lo puede compilar
 * y medir - incluida la suelta en (0,0), que es justo lo que paso-. En
 * main.c queda una llamada por evento y ninguna decision.
 */
#define MAPA_GESTO_ARRASTRA  2    /* a partir de aqui el mapa se mueve */
/*
 * Y por debajo de ESTE recorrido total, al soltar, fue un toque.
 *
 * Eran 8 px y el usuario los midio con el dedo - 13/10/2026: en el mapa
 * costaba dar a los indicativos y sacar su ficha abajo. Ocho
 * pixeles es menos de lo que se mueve un dedo en un panel resistivo
 * mientras aprieta: la presion cambia, la lectura se va, y el gesto
 * acababa contando como arrastre. Veinte son el 2,5 % del ancho del mapa
 * -o sea, "no se ha movido" a la vista- y dejan sitio al temblor.
 */
#define MAPA_GESTO_TOQUE    20

typedef struct {
    int16_t x0, y0;    /* donde empezo */
    int16_t x, y;      /* el ultimo sitio con dedo */
} mapa_gesto_t;

void    mapa_gesto_empieza(mapa_gesto_t *g, int16_t x, int16_t y);

/*
 * Dedo que sigue puesto. Devuelve 1 si el mapa tiene que moverse, y
 * entonces deja en dx/dy cuanto. Devuelve 0 mientras el movimiento no
 * pase del temblor.
 */
uint8_t mapa_gesto_mueve(mapa_gesto_t *g, int16_t x, int16_t y,
                         int16_t *dx, int16_t *dy);

/*
 * Dedo levantado. Devuelve 1 si el gesto ha sido UN TOQUE, y entonces
 * deja en tx/ty donde.
 *
 * NO recibe las coordenadas de la suelta A PROPOSITO: esas son (0,0).
 * Ver el comentario de arriba.
 */
uint8_t mapa_gesto_suelta(const mapa_gesto_t *g, int16_t *tx, int16_t *ty);

/*
 * SACA EL LOCALIZADOR DE UN RENGLON DEL PANEL DE FT8.
 *
 * El renglon viene con sus campos separados por tabulador
 * ("Hora\tHz\tdB\tDT\tMensaje\tkm\tPais") y el localizador, si lo hay,
 * es la ultima palabra del mensaje. Copia hasta 6 caracteres mas el cero
 * en `out` y devuelve 1; devuelve 0 si ese renglon no lleva localizador.
 *
 * Y AQUI ESTA LA TRAMPA QUE LLENA EL MAPA DE PUNTOS FALSOS: `RR73` tiene
 * exactamente la forma de un localizador -dos letras de la A a la R y dos
 * digitos- y es como TERMINA casi todos los contactos de FT8. Si se
 * aceptara, cada QSO acabado pondria una marca en el Pacifico, al noreste
 * de Fiyi, y el mapa acabaria con un racimo de estaciones que no existen
 * justo donde no hay nadie. Igual pasa con `RR72` y `RR71`, que tambien se
 * usan. Van fuera por nombre, que es lo unico que se puede hacer: por la
 * forma no se distinguen.
 */
uint8_t mapa_loc_de_linea(const char *linea, char *out);

/*
 * Y EL INDICATIVO DE QUIEN LLAMA, del mismo renglon de FT8: la palabra
 * anterior al localizador. Copia hasta once caracteres mas el cero y
 * devuelve 1; devuelve 0 si ese renglon no trae un CQ con indicativo y
 * localizador. Ver mapa.c para que cuenta como indicativo y por que hace
 * falta decirlo.
 */
uint8_t mapa_ind_de_linea(const char *linea, char *out);

/*
 * LO MISMO, PERO BUSCANDO EN TODAS LAS PALABRAS DE UN CAMPO - 28/09/2026.
 *
 * Pedido por el usuario.
 *
 * Y lo tiene, pero no sirve la funcion de arriba. En FT8 el localizador, si
 * lo hay, es SIEMPRE la ultima palabra del mensaje, y por eso aquella mira
 * solo esa. En JTTY un mensaje son varios atomos pegados -"CQ K1ABC CQ
 * FN42 599 123"- y el localizador viene en SU PROPIO atomo, que puede caer
 * en cualquier sitio. Mirar solo la ultima palabra dejaria el mapa vacio
 * justo en los mensajes que si dicen donde esta el otro.
 *
 * Asi que esta recorre TODAS las palabras del campo `campo` (contando
 * desde 1) y devuelve la primera que sea un localizador de verdad, con las
 * mismas reglas y el mismo guarda de RR73 que la de arriba.
 *
 * Y por eso la de FT8 no se cambia por esta: en FT8, "buscar en todas"
 * aceptaria como localizador cualquier palabra de cuatro con forma de
 * localizador que apareciera antes del final, y el mensaje de FT8 tiene de
 * eso. La regla de cada modo es la suya.
 */
uint8_t mapa_loc_de_campo(const char *linea, uint8_t campo, char *out);

/* Pinta el fondo, la rejilla y la costa. Se llama desde un callback de
 * gfx2_render(), una vez por banda. */
void mapa_dibuja(gfx2_surf_t *s, const mapa_caja_t *c);

/*
 * Una marca en el mapa. `viva` la pinta rellena y con halo -lo que acaba
 * de entrar- y a 0 la pinta hueca, que es como se distingue de un vistazo
 * lo de hace un minuto de lo de hace media hora.
 */
void mapa_marca(gfx2_surf_t *s, const mapa_caja_t *c,
                int16_t px, int16_t py, uint8_t viva, gfx2_rgba_t color);

/*
 * LOS DOS MANDOS DEL ZOOM, abajo a la izquierda y ENCIMA del mapa.
 *
 * Pedido por el usuario.
 *
 * Viven aqui y no en main.c aunque sea main.c quien los pone: main.c no lo
 * puede compilar el simulador, asi que un boton dibujado alli no se puede
 * ni mirar ni medir sin la placa delante. Y ademas el dibujo y el reparto
 * de toques usan la MISMA geometria - que es de aqui-, con lo que no hay
 * dos copias que puedan separarse.
 *
 * Van sobre el mapa y no en la cabecera porque el zoom es del mapa: un
 * mando lejos de lo que manda obliga a mirar dos sitios. Y con fondo
 * opaco debajo, porque sobre el mar oscuro se leerian pero sobre una costa
 * clara o un racimo de marcas, no.
 */
#define MAPA_MZ_LADO 40

void mapa_zoom_botones(gfx2_surf_t *s, const mapa_caja_t *c);

/* Que mando se ha tocado: 0 ninguno, 1 el de acercar, 2 el de alejar. */
uint8_t mapa_zoom_hit(const mapa_caja_t *c, int16_t x, int16_t y);

/* Nuestra posicion, con otra forma: un aspa. Sin ella el mapa no dice de
 * donde salen los saltos. */
void mapa_casa(gfx2_surf_t *s, const mapa_caja_t *c, int16_t px, int16_t py);

#endif /* MAPA_H */
