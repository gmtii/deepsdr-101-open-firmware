#include "semibanda.h"
#include <string.h>

/*
 * Las NUEVE tomas que de verdad se multiplican, de fuera hacia dentro:
 * h[0]=h[34], h[2]=h[32], ... h[16]=h[18]. Las impares son cero menos la
 * del centro, que es H_CENTRO. Ver el comentario de la cabecera.
 *
 * Salen de remez(35, [0, 40k, 56k, 96k], [1,0], fs=192k), con las tomas
 * pares forzadas a cero -el diseño ya las deja en el orden de 1e-16- y la
 * suma normalizada a uno exacto para que la continua no cambie de nivel.
 */
#define H_CENTRO  0.499061685f

static const float k_h[9] = {
     0.002334458f,   /* h[ 0] = h[34] */
    -0.004173014f,   /* h[ 2] = h[32] */
     0.007692380f,   /* h[ 4] = h[30] */
    -0.013036249f,   /* h[ 6] = h[28] */
     0.021048924f,   /* h[ 8] = h[26] */
    -0.033419058f,   /* h[10] = h[24] */
     0.054483553f,   /* h[12] = h[22] */
    -0.100254585f,   /* h[14] = h[20] */
     0.315792748f    /* h[16] = h[18] */
};

/* La cola del bloque anterior: las 34 ultimas muestras de cada rail. */
static float s_hi[SEMIBANDA_HIST];
static float s_hq[SEMIBANDA_HIST];

/* Y la del camino de vuelta, declarada aqui arriba porque la vacia el
 * mismo reset. Ver semibanda_interpola(). */
/* Tres muestras: la salida par mira x[m]..x[m-3]. Ver semibanda_interpola(). */
#define INTERP_HIST  3U
static float s_ih[INTERP_HIST];

void semibanda_reset(void)
{
    memset(s_hi, 0, sizeof s_hi);
    memset(s_hq, 0, sizeof s_hq);
    memset(s_ih, 0, sizeof s_ih);
}

/*
 * Una muestra de entrada por su indice ABSOLUTO dentro del bloque, que
 * puede ser negativo: los negativos vienen de la cola del bloque
 * anterior, y los primeros SEMIBANDA_HIST de la copia que se hizo antes
 * de empezar a pisar.
 *
 * Esta funcion existe para que el bucle de abajo se lea; el compilador la
 * mete en linea con -O2 (comprobado en el desensamblado: no queda ni una
 * llamada).
 */
static inline float toma(const float *x, const float *hist,
                         const float *ini, int32_t n)
{
    if (n < 0)                        { return hist[SEMIBANDA_HIST + n]; }
    if (n < (int32_t)SEMIBANDA_HIST)  { return ini[n]; }
    return x[n];
}

/*
 * ======================================================================
 * DOS BUCLES Y NO UNO, Y ESTE ES EL MOTIVO - 24/10/2026
 * ======================================================================
 *
 * La primera version tenia UN solo bucle que sacaba cada muestra con
 * toma(), o sea con dos comparaciones y un salto impredecible POR CADA
 * ACCESO. Son diecinueve accesos por muestra de salida, por los dos
 * raíles: casi veinte mil saltos por bloque.
 *
 * En el papel eran "nueve multiplicaciones por salida, un 3 %". En la
 * radio, lo que hubo fue audio roto y la interfaz a rastras, porque el
 * coste no estaba en las multiplicaciones sino en los saltos. La leccion
 * no es nueva en este arbol -es la misma del camino escalar del CMSIS- y
 * la escribo aqui porque la volvi a cometer: una cuenta de
 * multiplicaciones NO es una medida de coste.
 *
 * Asi que el trabajo se parte en dos:
 *
 *   LAS PRIMERAS 34 SALIDAS miran hacia atras -a la cola del bloque
 *   anterior o a la copia de las primeras muestras- y siguen con
 *   toma(). Son treinta y cuatro de doscientas cincuenta y seis.
 *
 *   LAS DEMAS, de la 34 en adelante, NO MIRAN HACIA ATRAS NUNCA: el
 *   indice mas bajo que leen es 2m-34, y con m >= 34 eso es >= m, o sea
 *   que esta por DELANTE de lo ya escrito. Ni una comparacion, ni un
 *   salto: una ventana de 35 flotantes seguidos y nueve multiplicaciones
 *   con la simetria aprovechada. Eso es el 87 % de las salidas.
 */
static void un_rail(float *x, float *hist, uint32_t n_sal)
{
    float ini[SEMIBANDA_HIST];
    uint32_t m;

    /* Lo que el bucle va a pisar antes de haberlo leido. Ver la cabecera. */
    memcpy(ini, x, sizeof ini);

    /* --- las que miran hacia atras --- */
    for (m = 0U; m < SEMIBANDA_HIST && m < n_sal; m++) {
        int32_t c = (int32_t)(2U * m);
        float acc = H_CENTRO * toma(x, hist, ini, c - 17);
        uint32_t j;

        for (j = 0U; j < 9U; j++) {
            int32_t a = c - (int32_t)(2U * j);          /* toma 2j     */
            int32_t b = c - (int32_t)(34U - 2U * j);    /* toma 34-2j  */

            acc += k_h[j] * (toma(x, hist, ini, a) + toma(x, hist, ini, b));
        }
        x[m] = acc;
    }

    /* --- y las demas, sin una sola bifurcacion ---
     *
     * p[t] es la entrada x[2m-34+t], asi que la toma k multiplica a
     * p[34-k]: la del centro -k=17- es p[17], y el par (2j, 34-2j) es
     * (p[34-2j], p[2j]). Como los dos llevan el mismo coeficiente, se
     * suman antes de multiplicar. */
    for (; m < n_sal; m++) {
        const float *p = x + (2U * m - SEMIBANDA_HIST);

        /*
         * TRES ACUMULADORES Y NO UNO. Sumar las nueve en fila hace que
         * cada suma espere a la anterior -una cadena de dependencias de
         * nueve eslabones- y la unidad de coma flotante se pasa la mitad
         * del tiempo parada. Con tres cadenas independientes avanzan a la
         * vez. El resultado cambia en el ultimo bit y da igual: esto es
         * un filtro de audio, no una suma de dinero.
         */
        float a0 = H_CENTRO * p[17] + k_h[0] * (p[0]  + p[34]);
        float a1 = k_h[1] * (p[2]  + p[32]) + k_h[2] * (p[4]  + p[30]);
        float a2 = k_h[3] * (p[6]  + p[28]) + k_h[4] * (p[8]  + p[26]);

        a0 += k_h[5] * (p[10] + p[24]);
        a1 += k_h[6] * (p[12] + p[22]);
        a2 += k_h[7] * (p[14] + p[20]);
        a0 += k_h[8] * (p[16] + p[18]);

        x[m] = (a0 + a1) + a2;
    }

    /* Y la cola para el bloque siguiente: las 34 ultimas de la ENTRADA,
     * no de la salida. Se cogen de donde todavia estan sin pisar -los
     * indices altos- o de `ini` si el bloque fuera tan corto que la
     * salida hubiera llegado hasta ahi. */
    {
        uint32_t n_ent = 2U * n_sal;
        uint32_t k;

        for (k = 0U; k < SEMIBANDA_HIST; k++) {
            int32_t n = (int32_t)(n_ent - SEMIBANDA_HIST + k);

            hist[k] = (n < (int32_t)n_sal) ? ini[n] : x[n];
        }
    }
}

/* Las dos tramas de salida de una de entrada, recortadas y en estereo. */
static void semib_pon(int16_t *e, uint32_t m, float par, float impar)
{
    int32_t a = (int32_t)(par   + (par   >= 0.0f ? 0.5f : -0.5f));
    int32_t b = (int32_t)(impar + (impar >= 0.0f ? 0.5f : -0.5f));

    if (a >  32767) { a =  32767; }
    if (a < -32768) { a = -32768; }
    if (b >  32767) { b =  32767; }
    if (b < -32768) { b = -32768; }

    e[4U * m]      = (int16_t)a;
    e[4U * m + 1U] = (int16_t)a;
    e[4U * m + 2U] = (int16_t)b;
    e[4U * m + 3U] = (int16_t)b;
}

void semibanda_diezma(float *i, float *q, uint32_t n_sal)
{
    if (i == 0 || q == 0 || n_sal == 0U) { return; }
    /* Con bloques mas cortos que la historia la cuenta de la cola de
     * arriba no vale, y ademas no hay ningun camino que lo pida: el
     * bloque son 256 salidas. */
    if (2U * n_sal < SEMIBANDA_TOMAS) { return; }

    un_rail(i, s_hi, n_sal);
    un_rail(q, s_hq, n_sal);
}

/* ---------------------------------------------------------------------
 * EL CAMINO DE VUELTA. Ver semibanda.h.
 * --------------------------------------------------------------------- */

/*
 * ======================================================================
 * EL DE VUELTA ES MUCHO MAS CORTO QUE EL DE IDA, Y TIENE MOTIVO.
 * ======================================================================
 *
 * La primera version uso las MISMAS treinta y cinco tomas que el
 * diezmador, "por simetria". Medido en el anfitrion salio mas caro que el
 * diezmador entero -5.280 ns contra 4.303- y fue lo que dejo la cascada a
 * rastras, porque el tiempo que se come la interrupcion de audio se lo
 * quita al bucle principal, que es quien pinta.
 *
 * Y sobraba casi entero. Las dos puntas de este camino NO son el mismo
 * problema:
 *
 *   AL BAJAR, lo que hay que rechazar es RADIOFRECUENCIA, y puede haber
 *   una emisora fuerte justo en la frecuencia que se dobla encima de la
 *   que se escucha. Por eso alli hacen falta 54 dB y una banda de paso
 *   que llegue a 40 kHz.
 *
 *   AL SUBIR, lo que hay es AUDIO, y el audio ya ha pasado por el filtro
 *   de audio: seis kilohercios como mucho. Las imagenes que crea meter
 *   ceros caen en 96-6 = 90 kHz y por encima. O sea que hay OCHENTA Y DOS
 *   kilohercios de banda de transicion para gastar, y con tanto sitio el
 *   filtro se queda en nada.
 *
 * SIETE TOMAS, de las que se multiplican DOS:
 *
 *     rizado 0..8 kHz        0,002 dB
 *     imagen desde 88 kHz    78,6 dB
 *
 * Y las muestras impares siguen siendo una copia retrasada de la entrada,
 * sin una sola multiplicacion, por lo mismo de siempre: con los ceros
 * intercalados la unica toma impar viva es la del centro.
 *
 * (Probe tambien la interpolacion lineal a secas, que seria gratis: deja
 * la imagen a 35 dB. Ultrasonica y seguramente inaudible, pero 35 dB de
 * basura a 90 kHz entrando en el amplificador no es algo que se regale
 * por ahorrar dos multiplicaciones.)
 */
#define INTERP_CENTRO  0.500052715f
static const float k_ih[2] = {
    -0.032899415f,   /* h[0] = h[6] */
     0.282873057f    /* h[2] = h[4] */
};

void semibanda_interpola(int16_t *estereo, uint32_t n_ent)
{
    float cola[INTERP_HIST];
    uint32_t m, k;

    if (estereo == 0 || n_ent == 0U) { return; }

    /*
     * LA COLA, ANTES DE PISAR NADA: las tres ultimas muestras de la
     * ENTRADA, que va a desaparecer debajo de la salida.
     */
    for (k = 0U; k < INTERP_HIST; k++) {
        int32_t n = (int32_t)(n_ent - INTERP_HIST + k);

        cola[k] = (n < 0) ? s_ih[INTERP_HIST + n] : (float)estereo[2 * n];
    }

    /*
     * HACIA ATRAS, que es lo que permite hacerlo en sitio sin copiar la
     * entrada: la trama m de entrada vive en el indice 2m y sus dos
     * tramas de salida van a 4m y 4m+2. Yendo de la ultima a la primera,
     * lo ya escrito empieza en 4m+4 y lo que queda por leer acaba en 2m.
     *
     * Y EN DOS TRAMOS, igual que el diezmador y por lo mismo: de la
     * muestra 3 en adelante no hay nada que mirar hacia atras, asi que
     * ese bucle no lleva ni una bifurcacion. Son el 99 % de las muestras.
     */
    for (m = n_ent; m-- > INTERP_HIST; ) {
        const float x0 = (float)estereo[2 * m];
        const float x1 = (float)estereo[2 * (m - 1U)];
        const float x2 = (float)estereo[2 * (m - 2U)];
        const float x3 = (float)estereo[2 * (m - 3U)];
        float par   = 2.0f * (k_ih[0] * (x0 + x3) + k_ih[1] * (x1 + x2));
        float impar = (2.0f * INTERP_CENTRO) * x1;

        semib_pon(estereo, m, par, impar);
    }
    /*
     * ==================================================================
     * *** ESTE BUCLE EMPEZABA EN INTERP_HIST + 1, Y ESO ERA DOS COSAS.
     * 26/10/2026, revision a fondo de espejos y armonicos. ***
     * ==================================================================
     *
     * 1. LA TRAMA 3 SE CALCULABA DOS VECES. El bucle de arriba baja hasta
     *    m == INTERP_HIST -o sea, su ultima vuelta es la 3- y este
     *    arrancaba en 4, con lo que su primera vuelta volvia a ser la 3.
     *    El resultado sale identico -los indices que lee, 0 a 6, todavia
     *    no los ha pisado nadie: semib_pon() para m=3 escribe en 12 y 14-,
     *    asi que no rompia nada. Era una trama entera recalculada por
     *    gusto, 256 de ellas por segundo.
     *
     * 2. Y CON UN BLOQUE CORTO SE SALIA DEL BUFFER. La unica guarda de
     *    esta funcion es n_ent == 0. Con n_ent = 2, el bucle de arriba no
     *    llega a correr -2 > 3 es falso- y este escribia las tramas 3 y 2,
     *    que no existen: semib_pon() para m=3 escribe en los indices 12 y
     *    14 de un buffer que solo tiene ocho. Hoy no pasa porque n_ent es
     *    siempre el bloque entero -256 o 512-, pero la funcion es publica
     *    y su cabecera no lo exige en ningun sitio.
     *
     * El tope lo arregla a la vez: con un bloque normal vale 3 y este
     * bucle hace 2, 1 y 0 -la 3 ya la hizo el de arriba-; con un bloque
     * de 2 vale 2 y hace 1 y 0, que son las que hay.
     */
    {
        uint32_t tope = (n_ent < (uint32_t)INTERP_HIST)
                        ? n_ent : (uint32_t)INTERP_HIST;

    for (m = tope; m-- > 0U; ) {
        float par, impar;
        float x[4];
        uint32_t t;

        for (t = 0U; t < 4U; t++) {
            int32_t n = (int32_t)m - (int32_t)t;

            x[t] = (n < 0) ? s_ih[INTERP_HIST + n] : (float)estereo[2 * n];
        }
        par   = 2.0f * (k_ih[0] * (x[0] + x[3]) + k_ih[1] * (x[1] + x[2]));
        impar = (2.0f * INTERP_CENTRO) * x[1];
        semib_pon(estereo, m, par, impar);
    }
    }

    for (k = 0U; k < INTERP_HIST; k++) { s_ih[k] = cola[k]; }
}
