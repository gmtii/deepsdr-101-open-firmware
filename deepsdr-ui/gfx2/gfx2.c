#include "gfx2.h"
#include "rm68120_exmc.h"
#include <string.h>

#ifdef GFX2_PROFILE
unsigned long g_blend_calls = 0, g_rrcov_calls = 0, g_draw_calls = 0;
#define PROF_BLEND() (g_blend_calls++)
#define PROF_RRCOV() (g_rrcov_calls++)
#define PROF_DRAW()  (g_draw_calls++)
#else
#define PROF_BLEND() ((void)0)
#define PROF_RRCOV() ((void)0)
#define PROF_DRAW()  ((void)0)
#endif

/* La banda compositora. Unica reserva grande de este modulo. */
static uint16_t s_band[GFX2_W * GFX2_BAND_H];

/* Ver el comentario de gfx2_banda_coge() en la cabecera. */
static uint8_t  s_band_cogida;
static uint16_t s_band_choques;

uint16_t *gfx2_banda_coge(void)
{
    if (s_band_cogida) {
        if (s_band_choques < 0xFFFFU) { s_band_choques++; }
        return (uint16_t *)0;
    }
    s_band_cogida = 1U;
    return s_band;
}

void gfx2_banda_suelta(void)
{
    s_band_cogida = 0U;
}

uint16_t gfx2_banda_choques(void)
{
    return s_band_choques;
}

/*
 * La banda, para quien YA la tiene cogida - 05/10/2026.
 *
 * La necesita la captura de pantalla, que se queda la banda durante los
 * veinticinco segundos que dura y la va rellenando a lo largo de muchas
 * vueltas del bucle principal: volver a pedirla con gfx2_banda_coge()
 * contaria un choque y devolveria cero, que es lo correcto para cualquier
 * otro y lo contrario de lo que hace falta aqui.
 *
 * Devuelve cero si NO esta cogida, a proposito: asi un fallo de secuencia
 * -usarla sin haberla pedido- se nota en vez de escribir donde no toca.
 */
/*
 * UNA SUBBANDA RECORTADA EN VERTICAL - 05/10/2026.
 *
 * La necesita la captura de pantalla. Cuando una capa se pinta por
 * gfx2_render(), la ventana que se pide LA RECORTA por construccion: la
 * marquesina de la cabecera, por ejemplo, pinta su degradado de y=0 a y=63
 * y se queda en y=10..53 porque su ventana es esa. Montando la pantalla
 * entera en una banda de 16 filas no hay ventana que recorte, asi que esa
 * capa se saldria y taparia la raya de abajo de la cabecera.
 *
 * Esto devuelve la MISMA banda vista solo entre dos filas. Solo recorta en
 * vertical a proposito: el paso de fila sigue siendo el de la banda de
 * verdad, asi que no hay que tocar ni una primitiva. En horizontal no hace
 * falta - las capas que lo necesitan ya se limitan solas.
 *
 * Con h <= 0 la subbanda sale vacia y todo lo que se pinte en ella se
 * descarta, que es justo lo que tiene que pasar cuando la capa no cae en
 * esta franja.
 */
gfx2_surf_t gfx2_sub_y(const gfx2_surf_t *s, int16_t y, int16_t h)
{
    gfx2_surf_t sub = *s;
    int16_t y0 = (s->y > y) ? s->y : y;
    int16_t y1 = ((int32_t)s->y + s->h < (int32_t)y + h)
                 ? (int16_t)((int32_t)s->y + s->h) : (int16_t)((int32_t)y + h);

    if (y1 <= y0) {
        sub.h = 0;
        return sub;
    }
    sub.px = s->px + (int32_t)(y0 - s->y) * s->w;
    sub.y  = y0;
    sub.h  = (int16_t)(y1 - y0);
    return sub;
}

uint16_t *gfx2_banda_mia(void)
{
    return s_band_cogida ? s_band : (uint16_t *)0;
}

uint16_t gfx2_banda_filas(void) { return (uint16_t)GFX2_BAND_H; }
uint32_t gfx2_banda_pixeles(void) { return (uint32_t)GFX2_W * (uint32_t)GFX2_BAND_H; }

/* ------------------------------------------------------------------------
 * Mezcla
 * ------------------------------------------------------------------------ */

/* Mezcla c sobre el pixel RGB565 dst con cobertura cov (0..255).
 * Se expande el destino de 565 a 888 replicando bits altos - la misma
 * expansion que hace el panel - se mezcla en 888 y se vuelve a 565. Hacerlo
 * en 565 directamente acumularia error de cuantizacion en cada capa. */
static inline uint16_t blend(uint16_t dst, gfx2_rgba_t c, uint16_t cov)
{
    uint32_t a = ((uint32_t)c.a * cov) / 255u;
    uint32_t dr, dg, db;

    PROF_BLEND();

    if (a == 0) return dst;

    if (a >= 255) {
        return gfx2_to565(c.r, c.g, c.b);
    }

    dr = (dst >> 11) & 0x1F; dr = (dr << 3) | (dr >> 2);
    dg = (dst >> 5)  & 0x3F; dg = (dg << 2) | (dg >> 4);
    db =  dst        & 0x1F; db = (db << 3) | (db >> 2);

    dr = (c.r * a + dr * (255u - a)) / 255u;
    dg = (c.g * a + dg * (255u - a)) / 255u;
    db = (c.b * a + db * (255u - a)) / 255u;

    return gfx2_to565((uint8_t)dr, (uint8_t)dg, (uint8_t)db);
}

/* --- la ventana de recorte en X (ver gfx2.h) ----------------------------
 * Limites en coordenadas de PANTALLA, x1 exclusivo. Arrancan abiertos de par
 * en par, asi que quien no la use no paga nada mas que dos comparaciones. */
static int16_t s_rec_x0 = (int16_t)-30000;
static int16_t s_rec_x1 = (int16_t) 30000;

void gfx2_recorte_x(int16_t x0, int16_t x1)
{
    s_rec_x0 = x0;
    s_rec_x1 = x1;
}

void gfx2_recorte_quita(void)
{
    s_rec_x0 = (int16_t)-30000;
    s_rec_x1 = (int16_t) 30000;
}

static inline void px_blend(gfx2_surf_t *s, int16_t x, int16_t y,
                            gfx2_rgba_t c, uint16_t cov)
{
    int16_t lx = (int16_t)(x - s->x);
    int16_t ly = (int16_t)(y - s->y);
    uint16_t *p;

    if (x < s_rec_x0 || x >= s_rec_x1) return;
    if (lx < 0 || ly < 0 || lx >= s->w || ly >= s->h) return;

    p = &s->px[(int32_t)ly * s->w + lx];
    *p = blend(*p, c, cov);
}

/* ------------------------------------------------------------------------
 * Render por bandas
 * ------------------------------------------------------------------------ */

/* Gancho que se llama entre bandas. Sirve para muestrear el tactil durante un
 * repintado: sin esto, la entrada solo se lee entre repintados enteros, y si
 * uno tarda un segundo hay que mantener el dedo puesto todo ese tiempo para
 * que se note. */
static void (*s_pump)(void) = 0;

/*
 * ======================================================================
 * EL CHIVATO DEL REPINTADO PARCIAL - 24/10/2026.
 * ======================================================================
 *
 * Encontrado en la revision a fondo pedida por el usuario.
 *
 * El espectro se salta las bandas que no han cambiado, y lo que le autoriza
 * a hacerlo es un contador de gfx.c: si alguien ha pintado dentro de su
 * rectangulo, no se salta nada. El comentario de aquel fichero afirmaba que
 * "todo lo que dibuja en la radio" pasaba por sus cinco funciones,
 * "incluido gfx2_render()". No era verdad: este fichero abre la ventana y
 * empuja los pixeles el mismo, asi que todo lo que pinta el compositor era
 * invisible para esa cuenta.
 *
 * Va por puntero y no por llamada directa a gfx.c, por lo mismo que
 * gfx2_set_pump() y que el gancho de fft.c: gfx2 es un compositor y no
 * tiene por que saber que existe un espectro que se salta bandas. Y, muy
 * concreto, media docena de bancos del simulador enlazan gfx2.c SIN gfx.c:
 * con la llamada escrita por su nombre dejarian de enlazar.
 *
 * Quien no lo ponga -los bancos- no paga nada: una comparacion de puntero
 * por banda.
 */
static void (*s_chivato)(uint16_t, uint16_t, uint16_t, uint16_t) = 0;

void gfx2_set_chivato(void (*fn)(uint16_t, uint16_t, uint16_t, uint16_t))
{
    s_chivato = fn;
}

void gfx2_set_pump(void (*fn)(void))
{
    s_pump = fn;
}

/*
 * El mismo gancho, para quien pinta por bandas SIN pasar por
 * gfx2_render() - 19/10/2026.
 *
 * spectrum_draw() se monta sus bandas el solo: pide la banda con
 * gfx2_banda_coge() y las vuelca con gfx_blit_arranca(). O sea que no
 * pasaba por el bucle de aqui abajo y, mientras pintaba el espectro -siete
 * u once milisegundos de bus en cada fotograma-, el tactil no existia.
 * Que es justo lo que este gancho esta puesto para evitar.
 */
void gfx2_pump(void)
{
    if (s_pump) { s_pump(); }
}

uint8_t gfx2_render(int16_t x, int16_t y, int16_t w, int16_t h,
                    void (*draw)(gfx2_surf_t *s, void *ctx), void *ctx)
{
    int16_t by;
    int16_t banda_h;

    if (w <= 0 || h <= 0) return 0U;
    if (w > GFX2_W) w = GFX2_W;

    /*
     * Y QUE LA VENTANA NO SE SALGA POR LA DERECHA NI POR ABAJO - 13/10/2026.
     *
     * Se veia como una persiana de rayas. La barra pedia repintar 450 px
     * empezando en el 458 de una pantalla de 800, y aqui solo se miraba
     * que el ANCHO cupiera -450 cabe en 800-, no que cupiera DONDE SE
     * PONE. El controlador recibe una ventana de columnas 458..907, no
     * tiene columna 907, y cada fila se derrama en la siguiente.
     *
     * El fallo de verdad estaba en quien pedia la ventana y ya esta
     * arreglado (ver barra_ventana_w() en ui_digi.c), pero recortar aqui
     * cuesta dos restas y convierte una pantalla llena de rayas en un trozo
     * del dibujo que falta, que es un fallo que se ve y se arregla en vez
     * de uno que asusta. Catorce sitios llaman a esto.
     */
    if (x < 0) { w = (int16_t)(w + x); x = 0; }
    if (y < 0) { h = (int16_t)(h + y); y = 0; }
    if (x >= GFX2_W || y >= GFX2_H) { return 0U; }
    if ((int32_t)x + w > GFX2_W) { w = (int16_t)(GFX2_W - x); }
    if ((int32_t)y + h > GFX2_H) { h = (int16_t)(GFX2_H - y); }
    if (w <= 0 || h <= 0) { return 0U; }

    /* La banda es de este modulo pero ya no la usa solo este modulo: desde el
     * 25/09/2026 spectrum_draw() tambien la pide, para montar el espectro por
     * bandas en vez de fila a fila. Coger y soltar en vez de suponer. */
    if (gfx2_banda_coge() == (uint16_t *)0) {
        return 0U;
    }

    /*
     * ==================================================================
     * LA BANDA SE ESTIRA CUANDO LA VENTANA ES ESTRECHA - 01/11/2026.
     * ==================================================================
     *
     * EL PORQUE, del usuario y despues de seis versiones persiguiendo
     * otras cosas: "se ralentiza igual moviendo el mando pa arriba y pa
     * abajo, avanzando varios hercios, sin que llegue a cambiar de
     * pantalla... y en cuanto paro el encoder vuelve a su velocidad
     * normal".
     *
     * Sin cruzar banda, sin reaparcar el oscilador y sin menu, lo unico
     * que corre en cada detente es repintar los digitos. Y eso, medido,
     * son unos 5 ms en su radio; a treinta detentes por segundo es casi
     * una cuarta parte del reloj, sobre un fotograma que ya cuesta 25 ms.
     * De ahi la bajada mientras se gira y la vuelta al soltar.
     *
     * Y la mitad de esos 5 ms no era dibujar: era REPETIR LA MAQUETA. La
     * banda tenia altura FIJA, asi que una franja de 64 filas se
     * componia en cuatro pasadas y en cada una el dibujo rehacia la
     * cuenta entera -los anchos de las cifras, donde empieza cada
     * pastilla, donde acaba la lectura- para quedarse con 16 filas.
     * Cuatro veces el mismo trabajo.
     *
     * El limite de verdad no es la ALTURA, es el BUFFER: caben
     * GFX2_W * GFX2_BAND_H pixeles. Con la ventana entera (800 px de
     * ancho) la altura maxima sigue siendo GFX2_BAND_H y no cambia
     * nada; con la caja de un digito -unos 40 px- caben las 64 filas de
     * una sentada y el dibujo se hace UNA vez.
     *
     * El resultado es el mismo pixel: la banda solo decide en cuantos
     * trozos se compone, no que se pinta.
     */
    {
        int32_t cabe = (int32_t)GFX2_W * (int32_t)GFX2_BAND_H / (int32_t)w;
        if (cabe < 1) { cabe = 1; }
        if (cabe > (int32_t)GFX2_H) { cabe = (int32_t)GFX2_H; }
        banda_h = (int16_t)cabe;
    }

    for (by = y; by < (int16_t)(y + h); by = (int16_t)(by + banda_h)) {
        gfx2_surf_t s;
        int16_t bh = (int16_t)((y + h) - by);
        int32_t n, i;

        if (bh > banda_h) bh = banda_h;

        s.px = s_band;
        s.x = x; s.y = by; s.w = w; s.h = bh;

        /* la banda arranca opaca en negro; el codigo de dibujo pinta el
         * fondo que corresponda como primera capa.
         *
         * memset y no un bucle de uint16 - 05/10/2026. Eran hasta 83.200
         * escrituras de 16 bits por repintado de cabecera, de una en una,
         * para dejar en negro algo que el dibujo va a tapar entero. memset
         * de la biblioteca escribe de palabra en palabra y aqui el buffer
         * esta alineado, asi que es la misma cuenta cuatro veces mas
         * barata. El valor es cero, que es el mismo en los dos bytes de
         * cada pixel, que es lo unico que hace valido cambiar uno por otro. */
        n = (int32_t)w * bh;
        memset(s_band, 0, (size_t)n * sizeof s_band[0]);
        (void)i;

        PROF_DRAW();
        draw(&s, ctx);

        /* una ventana, un volcado.
         *
         * Y SE CRONOMETRA - 05/10/2026. Esto empuja al bus con la CPU
         * parada, sin DMA, y hasta hoy no lo contaba nadie: g_lcd_ciclos
         * solo se sumaba dentro de gfx_blit(), que es de gfx.c y por aqui
         * no pasa. O sea que todo lo que pinta gfx2 -la cabecera, la franja
         * de estado, la marquesina, el eje de dB, el panel digital- era
         * tiempo de bus invisible para la fila "empuje". Ver espera_fin()
         * en lcd_dma.c, que tenia el mismo agujero por el otro lado. */
        {
            uint32_t t0 = rm68120_ciclo();

            if (s_chivato) {
                s_chivato((uint16_t)x, (uint16_t)by,
                          (uint16_t)(x + w - 1), (uint16_t)(by + bh - 1));
            }
            rm68120_set_window((uint16_t)x, (uint16_t)by,
                               (uint16_t)(x + w - 1), (uint16_t)(by + bh - 1));
            for (i = 0; i < n; i++) {
                rm68120_dato(s_band[i]);
            }
            g_lcd_ciclos += rm68120_ciclo() - t0;
        }

        if (s_pump) { s_pump(); }
    }

    gfx2_banda_suelta();
    return 1U;
}

void gfx2_render_screen(void (*draw)(gfx2_surf_t *s, void *ctx), void *ctx)
{
    (void)gfx2_render(0, 0, GFX2_W, GFX2_H, draw, ctx);
}

/* ------------------------------------------------------------------------
 * Primitivas
 * ------------------------------------------------------------------------ */

void gfx2_fill(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
               gfx2_rgba_t c)
{
    int16_t x0 = (int16_t)(x - s->x), y0 = (int16_t)(y - s->y);
    int16_t x1 = (int16_t)(x0 + w),   y1 = (int16_t)(y0 + h);
    int16_t iy, ix;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;

    /* y contra la ventana de recorte, traida a coordenadas locales */
    {
        int16_t r0 = (int16_t)(s_rec_x0 - s->x);
        int16_t r1 = (int16_t)(s_rec_x1 - s->x);
        if (x0 < r0) x0 = r0;
        if (x1 > r1) x1 = r1;
        if (x1 <= x0) return;
    }

    if (c.a >= 255) {
        uint16_t v = gfx2_to565(c.r, c.g, c.b);
        for (iy = y0; iy < y1; iy++) {
            uint16_t *row = &s->px[(int32_t)iy * s->w];
            for (ix = x0; ix < x1; ix++) row[ix] = v;
        }
    } else {
        for (iy = y0; iy < y1; iy++) {
            uint16_t *row = &s->px[(int32_t)iy * s->w];
            for (ix = x0; ix < x1; ix++) row[ix] = blend(row[ix], c, 255);
        }
    }
}

/*
 * UNA RAYA DE CUALQUIER ANGULO - 28/09/2026, para el mapa del mundo.
 *
 * Bresenham de toda la vida, con dos detalles que importan aqui:
 *
 * 1. RECORTA CONTRA LA BANDA, como todas las primitivas de este fichero,
 *    y ademas SALE PRONTO. El dibujo corre una vez por banda -veinte para
 *    la pantalla entera- y la costa del mundo son 1.762 puntos, o sea
 *    35.000 rayas por cuadro si no se cortara nada. Con el recorte de
 *    caja de abajo, cada banda solo recorre de verdad las que la tocan.
 *
 * 2. NO DIBUJA LA RAYA QUE CRUZA EL MAPA ENTERO. Eso no es cosa de esta
 *    funcion sino de quien la llama: en un mapa plano, una costa que pasa
 *    por el meridiano 180 -las Aleutianas, Chukotka, Fiyi- tiene un punto
 *    en x=799 y el siguiente en x=0, y unirlos pinta una raya horizontal
 *    de lado a lado. Ver mapa.c.
 */
void gfx2_line(gfx2_surf_t *s, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
               gfx2_rgba_t c)
{
    int32_t dx, dy, sx, sy, err;
    int16_t ymin = (y0 < y1) ? y0 : y1;
    int16_t ymax = (y0 < y1) ? y1 : y0;
    int16_t xmin = (x0 < x1) ? x0 : x1;
    int16_t xmax = (x0 < x1) ? x1 : x0;
    uint16_t v;

    /* Fuera de la banda: ni se empieza. */
    if (ymax < s->y || ymin >= (int16_t)(s->y + s->h)) { return; }
    if (xmax < s->x || xmin >= (int16_t)(s->x + s->w)) { return; }

    v = gfx2_to565(c.r, c.g, c.b);
    dx = (int32_t)((x1 > x0) ? (x1 - x0) : (x0 - x1));
    dy = -(int32_t)((y1 > y0) ? (y1 - y0) : (y0 - y1));
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = dx + dy;

    for (;;) {
        int16_t px = (int16_t)(x0 - s->x);
        int16_t py = (int16_t)(y0 - s->y);
        int32_t e2;

        if (px >= 0 && px < s->w && py >= 0 && py < s->h) {
            uint16_t *pp = &s->px[(int32_t)py * s->w + px];

            if (c.a >= 255) { *pp = v; }
            else            { *pp = blend(*pp, c, 255); }
        }
        if (x0 == x1 && y0 == y1) { break; }
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 = (int16_t)(x0 + sx); }
        if (e2 <= dx) { err += dx; y0 = (int16_t)(y0 + sy); }
    }
}

void gfx2_hline(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, gfx2_rgba_t c)
{
    gfx2_fill(s, x, y, w, 1, c);
}

void gfx2_vline(gfx2_surf_t *s, int16_t x, int16_t y, int16_t h, gfx2_rgba_t c)
{
    gfx2_fill(s, x, y, 1, h, c);
}

/* matriz de dither ordenado 4x4, valores 0..15 */
static const uint8_t k_dither[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

void gfx2_vgrad(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                gfx2_rgba_t c0, gfx2_rgba_t c1)
{
    int16_t iy, ix, y0, y1;

    if (h <= 0) return;
    /* Corte temprano: sin esto se recorrian las h filas del degradado en CADA
     * banda, descartando casi todas una por una. */
    if (!gfx2_band_hits(s, y, h)) return;

    /* solo las filas del degradado que caen dentro de esta banda */
    y0 = (int16_t)(s->y - y);
    y1 = (int16_t)(y0 + s->h);
    if (y0 < 0) y0 = 0;
    if (y1 > h) y1 = h;

    for (iy = y0; iy < y1; iy++) {
        int16_t sy = (int16_t)(y + iy);
        int16_t ly = (int16_t)(sy - s->y);
        uint32_t t;
        uint16_t *row;

        if (ly < 0 || ly >= s->h) continue;

        t = (h == 1) ? 0 : ((uint32_t)iy * 255u) / (uint32_t)(h - 1);
        row = &s->px[(int32_t)ly * s->w];

        /* EL COLOR SE MEZCLA UNA VEZ POR FILA Y NO POR PIXEL - 19/10/2026.
         *
         * La mezcla depende SOLO de t, y t depende solo de la fila. Estaba
         * dentro del bucle de columnas: cuatro multiplicaciones y cuatro
         * divisiones por pixel, 800 veces por fila, para obtener siempre el
         * mismo color. Lo unico que depende de la columna es el tramado.
         *
         * Medido en el PC, una banda de 800x16: 27 % menos. En el Cortex-M4
         * es mas, porque esto es aritmetica escalar y ahi el PC va sobrado.
         * Afecta a la cabecera entera y a la marquesina, que corre 21 veces
         * por segundo. El resultado es el mismo pixel a pixel. */
        {
            gfx2_rgba_t cf;
            uint16_t pal[4];
            uint8_t  k;
            cf.r = (uint8_t)((c0.r * (255u - t) + c1.r * t) / 255u);
            cf.g = (uint8_t)((c0.g * (255u - t) + c1.g * t) / 255u);
            cf.b = (uint8_t)((c0.b * (255u - t) + c1.b * t) / 255u);
            cf.a = (uint8_t)((c0.a * (255u - t) + c1.a * t) / 255u);

        /*
         * ==============================================================
         * CUATRO COLORES POR FILA, Y DESPUES SOLO GUARDAR - 01/11/2026.
         * ==============================================================
         *
         * EL PORQUE, dicho antes que ningun banco: que cambiar la linea
         * vertical y un digito de la frecuencia hiciera que todo lo demas
         * fuera lento no tenia sentido. Y no lo tenia.
         *
         * Medido: la traza del espectro compone 157.000 pixeles en 283
         * us -555 por microsegundo- y la cabecera 14.700 en 156 -94 por
         * microsegundo-. SEIS VECES MAS CARO EL PIXEL, dibujando mucho
         * menos. La diferencia es este bucle.
         *
         * Lo que se pagaba POR PIXEL: dos indices en la tabla de tramado,
         * una copia de la estructura de color, tres divisiones y seis
         * comparaciones de recorte, y una llamada a blend() que ademas
         * hace otra division y empaqueta a 565. Treinta y tantas
         * operaciones.
         *
         * Y para nada: el color de la fila es UNO -solo depende de t- y
         * el tramado depende de (x & 3), o sea que en toda la fila no hay
         * mas que CUATRO colores distintos. Se calculan los cuatro una
         * vez, ya empaquetados a 565, y el bucle de columnas se queda en
         * una carga y un guardado.
         *
         * El resultado es el MISMO pixel: misma mezcla, mismo tramado,
         * mismo redondeo, porque es literalmente la misma cuenta hecha
         * cuatro veces en vez de ochocientas. Lo vigila
         * sim/top_franjas.c, que compara los repintados parciales contra
         * el completo pixel a pixel.
         *
         * Si el degradado no fuera opaco -cf.a < 255- hay que mezclar con
         * lo que haya debajo y entonces el color SI depende del pixel: ese
         * caso se queda por el camino de antes. Hoy no se da -las paletas
         * traen alfa 255- pero no cuesta nada dejarlo correcto.
         */
        /*
         * Y NO HAY CAMINO LENTO: todos los degradados del arbol se piden
         * con gfx2_rgb(), que pone alfa 255 -son seis llamadas y estan
         * contadas-. Un degradado translucido tendria que mezclarse con
         * lo que hay debajo y entonces el color SI dependeria del pixel,
         * pero no existe ninguno. Si alguien añade uno, saldra opaco y se
         * vera a la primera; mantener aqui un segundo bucle para un caso
         * que no se da costaba 150 bytes de flash que el 450 no tiene.
         */
        for (k = 0U; k < 4U; k++) {
            int32_t dk = (int32_t)k_dither[sy & 3][k] - 8;
            int32_t r = cf.r + dk / 2, g = cf.g + dk / 4, b = cf.b + dk / 2;
            if (r < 0)   { r = 0; }
            if (r > 255) { r = 255; }
            if (g < 0)   { g = 0; }
            if (g > 255) { g = 255; }
            if (b < 0)   { b = 0; }
            if (b > 255) { b = 255; }
            pal[k] = gfx2_to565((uint8_t)r, (uint8_t)g, (uint8_t)b);
        }
        /*
         * ==============================================================
         * Y EL RECORTE, UNA VEZ POR FILA Y NO POR PIXEL - 02/11/2026.
         * ==============================================================
         *
         * El bucle de arriba se quedo en "una carga y un guardado"... mas
         * CUATRO comparaciones y dos saltos de recorte por pixel, que son
         * invariantes: ni la ventana de la banda ni el recorte horizontal
         * cambian dentro de la fila.
         *
         * Medido en un Cortex-M4 de verdad -qemu, contando instrucciones
         * sobre el codigo compilado para el GD32-: el degradado costaba
         * 30 instrucciones por pixel contra las 7,7 de un relleno liso.
         * La diferencia era esto.
         *
         * Asi que los tres recortes -la ventana de la banda, el trozo que
         * se pidio y s_rec_*- se cruzan ANTES y el bucle se queda en lo
         * que decia el comentario de arriba. Mismos pixeles exactamente:
         * los que se saltaban por `continue` son los que ahora no entran
         * en el rango. Lo vigila sim/top_franjas.c, que compara los
         * repintados parciales contra el completo pixel a pixel.
         */
        {
            int16_t xa = x, xb = (int16_t)(x + w);

            if (xa < s->x)        { xa = s->x; }
            if (xa < s_rec_x0)    { xa = s_rec_x0; }
            if (xb > (int16_t)(s->x + s->w)) { xb = (int16_t)(s->x + s->w); }
            if (xb > s_rec_x1)    { xb = s_rec_x1; }

            for (ix = xa; ix < xb; ix++) {
                row[ix - s->x] = pal[ix & 3];
            }
        }
        }
    }
}

/* Cobertura antialiasada de un pixel dentro de un rectangulo redondeado.
 * Supermuestreo 4x4: barato, suficiente para radios de 4-12px, y no hace
 * falta ni sqrt ni float. */
/*
 * ======================================================================
 * EL PIXEL, ¿CAE EN LA VENTANA QUE SE ESTA COMPONIENDO? - 01/11/2026.
 * ======================================================================
 *
 * EL PORQUE: cambiar la linea vertical y un digito de la frecuencia no
 * tendria que hacer que vaya lento todo lo demas, y lo hacia.
 *
 * Desde que la cabecera se compone A TROZOS -solo la caja de los digitos
 * que cambian al girar el mando- las pastillas de modo y de banda caen
 * ENTERAS fuera de la ventana. Y se seguian redondeando: los bucles de
 * esquina de gfx2_rrect() y gfx2_rrect_outline() miraban la banda
 * VERTICAL antes de llamar a rr_cov() -que son 16 submuestras con sus
 * multiplicaciones- pero NO la horizontal, asi que se pagaban enteras y
 * px_blend() las tiraba despues una a una.
 *
 * La pregunta se hace UNA VEZ por rectangulo y no por pixel: lo que de
 * verdad pasa es que la pastilla cae entera fuera, y preguntarlo pixel a
 * pixel costaba 96 bytes mas de flash para cazar ademas un caso -la
 * pastilla a medio entrar- que no se da al girar el mando.
 *
 * Medido con callgrind sobre el banco de la cabecera: rr_cov() se
 * llevaba el 38 % de TODO, mas del triple que dibujar los glifos.
 */
/*
 * ===========================================================================
 * LA COBERTURA DE LA ESQUINA, EN TABLA - 02/11/2026.
 * ===========================================================================
 *
 * Medido con callgrind sobre el repintado de la franja de estado: rr_cov()
 * se llevaba el 50,5 % de TODO, y calcularla una vez por pixel en vez de
 * cuatro solo bajo al 35,6 %. Sigue siendo el primero de la lista.
 *
 * Y no hace falta calcularla NUNCA mas de una vez por radio: la cobertura
 * del pixel (ix, iy) de una esquina no depende ni del color, ni de donde
 * este el rectangulo, ni de lo grande que sea -mientras r quepa, que ya se
 * recorta-. Depende SOLO de ix, iy y r.
 *
 * Asi que se guarda. Dos huecos porque gfx2_rrect_outline() alterna entre
 * dos radios -el de fuera y el de dentro- dentro del mismo bucle, y con uno
 * solo se estaria recalculando en cada vuelta.
 *
 * Son 2 x 16 x 16 = 512 bytes de RAM. Radios mas grandes que 16 no se usan
 * en este arbol, y si alguien pone uno se va por el camino de siempre, que
 * sigue estando aqui y da exactamente lo mismo.
 */
#define RR_TAB_MAX  16
static uint8_t s_rr_tab[2][RR_TAB_MAX * RR_TAB_MAX];
static int16_t s_rr_rad[2] = { -1, -1 };
static uint8_t s_rr_turno;

static uint16_t rr_cov(int16_t px, int16_t py, int16_t x, int16_t y,
                       int16_t w, int16_t h, int16_t r)
{
    int16_t sx, sy;
    uint16_t hit = 0;
    PROF_RRCOV();

    for (sy = 0; sy < 4; sy++) {
        for (sx = 0; sx < 4; sx++) {
            /* centro de la submuestra, en 1/8 de pixel */
            int32_t fx = (int32_t)px * 8 + sx * 2 + 1;
            int32_t fy = (int32_t)py * 8 + sy * 2 + 1;
            int32_t X0 = (int32_t)x * 8, Y0 = (int32_t)y * 8;
            int32_t X1 = (int32_t)(x + w) * 8, Y1 = (int32_t)(y + h) * 8;
            int32_t R = (int32_t)r * 8;
            int32_t cx, cy, dx, dy;

            if (fx < X0 || fx >= X1 || fy < Y0 || fy >= Y1) continue;

            /* centro de la esquina mas cercana, si estamos en una */
            cx = fx; cy = fy;
            if (fx < X0 + R) cx = X0 + R;
            else if (fx > X1 - R) cx = X1 - R;
            if (fy < Y0 + R) cy = Y0 + R;
            else if (fy > Y1 - R) cy = Y1 - R;

            dx = fx - cx; dy = fy - cy;
            if (dx == 0 && dy == 0) { hit++; continue; }
            if (dx * dx + dy * dy <= R * R) hit++;
        }
    }
    return (uint16_t)((hit * 255u) / 16u);
}

/*
 * La tabla del radio r, calculada con el MISMO rr_cov() de arriba sobre un
 * rectangulo de referencia de 2r x 2r: ahi el recorte de r cae igual que en
 * cualquier otro, asi que el numero es el mismo. Devuelve 0 si el radio no
 * cabe en la tabla, y entonces quien llama usa el camino de siempre.
 */
static const uint8_t *rr_tabla(int16_t r)
{
    uint8_t k;
    int16_t ix, iy;

    if (r < 1 || r > RR_TAB_MAX) { return (const uint8_t *)0; }
    for (k = 0U; k < 2U; k++) {
        if (s_rr_rad[k] == r) { return s_rr_tab[k]; }
    }
    k = (uint8_t)(s_rr_turno & 1U);
    s_rr_turno++;
    for (iy = 0; iy < r; iy++) {
        for (ix = 0; ix < r; ix++) {
            s_rr_tab[k][iy * RR_TAB_MAX + ix] =
                (uint8_t)rr_cov(ix, iy, 0, 0, (int16_t)(2 * r),
                                (int16_t)(2 * r), r);
        }
    }
    s_rr_rad[k] = r;
    return s_rr_tab[k];
}

void gfx2_rrect(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                int16_t r, gfx2_rgba_t c)
{
    int16_t iy, ix;
    uint16_t cov4;
    const uint8_t *tab;

    if (w <= 0 || h <= 0) return;
    /* La banda, Y TAMBIEN la ventana en horizontal: ver el comentario de
     * rr_cov(). Un rectangulo que cae entero fuera no tiene esquinas que
     * redondear, y los tramos rectos se recortan solos en gfx2_fill(). */
    if (!gfx2_band_hits(s, y, h)
        || (int16_t)(x + w) <= s->x
        || x >= (int16_t)(s->x + s->w)) return;
    if (r <= 0) { gfx2_fill(s, x, y, w, h, c); return; }
    if (r > w / 2) r = (int16_t)(w / 2);
    if (r > h / 2) r = (int16_t)(h / 2);

    /* interior recto de una sola pasada, los bordes con cobertura */
    gfx2_fill(s, x, (int16_t)(y + r), w, (int16_t)(h - 2 * r), c);
    gfx2_fill(s, (int16_t)(x + r), y, (int16_t)(w - 2 * r), r, c);
    gfx2_fill(s, (int16_t)(x + r), (int16_t)(y + h - r), (int16_t)(w - 2 * r), r, c);

    tab = rr_tabla(r);

    for (iy = 0; iy < r; iy++) {
        for (ix = 0; ix < r; ix++) {
            int16_t xs[2], ys[2];
            int k, l;
            xs[0] = (int16_t)(x + ix);          xs[1] = (int16_t)(x + w - 1 - ix);
            ys[0] = (int16_t)(y + iy);          ys[1] = (int16_t)(y + h - 1 - iy);
            /*
             * ==============================================================
             * UNA VEZ PARA LAS CUATRO ESQUINAS - 02/11/2026.
             * ==============================================================
             *
             * EL PORQUE: con el mando enganchado al ancho de filtro -que
             * solo cambia una chapa enana- se ralentiza igual. O sea que el
             * coste no estaba en lo que se ve cambiar.
             *
             * Medido con callgrind sobre el repintado de la franja de
             * estado: rr_cov() se lleva el 50,5 % de TODO. Son 16
             * submuestras por pixel de esquina, y la franja tiene siete
             * pastillas redondeadas con cuatro esquinas cada una.
             *
             * Y LAS CUATRO DAN EL MISMO NUMERO. No es una aproximacion, es
             * simetria: para la esquina de la izquierda la distancia al
             * centro del arco sale dx = 8*ix - 8*r + 2*sx + 1, y para la de
             * la derecha, con la submuestra espejada (sx' = 3 - sx), sale
             * dx' = -dx. Como solo cuenta dx*dx, las dieciseis submuestras
             * suman lo mismo. Igual en vertical.
             *
             * Asi que se calcula UNA vez por pixel de esquina en vez de
             * cuatro. Mismos pixeles exactamente. Lo vigila
             * sim/top_franjas.c, que compara los repintados parciales
             * contra el completo pixel a pixel.
             */
            /* Y la comprobacion de banda SIGUE yendo delante: rr_cov son
             * 16 submuestras que no se pagan por un pixel que no esta en
             * esta banda. Basta con que lo este UNA de las dos filas. */
            if ((ys[0] < s->y || ys[0] >= (int16_t)(s->y + s->h))
                && (ys[1] < s->y || ys[1] >= (int16_t)(s->y + s->h))) { continue; }
            cov4 = tab ? (uint16_t)tab[iy * RR_TAB_MAX + ix]
                       : rr_cov(xs[0], ys[0], x, y, w, h, r);
            if (!cov4) { continue; }
            for (k = 0; k < 2; k++) {
                for (l = 0; l < 2; l++) {
                    uint16_t cov = cov4;
                    if (ys[l] < s->y || ys[l] >= (int16_t)(s->y + s->h)) continue;
                    px_blend(s, xs[k], ys[l], c, cov);
                }
            }
        }
    }
}

void gfx2_rrect_outline(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w, int16_t h,
                        int16_t r, int16_t t, gfx2_rgba_t c)
{
    int16_t iy, ix, k;
    uint16_t cov_o, cov_i;

    if (w <= 0 || h <= 0 || t <= 0) return;
    /* La banda, Y TAMBIEN la ventana en horizontal: ver el comentario de
     * rr_cov(). Un rectangulo que cae entero fuera no tiene esquinas que
     * redondear, y los tramos rectos se recortan solos en gfx2_fill(). */
    if (!gfx2_band_hits(s, y, h)
        || (int16_t)(x + w) <= s->x
        || x >= (int16_t)(s->x + s->w)) return;

    if (r > w / 2) r = (int16_t)(w / 2);
    if (r > h / 2) r = (int16_t)(h / 2);

    /*
     * Solo se recorre el ANILLO del borde.
     *
     * La version anterior barria el rectangulo COMPLETO llamando a rr_cov()
     * dos veces por pixel (32 submuestras cada pixel) y descartaba el
     * interior. Para un boton de 126x48 eso eran ~194.000 operaciones cuando
     * los pixeles de borde que importan son ~350. Medido en el simulador:
     * una pantalla entera pasaba de 1.972.960 llamadas a rr_cov(), es decir
     * 31,5 millones de submuestras, que en el Cortex-M4 son segundos.
     *
     * Ahora: los cuatro tramos rectos son rellenos planos (sin antialiasing,
     * que en una recta no hace nada) y solo las cuatro esquinas de r x r
     * pasan por rr_cov.
     */
    if (r > 0) {
        /* tramos rectos */
        gfx2_fill(s, (int16_t)(x + r), y, (int16_t)(w - 2 * r), t, c);
        gfx2_fill(s, (int16_t)(x + r), (int16_t)(y + h - t), (int16_t)(w - 2 * r), t, c);
        gfx2_fill(s, x, (int16_t)(y + r), t, (int16_t)(h - 2 * r), c);
        gfx2_fill(s, (int16_t)(x + w - t), (int16_t)(y + r), t, (int16_t)(h - 2 * r), c);

        /* cuatro cajas de esquina, r x r */
        for (iy = 0; iy < r; iy++) {
            for (ix = 0; ix < r; ix++) {
                int16_t xs[2], ys[2];
                xs[0] = (int16_t)(x + ix);         xs[1] = (int16_t)(x + w - 1 - ix);
                ys[0] = (int16_t)(y + iy);         ys[1] = (int16_t)(y + h - 1 - iy);
                /* Una vez para las cuatro, por la misma simetria que
                 * gfx2_rrect(): ver el comentario de alli. Y con la
                 * comprobacion de banda delante, por lo mismo. */
                if ((ys[0] < s->y || ys[0] >= (int16_t)(s->y + s->h))
                    && (ys[1] < s->y || ys[1] >= (int16_t)(s->y + s->h))) { continue; }
                {
                    int16_t ri = (int16_t)(r - t);
                    if (ri < 0) { ri = 0; }
                    cov_o = rr_cov(xs[0], ys[0], x, y, w, h, r);
                    cov_i = cov_o ? rr_cov(xs[0], ys[0], (int16_t)(x + t),
                                           (int16_t)(y + t),
                                           (int16_t)(w - 2 * t),
                                           (int16_t)(h - 2 * t), ri)
                                  : (uint16_t)0;
                }
                if (cov_o <= cov_i) { continue; }
                for (k = 0; k < 4; k++) {
                    int16_t sx = xs[k & 1], sy = ys[(k >> 1) & 1];
                    if (sy < s->y || sy >= (int16_t)(s->y + s->h)) continue;
                    {
                        px_blend(s, sx, sy, c, (uint16_t)(cov_o - cov_i));
                    }
                }
            }
        }
    } else {
        /* sin radio: cuatro rectangulos, nada de supermuestreo */
        gfx2_fill(s, x, y, w, t, c);
        gfx2_fill(s, x, (int16_t)(y + h - t), w, t, c);
        gfx2_fill(s, x, y, t, h, c);
        gfx2_fill(s, (int16_t)(x + w - t), y, t, h, c);
    }
}

/* ------------------------------------------------------------------------
 * Texto
 * ------------------------------------------------------------------------ */

static const gfx2_glyph_t *glyph_for(const gfx2_font_t *f, uint16_t cp)
{
    uint8_t i;
    for (i = 0; i < f->n_ranges; i++) {
        if (cp >= f->ranges[i].lo && cp <= f->ranges[i].hi) {
            return &f->glyphs[f->ranges[i].first + (cp - f->ranges[i].lo)];
        }
    }
    /* sin glyph: caemos al espacio, que siempre esta en el primer rango */
    if (' ' >= f->ranges[0].lo && ' ' <= f->ranges[0].hi) {
        return &f->glyphs[f->ranges[0].first + (' ' - f->ranges[0].lo)];
    }
    return &f->glyphs[0];
}

/* Decodifica UTF-8 de 1, 2 y 3 bytes. */
static const char *next_cp(const char *p, uint16_t *cp)
{
    uint8_t c = (uint8_t)*p;
    if (c < 0x80) { *cp = c; return p + 1; }
    /* Tres bytes: hace falta desde que la pantalla de arranque escribe
     * katakana (U+30A2 y vecinos). Sin esto, cada katakana se leia como tres
     * bytes sueltos y salian tres caracteres de basura por celda. */
    if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *cp = (uint16_t)(((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
        return p + 3;
    }
    if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *cp = (uint16_t)(((c & 0x1F) << 6) | (p[1] & 0x3F));
        return p + 2;
    }
    *cp = c; /* byte suelto: lo tratamos como Latin-1 */
    return p + 1;
}

/*
 * UNA CADENA NULA NO PUEDE TUMBAR LA RADIO - 10/10/2026.
 *
 * Las tres funciones de texto recorrian `str` sin mirar si era cero. Casi
 * todos los rotulos son literales, pero los que pone main.c no: hay campos
 * de estado -el rotulo de la emisora, el del huso, el pie del QTH- que son
 * punteros que alguien tiene que rellenar, y el dia que uno no se rellena
 * esto se metia en *str con un puntero a cero.
 *
 * EN EL PC ESO ES UN SEGMENTATION FAULT Y SE VE. En la radio NO: aqui un
 * fallo de CPU no reinicia -no hay perro guardian y HardFault_Handler es un
 * bucle infinito-, asi que lo que se veria es la pantalla congelada y a
 * quitar la pila.
 *
 * Aparecio al añadir el cuarto boton de la pantalla del reloj: el banco
 * sim/captura_menus.c se cayo de golpe porque su ui_hora_state_t todavia no
 * tenia el campo nuevo. O sea que el banco encontro el fallo DE VERDAD -el
 * de la radio- disfrazado de fallo del banco, y arreglar solo el banco
 * habria sido tapar justo lo que habia que ver.
 *
 * Se trata como cadena vacia y se sigue dibujando. Un rotulo en blanco se ve
 * y se puede contar; una radio colgada, no.
 */
int16_t gfx2_text_w(const char *str, const gfx2_font_t *f)
{
    int16_t wsum = 0;
    uint16_t cp;
    if (str == 0) { return 0; }
    while (*str) {
        str = next_cp(str, &cp);
        wsum = (int16_t)(wsum + glyph_for(f, cp)->adv);
    }
    return wsum;
}

/*
 * Cuantos BYTES del principio caben en `tope` pixeles. Una sola pasada
 * hacia delante: ver el comentario de la cabecera.
 *
 * Se devuelven BYTES y no letras a proposito: quien llama corta el array
 * por ahi, y en UTF-8 una letra puede ser dos bytes. next_cp() avanza por
 * el principio de cada caracter, asi que el corte nunca cae en medio de
 * uno.
 */
uint8_t gfx2_text_cabe(const char *str, const gfx2_font_t *f, int16_t tope,
                       int16_t *ancho)
{
    const char *ini = str;
    const char *bueno = str;
    int16_t w = 0, w_bueno = 0;
    uint16_t cp;

    if (str == 0 || tope <= 0) { if (ancho) { *ancho = 0; } return 0U; }

    while (*str != '\0') {
        const char *sig = next_cp(str, &cp);
        int16_t aw = glyph_for(f, cp)->adv;

        if ((int16_t)(w + aw) > tope) { break; }
        w = (int16_t)(w + aw);
        str = sig;
        bueno = sig; w_bueno = w;
    }
    if (ancho) { *ancho = w_bueno; }
    return (uint8_t)(bueno - ini);
}

/*
 * UN glifo. Sale del bucle de gfx2_text() para que gfx2_text_in(), cuando
 * tiene que cortar, no lleve una copia del mismo dibujado. Devuelve lo que
 * avanza el cursor.
 *
 * ======================================================================
 * Y AHORA LAS DOS GUARDAS VIVEN AQUI - 24/10/2026.
 * ======================================================================
 *
 * Encontrado en la revision a fondo pedida por el usuario: habia DOS copias
 * de este bucle -esta y la de dentro de gfx2_text()-, byte por byte iguales
 * salvo por las guardas, y los dos arreglos de octubre entraron solo en una.
 * El comentario de arriba dice que esta funcion existe precisamente para no
 * tener dos copias, y gfx2_text() se quedo con la suya igualmente.
 *
 * Asi que ahora hay una sola y las guardas van dentro:
 *
 *   - EL CORTE HORIZONTAL (13/10/2026): un glifo entero fuera de la ventana
 *     recorria igual sus g->h x g->w medios bytes, los descodificaba,
 *     calculaba la cobertura y llamaba a px_blend(), que es quien lo
 *     rechazaba pixel a pixel. Medido con la marquesina de RDS -62
 *     caracteres en un hueco de 150x44-: 4.522 medios bytes leidos para
 *     escribir 758, o sea el 83 % tirado, veinticinco veces por segundo.
 *   - EL CORTE VERTICAL (24/10/2026): lo mismo por arriba y por abajo. El
 *     bucle de filas se entraba igual y se descartaba fila a fila con el
 *     `continue` de dentro. gfx2 compone por bandas de ocho filas sobre
 *     textos de catorce de alto, asi que la mayoria de las bandas no tocan
 *     la mayoria de los textos.
 *
 * SE SALTA, NO SE SALE: esta funcion devuelve lo que avanza el cursor y hay
 * quien encadena con eso -la lectura de dBm del S-metro, en ui_top.c-, asi
 * que el avance se devuelve siempre, se haya pintado o no.
 */
static int16_t gfx2_text_uno(gfx2_surf_t *s, int16_t cx, int16_t y,
                             uint16_t cp, const gfx2_font_t *f, gfx2_rgba_t c)
{
    const gfx2_glyph_t *g = glyph_for(f, cp);
    int16_t gy, gx;

    if ((int16_t)(cx + g->bx + g->w) <= s->x
        || (int16_t)(cx + g->bx) >= (int16_t)(s->x + s->w)) {
        return g->adv;
    }
    if (!gfx2_band_hits(s, (int16_t)(y + g->by), (int16_t)g->h)) {
        return g->adv;
    }

    if (g->w && g->h) {
        const uint8_t *bm = &f->bitmap[g->off];
        int16_t row_bytes = (int16_t)((g->w + 1) / 2);

        for (gy = 0; gy < g->h; gy++) {
            int16_t sy = (int16_t)(y + g->by + gy);
            if (sy < s->y || sy >= (int16_t)(s->y + s->h)) continue;
            for (gx = 0; gx < g->w; gx++) {
                uint8_t byte = bm[gy * row_bytes + (gx >> 1)];
                uint8_t nib = (gx & 1) ? (uint8_t)(byte & 0x0F)
                                       : (uint8_t)(byte >> 4);
                if (nib) {
                    uint16_t cov = (uint16_t)((nib * 255u) / 15u);
                    px_blend(s, (int16_t)(cx + g->bx + gx), sy, c, cov);
                }
            }
        }
    }
    return g->adv;
}

int16_t gfx2_text(gfx2_surf_t *s, int16_t x, int16_t y, const char *str,
                  const gfx2_font_t *f, gfx2_rgba_t c)
{
    int16_t cx = x;
    uint16_t cp;

    /*
     * EL BUCLE DE PIXELES ESTA EN gfx2_text_uno(), Y SOLO ALLI - 24/10/2026.
     *
     * Aqui habia una copia byte por byte de aquella funcion, con sus dos
     * guardas de recorte. Dos copias del mismo dibujado es como se llega a
     * que un arreglo valga en un sitio y no en el otro, y ya habia pasado:
     * los dos cortes de octubre -el horizontal y el vertical- entraron solo
     * en esta y gfx2_text_uno() se quedo sin ninguno, asi que el camino de
     * recorte de gfx2_text_in() seguia descodificando glifos enteros contra
     * ventanas que no los contienen. Ver el comentario de alli.
     */
    if (str == 0) { return cx; }   /* ver gfx2_text_w() */
    while (*str) {
        str = next_cp(str, &cp);
        cx = (int16_t)(cx + gfx2_text_uno(s, cx, y, cp, f, c));
    }
    return (int16_t)(cx - x);
}

/*
 * EL "_IN" ERA MENTIRA - 05/10/2026.
 *
 * Esta funcion colocaba el texto dentro de un ancho y SE OLVIDABA DEL
 * ANCHO: si no cabia, centrado lo sacaba por los dos lados y a la derecha
 * por la izquierda. El recorte de gfx2 es a la BANDA, no a la caja que te
 * han dado, asi que lo que se salia se pintaba encima del vecino.
 *
 * Y no es un fallo que avise: sale texto, se lee a medias, y quien lo mira
 * cree que el valor es "28 kB, 6824 seguidos, dir 5" cuando era otro.
 *
 * Cuando no cabe se alinea a la IZQUIERDA y se corta por la derecha, que es
 * lo unico legible: centrado se pierde texto por los dos lados y no se sabe
 * ni por donde empezaba. Se corta por caracteres enteros -un glifo a medias
 * se lee como otra letra- y la caja sigue siendo el limite duro.
 */
void gfx2_text_in(gfx2_surf_t *s, int16_t x, int16_t y, int16_t w,
                  const char *str, const gfx2_font_t *f, gfx2_rgba_t c,
                  gfx2_align_t al)
{
    int16_t tw;
    int16_t tx = x;

    if (str == 0) { return; }      /* ver gfx2_text_w() */
    /* Y si esta banda no toca el renglon, fuera antes de medir: gfx2_text_w()
     * recorre la cadena entera buscando glifos, y esto se llama una vez por
     * banda. Es la guarda que ya tienen gfx2_rrect(), gfx2_line() y, desde
     * hoy, gfx2_text_uno(); aqui faltaba. El alto es el de la linea de la
     * fuente, que es lo que ocupa el renglon. 24/10/2026. */
    if (!gfx2_band_hits(s, (int16_t)(y - 2),
                        (int16_t)(f->line_h + 4))) { return; }
    tw = gfx2_text_w(str, f);

    if (tw > w) {
        int16_t cx = x;
        uint16_t cp;

        while (*str) {
            const char *sig = next_cp(str, &cp);
            const gfx2_glyph_t *g = glyph_for(f, cp);

            if ((int16_t)(cx + g->adv) > (int16_t)(x + w)) { break; }
            (void)gfx2_text_uno(s, cx, y, cp, f, c);
            cx = (int16_t)(cx + g->adv);
            str = sig;
        }
        return;
    }

    if (al == GFX2_ALIGN_C) tx = (int16_t)(x + (w - tw) / 2);
    else if (al == GFX2_ALIGN_R) tx = (int16_t)(x + w - tw);
    gfx2_text(s, tx, y, str, f, c);
}

/* ------------------------------------------------------------------------
 * Waterfall
 * ------------------------------------------------------------------------ */

void gfx2_wf_blit(int16_t x, int16_t y, int16_t w, int16_t rows,
                  const uint8_t *idx, int16_t stride, int16_t head,
                  const uint16_t *cmap)
{
    /* Se vuelca por trozos de GFX2_BAND_H filas reutilizando la banda, de
     * forma que solo hace falta UNA ventana por trozo y el buffer de
     * historial puede seguir siendo de 8 bits. */
    int16_t done = 0;

    /*
     * Y SE PIDE LA BANDA, QUE ERA LA UNICA QUE NO LA PEDIA - 05/10/2026.
     *
     * Y se veia fatal: fondo morado, rayas negras y el texto fantasma. La
     * causa es esta funcion, que escribia en s_band SIN pedirla.
     *
     * Todos los demas la piden: gfx2_render() y los dos montadores por
     * bandas -el del espectro y el de la cascada por IPA- la cogen con
     * gfx2_banda_coge() y, si esta ocupada, se saltan ese fotograma. Este
     * camino -el de respaldo de la cascada, el que corre cuando el IPA no
     * puede- se la quedaba sin preguntar.
     *
     * Mientras no hubo nadie que tuviera la banda cogida mucho rato, no se
     * noto. La captura de pantalla la tiene cogida VEINTICINCO SEGUNDOS, y
     * en ese rato la cascada le escribia encima sus filas una y otra vez:
     * el morado era el suelo de la paleta del agua, y las rayas negras los
     * trozos de cada banda que se salvaban.
     *
     * O sea que no era un fallo de la captura: era un fallo viejo que la
     * captura destapo por ser la primera en tener la banda mas de un
     * instante.
     */
    if (gfx2_banda_coge() == (uint16_t *)0) {
        return;
    }

    while (done < rows) {
        int16_t chunk = (int16_t)(rows - done);
        int16_t r, i;
        int32_t n;

        if (chunk > GFX2_BAND_H) chunk = GFX2_BAND_H;

        for (r = 0; r < chunk; r++) {
            int16_t src = (int16_t)((head + done + r) % rows);
            const uint8_t *sp = &idx[(int32_t)src * stride];
            uint16_t *dp = &s_band[(int32_t)r * w];
            for (i = 0; i < w; i++) dp[i] = cmap[sp[i]];
        }

        if (s_chivato) {
            s_chivato((uint16_t)x, (uint16_t)(y + done),
                      (uint16_t)(x + w - 1),
                      (uint16_t)(y + done + chunk - 1));
        }
        rm68120_set_window((uint16_t)x, (uint16_t)(y + done),
                           (uint16_t)(x + w - 1), (uint16_t)(y + done + chunk - 1));
        n = (int32_t)w * chunk;
        for (i = 0; i < n; i++) rm68120_dato(s_band[i]);

        done = (int16_t)(done + chunk);
    }
    gfx2_banda_suelta();
}
