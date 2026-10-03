#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>

/*
 * Driver de tactil resistivo XPT2046 (compatible ADS7843/TSC2046) por
 * SPI bit-banged (GPIO a mano, sin periferico SPI hardware) - eleccion
 * deliberada: el touch no necesita velocidad, y asi nos ahorramos tener
 * que confirmar contra el datasheet del GD32F450 que AF exacto
 * corresponde a SPI1/SPI3 en PB3/4/5 (no hay conflicto de pines con el
 * bus EXMC del panel, comprobado: EXMC solo usa PD0,1,4,5,7,8,9,10,11,14,15
 * y PE7-15).
 *
 * Pines confirmados en el hardware real (Jorge, 27/07/2026):
 *   T_CLK   (DCLK)         PB3  - salida, reloj SPI hacia el XPT2046
 *   T_DOUT  (MISO)         PB4  - entrada, datos del XPT2046 hacia el MCU
 *   T_DIN   (MOSI)         PB5  - salida, datos del MCU hacia el XPT2046
 *   T_CS                   PD6  - salida, activo a nivel bajo
 *   BUSY                   PD3  - entrada, no se usa activamente todavia
 *                                 (el bit-bang controla el timing a mano,
 *                                 no hace falta esperar a BUSY como en un
 *                                 driver por SPI+DMA), pero se configura
 *                                 como entrada por si hace falta mas adelante.
 *   PENIRQ                 PD2  - entrada, activo a nivel bajo mientras
 *                                 hay contacto. Configurado con EXTI en
 *                                 flanco de bajada: la ISR SOLO levanta un
 *                                 flag (ninguna lectura SPI dentro de la
 *                                 ISR), consistente con el resto del
 *                                 proyecto (nada de trabajo pesado en
 *                                 manejadores de interrupcion).
 *
 * IMPORTANTE - PENDIENTE DE VALIDAR EN HARDWARE: la alineacion de bits
 * del protocolo XPT2046 (touch_read_raw) sigue el protocolo estandar
 * documentado (comando de 8 bits + 16 clocks de lectura, valor de 12
 * bits alineado a la izquierda en esos 16), pero no se ha podido probar
 * en el osciloscopio/hardware real todavia. Usar touch_debug_raw() para
 * volcar por UART los valores en crudo la primera vez que se pruebe, y
 * confirmar que responden de forma coherente al tocar las esquinas antes
 * de fiarse de touch_read()/la calibracion.
 */

/* Llamar una vez al arrancar: configura GPIO (bit-bang + CS + BUSY) y el
 * EXTI de PENIRQ (PD2 / EXTI2, flanco de bajada). */
void touch_init(void);

/* Lectura barata: solo mira el nivel del pin PENIRQ (sin transaccion
 * SPI). 1 = panel tocado en este instante, 0 = no. Usar para decidir si
 * merece la pena hacer touch_read_raw()/touch_read() (que si tienen
 * coste, varias transacciones SPI con promediado). */
uint8_t touch_is_pressed(void);

/* 1 si la ISR de PENIRQ ha marcado un flanco de bajada desde la ultima
 * vez que se llamo a esta funcion (y lo limpia al leerlo - "consume" el
 * evento). Pensado para que el bucle principal (o quien invoque el
 * dispatcher de touch) sepa que ha habido un toque nuevo sin tener que
 * hacer polling constante de touch_is_pressed(). No sustituye a
 * touch_is_pressed(): mientras el dedo sigue apoyado no hay mas flancos,
 * asi que para saber "sigue tocado" hay que seguir consultando el nivel. */
uint8_t touch_irq_pending(void);

/* Diagnostico: contador crudo de ejecuciones reales de EXTI2_IRQHandler
 * desde el arranque (no consume nada, a diferencia de touch_irq_pending()).
 * Si se queda a 0 aunque PENIRQ cambie de nivel (touch_is_pressed()),
 * confirma que la interrupcion no esta llegando a dispararse. */
uint32_t touch_irq_count(void);

/* Lectura cruda (sin calibrar) de los canales X/Y del XPT2046, con
 * promediado de varias muestras para reducir ruido tipico del tactil
 * resistivo. Devuelve 1 si se pudo leer (PENIRQ estaba activo), 0 si no
 * habia contacto. raw_x/raw_y quedan en el rango del ADC del XPT2046
 * (0-4095, 12 bits) - NO son coordenadas de pantalla todavia. */
uint8_t touch_read_raw(uint16_t *raw_x, uint16_t *raw_y);

/*
 * Calibracion de 2 puntos por eje (min/max de ADC crudo <-> extremos de
 * pantalla), mas swap/inversion de ejes por si el panel resistivo esta
 * montado girado respecto al LCD (habitual, hay que comprobarlo a mano
 * tocando las esquinas y viendo que eje/sentido corresponde a cual).
 * Procedimiento tipico: tocar dos esquinas opuestas conocidas, anotar
 * los raw_x/raw_y de touch_read_raw() en cada una, y rellenar esto.
 */
typedef struct {
    uint16_t raw_x_min, raw_x_max;
    uint16_t raw_y_min, raw_y_max;
    uint8_t  swap_xy;   /* 1 si el eje X del tactil corresponde al Y de pantalla */
    uint8_t  invert_x;  /* 1 si raw_x_min visualmente cae en el lado derecho */
    uint8_t  invert_y;  /* 1 si raw_y_min visualmente cae abajo */
} touch_calibration_t;

void touch_set_calibration(const touch_calibration_t *cal);

/* Copies the currently-active calibration into *out - whatever the
 * last touch_set_calibration() call set (touch_init()'s hardcoded
 * default if none has run yet this session). Added alongside
 * settings.c/spi_flash.c's config-file persistence (17/08/2026) - that
 * code needs to read back the live calibration to save it, and there
 * was no getter before this. */
void touch_get_calibration(touch_calibration_t *out);

/* Lectura calibrada: aplica touch_set_calibration() a touch_read_raw() y
 * recorta el resultado a [0, GFX_SCREEN_WIDTH-1] x [0, GFX_SCREEN_HEIGHT-1].
 * Devuelve 1 si habia contacto (mismo criterio que touch_read_raw()). Sin
 * calibrar (antes de la primera touch_set_calibration()), usa una
 * calibracion identidad de 0-4095 -> 0-800/0-480, solo util para ver que
 * el driver responde, no para coordenadas reales. */
uint8_t touch_read(uint16_t *x, uint16_t *y);

/*
 * Cuantas lecturas seguidas y estables hacen falta para dar un toque por
 * bueno: 1 = sensible, 2 = normal (por defecto), 3 = firme. Ver el
 * comentario largo en touch.c. Cambiarlo reinicia la cuenta en curso, asi
 * que nunca deja un toque a medio confirmar.
 */
void    touch_set_firmeza(uint8_t n);
uint8_t touch_get_firmeza(void);

/* Vuelca por UART (debug_print) una lectura cruda etiquetada, pensado
 * para el primer bring-up: tocar cada esquina del panel y comprobar que
 * los valores cambian de forma coherente antes de fiarse de nada mas. */
void touch_debug_raw(void);

/*
 * Continuous, throttled raw+calibrated dump - added 18/08/2026 to
 * chase down "the screen edges don't respond to touch" reports. Call
 * once per main loop iteration (behind a temporary debug gate, same
 * pattern as CALIB_HEIGHT_TEST/SPI_FLASH_PROBE_TEST in main.c) while
 * chasing an edge-response issue: prints BOTH the raw ADC reading AND
 * touch_read()'s calibrated (x,y) on the same line, throttled to
 * roughly once every 150ms while a finger is held down (not once per
 * ~43ms frame, which would flood the UART) - so pressing near a
 * screen edge and watching the log answers directly which of the two
 * layers is actually at fault:
 *   - raw keeps changing smoothly all the way to the physical edge,
 *     but the calibrated (x,y) misbehaves (stays clamped at the same
 *     value, or the wrong one) -> a CALIBRATION problem (e.g.
 *     touch_calib.c's CAL_MARGIN keeps the measured raw_*_min/max away
 *     from the true physical edge, so touch_read() has to linearly
 *     EXTRAPOLATE beyond what was ever actually measured out there -
 *     if the panel is even slightly non-linear near its bezel, that
 *     extrapolation is exactly where it'd go wrong first).
 *   - raw itself goes flat / stops changing near the edge -> the
 *     resistive panel genuinely isn't sensing pressure there, a
 *     hardware/mechanical limitation of that specific area of the
 *     panel, not something touch_read()'s math can fix.
 * Does nothing (no-op) while nothing is pressed. */
void touch_debug_stream_poll(void);

#endif /* TOUCH_H */
