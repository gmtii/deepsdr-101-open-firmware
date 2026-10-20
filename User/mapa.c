#include "mapa.h"
#include "mapa_bin.h"
#include "palette.h"

/*
 * EL MAPA SIGUE AL TEMA (SOLO EN UNO) - 05/10/2026.
 *
 * *El que lo abrio* pedia que el mapa dejara de tener paleta propia y
 * siguiera la del resto de la interfaz, porque con algunos temas los
 * contornos de los continentes apenas se le veian.
 *
 * Era cierto, y era peor de lo que el creia: este fichero llevaba NUEVE
 * colores escritos a pelo y CERO referencias a la paleta. El mapa salia
 * azul oscuro con el tema que fuera, incluidos los dos claros que ya
 * habia. Un tema no cambiaba el mapa ni un pixel.
 *
 * POR QUE NO SE ARMONIZA PARA TODOS. Porque seria cambiarle la pantalla a
 * quien no ha pedido nada: los seis temas de antes se disenaron mirando
 * este mapa azul, y el azul sobre fondo oscuro funciona. Asi que el mapa
 * sigue al tema SOLO donde el tema lo pide -palette.h, campo mapa_sigue-,
 * y hoy lo pide Chemistry y nadie mas.
 *
 * EL REPARTO DE PAPELES, que es lo unico que hay que entender para anadir
 * otro tema a esto:
 *
 *     mar       surf_0     el fondo del mapa es un fondo de pantalla
 *     rejilla   grid       recesiva a proposito, como la del espectro
 *     ecuador   line       la misma rejilla, un escalon por encima
 *     costa     ink_dim    es el CONTENIDO del mapa, no decoracion
 *     boton     surf_2     un control en reposo
 *     borde     line       un contorno
 *     tinta     ink        el signo + / - encendido
 *     tinta_off ink_mute   y apagado
 *     casa      accent     "lo tuyo", igual que el digito de sintonia
 *
 * La casa va en accent y NO en warn aunque el amarillo de antes se le
 * pareciera: un color de estado no se reutiliza como decoracion, que es
 * regla vieja de palette.h.
 */
void mapa_tinte(mapa_tinte_t *t)
{
    if (g_pal->mapa_sigue) {
        t->mar       = PAL_SURF_0;
        t->tierra    = PAL_SURF_0;   /* sin .map no hay relleno: ver mapa.c */
        t->rejilla   = PAL_GRID;
        t->ecuador   = PAL_LINE;
        t->costa     = PAL_INK_DIM;
        t->frontera  = PAL_LINE;
        t->boton     = PAL_SURF_2;
        t->borde     = PAL_LINE;
        t->tinta     = PAL_INK;
        t->tinta_off = PAL_INK_MUTE;
        t->casa      = PAL_ACCENT;
        t->rotulo    = PAL_INK;
        /*
         * EL RAYO VA EN EL COLOR DE "LO TUYO" y comparte acento con el
         * aspa del QTH a proposito: sale de ella. La regla de palette.h
         * -un color de estado no se reutiliza como decoracion- se respeta,
         * porque accent no es un estado: es el digito de sintonia y el
         * aspa, o sea exactamente "lo tuyo".
         */
        t->linea     = PAL_ACCENT;
    } else if (mapa_bin_hay()) {
        /*
         * EL MAPA DEL DUEÑO TRAE SUS COLORES - 12/10/2026.
         *
         * El orden manda y es: un tema que pida seguir al mapa gana
         * siempre (arriba), porque eso lo ha elegido el usuario a proposito;
         * si no, el .map; y si no hay .map, los nueve de siempre.
         *
         * Que el fichero traiga los colores no es un adorno: quien hace un
         * mapa de mar casi blanco es el unico que sabe de que color tiene
         * que ser el punto encima.
         */
        t->mar       = mapa_bin_color(MAPA_COL_MAR,       0x0B1A2AUL);
        t->tierra    = mapa_bin_color(MAPA_COL_TIERRA,    0x16304AUL);
        t->rejilla   = mapa_bin_color(MAPA_COL_REJILLA,   0x16304AUL);
        t->ecuador   = mapa_bin_color(MAPA_COL_ECUADOR,   0x24466AUL);
        t->costa     = mapa_bin_color(MAPA_COL_COSTA,     0x3E7FA8UL);
        t->frontera  = mapa_bin_color(MAPA_COL_FRONTERA,  0x2C4356UL);
        t->boton     = mapa_bin_color(MAPA_COL_BOTON,     0x0A1622UL);
        t->borde     = mapa_bin_color(MAPA_COL_BORDE,     0x2C4356UL);
        t->tinta     = mapa_bin_color(MAPA_COL_TINTA,     0xDCE8F4UL);
        t->tinta_off = mapa_bin_color(MAPA_COL_TINTA_OFF, 0x44535FUL);
        t->casa      = mapa_bin_color(MAPA_COL_CASA,      0xFFC83CUL);
        /*
         * LOS DOS QUE LLEVABAN DESDE AGOSTO EN LA PALETA SIN QUE NADIE LOS
         * MIRARA - 12/10/2026.
         *
         * `rotulo` y `linea` son los huecos 13 y 14 de los quince colores
         * del .map. Estaban en el enum, estaban en tools/mapa_pack.py y los
         * cien tipos los traen puestos; lo que no habia era quien los
         * leyera, igual que paso con `frontera` hasta la V4.60. Hoy tienen
         * para que: el indicativo y el rayo desde el QTH.
         */
        t->rotulo    = mapa_bin_color(MAPA_COL_ROTULO,    0xDCE8F4UL);
        t->linea     = mapa_bin_color(MAPA_COL_LINEA,     0xFFC83CUL);
    } else {
        /* Los nueve de siempre, que es lo que veia la radio hasta hoy. */
        t->mar       = 0x0B1A2AUL;
        t->tierra    = 0x16304AUL;
        t->rejilla   = 0x16304AUL;
        t->ecuador   = 0x24466AUL;
        t->costa     = 0x3E7FA8UL;
        t->frontera  = 0x2C4356UL;
        t->boton     = 0x0A1622UL;
        t->borde     = 0x2C4356UL;
        t->tinta     = 0xDCE8F4UL;
        t->tinta_off = 0x44535FUL;
        t->casa      = 0xFFC83CUL;
        t->rotulo    = 0xDCE8F4UL;
        t->linea     = 0xFFC83CUL;
    }
}

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
/*
 * EL LOCALIZADOR A CENTESIMAS DE GRADO, SIN TOCAR LA PANTALLA - 12/10/2026.
 *
 * Esto estaba DENTRO de mapa_loc_a_px(), que es lo unico que lo necesitaba
 * hasta que aparecieron la distancia y el azimut del indicativo que se
 * toca en el mapa (ver mapa_eti.h). Ahora hacen falta los grados sin
 * proyectar, y la salida es una sola: un segundo parser de Maidenhead es
 * exactamente como se consiguio una vez tener el convenio de la longitud
 * mal en WSPR -ver el 28/09 en CAMBIOS.md-.
 *
 * NO recorta la latitud a la tabla de la costa: eso es cosa del DIBUJO
 * -una marca por encima del tope se pega al borde en vez de desaparecer-
 * y falsearia una distancia. El recorte se queda en mapa_loc_a_px(), que
 * es quien pinta.
 */
uint8_t mapa_loc_a_cent(const char *loc, int32_t *lon_c, int32_t *lat_c)
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

    if (lon_c != 0) { *lon_c = lon; }
    if (lat_c != 0) { *lat_c = lat; }
    return 1U;
}

uint8_t mapa_loc_a_px(const mapa_caja_t *c, const char *loc,
                      int16_t *px, int16_t *py)
{
    int32_t lon, lat;

    if (!mapa_loc_a_cent(loc, &lon, &lat)) { return 0U; }

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

/* Ver mapa.h. La cuenta de los pasos vive aqui para que el mando y los
 * botones no tengan cada uno la suya. */
uint8_t mapa_zoom_paso(mapa_caja_t *c, int32_t pasos, int16_t ax, int16_t ay)
{
    uint32_t e;

    if (c == (mapa_caja_t *)0) { return 1U; }
    e = (c->esc > 0U) ? (uint32_t)c->esc : 1U;
    for (; pasos > 0; pasos--) {
        if (e < (uint32_t)MAPA_ESC_MAX) { e *= 2U; }
    }
    for (; pasos < 0; pasos++) {
        if (e > 1U) { e /= 2U; }
    }
    return mapa_zoom(c, (uint8_t)e, ax, ay);
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

/* Ver mapa.h: el reparto del gesto vive aqui para poder medirlo. */
void mapa_gesto_empieza(mapa_gesto_t *g, int16_t x, int16_t y)
{
    if (g == 0) { return; }
    g->x0 = x; g->y0 = y;
    g->x  = x; g->y  = y;
}

uint8_t mapa_gesto_mueve(mapa_gesto_t *g, int16_t x, int16_t y,
                         int16_t *dx, int16_t *dy)
{
    int16_t ax, ay;

    if (g == 0) { return 0U; }
    ax = (int16_t)(x - g->x);
    ay = (int16_t)(y - g->y);
    if (ax > -MAPA_GESTO_ARRASTRA && ax < MAPA_GESTO_ARRASTRA
        && ay > -MAPA_GESTO_ARRASTRA && ay < MAPA_GESTO_ARRASTRA) {
        return 0U;
    }
    g->x = x;
    g->y = y;
    if (dx != 0) { *dx = ax; }
    if (dy != 0) { *dy = ay; }
    return 1U;
}

uint8_t mapa_gesto_suelta(const mapa_gesto_t *g, int16_t *tx, int16_t *ty)
{
    int16_t ax, ay;

    if (g == 0) { return 0U; }
    /*
     * EL RECORRIDO TOTAL, no "si alguna vez se movio dos pixeles". Un
     * dedo en un panel resistivo tiembla, y con el temblor contando como
     * arrastre no habria toque nunca. Lo que separa un toque de un
     * arrastre es DONDE ACABA respecto de donde empezo.
     */
    ax = (int16_t)(g->x - g->x0);
    ay = (int16_t)(g->y - g->y0);
    if (ax < 0) { ax = (int16_t)-ax; }
    if (ay < 0) { ay = (int16_t)-ay; }
    if (ax > MAPA_GESTO_TOQUE || ay > MAPA_GESTO_TOQUE) { return 0U; }
    if (tx != 0) { *tx = g->x0; }
    if (ty != 0) { *ty = g->y0; }
    return 1U;
}

/*
 * LAS REGLAS DE QUE ES UN LOCALIZADOR, EN UN SOLO SITIO.
 *
 * Estaban dentro de mapa_loc_de_linea(). Se sacan aqui porque desde el
 * 28/09/2026 hay DOS buscadores -el de FT8, que mira la ultima palabra, y
 * el de JTTY, que mira todas- y dos copias de estas reglas son dos copias
 * que un dia se separan. Lo que cambia entre los dos modos es DONDE se
 * busca, no que se acepta.
 */
static uint8_t loc_vale(const char *w, uint8_t n)
{
    if (n != 4U && n != 6U) { return 0U; }

    /*
     * LOS QUE TIENEN FORMA DE LOCALIZADOR Y NO LO SON. RR73 es como acaba
     * casi todo contacto de FT8 y caeria en el Pacifico, al noreste de
     * Fiyi: cada QSO terminado pondria una marca donde no hay nadie.
     */
    if (w[0] == 'R' && w[1] == 'R' && w[2] == '7'
        && (w[3] == '1' || w[3] == '2' || w[3] == '3')) { return 0U; }

    if (w[0] < 'A' || w[0] > 'R' || w[1] < 'A' || w[1] > 'R'
        || w[2] < '0' || w[2] > '9' || w[3] < '0' || w[3] > '9') { return 0U; }
    if (n == 6U) {
        if (w[4] < 'A' || w[4] > 'X' || w[5] < 'A' || w[5] > 'X') { return 0U; }
    }
    return 1U;
}

/* El principio del campo `campo` (contando desde 1), o 0 si no lo hay. */
static const char *campo_de(const char *linea, uint8_t campo)
{
    const char *p = linea;
    uint8_t tabs = 0U;

    if (campo <= 1U) { return linea; }
    while (*p != '\0') {
        if (*p == '\t') {
            tabs++;
            if (tabs == (uint8_t)(campo - 1U)) { return p + 1; }
        }
        p++;
    }
    return 0;
}

uint8_t mapa_loc_de_campo(const char *linea, uint8_t campo, char *out)
{
    const char *p;

    if (linea == 0 || out == 0) { return 0U; }
    out[0] = '\0';
    p = campo_de(linea, campo);
    if (p == 0) { return 0U; }

    while (*p != '\0' && *p != '\t') {
        uint8_t n = 0U;
        char w[8];

        /* Una palabra: hasta el espacio, el tabulador o el final. */
        while (p[n] != '\0' && p[n] != '\t' && p[n] != ' ' && n < 7U) {
            w[n] = p[n];
            n++;
        }
        w[n] = '\0';
        /* Si se paro por el tope de siete, no era una palabra de 4 ni de 6
         * y hay que saltarla entera, no tomar sus primeros siete. */
        if (n < 7U && loc_vale(w, n)) {
            uint8_t k;

            for (k = 0U; k <= n; k++) { out[k] = w[k]; }
            return 1U;
        }
        p += n;
        while (*p == ' ') { p++; }
        if (n == 7U) { while (*p != '\0' && *p != '\t' && *p != ' ') { p++; }
                       while (*p == ' ') { p++; } }
    }
    return 0U;
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
    /* Las reglas de que vale son las mismas para los dos buscadores y
     * viven en loc_vale(), arriba. */
    if (!loc_vale(out, n)) { out[0] = '\0'; return 0U; }
    return 1U;
}

/*
 * EL INDICATIVO DE QUIEN LLAMA EN UN CQ - 12/10/2026.
 *
 * En un CQ de FT8 el mensaje es "CQ CALL LOC", a veces con un adorno en
 * medio: "CQ DX CALL LOC", "CQ NA CALL LOC", "CQ POTA CALL LOC". O sea que
 * el indicativo NO esta en un sitio fijo contando desde el principio, pero
 * si contando desde el final: es la palabra ANTERIOR al localizador.
 *
 * Y por eso esta funcion exige que la ultima sea un localizador de verdad
 * -la misma regla de loc_vale() que usa todo lo demas- en vez de fiarse de
 * la posicion: un "CQ EA1ABC" sin localizador no tiene donde pintarse, asi
 * que tampoco tiene etiqueta, y devolver ahi "CQ" como indicativo seria
 * poner un rotulo que dice CQ encima de un punto que no existe.
 *
 * QUE CUENTA COMO INDICATIVO, y hace falta decirlo porque la palabra
 * anterior al localizador puede ser el adorno: de tres a once caracteres,
 * solo letras, digitos y la barra, y AL MENOS UN DIGITO. Lo del digito es
 * lo que separa un indicativo de "CQ", de "DX", de "NA" y de "POTA", que
 * es justo lo que hay en ese sitio cuando no hay indicativo.
 */
uint8_t mapa_ind_de_linea(const char *linea, char *out)
{
    const char *p = linea;
    const char *msg = 0;
    const char *ini = 0, *ant = 0;
    const char *q;
    uint8_t n = 0U, tabs = 0U, digitos = 0U;
    char loc[8];

    if (linea == 0 || out == 0) { return 0U; }
    out[0] = '\0';

    /* Sin localizador no hay donde pintar, asi que no hay indicativo que
     * dar. Y de paso valida que el renglon tiene la forma que se espera. */
    if (!mapa_loc_de_linea(linea, loc)) { return 0U; }

    while (*p != '\0') {
        if (*p == '\t') {
            tabs++;
            if (tabs == 4U) { msg = p + 1; }
            else if (tabs == 5U) { break; }
        }
        p++;
    }
    if (msg == 0) { return 0U; }

    /* El principio de la ULTIMA palabra y el de la ANTERIOR. */
    ini = msg;
    for (q = msg; *q != '\0' && *q != '\t'; q++) {
        if (*q == ' ') { ant = ini; ini = q + 1; }
    }
    if (ant == 0) { return 0U; }   /* "CQ LOC": no hay indicativo */

    while (ant[n] != '\0' && ant[n] != ' ' && ant[n] != '\t'
           && n < (uint8_t)11U) {
        char c = ant[n];

        if (c >= 'a' && c <= 'z') { c = (char)(c - 'a' + 'A'); }
        if (c >= '0' && c <= '9') { digitos++; }
        else if (!((c >= 'A' && c <= 'Z') || c == '/')) { return 0U; }
        out[n] = c;
        n++;
    }
    if (ant[n] != '\0' && ant[n] != ' ' && ant[n] != '\t') { return 0U; }
    out[n] = '\0';
    if (n < 3U || digitos == 0U) { out[0] = '\0'; return 0U; }
    return 1U;
}

/* --------------------------------------------------------------- */

/*
 * LA VENTANA, UNA VEZ POR FOTOGRAMA Y NO UNA POR BANDA - 12/10/2026.
 *
 * mapa_dibuja() se llama UNA VEZ POR BANDA de 16 filas, o sea dieciocho
 * veces por fotograma. Pedir ahi la ventana seria hacer dieciocho tandas
 * de lecturas de flash por fotograma, que es exactamente lo contrario de
 * lo que el cache de ventana viene a arreglar.
 *
 * Asi que esto se llama APARTE, antes de gfx2_render(), desde quien pinta
 * la pantalla. Calcula que trozo del mundo se ve y se lo dice a
 * mapa_bin.c; si ya esta cargado, no hace nada.
 *
 * Devuelve 1 si hubo recarga, por si alguien quiere medirla.
 */
uint8_t mapa_ventana(const mapa_caja_t *c)
{
    int32_t mw = mapa_mundo_w(c), mh = mapa_mundo_h(c);
    int32_t lon0, lon1, lat0, lat1;

    if (mw <= 0L || mh <= 0L) { return 0U; }

    /* Lo inverso de px_de_lon()/py_de_lat(): de la esquina en pixeles a
     * los grados por cien que se ven. */
    lon0 = ((int32_t)c->off_x * (2L * LON_C_MAX)) / mw - LON_C_MAX;
    lon1 = (((int32_t)c->off_x + (int32_t)c->w) * (2L * LON_C_MAX)) / mw
         - LON_C_MAX;
    lat1 = (int32_t)k_mapa_lat_tope_c
         - ((int32_t)c->off_y * (2L * (int32_t)k_mapa_lat_tope_c)) / mh;
    lat0 = (int32_t)k_mapa_lat_tope_c
         - (((int32_t)c->off_y + (int32_t)c->h)
            * (2L * (int32_t)k_mapa_lat_tope_c)) / mh;

    return mapa_bin_ventana(c->esc == 0U ? 1U : c->esc, lon0, lat0, lon1, lat1);
}

/*
 * ======================================================================
 * RELLENAR LA TIERRA - 14/10/2026
 * ======================================================================
 *
 * MAPA_COL_TIERRA lleva en la paleta de los cien mapas desde el primer dia
 * y no lo leia nadie: esta funcion rellenaba el panel con el color del mar
 * y dibujaba las costas encima. Por eso el catalogo tenia que avisar de que
 * "el estilo HOY NO CAMBIA EL DIBUJO" y por eso 013, 014 y 015 eran el
 * mismo fichero byte a byte.
 *
 * QUE es tierra lo dice el fichero -tools/mapa_pack.py marca los anillos
 * cerrados- y COMO se pinta lo dice el byte de estilo. La radio no averigua
 * topologia: eso ya esta decidido cuando el .map llega aqui.
 *
 * ----------------------------------------------------------------------
 * POR BANDA, Y LAS DIECISEIS FILAS DE UNA PASADA
 * ----------------------------------------------------------------------
 *
 * gfx2 compone en bandas de 16 filas, asi que esto corre dieciocho veces
 * por fotograma igual que el dibujado de la costa. Recorrer la geometria
 * UNA VEZ POR FILA serian dieciseis pasadas por banda, o sea 288 por
 * fotograma: eso no cabe.
 *
 * Asi que se recorre UNA vez por banda y cada arista deja su cruce en TODAS
 * las filas de la banda que atraviesa. El coste es el mismo recorrido que
 * ya se hace para pintar la costa, mas un apunte por cruce.
 *
 * LO QUE COSTO, MEDIDO ANTES DE ESCRIBIRLO (sim/mapa_relleno.c, sobre la
 * geometria de verdad capturada de un fotograma):
 *
 *     escala 1   4.247 cruces   razon 0,19 contra el dibujado de hoy
 *     escala 2                  razon 0,08
 *     escala 4                  razon 0,03
 *
 * Y la version ingenua -mirar todas las aristas en cada fila- habria
 * costado 448.128 cruces, 106 veces mas. Por eso el numero esta escrito:
 * para que nadie la escriba.
 *
 * ----------------------------------------------------------------------
 * PAR-IMPAR, Y POR QUE NO HACE FALTA SABER EL SENTIDO DE GIRO
 * ----------------------------------------------------------------------
 *
 * Se cuentan los cruces de cada fila, se ordenan y se pinta entre el 1º y
 * el 2º, el 3º y el 4º... Un lago dentro de un continente sale hueco solo,
 * y una isla dentro del lago vuelve a salir llena, sin que nadie calcule
 * quien esta dentro de quien.
 *
 * ----------------------------------------------------------------------
 * LO QUE NO SE RELLENA, Y ESTA BIEN QUE NO
 * ----------------------------------------------------------------------
 *
 * - Las aristas que cruzan el antimeridiano. Son las que el dibujado de la
 *   costa ya se salta con LON_C_MAX, porque en pantalla son un salto de
 *   lado a lado que no existe. Meterlas en el relleno pintaria una franja
 *   de tierra de un borde al otro.
 * - Los niveles troceados. mapa_pack.py solo marca anillos en el x1, que
 *   es el que guarda los trazos enteros; un anillo partido por casillas ya
 *   no cierra. O sea que el relleno se ve al zoom 1 y no al acercarse.
 *
 *   PERO EL MOTIVO QUE SE ESCRIBIO AQUI ERA EL EQUIVOCADO - 14/10/2026.
 *   Decia que haria falta "rellenar casilla por casilla, y eso es otro
 *   trabajo". Medido con sim/mapa_troceado.c sobre la geometria de verdad:
 *
 *     escala   segmentos   cruces/fila (max)   extremos sueltos   en el borde
 *        1        1556            36                 13              13
 *        2         416            34                 12              12
 *        4          93            11                  5               5
 *        8          37            15                  0               0
 *
 *   El par-impar no cuenta anillos: cuenta CRUCES. Dos piezas de un trazo
 *   cortado por una casilla comparten el punto del corte, asi que el juego
 *   de aristas es el MISMO que si el trazo fuera entero: el corte es
 *   interior y al barrido le da igual. Y se ve en la tabla: el 100% de los
 *   extremos sueltos estan pegados al borde de la VENTANA, ni uno en medio
 *   del mapa. O sea que trocear no rompe nada.
 *
 *   Lo que falta no es cerrar casillas: es saber si el borde izquierdo de
 *   cada fila cae en tierra o en agua -la paridad de partida-, porque una
 *   ventana dentro de un continente no tiene ninguna costa que cruzar y el
 *   par-impar la dejaria toda de mar. Y eso SI se puede saber sin tocar el
 *   formato: el x1 lleva sus anillos marcados y VIVE SIEMPRE EN RAM (ver
 *   mapa_bin.c, "el unico que vive en RAM"), asi que la paridad sale de
 *   contar sus cruces a la izquierda de la ventana. Un booleano por fila.
 *
 *   Queda sin hacer, pero ya no por el motivo que decia: no es otro
 *   trabajo grande, es un sembrado de paridad.
 *
 * LA MEMORIA. Dieciseis filas por MAPA_CRUCES_MAX cruces, en .bss y no en
 * la pila: son 1.296 bytes y la pila de esta radio no esta para regalar
 * kilobytes a una funcion de dibujo. Si una fila se pasa de cruces se
 * DESCARTA ESA FILA entera en vez de pintar media: media fila rellena es
 * una costa que no existe, y eso se ve; una fila sin rellenar entre dos
 * rellenas, con 16 filas por banda, no.
 */
#ifndef MAPA_CRUCES_MAX
#define MAPA_CRUCES_MAX 56
#endif

static int16_t s_cruce[GFX2_BAND_H][MAPA_CRUCES_MAX];
static uint8_t s_ncruce[GFX2_BAND_H];
static uint8_t s_cruce_lleno[GFX2_BAND_H];

static void cruces_limpia(void)
{
    uint8_t i;
    for (i = 0U; i < (uint8_t)GFX2_BAND_H; i++) {
        s_ncruce[i] = 0U;
        s_cruce_lleno[i] = 0U;
    }
}

/* Apunta el cruce de la arista (ax,ay)-(bx,by) con cada fila de la banda
 * que atraviesa. Las filas van de y0 a y0+alto-1, en coordenadas de
 * pantalla.
 *
 * LOS CRUCES DE FUERA DE [izq, der] NO SE APUNTAN - 16/10/2026, y esto es
 * lo que arreglo LAS RAYAS AL ACERCARSE. Ver el bloque RECORTAR ANTES DE
 * APUNTAR, mas abajo. Quien no quiera recortar pasa los topes del int16_t
 * y aqui no se tira nada.
 */
static void cruces_arista(int16_t ax, int16_t ay, int16_t bx, int16_t by,
                          int16_t y0, int16_t alto, int16_t izq, int16_t der)
{
    int16_t ylo, yhi, y;
    int32_t xlo, dx, dy;

    if (ay == by) { return; }            /* horizontal: no cruza ninguna fila */

    if (ay < by) { ylo = ay; yhi = by; xlo = ax; dx = (int32_t)bx - ax; }
    else         { ylo = by; yhi = ay; xlo = bx; dx = (int32_t)ax - bx; }
    dy = (int32_t)yhi - ylo;

    /*
     * LA REGLA DE ARRIBA SI Y ABAJO NO, que es lo que hace que par-impar
     * funcione en los vertices. Si las dos aristas que se juntan en un
     * pico contaran las dos su vertice, ese pico daria DOS cruces y el
     * relleno se daria la vuelta justo ahi. Contando [ylo, yhi) el vertice
     * lo cuenta una sola de las dos.
     */
    if (ylo < y0) { ylo = y0; }
    if (yhi > (int16_t)(y0 + alto)) { yhi = (int16_t)(y0 + alto); }

    for (y = ylo; y < yhi; y++) {
        uint8_t f = (uint8_t)(y - y0);
        int16_t x;

        if (y < (ay < by ? ay : by) || y >= (ay < by ? by : ay)) { continue; }
        x = (int16_t)(xlo + (dx * (int32_t)(y - (ay < by ? ay : by))) / dy);
        if (x < izq || x > der) { continue; }
        if (s_ncruce[f] >= (uint8_t)MAPA_CRUCES_MAX) {
            s_cruce_lleno[f] = 1U;
            continue;
        }
        s_cruce[f][s_ncruce[f]++] = x;
    }
}

/* ------------------------------------------- RELLENAR AL ACERCARSE
 *
 * Al zoom 1 el par-impar se arranca solo: toda fila empieza en el borde
 * del mapa, que es mar. Al acercarse no, y una ventana metida en mitad de
 * un continente saldria toda de mar. Lo que falta es LA PARIDAD DE
 * PARTIDA, y la trae el .map: un bit por fila en el borde izquierdo de
 * cada casilla (ver mapa_bin_par_lee()).
 *
 * SE SIEMBRA EN CADA BORDE DE CASILLA Y NO SOLO EN EL DE LA PANTALLA.
 * Sembrar una vez y dejar correr el par-impar hasta la otra punta hace
 * que una sola isla perdida por la simplificacion invierta todo lo que
 * tenga a su derecha. Volviendo a sembrar en cada casilla, ese daño no
 * sale de la casilla donde esta la isla.
 *
 * Y SE USAN LAS COSTAS, NO LOS ANILLOS. En los niveles troceados no hay
 * anillos -un anillo partido por casillas ya no cierra- pero el par-impar
 * no cuenta anillos: cuenta CRUCES, y dos piezas de un trazo cortado
 * comparten el punto del corte, asi que el juego de aristas es el mismo.
 * Medido en su dia con sim/mapa_troceado.c: el 100% de los extremos
 * sueltos estan pegados al borde de la ventana, ni uno en mitad del mapa.
 */
#define MAPA_SEM_COLS  8U     /* columnas de casilla visibles a la vez; sem[] es
                               * una mascara de 8 bits y no da para mas */
#define MAPA_SEM_BUF   8U     /* bytes de paridad que se leen de golpe */

/* De una columna de pantalla a su longitud, en centesimas. Inversa de
 * px_de_lon(). */
static int32_t lon_de_px(const mapa_caja_t *c, int16_t x)
{
    int32_t mw = mapa_mundo_w(c);

    if (mw <= 0) { return 0; }
    return (((int32_t)x - (int32_t)c->x + (int32_t)c->off_x) * 2L * LON_C_MAX)
           / mw - LON_C_MAX;
}

/* De una fila de pantalla a su latitud, en centesimas. Es la inversa
 * exacta de py_de_lat(): si una se toca, la otra tambien. */
static int32_t lat_de_py(const mapa_caja_t *c, int16_t y)
{
    int32_t t = (int32_t)k_mapa_lat_tope_c;
    int32_t mh = mapa_mundo_h(c);

    if (mh <= 0) { return 0; }
    return t - (((int32_t)y - (int32_t)c->y + (int32_t)c->off_y) * 2L * t) / mh;
}

/* La paridad de (casilla, fila dentro de la casilla), leyendo de la flash
 * en trozos de MAPA_SEM_BUF bytes. `ult_cas`/`ult_b0` son el trozo que ya
 * esta en `buf`: con las filas en orden, casi siempre acierta. */
static uint8_t semilla(uint16_t cas, uint16_t bit, uint8_t *buf,
                       uint16_t *ult_cas, uint16_t *ult_b0)
{
    uint16_t b = (uint16_t)(bit >> 3);
    uint16_t b0 = (uint16_t)(b & ~(uint16_t)(MAPA_SEM_BUF - 1U));
    uint8_t  n = mapa_bin_par_bytes();

    if (*ult_cas != cas || *ult_b0 != b0) {
        uint8_t cuantos = (uint8_t)MAPA_SEM_BUF;

        if ((uint16_t)b0 + cuantos > (uint16_t)n) {
            cuantos = (uint8_t)((uint16_t)n - b0);
        }
        if (!mapa_bin_par_lee(cas, (uint8_t)b0, buf, cuantos)) { return 0U; }
        *ult_cas = cas; *ult_b0 = b0;
    }
    return (uint8_t)((buf[b - b0] >> (bit & 7U)) & 1U);
}

/*
 * LOS BORDES DE LAS CASILLAS QUE SE VEN.
 *
 * Devuelve cuantas columnas de casilla caben (0 si el fichero dice que
 * trae paridad y no trae troceado, que es un fichero que no se puede
 * dibujar) y deja en bx[] la columna de pantalla de cada borde.
 *
 * SE CALCULA ANTES DE APUNTAR LOS CRUCES Y NO DESPUES - 16/10/2026. No es
 * un capricho de orden: bx[0] es el limite por la izquierda del recorte,
 * y sin el no se puede recortar nada. Ver RECORTAR ANTES DE APUNTAR.
 */
static uint8_t casillas_bordes(const mapa_caja_t *c, int16_t *bx,
                               int16_t *cx0_sal)
{
    uint8_t nx = mapa_bin_nx(), ny = mapa_bin_ny();
    uint8_t ncol = 0U, ci;
    int32_t ancho_cas, lon_izq;
    int16_t cx0;

    if (nx == 0U || ny == 0U || mapa_bin_par_bytes() == 0U) {
        /* El fichero dice que trae paridad y no trae troceado: no se
         * dibuja nada antes que dibujar al azar. */
        return 0U;
    }
    ancho_cas = (2L * LON_C_MAX) / (int32_t)nx;
    /* La casilla en la que cae el borde izquierdo de lo que se ve; se
     * empieza en SU borde, que puede quedar fuera de la pantalla por la
     * izquierda. */
    lon_izq = lon_de_px(c, c->x);
    cx0 = (int16_t)((lon_izq + LON_C_MAX) / ancho_cas);
    if (cx0 < 0) { cx0 = 0; }
    if (cx0 > (int16_t)(nx - 1U)) { cx0 = (int16_t)(nx - 1U); }

    for (ci = 0U; ci < (uint8_t)MAPA_SEM_COLS; ci++) {
        int16_t cx = (int16_t)(cx0 + (int16_t)ci);
        int16_t px;

        if (cx > (int16_t)(nx - 1U)) { break; }
        px = px_de_lon(c, -LON_C_MAX + (int32_t)cx * ancho_cas);
        if (ci != 0U && px >= (int16_t)(c->x + c->w)) { break; }
        bx[ci] = px;
        ncol = (uint8_t)(ci + 1U);
    }
    *cx0_sal = cx0;
    return ncol;
}

static void cruces_pinta_sembrado(gfx2_surf_t *s, const mapa_caja_t *c,
                                  int16_t y0, int16_t alto, gfx2_rgba_t color,
                                  const int16_t *bx, const uint8_t *sem,
                                  uint8_t ncol)
{
    uint8_t f;
    int16_t der = (int16_t)(c->x + c->w);

    for (f = 0U; f < (uint8_t)alto; f++) {
        uint8_t n = s_ncruce[f], i;
        int16_t y = (int16_t)(y0 + f);
        uint8_t col = 0U, dentro;
        int32_t xcur;

        if (s_cruce_lleno[f]) { continue; }

        /* La misma ordenacion por insercion que el camino del zoom 1: n
         * es pequeño y para eso insercion gana a cualquier cosa. */
        for (i = 1U; i < n; i++) {
            int16_t v = s_cruce[f][i];
            int16_t j = (int16_t)(i - 1U);
            while (j >= 0 && s_cruce[f][j] > v) {
                s_cruce[f][j + 1] = s_cruce[f][j];
                j--;
            }
            s_cruce[f][j + 1] = v;
        }

        dentro = (uint8_t)((sem[f] >> 0) & 1U);
        xcur   = bx[0];
        i = 0U;
        /*
         * LOS CRUCES QUE QUEDAN A LA IZQUIERDA DE LA PRIMERA CASILLA SE
         * TIRAN, Y NO HACERLO ERA EL FALLO - 15/10/2026.
         *
         * La ventana se carga con MEDIA PANTALLA DE MARGEN a cada lado,
         * asi que en la tabla hay cruces de casillas que no se ven. La
         * semilla ya los cuenta a todos -dice como esta la cosa EN el
         * borde de la casilla, no antes-, asi que volver a aplicarlos
         * invertia la paridad una vez por cada uno.
         *
         * Se veia solo cuando el punto caia cerca del borde izquierdo de
         * la pantalla, que es justo cuando el borde de su casilla se sale
         * por la izquierda y aparecen cruces por delante. En el banco:
         * dieciseis ventanas bien y la primera mal, que es la clase de
         * fallo que una foto no enseña y un barrido si.
         */
        while (i < n && s_cruce[f][i] < bx[0]) { i++; }

        for (;;) {
            int32_t sig_borde = (col + 1U < ncol) ? (int32_t)bx[col + 1U]
                                                  : (int32_t)32767;
            int32_t sig_cruce = (i < n) ? (int32_t)s_cruce[f][i]
                                        : (int32_t)32767;
            int32_t hasta;

            if (sig_borde >= (int32_t)der && sig_cruce >= (int32_t)der) {
                hasta = (int32_t)der;
            } else if (sig_cruce < sig_borde) {
                hasta = sig_cruce;
            } else {
                hasta = sig_borde;
            }

            if (dentro) {
                int32_t a = xcur, b = hasta;

                if (a < (int32_t)c->x) { a = c->x; }
                if (b > (int32_t)der)  { b = der; }
                if (b > a) {
                    gfx2_hline(s, (int16_t)a, y, (int16_t)(b - a), color);
                }
            }
            if (hasta >= (int32_t)der) { break; }
            xcur = hasta;
            if (sig_cruce < sig_borde) { dentro = (uint8_t)(dentro ^ 1U); i++; }
            else { col++; dentro = (uint8_t)((sem[f] >> col) & 1U); }
        }
    }
}

static void cruces_pinta(gfx2_surf_t *s, const mapa_caja_t *c,
                         int16_t y0, int16_t alto, gfx2_rgba_t color)
{
    uint8_t f;

    for (f = 0U; f < (uint8_t)alto; f++) {
        uint8_t n = s_ncruce[f], i;
        int16_t y = (int16_t)(y0 + f);

        if (s_cruce_lleno[f] || n < 2U) { continue; }

        /* Ordenacion por insercion: n es pequeño -medido, quince de media
         * y nunca cerca de cuarenta- y para eso insercion gana a cualquier
         * cosa con recursion o con tabla. */
        for (i = 1U; i < n; i++) {
            int16_t v = s_cruce[f][i];
            /*
             * int16_t Y NO int8_t - 14/10/2026.
             *
             * Aqui ponia int8_t "porque n es pequeño". Lo es HOY, con el
             * tope en cuarenta y pico; pero el indice del que inserta
             * llega hasta n-2, asi que en cuanto alguien suba el tope de
             * 128 esto se da la vuelta en silencio y el barrido ordena
             * mal. Lo descubri subiendolo a 512 para medir, que es
             * exactamente la clase de mina que deja un "es pequeño".
             */
            int16_t j = (int16_t)(i - 1U);
            while (j >= 0 && s_cruce[f][j] > v) {
                s_cruce[f][j + 1] = s_cruce[f][j];
                j--;
            }
            s_cruce[f][j + 1] = v;
        }

        for (i = 0U; (uint8_t)(i + 1U) < n; i = (uint8_t)(i + 2U)) {
            int32_t x0 = s_cruce[f][i], x1 = s_cruce[f][i + 1U];
            int8_t v;

            /*
             * EL MISMO TRAMO, TRES VECES: donde cae, un mundo a la
             * izquierda y un mundo a la derecha - 14/10/2026.
             *
             * El emparejamiento se hace en coordenadas DESENROLLADAS (ver
             * el bucle de los anillos), donde un continente que cruza el
             * antimeridiano sigue siendo un trozo continuo y los cruces se
             * ordenan sin ambiguedad. Pero el mundo es un cilindro, y en
             * pantalla ese trozo puede estar a un mundo de distancia.
             *
             * Dibujar el tramo tambien en +-un mundo cuesta dos
             * comparaciones por tramo -los dos de fuera se recortan y no
             * pintan nada- y evita tener que decidir en que lado del corte
             * esta cada cruce, que es justo donde se rompe el par-impar en
             * un cilindro.
             */
            for (v = -1; v <= 1; v++) {
                int32_t w = (int32_t)mapa_mundo_w(c);
                int32_t a = x0 + (int32_t)v * w;
                int32_t b = x1 + (int32_t)v * w;

                if (a < (int32_t)c->x) { a = c->x; }
                if (b > (int32_t)(c->x + c->w)) { b = (int32_t)(c->x + c->w); }
                if (b > a) {
                    gfx2_hline(s, (int16_t)a, y, (int16_t)(b - a), color);
                }
            }
        }
    }
}

void mapa_dibuja(gfx2_surf_t *s, const mapa_caja_t *c)
{
    mapa_tinte_t t;
    const int16_t *p = k_mapa_pts;
    uint16_t linea;
    int16_t i;

    if (!gfx2_band_hits(s, c->y, c->h)) { return; }

    mapa_tinte(&t);
    gfx2_fill(s, c->x, c->y, c->w, c->h, gfx2_rgb(t.mar));

    /*
     * Y LA TIERRA, SI EL MAPA LA TRAE - 14/10/2026. Ver el bloque de arriba.
     *
     * Va AQUI, entre el mar y la rejilla: el relleno es el fondo, y la
     * rejilla, el ecuador y las costas se pintan encima. Al reves taparia
     * las lineas que dan la referencia.
     *
     * El estilo de puntos y el de silueta comparten este relleno de momento
     * -el fichero ya los distingue, el dibujante todavia no-, y eso esta
     * escrito aqui y no solo en el diario para que quien lo lea no crea que
     * se le ha olvidado mirar el byte.
     */
    if (mapa_bin_hay() && mapa_bin_estilo() != MAPA_ESTILO_CONTORNO) {
        int16_t by0 = s->y, bh = s->h;

        if (by0 < c->y) { bh -= (int16_t)(c->y - by0); by0 = c->y; }
        if ((int16_t)(by0 + bh) > (int16_t)(c->y + c->h)) {
            bh = (int16_t)((c->y + c->h) - by0);
        }
        if (bh > (int16_t)GFX2_BAND_H) { bh = (int16_t)GFX2_BAND_H; }

        if (bh > 0) {
            mapa_bin_iter_t ir;
            uint16_t cuantas = mapa_bin_trazos(), tz;
            /*
             * DOS CAMINOS, Y LOS DISTINGUE EL FICHERO - 15/10/2026.
             *
             * Con el nivel ENTERO cargado se rellena como siempre: los
             * anillos cerrados y par-impar desde el borde del mundo, que
             * es mar. Con un nivel TROCEADO y paridad en el .map se
             * rellena con las COSTAS y se siembra la paridad en cada
             * borde de casilla. Un .map sin el bloque contesta que no y
             * la radio se comporta igual que antes: relleno al zoom 1 y
             * nada mas.
             */
            /*
             * TRES CAMINOS, Y LOS DISTINGUE EL FICHERO - 16/10/2026.
             *
             *   1. NIVEL ENTERO: anillos cerrados del mundo y par-impar
             *      desde el borde, que es mar. Es el de siempre y nunca
             *      ha fallado.
             *   2. NIVEL TROCEADO CON ANILLOS POR CASILLA: cada casilla
             *      trae los suyos, recortados contra su rectangulo, y el
             *      par-impar cierra dentro de ella por construccion. Sin
             *      semilla, sin tabla de paridad y sin nada que pueda
             *      contradecirse. Es el camino nuevo.
             *   3. NIVEL TROCEADO SIN ELLOS: un .map de antes de hoy. Se
             *      siembra con la tabla de paridad, que es lo que habia.
             *      SACA RAYAS -ver el bloque de abajo- y por eso el
             *      camino 2 existe; se deja porque un mapa viejo tiene
             *      que seguir pintando algo.
             */
            uint8_t por_casilla = (mapa_bin_relleno_n() != 0U) ? 1U : 0U;
            uint8_t sembrado = (!por_casilla && mapa_bin_par_hay()) ? 1U : 0U;

            /*
             * EL RELLENO AL ACERCARSE, Y LAS RAYAS QUE COSTO - 16/10/2026.
             *
             * LO QUE SE VEIA: a partir del primer nivel troceado -el x4,
             * o sea la segunda vez que se sube el zoom- salian RAYAS
             * HORIZONTALES, tramos de cincuenta a doscientos pixeles del
             * color que no tocaba. Al zoom 1 y 2 nunca.
             *
             * POR QUE. El relleno se hacia por par-impar sobre LAS COSTAS
             * de la casilla, y las costas de un nivel troceado vienen
             * CORTADAS: cada pieza empieza y acaba en el borde. Una
             * polilinea abierta no tiene "dentro", asi que el par-impar
             * no cierra, y lo que sale son tramos invertidos que siempre
             * acaban en un borde de casilla, donde la semilla volvia a
             * poner las cosas en su sitio.
             *
             * NO ERA LA TABLA DE PARIDAD, y eso costo averiguarlo: se
             * probo a sembrar con la verdad -sacada del render del nivel
             * entero, que es correcto- y seguian saliendo 23.
             *
             * LO QUE LO ARREGLA: que cada casilla lleve sus propios
             * ANILLOS CERRADOS, recortados contra su rectangulo con
             * Sutherland-Hodgman (tools/mapa_pack.py,
             * anillos_por_casillas). Con poligonos cerrados el par-impar
             * cierra DENTRO de la casilla por construccion: sin semilla,
             * sin tabla y sin nada que pueda contradecirse.
             *
             * Contado con sim/mapa_rayas.c sobre la ventana de Europa del
             * mapa 036, que es el de la foto:
             *
             *     costas cortadas + tabla de paridad   33 al x4, 10 al x8
             *     ... con filtros encima               18 al x4,  3 al x8
             *     anillos recortados por casilla        0 al x4,  0 al x8
             *
             * LO QUE CUESTA: el .map pasa de 82 a 114 kB -los anillos van
             * ademas de las costas, porque las costas siguen haciendo
             * falta para DIBUJAR-, y la ventana cargada engorda. Medido:
             * de las casillas de la ventana, 23 se quedan fuera por falta
             * de sitio, pero TODAS LAS QUE SE VEN entran (ver las dos
             * pasadas de mapa_bin_ventana). Lo que se pierde es margen,
             * o sea alguna recarga mas al arrastrar, no dibujo.
             *
             * Lo que se probo y se tiro, porque MEDIA PEOR:
             *   votar tres bits de la tabla        19 al x4 (contra 18)
             *   sembrar una sola vez                32
             *   tirar los cruces del borde          24
             *   juntar todos los cruces repetidos  117
             *   juntar solo los del borde           31
             *   sacar la tabla de la geometria del
             *   propio nivel, en el empaquetador    21
             */
            /*
             * RECORTAR ANTES DE APUNTAR - 16/10/2026.
             *
             * NO ES EL FALLO DE LAS RAYAS: se midio y las deja igual, 33
             * al x4 con recorte y sin el. Es OTRO fallo, que estaba al
             * lado y no se veia en la ventana de la foto.
             *
             * La tabla de cruces tiene MAPA_CRUCES_MAX por fila, y la
             * fila que se pasa SE DESCARTA ENTERA: se queda del color del
             * mar, o sea una raya de punta a punta. Y se descartaba sin
             * avisar: ni contador, ni aviso, nada.
             *
             * Lo medido con sim/mapa_rayas.c sobre los 51 mapas que
             * rellenan, 4 escalas y 12 ventanas repartidas por el mundo:
             *
             *   apuntando todos los cruces      67 por fila  -> DESBORDA
             *                                   20 filas descartadas
             *   apuntando solo los que cuentan  33 por fila  -> ni una
             *
             * Es decir: mas de la mitad de la tabla la ocupaban cruces
             * que no pintan nada. La ventana se carga con MEDIA PANTALLA
             * DE MARGEN a cada lado (mapa_ventana), asi que llegan aqui
             * aristas de casillas que no se ven.
             *
             * QUE SE TIRA, Y POR QUE NO CAMBIA NI UN PIXEL:
             *
             * - Por la izquierda, todo lo que cae antes de bx[0], el
             *   borde de la primera casilla. cruces_pinta_sembrado() YA
             *   los tiraba uno a uno al empezar la fila, porque la
             *   semilla de esa casilla ya los cuenta. Tirarlos aqui es
             *   el mismo descarte, pero antes de ocupar sitio.
             * - Por la derecha, todo lo que cae despues del borde de la
             *   pantalla. El bucle de pintado para en `der` y no los
             *   llega a mirar nunca.
             *
             * EN EL CAMINO DE LOS ANILLOS (zoom 1) NO SE RECORTA, y no es
             * un olvido: alli cruces_pinta() dibuja cada tramo tambien a
             * un mundo a cada lado, asi que un cruce de fuera de la
             * pantalla si pinta. Por eso los topes van al int16_t entero.
             */
            int16_t  bx[MAPA_SEM_COLS];
            uint8_t  ncol = 0U;
            int16_t  cx0 = 0;
            int16_t  rec_izq = (int16_t)-32768;
            int16_t  rec_der = (int16_t)32767;

            if (sembrado) {
                ncol = casillas_bordes(c, bx, &cx0);
                if (ncol != 0U) {
                    rec_izq = bx[0];
                    rec_der = (int16_t)(c->x + c->w);
                }
            }

            if (sembrado && ncol == 0U) { cuantas = 0U; }

            cruces_limpia();
            mapa_bin_primero(&ir);
            for (tz = 0U; tz < cuantas; tz++) {
                int16_t ax = 0, ay = 0;
                int32_t alon = 0;
                int16_t k = 0;

                if (!mapa_bin_trazo(&ir)) { break; }
                if (por_casilla)   { if (!ir.relleno) { continue; } }
                else if (sembrado) { if (ir.frontera) { continue; } }
                else if (!ir.anillo) { continue; }

                /*
                 * EL ANTIMERIDIANO NO SE SALTA: SE DESENROLLA - 14/10/2026.
                 *
                 * La primera version copiaba la guarda del dibujado de la
                 * costa -saltarse la arista cuyo salto de longitud pasa de
                 * 180 grados- y el resultado fue que NORTEAMERICA Y
                 * GROENLANDIA SE QUEDABAN SIN RELLENAR mientras Africa,
                 * Eurasia y Suramerica si. Se vio en la foto del banco.
                 *
                 * El motivo: el anillo de Norteamerica incluye las
                 * Aleutianas, que cruzan el antimeridiano. Saltarse UNA
                 * arista deja impar el numero de cruces de todas las filas
                 * que esa arista tocaba, y con par-impar eso no pinta "un
                 * poco mal": no pinta nada en todo el continente.
                 *
                 * Para una LINEA, saltarsela es correcto -en pantalla seria
                 * un trazo de borde a borde que no existe-. Para un
                 * RELLENO, no: el relleno necesita el anillo entero.
                 *
                 * Asi que se desenrolla: cada punto se trae al lado del
                 * anterior sumando o restando una vuelta. El anillo deja de
                 * dar la vuelta al mundo y se vuelve un trozo continuo, con
                 * longitudes que pueden salirse de +-180. Eso esta bien
                 * aqui, porque de este bucle solo salen numeros de pixel; y
                 * de que el resultado se vea en el sitio correcto se encarga
                 * cruces_pinta(), que dibuja cada tramo tambien a un mundo a
                 * cada lado.
                 */
                int32_t vuelta = 0;

                for (;; k++) {
                    int32_t lon, lat;
                    int16_t bxp, byy;

                    if (!mapa_bin_punto(&ir, &lon, &lat)) { break; }
                    if (k > 0) {
                        int32_t d = lon + vuelta - alon;

                        if (d >  LON_C_MAX) { vuelta -= 2L * LON_C_MAX; }
                        else if (d < -LON_C_MAX) { vuelta += 2L * LON_C_MAX; }
                    }
                    bxp = px_de_lon(c, lon + vuelta);
                    byy = py_de_lat(c, lat);
                    if (k > 0) {
                        cruces_arista(ax, ay, bxp, byy, by0, bh,
                                      rec_izq, rec_der);
                    }
                    ax = bxp; ay = byy; alon = lon + vuelta;
                }
            }
            if (!sembrado) {
                /* Camino 1 y camino 2: los dos tienen anillos cerrados y
                 * el par-impar se arranca solo desde fuera. */
                cruces_pinta(s, c, by0, bh, gfx2_rgb(t.tierra));
            } else {
                /*
                 * LAS SEMILLAS DE ESTA BANDA. Van en la PILA y no en
                 * .bss: son cuarenta y pico bytes y en esta radio quedan
                 * 72 libres en toda la SRAM, asi que el sitio barato es
                 * el que se devuelve al salir.
                 *
                 * Los bordes de casilla -bx[], ncol y cx0- ya
                 * estan hechos: se calculan ARRIBA, antes de apuntar los
                 * cruces, porque bx[0] es el limite del recorte.
                 */
                uint8_t  sem[GFX2_BAND_H];
                uint8_t  crudo[GFX2_BAND_H + 2U];
                uint8_t  pbuf[MAPA_SEM_BUF];
                uint16_t ult_cas = 0xFFFFU, ult_b0 = 0xFFFFU;
                uint8_t  nx = mapa_bin_nx(), ny = mapa_bin_ny();
                uint8_t  ci;

                if (ncol != 0U) {
                    uint8_t f;

                    /*
                     * UNA FILA SOLA NO DECIDE - 16/10/2026. ESTE ES EL
                     * ARREGLO DE LAS RAYAS AL ACERCARSE.
                     *
                     * La tabla de paridad se muestrea en latitudes fijas,
                     * un bit cada 1/16 de grado, y la pantalla pinta otras
                     * -las que caen donde caen segun el zoom y el
                     * arrastre-. Donde una costa va casi horizontal, la
                     * muestra de la tabla y la fila de la pantalla acaban
                     * a distinto lado de ella y la tabla contesta lo
                     * contrario de lo que se ve.
                     *
                     * Y la semilla no es un pixel: ARRANCA EL PAR-IMPAR DE
                     * LA FILA. Una semilla al reves pinta la fila entera
                     * al reves, de punta a punta de la pantalla. Eso es la
                     * raya horizontal, y por eso aparecia justo al pasar
                     * al primer nivel troceado -el unico que siembra- y no
                     * antes.
                     *
                     * Asi que se leen bh+2 filas -una de margen arriba y
                     * otra abajo- y cada fila se queda con LA MAYORIA DE
                     * ELLA Y SUS DOS VECINAS, columna a columna. Una fila
                     * que discrepa de las dos de al lado es ruido de
                     * muestreo, no una costa: una costa de verdad la ven
                     * tambien las vecinas.
                     *
                     * Medido con sim/mapa_rayas.c, el mapa 036 centrado
                     * en Europa:
                     *
                     *      sin filtrar      33 rayas al x4, 10 al x8
                     *      mayoria de 3     18 al x4,        3 al x8
                     *
                     * ESTO NO LAS QUITA TODAS, Y HAY QUE DECIRLO. Las que
                     * quedan no son ruido de muestreo: son que el
                     * par-impar sobre las costas de un nivel TROCEADO no
                     * cierra, porque las piezas van cortadas por las
                     * casillas y sus extremos dejan cruces sueltos. Eso
                     * se arregla en el fichero -que cada casilla lleve
                     * sus anillos cerrados contra su propio borde-, no
                     * aqui. Mientras tanto, esto se queda porque baja de
                     * 33 a 18 y no cuesta nada.
                     *
                     * Tambien se probo, y se tiro porque MEDIA PEOR:
                     *
                     *   votar tres bits de la tabla ademas de las filas
                     *                           19 al x4 (contra 18)
                     *   sembrar una sola vez y no en cada casilla
                     *                           32 al x4
                     *   tirar los cruces que caen en el borde de casilla
                     *                           24 al x4
                     *   juntar los cruces repetidos en el mismo pixel
                     *                          117 al x4
                     *   sacar la tabla de paridad de la geometria del
                     *   propio nivel, en el empaquetador
                     *                           21 al x4
                     *
                     * Y lo que si sale a 0 en los cuatro zooms es
                     * rellenar con el NIVEL ENTERO, que no tiene cortes.
                     * No cabe: el x1 mide 9.372 B, el buffer del mapa
                     * 10.240, y en el chip de 512 k quedan 32 bytes
                     * libres en toda la SRAM.
                     *
                     * LO QUE SE PIERDE CON LA MAYORIA: una franja de
                     * tierra de una sola fila de alto, y solo para
                     * ARRANCAR la fila en el borde de la casilla. Dentro
                     * de la casilla la siguen dibujando sus propias
                     * costas.
                     *
                     * LAS DOS DE MARGEN SE LEEN Y NO SE PINTAN: hacen
                     * falta para que la primera y la ultima fila de la
                     * banda tengan vecinas. Sin ellas la raya volveria,
                     * pero solo en los bordes de banda, que es peor:
                     * aparece cada dieciseis filas y parece otra cosa.
                     */
                    for (f = 0U; f < (uint8_t)bh + 2U; f++) {
                        int32_t lat = lat_de_py(c, (int16_t)(by0 + f - 1));
                        int32_t alto_cas = (2L * 9000L) / (int32_t)ny;
                        int32_t cy = (9000L - lat) / alto_cas;
                        int32_t arriba;
                        uint16_t bit;
                        uint8_t  m = 0U;

                        if (cy < 0) { cy = 0; }
                        if (cy > (int32_t)(ny - 1U)) { cy = (int32_t)(ny - 1U); }
                        arriba = 9000L - cy * alto_cas;
                        bit = (uint16_t)(((arriba - lat)
                                          * (int32_t)mapa_bin_par_bytes() * 8L)
                                         / alto_cas);
                        if (bit >= (uint16_t)(mapa_bin_par_bytes() * 8U)) {
                            bit = (uint16_t)(mapa_bin_par_bytes() * 8U - 1U);
                        }
                        for (ci = 0U; ci < ncol; ci++) {
                            uint16_t cas = (uint16_t)((uint16_t)cy * (uint16_t)nx
                                                      + (uint16_t)(cx0 + (int16_t)ci));
                            if (semilla(cas, bit, pbuf, &ult_cas, &ult_b0)) {
                                m |= (uint8_t)(1U << ci);
                            }
                        }
                        crudo[f] = m;
                    }
                    for (f = 0U; f < (uint8_t)bh; f++) {
                        uint8_t a = crudo[f], b = crudo[f + 1U], d = crudo[f + 2U];

                        /* Mayoria bit a bit de las tres mascaras. */
                        sem[f] = (uint8_t)((a & b) | (b & d) | (a & d));
                    }
                    cruces_pinta_sembrado(s, c, by0, bh, gfx2_rgb(t.tierra),
                                          bx, sem, ncol);
                }
            }
        }
    }

    /*
     * LA REJILLA VA CADA 30 GRADOS y el ecuador va mas claro. No es
     * adorno: sin ninguna referencia, un punto en mitad del Atlantico no
     * se sabe si esta a la altura de Canarias o de Islandia.
     */
    for (i = -180; i <= 180; i += 30) {
        int16_t x = px_de_lon(c, (int32_t)i * 100L);

        if (x < c->x || x >= (int16_t)(c->x + c->w)) { continue; }
        gfx2_vline(s, x, c->y, c->h, gfx2_rgb(t.rejilla));
    }
    for (i = -60; i <= 60; i += 30) {
        int16_t y = py_de_lat(c, (int32_t)i * 100L);

        if (y < c->y || y >= (int16_t)(c->y + c->h)) { continue; }
        gfx2_hline(s, c->x, y, c->w, gfx2_rgb(i == 0 ? t.ecuador : t.rejilla));
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
    /*
     * DE DONDE SALEN LOS TRAZOS - 12/10/2026.
     *
     * Si el usuario ha puesto un .map en el pendrive, de ahi; si no, de la
     * tabla de serie, que es lo de siempre. El bucle es el mismo y la
     * unica diferencia son las dos lineas que sacan el siguiente punto:
     * de la tabla es leerlo, del fichero es leerlo Y SUMARLO al anterior,
     * porque alli van guardados los saltos (ver mapa_bin.h).
     *
     * Dos sumas por punto al lado de la multiplicacion y la division que
     * ya hacen px_de_lon() y py_de_lat() no se notan, y a cambio en los
     * mismos ocho kilobytes de RAM caben un 60 % mas de puntos.
     */
    {
        mapa_bin_iter_t it;
        uint8_t del_fichero = mapa_bin_hay();
        uint16_t cuantas = del_fichero ? mapa_bin_trazos() : k_mapa_n_lineas;

        if (del_fichero) { mapa_bin_primero(&it); }

    for (linea = 0U; linea < cuantas; linea++) {
        int16_t n;
        int16_t k;
        int16_t ax = 0, ay = 0;
        int32_t alon = 0;

        if (del_fichero) {
            if (!mapa_bin_trazo(&it)) { break; }
            /*
             * LOS ANILLOS DE RELLENO NO SE DIBUJAN - 16/10/2026.
             *
             * En los niveles troceados cada casilla lleva, detras de sus
             * costas, los anillos recortados contra su propio rectangulo.
             * Sirven para rellenar y nada mas: los lados que el recorte
             * añade van POR EL BORDE DE LA CASILLA, y pintarlos seria una
             * rejilla de costas que no existe.
             */
            if (it.relleno) { continue; }
            n = 0;                       /* lo marca el iterador */
        } else {
            n = *p++;
        }

        for (k = 0; del_fichero ? 1 : (k < n); k++) {
            int32_t lon, lat;
            int16_t bx;

            if (del_fichero) {
                if (!mapa_bin_punto(&it, &lon, &lat)) { break; }
            } else {
                lon = *p++;
                lat = *p++;
            }
            bx = px_de_lon(c, lon);
            int16_t by = py_de_lat(c, lat);

            if (k > 0) {
                int32_t d = (lon > alon) ? (lon - alon) : (alon - lon);

                /*
                 * Y EL TRAZO QUE NO TOCA ESTA BANDA SE SALTA ENTERO.
                 * 13/10/2026.
                 *
                 * gfx2 compone en bandas de 16 filas, asi que esta
                 * funcion corre DIECIOCHO VECES por fotograma con el mapa
                 * de FT8 (288 px de alto), y hasta aqui recorria la costa
                 * entera las dieciocho. Medido con la tabla de serie
                 * (1.762 puntos, 106 trazos), un fotograma:
                 *
                 *   llamadas a gfx2_line()        24.822
                 *   las que de verdad pintan       1.556   = 6,3 %
                 *
                 * O sea que el 93,7 % entraba en recorta() -un
                 * Cohen-Sutherland con hasta cuatro divisiones ENTERAS de
                 * divisor variable- y en gfx2_line() para darse la vuelta
                 * sin escribir un pixel.
                 *
                 * Dos comparaciones lo evitan, y son EXACTAS: un segmento
                 * con los dos extremos por encima de la banda, o los dos
                 * por debajo, no puede cruzarla. No es una heuristica ni
                 * un recorte aproximado; es la misma respuesta mas
                 * barata.
                 */
                int16_t b0 = s->y, b1 = (int16_t)(s->y + s->h);
                uint8_t fuera = (uint8_t)((ay < b0 && by < b0)
                                       || (ay >= b1 && by >= b1));

                if (d < LON_C_MAX && !fuera) {
                    /* Recortado a la caja, no a la banda: ver recorta(). */
                    int32_t rx0 = ax, ry0 = ay, rx1 = bx, ry1 = by;

                    if (recorta(c, &rx0, &ry0, &rx1, &ry1)) {
                        gfx2_line(s, (int16_t)rx0, (int16_t)ry0,
                                     (int16_t)rx1, (int16_t)ry1,
                                  gfx2_rgb(del_fichero && it.frontera
                                           ? t.frontera : t.costa));
                    }
                }
            }
            ax = bx; ay = by; alon = lon;
        }
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
/*
 * EL MENOS A LA IZQUIERDA Y EL MAS A LA DERECHA - 30/09/2026, por el usuario.
 *
 * Estaba al reves. Es el orden de todo lo demas de esta radio -ver los dos
 * botones grandes de ui_det.c, y los de la pantalla de QTH- y el de
 * cualquier escala: lo que resta a la izquierda, lo que suma a la derecha.
 *
 * Esta funcion la usan LAS DOS: mz_uno() para dibujar y mapa_zoom_hit()
 * para el toque. Por eso el cambio va aqui y en un solo sitio: con las
 * posiciones escritas dos veces, invertir el dibujo y dejarse el toque deja
 * unos botones que hacen lo contrario de lo que ponen, y eso no lo ve el
 * compilador.
 */
static int16_t mz_x(const mapa_caja_t *c, uint8_t mas)
{
    return (int16_t)(c->x + MZ_MARGEN
                     + (mas ? (MAPA_MZ_LADO + MZ_MARGEN) : 0));
}

static void mz_uno(gfx2_surf_t *s, const mapa_caja_t *c, uint8_t mas, uint8_t vale)
{
    int16_t x = mz_x(c, mas), y = mz_y(c);
    mapa_tinte_t t;
    uint32_t tinta;
    int16_t cx = (int16_t)(x + MAPA_MZ_LADO / 2);
    int16_t cy = (int16_t)(y + MAPA_MZ_LADO / 2);

    mapa_tinte(&t);
    tinta = vale ? t.tinta : t.tinta_off;

    gfx2_rrect(s, x, y, MAPA_MZ_LADO, MAPA_MZ_LADO, 8, gfx2_rgba(t.boton, 215U));
    gfx2_rrect_outline(s, x, y, MAPA_MZ_LADO, MAPA_MZ_LADO, 8, 1,
                       gfx2_rgb(t.borde));
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
    mapa_tinte_t t;

    if (px < c->x || px >= (int16_t)(c->x + c->w)) { return; }
    if (py < c->y || py >= (int16_t)(c->y + c->h)) { return; }

    mapa_tinte(&t);

    /* Un aspa, que no se confunde con ninguna marca de estacion. */
    gfx2_line(s, (int16_t)(px - 4), (int16_t)(py - 4),
                 (int16_t)(px + 4), (int16_t)(py + 4), gfx2_rgb(t.casa));
    gfx2_line(s, (int16_t)(px - 4), (int16_t)(py + 4),
                 (int16_t)(px + 4), (int16_t)(py - 4), gfx2_rgb(t.casa));
}
