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

/* Y aqui estaba ultimo_domingo(), que solo usaba la de arriba. La buscadora
 * de domingos que queda es domingo() en husos.c, que ademas es mas general:
 * "el primer domingo a partir del dia D" incluye el ultimo del mes y tambien
 * los casos que un ordinal no sabe decir (ver Chile). */

/*
 * *** AQUI ESTABA hora_verano_espanna(), Y SE BORRO EL 10/10/2026. ***
 *
 * No por limpieza: por DUPLICADO. Al sacar España del firmware se quedo sin
 * una sola llamada, y mirandola de cerca resulto ser UNA SEGUNDA
 * IMPLEMENTACION de la regla europea -la otra es la fila HUSO_DST_EU de
 * husos.c, que ademas la tiene como datos y no como codigo-.
 *
 * La cabecera de este mismo fichero dice, sobre la tabla de dias por mes:
 * "ese es exactamente el fallo que este proyecto ya ha pagado varias veces:
 * la misma lista en varios sitios y ningun error de compilacion cuando una
 * se queda atras". Dejarla habria sido escribir eso y hacer lo contrario
 * tres pantallas mas abajo.
 *
 * sim/calendariotest.c media esta funcion contra zoneinfo; ahora mide
 * husos_verano(HUSO_DST_EU, ...) con las mismas muestras, asi que la
 * comprobacion no ha perdido nada.
 */

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

/*
 * ======================================================================
 * EL HUSO HORARIO. Ver hora_comun.h para el porque entero.
 * ======================================================================
 */
static int16_t s_huso_min = 60;   /* CET: lo que habia cableado, como arranque */
static uint8_t s_huso_medido;

static int16_t huso_recorta(int16_t m)
{
    if (m < HORA_HUSO_MIN) { return (int16_t)HORA_HUSO_MIN; }
    if (m > HORA_HUSO_MAX) { return (int16_t)HORA_HUSO_MAX; }
    return m;
}

void hora_huso_pon(int16_t minutos)
{
    /*
     * UNA TABLA NO PISA A UNA EMISORA. Si el RDS de tu ciudad ya ha dicho
     * que estas en +5:30, que la lista de ciudades -o un CONFIG.CSV viejo-
     * lo cambie al cargarse seria sustituir una medida por una suposicion.
     * Para eso esta hora_huso_mide(), que si manda.
     */
    if (s_huso_medido) { return; }
    s_huso_min = huso_recorta(minutos);
}

void hora_huso_mide(int16_t minutos)
{
    s_huso_min = huso_recorta(minutos);
    s_huso_medido = 1U;
}

int16_t hora_huso(void)      { return s_huso_min; }
uint8_t hora_huso_medido(void) { return s_huso_medido; }

void hora_suma_minutos(int16_t minutos,
                       uint8_t *hora, uint8_t *minuto, uint8_t *dia,
                       uint8_t *mes, uint8_t *anno2, uint8_t *dia_semana)
{
    int32_t t = (int32_t)(*hora) * 60 + (int32_t)(*minuto) + (int32_t)minutos;
    int8_t  dias = 0;

    /*
     * Se parte en "dias enteros" + "lo que queda", y los dias se le pasan a
     * hora_suma_horas() en bloques de 24: asi el calendario lo arrastra la
     * funcion que ya sabe hacerlo -con sus meses, sus bisiestos y su dia de
     * la semana recalculado- y aqui solo se hace la cuenta de minutos. Dos
     * sitios haciendo el mismo calendario es el fallo que hora_comun.c
     * existe para no repetir.
     */
    while (t < 0)       { t += 24 * 60; dias--; }
    while (t >= 24 * 60) { t -= 24 * 60; dias++; }

    *hora   = (uint8_t)(t / 60);
    *minuto = (uint8_t)(t % 60);
    if (dias != 0) {
        /* 24 h por dia: mueve la fecha sin tocar la hora que acaba de
         * quedar puesta. */
        uint8_t h = *hora;
        hora_suma_horas((int8_t)(dias * 24), &h, dia, mes, anno2, dia_semana);
        *hora = h;
    } else {
        *dia_semana = hora_dia_semana(*dia, *mes, *anno2);
    }
}
