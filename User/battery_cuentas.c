#include "battery.h"

/*
 * LAS CUENTAS DEL VOLTIMETRO, EN SU PROPIO FICHERO - 12/10/2026.
 *
 * ESTE FICHERO NO TOCA UN REGISTRO, y eso es justo para lo que esta: asi
 * se compila tal cual en el PC y el banco (sim/bateria.c) puede llamar a
 * las cuentas de VERDAD en vez de a una copia.
 *
 * El intento anterior las dejo dentro de battery.c y el banco no pudo
 * compilarlas: ese fichero arrastra el HAL entero -rcu_periph_clock_enable,
 * adc_clock_config, los diez registros del ADC- y para medir una division
 * no hay que simular un periferico. Separarlas cuesta un fichero y es la
 * diferencia entre medir la cuenta y medir una copia de la cuenta escrita
 * en el banco, que es la manera clasica de que las dos se equivoquen
 * igual.
 *
 * Lo que se queda en battery.c es lo que SI es el periferico: configurar,
 * disparar, esperar con plazo y restaurar el canal.
 */

/*
 * LAS CUENTAS, APARTE DEL ADC - 12/10/2026.
 *
 * Estas tres funciones no tocan un registro: entra un numero crudo y sale
 * un numero con unidades. Estan separadas para que el banco del PC las
 * pueda medir de verdad (ver sim/bateria.c), porque lo que se puede
 * equivocar aqui son las cuentas y los topes, no el periferico - y un
 * banco que no puede llamar a la cuenta acaba comprobando otra cosa.
 */
uint16_t battery_vdda_de_raw(uint16_t raw)
{
    uint32_t vdda;

    /*
     * Y QUE LA LECTURA TENGA SENTIDO ANTES DE DIVIDIR POR ELLA. Con raw a
     * cero esto seria una division por cero -que en este nucleo no
     * levanta excepcion, devuelve basura- y con raw muy pequeño saldria un
     * rail de decenas de voltios que luego se propaga a la bateria y al
     * termometro. Un raw valido para 1,2 V sobre un rail de 2,5 a 3,6 V
     * cae entre 1365 y 1966; se admite de 900 a 2600, que es holgado y
     * sigue descartando lo imposible.
     */
    if (raw < 900U || raw > 2600U) { return (uint16_t)BATTERY_VDDA_PORSI; }

    vdda = ((uint32_t)BATTERY_VREFINT_MV * 4095UL) / (uint32_t)raw;
    if (vdda < 2000UL) { vdda = 2000UL; }
    if (vdda > 4000UL) { vdda = 4000UL; }
    return (uint16_t)vdda;
}

uint16_t battery_mv_de_raw(uint16_t raw, uint16_t vdda_mv, int16_t calib_mv)
{
    uint32_t adc_mv = ((uint32_t)raw * (uint32_t)vdda_mv) / 4095UL;
    int32_t  mv = (int32_t)(adc_mv * 4UL) + (int32_t)BATTERY_DIODE_DROP_MV
                + (int32_t)calib_mv;

    /* El ajuste puede ser negativo y la bateria puede estar a cero: sin
     * este suelo, restar daria un numero enorme al volver a sin signo. */
    if (mv < 0) { mv = 0; }
    if (mv > 65535) { mv = 65535; }
    return (uint16_t)mv;
}

int16_t battery_temp_de_raw(uint16_t raw, uint16_t vdda_mv)
{
    int32_t mv    = ((int32_t)raw * (int32_t)vdda_mv) / 4095;
    int32_t t_x10 = (((1450 - mv) * 10) / 41) + 250;

    if (t_x10 < -400) { t_x10 = -400; }   /* fuera de rango: algo va mal, */
    if (t_x10 > 1500) { t_x10 = 1500; }   /* mejor un tope que un absurdo */
    return (int16_t)(t_x10 / 10);
}
