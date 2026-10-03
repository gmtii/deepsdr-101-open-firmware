#include "lcd_dma.h"
#include "rm68120_exmc.h"
#include "gd32f4xx.h"

/* El canal. Cualquiera del DMA1 vale: el DMA1 entero estaba sin usar. */
#define LCD_DMA_PERIF   DMA1
#define LCD_DMA_CANAL   DMA_CH0

/* Tope de espera. Empujar una banda entera son 756*24 = 18.144 pixeles a
 * unos 45 ns, o sea 0,8 ms; a 200 MHz eso son 163.000 ciclos. Un millon de
 * vueltas es mas de diez veces eso: esta para que un DMA colgado no cuelgue
 * el dibujo, no para medir nada. En este proyecto no se pone trabajo sin
 * tope en un camino de interfaz. */
#define LCD_DMA_TOPE    1000000UL

/*
 * ENCENDIDO DE FABRICA. Se puede poner a 0 desde la linea de compilacion
 * (-DLCD_DMA_ON_DEF=0) para generar un binario gemelo con todo lo demas
 * igual pero sin DMA.
 *
 * Existe por una razon concreta: si el DMA saliera mal en la radio, lo que
 * se veria es la pantalla escrita de cualquier manera, y desde ahi no se
 * puede llegar al interruptor de la ventana de informacion para apagarlo.
 * Un binario de vuelta, que conserve todos los demas arreglos, vale mas que
 * una explicacion.
 */
#ifndef LCD_DMA_ON_DEF
#define LCD_DMA_ON_DEF 1U
#endif

/* La SRAM principal. El DMA es un maestro del bus AHB y NO ve el TCM
 * (0x10000000), igual que el IPA - ver ipa_blit.c. */
#define SRAM_INI  0x20000000UL
#define SRAM_FIN  0x20030000UL

static uint8_t  s_vale;      /* la autoprueba salio bien */
static uint8_t  s_on;        /* encendido por el usuario */
static uint8_t  s_enmarcha;  /* hay un envio sin recoger */
static uint16_t s_fallos;

/* Los dos buffers de la autoprueba. Pequenos y en .bss, no en la pila. */
static uint16_t s_prueba_ori[8];
static uint16_t s_prueba_des;

static uint8_t en_sram(const void *p, uint32_t bytes)
{
    uint32_t a = (uint32_t)p;
    return (uint8_t)((a >= SRAM_INI) && ((a + bytes) <= SRAM_FIN));
}

/* Configura el canal para leer de `origen` (avanzando) y escribir en
 * `destino` (fijo), n palabras de 16 bits, y lo arranca. */
static void arranca(uint32_t origen, uint32_t destino, uint32_t n)
{
    dma_single_data_parameter_struct c;

    dma_deinit(LCD_DMA_PERIF, LCD_DMA_CANAL);
    dma_single_data_para_struct_init(&c);

    /* En memoria-a-memoria el puerto "periferico" es el ORIGEN y el puerto
     * "memoria" es el DESTINO. Se lee asi de raro, pero es lo que dice el
     * manual y es lo que la autoprueba comprueba. */
    c.periph_addr         = origen;
    c.periph_inc          = DMA_PERIPH_INCREASE_ENABLE;   /* el buffer avanza */
    c.memory0_addr        = destino;
    c.memory_inc          = DMA_MEMORY_INCREASE_DISABLE;  /* el panel, fijo */
    c.periph_memory_width = DMA_PERIPH_WIDTH_16BIT;
    c.circular_mode       = DMA_CIRCULAR_MODE_DISABLE;
    c.direction           = DMA_MEMORY_TO_MEMORY;
    c.number              = n;
    /* BAJA, no alta. La puse alta sin pensarlo y esta mal: el audio y la
     * captura de radiofrecuencia van a ULTRA_HIGH en el DMA0 porque no
     * pueden esperar, y la pantalla si puede - un fotograma tarde no lo
     * nota nadie, una muestra de audio tarde si. Dentro del DMA1 no compite
     * con nadie (esta el solo), asi que esto no cuesta nada; pero el campo
     * dice lo que este trabajo vale, y vale poco. */
    c.priority            = DMA_PRIORITY_LOW;
    dma_single_data_mode_init(LCD_DMA_PERIF, LCD_DMA_CANAL, &c);

    dma_flag_clear(LCD_DMA_PERIF, LCD_DMA_CANAL, DMA_FLAG_FTF);
    dma_channel_enable(LCD_DMA_PERIF, LCD_DMA_CANAL);
}

static uint8_t espera_fin(void)
{
    uint32_t v;

    for (v = 0UL; v < LCD_DMA_TOPE; v++) {
        if (dma_flag_get(LCD_DMA_PERIF, LCD_DMA_CANAL, DMA_FLAG_FTF) != RESET) {
            dma_flag_clear(LCD_DMA_PERIF, LCD_DMA_CANAL, DMA_FLAG_FTF);
            dma_channel_disable(LCD_DMA_PERIF, LCD_DMA_CANAL);
            return 1U;
        }
    }
    dma_channel_disable(LCD_DMA_PERIF, LCD_DMA_CANAL);
    return 0U;
}

void lcd_dma_init(void)
{
    uint16_t i;

    s_vale = 0U; s_on = 0U; s_enmarcha = 0U; s_fallos = 0U;

    rcu_periph_clock_enable(RCU_DMA1);

    /* La autoprueba: la MISMA configuracion que se va a usar de verdad -
     * destino fijo, 16 bits, memoria a memoria- pero contra un uint16_t de
     * RAM en vez de contra el panel. Si el destino de verdad se queda
     * quieto, al acabar tiene que valer lo ULTIMO del origen. Si el
     * incremento del destino estuviera al reves, valdria lo PRIMERO, que es
     * justo el error que se quiere cazar antes de tocar la pantalla. */
    for (i = 0U; i < 8U; i++) {
        s_prueba_ori[i] = (uint16_t)(0x1000U + i);
    }
    s_prueba_des = 0U;

    arranca((uint32_t)&s_prueba_ori[0], (uint32_t)&s_prueba_des, 8U);
    if (!espera_fin()) {
        return;                       /* ni siquiera termina: fuera */
    }
    if (s_prueba_des != s_prueba_ori[7]) {
        return;                       /* termina pero no hace lo que se pide */
    }

    s_vale = 1U;
    s_on   = LCD_DMA_ON_DEF;
}

uint8_t lcd_dma_hay(void)
{
    return (uint8_t)(s_vale && s_on);
}

void lcd_dma_pon(uint8_t on)
{
    if (s_enmarcha) { (void)lcd_dma_espera(); }
    s_on = (uint8_t)(on ? 1U : 0U);
}

uint16_t lcd_dma_fallos(void)
{
    return s_fallos;
}

uint8_t lcd_dma_manda(const uint16_t *px, uint32_t n)
{
    if (!s_vale || !s_on || s_enmarcha) {
        return 0U;
    }
    /* El contador del DMA es de 16 bits: 65535 transferencias como mucho. */
    if ((n == 0UL) || (n > 65535UL)) {
        return 0U;
    }
    if (!en_sram(px, n * 2UL)) {
        s_fallos++;
        return 0U;
    }
    arranca((uint32_t)px, LCD_DAT_ADDR, n);
    s_enmarcha = 1U;
    return 1U;
}

uint8_t lcd_dma_espera(void)
{
    if (!s_enmarcha) {
        return 1U;
    }
    s_enmarcha = 0U;
    if (espera_fin()) {
        return 1U;
    }
    /* Un DMA que no termina no va a empezar a terminar, y dejarlo encendido
     * seria colgar el dibujo una vez por banda. Se apaga solo y el resto del
     * firmware sigue por el camino de la CPU. */
    s_fallos++;
    s_vale = 0U;
    return 0U;
}
