#include "mapa_eti.h"
#include "mapa_bin.h"
#include "dxcc.h"
#include "idioma.h"
#include "font_ui_14.h"
#include <math.h>

/* ---------------------------------------------------------------------
 * EL ESTADO, Y LO QUE OCUPA
 * ---------------------------------------------------------------------
 *
 *   la lista de etiquetas   MAPA_ETI_MAX x 24 B   =  288 B
 *   los arcos en pixeles    6 x 17 x 4 B          =  408 B
 *   el elegido y su ficha                         =   32 B
 *
 * No hay copia de los arcos en grados: se recalculan enteros en cada
 * repintado -que ocurre cuando algo cambia, no treinta veces por segundo-
 * y asi la cuenta de la esfera esta en un sitio. Ver mapa_eti.h.
 */

typedef struct {
    char     call[MAPA_ETI_CALL];
    char     loc[MAPA_ETI_LOC];
    /*
     * La posicion en centesimas de grado, GUARDADA y no vuelta a sacar del
     * localizador - 13/10/2026.
     *
     * Antes la ficha la recalculaba con mapa_loc_a_cent(e->loc) cada vez
     * que se tocaba una etiqueta. Eso valia mientras todo lo que entraba
     * aqui tuviera localizador; un avion no lo tiene. Guardarla cuesta
     * ocho bytes por entrada -96 en total- y ademas le quita una
     * conversion a la ficha.
     */
    int32_t  lon_c, lat_c;
    int16_t  px, py;          /* la marca de la estacion */
    int16_t  ex, ey, ew;      /* la caja del texto: esquina y ancho */
    int16_t  snr;
    uint16_t minuto;
    uint8_t  veces;           /* cuantos mensajes suyos hay en la lista */
    uint8_t  elegida;
    uint8_t  avion;           /* ver mapa_eti_dx_t */
    /*
     * LO QUE SE SABE ADEMAS DEL NOMBRE - 19/10/2026. En un avion, su OACI
     * y su modelo. Ver mapa_eti_dx_t::extra.
     *
     * SE GUARDA EN LA ENTRADA Y NO SE DEJA UN PUNTERO, aunque el puntero
     * costaria 48 bytes en vez de 384. Dos razones:
     *
     *   - el buffer de quien llama se reutiliza en cada vuelta de su
     *     bucle, asi que el puntero apuntaria al ultimo avion de la lista
     *     para todas las etiquetas;
     *   - y los datos de la ficha tienen que vivir en la entrada, que es
     *     justo lo que se arreglo el 13/10 (ver el comentario de
     *     sel_copia()): si no, la ficha sale bien o mal segun el orden en
     *     que el que llama haga las cosas.
     */
    char     extra[MAPA_ETI_EXTRA];
} eti_t;

static mapa_caja_t s_caja;
static uint8_t     s_hay_caja;

static int32_t s_qth_lon, s_qth_lat;   /* centesimas de grado */
static uint8_t s_hay_qth;

static eti_t   s_eti[MAPA_ETI_MAX];
static uint8_t s_n;
static uint8_t s_tope;

static int16_t s_arco[MAPA_ETI_RAYOS][MAPA_ETI_PTS][2];
static uint8_t s_arco_pts[MAPA_ETI_RAYOS];
static uint8_t s_arcos;

/* El elegido. Se guarda ENTERO y no un indice: ver mapa_eti.h. */
static char     s_sel[MAPA_ETI_CALL];
static char     s_sel_loc[MAPA_ETI_LOC];
static int16_t  s_sel_snr;
static uint16_t s_sel_min;
static uint16_t s_sel_km;
static int16_t  s_sel_az;
static uint8_t  s_sel_veces;
/* 1 = el elegido es un avion: ni "DX" delante ni pais. Ver mapa_eti.h. */
static uint8_t  s_sel_avion;
/* Lo que se sabe del elegido ademas de su nombre: en un avion, su OACI y
 * su modelo. Se copia SOLO del elegido - ver mapa_eti_dx_t::extra. Treinta
 * y dos bytes porque el modelo mas largo que genera tools/icao24.py son 23
 * mas los seis de la OACI, el espacio y el cero. */
static char     s_sel_extra[32];
static uint8_t  s_sel_visto;    /* lo han vuelto a ofrecer en este fotograma */

#define ETI_SEP     6     /* del punto al texto */
#define ETI_PAD     3     /* del texto al borde de su chapa */
#define ETI_ALTO    16
#define ETI_FICHA_H 40
#define ETI_MARGEN  8

/* ------------------------------------------------------------------ */
/* Cadenas, sin sprintf: la misma politica que settings.c y main.c.    */
/* ------------------------------------------------------------------ */
/*
 * UNA LETRA, Y SOLO SI CABE.
 *
 * El mismo tope que pon_txt(): se para en tope-1 para que siempre quede
 * sitio para el '\0'. Existe porque los sitios donde se escribia un
 * caracter suelto -el signo del dB, la x de las veces- lo hacian con un
 * b[i++] a pelo, sin mirar nada.
 */
static uint8_t pon_car(char *b, uint8_t i, uint8_t tope, char c)
{
    if (i < (uint8_t)(tope - 1U)) { b[i++] = c; }
    return i;
}

/*
 * EL TOPE TAMBIEN AQUI - 16/10/2026, Y ESTO SE SALIA DE LA PILA.
 *
 * pon_txt() y pon_punto() reciben `tope` y lo respetan; pon_u32() no lo
 * recibia siquiera, asi que escribia sus digitos donde cayeran. Como la
 * ficha arma la cadena a trozos -indicativo, localizador, pais,
 * kilometros, azimut, dB, veces, "hace N min"- y los numeros van al
 * final, bastaba una ficha larga para que los ultimos digitos cayeran
 * FUERA del char buf[80] de mapa_eti_ficha(), que vive en la pila.
 *
 * Medido con AddressSanitizer sobre el banco: con ZL4AA en RE78, SNR -25,
 * x12 y hace 600 minutos, se escribian TRES BYTES detras de buf. En la
 * pila de esa funcion, detras de buf, estan `t`, `x`, `y` y `w`.
 *
 * El banco no lo veia porque sim/mapa_eti.c declaraba su buffer de 96 y
 * no de 80: estaba probando con mas sitio del que tiene la radio. Ya son
 * los dos de 80.
 */
static uint8_t pon_u32(char *b, uint8_t i, uint8_t tope, uint32_t v)
{
    char tmp[12];
    uint8_t n = 0U;

    if (v == 0UL) { return pon_car(b, i, tope, '0'); }
    while (v > 0UL && n < (uint8_t)sizeof tmp) {
        tmp[n++] = (char)('0' + (char)(v % 10UL));
        v /= 10UL;
    }
    while (n > 0U) { i = pon_car(b, i, tope, tmp[--n]); }
    return i;
}

static uint8_t pon_txt(char *b, uint8_t i, uint8_t tope, const char *t)
{
    while (*t != '\0' && i < (uint8_t)(tope - 1U)) { b[i++] = *t++; }
    return i;
}

static uint8_t pon_punto(char *b, uint8_t i, uint8_t tope)
{
    /* " · ", el mismo separador que usan las filas de Informacion. */
    i = pon_txt(b, i, tope, " \xC2\xB7 ");
    return i;
}

static uint8_t mismo(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) { return 0U; }
        a++; b++;
    }
    return (uint8_t)(*a == *b);
}

static void copia(char *d, const char *o, uint8_t tope)
{
    uint8_t i = 0U;

    if (o != 0) {
        while (o[i] != '\0' && i < (uint8_t)(tope - 1U)) { d[i] = o[i]; i++; }
    }
    d[i] = '\0';
}

/* ------------------------------------------------------------------ */
/*
 * EL CONTRASTE, Y POR QUE SE MIRA EN CALIENTE.
 *
 * El color del rotulo lo trae el .map, y el packer solo comprueba UN par:
 * que la marca se vea sobre el mar. Nadie ha comprobado nunca que el
 * rotulo se vea sobre la chapa, y entre los cien tipos hay alguno con el
 * rotulo casi del mismo tono que el fondo del boton -el 027 lo tiene en
 * 0E3020 sobre un mar 020300-. Un indicativo que no se lee no es un
 * indicativo.
 *
 * Asi que aqui se mide: si la diferencia de luminancia no llega al mismo
 * CONTRASTE_MINIMO que ya usa el packer para la marca, se pinta con la
 * tinta de siempre, que por construccion si se ve sobre la chapa -es la
 * que llevan los dos mandos del zoom-. Es una linea y evita entregar cien
 * mapas de los que unos cuantos no enseñan nada.
 */
#define ETI_CONTRASTE 48L

static int32_t luz(uint32_t c)
{
    return (299L * (int32_t)((c >> 16) & 0xFFUL)
          + 587L * (int32_t)((c >> 8) & 0xFFUL)
          + 114L * (int32_t)(c & 0xFFUL)) / 1000L;
}

static uint32_t tinta_legible(const mapa_tinte_t *t)
{
    int32_t d = luz(t->rotulo) - luz(t->boton);

    if (d < 0L) { d = -d; }
    return (d >= ETI_CONTRASTE) ? t->rotulo : t->tinta;
}

/* ------------------------------------------------------------------ */
uint8_t mapa_eti_tope(const mapa_caja_t *c)
{
    uint32_t e = (c != 0 && c->esc > 0U) ? (uint32_t)c->esc : 1UL;
    uint32_t n = 4UL + e;

    if (n > (uint32_t)MAPA_ETI_MAX) { n = (uint32_t)MAPA_ETI_MAX; }
    return (uint8_t)n;
}

void mapa_eti_empieza(const mapa_caja_t *c, const char *qth)
{
    s_n = 0U;
    s_arcos = 0U;
    s_sel_visto = 0U;
    s_hay_caja = 0U;
    s_hay_qth = 0U;

    if (c == 0) { return; }
    s_caja = *c;
    s_hay_caja = 1U;
    s_tope = mapa_eti_tope(c);

    if (qth != 0 && qth[0] != '\0'
        && mapa_loc_a_cent(qth, &s_qth_lon, &s_qth_lat)) {
        s_hay_qth = 1U;
    }
}

uint8_t mapa_eti_n(void) { return s_n; }

/* ------------------------------------------------------------------ */
/*
 * EL ARCO: la ortodromica partida en MAPA_ETI_PTS puntos.
 *
 * Interpolacion esferica (slerp) entre los dos vectores unitarios. No se
 * interpolan longitud y latitud por separado, que es el error clasico y
 * da precisamente la raya recta del mapa plano -el camino equivocado-.
 *
 * DIECISEIS TRAMOS, y el numero sale de una cuenta: el tramo mas largo
 * posible es media vuelta al mundo entre dieciseis, o sea 1.250 km, y la
 * flecha de esa cuerda contra su arco son 6.371*(1-cos(0,098)) = 30 km.
 * En un mundo de 800 px eso es 0,6 pixeles a escala 1 y 4,8 a escala 8;
 * con MAPA_ETI_PTS a 17 queda en 0,15 y 1,2. Por debajo del grosor de la
 * raya, que es lo que hay que pedirle.
 */
static void arco_calcula(uint8_t slot, int32_t lon_c, int32_t lat_c)
{
    const float g2r = 3.14159265358979323846f / 180.0f;
    float la0 = (float)s_qth_lat * 0.01f * g2r;
    float lo0 = (float)s_qth_lon * 0.01f * g2r;
    float la1 = (float)lat_c * 0.01f * g2r;
    float lo1 = (float)lon_c * 0.01f * g2r;
    float x0 = cosf(la0) * cosf(lo0), y0 = cosf(la0) * sinf(lo0), z0 = sinf(la0);
    float x1 = cosf(la1) * cosf(lo1), y1 = cosf(la1) * sinf(lo1), z1 = sinf(la1);
    float cosd = x0 * x1 + y0 * y1 + z0 * z1;
    float d, sind;
    uint8_t k, n = 0U;

    if (cosd >  1.0f) { cosd =  1.0f; }
    if (cosd < -1.0f) { cosd = -1.0f; }
    /*
     * CON atan2f Y NO CON acosf - 26/10/2026, haciendo hueco en la flash
     * interna. acosf era la unica llamada de todo el firmware y el
     * enlazador metia __ieee754_acosf entero por ella: 580 bytes. atan2f
     * y sqrtf ya estaban -y sqrtf en un Cortex-M4 con FPU es UNA
     * instruccion-, asi que esto sale gratis.
     *
     * Y de paso se ahorra el sinf: el arco va de 0 a pi, donde el seno no
     * es negativo, asi que sin(d) es exactamente la raiz de 1-cos^2 que
     * ya hay que calcular para el atan2. Cerca de los extremos -dos
     * puntos casi iguales, o antipodas- esta forma es ademas mas estable
     * que acosf, que es donde acosf pierde cifras.
     */
    sind = sqrtf(1.0f - cosd * cosd);
    d = atan2f(sind, cosd);

    for (k = 0U; k < (uint8_t)MAPA_ETI_PTS; k++) {
        float f = (float)k / (float)(MAPA_ETI_PTS - 1U);
        float a, b, x, y, z, la, lo;
        int16_t px, py;

        if (sind < 1e-6f) {
            /* La misma casilla, o la antipoda exacta: no hay arco unico y
             * tampoco hay nada que enseñar. Se deja en el QTH. */
            a = 1.0f - f; b = f;
        } else {
            a = sinf((1.0f - f) * d) / sind;
            b = sinf(f * d) / sind;
        }
        x = a * x0 + b * x1;
        y = a * y0 + b * y1;
        z = a * z0 + b * z1;
        la = atan2f(z, sqrtf(x * x + y * y)) / g2r;
        lo = atan2f(y, x) / g2r;

        if (!mapa_grados_a_px(&s_caja, lo, la, &px, &py)) {
            /*
             * Fuera del recorte de latitud de la tabla (+-71,5 grados): el
             * arco se PARTE ahi y se reanuda cuando vuelve a entrar. No se
             * pega al borde -una raya horizontal pegada al marco no dice
             * por donde va el salto, dice que el dibujo se ha roto- y no
             * se corta entero, porque un salto que pasa por encima del
             * circulo polar sigue existiendo al otro lado.
             *
             * El hueco se marca con MAPA_ETI_FUERA y lo salta el dibujo.
             */
            px = MAPA_ETI_FUERA;
            py = MAPA_ETI_FUERA;
        }
        s_arco[slot][n][0] = px;
        s_arco[slot][n][1] = py;
        n++;
    }
    s_arco_pts[slot] = n;
}

/* ------------------------------------------------------------------ */
static uint8_t choca(int16_t ax, int16_t ay, int16_t aw,
                     int16_t bx, int16_t by, int16_t bw)
{
    if (ax + aw <= bx || bx + bw <= ax) { return 0U; }
    if (ay + ETI_ALTO <= by || by + ETI_ALTO <= ay) { return 0U; }
    return 1U;
}

/*
 * LOS DATOS DE LA FICHA SALEN DE LA ENTRADA ELEGIDA, Y SE COPIAN EN DOS
 * SITIOS: al tocar y al cerrar la lista.
 *
 * *** La primera version solo los rellenaba al METER una estacion que ya
 * estuviera elegida, y sim/mapa_eti.c se puso rojo en la primera pasada:
 * la ficha salia "DX JA5AFW · +0 dB · hace 600 min", sin localizador, sin
 * kilometros y sin azimut. ***
 *
 * Y en la radio NO se habria visto, porque alli el toque llama a
 * ft8_mapa_draw(), que vuelve a rellenar la lista entera y entonces si
 * entraban los datos. O sea que funcionaba por el orden en que el que
 * llama hace las cosas, que es la clase de acuerdo que nadie escribe y
 * que se rompe el dia que alguien repinta de otra manera.
 *
 * Ahora los datos viven en la entrada -el modulo ya los tenia que
 * guardar- y esto solo los copia. El que llama puede tocar y pintar en el
 * orden que quiera.
 */
static void sel_copia(const eti_t *e)
{
    int32_t lon_c = e->lon_c, lat_c = e->lat_c;

    copia(s_sel, e->call, (uint8_t)MAPA_ETI_CALL);
    copia(s_sel_loc, e->loc, (uint8_t)MAPA_ETI_LOC);
    s_sel_snr = e->snr;
    s_sel_min = e->minuto;
    s_sel_veces = e->veces;
    s_sel_avion = e->avion;
    copia(s_sel_extra, e->extra, (uint8_t)sizeof s_sel_extra);
    s_sel_km = 0U;
    s_sel_az = -1;

    if (!s_hay_qth) { return; }
    {
        const float g2r = 3.14159265358979323846f / 180.0f;
        float la0 = (float)s_qth_lat * 0.01f * g2r;
        float lo0 = (float)s_qth_lon * 0.01f * g2r;
        float la1 = (float)lat_c * 0.01f * g2r;
        float lo1 = (float)lon_c * 0.01f * g2r;
        float dla = la1 - la0, dlo = lo1 - lo0;
        float a = sinf(dla * 0.5f) * sinf(dla * 0.5f)
                + cosf(la0) * cosf(la1) * sinf(dlo * 0.5f) * sinf(dlo * 0.5f);
        float km, az;

        /* El recorte de `a` es el mismo que ya hubo que poner en
         * ft8_decoder.c: en float sale por los dos bordes justo en el QTH
         * propio y en su antipoda, y sqrtf(1-a) daba NaN. */
        if (a < 0.0f) { a = 0.0f; }
        if (a > 1.0f) { a = 1.0f; }
        km = 6371.0f * 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
        s_sel_km = (uint16_t)((km > 65535.0f) ? 65535.0f : (km + 0.5f));

        az = atan2f(sinf(dlo) * cosf(la1),
                    cosf(la0) * sinf(la1) - sinf(la0) * cosf(la1) * cosf(dlo))
             / g2r;
        if (az < 0.0f) { az += 360.0f; }
        s_sel_az = (int16_t)(az + 0.5f);
        if (s_sel_az >= 360) { s_sel_az = 0; }
    }
}

uint8_t mapa_eti_pon(const mapa_eti_dx_t *dx)
{
    int16_t px, py, ew, ex, ey;
    int32_t lon_c, lat_c;
    uint8_t i, elegida;

    if (!s_hay_caja || dx == 0 || dx->call[0] == '\0') { return 0U; }

    elegida = (uint8_t)(s_sel[0] != '\0' && mismo(dx->call, s_sel));

    /*
     * LA MISMA ESTACION NO SE PONE DOS VECES: se cuenta. En FT8 el que
     * llama repite su CQ cada minuto, asi que el mismo indicativo puede
     * estar seis veces en la lista de dieciseis. Y la primera vez que se
     * ofrece es la MAS NUEVA, que es la que manda para el dB y la hora.
     */
    for (i = 0U; i < s_n; i++) {
        if (mismo(s_eti[i].call, dx->call)) {
            if (s_eti[i].veces < 99U) { s_eti[i].veces++; }
            return 0U;
        }
    }

    /*
     * DE DONDE SALE LA POSICION - 13/10/2026.
     *
     * De las coordenadas si vienen -los aviones de HFDL las mandan- y del
     * localizador si no, que es lo de siempre. mapa_e4_a_px() quiere
     * diezmilesimas de grado y aqui se manejan centesimas, de ahi el x100;
     * el mayor es 18.000 x 100 = 1.800.000, que cabe de sobra en int32.
     */
    if (dx->hay_pos) {
        lon_c = dx->lon_c;
        lat_c = dx->lat_c;
    } else if (!mapa_loc_a_cent(dx->loc, &lon_c, &lat_c)) {
        return 0U;
    }
    if (!mapa_e4_a_px(&s_caja, lon_c * 100L, lat_c * 100L, &px, &py)) {
        return 0U;
    }

    /* Lo que no se ve no se etiqueta. El recorte es el de la caja, no el
     * de la pantalla: el mapa vive dentro de su hueco. */
    if (px < s_caja.x || px >= (int16_t)(s_caja.x + s_caja.w)) { return 0U; }
    if (py < s_caja.y || py >= (int16_t)(s_caja.y + s_caja.h)) { return 0U; }

    ew = (int16_t)(gfx2_text_w(dx->call, &font_ui_14) + 2 * ETI_PAD);

    /*
     * SEIS SITIOS ANTES DE RENDIRSE - 13/10/2026.
     *
     * En la lista estaban M7BSI (IO92), EA4AXM (IN80) y G8KHF (IO92). Dos
     * de los tres comparten LOCALIZADOR, o sea el mismo pixel. Aqui se
     * probaba UN sitio -a la derecha del punto, centrado- y, si chocaba,
     * el que llegaba tarde se callaba ENTERO: sin chapa, sin punto y sin
     * rayo. Desaparecia de la pantalla una estacion que la radio habia
     * decodificado.
     *
     * Y no es un caso raro: un cuadrado Maidenhead son dos grados de
     * longitud por uno de latitud, mas de cien kilometros de lado. Dos
     * estaciones inglesas en IO92 un domingo por la manana es lo normal.
     *
     * Ahora se prueban seis colocaciones en orden -derecha e izquierda, a
     * la altura del punto y un renglon arriba y otro abajo- y solo se
     * rinde si las seis chocan. Con eso, dos en el mismo cuadrado salen
     * las dos y cuatro salen al menos tres.
     *
     * EL ORDEN IMPORTA: la derecha a la altura del punto es la de
     * siempre, asi que una etiqueta sola no se mueve ni un pixel de donde
     * estaba. Lo que cambia es lo que pasa cuando no cabe.
     */
    {
        static const struct { int8_t lado; int8_t fila; } k_sitios[] = {
            { +1,  0 }, { -1,  0 },
            { +1, -1 }, { -1, -1 },
            { +1, +1 }, { -1, +1 }
        };
        uint8_t k, vale = 0U;

        if (!elegida && s_n >= s_tope) { return 0U; }
        if (s_n >= (uint8_t)MAPA_ETI_MAX) { return 0U; }

        for (k = 0U; k < sizeof k_sitios / sizeof k_sitios[0]; k++) {
            int16_t cx = (k_sitios[k].lado > 0)
                       ? (int16_t)(px + ETI_SEP)
                       : (int16_t)(px - ETI_SEP - ew);
            int16_t cy = (int16_t)(py - ETI_ALTO / 2
                                   + k_sitios[k].fila * (ETI_ALTO + 2));

            /* Fuera de la caja del mapa no vale: el recorte es el del
             * hueco, no el de la pantalla. */
            if (cx < s_caja.x) { continue; }
            if (cx + ew > (int16_t)(s_caja.x + s_caja.w)) { continue; }
            if (cy < s_caja.y) { continue; }
            if (cy + ETI_ALTO > (int16_t)(s_caja.y + s_caja.h)) { continue; }

            /*
             * EL ELEGIDO ENTRA SIEMPRE, y por eso solo el no mira si
             * choca: tocar un indicativo y que desaparezca porque uno
             * nuevo le ha pisado el sitio es la clase de cosa que se vive
             * como que la radio no ha entendido el toque.
             */
            if (!elegida) {
                uint8_t choque = 0U;
                for (i = 0U; i < s_n; i++) {
                    if (choca(cx, cy, ew, s_eti[i].ex, s_eti[i].ey,
                              s_eti[i].ew)) { choque = 1U; break; }
                }
                if (choque) { continue; }
            }
            ex = cx; ey = cy; vale = 1U;
            break;
        }
        if (!vale) { return 0U; }
    }

    copia(s_eti[s_n].call, dx->call, (uint8_t)MAPA_ETI_CALL);
    copia(s_eti[s_n].loc, dx->loc, (uint8_t)MAPA_ETI_LOC);
    s_eti[s_n].lon_c = lon_c;
    s_eti[s_n].lat_c = lat_c;
    s_eti[s_n].avion = dx->avion;
    copia(s_eti[s_n].extra, dx->extra ? dx->extra : "",
          (uint8_t)MAPA_ETI_EXTRA);
    s_eti[s_n].snr = dx->snr;
    s_eti[s_n].minuto = dx->minuto;
    s_eti[s_n].px = px;
    s_eti[s_n].py = py;
    s_eti[s_n].ex = ex;
    s_eti[s_n].ey = ey;
    s_eti[s_n].ew = ew;
    s_eti[s_n].veces = 1U;
    s_eti[s_n].elegida = elegida;
    s_n++;

    if (elegida) { s_sel_visto = 1U; }

    /* El rayo. El elegido se lleva un hueco siempre; los demas, los que
     * queden. */
    if (s_hay_qth && s_arcos < (uint8_t)MAPA_ETI_RAYOS) {
        arco_calcula(s_arcos, lon_c, lat_c);
        s_arcos++;
    }
    return 1U;
}

void mapa_eti_cierra(void)
{
    /*
     * SI EL ELEGIDO YA NO SE OYE, SE SUELTA.
     *
     * La alternativa -dejar la ficha puesta con los datos de hace media
     * hora- es una pantalla que afirma algo que ya no es verdad, y encima
     * sin su etiqueta en el mapa, asi que no se puede ni ver de quien
     * habla. Que la ficha desaparezca dice exactamente lo que pasa: esa
     * estacion ya no esta en la lista.
     */
    uint8_t i;

    if (s_sel[0] == '\0') { return; }
    if (!s_sel_visto) { mapa_eti_suelta(); return; }

    /* Sigue ahi: la ficha se pone al dia -otro dB, otra hora, otro
     * recuento- en vez de quedarse con lo de la primera vez. */
    for (i = 0U; i < s_n; i++) {
        if (mismo(s_eti[i].call, s_sel)) { sel_copia(&s_eti[i]); return; }
    }
}

/* ------------------------------------------------------------------ */
void mapa_eti_dibuja(gfx2_surf_t *s)
{
    mapa_tinte_t t;
    uint32_t tinta;
    uint8_t i, k;

    if (!s_hay_caja || s == 0) { return; }
    mapa_tinte(&t);
    tinta = tinta_legible(&t);

    /*
     * PRIMERO LOS RAYOS Y DESPUES LAS ETIQUETAS: el texto va encima de las
     * rayas y no al reves. Una raya cruzando un indicativo lo hace
     * ilegible, y el indicativo es el dato.
     */
    for (i = 0U; i < s_arcos; i++) {
        for (k = 1U; k < s_arco_pts[i]; k++) {
            int16_t x0 = s_arco[i][k - 1U][0], y0 = s_arco[i][k - 1U][1];
            int16_t x1 = s_arco[i][k][0],      y1 = s_arco[i][k][1];
            int16_t dx;

            /* Un extremo fuera del mapa: ese tramo no existe. */
            if (x0 == MAPA_ETI_FUERA || x1 == MAPA_ETI_FUERA) { continue; }
            dx = (int16_t)(x1 - x0);

            /*
             * EL SALTO DEL ANTIMERIDIANO. Este mapa no da la vuelta por el
             * borde (ver mapa_arrastra), asi que un arco que cruza los 180
             * grados sale proyectado con dos puntos en extremos opuestos de
             * la pantalla. Unirlos pintaria una raya horizontal de lado a
             * lado que no es ningun camino.
             *
             * Se reconoce sin ambiguedad: un tramo de verdad es como mucho
             * un dieciseisavo de una ortodromica, o sea unos 1.250 km, que
             * ni a escala 8 llegan a un cuarto de la pantalla. Medio mundo
             * de ancho solo puede ser el salto.
             */
            if (dx > (int16_t)(s_caja.w / 2) || dx < (int16_t)(-s_caja.w / 2)) {
                continue;
            }
            gfx2_line(s, x0, y0, x1, y1, gfx2_rgba(t.linea, 170U));
        }
    }

    for (i = 0U; i < s_n; i++) {
        const eti_t *e = &s_eti[i];

        gfx2_rrect(s, e->ex, e->ey, e->ew, ETI_ALTO, 4,
                   gfx2_rgba(t.boton, e->elegida ? 240U : 200U));
        if (e->elegida) {
            gfx2_rrect_outline(s, e->ex, e->ey, e->ew, ETI_ALTO, 4, 1,
                               gfx2_rgb(t.casa));
        }
        gfx2_text_in(s, (int16_t)(e->ex + ETI_PAD), (int16_t)(e->ey + 1),
                     (int16_t)(e->ew - 2 * ETI_PAD), e->call, &font_ui_14,
                     gfx2_rgb(e->elegida ? t.casa : tinta), GFX2_ALIGN_L);
    }
}

/* ------------------------------------------------------------------ */
/*
 * EL TEXTO DE LA FICHA, APARTE DEL DIBUJO - y no es manía de separar.
 *
 * Lo que hay que comprobar de esta ficha son los NUMEROS: que los
 * kilometros son los que son, que el azimut apunta a donde apunta, que el
 * "x4" cuenta de verdad y que "hace 3 min" cruza la medianoche sin
 * inventarse un numero negativo. Nada de eso se puede mirar en una
 * pantalla pintada sin ponerse a leer pixeles, y un banco que lee pixeles
 * para comprobar una resta mide el dibujo, no la cuenta.
 *
 * Asi que el texto se arma aqui, se devuelve, y el dibujo lo pinta. El
 * banco llama a esta y compara cadenas. Ver sim/mapa_eti.c.
 */
uint8_t mapa_eti_texto(char *buf, uint8_t tope, uint16_t ahora)
{
    uint8_t i = 0U;

    if (buf == 0 || tope < 8U) { return 0U; }
    buf[0] = '\0';
    if (s_sel[0] == '\0') { return 0U; }

    /*
     * "DX" SOLO PARA LAS ESTACIONES - 13/10/2026. Un avion no es un DX:
     * es un avion. Y por eso tampoco se le busca el pais mas abajo.
     */
    if (!s_sel_avion) { i = pon_txt(buf, i, tope, "DX "); }
    i = pon_txt(buf, i, tope, s_sel);
    if (s_sel_loc[0] != '\0') {
        i = pon_txt(buf, i, tope, " ");
        i = pon_txt(buf, i, tope, s_sel_loc);
    }
    /*
     * EL PAIS, ANTES DE LOS KILOMETROS - 13/10/2026.
     *
     * No se guarda: se busca aqui por el indicativo que ya esta en la
     * ficha. dxcc_pais() mira una tabla en flash y devuelve un puntero a
     * flash, asi que guardarlo en la seleccion costaria cuatro bytes de
     * SRAM para no ahorrar nada.
     *
     * Y si el prefijo no esta en la tabla NO SE PONE NADA: dxcc.h lo dice
     * con todas las letras -un hueco es una respuesta honrada, un pais
     * equivocado no-, y aqui ademas el hueco no se nota porque los puntos
     * medios los pone pon_punto() solo cuando hay algo que separar.
     */
    {
        /*
         * Y NO SE LE BUSCA EL PAIS A UN AVION, que es lo que el flag de
         * arriba evita de verdad. dxcc_pais() mira el PREFIJO, y un
         * distintivo de vuelo no es un indicativo: "UAL123" -United
         * Airlines- empieza por "UA", que en la tabla es Ucrania, y
         * "AFR447" por "AF". Un pais equivocado con cara de dato es peor
         * que ningun pais; eso ya lo dice dxcc.h para los huecos.
         */
        const char *pais = s_sel_avion ? (const char *)0 : dxcc_pais(s_sel);

        if (pais != 0) {
            i = pon_punto(buf, i, tope);
            i = pon_txt(buf, i, tope, pais);
        }
    }
    /*
     * Y LO QUE SE SEPA ADEMAS, detras del nombre y delante de la
     * distancia: en un avion, su direccion OACI y su modelo. Va aqui y no
     * al final porque es QUIEN ES, igual que el pais de una estacion dos
     * bloques mas arriba; los kilometros y el rumbo son donde esta.
     */
    if (s_sel_extra[0] != '\0') {
        i = pon_punto(buf, i, tope);
        i = pon_txt(buf, i, tope, s_sel_extra);
    }
    if (s_sel_km > 0U) {
        i = pon_punto(buf, i, tope);
        i = pon_u32(buf, i, tope, (uint32_t)s_sel_km);
        i = pon_txt(buf, i, tope, " km");
    }
    if (s_sel_az >= 0) {
        i = pon_punto(buf, i, tope);
        i = pon_txt(buf, i, tope, "AZ ");
        i = pon_u32(buf, i, tope, (uint32_t)s_sel_az);
    }
    /*
     * EL dB Y EL "HACE N MIN", SOLO PARA LAS ESTACIONES - 13/10/2026.
     *
     * De un avion de HFDL no se sabe ninguna de las dos cosas: la tabla
     * guarda cuantas veces se le ha oido, no con cuanta senal ni cuando.
     * Escribir "+0 dB · hace 0 min" seria inventarse dos datos y ademas
     * los dos salen creibles, que es lo peor. La ficha de un avion dice
     * lo que de verdad se sabe: quien es, a cuanto esta y en que rumbo.
     */
    if (!s_sel_avion) {
    i = pon_punto(buf, i, tope);
    if (s_sel_snr < 0) {
        i = pon_car(buf, i, tope, '-');
        i = pon_u32(buf, i, tope, (uint32_t)(-s_sel_snr));
    } else {
        i = pon_car(buf, i, tope, '+');
        i = pon_u32(buf, i, tope, (uint32_t)s_sel_snr);
    }
    i = pon_txt(buf, i, tope, " dB");
    }
    if (s_sel_veces > 1U) {
        i = pon_punto(buf, i, tope);
        i = pon_car(buf, i, tope, 'x');
        i = pon_u32(buf, i, tope, (uint32_t)s_sel_veces);
    }
    if (!s_sel_avion) {
        /*
         * HACE CUANTO, y la vuelta del dia se cuenta a mano: los minutos
         * UTC dan la vuelta en 1.440 y a las 00:03 un mensaje de las 23:58
         * no es "de hace menos cinco minutos", es de hace cinco.
         */
        int32_t d = (int32_t)ahora - (int32_t)s_sel_min;

        if (d < 0L) { d += 1440L; }
        if (d > 600L) { d = 600L; }   /* mas de diez horas no es "hace" */
        i = pon_punto(buf, i, tope);
        i = pon_txt(buf, i, tope, tr("hace ", ""));
        i = pon_u32(buf, i, tope, (uint32_t)d);
        i = pon_txt(buf, i, tope, tr(" min", " min ago"));
    }
    /* Y el cierre DENTRO. Con i == tope, buf[i] ya era el byte de
     * detras: el ultimo sitio donde se podia perder lo ganado arriba. */
    if (i > (uint8_t)(tope - 1U)) { i = (uint8_t)(tope - 1U); }
    buf[i] = '\0';
    return i;
}

/*
 * EL HUECO DE LETRA QUE TIENE LA FICHA, calculado por la MISMA cuenta que
 * usa el dibujo y no por una copia. Existe para el banco: desde que la
 * ficha lleva el pais (13/10/2026) el texto puede crecer 14 letras de
 * golpe, y lo que pasa cuando no cabe es que gfx2_text_in() lo recorta por
 * la derecha - o sea que se pierde el "hace N min" y nadie se entera.
 *
 * Devuelve 0 si no hay caja o si la ficha no se dibujaria.
 */
static int16_t ficha_hueco(void)
{
    int16_t x, w;

    if (!s_hay_caja) { return 0; }
    x = (int16_t)(s_caja.x + ETI_MARGEN + 2 * (MAPA_MZ_LADO + ETI_MARGEN));
    w = (int16_t)(s_caja.x + s_caja.w - ETI_MARGEN - x);
    if (w < 80) { return 0; }
    return (int16_t)(w - 20);   /* los 10 px de aire de cada lado */
}

int16_t mapa_eti_ficha_hueco_dbg(void)
{
    return ficha_hueco();
}

void mapa_eti_caja_dbg(uint8_t i, int16_t *x, int16_t *y, int16_t *w)
{
    if (i >= s_n) {
        if (x) { *x = 0; } if (y) { *y = 0; } if (w) { *w = 0; }
        return;
    }
    if (x) { *x = s_eti[i].ex; }
    if (y) { *y = s_eti[i].ey; }
    if (w) { *w = s_eti[i].ew; }
}

const char *mapa_eti_call_dbg(uint8_t i)
{
    return (i < s_n) ? s_eti[i].call : "";
}

void mapa_eti_ficha(gfx2_surf_t *s, uint16_t ahora)
{
    char buf[80];
    mapa_tinte_t t;
    int16_t x, y, w;

    if (!s_hay_caja || s == 0) { return; }

    /*
     * LA GUARDA DE BANDA, QUE FALTABA - 13/10/2026.
     *
     * Esta es la unica cosa del fichero que no la tenia, y es de las caras:
     * mapa_eti_texto() arma la cadena entera -y desde hoy busca el pais,
     * que recorre los 329 prefijos de dxcc.c sin salida anticipada-. Sin
     * esta linea eso se hacia DIECIOCHO veces por fotograma, una por banda
     * de 16 filas, cuando la ficha mide 40 px y toca TRES. Las otras quince
     * armaban el texto, buscaban el pais entre 329 y dibujaban fuera de la
     * banda, o sea en nada.
     *
     * Son ~5.900 comparaciones de prefijo por fotograma para un pais que
     * solo cambia cuando cambias de estacion elegida.
     */
    {
        int16_t fy = (int16_t)(s_caja.y + s_caja.h - ETI_MARGEN - ETI_FICHA_H);

        if (!gfx2_band_hits(s, fy, ETI_FICHA_H)) { return; }
    }

    if (mapa_eti_texto(buf, (uint8_t)sizeof buf, ahora) == 0U) { return; }
    mapa_tinte(&t);

    /* A la derecha de los dos mandos del zoom; ver mapa_eti.h. */
    x = (int16_t)(s_caja.x + ETI_MARGEN + 2 * (MAPA_MZ_LADO + ETI_MARGEN));
    y = (int16_t)(s_caja.y + s_caja.h - ETI_MARGEN - ETI_FICHA_H);
    w = (int16_t)(s_caja.x + s_caja.w - ETI_MARGEN - x);
    if (w < 80) { return; }

    gfx2_rrect(s, x, y, w, ETI_FICHA_H, 8, gfx2_rgba(t.boton, 225U));
    gfx2_rrect_outline(s, x, y, w, ETI_FICHA_H, 8, 1, gfx2_rgb(t.borde));
    gfx2_text_in(s, (int16_t)(x + 10), (int16_t)(y + (ETI_FICHA_H - 14) / 2),
                 (int16_t)(w - 20), buf, &font_ui_14,
                 gfx2_rgb(tinta_legible(&t)), GFX2_ALIGN_L);
}

/* ------------------------------------------------------------------ */
/*
 * EL BLANCO DEL DEDO, Y POR QUE ES TAN GRANDE - 13/10/2026.
 *
 * Era un rectangulo de 24 x 24 alrededor del punto mas la caja del texto,
 * y 24 px es menos de la mitad de lo que este proyecto se exige a si mismo
 * para un blanco de dedo: sim/config.c comprueba que ninguna celda de los
 * ajustes baje de 47 px por ese mismo motivo. Un punto de seis pixeles con
 * un margen de doce es un blanco que hay que acertar mirando.
 *
 * Ahora son 44 x 44 alrededor del punto -casi los 47- y la caja del texto
 * con seis pixeles de aire por lado.
 */
#define ETI_DEDO  22   /* medio lado del cuadrado del punto */
#define ETI_AIRE   6   /* lo que se agranda la caja del texto */

uint8_t mapa_eti_toca(int16_t x, int16_t y)
{
    uint8_t i, mejor = 0xFFU;
    int32_t mejor_d = 0;

    if (!s_hay_caja) { return 0U; }

    /*
     * GANA EL MAS CERCANO, no el primero de la lista.
     *
     * Con los blancos agrandados dos estaciones vecinas se solapan, y
     * quedarse con la primera que contenga el punto significa quedarse con
     * la que mas reciente sea -que es el orden de la lista- y no con la que
     * se ha tocado. Tocas una y se elige la de al lado: eso no se vive como
     * "he fallado", se vive como que la radio no hace caso.
     */
    for (i = 0U; i < s_n; i++) {
        const eti_t *e = &s_eti[i];
        uint8_t dentro = 0U;
        int32_t dx, dy, d;

        if (x >= (int16_t)(e->ex - ETI_AIRE)
            && x < (int16_t)(e->ex + e->ew + ETI_AIRE)
            && y >= (int16_t)(e->ey - ETI_AIRE)
            && y < (int16_t)(e->ey + ETI_ALTO + ETI_AIRE)) {
            dentro = 1U;
        }
        if (!dentro) {
            int16_t ax = (int16_t)(x - e->px), ay = (int16_t)(y - e->py);

            if (ax > -ETI_DEDO && ax < ETI_DEDO
                && ay > -ETI_DEDO && ay < ETI_DEDO) { dentro = 1U; }
        }
        if (!dentro) { continue; }

        /* La distancia se mide al PUNTO, que es donde esta la estacion, y
         * no a su etiqueta: la etiqueta es un rotulo que puede haber
         * quedado a un lado o a otro segun lo que cupiera. */
        dx = (int32_t)x - (int32_t)e->px;
        dy = (int32_t)y - (int32_t)e->py;
        d = dx * dx + dy * dy;
        if (mejor == 0xFFU || d < mejor_d) { mejor = i; mejor_d = d; }
    }

    if (mejor != 0xFFU) {
        if (mismo(s_eti[mejor].call, s_sel)) { return 0U; }   /* ya estaba */
        sel_copia(&s_eti[mejor]);
        return 1U;
    }

    if (s_sel[0] != '\0') { mapa_eti_suelta(); return 1U; }
    return 0U;
}

const char *mapa_eti_sel(void) { return s_sel; }

void mapa_eti_suelta(void)
{
    s_sel[0] = '\0';
    s_sel_loc[0] = '\0';
    s_sel_km = 0U;
    s_sel_az = -1;
    s_sel_veces = 0U;
    s_sel_snr = 0;
    s_sel_min = 0U;
    s_sel_avion = 0U;
    s_sel_extra[0] = '\0';
}
