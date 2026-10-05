#ifndef GFX2_H_INCLUDED
#define GFX2_H_INCLUDED

#include <stdint.h>
#include "gfx2_font.h"

/*
 * gfx2 - nucleo grafico con compositor por bandas para el DEEPSDR 101.
 *
 * QUE PROBLEMA RESUELVE
 * ---------------------
 * El gfx.c actual escribe directo a la GRAM del panel. Eso impide tres
 * cosas a la vez: antialiasing (no se puede leer el fondo para mezclar),
 * transparencias, y actualizacion sin parpadeo. Y ademas resulta lento
 * donde mas duele: gfx_char() abre una ventana del controlador POR CADA
 * PIXEL del glyph, 17 accesos al bus para pintar uno.
 *
 * gfx2 compone en RAM y vuelca. Un framebuffer completo no cabe
 * (800*480*2 = 750KB frente a 192KB de SRAM), asi que compone por BANDAS
 * horizontales: se reserva una banda de GFX2_BAND_H filas, el codigo de
 * dibujo se ejecuta una vez por banda con recorte vertical, y cada banda
 * sale al panel con UNA sola ventana.
 *
 * El coste de reejecutar el dibujo por banda es CPU, que sobra; lo que no
 * sobra son accesos al bus EXMC, que es justo lo que esto minimiza.
 *
 * PRESUPUESTO DE RAM
 * ------------------
 * La banda es 800 x GFX2_BAND_H x 2 bytes. Con BAND_H=24 son 37.5KB.
 * Eso solo cabe si el waterfall pasa a guardarse como indices de 8 bits
 * en vez de RGB565 (libera 56KB) - ver wf_cmap.h y el comentario de
 * gfx2_wf_blit().
 */

#define GFX2_W      800
#define GFX2_H      480
/*
 * Altura de la banda compositora, en filas. El gasto de RAM es 800 * n * 2
 * bytes, asi que durante el port por etapas conviene empezar pequeno:
 *
 *   ETAPA 1:          8 filas = 12,5 KB, que era lo que cabia con el
 *                     waterfall todavia guardado en RGB565.
 *   ETAPA 3 (ahora): 24 filas = 37,5 KB, ya con el waterfall en indices de
 *                     8 bits, que libero 56 KB.
 *
 * Mas filas significa menos bandas, y por tanto menos veces que se reejecuta
 * el codigo de dibujo por cada repintado. Se puede forzar desde la linea de
 * compilacion con -DGFX2_BAND_H=n.
 */
/*
 * ETAPA 5 (25/09/2026): 16 filas = 25 kB, bajando desde 24 = 37,5 kB.
 *
 * Los 12.800 bytes que se liberan son para FT8, que con todo dentro se
 * pasaba de RAM por 4.460. Es la palanca mas barata que hay: los otros
 * sitios donde queda memoria son la cascada -que ya la comparte con FT8
 * por una union- y estrechar la ventana de FT8, que cuesta
 * decodificaciones.
 *
 * LO QUE CUESTA. Menos filas por banda son mas bandas por repintado, o sea
 * mas veces que se recorre el codigo de dibujo y mas ventanas abiertas en
 * el panel. Medido en el simulador con sus contadores: el espectro pasa de
 * 18 ventanas a 26 por fotograma, que a 17 accesos por ventana son 136
 * accesos mas de 157.000 - menos del uno por mil. El montaje sube algo mas,
 * del orden de un milisegundo, porque el coste por columna se paga una vez
 * por banda.
 *
 * Un milisegundo de fotograma a cambio de que FT8 quepa. Se paga con gusto.
 */
/*
 * ETAPA 6 (25/09/2026, el mismo dia por la tarde): 8 filas = 12,5 kB. O sea
 * de vuelta a lo que habia en la etapa 1, y por una razon bonita.
 *
 * *** Por el dueno del proyecto, despues de comparar con el proyecto padre:
 * "ok dale". ***
 *
 * DE DONDE VIENE. FT8 no tiene memoria propia: le pide prestado el buffer de
 * la cascada. El proyecto padre guarda la cascada en RGB565 -dos bytes por
 * punto, 114.624 en total- y por eso puede prestarle a FT8 sus 1.600 Hz de
 * ventana enteros. Nosotros la guardamos como INDICES DE PALETA, un byte,
 * 54.432... y esa optimizacion, que fue un acierto y que ahorro 56 kB, es
 * justo la que nos dejaba la ventana a la mitad. No habia nada que copiarles:
 * simplemente gastan el doble en cascada.
 *
 * Los 12.800 bytes que libera esta etapa son exactamente lo que faltaba para
 * los 256 bins. Con ellos la ventana de FT8 llega a 1.600 Hz, que ademas es
 * TODO lo que se puede ver: 1.600 Hz es el Nyquist del audio diezmado a
 * 3.200, asi que a partir de aqui ensanchar no significa nada.
 *
 * LO QUE CUESTA, MEDIDO CON EL BANCO DEL ESPECTRO Y NO ESTIMADO:
 *
 *     banda 16 filas ....  26 ventanas de bus, 157.690 accesos
 *     banda  8 filas ....  52 ventanas de bus, 158.132 accesos  (+0,3%)
 *
 * El trabajo de pixeles es el mismo; lo que se dobla es el numero de
 * pasadas, y con el el coste por columna, que se paga una vez por banda.
 * Seran un par de milisegundos sobre los ~20 del fotograma.
 *
 * Y NO LO PAGA FT8: en FT8 no se pinta espectro, el panel es texto. Lo pagan
 * AM, SSB, RTTY y los demas, que es justo donde se noto el trabajo de hacer
 * el fotograma rapido. Por eso esto se pregunto antes de hacerlo en vez de
 * decidirlo solo.
 */
/*
 * ETAPA 7 (25/09/2026, un rato despues): de vuelta a 16 filas, y sin
 * devolver nada a cambio.
 *
 * *** Por el dueno del proyecto, probando la etapa 6 en antena: "estoy
 * probando lsb en 7 mhz y se nota que ha bajado un poco la velocidad". ***
 *
 * O sea que el par de milisegundos que la etapa 6 daba por buenos se NOTAN.
 * Lo vio en la radio, no en un banco, y eso manda.
 *
 * Lo que hizo falta para deshacerlo sin perder los 1.600 Hz fue arreglar el
 * derroche que habia detras: FT8 guardaba una copia ENTERA de mag[] -23.808
 * bytes- para poder decodificar mientras la captura siguiente escribia. Ahora
 * se aparta lo NUEVO en vez de lo viejo, y para eso bastan 6.144 (ver `lado`
 * en ft8_shared_ram.h). Los 17.664 que sobran devuelven estas ocho filas y
 * aun dejan sitio.
 *
 * La leccion, que es la de siempre en este proyecto: cuando dos cosas se
 * pelean por la memoria, antes de quitarle a una conviene mirar si la otra
 * la esta gastando bien. Aqui no la estaba gastando bien.
 */
#ifndef GFX2_BAND_H
#define GFX2_BAND_H 16
#endif

/* La banda compositora, prestada.
 *
 * Existe porque el volcado de la cascada por IPA (ver ipa_blit.h) necesita
 * un destino en RAM donde el acelerador deje los pixeles ya convertidos a
 * RGB565, y ese destino ya existe: es esta misma banda de 800 x GFX2_BAND_H
 * que usa gfx2_wf_blit(). Darle otra seria pedir 37,5 kB mas de RAM para
 * tener dos veces lo mismo.
 *
 * Se presta y se devuelve dentro de la misma llamada: quien la use no puede
 * quedarse con el puntero de una vez para otra, porque entre medias la usa
 * el dibujo normal. Por eso se pide con coge() y se devuelve con suelta(),
 * en vez de tener un gfx2_banda() que la entregue a quien pase: el cerrojo
 * convierte "no se usan a la vez" de promesa en condicion comprobada, que es
 * la leccion que costo cara el 24/09/2026 con los borradores de spi_flash.c.
 *
 * coge() devuelve NULL si ya estaba cogida. Quien la pide TIENE que mirarlo. */
uint16_t *gfx2_banda_coge(void);
void      gfx2_banda_suelta(void);
/* La banda para quien ya la tiene cogida; 0 si no la tiene. Ver
 * gfx2_banda_mia() en gfx2.c. */
uint16_t *gfx2_banda_mia(void);

uint16_t  gfx2_banda_filas(void);
/* Cuantos pixeles caben en la banda en total. Lo necesita quien la use para
 * algo que no sea "una tira de GFX2_W de ancho" - por ejemplo el espectro,
 * que la parte en dos mitades para montar una mientras el DMA envia la
 * otra, y tiene que poder comprobar que las dos caben. */
uint32_t  gfx2_banda_pixeles(void);

/* Veces que alguien pidio la banda estando cogida, desde el arranque.
 * Deberia ser cero para siempre: gfx2_render() la coge y la suelta dentro de
 * la misma llamada, y spectrum_draw() se llama despues, nunca a la vez. Si
 * esto no es cero, hay un camino nuevo que las solapa. */
uint16_t  gfx2_banda_choques(void);

/* --- Superficie de composicion ------------------------------------------
 * Un rectangulo de pixeles en RAM mas su posicion en pantalla. Las
 * primitivas reciben coordenadas de PANTALLA y recortan solas contra la
 * banda, asi que el codigo de dibujo se escribe una vez y no se entera de
 * que esta corriendo por bandas. */
typedef struct {
    uint16_t *px;    /* buffer de w*h pixeles RGB565 */
    int16_t   x, y;  /* esquina superior izquierda en coordenadas de pantalla */
    int16_t   w, h;
} gfx2_surf_t;
/* La misma banda vista solo entre dos filas, para que una capa que se
 * recorta sola con su ventana siga recortandose al montar la pantalla
 * entera. Ver gfx2_sub_y() en gfx2.c. */
gfx2_surf_t gfx2_sub_y(const gfx2_surf_t *s, int16_t y, int16_t h);

/* Color en formato de trabajo: RGB888 + alpha, para poder mezclar bien.
 * Se convierte a RGB565 solo al escribir en la banda. */
typedef struct { uint8_t r, g, b, a; } gfx2_rgba_t;

static inline gfx2_rgba_t gfx2_rgb(uint32_t hex)
{
    gfx2_rgba_t c;
    c.r = (uint8_t)((hex >> 16) & 0xFF);
    c.g = (uint8_t)((hex >> 8) & 0xFF);
    c.b = (uint8_t)(hex & 0xFF);
    c.a = 255;
    return c;
}

static inline gfx2_rgba_t gfx2_rgba(uint32_t hex, uint8_t a)
{
    gfx2_rgba_t c = gfx2_rgb(hex);
    c.a = a;
    return c;
}

static inline uint16_t gfx2_to565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8) << 8) |
                      ((uint16_t)(g & 0xFC) << 3) |
                      ((uint16_t)b >> 3));
}

/* Cierto si el rango vertical [y, y+h) toca la banda que se esta componiendo.
 * Las primitivas y el codigo de dibujo lo usan para salir cuanto antes: el
 * callback de dibujo se ejecuta una vez POR BANDA (20 para pantalla completa),
 * asi que todo lo que no se corte pronto se paga 20 veces. */
static inline int gfx2_band_hits(const gfx2_surf_t *s, int16_t y, int16_t h)
{
    return !(y >= (int16_t)(s->y + s->h) || (int16_t)(y + h) <= s->y);
}

/* --- Render por bandas --------------------------------------------------
 * Ejecuta draw() una vez por banda que interseque el rectangulo pedido y
 * vuelca cada banda al panel. Es el unico punto que habla con el driver. */
void gfx2_render(int16_t x, int16_t y, int16_t w, int16_t h,
                 void (*draw)(gfx2_surf_t *s, void *ctx), void *ctx);

/* Igual, pero para toda la pantalla. */
void gfx2_render_screen(void (*draw)(gfx2_surf_t *s, void *ctx), void *ctx);

/* Registra una funcion que se llama tras volcar cada banda. Pensada para
 * muestrear la entrada durante un repintado largo. 0 la desactiva. */
void gfx2_set_pump(void (*fn)(void));

/* --- Primitivas ---------------------------------------------------------
 * Todas toman coordenadas de PANTALLA y mezclan segun el alpha del color. */
void gfx2_fill(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
               gfx2_rgba_t c);

/* Degradado vertical de c0 (arriba) a c1 (abajo). Usa dithering ordenado
 * 4x4 para que la cuantizacion a RGB565 no produzca bandas visibles:
 * en un degradado de 64px con 32 niveles de verde, sin dither se ven los
 * escalones. */
void gfx2_vgrad(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                gfx2_rgba_t c0, gfx2_rgba_t c1);

/* Rectangulo de esquinas redondeadas, con bordes antialiasados. r=0 da un
 * rectangulo normal. */
void gfx2_rrect(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                int16_t r, gfx2_rgba_t c);

/* Solo el contorno, grosor t, tambien con esquinas antialiasadas. */
void gfx2_rrect_outline(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                        int16_t r, int16_t t, gfx2_rgba_t c);

/* Una raya de cualquier angulo (Bresenham), recortada contra la banda.
 * Ver su comentario en gfx2.c: la costa del mundo son 1.762 puntos y el
 * dibujo corre una vez por banda, asi que lo que importa es que salga
 * pronto cuando no toca. */
void gfx2_line(gfx2_surf_t *s, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
               gfx2_rgba_t c);

void gfx2_hline(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, gfx2_rgba_t c);
void gfx2_vline(gfx2_surf_t *s, int16_t x, int16_t y, int16_t h, gfx2_rgba_t c);

/* --- Texto -------------------------------------------------------------- */
typedef enum { GFX2_ALIGN_L = 0, GFX2_ALIGN_C, GFX2_ALIGN_R } gfx2_align_t;

/* Dibuja str con la linea superior en y. Devuelve el ancho consumido. */
int16_t gfx2_text(gfx2_surf_t *s, int16_t x, int16_t y, const char *str,
                  const gfx2_font_t *f, gfx2_rgba_t c);

/* Igual, pero alineado dentro de [x, x+w). */
void gfx2_text_in(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w,
                  const char *str, const gfx2_font_t *f, gfx2_rgba_t c,
                  gfx2_align_t al);

/* Ancho de str en px, sin dibujar. Se puede llamar sin superficie. */
int16_t gfx2_text_w(const char *str, const gfx2_font_t *f);

/* --- Volcado del waterfall ----------------------------------------------
 * idx es el buffer de indices de 8 bits (no RGB565): el ahorro de 56KB que
 * paga la banda compositora. La conversion a color se hace aqui con la LUT,
 * lo que tiene un efecto secundario util: cambiar de colormap repinta todo
 * el historial, no solo las filas nuevas. */
void gfx2_wf_blit(int16_t x, int16_t y, int16_t w, int16_t rows,
                  const uint8_t *idx, int16_t stride, int16_t head,
                  const uint16_t *cmap);

#endif /* GFX2_H_INCLUDED */
