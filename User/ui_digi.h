#ifndef UI_DIGI_H_INCLUDED
#define UI_DIGI_H_INCLUDED

#include <stdint.h>
#include "gfx2.h"   /* gfx2_surf_t: ver ui_digi_pinta_en() */

/*
 * PANEL DE MODOS DIGITALES - etapa 20, 22/09/2026.
 *
 * El texto que sale de RTTY y de CW, con su boton de borrar y su chapa de
 * estado. Era lo ULTIMO que quedaba dibujado con gfx.c, o sea con la
 * tipografia de mapa de bits y los rectangulos planos de antes del rediseno,
 * y encima es la pantalla que mas se mira cuando estas decodificando.
 *
 * Como en el resto de gfx2, aqui no se sabe de RTTY ni de CW: llegan ocho
 * renglones ya compuestos y una chapa ya redactada. Quien decide que pone es
 * main.c, que es quien habla con los decodificadores.
 *
 * POR QUE EL TEXTO VA EN TIPOGRAFIA PROPORCIONAL
 * ----------------------------------------------
 * La version anterior dibujaba caracter a caracter en una rejilla de ancho
 * fijo, 12 px por hueco, porque la fuente de mapa de bits era de ancho fijo.
 * gfx2 no trae ninguna monoespaciada con letras -font_num_44 solo tiene
 * cifras-, y anadir una costaba unos 6 KB de flash por un detalle que nadie
 * pide: lo que se lee en este panel son indicativos y frases, no columnas.
 *
 * Asi que cada renglon se dibuja de una vez, proporcional. Sale mas legible,
 * cuesta cero flash, y de paso desaparece RTTY_TEXT_CHAR_W, que era un
 * numero copiado a mano de las tripas de gfx.c y que habria que haber
 * corregido a mano el dia que la fuente cambiara.
 */

#define UDG_ROWS 8

/*
 * Y EL TOPE, que no es lo mismo - 25/09/2026.
 *
 * UDG_ROWS son los ocho renglones del panel de RTTY/CW/NAVTEX, que es el que
 * convive con el osciloscopio de sintonia y por eso solo tiene media
 * pantalla. FT8 no lleva osciloscopio -ahi no hace falta, pedido por el
 * usuario- asi que se queda con el panel entero y le caben el doble de
 * renglones.
 *
 * El numero de renglones que se pintan lo dice ahora el que llama, en el
 * campo `filas`. Un 0 quiere decir UDG_ROWS, que es lo que habia: asi los
 * paneles que ya existian no tuvieron que tocarse ni uno.
 */
#define UDG_ROWS_MAX 24

/*
 * CABECERA DEL PANEL - 22/09/2026. Los ppm y el boton de borrar parpadeaban.
 *
 * Parpadeaban por donde estaban, no por como se dibujaban. Los dos vivian
 * ENCIMA del trazo, y spectrum_draw() repinta el trazo entero en cada cuadro:
 * cada frame los borraba y volvian a dibujarse justo despues. Eso es un
 * parpadeo por construccion, y no hay forma de quitarlo mientras sigan ahi.
 *
 * Asi que se mudan a una franja propia en lo alto del panel de texto, que
 * solo se repinta cuando cambia el texto. El trazo cede 30 px -le quedan
 * 142, de sobra para sintonizar- y a cambio el boton queda al lado del texto
 * que borra y la velocidad al lado de las letras que salen de ella.
 *
 * La geometria de los dos la decide ESTE fichero, que es quien los dibuja:
 * antes se la pasaba main.c y era otra copia de lo mismo en dos sitios.
 */
#define UDG_HDR_H   30
#define UDG_LINE_H  18
#define UDG_BTN_X   6
#define UDG_BTN_W  104
#define UDG_BTN_H   24

/*
 * SEGUNDO BOTON, a la derecha del primero - 24/09/2026.
 *
 * Lo usa SSTV para elegir en que formato se guardan las fotos en el
 * pendrive (o si no se guardan). Entre el primer boton y la chapa de
 * estado hay casi 500 px libres en los 800 de pantalla, asi que cabe sin
 * apretar a nadie.
 *
 * Y NO va del mismo ancho que el primero, que es lo que ponia aqui hasta el
 * 30/09/2026: UDG_BTN_W son 104 y UDG_BTN2_W son 140, y el comentario del
 * propio UDG_BTN2_W -diez lineas mas abajo- ya decia "mas ancho que el
 * primero" con la razon correcta. Dos frases opuestas a diez lineas de
 * distancia; se queda la que cuadra con el numero.
 */
#define UDG_BTN2_GAP  10
#define UDG_BTN2_X   (UDG_BTN_X + UDG_BTN_W + UDG_BTN2_GAP)
#define UDG_BTN2_W   140   /* mas ancho que el primero: sus rotulos son mas largos
                            * ("Fallo al guardar" son 123 px) y hay 500 px libres
                            * hasta la chapa. Lo mide sim/digi.c contra la tabla
                            * que saca tools/gen_rotulos.py de main.c. */

typedef struct {
    int16_t      y, h;              /* donde va el panel: cabecera + renglones */

    /* Los renglones, del mas viejo al mas nuevo. Un 0 es un renglon vacio. */
    const char  *fila[UDG_ROWS_MAX];
    uint8_t      filas;             /* cuantos se pintan. 0 = UDG_ROWS */

    /* Chapa de estado, a la derecha de la cabecera: a que velocidad va y si
     * el decodificador esta enganchado. */
    const char  *chip;              /* "21 ppm", "45 bd"... 0 = no dibujarla */
    uint8_t      enganchado;        /* 1 = punto de "esto tiene forma de Morse" */

    /* Boton de borrar el texto, a la izquierda de la cabecera. */
    const char  *btn;               /* rotulo, 0 = sin boton */
    uint8_t      btn_press;

    /* El segundo, a su derecha. Mismo trato: 0 = no dibujarlo. */
    const char  *btn2;
    uint8_t      btn2_press;

    /*
     * TERCER BOTON, entre la barra y la chapa - 25/09/2026.
     *
     * Los otros dos no valian: el segundo comparte sitio con la barra (ver
     * UDG_BAR_X, que es literalmente UDG_BTN2_X) y en HFDL la barra SI se
     * usa. Asi que este vive en el hueco que queda cuando la barra se acorta
     * -ver barra_w- y de ahi a la chapa.
     *
     * Solo se dibuja si hay sitio de verdad: con la barra a su ancho normal
     * no lo hay, y entonces no sale. Mas vale que no aparezca a que se monte
     * encima de la chapa.
     */
    const char  *btn3;
    uint8_t      btn3_press;


    /*
     * CUARTO BOTON, entre la barra y la chapa - 25/09/2026.
     *
     * Son dos gestos distintos y por eso son dos botones: el tercero es
     * "elige una", con su lista; este es "la siguiente", sin levantar el
     * dedo de la pantalla. Una lista de 21 frecuencias que hay que recorrer
     * entera para ver cual entra son 21 aperturas de ventana con el tercero
     * y 21 toques con este.
     *
     * Vive en la franja que va desde el final de la barra hasta donde
     * empieza la chapa, que es el sitio que sobraba. Como el tercero, si no
     * hay hueco de verdad no se dibuja: mas vale que no aparezca a que se
     * monte encima de la chapa.
     */
    const char  *btn4;
    uint8_t      btn4_press;

    /*
     * QUINTO BOTON: EL MAPA - 28/09/2026.
     *
     * Y ese hueco existe de verdad: el cuarto boton empieza en 430 y no
     * pasa de 150 px de ancho (ver UDG_BTN4_MAX_W), asi que de 590 a la
     * chapa hay 124 px sin nada, justo debajo de donde la barra de estado
     * pone BW.
     *
     * Hasta hoy el mapa se ponia y se quitaba TOCANDO LA LISTA, y el
     * comentario que lo justificaba decia que no habia hueco para un
     * boton. Lo habia: lo que no habia era haberlo medido. Un gesto sin
     * mando visible se descubre por accidente o no se descubre.
     *
     * `btn5_press` no es "lo estas pulsando" sino "el mapa esta puesto",
     * igual que las chapas de la barra de estado marcan lo que esta
     * encendido. El rotulo no cambia: un boton que dice una cosa cuando
     * esta puesto y otra cuando no obliga a pensar cual de las dos es el
     * estado y cual la accion.
     */
    const char  *btn5;
    uint8_t      btn5_press;

    /*
     * COLOR POR FILA - 27/09/2026.
     *
     * Son dos cosas distintas y por eso son dos campos:
     *
     *   fila_tinta[i]  el color de la LETRA de esa fila. Es para destacar
     *                  UNAS filas entre las demas -las CQ, que son las
     *                  unicas a las que se puede contestar-, asi que va
     *                  por fila y lo decide quien sabe que hay en ella.
     *
     *   cebra          el color del FONDO, alternando fila si y fila no.
     *                  Es para leer una TABLA de seis columnas sin
     *                  perder el renglon a mitad de camino, asi que no
     *                  depende del contenido: depende de si la fila es
     *                  par o impar y ya esta.
     *
     * Mezclarlos en un solo campo habria obligado a que quien pone las
     * CQ supiera tambien si va en fila par, que es justo lo que no tiene
     * por que saber.
     */
    uint8_t      fila_tinta[UDG_ROWS_MAX];   /* UDG_T_* ; 0 = como siempre */
    uint8_t      cebra;                      /* 1 = fondo alterno */

    /*
     * BARRA DE PROGRESO, en el sitio de los botones - 25/09/2026, para FT8.
     *
     * FT8 no decodifica al vuelo: escucha 15 segundos y suelta todo de golpe
     * al final. Catorce de esos quince segundos la pantalla no tiene nada
     * nuevo que ensenar, y eso -sin barra- se ve exactamente igual que un
     * modo que no ha arrancado. La barra es lo que diferencia "esperando" de
     * "muerto", y por eso no es adorno.
     */
    /*
     * COLUMNAS - 25/09/2026, para FT8.
     *
     * Con esto a 0 (o `cols` a 0) cada renglon se pinta entero desde el
     * margen, que es lo que hacen RTTY, CW y NAVTEX: ahi lo que llega es
     * texto corrido y no tiene columnas que cuadrar.
     *
     * Con columnas, el renglon viene con sus campos separados por TABULADOR
     * y cada uno se pinta en su x. Eso es lo unico que cuadra de verdad una
     * tabla en tipografia proporcional: alinear por caracteres no funciona
     * porque los caracteres no miden lo mismo -en font_ui_14b los digitos
     * miden todos 9 px, pero el '+' mide 11 y el '-' mide 6-.
     *
     * Una x NEGATIVA quiere decir "alineado por la derecha terminando en
     * -x". Sirve para la ultima columna, que en FT8 son kilometros y queda
     * mejor pegada al borde que colgando en mitad de la pantalla.
     */
    const int16_t *cols;            /* 0 = sin columnas: renglon corrido */
    uint8_t        ncols;

    /* Renglon de titulos, en gris y con una raya debajo. Se pinta entre la
     * cabecera de botones y los renglones, y ocupa uno de ellos. 0 = ninguno;
     * lleva sus campos separados por tabulador, igual que los renglones. */
    const char    *titulos;

    /*
     * ======================================================================
     * LA SEGUNDA DISPOSICION: LOS BOTONES A LA IZQUIERDA Y LA BARRA A LA
     * DERECHA DEL TODO - 13/10/2026
     * ======================================================================
     *
     * Se podia, y lo que lo hacia posible es que la chapa de FT8 decia
     * "8/15" -el segundo dentro de la ranura- que es EXACTAMENTE lo que ya
     * dibuja el RELLENO de la barra. Dos sitios de la cabecera contando lo
     * mismo: uno con una raya que crece y otro con un numero.
     *
     * Asi que el numero se mete dentro de la barra, la chapa se apaga, y
     * la barra se va al sitio de la chapa: a la derecha del todo y mas
     * ancha que nunca. Los botones se juntan a la izquierda:
     *
     *   la de siempre   Borrar | barra | btn3 | btn4 | btn5 | chapa
     *   esta            Borrar | btn3 | btn4 | btn5 |      barra
     *
     * NO SE PIERDE NADA por el camino: el segundo de la ranura entra en la
     * barra, "sin hora" ya lo decia la propia barra con todas las letras y
     * con el relleno a cero -que es mas de lo que decia el punto verde-, y
     * los cuatro botones siguen ahi.
     *
     * Es un campo del estado y no una regla implicita del tipo "si no hay
     * chapa, todo se mueve": una regla asi es como se consigue que un modo
     * que un dia se quede sin chapa vea su cabecera reordenarse sola sin
     * haber pedido nada. Lo piden los que lo quieren.
     *
     * Las posiciones de las dos disposiciones salen de las MISMAS
     * funciones -btn3_x(), bar_x()...- que usan el dibujo y el reparto de
     * toques, asi que no hay forma de que un boton se pinte en un sitio y
     * responda en otro. Lo mide sim/digi.c.
     */
    uint8_t      barra_dcha;

    uint8_t      barra_on;          /* 0 = sin barra (y entonces mandan los botones) */
    /* Ancho de la barra. 0 = el de siempre (300). Se acorta para hacerle
     * sitio al tercer boton, que es lo unico que hay a su derecha. */
    int16_t      barra_w;
    uint8_t      barra_v;           /* cuanto lleva */
    uint8_t      barra_max;         /* de cuanto (0 se trata como 1) */
    const char  *barra_txt;         /* rotulo dentro de la barra, 0 = ninguno */

    /*
     * LO QUE VA PEGADO AL PUNTO VERDE - 13/10/2026.
     *
     * El segundo de la ranura y el punto cuentan la misma historia -como
     * va la tanda-, y los numeros de diagnostico cuentan otra. Metidos
     * todos en el mismo renglon centrado, el "8/15" quedaba en el extremo
     * contrario al punto que lo acompana.
     *
     * Asi que la barra admite DOS textos: este se dibuja pegado a la
     * derecha, justo antes del punto, y `barra_txt` se centra en lo que
     * quede a su izquierda. 0 = no hay, y entonces `barra_txt` se centra
     * en la barra entera como siempre: los demas modos no se enteran.
     *
     * Solo se dibuja con `barra_dcha`, que es la disposicion que tiene
     * punto. Sin punto no hay nada a lo que pegarse.
     */
    const char  *barra_txt_dcha;
} ui_digi_state_t;

/* El panel de renglones, cabecera incluida. DEVUELVE 1 si de verdad lo ha
 * pintado y 0 si la banda compositora estaba cogida y no se ha dibujado
 * nada: quien tenga una bandera de "hay que repintar" no puede bajarla sin
 * mirar esto. Ver el comentario de la funcion. */
uint8_t ui_digi_draw_texto(const ui_digi_state_t *st);

/* El panel entero en una banda que trae otro, para la captura de pantalla. */
void    ui_digi_pinta_en(gfx2_surf_t *s, const ui_digi_state_t *st);
/* Solo la franja de la cabecera (botones, barra y chapa), sin tocar el
 * cuerpo. Para quien pinta el cuerpo el mismo - el mapa- y no quiere que se
 * pinte dos veces. Ver su comentario en ui_digi.c. */
void    ui_digi_draw_cabecera(const ui_digi_state_t *st);
void    ui_digi_draw_fila(const ui_digi_state_t *st, uint8_t i); /* solo uno */

/*
 * EL REPINTADO QUE SOLO TOCA LO QUE HA CAMBIADO.
 *
 * Es el que hay que usar en vez de ui_digi_draw_texto() para el repintado
 * periodico de un panel. Lleva por dentro una huella del marco y otra de
 * cada renglon: si el marco cambia repinta entero, y si no, solo los
 * renglones cuyo texto, tinta o condicion de "renglon vivo" se hayan
 * movido. Ver su comentario largo en ui_digi.c, con los accesos al bus
 * medidos.
 *
 * Devuelve 0 si no pudo pintar -la banda compositora estaba cogida-, igual
 * que ui_digi_draw_texto().
 */
uint8_t ui_digi_draw_cambios(const ui_digi_state_t *st);

/* Tira lo apuntado: el siguiente ui_digi_draw_cambios() repintara entero.
 * Lo llama quien sabe que los pixeles que hay debajo ya no son de este
 * panel: el mapa encima, un menu que se cierra, una foto de la pantalla. */
void    ui_digi_olvida(void);
void    ui_digi_draw_chip(const ui_digi_state_t *st);   /* la chapa de estado */
/* Lo que de verdad se lee en la chapa: el texto ya recortado a lo que cabe.
 * Devuelve un puntero DENTRO de st->chip, o "" si no hay chapa. Lo usa el
 * banco de la cabecera del fax, que tiene que comprobar que el estado no se
 * pierde por delante; preguntar es lo unico que vale, porque una copia del
 * recorte en el banco mide la copia. Ver chip_txt() en ui_digi.c. */
const char *ui_digi_chip_visible(const ui_digi_state_t *st);
void    ui_digi_draw_barra(const ui_digi_state_t *st);  /* solo la barra de progreso */

/* Columnas de FT8, en pixeles. Las define ui_digi.c -es quien pinta- y se
 * exportan para que main.c no tenga que repetirlas al montar los titulos. */
extern const int16_t ui_digi_cols_ft8[7];

/* Ver ui_digi.c. */
#define UI_DIGI_COLS_HFDL_N 6U
extern const int16_t ui_digi_cols_hfdl[6];
#define UI_DIGI_COLS_WSPR_N 6U
extern const int16_t ui_digi_cols_wspr[6];
#define UI_DIGI_COLS_AIS_N 6U
extern const int16_t ui_digi_cols_ais[6];
#define UI_DIGI_COLS_ALE_N 5U
extern const int16_t ui_digi_cols_ale[5];
#define UI_DIGI_COLS_JTTY_N 5U
extern const int16_t ui_digi_cols_jtty[5];

/*
 * EL IDENTIFICADOR DE TRAMAS - 02/10/2026. Tres columnas y de sobra:
 * cada cuanto se repite, que creemos que es, y con cuanta confianza.
 *
 *     "106,67 ms"    unos 80 px  /  "Periodo"  60  ->  Periodo   96
 *     "sin identificar"  ~150 px /  "Señal"    42  ->  Señal    560
 *     "98%"              ~34 px  /  "Calidad"  60
 *
 * A la del medio se le da casi toda la pantalla a proposito: ahi van los
 * nombres largos -"INMARSAT Std C", "TETRAPOL (VHF/UHF)"- y el dia que la
 * tabla crezca no quiero volver a medir columnas.
 */
#define UI_DIGI_COLS_IDENT_N 3U
extern const int16_t ui_digi_cols_ident[3];
#define UI_DIGI_COLS_FT8_N 7
void    ui_digi_draw_boton(const ui_digi_state_t *st);
uint8_t ui_digi_boton_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y);
void    ui_digi_draw_boton2(const ui_digi_state_t *st);
uint8_t ui_digi_boton2_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y);
void    ui_digi_draw_boton3(const ui_digi_state_t *st);
uint8_t ui_digi_boton3_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y);
/* Los colores que puede pedir fila_tinta[]. Son nombres y no colores para
 * que quien rellena la tabla no tenga que saber de paletas ni de temas. */
#define UDG_T_NORMAL  0U   /* la de siempre: viva abajo, apagada arriba */
#define UDG_T_LLAMA   1U   /* destacada - en FT8, las CQ */

void    ui_digi_draw_boton4(const ui_digi_state_t *st);
uint8_t ui_digi_boton4_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y);
void    ui_digi_draw_boton5(const ui_digi_state_t *st);
uint8_t ui_digi_boton5_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y);
/* Solo para el banco: ver su comentario en ui_digi.c. */
int16_t ui_digi_btn5_x_dbg(const ui_digi_state_t *st);
int16_t ui_digi_btn5_w_dbg(const ui_digi_state_t *st);
int16_t ui_digi_btn4_x_dbg(const ui_digi_state_t *st);
int16_t ui_digi_btn4_w_dbg(const ui_digi_state_t *st);
int16_t ui_digi_btn3_w_dbg(const ui_digi_state_t *st);
int16_t ui_digi_btn3_x_dbg(const ui_digi_state_t *st);
int16_t ui_digi_barra_x_dbg(const ui_digi_state_t *st);
int16_t ui_digi_barra_w_dbg(const ui_digi_state_t *st);
/* Lo ancha que es la BANDA QUE SE REPINTA al cambiar la barra, que no es
 * lo mismo que la barra: puede incluir el tercer boton. Lo que mide el
 * banco es que `barra_x + esto` no se sale de la pantalla - ver el
 * comentario de barra_ventana_w() en ui_digi.c. */
int16_t ui_digi_barra_ventana_w_dbg(const ui_digi_state_t *st);
int16_t ui_digi_chip_x_dbg(const ui_digi_state_t *st);
int16_t ui_digi_chip_w_dbg(const ui_digi_state_t *st);
uint8_t ui_digi_chip_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y);

/*
 * Y la barra, para cuando es ella la que lleva lo que llevaba la chapa.
 * En FT8 ese toque cuadra la ranura; ver ui_digi.c.
 */
uint8_t ui_digi_barra_hit(const ui_digi_state_t *st, uint16_t x, uint16_t y);

#endif /* UI_DIGI_H_INCLUDED */
