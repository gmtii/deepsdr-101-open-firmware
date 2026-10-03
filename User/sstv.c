/*
 * SSTV. Ver sstv.h para el porque de cada pieza.
 */
#include "sstv.h"
#include "disc_fm.h"
#include <string.h>

#ifdef __GNUC__
#define TCMRAM_BSS __attribute__((section(".tcmram")))
#else
#define TCMRAM_BSS
#endif

/* Las frecuencias que fija la norma. */
#define SSTV_NEGRO_HZ   1500.0f
#define SSTV_BLANCO_HZ  2300.0f
#define SSTV_CENTRO_HZ  1900.0f
#define SSTV_SYNC_HZ    1200.0f
#define SSTV_LIDER_HZ   1900.0f
#define SSTV_UNO_HZ     1100.0f
#define SSTV_CERO_HZ    1300.0f

/* Por debajo de esto se considera pulso de sincronismo. Esta a medio camino
 * entre los 1200 del sincronismo y los 1500 del negro. */
#define SYNC_UMBRAL_HZ  1350.0f

/* ==========================================================================
 * LA TABLA DE MODOS
 * ==========================================================================
 *
 * Cada modo se describe con unos pocos TIEMPOS y una familia; todo lo demas
 * -donde empieza cada canal, cuanto dura la linea- se calcula. La
 * alternativa, una tabla con los desplazamientos ya hechos, es la misma
 * informacion escrita dos veces, y se ve lo que pasa con eso en la propia
 * implementacion de referencia de la que salen estos numeros: sus constantes
 * de Martin 1 no cuadran con su propia formula.
 *
 * CUATRO FAMILIAS, y no se parecen tanto como su nombre comun sugiere:
 *
 *   MARTIN y SCOTTIE mandan rojo, verde y azul, un barrido entero por canal.
 *   Solo se diferencian en donde cae el pulso de sincronismo dentro de la
 *   linea.
 *
 *   ROBOT no manda colores: manda LUMINANCIA y dos diferencias de color, que
 *   es como la television analogica de toda la vida. El ojo distingue mucho
 *   peor el color que el brillo, asi que las dos diferencias van a media
 *   resolucion - y en Robot 36 encima van ALTERNANDO, una en cada linea, de
 *   forma que cada par de lineas comparte el color. Eso es lo que hace que
 *   quepa una imagen de 240 lineas en 36 segundos.
 *
 *   PD lleva esa idea un paso mas: en UNA linea transmitida manda la
 *   luminancia de DOS lineas de imagen y un solo par de diferencias de color
 *   para las dos. Cuatro barridos por transmision, sin separadores entre
 *   ellos.
 *
 * LO QUE NO ENTRA: los modos PD grandes son de 640 u 800 puntos de ancho y
 * hasta 616 de alto. Aqui se muestrean a 320 columnas (que es lo que hace el
 * muestreador solo, promediando) y se deja caer una de cada N lineas para que
 * quepan en las 256 que hay. En PD120, PD180 y PD240 eso sale EXACTO -640 por
 * 496 a la mitad es 320 por 248, misma proporcion- y en PD160 y PD290 la
 * imagen sale un poco achatada. Es eso o no tenerlos.
 */
typedef enum {
    FAM_MARTIN = 0,
    FAM_SCOTTIE,
    FAM_ROBOT36,   /* croma a media resolucion y ALTERNANDO por linea */
    FAM_ROBOT72,   /* las dos cromas en cada linea, a media resolucion */
    FAM_PD         /* dos lineas de imagen por linea transmitida */
} sstv_fam_t;

typedef struct {
    uint8_t     vis;
    const char *nombre;
    uint8_t     fam;
    uint16_t    ancho, alto;    /* la resolucion NATIVA del modo */
    float       sync, porche, sep, scan;
    float       scan2;          /* barrido de los canales de color; 0 = igual */
    float       sep2;           /* separador extra tras cada canal (Robot 72) */
} sstv_modo_t;

static const sstv_modo_t k_modos[] = {
    /*                                        anch alto   sync     porche     sep       scan      scan2     sep2   */
    { 44U, "Martin 1",   (uint8_t)FAM_MARTIN,  320U,256U, 0.004862f,0.000572f,0.000572f,0.146432f,0.0f,     0.0f      },
    { 40U, "Martin 2",   (uint8_t)FAM_MARTIN,  320U,256U, 0.004862f,0.000572f,0.000572f,0.073216f,0.0f,     0.0f      },
    { 60U, "Scottie 1",  (uint8_t)FAM_SCOTTIE, 320U,256U, 0.009000f,0.001500f,0.001500f,0.138240f,0.0f,     0.0f      },
    { 56U, "Scottie 2",  (uint8_t)FAM_SCOTTIE, 320U,256U, 0.009000f,0.001500f,0.001500f,0.088064f,0.0f,     0.0f      },
    { 76U, "Scottie DX", (uint8_t)FAM_SCOTTIE, 320U,256U, 0.009000f,0.001500f,0.001500f,0.345600f,0.0f,     0.0f      },
    /* Robot: el separador ancho antes del color lleva ademas un porche que
     * DICE cual de las dos diferencias viene - ver croma_de_linea(). */
    {  8U, "Robot 36",   (uint8_t)FAM_ROBOT36, 320U,240U, 0.009000f,0.003000f,0.004500f,0.088000f,0.044000f,0.001500f },
    { 12U, "Robot 72",   (uint8_t)FAM_ROBOT72, 320U,240U, 0.009000f,0.003000f,0.004500f,0.138000f,0.069000f,0.001500f },
    /* PD: sin separadores entre canales, y el barrido sale del tiempo por
     * punto que define cada modo, multiplicado por su ancho nativo. */
    { 93U, "PD50",       (uint8_t)FAM_PD,      320U,256U, 0.020000f,0.002080f,0.0f,     0.000286f,0.0f,     0.0f      },
    { 99U, "PD90",       (uint8_t)FAM_PD,      320U,256U, 0.020000f,0.002080f,0.0f,     0.000532f,0.0f,     0.0f      },
    { 95U, "PD120",      (uint8_t)FAM_PD,      640U,496U, 0.020000f,0.002080f,0.0f,     0.000190f,0.0f,     0.0f      },
    { 98U, "PD160",      (uint8_t)FAM_PD,      512U,400U, 0.020000f,0.002080f,0.0f,     0.000382f,0.0f,     0.0f      },
    { 96U, "PD180",      (uint8_t)FAM_PD,      640U,496U, 0.020000f,0.002080f,0.0f,     0.000286f,0.0f,     0.0f      },
    { 97U, "PD240",      (uint8_t)FAM_PD,      640U,496U, 0.020000f,0.002080f,0.0f,     0.000382f,0.0f,     0.0f      },
    { 94U, "PD290",      (uint8_t)FAM_PD,      800U,616U, 0.020000f,0.002080f,0.0f,     0.000286f,0.0f,     0.0f      }
};
#define MODOS_N (sizeof(k_modos) / sizeof(k_modos[0]))

/* Lo mas que puede mandar un modo en una sola linea transmitida. PD manda
 * cuatro barridos; los demas, tres o dos. */
#define CH_MAX 4U

uint8_t     sstv_modos_n(void)              { return (uint8_t)MODOS_N; }
uint8_t     sstv_modo_vis(uint8_t i)        { return (i < MODOS_N) ? k_modos[i].vis : 0U; }
const char *sstv_modo_nombre(uint8_t i)     { return (i < MODOS_N) ? k_modos[i].nombre : 0; }

/*
 * EL PLAN DE UNA LINEA, calculado a partir de los tiempos del modo.
 *
 * Esta es la unica funcion que sabe como se monta cada familia, y la usan
 * tanto el decodificador como el banco del simulador. Si estuviera escrita
 * dos veces, el banco comprobaria que dos copias coinciden.
 */
uint8_t sstv_plan(uint8_t i, sstv_plan_t *pl)
{
    const sstv_modo_t *m;
    float chan, chan2;
    uint8_t c;

    if (i >= MODOS_N || !pl) { return 0U; }
    m = &k_modos[i];

    for (c = 0U; c < CH_MAX; c++) { pl->off[c] = 0.0f; pl->scan[c] = 0.0f; }
    pl->ancho = m->ancho;
    pl->alto  = m->alto;
    pl->sync  = m->sync;
    pl->lineas_img = 1U;
    pl->alterna = 0U;

    switch ((sstv_fam_t)m->fam) {
    case FAM_MARTIN:
        /* sincronismo, porche, y luego verde, azul y rojo seguidos */
        chan = m->sep + m->scan;
        pl->n_ch = 3U;
        pl->color = SSTV_COLOR_GBR;
        pl->off[0] = m->sync + m->porche;
        pl->off[1] = pl->off[0] + chan;
        pl->off[2] = pl->off[1] + chan;
        pl->scan[0] = pl->scan[1] = pl->scan[2] = m->scan;
        pl->linea = m->sync + m->porche + 3.0f * chan;
        break;

    case FAM_SCOTTIE:
        /* El sincronismo cae justo antes del ROJO, asi que contando desde el
         * pulso el orden en el tiempo es rojo, verde, azul - aunque los
         * indices sigan siendo verde, azul, rojo. */
        chan = m->sep + m->scan;
        pl->n_ch = 3U;
        pl->color = SSTV_COLOR_GBR;
        pl->off[2] = m->sync + m->porche;
        pl->off[0] = pl->off[2] + chan;
        pl->off[1] = pl->off[0] + chan;
        pl->scan[0] = pl->scan[1] = pl->scan[2] = m->scan;
        pl->linea = m->sync + 3.0f * chan;
        break;

    case FAM_ROBOT36:
        /* Luminancia entera y UNA diferencia de color a media resolucion.
         * Cual de las dos es lo dice el porche que va justo delante. */
        pl->n_ch = 2U;
        pl->color = SSTV_COLOR_YUV;
        pl->alterna = 1U;
        pl->off[0] = m->sync + m->porche;
        pl->off[1] = pl->off[0] + m->scan + m->sep + m->sep2;
        pl->scan[0] = m->scan;
        pl->scan[1] = m->scan2;
        pl->linea = pl->off[1] + m->scan2;
        break;

    case FAM_ROBOT72:
        pl->n_ch = 3U;
        pl->color = SSTV_COLOR_YUV;
        chan  = m->sep + m->scan;
        chan2 = m->sep + m->scan2;
        pl->off[0] = m->sync + m->porche;
        pl->off[1] = pl->off[0] + chan + m->sep2;
        pl->off[2] = pl->off[1] + chan2 + m->sep2;
        pl->scan[0] = m->scan;
        pl->scan[1] = pl->scan[2] = m->scan2;
        pl->linea = pl->off[2] + m->scan2;
        break;

    case FAM_PD:
    default:
        /*
         * Cuatro barridos seguidos y sin separadores: la luminancia de la
         * linea impar, las dos diferencias de color -que valen para las DOS
         * lineas- y la luminancia de la par.
         *
         * El barrido de cada canal es el tiempo por punto del modo por su
         * ancho NATIVO, no por los 320 en los que lo muestreamos: lo que
         * manda la emisora no depende de lo que nosotros hagamos con ello.
         */
        chan = m->scan * (float)m->ancho;
        pl->n_ch = 4U;
        pl->color = SSTV_COLOR_YUV;
        pl->lineas_img = 2U;
        pl->off[0] = m->sync + m->porche;
        pl->off[1] = pl->off[0] + chan;
        pl->off[2] = pl->off[1] + chan;
        pl->off[3] = pl->off[2] + chan;
        pl->scan[0] = pl->scan[1] = pl->scan[2] = pl->scan[3] = chan;
        pl->linea = pl->off[3] + chan;
        break;
    }

    /* Cuantas lineas hay que tirar para que la imagen quepa en las que
     * caben. Sale hacia arriba: mas vale achatar que cortar. */
    pl->vdec = (uint8_t)((m->alto + SSTV_ALTO - 1U) / SSTV_ALTO);
    if (pl->vdec == 0U) { pl->vdec = 1U; }
    return 1U;
}

/* La version vieja, que el banco y la interfaz siguen usando para lo simple. */
uint8_t sstv_modo_tiempos(uint8_t i, float *linea_s, float *sync_s,
                          float *scan_s, float off_s[3])
{
    sstv_plan_t pl;
    uint8_t c;

    if (!sstv_plan(i, &pl)) { return 0U; }
    if (linea_s) { *linea_s = pl.linea; }
    if (sync_s)  { *sync_s  = pl.sync; }
    if (scan_s)  { *scan_s  = pl.scan[0]; }
    if (off_s)   { for (c = 0U; c < 3U; c++) { off_s[c] = pl.off[c]; } }
    return 1U;
}

float sstv_hz_de_nivel(uint8_t v)
{
    return SSTV_NEGRO_HZ
         + ((float)v / 255.0f) * (SSTV_BLANCO_HZ - SSTV_NEGRO_HZ);
}

static uint8_t nivel_de_hz(float hz)
{
    float t = (hz - SSTV_NEGRO_HZ) / (SSTV_BLANCO_HZ - SSTV_NEGRO_HZ);
    if (t <= 0.0f) { return 0U; }
    if (t >= 1.0f) { return 255U; }
    return (uint8_t)(t * 255.0f + 0.5f);
}

/* ==========================================================================
 * ESTADO
 * ========================================================================== */
static uint8_t   s_on;
static float     s_fs;
static uint8_t   s_estado;
static disc_fm_t s_disc;

/* --- el modo en curso, ya resuelto a muestras --- */
static uint8_t   s_modo;          /* indice en k_modos */
static sstv_plan_t s_pl;          /* el plan del modo, en segundos */
static uint32_t  s_n_linea;       /* muestras por linea transmitida */
static uint32_t  s_n_sync;        /* muestras del pulso de sincronismo */
static uint32_t  s_n_off[CH_MAX]; /* donde empieza cada canal, en muestras */
static uint32_t  s_n_scan[CH_MAX];/* cuanto dura cada uno, en muestras */
static uint32_t  s_n_sep_a, s_n_sep_b;  /* la ventana del porche que dice la croma */

/* --- deteccion del pulso de sincronismo --- */
static uint32_t  s_muestra;       /* contador libre desde el arranque */
static uint32_t  s_bajo_n;        /* muestras seguidas por debajo del umbral */
static uint32_t  s_t0;            /* muestra en la que empezo la linea */
static uint8_t   s_hay_t0;

/* --- la linea en curso --- */
static TCMRAM_BSS uint16_t s_sum[CH_MAX][SSTV_ANCHO];
static TCMRAM_BSS uint8_t  s_cnt[CH_MAX][SSTV_ANCHO];
/* El porche que dice cual de las dos diferencias de color trae esta linea en
 * Robot 36. Ver croma_de_linea(). */
static float     s_sep_sum;
static uint32_t  s_sep_n;
/* Robot 36 manda una croma por linea, asi que hay que GUARDAR una linea
 * entera hasta que llegue la siguiente con la otra mitad del color. */
static uint8_t   s_r36_y[SSTV_ANCHO];
static uint8_t   s_r36_c[SSTV_ANCHO];
static uint8_t   s_r36_es_cr;
static uint8_t   s_r36_hay;
/* Los dos renglones de salida NO van en la RAM rapida: se tocan una vez por
 * linea -dos veces por segundo- y no en el bucle de muestras. Los que si van
 * son los acumuladores de arriba, que se leen y escriben en cada una de las
 * doce mil muestras por segundo. La RAM rapida son 64 kB para todo el
 * aparato: gastarla en algo que se toca dos veces por segundo es quitarsela
 * a quien la necesita de verdad. */
/*
 * DOCE RENGLONES EN ANILLO.
 *
 * Empezo en dos. Subio a cuatro porque PD cierra DOS lineas de imagen
 * seguidas sin que el bucle principal tenga ocasion de llevarselas en
 * medio, y con doble buffer la segunda pisaba a la primera antes de que
 * nadie la hubiera visto.
 *
 * Y sube a doce el 24/09/2026, al empezar a GUARDAR las fotos en el
 * pendrive: escribir un bloque de 4 kB en la flash SPI bloquea el bucle
 * principal medio segundo -cuatro ciclos de borrado y programado, y el
 * borrado lo pone el chip-. El decodificador sigue corriendo mientras
 * tanto, porque va en la interrupcion, asi que las lineas que cierre en
 * ese medio segundo tienen que caber aqui o se pierden.
 *
 * Martin 1 entrega una linea cada 0,446 s y un bloque se llena cada 4,3
 * renglones: con cuatro sobraba. Martin 2 va al doble y el colchon se
 * quedaba en 0,9 s para una escritura de 0,5. Doce dan 2,7 s en el modo
 * mas rapido, que es margen de verdad y no de justito. Cuesta 7,7 kB de
 * los 30 que quedaban libres.
 */
#define SAL_N 12U
static uint8_t   s_sal[SAL_N][SSTV_ANCHO * 3U];
static uint16_t  s_sal_ys[SAL_N];
static uint8_t   s_sal_cab, s_sal_col;
static uint16_t  s_y;

typedef enum { T_OTRO = 0, T_1100, T_1200, T_1300, T_NEGRO, T_LIDER } tono_t;

/* --- la cabecera VIS --- */
/*
 * NO SE PIDE QUE NINGUN TRAMO SEA SEGUIDO. Esto es lo que costo encontrar.
 *
 * El lider dura 300 ms y la primera version lo buscaba como UN tramo
 * ininterrumpido de ese tono. Con eso basta una excursion de ruido de catorce
 * muestras -algo menos de dos milisegundos- para partirlo en dos de 150 ms, y
 * entonces ninguno de los dos llega al cuarto de segundo que se exige: la
 * cabecera no se detecta NUNCA, con un ruido treinta veces menor que la señal
 * y con la imagen saliendo perfecta. Alargar la espera lo empeoraba, porque
 * cuanto mas larga se pide la racha mas facil es que el ruido la parta.
 *
 * Asi que no se cuentan rachas, se cuenta PRESENCIA: un contador por tono que
 * sube cuando el tono esta y baja cuando no, con suelo en cero. Una excursion
 * de ruido le quita unas pocas muestras a un contador que va por miles, y lo
 * que viene despues se las devuelve. Lo que se mide deja de ser "cuanto lleva
 * seguido" y pasa a ser "cuanto ha estado", que es lo que importa.
 *
 * Y para lo que SI es corto -el corte de 10 ms, el bit de arranque de 30- el
 * mismo contador sirve, porque ademas de decir cuanto ha estado guarda DESDE
 * CUANDO: ese instante es el que fija el reloj de los ocho bits.
 */

/*
 * SUAVIZADO DE LA FRECUENCIA, antes de clasificar. La constante esta MEDIDA,
 * no elegida: se probaron 0,10 / 0,15 / 0,25 / 0,40 y 0,15 es la unica que
 * aguanta ruido sin romper el corte de 10 ms, que es el tramo mas corto que
 * hay que reconocer. Mas lenta y el corte no llega a verse; mas rapida y el
 * temblor de la frecuencia vuelve.
 */
#define SUAVE_A 0.15f
static float s_hz_suave;


typedef enum {
    V_LIDER1 = 0,   /* esperando los 300 ms de 1900 Hz */
    V_CORTE,        /* y luego los 10 ms de 1200 */
    V_LIDER2,       /* y otros 300 ms de 1900 */
    V_ARRANQUE,     /* y el bit de arranque, que es el que pone el reloj */
    V_BITS
} vis_est_t;

static uint8_t   s_vis_est;
static uint32_t  s_cnt_lider;     /* presencia de 1900 Hz */
static uint32_t  s_cnt_1200;      /* presencia de 1200 Hz */
static uint32_t  s_ini_1200;      /* desde cuando */
static uint32_t  s_vis_plazo;     /* cuando se renuncia y se vuelve a empezar */
static uint32_t  s_vis_t0;        /* muestra en la que empieza el bit 0 */
static uint8_t   s_vis_bit;
static uint8_t   s_vis_val;
static uint8_t   s_vis_par;
static float     s_bit_sum;       /* el tercio central del bit, promediado */
static uint32_t  s_bit_n;

static sstv_info_t s_info;

/* ==========================================================================
 * ARRANQUE
 * ========================================================================== */
static void modo_aplica(uint8_t i)
{
    uint8_t c;

    if (!sstv_plan(i, &s_pl)) { return; }
    s_modo = i;
    s_n_linea = (uint32_t)(s_pl.linea * s_fs + 0.5f);
    s_n_sync  = (uint32_t)(s_pl.sync * s_fs + 0.5f);
    for (c = 0U; c < CH_MAX; c++) {
        s_n_off[c]  = (uint32_t)(s_pl.off[c] * s_fs + 0.5f);
        s_n_scan[c] = (uint32_t)(s_pl.scan[c] * s_fs + 0.5f);
    }
    /* La ventana donde mirar el porche de la croma: el hueco entre el final
     * de la luminancia y el principio del color. Se mira su tercio central,
     * por lo mismo que los bits de la cabecera. */
    if (s_pl.alterna) {
        uint32_t a = s_n_off[0] + s_n_scan[0];
        uint32_t b = s_n_off[1];
        uint32_t t = (b > a) ? (b - a) : 0U;
        s_n_sep_a = a + t / 3U;
        s_n_sep_b = b - t / 3U;
    } else {
        s_n_sep_a = s_n_sep_b = 0U;
    }
    s_info.modo = k_modos[i].nombre;
    /*
     * *** 24/09/2026, por el dueno del proyecto: "y ahora a la derecha me
     * ha puesto modo no soportado vis 044" ***
     *
     * A CERO, no al VIS del modo. s_info.vis significa UNA cosa: "llego un
     * VIS valido de un modo que no sabemos hacer" (ver donde se pone, al
     * final de la maquina del VIS). Es lo unico para lo que lo lee quien
     * pinta la cabecera.
     *
     * Ponerle aqui el VIS del modo que SI se acaba de aplicar le daba un
     * segundo significado, y entonces el que lo lee no puede distinguirlos:
     * en cuanto el estado dejaba de ser SSTV_IMAGEN -la foto acaba, o se
     * pierde la señal- la cabecera veia un 44 y anunciaba que Martin 1 no
     * esta soportado. Martin 1 es la PRIMERA entrada de la tabla.
     *
     * Tercera vez en este proyecto que un solo hueco carga con dos
     * significados y el que lo lee no puede separarlos. Las otras dos
     * fueron los motivos del volcado y los veredictos del almacen.
     */
    s_info.vis  = 0U;
}

static void linea_limpia(void)
{
    memset(s_sum, 0, sizeof s_sum);
    memset(s_cnt, 0, sizeof s_cnt);
    s_sep_sum = 0.0f;
    s_sep_n = 0U;
}

void sstv_start(float fs_hz)
{
    if (fs_hz <= 0.0f) { return; }
    s_fs = fs_hz;
    disc_fm_init(&s_disc, SSTV_CENTRO_HZ, fs_hz);

    memset(&s_info, 0, sizeof s_info);
    linea_limpia();
    memset(s_sal, 0, sizeof s_sal);
    s_sal_cab = s_sal_col = 0U;
    s_y = 0U;
    s_r36_hay = 0U; s_r36_es_cr = 0U;

    s_muestra = 0UL; s_bajo_n = 0UL; s_t0 = 0UL; s_hay_t0 = 0U;
    s_vis_est = (uint8_t)V_LIDER1;
    s_vis_t0 = 0UL; s_vis_bit = 0U; s_vis_val = 0U; s_vis_par = 0U;
    s_cnt_lider = 0UL; s_cnt_1200 = 0UL; s_ini_1200 = 0UL; s_vis_plazo = 0UL;
    s_hz_suave = SSTV_CENTRO_HZ;

    modo_aplica(0U);           /* algo puesto por si se fuerza sin cabecera */
    s_info.modo = 0; s_info.vis = 0U;
    s_estado = SSTV_BUSCA;
    s_on = 1U;
}

void sstv_stop(void)      { s_on = 0U; s_estado = SSTV_PARADO; }
uint8_t sstv_activo(void) { return s_on; }

void sstv_fuerza(uint8_t indice_modo)
{
    if (indice_modo >= MODOS_N) { return; }
    modo_aplica(indice_modo);
    s_estado = SSTV_IMAGEN;
    s_hay_t0 = 0U;
    s_y = 0U;
    linea_limpia();
    s_info.imagenes++;
}

const uint8_t *sstv_linea(uint16_t *y)
{
    uint8_t i;

    if (s_sal_col == s_sal_cab) { return 0; }
    i = s_sal_col;
    s_sal_col = (uint8_t)((s_sal_col + 1U) % SAL_N);
    if (y) { *y = s_sal_ys[i]; }
    return s_sal[i];
}

void sstv_info(sstv_info_t *out)
{
    if (!out) { return; }
    *out = s_info;
    out->estado = s_estado;
    out->linea = s_y;
    out->nivel = disc_fm_nivel(&s_disc);
}

/* ==========================================================================
 * CERRAR UNA LINEA
 * ==========================================================================
 *
 * El orden de los canales es verde, azul, rojo - no rojo, verde, azul. Es lo
 * que manda el formato y es una de esas cosas que si se escribe mal no
 * revienta nada: sale una foto con los colores cambiados, que parece un
 * problema de recepcion.
 */
/* Media de un canal en un punto, o 128 (neutro) si ahi no llego nada. */
static uint8_t val(uint8_t c, uint16_t x)
{
    return s_cnt[c][x] ? (uint8_t)(s_sum[c][x] / s_cnt[c][x]) : 128U;
}

/*
 * De luminancia y diferencias de color a rojo, verde y azul.
 *
 * Es la conversion de la television analogica de siempre, la misma que usa
 * el JPEG. No es una aproximacion nuestra: los modos Robot y PD mandan
 * exactamente esto, y escribir otra cosa da una foto con los tonos de piel
 * verdosos - que no parece un fallo de codigo.
 */
static void yuv_a_rgb(int32_t y, int32_t cr, int32_t cb, uint8_t *d)
{
    int32_t r = y + ((1402 * (cr - 128)) / 1000);
    int32_t g = y - ((344 * (cb - 128)) / 1000) - ((714 * (cr - 128)) / 1000);
    int32_t b = y + ((1772 * (cb - 128)) / 1000);

    if (r < 0) { r = 0; } if (r > 255) { r = 255; }
    if (g < 0) { g = 0; } if (g > 255) { g = 255; }
    if (b < 0) { b = 0; } if (b > 255) { b = 255; }
    d[0] = (uint8_t)r; d[1] = (uint8_t)g; d[2] = (uint8_t)b;
}

/*
 * Pone una linea de imagen en la cola de salida, si toca pintarla.
 *
 * "Si toca" porque los modos PD grandes tienen mas lineas de las que caben:
 * ahi se pinta una de cada vdec. Se decide AQUI y no al dibujar, para que el
 * panel no tenga que saber nada de resoluciones de modos.
 */
static void mete_linea(const uint8_t *rgb, uint16_t y_img)
{
    uint8_t sig;

    if (s_pl.vdec > 1U && (y_img % s_pl.vdec) != 0U) { return; }
    y_img = (uint16_t)(y_img / s_pl.vdec);
    if (y_img >= SSTV_ALTO) { return; }

    sig = (uint8_t)((s_sal_cab + 1U) % SAL_N);
    if (sig == s_sal_col) { return; }   /* la cola va llena: se tira */
    memcpy(s_sal[s_sal_cab], rgb, SSTV_ANCHO * 3U);
    s_sal_ys[s_sal_cab] = y_img;
    s_sal_cab = sig;
}

/*
 * CUAL DE LAS DOS DIFERENCIAS DE COLOR TRAE ESTA LINEA (solo Robot 36).
 *
 * No hay que adivinarlo ni contar lineas: la propia señal lo dice. El
 * separador que va entre la luminancia y el color se transmite a 1500 Hz
 * cuando lo que viene es R-Y y a 2300 Hz cuando es B-Y. O sea que la misma
 * frecuencia que en la imagen significa "negro" y "blanco" aqui significa
 * "rojo" y "azul".
 *
 * Contar lineas tambien funcionaria mientras no se pierda ninguna. Leer el
 * porche funciona AUNQUE se pierda: cada linea dice lo suyo por su cuenta.
 */
static uint8_t croma_de_linea(void)
{
    if (s_sep_n == 0U) { return s_r36_es_cr ? 0U : 1U; }   /* sin dato: alternar */
    return (uint8_t)((s_sep_sum / (float)s_sep_n) < 1900.0f);  /* 1 = R-Y */
}

static void linea_cierra(void)
{
    static uint8_t rgb[SSTV_ANCHO * 3U];
    uint16_t x;

    if (s_pl.color == SSTV_COLOR_GBR) {
        /*
         * El orden de los canales es verde, azul, rojo - no rojo, verde,
         * azul. Es lo que manda el formato y es una de esas cosas que si se
         * escribe mal no revienta nada: sale una foto con los colores
         * cambiados, que parece un problema de recepcion. La primera version
         * lo puso al reves CON este mismo comentario al lado diciendo lo
         * correcto.
         */
        for (x = 0U; x < SSTV_ANCHO; x++) {
            rgb[x * 3U]      = val(2U, x);   /* rojo   <- canal 2 */
            rgb[x * 3U + 1U] = val(0U, x);   /* verde  <- canal 0 */
            rgb[x * 3U + 2U] = val(1U, x);   /* azul   <- canal 1 */
        }
        mete_linea(rgb, s_y);
        s_y++;

    } else if (s_pl.lineas_img == 2U) {
        /* PD: luminancia de la linea impar, el par de diferencias de color
         * -que valen para las dos- y luminancia de la par. */
        for (x = 0U; x < SSTV_ANCHO; x++) {
            yuv_a_rgb(val(0U, x), val(1U, x), val(2U, x), &rgb[x * 3U]);
        }
        mete_linea(rgb, s_y);
        for (x = 0U; x < SSTV_ANCHO; x++) {
            yuv_a_rgb(val(3U, x), val(1U, x), val(2U, x), &rgb[x * 3U]);
        }
        mete_linea(rgb, (uint16_t)(s_y + 1U));
        s_y = (uint16_t)(s_y + 2U);

    } else if (s_pl.alterna) {
        /* Robot 36: esta linea trae media informacion de color. Hay que
         * esperar a la siguiente y sacar las dos juntas. */
        uint8_t es_cr = croma_de_linea();

        if (!s_r36_hay || es_cr == s_r36_es_cr) {
            /* O es la primera, o han llegado dos iguales seguidas -lo que
             * significa que se ha perdido una linea por el camino-. En los
             * dos casos, esta pasa a ser la guardada y se espera a la otra. */
            for (x = 0U; x < SSTV_ANCHO; x++) {
                s_r36_y[x] = val(0U, x);
                s_r36_c[x] = val(1U, x);
            }
            s_r36_es_cr = es_cr;
            s_r36_hay = 1U;
        } else {
            const uint8_t *cr = es_cr ? 0 : s_r36_c;
            const uint8_t *cb = es_cr ? s_r36_c : 0;

            for (x = 0U; x < SSTV_ANCHO; x++) {
                uint8_t c_cr = cr ? cr[x] : val(1U, x);
                uint8_t c_cb = cb ? cb[x] : val(1U, x);
                yuv_a_rgb(s_r36_y[x], c_cr, c_cb, &rgb[x * 3U]);
            }
            mete_linea(rgb, s_y);
            for (x = 0U; x < SSTV_ANCHO; x++) {
                uint8_t c_cr = cr ? cr[x] : val(1U, x);
                uint8_t c_cb = cb ? cb[x] : val(1U, x);
                yuv_a_rgb(val(0U, x), c_cr, c_cb, &rgb[x * 3U]);
            }
            mete_linea(rgb, (uint16_t)(s_y + 1U));
            s_y = (uint16_t)(s_y + 2U);
            s_r36_hay = 0U;
        }

    } else {
        /* Robot 72: luminancia y las dos diferencias, todas en esta linea. */
        for (x = 0U; x < SSTV_ANCHO; x++) {
            yuv_a_rgb(val(0U, x), val(1U, x), val(2U, x), &rgb[x * 3U]);
        }
        mete_linea(rgb, s_y);
        s_y++;
    }

    if (s_y >= s_pl.alto) {
        /* Imagen completa: se vuelve a esperar cabecera. Seguir pintando
         * seria inventarse que hay mas imagen. */
        s_y = 0U;
        s_r36_hay = 0U;
        s_estado = SSTV_BUSCA;
        s_vis_est = (uint8_t)V_LIDER1;
        s_hay_t0 = 0U;
    }
    linea_limpia();
}

/* ==========================================================================
 * LA CABECERA VIS
 * ==========================================================================
 *
 * 300 ms a 1900 Hz, 10 ms a 1200, otros 300 ms a 1900, y luego diez tramos de
 * 30 ms: arranque, siete bits de datos (el de menos peso primero), paridad
 * par y parada. Un 1 va a 1100 Hz y un 0 a 1300.
 *
 * La paridad se comprueba y se exige. Son solo siete bits y un error los
 * convierte en otro modo perfectamente valido: sin comprobarla, un chasquido
 * en el momento justo pone el decodificador a pintar Scottie DX cuando lo que
 * viene es Martin 1, y eso no se ve como un fallo de recepcion sino como una
 * imagen que no aparece.
 */
void sstv_vis_dbg(uint8_t *est, uint32_t *lider, uint32_t *c1200, float *hz)
{
    if (est)   { *est = s_vis_est; }
    if (lider) { *lider = s_cnt_lider; }
    if (c1200) { *c1200 = s_cnt_1200; }
    if (hz)    { *hz = s_hz_suave; }
}

static uint8_t clasifica(float hz)
{
    /* Bandas que se tocan entre si, sin huecos: asi la transicion de un tono
     * al siguiente pasa por bandas intermedias en vez de caer en un "no se
     * que es esto" que habria que interpretar aparte. */
    if (hz < 1150.0f) { return (uint8_t)T_1100; }
    if (hz < 1250.0f) { return (uint8_t)T_1200; }
    if (hz < 1400.0f) { return (uint8_t)T_1300; }
    if (hz < 1700.0f) { return (uint8_t)T_NEGRO; }
    return (uint8_t)T_LIDER;
}

/* Un contador de presencia: sube con el tono, baja sin el, suelo en cero, y
 * apunta desde cuando lleva subiendo. */
static void presencia(uint32_t *cnt, uint32_t *ini, uint8_t hay, uint32_t ahora)
{
    if (hay) {
        if (*cnt == 0UL && ini) { *ini = ahora; }
        /* Con tope: sin el, un lider larguisimo deja el contador tan alto que
         * la maquina de estados se queda pegada en el paso siguiente durante
         * segundos aunque el tono ya haya cambiado. */
        if (*cnt < 60000UL) { (*cnt)++; }
    } else if (*cnt > 0UL) {
        (*cnt)--;
    }
}

static void vis_paso(float hz)
{
    uint8_t t = clasifica(hz);
    /*
     * PRESENCIA NETA, no duracion. El lider dura 300 ms y aqui se piden 150:
     * es un contador que sube con el tono y baja sin el, asi que 150 de neto
     * son 225 ms de lider con 75 de ruido dentro, o sea hasta un 25% de
     * muestras mal clasificadas. Pedir los 300 enteros era el borde exacto
     * donde se caia: a ruido 0,10 el lider clasificaba bien el 91% del tiempo
     * y el neto se quedaba en 246 ms, cuatro por debajo del umbral.
     */
    /*
     * PRESENCIA NETA, no duracion seguida: el contador sube con el tono y
     * baja sin el. El lider dura 300 ms y aqui se piden 150, o sea que aguanta
     * hasta un 25% de muestras mal clasificadas sin enterarse.
     */
    uint32_t largo = (uint32_t)(0.15f * s_fs);      /* el lider */
    uint32_t corto = (uint32_t)(0.004f * s_fs);     /* lo minimo de un 1200 */
    uint32_t ms30  = (uint32_t)(0.030f * s_fs + 0.5f);

    presencia(&s_cnt_lider, 0, (uint8_t)(t == (uint8_t)T_LIDER), s_muestra);
    presencia(&s_cnt_1200, &s_ini_1200, (uint8_t)(t == (uint8_t)T_1200), s_muestra);

    /* Plazo: si una cabecera empieza y no termina, se vuelve a empezar en vez
     * de quedarse esperando para siempre un tramo que ya no va a venir. */
    if (s_vis_est != (uint8_t)V_LIDER1
        && s_vis_est != (uint8_t)V_BITS
        && (int32_t)(s_muestra - s_vis_plazo) > 0) {
        s_vis_est = (uint8_t)V_LIDER1;
    }

    switch ((vis_est_t)s_vis_est) {
    case V_LIDER1:
        if (s_cnt_lider >= largo) {
            s_vis_est = (uint8_t)V_CORTE;
            s_vis_plazo = s_muestra + (uint32_t)(0.5f * s_fs);
            s_cnt_1200 = 0UL;
        }
        break;

    case V_CORTE:
        if (s_cnt_1200 >= corto) {
            s_vis_est = (uint8_t)V_LIDER2;
            s_vis_plazo = s_muestra + (uint32_t)(1.0f * s_fs);
            s_cnt_lider = 0UL;
        }
        break;

    case V_LIDER2:
        if (s_cnt_lider >= largo) {
            s_vis_est = (uint8_t)V_ARRANQUE;
            s_vis_plazo = s_muestra + (uint32_t)(0.5f * s_fs);
            s_cnt_1200 = 0UL;
        }
        break;

    case V_ARRANQUE:
        /*
         * El bit de arranque: 30 ms a 1200 Hz. Aqui no basta con saber que
         * esta, hay que saber DONDE EMPIEZA, porque de ese instante cuelga el
         * reloj de los ocho bits que vienen detras. Por eso el contador de
         * presencia guarda tambien su origen.
         */
        if (s_cnt_1200 >= corto) {
            s_vis_est = (uint8_t)V_BITS;
            s_vis_t0 = s_ini_1200 + ms30;   /* el bit 0 empieza tras el arranque */
            s_vis_bit = 0U; s_vis_val = 0U; s_vis_par = 0U;
            s_bit_sum = 0.0f; s_bit_n = 0UL;
        }
        break;

    case V_BITS:
    default: {
        /*
         * CADA BIT SE PROMEDIA SOBRE SU TERCIO CENTRAL, no se lee de una
         * muestra.
         *
         * La primera version miraba la frecuencia en el instante justo de la
         * mitad del bit. Con la señal limpia sale bien; con ruido, UNA
         * muestra es una tirada de dados, y basta que una de las ocho caiga
         * mal para que la paridad no cuadre y la cabecera entera se tire. El
         * banco lo enseño sin ambiguedad: el detector recorria los cuatro
         * pasos, llegaba a leer los ocho bits, y aun asi no salia ningun
         * modo.
         *
         * Promediando los diez milisegundos centrales -ciento veinte
         * muestras- el ruido se divide por once. Es la misma leccion que el
         * lider, a otra escala: no decidir por un instante, decidir por un
         * rato.
         */
        uint32_t base = s_vis_t0 + ms30 * s_vis_bit;
        uint32_t ini  = base + ms30 / 3U;
        uint32_t fin  = base + (2U * ms30) / 3U;

        if ((int32_t)(s_muestra - ini) >= 0 && (int32_t)(s_muestra - fin) < 0) {
            s_bit_sum += hz;
            s_bit_n++;
        }

        if ((int32_t)(s_muestra - fin) >= 0 && s_bit_n > 0UL) {
            uint8_t b = (uint8_t)((s_bit_sum / (float)s_bit_n) < 1200.0f);
            s_bit_sum = 0.0f; s_bit_n = 0UL;

            if (s_vis_bit < 7U) {
                if (b) { s_vis_val = (uint8_t)(s_vis_val | (1U << s_vis_bit)); }
                s_vis_par = (uint8_t)(s_vis_par ^ b);
                s_vis_bit++;
                break;
            }

            if (b == s_vis_par) {
                uint8_t i;
                for (i = 0U; i < (uint8_t)MODOS_N; i++) {
                    if (k_modos[i].vis == s_vis_val) {
                        modo_aplica(i);
                        s_estado = SSTV_IMAGEN;
                        s_hay_t0 = 0U;
                        s_y = 0U;
                        linea_limpia();
                        s_info.imagenes++;
                        break;
                    }
                }
                /* Un VIS valido de un modo que no sabemos hacer se guarda para
                 * poder verlo: es lo que hace falta para decidir si vale la
                 * pena anadirlo. */
                if (i >= (uint8_t)MODOS_N) { s_info.vis = s_vis_val; }
            }
            s_vis_est = (uint8_t)V_LIDER1;
            s_cnt_lider = 0UL;
        }
        break;
    }
    }
}

/* ==========================================================================
 * LA ENTRADA
 * ========================================================================== */
void sstv_process(const float *audio, uint32_t n)
{
    uint32_t k;

    if (!s_on || !audio) { return; }

    for (k = 0U; k < n; k++) {
        float hz_cruda = disc_fm_paso(&s_disc, audio[k]);
        float hz;
        uint8_t bajo;

        /* El suavizado se usa para DECIDIR (que tono es, si hay sincronismo)
         * y la cruda para MEDIR el brillo de cada punto: en el brillo el
         * suavizado emborronaria los bordes verticales de la foto, y ahi cada
         * punto ya lleva su propio promedio de varias muestras. */
        s_hz_suave += SUAVE_A * (hz_cruda - s_hz_suave);
        hz = s_hz_suave;
        bajo = (uint8_t)(hz < SYNC_UMBRAL_HZ);

        s_muestra++;

        if (s_estado == SSTV_BUSCA) {
            vis_paso(hz);
            continue;
        }

        /*
         * SINCRONISMO, LINEA A LINEA. Esta es la ventaja del SSTV sobre el
         * fax: cada linea trae su marca, asi que no hay que adivinar el ritmo
         * ni corregir inclinacion. Lo unico que hay que hacer es no creerse
         * cualquier bajada de frecuencia.
         *
         * Por eso un pulso solo vale si cae DONDE TOCA: a una distancia del
         * anterior que se parezca a la duracion de la linea. Dentro de la
         * imagen hay zonas oscuras que bajan hasta 1500 Hz y ruido que baja
         * mas; sin esta condicion, cualquiera de ellas parte la imagen por la
         * mitad.
         */
        if (bajo) {
            s_bajo_n++;
        } else {
            if (s_bajo_n >= (s_n_sync / 2U)) {
                uint32_t ini = s_muestra - s_bajo_n - 1UL;   /* donde empezo */

                if (!s_hay_t0) {
                    s_t0 = ini; s_hay_t0 = 1U;
                    linea_limpia();
                } else {
                    uint32_t d = ini - s_t0;
                    if (d > (s_n_linea * 4U) / 5U && d < (s_n_linea * 6U) / 5U) {
                        linea_cierra();
                        s_t0 = ini;
                    } else {
                        s_info.sincs_malos++;
                    }
                }
            }
            s_bajo_n = 0UL;
        }

        if (!s_hay_t0) { continue; }

        /*
         * Y repartir la muestra en su canal y su punto. Como mucho cuatro
         * comparaciones por muestra, y cada canal tiene su propio barrido -
         * en Robot y PD no todos duran lo mismo-.
         *
         * El ancho de destino son SIEMPRE 320 columnas, sea cual sea el del
         * modo. Eso no es un recorte: la division reparte las muestras entre
         * 320 cajones y cada una suma en el suyo, o sea que un modo de 640
         * puntos sale PROMEDIADO de dos en dos, que es exactamente lo que hay
         * que hacer para reducirlo.
         */
        {
            uint32_t t = s_muestra - s_t0;
            uint8_t c;

            for (c = 0U; c < s_pl.n_ch; c++) {
                if (s_n_scan[c] != 0U
                    && t >= s_n_off[c] && t < s_n_off[c] + s_n_scan[c]) {
                    uint32_t px = ((t - s_n_off[c]) * SSTV_ANCHO) / s_n_scan[c];
                    if (px < SSTV_ANCHO && s_cnt[c][px] < 250U) {
                        s_sum[c][px] = (uint16_t)(s_sum[c][px] + nivel_de_hz(hz_cruda));
                        s_cnt[c][px]++;
                    }
                    break;
                }
            }

            /* Y el porche que dice que croma trae esta linea, en Robot 36. */
            if (s_n_sep_b > s_n_sep_a && t >= s_n_sep_a && t < s_n_sep_b) {
                s_sep_sum += hz;
                s_sep_n++;
            }

            /* Si la linea se pasa de largo sin que llegue un sincronismo
             * -desvanecimiento- se cierra igual y se sigue: mejor una linea
             * con un trozo perdido que la imagen entera parada. */
            if (t > (s_n_linea * 6U) / 5U) {
                linea_cierra();
                s_t0 = s_muestra;
                s_info.sincs_malos++;
            }
        }
    }
}
