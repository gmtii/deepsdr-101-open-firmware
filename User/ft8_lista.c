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

uint8_t ft8_lista_cuantos(const char *(*linea)(uint8_t), uint8_t guardadas,
                          uint8_t solo_cq)
{
    uint8_t k, pasan = 0U;

    if (linea == 0) { return 0U; }

    for (k = 0U; k < guardadas; k++) {
        /* linea(0) es el mas nuevo; del mas viejo al mas nuevo es al reves. */
        const char *l = linea((uint8_t)(guardadas - 1U - k));

        if (l == 0 || l[0] == '\0') { continue; }
        if (solo_cq && !ft8_lista_es_cq(l)) { continue; }
        pasan++;
    }
    return pasan;
}

uint8_t ft8_lista_escoge(const char *(*linea)(uint8_t), uint8_t guardadas,
                         uint8_t solo_cq, uint8_t caben,
                         uint8_t *subidos, uint8_t *idx)
{
    uint8_t k, pasan;

    if (linea == 0 || idx == 0 || caben == 0U) { return 0U; }

    /*
     * DOS PASADAS Y NO UNA, y la primera NO guarda nada: la segunda tiene
     * que empezar por el primero que se va a ENSEÑAR, y cual es depende de
     * cuantos pasen en total. Guardar en la primera pediria un buffer del
     * tamaño de la lista entera para tirar despues la mitad.
     */
    pasan = ft8_lista_cuantos(linea, guardadas, solo_cq);

    {
        /*
         * EL SCROLL ES UNA RESTA, Y SE RECORTA AQUI.
         *
         * `tope` son los renglones que hay por encima de la ventana cuando
         * esta pegada al final. Subir `tope` renglones es llegar al mas
         * viejo; mas que eso no existe, asi que se recorta - y se devuelve
         * recortado, que es lo que le ahorra a quien lleva el dedo tener
         * que contar los que pasan el filtro por su cuenta.
         */
        uint8_t tope  = (pasan > caben) ? (uint8_t)(pasan - caben) : 0U;
        uint8_t sube  = (subidos != 0) ? *subidos : 0U;
        uint8_t salta;
        uint8_t vistos = 0U, n = 0U;

        if (sube > tope) { sube = tope; }
        if (subidos != 0) { *subidos = sube; }
        salta = (uint8_t)(tope - sube);

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

uint8_t ft8_lista_arrastra(int16_t *acum, int16_t dy, uint8_t alto,
                           uint8_t *subidos)
{
    int16_t a;
    int16_t pasos;
    int32_t v;

    if (acum == 0 || subidos == 0 || alto == 0U) { return 0U; }

    a = (int16_t)(*acum + dy);
    pasos = (int16_t)(a / (int16_t)alto);
    *acum = (int16_t)(a - pasos * (int16_t)alto);

    if (pasos == 0) { return 0U; }

    v = (int32_t)(*subidos) + (int32_t)pasos;
    if (v < 0)    { v = 0; }
    if (v > 255)  { v = 255; }
    if ((uint8_t)v == *subidos) { return 0U; }
    *subidos = (uint8_t)v;
    return 1U;
}

uint8_t ft8_lista_nuevos(const char *(*linea)(uint8_t), uint8_t cuantos,
                         uint8_t solo_cq)
{
    uint8_t k, n = 0U;

    if (linea == 0) { return 0U; }

    for (k = 0U; k < cuantos; k++) {
        const char *l = linea(k);   /* 0 es el mas nuevo */

        if (l == 0 || l[0] == '\0') { continue; }
        if (solo_cq && !ft8_lista_es_cq(l)) { continue; }
        n++;
    }
    return n;
}
