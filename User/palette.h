#ifndef PALETTE_H
#define PALETTE_H

#include <stdint.h>

/*
 * Paleta de la UI, conmutable en caliente.
 *
 * ----------------------------------------------------------------------------
 * POR QUE ESTO ES CONMUTABLE Y NO UNA CONSTANTE
 * ----------------------------------------------------------------------------
 * Hubo una revision 2 de esta paleta, hecha a partir de medir una FOTO de la
 * placa: se compararon relaciones de luminancia dentro de la imagen y salio
 * que la jerarquia de superficies estaba aplastada (saltos disenados de 2x-7x
 * que en la foto eran de 1,2x-1,3x). La conclusion fue que el panel no
 * distinguia nada entre 0x0D y 0x30, y se re-baso la paleta entera a negro
 * real con saltos tres veces mayores.
 *
 * Esa conclusion era FALSA, y el fallo estaba en el metodo. Se afirmo que las
 * relaciones dentro de una misma foto son "inmunes a la exposicion". Eso solo
 * vale para un sensor de respuesta lineal. Una camara de movil aplica una
 * curva de tono con fuerte levantamiento de sombras, que comprime justo la
 * zona oscura: precisamente donde estaban todos los tonos que se querian
 * medir. Medir tonos oscuros fotografiando una pantalla no sirve.
 *
 * La medida buena vino de la escala de escalones en el propio panel: resuelve
 * pasos de 4 unidades RGB. Los saltos de la revision 1 son de 8, 9 y 12,
 * todos comodamente por encima del umbral. La revision 1 estaba bien.
 *
 * Asi que la revision 1 vuelve a ser la de por defecto, y la 2 se queda
 * disponible para elegir MIRANDO EL PANEL en vez de deduciendo. Es lo que
 * habria que haber hecho desde el principio, y de paso deja el tema abierto
 * para quien use la radio a pleno sol, donde la 2 probablemente gane.
 *
 * ----------------------------------------------------------------------------
 * ORGANIZACION
 * ----------------------------------------------------------------------------
 * Por FUNCION, no por color: superficies, tinta, marcas de datos y estado. Una
 * etiqueta nunca lleva el color de una serie, y un color de estado nunca se
 * reutiliza como decoracion.
 *
 * Las marcas que coexisten sobre el espectro (traza, marcador de VFO y banda
 * de paso) estan validadas como conjunto categorico con el validador de la
 * guia de visualizacion, y pasan los cinco controles tanto contra la
 * superficie de la revision 1 como contra negro real:
 *
 *   luminosidad   los 3 dentro de la banda oscura L 0,48-0,67      PASS
 *   croma         los 3 >= 0,1                                      PASS
 *   CVD           peor par ACCENT<->TRACE dE 14,3 (protanopia)      PASS
 *   vision normal peor par PASSBAND<->TRACE dE 16,3                 PASS
 *   contraste     los 3 >= 3:1 contra la superficie                 PASS
 *
 * El par traza/VFO queda en dE 6,0 en tritanopia, banda admisible solo con
 * codificacion secundaria. La hay, y es geometrica: la traza es un area
 * rellena, el VFO una linea con cabeza triangular y la banda de paso un
 * rectangulo translucido.
 *
 * ESTADO Y DALTONISMO: el verde y el rojo de estado quedan en dE 3,0 en
 * deuteranopia, o sea indistinguibles para parte de la poblacion. Por eso el
 * on/off NO se codifica con verde/rojo sino con relleno frente a contorno mas
 * la palabra. El rojo queda reservado a sobrecarga, siempre con texto.
 */

typedef struct {
    const char *name;

    /* superficies, de mas honda a mas elevada */
    uint32_t surf_0;    /* fondo de pantalla y paneles de datos */
    uint32_t surf_1;    /* barras superior e inferior */
    uint32_t surf_2;    /* tiles y controles en reposo */
    uint32_t surf_3;    /* control pulsado o resaltado */
    uint32_t line;      /* separadores y contornos */
    uint32_t grid;      /* rejilla del espectro: recesiva a proposito */
    uint32_t hdr_top;   /* arranque del degradado de la cabecera */

    /* tinta */
    uint32_t ink;
    uint32_t ink_dim;
    uint32_t ink_mute;

    /* marcas de datos */
    uint32_t trace;
    uint32_t accent;
    uint32_t passband;

    /* estado */
    uint32_t ok;
    uint32_t warn;
    uint32_t crit;

} palette_t;

/* Revision 1: superficies oscuras, saltos de 8-12 unidades. Por defecto.
 * Validada en hardware: el panel resuelve pasos de 4, asi que los distingue. */
static const palette_t k_pal_oscura = {
    "Oscura",
    0x0D0F12, 0x15181D, 0x1E232A, 0x2A313A, 0x323A45, 0x1E242C, 0x1B2027,
    0xEEF2F7, 0x9AA5B3, 0x646E7C,
    0x17A394, 0xC77A14, 0x3B7FD4,
    0x2F9E52, 0xC77A14, 0xD94040
};

/* Revision 2: negro real y saltos ~3 veces mayores. Se conserva porque para
 * uso a pleno sol, donde el reflejo levanta el negro de verdad, el contraste
 * extra probablemente compense el aspecto mas lavado en interior. */
static const palette_t k_pal_contraste = {
    "Contrastada",
    0x000000, 0x24282E, 0x3C424B, 0x555D68, 0x6A7381, 0x2A2F36, 0x2E343C,
    0xF2F5F8, 0xAEB6C0, 0x868F99,
    0x17A394, 0xC77A14, 0x3B7FD4,
    0x2F9E52, 0xC77A14, 0xD94040
};

/*
 * NOTA sobre la tinta apagada, 22/09/2026: ink_mute subio 5 unidades en
 * la oscura y 10 en la contrastada. Lo encontro tools/paleta_check.py al
 * escribirlo: sobre las superficies ELEVADAS -los tiles, surf_2- la
 * relacion de contraste se quedaba en 2,84 y 2,71 a uno, por debajo del
 * 3 a 1 que pide un texto secundario. Contra el fondo de pantalla iba
 * sobrada, y por eso no habia cantado nunca: el renglon de ayuda de cada
 * casilla no se lee sobre el fondo, se lee sobre la casilla.
 *
 * La validacion que se hizo en la placa fue de RESOLUCION -que el panel
 * distinga los escalones-, no de contraste de texto, asi que subirla no
 * contradice aquello: son dos comprobaciones distintas y hasta ahora
 * solo se habia hecho una.
 */

/*
 * TEMAS ADICIONALES, 22/09/2026.
 *
 * Los dos de arriba se diferencian en cuanta luz tienen. Estos dos se
 * diferencian en el TONO, que es lo que de verdad cansa o descansa la
 * vista segun la hora y la luz que haya en la habitacion.
 *
 * Las marcas de datos -traza, acento y banda de paso- son las MISMAS en
 * los cuatro. No es pereza: ese trio esta validado como conjunto
 * categorico y cambiarlo por tema significaria volver a validar cuatro
 * conjuntos y arriesgarse a que uno pase de largo. Se comprobo que el
 * trio sigue pasando las cinco comprobaciones contra las superficies
 * nuevas -mismos numeros: dE 14,3 en protanopia, 16,3 en vision normal,
 * los tres por encima de 3 a 1 de contraste-, asi que lo que cambia de
 * un tema a otro es el mundo sobre el que se dibujan, no las marcas.
 */

/* Grises calidos, sin apenas azul. Para la noche y para luz de bombilla:
 * el azul es lo que mas espabila al ojo, y aqui casi no hay. */
static const palette_t k_pal_ambar = {
    "Ámbar",
    0x120F0B, 0x1C1811, 0x272118, 0x342C21, 0x3E3428, 0x261F16, 0x221C14,
    0xF8F2E8, 0xB9AB92, 0x7F7360,
    0x17A394, 0xC77A14, 0x3B7FD4,
    0x2F9E52, 0xC77A14, 0xD94040
};

/* Grises azulados. Es la mas parecida a la oscura de siempre, un punto
 * mas fria, y la que mejor se lleva con luz de dia entrando por una
 * ventana. */
static const palette_t k_pal_fria = {
    "Fría",
    0x0A0F15, 0x131A22, 0x1C2430, 0x27313F, 0x313C4B, 0x1B232D, 0x18202A,
    0xEEF4FB, 0x9DABBC, 0x667381,
    0x17A394, 0xC77A14, 0x3B7FD4,
    0x2F9E52, 0xC77A14, 0xD94040
};

/* La paleta activa. Cambiarla y repintar es todo lo que hace falta. */
extern const palette_t *g_pal;

/*
 * Los nombres PAL_* siguen existiendo y ahora leen de la paleta activa. Se
 * usan como argumento de funciones (gfx2_rgb(PAL_SURF_0)) y como
 * inicializador de arrays automaticos, que en C99 admiten valores no
 * constantes, asi que el cambio no obliga a tocar ningun sitio de uso.
 */
#define PAL_SURF_0    (g_pal->surf_0)
#define PAL_SURF_1    (g_pal->surf_1)
#define PAL_SURF_2    (g_pal->surf_2)
#define PAL_SURF_3    (g_pal->surf_3)
#define PAL_LINE      (g_pal->line)
#define PAL_GRID      (g_pal->grid)
#define PAL_HDR_TOP   (g_pal->hdr_top)
#define PAL_INK       (g_pal->ink)
#define PAL_INK_DIM   (g_pal->ink_dim)
#define PAL_INK_MUTE  (g_pal->ink_mute)
#define PAL_TRACE     (g_pal->trace)
#define PAL_ACCENT    (g_pal->accent)
#define PAL_PASSBAND  (g_pal->passband)
#define PAL_OK        (g_pal->ok)
#define PAL_WARN      (g_pal->warn)
#define PAL_CRIT      (g_pal->crit)

/*
 * PANTALLA DE ARRANQUE
 *
 * No tiene colores propios: usa los MISMOS que la pantalla principal. Empezo
 * siendo verde sobre negro, estilo Matrix, y se cambio a proposito - el
 * arranque es lo primero que se ve del aparato y decia una cosa mientras el
 * resto decia otra.
 *
 * La estela cae por la rampa de tinta -ink, ink_dim, ink_mute, line-, que ya
 * esta ordenada de mas claro a mas oscuro y validada en el panel; el titulo
 * va en el ambar de acento, el mismo del digito de sintonia y de los chips
 * encendidos. Son alias, no valores: el dia que cambie la paleta, el arranque
 * cambia con ella sin tocar nada.
 */
#define PAL_SP_BG     (g_pal->surf_0)
#define PAL_SP_HEAD   (g_pal->ink)
#define PAL_SP_HOT    (g_pal->ink_dim)
#define PAL_SP_MID    (g_pal->ink_mute)
#define PAL_SP_DIM    (g_pal->line)
#define PAL_SP_TITLE  (g_pal->accent)
#define PAL_SP_SUB    (g_pal->ink_mute)

#endif /* PALETTE_H */
