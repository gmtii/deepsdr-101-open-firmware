#include "battery.h"
#include "gd32f4xx.h"

/*
 * Assumed pack range for battery_get_percent()'s linear mapping (see
 * battery.h) - a single-cell Li-ion/Li-Po as a starting placeholder,
 * NOT confirmed against this board's actual battery. Change these two
 * once known.
 */
#define BATTERY_EMPTY_MV 3000U
#define BATTERY_FULL_MV  4200U

/*
 * REVISION A FONDO DEL 08/10/2026: LAS DOS ESPERAS DEL ADC, CON PLAZO.
 *
 * Las dos conversiones de este fichero esperaban el fin de conversion
 * con un "while (SET != adc_flag_get(...)) { }" SIN SALIDA. El
 * razonamiento escrito era correcto -una conversion de 480 ciclos de
 * muestreo mas doce de aproximacion no llega al milisegundo- pero
 * describe lo que pasa cuando el ADC contesta, y si no contesta -reloj
 * del periferico apagado por un cambio en el arbol de relojes, la
 * calibracion del arranque que se quedo a medias, un hardfault en otro
 * sitio que dejo ADC0 a medio configurar- la radio se queda COLGADA con
 * el audio sonando -va por DMA- y la pantalla congelada. Es exactamente
 * el cuadro que ya costo una tarde con el reparto de memoria del 4285,
 * y encima lo dispara un medidor de bateria, que es lo ultimo por lo que
 * uno miraria.
 *
 * Con el tope la conversion sale igual -una vuelta del bucle son unos
 * pocos ciclos de nucleo y los 20 us de la conversion a 200 MHz son
 * cuatro mil, asi que cien mil vueltas son dos ordenes de magnitud de
 * sobra- y, si el ADC no contesta, se devuelve un valor de "no medido"
 * y el bucle principal sigue vivo.
 */
#define BATTERY_EOC_VUELTAS   100000UL

/* Lo que se devuelve cuando el ADC no contesta: el mismo tope de abajo
 * que ya se usa para "algo va mal, mejor un tope que un absurdo". */
#define BATTERY_TEMP_NO_MEDIDA  ((int16_t)-40)

void battery_init(void)
{
    rcu_periph_clock_enable(RCU_ADC0);

    /* PCLK2 (APB2) = 100MHz on this board's clock tree (AHB/2 off a
     * 200MHz core - see system_gd32f4xx.c's system_clock_200m_25m_hxtal(),
     * the function actually selected by this project's __SYSTEM_CLOCK_*
     * define). DIV4 = 25MHz, under the GD32F450 datasheet's 30MHz ADC
     * clock ceiling. Not latency-critical (a battery gauge polled from
     * the main loop), so there's no reason to push closer to the limit. */
    adc_clock_config(ADC_ADCCK_PCLK2_DIV4);

    adc_resolution_config(ADC0, ADC_RESOLUTION_12B);
    adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);

    /* Single conversion, software-triggered, one regular channel - no
     * scan/continuous/DMA, this isn't a streaming source like sdr_rx's
     * I/Q path. */
    adc_special_function_config(ADC0, ADC_SCAN_MODE, DISABLE);
    adc_special_function_config(ADC0, ADC_CONTINUOUS_MODE, DISABLE);
    adc_channel_length_config(ADC0, ADC_REGULAR_CHANNEL, 1U);
    adc_external_trigger_config(ADC0, ADC_REGULAR_CHANNEL, EXTERNAL_TRIGGER_DISABLE);

    /* Route channel 18 to VBAT/4 internally (ADC_SYNCCTL_VBATEN - see
     * battery.h's comment on the divider). The VBAT channel needs a
     * long sample time per the datasheet (it's a higher-impedance
     * internal path than a regular external pin) - use the longest
     * available, 480 cycles, since accuracy matters far more than
     * speed for a housekeeping reading like this. */
    adc_channel_16_to_18(ADC_VBAT_CHANNEL_SWITCH, ENABLE);
    adc_regular_channel_config(ADC0, 0U, ADC_CHANNEL_18, ADC_SAMPLETIME_480);

    adc_enable(ADC0);
    /* Datasheet-mandated stabilization delay after enabling the ADC
     * and before calibration. This only runs once at boot, so a crude
     * cycle-counting busy loop is fine here - same reasoning the
     * project already applies to its other uncalibrated delay loops
     * (see touch.c's delay_us_approx()), just without even bothering
     * to name it: a few thousand NOPs is a healthy multiple of the
     * datasheet's microsecond-scale requirement at this core clock. */
    {
        volatile uint32_t d;
        for (d = 0; d < 2000U; d++) {
            __NOP();
        }
    }
    adc_calibration_enable(ADC0);
}

/*
 * EL AJUSTE DEL USUARIO. Vive aqui y lo guarda/restaura settings; este
 * fichero solo lo acota, porque acotar donde se guarda y no donde se usa
 * deja la puerta abierta a que entre por otro sitio.
 */
static int16_t s_calib_mv;

void battery_calib_pon(int16_t mv)
{
    if (mv < (int16_t)BATTERY_CALIB_MIN) { mv = (int16_t)BATTERY_CALIB_MIN; }
    if (mv > (int16_t)BATTERY_CALIB_MAX) { mv = (int16_t)BATTERY_CALIB_MAX; }
    s_calib_mv = mv;
}

int16_t battery_calib_lee(void) { return s_calib_mv; }

/*
 * UNA CONVERSION DEL CANAL QUE SE LE DIGA, dejando el ADC como estaba.
 *
 * Sale de aqui porque ya habia DOS copias de este baile -la bateria y el
 * termometro- y la referencia interna seria la tercera. El comentario del
 * termometro decia que no restaurar el canal da "el tipo de averia que
 * aparece lejos de aqui y cuesta encontrar"; con tres copias, la
 * probabilidad de que una se olvide de restaurar es tres veces mayor.
 *
 * Devuelve 0 si el ADC no contesta dentro del plazo, y restaura igual.
 */
/*
 * UNA ESPERA CORTA, A OJO. Como la de spi_flash.c y la de touch.c: no
 * hace falta que sea exacta, hace falta que sea SUFICIENTE, y por eso el
 * bucle esta dimensionado largo.
 */
static void espera_us(uint32_t us)
{
    volatile uint32_t n = us * 50UL;   /* ~200 MHz, 4 ciclos por vuelta */

    while (n-- != 0UL) { }
}

/*
 * UNA CONVERSION DEL CANAL QUE SE LE DIGA, dejando el ADC como estaba.
 *
 * LO QUE LE FALTABA A LA PRIMERA VERSION, Y QUE SE VIO EN LA RADIO Y NO
 * AQUI - 12/10/2026. La fila "Calibrar voltaje" marcaba 3,00 V CLAVADOS,
 * que es exactamente el valor de reserva. O sea que no estaba midiendo:
 * estaba cayendo en el "porsi" y enseñandolo como si fuera una medida.
 * Tres cosas faltaban, y las tres son del periferico, no de la cuenta:
 *
 *   - LA BANDERA DE FIN SIN LIMPIAR ANTES DE DISPARAR. Si quedaba puesta
 *     de una conversion anterior, el bucle de espera salia al instante y
 *     se leia el dato VIEJO, que es el de otro canal.
 *   - EL TIEMPO DE ARRANQUE DE LA REFERENCIA. Encender el interruptor de
 *     la referencia interna no la deja lista en el mismo ciclo: la hoja de
 *     datos pide unas decenas de microsegundos. Convertir antes da un
 *     numero sin sentido.
 *   - Y LA PRIMERA CONVERSION DESPUES DE CAMBIAR DE CANAL, que en esta
 *     familia puede traerse el muestreo del canal anterior. Se tira y se
 *     hace otra.
 *
 * Nada de esto lo podia cazar el banco del PC: ahi no hay periferico que
 * tarde en arrancar. Lo caza una foto de la pantalla, y por eso la fila
 * enseña ahora tambien el numero crudo.
 */
static uint16_t lee_canal(uint8_t canal, uint32_t interruptor)
{
    uint16_t raw = 0U;
    uint32_t vueltas;
    uint8_t  hubo;
    uint8_t  intento;

    adc_channel_16_to_18(ADC_VBAT_CHANNEL_SWITCH, DISABLE);
    adc_channel_16_to_18(interruptor, ENABLE);
    adc_regular_channel_config(ADC0, 0U, canal, ADC_SAMPLETIME_480);

    /* Que la referencia o el sensor arranquen antes de preguntarles. */
    espera_us(30U);

    /* Dos conversiones: la primera se tira. Ver el comentario de arriba. */
    for (intento = 0U; intento < 2U; intento++) {
        adc_flag_clear(ADC0, ADC_FLAG_EOC);   /* ANTES de disparar */
        adc_software_trigger_enable(ADC0, ADC_REGULAR_CHANNEL);
        vueltas = BATTERY_EOC_VUELTAS;
        hubo = 1U;
        while (SET != adc_flag_get(ADC0, ADC_FLAG_EOC)) {
            if (--vueltas == 0U) { hubo = 0U; break; }
        }
        if (!hubo) { raw = 0U; break; }
        raw = adc_regular_data_read(ADC0);
        adc_flag_clear(ADC0, ADC_FLAG_EOC);
    }

    /* PASE LO QUE PASE, el canal vuelve a ser el de la bateria. */
    adc_channel_16_to_18(interruptor, DISABLE);
    adc_channel_16_to_18(ADC_VBAT_CHANNEL_SWITCH, ENABLE);
    adc_regular_channel_config(ADC0, 0U, ADC_CHANNEL_18, ADC_SAMPLETIME_480);
    return raw;
}

/*
 * EL NUMERO CRUDO DE LA REFERENCIA, para que se pueda VER cuando no
 * cuadra. Un valor de reserva que se enseña como si fuera una medida es
 * justo lo que escondio este fallo durante una version entera.
 */
uint16_t battery_vrefint_crudo(void)
{
    return lee_canal(ADC_CHANNEL_17, ADC_TEMP_VREF_CHANNEL_SWITCH);
}

uint16_t battery_vdda_mv(void)
{
    return battery_vdda_de_raw(battery_vrefint_crudo());
}

uint16_t battery_get_millivolts(void)
{
    uint16_t raw;

    adc_software_trigger_enable(ADC0, ADC_REGULAR_CHANNEL);
    {
        uint32_t vueltas = BATTERY_EOC_VUELTAS;
        while (SET != adc_flag_get(ADC0, ADC_FLAG_EOC)) {
            /* Blocking - one conversion at 480 sample cycles + 12
             * successive-approximation cycles is well under a millisecond
             * even at this ADC clock, and battery_get_millivolts() is only
             * ever called from the main loop (see battery.h), never the
             * demod ISR - spinning here is fine. */
            /* ...pero con plazo. Ver BATTERY_EOC_VUELTAS. */
            if (--vueltas == 0U) { return 0U; }
        }
    }
    raw = adc_regular_data_read(ADC0);
    adc_flag_clear(ADC0, ADC_FLAG_EOC);

    /*
     * Y la cuenta, contra el rail MEDIDO y no contra una constante. Ver
     * BATTERY_VREFINT_MV en battery.h: la de antes eran 3300 mV sobre un
     * regulador de tres voltios.
     */
    return battery_mv_de_raw(raw, battery_vdda_mv(), s_calib_mv);
}

int16_t battery_get_chip_temp_c(void)
{
    uint16_t raw;

    /*
     * El canal 16 (sensor de temperatura) y el 18 (VBAT) comparten
     * ADC0, asi que hay que pedir el otro interruptor interno,
     * reconfigurar el canal, convertir, y DEJARLO COMO ESTABA. Si no se
     * restaura, la siguiente medida de bateria leeria el sensor de
     * temperatura y daria un voltaje absurdo, que es el tipo de averia
     * que aparece lejos de aqui y cuesta encontrar.
     */
    adc_channel_16_to_18(ADC_VBAT_CHANNEL_SWITCH, DISABLE);
    adc_channel_16_to_18(ADC_TEMP_VREF_CHANNEL_SWITCH, ENABLE);
    adc_regular_channel_config(ADC0, 0U, ADC_CHANNEL_16, ADC_SAMPLETIME_480);

    adc_software_trigger_enable(ADC0, ADC_REGULAR_CHANNEL);
    {
        uint32_t vueltas = BATTERY_EOC_VUELTAS;
        uint8_t  hubo = 1U;
        while (SET != adc_flag_get(ADC0, ADC_FLAG_EOC)) {
            /* bloqueante y corto, mismo razonamiento que la bateria...
             * y con el mismo plazo, ver BATTERY_EOC_VUELTAS */
            if (--vueltas == 0U) { hubo = 0U; break; }
        }
        if (!hubo) {
            /* El canal hay que devolverlo a VBAT PASE LO QUE PASE: si se
             * sale de aqui con el sensor de temperatura puesto, la
             * siguiente medida de bateria lee el sensor y da un voltaje
             * absurdo, que es justo la averia lejana que el comentario de
             * arriba dice querer evitar. */
            adc_channel_16_to_18(ADC_TEMP_VREF_CHANNEL_SWITCH, DISABLE);
            adc_channel_16_to_18(ADC_VBAT_CHANNEL_SWITCH, ENABLE);
            adc_regular_channel_config(ADC0, 0U, ADC_CHANNEL_18, ADC_SAMPLETIME_480);
            return BATTERY_TEMP_NO_MEDIDA;
        }
    }
    raw = adc_regular_data_read(ADC0);
    adc_flag_clear(ADC0, ADC_FLAG_EOC);

    /* restaurar la configuracion de bateria antes de hacer ninguna cuenta */
    adc_channel_16_to_18(ADC_TEMP_VREF_CHANNEL_SWITCH, DISABLE);
    adc_channel_16_to_18(ADC_VBAT_CHANNEL_SWITCH, ENABLE);
    adc_regular_channel_config(ADC0, 0U, ADC_CHANNEL_18, ADC_SAMPLETIME_480);

    /* T = (V25 - Vts) / pendiente + 25, con la hoja de datos: V25 =
     * 1450 mV y pendiente 4,1 mV por grado, contra el rail MEDIDO -esta
     * cuenta llevaba el mismo 3300 y por tanto el mismo error-. */
    return battery_temp_de_raw(raw, battery_vdda_mv());
}

/*
 * EL PORCENTAJE, SIN VOLVER A MEDIR - 05/10/2026.
 *
 * Existe porque quien ya tiene los milivoltios no debe pagar una segunda
 * conversion para sacar el porcentaje de la MISMA tension. Eso es lo que
 * hacia top_sync() en main.c: battery_get_percent() y battery_get_millivolts()
 * seguidas, dos esperas activas del convertidor de ~20 us cada una, en cada
 * repintado de la cabecera.
 *
 * La cuenta vive aqui y no alli a proposito: el mapeo tension->porcentaje es
 * de este fichero, con sus constantes, y copiarlo a main.c seria la clase de
 * duplicado que luego se separa.
 */
uint8_t battery_pct_de_mv(uint16_t mv)
{
    uint32_t pct;

    if (mv <= BATTERY_EMPTY_MV) {
        return 0U;
    }
    if (mv >= BATTERY_FULL_MV) {
        return 100U;
    }

    pct = ((uint32_t)(mv - BATTERY_EMPTY_MV) * 100UL) / (uint32_t)(BATTERY_FULL_MV - BATTERY_EMPTY_MV);
    return (uint8_t)pct;
}

uint8_t battery_get_percent(void)
{
    return battery_pct_de_mv(battery_get_millivolts());
}
