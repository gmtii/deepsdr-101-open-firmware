#include "touch.h"
#include "irq_prio.h"
#include "gd32f4xx.h"
#include "gfx.h"
#include "debug_uart.h"

/* --- Definicion de pines (confirmados en hardware real) --- */
#define T_CLK_PORT   GPIOB
#define T_CLK_PIN    GPIO_PIN_3
#define T_DOUT_PORT  GPIOB
#define T_DOUT_PIN   GPIO_PIN_4
#define T_DIN_PORT   GPIOB
#define T_DIN_PIN    GPIO_PIN_5
#define T_CS_PORT    GPIOD
#define T_CS_PIN     GPIO_PIN_6
#define T_BUSY_PORT  GPIOD
#define T_BUSY_PIN   GPIO_PIN_3
#define T_IRQ_PORT   GPIOD
#define T_IRQ_PIN    GPIO_PIN_2

/* Comandos XPT2046 estandar (protocolo ADS7843/TSC2046, ver comentario
 * en touch.h): S=1, modo 12 bits, diferencial, CON power-down entre
 * conversiones (PD1PD0=00). Este ultimo bit es critico para PENIRQ: el
 * comparador que activa PENIRQ solo se rearma cuando el chip vuelve a
 * power-down tras una conversion; con PD=11 ("siempre encendido", que
 * era el valor original aqui para ahorrarnos el pequeño retardo de
 * arranque del ADC) PENIRQ deja de responder a la presion del panel -
 * exactamente el sintoma que se vio en hardware real (PENIRQ nunca
 * cambiaba de nivel al tocar, pese a que el pin del MCU y el pull-up
 * funcionaban bien probado con un jumper a GND). */
#define CMD_READ_X  0xD0  /* canal 101 (X), 12bit, diferencial, PD=00 */
#define CMD_READ_Y  0x90  /* canal 001 (Y), 12bit, diferencial, PD=00 */

/* Numero de muestras por eje para promediar y reducir ruido tipico del
 * tactil resistivo. Se descartan la muestra mas alta y mas baja
 * (filtro de mediana simplificado) antes de promediar el resto. */
#define TOUCH_SAMPLES 5

static volatile uint8_t s_irq_flag = 0;
static volatile uint32_t s_irq_count = 0; /* diagnostico: cuantas veces se ha ejecutado EXTI2_IRQHandler de verdad */
static touch_calibration_t s_cal = {
    0, 4095,   /* raw_x_min, raw_x_max: identidad hasta que se calibre */
    0, 4095,   /* raw_y_min, raw_y_max */
    0, 0, 0    /* swap_xy, invert_x, invert_y */
};

/* extern del contador de ms de main.c, igual que ya usa rm68120_exmc.c,
 * para el pequeño delay entre CS y el primer clock que pide el datasheet
 * (tiempo de adquisicion tras seleccionar el chip). */
extern volatile uint32_t g_msticks;

/*
 * FORCED -O0 (30/07/2026): same uncalibrated-NOP-loop issue as
 * i2c_bitbang.c's delay_i2c() - pinned here for the same reason, even
 * though this one (touch SPI bit-bang) wasn't the confirmed cause of
 * the spectrum/noise regression. See delay_i2c()'s comment for the
 * full story before removing this.
 */
__attribute__((optimize("O0")))
static void delay_us_approx(uint32_t us)
{
    /* Bucle sin calibrar a instruccion - solo para los ~1-2us de guarda
     * entre flancos que pide el XPT2046, no es critico como los tiempos
     * del bus EXMC. Si hace falta mas precision, cambiar por un timer. */
    volatile uint32_t i;
    for (i = 0; i < us * 20U; i++) {
        __NOP();
    }
}

static inline void t_clk(uint8_t level)
{
    gpio_bit_write(T_CLK_PORT, T_CLK_PIN, level ? SET : RESET);
}

static inline void t_din(uint8_t level)
{
    gpio_bit_write(T_DIN_PORT, T_DIN_PIN, level ? SET : RESET);
}

static inline uint8_t t_dout(void)
{
    return (gpio_input_bit_get(T_DOUT_PORT, T_DOUT_PIN) == SET) ? 1 : 0;
}

static inline void t_cs(uint8_t level)
{
    gpio_bit_write(T_CS_PORT, T_CS_PIN, level ? SET : RESET);
}

/*
 * Transaccion completa: manda `cmd` (8 bits, MSB primero) y devuelve el
 * valor de 12 bits leido a continuacion. Protocolo SPI modo 0: DIN se
 * fija con CLK en bajo y se muestra por el XPT2046 en el flanco de
 * subida; DOUT cambia en el flanco de bajada y se lee con CLK en bajo.
 *
 * Tras los 8 bits de comando, el XPT2046 mete un bit nulo y despues los
 * 12 bits de dato (MSB primero), completando 16 bits de "lectura" en
 * total. Se descartan los 3 bits menos significativos del resultado de
 * 16 bits para quedarnos con los 12 bits de dato alineados a la derecha
 * (>> 3), que es la convencion estandar de este protocolo.
 *
 * NO VALIDADO AUN EN OSCILOSCOPIO - ver aviso en touch.h. Si
 * touch_debug_raw() da valores que no cambian de forma coherente al
 * tocar el panel, empezar a depurar por aqui.
 */
static uint16_t xpt2046_transfer(uint8_t cmd)
{
    uint8_t i;
    uint32_t result = 0;

    t_clk(0);
    for (i = 0; i < 8; i++) {
        t_din((cmd & 0x80) ? 1 : 0);
        cmd = (uint8_t)(cmd << 1);
        delay_us_approx(1);
        t_clk(1);
        delay_us_approx(1);
        t_clk(0);
    }

    /* 16 clocks de lectura: bit nulo + 12 bits de dato + 3 bits de relleno */
    for (i = 0; i < 16; i++) {
        delay_us_approx(1);
        t_clk(1);
        result = (result << 1) | t_dout();
        delay_us_approx(1);
        t_clk(0);
    }

    return (uint16_t)((result >> 3) & 0x0FFF);
}

void touch_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_SYSCFG);

    /* CLK, DIN, CS: salidas push-pull */
    gpio_mode_set(T_CLK_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, T_CLK_PIN);
    gpio_output_options_set(T_CLK_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, T_CLK_PIN);
    gpio_mode_set(T_DIN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, T_DIN_PIN);
    gpio_output_options_set(T_DIN_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, T_DIN_PIN);
    gpio_mode_set(T_CS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, T_CS_PIN);
    gpio_output_options_set(T_CS_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, T_CS_PIN);

    /* DOUT: entrada, sin pull (el XPT2046 lo maneja activamente cuando
     * CS esta bajo; con CS alto queda en alta impedancia, pero no lo
     * leemos en ese estado). */
    gpio_mode_set(T_DOUT_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, T_DOUT_PIN);

    /* BUSY: entrada, no usada activamente todavia (ver touch.h) */
    gpio_mode_set(T_BUSY_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, T_BUSY_PIN);

    /* PENIRQ: entrada con pull-up (open-drain en el XPT2046, activo a
     * nivel bajo) + EXTI en flanco de bajada. La ISR SOLO marca un flag,
     * ninguna transaccion SPI dentro de la interrupcion. */
    gpio_mode_set(T_IRQ_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, T_IRQ_PIN);
    syscfg_exti_line_config(EXTI_SOURCE_GPIOD, EXTI_SOURCE_PIN2);
    exti_init(EXTI_2, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    exti_interrupt_flag_clear(EXTI_2);
    /* Prioridad 2, la ultima: es un dedo y puede esperar un milisegundo.
     * Antes ponia 2 tambien, pero con otro significado - ver irq_prio.h. */
    nvic_irq_enable(EXTI2_IRQn, IRQ_PRIO_TACTIL, IRQ_SUB_NINGUNA);

    t_cs(1);
    t_clk(0);
    t_din(0);

    /*
     * Ciclo de "cebado": una conversion dummy con PD=00 nada mas
     * arrancar, para asegurar que el chip queda en el estado que arma
     * PENIRQ desde el primer instante (por si el estado por defecto tras
     * power-on-reset no fuera suficiente en este lote/revision del
     * chip). El valor leido se descarta - no hay dedo en el panel
     * todavia necesariamente, esto es solo para dejar el ADC en el modo
     * correcto. */
    t_cs(0);
    delay_us_approx(2);
    (void)xpt2046_transfer(CMD_READ_Y);
    t_cs(1);

    /*
     * Calibracion medida en hardware real (Jorge, 27/07/2026), tocando
     * las 4 esquinas del panel con GFX_SCREEN_WIDTH/HEIGHT=800x480. El
     * rango de raw_x/raw_y de las 4 esquinas dio estos limites, pero en
     * la practica el eje X salio invertido (se pulsaba el lado
     * contrario al boton, misma altura correcta) - de ahi invert_x=1;
     * el eje Y si coincidia directamente con la pantalla.
     * Especifica de ESTA unidad de hardware - si se cambia de
     * panel/XPT2046 fisico, volver a medir con touch_debug_raw() en las
     * 4 esquinas y reajustar esto (incluido si invert_x/invert_y siguen
     * haciendo falta).
     */
    {
        touch_calibration_t cal = {130, 3990, 230, 3805, 0, 1, 0};
        touch_set_calibration(&cal);
    }

    debug_print("touch_init: completado\n");
}

void EXTI2_IRQHandler(void)
{
    if (exti_interrupt_flag_get(EXTI_2) != RESET) {
        exti_interrupt_flag_clear(EXTI_2);
        s_irq_flag = 1;
        s_irq_count++;
    }
}

/* Diagnostico: numero de veces que EXTI2_IRQHandler se ha ejecutado de
 * verdad desde el arranque, sin consumir/limpiar nada (a diferencia de
 * touch_irq_pending()). Sirve para confirmar si el ISR llega a disparar
 * alguna vez, independientemente de la logica de consumo del flag. */
uint32_t touch_irq_count(void)
{
    return s_irq_count;
}

uint8_t touch_irq_pending(void)
{
    uint8_t pending;

    /*
     * Sin __disable_irq()/__enable_irq(): en pruebas reales en hardware,
     * reactivar interrupciones globales aqui (PRIMASK) colgaba el
     * sistema de forma reproducible - causa no resuelta del todo (las
     * intrinsecas CMSIS son correctas, cpsid/cpsie de una instruccion,
     * y EXTI2 estaba enmascarado en el NVIC durante la prueba que aislo
     * el problema, asi que no deberia haber nada pendiente que disparar).
     * Si se quiere investigar mas a fondo, hace falta un depurador en
     * vivo mirando registros de excepcion en el momento del cuelgue.
     *
     * Mientras tanto, esto es seguro sin proteccion: s_irq_flag es un
     * unico byte, escrito solo por la ISR y leido/limpiado solo aqui; en
     * Cortex-M una lectura o escritura de un byte es atomica frente a
     * interrupciones (no puede interrumpirse a mitad de instruccion). El
     * unico riesgo real es perder un flanco si la ISR escribe 1 justo
     * entre el read y el write de abajo - en ese caso, touch_is_pressed()
     * (que se sigue consultando por nivel en cada vuelta del bucle) ya
     * cubre el contacto igualmente en la siguiente iteracion.
     */
    pending = s_irq_flag;
    s_irq_flag = 0;

    return pending;
}

uint8_t touch_is_pressed(void)
{
    /* PENIRQ activo a nivel bajo. Solo valido leerlo con CS en alto
     * (fuera de una transaccion SPI); touch_read_raw() lo respeta. */
    return (gpio_input_bit_get(T_IRQ_PORT, T_IRQ_PIN) == RESET) ? 1 : 0;
}

/* Ordena 5 muestras con insertion sort (n pequeño, de sobra) y devuelve
 * la media de las 3 centrales, descartando el minimo y el maximo -
 * filtro de mediana simplificado, barato y efectivo contra el ruido
 * puntual tipico de un tactil resistivo. */
static uint16_t filtered_average(uint16_t *samples, uint8_t n)
{
    uint8_t i, j;
    uint32_t sum = 0;
    uint8_t count = 0;

    for (i = 1; i < n; i++) {
        uint16_t key = samples[i];
        j = i;
        while (j > 0 && samples[j - 1] > key) {
            samples[j] = samples[j - 1];
            j--;
        }
        samples[j] = key;
    }

    for (i = 1; i < (uint8_t)(n - 1); i++) {
        sum += samples[i];
        count++;
    }
    return (uint16_t)(sum / count);
}

uint8_t touch_read_raw(uint16_t *raw_x, uint16_t *raw_y)
{
    uint16_t samples_x[TOUCH_SAMPLES];
    uint16_t samples_y[TOUCH_SAMPLES];
    uint8_t i;

    if (!touch_is_pressed()) {
        return 0;
    }

    t_cs(0);
    delay_us_approx(2); /* tiempo de adquisicion tras seleccionar chip */

    for (i = 0; i < TOUCH_SAMPLES; i++) {
        samples_x[i] = xpt2046_transfer(CMD_READ_X);
    }
    for (i = 0; i < TOUCH_SAMPLES; i++) {
        samples_y[i] = xpt2046_transfer(CMD_READ_Y);
    }

    t_cs(1);

    /* Comprobar de nuevo tras la transaccion: si el dedo se levanto a
     * mitad de la lectura, mejor descartar la muestra que devolver un
     * punto espurio (arrastre hacia una esquina, tipico artefacto). */
    if (!touch_is_pressed()) {
        return 0;
    }

    *raw_x = filtered_average(samples_x, TOUCH_SAMPLES);
    *raw_y = filtered_average(samples_y, TOUCH_SAMPLES);
    return 1;
}

void touch_set_calibration(const touch_calibration_t *cal)
{
    s_cal = *cal;
}

void touch_get_calibration(touch_calibration_t *out)
{
    *out = s_cal;
}

static uint16_t clamp_u16(int32_t v, uint16_t max_exclusive)
{
    if (v < 0) {
        return 0;
    }
    if (v >= (int32_t)max_exclusive) {
        return (uint16_t)(max_exclusive - 1);
    }
    return (uint16_t)v;
}

/*
 * FIRMEZA DEL TOQUE - 22/09/2026, pedido por el usuario: el tactil
 * respondia a la minima y habia que endurecerlo.
 *
 * Hasta ahora bastaba UNA lectura con el pin PENIRQ bajo para dar un toque
 * por bueno. Sobre un panel resistivo barato eso convierte en pulsacion
 * cualquier roce, cualquier apoyo del canto de la mano y cualquier glitch
 * electrico - y desde que los objetivos tactiles son grandes y no hay zonas
 * muertas, un roce cae casi siempre encima de algo.
 *
 * Ahora un toque tiene que confirmarse: s_confirmar lecturas seguidas, y
 * ademas estables entre si (las dos ultimas a menos de TOUCH_JITTER_PX una de
 * otra, ya en coordenadas de pantalla). Lo primero descarta el glitch de una
 * muestra; lo segundo descarta el artefacto clasico del panel resistivo, que
 * es un salto a una esquina mientras el dedo entra o sale.
 *
 * El numero de confirmaciones es ajustable desde los ajustes (celda
 * "Tactil"): 1 = sensible, 2 = normal, 3 = firme. No se pone en tiempo
 * porque el bucle principal no corre a ritmo fijo - a 30 fotogramas por
 * segundo, 2 confirmaciones son unos 66 ms, que es menos que un toque
 * deliberado (100-200 ms) y mas que un roce.
 *
 * La SUELTA no se filtra: soltar tarde es peor que soltar pronto, y el
 * codigo de arriba ya decide la accion en la pulsacion, no en la suelta.
 */
/*
 * EL SALTO MAXIMO ES UNA VELOCIDAD, NO UNA DISTANCIA - 11/10/2026.
 *
 * Y lo que iba regular no era el mapa: era ESTO.
 *
 * El parrafo de arriba se escribio para un filtro que solo tenia que
 * decidir si un TOQUE era de verdad, y con un dedo que se posa y no se
 * mueve, 40 px entre lectura y lectura no los da nadie. Seis dias
 * despues el mapa empezo a arrastrarse con el dedo -28/09/2026- y este
 * filtro paso a correr en mitad de un gesto continuo sin que nadie
 * volviera a mirarlo.
 *
 * LO QUE SE MIDIO (sim/tactil_arrastre.c, que compila este fichero y le
 * dicta el dedo haciendo de XPT2046):
 *
 *   paso por lectura   sueltas inventadas en 30 lecturas
 *        35 px                   0
 *        40 px                   1
 *        45 px                  16     <- una de cada dos
 *
 * Y el paso por lectura no lo elige el dedo, lo elige el BUCLE: cada
 * movimiento repinta el mapa entero, 254.740 accesos al bus (medido en
 * sim/mapa_pant.c). Lo que eso tarda depende del ajuste del bus de la
 * pantalla, que se toca desde Informacion: a 45 ns por acceso -lo que
 * lleva la radio del usuario, fila "Bus pantalla"- son 11,5 ms; con los
 * tiempos que trae el codigo por defecto, 30,6. Con 20
 * ms entre lecturas, 40 px son 385 mm/s; con 30 ms, 257 mm/s. Un dedo
 * arrastrando algo por una pantalla de 154 mm hace entre 100 y 600 mm/s
 * sin despeinarse. O sea que el filtro llamaba imposible a un gesto
 * normal.
 *
 * Y COMO EL BUCLE NO CORRE A RITMO FIJO, el mismo gesto caia unas veces
 * por debajo y otras por encima. De ahi el "un poco regular": no estaba
 * roto, estaba ENTRECORTADO.
 *
 * El comentario de arriba ya habia visto la mitad del problema -"no se
 * pone en tiempo porque el bucle principal no corre a ritmo fijo"- y
 * saco la conclusion contraria: un bucle que no corre a ritmo fijo es
 * justamente la razon por la que una distancia por lectura no significa
 * nada. 40 px en 5 ms son 8.000 px/s y no existen; 40 px en 30 ms son
 * 1.333 px/s y es un martes cualquiera. Asi que el tope se mide en
 * PIXELES POR MILISEGUNDO, que es lo que siempre quiso decir: "una
 * velocidad que ningun dedo alcanza".
 *
 * El suelo de 40 px se queda: con el bucle rapido el tope no baja de lo
 * que habia, o sea que esto no aprieta en ningun sitio, solo afloja donde
 * apretaba de mas. Y el techo esta para que una vuelta larguisima no
 * acabe consintiendolo todo.
 *
 * El tiempo sale de g_msticks, que este fichero ya miraba para la traza
 * de depuracion: no hay contador nuevo que mantener.
 */
#define TOUCH_JITTER_PX     40   /* suelo: lo que habia, para bucles rapidos */
#define TOUCH_JITTER_PX_MS   8   /* px por ms: 8.000 px/s, 1.540 mm/s */
#define TOUCH_JITTER_PX_MAX 400  /* techo: media pantalla */
#define TOUCH_CONFIRM_MAX  3

/*
 * ======================================================================
 * LA FIRMEZA SE MIDE EN MILISEGUNDOS, NO EN LECTURAS - 19/10/2026
 * ======================================================================
 *
 * Avisado por el usuario, que en STANAG y en IDENT la radio responde al
 * dedo mas despacio que en los demas modos.
 *
 * El comentario de arriba ya lo dice casi todo sin darse cuenta: "no se
 * pone en tiempo porque el bucle principal no corre a ritmo fijo - a 30
 * fotogramas por segundo, 2 confirmaciones son unos 66 ms, que es menos
 * que un toque deliberado y mas que un roce".
 *
 * O sea que lo que separa un toque de un roce SIEMPRE fue un TIEMPO, y lo
 * que habia escrito era una CUENTA DE LECTURAS que por casualidad daba ese
 * tiempo al ritmo al que corria el bucle aquel dia. Cuando el bucle se
 * alarga -en los dos modos que traen dos transformadas- esa misma cuenta
 * se convierte en 200 ms y la radio se come los toques. Y en cuanto el
 * tactil se mira mas veces por vuelta (ver touch_latido()), la misma
 * cuenta se convierte en dos milisegundos y vuelve a entrar cualquier
 * roce. La cuenta no significa nada; el tiempo si.
 *
 * Asi que se dice con todas las letras: entre dos lecturas que CUENTAN
 * para confirmar tienen que pasar al menos TOUCH_CONF_MS. Mirar mas
 * deprisa ya no hace la radio mas sensible, y mirar mas despacio ya no la
 * hace sorda - solo tarda mas en enterarse, que es lo que pasaba antes.
 *
 * VEINTE MILISEGUNDOS es lo que daba la cuenta de dos en los modos donde
 * el tactil iba bien: el fotograma son 22 ms (SPECTRUM_FRAME_MS_DEF) y el
 * bucle va a ese ritmo. O sea que esto no cambia el tacto donde ya era
 * bueno; lo arregla donde no lo era. Con firmeza 3 son 40 ms, y con 1 no
 * hay espera, que es lo que "sensible" quiere decir.
 *
 * Una vez CONFIRMADO el contacto esto no se mira: mientras el dedo siga
 * puesto, cada lectura vale, que es lo que necesita un arrastre.
 */
#define TOUCH_CONF_MS  20U

static uint8_t  s_confirmar = 2U;   /* por defecto, "normal" */
static uint8_t  s_conf_n    = 0U;   /* lecturas buenas seguidas */
static uint32_t s_conf_ini_ms = 0U; /* cuando se poso el dedo de esta serie */
static uint16_t s_conf_x    = 0U;
static uint16_t s_conf_y    = 0U;
static uint32_t s_conf_ms   = 0U;   /* cuando se acepto s_conf_x/s_conf_y */
/* El salto que esta en cuarentena, esperando a que la siguiente lectura
 * diga si era el dedo moviendose o el panel inventandose una esquina. */
static uint16_t s_cand_x    = 0U;
static uint16_t s_cand_y    = 0U;
static uint8_t  s_cand      = 0U;

/* Lo que se le consiente recorrer a un dedo en el hueco que haya pasado. */
static uint16_t tope_salto(uint32_t hueco_ms)
{
    uint32_t t = (uint32_t)TOUCH_JITTER_PX_MS * hueco_ms;

    if (t < (uint32_t)TOUCH_JITTER_PX)      { t = (uint32_t)TOUCH_JITTER_PX; }
    if (t > (uint32_t)TOUCH_JITTER_PX_MAX)  { t = (uint32_t)TOUCH_JITTER_PX_MAX; }
    return (uint16_t)t;
}

static uint8_t lejos(uint16_t ax, uint16_t ay,
                     uint16_t bx, uint16_t by, uint16_t tope)
{
    int32_t dx = (int32_t)ax - (int32_t)bx;
    int32_t dy = (int32_t)ay - (int32_t)by;

    if (dx < 0) { dx = -dx; }
    if (dy < 0) { dy = -dy; }
    return (uint8_t)(((dx > (int32_t)tope) || (dy > (int32_t)tope)) ? 1U : 0U);
}

void touch_set_firmeza(uint8_t n)
{
    if (n < 1U) { n = 1U; }
    if (n > TOUCH_CONFIRM_MAX) { n = TOUCH_CONFIRM_MAX; }
    s_confirmar = n;
    s_conf_n = 0U;
    s_cand   = 0U;   /* la cuarentena tampoco sobrevive a un cambio de firmeza */
}

uint8_t touch_get_firmeza(void)
{
    return s_confirmar;
}

static uint8_t touch_read_una(uint16_t *x, uint16_t *y);

uint8_t touch_read(uint16_t *x, uint16_t *y)
{
    uint16_t px, py, tope;
    uint32_t ahora = g_msticks;
    uint32_t hueco;

    if (!touch_read_una(&px, &py)) {
        /* NO HAY DEDO, y eso no se filtra: "soltar tarde es peor que
         * soltar pronto". Esta es la UNICA via por la que este fichero
         * dice que no hay contacto - ver mas abajo por que importa. */
        s_conf_n = 0U;
        s_cand   = 0U;
        return 0;
    }

    hueco = (uint32_t)(ahora - s_conf_ms);
    tope = tope_salto(hueco);

    if ((s_conf_n != 0U) && lejos(px, py, s_conf_x, s_conf_y, tope)) {
        if (s_conf_n >= s_confirmar) {
            /*
             * UN SALTO NO ES UNA SUELTA - 11/10/2026.
             *
             * Aqui se devolvia 0, y 0 quiere decir "no hay dedo". Lo leen
             * asi TODOS los llamadores; el del mapa, ademas, da el
             * arrastre por terminado (ver s_mapa_tap en main.c). O sea
             * que la respuesta a "el dedo se ha movido mucho" era "el
             * dedo ya no esta", que es la unica respuesta que seguro es
             * falsa: el dedo esta pegado al cristal, lo acabamos de leer.
             *
             * El contacto YA estaba confirmado, asi que no hay nada que
             * volver a confirmar - lo que decide el filtro es si se CREE
             * el sitio nuevo, no si hay alguien. Y para eso no hace falta
             * contestar una suelta: basta con quedarse donde se estaba y
             * que lo diga la siguiente lectura. Si coincide con el salto,
             * el dedo se movio de verdad y se va; si no coincide, era el
             * panel inventandose una esquina y nunca nos movimos.
             *
             * Cuesta UNA lectura de retraso en un gesto muy rapido, y no
             * pierde ni un pixel: el ancla se queda quieta, asi que el
             * siguiente desplazamiento que se entregue lleva el camino
             * entero.
             */
            if (s_cand && !lejos(px, py, s_cand_x, s_cand_y, tope)) {
                s_cand = 0U;   /* dos lecturas de acuerdo: era el dedo */
            } else {
                s_cand   = 1U;
                s_cand_x = px;
                s_cand_y = py;
                *x = s_conf_x;   /* se queda donde estaba... */
                *y = s_conf_y;
                return 1;        /* ...y SIGUE HABIENDO DEDO */
            }
        } else {
            /* Todavia ENTRANDO. Aqui el salto si es motivo para empezar
             * la serie de cero: es justo el artefacto de esquina que se
             * queria descartar, y nadie esta arrastrando nada todavia. */
            s_conf_n = 0U;
            s_cand   = 0U;
        }
    } else {
        s_cand = 0U;
    }

    s_conf_x  = px;
    s_conf_y  = py;
    /* El reloj se apunta SOLO al aceptar un sitio: el tope de la proxima
     * se mide contra la distancia que de verdad se compara. Una lectura en
     * cuarentena no mueve ni el sitio ni la hora. */
    s_conf_ms = ahora;
    /*
     * Y LA CUENTA SOLO SUBE SI HA PASADO TIEMPO. Ver TOUCH_CONF_MS. La
     * primera de la serie cuenta siempre -es la que arranca el reloj-;
     * las siguientes, solo cuando el contacto lleva lo suficiente. Una
     * vez confirmado ya no se mira: mientras el dedo siga puesto, cada
     * lectura vale.
     */
    /*
     * EL RELOJ CORRE DESDE QUE EL DEDO SE POSO, NO DESDE LA LECTURA
     * ANTERIOR.
     *
     * *** Y esta es la version buena: la primera comparaba `hueco` -el
     * tiempo desde la lectura aceptada anterior- contra TOUCH_CONF_MS, y
     * eso deja el tactil MUERTO en cuanto se mira mas deprisa que la
     * firmeza. Mirando cada 10 ms, el hueco es siempre 10, nunca llega a
     * 20, y el contacto no se confirma JAMAS por mucho que se aguante el
     * dedo. Lo cazo sim/tactil_arrastre.c en su tabla de toques: la fila
     * de 10 ms salia 0/20 entera, con el dedo puesto trescientos
     * milisegundos. ***
     *
     * Lo que tiene que pasar es que el dedo LLEVE puesto su tiempo: una
     * confirmacion mas por cada TOUCH_CONF_MS desde que se poso, hasta
     * la firmeza pedida. Asi mirar mas deprisa adelanta el momento en que
     * uno se entera, pero nunca por debajo del tiempo; y mirar mas
     * despacio solo lo retrasa.
     */
    if (s_conf_n == 0U) {
        s_conf_n = 1U;
        s_conf_ini_ms = ahora;
    } else if (s_conf_n < s_confirmar) {
        uint32_t llevan = (uint32_t)(ahora - s_conf_ini_ms);
        uint32_t quiere = 1UL + (llevan / TOUCH_CONF_MS);

        if (quiere > (uint32_t)s_confirmar) { quiere = (uint32_t)s_confirmar; }
        if (quiere > (uint32_t)s_conf_n)    { s_conf_n = (uint8_t)quiere; }
    }
    (void)hueco;

    if (s_conf_n < s_confirmar) {
        return 0;
    }

    *x = px;
    *y = py;
    return 1;
}

static uint8_t touch_read_una(uint16_t *x, uint16_t *y)
{
    uint16_t raw_x, raw_y;
    int32_t val_x, val_y; /* scaled from the raw_x/raw_y HARDWARE channels respectively, BEFORE swap_xy relabels which one ends up as the final screen x vs y */
    int32_t range_x, range_y;
    uint16_t target_w, target_h; /* which screen dimension each hardware channel actually lands in - see the SWAP FIX comment below */

    if (!touch_read_raw(&raw_x, &raw_y)) {
        return 0;
    }

    range_x = (int32_t)s_cal.raw_x_max - (int32_t)s_cal.raw_x_min;
    range_y = (int32_t)s_cal.raw_y_max - (int32_t)s_cal.raw_y_min;
    if (range_x == 0) {
        range_x = 1;
    }
    if (range_y == 0) {
        range_y = 1;
    }

    /*
     * SWAP FIX (added alongside touch_calib.c): on a NON-square panel
     * (800x480 here), scaling val_x to GFX_SCREEN_WIDTH and val_y to
     * GFX_SCREEN_HEIGHT and only THEN swapping them (the original code)
     * is wrong whenever swap_xy=1 - it would hand out a "screen x" that
     * only ever reached GFX_SCREEN_HEIGHT-1 (480) instead of
     * GFX_SCREEN_WIDTH-1 (800), and a "screen y" clamped down from a
     * 0-799 range to 0-479, silently losing the top ~320 rows. Fixed by
     * deciding EACH hardware channel's target scale (width or height)
     * from swap_xy BEFORE scaling, so the swap only ever relabels which
     * value is x and which is y - never which scale was used to produce
     * it. With swap_xy=0 this is numerically identical to the old code.
     */
    target_w = s_cal.swap_xy ? GFX_SCREEN_HEIGHT : GFX_SCREEN_WIDTH;  /* what raw_x's channel scales to */
    target_h = s_cal.swap_xy ? GFX_SCREEN_WIDTH  : GFX_SCREEN_HEIGHT; /* what raw_y's channel scales to */

    val_x = ((int32_t)raw_x - (int32_t)s_cal.raw_x_min) * (int32_t)target_w / range_x;
    val_y = ((int32_t)raw_y - (int32_t)s_cal.raw_y_min) * (int32_t)target_h / range_y;

    if (s_cal.invert_x) {
        val_x = (int32_t)target_w - 1 - val_x;
    }
    if (s_cal.invert_y) {
        val_y = (int32_t)target_h - 1 - val_y;
    }

    if (s_cal.swap_xy) {
        *x = clamp_u16(val_y, GFX_SCREEN_WIDTH);
        *y = clamp_u16(val_x, GFX_SCREEN_HEIGHT);
    } else {
        *x = clamp_u16(val_x, GFX_SCREEN_WIDTH);
        *y = clamp_u16(val_y, GFX_SCREEN_HEIGHT);
    }
    return 1;
}

void touch_debug_raw(void)
{
    uint16_t raw_x, raw_y;

    if (touch_read_raw(&raw_x, &raw_y)) {
        debug_print_dec("touch raw_x", raw_x);
        debug_print_dec("touch raw_y", raw_y);
    } else {
        debug_print("touch: sin contacto\n");
    }
}

static uint32_t s_debug_stream_last_ms = 0U;
#define TOUCH_DEBUG_STREAM_PERIOD_MS 150UL

void touch_debug_stream_poll(void)
{
    uint16_t raw_x, raw_y;

    if (!touch_is_pressed()) {
        return;
    }
    if ((g_msticks - s_debug_stream_last_ms) < TOUCH_DEBUG_STREAM_PERIOD_MS) {
        return;
    }
    s_debug_stream_last_ms = g_msticks;

    if (touch_read_raw(&raw_x, &raw_y)) {
        uint16_t cal_x = 0U, cal_y = 0U;
        uint8_t cal_ok = touch_read(&cal_x, &cal_y);

        debug_print_dec("touch_debug_stream: raw_x", raw_x);
        debug_print_dec("touch_debug_stream: raw_y", raw_y);
        if (cal_ok) {
            debug_print_dec("touch_debug_stream: calibrated x", cal_x);
            debug_print_dec("touch_debug_stream: calibrated y", cal_y);
        } else {
            debug_print("touch_debug_stream: touch_read() returned 0 (lost contact between the two reads?)\n");
        }
        debug_print("\n");
    }
}

/*
 * ======================================================================
 * EL LATIDO DEL TACTIL - 19/10/2026
 * ======================================================================
 *
 * Esto es lo que se engancha a gfx2_set_pump(), el gancho que gfx2.c
 * llama ENTRE BANDAS de cada repintado. Su propio comentario, escrito
 * hace tiempo, dice para que existe: "sirve para muestrear el tactil
 * durante un repintado: sin esto, la entrada solo se lee entre repintados
 * enteros, y si uno tarda un segundo hay que mantener el dedo puesto todo
 * ese tiempo para que se note".
 *
 * Y no lo llamaba nadie. El gancho estaba montado y sin enchufar, como le
 * paso a rtty_auto.c. Un repintado del panel digital son 253.940 accesos
 * al bus; mientras dura, el tactil no existia.
 *
 * LO QUE HACE Y LO QUE NO. Solo MIRA: adelanta el filtro de confirmacion
 * y se apunta donde estaba el dedo. No despacha el toque ni repinta nada,
 * y eso no es pereza - esto corre DENTRO de gfx2_render(), con la banda
 * compositora cogida. Un repintado desde aqui se encontraria la banda
 * ocupada y se iria sin dibujar, que es como desaparece un boton.
 *
 * EL PESTILLO. Si el dedo entra y sale entero mientras dura un repintado,
 * el bucle principal no llega a ver ni la pulsacion ni la suelta. Por eso
 * esto deja apuntado que HUBO un contacto confirmado y donde, y
 * demo_touch_poll() lo recoge en la vuelta siguiente. Un toque que paso
 * se atiende tarde; antes no se atendia nunca.
 *
 * EL RITMO. Entre bandas hay menos de un milisegundo, y mirar a ese ritmo
 * no sirve de nada: lo que confirma un contacto es que dure (ver
 * TOUCH_CONF_MS), no cuantas veces se mire. Asi que esto tiene su propio
 * reloj y no mira mas de una vez cada TOUCH_LATIDO_MS. A ocho
 * milisegundos, un repintado de 11 ms se mira una o dos veces y la
 * confirmacion sale a los veinte, pase lo que pase con el bucle.
 */
#define TOUCH_LATIDO_MS  8U

static uint32_t s_latido_ms;
static uint8_t  s_lat_hubo;    /* hubo contacto confirmado sin recoger */
static uint16_t s_lat_x, s_lat_y;

void touch_latido(void)
{
    uint16_t x = 0U, y = 0U;

    if ((uint32_t)(g_msticks - s_latido_ms) < TOUCH_LATIDO_MS) { return; }
    s_latido_ms = g_msticks;

    if (touch_read(&x, &y)) {
        s_lat_hubo = 1U;
        s_lat_x = x;
        s_lat_y = y;
    }
}

uint8_t touch_pendiente(uint16_t *x, uint16_t *y)
{
    if (!s_lat_hubo) { return 0U; }
    s_lat_hubo = 0U;
    if (x) { *x = s_lat_x; }
    if (y) { *y = s_lat_y; }
    return 1U;
}

void touch_pendiente_borra(void)
{
    s_lat_hubo = 0U;
}
