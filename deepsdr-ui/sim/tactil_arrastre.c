/*
 * ======================================================================
 * EL DEDO QUE ARRASTRA, CONTRA EL TACTIL DE VERDAD - 11/10/2026
 * ======================================================================
 *
 * Asi que el arrastre no esta muerto: esta ENTRECORTADO. Y eso no se
 * mide mirando el mapa, se mide mirando lo unico que hay entre el dedo y
 * el mapa: touch_read().
 *
 * ESTE BANCO COMPILA User/touch.c DEL FIRMWARE, no una copia. El filtro
 * de confirmacion -las lecturas seguidas que hacen falta para dar un
 * toque por bueno, y el salto maximo que se les tolera- vive ahi dentro,
 * en variables estaticas; un banco que se lo reimplemente estaria
 * midiendo su propia copia y saldria verde pase lo que pase en la radio.
 *
 * COMO SE LE DICTA EL DEDO. El XPT2046 no se habla por el periferico SPI:
 * se habla a patadas de GPIO, bit a bit (ver xpt2046_transfer() en
 * touch.c). Eso, que en la radio es una pega, aqui es un regalo: basta
 * con que este banco DEFINA gpio_bit_write()/gpio_input_bit_get() para
 * ser el chip. El firmware cree que esta hablando con el panel y lo que
 * tiene delante es este fichero contestandole el dedo que nos convenga.
 *
 * LO QUE SE MIDE, Y POR QUE ESE NUMERO. touch_read() devuelve 0 cuando no
 * hay contacto. Sus llamadores -todos- leen eso como "ha levantado el
 * dedo"; el del mapa, ademas, da el arrastre por terminado. Asi que la
 * pregunta no es estetica: ¿cuantas veces, en mitad de un arrastre con el
 * dedo PEGADO a la pantalla, contesta touch_read() que no hay nadie?
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gd32f4xx.h"
#include "touch.h"

volatile uint32_t g_msticks = 0U;

/* ---------------------------------------------------------------- */
/* El chip de mentira.                                              */
/* ---------------------------------------------------------------- */

/* Donde esta el dedo AHORA, en coordenadas de pantalla. -1 = no hay dedo. */
static int32_t s_dedo_x = -1, s_dedo_y = -1;

/* Puertos y patillas, copiados de la cabecera de touch.c porque aqui
 * hacemos de chip y hay que saber quien es quien. Si alguna vez cambian
 * ahi, este banco deja de contestar y se nota al instante: touch_read()
 * no devolveria NADA, no un numero distinto. */
#define P_GPIOB 1U
#define P_GPIOD 3U
#define PIN_CLK  (1U << 3)   /* GPIOB */
#define PIN_DOUT (1U << 4)   /* GPIOB */
#define PIN_DIN  (1U << 5)   /* GPIOB */
#define PIN_CS   (1U << 6)   /* GPIOD */
#define PIN_IRQ  (1U << 2)   /* GPIOD */

#define CMD_X 0xD0
#define CMD_Y 0x90

static uint8_t  s_clk;          /* nivel actual del reloj */
static uint8_t  s_din;          /* nivel actual del dato de entrada */
static uint8_t  s_nclk;         /* pulsos dentro de la transferencia */
static uint8_t  s_cmd;          /* mandato que se esta recibiendo */
static uint32_t s_salida;       /* 16 bits a soltar por DOUT */
static uint8_t  s_dout;         /* el bit que DOUT ensena ahora mismo */

/* De pantalla a cuentas del convertidor. La calibracion que se le mete al
 * firmware mas abajo es la recta mas simple posible (0..4095 -> 0..ancho),
 * asi que esto es su inversa y el viaje de ida y vuelta es la identidad
 * salvo redondeo. Lo que se quiere medir es el FILTRO, no la calibracion:
 * esa ya tiene su propio banco. */
static uint16_t cuenta_de_px(int32_t px, int32_t ancho)
{
    int32_t v = (px * 4095 + (ancho / 2)) / (ancho - 1);
    if (v < 0) { v = 0; }
    if (v > 4095) { v = 4095; }
    return (uint16_t)v;
}

void rcu_periph_clock_enable(uint32_t p) { (void)p; }
void gpio_mode_set(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{ (void)a; (void)b; (void)c; (void)d; }
void gpio_output_options_set(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{ (void)a; (void)b; (void)c; (void)d; }
void gpio_bit_set(uint32_t p, uint32_t pin) { (void)p; (void)pin; }
void gpio_bit_reset(uint32_t p, uint32_t pin) { (void)p; (void)pin; }
void syscfg_exti_line_config(uint8_t a, uint8_t b) { (void)a; (void)b; }
void exti_init(uint32_t a, uint8_t b, uint8_t c) { (void)a; (void)b; (void)c; }
uint8_t exti_interrupt_flag_get(uint32_t a) { (void)a; return 0U; }
void exti_interrupt_flag_clear(uint32_t a) { (void)a; }
void nvic_irq_enable(uint8_t a, uint8_t b, uint8_t c) { (void)a; (void)b; (void)c; }

/* La patilla de seleccion arranca la cuenta de pulsos de cero: cada vez
 * que el firmware baja CS empieza una conversacion nueva. */
void gpio_bit_write(uint32_t port, uint32_t pin, bit_status val)
{
    uint8_t v = (val == SET) ? 1U : 0U;

    if (port == P_GPIOD && pin == PIN_CS) {
        if (v == 0U) { s_nclk = 0U; s_cmd = 0U; s_salida = 0U; }
        return;
    }
    if (port != P_GPIOB) { return; }

    if (pin == PIN_DIN) { s_din = v; return; }
    if (pin != PIN_CLK) { return; }

    if (v != 0U && s_clk == 0U) {
        /* FLANCO DE SUBIDA, que es donde pasa todo. Los ocho primeros
         * recogen el mandato; los dieciseis siguientes sacan el dato, y
         * el firmware lee DOUT con el reloj ya arriba (ver el bucle de
         * lectura de xpt2046_transfer), asi que el bit se pone AQUI. */
        if (s_nclk < 8U) {
            s_cmd = (uint8_t)((s_cmd << 1) | s_din);
            if (s_nclk == 7U) {
                int32_t v12;
                if (s_dedo_x < 0) {
                    v12 = 0;
                } else if (s_cmd == CMD_X) {
                    v12 = cuenta_de_px(s_dedo_x, 800);
                } else if (s_cmd == CMD_Y) {
                    v12 = cuenta_de_px(s_dedo_y, 480);
                } else {
                    v12 = 0;
                }
                /* Un bit nulo, doce de dato, tres de relleno: es lo que
                 * touch.c deshace con (result >> 3) & 0x0FFF. */
                s_salida = ((uint32_t)v12 & 0x0FFFU) << 3;
            }
        } else if (s_nclk < 24U) {
            uint8_t k = (uint8_t)(23U - s_nclk);   /* 15..0 */
            s_dout = (uint8_t)((s_salida >> k) & 1U);
        }
        s_nclk++;
        if (s_nclk >= 24U) { s_nclk = 0U; s_cmd = 0U; }
    }
    s_clk = v;
}

bit_status gpio_input_bit_get(uint32_t port, uint32_t pin)
{
    if (port == P_GPIOD && pin == PIN_IRQ) {
        /* PENIRQ es activo a nivel BAJO: RESET = hay dedo. */
        return (s_dedo_x >= 0) ? RESET : SET;
    }
    if (port == P_GPIOB && pin == PIN_DOUT) {
        return s_dout ? SET : RESET;
    }
    return SET;
}

/* ---------------------------------------------------------------- */
/* El banco.                                                        */
/* ---------------------------------------------------------------- */

static int fallos = 0;

static void mal(const char *q)
{
    printf("  MAL: %s\n", q);
    fallos++;
}

static void calibracion_recta(void)
{
    touch_calibration_t c;
    memset(&c, 0, sizeof c);
    c.raw_x_min = 0;    c.raw_x_max = 4095;
    c.raw_y_min = 0;    c.raw_y_max = 4095;
    c.swap_xy = 0; c.invert_x = 0; c.invert_y = 0;
    touch_set_calibration(&c);
}

/* Pone el dedo y pide UNA lectura al firmware, avanzando el reloj los
 * milisegundos que se diga. Devuelve 1 si touch_read() dijo que habia
 * contacto. */
static uint8_t muestra(int32_t px, int32_t py, uint32_t ms, uint16_t *lx, uint16_t *ly)
{
    uint16_t x = 0, y = 0;
    uint8_t  r;
    s_dedo_x = px; s_dedo_y = py;
    g_msticks += ms;
    r = touch_read(&x, &y);
    if (lx) { *lx = x; }
    if (ly) { *ly = y; }
    return r;
}

static void quita_dedo(void)
{
    uint16_t x, y;
    s_dedo_x = -1; s_dedo_y = -1;
    g_msticks += 20U;
    (void)touch_read(&x, &y);
}

/*
 * UN ARRASTRE. El dedo entra en (x0,y0), recorre `n` muestras moviendose
 * `paso` pixeles en horizontal cada una, y se levanta. Devuelve cuantas
 * de esas muestras -con el dedo PEGADO a la pantalla- el firmware dio por
 * "no hay contacto".
 */
static int arrastre_ms(int32_t x0, int32_t y0, int32_t paso, int n,
                       uint32_t hueco, int *seguidas_ok)
{
    int i, perdidas = 0, racha = 0, mejor = 0;

    quita_dedo();

    /* Entrada: el dedo se posa y el filtro tiene que confirmarlo. Eso NO
     * se cuenta como perdida: confirmar la entrada es justo para lo que
     * esta el filtro. Se le dan unas cuantas quietas. */
    for (i = 0; i < 4; i++) { (void)muestra(x0, y0, hueco, 0, 0); }

    for (i = 1; i <= n; i++) {
        int32_t px = x0 + paso * (int32_t)i;
        if (px < 0 || px > 799) { break; }
        if (!muestra(px, y0, hueco, 0, 0)) {
            perdidas++;
            if (racha > mejor) { mejor = racha; }
            racha = 0;
        } else {
            racha++;
        }
    }
    if (racha > mejor) { mejor = racha; }
    if (seguidas_ok) { *seguidas_ok = mejor; }
    quita_dedo();
    return perdidas;
}

static int arrastre(int32_t x0, int32_t y0, int32_t paso, int n, int *seguidas_ok)
{
    return arrastre_ms(x0, y0, paso, n, 20U, seguidas_ok);
}

/*
 * ======================================================================
 * EL TOQUE CORTO CONTRA UN BUCLE LENTO - 19/10/2026
 * ======================================================================
 *
 * Avisado por el usuario: en STANAG y en IDENT la radio responde al dedo
 * mas despacio que en los demas modos.
 *
 * Y no es que el tactil este mal: es que el bucle principal lo MUESTREA
 * pocas veces por segundo cuando el modo trae trabajo pesado. Un toque
 * solo se ve si, mientras el dedo esta puesto, el bucle pasa por aqui las
 * veces suficientes:
 *
 *   - touch_read() no da contacto hasta que lo CONFIRMA, y la firmeza por
 *     omision son DOS lecturas seguidas (ver s_confirmar en touch.c);
 *   - y despues hace falta una lectura mas, ya sin dedo, para que el que
 *     llama vea la suelta y dispare la accion.
 *
 * O sea TRES muestreos dentro de la duracion del toque. Mirando el tactil
 * cada 20 ms eso son 60 y no se nota; cada 65 ms son 195, y un toque
 * normal dura menos.
 *
 * LO QUE MIDE ESTE BANCO ES EL PERIODO DE MUESTREO, no el del bucle. No
 * son lo mismo y en eso esta el arreglo: el bucle tarda lo que tarda
 * -depende del modo y del dibujo- pero el tactil se puede mirar VARIAS
 * VECES por vuelta, y entonces el periodo que cuenta es el de mirarlo.
 *
 * Y NO SE ARREGLA AFLOJANDO LA FIRMEZA. Confirmar con dos lecturas
 * SEPARADAS EN EL TIEMPO es lo que distingue un toque de un roce o de un
 * apoyo del canto de la mano, y eso lo pidio el usuario expresamente
 * (ver el comentario de la firmeza en touch.c). Dos lecturas seguidas
 * dentro de la misma llamada confirmarian igual de rapido y volverian a
 * dar por bueno cualquier roce: el filtro mide tiempo, no cuentas.
 *
 * Esto lo MIDE, en vez de razonarlo: dedo puesto D milisegundos, el
 * tactil mirado cada P, y se cuenta en cuantos desfases de los dos
 * relojes sale el toque. Lo que hay que mirar de la tabla es la columna
 * de los toques cortos: ahi es donde se pierde.
 */
static int toque_visto(uint32_t dur_ms, uint32_t vuelta_ms, uint32_t desfase_ms)
{
    uint16_t x, y;
    uint32_t t;
    uint8_t  dentro = 0U;   /* el estado que lleva demo_touch_poll() */
    int      visto = 0;

    quita_dedo();
    /* El reloj del bucle corre desde antes; el dedo se posa en `desfase`
     * y se levanta en `desfase + dur`. Se mira un poco mas alla para que
     * la suelta tenga su pasada. */
    for (t = 0U; t < desfase_ms + dur_ms + 4U * vuelta_ms; t += vuelta_ms) {
        uint8_t hay_dedo = (uint8_t)(t >= desfase_ms && t < (desfase_ms + dur_ms));
        uint8_t leido;

        s_dedo_x = hay_dedo ? 400 : -1;
        s_dedo_y = hay_dedo ? 240 : -1;
        g_msticks += vuelta_ms;
        leido = touch_read(&x, &y);

        if (leido) {
            dentro = 1U;
        } else if (dentro) {
            dentro = 0U;
            visto = 1;          /* pulsacion + suelta: el toque cuenta */
        }
    }
    quita_dedo();
    return visto;
}

/* De cada 20 desfases posibles, cuantos salen. 20 de 20 = el toque SIEMPRE
 * se coge; 0 de 20 = no se coge nunca. */
static int cuantos_de_veinte(uint32_t dur_ms, uint32_t vuelta_ms)
{
    uint32_t k;
    int n = 0;
    for (k = 0U; k < 20U; k++) {
        if (toque_visto(dur_ms, vuelta_ms, (k * vuelta_ms) / 20U)) { n++; }
    }
    return n;
}

static void mide_toques(void)
{
    static const uint32_t k_dur[]    = { 40U, 60U, 80U, 120U, 200U, 300U };
    static const uint32_t k_vuelta[] = { 10U, 20U, 35U, 50U, 65U, 100U };
    size_t d, v;

    printf("\n  -- (5) toques cortos contra un muestreo lento --\n");
    printf("     de cada 20 toques, cuantos ve la radio\n\n");
    printf("     mirado |");
    for (d = 0U; d < sizeof k_dur / sizeof k_dur[0]; d++) {
        printf(" %4u ms", (unsigned)k_dur[d]);
    }
    printf("   <- lo que dura el dedo puesto\n");
    printf("     -------+");
    for (d = 0U; d < sizeof k_dur / sizeof k_dur[0]; d++) { printf("--------"); }
    printf("\n");

    for (v = 0U; v < sizeof k_vuelta / sizeof k_vuelta[0]; v++) {
        printf("     %3u ms |", (unsigned)k_vuelta[v]);
        for (d = 0U; d < sizeof k_dur / sizeof k_dur[0]; d++) {
            printf("   %2d/20", cuantos_de_veinte(k_dur[d], k_vuelta[v]));
        }
        printf("\n");
    }
    printf("\n");

    /*
     * EL LISTON, Y DE DONDE SALEN LOS DOS NUMEROS.
     *
     * 120 ms es lo que dura un toque deliberado de los que la gente hace
     * de verdad; lo dice el comentario de la firmeza en touch.c, que
     * cuenta 100-200 ms. 35 ms es el periodo de muestreo que la radio
     * tiene que garantizar: el bucle de STANAG marcaba 65 ms por vuelta y
     * el tactil se mira DOS veces en cada una -ver main.c, el segundo
     * demo_touch_poll() detras de la cadena de polls-, asi que 32,5.
     *
     * Si esta linea deja de estar a 20/20, la radio se come toques y
     * desde fuera parece que no responde. Es lo que paso en STANAG y en
     * IDENT, donde el tactil se miraba una sola vez por vuelta.
     */
    {
        int n = cuantos_de_veinte(120U, 35U);
        int m = cuantos_de_veinte(80U, 35U);
        int c = cuantos_de_veinte(40U, 10U);

        if (n < 20) {
            printf("     *** mirando el tactil cada 35 ms se pierden %d de\n"
                   "         cada 20 toques de 120 ms ***\n", 20 - n);
            fallos++;
        } else {
            printf("     mirado cada 35 ms, los toques de 120 ms salen todos"
                   "  ok\n");
        }
        if (m < 20) {
            printf("     *** y %d de cada 20 de los de 80 ms ***\n", 20 - m);
            fallos++;
        } else {
            printf("     y los de 80 ms tambien  ok\n");
        }
        /*
         * Y EL LISTON DE VERDAD, que es el que pone el gancho del dibujo.
         *
         * touch_latido() mira el tactil cada 8 ms desde DENTRO de cada
         * repintado (gfx2_set_pump()), asi que el periodo real de la radio
         * ya no lo marca el bucle: lo marca ese reloj. A ese ritmo tiene
         * que salir hasta el toque mas corto que hace una persona.
         *
         * Esta linea ademas vigila lo contrario, y de eso ya se ha caido
         * una version: mirar MAS DEPRISA que la firmeza no puede dejar el
         * tactil muerto. La primera version de TOUCH_CONF_MS comparaba el
         * hueco entre dos lecturas en vez del tiempo desde que se poso el
         * dedo, y con 10 ms de periodo no confirmaba NUNCA - esta misma
         * fila salia 0/20 con el dedo puesto trescientos milisegundos.
         */
        if (c < 20) {
            printf("     *** mirando cada 10 ms se pierden %d de cada 20\n"
                   "         toques de 40 ms: la firmeza no puede depender\n"
                   "         de cada cuanto se mire ***\n", 20 - c);
            fallos++;
        } else {
            printf("     y mirando cada 10 ms salen hasta los de 40 ms  ok\n");
        }
    }
}

int main(void)
{
    int paso;
    int umbral_roto = -1;

    printf("\n== EL DEDO QUE ARRASTRA, CONTRA User/touch.c ==\n\n");

    touch_init();
    calibracion_recta();

    /*
     * MEDIDA 0: el banco se cree a si mismo. Si el chip de mentira no
     * contesta bien, todo lo demas es ruido. Dedo quieto en un sitio
     * conocido; el firmware tiene que acabar dando ESE sitio.
     */
    {
        uint16_t x = 0, y = 0;
        int i;
        uint8_t r = 0;
        quita_dedo();
        for (i = 0; i < 4; i++) { r = muestra(400, 240, 20U, &x, &y); }
        printf("  dedo quieto en (400,240) -> contacto=%u en (%u,%u)\n", r, x, y);
        if (!r) { mal("el tactil no ve un dedo quieto: el banco no sirve"); }
        if (x > 402U || x < 398U || y > 242U || y < 238U) {
            mal("el sitio que sale no es donde se puso el dedo");
        }
    }

    /*
     * MEDIDA 1: A QUE VELOCIDAD DEJA DE CREERSE EL DEDO.
     *
     * Con el dedo pegado a la pantalla todo el rato, se barre el paso por
     * muestra. Lo que se cuenta son las veces que el firmware contesta
     * "no hay contacto" en mitad del gesto, que es lo que el mapa lee
     * como "ha levantado el dedo" y le hace soltar el arrastre.
     */
    printf("\n  -- cuantas veces dice \"no hay dedo\" habiendolo --\n");
    printf("  paso/muestra   suelta inventada   muestras seguidas buenas\n");
    for (paso = 5; paso <= 80; paso += 5) {
        int seg = 0;
        int p = arrastre(60, 240, paso, 30, &seg);
        printf("  %3d px %18d %22d%s\n", paso, p, seg,
               (p > 0 && umbral_roto < 0) ? "   <-- aqui se rompe" : "");
        if (p > 0 && umbral_roto < 0) { umbral_roto = paso; }
    }

    if (umbral_roto < 0) {
        printf("\n  El filtro no inventa ninguna suelta en todo el barrido.\n");
    } else {
        printf("\n  A partir de %d px por muestra, el arrastre se corta solo.\n",
               umbral_roto);
    }

    /*
     * MEDIDA 2: Y ESO EN MILIMETROS POR SEGUNDO, QUE ES LO QUE HACE EL DEDO.
     *
     * La pantalla son 800 px en 154 mm de ancho util, o sea 5,19 px/mm. El
     * hueco entre lecturas durante un arrastre del mapa no es libre: cada
     * movimiento repinta el mapa entero -800x230, 184.000 accesos al bus-
     * y eso, al ritmo que ya esta medido en este proyecto (384.000 accesos
     * = 21 ms, ver splash.h), son unos 10 ms SOLO de pintar, mas el resto
     * de la vuelta del bucle.
     */
    if (umbral_roto > 0) {
        double pxmm = 800.0 / 154.0;
        int ms;
        printf("  -- y a que velocidad de dedo corresponde eso --\n");
        for (ms = 10; ms <= 40; ms += 10) {
            double pxs = (double)umbral_roto * 1000.0 / (double)ms;
            printf("     si entre lectura y lectura pasan %2d ms -> %6.0f px/s"
                   " = %5.0f mm/s\n", ms, pxs, pxs / pxmm);
        }
        printf("\n     Un dedo arrastrando algo por una pantalla hace entre 100\n"
               "     y 600 mm/s sin despeinarse.\n");
    }

    /*
     * MEDIDA 3: EL CASO DE VERDAD. El mapa ampliado, que es donde el usuario
     * lo estaba probando. Un arrastre de lado a lado de la pantalla en
     * medio segundo son 800 px en ~25 lecturas de 20 ms: 32 px por
     * muestra. Nada raro.
     */
    {
        int seg = 0;
        int p = arrastre(40, 240, 32, 22, &seg);
        printf("\n  -- el gesto de verdad: media pantalla en medio segundo --\n");
        printf("     32 px por lectura: %d sueltas inventadas, "
               "la racha buena mas larga son %d lecturas de 22\n", p, seg);
        if (p > 0) {
            printf("     Cada una de esas %d es un arrastre que se termina solo\n"
                   "     con el dedo todavia en el cristal.\n", p);
        }
    }

    /*
     * ==================================================================
     * LO QUE TIENE QUE CUMPLIRSE, PASE LO QUE PASE
     * ==================================================================
     *
     * Hasta aqui el banco MIDE. De aqui abajo EXIGE, que es lo unico que
     * hace que mañana siga sirviendo de algo.
     */

    /*
     * (1) UN ARRASTRE NORMAL NO SE CORTA SOLO.
     *
     * "Normal" no es una opinion: la pantalla mide 154 mm de ancho util y
     * un dedo que arrastra algo por ella hace entre 100 y 600 mm/s. El
     * hueco entre lecturas tampoco es una opinion: repintar el mapa son
     * 254.740 accesos al bus, entre 7,6 y 26,7 ms segun los tiempos del
     * EXMC (medido en sim/mapa_pant.c), mas el resto de la vuelta del
     * bucle. Se barre la rejilla entera de esas dos cosas.
     */
    printf("  -- (1) un arrastre de dedo normal, a cualquier ritmo de bucle --\n");
    {
        static const int k_ms[]  = { 10, 15, 20, 25, 30, 40 };
        static const int k_mms[] = { 100, 200, 300, 400, 500, 600 };
        double pxmm = 800.0 / 154.0;
        unsigned i, j;
        int peor = 0, peor_ms = 0, peor_mms = 0;

        for (i = 0; i < sizeof k_ms / sizeof k_ms[0]; i++) {
            for (j = 0; j < sizeof k_mms / sizeof k_mms[0]; j++) {
                int paso = (int)((double)k_mms[j] * pxmm * (double)k_ms[i] / 1000.0);
                int p;
                if (paso < 1) { paso = 1; }
                p = arrastre_ms(40, 240, paso, 700 / (paso ? paso : 1), (uint32_t)k_ms[i], 0);
                if (p > peor) { peor = p; peor_ms = k_ms[i]; peor_mms = k_mms[j]; }
            }
        }
        if (peor == 0) {
            printf("     ninguna suelta inventada en las 36 combinaciones\n");
        } else {
            printf("     lo peor: %d sueltas inventadas a %d mm/s con %d ms "
                   "entre lecturas\n", peor, peor_mms, peor_ms);
            mal("un arrastre de dedo normal se corta solo");
        }
    }

    /*
     * (2) UN ARTEFACTO SUELTO NO MUEVE EL MAPA NI TERMINA EL GESTO.
     *
     * El panel resistivo salta a una esquina de vez en cuando. Eso es lo
     * que el filtro venia a parar, y tiene que seguir parandolo: la
     * lectura mala no se puede creer... pero tampoco se puede contestar
     * "no hay dedo", porque el dedo SIGUE AHI y quien lo lee da el
     * arrastre por terminado.
     */
    printf("  -- (2) un artefacto suelto en mitad del arrastre --\n");
    {
        uint16_t x = 0, y = 0;
        int i;
        uint8_t hay;

        quita_dedo();
        for (i = 0; i < 6; i++) { (void)muestra(400, 240, 20U, &x, &y); }

        /* El salto a la esquina. El dedo de verdad no se ha movido: lo que
         * cambia es lo que el panel contesta. */
        hay = muestra(5, 470, 20U, &x, &y);
        printf("     salto a (5,470): contacto=%u, sitio (%u,%u)\n", hay, x, y);
        if (!hay) { mal("el artefacto se contesta como una suelta"); }
        if (x < 380U || x > 420U || y < 220U || y > 260U) {
            mal("el artefacto se ha creido y el mapa se iria con el");
        }

        /* Y la siguiente, con el dedo donde estaba, tiene que seguir bien. */
        hay = muestra(404, 242, 20U, &x, &y);
        if (!hay || x < 395U || x > 415U) {
            mal("tras el artefacto no se recupera el sitio del dedo");
        }
        quita_dedo();
    }

    /*
     * (3) Y LA ENTRADA SIGUE PIDIENDO CONFIRMACION.
     *
     * Esto es lo que el filtro hacia bien y no se puede perder por el
     * camino. Una sola lectura suelta no es un toque.
     */
    printf("  -- (3) una lectura suelta sigue sin ser un toque --\n");
    {
        uint16_t x, y;
        uint8_t hay;
        quita_dedo();
        touch_set_firmeza(2U);
        hay = muestra(300, 200, 20U, &x, &y);
        if (hay) { mal("una sola lectura se da por toque con firmeza 2"); }
        else { printf("     con firmeza 2, la primera lectura no cuenta\n"); }
        hay = muestra(301, 201, 20U, &x, &y);
        if (!hay) { mal("la segunda lectura seguida tendria que confirmar"); }
        quita_dedo();
    }

    /*
     * (4) Y SOLTAR SE NOTA EN LA PRIMERA.
     *
     * Lo dice el comentario de touch.c y tiene que seguir siendo verdad:
     * "soltar tarde es peor que soltar pronto".
     */
    printf("  -- (4) levantar el dedo se nota en la primera lectura --\n");
    {
        uint16_t x, y;
        int i;
        quita_dedo();
        for (i = 0; i < 6; i++) { (void)muestra(400, 240, 20U, &x, &y); }
        s_dedo_x = -1; s_dedo_y = -1;
        g_msticks += 20U;
        if (touch_read(&x, &y)) { mal("el dedo levantado sigue dando contacto"); }
        else { printf("     sin dedo, contacto=0 a la primera\n"); }
    }

    mide_toques();

    printf("\n");
    if (fallos == 0) {
        printf("TODO CORRECTO: el banco mide el tactil de verdad.\n\n");
        return 0;
    }
    printf("%d FALLOS\n\n", fallos);
    return 1;
}
