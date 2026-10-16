#include "rapido.h"

/* Ver rapido.h para el porque. Aqui el como. */

#define R_PI    3.14159265358979f
#define R_PI_2  1.57079632679490f

/*
 * ===========================================================================
 * ARCOTANGENTE DE CUATRO CUADRANTES, POR OCTANTES - 06/10/2026.
 * ===========================================================================
 *
 * LA VERSION ANTERIOR ERA LA EQUIVOCADA PARA ESTO, y se vio midiendo.
 *
 * Usaba la razon r = (x-|y|)/(x+|y|) con una correccion cubica. Esa razon
 * vale tan(pi/4 - theta): cuando el angulo es PEQUEÑO, r se va a 1, que es
 * justo el extremo del intervalo donde el ajuste minimax tiene su error
 * maximo. O sea que el atajo era mas malo precisamente donde mas se usa.
 *
 * Medido, con el discriminador de FM escrito tal cual esta:
 *
 *   theta=0,001 rad   error de la PENDIENTE   -21,5 %
 *   NFM +-5 kHz       THD 2,04 %   H3 a -34,7 dBc
 *   NFM +-1,5 kHz     THD 1,88 %   H3 a -34,7 dBc
 *   WFM +-20 kHz      THD 0,96 %
 *   WFM +-75 kHz      THD 0,11 %
 *
 * Y la firma es contraintuitiva, que es por lo que no se habia notado:
 * cuanto MAS FLOJA es la modulacion, PEOR suena. El tercer armonico se queda
 * clavado en -35 dBc mientras la señal baja. El comentario que habia decia
 * que el error quedaba "muy por debajo del ruido del canal"; -35 dBc sobre
 * una emisora fuerte no esta por debajo de nada. Y ademas el error no es
 * ruido: es una funcion fija del angulo, asi que sale como RAYAS y no como
 * suelo.
 *
 * LO QUE HAY AHORA: octantes. Se divide siempre el valor pequeño entre el
 * grande, asi que z = tan(angulo pequeño) se queda en [-1,1] y, cuando el
 * angulo tiende a cero, z tiende a cero - que es donde un polinomio impar
 * es exacto, no donde peor va.
 *
 *   atan(z) ~= z*(0,9953547 - 0,2886679 z^2 + 0,0793310 z^4)
 *
 * Medido sobre 2.000.001 puntos de los cuatro cuadrantes:
 *
 *                    error max      pendiente en 0   THD NFM +-5k
 *   el de antes      0,0102 rad        -21,5 %          2,04 %
 *   este             0,00062 rad       -0,47 %          0,083 %
 *   atan2f()         0,00000026 rad     0,00 %          0,011 %
 *
 * Dieciseis veces mas exacto, cuarenta y seis veces mejor de pendiente, y la
 * distorsion del discriminador baja de 2 % a 0,08 %, que ya es practicamente
 * el suelo de la propia medida.
 *
 * Y NO CUESTA MAS: una division y cuatro multiplicaciones frente a una
 * division y tres. En este micro la division es lo que manda (unos catorce
 * ciclos) y las multiplicaciones valen uno, asi que la diferencia se pierde
 * en el ruido. Medido en el PC salia incluso algo mas rapido.
 *
 * EL ORIGEN, ADEMAS, AHORA DEVUELVE CERO. El comentario de la version
 * anterior decia "cualquier valor vale y cero es el que menos sorprende", y
 * lo que devolvia era pi/2: con I y Q exactamente a cero -arranque, bloque
 * silenciado, buffers recien puestos a cero- el discriminador soltaba una
 * continua de pi/2 por la ganancia, que es un golpe a fondo de escala. Ahora
 * devuelve 0, como atan2f().
 *
 * Lo ata sim/atantest.c, que mide las tres cosas -error, pendiente y THD del
 * discriminador- contra atan2f(). Ver make atan.
 */
float rapido_atan2(float y, float x)
{
    float ax = (x >= 0.0f) ? x : -x;
    float ay = (y >= 0.0f) ? y : -y;
    float z, z2, a;

    /* El origen: el angulo no existe y cero es lo que contesta atan2f(). Se
     * mira aqui y no con un epsilon dentro de la division, que es lo que
     * hacia la version anterior y lo que la hacia devolver pi/2. */
    if ((ax == 0.0f) && (ay == 0.0f)) {
        return 0.0f;
    }

    if (ax >= ay) {
        z = y / x;                       /* |z| <= 1, y ax > 0 aqui */
    } else {
        z = x / y;                       /* |z| < 1, y ay > 0 aqui */
    }

    z2 = z * z;
    a = z * (0.9953547f + z2 * (-0.2886679f + z2 * 0.0793310f));

    if (ax >= ay) {
        if (x < 0.0f) { a += (y >= 0.0f) ? R_PI : -R_PI; }
    } else {
        a = ((y >= 0.0f) ? R_PI_2 : -R_PI_2) - a;
    }
    return a;
}
