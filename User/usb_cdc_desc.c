/*
 * Ver usb_cdc_desc.h para por que esto son bytes, que describe y por que
 * hacen falta las dos asociaciones.
 *
 * NO HAY NI UN #include DEL GD32 AQUI, Y ES A PROPOSITO: asi el banco del
 * ordenador compila ESTE fichero y no una copia suya.
 */
#include "usb_cdc_desc.h"

/*
 * EL IDENTIFICADOR DEL FABRICANTE.
 *
 * 0x28E9 es el mismo fabricante que ya usa el cargador para su disco; el
 * producto cambia, 0x018B en vez de 0x018A. Dos identificadores distintos
 * para dos cosas distintas -un disco y esto- es lo correcto, y ademas
 * Windows cachea el driver por la pareja: compartirla haria que el puerto
 * heredara el driver del disco.
 *
 * Esto NO es un identificador comprado al USB-IF. Para un cacharro que no
 * se vende no hace falta, pero conviene que este escrito.
 */
#define VID_LO  0xE9U
#define VID_HI  0x28U
#define PID_LO  0x8BU
#define PID_HI  0x01U

const uint8_t usb_cdc_desc_dev[USB_CDC_DESC_DEV_LEN] = {
    18U,
    0x01U,          /* dispositivo */
    0x00U, 0x02U,   /* bcdUSB 2.00 */
    /*
     * 0xEF / 0x02 / 0x01 = "Miscellaneous / Common Class / Interface
     * Association". Esto NO es decoracion: es lo que le dice al anfitrion
     * que hay varias funciones en el mismo equipo y que mire los
     * descriptores de asociacion. Dejandolo en 0x02 (CDC) -que es lo que
     * valia cuando aqui solo habia un puerto serie- Windows no monta el
     * COM en cuanto aparece la tarjeta de sonido, y no da ningun error.
     */
    0xEFU, 0x02U, 0x01U,
    64U,            /* bMaxPacketSize0 */
    VID_LO, VID_HI,
    PID_LO, PID_HI,
    0x00U, 0x01U,   /* bcdDevice 1.00 */
    0x01U, 0x02U, 0x03U,
    0x01U           /* una configuracion */
};

/* El total de los descriptores de la interfaz de control de audio: su
 * cabecera (9) + el terminal de entrada (12) + el de salida (9). */
#define AC_TOTAL  30U

const uint8_t usb_cdc_desc_config[USB_CDC_DESC_CONFIG_LEN] = {
    /* ================= configuracion ================= */
    9U, 0x02U,
    (uint8_t)(USB_CDC_DESC_CONFIG_LEN & 0xFFU),
    (uint8_t)(USB_CDC_DESC_CONFIG_LEN >> 8),
    0x04U,          /* cuatro interfaces: dos del serie, dos del audio */
    0x01U,          /* bConfigurationValue */
    0x00U,
    0xC0U,          /* alimentacion propia */
    0x32U,          /* 100 mA. La radio tiene bateria, pero declarar 0 no
                     * esta permitido y declarar de mas hace que un
                     * concentrador flojo rechace el equipo. */

    /* ========== asociacion: el puerto serie son la 0 y la 1 ========== */
    8U, 0x0BU,
    0x00U,          /* primera interfaz */
    0x02U,          /* cuantas */
    0x02U, 0x02U, 0x01U,   /* CDC / ACM / ordenes AT */
    0x00U,

    /* ---- interfaz 0: CDC Communication ---- */
    9U, 0x04U,
    0x00U, 0x00U,
    0x01U,          /* un endpoint: el de interrupcion */
    0x02U, 0x02U, 0x01U,
    0x00U,

    /* ---- los cuatro funcionales de CDC ---- */
    5U, 0x24U, 0x00U, 0x10U, 0x01U,   /* Header, CDC 1.10 */
    5U, 0x24U, 0x01U, 0x00U, 0x01U,   /* Call Management: no la hace, y los
                                       * datos van por la interfaz 1 */
    4U, 0x24U, 0x02U, 0x02U,          /* ACM: admite Set/Get_Line_Coding */
    5U, 0x24U, 0x06U, 0x00U, 0x01U,   /* Union: manda la 0, esclava la 1 */

    /* ---- endpoint de interrupcion ----
     * No se usa nunca: un receptor no tiene portadora que anunciar ni
     * errores de trama que contar. Pero tiene que estar, y con el intervalo
     * mas lento que admite para que provoque el minimo sondeo. */
    7U, 0x05U, USB_CDC_EP_NOTIF, 0x03U, 8U, 0x00U, 0xFFU,

    /* ---- interfaz 1: CDC Data ---- */
    9U, 0x04U,
    0x01U, 0x00U,
    0x02U,
    0x0AU, 0x00U, 0x00U,
    0x00U,

    /* del ordenador a la radio, y de la radio al ordenador */
    7U, 0x05U, USB_CDC_EP_OUT, 0x02U, USB_CDC_EP_TAM, 0x00U, 0x00U,
    7U, 0x05U, USB_CDC_EP_IN,  0x02U, USB_CDC_EP_TAM, 0x00U, 0x00U,

    /* ========== asociacion: el audio son la 2 y la 3 ========== */
    8U, 0x0BU,
    0x02U, 0x02U,
    0x01U, 0x00U, 0x00U,   /* Audio */
    0x00U,

    /* ---- interfaz 2: Audio Control, sin endpoints ---- */
    9U, 0x04U,
    0x02U, 0x00U,
    0x00U,
    0x01U, 0x01U, 0x00U,   /* Audio / AudioControl */
    0x00U,

    /* cabecera de control: ADC 1.00, y la unica interfaz de flujo es la 3 */
    9U, 0x24U, 0x01U,
    0x00U, 0x01U,
    (uint8_t)(AC_TOTAL & 0xFFU), (uint8_t)(AC_TOTAL >> 8),
    0x01U,          /* una interfaz de flujo */
    0x03U,          /* y es la 3 */

    /*
     * Terminal de ENTRADA, tipo 0x0710 = "Radio Receiver".
     *
     * El tipo no es cosmetico del todo: Windows lo enseña al usuario y hay
     * programas que lo miran para separar entradas. 0x0710 es exactamente
     * lo que esto es, y se distingue de un microfono (0x0201) en la lista
     * de dispositivos de grabacion, que es donde el que use WSJT-X va a
     * tener que elegirlo entre todo lo que tenga enchufado.
     */
    12U, 0x24U, 0x02U,
    0x01U,          /* identificador del terminal */
    0x10U, 0x07U,   /* Radio Receiver */
    0x00U,
    (uint8_t)USB_AUD_CANALES,
    0x03U, 0x00U,   /* izquierda + derecha */
    0x00U, 0x00U,

    /* Terminal de SALIDA: 0x0101 = flujo USB, alimentado del terminal 1 */
    9U, 0x24U, 0x03U,
    0x02U,
    0x01U, 0x01U,
    0x00U,
    0x01U,          /* viene del terminal de entrada */
    0x00U,

    /*
     * ---- interfaz 3, ajuste 0: SIN endpoints ----
     *
     * Obligatoria, y no es de adorno. El anfitrion la deja puesta mientras
     * nadie grabe, y asi el ancho de banda isocrono -que se reserva al
     * configurar, no al usar- no se gasta. Sin ella el equipo reservaria
     * sus 196 bytes por milisegundo desde que se enchufa.
     */
    9U, 0x04U,
    0x03U, 0x00U,
    0x00U,
    0x01U, 0x02U, 0x00U,
    0x00U,

    /* ---- interfaz 3, ajuste 1: con el isocrono ---- */
    9U, 0x04U,
    0x03U, 0x01U,
    0x01U,
    0x01U, 0x02U, 0x00U,
    0x00U,

    /*
     * General de flujo: va al terminal 2, un retardo, formato PCM.
     *
     * 0x24 (CS_INTERFACE) Y NO 0x25 (CS_ENDPOINT), que es lo que puse la
     * primera vez. Los dos valen 7 bytes y los dos llevan el subtipo 0x01,
     * asi que se parecen muchisimo - pero este va DENTRO de la interfaz y
     * ANTES del endpoint, y el otro va detras del endpoint y habla de el.
     *
     * Con 0x25 aqui, el anfitrion ve un descriptor de endpoint sin ningun
     * endpoint al que pegarse y abandona la configuracion. No da error: el
     * equipo se queda a medio enumerar y no sale ningun COM. Costo una
     * version entera.
     */
    7U, 0x24U, 0x01U,
    0x02U,
    0x01U,
    0x01U, 0x00U,

    /* formato de tipo I: 2 canales, 16 bits, una sola frecuencia */
    11U, 0x24U, 0x02U,
    0x01U,
    (uint8_t)USB_AUD_CANALES,
    (uint8_t)(USB_AUD_BITS / 8U),
    (uint8_t)USB_AUD_BITS,
    0x01U,          /* una frecuencia y no un rango */
    (uint8_t)(USB_AUD_HZ & 0xFFUL),
    (uint8_t)((USB_AUD_HZ >> 8) & 0xFFUL),
    (uint8_t)((USB_AUD_HZ >> 16) & 0xFFUL),

    /*
     * ---- el endpoint isocrono ----
     *
     * Mide NUEVE bytes y no siete: los de audio llevan dos campos de mas
     * (bRefresh y bSynchAddress). Poner 7 aqui es el error clasico, y lo
     * que pasa es que el anfitrion lee los dos bytes siguientes como si
     * fueran el principio del descriptor de despues.
     *
     * 0x05 = isocrono + asincrono. Asincrono porque el reloj de la radio es
     * suyo y no se ajusta al del ordenador: es el propio cristal de
     * 12,288 MHz. Declararlo "adaptativo" seria prometer que seguimos su
     * reloj, y no lo hacemos.
     */
    9U, 0x05U,
    USB_AUD_EP_IN,
    0x05U,
    (uint8_t)(USB_AUD_EP_TAM & 0xFFU), (uint8_t)(USB_AUD_EP_TAM >> 8),
    0x01U,          /* una trama por milisegundo */
    0x00U, 0x00U,

    /* general de endpoint de audio */
    7U, 0x25U, 0x01U,
    0x00U, 0x00U, 0x00U, 0x00U
};

/*
 * Y QUE EL ARRAY MIDA LO QUE DICE MEDIR.
 *
 * wTotalLength sale del mismo define que el tamaño del array, asi que no
 * pueden discrepar. Lo que si puede pasar -y es lo facil escribiendo 174
 * bytes a mano- es que al añadir o quitar un descriptor el define se quede
 * como estaba. Entonces el array sale de otro tamaño y esto lo para aqui,
 * en vez de en un equipo que no enumera.
 */
_Static_assert(sizeof(usb_cdc_desc_config) == USB_CDC_DESC_CONFIG_LEN,
               "el descriptor de configuracion no mide lo que dice");
_Static_assert(sizeof(usb_cdc_desc_dev) == USB_CDC_DESC_DEV_LEN,
               "el descriptor de dispositivo no mide lo que dice");
