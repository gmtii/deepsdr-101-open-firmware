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

    /*
     * EL MAPA SIGUE AL TEMA - 05/10/2026, de un issue.
     *
     * mapa.c nacio con NUEVE colores escritos a pelo y cero referencias a
     * esta paleta: mar, rejilla, ecuador, costa, los dos botones de zoom y
     * la casa. Siempre azul oscuro, pusieras el tema que pusieras.
     *
     * Esto NO se arregla armonizando el mapa para todos: los seis temas de
     * antes se disenaron con ese mapa azul delante, y cambiarselo de golpe
     * es cambiarle la pantalla a quien no ha pedido nada. Asi que el mapa
     * sigue al tema SOLO donde el tema lo pide, y hoy lo pide uno.
     *
     * Quien anada un tema claro nuevo pone este 1 y se lleva el mapa
     * detras; quien no lo ponga se queda con el azul de siempre, que es lo
     * que ya tenia.
     */
    uint8_t mapa_sigue;

} palette_t;

/* Revision 1: superficies oscuras, saltos de 8-12 unidades. Por defecto.
 * Validada en hardware: el panel resuelve pasos de 4, asi que los distingue. */
static const palette_t k_pal_oscura = {
    "Oscura",
    0x0D0F12, 0x15181D, 0x1E232A, 0x2A313A, 0x323A45, 0x1E242C, 0x1B2027,
    0xEEF2F7, 0x9AA5B3, 0x646E7C,
    0x17A394, 0xC77A14, 0x3B7FD4,
    0x2F9E52, 0xC77A14, 0xD94040,
    0U    /* mapa: el azul de siempre */
};

/* Revision 2: negro real y saltos ~3 veces mayores. Se conserva porque para
 * uso a pleno sol, donde el reflejo levanta el negro de verdad, el contraste
 * extra probablemente compense el aspecto mas lavado en interior. */
static const palette_t k_pal_contraste = {
    "Contrastada",
    0x000000, 0x24282E, 0x3C424B, 0x555D68, 0x6A7381, 0x2A2F36, 0x2E343C,
    0xF2F5F8, 0xAEB6C0, 0x868F99,
    0x17A394, 0xC77A14, 0x3B7FD4,
    0x2F9E52, 0xC77A14, 0xD94040,
    0U    /* mapa: el azul de siempre */
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
    0x2F9E52, 0xC77A14, 0xD94040,
    0U    /* mapa: el azul de siempre */
};

/* Grises azulados. Es la mas parecida a la oscura de siempre, un punto
 * mas fria, y la que mejor se lleva con luz de dia entrando por una
 * ventana. */
static const palette_t k_pal_fria = {
    "Fría",
    0x0A0F15, 0x131A22, 0x1C2430, 0x27313F, 0x313C4B, 0x1B232D, 0x18202A,
    0xEEF4FB, 0x9DABBC, 0x667381,
    0x17A394, 0xC77A14, 0x3B7FD4,
    0x2F9E52, 0xC77A14, 0xD94040,
    0U    /* mapa: el azul de siempre */
};

/*
 * ===========================================================================
 * TRES TEMAS MAS, 23/09/2026 - de una foto de otro firmware que le gusto al
 * dueno del proyecto.
 * ===========================================================================
 * Los dos primeros son temas CLAROS con tinta negra, y eso no es "cambiar
 * tres numeros". Esta paleta esta disenada al reves de lo que piden: las
 * superficies ACLARAN segun se elevan y la tinta OSCURECE segun pierde
 * importancia. En un tema claro las dos rampas se invierten a la vez. Copiar
 * el fondo de la foto y dejar los otros trece colores como estaban da una
 * pantalla donde el renglon de ayuda de cada casilla desaparece y los tiles
 * no se separan del fondo - y eso no lo avisa nadie, se ve en la radio.
 *
 * Asi que estos tres NO estan escritos a ojo: los genera
 * tools/tema_disena.py del simulador. Los colores medidos de la foto entran
 * como semilla (fondo, barra y boton pulsado) y las dos rampas salen por
 * regla - "alejate del fondo hasta llegar a 4,5:1 / 3,2:1 de contraste"-,
 * bisecando, para que el alejamiento sea el MINIMO que cumple y el tono se
 * conserve.
 *
 * LAS MARCAS SI CAMBIAN, y es la diferencia de fondo con los cuatro temas de
 * arriba. Aquellos comparten el trio teal/ambar/azul porque todos tienen
 * superficies oscuras. Sobre el oliva ese trio se queda en 1,2 a 1,6 a uno
 * de contraste: invisible. Sobre un fondo claro de L=0,36 la unica direccion
 * posible es hacia abajo, asi que las marcas de estos dos temas son oscuras
 * a la fuerza. Se ELIGEN buscando, no proponiendo: cada marca tiene su arco
 * de tono (para que el tema siga pareciendo el tema) y dentro de el se
 * maximiza la peor distancia de color entre pares y contra el fondo,
 * incluyendo protanopia, deuteranopia y tritanopia.
 *
 * QUE VALEN ESOS NUMEROS. Los dE que saca ese script NO son comparables con
 * los que hay escritos mas arriba: aquellos venian de un validador externo
 * con otro modelo de simulacion de daltonismo. Es otra regla de medir. Por
 * eso el criterio usado es RELATIVO - medido con ESA regla, el trio de cada
 * tema nuevo tiene que quedar por encima del trio que ya lleva la radio-, y
 * eso se cumple con holgura:
 *
 *     Oscura (el de siempre)   peor par  0,0   <- tritanopia, traza/banda
 *     Oliva                    peor par 18,8
 *     Naranja                  peor par 16,6
 *     Fosforo                  peor par  9,8
 *
 * El cero del tema de siempre no es un fallo nuevo: palette.h ya lo decia
 * arriba con otras palabras ("el par traza/VFO queda en dE 6,0 en
 * tritanopia, banda admisible solo con codificacion secundaria"). La
 * codificacion secundaria -area rellena, linea con cabeza, rectangulo
 * translucido- es la que hace el trabajo en ese caso, y sigue ahi.
 *
 * Fosforo se queda en 9,8 A PROPOSITO: al optimizador le sale mejor nota
 * subiendo la traza a un verde claro, pero entonces la traza y el texto son
 * el mismo color y el tema deja de ser un tema de fosforo. Se le puso tope
 * de luz a la traza y a la banda de paso, y 9,8 es lo que hay dentro de ese
 * tope. Sigue siendo mucho mas que el 0,0 que lleva la radio hoy.
 */

/* Verde oliva con tinta negra. Fondo, barra y pulsado son los de la foto;
 * lo demas esta calculado. */
static const palette_t k_pal_oliva = {
    "Oliva",
    0x9BAA50, 0x949E4F, 0x8B954B, 0x848E47, 0x798141, 0x95A34D, 0x98A550,
    0x000000, 0x282A15, 0x3E4321,
    0x095E49, 0xA31010, 0x4E18F0,
    0x1C5E43, 0x664F0A, 0x4F0808,
    0U    /* mapa: el azul de siempre */
};

/* Naranja fuerte con tinta negra. El mas duro de los tres para las marcas:
 * con el fondo en L=0,32 no hay sitio hacia arriba, asi que las tres marcas
 * son oscuras y el margen que queda es el mas justo de los dos claros. */
static const palette_t k_pal_naranja = {
    "Naranja",
    0xFF6C25, 0xF06422, 0xE2601F, 0xD75C1D, 0xCB561B, 0xF76924, 0xF96924,
    0x000000, 0x3A1808, 0x62290D,
    0x214E6E, 0x300530, 0x1313C2,
    0x104F15, 0x301E05, 0x940F30,
    0U    /* mapa: el azul de siempre */
};

/* Fosforo: casi negro con tinta verde, como un terminal. Aqui la rampa va
 * en el sentido de siempre; lo que cambia es que todo el mundo es verde. */
static const palette_t k_pal_fosforo = {
    "Fósforo",
    0x081810, 0x091F14, 0x0A2618, 0x05452A, 0x0C3320, 0x0B2F1E, 0x081C12,
    0x27FF9B, 0x1A9D60, 0x167F4D,
    0x388C70, 0xFFD91A, 0x1168A6,
    0x1AFF9F, 0xFFE41A, 0xC91432,
    0U    /* mapa: el azul de siempre */
};

/*
 * ===========================================================================
 * CHEMISTRY - 05/10/2026, de un issue, y la paleta la escribio el que
 * abrio el issue.
 * ===========================================================================
 * "my LCD must be far less capable of displaying low intensity colours than
 * yours. This means that pretty much all of the palette options are
 * completely unusable for me. Anywhere where you have decided on any colour
 * with a main component less than 7F makes it unreadable on my screen."
 *
 * Su panel APLASTA LA ZONA OSCURA: por debajo de 0x7F todo se le va al
 * negro, asi que no distingue un tono oscuro de otro. Los seis temas de
 * antes construyen la jerarquia de superficies ahi abajo -la oscura va de
 * 0x0D a 0x2A-, de modo que para el son una mancha plana. No es que se vean
 * mal: es que no se ven.
 *
 * DOS COSAS QUE CONFIRMAN EL DIAGNOSTICO, y las dos las cuenta el en el
 * issue sin saber que lo confirman:
 *
 *   "I have tried the brightness on 100% and it's still not clear". Claro:
 *   subir la retroiluminacion sube tambien el negro, asi que los tonos
 *   oscuros no se SEPARAN. No hay ajuste de brillo que arregle un panel que
 *   aplasta sombras; la unica salida es no usar tonos oscuros.
 *
 *   "when I tried to take a picture it was readable, I guess the phone
 *   camera is more sensitive to IR or something". No es infrarrojo. Es la
 *   curva de tono del movil, que levanta mucho las sombras - exactamente lo
 *   que ya esta escrito en la cabecera de este fichero, donde esa misma
 *   curva nos hizo tirar una paleta buena creyendo que era mala. Alli la
 *   camara nos engano; aqui le revela a el un texto que su panel no le
 *   ensena. Mismo fenomeno, direcciones contrarias.
 *
 * LA PALETA ES SUYA, NO MIA. Surfaces y tinta van TAL CUAL las mando, sin
 * redondear ni "mejorar": las ajusto yo a ojo en un panel que no tengo
 * delante y las rompo. El es el instrumento de medida aqui. Y pasan enteras
 * tools/paleta_check.py: los cuatro saltos de superficie y la tinta con
 * 21:1 sobre el fondo, que es el mejor numero de los ocho temas.
 *
 * LOS DOS QUE SI HE CAMBIADO, y por que. paleta_check.py no mira ok/warn, y
 * ahi su paleta tenia dos agujeros que el no podia ver porque no corre el
 * script:
 *
 *     ok    #00FF00 sobre blanco   1,37:1
 *     warn  #FFD700 sobre blanco   1,40:1
 *
 * Verde puro y oro sobre papel blanco. Eso no es cosmetico, apaga cuatro
 * cosas: la LUZ DE ENGANCHE de los modos digitales (ui_digi.c pinta ok
 * enganchado y line suelto - con su paleta los dos quedaban a ~1,3:1 contra
 * el blanco, o sea que la luz dejaba de decir nada), el ICONO DE BATERIA,
 * el TEXTO DE AVISO del teclado y los MARCADORES del espectro.
 *
 * Los de aqui cumplen las DOS reglas a la vez, la del proyecto y la suya:
 *
 *     ok    #00A000   3,48:1 sobre blanco   componente mayor 0xA0  >= 0x7F
 *     warn  #C47A00   3,43:1 sobre blanco   componente mayor 0xC4  >= 0x7F
 *
 * o sea que se ven en un panel normal Y le siguen quedando por encima de su
 * umbral, que es lo que no conseguia ningun verde oscuro de los habituales.
 * El crit que el eligio, #FF0029, ya cumplia las dos (3,97:1 y 0xFF) y se
 * queda como esta.
 *
 * EL MAPA. Es la otra mitad del issue: "the world map uses its own palette
 * ... the outlines for the continents are barely legible for me". Cierto,
 * mapa.c llevaba nueve colores a pelo. Este es el unico tema con
 * mapa_sigue = 1, asi que el mapa se vuelve claro SOLO aqui y los demas
 * temas no se enteran. Ver mapa.c.
 *
 * LO QUE NO SE HA PODIDO COMPROBAR. Ninguno de nosotros dos tiene ese
 * panel. Las superficies y la tinta las ha probado el en el suyo; los dos
 * colores de estado y el mapa estan medidos, no vistos. Si vuelve a
 * escribir, eso es lo que hay que preguntarle.
 */
static const palette_t k_pal_chemistry = {
    "Chemistry",
    0xFFFFFF, 0xEDEDED, 0xE9E9E9, 0xCCCCCC, 0xC7C7C7, 0xF0F0F0, 0xFFFFFF,
    0x000000, 0x000000, 0x7F7F7F,
    0x919191, 0x6B6B6B, 0x000000,
    0x00A000, 0xC47A00, 0xFF0029,
    1U    /* el mapa sigue a este tema */
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
