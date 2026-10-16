/* Ver usb_cdc_anillo.h para la disciplina y por que no hay contador. */
#include "usb_cdc_anillo.h"

uint8_t anillo_pon(anillo_t *a, uint8_t *buf, uint16_t tam)
{
    a->cabeza = 0U;
    a->cola   = 0U;
    a->dato   = buf;

    /* Potencia de dos, y al menos dos: con uno solo el hueco que se
     * desperdicia no deja sitio para nada. */
    if ((buf == 0) || (tam < 2U) || ((tam & (uint16_t)(tam - 1U)) != 0U)) {
        a->masc = 0U;
        a->dato = 0;
        return 0U;
    }
    a->masc = (uint16_t)(tam - 1U);
    return 1U;
}

void anillo_reset(anillo_t *a)
{
    a->cabeza = 0U;
    a->cola   = 0U;
}

uint16_t anillo_lleno(const anillo_t *a)
{
    uint16_t c = a->cabeza;   /* una sola lectura de cada uno: volverlas a */
    uint16_t t = a->cola;     /* leer abajo daria dos fotos distintas */

    return (uint16_t)((c - t) & a->masc);
}

uint16_t anillo_hueco(const anillo_t *a)
{
    /* El -1 es el hueco que se desperdicia para poder distinguir lleno de
     * vacio. Ver la cabecera. Con la mascara a cero -anillo sin sitio- esto
     * devuelve cero y no se mete nada, que es lo que se quiere. */
    if (a->masc == 0U) { return 0U; }
    return (uint16_t)(a->masc - anillo_lleno(a));
}

uint16_t anillo_mete(anillo_t *a, const uint8_t *src, uint16_t n)
{
    uint16_t sitio = anillo_hueco(a);
    uint16_t c = a->cabeza;
    uint16_t i;

    if (n > sitio) { n = sitio; }
    for (i = 0U; i < n; i++) {
        a->dato[(c + i) & a->masc] = src[i];
    }
    /*
     * La cabeza se mueve AL FINAL y de una vez.
     *
     * Moviendola byte a byte, el que saca podria ver un byte ya contado y
     * todavia no escrito -la escritura del dato y la del indice pueden
     * reordenarse en el compilador-. Moviendola una sola vez al final, lo
     * que el otro lado ve es: o no hay nada nuevo, o estan todos.
     */
    a->cabeza = (uint16_t)((c + n) & a->masc);
    return n;
}

uint16_t anillo_saca(anillo_t *a, uint8_t *dst, uint16_t n)
{
    uint16_t hay = anillo_lleno(a);
    uint16_t t = a->cola;
    uint16_t i;

    if (n > hay) { n = hay; }
    for (i = 0U; i < n; i++) {
        dst[i] = a->dato[(t + i) & a->masc];
    }
    a->cola = (uint16_t)((t + n) & a->masc);
    return n;
}
