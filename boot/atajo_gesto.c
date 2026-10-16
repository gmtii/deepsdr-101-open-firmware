#include "atajo_gesto.h"

/*
 * LA TABLA DE CUADRATURA, LA MISMA QUE User/encoder.c.
 *
 * Al principio esto contaba CAMBIOS de estado a secas, y estaba mal: dos
 * lineas con ruido rebotando entre dos estados vecinos dan cambios a
 * puñados sin que nadie toque el mando, y despues de ocho se habria ido a
 * DFU sola. Contando PASOS CON SIGNO, un rebote suma y resta y se queda en
 * cero; un giro de verdad se acumula. Se pide el modulo, asi que vale
 * girar hacia donde se quiera.
 *
 * Indice = (anterior << 2) | nuevo, con estado = (A << 1) | B. Las
 * transiciones imposibles -dos bits a la vez, que es ruido- valen 0.
 */
static const signed char k_cuad[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0,
};

atajo_r atajo_empieza(atajo_t *a, uint32_t boton, uint32_t ab)
{
    a->n     = 0U;
    a->ant   = ab & 3U;
    a->pasos = 0;
    return boton ? ATAJO_SIGUE : ATAJO_NO;
}

atajo_r atajo_muestra(atajo_t *a, uint32_t boton, uint32_t ab)
{
    /* Soltado a mitad: no era el gesto. Mandar en modo actualizacion a
     * quien solo tuvo el mando pulsado un rato es lo correcto. */
    if (!boton) { return ATAJO_NO; }

    a->pasos += k_cuad[(a->ant << 2) | (ab & 3U)];
    a->ant    = ab & 3U;
    a->n++;

    if ((a->pasos >= ATAJO_PASOS_PIDE) || (a->pasos <= -ATAJO_PASOS_PIDE)) {
        return ATAJO_SI;
    }

    /* Pulsado pero quieto: es el que quiere modo actualizacion. Que no
     * espere la ventana entera por nuestra culpa. */
    if ((a->n >= ATAJO_MUESTRAS_SECA) && (a->pasos == 0)) { return ATAJO_NO; }

    if (a->n >= ATAJO_MUESTRAS_MAX) { return ATAJO_NO; }

    return ATAJO_SIGUE;
}
