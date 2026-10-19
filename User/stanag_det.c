#include "stanag_det.h"

#include <math.h>

/* Ver stanag_det.h para el porque de todo esto. Aqui solo esta el como. */

#define DIEZMA      4U      /* 12 kHz -> 3 kHz complejos */
#define NFFT        (2U * STANAG_DET_N)   /* con relleno de ceros: lineal, no circular */
#define CADA        256U    /* muestras nuevas entre medida y medida */
#define RETARDO_MIN 30U     /* 10 ms: por debajo es la propia señal, no su trama */
/*
 * Hasta donde se mira. Con relleno de ceros, el retardo r solo tiene
 * (N - r) muestras solapadas: al final del todo quedan cuatro gatos y el
 * resultado es puro ruido por pocas muestras. Tres cuartos es donde
 * todavia hay un cuarto del anillo correlacionandose.
 */
#define RETARDO_MAX ((STANAG_DET_N * 3U) / 4U)
#define BASE_VENT   65U     /* ventana de la linea base, impar para que tenga centro */
/*
 * El factor sobre la desviacion tipica. LimeAuto usa 1,2 y aqui hacen
 * falta 3, y no es que su numero este mal: es que buscamos cosas
 * distintas. El MIRA EN SITIOS CONOCIDOS -comprueba si hay pico justo en
 * la muestra 5333, donde sabe que lo tendria un 4285- y con eso 1,2 le
 * sobra, porque la probabilidad de que el ruido ponga un pico justo ahi es
 * baja. Nosotros buscamos A CIEGAS en setecientos retardos, asi que el
 * maximo del ruido entre setecientos intentos pasa 1,2 sigmas casi
 * siempre. El banco lo enseña sin discusion: con 1,2, ruido blanco puro
 * se detectaba como trama de 127 ms.
 */
#define QECART      3.0f
#define PROMEDIOS   5U      /* sus cinco pasadas de integracion */

/*
 * LAS MEDIDAS DEL ESPECTRO - 03/10/2026. Ver la cabecera.
 *
 * Hercios por casilla de la transformada: 3000 Hz de banda compleja
 * repartidos en NFFT casillas. Con N=1024 y NFFT=2048 son 1,46 Hz por
 * casilla, aunque la resolucion DE VERDAD es el doble -3000/1024 = 2,93 Hz-
 * porque la mitad del relleno son ceros. Se interpola, no se inventa.
 */
#define HZ_CASILLA   ((float)STANAG_DET_HZ / (float)NFFT)
/*
 * DOS RAYAS MAS JUNTAS QUE ESTO SON LA MISMA RAYA. Tiene que ser mayor que
 * 2*TONO_ANCHO o las dos ventanas de potencia se solapan y una portadora
 * con su propia falda se cuenta como dos tonos: medido en la grabacion de
 * portadora del usuario, la "segunda raya" salia a 72 Hz de la primera.
 *
 * El precio es que las FSK de menos de 120 Hz de salto -la MD-674 militar
 * de los sesenta va a 85- quedan fuera. No se pueden tener las dos cosas
 * con ventanas de +-50 Hz, y de esas dos cosas la que importa es no decir
 * que un heterodino es una FSK.
 */
#define TONO_SEP_MIN 120.0f
/*
 * LA SEGUNDA RAYA YA NO SE MIDE CONTRA LA PRIMERA - 06/10/2026.
 *
 * Pedido por el usuario.
 *
 * Los dos criterios que miraban las rayas POR SEPARADO -que la segunda no
 * cayera mas de 10 dB bajo la primera, y que cada una llevara el 12 % de
 * la potencia- daban por hecho que los dos tonos llegan parejos. Y eso es
 * cierto EN EL TRANSMISOR. Despues de 2.000 km de ionosfera, no: los dos
 * tonos van separados 850 Hz, los desvanece por su cuenta, y uno llega 11
 * dB mas flojo que el otro. Un minuto despues, al reves. Por eso "iba
 * variando": cuando el desvanecimiento los juntaba, el detector si veia la
 * FSK.
 *
 * Medido sobre la grabacion de 5339: caida 11,1 dB (el limite eran 10) y la
 * raya baja con el 4,8 % (el minimo era 12). Fallaba por los dos, y pasaba
 * de sobra el que mira las dos JUNTAS: 78,1 % contra un minimo de 35.
 *
 * LO QUE SE MIDE AHORA: cuanto destaca CADA raya sobre el ruido DE SU
 * PROPIO ENTORNO. Eso no lo toca el desvanecimiento -sube y baja la raya y
 * su ruido a la vez- y distingue lo que hay que distinguir: una raya de FSK
 * desvanecida sigue sobresaliendo de lo que tiene al lado; una cresta de
 * ruido, por definicion, no.
 *
 * Medido sobre las diez grabaciones del banco (mediana de las ventanas):
 *
 *   SON dos tonos                     NO lo son
 *     sitor de Oostende ... 26,1        stanag 4285 ..... 6,4
 *     4481 FSK ............ 25,6        4481 PSK ........ 6,3
 *     navtex de 8424 ...... 17,7        4415 ............ 4,7
 *     el de 5339 .......... 10,8        4529 ............ 4,3
 *                                       4538 ............ 4,0
 *                                       4539 ............ 2,9
 *
 * Entre 6,4 y 10,8 hay un hueco limpio. El corte va en 9 dB, con margen
 * por los dos lados. Lo ata sim/tonos_aire.c, que pasa las diez.
 */
#define TONO_REALCE   9.0f    /* dB que cada raya saca al ruido de al lado */
#define TONO_ENTORNO  300.0f  /* hasta donde se mira ese ruido */
#define TONO_HUECO     75.0f  /* y lo que se deja fuera, por ser la raya */
#define TONO_VALLE      6.0f  /* dB que tiene que bajar entre las dos rayas */
#define TONO_CAIDA   100.0f   /* puerta floja: 20 dB. El realce es quien decide */
#define PICO_VENT_HZ 8.0f     /* el +-8 Hz del "esto es una portadora" */
#define TONO_ANCHO   50.0f    /* cuanto se le cuenta a cada raya, a cada lado */
#define TONO_PARTE   0.35f    /* y que parte de la potencia tienen que ser */
/* Cuantas rayas se listan al contar señales. Seis son tres FSK a la vez, que
 * es lo que trajo el usuario; con ocho hay sitio para cuatro y de sobra. Mas
 * que eso ya no es "varias señales", es una banda llena y no hay nada que
 * identificar. Ver el bloque de "CUANTAS SEÑALES HAY" en det_espectro(). */
#define DET_TONOS_MAX  8U
/*
 * Y EL SALTO MAS ANCHO QUE SE PUBLICA - 10/10/2026.
 *
 * No es un numero elegido: sale de la tabla de ident.c. El salto mas ancho
 * que cualquiera de sus firmas puede usar son los 900..1100 Hz del APRS
 * (1200/2200); las demas van de 150 a 920. Asi que dos rayas separadas mas
 * de eso NO PUEDEN SER UNA FSK CONOCIDA: son dos señales distintas, y
 * publicarlas como un par es publicar un numero que ninguna fila puede
 * aprovechar y que ademas tapa a las filas de "sin dos tonos claros".
 *
 * Esto es lo que quedaba del caso del usuario: con tres RTTY y
 * desvanecimiento, hay ventanas en las que solo sobreviven DOS rayas y son
 * de señales distintas -la de 542 de una y la de 2222 de otra-. Agrupar no
 * las separa, porque con dos rayas no hay nada que agrupar. El tope si.
 *
 * Medido sobre la grabacion: 41 de 113 ventanas publicaban saltos de 1600 a
 * 1999 Hz, y arrastraban la mediana.
 *
 * sim/identtest.c comprueba que ninguna fila de la tabla pida mas de esto,
 * para que el dia que entre una firma de salto mas ancho salte el banco en
 * vez de quedarse muda.
 */
#define TONO_SEP_MAX 1200.0f
/* TONO_MIN_UNO se retiro el 06/10/2026: era un minimo ABSOLUTO de potencia
 * por raya, y el desvanecimiento selectivo de la ionosfera lo hace inutil.
 * Lo sustituye TONO_REALCE, que mide cada raya contra su propio ruido. */

/*
 * EL HISTOGRAMA DE RACHAS VIVE EN LA COLA DE s_acf, Y ESO HAY QUE DECIRLO.
 *
 * s_acf tiene STANAG_DET_N = 1024 casillas pero la autocorrelacion solo usa
 * las RETARDO_MAX = 768 primeras: de la 768 a la 1023 no las toca nadie.
 * Ahi van las 256 del histograma, que se borra en cada pasada. Es memoria
 * prestada dentro de memoria prestada, y la alternativa era pedir 512 bytes
 * estaticos cuando al enlazador le sobran ocho.
 *
 * Si algun dia RETARDO_MAX sube, esto se pisa. Por eso el _Static_assert.
 */
/*
 * ==========================================================================
 * LA VELOCIDAD DE SIMBOLO DE LO QUE NO SON DOS TONOS - 05/10/2026
 * ==========================================================================
 *
 * Pedido por el usuario.
 *
 * El medidor de velocidad que ya habia cuenta rachas del discriminador y
 * solo arranca cuando se han visto DOS tonos. Con veintiuno no arranca, y
 * sin velocidad la unica medida que quedaba era el ancho, que en un MFSK no
 * dice casi nada: dos modos del mismo ancho con distinto numero de tonos no
 * son la misma señal.
 *
 * POR QUE NO SE MIDE EL PEINE DE TONOS, que fue lo primero que intente.
 * Porque en un MFSK ortogonal -y lo son casi todos- la separacion entre
 * tonos ES el inverso de la duracion del simbolo, asi que el lobulo de cada
 * tono mide exactamente lo que hay hasta el de al lado: sumados a lo largo
 * del tiempo encajan unos con otros y el espectro medio sale PLANO. El
 * peine solo existe en el instante, y en el instante solo hay ocho tonos de
 * los veintiuno. Promediar espectros no lo saca, y promediar sus
 * autocorrelaciones tampoco: la ventana de este motor son 341 ms, ocho
 * simbolos, y ocho lineas de 23 Hz de ancho separadas 23 Hz no son un
 * peine, son una meseta.
 *
 * LO QUE SI SE VE, Y ES MEJOR: CUANDO CAMBIA DE TONO. Si se multiplica la
 * señal por su propia copia retrasada W muestras, el resultado tiene fase
 * constante mientras el tono no cambie -da igual CUAL sea el tono- y se
 * desordena en el salto. Sumando W de esos productos y mirando el modulo
 * sale una curva que vale casi uno dentro de cada simbolo y cae en cada
 * frontera. Autocorrelando esa curva, el primer pico es la duracion del
 * simbolo.
 *
 * Y es barato: la suma deslizante se actualiza en una resta y una suma por
 * muestra, sin una sola transformada.
 *
 * ALCANCE: de 10 a 150 baudios. Por arriba lo limita W -por debajo de W
 * muestras por simbolo la coherencia no llega a asentarse- y por abajo, la
 * ventana. Ahi dentro caben el RTTY (45,45 y 50), el SITOR y el NAVTEX
 * (100), el ALE 2G (125), el PSK31 (31,25) y toda la familia Olivia
 * (23-31). Los STANAG y el HFDL van a 1800 y 2400 y se salen por arriba,
 * pero a esos los identifica el periodo de la trama, que es otra medida.
 */
#define SIMB_W     16U     /* muestras de la correlacion deslizante */
#define SIMB_LMIN  20U     /* 6,7 ms: 150 baudios */
#define SIMB_LMAX  300U    /* 100 ms: 10 baudios */
/*
 * 0,85 LA PRIMERA VEZ, Y NO SALIA. Con ese olvido la media efectiva son
 * siete ventanas -medio segundo- y en medio segundo el ruido pesa tanto
 * como el simbolo: el pico salia en el retardo 32, que no es ningun
 * simbolo sino el doble de la ventana deslizante. Con 0,98 la media son
 * cincuenta ventanas, unos cuatro segundos, y el pico aparece donde tiene
 * que aparecer. Lo que cuesta es la espera, y una identificacion que tarda
 * cuatro segundos sigue siendo una identificacion.
 */
#define SIMB_OLVIDO 0.98f
/* Cuantos productos lleva CADA retardo, el mismo numero para todos. Ver
 * el bloque grande dentro de simb_acumula(). Tiene que caber para el
 * retardo mas largo: STANAG_DET_N - 2*SIMB_W - SIMB_LMAX = 692. */
#define SIMB_PROD   480U

/*
 * EL REPARTO TIENE QUE CABER, Y LA PRIMERA VEZ NO CABIA. s_simbac se salia
 * 45 floats por el final y lo que pisaba era el cuaderno de la cadencia del
 * modo IDENT, que vive justo detras en el mismo prestamo: el FT8 dejo de
 * reconocerse y nada dijo nada. Esto lo caza en la compilacion.
 */
_Static_assert((2U * STANAG_DET_N) + (2U * NFFT) + STANAG_DET_N + (NFFT / 2U)
               + STANAG_DET_N + (SIMB_LMAX + 1U) <= STANAG_DET_FLOATS,
               "STANAG_DET_FLOATS se ha quedado corto para el reparto");

#define HIST_N      256U

/* El peine de la velocidad; ver el bloque gordo dentro de mide_rachas(). */
#define PEINE_TMIN   18U    /* muestras por simbolo: 167 Bd a 3 kHz */
#define PEINE_TMAX  180U    /* ... 16,7 Bd, y dos puas en 256 casillas */
#define PEINE_MINIMO 0.12f  /* cuanto tiene que superar a la casualidad */
#define HIST_BASE   RETARDO_MAX
_Static_assert(HIST_BASE + HIST_N <= STANAG_DET_N,
               "el histograma de rachas se sale de la cola de s_acf");

/* De casilla de la transformada a hercios de AUDIO. Las casillas de la
 * mitad de arriba son frecuencias negativas: el oscilador bajo la señal
 * desde 1500 Hz, asi que la casilla 0 ES 1500 Hz de audio. */
static float casilla_a_audio(uint32_t k)
{
    float f = (k < (NFFT / 2U)) ? (float)k : ((float)k - (float)NFFT);
    return 1500.0f + f * HZ_CASILLA;
}

/* El reparto del bloque prestado. Ver STANAG_DET_FLOATS en la cabecera. */
static float *s_anillo;     /* 2*STANAG_DET_N, complejo intercalado */
static float *s_fft;        /* 2*NFFT */
static float *s_acf;        /* STANAG_DET_N */
static float *s_cos;        /* NFFT/2 */
static float *s_coh;        /* STANAG_DET_N: la coherencia muestra a muestra */
static float *s_simbac;     /* SIMB_LMAX+1: su autocorrelacion PROMEDIADA */

/*
 * LA RELACION CONSTANTE DE 7 BITS. El porque, con los numeros medidos,
 * esta en stanag_det.h; aqui solo esta el como.
 *
 * Tres candidatos, y son estos tres porque a 3 kHz dan un numero ENTERO de
 * muestras por bit: 60, 40 y 30. Con eso el indice de bit es una division
 * entera del indice absoluto de muestra, o sea una FUNCION PURA de donde
 * esta la muestra, sin acumulador y sin deriva.
 *
 * Y tiene que ser una funcion pura del indice ABSOLUTO, no de la posicion
 * en el anillo, y esto es el unico punto fino de todo esto: el anillo
 * avanza CADA = 256 muestras entre medida y medida, y un bit a 50 baudios
 * son 60. 256/60 = 4,27, o sea que si el desplazamiento de grupo se cuenta
 * desde el principio del anillo, cada pasada lo cuenta desde un origen
 * distinto y los siete desplazamientos se embarran unos con otros. Que es
 * exactamente perder el metodo: todo esto vale porque UNO de los siete
 * gana y los otros seis se quedan en el 27% del azar.
 *
 * Las ventanas se solapan cuatro veces (1024 de anillo, 256 nuevas), asi
 * que cada bit se cuenta cuatro veces. No molesta: se cuenta cuatro veces
 * arriba y cuatro veces abajo, y lo que se publica es el cociente. Lo que
 * si cambia es cuanto tarda en haber bastante, y de ahi sale CR7_MINIMO.
 */
#define CR7_N       3U
static const uint32_t k_cr7_T[CR7_N]      = { 60U, 40U, 30U };
static const uint16_t k_cr7_bd_x10[CR7_N] = { 500U, 750U, 1000U };
#define CR7_OLVIDO  0.97f   /* memoria 1/(1-0,97) = 33 ventanas = 2,8 s */
#define CR7_MINIMO  40.0f   /* peso acumulado por casilla antes de publicar */

/* Unos que tiene cada patron de 7 bits. Tabla y no bucle porque esto se
 * llama una vez por bit y por candidato. */
static const uint8_t k_popc7[128] = {
    0,1,1,2,1,2,2,3, 1,2,2,3,2,3,3,4, 1,2,2,3,2,3,3,4, 2,3,3,4,3,4,4,5,
    1,2,2,3,2,3,3,4, 2,3,3,4,3,4,4,5, 2,3,3,4,3,4,4,5, 3,4,4,5,4,5,5,6,
    1,2,2,3,2,3,3,4, 2,3,3,4,3,4,4,5, 2,3,3,4,3,4,4,5, 3,4,4,5,4,5,5,6,
    2,3,3,4,3,4,4,5, 3,4,4,5,4,5,5,6, 3,4,4,5,4,5,5,6, 4,5,5,6,5,6,6,7
};

/*
 * Y CUATRO FASES DE MUESTREO POR BIT, QUE NO SOBRAN - 08/10/2026.
 *
 * La primera version muestreaba a UNA fase, la del centro del bit
 * contado desde el indice absoluto. Pero ese indice no sabe nada del
 * reloj del transmisor, asi que el punto de muestreo cae donde cae
 * dentro del bit. Medido con el motor de verdad sobre cinco
 * grabaciones, con una sola fase:
 *
 *   16911   75%    SITOR-B  85%    NAVTEX  75%    Baudot 34%   4481 20%
 *
 * Separa, pero el 75 contra un umbral de 70 son CINCO PUNTOS, y en este
 * mismo fichero esta escrito lo que vale un margen del seis por ciento:
 * lo decide el desvanecimiento de turno. Fuera de la radio, con
 * recuperacion de reloj de verdad, los mismos tres daban 99, 100 y 93.
 * Lo que faltaba era el reloj, no el metodo.
 *
 * En vez de recuperarlo se PRUEBA, que es lo que hace este fichero con
 * todo lo demas: cuatro puntos de muestreo repartidos por el bit -a
 * 1/8, 3/8, 5/8 y 7/8- y gana el que mejor sale. El peor caso deja el
 * punto bueno a T/8 del centro, que a 50 baudios son 7 muestras de 60,
 * menos de lo que ya alisa el propio discriminador.
 *
 * Cuesta 1008 bytes de .bss y unas 200 cuentas mas por ventana.
 */
#define CR7_F       4U
static float   s_cr7_t[CR7_N][CR7_F][7];  /* grupos vistos, con olvido */
static float   s_cr7_4[CR7_N][CR7_F][7];  /* ... los que tenian cuatro unos */
static float   s_cr7_3[CR7_N][CR7_F][7];  /* ... y tres: la otra polaridad */
static uint8_t s_cr7_reg[CR7_N][CR7_F];   /* los ultimos 7 bits */
static uint8_t s_cr7_hay[CR7_N][CR7_F];   /* cuantos lleva, hasta 7 */

static uint32_t s_w, s_desde, s_llenas;
static uint32_t s_rtot;     /* muestras ESCRITAS en el anillo, desde el init.
                             * Es el indice absoluto que necesita la CR7. */
static uint32_t s_simbn;    /* ventanas acumuladas en s_simbac */
static uint32_t s_dec;      /* cuantas van acumuladas del diezmado */
static float    s_dr, s_di; /* el acumulador del diezmado */
static uint8_t  s_osc;      /* fase del oscilador, 0..7 */
static uint8_t  s_promedios, s_listo, s_vale;
static uint32_t s_metidas;   /* muestras de audio que han entrado; ver _diag */
static uint32_t s_pasos;     /* ventanas procesadas; ver _diag */

static stanag_det_t s_r;

/*
 * El oscilador de 1500 Hz a 12 kHz son OCHO valores y se acabo. Al bajar
 * la señal a banda base hay que multiplicarla por exp(-j2*pi*1500*t), y a
 * 12 kHz eso recorre la circunferencia en ocho pasos exactos: 0, -45, -90
 * ... No hay acumulador de fase, no hay deriva y no hay senos que
 * calcular. Mismo truco que wspr_rx.c y jtty_rx.c.
 */
#define R2  0.70710678f
static const float k_osc_r[8] = { 1.0f,  R2,  0.0f, -R2, -1.0f, -R2,  0.0f,  R2 };
static const float k_osc_i[8] = { 0.0f, -R2, -1.0f, -R2,  0.0f,  R2,  1.0f,  R2 };

uint8_t stanag_det_init(float *trabajo, uint32_t n_floats)
{
    uint32_t i;

    s_vale = 0U;
    if (trabajo == 0 || n_floats < STANAG_DET_FLOATS) { return 0U; }

    s_anillo = &trabajo[0];
    s_fft    = &trabajo[2U * STANAG_DET_N];
    s_acf    = &trabajo[2U * STANAG_DET_N + 2U * NFFT];
    s_cos    = &trabajo[2U * STANAG_DET_N + 2U * NFFT + STANAG_DET_N];
    s_coh    = &trabajo[2U * STANAG_DET_N + 2U * NFFT + STANAG_DET_N + NFFT / 2U];
    s_simbac = &trabajo[2U * STANAG_DET_N + 2U * NFFT + STANAG_DET_N
                        + NFFT / 2U + STANAG_DET_N];

    for (i = 0U; i < (2U * STANAG_DET_N); i++) { s_anillo[i] = 0.0f; }
    for (i = 0U; i < STANAG_DET_N; i++) { s_acf[i] = 0.0f; }  /* y su cola */
    /* La tabla de giros: cos(2*pi*k/NFFT) para k < NFFT/2. El seno sale de
     * la misma tabla porque sen(x) = cos(x - pi/2) y el coseno es par:
     * sen(2*pi*k/NFFT) = tabla[|k - NFFT/4|]. Media tabla y ningun signo
     * que recordar. */
    for (i = 0U; i < (NFFT / 2U); i++) {
        s_cos[i] = cosf((2.0f * 3.14159265358979f * (float)i) / (float)NFFT);
    }

    s_w = 0U; s_desde = 0U; s_llenas = 0U; s_rtot = 0U;
    for (i = 0U; i < CR7_N; i++) {
        uint8_t f, o;
        for (f = 0U; f < CR7_F; f++) {
            for (o = 0U; o < 7U; o++) {
                s_cr7_t[i][f][o] = 0.0f;
                s_cr7_4[i][f][o] = 0.0f;
                s_cr7_3[i][f][o] = 0.0f;
            }
            s_cr7_reg[i][f] = 0U; s_cr7_hay[i][f] = 0U;
        }
    }
    s_dec = 0U; s_dr = 0.0f; s_di = 0.0f; s_osc = 0U;
    s_promedios = 0U; s_listo = 0U; s_metidas = 0UL; s_pasos = 0UL;
    s_r.vale = 0U; s_r.periodo_us = 0U; s_r.retardo = 0U;
    s_r.picos = 0U; s_r.calidad = 0U;
    s_r.hay_esp = 0U; s_r.hay_tonos = 0U; s_r.baudios_x10 = 0U; s_r.ciclo_pc = 0U;
    s_r.simb_x10 = 0U; s_r.tonos = 0U; s_r.simb_q = 0U;
    s_r.cr7_pc = 0U; s_r.cr7_bd_x10 = 0U;
    for (i = 0U; i <= SIMB_LMAX; i++) { s_simbac[i] = 0.0f; }
    s_simbn = 0U;
    s_vale = 1U;
    return 1U;
}

void stanag_det_mete(const float *audio, uint32_t n)
{
    uint32_t i;

    if (!s_vale) { return; }
    s_metidas += n;

    for (i = 0U; i < n; i++) {
        float v = audio[i];

        /* A banda base. */
        s_dr += v * k_osc_r[s_osc];
        s_di += v * k_osc_i[s_osc];
        s_osc = (uint8_t)((s_osc + 1U) & 7U);
        s_dec++;
        if (s_dec < DIEZMA) { continue; }

        /* Y diezmado: la suma de cuatro ES el filtro. Su primer cero cae
         * en 3 kHz, justo donde empieza lo que se doblaria encima. */
        s_anillo[2U * s_w]      = s_dr * 0.25f;
        s_anillo[2U * s_w + 1U] = s_di * 0.25f;
        s_dr = 0.0f; s_di = 0.0f; s_dec = 0U;

        s_w++; if (s_w == STANAG_DET_N) { s_w = 0U; }
        s_rtot++;
        if (s_llenas < STANAG_DET_N) { s_llenas++; }
        s_desde++;
        if (s_desde >= CADA && s_llenas >= STANAG_DET_N) {
            s_desde = 0U;
            s_listo = 1U;
        }
    }
}

/*
 * Transformada de NFFT puntos, en el sitio, radix-2. `inv` a 1 hace la
 * inversa (sin dividir por N: aqui todo se normaliza contra el retardo
 * cero, asi que la escala da igual y la division seria tirar ciclos).
 */
static void fft(float *z, uint8_t inv)
{
    uint32_t n = NFFT, i, j, paso;

    j = 0U;
    for (i = 1U; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) { j ^= bit; }
        j ^= bit;
        if (i < j) {
            float tr = z[2U*i], ti = z[2U*i+1U];
            z[2U*i] = z[2U*j]; z[2U*i+1U] = z[2U*j+1U];
            z[2U*j] = tr;      z[2U*j+1U] = ti;
        }
    }

    /*
     * ======================================================================
     * EL GIRO SE SACA UNA VEZ POR COLUMNA, NO UNA POR MARIPOSA - 19/10/2026
     * ======================================================================
     *
     * Pedido por el usuario: que la radio vaya mas rapido.
     *
     * Los dos bucles estaban al reves. El de dentro era el de `k`, que es
     * justo el que elige el giro, asi que `wr` y `wi` se volvian a calcular
     * en CADA mariposa: una multiplicacion para el indice, una comparacion
     * y una resta para el seno, y dos lecturas de tabla. Once etapas por
     * 1024 mariposas son 11.264 giros calculados por transformada.
     *
     * Dandole la vuelta a los dos bucles, el giro se calcula una vez por
     * columna y se reparte entre todas las mariposas que lo usan. Los giros
     * calculados pasan a ser 1 + 2 + 4 + ... + 1024 = 2.047: CINCO VECES Y
     * MEDIA menos. Las mariposas son las mismas, en el mismo orden dentro
     * de cada una, asi que el resultado es el mismo bit a bit - y lo
     * comprueban identtest, identgrab y stanagdet, que no se mueven ni un
     * acierto.
     *
     * Y LA PRIMERA ETAPA NO LLEVA GIRO: con paso 2 solo hay k = 0, o sea
     * w = 1. Se saca aparte y se queda en una suma y una resta, sin las
     * cuatro multiplicaciones. Son 1.024 mariposas de las 11.264.
     *
     * El salto entre elementos crece con la etapa, pero en esta radio la
     * RAM no tiene cache: una lectura separada cuesta lo mismo que una
     * seguida.
     */
    for (i = 0U; i < n; i += 2U) {
        float br = z[2U*i+2U], bi = z[2U*i+3U];
        z[2U*i+2U] = z[2U*i]    - br;
        z[2U*i+3U] = z[2U*i+1U] - bi;
        z[2U*i]    += br;
        z[2U*i+1U] += bi;
    }

    for (paso = 4U; paso <= n; paso <<= 1) {
        uint32_t medio = paso >> 1;
        uint32_t salto = n / paso;
        uint32_t k, t = 0U;

        for (k = 0U; k < medio; k++, t += salto) {
            float wr = s_cos[t];
            float ws = s_cos[(t >= (NFFT/4U)) ? (t - (NFFT/4U)) : ((NFFT/4U) - t)];
            float wi = inv ? ws : -ws;

            for (i = 0U; i < n; i += paso) {
                uint32_t a = i + k, b = a + medio;
                float xr = z[2U*b]*wr - z[2U*b+1U]*wi;
                float xi = z[2U*b]*wi + z[2U*b+1U]*wr;
                z[2U*b]    = z[2U*a]    - xr;
                z[2U*b+1U] = z[2U*a+1U] - xi;
                z[2U*a]    += xr;
                z[2U*a+1U] += xi;
            }
        }
    }
}

/*
 * EL ESPECTRO, EN SEIS NUMEROS.
 *
 * Una sola pasada saca la potencia total, el pico y los anchos; una
 * segunda, el par de tonos. Lo que NO se hace es guardar el espectro:
 * son 2048 casillas y no hay sitio, y ademas no hacen falta.
 *
 * EL ORDEN DE RECORRIDO NO ES EL DE LAS CASILLAS. La casilla 0 es 1500 Hz
 * de audio y la mitad de arriba son frecuencias negativas, asi que para
 * los anchos hay que quedarse con el minimo y el maximo EN HERCIOS, no en
 * indices. Haciendolo por indices, una señal centrada en 1500 Hz sale
 * ocupando los 3 kHz enteros.
 */
/*
 * LOS BAUDIOS Y EL CICLO DE TRABAJO, DE UNA PASADA POR EL ANILLO.
 *
 * Los baudios salen de medir RACHAS: cuanto dura cada tramo entre dos
 * cambios. La racha mas corta que se repite es un simbolo, y las demas son
 * multiplos suyos. Contra las grabaciones de la noche del 2 al 3 de
 * octubre esto da 10,00 ms clavados en el SITOR, o sea 100,00 baudios.
 *
 * DE QUE SE MIRAN LOS CAMBIOS, QUE ES LO QUE IMPORTA. Si hay dos tonos, de
 * la frecuencia instantanea respecto al punto medio entre ellos: eso es un
 * discriminador de FSK y da el bit directamente. La frecuencia instantanea
 * entre dos muestras seguidas es el angulo de z[n] * conj(z[n-1]), pero no
 * hace falta el angulo: basta con girarlo por el punto medio y mirarle el
 * SIGNO a la parte imaginaria. Un producto complejo por una constante y ya,
 * sin una sola arcotangente en todo el anillo.
 *
 * Si NO hay dos tonos no se miden baudios y se dice que no se saben, que es
 * lo honrado: en una PSK los cambios de fase no se cuentan asi, y un numero
 * inventado aqui se leeria como una medida.
 *
 * El ciclo de trabajo es aparte y SIEMPRE se mide: que porcentaje del
 * anillo lleva señal. Es lo unico que separa un NAVTEX de un DSC, que
 * llevan exactamente la misma modulacion y se distinguen en que uno emite
 * seguido y el otro a rafagas de un segundo.
 */
/*
 * EL OLVIDO Y LA PUBLICACION DE LA CR7, Y VAN LOS DOS ANTES DE CUALQUIER
 * RETORNO DE mide_rachas().
 *
 * Si el olvido viviera dentro -donde esta el del histograma de rachas-,
 * una señal que se va dejaria los contadores QUIETOS: sin dos tonos no
 * se llega hasta alli, y el porcentaje se quedaria colgado diciendo que
 * si para siempre. Es el mismo fallo que la fila de texto del panel que
 * no se borraba sola, y ese ya costo una foto y un cabreo.
 *
 * Asi, cuando la señal desaparece los pesos bajan solos y en cuanto uno
 * cae por debajo de CR7_MINIMO se publica 0, que es "no lo se".
 */
static void cr7_pasada(void)
{
    uint8_t c, f, o, mejor_c = 0U;
    float mejor = 0.0f;

    s_r.cr7_pc = 0U; s_r.cr7_bd_x10 = 0U;

    for (c = 0U; c < CR7_N; c++) {
        for (f = 0U; f < CR7_F; f++) {
            for (o = 0U; o < 7U; o++) {
                float t = s_cr7_t[c][f][o];

                s_cr7_t[c][f][o] = t * CR7_OLVIDO;
                s_cr7_4[c][f][o] *= CR7_OLVIDO;
                s_cr7_3[c][f][o] *= CR7_OLVIDO;

                if (t < CR7_MINIMO) { continue; }
                {
                    /* Las dos polaridades: cuatro unos por un lado son
                     * tres por el otro, y cual sea depende de que tono
                     * mando el transmisor arriba, que no se sabe. */
                    float a4 = s_cr7_4[c][f][o] / t;
                    float a3 = s_cr7_3[c][f][o] / t;
                    float v  = (a4 > a3) ? a4 : a3;
                    if (v > mejor) { mejor = v; mejor_c = c; }
                }
            }
        }
    }

    if (mejor > 0.0f) {
        float pc = mejor * 100.0f + 0.5f;
        if (pc > 100.0f) { pc = 100.0f; }
        s_r.cr7_pc     = (uint8_t)pc;
        s_r.cr7_bd_x10 = k_cr7_bd_x10[mejor_c];
    }
}

static void mide_rachas(void)
{
    uint32_t i, n = 0U, mejor_b = 0U;
    float env_mx = 0.0f, umbral;
    float ur, ui;
    uint32_t con = 0U;

    cr7_pasada();

    s_r.baudios_x10 = 0U;
    s_r.ciclo_pc = 0U;

    /* El techo de la envolvente, para el ciclo de trabajo. Se toma el
     * maximo y se baja 9 dB: por debajo de eso no hay señal, hay cola. */
    for (i = 0U; i < STANAG_DET_N; i++) {
        float re = s_anillo[2U*i], im = s_anillo[2U*i+1U];
        float e = re*re + im*im;
        if (e > env_mx) { env_mx = e; }
    }
    if (!(env_mx > 0.0f)) { return; }
    umbral = env_mx * 0.125f;
    for (i = 0U; i < STANAG_DET_N; i++) {
        float re = s_anillo[2U*i], im = s_anillo[2U*i+1U];
        if ((re*re + im*im) >= umbral) { con++; }
    }
    s_r.ciclo_pc = (uint8_t)(((float)con * 100.0f) / (float)STANAG_DET_N + 0.5f);

    if (!s_r.hay_tonos) { return; }

    /* El giro del punto medio entre los dos tonos, en banda base: el
     * oscilador ya bajo la señal 1500 Hz, asi que el punto medio de verdad
     * es (bajo+alto)/2 - 1500. */
    {
        float fm = (((float)s_r.tono_bajo + (float)s_r.tono_alto) * 0.5f)
                   - 1500.0f;
        float w = (2.0f * 3.14159265358979f * fm) / (float)STANAG_DET_HZ;
        ur = cosf(w); ui = sinf(w);
    }

    /*
     * EL HISTOGRAMA SE ACUMULA, NO SE BORRA, y esto no es un detalle: una
     * ventana son 1024 muestras a 3 kHz, o sea 341 ms, y a 100 baudios ahi
     * caben 34 simbolos. Diecisiete rachas no son una medida de nada. La
     * primera version lo borraba en cada pasada y por eso no sacaba
     * velocidad NUNCA: siempre se quedaba por debajo del minimo.
     *
     * Acumulando con olvido -se queda el 80% de lo anterior- a los pocos
     * segundos hay cientos de rachas, y lo viejo se va solo cuando la
     * señal cambia. Es el mismo promediado que la autocorrelacion de
     * abajo, por la misma razon.
     */
    /*
     * EL OLVIDO ERA 0,8 Y A 50 BAUDIOS SALIA CARA O CRUZ - 05/10/2026.
     *
     * De FUG, la marina francesa en 13.418 kHz: FSK limpia,
     * 850 Hz de salto, 50 baudios. El motor le vio los dos tonos perfectos
     * -1195 y 2045- y dijo "velocidad 0,0 Bd". Sin velocidad no cuadra
     * ninguna fila de dos tonos, asi que la pantalla se cayo a la pista
     * del ancho y solto "SSTV o fax meteorologico". Un disparate con dos
     * tonos delante.
     *
     * Sinteticé FSK limpia y la barrí, y el patron no era el salto:
     *
     *    salto  170 Hz  50 Bd -> 50,0      170 Hz  75 Bd -> 75,1
     *    salto  450 Hz  50 Bd ->  0,0      450 Hz  75 Bd -> 75,3
     *    salto  850 Hz  50 Bd -> 50,0      850 Hz  75 Bd -> 75,3
     *    salto 1000 Hz  50 Bd ->  0,0     1000 Hz  75 Bd -> 75,2
     *
     * Los 75 salen SIEMPRE. Los 50 fallan a capricho. Y la cuenta lo dice:
     * una ventana son 341 ms, a 50 baudios caben 17 simbolos, y con bits
     * al azar la racha media son dos simbolos -o sea unas 8,5 rachas por
     * ventana-. Con olvido 0,8 el regimen permanente es
     *
     *     8,5 / (1 - 0,8) = 42,5 rachas   contra un umbral de 40.
     *
     * Un SEIS POR CIENTO de margen. A 75 baudios son 64 contra 40, que si
     * es margen, y por eso 75 no fallaba nunca. Seis por ciento no es
     * margen: es que la decida el desvanecimiento de turno.
     *
     * Con 0,94 el permanente es 8,5/0,06 = 142 a 50 baudios, y 130 a
     * 45,45, que es la velocidad mas lenta de la tabla. Tres veces el
     * umbral en el peor caso.
     *
     * LO QUE CUESTA, dicho claro: la memoria pasa de 1/(1-0,8) = 5
     * ventanas -1,7 s- a 16,7 -5,7 s-. O sea que al cambiar de señal tarda
     * cuatro segundos mas en olvidar la anterior. Se acepta: una velocidad
     * que no sale es un fallo que se ve en pantalla, y cuatro segundos de
     * mas al sintonizar no los nota nadie en un modo que existe justo para
     * quedarse mirando una señal.
     *
     * Es el mismo fallo que la V3.42, con la misma forma: memoria corta
     * medida con una señal rapida y estrenada con una lenta.
     */
    for (i = 0U; i < HIST_N; i++) { s_acf[HIST_BASE + i] *= 0.94f; }


    {
        uint8_t ant = 2U;          /* 2 = todavia no hay anterior */
        uint32_t racha = 0U;
        uint32_t k0 = s_w;         /* el anillo, en orden */
        float suave = 0.0f;

        /*
         * EL DISCRIMINADOR SE ALISA ANTES DE MIRARLE EL SIGNO, Y SIN ESO NO
         * SALE NADA. La primera version cortaba el signo muestra a muestra
         * y el ruido metia rachas de una y dos muestras a miles: contra las
         * nueve grabaciones daba entre 1700 y 2800 baudios en TODAS,
         * incluida una portadora. La racha mas corta que se repetia era el
         * ruido, no el simbolo.
         *
         * Con la señal a 3 kHz, un simbolo de 100 baudios son treinta
         * muestras, asi que una media movil de cinco no toca el simbolo y
         * se lleva por delante el ruido. Es la misma media movil de un bit
         * que lleva navtex.c por dentro, solo que mas corta porque aqui no
         * se sabe de antemano cuanto dura un bit.
         */
        for (i = 1U; i < STANAG_DET_N; i++) {
            uint32_t a = k0 + i - 1U; if (a >= STANAG_DET_N) { a -= STANAG_DET_N; }
            uint32_t b = k0 + i;      if (b >= STANAG_DET_N) { b -= STANAG_DET_N; }
            float ar = s_anillo[2U*a], ai = s_anillo[2U*a+1U];
            float br = s_anillo[2U*b], bi = s_anillo[2U*b+1U];
            /* d = z[b] * conj(z[a]) */
            float dr = br*ar + bi*ai;
            float di = bi*ar - br*ai;
            /* y girado por el punto medio: nos quedamos con la imaginaria */
            float q = di*ur - dr*ui;
            float mag = dr*dr + di*di;
            uint8_t bit;

            /*
             * NORMALIZADO, QUE SI NO MANDA LA AMPLITUD Y NO LA FRECUENCIA.
             *
             * q viene multiplicado por |z|^2, asi que en un desvanecimiento
             * se va a cero y el signo lo decide el ruido. Dividiendo por el
             * modulo, lo que queda es el SENO del error de frecuencia, que
             * es lo que se queria y no depende de lo fuerte que llegue.
             *
             * Sin esto, las nueve grabaciones daban entre 600 y 2800
             * baudios; el discriminador estaba midiendo desvanecimientos.
             */
            if (mag > 1e-20f) { q /= sqrtf(mag); }
            else              { q = suave; }   /* sin señal, se mantiene */

            suave += 0.12f * (q - suave);
            if (i < 16U) { continue; }       /* que la media se asiente */
            bit = (suave > 0.0f) ? 1U : 0U;

            /*
             * LA CR7, en el mismo cortador de bits y no en otro. Si se
             * escribiera un segundo discriminador acabaria habiendo dos
             * verdades sobre que bit es este, y la de abajo -alisada y
             * normalizada por el modulo- costo nueve grabaciones.
             */
            {
                uint32_t abso = (s_rtot - STANAG_DET_N) + i;
                uint8_t c, f;
                for (c = 0U; c < CR7_N; c++) {
                    uint32_t T = k_cr7_T[c];
                    uint32_t d = abso % T;
                    for (f = 0U; f < CR7_F; f++) {
                        uint8_t off, u;
                        /* las cuatro fases: 1/8, 3/8, 5/8 y 7/8 del bit */
                        if (d != ((((uint32_t)f * 2U) + 1U) * T) / 8U) {
                            continue;
                        }
                        s_cr7_reg[c][f] =
                            (uint8_t)(((uint8_t)(s_cr7_reg[c][f] << 1U)
                                       | bit) & 0x7FU);
                        if (s_cr7_hay[c][f] < 7U) { s_cr7_hay[c][f]++; continue; }
                        off = (uint8_t)((abso / T) % 7U);
                        u   = k_popc7[s_cr7_reg[c][f]];
                        s_cr7_t[c][f][off] += 1.0f;
                        if      (u == 4U) { s_cr7_4[c][f][off] += 1.0f; }
                        else if (u == 3U) { s_cr7_3[c][f][off] += 1.0f; }
                    }
                }
            }

            if (ant == 2U) { ant = bit; racha = 1U; continue; }
            if (bit == ant) { racha++; continue; }
            if (racha < HIST_N) { s_acf[HIST_BASE + racha] += 1.0f; n++; }
            ant = bit; racha = 1U;
        }
    }
    (void)n;
    {
        /* Sobre lo ACUMULADO, no sobre esta pasada. */
        float total = 0.0f;
        for (i = 3U; i < HIST_N; i++) { total += s_acf[HIST_BASE + i]; }
        if (total < 40.0f) { return; }
    }

    /*
     * EL SIMBOLO ES EL GRUPO MAS BAJO, NO EL MAS POBLADO.
     *
     * El histograma no es una joroba: son GRUPOS, en el simbolo y en sus
     * multiplos. Medido en las dos grabaciones de SITOR, con el simbolo en
     * 30 muestras:
     *
     *   8424 kHz   28..32 -> 22 rachas    58..62 -> 11
     *   Oostende   28..32 ->  7 rachas    58..62 -> 18
     *
     * En la de Oostende el grupo de DOS simbolos es mas poblado que el de
     * uno, asi que coger la moda da 60 muestras y 50 baudios: la mitad de
     * la velocidad real. Eso es lo que pasaba. El simbolo es el grupo mas
     * BAJO de los que son grupo de verdad.
     *
     * Y "de verdad" hay que definirlo, porque abajo del todo hay siempre
     * rachas sueltas de los desvanecimientos -en la de 8424 kHz, catorce
     * repartidas entre las casillas 3 y 14-. Un grupo de verdad:
     *   - se lleva al menos el 12% de todas las rachas en +-2 casillas,
     *   - y al menos la cuarta parte de lo que se lleva el grupo mayor,
     *   - y es un maximo local, no la ladera de otro.
     * Con eso, el ruido de la de 8424 kHz se queda en el 6% y no entra,
     * mientras que el grupo bueno saca el 23%.
     */
    /*
     * ==================================================================
     * EL PEINE - 08/10/2026. Y ESTO SUSTITUYE AL "GRUPO MAS BAJO".
     * ==================================================================
     *
     * Pedido por el usuario.
     *
     * Lo que habia aqui buscaba EL GRUPO MAS BAJO QUE FUERA UN GRUPO DE
     * VERDAD: la racha mas corta que se repite. Con una alternancia limpia
     * eso ES el simbolo. Con datos cifrados NO, y el banco
     * sim/det_velocidad.c lo tiene medido contra la verdad sacada por un
     * camino que no comparte nada con este fichero:
     *
     *     sitorb_tfc       es 100,0 Bd   y decia 103,3     bien
     *     navtex8424       es 100,0 Bd   y decia 610,8     511% de error
     *     stanag4481_fsk   es  75,0 Bd   y decia 636,0     748% de error
     *
     * La MISMA velocidad y el MISMO salto en dos grabaciones limpias, una
     * bien y la otra con un 511%. Eso no es la grabacion: es el medidor.
     * 610 baudios a 3 kHz son CINCO MUESTRAS por racha, o sea que se estaba
     * agarrando al ruido, y los tres filtros que tenia -el 12% del total,
     * el 25% del mayor y el maximo local- no lo echaban.
     *
     * LA IDEA BUENA ESTABA EN EL PROPIO COMENTARIO DE ABAJO, sin usarse:
     * "el histograma no es una joroba, son GRUPOS, en el simbolo y en sus
     * multiplos". Pues entonces no se busca un grupo: se busca EL PERIODO
     * QUE EXPLICA TODOS LOS GRUPOS A LA VEZ. Se prueba cada periodo
     * candidato como un PEINE -puas en T, 2T, 3T...- y gana el que mas peso
     * del histograma recoge.
     *
     * Y HAY QUE DESCONTAR LA CASUALIDAD, que es lo que hace que esto
     * funcione y que un peine ingenuo no: un peine de periodo pequeño tiene
     * muchas mas puas y recoge mas peso por puro azar -con T = 5 las puas
     * caen en 5, 10, 15... y 30 es multiplo de 5, asi que el simbolo de
     * verdad tambien cae dentro-. Asi que a lo recogido se le resta lo que
     * recogeria el mismo peine sobre un histograma plano. Un peine de cinco
     * puas que recoge el 20% no vale nada si cubre el 20% de las casillas;
     * uno de dos puas que recoge el 60% cubriendo el 3% es la señal.
     *
     * Y LA FUNDAMENTAL TIENE QUE ESTAR POBLADA. Sin eso, el peine de 2T
     * puntua casi igual que el de T -sus puas son un subconjunto- y la
     * velocidad saldria la mitad. Se exige que la pua de T lleve al menos
     * la quinta parte de lo que lleva el peine entero.
     *
     * ALCANCE: de 18 a 180 muestras por simbolo, o sea de 167 a 16,7
     * baudios a 3 kHz. Por debajo de 18 no hay simbolo que medir con este
     * discriminador -es la zona del ruido-, y por encima de 180 no caben
     * dos puas en las 256 casillas del histograma.
     */
    {
        float total = 0.0f, mejor_s = 0.0f;
        uint32_t T;

        for (i = 3U; i < HIST_N; i++) { total += s_acf[HIST_BASE + i]; }
        if (!(total > 0.0f)) { return; }

        for (T = PEINE_TMIN; T <= PEINE_TMAX; T++) {
            uint32_t w = T / 16U;      /* tolerancia proporcional */
            uint32_t k, casillas = 0U;
            float cogido = 0.0f, fund = 0.0f, esperado, punt;

            if (w < 1U) { w = 1U; }

            for (k = T; k < HIST_N; k += T) {
                uint32_t lo = (k > w) ? (k - w) : 1U;
                uint32_t hi = k + w; if (hi >= HIST_N) { hi = HIST_N - 1U; }
                float pua = 0.0f;

                for (i = lo; i <= hi; i++) { pua += s_acf[HIST_BASE + i]; }
                cogido += pua;
                casillas += (hi - lo) + 1U;
                if (k == T) { fund = pua; }
            }

            /* Lo que recogeria el mismo peine si el histograma fuera plano. */
            esperado = total * ((float)casillas / (float)(HIST_N - 3U));
            punt = cogido - esperado;

            /* La fundamental tiene que llevar su parte, o esto mide 2T. */
            if (!(cogido > 0.0f) || fund < (cogido * 0.20f)) { continue; }

            if (punt > mejor_s) { mejor_s = punt; mejor_b = T; }
        }

        /*
         * Y SI NINGUN PEINE DESTACA, NO SE DICE NADA. Un peine que apenas
         * supera a la casualidad es ruido con forma, y publicar un numero
         * inventado es peor que publicar cero: el cero la tabla del ident lo
         * entiende -"sin medir"- y el numero inventado se lo cree.
         */
        if (mejor_b == 0U || mejor_s < (total * PEINE_MINIMO)) {
            mejor_b = 0U;
        }
    }
    if (mejor_b == 0U) { return; }

    /*
     * Y afinando: la media de las rachas que caen en esa casilla y sus dos
     * vecinas pesa mejor que el centro de la casilla. Con 30 muestras por
     * bit, una casilla de una muestra es un 3% de error, y 3% sobre 100
     * baudios son 3 baudios - suficiente para confundir 45,45 con 50.
     */
    {
        float suma = 0.0f, peso = 0.0f;
        uint32_t j0 = (mejor_b > 1U) ? (mejor_b - 1U) : 1U;
        uint32_t j1 = mejor_b + 2U; if (j1 > HIST_N) { j1 = HIST_N; }
        for (i = j0; i < j1; i++) {
            suma += (float)i * s_acf[HIST_BASE + i];
            peso += s_acf[HIST_BASE + i];
        }
        if (peso > 0.0f) {
            float muestras = suma / peso;
            if (muestras >= 1.0f) {
                float bd = (float)STANAG_DET_HZ / muestras;
                if (bd > 0.0f && bd < 6000.0f) {
                    s_r.baudios_x10 = (uint16_t)(bd * 10.0f + 0.5f);
                }
            }
        }
    }
}

/*
 * ==========================================================================
 * LA REJILLA DE TONOS - 05/10/2026
 * ==========================================================================
 *
 * Pedido por el usuario.
 *
 * Y no podia hacer otra cosa: hasta hoy este fichero solo sabia buscar DOS
 * rayas. Un peine de veintiuna le era invisible, asi que se quedaba con el
 * ancho de banda -que en un MFSK no dice casi nada: una Olivia de 500 Hz y
 * otra de 500 Hz con la mitad de tonos ocupan lo mismo y no son la misma
 * señal- y soltaba una pista equivocada.
 *
 * Lo que de verdad separa a toda esa familia es CADA CUANTO ESTA EL
 * SIGUIENTE TONO. Y eso se mide sin saber donde esta ninguno: un peine de
 * rayas separadas S correlaciona consigo mismo desplazado S. O sea, la
 * autocorrelacion DEL ESPECTRO, no de la señal.
 *
 * TRES COSAS QUE NO SON OBVIAS
 * ----------------------------
 * 1. HACE FALTA EL ESPECTRO PROMEDIADO, no el de una ventana. Una ventana
 *    son 341 ms y a 23 baudios ahi caben ocho simbolos: en el espectro
 *    instantaneo solo hay ocho de los veintiun tonos encendidos, y ocho
 *    rayas sueltas no son un peine. Promediando con olvido, a los pocos
 *    segundos estan las veintiuna y la rejilla salta a la vista. Por eso
 *    s_med existe y por eso el prestamo crecio 1024 floats.
 *
 * 2. SE PROMEDIA A MEDIA RESOLUCION -casillas de 2,93 Hz en vez de 1,46-
 *    para que quepa. No cuesta precision donde importa: el pico de la
 *    correlacion se afina por interpolacion parabolica, que da decimas de
 *    casilla. Lo que si cuesta es el suelo: por debajo de unos 9 Hz de
 *    separacion esto no ve nada. El WSPR -1,46 Hz- queda fuera por
 *    definicion y el FT8 -6,25- en el limite. A esos dos los reconoce la
 *    cadencia, que es otra medida y otro instrumento.
 *
 * 3. EL PRIMER PICO, NO EL MAS ALTO. Un peine de separacion S correlaciona
 *    igual de bien a 2S y a 3S -son los mismos tonos saltando de dos en
 *    dos-. La separacion es la MENOR, asi que al encontrar el mejor se
 *    prueba si su mitad y su tercio tambien valen.
 */

/*
 * La coherencia de esta ventana y su autocorrelacion, promediada con las
 * anteriores. Ver el comentario grande de SIMB_W.
 */
static void simb_acumula(void)
{
    uint32_t i, L;
    float sr = 0.0f, si = 0.0f, se = 0.0f;
    float media = 0.0f, var = 0.0f;

    /*
     * La suma deslizante de W productos z[n]*conj(z[n-W]), y la energia de
     * la misma ventana para normalizar. Las dos se llevan al dia con una
     * suma y una resta por muestra: sin esto esto costaria W veces mas.
     */
    for (i = 0U; i < STANAG_DET_N; i++) { s_coh[i] = 0.0f; }

    /*
     * LOS INDICES AVANZAN, NO SE DIVIDEN - 18/10/2026.
     *
     * Aqui habia cuatro "% STANAG_DET_N" POR MUESTRA, o sea cuatro mil
     * divisiones por ventana y doce ventanas por segundo, dentro del bucle
     * que lee el tactil. En un Cortex-M4 una division entera no es gratis,
     * y el valor que se busca avanza de uno en uno: lo que hace falta es
     * sumar y dar la vuelta con una comparacion. Sale EL MISMO numero, no
     * una aproximacion, y lo comprueban identtest e ident_grabaciones.
     *
     * TRES INDICES Y NO CUATRO, y eso es lo que me mordio: en el original
     * `c` y `b` son LA MISMA CUENTA -los dos son (s_w + i - SIMB_W)-, asi
     * que al pasarlos a contadores les puse uno a cada uno y el de la
     * resta arrancaba SIMB_W posiciones atrasado. Un MFSK de 21 tonos a
     * 23,44 baudios salia medido como 150 Bd.
     */
    /*
     * ======================================================================
     * Y DESDE EL 19/10/2026, NI SIQUIERA AVANZAN: LA VENTANA YA ESTA LISTA
     * ======================================================================
     *
     * Pedido por el usuario: que la radio vaya mas rapido.
     *
     * stanag_det_paso() empieza copiando el anillo EN ORDEN a s_fft[], que
     * es justo lo que este bucle estaba reconstruyendo a mano con tres
     * contadores y seis comparaciones por muestra. Llamando a esto ANTES de
     * la transformada, la ventana se lee de corrido:
     *
     *     z[j] = s_fft[2j], s_fft[2j+1]      j = 0 .. STANAG_DET_N-1
     *
     * y los tres indices son j, j-SIMB_W y j-2*SIMB_W, sin vuelta que dar.
     * Sale el MISMO numero -es la misma ventana leida del mismo sitio- y
     * se ahorran tres comparaciones y tres sumas por muestra.
     *
     * Y hay una segunda cosa que esto compra, que es la que de verdad
     * importa: la ventana deja de leerse del ANILLO, que la interrupcion
     * del audio sigue escribiendo mientras esto corre. s_fft[] no lo toca
     * nadie mas. O sea que lo que se mide es una FOTO y no una pelicula.
     * Esa es la razon por la que los dos intentos de partir el trabajo en
     * varias vueltas acabaron midiendo basura -estan contados aqui abajo-:
     * no era la idea, era que la leian del anillo.
     */
    {
        const float *z = s_fft;      /* la ventana en orden, ya copiada */

        for (i = SIMB_W; i < STANAG_DET_N; i++) {
            uint32_t ja = 2U * i;
            uint32_t jb = 2U * (i - SIMB_W);
            float ar = z[ja], ai = z[ja + 1U];
            float br = z[jb], bi = z[jb + 1U];

            sr += ar*br + ai*bi;          /* Re(z[n] * conj(z[n-W])) */
            si += ai*br - ar*bi;          /* Im(...) */
            se += ar*ar + ai*ai;

            if (i >= (2U * SIMB_W)) {
                uint32_t jd = 2U * (i - 2U * SIMB_W);
                float dr = z[jd], di = z[jd + 1U];
                sr -= br*dr + bi*di;
                si -= bi*dr - br*di;
                se -= br*br + bi*bi;
            }
            if (se > 1e-20f) {
                s_coh[i] = sqrtf(sr*sr + si*si) / se;
            }
        }
    }

    for (i = 2U*SIMB_W; i < STANAG_DET_N; i++) { media += s_coh[i]; }
    media /= (float)(STANAG_DET_N - 2U*SIMB_W);
    for (i = 2U*SIMB_W; i < STANAG_DET_N; i++) {
        float d = s_coh[i] - media;
        s_coh[i] = d;
        var += d * d;
    }
    if (!(var > 0.0f)) { return; }

    /*
     * ======================================================================
     * LOS MISMOS PRODUCTOS PARA TODOS LOS RETARDOS - 19/10/2026
     * ======================================================================
     *
     * Pedido por el usuario: que la radio vaya mas rapido. Y este bucle es
     * lo mas caro que hay en el identificador: 281 retardos por casi mil
     * productos cada uno son 234.000 multiplicaciones por ventana, mas que
     * las dos transformadas de 2048 juntas.
     *
     * DE DONDE SALE EL AHORRO, Y POR QUE ADEMAS ESTABA SESGADO. El bucle
     * de dentro llegaba hasta el final de la ventana, asi que el retardo 20
     * sumaba 972 productos y el 300 solo 692. Pero los dos se dividian por
     * el MISMO `var`, que es la energia de la ventana entera. O sea que un
     * retardo largo salia sistematicamente un 30 % mas bajo que uno corto
     * aunque la señal fuera igual de periodica en los dos, y quien elige
     * se queda con el mayor: habia un sesgo hacia los simbolos cortos -o
     * sea hacia las velocidades altas- metido en la normalizacion.
     *
     * Fijando el numero de productos los 281 retardos se miden con la misma
     * vara, que es lo que siempre se quiso, y de paso el bucle cuesta
     * SIMB_PROD/992 de lo que costaba. La escala se conserva dividiendo por
     * la parte de `var` que corresponde a esos productos, asi que el 0,10
     * de mide_simbolos() y el tanto por ciento de calidad siguen
     * significando lo mismo.
     *
     * POR QUE 480. Tiene que caber para el retardo mas largo -992 - 300 =
     * 692- y tiene que dejar bastantes periodos del simbolo mas lento:
     * a 300 muestras por simbolo, 480 productos son algo mas de un simbolo
     * y medio por ventana, y lo que afina la medida no es una ventana sino
     * el promedio con olvido de unas cincuenta (SIMB_OLVIDO). Medido con
     * las nueve grabaciones de referencia: ni un acierto de diferencia.
     */
    {
        const uint32_t n0 = 2U * SIMB_W;
        /* La misma porcion de `var` que cubren esos productos. */
        const float esc = var * ((float)SIMB_PROD
                                 / (float)(STANAG_DET_N - n0));

        for (L = SIMB_LMIN; L <= SIMB_LMAX; L++) {
            float ac = 0.0f;
            uint32_t fin;

            if ((n0 + L) >= STANAG_DET_N) { break; }
            fin = n0 + SIMB_PROD;
            if ((fin + L) > STANAG_DET_N) { fin = STANAG_DET_N - L; }
            for (i = n0; i < fin; i++) {
                ac += s_coh[i] * s_coh[i + L];
            }
            ac /= esc;
            s_simbac[L] = s_simbac[L] * SIMB_OLVIDO + ac * (1.0f - SIMB_OLVIDO);
        }
    }
    if (s_simbn < 10000U) { s_simbn++; }
}

static void mide_simbolos(void)
{
    uint32_t L, mejor = 0U;
    float mejorv = 0.0f;

    s_r.simb_x10 = 0U; s_r.tonos = 0U; s_r.simb_q = 0U;
    if (s_simbn < 40U) { return; }   /* unos tres segundos de ventanas */

    for (L = SIMB_LMIN; L <= SIMB_LMAX; L++) {
        if (s_simbac[L] > mejorv) { mejorv = s_simbac[L]; mejor = L; }
    }
    if (mejor == 0U || mejorv < 0.10f) { return; }

    /* El PRIMER maximo que llegue al 60% del mejor: un simbolo de T
     * correlaciona igual de bien a 2T y a 3T, y la duracion es la menor. */
    for (L = SIMB_LMIN + 1U; L + 1U <= SIMB_LMAX; L++) {
        if (s_simbac[L] < mejorv * 0.6f) { continue; }
        if (s_simbac[L] < s_simbac[L-1U] || s_simbac[L] < s_simbac[L+1U]) { continue; }
        mejor = L; mejorv = s_simbac[L];
        break;
    }

    /* Afinado con una parabola: con 3 kHz y un simbolo de 128 muestras, una
     * muestra de error es un 0,8% y eso basta para confundir 23,4 con 23,6. */
    {
        float d = 0.0f, bd;
        if (mejor > SIMB_LMIN && (mejor + 1U) <= SIMB_LMAX) {
            float a = s_simbac[mejor - 1U], b = mejorv, c = s_simbac[mejor + 1U];
            float den = a - 2.0f * b + c;
            if (den != 0.0f) {
                d = 0.5f * (a - c) / den;
                if (d > 0.5f) { d = 0.5f; }
                if (d < -0.5f) { d = -0.5f; }
            }
        }
        bd = (float)STANAG_DET_HZ / ((float)mejor + d);
        if (bd > 0.0f && bd < 1000.0f) {
            s_r.simb_x10 = (uint16_t)(bd * 10.0f + 0.5f);
        }
    }
    {
        float q = mejorv * 100.0f;
        if (q > 100.0f) { q = 100.0f; }
        s_r.simb_q = (uint8_t)q;
    }

    /*
     * Y CUANTOS TONOS, dando por hecho que es MFSK ORTOGONAL -separacion
     * igual a la velocidad, que es como estan hechos Olivia, Contestia,
     * MFSK y casi todos-. Es una cuenta sobre dos medidas, no una medida:
     * ancho dividido entre separacion. En un modo donde la separacion NO
     * sea la velocidad -el ALE 2G va a 125 baudios con los tonos a 250 Hz-
     * este numero sale el doble de lo que es, y por eso quien lo use tiene
     * que saberlo. Lo sabe: ver las firmas de ident.c.
     */
    if (s_r.simb_x10 > 0U && s_r.ancho10_hz > 0U) {
        uint32_t t = (((uint32_t)s_r.ancho10_hz * 10UL) + (s_r.simb_x10 / 2U))
                     / s_r.simb_x10;
        if (t > 60U) { t = 60U; }
        s_r.tonos = (uint8_t)t;
    }
}

/*
 * Cuanto sobresale una raya del ruido que tiene AL LADO - 06/10/2026.
 *
 * Es lo que sustituye a los dos criterios que comparaban las dos rayas
 * entre si (ver TONO_REALCE). El desvanecimiento selectivo sube y baja una
 * raya y su ruido a la vez, asi que esta medida no se entera de el; y
 * separa lo que hay que separar: una raya de FSK sobresale de lo que tiene
 * alrededor aunque llegue floja, y una cresta de ruido no, por definicion.
 *
 * El fondo se toma como la MEDIA de las casillas que caen entre TONO_HUECO
 * y TONO_ENTORNO de la raya: el hueco deja fuera la propia raya y su falda,
 * y el entorno acota para que no se cuele la otra raya, que esta a 850 Hz
 * en el caso que origino esto.
 */
/*
 * ¿Hay un valle entre las dos rayas? Ver donde se usa.
 *
 * El minimo del tramo de en medio -dejando fuera las faldas de las dos-
 * tiene que estar TONO_VALLE decibelios por debajo de la MENOR de las dos
 * rayas. Con la menor y no con la media: si una viene desvanecida, exigir
 * el valle contra la fuerte seria pedirle al valle que baje mas de lo que
 * baja la propia raya floja.
 */
static float realce(float fc);

/* De hercios de audio a k CON SIGNO, que es el que recorre un tramo
 * seguido. Ver casilla_a_audio(), que hace el camino de vuelta. */
#define K_DE_HZ(f)   ((int32_t)floorf(((f) - 1500.0f) / HZ_CASILLA))
/* El indice real en s_fft[] a partir de ese k con signo. */
#define I_DE_K(k)    ((uint32_t)(((k) < 0) ? ((k) + (int32_t)NFFT) : (k)))
/* Los dos extremos que existen: k = -NFFT/2 .. NFFT/2 - 1 */
#define K_MIN        (-(int32_t)(NFFT / 2U))
#define K_MAX        ((int32_t)(NFFT / 2U) - 1)


static uint8_t hay_valle(float fa, float fb)
{
    float lo = (fa < fb) ? fa : fb;
    float hi = (fa < fb) ? fb : fa;
    float pa = 0.0f, pb = 0.0f, valle = 0.0f;
    float menor;
    uint32_t i;
    uint8_t  hay = 0U;

    /* Igual que en realce(): las tres ramas viven entre lo-TONO_ANCHO y
     * hi+TONO_ANCHO, que es un tramo seguido en k. Lo de fuera no entraba
     * en ninguna. Ver el comentario de realce(). */
    {
        int32_t k;
        int32_t k0 = K_DE_HZ(lo - TONO_ANCHO) - 2;
        int32_t k1 = K_DE_HZ(hi + TONO_ANCHO) + 2;

        if (k0 < K_MIN) { k0 = K_MIN; }
        if (k1 > K_MAX) { k1 = K_MAX; }

        for (k = k0; k <= k1; k++) {
            float f, v;

            i = I_DE_K(k);
            f = casilla_a_audio(i);
            v = s_fft[2U*i];

            if (f >= lo - TONO_ANCHO && f <= lo + TONO_ANCHO) {
                if (v > pa) { pa = v; }
            } else if (f >= hi - TONO_ANCHO && f <= hi + TONO_ANCHO) {
                if (v > pb) { pb = v; }
            } else if (f > lo + TONO_ANCHO && f < hi - TONO_ANCHO) {
                if (!hay || v < valle) { valle = v; hay = 1U; }
            }
        }
    }
    /* Sin tramo de en medio no hay nada que mirar: las dos rayas estan
     * pegadas y eso ya lo filtra TONO_SEP_MIN. */
    if (!hay || pa <= 0.0f || pb <= 0.0f) { return 0U; }
    menor = (pa < pb) ? pa : pb;
    if (valle <= 0.0f) { return 1U; }
    return (uint8_t)((10.0f * log10f(menor / valle)) >= TONO_VALLE);
}

/*
 * ======================================================================
 * LAS CASILLAS QUE PUEDEN IMPORTAR, Y NO LAS 2048 - 19/10/2026
 * ======================================================================
 *
 * Pedido por el usuario: que la radio vaya mas rapido.
 *
 * casilla_a_audio() es afin a trozos y MONOTONA en cada mitad:
 *
 *     f(k) = 1500 + k * HZ_CASILLA,   con k entre -1024 y 1023
 *     la casilla es  k           si k >= 0
 *                    k + NFFT    si k < 0
 *
 * O sea que "las casillas a menos de D hercios de fc" son un TRAMO
 * SEGUIDO en k, no un conjunto disperso que haya que ir a buscar
 * recorriendo las 2048. Con HZ_CASILLA = 1,46 Hz y D = 300, son 411
 * casillas de 2048: CINCO VECES MENOS.
 *
 * Y realce() no se llama una vez por ventana: se llama por cada maximo
 * local que pasa los filtros baratos, que con ruido de verdad son unos
 * SETENTA Y OCHO (medido con sim/ident_grabaciones.c y gprof). 78 x 2048
 * son 160.000 vueltas por ventana, el 20-25 % de todo lo que cuesta el
 * identificador. Pasan a 32.000.
 *
 * EL RESULTADO ES EL MISMO BIT A BIT, y eso es a proposito: el tramo se
 * calcula con holgura para que CONTENGA todas las casillas que pasaban
 * alguna de las dos ramas, y dentro del bucle se dejan las comparaciones
 * tal cual estaban. Las que se dejan fuera no entraban en ninguna rama,
 * o sea que no aportaban nada - no es una aproximacion, es no visitar lo
 * que no cuenta. Lo comprueban identtest e identgrab, que miden las
 * nueve grabaciones de referencia y no se mueven ni un acierto.
 */
static float realce(float fc)
{
    float pico = 0.0f, suma = 0.0f;
    uint32_t n = 0U;
    int32_t k, k0, k1;
    /* +2 de holgura por el redondeo de los dos extremos. */
    const int32_t semi = (int32_t)(TONO_ENTORNO / HZ_CASILLA) + 2;

    k0 = K_DE_HZ(fc) - semi; if (k0 < K_MIN) { k0 = K_MIN; }
    k1 = K_DE_HZ(fc) + semi; if (k1 > K_MAX) { k1 = K_MAX; }

    for (k = k0; k <= k1; k++) {
        uint32_t i = I_DE_K(k);
        float f = casilla_a_audio(i);
        float d = (f > fc) ? (f - fc) : (fc - f);

        if (d <= TONO_ANCHO) {
            if (s_fft[2U*i] > pico) { pico = s_fft[2U*i]; }
        } else if (d > TONO_HUECO && d <= TONO_ENTORNO) {
            suma += s_fft[2U*i];
            n++;
        }
    }
    if (n == 0U || suma <= 0.0f || pico <= 0.0f) { return 0.0f; }
    return 10.0f * log10f(pico / (suma / (float)n));
}

static void mide_espectro(void)
{
    uint32_t i, kp = 0U;
    float tot = 0.0f, mx = 0.0f, cerca = 0.0f;
    float u3[2], u10[2], u20[2];
    float p1 = 0.0f, p2 = 0.0f, f1 = 0.0f, f2 = 0.0f;

    s_r.hay_esp = 0U;
    s_r.centro_hz = 0U; s_r.ancho3_hz = 0U; s_r.ancho10_hz = 0U;
    s_r.ancho20_hz = 0U; s_r.pico_pc = 0U;
    s_r.hay_tonos = 0U; s_r.tono_bajo = 0U; s_r.tono_alto = 0U; s_r.sep_hz = 0U;
    s_r.n_senales = 0U;

    for (i = 0U; i < NFFT; i++) {
        float v = s_fft[2U*i];
        tot += v;
        if (v > mx) { mx = v; kp = i; }
    }
    if (!(mx > 0.0f) || !(tot > 0.0f)) { return; }
#ifdef DET_DBG
    { extern void det_dbg_esp(uint32_t kp, double audio, double mx, double tot);
      det_dbg_esp(kp, (double)casilla_a_audio(kp), (double)mx, (double)tot); }
#endif

    /* Los tres umbrales, en potencia: -3 dB es la mitad, -10 es la
     * decima parte, -20 la centesima. */
    u3[0] = u10[0] = u20[0] = 4000.0f;    /* minimos, empiezan arriba */
    u3[1] = u10[1] = u20[1] = 0.0f;       /* maximos, empiezan abajo */
    {
        float pico_f = casilla_a_audio(kp);
        for (i = 0U; i < NFFT; i++) {
            float v = s_fft[2U*i];
            float f = casilla_a_audio(i);

            if (f < pico_f + PICO_VENT_HZ && f > pico_f - PICO_VENT_HZ) {
                cerca += v;
            }
            if (v >= mx * 0.01f) {
                if (f < u20[0]) { u20[0] = f; }
                if (f > u20[1]) { u20[1] = f; }
                if (v >= mx * 0.1f) {
                    if (f < u10[0]) { u10[0] = f; }
                    if (f > u10[1]) { u10[1] = f; }
                    if (v >= mx * 0.5f) {
                        if (f < u3[0]) { u3[0] = f; }
                        if (f > u3[1]) { u3[1] = f; }
                    }
                }
            }
        }
        s_r.centro_hz = (uint16_t)(pico_f + 0.5f);
    }
    s_r.ancho3_hz  = (uint16_t)((u3[1]  > u3[0])  ? (u3[1]  - u3[0])  : 0.0f);
    s_r.ancho10_hz = (uint16_t)((u10[1] > u10[0]) ? (u10[1] - u10[0]) : 0.0f);
    s_r.ancho20_hz = (uint16_t)((u20[1] > u20[0]) ? (u20[1] - u20[0]) : 0.0f);
    s_r.pico_pc    = (uint8_t)((cerca / tot) * 100.0f + 0.5f);

    /*
     * EL PAR DE TONOS. La raya mas alta, y luego la mas alta de todo lo que
     * este a mas de TONO_SEP_MIN de ella. Si la segunda no llega a 10 dB
     * por debajo de la primera, es que de verdad son dos y no una raya con
     * su falda: una FSK reparte el tiempo entre sus dos tonos, asi que
     * salen casi iguales.
     */
    f1 = casilla_a_audio(kp);
    p1 = mx;

    /*
     * ======================================================================
     * PRIMERO: CUANTAS SEÑALES HAY. 10/10/2026.
     * ======================================================================
     *
     * Pedido por el usuario.
     *
     * Lo que habia debajo cogia la raya mas alta y luego LA MAS ALTA DE TODO
     * lo que estuviera a mas de 120 Hz. Con una señal eso son sus dos tonos.
     * Con tres RTTY a la vez -que es lo que hay en 40 m un sabado de
     * concurso- cogia el tono de ARRIBA de una y el de ABAJO de otra:
     *
     *     canal 1    372 / 542 Hz      canal 2   855 / 1028
     *     canal 3   2045 / 2224 Hz
     *     y publicaba  "850 y 2227, salto 1019"
     *
     * 1019 Hz cae cerca del 1200/2200 del APRS, asi que la radio contestaba
     * "APRS / paquete AFSK 1200, 70%" sobre tres teletipos. La tabla estaba
     * bien y los margenes estaban bien: el fallo era de donde salian los
     * numeros. Es el mismo sitio que ya fallo el 02/10 con las PSK anchas.
     *
     * COMO SE AGRUPAN, Y POR QUE ASI. Se listan las rayas que de verdad
     * parecen un tono -que sobresalen de su propio ruido, mismo criterio que
     * ya decidia antes- y se emparejan POR CERCANIA: la pareja con el hueco
     * mas pequeño primero, luego la siguiente, y asi.
     *
     * La suposicion es explicita y conviene verla escrita: DOS TONOS DE LA
     * MISMA SEÑAL ESTAN MAS CERCA ENTRE SI QUE DE LOS DE LA SEÑAL DE AL
     * LADO. En la grabacion de referencia los huecos salen 170, 173 y 179 dentro
     * de cada una, y 313 y 1017 entre señales distintas: se parten solos. Si
     * dos estaciones se solaparan tanto como para romper esto, tampoco se
     * podrian decodificar, asi que el caso no se pierde: no existe.
     *
     * No se toca nada cuando solo hay una señal -dos rayas o menos- y por
     * eso las nueve grabaciones de siempre siguen midiendo lo mismo.
     */
    {
        float cf[DET_TONOS_MAX];
        float cp[DET_TONOS_MAX];
        uint8_t nc = 0U;
        uint8_t a, b;

        /*
         * OJO AL ORDEN: casilla_a_audio() NO crece con el indice. El espectro
         * esta guardado como lo deja la FFT -primero las frecuencias de
         * arriba y luego las de abajo-, asi que recorrer i de 0 a NFFT da
         * frecuencias que suben, pegan un salto y vuelven a subir. La primera
         * version de esto emparejaba suponiendo orden y habria juntado la
         * raya de 2224 Hz con la de 372. Se ordena.
         */
        for (i = 1U; i + 1U < NFFT; i++) {
            float v = s_fft[2U*i];
            float f = casilla_a_audio(i);

            if (v <= s_fft[2U*(i-1U)] || v < s_fft[2U*(i+1U)]) { continue; }
            if (v * (float)TONO_CAIDA < mx) { continue; }
            if (f < 100.0f || f > 3800.0f) { continue; }   /* fuera del audio */
            if (realce(f) < TONO_REALCE) { continue; }

            /* Insercion ordenada por frecuencia, quedandose con las mas
             * fuertes si se llena. */
            if (nc < DET_TONOS_MAX) {
                a = nc;
                while (a > 0U && cf[a - 1U] > f) {
                    cf[a] = cf[a - 1U]; cp[a] = cp[a - 1U]; a--;
                }
                cf[a] = f; cp[a] = v; nc++;
            } else {
                uint8_t flojo = 0U;
                for (a = 1U; a < nc; a++) { if (cp[a] < cp[flojo]) { flojo = a; } }
                if (v > cp[flojo]) {
                    for (a = flojo; a + 1U < nc; a++) { cf[a] = cf[a+1U]; cp[a] = cp[a+1U]; }
                    a = (uint8_t)(nc - 1U);
                    while (a > 0U && cf[a - 1U] > f) {
                        cf[a] = cf[a - 1U]; cp[a] = cp[a - 1U]; a--;
                    }
                    cf[a] = f; cp[a] = v;
                }
            }
        }

        /* Y una sola raya por entorno: lo que cae a menos de TONO_SEP_MIN es
         * la falda de la de al lado, no otro tono. Ya estan ordenadas. */
        a = 0U;
        while (a + 1U < nc) {
            if ((cf[a + 1U] - cf[a]) < TONO_SEP_MIN) {
                uint8_t tira = (cp[a] >= cp[a + 1U]) ? (uint8_t)(a + 1U) : a;
                for (b = tira; b + 1U < nc; b++) { cf[b] = cf[b+1U]; cp[b] = cp[b+1U]; }
                nc--;
            } else {
                a++;
            }
        }

        /*
         * ANTES DE CONTAR NADA: ¿ESO SON TONOS O ES UNA PSK ANCHA?
         *
         * El 4285 tiene un espectro erizado y SIEMPRE hay dos o tres crestas
         * que sobresalen de su ruido local. Contarlas da "2 señales" sobre
         * una sola emision, que es un aviso falso en pantalla.
         *
         * Lo que las separa es lo mismo que ya separaba una FSK de una PSK
         * en la puerta de abajo, y esta medido en este fichero desde el
         * 02/10: los tonos de una FSK se llevan CASI TODA la potencia -60 a
         * 68% en los SITOR- y las crestas de una PSK no -7 a 14% en los
         * STANAG-. Asi que si las rayas que he listado no suman TONO_PARTE
         * del total, no son tonos y aqui no hay nada que contar ni que
         * agrupar: se cae al barrido de siempre.
         */
        {
            float en = 0.0f;
            for (a = 0U; a < nc; a++) {
                for (i = 0U; i < NFFT; i++) {
                    float f = casilla_a_audio(i);
                    float d = (f > cf[a]) ? (f - cf[a]) : (cf[a] - f);
                    if (d <= TONO_ANCHO) { en += s_fft[2U*i]; }
                }
            }
            if ((en / tot) < TONO_PARTE) { nc = 0U; }
        }

        s_r.n_senales = (uint8_t)((nc + 1U) / 2U);

        if (nc >= 2U) {
            /*
             * SE EMPAREJA POR CERCANIA -el hueco mas pequeño
             * primero- y se publica la pareja de la raya MAS FUERTE, que es
             * la señal que se esta oyendo mas alto y la que el usuario va a
             * querer sintonizar.
             */
            uint8_t usada[DET_TONOS_MAX];
            uint8_t mejor = 0U;

            for (a = 0U; a < nc; a++) { usada[a] = 0U; }
            for (a = 1U; a < nc; a++) { if (cp[a] > cp[mejor]) { mejor = a; } }

            for (;;) {
                float dmin = 1e30f;
                uint8_t ia = 0U, ib = 0U, hay = 0U;

                for (a = 0U; a < nc; a++) {
                    if (usada[a]) { continue; }
                    for (b = (uint8_t)(a + 1U); b < nc; b++) {
                        if (usada[b]) { continue; }
                        if ((cf[b] - cf[a]) < dmin) {
                            dmin = cf[b] - cf[a]; ia = a; ib = b; hay = 1U;
                        }
                    }
                }
                if (!hay) { break; }
                usada[ia] = 1U; usada[ib] = 1U;
                if (ia == mejor || ib == mejor) {
                    f1 = cf[ia]; p1 = cp[ia];
                    f2 = cf[ib]; p2 = cp[ib];
                    break;
                }
            }
            /* Lo que se llevan las OTRAS señales no cuenta como "potencia
             * del paso de banda" para esta: si no, con tres RTTY ninguna
             * llegaria nunca al 35% y no habria tonos para nadie. Con una
             * sola señal este bucle no descuenta nada y `tot` se queda como
             * estaba, que es lo que mide las nueve grabaciones de siempre. */
            for (a = 0U; a < nc; a++) {
                float d1 = (cf[a] > f1) ? (cf[a] - f1) : (f1 - cf[a]);
                float d2 = (cf[a] > f2) ? (cf[a] - f2) : (f2 - cf[a]);
                if (d1 <= TONO_ANCHO || d2 <= TONO_ANCHO) { continue; }
                for (i = 0U; i < NFFT; i++) {
                    float f = casilla_a_audio(i);
                    float d = (f > cf[a]) ? (f - cf[a]) : (cf[a] - f);
                    if (d <= TONO_ANCHO) { tot -= s_fft[2U*i]; }
                }
            }
            if (!(tot > 0.0f)) { return; }
        } else {
            /*
             * MENOS DE DOS RAYAS CON REALCE. Se cae al barrido de siempre -la
             * mas alta de todo lo que este a mas de TONO_SEP_MIN-, que es lo
             * que habia antes del 10/10/2026. Aqui no hay nada que agrupar:
             * o es una sola señal, o no hay tonos y las puertas de abajo lo
             * van a tirar igual.
             */
            for (i = 0U; i < NFFT; i++) {
                float v = s_fft[2U*i];
                float f = casilla_a_audio(i);
                float d = (f > f1) ? (f - f1) : (f1 - f);
                if (d < TONO_SEP_MIN) { continue; }
                if (v > p2) { p2 = v; f2 = f; }
            }
            if (s_r.n_senales == 0U) { s_r.n_senales = 1U; }
        }
    }
    /*
     * Dos rayas fuertes separadas mas de lo que cabe en una FSK son DOS
     * señales, aunque solo se vean esas dos. Que la cuenta lo diga aunque
     * el par no se publique: "2 señales" es informacion util, y "ningun
     * tono" a secas se leeria como "no hay nada ahi".
     */
    if (p2 > 0.0f && p1 > 0.0f && p2 * (float)TONO_CAIDA >= p1
        && ((f2 > f1) ? (f2 - f1) : (f1 - f2)) > TONO_SEP_MAX
        && s_r.n_senales == 1U) {
        /* Solo si ya se habia decidido que eso son tonos (n_senales 1). Con
         * n_senales 0 -una PSK ancha- esto estaria inventando dos señales a
         * partir de dos crestas de ruido, que es el aviso falso que daba el
         * STANAG 4285. */
        s_r.n_senales = 2U;
    }

    if (p2 > 0.0f && p1 > 0.0f && p2 * (float)TONO_CAIDA >= p1) {
        /*
         * Y AHORA LA PRUEBA QUE DE VERDAD DECIDE, QUE LA PRIMERA VERSION NO
         * TENIA Y POR ESO DABA "DOS TONOS" EN TODO.
         *
         * Que dos rayas esten a menos de 10 dB una de otra no dice nada: en
         * el espectro erizado de una PSK ancha SIEMPRE hay dos picos de
         * ruido parecidos, y el detector decia que un STANAG 4285 eran dos
         * tonos separados 160 Hz. Lo medido contra las nueve grabaciones:
         * el criterio de los 10 dB sacaba hay_tonos=1 en las NUEVE.
         *
         * Lo que separa una FSK de verdad es que sus dos tonos se llevan
         * CASI TODA la potencia, porque ahi es donde esta la señal y no hay
         * otro sitio. Sumando lo que cae a +-25 Hz de cada raya:
         *
         *     SITOR de 8424 kHz ....  60%        STANAG 4285 ....  7%
         *     SITOR de Oostende ....  68%        STANAG 4415 ....  8%
         *                                        STANAG 4529 .... 14%
         *
         * Con 35% se parten por la mitad y sobra sitio por los dos lados.
         */
        float en1 = 0.0f, en2 = 0.0f;
        float r1, r2;

        for (i = 0U; i < NFFT; i++) {
            float f = casilla_a_audio(i);
            float d1 = (f > f1) ? (f - f1) : (f1 - f);
            float d2 = (f > f2) ? (f - f2) : (f2 - f);
            if (d1 <= TONO_ANCHO)      { en1 += s_fft[2U*i]; }
            else if (d2 <= TONO_ANCHO) { en2 += s_fft[2U*i]; }
        }
        r1 = realce(f1);
        r2 = realce(f2);
        /*
         * CADA UNO LA SUYA, Y ESTO ES LO QUE DESCARTA UNA PORTADORA.
         *
         * Sumando las dos rayas juntas, un heterodino fuerte con una
         * cresta de ruido al lado pasa el corte el solo: la portadora pone
         * el 39% y la "segunda raya" el 2%. Exigiendo que CADA UNA llegue
         * al 12% eso se cae, porque una FSK de verdad reparte el tiempo
         * entre sus dos tonos y las dos salen parecidas.
         *
         * Medido en la grabacion de portadora (02/10, 23:10):
         * 39% y 2%. En el SITOR de 8424 kHz: 31% y 29%.
         */
#ifdef DET_DBG
        { extern void det_dbg_tonos(double f1,double f2,double e1,double e2);
          det_dbg_tonos((double)f1,(double)f2,(double)(en1/tot),(double)(en2/tot)); }
#endif
        /*
         * Y el veredicto: las dos juntas se llevan casi toda la potencia
         * (eso descarta las PSK anchas) Y cada una sobresale de su propio
         * ruido (eso descarta una portadora con una cresta al lado, y
         * aguanta el desvanecimiento selectivo). Ver TONO_REALCE.
         */
        /*
         * Y QUE ENTRE LAS DOS HAYA UN VALLE - 06/10/2026.
         *
         * Sin esto, el arreglo del realce hizo que el detector encontrara
         * dos tonos el 90 % del tiempo en la grabacion de 5339 -frente al 2
         * % de antes- pero la separacion bailaba entre 124 y 876 Hz. O sea
         * que en algunas ventanas la "segunda raya" era la FALDA de la
         * primera, no el otro tono.
         *
         * Y eso importa mas de lo que parece: ident_casa() busca la fila por
         * la separacion, asi que una separacion que baila es un nombre que
         * baila. Es exactamente lo que se veia: el modo que daba el
         * Ident iba cambiando entre varios.
         *
         * Subir TONO_SEP_MIN no vale: el RTTY de aficionado va a 170 Hz de
         * salto y se quedaria fuera. Lo que distingue dos rayas de una raya
         * ancha es que entre ellas la señal BAJA. Se mira el minimo del
         * tramo de en medio: tiene que estar por debajo de la menor de las
         * dos rayas, y por un margen.
         */
        if (r1 >= TONO_REALCE && r2 >= TONO_REALCE
            && ((f2 > f1) ? (f2 - f1) : (f1 - f2)) <= TONO_SEP_MAX
            && hay_valle(f1, f2)
            && ((en1 + en2) / tot) >= TONO_PARTE) {
            s_r.hay_tonos = 1U;
            s_r.tono_bajo = (uint16_t)(((f1 < f2) ? f1 : f2) + 0.5f);
            s_r.tono_alto = (uint16_t)(((f1 > f2) ? f1 : f2) + 0.5f);
            s_r.sep_hz    = (uint16_t)(s_r.tono_alto - s_r.tono_bajo);
        }
    }
    s_r.hay_esp = 1U;
}

uint8_t stanag_det_paso(stanag_det_t *out)
{
    uint32_t i, r, mejor_r = 0U, cuenta = 0U;
    float a0, mejor = 0.0f, suma = 0.0f, suma2 = 0.0f, sigma;

    if (!s_vale || !s_listo) { return 0U; }
    s_listo = 0U;
    s_pasos++;

    /*
     * El anillo en orden, y DETRAS MIL VEINTICUATRO CEROS. Ese relleno es
     * lo que convierte la autocorrelacion circular en lineal: sin el, la
     * cola de la señal se correlaciona con su propio principio y aparecen
     * picos que no existen, justo en los retardos largos que son los que
     * nos interesan.
     */
    for (i = 0U; i < STANAG_DET_N; i++) {
        uint32_t k = s_w + i; if (k >= STANAG_DET_N) { k -= STANAG_DET_N; }
        s_fft[2U*i]    = s_anillo[2U*k];
        s_fft[2U*i+1U] = s_anillo[2U*k+1U];
    }
    for (i = STANAG_DET_N; i < NFFT; i++) {
        s_fft[2U*i] = 0.0f; s_fft[2U*i+1U] = 0.0f;
    }

    /*
     * LA COHERENCIA SE SACA AQUI, ANTES DE LA TRANSFORMADA - 19/10/2026.
     *
     * No es un capricho de orden: simb_acumula() necesita LA VENTANA, y
     * aqui la ventana esta en s_fft[] en orden y recien copiada. Despues
     * de fft() ahi hay un espectro y la ventana ya no existe en ningun
     * sitio, que es por lo que hasta hoy se iba a buscarla al ANILLO con
     * tres contadores dando la vuelta - y leyendo, de paso, un buffer que
     * la interrupcion del audio sigue escribiendo. Ver su comentario.
     *
     * No depende del espectro para nada: lee la ventana, escribe s_coh[] y
     * s_simbac[]. Y mide_simbolos(), que si depende de lo que esto deja,
     * sigue detras.
     */
    simb_acumula();

    fft(s_fft, 0U);
    for (i = 0U; i < NFFT; i++) {
        float xr = s_fft[2U*i], xi = s_fft[2U*i+1U];
        s_fft[2U*i]    = xr*xr + xi*xi;
        s_fft[2U*i+1U] = 0.0f;
    }

    /*
     * AQUI, Y SOLO AQUI, EXISTE EL ESPECTRO. La inversa de la linea
     * siguiente lo machaca para dejar la autocorrelacion en su sitio, asi
     * que todo lo que se quiera saber del espectro hay que sacarlo ahora.
     * No se guarda: se mide y quedan seis numeros.
     */
    mide_espectro();
    mide_simbolos();
    mide_rachas();

    fft(s_fft, 1U);

    /*
     * UNA PASADA ES UNA PASADA, HAYA SEÑAL O NO - 02/10/2026.
     *
     * Aqui habia cuatro `return 0` repartidos antes de contar nada: señal
     * a cero, faltan promedios, sin muestras, sin dispersion. Los cuatro
     * dejaban al modo sin enterarse de que esto habia corrido. Y como el
     * panel se repinta cuando sube la cuenta de medidas, la pantalla se
     * quedaba con los contadores del primer instante.
     *
     * Pedido por el usuario. No estaba parado. Nadie repintaba.
     *
     * Ahora esto devuelve 1 siempre que ha procesado una ventana, y es
     * `out->vale` quien dice si ademas hay trama. Asi el que llama puede
     * enseñar que esto esta vivo aunque no vea nada.
     */
    a0 = s_fft[0];
    if (!(a0 > 0.0f)) { a0 = 1.0f; }   /* tambien pilla el NaN, que no es > 0 */

    /*
     * La media de PROMEDIOS pasadas, como LimeAuto. Una sola pasada tiene
     * tanto ruido que cualquier cosa parece un pico; promediando, lo que
     * no se repite se va y lo que si se repite se queda, que es
     * exactamente la diferencia entre ruido y trama.
     *
     * Se toma el MODULO y no la parte real: el oscilador de aqui no esta
     * enganchado a la portadora de la señal, asi que entre una trama y la
     * siguiente la fase ha girado lo que le haya dado la gana. El pico
     * sigue estando; lo que no se puede dar por supuesto es su fase.
     */
    {
        float peso = (s_promedios < PROMEDIOS)
                     ? (1.0f / (float)(s_promedios + 1U))
                     : (1.0f / (float)PROMEDIOS);
        for (i = 0U; i < RETARDO_MAX; i++) {
            float re = s_fft[2U*i], im = s_fft[2U*i+1U];
            /*
             * DIVIDIR POR LAS MUESTRAS QUE DE VERDAD SE SOLAPAN. Esto
             * faltaba y era el fallo gordo: con el relleno de ceros, al
             * retardo r solo se solapan (N - r) muestras, asi que la
             * autocorrelacion cruda BAJA SOLA segun el retardo aunque la
             * señal sea igual de parecida a si misma. El banco lo caza de
             * inmediato: sin esto, el "pico mas alto" que salia era el
             * principio de esa cuesta -el retardo minimo- y no la trama.
             *
             * Dividiendo, lo que queda es cuanto se parece la señal a si
             * misma POR MUESTRA, que es lo que se queria medir.
             */
            float v = sqrtf(re*re + im*im) / (a0 * (float)(STANAG_DET_N - i));
            s_acf[i] += peso * (v - s_acf[i]);
        }
        if (s_promedios < PROMEDIOS) { s_promedios++; }
    }

    s_r.vale = 0U; s_r.periodo_us = 0U; s_r.retardo = 0U;
    s_r.picos = 0U; s_r.calidad = 0U;

    if (s_promedios < PROMEDIOS) {
        if (out) { *out = s_r; }    /* aun no hay media, pero la pasada cuenta */
        return 1U;
    }

    /*
     * La desviacion tipica de lo que NO es pico, para saber que es pico.
     * Se mide sobre la diferencia entre vecinos y no sobre la
     * autocorrelacion cruda: esta baja sola segun el retardo -hay menos
     * solape-, y una pendiente no es dispersion.
     */
    for (r = RETARDO_MIN; r < (RETARDO_MAX - 2U); r++) {
        float d = s_acf[r] - s_acf[r + 1U];
        suma += d; suma2 += d*d; cuenta++;
    }
    if (cuenta == 0U) { if (out) { *out = s_r; } return 1U; }
    {
        float m = suma / (float)cuenta;
        float v = (suma2 / (float)cuenta) - (m * m);
        sigma = (v > 0.0f) ? sqrtf(v) : 0.0f;
    }
    /* Sin dispersion no hay con que comparar un pico: señal plana o
     * silencio. La pasada cuenta igual. */
    if (!(sigma > 0.0f)) { if (out) { *out = s_r; } return 1U; }

    /* El pico mas alto por encima de su entorno. La linea base es la media
     * de una ventana ancha alrededor, que es el zmin de LimeAuto con otro
     * nombre. */
    for (r = RETARDO_MIN; r < (RETARDO_MAX - BASE_VENT); r++) {
        uint32_t j, j0 = (r > (BASE_VENT/2U)) ? (r - BASE_VENT/2U) : 1U;
        uint32_t j1 = j0 + BASE_VENT;
        float s = 0.0f, alto;

        /*
         * Y que sea un MAXIMO LOCAL de verdad, no un punto cualquiera de
         * una meseta. Una portadora sola se parece a si misma a todos los
         * retardos: sin esta condicion, el primer punto que pasara el
         * umbral se daria por trama, y el banco enseña justo eso -una
         * portadora pura detectada como trama de 10 ms-.
         */
        if (s_acf[r] < s_acf[r-1U] || s_acf[r] < s_acf[r+1U]) { continue; }
        if (s_acf[r] <= s_acf[r-3U] || s_acf[r] <= s_acf[r+3U]) { continue; }

        for (j = j0; j < j1; j++) { s += s_acf[j]; }
        alto = s_acf[r] - (s / (float)(j1 - j0));
        if (alto > mejor) { mejor = alto; mejor_r = r; }
    }


    /*
     * EL PICO MAS ALTO NO ES EL PERIODO: EL PERIODO ES EL MAS PEQUEÑO.
     *
     * Una trama de 106 ms deja pico en 106, en 213, en 320... y con ruido
     * encima cualquiera de ellos puede salir mas alto que el primero. El
     * banco lo enseño: con el ruido a la par de la señal, una trama de 320
     * muestras se media como de 640, que es su doble exacto. No es un
     * fallo de medida -los dos picos existen de verdad- sino de eleccion.
     *
     * Asi que, una vez encontrado el mas alto, se mira si su mitad, su
     * tercio o su cuarto tambien son pico. Si lo son, ESE es el periodo y
     * el alto era un multiplo suyo. Es el mismo problema -y el mismo
     * arreglo- que tiene cualquier detector de tono fundamental.
     */
    if (mejor_r != 0U) {
        uint32_t k;
        for (k = 4U; k >= 2U; k--) {
            uint32_t c = mejor_r / k;
            float ent;
            if (c < RETARDO_MIN || (mejor_r % k) > 2U) { continue; }
            ent = s_acf[c] - ((s_acf[c - 3U] + s_acf[c + 3U]) * 0.5f);
            if (ent > (QECART * sigma)) {
                mejor_r = c;
                mejor   = ent;
                break;
            }
        }
    }

    if (mejor_r != 0U && mejor > (QECART * sigma)) {
        /*
         * Donde cae el pico DE VERDAD, que casi nunca es en una muestra
         * justa. Con los tres puntos de alrededor se ajusta una parabola y
         * su vertice da el retardo con una fraccion de muestra. A 3 kHz
         * cada muestra son 333 us, asi que sin esto no se podria separar
         * una trama de 106,67 ms de otra de 107,00.
         */
        float ym = s_acf[mejor_r - 1U], y0 = s_acf[mejor_r], yp = s_acf[mejor_r + 1U];
        float den = (ym - 2.0f*y0 + yp);
        float ajuste = (den != 0.0f) ? (0.5f * (ym - yp) / den) : 0.0f;
        float retardo;

        if (ajuste > 1.0f || ajuste < -1.0f) { ajuste = 0.0f; }
        retardo = (float)mejor_r + ajuste;              /* en muestras de 3 kHz */

        /* Cuantos multiplos se ven tambien. Un pico suelto puede ser
         * cualquier cosa; una trama deja su rastro en 2T, 3T... */
        for (i = 2U; i <= 3U; i++) {
            uint32_t k = (uint32_t)((retardo * (float)i) + 0.5f);
            if ((k + 2U) >= RETARDO_MAX) { break; }
            if ((s_acf[k] - ((s_acf[k-2U] + s_acf[k+2U]) * 0.5f)) > (QECART * sigma)) {
                s_r.picos++;
            }
        }

        s_r.retardo    = (uint16_t)(retardo + 0.5f);
        /* Decenas de microsegundos: retardo/3000 segundos -> x1e5. */
        s_r.periodo_us = (uint16_t)(((retardo * 100000.0f) / (float)STANAG_DET_HZ) + 0.5f);
        {
            float q = (mejor / (QECART * sigma)) * 25.0f;
            if (q > 100.0f) { q = 100.0f; }
            s_r.calidad = (uint8_t)q;
        }
        s_r.picos++;   /* el fundamental cuenta */
        /*
         * Y NO SE DA POR BUENA SIN AL MENOS UN MULTIPLO. Esta es la
         * condicion que de verdad separa una trama de un golpe de suerte:
         * una señal con trama la repite una y otra vez, asi que deja su
         * rastro en 2T y en 3T. El ruido puede poner un pico en cualquier
         * sitio, pero ponerlo ADEMAS en el doble de ese sitio ya no es
         * casualidad.
         *
         * Una portadora sola tambien cae por aqui: se parece a si misma a
         * todos los retardos por igual, asi que no tiene ni fundamental ni
         * multiplos, solo una meseta.
         *
         * PERO SOLO SE PUEDE EXIGIR CUANDO HAY SITIO PARA MIRARLO. Si el
         * periodo pasa de la mitad del rango, su doble cae fuera del
         * anillo y la condicion seria imposible de cumplir: el banco lo
         * enseño con una trama de 213 ms, que se rechazaba no por mala
         * sino por larga. En ese caso se pide mas altura al fundamental en
         * vez de un multiplo que no se puede ver.
         */
        if (((uint32_t)(retardo * 2.0f) + 2U) < RETARDO_MAX) {
            s_r.vale = (s_r.picos >= 2U) ? 1U : 0U;
        } else {
            s_r.vale = (mejor > (2.0f * QECART * sigma)) ? 1U : 0U;
        }
        if (!s_r.vale) {
            s_r.periodo_us = 0U; s_r.retardo = 0U;
            s_r.picos = 0U; s_r.calidad = 0U;
        }
    }

    if (out) { *out = s_r; }
    return 1U;
}

void stanag_det_diag(uint32_t *muestras, uint32_t *llenas, uint8_t *pasadas)
{
    if (muestras) { *muestras = s_metidas; }
    if (llenas)   { *llenas   = s_llenas; }
    if (pasadas)  { *pasadas  = (uint8_t)((s_pasos > 255UL) ? 255UL : s_pasos); }
}
