#include "rtc_hw.h"
#include "rtc_cal.h"
#include "gd32f4xx.h"

/*
 * EL RELOJ DE HARDWARE. Ver rtc_hw.h para el porque de cada funcion;
 * aqui va el porque de cada numero.
 *
 * Reescrito entero el 13/10/2026 sobre el driver de GigaDevice que ya
 * venia en Firmware/Source/gd32f4xx_rtc.c -que hasta entonces se
 * enlazaba y no lo llamaba nadie- y sobre la hoja del GD32F4xx. La
 * aritmetica no esta aqui: esta en rtc_cal.c, que el simulador SI
 * compila y un banco SI mira.
 */

/*
 * LOS DOS PRESCALERES: 32768 / (128 * 256) = 1 Hz exacto.
 *
 * El asincrono manda el consumo y el sincrono la resolucion del
 * desplazamiento fino. Con el sincrono en 255, un paso de fraccion son
 * 1/256 de segundo, unos 3,9 ms, que es lo que puede corregir
 * rtc_hw_apply_shift() de una vez. Subir el sincrono daria pasos mas
 * finos y bajaria el asincrono, que gasta mas. Esta reparto es el que
 * recomienda el fabricante y el que hace que las dos cuentas salgan
 * enteras.
 */
#define RTC_ASYN_PRE     127U
#define RTC_SYN_PRE      255U

/* Un paso de fraccion, en milisegundos y por mil, para no perder el
 * decimal al acumular: 1000 / 256 = 3,90625 ms. */
#define PASO_US          3906U      /* microsegundos por paso de fraccion */

/*
 * LA SEMILLA DE ARRANQUE EN FRIO es a proposito una fecha redonda y
 * vieja. Si al encender sale "01/01/2026 00:00", se ve de lejos que
 * nadie ha puesto el reloj. Sembrar la fecha de compilacion seria peor:
 * parece una hora buena y no lo es.
 */
#define SEMILLA_YEAR     2026U
#define SEMILLA_MONTH    1U
#define SEMILLA_DAY      1U

/*
 * EL REGISTRO DE RESPALDO, Y POR QUE LLEVA UNA MAGIA.
 *
 * BKP0 guarda una magia y BKP1 los segundos de epoca de la ultima
 * sincronizacion. La magia hace falta porque estos registros sobreviven
 * a cualquier cosa menos a un corte de corriente: sin ella, lo que
 * dejara ahi dentro otro firmware -o el mismo antes de esta version- se
 * leeria como una fecha y la radio diria "sincronizada hace tres
 * semanas" sin haberlo estado nunca.
 *
 * Al cambiar de version la magia no coincide y la radio dice "nunca
 * sincronizado", que es la respuesta prudente: no sabe, y lo dice. En
 * cuanto entre el primer DCF77, RDS o toque de teclado, se arregla.
 */
#define BKP_MAGIA        0x52544331UL   /* "RTC1" */

static uint8_t  s_lxtal_mal    = 1U;    /* hasta que se demuestre lo contrario */
static uint8_t  s_arrancado    = 0U;
static uint8_t  s_sucio        = 0U;
static uint32_t s_seq          = 0UL;
static int32_t  s_shift_us     = 0L;    /* se acumula en us y se sirve en ms */

/* --------------------------------------------------------------- */

static void respaldo_abre(void)
{
    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();
}

static uint32_t epoca_ahora(void)
{
    rtc_hw_datetime_t d;
    rtc_hw_get(&d);
    return rtc_cal_epoca(d.year, d.month, d.day, d.hour, d.minute, d.second);
}

static void marca_sincronizado(void)
{
    uint32_t e = epoca_ahora();
    if (e == 0UL) { return; }       /* sin calendario util no se anota nada */
    respaldo_abre();
    RTC_BKP0 = BKP_MAGIA;
    RTC_BKP1 = e;
}

/*
 * Carga el calendario. Se usa tanto al sembrar como en rtc_hw_set(), y
 * es el UNICO sitio que llama a rtc_init(): asi no hay dos maneras de
 * dejar el periferico configurado que puedan separarse.
 */
static uint8_t calendario_pon(uint16_t year, uint8_t month, uint8_t day,
                              uint8_t hour, uint8_t minute, uint8_t second)
{
    rtc_parameter_struct p;

    if (!rtc_cal_valida(year, month, day, hour, minute, second)) { return 0U; }

    /* El segundo 60 del intercalar no cabe en el periferico: se guarda
     * como 59 y el propio RTC pasa al 0 un tic despues. Se admite la
     * trama y se pierde el segundo, que es justo lo que hace la emisora. */
    if (second > 59U) { second = 59U; }

    p.year           = rtc_cal_dec_a_bcd((uint8_t)(year - 2000U));
    p.month          = rtc_cal_dec_a_bcd(month);
    p.date           = rtc_cal_dec_a_bcd(day);
    /* El periferico NO deduce el dia de la semana: hay que darselo, y
     * si se le da mal se queda mal hasta la proxima puesta en hora. */
    p.day_of_week    = rtc_cal_dia_semana(year, month, day);
    p.hour           = rtc_cal_dec_a_bcd(hour);
    p.minute         = rtc_cal_dec_a_bcd(minute);
    p.second         = rtc_cal_dec_a_bcd(second);
    p.factor_asyn    = RTC_ASYN_PRE;
    p.factor_syn     = RTC_SYN_PRE;
    p.am_pm          = RTC_AM;
    p.display_format = RTC_24HOUR;

    return (rtc_init(&p) == SUCCESS) ? 1U : 0U;
}

/* --------------------------------------------------------------- */

void rtc_hw_init(void)
{
    uint32_t espera;

    respaldo_abre();

    /*
     * EL CRISTAL, CON TIEMPO MEDIDO Y NO INFINITO.
     *
     * Un 32.768 Hz tarda del orden de cientos de milisegundos en
     * arrancar, mucho mas que un cuarzo rapido. Esperar sin tope seria
     * dejar la radio colgada en el arranque si el cristal esta mal
     * soldado o le faltan los condensadores; esperar poco seria
     * declararlo roto cuando solo iba lento. Se le dan cuatro millones
     * de vueltas, que a esta velocidad de reloj son del orden de un
     * segundo largo, y si no engancha se sigue arrancando: la radio
     * tiene que encender aunque su reloj no valga.
     */
    rcu_osci_on(RCU_LXTAL);
    for (espera = 4000000UL; espera > 0UL; espera--) {
        if (rcu_flag_get(RCU_FLAG_LXTALSTB) != RESET) { break; }
    }
    s_lxtal_mal = (espera == 0UL) ? 1U : 0U;

    rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);
    rcu_periph_clock_enable(RCU_RTC);
    rtc_register_sync_wait();

    /*
     * Y AQUI ESTA LA DECISION QUE IMPORTA: si el calendario ya iba, no
     * se toca.
     *
     * YCM lo pone el propio hardware al programar el año y solo se
     * borra cuando el dominio de respaldo se queda sin alimentacion. O
     * sea que es exactamente la pregunta "¿este RTC viene de un corte
     * de verdad, o de un simple reinicio?". Usarlo en vez de una magia
     * propia tiene una ventaja que se nota: una radio que ya venia
     * contando bien NO pierde la hora al instalar esta version.
     */
    if (rtc_flag_get(RTC_FLAG_YCM) != RESET) {
        s_arrancado = 1U;
        return;
    }

    s_arrancado = calendario_pon(SEMILLA_YEAR, SEMILLA_MONTH, SEMILLA_DAY,
                                 0U, 0U, 0U);
}

bool rtc_hw_lxtal_failed(void)
{
    return (s_lxtal_mal != 0U);
}

void rtc_hw_get(rtc_hw_datetime_t *out)
{
    rtc_parameter_struct p;

    if (out == 0) { return; }

    if (!s_arrancado) {
        out->year = SEMILLA_YEAR; out->month = SEMILLA_MONTH;
        out->day  = SEMILLA_DAY;  out->hour  = 0U;
        out->minute = 0U;         out->second = 0U;
        return;
    }

    rtc_current_time_get(&p);
    out->year   = (uint16_t)(2000U + rtc_cal_bcd_a_dec(p.year));
    out->month  = rtc_cal_bcd_a_dec((uint8_t)(p.month & 0x1FU));
    out->day    = rtc_cal_bcd_a_dec(p.date);
    out->hour   = rtc_cal_bcd_a_dec(p.hour);
    out->minute = rtc_cal_bcd_a_dec(p.minute);
    out->second = rtc_cal_bcd_a_dec(p.second);

    /*
     * UN CALENDARIO IMPOSIBLE NO SE SIRVE COMO SI FUERA BUENO. Pasa si
     * se lee el shadow a medias o si el dominio acaba de perder la
     * alimentacion: salen ceros, y "mes 0" recorre el arbol entero
     * hasta la pantalla. Se devuelve la semilla, que al menos es una
     * fecha que existe y que se ve falsa.
     */
    if (!rtc_cal_valida(out->year, out->month, out->day,
                        out->hour, out->minute, out->second)) {
        out->year = SEMILLA_YEAR; out->month = SEMILLA_MONTH;
        out->day  = SEMILLA_DAY;  out->hour  = 0U;
        out->minute = 0U;         out->second = 0U;
    }
}

void rtc_hw_set(const rtc_hw_datetime_t *dt)
{
    if (dt == 0) { return; }
    if (!calendario_pon(dt->year, dt->month, dt->day,
                        dt->hour, dt->minute, dt->second)) {
        return;                     /* fecha imposible: no se escribe nada */
    }
    s_arrancado = 1U;
    s_sucio     = 1U;
    s_seq++;
    marca_sincronizado();
}

bool rtc_hw_apply_shift(bool add_one_second, uint16_t subsecond_fraction)
{
    uint32_t add;
    int32_t  us;

    if (!s_arrancado) { return false; }
    if (subsecond_fraction > RTC_SYN_PRE) { return false; }

    add = add_one_second ? RTC_SHIFT_ADD1S_SET : RTC_SHIFT_ADD1S_RESET;
    if (rtc_second_adjust(add, (uint32_t)subsecond_fraction) != SUCCESS) {
        return false;
    }

    /* La cuenta se lleva en microsegundos y se sirve en milisegundos:
     * acumulando en ms, un desplazamiento de 3,9 ms se guardaria como 3
     * y al cabo de mil correcciones el total mentiria casi un segundo. */
    us  = add_one_second ? 1000000L : 0L;
    us -= (int32_t)subsecond_fraction * (int32_t)PASO_US;
    s_shift_us += us;

    marca_sincronizado();
    return true;
}

bool rtc_hw_consume_dirty(void)
{
    if (s_sucio) { s_sucio = 0U; return true; }
    return false;
}

uint32_t rtc_hw_get_set_seq(void)
{
    return s_seq;
}

int32_t rtc_hw_get_cumulative_shift_ms(void)
{
    return s_shift_us / 1000L;
}

bool rtc_hw_has_ever_synced(void)
{
    return (RTC_BKP0 == BKP_MAGIA);
}

uint32_t rtc_hw_get_seconds_since_sync(void)
{
    uint32_t ahora, antes;

    if (RTC_BKP0 != BKP_MAGIA) { return 0UL; }

    ahora = epoca_ahora();
    antes = RTC_BKP1;

    /*
     * HACIA ATRAS NO. Pasa si alguien pone el reloj a una hora ANTERIOR
     * a la ultima sincronizacion -un huso mal puesto, una emisora que
     * manda mal-. Sin esto, la resta sin signo da cuatro mil millones y
     * la pantalla dice "hace 136 años".
     */
    if (ahora < antes) { return 0UL; }
    return ahora - antes;
}
