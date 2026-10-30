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
    uint32_t salto;

    /*
     * PRIMERO, QUE LA CUENTA QUEPA - 09/10/2026.
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

    /* Lo que se recorre de un reaparcado al siguiente deja el aterrizaje a
     * off_min del borde: ver la rama de "se ha salido por arriba". Con un
     * off_max mal puesto -mas pequeño que el margen- esto se quedaria en
     * off_min, que es el comportamiento de antes y nunca es peor. */
    salto = (off_max_hz > off_min_hz) ? (off_max_hz - off_min_hz) : off_min_hz;

    /*
     * ==================================================================
     * SE HA SALIDO POR ARRIBA: VENIA SUBIENDO.
     * ==================================================================
     *
     * Se reaparca dejando la distancia en el extremo CONTRARIO, para que
     * quede toda la ventana por delante en el sentido en que se estaba
     * yendo. Reaparcar al valor clasico seria dejarlo en medio y volver a
     * saltar a la mitad de camino: con Fs/4 = 24 kHz y un tope de 44,
     * subir daria un salto cada 20 kHz en vez de cada 38. Lo midio
     * sim/ncotest.c - la primera version hacia justo eso.
     *
     * *** Y EL EXTREMO CONTRARIO ES -(off_max - off_min), NO +off_min.
     * 30/10/2026, con el agujero del centro ya quitado. ***
     *
     * Mientras el centro estaba cerrado, subiendo solo se podia volver a
     * entrar por +off_min y el recorrido entre saltos era off_max -
     * off_min: 84 kHz a 192 kHz de muestreo. Abierto el centro, el sitio
     * que deja mas ventana por delante es el otro extremo, y el recorrido
     * pasa a ser off_max + (off_max - off_min) = 174 kHz. El panorama
     * enseña 192 y el salto queda en el borde, que es donde el usuario lo
     * pidio.
     *
     * EL MARGEN DE BORDE SE RESPETA, y no es un adorno: off_max ya esta
     * puesto donde el filtro antialias del conversor sigue plano, y
     * aterrizar a off_max EXACTOS fue el fallo del 09/10/2026 -la emisora
     * que acabas de perseguir aparece justo donde el codec ya no entrega
     * nivel-. Por eso se descuenta off_min, que es el mismo margen que el
     * resto del fichero usa para hablar del borde.
     *
     * Y la cuenta se protege: `freq + salto` no se puede desbordar en
     * uint32 con las frecuencias de esta radio, pero `freq - salto` si se
     * iria por debajo de cero en onda larga, que es exactamente el fallo
     * que arreglo lo del 09/10/2026 y que no puede volver.
     */
    if (off > (int64_t)off_max_hz) {
        return freq_hz + salto;               /* venia subiendo: sitio arriba */
    }

    /*
     * ==================================================================
     * Y POR ABAJO, CON EL OSCILADOR POR ENCIMA - 24/10/2026, avisado por
     * el usuario: "el NCO no barre toda la pantalla".
     * ==================================================================
     *
     * Y no la barria. Aqui ponia que las distancias NEGATIVAS no se
     * admiten nunca, asi que la ventana util iba de off_min a off_max y
     * se acababa ahi: el oscilador queda en el CENTRO de lo que se ve, de
     * modo que el NCO cubria del centro al borde derecho y la mitad
     * izquierda de la pantalla no era suya. Pasar del centro reaparcaba,
     * y al reaparcar se mueve la imagen entera.
     *
     * Era una limitacion heredada de cuando la distancia era fija: con el
     * oscilador SIEMPRE por debajo, no habia nada que decidir. Pero una
     * distancia negativa no tiene nada de malo -significa que el
     * oscilador esta por encima de lo que se escucha, y a un receptor en
     * cuadratura eso le da igual-, y el giro grueso de demod_am.c ya la
     * admite con signo.
     *
     * AHORA LO QUE SE EXIGE ES |distancia| por debajo de max, y nada mas.
     *
     * *** EL AGUJERO DEL MEDIO SE QUITO EL 30/10/2026. *** Aqui ponia que
     * por debajo de off_min la señal se sienta encima de la fuga del
     * oscilador y que cruzar el centro seguia pidiendo un reaparcado. Lo
     * primero dejo de ser cierto el 25/10/2026 -demod_am.c resta la
     * continua de I y de Q antes de bajar en frecuencia- y lo segundo era
     * justo el salto que el usuario pidio quitar. El razonamiento entero,
     * y lo que SI se pierde al pasar por el centro, estan en la cabecera
     * de esta funcion, en nco.h.
     *
     * HACIA DONDE SE REAPARCA, que es lo unico con miga: al salirse por
     * arriba se deja en el extremo de ABAJO, con toda la ventana por
     * delante hacia arriba; al salirse por abajo, al reves. Ya no hay
     * tercera rama.
     *
     * Y las dos salidas usan SUMA o una resta protegida, asi que la cuenta
     * de onda larga que arreglo lo de 09/10/2026 no puede volver.
     */
    if (off < -(int64_t)off_max_hz) {
        /* Venia bajando: sitio abajo. Y aqui si hay que mirar que la resta
         * quepa; ver la guarda de onda larga del principio. */
        return (freq_hz > salto) ? (freq_hz - salto) : (freq_hz - off_min_hz);
    }
    /*
     * Y EL AGUJERO DEL MEDIO YA NO ESTA. Aqui habia una tercera rama que
     * reaparcaba en cuanto |off| bajaba de off_min, y es la que pegaba el
     * salto de once kilohercios al cruzar el centro. Ver la cabecera de
     * esta funcion en nco.h: lo que protegia -la fuga del oscilador
     * sentandose encima de la emisora- lo resuelve desde el 25/10/2026 la
     * resta de continua de demod_am.c, y lo que queda dentro de esos doce
     * kilohercios -la emisora encima de su propia imagen- es de nacimiento
     * y no se arregla moviendo el oscilador.
     */

    return lo_actual_hz;                      /* cabe: no se toca nada */
}
