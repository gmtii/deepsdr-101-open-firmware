#ifndef SPECTRUM_H
#define SPECTRUM_H

#include <stdint.h>
#include "gfx2.h"   /* gfx2_surf_t: ver spectrum_pinta_en() */

/*
 * El dibujado de la traza del espectro y el mapa de color dB -> RGB565,
 * que se comparte: la misma paleta pinta el espectro en vivo y las filas
 * de la cascada, para que la escala se lea igual en los dos sitios.
 *
 * COMO ESTA PENSADO EL COSTE, que esto es el camino caliente de toda la
 * interfaz. La primera version llamaba a una funcion de color en coma
 * flotante -con divisiones y bifurcaciones- una vez POR PIXEL ENCENDIDO,
 * hasta unas 80.000 llamadas por cuadro, y eso se comia el tiempo de
 * cuadro un orden de magnitud por encima de lo que costaba escribir los
 * pixeles por el EXMC. Esta version saca TODA la coma flotante del bucle
 * de pixeles:
 *
 *   - La paleta es una tabla de 256 entradas en RGB565 que monta una
 *     sola vez spectrum_init().
 *   - Una vez por cuadro, los dB se pliegan a un valor por COLUMNA de
 *     pantalla -promediando todos los bins de la FFT que caen en esa
 *     columna, que es antialias barato y mata el centelleo del vecino
 *     mas proximo-, se suavizan y se cuantizan a alturas enteras.
 *   - Una vez por cuadro se precalcula un color por FILA para el
 *     degradado vertical.
 *   - Y el bucle de dentro se queda en una comparacion entera y una
 *     escritura de 16 bits en una tira de fila que se manda con
 *     gfx_blit() una vez por fila.
 *
 * LO QUE SE VE:
 *   - Relleno en degradado vertical: cada barra coloreada por su altura
 *     a traves de la paleta, el aspecto clasico de mapa de calor.
 *   - Suavizado exponencial asimetrico por columna: ataque rapido,
 *     caida lenta. Las señales saltan al instante y el suelo se calma.
 *   - Marcas de pico que se van apagando, opcionales.
 *   - Una rejilla de referencia FIJA, tenue por debajo de la traza:
 *     lineas horizontales en fracciones iguales de la altura del panel y
 *     verticales en las columnas del cuarto, la mitad y los tres cuartos
 *     -las mismas posiciones que las marcas de spec_span_labels_draw(),
 *     asi que la rejilla cuadra con los rotulos de frecuencia de debajo-.
 *     Fija en PIXELES, NO atada a la escala de dB ni al tramo de
 *     frecuencia que haya puesto; por que esa distincion importa esta en
 *     el comentario de SPECTRUM_GRID_ROWS/COLS.
 *   - Un pixel de traza brillante encima de cada barra.
 */

/*
 * Cuantas lineas lleva la rejilla de referencia. Poniendo cualquiera de
 * los dos a 0 se quita ese eje.
 *
 * *** 08/09/2026: se paso de una rejilla en dB a una rejilla fija en
 * PIXELES ***, pedido por el usuario. Las lineas horizontales se pintaban
 * cada SPECTRUM_GRID_DB dB, llevados a pantalla con el db_min/db_max DE
 * ESE MOMENTO. Con el automatico del espectro -ver s_spec_agc_enabled en
 * main.c- moviendo db_min y db_max cuadro a cuadro, la posicion EN
 * PIXELES de cada linea subia y bajaba con ellos: un temblor constante y
 * molesto en unas lineas que estan ahi justamente para ser una referencia
 * quieta. Unas filas y columnas repartidas por igual, calculadas del
 * ancho y el alto del propio panel, no pueden moverse con nada que cambie
 * por cuadro -ni la escala de dB, ni el tramo de frecuencia, ni el
 * automatico, ni el zoom-: son lineas de referencia quietas que parten el
 * panel en bandas iguales, el mismo papel que hace una hoja de papel
 * milimetrado, y no la lectura calibrada de ningun dB ni de ningun
 * hercio en concreto.
 */
#define SPECTRUM_GRID_ROWS 4 /* horizontal lines - N lines divide the panel into N+1 equal vertical bands */
#define SPECTRUM_GRID_COLS 3 /* vertical lines - matches spec_span_labels_draw()'s quarter/half/three-quarter tick columns */

/* Smoothing factors (0..1): applied per displayed frame, not per FFT
 * block. Attack is how fast the trace RISES toward a stronger value,
 * decay how fast it FALLS. */
#define SPECTRUM_EMA_ATTACK 0.60f
#define SPECTRUM_EMA_DECAY  0.25f

/* Peak-hold: 0 disables. Decay is in dB per displayed frame. */
#define SPECTRUM_PEAK_HOLD  0
#define SPECTRUM_PEAK_DECAY 0.4f

/* La marca vertical del centro: el VFO, cuando lo que entra es el
 * espectro de I/Q ya desplazado. Se pinta POR DEBAJO de las señales y por
 * encima de la rejilla, asi que nunca tapa una traza de verdad. Con 0 se
 * quita.
 *
 * Cuando esta puesta la sintonia por frecuencia intermedia baja -ver la
 * nota de LOW-IF TUNING en demod_am.h-, la señal que se demodula ya no
 * esta en el bin del oscilador local: esta DEMOD_IF_OFFSET_HZ mas alla.
 * El parametro center_mark_offset_px de spectrum_draw() corre esta linea
 * para que siga al punto de demodulacion de verdad en vez de quedarse
 * siempre clavada en el centro. */
#define SPECTRUM_CENTER_MARK 1

/* Build the palette LUT. Call once at startup, before the first
 * spectrum_draw()/spectrum_colormap(). */
void spectrum_init(void);

/*
 * Three trace styles - HEATMAP and LINE added 31/07/2026, OUTLINE
 * added 07/08/2026, all per the project owner. HEATMAP is the
 * original palette-gradient bars; LINE is a plain continuous-line
 * look (flat fill + bright outline on a dark navy background, like a
 * classic panadapter); OUTLINE is LINE with the fill removed - just
 * the 1px contour on the dark navy background, so gridlines/band
 * tint/center mark show straight through underneath the trace
 * instead of being covered by a filled bar. Toggled live via
 * spectrum_set_style() - the NEXT spectrum_draw() call picks it up,
 * no re-init needed. Only affects the SPECTRUM panel's own
 * fill/background/trace colors - the waterfall keeps using
 * spectrum_colormap() (the palette LUT) regardless of this setting,
 * since a flat-color waterfall would be useless (it's the palette
 * gradient that makes a waterfall readable at a glance).
 */
typedef enum {
    SPECTRUM_STYLE_HEATMAP = 0, /* existing palette-gradient bars (default, unchanged) */
    SPECTRUM_STYLE_LINE    = 1, /* flat fill + bright outline on a dark navy background */
    SPECTRUM_STYLE_OUTLINE = 2  /* same navy background as LINE, but no fill - contour only */
} spectrum_style_t;

void spectrum_set_style(spectrum_style_t style);
spectrum_style_t spectrum_get_style(void);

/*
 * Como se trata la traza, y SOLO en el modo de mapa de calor. Anadido el
 * 08/09/2026, pedido por el usuario, corrigiendo la primera version de
 * esta misma funcion: al unir la traza de arriba de las barras a traves
 * de un escalon -ver la pasada 1.6 de spectrum_draw()- se seguia usando
 * para el puente el SPEC_COLOR_TRACE fijo y brillante, o sea blanco, y
 * eso se lee como un contorno blanco duro pegado a la silueta entera del
 * espectro. Queda mal, sobre todo con paletas donde el blanco no aparece
 * ya cerca de lo alto del degradado. Por defecto -0, apagado- la traza Y
 * el puente que une se pintan ahora con el MISMO color de fila de la
 * paleta que el relleno de la barra ya usa a esa altura, con lo que el
 * borde de arriba se lee como un grafico de barras limpio, unido y del
 * color que le toca, sin ningun realce aparte. Poniendolo a 1 se vuelve
 * al contorno blanco explicito de antes, para quien prefiera ese realce.
 * Solo afecta al mapa de calor: LINE y OUTLINE se quedan con su
 * SPEC_LINE_TRACE fijo pase lo que pase, porque en ESOS dos estilos la
 * linea de traza o de contorno es todo lo que se ve -OUTLINE no pinta
 * nada mas- y no un realce opcional encima de un relleno.
 */
void spectrum_set_heatmap_trace_white(uint8_t white);
uint8_t spectrum_get_heatmap_trace_white(void);

/*
 * EL PUENTE DE LA TRAZA, ahora conmutable - etapa 30, 24/09/2026.
 *
 * Lo que hace el puente esta explicado en spectrum_draw(), paso 1.6: donde
 * dos columnas vecinas tienen alturas muy distintas, sus pixeles de traza no
 * comparten fila y el borde de arriba se lee como puntos sueltos en vez de
 * como una linea. El puente los une con un escalon, igual que una grafica de
 * linea.
 *
 * Por defecto va ENCENDIDO, que es como ha estado desde que se escribio.
 * Apagarlo devuelve los puntos sueltos.
 */
/* El espectro montado en una banda que trae otro, para la captura de
 * pantalla. Usa el mismo montador que el dibujado normal. Ver
 * spectrum_pinta_en() en spectrum.c. */
void    spectrum_pinta_en(gfx2_surf_t *sf, int16_t px, int16_t py);

/*
 * REPINTADO PARCIAL DEL ESPECTRO - 05/10/2026.
 *
 * spectrum_draw() se salta las bandas de filas que son identicas al cuadro
 * anterior (ver "LAS BANDAS QUE NO HACE FALTA VOLCAR" en spectrum.c). Eso
 * vale mientras nadie pinte dentro del rectangulo del espectro por su
 * cuenta. Quien lo haga -o quien borre la pantalla entera- tiene que
 * llamar a esto para que el siguiente cuadro salga completo.
 *
 * spectrum_parcial_diag() saca cuantas bandas se saltaron y cuantas habia,
 * para la ventana de informacion y para los bancos.
 */
void    spectrum_invalida(void);
void    spectrum_parcial_diag(uint32_t *saltadas, uint32_t *total);
void    spectrum_parcial_set(uint8_t on);
uint8_t spectrum_parcial_get(void);

void    spectrum_set_bridge(uint8_t on);
uint8_t spectrum_get_bridge(void);

/*
 * La paleta del mapa de color dB -> RGB565 que se comparte, el de
 * spectrum_colormap() de mas abajo. Anadida el 08/09/2026, pedida por el
 * usuario, que queria una casilla para cambiar de paleta y llegar a otras
 * combinaciones de color.
 *
 * Al contrario que spectrum_style_t, que solo cambia el aspecto del
 * relleno o el contorno del panel del ESPECTRO, esto afecta a los DOS
 * sitios: al estilo de mapa de calor del espectro Y a la cascada, porque
 * los dos sacan el color de la misma tabla -ver la nota de COMO ESTA
 * PENSADO EL COSTE, mas arriba-. Un cambio de paleta se ve en los dos a
 * la vez, y en ninguno de los dos cuando spectrum_style_t es LINE u
 * OUTLINE -no hay degradado que colorear-, aunque la cascada de debajo
 * sigue enseñando la paleta nueva de todas formas.
 *
 * CLASSIC es ahora el degradado "Classic" de verdad de SDR++, ver abajo.
 * Se deja la primera y por defecto para que lo que ve quien no toque la
 * casilla nueva cambie lo menos posible: el Classic de SDR++ y el que
 * este proyecto se invento antes son los dos degradados de oscuro a
 * caliente.
 */
typedef enum {
    SPECTRUM_PALETTE_CLASSIC       = 0,  /* dark navy -> blues -> white -> yellow -> orange -> red -> dark red (SDR++'s own "Classic", exact) */
    SPECTRUM_PALETTE_FIRE          = 1,  /* black -> red -> orange -> yellow -> white (this project's own addition, not from SDR++) */
    SPECTRUM_PALETTE_VIRIDIS       = 2,  /* dark purple -> blue -> teal -> green -> yellow (exact, matplotlib/B.I.D.S.) */
    SPECTRUM_PALETTE_GRAYSCALE     = 3,  /* black -> white, no hue at all (exact, matches SDR++'s "Grey Scale") */
    SPECTRUM_PALETTE_TURBO         = 4,  /* dark blue -> blue -> green -> yellow -> dark red (exact, Google AI) */
    SPECTRUM_PALETTE_INFERNO       = 5,  /* near-black -> purple -> red -> orange -> pale yellow (exact, B.I.D.S.) */
    SPECTRUM_PALETTE_MAGMA         = 6,  /* near-black -> purple -> pink/red -> orange -> pale pink-white (exact, B.I.D.S.) */
    SPECTRUM_PALETTE_PLASMA        = 7,  /* deep blue-purple -> magenta -> orange -> pale yellow (exact, B.I.D.S.) */
    SPECTRUM_PALETTE_GQRX          = 8,  /* black -> blue -> cyan/green -> yellow -> red -> white (exact, csete) */
    SPECTRUM_PALETTE_ELECTRIC      = 9,  /* black -> blue -> cyan -> white (exact, Ryzerth) */
    SPECTRUM_PALETTE_CLASSIC_GREEN = 10, /* black -> blues -> pale green -> orange -> red -> dark red (exact, Paul PD0SWL) */
    SPECTRUM_PALETTE_SMOKE         = 11, /* white -> grays -> black, i.e. an INVERTED grayscale (exact, Yaroslav Andrianov) */
    SPECTRUM_PALETTE_TEMPER_COLORS = 12, /* black -> indigo -> violet -> slate blue -> dusty rose -> plum (exact, Yaroslav Andrianov) */
    SPECTRUM_PALETTE_VIVID         = 13, /* black -> purple -> viridis-like band -> yellow -> orange -> red (exact, Yaroslav Andrianov) */
    SPECTRUM_PALETTE_WEBSDR        = 14, /* black -> navy -> magenta -> pale yellow -> white (exact, Ryzerth) */
    SPECTRUM_PALETTE_PHOSPHOR      = 15  /* negro -> verde apagado -> verde brillante -> blanco verdoso (propia, para el tema Fósforo - ver spectrum.c) */
} spectrum_palette_t;

/*
 * *** AL DIA, 08/09/2026 ***. CLASSIC, VIRIDIS, TURBO, INFERNO, MAGMA,
 * PLASMA, GQRX, ELECTRIC y CLASSIC_GREEN estaban todas aproximadas de
 * memoria o por conocimiento general en la primera version de esta
 * funcion. Despues el usuario subio los ficheros JSON DE VERDAD de
 * root/res/colormaps/ de SDR++ (AlexandreRouma/SDRPlusPlus), asi que esas
 * nueve usan ya los colores de parada REALES de esos ficheros. Seis de
 * ellas -GQRX, INFERNO, MAGMA, PLASMA, TURBO y VIRIDIS- vienen de origen
 * como mapas completos de 256 entradas, asi que esas se copian tal cual,
 * una entrada de tabla por cada entrada del fichero, sin interpolar nada:
 * ver las tablas k_lut_* de spectrum.c. SMOKE, TEMPER_COLORS, VIVID y
 * WEBSDR son nuevas, de esa misma subida, y tambien exactas. FIRE sigue
 * siendo invencion de este proyecto: no esta en el juego de SDR++.
 */
/*
 * La lista de paletas, recorrible - 22/09/2026.
 *
 * Antes la misma lista estaba escrita siete veces por el firmware (los dos
 * switch de build_lut(), el rotulo del tile, el nombre en ajustes, el ciclo
 * del boton, y el guardar y el leer de CONFIG.CSV), y tres paletas ya se
 * mostraban con el nombre de otra porque a una de las siete copias le
 * faltaba su rama. Ahora la lista vive en una sola tabla dentro de
 * spectrum.c y esto es la ventana a esa tabla: quien necesite nombres,
 * claves, el recuento o un color de muestra la recorre, no la copia.
 *
 * spectrum_palette_muestra() da un color de CUALQUIER paleta sin cambiar la
 * que esta puesta, que es lo que permite ensenar las quince a la vez en la
 * pantalla de paletas sin que el espectro parpadee.
 */
uint8_t     spectrum_palette_count(void);
const char *spectrum_palette_nombre(uint8_t i);   /* "Clasica verde" */
const char *spectrum_palette_clave(uint8_t i);    /* "CLASSIC_GREEN" */
uint16_t    spectrum_palette_muestra(uint8_t i, uint8_t idx); /* idx 0..255 */
int16_t     spectrum_palette_de_clave(const char *s, uint32_t n); /* -1 si no es ninguna */

void spectrum_set_palette(spectrum_palette_t palette);
spectrum_palette_t spectrum_get_palette(void);

/*
 * El suavizado de la linea en el espacio. Anadido el 01/08/2026 y
 * manejable en vivo desde la casilla, el boton y la chapa del NB de
 * main.c, reaprovechados -ver el comentario de s_spec_smooth_passes alli:
 * el viejo interruptor del supresor de impulsos no movia ningun proceso de
 * señal de verdad, asi que su sitio en la interfaz estaba libre-. Pasa un
 * filtro de tres puntas (1,2,1)/4 sobre las alturas de las columnas
 * vecinas, `passes` veces, DESPUES de cuantizar a pixeles: solo suaviza el
 * borde de la traza y del relleno en pantalla y no toca nunca el estado
 * por columna -la media exponencial y la retencion de pico- que
 * spectrum_draw() se guarda entre cuadros (ver sus comentarios de la
 * pasada 1 y la 1.5). Por eso no puede acumularse ni irse cuadro a cuadro,
 * como si pasaria suavizando la propia media.
 *
 * Con 0 se quita -borde afilado por bin, el aspecto original-. Cada pasada
 * de mas cuesta un bucle entero mas de unas `w` vueltas por cuadro
 * -despreciable al lado del bucle de w*h pixeles- y redondea un poco mas
 * la traza. Entre 2 y 3 pasadas es el margen donde el usuario encontro una
 * linea visiblemente mas suave sin aplastar los picos de verdad. Por
 * dentro se recorta a [0, SPECTRUM_LINE_SMOOTH_MAX].
 */
#define SPECTRUM_LINE_SMOOTH_MAX 5
void spectrum_set_line_smooth(uint8_t passes);
uint8_t spectrum_get_line_smooth(void);

/* dB -> RGB565 through the shared palette (LUT-backed; the float here
 * is fine for per-COLUMN use, e.g. coloring a waterfall line - just
 * never call it per pixel). */
uint16_t spectrum_colormap(float db, float db_min, float db_max);

/*
 * Las dos mitades de spectrum_colormap(), por separado.
 *
 * spectrum_colormap() ya era internamente "normaliza el dB a 0..255 y mira
 * en una LUT". Partirlo permite que el waterfall guarde el INDICE (1 byte)
 * en vez del color (2 bytes), lo que ahorra 56 KB de RAM en su historial.
 *
 * Efecto secundario util: al guardar el indice, cambiar de paleta repinta
 * todo el historial y no solo las filas nuevas.
 */
uint8_t spectrum_colormap_index(float db, float db_min, float db_max);

/*
 * LA MISMA, PARTIDA EN DOS PARA LOS BUCLES - 24/10/2026.
 *
 * La escala sale UNA VEZ por linea -lleva la division- y el indice de cada
 * pixel es una resta y una multiplicacion. Quien pinte una linea entera usa
 * estas dos; quien necesite un solo valor suelto puede seguir con la de
 * arriba, que es estas dos juntas. El porque, en spectrum.c.
 *
 * Con db_max <= db_min la escala sale 0 y todos los indices salen 0, igual
 * que hacia la de arriba.
 */
float   spectrum_colormap_escala(float db_min, float db_max);
uint8_t spectrum_colormap_idx(float db, float db_min, float escala);
const uint16_t *spectrum_colormap_lut(void);

/*
 * Pinta `n_bins` valores en dB dentro del rectangulo (x,y,w,h). Cada
 * columna promedia los bins que le tocan; el estado de suavizado y de
 * pico se guarda por dentro, una entrada por columna, y se reinicia solo
 * si cambia `w`. db_min y db_max fijan la escala vertical y el tramo de
 * la paleta.
 *
 * center_mark_offset_px corre la linea de SPECTRUM_CENTER_MARK estos
 * pixeles desde el centro de verdad -positivo a la derecha, o sea hacia
 * frecuencias mas altas-. Con 0 se marca el centro real, el oscilador
 * local, como antes. Por dentro se recorta para no salirse del area de
 * dibujo.
 *
 * band_active, band_lo_offset_px y band_hi_offset_px: anadidos el
 * 03/08/2026, pedidos por el usuario. Son una zona de fondo teñida,
 * opcional, detras de la traza, que enseña QUE trozo del panadaptador se
 * esta demodulando ahora mismo -el ancho de audio de AM/USB/LSB, llevado
 * de vuelta al eje de frecuencia-, para que se vea de un golpe que es lo
 * que se esta oyendo y no solo donde esta el punto de demodulacion. Con
 * band_active a 0 no se pinta nada de eso y queda exactamente como antes;
 * asi se usa en NFM y WFM, que no tienen un ancho de audio que elija quien
 * llama, como si lo tienen AM y las bandas laterales. Cuando esta activa,
 * band_lo_offset_px y band_hi_offset_px son desplazamientos en pixeles
 * desde el centro horizontal del panel, CON EL MISMO origen y el mismo
 * convenio de signo que center_mark_offset_px -asi quien llama puede
 * construirlos directamente encima de ese mismo valor, ver el sitio donde
 * se llama en main.c-. El orden entre el bajo y el alto da igual: por
 * dentro se recortan y se ordenan. El tinte solo sustituye pixeles de
 * FONDO -raya de rejilla o relleno liso, lo que tocara-: no tapa nunca la
 * traza, ni el relleno de las barras, ni la marca de pico, ni la del
 * centro, que se siguen pintando encima igual que sin el.
 */
/*
 * ======================================================================
 * LO QUE VA ENCIMA DE LA TRAZA - 15/10/2026
 * ======================================================================
 *
 * Los nombres de las emisoras se pintaban con un gfx2_render() APARTE,
 * justo despues de spectrum_draw(). Tres cosas iban mal y las tres son la
 * misma:
 *
 *   1. gfx2_render() arranca su banda EN NEGRO y encima pinta lo que
 *      diga el dibujante (ver gfx2.c). El dibujante de los carteles solo
 *      pinta letras y rayitas, asi que lo que llegaba a la pantalla era
 *      una franja NEGRA de 756x36 encima de la traza, que se comia el
 *      sombreado del paso de banda y las lineas de la rejilla.
 *   2. La franja empezaba dos pixeles por debajo del techo de la traza,
 *      asi que esos dos pixeles del sombreado sobrevivian: el "trocito
 *      que no deberia de estar" era eso, un resto de la señal flotando
 *      encima de un agujero negro.
 *   3. Y spectrum_draw() repinta por bandas y SE SALTA las que no han
 *      cambiado (ver s_parcial_hay). Despues de que otro le pise la
 *      franja, el no sabe que esta sucia y no la repinta: de ahi el
 *      parpadeo, segun el cuadro tocase repintar o no.
 *
 * Y la captura de pantalla no pasa por ahi -va por spectrum_pinta_en()-,
 * asi que en las capturas no salian los carteles.
 *
 * LA REGLA QUE SALE DE ESTO: lo que va encima de la traza lo tiene que
 * pintar QUIEN PINTA LA TRAZA, en el mismo buffer y en la misma pasada.
 * Dos compositores sobre el mismo rectangulo siempre acaban asi.
 *
 * `fn` se llama una vez por banda, con una superficie que ya trae la
 * traza montada y cuyas coordenadas son las de la PANTALLA. Se pone a
 * cero para quitarlo; vale para una sola cosa a la vez, a proposito: si
 * algun dia hace falta encadenar dos, que se vea en el codigo de quien
 * las encadena y no aqui.
 *
 * SIN `ctx`. El patron de este proyecto lo lleva (gfx2_render lo lleva),
 * pero aqui serian cuatro bytes de .bss para guardar un cero: el unico
 * que lo usa saca su estado de sus propias variables. Con DOCE bytes
 * libres en toda la radio, un parametro por simetria es un parametro que
 * no cabe.
 *
 * SIN `alto`: LA ZONA LLEGA HASTA LA PRIMERA RAYA DE LA REJILLA - 15/10.
 *
 * Se lo pasaba el que engancha, y el que engancha pedia 46 filas para
 * sus tres renglones de texto. Pero la primera raya de la rejilla cae en
 * la 41 -el panel mide 208 y cuatro rayas lo parten en cinco-, asi que
 * pedir 46 se saltaba esa raya y la zona se estiraba hasta la SEGUNDA,
 * comiendose dos huecos del espectro en vez de uno.
 *
 * Pedir una altura en pixeles para algo que tiene que acabar en una raya
 * es pedir que un dia no cuadre. Lo decide spectrum.c, que es quien sabe
 * donde estan las rayas: la zona va del techo de la traza a la primera
 * raya, incluida. Quien pinta encima reparte sus renglones ahi dentro.
 */
void spectrum_encima(void (*fn)(gfx2_surf_t *s));

/* El viejo comentario de `alto`, que sigue explicando por que esta zona
 * no se puede saltar nunca en el repintado parcial:
 *
 * `alto` ES OBLIGATORIO Y NO ES DECORACION - 15/10/2026.
 *
 * Y se movian con retraso porque este dibujo SE SALTA las bandas donde
 * la traza no llega (ver s_parcial_hay mas abajo): en el techo del
 * espectro no hay señal casi nunca, asi que las bandas de arriba se
 * saltaban casi siempre y los carteles solo se repintaban cuando subia
 * un pico hasta ahi.
 *
 * Es el mismo fallo de esta mañana con otra cara. Entonces era otro el
 * que pintaba sobre la traza sin que la traza lo supiera; ahora pinta
 * quien debe, pero la traza seguia decidiendo que repintar mirando SOLO
 * la señal. Quien pinta tiene que saber TODO lo que hay en el
 * rectangulo, no solo lo suyo.
 *
 * Asi que el gancho dice cuantas filas desde el techo ocupa, y esas
 * bandas no se saltan nunca y se montan de ancho completo -a medias
 * dejarian carteles viejos en las columnas no repintadas-. Cuesta el
 * 16% del espectro en cada cuadro, y solo con los carteles encendidos.
 */
/*
 * La primera raya horizontal de la rejilla a partir de la fila `desde`,
 * contando desde el techo de la traza. La necesita quien pinta encima
 * para que sus rayitas acaben EN la raya y no a media altura: ver
 * "las lineas del bw tienen que estar a ras" en spectrum.c. Devuelve
 * `desde` si no hay ninguna.
 */
int16_t spectrum_rejilla_fila(int16_t desde);

/*
 * El fondo del panel de la traza, en 0xRRGGBB, para que quien pinte DENTRO
 * del panel pueda elegir un color que se vea. Ver el comentario del .c:
 * la tinta del cromo es negra en tres de los ocho temas y este panel es
 * casi negro en los ocho.
 */
uint32_t spectrum_fondo_rgb(void);

void spectrum_draw(const float *db, uint32_t n_bins,
                    uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                    float db_min, float db_max,
                    int16_t center_mark_offset_px,
                    uint8_t band_active,
                    int16_t band_lo_offset_px, int16_t band_hi_offset_px);

#endif /* SPECTRUM_H */
