#include "ft8_lista.h"

uint8_t ft8_lista_es_cq(const char *l)
{
    uint8_t tabs = 0U;

    if (l == (const char *)0) { return 0U; }
    while (*l != '\0' && tabs < 4U) {
        if (*l == '\t') { tabs++; }
        l++;
    }
    if (tabs < 4U) { return 0U; }
    while (*l == ' ') { l++; }
    if (l[0] != 'C' || l[1] != 'Q') { return 0U; }
    return (uint8_t)((l[2] == ' ') || (l[2] == '\t') || (l[2] == '\0'));
}

uint8_t ft8_lista_escoge(const char *(*linea)(uint8_t), uint8_t guardadas,
                         uint8_t solo_cq, uint8_t caben, uint8_t *idx)
{
    uint8_t k, pasan = 0U;

    if (linea == 0 || idx == 0 || caben == 0U) { return 0U; }

    /*
     * DOS PASADAS Y NO UNA, y la primera NO guarda nada: la segunda tiene
     * que empezar por el primero que se va a ENSEÑAR, y cual es depende de
     * cuantos pasen en total. Guardar en la primera pediria un buffer del
     * tamaño de la lista entera para tirar despues la mitad.
     */
    for (k = 0U; k < guardadas; k++) {
        /* linea(0) es el mas nuevo; del mas viejo al mas nuevo es al reves. */
        const char *l = linea((uint8_t)(guardadas - 1U - k));

        if (l == 0 || l[0] == '\0') { continue; }
        if (solo_cq && !ft8_lista_es_cq(l)) { continue; }
        pasan++;
    }

    {
        uint8_t salta = (pasan > caben) ? (uint8_t)(pasan - caben) : 0U;
        uint8_t vistos = 0U, n = 0U;

        for (k = 0U; k < guardadas && n < caben; k++) {
            uint8_t j = (uint8_t)(guardadas - 1U - k);
            const char *l = linea(j);

            if (l == 0 || l[0] == '\0') { continue; }
            if (solo_cq && !ft8_lista_es_cq(l)) { continue; }
            if (vistos++ < salta) { continue; }
            idx[n++] = j;
        }
        return n;
    }
}
