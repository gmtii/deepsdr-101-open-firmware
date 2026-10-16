#include "rtc_cal.h"

uint8_t rtc_cal_bcd_a_dec(uint8_t bcd)
{
    uint8_t alto = (uint8_t)((bcd >> 4) & 0x0FU);
    uint8_t bajo = (uint8_t)(bcd & 0x0FU);
    /* Un nibble por encima de nueve no es BCD. Pasa con un registro
     * sin inicializar (0xFF) y con una lectura a medias del shadow.
     * Devolver 0 es decir "no se"; devolver 15*10+15 seria inventarse
     * un 165 que luego viaja hasta la pantalla como una hora. */
    if (alto > 9U || bajo > 9U) { return 0U; }
    return (uint8_t)(alto * 10U + bajo);
}

uint8_t rtc_cal_dec_a_bcd(uint8_t dec)
{
    if (dec > 99U) { return 0U; }
    return (uint8_t)(((dec / 10U) << 4) | (dec % 10U));
}

static uint8_t bisiesto(uint16_t y)
{
    /*
     * LA REGLA ENTERA, AUNQUE AQUI SOBRE DOS TERCIOS - 13/10/2026.
     *
     * rtc_cal_valida() solo admite 2000..2099, que es lo que el RTC
     * puede guardar: su campo de año son dos cifras BCD. Y dentro de
     * ESE siglo, "divisible por 4" y la regla gregoriana completa dan
     * exactamente la misma respuesta todos los años: la primera vez
     * que se separan es en 2100.
     *
     * O sea que los terminos del 100 y del 400 NO se pueden provocar
     * desde aqui, y el banco no puede verlos fallar. Se dejan igual, y
     * se dice por que: el dia que alguien amplie el rango -o copie
     * esta funcion a otro sitio- la regla ya esta bien. Lo que no se
     * hace es fingir que el banco los cubre.
     *
     * Quien SI ejerce la regla completa es rtc_cal_dias_desde_1970(),
     * que no pasa por aqui -lleva su propia cuenta, yoe/4 - yoe/100 +
     * yoe/400- y trabaja de 1970 en adelante sin tope. El banco le
     * mide 2100 y 2000 por ahi.
     */
    return (uint8_t)(((y % 4U) == 0U && (y % 100U) != 0U) || (y % 400U) == 0U);
}

static uint8_t dias_del_mes(uint16_t y, uint8_t m)
{
    static const uint8_t k[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m < 1U || m > 12U) { return 0U; }
    if (m == 2U && bisiesto(y))  { return 29U; }
    return k[m - 1U];
}

int32_t rtc_cal_dias_desde_1970(uint16_t year, uint8_t month, uint8_t day)
{
    int32_t y, era, yoe, doy, doe;

    if (year < 1970U || month < 1U || month > 12U || day < 1U) { return 0; }

    /*
     * EL AÑO EMPIEZA EN MARZO, y no es una excentricidad.
     *
     * Con el año civil (enero a diciembre) febrero esta en medio, asi
     * que el dia que el bisiesto añade cae DENTRO de la cuenta y hay
     * que tratarlo aparte en cada tramo. Corriendo el origen a marzo,
     * el 29 de febrero pasa a ser el ULTIMO dia del año y deja de
     * estorbar: la cuenta de dias de los meses se vuelve una formula
     * cerrada, (153*m + 2) / 5, sin tabla y sin casos.
     */
    y = (int32_t)year;
    y -= (month <= 2U);                       /* enero y febrero son del año anterior */
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;                      /* año dentro de la era, 0..399 */
    doy = (153 * ((int32_t)month + (month > 2U ? -3 : 9)) + 2) / 5
          + (int32_t)day - 1;                 /* dia dentro del año que empieza en marzo */
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;       /* 719468 = dias de 0000-03-01 a 1970-01-01 */
}

uint32_t rtc_cal_epoca(uint16_t year, uint8_t month, uint8_t day,
                       uint8_t hour, uint8_t minute, uint8_t second)
{
    int32_t d = rtc_cal_dias_desde_1970(year, month, day);
    if (d < 0) { return 0U; }
    return (uint32_t)d * 86400UL
         + (uint32_t)hour * 3600UL
         + (uint32_t)minute * 60UL
         + (uint32_t)second;
}

uint8_t rtc_cal_valida(uint16_t year, uint8_t month, uint8_t day,
                       uint8_t hour, uint8_t minute, uint8_t second)
{
    if (year < 2000U || year > 2099U) { return 0U; }
    if (month < 1U || month > 12U)    { return 0U; }
    if (day < 1U || day > dias_del_mes(year, month)) { return 0U; }
    if (hour > 23U || minute > 59U)   { return 0U; }
    /*
     * El 60 se admite a proposito: es el segundo intercalar, y una
     * emisora de tiempo lo manda de verdad. Rechazarlo seria tirar la
     * unica trama del año en la que el patron dice algo distinto.
     */
    if (second > 60U) { return 0U; }
    return 1U;
}

uint8_t rtc_cal_dia_semana(uint16_t year, uint8_t month, uint8_t day)
{
    int32_t d = rtc_cal_dias_desde_1970(year, month, day);
    /* 1970-01-01 fue jueves. El RTC numera 1=lunes..7=domingo, asi que
     * el jueves es el 4: se parte de ahi y se gira. */
    int32_t s = (d + 3) % 7;                 /* 0 = lunes */
    if (s < 0) { s += 7; }
    return (uint8_t)(s + 1);
}
