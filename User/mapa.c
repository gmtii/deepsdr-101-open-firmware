#include "mapa.h"

/*
 * LA PROYECCION, EN DOS LINEAS.
 *
 *   px = x + (lon + 180) * w / 360
 *   py = y + (tope - lat) * h / (2 * tope)
 *
 * Todo en centesimas de grado y con enteros de 32 bits: el producto mas
 * grande es 36000 * 800 = 28.800.000, que cabe de sobra, y asi no entra
 * la coma flotante en un camino de dibujo.
 */
#define LON_C_MAX 18000L

/*
 * EL MUNDO, A LA ESCALA QUE LLEVE LA CAJA.
 *
 * El ancho se elige -es el del hueco por la escala-; el alto NO: sale de
 * los 360 x 143 grados. Que salga de ahi y no de `h` es justamente lo que
 * impide que el mapa se estire para llenar el hueco.
 */
int32_t mapa_mundo_w(const mapa_caja_t *c)
{
    uint8_t e = (c->esc == 0U) ? 1U : c->esc;

    return (int32_t)c->w * (int32_t)e;
}

int32_t mapa_mundo_h(const mapa_caja_t *c)
{
    return (mapa_mundo_w(c) * (2L * (int32_t)k_mapa_lat_tope_c)) / 36000L;
}

/*
 * LA PROYECCION, CON ESCALA Y DESPLAZAMIENTO.
 *
 * Todo lo que se pinta en el mapa pasa por estas dos, y por eso el zoom y
 * el arrastre no han hecho falta tocarlos en ningun sitio mas: la costa,
 * la rejilla, las marcas y el aspa de casa preguntan aqui.
 *
 * Los enteros siguen siendo de 32 bits y siguen cabiendo: el producto mas
 * grande es 36000 * (800 * 8) = 230.400.000.
 */
static int16_t px_de_lon(const mapa_caja_t *c, int32_t lon_c)
{
    return (int16_t)((int32_t)c->x - (int32_t)c->off_x
                     + ((lon_c + LON_C_MAX) * mapa_mundo_w(c)) / (2L * LON_C_MAX));
}

static int16_t py_de_lat(const mapa_caja_t *c, int32_t lat_c)
{
    int32_t t = (int32_t)k_mapa_lat_tope_c;

    return (int16_t)((int32_t)c->y - (int32_t)c->off_y
                     + ((t - lat_c) * mapa_mundo_h(c)) / (2L * t));
}

/*
 * RECORTAR UN TRAZO A LA CAJA (Cohen-Sutherland).
 *
 * POR QUE HACE FALTA DE VERDAD, y no vale con rechazar los que caen
 * enteros fuera: gfx2_line recorta contra la BANDA que se esta
 * componiendo, no contra la caja del mapa. Mientras el mapa ocupaba el
 * hueco entero eran lo mismo y no se noto; con zoom hay mundo de sobra por
 * los cuatro lados, y un trazo de costa que entra y sale de la caja se
 * pintaria tambien por fuera - encima de lo que hubiera al lado-.
 *
 * Y de paso ahorra el trabajo: al zoom 8 un trazo puede medir miles de
 * pixeles de los que se ven veinte, y Bresenham los recorreria todos.
 *
 * Devuelve 0 si no queda nada que pintar.
 */
#define FUERA_IZQ  1U
#define FUERA_DER  2U
#define FUERA_ARR  4U
#define FUERA_ABA  8U

static uint8_t zona(const mapa_caja_t *c, int32_t x, int32_t y)
{
    uint8_t z = 0U;

    if (x < (int32_t)c->x)                        { z |= FUERA_IZQ; }
    else if (x > (int32_t)(c->x + c->w - 1))      { z |= FUERA_DER; }
    if (y < (int32_t)c->y)                        { z |= FUERA_ARR; }
    else if (y > (int32_t)(c->y + c->h - 1))      { z |= FUERA_ABA; }
    return z;
}

static uint8_t recorta(const mapa_caja_t *c, int32_t *x0, int32_t *y0,
                       int32_t *x1, int32_t *y1)
{
    int32_t xa = *x0, ya = *y0, xb = *x1, yb = *y1;
    uint8_t za = zona(c, xa, ya), zb = zona(c, xb, yb);
    int32_t izq = c->x, der = (int32_t)c->x + c->w - 1;
    int32_t arr = c->y, aba = (int32_t)c->y + c->h - 1;
    uint8_t vueltas = 0U;

    while (1) {
        if ((za | zb) == 0U) { break; }        /* los dos dentro */
        if ((za & zb) != 0U) { return 0U; }    /* los dos al mismo lado */
        /* Un trazo necesita como mucho cuatro cortes; el tope es por si
         * una division entera dejara un punto justo en el borde y esto se
         * quedara dando vueltas. Mas vale no pintarlo que no volver. */
        if (++vueltas > 8U) { return 0U; }

        {
            uint8_t z = (za != 0U) ? za : zb;
            int32_t x = 0, y = 0;

            if ((z & FUERA_ARR) != 0U) {
                x = xa + ((xb - xa) * (arr - ya)) / ((yb - ya) != 0 ? (yb - ya) : 1);
                y = arr;
            } else if ((z & FUERA_ABA) != 0U) {
                x = xa + ((xb - xa) * (aba - ya)) / ((yb - ya) != 0 ? (yb - ya) : 1);
                y = aba;
            } else if ((z & FUERA_DER) != 0U) {
                y = ya + ((yb - ya) * (der - xa)) / ((xb - xa) != 0 ? (xb - xa) : 1);
                x = der;
            } else {
                y = ya + ((yb - ya) * (izq - xa)) / ((xb - xa) != 0 ? (xb - xa) : 1);
                x = izq;
            }
            if (z == za) { xa = x; ya = y; za = zona(c, xa, ya); }
            else         { xb = x; yb = y; zb = zona(c, xb, yb); }
        }
    }
    *x0 = xa; *y0 = ya; *x1 = xb; *y1 = yb;
    return 1U;
}

/*
 * Que el trozo visible no se salga del mundo. Si el mundo es mas pequeño
 * que el hueco en un eje -a escala 1 no lo es en ninguno, pero podria
 * serlo si algun dia el hueco cambia- se centra en vez de pegarse a un
 * borde, que es lo que se espera al ver algo mas pequeño que su marco.
 */
static void ajusta(mapa_caja_t *c)
{
    int32_t mw = mapa_mundo_w(c), mh = mapa_mundo_h(c);
    int32_t maxx = mw - (int32_t)c->w, maxy = mh - (int32_t)c->h;

    if (maxx <= 0) { c->off_x = (int16_t)(maxx / 2); }
    else if (c->off_x < 0) { c->off_x = 0; }
    else if ((int32_t)c->off_x > maxx) { c->off_x = (int16_t)maxx; }

    if (maxy <= 0) { c->off_y = (int16_t)(maxy / 2); }
    else if (c->off_y < 0) { c->off_y = 0; }
    else if ((int32_t)c->off_y > maxy) { c->off_y = (int16_t)maxy; }
}

uint8_t mapa_e4_a_px(const mapa_caja_t *c, int32_t lon_e4, int32_t lat_e4,
                     int16_t *px, int16_t *py)
{
    /* De diezmilesimas a centesimas, redondeando al mas cercano y no
     * truncando: truncar corre todo medio pixel hacia el ecuador y hacia
     * Greenwich, poco pero siempre en la misma direccion. */
    int32_t lon_c = (lon_e4 >= 0) ? ((lon_e4 + 50) / 100) : ((lon_e4 - 50) / 100);
    int32_t lat_c = (lat_e4 >= 0) ? ((lat_e4 + 50) / 100) : ((lat_e4 - 50) / 100);

    if (lon_c < -LON_C_MAX || lon_c > LON_C_MAX) { return 0U; }
    if (lat_c < -(int32_t)k_mapa_lat_tope_c ||
        lat_c >  (int32_t)k_mapa_lat_tope_c) { return 0U; }
    if (px != 0) { *px = px_de_lon(c, lon_c); }
    if (py != 0) { *py = py_de_lat(c, lat_c); }
    return 1U;
}

uint8_t mapa_grados_a_px(const mapa_caja_t *c, float lon, float lat,
                         int16_t *px, int16_t *py)
{
    int32_t lon_c = (int32_t)(lon * 100.0f);
    int32_t lat_c = (int32_t)(lat * 100.0f);

    if (lon_c < -LON_C_MAX || lon_c > LON_C_MAX) { return 0U; }
    if (lat_c < -(int32_t)k_mapa_lat_tope_c ||
        lat_c >  (int32_t)k_mapa_lat_tope_c) { return 0U; }
    if (px != 0) { *px = px_de_lon(c, lon_c); }
    if (py != 0) { *py = py_de_lat(c, lat_c); }
    return 1U;
}

/*
 * EL LOCALIZADOR MAIDENHEAD, Y EL CENTRO DEL CUADRADO.
 *
 * Un localizador no es un punto, es un RECTANGULO: el de cuatro
 * caracteres mide 2 grados de longitud por 1 de latitud, unos 150 km de
 * ancho en nuestra latitud. Se pinta su centro y no su esquina, que es lo
 * unico honrado que se puede hacer con un dato que no dice mas.
 *
 * El de seis caracteres afina a 5 minutos por 2,5, unos 9 km, y por eso
 * se admite: cuando el otro lo manda, se usa.
 *
 * OJO CON LA CUENTA DE LA LONGITUD: la primera letra vale 20 grados, el
 * primer digito 2, y la tercera letra 2/24. La latitud es la mitad en
 * todo. Es el mismo convenio que el localizador de WSPR, donde ya se
 * metio la pata una vez -ver el 28/09 en CAMBIOS.md- por copiar una
 * formula en vez de escribirla desde la definicion.
 */
uint8_t mapa_loc_a_px(const mapa_caja_t *c, const char *loc,
                      int16_t *px, int16_t *py)
{
    int32_t lon, lat;
    uint8_t n = 0U;
    char g[6];

    if (loc == 0) { return 0U; }
    while (loc[n] != '\0') {
        if (n >= 6U) { return 0U; }
        g[n] = loc[n];
        if (g[n] >= 'a' && g[n] <= 'z') { g[n] = (char)(g[n] - 'a' + 'A'); }
        n++;
    }
    if (n != 4U && n != 6U) { return 0U; }
    if (g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R') { return 0U; }
    if (g[2] < '0' || g[2] > '9' || g[3] < '0' || g[3] > '9') { return 0U; }

    /* En centesimas de grado, desde la esquina suroeste del cuadrado. */
    lon = (int32_t)(g[0] - 'A') * 2000L + (int32_t)(g[2] - '0') * 200L - 18000L;
    lat = (int32_t)(g[1] - 'A') * 1000L + (int32_t)(g[3] - '0') * 100L -  9000L;

    if (n == 6U) {
        if (g[4] < 'A' || g[4] > 'X' || g[5] < 'A' || g[5] > 'X') { return 0U; }
        /* 24 subcuadrados: 2 grados / 24 y 1 grado / 24 */
        lon += ((int32_t)(g[4] - 'A') * 200L) / 24L + (200L / 48L);
        lat += ((int32_t)(g[5] - 'A') * 100L) / 24L + (100L / 48L);
    } else {
        lon += 100L;   /* el centro del cuadrado de 2 grados */
        lat +=  50L;   /* y del de 1 */
    }

    if (lat < -(int32_t)k_mapa_lat_tope_c) { lat = -(int32_t)k_mapa_lat_tope_c; }
    if (lat >  (int32_t)k_mapa_lat_tope_c) { lat =  (int32_t)k_mapa_lat_tope_c; }
    if (px != 0) { *px = px_de_lon(c, lon); }
    if (py != 0) { *py = py_de_lat(c, lat); }
    return 1U;
}

void mapa_pon(mapa_caja_t *c, int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (c == 0) { return; }
    c->x = x; c->y = y; c->w = w; c->h = h;
    c->esc = 1U;
    /* Centrado: a escala 1 el mundo es mas alto que el hueco (317 contra
     * 288 con el panel de FT8), y lo que sobra se reparte arriba y abajo
     * en vez de dejar todo el recorte en un polo. */
    c->off_x = (int16_t)((mapa_mundo_w(c) - (int32_t)w) / 2);
    c->off_y = (int16_t)((mapa_mundo_h(c) - (int32_t)h) / 2);
    ajusta(c);
}

uint8_t mapa_zoom(mapa_caja_t *c, uint8_t esc, int16_t ax, int16_t ay)
{
    int32_t mw0, mh0, mundox, mundoy;

    if (c == 0) { return 1U; }
    if (esc < 1U) { esc = 1U; }
    if (esc > (uint8_t)MAPA_ESC_MAX) { esc = (uint8_t)MAPA_ESC_MAX; }

    /*
     * Donde cae el ancla DENTRO DEL MUNDO, antes de cambiar la escala.
     * Despues se vuelve a poner el desplazamiento para que ese mismo punto
     * del mundo siga cayendo en el mismo pixel de la pantalla.
     */
    mw0 = mapa_mundo_w(c);
    mh0 = mapa_mundo_h(c);
    mundox = (int32_t)c->off_x + ((int32_t)ax - (int32_t)c->x);
    mundoy = (int32_t)c->off_y + ((int32_t)ay - (int32_t)c->y);

    c->esc = esc;
    {
        int32_t mw = mapa_mundo_w(c), mh = mapa_mundo_h(c);

        /* La regla de tres, con el producto antes de la division para no
         * perder el trozo de pixel en cada paso de zoom. */
        c->off_x = (int16_t)(((mundox * mw) / mw0) - ((int32_t)ax - (int32_t)c->x));
        c->off_y = (int16_t)(((mundoy * mh) / mh0) - ((int32_t)ay - (int32_t)c->y));
    }
    ajusta(c);
    return c->esc;
}

void mapa_arrastra(mapa_caja_t *c, int16_t dx, int16_t dy)
{
    if (c == 0) { return; }
    /* El dedo lleva el MAPA, no la ventana: arrastrar a la derecha trae lo
     * que habia a la izquierda, o sea que el trozo visible retrocede. */
    c->off_x = (int16_t)((int32_t)c->off_x - (int32_t)dx);
    c->off_y = (int16_t)((int32_t)c->off_y - (int32_t)dy);
    ajusta(c);
}

uint8_t mapa_loc_de_linea(const char *linea, char *out)
{
    const char *p = linea;
    const char *msg = 0;
    const char *ult;
    uint8_t tabs = 0U, n = 0U;

    if (linea == 0 || out == 0) { return 0U; }
    out[0] = '\0';

    /* El quinto campo es el mensaje: cuatro tabuladores por delante. */
    while (*p != '\0') {
        if (*p == '\t') {
            tabs++;
            if (tabs == 4U) { msg = p + 1; }
            else if (tabs == 5U) { break; }
        }
        p++;
    }
    if (msg == 0) { return 0U; }

    /* La ultima palabra del mensaje, que acaba en tabulador o en cero. */
    ult = msg;
    {
        const char *q = msg;

        while (*q != '\0' && *q != '\t') {
            if (*q == ' ') { ult = q + 1; }
            q++;
        }
        n = 0U;
        while (ult[n] != '\0' && ult[n] != '\t' && ult[n] != ' ' && n < 6U) {
            out[n] = ult[n];
            n++;
        }
        if (ult[n] != '\0' && ult[n] != '\t' && ult[n] != ' ') { out[0] = '\0'; return 0U; }
    }
    out[n] = '\0';
    if (n != 4U && n != 6U) { out[0] = '\0'; return 0U; }

    /*
     * LOS QUE TIENEN FORMA DE LOCALIZADOR Y NO LO SON. Ver el comentario
     * de esta funcion en mapa.h: RR73 es como acaba casi todo contacto de
     * FT8 y caeria en el Pacifico.
     */
    if (out[0] == 'R' && out[1] == 'R' && out[2] == '7'
        && (out[3] == '1' || out[3] == '2' || out[3] == '3')) {
        out[0] = '\0';
        return 0U;
    }

    /* Y que sea un localizador de verdad, no cualquier cosa de cuatro. */
    if (out[0] < 'A' || out[0] > 'R' || out[1] < 'A' || out[1] > 'R'
        || out[2] < '0' || out[2] > '9' || out[3] < '0' || out[3] > '9') {
        out[0] = '\0';
        return 0U;
    }
    if (n == 6U) {
        if (out[4] < 'A' || out[4] > 'X' || out[5] < 'A' || out[5] > 'X') {
            out[0] = '\0';
            return 0U;
        }
    }
    return 1U;
}

/* --------------------------------------------------------------- */

void mapa_dibuja(gfx2_surf_t *s, const mapa_caja_t *c)
{
    static const uint32_t k_mar    = 0x0B1A2AUL;
    static const uint32_t k_rejilla= 0x16304AUL;
    static const uint32_t k_costa  = 0x3E7FA8UL;
    const int16_t *p = k_mapa_pts;
    uint16_t linea;
    int16_t i;

    if (!gfx2_band_hits(s, c->y, c->h)) { return; }

    gfx2_fill(s, c->x, c->y, c->w, c->h, gfx2_rgb(k_mar));

    /*
     * LA REJILLA VA CADA 30 GRADOS y el ecuador va mas claro. No es
     * adorno: sin ninguna referencia, un punto en mitad del Atlantico no
     * se sabe si esta a la altura de Canarias o de Islandia.
     */
    for (i = -180; i <= 180; i += 30) {
        int16_t x = px_de_lon(c, (int32_t)i * 100L);

        if (x < c->x || x >= (int16_t)(c->x + c->w)) { continue; }
        gfx2_vline(s, x, c->y, c->h, gfx2_rgb(k_rejilla));
    }
    for (i = -60; i <= 60; i += 30) {
        int16_t y = py_de_lat(c, (int32_t)i * 100L);

        if (y < c->y || y >= (int16_t)(c->y + c->h)) { continue; }
        gfx2_hline(s, c->x, y, c->w, gfx2_rgb(i == 0 ? 0x24466AUL : k_rejilla));
    }

    /*
     * Y LA COSTA. El formato es: cuantos puntos, y despues los pares
     * (longitud, latitud) en centesimas de grado; un cero por cuantos
     * acaba la tabla.
     *
     * LA RAYA QUE NO SE PINTA. En un mapa plano, una costa que pasa por
     * el meridiano 180 -las Aleutianas, Chukotka, Fiyi- tiene un punto en
     * el borde derecho y el siguiente en el izquierdo. Unirlos pinta una
     * raya horizontal que cruza el mapa entero y se lee como un fallo del
     * dibujo. Por eso se corta el trazo cuando dos puntos seguidos se
     * separan mas de medio mundo: no es que la costa salte, es que el
     * mapa esta cortado por ahi.
     */
    for (linea = 0U; linea < k_mapa_n_lineas; linea++) {
        int16_t n = *p++;
        int16_t k;
        int16_t ax = 0, ay = 0;
        int32_t alon = 0;

        for (k = 0; k < n; k++) {
            int32_t lon = *p++;
            int32_t lat = *p++;
            int16_t bx = px_de_lon(c, lon);
            int16_t by = py_de_lat(c, lat);

            if (k > 0) {
                int32_t d = (lon > alon) ? (lon - alon) : (alon - lon);

                if (d < LON_C_MAX) {
                    /* Recortado a la caja, no a la banda: ver recorta(). */
                    int32_t rx0 = ax, ry0 = ay, rx1 = bx, ry1 = by;

                    if (recorta(c, &rx0, &ry0, &rx1, &ry1)) {
                        gfx2_line(s, (int16_t)rx0, (int16_t)ry0,
                                     (int16_t)rx1, (int16_t)ry1,
                                  gfx2_rgb(k_costa));
                    }
                }
            }
            ax = bx; ay = by; alon = lon;
        }
    }
}

void mapa_marca(gfx2_surf_t *s, const mapa_caja_t *c,
                int16_t px, int16_t py, uint8_t viva, gfx2_rgba_t color)
{
    if (px < c->x || px >= (int16_t)(c->x + c->w)) { return; }
    if (py < c->y || py >= (int16_t)(c->y + c->h)) { return; }

    if (viva) {
        /* Halo primero, punto encima: asi se ve sobre la costa clara. */
        gfx2_fill(s, (int16_t)(px - 3), (int16_t)(py - 3), 7, 7,
                  gfx2_rgba(0x000000UL, 110U));
        gfx2_fill(s, (int16_t)(px - 2), (int16_t)(py - 1), 5, 3, color);
        gfx2_fill(s, (int16_t)(px - 1), (int16_t)(py - 2), 3, 5, color);
    } else {
        /* Hueco: solo el contorno. Lo de hace rato no puede pesar igual
         * que lo de ahora mismo, o el mapa se llena y deja de decir
         * cuando ha abierto una banda. */
        gfx2_hline(s, (int16_t)(px - 2), (int16_t)(py - 2), 5, color);
        gfx2_hline(s, (int16_t)(px - 2), (int16_t)(py + 2), 5, color);
        gfx2_vline(s, (int16_t)(px - 2), (int16_t)(py - 1), 3, color);
        gfx2_vline(s, (int16_t)(px + 2), (int16_t)(py - 1), 3, color);
    }
}

/* --------------------------------------------------------------- */
/* Los dos mandos del zoom. Ver mapa.h para el porque de que esten aqui. */

#define MZ_MARGEN 8

static int16_t mz_y(const mapa_caja_t *c)
{
    return (int16_t)(c->y + c->h - MZ_MARGEN - MAPA_MZ_LADO);
}
static int16_t mz_x(const mapa_caja_t *c, uint8_t mas)
{
    return (int16_t)(c->x + MZ_MARGEN
                     + (mas ? 0 : (MAPA_MZ_LADO + MZ_MARGEN)));
}

static void mz_uno(gfx2_surf_t *s, const mapa_caja_t *c, uint8_t mas, uint8_t vale)
{
    int16_t x = mz_x(c, mas), y = mz_y(c);
    uint32_t tinta = vale ? 0xDCE8F4UL : 0x44535FUL;
    int16_t cx = (int16_t)(x + MAPA_MZ_LADO / 2);
    int16_t cy = (int16_t)(y + MAPA_MZ_LADO / 2);

    gfx2_rrect(s, x, y, MAPA_MZ_LADO, MAPA_MZ_LADO, 8, gfx2_rgba(0x0A1622UL, 215U));
    gfx2_rrect_outline(s, x, y, MAPA_MZ_LADO, MAPA_MZ_LADO, 8, 1,
                       gfx2_rgb(0x2C4356UL));
    /* El signo son dos rectangulos y no un caracter: asi no depende de que
     * tipografia este cargada ni de como centre ella el '+'. */
    gfx2_fill(s, (int16_t)(cx - 8), (int16_t)(cy - 1), 17, 3, gfx2_rgb(tinta));
    if (mas) {
        gfx2_fill(s, (int16_t)(cx - 1), (int16_t)(cy - 8), 3, 17, gfx2_rgb(tinta));
    }
}

void mapa_zoom_botones(gfx2_surf_t *s, const mapa_caja_t *c)
{
    uint8_t e = (c->esc == 0U) ? 1U : c->esc;

    /* Apagado cuando ya no se puede ir por ahi: un mando que no hace nada
     * y no lo dice se lee como que el mapa se ha quedado colgado. */
    mz_uno(s, c, 1U, (uint8_t)(e < (uint8_t)MAPA_ESC_MAX));
    mz_uno(s, c, 0U, (uint8_t)(e > 1U));
}

uint8_t mapa_zoom_hit(const mapa_caja_t *c, int16_t x, int16_t y)
{
    if (y < mz_y(c) || y >= (int16_t)(mz_y(c) + MAPA_MZ_LADO)) { return 0U; }
    if (x >= mz_x(c, 1U) && x < (int16_t)(mz_x(c, 1U) + MAPA_MZ_LADO)) { return 1U; }
    if (x >= mz_x(c, 0U) && x < (int16_t)(mz_x(c, 0U) + MAPA_MZ_LADO)) { return 2U; }
    return 0U;
}

void mapa_casa(gfx2_surf_t *s, const mapa_caja_t *c, int16_t px, int16_t py)
{
    static const uint32_t k_casa = 0xFFC83CUL;

    if (px < c->x || px >= (int16_t)(c->x + c->w)) { return; }
    if (py < c->y || py >= (int16_t)(c->y + c->h)) { return; }

    /* Un aspa, que no se confunde con ninguna marca de estacion. */
    gfx2_line(s, (int16_t)(px - 4), (int16_t)(py - 4),
                 (int16_t)(px + 4), (int16_t)(py + 4), gfx2_rgb(k_casa));
    gfx2_line(s, (int16_t)(px - 4), (int16_t)(py + 4),
                 (int16_t)(px + 4), (int16_t)(py - 4), gfx2_rgb(k_casa));
}
