#include "reloj.h"

uint32_t reloj_ms_del_dia(uint32_t ahora_ms, uint32_t desfase_ms)
{
    /* En 64 bits: los dos sumandos caben de sobra en 32, pero la SUMA no -el
     * contador de arranque llega a 2^32 a los 49 dias y el desfase puede ser
     * de casi un dia-. Sumar en 32 bits daria la vuelta en silencio y el
     * reloj se iria varias horas sin que nada avisara. */
    return (uint32_t)((((uint64_t)ahora_ms) + (uint64_t)desfase_ms)
                      % (uint64_t)RELOJ_MS_DIA);
}

uint32_t reloj_desfase_minuto(uint32_t ahora_ms, uint8_t hh, uint8_t mm)
{
    int32_t deseado, ahora, delta;

    if (hh > 23U) { hh = 23U; }
    if (mm > 59U) { mm = 59U; }
    deseado = (int32_t)hh * 60 + (int32_t)mm;
    ahora   = (int32_t)((ahora_ms / RELOJ_MS_MIN) % 1440UL);
    delta   = deseado - ahora;
    if (delta < 0) { delta += 1440; }
    return (uint32_t)delta * RELOJ_MS_MIN;
}

uint32_t reloj_desfase_anclado(uint32_t ahora_ms, int32_t mins_locales,
                               uint32_t atraso_ms)
{
    int64_t borde, d;

    mins_locales %= 1440;
    if (mins_locales < 0) { mins_locales += 1440; }

    /* En que instante del contador de arranque cayo el borde de minuto. Puede
     * salir NEGATIVO si se ancla en los primeros segundos de vida del aparato
     * -el atraso es mayor que lo que lleva encendido-, y por eso la cuenta va
     * con signo y en 64 bits: en enteros sin signo ese caso daria la vuelta y
     * el reloj se iria 49 dias. */
    borde = (int64_t)ahora_ms - (int64_t)atraso_ms;

    /* Y el desfase que hace que en ese instante marcara las mins_locales en
     * punto. El modulo va en 64 bits porque ahora_ms no viene reducido a un
     * dia: lleva los milisegundos desde el arranque, sin vueltas. */
    d = (int64_t)mins_locales * (int64_t)RELOJ_MS_MIN - borde;
    d %= (int64_t)RELOJ_MS_DIA;
    if (d < 0) { d += (int64_t)RELOJ_MS_DIA; }
    return (uint32_t)d;
}
