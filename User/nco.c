#include "nco.h"
#include "nco_tabla.h"
#include <math.h>

/*
 * Los numeros de la rejilla ya NO estan aqui: viven en nco.h, con la cuenta
 * que los usa (NCO_FASE_BITS y companeros). Estaban duplicados -una copia
 * aqui y la medida de la tabla en nco_tabla.h- y de esa pareja no se entera
 * nadie hasta que alguien cambia una: la tabla se leeria con el indice
 * equivocado y el oscilador saldria distorsionado sin que fallara nada al
 * compilar. Esto ata las dos.
 */
_Static_assert((1UL << NCO_FASE_BITS) == NCO_TABLA_N,
               "la rejilla de fase de nco.h no cuadra con el tamano de la tabla");

/* Una vuelta entera MAS el punto de cierre, para que la interpolacion del
 * ultimo tramo no tenga que dar la vuelta al indice con un modulo. Cuesta
 * cuatro bytes y quita una rama del bucle mas caliente que hay. */
/*
 * LA TABLA VIVE EN FLASH, NO EN RAM - 04/10/2026.
 *
 * Hasta hoy era `static float s_sen[1025]` y se rellenaba en nco_init(). Son
 * 4.100 bytes de los 196.608 que tiene esta radio, gastados en unos numeros
 * que no cambian nunca. Mientras sobro sitio no se noto; el 04/10/2026 el
 * enlazador se planto por DIECISEIS bytes al añadir cuatro medidas al
 * identificador de señales:
 *
 *     region `RAM' overflowed by 16 bytes
 *
 * La tabla esta ahora en User/nco_tabla.c, generada por
 * tools/gen_nco_tabla.py y comprobada en cada `make comprueba`. Se ganan
 * 4.100 bytes de RAM a cambio de 4.100 de flash, de la que sobran 140 kB.
 *
 * nco_init() se queda porque la llaman seis ficheros y porque manda la
 * regla de este proyecto: una funcion que existe no se quita de debajo de
 * quien la usa. Ahora no hace nada, y eso esta escrito aqui para que nadie
 * la busque pensando que se le ha olvidado algo.
 */
void nco_init(void)
{
}

/*
 * Seno y coseno de una fase de 32 bits, interpolados. El coseno es el mismo
 * seno un cuarto de vuelta por delante: 2^30 en el acumulador.
 *
 * 05/10/2026: la cuenta se mudo A LA CABECERA, en linea, porque sam.c la
 * llama una vez por muestra dentro de la interrupcion de audio y alli el
 * coste de la llamada pesa tanto como la cuenta. Esto se queda como el
 * nombre de siempre para el resto del fichero, llamando a la de alli: UNA
 * cuenta, dos nombres, cero copias. Ver nco_sen_cos_fase() en nco.h.
 */
static void sen_cos(uint32_t fase, float *sen, float *cos_)
{
    nco_sen_cos_fase(fase, sen, cos_);
}

/*
 * SENO Y COSENO DE UNA FASE EN RADIANES - 04/10/2026.
 *
 * Pedido por el usuario.
 *
 * Y era esto. sam.c llamaba a sinf() y a cosf() de la biblioteca UNA VEZ POR
 * MUESTRA, dentro de la interrupcion de audio y a 96 kHz. En este Cortex-M4
 * esas dos no son instrucciones: son rutinas de software de un par de
 * cientos de ciclos cada una. Doscientas cincuenta y seis muestras por
 * bloque por dos rutinas se comen una parte larga de los 533.333 ciclos que
 * dura un bloque, y lo que sobra no llega para el resto de la radio.
 *
 * La tabla de 1024 puntos con interpolacion que este fichero ya tenia da lo
 * mismo en unos pocos ciclos, y su error esta MEDIDO en sim/ncotest.c. Lo
 * unico que faltaba era poder pedirsela con una fase en radianes en vez de
 * con un acumulador que avanza solo: el oscilador de un PLL no avanza a
 * paso fijo, lo mueve el filtro de lazo.
 *
 * La conversion a la rejilla de 32 bits es una multiplicacion y un truncado,
 * y el redondeo que mete es mas pequeño que el error de la propia tabla.
 */
void nco_sen_cos_rad(float rad, float *sen, float *cos_)
{
    /* 2^32 / (2*pi): de radianes a la rejilla del acumulador. */
    const float k = 683565275.0f;
    float r = rad;

    /*
     * Fuera de una vuelta el truncado a uint32 no esta definido, asi que se
     * trae dentro. sam.c ya la mantiene en [0, 2pi), pero esto no se fia:
     * una fase que se escape no puede convertirse en basura silenciosa.
     *
     * ACOTADO - 06/10/2026. Era un par de while de restas, y eso no es una
     * salvaguarda: es una bomba de latencia DENTRO del camino de la
     * interrupcion de audio. Los llamantes -rds.c y hfdl_costas.c- sacan la
     * fase de un integrador de lazo; si un lazo se desboca y r llega a 1e5
     * son dieciseis mil vueltas en un bloque de 2,67 ms, y con r infinito no
     * termina nunca. Con r NaN las dos comparaciones son falsas y se cae en
     * un truncado indefinido, que es justo lo que el while pretendia evitar.
     *
     * fmodf() cuesta lo mismo y esta acotada. El NaN se mira aparte, porque
     * fmodf(NaN) sigue siendo NaN: se devuelve el angulo cero, que es lo que
     * menos daño hace y lo unico que se puede decir de una fase que no es un
     * numero.
     */
    /*
     * CAMINO RAPIDO DELANTE, SALVAGUARDA DETRAS - 10/10/2026.
     *
     * Todo lo de abajo esta para un llamante que se desboque, y hace falta:
     * el comentario de aqui arriba cuenta por que es fmodf y no un while.
     * Pero el llamante normal NO se desboca -rds.c acota la fase veinte
     * lineas despues de llamar, y lo dice en su comentario- asi que se
     * pagaba un fmodf de la biblioteca, 40 a 100 ciclos, 12.000 veces por
     * segundo, para no hacer nada.
     *
     * Con la fase ya en la vuelta, esto no toca r: fmodf de un valor en
     * rango devuelve ese mismo valor, o sea bit a bit lo mismo. La
     * proteccion contra NaN, infinito y lazos desbocados sigue entera, en la
     * rama que casi nunca se recorre.
     */
    if (r >= 0.0f && r < 6.28318530718f) {
        /* ya esta en la vuelta: nada que hacer */
    } else if (r < 0.0f && r >= -6.28318530718f) {
        r += 6.28318530718f;
    } else if (!(r == r)) {              /* NaN: r != r */
        r = 0.0f;
    } else {
        r = fmodf(r, 6.28318530718f);
        if (r < 0.0f) { r += 6.28318530718f; }
        /* fmodf de un infinito devuelve NaN; y si aun asi quedara fuera de
         * la vuelta -no deberia-, se recorta en vez de truncar mal. */
        if (!(r == r) || (r < 0.0f) || (r >= 6.28318530718f)) { r = 0.0f; }
    }

    sen_cos((uint32_t)(r * k), sen, cos_);
}

void nco_freq(nco_t *o, float hz, float fs_hz)
{
    double v;

    if (fs_hz <= 0.0f) {
        o->inc = 0U;
        return;
    }
    /*
     * En double y no en float a proposito. El incremento es hz/fs * 2^32, y
     * 2^32 tiene 33 bits significativos: un float solo lleva 24, asi que
     * redondear aqui en float tiraria los nueve ultimos bits del incremento
     * y dejaria el error de frecuencia en decimas de hercio en vez de en
     * cienmilesimas. Es una division por muestra... no, es una division por
     * CAMBIO de frecuencia, que pasa cuando alguien mueve el mando. Gratis.
     */
    v = (double)hz / (double)fs_hz;
    v = v - floor(v);                      /* a [0,1): lo de fuera no significa nada */
    o->inc = (uint32_t)(v * 4294967296.0 + 0.5);
}

float nco_freq_real(const nco_t *o, float fs_hz)
{
    double f = (double)o->inc / 4294967296.0;
    if (f > 0.5) {
        f -= 1.0;                          /* por encima de Fs/2 es una frecuencia negativa */
    }
    return (float)(f * (double)fs_hz);
}

void nco_fase_cero(nco_t *o)
{
    o->fase = 0U;
}

void nco_paso(nco_t *o, float *sen, float *cos_)
{
    sen_cos(o->fase, sen, cos_);
    o->fase += o->inc;
}

void nco_mezcla(nco_t *o, float *i_buf, float *q_buf, uint32_t n)
{
    uint32_t fase = o->fase;
    uint32_t inc = o->inc;
    uint32_t k;

    /*
     * (I + jQ) * (cos - j sen) = (I cos + Q sen) + j (Q cos - I sen).
     *
     * Con f = +Fs/4 esto da 1, -j, -1, +j, que es lo que ya hacia a mano
     * demod_am.c. Lo comprueba sim/ncotest.c muestra a muestra.
     */
    for (k = 0U; k < n; k++) {
        float s, c, vi, vq;

        sen_cos(fase, &s, &c);
        vi = i_buf[k];
        vq = q_buf[k];
        i_buf[k] = vi * c + vq * s;
        q_buf[k] = vq * c - vi * s;
        fase += inc;
    }
    o->fase = fase;
}

uint32_t nco_lo_aparcado(uint32_t freq_hz, uint32_t lo_actual_hz,
                         uint32_t off_ini_hz, uint32_t off_min_hz,
                         uint32_t off_max_hz)
{
    int64_t off;

    /*
     * PRIMERO, QUE LA CUENTA QUEPA - 09/10/2026.
     *
     * *** De un video de un usuario, con su transcripcion: "esta en 66 pero
     * no esta nada ahi", "ahi esta, 66, y por que pone 91,3?", "ahora ha
     * decidido no recibir nada", y al final la frase que lo delata: "me fui
     * a radiodifusion mas arriba y volvi, y se normalizo todo". ***
     *
     * Irse y volver solo arregla lo que depende de DONDE ESTABA EL
     * OSCILADOR, y lo unico que recuerda eso es esta funcion.
     *
     * Las tres ramas de abajo restan sin mirar, y devuelven uint32_t. La
     * radio llega hasta 30 kHz -TUNE_MIN_HZ, bajado el 01/09/2026 para
     * alcanzar DCF77- y la distancia de aparcado son 44 kHz: por debajo de
     * 44 kHz de dial, `freq - off_max` SE VA POR DEBAJO DE CERO y da la
     * vuelta. No sale un oscilador raro: salen 4.294 MHz, que ni es banda
     * baja ni cabe en el otro generador, asi que la sintonia se queda donde
     * le da la gana y el panel se centra en un numero inventado.
     *
     * El banco solo probaba el aparcado en 7,15 MHz. Ver prueba_aparcado_lf()
     * en sim/ncotest.c, que es donde esto se pone rojo.
     */
    if (freq_hz <= off_min_hz) {
        /* No hay ningun oscilador valido por debajo: la sintonia esta mas
         * baja que la propia distancia minima. No deberia pasar -el suelo
         * de sintonia es muy superior- pero devolver una resta negativa
         * seria peor que devolver algo que al menos existe. */
        return freq_hz;
    }

    /* Sin oscilador previo no hay nada que conservar. */
    if (lo_actual_hz == 0UL) {
        return (freq_hz > off_ini_hz) ? (freq_hz - off_ini_hz)
                                      : (freq_hz - off_min_hz);
    }

    off = (int64_t)freq_hz - (int64_t)lo_actual_hz;

    /*
     * Se ha salido por ARRIBA: venia subiendo. Se reaparca dejando la
     * distancia en el MINIMO, no en la de siempre, para que quede toda la
     * ventana por delante en el sentido en que se estaba yendo. Reaparcar al
     * valor clasico seria dejarlo en medio y volver a saltar a la mitad de
     * camino: con Fs/4 = 24 kHz y un tope de 44, subir daria un salto cada
     * 20 kHz en vez de cada 38. Lo midio sim/ncotest.c - la primera version
     * hacia justo eso.
     */
    if (off > (int64_t)off_max_hz) {
        return freq_hz - off_min_hz;
    }

    /* Y por ABAJO al reves: venia bajando, se deja en el maximo. Esta rama
     * cubre tambien las distancias negativas, que no se admiten nunca.
     *
     * Y "el maximo" es el maximo QUE CABE: por debajo de off_max de dial no
     * hay sitio para la distancia entera, asi que se deja la minima, que es
     * lo mas lejos que se puede dejar el oscilador sin cruzar el cero. Ver
     * la cuenta de arriba. */
    if (off < (int64_t)off_min_hz) {
        if (freq_hz > off_max_hz) {
            return freq_hz - off_max_hz;
        }
        return freq_hz - off_min_hz;
    }

    return lo_actual_hz;                      /* cabe: no se toca nada */
}
