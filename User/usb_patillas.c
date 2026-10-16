/*
 * Ver usb_patillas.h para que se mide, por que, y que se decide con el
 * numero que sale.
 */
#include "usb_patillas.h"
#include "gd32f4xx.h"

#define PAT_DM  GPIO_PIN_11   /* USBFS D-, patilla 72 */
#define PAT_DP  GPIO_PIN_12   /* USBFS D+, patilla 73 */

static uint8_t s_puesto = 0U;

void usb_pat_init(void)
{
    /*
     * Ahora es el aparcado. Antes dejaba las dos con bajada puesta, que es
     * lo que usb_pat_lee() se encarga de poner y quitar; ver alli.
     */
    usb_pat_parque();
}

uint8_t usb_pat_lee(void)
{
    uint8_t r = 0U;

    if (s_puesto == 0U) { usb_pat_init(); }

    /*
     * Se pone el modo en cada lectura a proposito. dfu_salto.c deja PA12
     * como SALIDA a cero -es como obliga al ordenador a volver a enumerar
     * antes de saltar al cargador-, y si el salto se cancela, la patilla se
     * queda asi. Una salida a cero leeria 0 pase lo que pase, o sea que la
     * fila diria "nada" con el pincho dentro, para siempre, y sin que nada
     * avisara.
     *
     * Y SE REPONE DESPUES, QUE ES LO QUE FALTABA - 09/10/2026.
     *
     * La primera version dejaba la bajada puesta y se iba. Mientras PA11 y
     * PA12 no fueran mas que dos patillas sueltas eso daba igual; en cuanto
     * se suelden los dos cables del mod son LAS LINEAS DE DATOS del USB, y
     * la pantalla de Informacion se repinta dos veces por segundo: la fila
     * dejaria 40 kOhm colgando de D+ y D- de forma permanente, puestos por
     * una fila que solo venia a mirar.
     *
     * Cuarenta kilohmios no tumban un USB de velocidad plena -con los 15 k
     * del anfitrion en paralelo salen 10,9 k, y contra los 1,5 k del
     * dispositivo el reposo se queda en 2,9 V, muy por encima de los 2,0 que
     * pide la norma-. Pero es una carga que nadie pidio, puesta a espaldas
     * del periferico que manda ahi, y de las que un dia explican un fallo
     * raro. Se repone el puerto entero, igual que en el barrido.
     */
    {
        uint32_t ctl = GPIO_CTL(GPIOA);
        uint32_t pud = GPIO_PUD(GPIOA);

        gpio_mode_set(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN,
                      PAT_DM | PAT_DP);

        if (gpio_input_bit_get(GPIOA, PAT_DP) != RESET) {
            r |= (uint8_t)USB_PAT_DP;
        }
        if (gpio_input_bit_get(GPIOA, PAT_DM) != RESET) {
            r |= (uint8_t)USB_PAT_DM;
        }

        GPIO_CTL(GPIOA) = ctl;
        GPIO_PUD(GPIOA) = pud;
    }
    return r;
}

void usb_pat_parque(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);

    /*
     * EL ESTADO DE REPOSO, DICHO EN VOZ ALTA.
     *
     * Despues del reset PA9, PA10, PA11 y PA12 ya son entradas sin
     * resistencia: esto no cambia nada el dia que se escribe. Esta aqui
     * para que el estado de reposo de las cuatro sea una LINEA DE CODIGO y
     * no una propiedad heredada del reset que nadie ha escrito en ningun
     * sitio -y que el primero que añada un init se lleva por delante sin
     * enterarse-.
     *
     * Sin resistencia y no con bajada: en cuanto esten los cables del mod,
     * las cuatro son lineas de datos de un USB y lo que toca colgar de ellas
     * es nada.
     */
    gpio_mode_set(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_NONE,
                  GPIO_PIN_9 | GPIO_PIN_10 | PAT_DM | PAT_DP);
    s_puesto = 1U;
}

/* ------------------------------------------------------------------ *
 * EL BARRIDO. Ver la cabecera para la lista y de donde sale.
 * ------------------------------------------------------------------ */

/* Un byte por patilla: los 3 bits de arriba el puerto (0=A..4=E) y los 5
 * de abajo el numero. Cabe en 27 bytes de flash. */
#define PAT(p, n)  ((uint8_t)(((p) << 5) | (n)))

static const uint8_t k_sueltas[] = {
    /*
     * PA9 VA EN LA LISTA AUNQUE EL FIRMWARE LA USE.
     *
     * Es la consola serie (debug_uart.c), o sea que normalmente esta como
     * salida y leerla no diria nada. Pero el usuario marco en la foto DOS
     * pastillas juntas, la 7a y la 8a de ese lado, y por el recuento esas
     * son PA10 y PA9: si solo se barriera PA10 saldria una de las dos y la
     * otra quedaria sin confirmar.
     *
     * Soltarla medio milisegundo no rompe nada -la consola va apagada por
     * omision, y aunque este encendida lo peor que pasa es un caracter
     * sucio- y el CTL/PUD de todo el puerto se repone despues, con su
     * seleccion de funcion alternativa intacta porque el AFSEL no se toca.
     */
    PAT(0,  9),
    PAT(0,  0), PAT(0,  4), PAT(0, 10), PAT(0, 15),
    PAT(1,  0), PAT(1,  1), PAT(1,  2), PAT(1, 10),
    PAT(2,  0), PAT(2,  1), PAT(2,  2), PAT(2,  3), PAT(2,  4), PAT(2,  5),
    PAT(2,  8), PAT(2, 10), PAT(2, 11), PAT(2, 12), PAT(2, 13),
    PAT(3,  0), PAT(3,  1), PAT(3,  2), PAT(3,  3), PAT(3,  4), PAT(3,  5),
    PAT(3,  6)
};
/*
 * El puerto 3 de la lista es GPIOE, no GPIOD: el D entero se lo lleva el
 * bus de la pantalla y no queda ni una suelta, asi que no esta en la
 * tabla. Esta nota existe porque "3" invita a leer "D".
 */
static const uint32_t k_puertos[5] = { GPIOA, GPIOB, GPIOC, GPIOE, 0U };
static const char     k_letras[5]  = { 'A', 'B', 'C', 'E', '?' };

#define PAT_N  ((uint8_t)(sizeof(k_sueltas) / sizeof(k_sueltas[0])))

static void pat_espera(void)
{
    /* La bajada son unos 40 kOhm y lo que cuelga de la patilla unos pocos
     * picofaradios: la constante de tiempo no llega al microsegundo. Diez
     * mil vueltas a 200 MHz son dos ordenes de magnitud mas. */
    volatile uint32_t i;

    for (i = 0U; i < 10000U; i++) { __NOP(); }
}

uint8_t usb_pat_barre(char *texto, uint8_t n)
{
    uint32_t ctl[4], pud[4];
    uint8_t  i, hallados = 0U, escritos = 0U, j = 0U;

    if ((texto == 0) || (n == 0U)) { return 0U; }
    texto[0] = '\0';

    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOE);

    /*
     * SE GUARDAN LOS DOS REGISTROS ENTEROS, NO PATILLA A PATILLA.
     *
     * Cada patilla son dos bits en CTL y otros dos en PUD, y hay que
     * reponer los dos. Guardando el registro completo de los cuatro
     * puertos -cuatro palabras- la reposicion es exacta por construccion:
     * no hay forma de reponer una mal porque no se reponen una a una.
     *
     * Y hace falta que sea exacta: aunque ninguna de las 26 la configure
     * el firmware, el CARGADOR si pudo dejar alguna puesta, y este barrido
     * corre con la radio en marcha.
     */
    for (i = 0U; i < 4U; i++) {
        ctl[i] = GPIO_CTL(k_puertos[i]);
        pud[i] = GPIO_PUD(k_puertos[i]);
    }

    for (i = 0U; i < PAT_N; i++) {
        uint8_t  p = (uint8_t)(k_sueltas[i] >> 5);
        uint8_t  b = (uint8_t)(k_sueltas[i] & 0x1FU);
        uint32_t puerto = k_puertos[p];

        gpio_mode_set(puerto, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN,
                      (uint32_t)1U << b);
    }

    pat_espera();

    for (i = 0U; i < PAT_N; i++) {
        uint8_t  p = (uint8_t)(k_sueltas[i] >> 5);
        uint8_t  b = (uint8_t)(k_sueltas[i] & 0x1FU);
        uint32_t puerto = k_puertos[p];

        if ((GPIO_ISTAT(puerto) & ((uint32_t)1U << b)) == 0U) { continue; }

        hallados++;
        if (escritos >= USB_PAT_MAX_NOMBRES) { continue; }
        /* "PC10 " son cinco bytes; se escribe solo si cabe entero. */
        if ((uint8_t)(j + 6U) > n) { continue; }
        if (j != 0U) { texto[j++] = ' '; }
        texto[j++] = 'P';
        texto[j++] = k_letras[p];
        if (b >= 10U) { texto[j++] = (char)('0' + (b / 10U)); }
        texto[j++] = (char)('0' + (b % 10U));
        texto[j] = '\0';
        escritos++;
    }

    /* Y se deja todo como estaba, hayamos encontrado algo o no. */
    for (i = 0U; i < 4U; i++) {
        GPIO_CTL(k_puertos[i]) = ctl[i];
        GPIO_PUD(k_puertos[i]) = pud[i];
    }

    return hallados;
}
