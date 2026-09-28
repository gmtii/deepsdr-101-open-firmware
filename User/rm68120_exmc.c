#include "rm68120_exmc.h"

/* Contador de milisegundos real, incrementado por SysTick_Handler en
 * main.c (SysTick configurado a 1ms exactos contra SystemCoreClock). */
extern volatile uint32_t g_msticks;

static void delay_ms(uint32_t ms)
{
    uint32_t start = g_msticks;
    while ((g_msticks - start) < ms) {
        __NOP();
    }
}

void rm68120_exmc_gpio_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOE);
    rcu_periph_clock_enable(RCU_EXMC);

    /* --- RESET: GPIO normal de salida push-pull --- */
    gpio_mode_set(LCD_RST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LCD_RST_PIN);
    gpio_output_options_set(LCD_RST_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, LCD_RST_PIN);
    gpio_bit_set(LCD_RST_PORT, LCD_RST_PIN);

    /* --- Puerto D: D2,D3,D13,D14,D15,D0,D1, NOE, NWE, NE0, A16 ---
     * PD0=D2 PD1=D3 PD4=NOE PD5=NWE PD7=NE0 PD8=D13 PD9=D14
     * PD10=D15 PD11=A16(RS) PD14=D0 PD15=D1
     */
    {
        uint32_t pins = GPIO_PIN_0  | GPIO_PIN_1  | GPIO_PIN_4  | GPIO_PIN_5  |
                        GPIO_PIN_7  | GPIO_PIN_8  | GPIO_PIN_9  | GPIO_PIN_10 |
                        GPIO_PIN_11 | GPIO_PIN_14 | GPIO_PIN_15;

        gpio_mode_set(GPIOD, GPIO_MODE_AF, GPIO_PUPD_NONE, pins);
        gpio_output_options_set(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, pins);
        gpio_af_set(GPIOD, GPIO_AF_12, pins);
    }

    /* --- Puerto E: D4..D12 (PE7-PE15) --- */
    {
        uint32_t pins = GPIO_PIN_7  | GPIO_PIN_8  | GPIO_PIN_9  | GPIO_PIN_10 |
                        GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |
                        GPIO_PIN_15;

        gpio_mode_set(GPIOE, GPIO_MODE_AF, GPIO_PUPD_NONE, pins);
        gpio_output_options_set(GPIOE, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, pins);
        gpio_af_set(GPIOE, GPIO_AF_12, pins);
    }
}

/* ---------------------------------------------------------------------
 * EL TIEMPO DE DATO DEL BUS, AJUSTABLE EN MARCHA. 24/09/2026.
 * ---------------------------------------------------------------------
 * Importa mucho mas de lo que parece: el volcado de la cascada son 54.432
 * escrituras de 16 bits seguidas, y el espectro otras tantas. Cada ciclo de
 * escritura dura (direccion + 1) + (dato + 1) ciclos de HCLK, y HCLK son
 * 200 MHz, o sea 5 ns. El campo del dato manda: va de 1 a 255.
 *
 * Y AQUI HAY UNA SORPRESA. Este fichero configura el bus en
 * rm68120_exmc_bus_init(), pero a esa funcion solo se llega desde
 * rm68120_init()... y main.c NO llama a rm68120_init(): lo salta desde el
 * 30/07/2026 porque reinicializar el panel rompia el reloj del audio (ver
 * el comentario de main.c). O sea que **la configuracion del bus que hay
 * escrita en este fichero no se ha aplicado nunca desde entonces**. Lo que
 * manda es lo que dejo el gestor de arranque, y no sabemos que es.
 *
 * Por eso estas funciones LEEN EL REGISTRO en vez de devolver una copia en
 * memoria: asi dicen lo que hay de verdad, lo haya puesto quien lo haya
 * puesto. Medir en vez de suponer, otra vez.
 *
 * Y por eso rm68120_bus_dato_pon() toca SOLO el campo del dato y deja el
 * resto del registro como esta: no vamos a reconfigurar a ciegas un bus que
 * lleva dos meses funcionando, solo a bajarle el numero que domina el coste.
 * No se guarda en ajustes: si te pasas y la pantalla se vuelve ilegible,
 * apagas y vuelve lo del gestor de arranque.
 */
volatile uint32_t g_lcd_ciclos;   /* ver la cabecera */

uint32_t rm68120_ciclo(void)
{
    return DWT->CYCCNT;
}

static uint16_t s_dato_arranque;   /* lo que habia antes de que tocaramos */
static uint8_t  s_dato_leido;

static uint16_t bus_dset(void)
{
    return (uint16_t)((EXMC_SNTCFG0 & EXMC_SNTCFG_DSET) >> 8);
}

uint16_t rm68120_bus_dato(void)
{
    if (!s_dato_leido) {
        s_dato_arranque = bus_dset();
        s_dato_leido    = 1U;
    }
    return bus_dset();
}

/* El de antes de tocar nada, para poder volver. */
uint16_t rm68120_bus_dato_arranque(void)
{
    (void)rm68120_bus_dato();
    return s_dato_arranque;
}

/* Nanosegundos aproximados de una escritura completa. */
uint16_t rm68120_bus_ns(void)
{
    uint32_t aset   = (EXMC_SNTCFG0 & EXMC_SNTCFG_ASET);
    uint32_t dset   = bus_dset();
    uint32_t ciclos = (aset + 1U) + (dset + 1U);
    uint32_t ns     = (ciclos * 1000U) / (SystemCoreClock / 1000000U);

    return (uint16_t)((ns > 65535U) ? 65535U : ns);
}

void rm68120_bus_dato_pon(uint16_t dato)
{
    uint32_t r;

    (void)rm68120_bus_dato();   /* que quede guardado el de arranque */
    if (dato < 1U)   { dato = 1U;   }
    if (dato > 255U) { dato = 255U; }

    r  = EXMC_SNTCFG0;
    r &= ~EXMC_SNTCFG_DSET;
    r |= ((uint32_t)dato << 8) & EXMC_SNTCFG_DSET;
    EXMC_SNTCFG0 = r;
}

void rm68120_exmc_bus_init(void)
{
    exmc_norsram_parameter_struct exmc_init_struct;
    exmc_norsram_timing_parameter_struct timing_init_struct;

    /*
     * Bajado de los maximos de seguridad (255/15/15/15) a valores mucho
     * mas rapidos pero todavia conservadores, para probar en hardware
     * real (Jorge, 27/07/2026 - reporto refresco muy lento con los
     * maximos). No he podido extraer con confianza la tabla de timing
     * exacta del datasheet del RM68120 (igual que nos paso con las
     * tablas de AF del GD32F450 - el texto del PDF no sale fiable), asi
     * que esto es un punto de partida razonable a verificar
     * empiricamente, no un valor "correcto" tomado del datasheet:
     *   - Si la imagen sale LIMPIA (sin ruido/pixeles corruptos): se
     *     puede seguir bajando estos valores metodicamente para ganar
     *     mas velocidad todavia.
     *   - Si aparece ruido/corrupcion: subir estos valores de nuevo
     *     (empezar por asyn_data_setuptime, es el que mas cuenta
     *     domina el tiempo total: son ciclos de HCLK POR CADA escritura
     *     al bus, y a ~10ns/ciclo con HCLK real ~98.3MHz, el valor
     *     anterior de 255 suponia ~2.6us solo en setup de datos POR
     *     PIXEL - con 800x480=384000 pixeles en un fill de pantalla
     *     completa, eso son mas de 1 segundo solo en esa espera).
     */
    timing_init_struct.asyn_access_mode      = EXMC_ACCESS_MODE_A;
    timing_init_struct.syn_data_latency      = EXMC_DATALAT_2_CLK;
    timing_init_struct.syn_clk_division      = EXMC_SYN_CLOCK_RATIO_DISABLE;
    timing_init_struct.bus_latency           = 3;
    /* Ya no es 15 fijo: ver rm68120_bus_dato_pon() justo encima. El aviso
     * de arriba decia "si la imagen sale limpia se puede seguir bajando
     * metodicamente" y llevaba desde julio sin que nadie lo hiciera, porque
     * bajarlo obligaba a recompilar y reflashear por cada intento. Ahora se
     * baja desde la ventana de informacion, mirando la pantalla.
     *
     * Y una correccion al comentario de arriba: dice HCLK ~98,3 MHz. Ya no.
     * El reloj es de 200 MHz con el AHB sin dividir (ver system_gd32f4xx.c,
     * __SYSTEM_CLOCK_200M_PLL_25M_HXTAL y RCU_AHB_CKSYS_DIV1), o sea 5 ns
     * por ciclo. Con 15 el ciclo de escritura sale a unos 100 ns. */
    timing_init_struct.asyn_data_setuptime   = 15;
    timing_init_struct.asyn_address_holdtime = 3;
    timing_init_struct.asyn_address_setuptime= 3;

    exmc_init_struct.norsram_region    = EXMC_BANK0_NORSRAM_REGION0; /* NE0 = tu CS (PD7) */
    exmc_init_struct.address_data_mux  = DISABLE;
    exmc_init_struct.memory_type       = EXMC_MEMORY_TYPE_SRAM;
    exmc_init_struct.databus_width     = EXMC_NOR_DATABUS_WIDTH_16B;
    exmc_init_struct.burst_mode        = DISABLE;
    exmc_init_struct.nwait_polarity    = EXMC_NWAIT_POLARITY_LOW;
    exmc_init_struct.wrap_burst_mode   = DISABLE;
    exmc_init_struct.nwait_config      = EXMC_NWAIT_CONFIG_BEFORE;
    exmc_init_struct.memory_write      = ENABLE;
    exmc_init_struct.nwait_signal      = DISABLE;
    exmc_init_struct.extended_mode     = DISABLE;
    exmc_init_struct.asyn_wait         = DISABLE;
    exmc_init_struct.write_mode        = EXMC_ASYN_WRITE;
    exmc_init_struct.read_write_timing = &timing_init_struct;
    exmc_init_struct.write_timing      = &timing_init_struct;

    exmc_norsram_init(&exmc_init_struct);
    exmc_norsram_enable(EXMC_BANK0_NORSRAM_REGION0);
}

void rm68120_hw_reset(void)
{
    gpio_bit_set(LCD_RST_PORT, LCD_RST_PIN);
    delay_ms(10);
    gpio_bit_reset(LCD_RST_PORT, LCD_RST_PIN);
    delay_ms(20);
    gpio_bit_set(LCD_RST_PORT, LCD_RST_PIN);
    delay_ms(120);   /* el RM68120 necesita tiempo tras el reset antes de aceptar comandos */
}

void rm68120_write_cmd(uint16_t cmd)
{
    LCD_REG = cmd;
}

void rm68120_write_data(uint16_t data)
{
    LCD_DAT = data;
}

void rm68120_write_cmd_data(uint16_t cmd, uint16_t data)
{
    LCD_REG = cmd;
    LCD_DAT = data;
}

uint16_t rm68120_read_data(void)
{
    return LCD_DAT;
}

/*
 * Define la ventana de escritura en la GRAM (CASET=columnas, RASET=filas)
 * y deja el controlador en modo "escritura de RAM" (RAMWR). Formato
 * tomado del mismo driver de referencia de Espressif que la secuencia
 * de init (ver rm68120_panel_init_sequence): cada limite se manda como
 * dos escrituras de 8 bits (byte alto / byte bajo) en subregistros
 * consecutivos de CASET/RASET.
 */
/* ---------------------------------------------------------------------
 * PRUEBA DEL DESPLAZAMIENTO POR HARDWARE. 25/09/2026.
 * ---------------------------------------------------------------------
 * LO QUE SE QUIERE SABER. Hoy, para meter UNA linea nueva en la cascada, se
 * repintan las 72: como la imagen se desplaza, todos los pixeles cambian de
 * sitio. Son 54.432 conversiones y 54.432 escrituras por linea.
 *
 * Los controladores de esta familia saben desplazar su propia memoria: se
 * les dice "de aqui a aqui es zona movil" y luego "el origen esta en la
 * fila N", y la imagen rueda sola. Si el RM68120 lo hace, escribir una
 * linea nueva costaria 756 escrituras en vez de 54.432 -setenta y dos veces
 * menos- y ademas el historial de 54.432 bytes dejaria de hacer falta.
 *
 * DOS PEGAS, Y POR ESO ESTO ES UNA PRUEBA Y NO UNA FUNCION:
 *
 *  1. El panel va GIRADO. La secuencia de arranque pone MADCTL = 0x60, que
 *     lleva el bit de intercambiar filas y columnas: el panel es de 480x800
 *     de pie y lo usamos como 800x480 tumbado. El desplazamiento mueve las
 *     filas DEL PANEL, y despues del intercambio esas filas son las
 *     columnas de la pantalla. Lo mas probable es que ruede DE LADO.
 *  2. Coge el ancho entero del panel. No se puede limitar a los 756 px de
 *     la cascada, asi que se llevaria por delante lo que haya al lado.
 *
 * Y una tercera: del RM68120 no hay documentacion fiable, asi que ni
 * siquiera se sabe si implementa estos registros. Si no los implementa,
 * escribirlos podria dejar el panel en un estado raro.
 *
 * ---------------------------------------------------------------------
 * PROBADO EL 25/09/2026 EN LA RADIO: NO. No se mueve ni un pixel, con el
 * origen puesto a 400 -media pantalla- y mirando el panel entero. El
 * RM68120 no implementa estos registros, o no en esta forma.
 *
 * Se deja la funcion escrita, sin llamarla nadie, por dos razones: que
 * conste que se probo y con que valores, para que nadie vuelva a perseguir
 * la idea a ciegas; y porque si algun dia aparece documentacion de verdad
 * del chip, el punto de partida ya esta aqui.
 * ---------------------------------------------------------------------
 *
 * POR QUE ERA SEGURO PROBARLO: main.c no llama a rm68120_init(), o sea
 * que quien inicializa el panel es el gestor de arranque, en cada encendido.
 * Apagar y encender lo devuelve a su sitio pase lo que pase aqui.
 *
 * Las direcciones siguen la convencion de este panel, que es la misma que
 * usa set_window aqui abajo: la orden va en el byte alto y el numero de
 * parametro en el bajo (0x2A00..0x2A03 para CASET). Asi que 0x33 -definir
 * la zona movil- son 0x3300..0x3305, y 0x37 -mover el origen- 0x3700 y
 * 0x3701.
 */
void rm68120_scroll_prueba(uint16_t origen)
{
    /* Zona movil = el panel entero en su orientacion NATIVA: 800 filas de
     * panel, que en pantalla son las 800 columnas. Arriba y abajo fijos a
     * cero. La suma de las tres tiene que dar la altura nativa. */
    const uint16_t fija_arriba = 0U;
    const uint16_t movil       = 800U;
    const uint16_t fija_abajo  = 0U;

    rm68120_write_cmd_data(0x3300, (uint16_t)((fija_arriba >> 8) & 0xFFU));
    rm68120_write_cmd_data(0x3301, (uint16_t)( fija_arriba       & 0xFFU));
    rm68120_write_cmd_data(0x3302, (uint16_t)((movil       >> 8) & 0xFFU));
    rm68120_write_cmd_data(0x3303, (uint16_t)( movil             & 0xFFU));
    rm68120_write_cmd_data(0x3304, (uint16_t)((fija_abajo  >> 8) & 0xFFU));
    rm68120_write_cmd_data(0x3305, (uint16_t)( fija_abajo        & 0xFFU));

    rm68120_write_cmd_data(0x3700, (uint16_t)((origen >> 8) & 0xFFU));
    rm68120_write_cmd_data(0x3701, (uint16_t)( origen       & 0xFFU));
}

void rm68120_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    rm68120_write_cmd_data(0x2A00, (x0 >> 8) & 0xFF);
    rm68120_write_cmd_data(0x2A01, x0 & 0xFF);
    rm68120_write_cmd_data(0x2A02, (x1 >> 8) & 0xFF);
    rm68120_write_cmd_data(0x2A03, x1 & 0xFF);

    rm68120_write_cmd_data(0x2B00, (y0 >> 8) & 0xFF);
    rm68120_write_cmd_data(0x2B01, y0 & 0xFF);
    rm68120_write_cmd_data(0x2B02, (y1 >> 8) & 0xFF);
    rm68120_write_cmd_data(0x2B03, y1 & 0xFF);

    rm68120_write_cmd(0x2C00); /* RAMWR: siguientes escrituras de datos van a GRAM */
}

/*
 * Rellena toda la pantalla con un color solido en formato RGB565. Util
 * para confirmar que el bus EXMC/GRAM funciona de verdad (si esto sale
 * limpio, el "ruido" que veias antes era solo contenido sin inicializar
 * de la GRAM, no un fallo de comunicacion).
 *
 * Ventana ajustada a horizontal (CASET 0-799, RASET 0-479) - confirmado
 * con hardware real que asi es como funciona el panel con el MADCTL
 * actual (0xA3), ver comentario de GFX_SCREEN_WIDTH/HEIGHT en gfx.h.
 */
void rm68120_fill_screen(uint16_t color_rgb565)
{
    uint32_t total_pixels = 800UL * 480UL;

    rm68120_set_window(0, 0, 799, 479);

    for (uint32_t i = 0; i < total_pixels; i++) {
        rm68120_write_data(color_rgb565);
    }
}

/*
 * Secuencia de inicializacion de registros del panel RM68120 480x800.
 *
 * ORIGEN: adaptada del driver oficial de Espressif Systems para el
 * ESP32-S2-HMI-DevKit (que usa un panel RM68120 de 480x800 en modo
 * paralelo), publicado bajo licencia Apache License 2.0:
 *   https://github.com/espressif/esp-dev-kits/blob/master/examples/esp32-s2-hmi-devkit-1/components/screen/controller_driver/rm68120/rm68120.c
 *   Copyright 2020 Espressif Systems (Shanghai) Co. Ltd.
 *   Licensed under the Apache License, Version 2.0
 *
 * Se ha traducido mecanicamente LCD_WRITE_REG(cmd,data)/LCD_WRITE_CMD(cmd)
 * a las funciones equivalentes de este proyecto (rm68120_write_cmd_data /
 * rm68120_write_cmd), sin alterar ningun valor de registro.
 *
 * AVISO: los valores de gamma/power/timing son los que usa el panel
 * concreto de esa placa de Espressif. Si tu panel de 480x800 es de otro
 * fabricante, la imagen puede salir con colores/contraste distintos a
 * los esperados aunque la inicializacion "funcione" (arranque sin
 * quedarse en blanco). Ajusta gamma (0xD1xx-0xD6xx) y VCOM (0xBE01) si
 * hace falta, contra el datasheet real de tu panel si lo consigues.
 */
#if !RM68120_USE_NT35510_SEQUENCE
static void rm68120_panel_init_sequence(void)
{
    /* QUITADO: el firmware original NO manda este Software Reset por
     * comando. Solo usa el pulso fisico del pin de reset y va directo
     * al chequeo de ID + secuencia de desbloqueo. Lo teniamos porque
     * el ejemplo de Espressif lo incluia, pero no es lo que hace el
     * firmware real de esta pantalla.
     * rm68120_write_cmd(0x0100); // Software Reset
     * delay_ms(10);
     */
    rm68120_write_cmd_data(0xF000, 0x55);
    rm68120_write_cmd_data(0xF001, 0xAA);
    rm68120_write_cmd_data(0xF002, 0x52);
    rm68120_write_cmd_data(0xF003, 0x08);
    rm68120_write_cmd_data(0xF004, 0x01);

    //GAMMA SETING  RED
    rm68120_write_cmd_data(0xD100, 0x00);
    rm68120_write_cmd_data(0xD101, 0x00);
    rm68120_write_cmd_data(0xD102, 0x1b);
    rm68120_write_cmd_data(0xD103, 0x44);
    rm68120_write_cmd_data(0xD104, 0x62);
    rm68120_write_cmd_data(0xD105, 0x00);
    rm68120_write_cmd_data(0xD106, 0x7b);
    rm68120_write_cmd_data(0xD107, 0xa1);
    rm68120_write_cmd_data(0xD108, 0xc0);
    rm68120_write_cmd_data(0xD109, 0xee);
    rm68120_write_cmd_data(0xD10A, 0x55);
    rm68120_write_cmd_data(0xD10B, 0x10);
    rm68120_write_cmd_data(0xD10C, 0x2c);
    rm68120_write_cmd_data(0xD10D, 0x43);
    rm68120_write_cmd_data(0xD10E, 0x57);
    rm68120_write_cmd_data(0xD10F, 0x55);
    rm68120_write_cmd_data(0xD110, 0x68);
    rm68120_write_cmd_data(0xD111, 0x78);
    rm68120_write_cmd_data(0xD112, 0x87);
    rm68120_write_cmd_data(0xD113, 0x94);
    rm68120_write_cmd_data(0xD114, 0x55);
    rm68120_write_cmd_data(0xD115, 0xa0);
    rm68120_write_cmd_data(0xD116, 0xac);
    rm68120_write_cmd_data(0xD117, 0xb6);
    rm68120_write_cmd_data(0xD118, 0xc1);
    rm68120_write_cmd_data(0xD119, 0x55);
    rm68120_write_cmd_data(0xD11A, 0xcb);
    rm68120_write_cmd_data(0xD11B, 0xcd);
    rm68120_write_cmd_data(0xD11C, 0xd6);
    rm68120_write_cmd_data(0xD11D, 0xdf);
    rm68120_write_cmd_data(0xD11E, 0x95);
    rm68120_write_cmd_data(0xD11F, 0xe8);
    rm68120_write_cmd_data(0xD120, 0xf1);
    rm68120_write_cmd_data(0xD121, 0xfa);
    rm68120_write_cmd_data(0xD122, 0x02);
    rm68120_write_cmd_data(0xD123, 0xaa);
    rm68120_write_cmd_data(0xD124, 0x0b);
    rm68120_write_cmd_data(0xD125, 0x13);
    rm68120_write_cmd_data(0xD126, 0x1d);
    rm68120_write_cmd_data(0xD127, 0x26);
    rm68120_write_cmd_data(0xD128, 0xaa);
    rm68120_write_cmd_data(0xD129, 0x30);
    rm68120_write_cmd_data(0xD12A, 0x3c);
    rm68120_write_cmd_data(0xD12B, 0x4A);
    rm68120_write_cmd_data(0xD12C, 0x63);
    rm68120_write_cmd_data(0xD12D, 0xea);
    rm68120_write_cmd_data(0xD12E, 0x79);
    rm68120_write_cmd_data(0xD12F, 0xa6);
    rm68120_write_cmd_data(0xD130, 0xd0);
    rm68120_write_cmd_data(0xD131, 0x20);
    rm68120_write_cmd_data(0xD132, 0x0f);
    rm68120_write_cmd_data(0xD133, 0x8e);
    rm68120_write_cmd_data(0xD134, 0xff);
    //GAMMA SETING GREEN
    rm68120_write_cmd_data(0xD200, 0x00);
    rm68120_write_cmd_data(0xD201, 0x00);
    rm68120_write_cmd_data(0xD202, 0x1b);
    rm68120_write_cmd_data(0xD203, 0x44);
    rm68120_write_cmd_data(0xD204, 0x62);
    rm68120_write_cmd_data(0xD205, 0x00);
    rm68120_write_cmd_data(0xD206, 0x7b);
    rm68120_write_cmd_data(0xD207, 0xa1);
    rm68120_write_cmd_data(0xD208, 0xc0);
    rm68120_write_cmd_data(0xD209, 0xee);
    rm68120_write_cmd_data(0xD20A, 0x55);
    rm68120_write_cmd_data(0xD20B, 0x10);
    rm68120_write_cmd_data(0xD20C, 0x2c);
    rm68120_write_cmd_data(0xD20D, 0x43);
    rm68120_write_cmd_data(0xD20E, 0x57);
    rm68120_write_cmd_data(0xD20F, 0x55);
    rm68120_write_cmd_data(0xD210, 0x68);
    rm68120_write_cmd_data(0xD211, 0x78);
    rm68120_write_cmd_data(0xD212, 0x87);
    rm68120_write_cmd_data(0xD213, 0x94);
    rm68120_write_cmd_data(0xD214, 0x55);
    rm68120_write_cmd_data(0xD215, 0xa0);
    rm68120_write_cmd_data(0xD216, 0xac);
    rm68120_write_cmd_data(0xD217, 0xb6);
    rm68120_write_cmd_data(0xD218, 0xc1);
    rm68120_write_cmd_data(0xD219, 0x55);
    rm68120_write_cmd_data(0xD21A, 0xcb);
    rm68120_write_cmd_data(0xD21B, 0xcd);
    rm68120_write_cmd_data(0xD21C, 0xd6);
    rm68120_write_cmd_data(0xD21D, 0xdf);
    rm68120_write_cmd_data(0xD21E, 0x95);
    rm68120_write_cmd_data(0xD21F, 0xe8);
    rm68120_write_cmd_data(0xD220, 0xf1);
    rm68120_write_cmd_data(0xD221, 0xfa);
    rm68120_write_cmd_data(0xD222, 0x02);
    rm68120_write_cmd_data(0xD223, 0xaa);
    rm68120_write_cmd_data(0xD224, 0x0b);
    rm68120_write_cmd_data(0xD225, 0x13);
    rm68120_write_cmd_data(0xD226, 0x1d);
    rm68120_write_cmd_data(0xD227, 0x26);
    rm68120_write_cmd_data(0xD228, 0xaa);
    rm68120_write_cmd_data(0xD229, 0x30);
    rm68120_write_cmd_data(0xD22A, 0x3c);
    rm68120_write_cmd_data(0xD22B, 0x4a);
    rm68120_write_cmd_data(0xD22C, 0x63);
    rm68120_write_cmd_data(0xD22D, 0xea);
    rm68120_write_cmd_data(0xD22E, 0x79);
    rm68120_write_cmd_data(0xD22F, 0xa6);
    rm68120_write_cmd_data(0xD230, 0xd0);
    rm68120_write_cmd_data(0xD231, 0x20);
    rm68120_write_cmd_data(0xD232, 0x0f);
    rm68120_write_cmd_data(0xD233, 0x8e);
    rm68120_write_cmd_data(0xD234, 0xff);

    //GAMMA SETING BLUE
    rm68120_write_cmd_data(0xD300, 0x00);
    rm68120_write_cmd_data(0xD301, 0x00);
    rm68120_write_cmd_data(0xD302, 0x1b);
    rm68120_write_cmd_data(0xD303, 0x44);
    rm68120_write_cmd_data(0xD304, 0x62);
    rm68120_write_cmd_data(0xD305, 0x00);
    rm68120_write_cmd_data(0xD306, 0x7b);
    rm68120_write_cmd_data(0xD307, 0xa1);
    rm68120_write_cmd_data(0xD308, 0xc0);
    rm68120_write_cmd_data(0xD309, 0xee);
    rm68120_write_cmd_data(0xD30A, 0x55);
    rm68120_write_cmd_data(0xD30B, 0x10);
    rm68120_write_cmd_data(0xD30C, 0x2c);
    rm68120_write_cmd_data(0xD30D, 0x43);
    rm68120_write_cmd_data(0xD30E, 0x57);
    rm68120_write_cmd_data(0xD30F, 0x55);
    rm68120_write_cmd_data(0xD310, 0x68);
    rm68120_write_cmd_data(0xD311, 0x78);
    rm68120_write_cmd_data(0xD312, 0x87);
    rm68120_write_cmd_data(0xD313, 0x94);
    rm68120_write_cmd_data(0xD314, 0x55);
    rm68120_write_cmd_data(0xD315, 0xa0);
    rm68120_write_cmd_data(0xD316, 0xac);
    rm68120_write_cmd_data(0xD317, 0xb6);
    rm68120_write_cmd_data(0xD318, 0xc1);
    rm68120_write_cmd_data(0xD319, 0x55);
    rm68120_write_cmd_data(0xD31A, 0xcb);
    rm68120_write_cmd_data(0xD31B, 0xcd);
    rm68120_write_cmd_data(0xD31C, 0xd6);
    rm68120_write_cmd_data(0xD31D, 0xdf);
    rm68120_write_cmd_data(0xD31E, 0x95);
    rm68120_write_cmd_data(0xD31F, 0xe8);
    rm68120_write_cmd_data(0xD320, 0xf1);
    rm68120_write_cmd_data(0xD321, 0xfa);
    rm68120_write_cmd_data(0xD322, 0x02);
    rm68120_write_cmd_data(0xD323, 0xaa);
    rm68120_write_cmd_data(0xD324, 0x0b);
    rm68120_write_cmd_data(0xD325, 0x13);
    rm68120_write_cmd_data(0xD326, 0x1d);
    rm68120_write_cmd_data(0xD327, 0x26);
    rm68120_write_cmd_data(0xD328, 0xaa);
    rm68120_write_cmd_data(0xD329, 0x30);
    rm68120_write_cmd_data(0xD32A, 0x3c);
    rm68120_write_cmd_data(0xD32B, 0x4A);
    rm68120_write_cmd_data(0xD32C, 0x63);
    rm68120_write_cmd_data(0xD32D, 0xea);
    rm68120_write_cmd_data(0xD32E, 0x79);
    rm68120_write_cmd_data(0xD32F, 0xa6);
    rm68120_write_cmd_data(0xD330, 0xd0);
    rm68120_write_cmd_data(0xD331, 0x20);
    rm68120_write_cmd_data(0xD332, 0x0f);
    rm68120_write_cmd_data(0xD333, 0x8e);
    rm68120_write_cmd_data(0xD334, 0xff);


    //GAMMA SETING  RED
    rm68120_write_cmd_data(0xD400, 0x00);
    rm68120_write_cmd_data(0xD401, 0x00);
    rm68120_write_cmd_data(0xD402, 0x1b);
    rm68120_write_cmd_data(0xD403, 0x44);
    rm68120_write_cmd_data(0xD404, 0x62);
    rm68120_write_cmd_data(0xD405, 0x00);
    rm68120_write_cmd_data(0xD406, 0x7b);
    rm68120_write_cmd_data(0xD407, 0xa1);
    rm68120_write_cmd_data(0xD408, 0xc0);
    rm68120_write_cmd_data(0xD409, 0xee);
    rm68120_write_cmd_data(0xD40A, 0x55);
    rm68120_write_cmd_data(0xD40B, 0x10);
    rm68120_write_cmd_data(0xD40C, 0x2c);
    rm68120_write_cmd_data(0xD40D, 0x43);
    rm68120_write_cmd_data(0xD40E, 0x57);
    rm68120_write_cmd_data(0xD40F, 0x55);
    rm68120_write_cmd_data(0xD410, 0x68);
    rm68120_write_cmd_data(0xD411, 0x78);
    rm68120_write_cmd_data(0xD412, 0x87);
    rm68120_write_cmd_data(0xD413, 0x94);
    rm68120_write_cmd_data(0xD414, 0x55);
    rm68120_write_cmd_data(0xD415, 0xa0);
    rm68120_write_cmd_data(0xD416, 0xac);
    rm68120_write_cmd_data(0xD417, 0xb6);
    rm68120_write_cmd_data(0xD418, 0xc1);
    rm68120_write_cmd_data(0xD419, 0x55);
    rm68120_write_cmd_data(0xD41A, 0xcb);
    rm68120_write_cmd_data(0xD41B, 0xcd);
    rm68120_write_cmd_data(0xD41C, 0xd6);
    rm68120_write_cmd_data(0xD41D, 0xdf);
    rm68120_write_cmd_data(0xD41E, 0x95);
    rm68120_write_cmd_data(0xD41F, 0xe8);
    rm68120_write_cmd_data(0xD420, 0xf1);
    rm68120_write_cmd_data(0xD421, 0xfa);
    rm68120_write_cmd_data(0xD422, 0x02);
    rm68120_write_cmd_data(0xD423, 0xaa);
    rm68120_write_cmd_data(0xD424, 0x0b);
    rm68120_write_cmd_data(0xD425, 0x13);
    rm68120_write_cmd_data(0xD426, 0x1d);
    rm68120_write_cmd_data(0xD427, 0x26);
    rm68120_write_cmd_data(0xD428, 0xaa);
    rm68120_write_cmd_data(0xD429, 0x30);
    rm68120_write_cmd_data(0xD42A, 0x3c);
    rm68120_write_cmd_data(0xD42B, 0x4A);
    rm68120_write_cmd_data(0xD42C, 0x63);
    rm68120_write_cmd_data(0xD42D, 0xea);
    rm68120_write_cmd_data(0xD42E, 0x79);
    rm68120_write_cmd_data(0xD42F, 0xa6);
    rm68120_write_cmd_data(0xD430, 0xd0);
    rm68120_write_cmd_data(0xD431, 0x20);
    rm68120_write_cmd_data(0xD432, 0x0f);
    rm68120_write_cmd_data(0xD433, 0x8e);
    rm68120_write_cmd_data(0xD434, 0xff);

    //GAMMA SETING GREEN
    rm68120_write_cmd_data(0xD500, 0x00);
    rm68120_write_cmd_data(0xD501, 0x00);
    rm68120_write_cmd_data(0xD502, 0x1b);
    rm68120_write_cmd_data(0xD503, 0x44);
    rm68120_write_cmd_data(0xD504, 0x62);
    rm68120_write_cmd_data(0xD505, 0x00);
    rm68120_write_cmd_data(0xD506, 0x7b);
    rm68120_write_cmd_data(0xD507, 0xa1);
    rm68120_write_cmd_data(0xD508, 0xc0);
    rm68120_write_cmd_data(0xD509, 0xee);
    rm68120_write_cmd_data(0xD50A, 0x55);
    rm68120_write_cmd_data(0xD50B, 0x10);
    rm68120_write_cmd_data(0xD50C, 0x2c);
    rm68120_write_cmd_data(0xD50D, 0x43);
    rm68120_write_cmd_data(0xD50E, 0x57);
    rm68120_write_cmd_data(0xD50F, 0x55);
    rm68120_write_cmd_data(0xD510, 0x68);
    rm68120_write_cmd_data(0xD511, 0x78);
    rm68120_write_cmd_data(0xD512, 0x87);
    rm68120_write_cmd_data(0xD513, 0x94);
    rm68120_write_cmd_data(0xD514, 0x55);
    rm68120_write_cmd_data(0xD515, 0xa0);
    rm68120_write_cmd_data(0xD516, 0xac);
    rm68120_write_cmd_data(0xD517, 0xb6);
    rm68120_write_cmd_data(0xD518, 0xc1);
    rm68120_write_cmd_data(0xD519, 0x55);
    rm68120_write_cmd_data(0xD51A, 0xcb);
    rm68120_write_cmd_data(0xD51B, 0xcd);
    rm68120_write_cmd_data(0xD51C, 0xd6);
    rm68120_write_cmd_data(0xD51D, 0xdf);
    rm68120_write_cmd_data(0xD51E, 0x95);
    rm68120_write_cmd_data(0xD51F, 0xe8);
    rm68120_write_cmd_data(0xD520, 0xf1);
    rm68120_write_cmd_data(0xD521, 0xfa);
    rm68120_write_cmd_data(0xD522, 0x02);
    rm68120_write_cmd_data(0xD523, 0xaa);
    rm68120_write_cmd_data(0xD524, 0x0b);
    rm68120_write_cmd_data(0xD525, 0x13);
    rm68120_write_cmd_data(0xD526, 0x1d);
    rm68120_write_cmd_data(0xD527, 0x26);
    rm68120_write_cmd_data(0xD528, 0xaa);
    rm68120_write_cmd_data(0xD529, 0x30);
    rm68120_write_cmd_data(0xD52A, 0x3c);
    rm68120_write_cmd_data(0xD52B, 0x4a);
    rm68120_write_cmd_data(0xD52C, 0x63);
    rm68120_write_cmd_data(0xD52D, 0xea);
    rm68120_write_cmd_data(0xD52E, 0x79);
    rm68120_write_cmd_data(0xD52F, 0xa6);
    rm68120_write_cmd_data(0xD530, 0xd0);
    rm68120_write_cmd_data(0xD531, 0x20);
    rm68120_write_cmd_data(0xD532, 0x0f);
    rm68120_write_cmd_data(0xD533, 0x8e);
    rm68120_write_cmd_data(0xD534, 0xff);

    //GAMMA SETING BLUE
    rm68120_write_cmd_data(0xD600, 0x00);
    rm68120_write_cmd_data(0xD601, 0x00);
    rm68120_write_cmd_data(0xD602, 0x1b);
    rm68120_write_cmd_data(0xD603, 0x44);
    rm68120_write_cmd_data(0xD604, 0x62);
    rm68120_write_cmd_data(0xD605, 0x00);
    rm68120_write_cmd_data(0xD606, 0x7b);
    rm68120_write_cmd_data(0xD607, 0xa1);
    rm68120_write_cmd_data(0xD608, 0xc0);
    rm68120_write_cmd_data(0xD609, 0xee);
    rm68120_write_cmd_data(0xD60A, 0x55);
    rm68120_write_cmd_data(0xD60B, 0x10);
    rm68120_write_cmd_data(0xD60C, 0x2c);
    rm68120_write_cmd_data(0xD60D, 0x43);
    rm68120_write_cmd_data(0xD60E, 0x57);
    rm68120_write_cmd_data(0xD60F, 0x55);
    rm68120_write_cmd_data(0xD610, 0x68);
    rm68120_write_cmd_data(0xD611, 0x78);
    rm68120_write_cmd_data(0xD612, 0x87);
    rm68120_write_cmd_data(0xD613, 0x94);
    rm68120_write_cmd_data(0xD614, 0x55);
    rm68120_write_cmd_data(0xD615, 0xa0);
    rm68120_write_cmd_data(0xD616, 0xac);
    rm68120_write_cmd_data(0xD617, 0xb6);
    rm68120_write_cmd_data(0xD618, 0xc1);
    rm68120_write_cmd_data(0xD619, 0x55);
    rm68120_write_cmd_data(0xD61A, 0xcb);
    rm68120_write_cmd_data(0xD61B, 0xcd);
    rm68120_write_cmd_data(0xD61C, 0xd6);
    rm68120_write_cmd_data(0xD61D, 0xdf);
    rm68120_write_cmd_data(0xD61E, 0x95);
    rm68120_write_cmd_data(0xD61F, 0xe8);
    rm68120_write_cmd_data(0xD620, 0xf1);
    rm68120_write_cmd_data(0xD621, 0xfa);
    rm68120_write_cmd_data(0xD622, 0x02);
    rm68120_write_cmd_data(0xD623, 0xaa);
    rm68120_write_cmd_data(0xD624, 0x0b);
    rm68120_write_cmd_data(0xD625, 0x13);
    rm68120_write_cmd_data(0xD626, 0x1d);
    rm68120_write_cmd_data(0xD627, 0x26);
    rm68120_write_cmd_data(0xD628, 0xaa);
    rm68120_write_cmd_data(0xD629, 0x30);
    rm68120_write_cmd_data(0xD62A, 0x3c);
    rm68120_write_cmd_data(0xD62B, 0x4A);
    rm68120_write_cmd_data(0xD62C, 0x63);
    rm68120_write_cmd_data(0xD62D, 0xea);
    rm68120_write_cmd_data(0xD62E, 0x79);
    rm68120_write_cmd_data(0xD62F, 0xa6);
    rm68120_write_cmd_data(0xD630, 0xd0);
    rm68120_write_cmd_data(0xD631, 0x20);
    rm68120_write_cmd_data(0xD632, 0x0f);
    rm68120_write_cmd_data(0xD633, 0x8e);
    rm68120_write_cmd_data(0xD634, 0xff);

    //AVDD VOLTAGE SETTING
    rm68120_write_cmd_data(0xB000, 0x05);
    rm68120_write_cmd_data(0xB001, 0x05);
    rm68120_write_cmd_data(0xB002, 0x05);
    //AVEE VOLTAGE SETTING
    rm68120_write_cmd_data(0xB100, 0x05);
    rm68120_write_cmd_data(0xB101, 0x05);
    rm68120_write_cmd_data(0xB102, 0x05);

    //AVDD Boosting
    rm68120_write_cmd_data(0xB600, 0x34);
    rm68120_write_cmd_data(0xB601, 0x34);
    rm68120_write_cmd_data(0xB603, 0x34);
    //AVEE Boosting
    rm68120_write_cmd_data(0xB700, 0x24);
    rm68120_write_cmd_data(0xB701, 0x24);
    rm68120_write_cmd_data(0xB702, 0x24);
    //VCL Boosting
    rm68120_write_cmd_data(0xB800, 0x24);
    rm68120_write_cmd_data(0xB801, 0x24);
    rm68120_write_cmd_data(0xB802, 0x24);
    //VGLX VOLTAGE SETTING
    rm68120_write_cmd_data(0xBA00, 0x14);
    rm68120_write_cmd_data(0xBA01, 0x14);
    rm68120_write_cmd_data(0xBA02, 0x14);
    //VCL Boosting
    rm68120_write_cmd_data(0xB900, 0x24);
    rm68120_write_cmd_data(0xB901, 0x24);
    rm68120_write_cmd_data(0xB902, 0x24);
    //Gamma Voltage
    rm68120_write_cmd_data(0xBc00, 0x00);
    rm68120_write_cmd_data(0xBc01, 0xa0);//vgmp: REVERTIDO a 0xa0. 0xC0 es el valor "oficial"
                                          //confirmado del firmware real (verificado dos veces),
                                          //pero desestabiliza este panel fisico concreto -
                                          //posible variacion de lote entre paneles.
    rm68120_write_cmd_data(0xBc02, 0x00);
    rm68120_write_cmd_data(0xBd00, 0x00);
    rm68120_write_cmd_data(0xBd01, 0xa0);//vgmn: REVERTIDO a 0xa0, mismo motivo que vgmp
    rm68120_write_cmd_data(0xBd02, 0x00);
    //VCOM Setting
    rm68120_write_cmd_data(0xBe01, RM68120_VCOM_VALUE);//3

    //ENABLE PAGE 0
    rm68120_write_cmd_data(0xF000, 0x55);
    rm68120_write_cmd_data(0xF001, 0xAA);
    rm68120_write_cmd_data(0xF002, 0x52);
    rm68120_write_cmd_data(0xF003, 0x08);
    rm68120_write_cmd_data(0xF004, 0x00);

    /* Secuencia completa restaurada. Ahora con delay_ms() real (SysTick)
     * en vez del bucle sin calibrar - primera prueba a hacer antes de
     * seguir troceando registros. */
    //Vivid Color Function Control
    rm68120_write_cmd_data(0xB400, 0x10);
    //Z-INVERSION
    rm68120_write_cmd_data(0xBC00, 0x05);
    rm68120_write_cmd_data(0xBC01, 0x05);
    rm68120_write_cmd_data(0xBC02, 0x05);

    //*************** add on 20111021**********************//
    rm68120_write_cmd_data(0xB700, 0x22);//GATE EQ CONTROL
    rm68120_write_cmd_data(0xB701, 0x22);//GATE EQ CONTROL
    rm68120_write_cmd_data(0xC80B, 0x2A);//DISPLAY TIMING CONTROL
    rm68120_write_cmd_data(0xC80C, 0x2A);//DISPLAY TIMING CONTROL
    rm68120_write_cmd_data(0xC80F, 0x2A);//DISPLAY TIMING CONTROL
    rm68120_write_cmd_data(0xC810, 0x2A);//DISPLAY TIMING CONTROL
    //*************** add on 20111021**********************//
    //PWM_ENH_OE =1
    rm68120_write_cmd_data(0xd000, 0x01);
    //DM_SEL =1
    rm68120_write_cmd_data(0xb300, 0x10);
    //VBPDA=07h
    rm68120_write_cmd_data(0xBd02, 0x07);
    //VBPDb=07h
    rm68120_write_cmd_data(0xBe02, 0x07);
    //VBPDc=07h
    rm68120_write_cmd_data(0xBf02, 0x07);
    //ENABLE PAGE 2
    rm68120_write_cmd_data(0xF000, 0x55);
    rm68120_write_cmd_data(0xF001, 0xAA);
    rm68120_write_cmd_data(0xF002, 0x52);
    rm68120_write_cmd_data(0xF003, 0x08);
    rm68120_write_cmd_data(0xF004, 0x02);
    //SDREG0 =0
    rm68120_write_cmd_data(0xc301, 0xa9);
    //DS=14
    rm68120_write_cmd_data(0xfe01, 0x94);
    //OSC =60h
    rm68120_write_cmd_data(0xf600, 0x60);
    //TE ON
    /* DESCARTADO: el usuario probo comentando esto en hardware real y
     * el apagado persiste igual. No es la causa.
     * rm68120_write_cmd_data(0x3500, 0x00); */
    //SLEEP OUT 
    rm68120_write_cmd(0x1100);
    delay_ms(100);
    //DISPLY ON
    rm68120_write_cmd(0x2900);
    delay_ms(100);

    rm68120_write_cmd_data(0x3A00, 0x55);
    rm68120_write_cmd_data(0x3600, RM68120_MADCTL_VALUE); /* MADCTL, ver comentario en el .h para el barrido de valores */
}
#endif /* !RM68120_USE_NT35510_SEQUENCE */

/*
 * ============================================================
 * SECUENCIA ALTERNATIVA - estilo NT35510, confirmada funcionando en
 * OTRO proyecto pero con ESTA MISMA placa y panel fisico (Jorge,
 * 27/07/2026). Traducida mecanicamente de WriteComm(cmd)/WriteData(dat)
 * a rm68120_write_cmd_data(cmd, dat), sin alterar ningun valor.
 *
 * AVISO IMPORTANTE: el fichero original tenia un comentario propio
 * admitiendo que las tablas de gamma D2-D6 estaban recortadas/ausentes
 * ("D2-D6 (сокращенно)... pero DEBERIAN ESTAR"). Aqui solo esta D1. Si
 * la imagen se ve pero con colores/contraste raros, esto es lo primero
 * a sospechar - anadir D2-D6 desde la secuencia de Espressif de arriba
 * (rm68120_panel_init_sequence) es una fuente razonable si hace falta.
 *
 * Direccion de columnas/filas (2A02=0x03,2A03=0x1F -> 0x031F=799;
 * 2B02=0x01,2B03=0xDF -> 0x01DF=479) coincide EXACTAMENTE con
 * GFX_SCREEN_WIDTH=800/HEIGHT=480 ya confirmados en hardware - buena
 * señal de que es el mismo panel fisico.
 *
 * MADCTL=0x60 aqui es un valor fijo de esta secuencia concreta, no usa
 * RM68120_MADCTL_VALUE - nuestra nota anterior en el .h dice que 0x60
 * "nunca mostro nada en frio", pero eso se probo SIN el resto de este
 * acompañamiento (VGMP/VGSP/VCOM/EQ/etc.) - con la secuencia completa
 * puede comportarse distinto. Si funciona, plantearse fijar
 * RM68120_MADCTL_VALUE a 0x60 y quitar este hardcodeo.
 */
static void rm68120_panel_init_sequence_nt35510(void)
{
    /* Entrada en modo de usuario / unlock */
    rm68120_write_cmd_data(0xF000, 0x55);
    rm68120_write_cmd_data(0xF001, 0xAA);
    rm68120_write_cmd_data(0xF002, 0x52);
    rm68120_write_cmd_data(0xF003, 0x08);
    rm68120_write_cmd_data(0xF004, 0x01);

    /* VGMP/VGSP */
    rm68120_write_cmd_data(0xBC01, 0x86);
    rm68120_write_cmd_data(0xBC02, 0x6A);

    /* VGMN/VGSN */
    rm68120_write_cmd_data(0xBD01, 0x86);
    rm68120_write_cmd_data(0xBD02, 0x6A);

    /* VCOM */
    rm68120_write_cmd_data(0xBE01, 0x67);

    /* Gamma D1 (RED) - D2-D6 AUSENTES en la fuente original, ver aviso arriba */
    rm68120_write_cmd_data(0xD100, 0x00);
    rm68120_write_cmd_data(0xD101, 0x5D);
    rm68120_write_cmd_data(0xD102, 0x00);
    rm68120_write_cmd_data(0xD103, 0x6B);
    rm68120_write_cmd_data(0xD104, 0x00);
    rm68120_write_cmd_data(0xD105, 0x84);
    rm68120_write_cmd_data(0xD106, 0x00);
    rm68120_write_cmd_data(0xD107, 0x9C);
    rm68120_write_cmd_data(0xD108, 0x00);
    rm68120_write_cmd_data(0xD109, 0xB1);
    rm68120_write_cmd_data(0xD10A, 0x00);
    rm68120_write_cmd_data(0xD10B, 0xD9);
    rm68120_write_cmd_data(0xD10C, 0x00);
    rm68120_write_cmd_data(0xD10D, 0xFD);
    rm68120_write_cmd_data(0xD10E, 0x01);
    rm68120_write_cmd_data(0xD10F, 0x38);
    rm68120_write_cmd_data(0xD110, 0x01);
    rm68120_write_cmd_data(0xD111, 0x68);
    rm68120_write_cmd_data(0xD112, 0x01);
    rm68120_write_cmd_data(0xD113, 0xB9);
    rm68120_write_cmd_data(0xD114, 0x01);
    rm68120_write_cmd_data(0xD115, 0xFB);
    rm68120_write_cmd_data(0xD116, 0x02);
    rm68120_write_cmd_data(0xD117, 0x63);
    rm68120_write_cmd_data(0xD118, 0x02);
    rm68120_write_cmd_data(0xD119, 0xB9);
    rm68120_write_cmd_data(0xD11A, 0x02);
    rm68120_write_cmd_data(0xD11B, 0xBB);
    rm68120_write_cmd_data(0xD11C, 0x03);
    rm68120_write_cmd_data(0xD11D, 0x03);
    rm68120_write_cmd_data(0xD11E, 0x03);
    rm68120_write_cmd_data(0xD11F, 0x46);
    rm68120_write_cmd_data(0xD120, 0x03);
    rm68120_write_cmd_data(0xD121, 0x69);
    rm68120_write_cmd_data(0xD122, 0x03);
    rm68120_write_cmd_data(0xD123, 0x8F);
    rm68120_write_cmd_data(0xD124, 0x03);
    rm68120_write_cmd_data(0xD125, 0xA4);
    rm68120_write_cmd_data(0xD126, 0x03);
    rm68120_write_cmd_data(0xD127, 0xB9);
    rm68120_write_cmd_data(0xD128, 0x03);
    rm68120_write_cmd_data(0xD129, 0xC7);
    rm68120_write_cmd_data(0xD12A, 0x03);
    rm68120_write_cmd_data(0xD12B, 0xC9);
    rm68120_write_cmd_data(0xD12C, 0x03);
    rm68120_write_cmd_data(0xD12D, 0xCB);
    rm68120_write_cmd_data(0xD12E, 0x03);
    rm68120_write_cmd_data(0xD12F, 0xCB);
    rm68120_write_cmd_data(0xD130, 0x03);
    rm68120_write_cmd_data(0xD131, 0xCB);
    rm68120_write_cmd_data(0xD132, 0x03);
    rm68120_write_cmd_data(0xD133, 0xCC);

    /* Ajustes adicionales */
    rm68120_write_cmd_data(0xBA00, 0x24);
    rm68120_write_cmd_data(0xBA01, 0x24);
    rm68120_write_cmd_data(0xBA02, 0x24);

    rm68120_write_cmd_data(0xB900, 0x24);
    rm68120_write_cmd_data(0xB901, 0x24);
    rm68120_write_cmd_data(0xB902, 0x24);

    /* Pagina LV2 */
    rm68120_write_cmd_data(0xF000, 0x55);
    rm68120_write_cmd_data(0xF001, 0xAA);
    rm68120_write_cmd_data(0xF002, 0x52);
    rm68120_write_cmd_data(0xF003, 0x08);
    rm68120_write_cmd_data(0xF004, 0x00);

    /* Display control */
    rm68120_write_cmd_data(0xB100, 0xCC);

    /* Inversion mode */
    rm68120_write_cmd_data(0xBC00, 0x05);
    rm68120_write_cmd_data(0xBC01, 0x05);
    rm68120_write_cmd_data(0xBC02, 0x05);

    /* Source EQ control */
    rm68120_write_cmd_data(0xB800, 0x01);
    rm68120_write_cmd_data(0xB801, 0x03);
    rm68120_write_cmd_data(0xB802, 0x03);
    rm68120_write_cmd_data(0xB803, 0x03);

    /* VGMN/VGSN parte 2 */
    rm68120_write_cmd_data(0xBD02, 0x07);
    rm68120_write_cmd_data(0xBD03, 0x31);

    /* VCOM parte 2 */
    rm68120_write_cmd_data(0xBE02, 0x07);
    rm68120_write_cmd_data(0xBE03, 0x31);

    /* VGH */
    rm68120_write_cmd_data(0xBF02, 0x07);
    rm68120_write_cmd_data(0xBF03, 0x31);

    rm68120_write_cmd_data(0xFF00, 0xAA);
    rm68120_write_cmd_data(0xFF01, 0x55);
    rm68120_write_cmd_data(0xFF02, 0x25);
    rm68120_write_cmd_data(0xFF03, 0x01);

    rm68120_write_cmd_data(0xF304, 0x11);
    rm68120_write_cmd_data(0xF306, 0x10);
    rm68120_write_cmd_data(0xF308, 0x00);

    /* Tearing effect mode */
    rm68120_write_cmd_data(0x3500, 0x00);

    /* COLMOD: 16bit por pixel */
    rm68120_write_cmd_data(0x3A00, 0x55);

    /* MADCTL - valor fijo de esta secuencia (ver aviso arriba) */
    rm68120_write_cmd_data(0x3600, 0x60);

    /* Direccion de columnas: 0 a 799 */
    rm68120_write_cmd_data(0x2A00, 0x00);
    rm68120_write_cmd_data(0x2A01, 0x00);
    rm68120_write_cmd_data(0x2A02, 0x03);
    rm68120_write_cmd_data(0x2A03, 0x1F);

    /* Direccion de filas: 0 a 479 */
    rm68120_write_cmd_data(0x2B00, 0x00);
    rm68120_write_cmd_data(0x2B01, 0x00);
    rm68120_write_cmd_data(0x2B02, 0x01);
    rm68120_write_cmd_data(0x2B03, 0xDF);

    /* Sleep out */
    rm68120_write_cmd(0x1100);
    delay_ms(50);

    /* Display on */
    rm68120_write_cmd(0x2900);
    delay_ms(50);
}

void rm68120_init(void)
{
    rm68120_exmc_gpio_init();
    rm68120_exmc_bus_init();
    rm68120_hw_reset();

    /* Replica el chequeo de deteccion de panel del firmware original:
     * lee comando 0x000A (compara con 8) y comando 0x3A00 (compara con
     * 0x77) para decidir la rama de inicializacion. Nosotros solo
     * guardamos el resultado para inspeccionarlo por depurador; seguimos
     * usando siempre la misma secuencia (la rama "por defecto"), pero
     * esto nos dice si esa es realmente la rama que el panel activaria. */
    g_panel_id_check1 = rm68120_read_status(0x000A);
    g_panel_id_check2 = rm68120_read_status(0x3A00);

#if RM68120_USE_NT35510_SEQUENCE
    rm68120_panel_init_sequence_nt35510();
#else
    rm68120_panel_init_sequence();
#endif
}

/*
 * Lee un registro de "solo lectura" documentado oficialmente en el
 * datasheet publico del RM68120 (a diferencia de los registros
 * 0xB0-0xBF de gamma/power, que NO estan documentados). Utilidad
 * tipica: RDDPM (0x0A00) = Read Display Power Mode, RDDSDR (0x0F00) =
 * Read Display Self-Diagnostic Result.
 *
 * Segun el datasheet: "first read out data is invalid. Normal data is
 * read out from 2nd read out data." Por eso descartamos la primera
 * lectura y devolvemos la segunda.
 */
uint16_t rm68120_read_status(uint16_t cmd)
{
    uint16_t dummy;
    uint16_t value;

    rm68120_write_cmd(cmd);
    dummy = rm68120_read_data();  /* primera lectura, invalida, descartada */
    value = rm68120_read_data();  /* segunda lectura, valida */
    (void)dummy;

    return value;
}
