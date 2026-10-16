#ifndef RM68120_EXMC_H
#define RM68120_EXMC_H

#include "gd32f4xx.h"

/*
 * Pinout verificado por multimetro sobre PCB (GD32F450VET6, LQFP100):
 *   RESET = PC9
 *   CS    = PD7   (EXMC_NE0)
 *   RS/DC = PD11  (EXMC_A16)
 *   WR    = PD5   (EXMC_NWE)
 *   RD    = PD4   (EXMC_NOE)
 *   D0    = PD14      D1  = PD15
 *   D2    = PD0        D3  = PD1
 *   D4    = PE7        D5  = PE8
 *   D6    = PE9        D7  = PE10
 *   D8    = PE11       D9  = PE12
 *   D10   = PE13       D11 = PE14
 *   D12   = PE15       D13 = PD8
 *   D14   = PD9        D15 = PD10
 *
 * Banco EXMC usado: Bank0 NOR/SRAM Region0 (NE0) -> base 0x60000000
 * RS mapeado a A16 -> offset de datos = 1 << (16+1) = 0x20000
 * (formula estandar para bus de 16 bits no multiplexado: el pin de
 * direccion externo A[n] corresponde a HADDR[n+1]).
 */

#define LCD_REG_ADDR   0x60000000UL   /* RS bajo  -> registro de comando */
#define LCD_DAT_ADDR   0x60020000UL   /* RS alto  -> registro de datos   */

#define LCD_REG (*(volatile uint16_t *)LCD_REG_ADDR)
#define LCD_DAT (*(volatile uint16_t *)LCD_DAT_ADDR)

#define LCD_RST_PORT   GPIOC
#define LCD_RST_PIN    GPIO_PIN_7  /* CORREGIDO a PC7 (Jorge, 27/07/2026) tras revisar de nuevo.
                                      OJO: hay una nota anterior en este mismo fichero que decia
                                      "PC7 no arreglo el problema, volvemos a PC9 confirmado por
                                      multimetro" - esa comprobacion pudo ser prematura (probar
                                      solo el pin sin la secuencia de init completa que ahora
                                      tenemos). Si el panel deja de reaccionar del todo (ni la
                                      demo de gfx.c que no usa rm68120_init funciona), revisar
                                      esta contradiccion primero: podria significar que ninguno
                                      de los dos esta bien, o que el multimetro midio otra cosa. */

/*
 * Valor de VCOM (registro 0xBE01 del panel). Confirmado por extraccion
 * directa del firmware original de fabrica (update4.bin) mediante
 * desensamblado: el propio firmware que sabemos que funciona usa
 * exactamente 0x3D. Anteriormente probamos 0x0A "a ciegas" y parecio
 * arreglar el caso de area pequeña, pero dado que el firmware real
 * usa 0x3D, sospechamos que 0x0A funcionaba por otra razon casual, no
 * porque fuera el VCOM correcto.
 */
#define RM68120_VCOM_VALUE  0x0A /* PRUEBA: volviendo al valor empiricamente estable.
                                     0x3D es el "oficial" (confirmado del firmware real),
                                     pero desde que lo pusimos, hasta el cuadrado pequeño
                                     (antes estable) volvio a fallar. Sospecha: tu panel
                                     fisico concreto necesita un VCOM distinto al de
                                     update4.bin, aunque sea "el mismo modelo" de fabrica. */

/*
 * MADCTL (Memory Access Control, registro 0x3600) - a diferencia de
 * los registros de gamma/power, ESTE SI esta documentado y es
 * estandar (bits MY/MX/MV/RGB-BGR). Confirmado por foto real: con nuestro
 * valor actual, un cuadrado que pedimos en la esquina superior
 * izquierda aparece en la esquina INFERIOR DERECHA -> mapeo rotado
 * 180 grados. Prueba estos valores hasta que (0,0) caiga arriba a la
 * izquierda como es de esperar:
 *   0x00, 0x40, 0x80, 0xC0, 0x20, 0x60, 0xA0, 0xE0
 * (si los colores salen con rojo/azul intercambiados, prueba tambien
 * sumando/restando 0x08 al valor que funcione, es el bit RGB/BGR)
 */
#define RM68120_MADCTL_VALUE 0xA3 /* REVERTIDO: 0x60+apaisado no muestra nada nunca en frio,
                                      ni siquiera con VGMP/VGMN y Software Reset ya corregidos.
                                      0xA3+vertical es la unica config que muestra imagen en
                                      frio real (con el fade pendiente de resolver). */

/*
 * Elige que secuencia de inicializacion usa rm68120_init():
 *   1 = la traducida de un proyecto NT35510 (Jorge, 27/07/2026),
 *       confirmada funcionando en OTRO proyecto pero con esta MISMA
 *       placa y panel fisico - la probamos primero por eso.
 *   0 = la original de Espressif (mas completa en gamma, pero con
 *       problemas de imagen sin resolver documentados en este fichero).
 */
#define RM68120_USE_NT35510_SEQUENCE 1

void rm68120_exmc_gpio_init(void);
void rm68120_exmc_bus_init(void);
void rm68120_hw_reset(void);

void rm68120_write_cmd(uint16_t cmd);
void rm68120_write_data(uint16_t data);
void rm68120_write_cmd_data(uint16_t cmd, uint16_t data);
uint16_t rm68120_read_data(void);

void rm68120_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void rm68120_fill_screen(uint16_t color_rgb565);
uint16_t rm68120_read_status(uint16_t cmd);

/* Resultado del chequeo de deteccion de panel que hace el firmware
 * original (comando 0x000A y 0x3A00, comparados con 8 y 0x77 para
 * decidir que secuencia de inicializacion usar). Definidas en main.c,
 * rellenadas por rm68120_init(). */
extern volatile uint16_t g_panel_id_check1; /* respuesta a comando 0x000A */
extern volatile uint16_t g_panel_id_check2; /* respuesta a comando 0x3A00 */

void rm68120_init(void);

/* ---------------------------------------------------------------------
 * ESCRITURA DE UN PIXEL, SIN LLAMADA. 24/09/2026.
 * ---------------------------------------------------------------------
 * rm68120_write_data() es una funcion de verdad, con su salto y su retorno:
 *
 *     0803ab9c <rm68120_write_data>:
 *         ldr   r3, [pc, #4]    @ 0x60020000
 *         strh  r0, [r3, #0]
 *         bx    lr
 *
 * En un bucle que escribe pixel a pixel eso son unos 10 ciclos de tramite
 * -salto, retorno y recarga de la tuberia de instrucciones- por CADA pixel.
 * Volcar la cascada entera son 54.432 pixeles: unos 2,7 ms tirados solo en
 * llamar y volver, encima de los 5,4 ms que cuesta el bus.
 *
 * Se midio en la radio: 14,9 ms por volcado, cuando la cuenta del bus sola
 * daba 5,4. Esta era una de las dos mitades que faltaban.
 *
 * Esto es lo mismo pero incorporado en quien lo llama: una instruccion.
 * rm68120_write_data() se queda tal cual para todo lo demas - no cambia
 * nada, y sustituirla en sitios donde se escribe un pixel suelto no
 * ganaria nada y ensuciaria el codigo.
 */
/* Ciclos acumulados dentro de gfx_blit(), o sea: cuanto del dibujo se va en
 * EMPUJAR pixeles al panel, separado de cuanto se va en DECIDIR que pixel va
 * en cada sitio. Sin esta separacion, "el espectro cuesta 65 ms" no dice
 * donde hay que trabajar, y las dos mitades se arreglan de maneras
 * completamente distintas: una con el bus o con un DMA, la otra con el
 * propio bucle. Cuesta dos lecturas de un contador por fila. */
extern volatile uint32_t g_lcd_ciclos;

/* Funcion de verdad y no una linea metida aqui: esta cabecera la incluye
 * tambien el simulador del PC (a traves de gfx.h), donde DWT no existe. La
 * definicion vive en el .c de cada lado. Dos llamadas por fila volcada no
 * cuestan nada; ponerlas por pixel si, y por eso rm68120_dato() SI esta
 * aqui dentro. */
uint32_t rm68120_ciclo(void);

static inline void rm68120_dato(uint16_t v)
{
    LCD_DAT = v;
}

/* Prueba del desplazamiento por hardware del panel. Ver el comentario gordo
 * en el .c: dice si el RM68120 sabe rodar su propia memoria, y en que
 * direccion lo hace con el panel girado como lo tenemos. Apagar y encender
 * lo deshace pase lo que pase. */
void rm68120_scroll_prueba(uint16_t origen);

/* Tiempo de dato del bus de la pantalla, en ciclos de HCLK (1..15). Ver el
 * comentario gordo en el .c: es el numero que decide cuanto cuesta volcar la
 * cascada, y se ajusta mirando la pantalla, no calculandolo. */
uint16_t rm68120_bus_dato(void);
uint16_t rm68120_bus_dato_arranque(void);
uint16_t rm68120_bus_ns(void);
void     rm68120_bus_dato_pon(uint16_t dato);

#endif /* RM68120_EXMC_H */
