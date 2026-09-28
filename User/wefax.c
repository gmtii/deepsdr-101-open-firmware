/*
 * WEFAX. Ver wefax.h para el porque de cada pieza.
 */
#include "wefax.h"
#include "disc_fm.h"
#include <string.h>
#include <math.h>

#ifdef __GNUC__
#define TCMRAM_BSS __attribute__((section(".tcmram")))
#else
#define TCMRAM_BSS
#endif

#define PI_F 3.14159265358979f

/*
 * Las cuatro velocidades, EN ORDEN CRECIENTE. El orden importa porque esta
 * lista es tambien la que recorre el ajuste de la interfaz: puesta como
 * estaba -120 primero, para que el indice 0 fuese la normal- el mando iba
 * 120, 60, 90, 240, que no es el orden de nada. Ahora el indice 0 es la mas
 * lenta y la normal es WEFAX_LPM_DEF_I.
 */
static const uint16_t k_lpm[WEFAX_LPM_N] = { 60U, 90U, 120U, 240U };
#define WEFAX_LPM_DEF_I 2U   /* 120 lpm, la normal */

/* Lineas de la señal de fase que se miran antes de decidir la velocidad. */
#define FASE_LINEAS 16U

/* ==========================================================================
 * LA ESCALA DE GRISES
 * ========================================================================== */
uint8_t wefax_gris_de_hz(float hz)
{
    float t = (hz - WEFAX_NEGRO_HZ) / (WEFAX_BLANCO_HZ - WEFAX_NEGRO_HZ);
    if (t <= 0.0f) { return 0U; }
    if (t >= 1.0f) { return 255U; }
    return (uint8_t)(t * 255.0f + 0.5f);
}

float wefax_hz_de_gris(uint8_t gris)
{
    return WEFAX_NEGRO_HZ
         + ((float)gris / 255.0f) * (WEFAX_BLANCO_HZ - WEFAX_NEGRO_HZ);
}

/* ==========================================================================
 * ESTADO
 * ========================================================================== */
static uint8_t s_on;
static float   s_fs;
static uint8_t s_estado;
static uint8_t s_lpm_i = WEFAX_LPM_DEF_I;
static float   s_ppm;

/* --- 1. bajada a banda base y discriminador --- */
/* El discriminador vive en disc_fm.c y lo comparte con el SSTV: son las
 * mismas tres decisiones medidas, y copiadas en dos ficheros solo se
 * corregiria una el dia que hiciera falta. */
static disc_fm_t s_disc;

/* --- 2. reloj de linea --- */
static uint32_t s_fase, s_inc;
static uint32_t s_px_ant;
static float    s_acc;
static uint32_t s_acc_n;

/* --- 3. la linea --- */
/*
 * DOS RENGLONES, no uno.
 *
 * La interrupcion escribe puntos todo el rato y el bucle principal pinta
 * cuando puede. Con un solo renglon, el instante en que se pinta cae en
 * medio de la escritura del siguiente: la mitad de arriba seria de la linea
 * nueva y la de abajo de la vieja. Eso no se ve como un fallo, se ve como
 * ruido - que es la peor forma de tener un fallo.
 *
 * Con dos, la interrupcion cierra uno y sigue por el otro, y el bucle
 * principal se lleva el cerrado entero.
 */
static TCMRAM_BSS uint8_t s_linea[2][WEFAX_ANCHO];
static uint8_t  s_esc;          /* en cual escribe la interrupcion */
static uint8_t  s_linea_lista;
static uint32_t s_lineas;

/* --- 4. señal de fase --- */
static uint8_t  s_fase_n;
static float    s_fase_pos[FASE_LINEAS];
static float    s_fase_bruta;     /* la posicion cruda anterior, para desenvolver */
static float    s_fase_off;       /* vueltas acumuladas, en puntos */
static uint8_t  s_fase_ajustes;   /* cuantos ajustes se han hecho ya */
static uint16_t s_fase_lineas;    /* lineas pasadas en la señal de fase */
static float    s_desliz;      /* puntos que se corre el pulso por linea */
/*
 * LA LINEA PARTIDA - 24/09/2026.
 *
 * Al final de cada pasada se corre el acumulador de linea para traer el
 * pulso a su sitio. Eso parte EN DOS la linea que se estaba acumulando en
 * ese momento: la que sale a continuacion lleva un trozo de una linea y un
 * trozo de la siguiente, y su pulso cae donde le da la gana.
 *
 * Esa linea entraba en el ajuste por minimos cuadrados de la pasada
 * siguiente como un punto mas. Y como es el PRIMERO de los dieciseis, o sea
 * el mas alejado del centro, es tambien el que mas peso tiene en la
 * pendiente: un pulso 380 puntos fuera de sitio inclina el ajuste 8 puntos
 * por linea, y la correccion sale once veces mayor de lo que toca.
 *
 * Esto llevaba ahi desde la etapa 26 y el banco lo daba por bueno, porque a
 * 120 lpm el salto era de diez puntos y la linea partida apenas mentia. Que
 * fuese pequeño era CASUALIDAD: el tono de arranque dura 5 s, y 5 s son 10
 * lineas justas a 120 lpm, 5 a 60 y 20 a 240 - pero SIETE Y MEDIA a 90. Con
 * la velocidad que no cuadra, el salto era de media linea y salio el fallo.
 * Una emisora de verdad no empieza la fase en un limite de linea nuestro a
 * ninguna velocidad, asi que esto se habria visto en el aire y en las
 * cuatro.
 *
 * El arreglo es tirar esa linea, que es la unica de la que SABEMOS que
 * miente, y no tocar las demas.
 */
static uint8_t  s_fase_tira;   /* la proxima linea esta partida: no mirarla */
static float    s_pulso_pos;   /* donde cayo el ultimo pulso */

/* --- 5. tonos de arranque y parada --- */
#define APT_N 240U             /* 20 ms: seis ciclos de 300 Hz */
static float    s_apt_buf[APT_N];
static uint16_t s_apt_n;
static float    s_apt300, s_apt450;

/* ==========================================================================
 * ARRANQUE
 * ========================================================================== */
static void reloj_fija(void)
{
    /* Una vuelta del acumulador por linea. lineas/s = lpm/60. */
    double lps = (double)k_lpm[s_lpm_i] / 60.0 * (1.0 + (double)s_ppm * 1e-6);
    s_inc = (uint32_t)(lps / (double)s_fs * 4294967296.0 + 0.5);
}

static void linea_reinicia(void)
{
    s_fase = 0U;
    s_px_ant = 0U;
    s_acc = 0.0f;
    s_acc_n = 0U;
    memset(s_linea, 0, sizeof s_linea);
    s_esc = 0U;
    s_linea_lista = 0U;
}

void wefax_start(float fs_hz)
{
    if (fs_hz <= 0.0f) { return; }
    s_fs = fs_hz;

    disc_fm_init(&s_disc, WEFAX_CENTRO_HZ, fs_hz);

    s_ppm = 0.0f;
    reloj_fija();
    linea_reinicia();

    s_lineas = 0UL;
    s_fase_n = 0U;
    s_fase_bruta = -1.0f;
    s_fase_off = 0.0f;
    s_fase_ajustes = 0U;
    s_fase_lineas = 0U;
    s_fase_tira = 0U;
    s_desliz = 0.0f;
    s_pulso_pos = 0.0f;
    s_apt_n = 0U;
    s_apt300 = s_apt450 = 0.0f;

    s_estado = WEFAX_BUSCA;
    s_on = 1U;
}

void wefax_stop(void)      { s_on = 0U; s_estado = WEFAX_PARADO; }
uint8_t wefax_activo(void) { return s_on; }

void wefax_set_lpm(uint16_t lpm)
{
    uint8_t i;
    for (i = 0U; i < WEFAX_LPM_N; i++) {
        if (k_lpm[i] == lpm) {
            s_lpm_i = i;
            /* Cambiar de velocidad tira el sincronismo: el ritmo que
             * llevabamos no significa nada con otra velocidad, y dejar la
             * fase donde estaba solo produce una linea partida. */
            s_ppm = 0.0f;
            reloj_fija();
            linea_reinicia();
            s_fase_n = 0U;
            s_fase_tira = 0U;
            s_desliz = 0.0f;
            return;
        }
    }
}

uint16_t wefax_get_lpm(void) { return k_lpm[s_lpm_i]; }
uint16_t wefax_lpm_opcion(uint8_t i) { return (i < WEFAX_LPM_N) ? k_lpm[i] : 0U; }

/*
 * El indice, para la interfaz y para CONFIG.CSV. Va aqui y no en main.c a
 * proposito: un indice guardado alli seria una SEGUNDA copia de cual es la
 * velocidad en vigor, y de esas ya se han separado tres en este proyecto.
 * Preguntando, solo hay una.
 */
uint8_t wefax_get_lpm_idx(void) { return s_lpm_i; }

void wefax_set_lpm_idx(uint8_t i)
{
    if (i < WEFAX_LPM_N) { wefax_set_lpm(k_lpm[i]); }
}

void wefax_set_ppm(float ppm)
{
    if (ppm > 30000.0f)  { ppm = 30000.0f; }
    if (ppm < -30000.0f) { ppm = -30000.0f; }
    s_ppm = ppm;
    reloj_fija();
}
float wefax_get_ppm(void) { return s_ppm; }

void wefax_sincroniza(void)
{
    s_fase = 0U;
    s_px_ant = 0U;
    s_acc = 0.0f;
    s_acc_n = 0U;
}

void wefax_fuerza_imagen(void)
{
    s_estado = WEFAX_IMAGEN;
    s_fase_n = 0U;
}

const uint8_t *wefax_linea(void)
{
    if (!s_linea_lista) { return 0; }
    s_linea_lista = 0U;
    return s_linea[s_esc ^ 1U];   /* el que acaba de cerrarse */
}

void wefax_info(wefax_info_t *out)
{
    if (!out) { return; }
    out->estado = s_estado;
    out->lpm    = k_lpm[s_lpm_i];
    out->ppm    = s_ppm;
    out->lineas = s_lineas;
    out->fase_n = s_fase_n;
    out->nivel  = disc_fm_nivel(&s_disc);
}

void wefax_fase_dbg(float *desliz_px_linea, float *pos_pulso)
{
    if (desliz_px_linea) { *desliz_px_linea = s_desliz; }
    if (pos_pulso)       { *pos_pulso = s_pulso_pos; }
}

/* ==========================================================================
 * LA SEÑAL DE FASE
 * ==========================================================================
 *
 * Cada linea de fase es negra con un pulso blanco que ocupa un 5% de la
 * linea. Aqui se busca el centro de ese pulso, y con la posicion en varias
 * lineas seguidas salen las dos cosas que hacen falta:
 *
 *   - DONDE empieza la linea: el pulso marca el principio, asi que basta con
 *     correr el contador para que el pulso caiga en el punto cero.
 *   - A QUE RITMO: si vamos mas lentos que la emisora, el pulso se va
 *     corriendo hacia la izquierda linea a linea; si mas rapidos, hacia la
 *     derecha. Lo que se corre por linea, dividido por el ancho, ES el error
 *     de velocidad.
 *
 * Ese segundo dato es el que quita la inclinacion, y no se puede sacar de
 * una sola linea por mucho que se mire: hace falta ver el pulso MOVERSE.
 */
static float pulso_centro(const uint8_t *l)
{
    uint32_t i, suma = 0UL, media = 0UL;
    float sx = 0.0f, sy = 0.0f, r, ang, pos;

    /*
     * MEDIA CIRCULAR, no media a secas.
     *
     * El pulso de fase esta al PRINCIPIO de la linea, o sea pegado al borde,
     * y cuando nuestro reloj va corrido el pulso se sale por un lado y entra
     * por el otro. En esas lineas queda PARTIDO: mitad en el punto 750 y
     * mitad en el 5. Una media normal de esas posiciones da 377, o sea el
     * centro de la linea - justo donde no esta.
     *
     * Y eso no se ve como un error pequeño: es un punto disparatado metido en
     * el ajuste de velocidad, que arrastra la recta entera. Se veia como un
     * decodificador que a veces enderezaba la imagen y a veces la torcia mas,
     * sin patron.
     *
     * La forma correcta de promediar algo que da la vuelta es tratarlo como
     * un ANGULO: cada punto blanco es un vector unitario, se suman, y el
     * angulo de la suma es la posicion. Con el pulso partido, los dos trozos
     * apuntan casi al mismo sitio y la suma cae donde tiene que caer.
     *
     * Y de regalo: la LONGITUD de esa suma dividida por el numero de puntos
     * dice si estaban todos juntos (cerca de 1) o desperdigados (cerca de 0).
     * O sea que la misma cuenta que da la posicion dice tambien si lo que se
     * esta mirando es un pulso o un trozo de mapa.
     */
    for (i = 0U; i < WEFAX_ANCHO; i++) {
        media += l[i];
        if (l[i] >= 160U) {
            float a = 2.0f * PI_F * (float)i / (float)WEFAX_ANCHO;
            sx += cosf(a); sy += sinf(a);
            suma++;
        }
    }

    media /= WEFAX_ANCHO;

    /*
     * Tres condiciones, y las tres hacen falta. Una linea de fase es NEGRA
     * con un pulso blanco corto y JUNTO; una linea de imagen cualquiera tiene
     * zonas claras repartidas. Sin estas tres, el seguidor de fase se cree
     * una linea de mapa y mide la velocidad contra ella - y eso paso: con la
     * señal de fase mas corta de lo que dura de verdad, la segunda pasada
     * caia dentro de la imagen y "medía" dieciocho veces el deslizamiento
     * real.
     */
    if (media > 80UL) { return -1.0f; }                  /* la linea no es negra */
    if (suma < 4UL || suma > WEFAX_ANCHO / 4U) { return -1.0f; }

    r = sqrtf(sx * sx + sy * sy) / (float)suma;
    if (r < 0.85f) { return -1.0f; }                     /* no estaban juntos */

    ang = atan2f(sy, sx);
    if (ang < 0.0f) { ang += 2.0f * PI_F; }
    pos = ang / (2.0f * PI_F) * (float)WEFAX_ANCHO;
    if (pos >= (float)WEFAX_ANCHO) { pos = 0.0f; }
    return pos;
}

static void fase_linea(const uint8_t *l)
{
    float p;

    s_fase_lineas++;

    /* La linea que acaba de partir el salto del acumulador - ver
     * s_fase_tira. Se cuenta (para el tope de seguridad de abajo) pero no
     * se mide. */
    if (s_fase_tira) {
        s_fase_tira = 0U;
        return;
    }

    p = pulso_centro(l);
    /*
     * Tope de seguridad: si la señal de fase se acaba (o nunca fue una señal
     * de fase) no se puede esperar aqui para siempre. Setenta lineas son mas
     * de lo que dura la fase de verdad, que son 30 segundos.
     */
    if (s_fase_lineas > 70U) {
        s_estado = WEFAX_IMAGEN;
        s_fase_n = 0U;
        return;
    }

    if (p < 0.0f) {
        /* Esta linea no tiene un pulso reconocible: se empieza otra vez. Una
         * medida de velocidad sacada de lineas salteadas no vale, porque no
         * se sabe cuantas lineas hay entre dos posiciones. */
        s_fase_n = 0U;
        s_fase_bruta = -1.0f;
        s_fase_off = 0.0f;
        return;
    }

    /*
     * DESENVOLVER. El pulso esta al PRINCIPIO de la linea, o sea pegado al
     * borde, que es justo donde se sale por un lado y entra por el otro. Sin
     * esto, un salto de 5 a 750 se lee como "se ha corrido 745 puntos" en vez
     * de "se ha corrido 11 hacia atras", y el ajuste sale del reves y
     * enorme.
     *
     * Esto costo la primera version: con la emisora LENTA salia bien y con la
     * RAPIDA salia disparatado, porque solo en ese sentido el pulso cruza el
     * cero. Un fallo que solo aparece en la mitad de los casos.
     */
    if (s_fase_bruta >= 0.0f) {
        float d = p - s_fase_bruta;
        if (d >  (float)WEFAX_ANCHO * 0.5f) { s_fase_off -= (float)WEFAX_ANCHO; }
        if (d < -(float)WEFAX_ANCHO * 0.5f) { s_fase_off += (float)WEFAX_ANCHO; }
    }
    s_fase_bruta = p;
    s_pulso_pos = p;

    s_fase_pos[s_fase_n] = p + s_fase_off;
    s_fase_n++;

    if (s_fase_n < FASE_LINEAS) { return; }

    {
        /*
         * Ajuste por minimos cuadrados de posicion = a + b * linea. Se usa la
         * formula cerrada para una x que va de 0 a n-1.
         *
         * Por minimos cuadrados y no con la primera y la ultima porque el
         * centro del pulso lleva ruido: con dos puntos, el ruido de esos dos
         * ES la medida. Con dieciseis, se promedia.
         */
        float n = (float)FASE_LINEAS;
        float sx = 0.0f, sy = 0.0f, sxy = 0.0f, sxx = 0.0f;
        uint8_t i;
        float b, den;

        for (i = 0U; i < FASE_LINEAS; i++) {
            float x = (float)i, y = s_fase_pos[i];
            sx += x; sy += y; sxy += x * y; sxx += x * x;
        }
        den = n * sxx - sx * sx;
        if (den > 0.0f) {
            b = (n * sxy - sx * sy) / den;

            /*
             * b son los puntos que se corre el pulso por linea, y su SIGNO
             * dice hacia donde hay que ir - al reves de lo que parece.
             *
             * Si el pulso se corre hacia la DERECHA es que las lineas de la
             * emisora duran MAS que las nuestras: vamos demasiado rapidos y
             * hay que FRENAR. Por eso la correccion lleva el signo cambiado.
             * Escrito con el signo "natural" el ajuste empeora el doble en
             * vez de arreglar, que es lo que hacia la primera version.
             */
            s_desliz = b;
            wefax_set_ppm(s_ppm - (b / (float)WEFAX_ANCHO) * 1.0e6f);
        }

        /*
         * Y el desfase: correr el contador para que el pulso caiga donde le
         * toca. El pulso ocupa el primer 5% de la linea, asi que su centro va
         * en el 2,5%: llevarlo al punto cero desplazaria la imagen entera
         * hacia la izquierda ese trozo.
         *
         * Se RESTA. Sumar al acumulador adelanta lo que se ve; para traer un
         * pulso que aparece en la posicion p hasta la posicion q hay que
         * correr el acumulador q - p, que con p mayor que q es negativo. La
         * primera version sumaba p a secas, y eso corre la imagen justo al
         * doble y en el sentido contrario.
         */
        {
            float destino = 0.025f * (float)WEFAX_ANCHO;
            float d = (destino - s_fase_bruta) / (float)WEFAX_ANCHO;
            s_fase = (uint32_t)((int32_t)s_fase + (int32_t)(d * 4294967296.0f));
            s_fase_tira = 1U;   /* este salto parte la linea en curso */
        }

        s_fase_n = 0U;
        s_fase_bruta = -1.0f;
        s_fase_off = 0.0f;
        s_fase_ajustes++;

        /*
         * DOS PASADAS. La primera quita el grueso del error; la segunda mide
         * lo que queda, que es donde estan las decimas de punto por linea que
         * deciden si a las mil lineas la carta sale recta o con un par de
         * grados de caida. Treinta y dos lineas son dieciseis segundos, y la
         * señal de fase dura treinta.
         */
        if (s_fase_ajustes >= 2U) {
            s_estado = WEFAX_IMAGEN;
        }
    }
}

/* ==========================================================================
 * LOS TONOS DE ARRANQUE Y PARADA
 * ==========================================================================
 *
 * No son tonos de audio: son el BRILLO alternando entre negro y blanco a 300
 * Hz (arranque) o a 450 Hz (parada) durante cinco segundos. O sea que hay que
 * buscarlos en lo que sale del discriminador, no en el audio.
 */
static float goertzel(const float *x, uint32_t n, float hz, float fs)
{
    float w = 2.0f * PI_F * hz / fs;
    float coef = 2.0f * cosf(w);
    float s0, s1 = 0.0f, s2 = 0.0f;
    float m;
    uint32_t i;

    for (i = 0U; i < n; i++) {
        s0 = x[i] + coef * s1 - s2;
        s2 = s1; s1 = s0;
    }
    m = s1 * s1 + s2 * s2 - coef * s1 * s2;

    /* Normalizado a AMPLITUD AL CUADRADO, dividiendo por (n/2)^2. Asi el
     * umbral de mas abajo es un numero con significado -cuanto tiene que
     * oscilar el brillo- y no un numero que depende del tamano de la
     * ventana. Un tono de arranque real oscila de negro a blanco, o sea una
     * amplitud de unos 160 en la escala de grises. */
    return m / (0.25f * (float)n * (float)n);
}

static void apt_mira(float v)
{
    s_apt_buf[s_apt_n] = v;
    s_apt_n++;
    if (s_apt_n < APT_N) { return; }
    s_apt_n = 0U;

    {
        float m3 = goertzel(s_apt_buf, APT_N, WEFAX_ARRANQUE_HZ, s_fs);
        float m4 = goertzel(s_apt_buf, APT_N, WEFAX_PARADA_HZ, s_fs);

        /* Media exponencial de un segundo largo: los tonos duran cinco, asi
         * que esto los ve de sobra y en cambio no lo dispara un golpe. */
        s_apt300 += 0.04f * (m3 - s_apt300);
        s_apt450 += 0.04f * (m4 - s_apt450);

        if (s_estado == WEFAX_BUSCA) {
            if (s_apt300 > 200.0f && s_apt300 > s_apt450 * 3.0f) {
                s_estado = WEFAX_FASE;
                s_fase_n = 0U;
                s_fase_bruta = -1.0f;
                s_fase_off = 0.0f;
                s_fase_ajustes = 0U;
                s_fase_lineas = 0U;
                s_apt300 = s_apt450 = 0.0f;
            }
        } else if (s_estado == WEFAX_IMAGEN || s_estado == WEFAX_FASE) {
            if (s_apt450 > 200.0f && s_apt450 > s_apt300 * 3.0f) {
                s_estado = WEFAX_BUSCA;
                s_fase_n = 0U;
                s_apt300 = s_apt450 = 0.0f;
            }
        }
    }
}

/* ==========================================================================
 * LA ENTRADA
 * ========================================================================== */
void wefax_process(const float *audio, uint32_t n)
{
    uint32_t k;

    if (!s_on || !audio) { return; }

    for (k = 0U; k < n; k++) {
        float hz = disc_fm_paso(&s_disc, audio[k]);
        float gris_f;
        uint32_t px;

        gris_f = (float)wefax_gris_de_hz(hz);
        apt_mira(gris_f - 128.0f);

        /* 4. repartir en puntos: el acumulador da una vuelta por linea y su
         * parte alta dice en que punto de la linea vamos. */
        s_fase += s_inc;
        px = (uint32_t)(((uint64_t)s_fase * (uint64_t)WEFAX_ANCHO) >> 32);

        if (px != s_px_ant) {
            if (s_acc_n > 0UL) {
                float g = s_acc / (float)s_acc_n;
                if (g < 0.0f) { g = 0.0f; }
                if (g > 255.0f) { g = 255.0f; }
                s_linea[s_esc][s_px_ant] = (uint8_t)g;
            }
            s_acc = 0.0f;
            s_acc_n = 0UL;

            /* Vuelta al principio: la linea esta entera. */
            if (px < s_px_ant) {
                s_lineas++;
                if (s_estado == WEFAX_FASE) {
                    fase_linea(s_linea[s_esc]);
                } else if (s_estado == WEFAX_IMAGEN) {
                    s_linea_lista = 1U;
                }
                s_esc ^= 1U;   /* cerrado: se sigue por el otro */
            }
            s_px_ant = px;
        }

        s_acc += gris_f;
        s_acc_n++;
    }
}
