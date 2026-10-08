/* Ver s4415.h para el porque de cada numero. Aqui esta el como. */
#include "s4415.h"
#include <stdint.h>
#include "hfdl_viterbi.h"
#include <math.h>
#include <string.h>

#define PI_F       3.14159265f
#define OCTAVO     0.78539816f   /* 45 grados en radianes, un tribit */

#define RRC_SPAN   4U
#define RRC_TAPS   (2U * RRC_SPAN * S4415_SPS + 1U)   /* 41 */
#define RRC_ALFA   0.35f

/*
 * La cola del filtro adaptado es de 64 y no de 41 A PROPOSITO: con 41 cada
 * avance del indice es un "% 41", que en un Cortex-M4 no es gratis y se paga
 * OCHENTA Y DOS VECES POR MUESTRA. Con 64 es un "& 63".
 */
#define RRC_ANI    64U
#define RRC_ANI_M  (RRC_ANI - 1U)

/*
 * LA COLA DE AUDIO CRUDO. La interrupcion SOLO COPIA aqui; mezclar y filtrar
 * se hace en s4415_paso(), desde el bucle principal. Es la misma promesa que
 * stanag_rx.c tiene escrita y que yo rompi: "lo que hace por bloque es un
 * oscilador de ocho valores y una suma: acotado y proporcional al bloque".
 *
 * 2048 muestras son 170 ms. Si el bucle principal se retrasa mas que eso se
 * tira lo mas viejo y SE CUENTA, que es mejor que pegar dos trozos de audio
 * de sitios distintos y no enterarse.
 */
#define COLA_N     2048U
#define COLA_M     (COLA_N - 1U)
#define COLA_ESC   8000.0f

/* Cuantas muestras muele s4415_paso() como mucho en una llamada. Es el tope
 * que impide que esto se coma el bucle principal pase lo que pase. */
#define MUELE_MAX  1024U

/*
 * EL ANILLO VA A 12 kHz, NO A SIMBOLO. Y esto no es un capricho.
 *
 * Decimar por cinco con un contador fijo deja la fase de muestreo donde
 * cayo al arrancar. Entre esa fase y la del transmisor puede haber hasta
 * dos muestras y media, o sea MEDIO SIMBOLO: el peor sitio posible, justo
 * en el cruce del ojo. Y deslizar la ventana de correlacion no lo arregla,
 * porque un simbolo son cinco muestras y al deslizar un simbolo se cae en
 * la misma fase.
 *
 * Asi que se guarda la salida del filtro adaptado a 12 kHz y se correla con
 * paso cinco empezando en CUALQUIER muestra. El barrido prueba las cinco
 * fases por si solo, sin codigo aparte.
 *
 * 4096 muestras complejas = 32 kB de las 54 prestadas. Hacen falta 2.240
 * para cubrir desde el principio de la plantilla hasta el final de C3
 * (catorce simbolos de preambulo), y sobra sitio para el bloque de audio
 * que entre entre dos pasos.
 */
#define RING       4096U
#define RING_M     (RING - 1U)

/* Muestras que ocupa un simbolo de preambulo (32 tribits) y un segmento. */
#define SIM_MUE    (S4415_SET * S4415_SPS)            /* 160 */
#define SEG_MUE    (S4415_SEG * SIM_MUE)              /* 2400 = 200 ms */
#define PLANT_MUE  (S4415_PLANT * S4415_SPS)          /* 1440 = 120 ms */
#define HASTA_C3   (14U * SIM_MUE)                    /* 2240 */

/*
 * Cuanto tiene que valer la correlacion para dar un segmento por bueno.
 * La medida esta en docs/STANAG_4415_MEDIDO.md: los veinticuatro segmentos
 * de la grabacion del dueño salieron muy por encima de la mediana del ruido
 * de fondo. 0,25 deja un factor cinco por debajo y un factor dos por encima.
 */
#define UMBRAL     0.25f

/*
 * El filtro grueso. Correlar los 288 tribits en cada muestra son 3,5 millones
 * de multiplicaciones complejas por segundo, que la radio puede pero no
 * gratis. Asi que primero se correla SOLO EL PRIMER TROZO de 32, que son 32
 * multiplicaciones, y la plantilla entera se calcula unicamente donde ese
 * trozo pasa de UMBRAL_1. Nueve veces menos trabajo.
 *
 * 0,45 sobre 32 muestras de ruido salta una vez de cada setecientas, asi que
 * lo que se ahorra no se vuelve a gastar en falsas alarmas.
 */
#define UMBRAL_1   0.45f

/*
 * EL ENTRELAZADOR, TABLA X Y 5.3.2.3.4/5 DEL ESTANDAR.
 *
 * A 75 bps en frecuencia fija -"75N" en la tabla X- no vale lo de los demas:
 *
 *      largo   20 filas x 36 columnas = 720 bits  (4,8 s)
 *      corto   10 filas x  9 columnas =  90 bits  (0,6 s)
 *
 * La CARGA avanza la fila de 7 en 7 modulo el numero de filas -no de 9 en 9
 * modulo 40 como en el resto de velocidades- y la LECTURA baja la columna de
 * 7 en 7 -no de 17 en 17-. Las dos excepciones estan escritas aparte en el
 * estandar justo para esta velocidad, y si se usan las de los demas no sale
 * nada: la permutacion es valida igual, pero es otra.
 *
 * Para deshacerlo hace falta el inverso de 7: 7x3 = 21 = 1 modulo 20 y
 * tambien modulo 10, asi que el inverso es 3 en los dos casos.
 */
#define ENT_FIL_L   20U
#define ENT_COL_L   36U
#define ENT_FIL_C   10U
#define ENT_COL_C    9U
#define ENT_SALTO    7U
#define ENT_INV      3U
#define SETS_MAX    (ENT_FIL_L * ENT_COL_L / 2U)   /* 360 sets de datos */
#define BITS_MAX    (ENT_FIL_L * ENT_COL_L)        /* 720 bits codificados */

/*
 * EL ORDEN DE LOS DOS BITS DENTRO DEL DIBIT, Y QUE NO SE SABE.
 *
 * La tabla XI dice que a 75 bps en frecuencia fija se sacan DOS bits del
 * entrelazador por simbolo de canal, y la tabla XIV indexa por "00, 01, 10,
 * 11". Que el primero que sale del entrelazador sea el de la izquierda es lo
 * razonable y es lo que se hace aqui, pero EL ESTANDAR NO LO DICE con todas
 * las letras y la grabacion del dueño no sirve para comprobarlo: va 84% a
 * cero, o sea relleno de inactividad, y con todo ceros los dos ordenes dan lo
 * mismo.
 *
 * Asi que queda escrito como lo que es -una suposicion- y con el interruptor
 * a mano. Con una grabacion que lleve trafico de verdad se resuelve en un
 * minuto: el orden bueno saca texto y el otro saca basura.
 */
#define BIT_ALTO_PRIMERO  1

/* --- las tablas del estandar -------------------------------------------- */

/*
 * TABLA XVII: simbolo de preambulo (0..7) -> 32 tribits, que son la funcion
 * de Walsh de longitud ocho repetida cuatro veces. Un tribit solo vale 0 o 4
 * -o sea BPSK- asi que cabe en un bit y las ocho filas caben en ocho bytes,
 * leidos del bit mas alto al mas bajo:
 *
 *      0 -> 0000 0000      4 -> 0000 4444
 *      1 -> 0404 0404      5 -> 0404 4040
 *      2 -> 0044 0044      6 -> 0044 4400
 *      3 -> 0440 0440      7 -> 0440 4004
 *
 * *** AQUI HUBO UN FALLO Y SE DEJA ESCRITO. La primera version guardo estos
 * patrones en NIBBLES -0x05 por "0404"- y los leyo como BYTES, con lo que
 * "0404 0404" salia "0000 0404". La correlacion habria dado ruido y el
 * enganche no habria aparecido nunca. Lo cazo el banco antes de que llegara
 * a la radio, que es para lo que esta. ***
 */
static const uint8_t k_txvii[8] = {
    0x00, 0x55, 0x33, 0x66, 0x0F, 0x5A, 0x3C, 0x69
};

/*
 * TABLA XIV, Y POR QUE NO HACE FALTA UNA TABLA. El estandar da dos juegos:
 *
 *   a) sets normales      00 -> (0000) x8   01 -> (0404) x8
 *                         10 -> (0044) x8   11 -> (0440) x8
 *   b) sets excepcionales 00 -> (0000 4444) x4   01 -> (0404 4040) x4
 *                         10 -> (0044 4400) x4   11 -> (0440 4004) x4
 *
 * Escritos asi parecen ocho patrones nuevos. No lo son: (0000) ocho veces y
 * (0000 0000) cuatro veces son los MISMOS 32 tribits, y lo mismo las otras
 * tres. O sea que los ocho patrones de la tabla XIV son EXACTAMENTE los ocho
 * de la tabla XVII, los normales las filas 0..3 y los excepcionales las 4..7.
 *
 * Asi que el desensanchado es un unico decisor de ocho formas sobre k_txvii:
 * el dibit son los dos bits de abajo del indice y el bit de arriba dice si el
 * set era excepcional.
 *
 * Y ESO ES LO QUE MARCA EL BLOQUE DEL ENTRELAZADOR. El estandar lo dice:
 * "The receive modem shall use the modification of the known data at
 * interleaver boundaries to synchronize without a preamble". El set
 * excepcional es el 360 de cada bloque con entrelazado largo -el ultimo- y el
 * 45 con el corto. No hay que suponer donde cae el bloque: se ve.
 */

/*
 * El aleatorizador del preambulo. El estandar lo da escrito, no hay que
 * deducirlo: "shall repeat every 32 transmitted symbols: 7430515022115743
 * 5026216200505266, where 7 shall always be used first and 6 shall be used
 * last."
 */
static const uint8_t k_scr_sync[32] = {
    7,4,3,0,5,1,5,0,2,2,1,1,5,7,4,3,5,0,2,6,2,1,6,2,0,0,5,0,5,2,6,6
};

/* Los nueve simbolos fijos del preambulo, que son la plantilla. */
static const uint8_t k_fijos[S4415_PRE_FIJOS] = { 0U,1U,3U,0U,1U,3U,1U,2U,0U };

/* Tabla XV: D1,D2 -> velocidad y entrelazado. Solo las que existen. */
static const uint8_t k_txv[][3] = {
    /* D1, D2, indice de velocidad (ver k_bps) ; largo = D1 < 6 */
    { 6, 4, 0 }, { 4, 4, 0 },   /* 2400 */
    { 6, 5, 1 }, { 4, 5, 1 },   /* 1200 */
    { 6, 6, 2 }, { 4, 6, 2 },   /*  600 */
    { 6, 7, 3 }, { 4, 7, 3 },   /*  300 */
    { 7, 4, 4 }, { 5, 4, 4 },   /*  150 */
    { 7, 5, 5 }, { 5, 5, 5 },   /*   75 */
    { 7, 6, 6 },                /* 4800, solo corto */
    { 7, 7, 7 },                /* 2400 voz segura, solo corto */
};
static const char *const k_bps[8] = {
    "2400", "1200", "600", "300", "150", "75", "4800", "2400 voz"
};

/*
 * EL OSCILADOR DE BAJADA, EXACTO Y SIN TRIGONOMETRIA.
 *
 * *** El dueño, 07/10/2026: "me cuelga nada mas entrar en stanag". ***
 *
 * Y LA CULPA FUE DE NO LEER LO QUE YO MISMO HABIA ESCRITO. stanag_rx.c, el
 * fichero de al lado, lleva este comentario desde hace dias:
 *
 *   "La primera version de este modulo rompio esa promesa: metia aqui dos
 *    sinf, dos cosf y un filtro de 41 tomas POR MUESTRA, o sea unos 700
 *    ciclos por muestra dentro de la interrupcion."
 *
 * Pues la primera version de ESTE hizo exactamente lo mismo. A 12.000
 * muestras por segundo son 24.000 llamadas a la libreria de matematicas por
 * segundo DENTRO DE LA INTERRUPCION DE AUDIO, y en un Cortex-M4 sin coma
 * flotante de doble precision eso no cabe en 200 MHz: la interrupcion no
 * termina antes de que llegue la siguiente y la radio se queda clavada nada
 * mas entrar en el modo.
 *
 * 1800 / 12000 = 3/20, asi que la fase se repite cada VEINTE muestras
 * clavadas. Es una tabla de veinte, no una llamada a cosf(). Y ademas no
 * acumula error nunca, al contrario que la fase que iba sumando antes.
 */
static const float k_nco_c[20] = {
       1.00000000f,    0.58778525f,   -0.30901699f,   -0.95105652f,   -0.80901699f,
       0.00000000f,    0.80901699f,    0.95105652f,    0.30901699f,   -0.58778525f,
      -1.00000000f,   -0.58778525f,    0.30901699f,    0.95105652f,    0.80901699f,
       0.00000000f,   -0.80901699f,   -0.95105652f,   -0.30901699f,    0.58778525f
};
static const float k_nco_s[20] = {
       0.00000000f,    0.80901699f,    0.95105652f,    0.30901699f,   -0.58778525f,
      -1.00000000f,   -0.58778525f,    0.30901699f,    0.95105652f,    0.80901699f,
       0.00000000f,   -0.80901699f,   -0.95105652f,   -0.30901699f,    0.58778525f,
       1.00000000f,    0.58778525f,   -0.30901699f,   -0.95105652f,   -0.80901699f
};

/* Las ocho fases de 45 grados, para no llamar a cosf() en el bucle interno. */
static const float k_cos8[8] = {
    1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f, 0.0f, 0.70710678f
};
static const float k_sin8[8] = {
    0.0f, 0.70710678f, 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f
};

const char *s4415_bps_txt(uint8_t idx)
{
    return (idx < 8U) ? k_bps[idx] : "?";
}

/* El tribit del patron p en la posicion i (0..31): 0 o 4. */
static inline uint8_t pat_trib(uint8_t p, uint8_t i)
{
    return ((p >> (7U - (i & 7U))) & 1U) ? 4U : 0U;
}

/* --- estado, todo en la RAM prestada ------------------------------------ */

typedef struct {
    float    rrc[RRC_TAPS];
    float    mi[RRC_ANI], mq[RRC_ANI];     /* cola del filtro adaptado */
    uint16_t mp;
    uint8_t  nco;                          /* 0..19, el oscilador exacto */

    int16_t  cola[COLA_N];                 /* audio crudo de la interrupcion */
    /*
     * REVISION A FONDO DEL 08/10/2026: VOLATILE, COMO EN EL MODULO GEMELO.
     *
     * cola_w la escribe la INTERRUPCION DE AUDIO (s4415_mete) y cola_r el
     * bucle principal (muele), y las dos se leen desde el otro lado para
     * decidir si la cola se ha desbordado. Sin volatile el compilador
     * puede guardarse cualquiera de las dos en un registro a lo largo de
     * muele() y no volver a mirar la memoria: la cola parece quieta, el
     * desbordamiento no se ve -o se ve donde no esta- y el sintoma que
     * sale es "no engancha", que no apunta a la causa ni de lejos.
     * stanag_rx.c, que hace lo mismo con su s_crudo_w, ya lo tenia
     * marcado desde el primer dia; aqui se habia quedado sin marcar.
     */
    volatile uint32_t cola_w, cola_r;
    float    nivel;
    uint32_t tiradas;                      /* muestras que no se molieron */

    float    r[RING], i[RING];             /* filtro adaptado a 12 kHz */
    uint32_t n;                            /* muestras escritas */
    uint32_t visto;                        /* hasta donde ha mirado el barrido */

    uint8_t  plant[S4415_PLANT];           /* los 288 tribits de la plantilla */

    /* enganche */
    uint8_t  hay;
    uint32_t t_ult;                        /* muestra inicial del ultimo segmento */
    uint8_t  pend;                         /* esperando a que lleguen D1..C3 */
    uint32_t t_pend;
    float    dps_pend;                     /* fase por simbolo del enganche */
    float    corr_pend;

    /* datos */
    uint8_t  en_datos;
    uint32_t t_dat;                        /* muestra del siguiente set de 32 */
    float    dps_dat;
    uint16_t scr_i;
    uint8_t  scr[S4415_SCR_DAT];

    uint8_t  dib[256];
    uint16_t dib_n, dib_sale;
    uint32_t perdidas;                     /* muestras que el barrido no vio */

    /* desensanchado -> bits blandos, en orden de LECTURA del entrelazador */
    uint8_t  blando[BITS_MAX];
    uint16_t blando_n;
    uint32_t sets;                         /* sets de datos vistos */
    uint32_t exc_sitio[4];                 /* donde cayeron los excepcionales */
    uint8_t  exc_n;

    /* FEC */
    uint8_t  desent[BITS_MAX];
    hfdl_viterbi_metric_t   vma, vmb;
    hfdl_viterbi_decision_t vdec[SETS_MAX + 8U];
    uint32_t vest;                         /* estado del codificador */
    uint8_t  bits[(SETS_MAX + 7U) / 8U];

    /* texto */
    char     txt[S4415_LINEAS][S4415_COLS + 1U];
    uint8_t  txt_n, txt_col;
    uint16_t areg; uint8_t an;             /* los ocho bits a medio juntar */
    uint32_t eomreg;
    uint8_t  fin;                          /* bloques que faltan tras el EOM */
    uint8_t  noticia;                      /* un bloque ha salido: hay que repintar */

    s4415_est_t est;
} s4415_st_t;

static s4415_st_t *s_st;
static uint8_t     s_on;


/* --- el aleatorizador de datos (figura 6) -------------------------------- */
/*
 * Doce etapas, b1..b12. La realimentacion sale de b12 y entra, por un XOR, en
 * la entrada de b2, b5 y b7, ademas de ser la entrada de b1. Ocho
 * desplazamientos por simbolo y la salida son b3,b2,b1 como MSB..LSB.
 *
 * Esto se leyo del dibujo del estandar -no viene escrito en texto- y se
 * COMPROBO contra la grabacion del dueño: la coherencia BPSK del tramo de
 * datos pasa de 0,096 a 0,318 al aplicarlo, contra un techo de 0,149 que dan
 * las secuencias al azar. Ver docs/STANAG_4415_MEDIDO.md.
 */
static void scr_datos(uint8_t *out, uint16_t n)
{
    uint8_t b[13];
    uint16_t k;
    uint8_t j;
    static const char ini[12] = { '1','0','1','1','1','0','1','0','1','1','0','1' };

    for (j = 0U; j < 12U; j++) { b[12U - j] = (uint8_t)(ini[j] - '0'); }
    b[0] = 0U;

    for (k = 0U; k < n; k++) {
        uint8_t p;
        for (p = 0U; p < 8U; p++) {
            uint8_t fb = b[12], nb[13];
            nb[0] = 0U;
            nb[1] = fb;
            nb[2] = (uint8_t)(b[1] ^ fb);
            nb[3] = b[2];
            nb[4] = b[3];
            nb[5] = (uint8_t)(b[4] ^ fb);
            nb[6] = b[5];
            nb[7] = (uint8_t)(b[6] ^ fb);
            for (j = 8U; j < 13U; j++) { nb[j] = b[j - 1U]; }
            for (j = 0U; j < 13U; j++) { b[j] = nb[j]; }
        }
        out[k] = (uint8_t)((b[3] << 2) | (b[2] << 1) | b[1]);
    }
}

/* --- arranque ------------------------------------------------------------ */

static void rrc_llena(float *h)
{
    int16_t i;
    float e = 0.0f;
    for (i = 0; i < (int16_t)RRC_TAPS; i++) {
        float t = ((float)i - (float)(RRC_SPAN * S4415_SPS)) / (float)S4415_SPS;
        float v;
        float d = 1.0f - (4.0f * RRC_ALFA * t) * (4.0f * RRC_ALFA * t);
        if (fabsf(t) < 1e-6f) {
            v = 1.0f - RRC_ALFA + (4.0f * RRC_ALFA / PI_F);
        } else if (fabsf(d) < 1e-4f) {
            v = (RRC_ALFA / 1.41421356f) *
                ((1.0f + 2.0f / PI_F) * sinf(PI_F / (4.0f * RRC_ALFA)) +
                 (1.0f - 2.0f / PI_F) * cosf(PI_F / (4.0f * RRC_ALFA)));
        } else {
            v = (sinf(PI_F * t * (1.0f - RRC_ALFA)) +
                 4.0f * RRC_ALFA * t * cosf(PI_F * t * (1.0f + RRC_ALFA))) /
                (PI_F * t * d);
        }
        h[i] = v;
        e += v * v;
    }
    e = sqrtf(e);
    if (e > 0.0f) { for (i = 0; i < (int16_t)RRC_TAPS; i++) { h[i] /= e; } }
}

/*
 * La memoria NO es suya: se la pasa quien lo arranca, igual que a
 * stanag_rx.c. Asi el reparto del prestamo lo lleva un solo fichero
 * -stanag_modo.c- y no cuatro modulos cada uno pidiendo por su cuenta.
 */
_Static_assert(sizeof(s4415_st_t) <= (S4415_FLOATS * 4U),
               "s4415: el estado no cabe en S4415_FLOATS");

uint8_t s4415_init(float *trabajo, uint32_t n_floats)
{
    s_on = 0U; s_st = 0;
    if (trabajo == 0 || n_floats < S4415_FLOATS) { return 0U; }
    if ((((uintptr_t)trabajo) & 3U) != 0U) { return 0U; }

    s_st = (s4415_st_t *)(void *)trabajo;
    memset(s_st, 0, sizeof *s_st);
    rrc_llena(s_st->rrc);
    scr_datos(s_st->scr, S4415_SCR_DAT);
    {
        uint16_t k;
        for (k = 0U; k < S4415_PLANT; k++) {
            uint8_t i32 = (uint8_t)(k % S4415_SET);
            s_st->plant[k] = (uint8_t)
                ((pat_trib(k_txvii[k_fijos[k / S4415_SET]], i32) + k_scr_sync[i32]) % 8U);
        }
    }
    s_on = 1U;
    return 1U;
}

uint32_t s4415_ram_bytes(void) { return (uint32_t)sizeof(s4415_st_t); }

void s4415_fin(void)
{
    s_st = 0;
    s_on = 0U;
}

uint8_t s4415_activo(void) { return s_on; }

/* --- entrada de audio ---------------------------------------------------- */

/*
 * LA INTERRUPCION DE AUDIO SOLO COPIA. NADA MAS. Ver el comentario del
 * oscilador, arriba, y el de stanag_rx_mete() en el fichero de al lado.
 *
 * La ganancia va aqui porque la cola es de enteros y hay que fijarle una
 * escala: el audio de banda lateral no viene normalizado, asi que se mide su
 * nivel medio -una milesima por muestra, unos 80 ms- y se divide.
 */
void s4415_mete(const float *audio, uint32_t n)
{
    uint32_t k, w;

    if (!s_on || s_st == 0 || audio == 0) { return; }

    w = s_st->cola_w;
    for (k = 0U; k < n; k++) {
        float x = audio[k], v;
        s_st->nivel += 0.001f * (fabsf(x) - s_st->nivel);
        v = (s_st->nivel > 1e-12f) ? ((x * 0.25f * COLA_ESC) / s_st->nivel)
                                   : (x * COLA_ESC);
        if (v >  32767.0f) { v =  32767.0f; }
        if (v < -32767.0f) { v = -32767.0f; }
        s_st->cola[w & COLA_M] = (int16_t)v;
        w++;
    }
    s_st->cola_w = w;
}

/*
 * Y AQUI SE MUELE, desde el bucle principal: mezcla a banda base con la tabla
 * de veinte y filtro adaptado. Como mucho MUELE_MAX muestras por llamada, que
 * es el tope que impide que esto deje sin aire al resto de la radio.
 */
static void muele(void)
{
    uint32_t hay, cuantas, k;

    if ((s_st->cola_w - s_st->cola_r) > COLA_N) {
        /* el bucle principal ha ido tarde: se tira lo viejo y se cuenta */
        uint32_t perdidas = (s_st->cola_w - s_st->cola_r) - COLA_N;
        s_st->tiradas += perdidas;
        s_st->cola_r = s_st->cola_w - COLA_N;
    }
    hay = s_st->cola_w - s_st->cola_r;
    cuantas = (hay > MUELE_MAX) ? MUELE_MAX : hay;

    for (k = 0U; k < cuantas; k++) {
        float x = (float)s_st->cola[s_st->cola_r & COLA_M] * (1.0f / COLA_ESC);
        float ir = 0.0f, qr = 0.0f;
        uint16_t j, p;

        s_st->cola_r++;

        s_st->mi[s_st->mp] =  x * k_nco_c[s_st->nco];
        s_st->mq[s_st->mp] = -x * k_nco_s[s_st->nco];
        s_st->mp = (uint16_t)((s_st->mp + 1U) & RRC_ANI_M);
        s_st->nco = (uint8_t)((s_st->nco + 1U) % 20U);

        /* filtro adaptado, a 12 kHz: hay que guardar TODAS las fases */
        p = (uint16_t)((s_st->mp + RRC_ANI - RRC_TAPS) & RRC_ANI_M);
        for (j = 0U; j < RRC_TAPS; j++) {
            ir += s_st->rrc[j] * s_st->mi[p];
            qr += s_st->rrc[j] * s_st->mq[p];
            p = (uint16_t)((p + 1U) & RRC_ANI_M);
        }
        s_st->r[s_st->n & RING_M] = ir;
        s_st->i[s_st->n & RING_M] = qr;
        s_st->n++;
    }
}

static inline void mue(uint32_t idx, float *r, float *i)
{
    uint32_t p = idx & RING_M;
    *r = s_st->r[p]; *i = s_st->i[p];
}

/* --- las correlaciones --------------------------------------------------- */

/* El trozo grueso: solo los 32 primeros tribits de la plantilla. */
static float corr_trozo0(uint32_t t0)
{
    float ar = 0.0f, ai = 0.0f, e = 0.0f;
    uint8_t j;
    for (j = 0U; j < S4415_SET; j++) {
        uint8_t t = s_st->plant[j];
        float wr = k_cos8[t], wi = k_sin8[t];
        float sr, si;
        mue(t0 + (uint32_t)j * S4415_SPS, &sr, &si);
        ar += sr * wr + si * wi;
        ai += si * wr - sr * wi;
        e  += sr * sr + si * si;
    }
    if (e <= 0.0f) { return 0.0f; }
    return sqrtf((ar * ar) + (ai * ai)) / sqrtf(e * (float)S4415_SET);
}

/*
 * La plantilla entera, en nueve trozos de 32.
 *
 * NO SE SUMAN LOS 288 DE GOLPE, y la razon es fisica: 288 tribits son 120 ms,
 * y para que una suma coherente de 120 ms no se cancele sola hace falta que
 * la portadora este puesta con menos de dos hercios de error. Nadie sintoniza
 * una banda lateral con dos hercios.
 *
 * Asi que se correla trozo a trozo -13,3 ms cada uno, que aguantan +-37 Hz-,
 * se mide la RAMPA DE FASE entre trozos consecutivos, que es justamente el
 * error de portadora, y se suman los nueve ya desgirados. Se recupera casi
 * toda la ganancia de los 288 y se aguantan los mismos +-37 Hz.
 *
 * Devuelve 0..1, y en `dps` la fase que avanza por tribit, que es lo que
 * hace falta despues para leer D1..C3 y para desensanchar los datos.
 */
static float corr_plantilla(uint32_t t0, float *dps)
{
    float gr[S4415_PRE_FIJOS], gi[S4415_PRE_FIJOS];
    float e = 0.0f, pr = 0.0f, pi = 0.0f, sr = 0.0f, si = 0.0f, d;
    uint8_t c, j;

    for (c = 0U; c < S4415_PRE_FIJOS; c++) {
        float ar = 0.0f, ai = 0.0f;
        for (j = 0U; j < S4415_SET; j++) {
            uint16_t k = (uint16_t)(c * S4415_SET + j);
            uint8_t t = s_st->plant[k];
            float wr = k_cos8[t], wi = k_sin8[t];
            float xr, xi;
            mue(t0 + (uint32_t)k * S4415_SPS, &xr, &xi);
            ar += xr * wr + xi * wi;
            ai += xi * wr - xr * wi;
            e  += xr * xr + xi * xi;
        }
        gr[c] = ar; gi[c] = ai;
    }
    if (e <= 0.0f) { if (dps) { *dps = 0.0f; } return 0.0f; }

    for (c = 1U; c < S4415_PRE_FIJOS; c++) {
        pr += gr[c] * gr[c - 1U] + gi[c] * gi[c - 1U];
        pi += gi[c] * gr[c - 1U] - gr[c] * gi[c - 1U];
    }
    d = atan2f(pi, pr);                 /* fase que avanza por trozo de 32 */

    for (c = 0U; c < S4415_PRE_FIJOS; c++) {
        float a = -d * (float)c;
        float cr = cosf(a), ci = sinf(a);
        sr += gr[c] * cr - gi[c] * ci;
        si += gr[c] * ci + gi[c] * cr;
    }
    if (dps) { *dps = d / (float)S4415_SET; }
    return sqrtf((sr * sr) + (si * si)) / sqrtf(e * (float)S4415_PLANT);
}

/*
 * Lee un simbolo de preambulo suelto (0..7) colocado en la muestra ts,
 * contra los ocho patrones de la tabla XVII. `cal` sale 0..1.
 */
static uint8_t lee_sim(uint32_t ts, float dps, float *cal)
{
    float e = 0.0f, vmax = -1.0f, v2 = -1.0f;
    uint8_t mejor = 0U, s, j;
    float xr[S4415_SET], xi[S4415_SET];

    /*
     * EL DESGIRADO VA CON UN ROTADOR, NO CON TREINTA Y DOS cosf().
     * Girar un angulo fijo treinta y dos veces es multiplicar treinta y dos
     * veces por el mismo numero complejo. Dos llamadas a la libreria por
     * conjunto en vez de sesenta y cuatro.
     */
    float cr = 1.0f, ci = 0.0f;
    float dr = cosf(-dps), di = sinf(-dps);
    for (j = 0U; j < S4415_SET; j++) {
        float sr, si, t;
        mue(ts + (uint32_t)j * S4415_SPS, &sr, &si);
        xr[j] = sr * cr - si * ci;      /* desgirado por el error de portadora */
        xi[j] = sr * ci + si * cr;
        e += (sr * sr) + (si * si);
        t = cr * dr - ci * di; ci = cr * di + ci * dr; cr = t;
    }
    for (s = 0U; s < 8U; s++) {
        float ar = 0.0f, ai = 0.0f, v;
        for (j = 0U; j < S4415_SET; j++) {
            uint8_t t = (uint8_t)((pat_trib(k_txvii[s], j) + k_scr_sync[j]) % 8U);
            float wr = k_cos8[t], wi = k_sin8[t];
            ar += xr[j] * wr + xi[j] * wi;
            ai += xi[j] * wr - xr[j] * wi;
        }
        v = (ar * ar) + (ai * ai);
        if (v > vmax) { v2 = vmax; vmax = v; mejor = s; }
        else if (v > v2) { v2 = v; }
    }
    (void)v2;
    if (cal) {
        *cal = (e > 0.0f) ? (sqrtf(vmax) / sqrtf(e * (float)S4415_SET)) : 0.0f;
    }
    return mejor;
}

static inline float mayor4(float a, float b, float c, float d)
{
    float m = (a > b) ? a : b;
    if (c > m) { m = c; }
    if (d > m) { m = d; }
    return m;
}

/* De una diferencia de amplitudes a un bit blando 0..255. */
static uint8_t blando_de(float d, float amax)
{
    float v;
    if (amax <= 0.0f) { return 128U; }
    v = 128.0f + 127.0f * (d / amax);
    if (v < 0.0f) { v = 0.0f; }
    if (v > 255.0f) { v = 255.0f; }
    return (uint8_t)v;
}

/*
 * DESENSANCHADO. 32 tribits en la muestra ts, con el aleatorizador de datos
 * en la posicion si0, contra las OCHO formas de la tabla XVII (que son las
 * ocho de la tabla XIV, ver arriba).
 *
 * Devuelve el indice ganador 0..7 -dibit = indice & 3, excepcional = indice
 * >= 4- y, en `blandos`, los dos bits con su confianza para el Viterbi: 0 es
 * un cero segurisimo, 255 un uno segurisimo y 128 "ni idea". Eso es lo que
 * hace que el Viterbi corrija de verdad en vez de tragarse decisiones duras.
 */
static uint8_t lee_set(uint32_t ts, float dps, uint16_t si0,
                       uint8_t *blandos, uint8_t *margen)
{
    float a[8];
    float vmax = -1.0f, v2 = 0.0f, amax = 0.0f, d1, d0;
    uint8_t mejor = 0U, pp, j;
    float xr[S4415_SET], xi[S4415_SET];

    {   /* el mismo rotador que en lee_sim(): dos llamadas, no sesenta y cuatro */
        float cr = 1.0f, ci = 0.0f;
        float dr = cosf(-dps), di = sinf(-dps);
        for (j = 0U; j < S4415_SET; j++) {
            float sr, si, t;
            mue(ts + (uint32_t)j * S4415_SPS, &sr, &si);
            xr[j] = sr * cr - si * ci;
            xi[j] = sr * ci + si * cr;
            t = cr * dr - ci * di; ci = cr * di + ci * dr; cr = t;
        }
    }
    for (pp = 0U; pp < 8U; pp++) {
        float ar = 0.0f, ai = 0.0f, v;
        for (j = 0U; j < S4415_SET; j++) {
            uint16_t sidx = (uint16_t)((si0 + j) % S4415_SCR_DAT);
            uint8_t t = (uint8_t)((pat_trib(k_txvii[pp], j) + s_st->scr[sidx]) % 8U);
            float wr = k_cos8[t], wi = k_sin8[t];
            ar += xr[j] * wr + xi[j] * wi;
            ai += xi[j] * wr - xr[j] * wi;
        }
        v = (ar * ar) + (ai * ai);
        a[pp] = sqrtf(v);
        if (a[pp] > amax) { amax = a[pp]; }
        if (v > vmax) { v2 = vmax; vmax = v; mejor = pp; }
        else if (v > v2) { v2 = v; }
    }

    /* bit alto: gana el grupo de los indices con el bit 1 puesto */
    d1 = mayor4(a[2], a[3], a[6], a[7]) - mayor4(a[0], a[1], a[4], a[5]);
    d0 = mayor4(a[1], a[3], a[5], a[7]) - mayor4(a[0], a[2], a[4], a[6]);
    if (blandos) {
#if BIT_ALTO_PRIMERO
        blandos[0] = blando_de(d1, amax);
        blandos[1] = blando_de(d0, amax);
#else
        blandos[0] = blando_de(d0, amax);
        blandos[1] = blando_de(d1, amax);
#endif
    }
    if (margen) {
        float m = (v2 > 0.0f) ? (sqrtf(vmax / v2) * 10.0f) : 255.0f;
        *margen = (m > 255.0f) ? 255U : (uint8_t)m;
    }
    return mejor;
}

/* --- un paso ------------------------------------------------------------- */

static void mete_dibit(uint8_t d)
{
    s_st->dib[s_st->dib_n] = d;
    s_st->dib_n = (uint16_t)((s_st->dib_n + 1U) % (uint16_t)(sizeof s_st->dib));
    if (s_st->dib_n == s_st->dib_sale) {
        s_st->dib_sale = (uint16_t)((s_st->dib_sale + 1U) % (uint16_t)(sizeof s_st->dib));
    }
    s_st->est.dibits++;
}

/* --- el texto ------------------------------------------------------------ */

static void pon_char(char c)
{
    if (s_st->txt_n == 0U) { s_st->txt_n = 1U; s_st->txt_col = 0U; s_st->txt[0][0] = '\0'; }
    if (c == '\r') { return; }
    if (c == '\n' || s_st->txt_col >= S4415_COLS) {
        if (s_st->txt_n < S4415_LINEAS) {
            s_st->txt_n++;
        } else {
            uint8_t k;
            for (k = 1U; k < S4415_LINEAS; k++) {
                memcpy(s_st->txt[k - 1U], s_st->txt[k], sizeof s_st->txt[0]);
            }
        }
        s_st->txt_col = 0U;
        s_st->txt[s_st->txt_n - 1U][0] = '\0';
        if (c == '\n') { return; }
    }
    s_st->txt[s_st->txt_n - 1U][s_st->txt_col++] = c;
    s_st->txt[s_st->txt_n - 1U][s_st->txt_col] = '\0';
}

static void pon_hex(uint8_t v)
{
    static const char k[16] = { '0','1','2','3','4','5','6','7',
                                '8','9','A','B','C','D','E','F' };
    pon_char(k[(v >> 4) & 0x0FU]);
    pon_char(k[v & 0x0FU]);
    pon_char(' ');
}

/*
 * UN BIT DECODIFICADO.
 *
 * El EOM del estandar es el numero de ocho digitos 4B65A5B2 con el bit mas
 * significativo primero (5.3.2.3.1), asi que se busca en un registro de 32
 * bits que va corriendo. Es la unica marca del tramo de datos que no depende
 * de suponer nada del formato de los caracteres.
 *
 * El texto se junta de ocho en ocho bits con el MENOS significativo primero,
 * que es el ASCII sincrono con el que se valido la cadena del 4285. Aqui no
 * esta confirmado en el aire -la grabacion que hay va a relleno- y por eso
 * tambien esta el modo HEX, que no supone nada.
 */
static void pon_bit(uint8_t b)
{
    s_st->eomreg = (s_st->eomreg << 1) | (uint32_t)(b & 1U);
    if (s_st->eomreg == 0x4B65A5B2UL) {
        s_st->est.eom++;
        /*
         * EL FINAL DE LA EMISION, dicho por el estandar y no por una cuenta
         * atras inventada. Detras del EOM van 144 bits de vaciado "mas los
         * que hagan falta para completar el bloque del entrelazador que
         * contiene el ultimo de ellos" (5.3.2.3.2). 144 cabe de sobra en un
         * bloque, asi que con terminar el que va y uno mas esta todo.
         *
         * Sin esto el modulo se queda desensanchando ruido para siempre
         * cuando la emision acaba, y eso ensucia las cuentas de la pantalla
         * -y en el banco salia como "desalineos" que no eran tales-.
         */
        if (s_st->fin == 0U) { s_st->fin = 2U; }
        pon_char('\n');
        pon_char('['); pon_char('E'); pon_char('O'); pon_char('M'); pon_char(']');
        pon_char('\n');
        s_st->an = 0U; s_st->areg = 0U;
        return;
    }
    s_st->areg = (uint16_t)((s_st->areg >> 1) | (b ? 0x80U : 0U));
    s_st->an++;
    if (s_st->an < 8U) { return; }
    s_st->an = 0U;
    {
        uint8_t v = (uint8_t)(s_st->areg & 0xFFU);
        s_st->areg = 0U;
        if (s_st->est.formato == S4415_HEX) {
            pon_hex(v);
        } else if (v == 0x0AU || v == 0x0DU) {
            pon_char('\n');
        } else if (v >= 0x20U && v < 0x7FU) {
            pon_char((char)v);
        } else {
            pon_char('.');
        }
    }
}

/* --- el desentrelazador y el Viterbi ------------------------------------- */

/*
 * UN BLOQUE ENTERO. Entra `n` bits blandos en el orden en que salieron del
 * aire -que es el orden de LECTURA del entrelazador del transmisor- y sale
 * texto.
 *
 * Deshacer el entrelazado es ir de la posicion de lectura k a la de carga m:
 *
 *      fila    r = k mod F
 *      columna c = (k/F - 7*r) mod C      <- bajada de 7, no de 17
 *      carga   m = c*F + (3*r) mod F      <- 3 es el inverso de 7
 *
 * Y detras el Viterbi del 4285/HFDL, que es el mismo codigo: K=7, rate 1/2,
 * generadores x6+x4+x3+x+1 y x6+x5+x4+x3+1 (figura 4 del estandar), que son
 * los 133/171 en octal de toda la vida.
 */
static void bloque_fec(void)
{
    hfdl_viterbi_t v;
    uint16_t n = s_st->blando_n;
    uint16_t filas = s_st->est.largo ? ENT_FIL_L : ENT_FIL_C;
    uint16_t cols  = s_st->est.largo ? ENT_COL_L : ENT_COL_C;
    uint16_t k, nb;
    uint32_t mejor = 0U, mmin = 0xFFFFFFFFUL, e;

    if (n != (uint16_t)(filas * cols)) { s_st->blando_n = 0U; return; }

    for (k = 0U; k < n; k++) {
        uint16_t r = (uint16_t)(k % filas);
        uint16_t c = (uint16_t)(((k / filas) + cols * ENT_SALTO - ENT_SALTO * r) % cols);
        uint16_t m = (uint16_t)(c * filas + (ENT_INV * r) % filas);
        s_st->desent[m] = s_st->blando[k];
    }

    nb = (uint16_t)(n / 2U);     /* bits de datos: el codigo es rate 1/2 */
    if (!hfdl_viterbi_init(&v, &s_st->vma, &s_st->vmb, s_st->vdec,
                           (uint32_t)(SETS_MAX + 6U), s_st->vest)) {
        s_st->blando_n = 0U; return;
    }
    if (!hfdl_viterbi_update_block(&v, s_st->desent, (uint32_t)nb)) {
        s_st->blando_n = 0U; return;
    }
    /* El codigo NO se corta entre bloques: sigue. Asi que el estado final
     * mejor de este bloque es el estado inicial del siguiente, y no cero.
     * Metrica mas baja = mejor camino (es la convencion de libfec). */
    for (e = 0U; e < HFDL_VITERBI_NUM_STATES; e++) {
        uint32_t m = hfdl_viterbi_get_final_metric(&v, e);
        if (m < mmin) { mmin = m; mejor = e; }
    }
    hfdl_viterbi_chainback(&v, s_st->bits, (uint32_t)nb, mejor);
    s_st->vest = mejor;

    for (k = 0U; k < nb; k++) {
        pon_bit((uint8_t)((s_st->bits[k >> 3] >> (7U - (k & 7U))) & 1U));
    }
    s_st->est.bloques++;
    s_st->noticia = 1U;
    s_st->est.bits += nb;
    s_st->blando_n = 0U;
    if (s_st->fin > 0U) {
        s_st->fin--;
        if (s_st->fin == 0U) {
            s_st->en_datos = 0U;
            s_st->est.fin_tx = 1U;
        }
    }
}

/* Cierra un enganche pendiente leyendo D1, D2, C1, C2 y C3. */
static void cierra_pendiente(void)
{
    uint8_t d1, d2, c1, c2, c3, i;
    float q, qs = 0.0f;
    uint32_t t0 = s_st->t_pend;
    float dps = s_st->dps_pend;

    d1 = lee_sim(t0 +  9U * SIM_MUE, dps, &q); qs += q;
    d2 = lee_sim(t0 + 10U * SIM_MUE, dps, &q); qs += q;
    c1 = lee_sim(t0 + 11U * SIM_MUE, dps, &q); qs += q;
    c2 = lee_sim(t0 + 12U * SIM_MUE, dps, &q); qs += q;
    c3 = lee_sim(t0 + 13U * SIM_MUE, dps, &q); qs += q;

    /*
     * REVISION A FONDO DEL 08/10/2026: SI D1/D2 NO CASAN, LA LECTURA NO
     * VALE Y NO SE TOCA NADA.
     *
     * El bucle de abajo buscaba la fila de la tabla XV y, si no la
     * encontraba, se iba tal cual: bps_idx y largo se quedaban con los
     * del segmento ANTERIOR y nadie se enteraba. Con un desvanecimiento
     * justo encima de D1/D2 -que es lo normal en estas bandas, son dos
     * simbolos de 416 us- el entrelazado que quedaba puesto era el de
     * antes y el desentrelazado salia cruzado durante BLOQUES ENTEROS,
     * 4,8 segundos cada uno, sin una sola señal de que algo iba mal.
     *
     * D1 y D2 son los dos unicos valores del segmento que no se pueden
     * comprobar contra nada -la cuenta de C1..C3 si se comprueba, baja de
     * uno en uno-, asi que lo unico honrado es tirar el segmento entero:
     * se guardan los dos simbolos crudos para poder discutirlo y se
     * vuelve sin cambiar el estado. El siguiente segmento llega en 200 ms
     * y trae los mismos D1/D2.
     */
    {
        uint8_t casa = 0U;
        for (i = 0U; i < (uint8_t)(sizeof k_txv / sizeof k_txv[0]); i++) {
            if (k_txv[i][0] == d1 && k_txv[i][1] == d2) {
                s_st->est.bps_idx = k_txv[i][2];
                s_st->est.largo   = (uint8_t)(d1 < 6U);
                casa = 1U;
                break;
            }
        }
        if (!casa) {
            s_st->est.d1 = d1; s_st->est.d2 = d2;
            s_st->pend = 0U;
            return;
        }
    }
    /* Tabla XVI: C1..C3 llevan dos bits cada uno con un "1" delante, o sea
     * que el simbolo va de 4 a 7 y los dos bits son simbolo-4. */
    s_st->est.cuenta   = (uint8_t)(((c1 & 3U) << 4) | ((c2 & 3U) << 2) | (c3 & 3U));
    s_st->est.corr     = (uint8_t)(s_st->corr_pend * 100.0f);
    s_st->est.calidad  = (uint8_t)((qs / 5.0f) * 100.0f);
    s_st->est.engancha = 1U;
    s_st->est.segmentos++;
    s_st->est.d1 = d1; s_st->est.d2 = d2;

    /* El ultimo segmento es el de cuenta cero: los datos empiezan detras. */
    if (s_st->est.cuenta == 0U) {
        s_st->en_datos = 1U;
        s_st->t_dat    = t0 + SEG_MUE;
        s_st->dps_dat  = dps;
        s_st->scr_i    = 0U;
        s_st->blando_n = 0U;
        s_st->sets     = 0U;
        s_st->exc_n    = 0U;
        s_st->fin      = 0U;
        s_st->vest     = 0U;   /* el codificador arranca en cero */
    }
    s_st->pend = 0U;
}

/*
 * *** EL DUEÑO, 07/10/2026: "algo ha pasado en el modo stanag que deja la
 * radio colgada y me cuesta mucho salir de el". ***
 *
 * Y LO QUE PASO FUE ESTO, QUE ESTABA AVISADO POR ESCRITO.
 *
 * Lo que devuelve esta funcion no es "he trabajado": es "HAY NOTICIA". El
 * que llama -stanag_modo_poll()- sube con ella la cuenta de medidas, y main.c
 * repinta el panel ENTERO cada vez que esa cuenta cambia.
 *
 * La primera version devolvia 1 por cada dibit desensanchado. Son SETENTA Y
 * CINCO POR SEGUNDO. O sea setenta y cinco repintados del panel por segundo:
 * el bucle principal se queda sin aire, los botones tardan en contestar y la
 * radio parece colgada. Exactamente el circulo que stanag_modo.c ya tiene
 * descrito en su propio comentario -"cuanto mas miraba la pantalla, menos
 * enganchaba"- y que ya se pago una vez con el 4285.
 *
 * Noticia es: un segmento de preambulo leido entero, o un bloque del
 * entrelazador decodificado. Lo primero pasa cinco veces por segundo durante
 * el preambulo y luego nunca mas; lo segundo, una vez cada 4,8 segundos.
 * Desensanchar un dibit NO es noticia: no cambia nada que se pueda leer en
 * una pantalla.
 *
 * Y el banco lo mide: sim/s4415test.c cuenta cuantas veces se dice que hay
 * noticia por segundo de audio y falla si pasa de diez. Un banco que solo
 * mira si acierta no habria visto esto nunca.
 */
uint8_t s4415_paso(void)
{
    uint8_t algo = 0U;

    if (!s_on || s_st == 0) { return 0U; }

    /* 0. moler el audio que dejo la interrupcion */
    muele();

    /* 1. cerrar el enganche anterior si ya han llegado D1..C3 */
    if (s_st->pend && s_st->n >= (s_st->t_pend + HASTA_C3 + S4415_SPS)) {
        cierra_pendiente();
        algo = 1U;
    }

    /* 2. desensanchar si estamos en datos */
    while (s_st->en_datos &&
           s_st->n >= (s_st->t_dat + SIM_MUE + S4415_SPS)) {
        uint8_t m = 0U, bl[2];
        uint8_t g = lee_set(s_st->t_dat, s_st->dps_dat, s_st->scr_i, bl, &m);
        uint16_t tope = (uint16_t)(s_st->est.largo
                        ? (ENT_FIL_L * ENT_COL_L) : (ENT_FIL_C * ENT_COL_C));

        mete_dibit((uint8_t)(g & 3U));
        s_st->est.margen = m;

        if (s_st->blando_n + 2U <= tope) {
            s_st->blando[s_st->blando_n++] = bl[0];
            s_st->blando[s_st->blando_n++] = bl[1];
        }

        /*
         * EL SET EXCEPCIONAL CIERRA EL BLOQUE, y es el estandar quien lo dice:
         * "every 360th set (following the end of sync pattern) if long
         * interleave". O sea el ultimo de los 360.
         *
         * El bloque NO se alinea con el: se alinea con el preambulo, que ya
         * dice exactamente donde empiezan los datos, y asi no se pierde el
         * primer bloque -4,8 segundos- esperando a ver una marca. El set
         * excepcional se usa para COMPROBARLO, que es mejor uso: si cae donde
         * toca, el alineamiento es bueno; si cae en otro sitio, es que se ha
         * perdido el paso y hay que volver a empezar ahi mismo.
         *
         * Las dos cuentas salen en el estado a proposito. Un receptor que
         * decodifica siempre y nunca dice si la marca cuadraba esta tapando
         * la unica prueba que tiene.
         */
        if (g >= 4U) {
            if (s_st->exc_n < 4U) { s_st->exc_sitio[s_st->exc_n] = s_st->sets; }
            /*
             * REVISION A FONDO DEL 08/10/2026: TOPADO EN 255.
             *
             * exc_n es uint8_t y se subia sin tope. Los sets excepcionales
             * caen uno por bloque del entrelazador, o sea uno cada 4,8
             * segundos: a los veinte minutos y pico de recepcion seguida
             * el contador daba la vuelta a 0 y el panel decia "0
             * excepcionales" con la emision entrando perfectamente -y 0
             * excepcionales es justo lo que se mira para sospechar que el
             * alineamiento no cuadra-. Mentir hacia abajo es peor que
             * saturar: saturado dice "muchos", que es verdad.
             */
            if (s_st->exc_n < 255U) { s_st->exc_n++; }
            s_st->est.excepcionales = s_st->exc_n;
        }
        if (s_st->blando_n >= tope) {
            /* El bloque se cierra por la cuenta del preambulo. Si el ultimo
             * set era el excepcional, la marca cuadra; si no, se cuenta. */
            if (g < 4U) { s_st->est.sin_marca++; }
            bloque_fec();
        } else if (g >= 4U) {
            /*
             * Un set excepcional en medio de un bloque. Se CUENTA y no se
             * hace nada mas, a proposito: con un desvanecimiento un set
             * normal se puede leer como excepcional, y tirar el bloque por
             * una sola lectura seria perder 4,8 segundos de datos buenos por
             * un error de uno entre mil. Si el alineamiento estuviera mal de
             * verdad, esta cuenta y `sin_marca` subirian las dos a la vez y
             * se veria en la pantalla.
             */
            s_st->est.desalineos++;
        }

        s_st->sets++;
        s_st->t_dat += SIM_MUE;
        s_st->scr_i  = (uint16_t)((s_st->scr_i + S4415_SET) % S4415_SCR_DAT);
        /* y aqui NO se dice que hay noticia: ver la cabecera de la funcion */
    }

    if (s_st->noticia) { s_st->noticia = 0U; algo = 1U; }

    /* 3. barrer el preambulo. Hace falta la plantilla entera por delante. */
    if (s_st->n < PLANT_MUE) { return algo; }

    /*
     * Si el barrido se ha quedado tan atras que el anillo ya se piso, se
     * salta hacia delante y se cuenta. Callarlo seria mentir en el estado.
     *
     * La primera condicion no sobra: despues de un enganche `visto` salta a
     * 200 ms por delante, o sea POR DELANTE DE `n`, y la resta de enteros sin
     * signo daria cuatro mil millones de muestras perdidas. Eso lo cazo el
     * banco en la primera pasada.
     */
    if (((s_st->visto + PLANT_MUE) < s_st->n) &&
        ((s_st->n - s_st->visto) > (uint32_t)(RING - HASTA_C3 - 64U))) {
        uint32_t objetivo = s_st->n - PLANT_MUE;
        s_st->perdidas += objetivo - s_st->visto;
        s_st->visto = objetivo;
    }

    while ((s_st->visto + PLANT_MUE) <= s_st->n) {
        uint32_t t0 = s_st->visto;

        if (s_st->pend) { break; }      /* uno cada vez */

        if (corr_trozo0(t0) >= UMBRAL_1) {
            float dps = 0.0f;
            float c = corr_plantilla(t0, &dps);
            if (c >= UMBRAL) {
                /* afinar: el pico tiene anchura, quedarse con el mejor de +-4 */
                uint32_t mejor = t0; float cmej = c, dmej = dps;
                int8_t k;
                for (k = -4; k <= 4; k++) {
                    uint32_t tt;
                    float cc, dd;
                    if (k == 0) { continue; }
                    if ((int32_t)t0 + k < 0) { continue; }
                    tt = (uint32_t)((int32_t)t0 + k);
                    if ((tt + PLANT_MUE) > s_st->n) { continue; }
                    cc = corr_plantilla(tt, &dd);
                    if (cc > cmej) { cmej = cc; mejor = tt; dmej = dd; }
                }
                s_st->pend      = 1U;
                s_st->t_pend    = mejor;
                s_st->dps_pend  = dmej;
                s_st->corr_pend = cmej;
                s_st->hay       = 1U;
                s_st->t_ult     = mejor;
                /* el siguiente segmento esta 200 ms despues: no hace falta
                 * mirar lo de en medio, y ademas asi el pico no se cuenta
                 * dos veces */
                s_st->visto = mejor + SEG_MUE - 40U;
                continue;
            }
        }
        s_st->visto++;
    }
    return algo;
}

void s4415_estado(s4415_est_t *out)
{
    if (out == 0) { return; }
    if (s_st == 0) { memset(out, 0, sizeof *out); return; }
    *out = s_st->est;
}

uint32_t s4415_perdidas(void)
{
    return (s_st == 0) ? 0U : s_st->perdidas;
}

/* Muestras que la cola tuvo que tirar porque el bucle principal fue tarde.
 * Si esto no es cero, lo que salga detras no es de fiar y hay que decirlo. */
uint32_t s4415_tiradas(void)
{
    return (s_st == 0) ? 0U : s_st->tiradas;
}

/* Limpiar lo que hay en pantalla SIN reiniciar el modo. La primera version
 * llamaba a recoloca() desde stanag_modo_borra(), que para y vuelve a
 * arrancar: el boton Borrar tiraba el enganche entero. */
void s4415_borra(void)
{
    uint8_t k;
    if (s_st == 0) { return; }
    for (k = 0U; k < S4415_LINEAS; k++) { s_st->txt[k][0] = '\0'; }
    s_st->txt_n = 0U; s_st->txt_col = 0U;
    s_st->areg = 0U; s_st->an = 0U;
}

void s4415_formato(uint8_t f)
{
    if (s_st == 0) { return; }
    s_st->est.formato = (uint8_t)((f < S4415_FORMATOS) ? f : S4415_ASCII);
}

uint8_t s4415_nlineas(void) { return (s_st == 0) ? 0U : s_st->txt_n; }

const char *s4415_linea(uint8_t i)
{
    if (s_st == 0 || i >= s_st->txt_n) { return ""; }
    return s_st->txt[i];
}

uint16_t s4415_excepcional(uint8_t i)
{
    if (s_st == 0 || i >= 4U || i >= s_st->exc_n) { return 0xFFFFU; }
    return (uint16_t)s_st->exc_sitio[i];
}

uint16_t s4415_saca_dibits(uint8_t *out, uint16_t max)
{
    uint16_t n = 0U;
    if (s_st == 0 || out == 0) { return 0U; }
    while (s_st->dib_sale != s_st->dib_n && n < max) {
        out[n++] = s_st->dib[s_st->dib_sale];
        s_st->dib_sale = (uint16_t)((s_st->dib_sale + 1U) % (uint16_t)(sizeof s_st->dib));
    }
    return n;
}
