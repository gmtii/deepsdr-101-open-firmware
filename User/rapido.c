#include "rapido.h"

/* Ver rapido.h para el porque. Aqui el como. */

#define R_PI_4  0.78539816339f
#define R_3PI_4 2.35619449019f

/*
 * La aproximacion clasica de la razon (x-|y|)/(x+|y|) con correccion cubica.
 *
 * LOS DOS COEFICIENTES VAN JUNTOS Y NO SE TOCAN POR SEPARADO. La primera
 * version llevaba -pi/4 en el termino lineal en vez de -0,9817, que parece lo
 * mismo y no lo es: el error se multiplica por DOCE, de 0,0039 a 0,0461
 * radianes. Las dos cifras estan medidas en sim/samtest.c, una detras de
 * otra, el mismo dia.
 */
float rapido_atan2(float y, float x)
{
    float ay = (y >= 0.0f) ? y : -y;
    float r, ang;

    /* El epsilon evita la division por cero en el origen, donde el angulo no
     * existe; cualquier valor vale y cero es el que menos sorprende. */
    ay += 1e-20f;

    if (x >= 0.0f) {
        r = (x - ay) / (x + ay);
        ang = R_PI_4;
    } else {
        r = (x + ay) / (ay - x);
        ang = R_3PI_4;
    }
    ang += 0.1963f * r * r * r - 0.9817f * r;

    return (y < 0.0f) ? -ang : ang;
}
