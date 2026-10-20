#ifndef SPEC_ROTULOS_H
#define SPEC_ROTULOS_H

#include <stdint.h>
#include "gfx2.h"
#include "gfx2_font.h"

/*
 * ======================================================================
 * LOS NOMBRES DE LAS EMISORAS, SOBRE EL ESPECTRO - 15/10/2026
 * ======================================================================
 *
 * Pedido por el usuario, con tres condiciones que son las que fijan el
 * diseño:
 *
 *   - los carteles solo se mueven cuando se mueve la frecuencia;
 *   - van ligados al VFO, a la barra central que pone las frecuencias;
 *   - un boton nuevo en el espectro para activarlos o no.
 *
 * LIGADOS AL VFO quiere decir una cosa muy concreta: la x de cada cartel
 * sale de los MISMOS center_hz y span_hz con los que se dibuja la regla
 * de frecuencias (ver spec_chrome.h), no de una cuenta paralela. Esto no
 * es puntilloso: en WFM ya paso una vez que la regla y el salto del VFO
 * usaban anchos distintos, y el sintoma era tocar una emisora y quedarte
 * a 24 kHz. Dos cuentas que deberian dar lo mismo acaban no dandolo.
 *
 * POR ESO EL CARTEL GUARDA SU FRECUENCIA Y NO SU PIXEL. Al mover el VFO
 * la x se recalcula sola y el cartel se desplaza con el espectro, que es
 * lo que el usuario pidio. Solo hay que volver a preguntarle a la base
 * cuando la ventana se sale de lo que se consulto.
 *
 * ESTE MODULO NO TOCA LA FLASH. Recibe los nombres ya resueltos, igual
 * que spec_chrome recibe el estado de la radio ya resuelto. Asi se
 * compila en el simulador y se puede RENDERIZAR Y MIRAR, que con algo
 * que se juzga por como queda es la unica prueba que vale.
 */

/*
 * CUANTOS CABEN, Y EL NUMERO SALE DE LOS DOS LIMITES A LA VEZ.
 *
 * POR PANTALLA. Con font_ui_11 (12 px de cuerpo, 15 de linea) tres filas
 * miden 45 px y entran encima de la primera raya de la rejilla, que esta
 * en la 48. Un nombre de catorce letras ronda los 75 px, asi que en cada
 * fila caben unos diez y en tres unos treinta. Lo que limita ya no es la
 * pantalla.
 *
 * POR MEMORIA, Y AQUI ESTABA EL TOPE DE VERDAD. Con los textos metidos
 * en esta estructura, cada cartel costaba quince bytes y la radio iba al
 * 99,99% de la SRAM: por eso eran SEIS primero y SIETE despues. Hoy son
 * TRECE sin pedir un byte -de hecho sobran sesenta- porque los textos se
 * han ido de aqui. Ver spec_rot_txt_t, mas abajo.
 *
 * TRECE Y NO MAS, y el numero no es de pantalla ni de RAM sino de la
 * cuenta de casillas: el paso es span/(max-1) y con trece sale 96/12 =
 * 8 kHz clavados, que es un numero redondo en una rejilla que se cuenta
 * desde cero hercios. Con catorce saldria 7.384 Hz y las casillas no
 * caerian en kilohercios enteros.
 *
 * MEDIDO SOBRE EMISORAS.BIN, barriendo de 2,5 a 27 MHz cada 24 kHz y las
 * 24 horas: la peor ventana de 96 kHz tiene 23 emisoras EN ANTENA a la
 * vez. Con trece huecos se nombra mas de la mitad; con siete, menos de
 * un tercio.
 */
#define SPEC_ROT_MAX   13U
#define SPEC_ROT_TXT   14U   /* letras utiles, sin contar el cero */
#define SPEC_ROT_FILAS 3U    /* filas de carteles, en zigzag por casilla */

/*
 * CUANTAS SE MIRAN ANTES DE ELEGIR. Las 23 medidas mas un 20% de sitio
 * por si la lista crece. Vive en la PILA de quien llama, no en .bss.
 */
#define SPEC_ROT_CAND  28U

/*
 * A QUE DISTANCIA DEL DIAL SE CONSIDERA QUE UNA EMISORA "ES LA QUE SUENA".
 *
 * Los mismos 600 Hz que usa el cartel de al lado del reloj (emisoras_busca
 * en main.c). Que sean los mismos NO es cosmetico: es lo que permite
 * decir la regla en una linea -si su nombre esta junto al reloj, esta en
 * el espectro- y que no haya un tercer estado en el que esta en un sitio
 * y no en el otro.
 *
 * Y es el numero que hace defendible meter la sintonia en el reparto;
 * ver el comentario largo de spec_rotulos_filtra().
 */
#define SPEC_ROT_SINTO 600UL

/*
 * EL CONTRASTE MINIMO DE UN CARTEL CONTRA EL PANEL, en luminancia.
 *
 * El mismo 48 que usa mapa_bin.c para exigir que el punto de datos se vea
 * sobre el mar. Que sea el mismo numero no es casualidad: es la misma
 * pregunta -¿se ve esto encima de aquello?- y tener dos respuestas
 * distintas en la misma radio solo sirve para que una de las dos este mal.
 */
#define SPEC_ROT_CONTRASTE 48

/*
 * Y EL QUE SE BUSCA CUANDO HAY QUE ELEGIR COLOR, que no es el mismo.
 *
 * 48 es el minimo para que algo NO SEA INVISIBLE; con letra de once
 * pixeles sobre un panel negro eso es poco. Cuando el color hay que
 * fabricarlo -porque la tinta del tema no vale- se apunta mas alto y sale
 * tan legible como en los temas oscuros, que dan de 172 para arriba.
 */
#define SPEC_ROT_CONTRASTE_BUENO 120

/*
 * DE QUE COLOR VAN LOS CARTELES, EN ESTE TEMA.
 *
 * `fondo` es el fondo del PANEL DE LA TRAZA, no el del cromo: ahi es donde
 * se pintan. Devuelve el color del texto y el de la rayita. El porque
 * entero, con la tabla de los ocho temas medida, esta en el .c.
 */
void spec_rotulos_tinta(uint32_t fondo, uint32_t ink, uint32_t ink_dim,
                        uint32_t traza, uint32_t *texto, uint32_t *raya);

/*
 * LO QUE LLEGA DE LA BASE, antes de elegir. `clave` es el identificador
 * del nombre -dos entradas con la misma clave son la misma emisora-, y
 * por eso los repetidos se quitan sin leer una sola letra de la flash.
 */
typedef struct {
    uint32_t hz;
    uint16_t clave;
} spec_rot_cand_t;

/*
 * LA LISTA QUE SE GUARDA ENTRE CUADROS. Catorce bytes por cartel menos
 * que antes, porque aqui NO hay texto: solo la frecuencia (en decenas de
 * hercio sobre `base_hz`) y la clave del nombre en la base.
 */
typedef struct {
    uint32_t base_hz;                       /* de donde cuelgan las d10 */
    uint16_t d10[SPEC_ROT_MAX];             /* (hz - base_hz) / 10 */
    uint16_t clave[SPEC_ROT_MAX];           /* el nombre, sin resolver */
    uint8_t  n;                             /* cuantos valen */
} spec_rotulos_t;

/*
 * ======================================================================
 * Y LOS TEXTOS, QUE VIVEN EN LA PILA DE QUIEN DIBUJA - 15/10/2026
 * ======================================================================
 *
 * Trece nombres de catorce letras son 195 bytes. En .bss no caben: la
 * radio tiene OCHO libres. En la pila de quien dibuja, si - ahi hay mas
 * de mil de margen (ver tools/pila.py), y ademas se devuelven solos al
 * salir del cuadro.
 *
 * LO QUE CUESTA, Y POR QUE SE PAGA. Los nombres hay que leerlos de la
 * flash en CADA cuadro en vez de una vez cada 120 ms: son dos lecturas
 * cortas por cartel, 26 por cuadro, unos 300 microsegundos. A cambio
 * caben el doble de carteles y sobran sesenta bytes de RAM para lo
 * siguiente que haga falta. Con ocho libres, esa cuenta sale.
 *
 * LO QUE NO SE PUEDE HACER: resolverlos DENTRO del gancho de
 * spectrum_draw(). El gancho corre una vez por banda de 16 filas, o sea
 * tres veces por cuadro, y serian 78 lecturas en vez de 26. Se resuelven
 * una vez, fuera, y se le pasa el bloque ya hecho.
 */
typedef char spec_rot_txt_t[SPEC_ROT_MAX][SPEC_ROT_TXT + 1U];

/* La frecuencia del cartel i, que ya no esta guardada tal cual. */
uint32_t spec_rotulos_hz(const spec_rotulos_t *rr, uint8_t i);

/* En que fila va un cartel: 0 arriba del todo, SPEC_ROT_FILAS-1 abajo.
 * Sale de su CASILLA, o sea de su frecuencia, no de donde este en la
 * lista: por eso un nombre no cambia de fila mientras esta en pantalla. */
uint8_t spec_rotulos_fila(const spec_rotulos_t *rr, uint8_t i, uint32_t span_hz);

/* Mete un cartel. La usan el firmware y los bancos: asi el empaquetado
 * de arriba esta en un sitio y no en tres. */
void spec_rotulos_pon(spec_rotulos_t *rr, uint8_t i, uint32_t hz,
                      uint16_t clave);

/*
 * ======================================================================
 * LA ELECCION, QUE ES DONDE ESTABA EL FALLO - 15/10/2026
 * ======================================================================
 *
 * Lo que se veia en el video: en 7,260 MHz con 96 kHz de ventana se
 * pintaban CUATRO carteles, tres de ellos con el mismo nombre
 * ("SunDance Radio" en 7.215, 7.220 y 7.225) y todos en el tercio
 * izquierdo. La emisora sintonizada, la que sale al lado del reloj, no
 * tenia cartel. Y el lado derecho de la pantalla, vacio siempre.
 *
 * Tres causas, las tres medidas con la base de verdad:
 *
 *   1. Se le pedia a la base el DOBLE de ventana -media pantalla de
 *      margen a cada lado, para que el cache aguantase- pero con sitio
 *      para seis. Los seis primeros salen por abajo, o sea de la parte
 *      que NO se ve. Medido en 7,260: dos de los seis caian fuera de la
 *      pantalla. El margen que hacia el cache barato era el mismo que
 *      vaciaba la pantalla.
 *   2. No se miraban los repetidos. Una emisora en tres canales seguidos
 *      se come tres huecos y luego el dibujante los tira por solaparse,
 *      asi que el coste se paga entero y no se ve ninguno.
 *   3. Se cogian los primeros por frecuencia, no los repartidos. Con 15
 *      en antena y sitio para seis, los seis son siempre los de la
 *      izquierda.
 *
 * ESTA FUNCION ES LO QUE ANTES ESTABA SUELTO DENTRO DE main.c, y por eso
 * no habia banco que lo cazara: el simulador no enlaza main.c. Aqui se
 * compila y se mide. Ver sim/rotulos_elige.c.
 *
 * Trabaja EN SITIO sobre `c`, que tiene que venir ordenada por hz, y
 * devuelve cuantas quedan apiladas al principio.
 */
/*
 * `tune_hz` VUELVE A USARSE, Y LA DIFERENCIA ES UN NUMERO - 15/10/2026.
 *
 * No salia porque 5.544.000 es multiplo exacto de 8.000, o sea el paso
 * de la rejilla, o sea que cae JUSTO EN EL BORDE de su casilla: el peor
 * descentre posible, 4.000 Hz. Pierde contra cualquiera que la acompañe
 * -alli, Shannon HFDL en 5.547-. Y no es rareza: medido sobre toda la
 * base, el 17,8% de las emisoras en antena caen en el borde de su
 * casilla y el 26,7% se quedan sin cartel. Un cartel por casilla tiene
 * ese precio, y se paga a gusto... salvo cuando la que se queda fuera es
 * precisamente la que estas escuchando.
 *
 * ESTO YA SE INTENTO ESTA MISMA TARDE Y SALIO MAL. Lo apunte aqui: "al
 * cruzar el dial de una casilla a la siguiente cambiaban DOS carteles de
 * golpe". Era cierto el sintoma y estaba mal el diagnostico: no fallaba
 * meter la sintonia, fallaba meterla SIN TOLERANCIA. Si manda "la mas
 * cercana al dial" pase lo que pase, siempre hay una mandando, y cruzar
 * un borde suelta una casilla y coge otra: dos cambios en un clic.
 *
 * Con SPEC_ROT_SINTO hay un tercer estado -ninguna manda- que ocupa casi
 * todo el dial, y los dos cambios dejan de poder ocurrir en el mismo
 * clic. MEDIDO sobre la base entera, 11.640 clics de 1 kHz barriendo las
 * 24 horas:
 *
 *     sin tolerancia:        352 clics cambian DOS carteles
 *     con 600 Hz:              0 clics cambian dos; 1.063 cambian uno
 *
 * Esos 1.063 son el caso que se buscaba: el clic en que el dial se posa
 * sobre una emisora catalogada, o se baja de ella. Lo que desaparece es
 * la vecina de su casilla, y vuelve en cuanto te vas. Es la unica
 * dependencia del dial que queda en el espectro, vale un cartel, y pasa
 * dentro del sombreado del paso de banda, que es donde el usuario esta
 * mirando.
 *
 * Lo que NO cambia: la fila sigue siendo casilla%3, o sea funcion pura
 * de la frecuencia, y sigue habiendo un cartel por casilla. Ni saltos de
 * fila ni nombres repetidos: esos dos fallos eran del cartel aparte que
 * se quito, no de esto.
 */
uint8_t spec_rotulos_filtra(spec_rot_cand_t *c, uint8_t n,
                            uint32_t center_hz, uint32_t span_hz,
                            uint32_t tune_hz, uint8_t max);

/*
 * Apaga los carteles que, despues de recortar el nombre a catorce
 * letras, DIGAN LO MISMO que otro que ya esta y encima le caiga al lado.
 * Dos claves distintas pueden acabar en el mismo texto ("Shannon
 * Aeradio I" y "Shannon Aeradio H") y dos carteles iguales pegados no
 * informan de nada. Se llama despues de resolver los nombres; el que se
 * apaga se queda con el texto vacio y el dibujante ya se lo salta.
 */
void spec_rotulos_limpia(const spec_rotulos_t *rr, spec_rot_txt_t txt,
                         uint32_t span_hz);

/*
 * Mete `origen` en `dest` recortado a SPEC_ROT_TXT letras, y si hay que
 * cortar CORTA POR UN ESPACIO: "China National Radio 1" se queda en
 * "China National" y no en "China Nationa", que parece un fallo de
 * memoria. Solo si al cortar por el espacio queda algo legible -seis
 * letras-; si no, se corta a lo bruto, que es mejor que dejarlo en "SOH".
 */
void spec_rotulos_recorta(char *dest, const char *origen);

/*
 * Pinta los carteles que caigan dentro de la traza.
 *
 * `center_hz` y `span_hz` son LOS DE LA REGLA (spec_chrome_t), no otros.
 * `y` es la linea de arriba de la zona de carteles y `alto` lo que se le
 * deja; el dibujante reparte dentro, en dos filas, y no se sale.
 *
 * Devuelve cuantos ha pintado de verdad: los que se salen por los bordes
 * o se pisan con uno ya puesto no se dibujan, y ese numero es el que
 * permite que el banco compruebe el reparto sin mirar pixeles.
 */
uint8_t spec_rotulos_draw(gfx2_surf_t *s, const spec_rotulos_t *rr,
                          const spec_rot_txt_t txt,
                          uint32_t center_hz, uint32_t span_hz,
                          int16_t x0, int16_t ancho, int16_t y, int16_t alto,
                          uint8_t *filas,
                          gfx2_rgba_t color, gfx2_rgba_t color_linea);

/*
 * `filas` ES PARA QUE SE PUEDA MEDIR LO QUE SE PINTA, Y VA EN NULL EN LA
 * RADIO - 15/10/2026.
 *
 * El banco comprobaba spec_rotulos_fila(), que es la REGLA, y estaba
 * verde mientras la pantalla movia carteles de fila: el dibujante tenia
 * una repesca -"si en tu fila no cabe, prueba otra"- que la regla no
 * sabia. Medir la regla en vez del resultado es como no medir.
 *
 * Con esto el banco lee la fila en la que ACABO cada cartel (0xFF si no
 * se pinto) y puede exigir que no cambie entre dos ventanas. El firmware
 * pasa NULL y no paga nada.
 */

/* La x de una frecuencia dentro de la traza, con la MISMA cuenta que la
 * regla. Se expone porque el banco la necesita y porque quien quiera
 * poner otra cosa sobre el espectro tiene que usar esta y no la suya. */
int16_t spec_rotulos_x(uint32_t hz, uint32_t center_hz, uint32_t span_hz,
                       int16_t x0, int16_t ancho);

uint8_t spec_rotulos_selftest(void);

#endif /* SPEC_ROTULOS_H */
