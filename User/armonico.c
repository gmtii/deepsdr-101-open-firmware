/* Ver armonico.h para el porque. Aqui solo esta el como. */
#include "armonico.h"

/*
 * EL NIVEL QUE CABE ESPERAR DE CADA ARMONICO. Ver la tabla de armonico.h.
 *
 * Los IMPARES salen de la propia cuadrada y valen 1/n del fundamental, que
 * no depende de nada: 1/3 son -9,5 dB, 1/5 son -14,0 y 1/7 son -16,9.
 *
 * Los PARES no existen en una cuadrada perfecta: salen SOLO del error de
 * ciclo de trabajo, que lo fija el umbral de conmutacion del mezclador.
 * Medido en sim/qsd_armonico.c, con un 1 % de error -que es lo tipico- el
 * 2.o queda en -32 dB. Se le pone -30 para no ser optimista.
 *
 * *** Y TODOS LOS PARES VALEN LO MISMO, NO 1/n - 26/10/2026. ***
 *
 * Aqui ponia que "los pares de mas arriba caen con el mismo 1/n" y de ahi
 * salian el -36 del 4.o y el -40 del 6.o. Es falso, y lo desmiente el
 * banco de este mismo proyecto: sim/qsd_armonico.c, con el ciclo al 51 %,
 * mide -31,7 dB en el 2.o y -31,7 dB en el 4.o. Iguales.
 *
 * Y la cuenta dice por que. El coeficiente del armonico k de una cuadrada
 * de ciclo D vale sin(k*pi*D)/(k*pi). Con D = 0,5 + e y k par, k = 2m:
 *
 *     sin(m*pi + 2*m*pi*e) / (2*m*pi)  =  (-1)^m * sin(2*m*pi*e) / (2*m*pi)
 *
 * y para e pequeño eso es (-1)^m * e, SIN m dentro. O sea que el nivel de
 * los pares lo fija el error de ciclo y nada mas: no decae con el orden.
 * La regla 1/n vale solo para los impares, que es de donde venia.
 *
 * Importa porque este numero es el que ordena la lista y, desde el arreglo
 * de arm_busca() del mismo dia, tambien el que decide cual sobrevive al
 * recorte.
 *
 * EN DECIMAS DE dB Y POSITIVO, o sea "cuantos dB por debajo".
 */
uint16_t arm_nivel_dbx10(uint8_t n)
{
    if (n == 0U) { return 999U; }        /* no es un armonico: el ultimo */
    if ((n % 2U) == 0U) { return 300U; } /* -30,0 dB, TODOS los pares */
    switch (n) {
    case 1U:  return 0U;
    case 3U:  return 95U;    /* -9,5  dB, 1/3 */
    case 5U:  return 140U;   /* -14,0 dB, 1/5 */
    default:  return 169U;   /* -16,9 dB, 1/7 */
    }
}

uint8_t arm_busca(uint32_t lo_hz, uint32_t span_hz, uint32_t tope_rf_hz,
                  arm_rango_fn rangos, uint16_t n_rangos,
                  arm_hit_t *out, uint8_t max)
{
    uint8_t n, puestos = 0U;
    uint32_t medio;

    if (lo_hz == 0UL || rangos == 0 || out == 0 || max == 0U) { return 0U; }
    medio = span_hz / 2UL;

    for (n = ARM_N_MIN; n <= ARM_N_MAX; n++) {
        /*
         * n*LO en 32 bits. Con el LO en 180 MHz y n=7 son 1.260 MHz, que
         * cabe de sobra; aun asi se comprueba, porque el dia que alguien
         * suba ARM_N_MAX o el margen de sintonia esto se desbordaria en
         * silencio y empezaria a enseñar bandas al azar.
         */
        uint32_t f;
        uint16_t i;

        if (lo_hz > (0xFFFFFFFFUL / (uint32_t)n)) { break; }
        f = lo_hz * (uint32_t)n;

        /*
         * Y SI EL FILTRO DE ENTRADA NO LO DEJA PASAR, NO SE CUENTA.
         * 24/10/2026. Ver el comentario de tope_rf_hz en armonico.h: esto
         * nombraba la banda de FM comercial con el dial en 24 MHz, cuando
         * el paso-bajo de 36 MHz la rechaza de largo.
         */
        if (tope_rf_hz != 0UL && f > tope_rf_hz) { continue; }

        for (i = 0U; i < n_rangos; i++) {
            uint32_t lo = 0UL, hi = 0UL;
            const char *nom = 0;
            uint32_t a, b;

            if (!rangos(i, &lo, &hi, &nom)) { continue; }
            if (nom == 0 || lo == 0UL) { continue; }
            if (hi < lo) { continue; }

            /*
             * Se solapan el rango [lo,hi] y el tramo [f-medio, f+medio]?
             * Con restas y no con sumas: f+medio se puede desbordar arriba
             * del todo, y f-medio por abajo cuando f es pequeño.
             */
            a = (f > medio) ? (f - medio) : 0UL;
            b = (medio > (0xFFFFFFFFUL - f)) ? 0xFFFFFFFFUL : (f + medio);
            if (hi < a || lo > b) { continue; }

            {
                /*
                 * El desvio se mide al trozo del rango que de verdad CAE
                 * dentro, no al centro del rango entero: una banda de
                 * radiodifusion de 300 kHz tiene su centro fuera del tramo
                 * de 96, y decir "a -180 kHz" de algo que se esta oyendo es
                 * mentira. Se recorta primero.
                 *
                 * 24/10/2026: ANTES SE CALCULABA DOS VECES -primero contra
                 * el centro del rango entero y luego contra el recortado,
                 * que pisaba al primero-. La primera cuenta no tenia ningun
                 * efecto y solo despistaba al leer. Fuera.
                 */
                uint32_t cl = (lo > a) ? lo : a;
                uint32_t ch = (hi < b) ? hi : b;
                uint32_t c  = cl + ((ch - cl) / 2UL);
                arm_hit_t t;
                int32_t off = (c >= f) ? (int32_t)(c - f)
                                       : -(int32_t)(f - c);

                t.n      = n;
                t.par    = (uint8_t)(((n % 2U) == 0U) ? 1U : 0U);
                t.f_hz   = f;
                t.nombre = nom;

                /*
                 * ==========================================================
                 * *** Y EL DESVIO VA EN EL EJE DE LA PANTALLA, NO EN EL DE
                 * LA RADIOFRECUENCIA - 26/10/2026, revision a fondo de
                 * espejos y armonicos. ***
                 * ==========================================================
                 *
                 * `off` tal cual sale de arriba es "cuanto se separa la
                 * banda de n*LO EN RADIOFRECUENCIA". Y quien lo lee -la
                 * fila de Informacion- lo imprime como si fuera "cuanto se
                 * separa del centro EN LA PANTALLA". No es lo mismo, y lo
                 * mide el banco de fisica de este mismo proyecto,
                 * sim/qsd_armonico.c:
                 *
                 *     n=1  una, en su lado
                 *     n=2  DOS, una a cada lado
                 *     n=3  una, en el lado CONTRARIO
                 *     n=4  DOS, una a cada lado
                 *     n=5  una, en SU lado
                 *
                 * El porque es el desfase entre I y Q, que para el armonico
                 * n vale n*90 grados: con n%4==1 son +90 -mismo lado- y con
                 * n%4==3 son 270, o sea -90, y la banda lateral SE DA LA
                 * VUELTA. Una señal POR ENCIMA de 3*LO sale POR DEBAJO del
                 * centro.
                 *
                 * El caso de portada de la cabecera lo sufria entero: dial
                 * 3.411, 3*LO en 10.161, la banda de 30 m. La fila decia
                 * "-29k" y el FT8 estaba en +29, al otro lado de la
                 * pantalla. O sea justo la confusion que este modulo existe
                 * para evitar.
                 *
                 * Y LOS PARES SALEN POR LOS DOS LADOS A LA VEZ, asi que un
                 * solo numero con signo es mentira por construccion: se
                 * deja el MODULO y quien lo imprima que ponga el ±. Para
                 * eso esta el campo `par`, que se calculaba desde el 24/10
                 * y no lo miraba nadie.
                 */
                if ((n % 4U) == 3U) { off = -off; }
                if (t.par && off < 0) { off = -off; }
                t.off_hz = off;

                /*
                 * ==========================================================
                 * *** Y SE QUEDAN LOS MAS FUERTES, NO LOS PRIMEROS.
                 * 26/10/2026. ***
                 * ==========================================================
                 *
                 * Aqui se escribia `out[puestos]` mientras hubiera hueco, y
                 * la ordenacion por nivel se hacia DESPUES, sobre lo que ya
                 * habia quedado dentro. O sea: la lista salia ordenada por
                 * nivel, pero el RECORTE era por numero de armonico, que es
                 * el criterio que el 24/10 se documento como equivocado.
                 *
                 * El bucle recorre n de 2 a 7 en orden creciente. Con max=4
                 * -que es como lo llama main.c- bastaba con que cuatro
                 * bandas cayeran en el 2.o armonico para que el 3.o, que
                 * esta VEINTE DECIBELIOS mas fuerte, no llegara a
                 * escribirse nunca. Y main.c solo enseña h[0], asi que el
                 * sospechoso que se nombraba era el flojo.
                 *
                 * Pasa de verdad y con la tabla de bandas de esta radio: el
                 * dial en 80 metros, por ejemplo, gasta tres de los cuatro
                 * huecos en 40 m / 41 m / SSTV 40 -que comparten los 7,16
                 * MHz, o sea el MISMO armonico- y el x5 de 16 metros se
                 * queda fuera.
                 *
                 * Con el hueco lleno se busca el mas flojo de los que hay y
                 * se le cede el sitio solo si el nuevo es mas fuerte. Son
                 * como mucho cuatro comparaciones por acierto.
                 */
                if (puestos < max) {
                    out[puestos] = t;
                } else {
                    uint8_t peor = 0U, q;

                    for (q = 1U; q < max; q++) {
                        if (arm_nivel_dbx10(out[q].n)
                                > arm_nivel_dbx10(out[peor].n)) { peor = q; }
                    }
                    if (arm_nivel_dbx10(t.n) < arm_nivel_dbx10(out[peor].n)) {
                        out[peor] = t;
                    }
                }
            }
            /* El contador se satura en vez de salirse por la puerta de
             * atras: saliendo aqui se devolvia la lista SIN ORDENAR, que es
             * justo lo que la cabecera promete que no pasa. */
            if (puestos < 250U) { puestos++; }
        }
    }

    /*
     * Y AHORA POR NIVEL, NO POR NUMERO DE ARMONICO - 24/10/2026.
     *
     * Aqui se devolvia la lista en el orden en que sale del bucle, o sea de
     * menor a mayor armonico, y la cabecera lo justificaba diciendo que "el
     * 2.o entra con mucha mas fuerza que el 7.o". Eso es falso para una
     * cuadrada: una cuadrada de ciclo 50 % NO TIENE armonicos pares, y los
     * que se ven salen del error de ciclo. El 3.o esta siempre a -9,5 dB y
     * el 2.o a unos -30: el "sospechoso principal" era el equivocado.
     *
     * Insercion directa sobre la parte que se ha escrito. Como mucho son
     * `max` elementos -cuatro hoy-, asi que no merece nada mejor.
     */
    {
        uint8_t escritos = (puestos < max) ? puestos : max;
        uint8_t i, j;

        for (i = 1U; i < escritos; i++) {
            arm_hit_t t = out[i];
            uint16_t nt = arm_nivel_dbx10(t.n);
            j = i;
            while (j > 0U && arm_nivel_dbx10(out[j - 1U].n) > nt) {
                out[j] = out[j - 1U];
                j--;
            }
            out[j] = t;
        }
    }
    return puestos;
}
