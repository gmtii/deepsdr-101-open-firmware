#include "hora_comun.h"

const uint8_t k_hora_dias_mes[13] = {
    0U, 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
};

uint8_t hora_bisiesto(uint8_t anno2)
{
    /* 2000..2099: basta el multiplo de 4. 2000 lo fue (multiplo de 400) y
     * 2100 no lo sera, pero 2100 ya no cabe en dos cifras de este siglo. */
    return (uint8_t)((anno2 % 4U) == 0U);
}

uint8_t hora_dias_del_mes(uint8_t mes, uint8_t anno2)
{
    if (mes < 1U || mes > 12U) { return 0U; }
    if (mes == 2U && hora_bisiesto(anno2)) { return 29U; }
    return k_hora_dias_mes[mes];
}

/*
 * Dia de la semana por el metodo de Sakamoto. La tabla k_sak son los
 * desplazamientos acumulados de cada mes; el truco de restar un año cuando
 * el mes es enero o febrero es lo que hace que el dia bisiesto caiga al
 * final del "año" y no haya que tratarlo aparte.
 *
 * Sakamoto da 0=domingo. Aqui se devuelve el convenio de DCF77 (1=lunes ...
 * 7=domingo) porque es el que viaja en dcf_info_t, y convertir en un solo
 * sitio es lo que evita que cada emisora traiga el suyo.
 */
uint8_t hora_dia_semana(uint8_t dia, uint8_t mes, uint8_t anno2)
{
    static const uint8_t k_sak[12] = { 0U, 3U, 2U, 5U, 0U, 3U,
                                       5U, 1U, 4U, 6U, 2U, 4U };
    uint16_t y = (uint16_t)(2000U + anno2);
    uint8_t  dom;

    if (mes < 1U || mes > 12U) { return 1U; }
    if (mes < 3U) { y = (uint16_t)(y - 1U); }
    dom = (uint8_t)((y + y / 4U - y / 100U + y / 400U
                     + k_sak[mes - 1U] + dia) % 7U);
    return (uint8_t)((dom == 0U) ? 7U : dom);
}

uint8_t hora_dia_del_anno_a_fecha(uint16_t doy, uint8_t anno2,
                                  uint8_t *dia, uint8_t *mes)
{
    uint8_t m;
    uint16_t resto = doy;

    if (doy < 1U) { return 0U; }
    for (m = 1U; m <= 12U; m++) {
        uint16_t n = hora_dias_del_mes(m, anno2);
        if (resto <= n) {
            *mes = m;
            *dia = (uint8_t)resto;
            return 1U;
        }
        resto = (uint16_t)(resto - n);
    }
    return 0U;   /* 366 en año no bisiesto, o mas: no existe */
}

/* El dia del ultimo domingo de un mes. */
static uint8_t ultimo_domingo(uint8_t mes, uint8_t anno2)
{
    uint8_t ultimo = hora_dias_del_mes(mes, anno2);
    uint8_t ds = hora_dia_semana(ultimo, mes, anno2);   /* 1=lunes..7=domingo */
    /* Si el ultimo dia es domingo (7), ya esta; si no, hay que retroceder
     * tantos dias como indique el convenio. */
    return (uint8_t)(ultimo - (ds % 7U));
}

uint8_t hora_verano_espanna(uint8_t dia, uint8_t mes, uint8_t anno2,
                            uint8_t hora_utc)
{
    uint8_t cambio;

    if (mes < 3U || mes > 10U) { return 0U; }   /* nov..feb: invierno seguro */
    if (mes > 3U && mes < 10U) { return 1U; }   /* abr..sep: verano seguro */

    if (mes == 3U) {
        cambio = ultimo_domingo(3U, anno2);
        if (dia > cambio) { return 1U; }
        if (dia < cambio) { return 0U; }
        return (uint8_t)(hora_utc >= 1U);
    }

    cambio = ultimo_domingo(10U, anno2);
    if (dia < cambio) { return 1U; }
    if (dia > cambio) { return 0U; }
    return (uint8_t)(hora_utc < 1U);
}

void hora_suma_horas(int8_t desplazamiento,
                     uint8_t *hora, uint8_t *dia, uint8_t *mes,
                     uint8_t *anno2, uint8_t *dia_semana)
{
    int16_t h = (int16_t)((int16_t)*hora + (int16_t)desplazamiento);

    while (h < 0) {
        h = (int16_t)(h + 24);
        if (*dia > 1U) {
            (*dia)--;
        } else {
            *mes = (uint8_t)((*mes == 1U) ? 12U : (*mes - 1U));
            if (*mes == 12U) { *anno2 = (uint8_t)((*anno2 + 99U) % 100U); }
            *dia = hora_dias_del_mes(*mes, *anno2);
        }
    }
    while (h > 23) {
        h = (int16_t)(h - 24);
        if (*dia < hora_dias_del_mes(*mes, *anno2)) {
            (*dia)++;
        } else {
            *dia = 1U;
            *mes = (uint8_t)((*mes == 12U) ? 1U : (*mes + 1U));
            if (*mes == 1U) { *anno2 = (uint8_t)((*anno2 + 1U) % 100U); }
        }
    }
    *hora = (uint8_t)h;
    /* El dia de la semana se RECALCULA de la fecha en vez de arrastrarse:
     * arrastrarlo obliga a acertar en los dos sentidos y en los saltos de
     * mes, y esa es una cuenta mas que puede salir mal sin que nada avise.
     * Sacarlo de la fecha no puede desincronizarse con ella. */
    *dia_semana = hora_dia_semana(*dia, *mes, *anno2);
}
