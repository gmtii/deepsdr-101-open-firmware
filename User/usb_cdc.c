/*
 * El puerto serie por USB. Ver usb_cdc.h para por que se arranca a mano.
 */
#include "drv_usb_core.h"
#include "drv_usb_dev.h"
#include "usbd_core.h"
#include "usbd_enum.h"
#include "drv_usb_hw.h"    /* usb_intr_config, usb_mdelay */
#include "drv_usbd_int.h"  /* usbd_isr */
#include "usb_cdc.h"
#include "usb_cdc_hw.h"
#include "usb_cdc_desc.h"
#include "usb_cdc_anillo.h"
#include "cat.h"
#include <string.h>

/* ------------------------------------------------------------------ *
 * Estado
 * ------------------------------------------------------------------ */

usb_core_driver g_usb_cdc;
static uint8_t  s_arrancado;
static uint8_t  s_parado;      /* montado, pero desconectado a mano */
static volatile uint32_t s_isrs;
static volatile uint32_t s_perdidos;

static anillo_t s_hacia_pc;    /* lo escribe el bucle, lo vacia la ISR */
static anillo_t s_desde_pc;    /* lo llena la ISR, lo lee el bucle */
static uint8_t  s_buf_hacia[256];
static uint8_t  s_buf_desde[256];

/*
 * EL ANILLO DEL AUDIO, Y POR QUE MIDE DIEZ VECES MAS.
 *
 * Por aqui pasan 192 bytes CADA MILISEGUNDO y no se puede llegar tarde: una
 * trama que no esta lista cuando el anfitrion la pide es un hueco que se
 * oye. Lo que lo llena es la interrupcion del audio, en bloques; lo que lo
 * vacia es la interrupcion del USB, trama a trama. Entre un bloque y el
 * siguiente hay que tener de sobra.
 *
 * 2048 bytes son 10,6 ms a 48 kHz estereo. El bloque de audio de esta radio
 * son 256 muestras a 96 kHz, o sea 2,7 ms; con cuatro bloques de colchon
 * aguanta un fotograma lento sin dejar hueco.
 */
#define AUD_ANILLO_TAM  2048U
static anillo_t s_audio;
static uint8_t  s_buf_audio[AUD_ANILLO_TAM];
static volatile uint8_t  s_audio_on;     /* el PC ha pedido el ajuste 1 */
static volatile uint32_t s_audio_huecos; /* tramas vacias DESPUES de arrancar */
static volatile uint8_t  s_audio_vivo;   /* ya ha salido audio de verdad */
static uint8_t  s_tx_aud[USB_AUD_EP_TAM];

static uint8_t  s_rx[USB_CDC_EP_TAM];
static uint8_t  s_tx[USB_CDC_EP_TAM];
static volatile uint8_t s_tx_ocupado;

/*
 * LA CONFIGURACION DE LINEA QUE PIDE EL ORDENADOR, Y QUE NO SIRVE PARA NADA.
 *
 * Windows y Linux mandan SET_LINE_CODING con baudios, bits y paridad en
 * cuanto se abre el puerto, y GET_LINE_CODING para leerlo de vuelta. Aqui
 * detras no hay un UART: los bytes van por USB a 12 Mbit/s pase lo que
 * pase. Pero hay que GUARDARLO y devolver lo mismo que mandaron, porque
 * hay programas -hamlib entre ellos- que configuran el puerto y luego
 * comprueban que se quedo configurado; si se les devuelve otra cosa, dan el
 * puerto por roto.
 */
static uint8_t s_linea[7] = { 0x00U, 0xC2U, 0x01U, 0x00U, /* 115200 */
                              0x00U, 0x00U, 0x08U };      /* 1 stop, sin
                                                           * paridad, 8 bits */

/*
 * LAS PETICIONES DE LA CLASE DE AUDIO.
 *
 * Windows le pregunta al endpoint isocrono por su frecuencia de muestreo
 * aunque el descriptor diga que no se puede cambiar (bmAttributes a 0 en el
 * general de endpoint). Contestando STALL, el driver de audio da el
 * dispositivo por roto y no sale como entrada de grabacion - el puerto
 * serie sobrevive, porque son dos funciones distintas, asi que el sintoma
 * seria "el COM va y el microfono no".
 *
 * Son tres bytes en little endian con los hercios. Se contesta lo que
 * decimos en el descriptor y se acepta cualquier intento de cambiarla sin
 * hacer nada: es la unica que hay.
 */
#define AUD_GET_CUR                0x81U
#define AUD_SET_CUR                0x01U
#define AUD_CS_SAMPLING_FREQ       0x01U

static uint8_t s_aud_hz[3] = {
    (uint8_t)(USB_AUD_HZ & 0xFFUL),
    (uint8_t)((USB_AUD_HZ >> 8) & 0xFFUL),
    (uint8_t)((USB_AUD_HZ >> 16) & 0xFFUL)
};

/*
 * Y UN SITIO APARTE DONDE TIRAR LO QUE MANDE EL ORDENADOR.
 *
 * La primera version usaba s_aud_hz tambien como destino del SET_CUR, y eso
 * es un error: si el ordenador intentara poner 44.100, el GET_CUR
 * siguiente contestaria 44.100 mientras el isocrono sigue mandando 48.000.
 * Mentiria sobre si mismo, y el que lo oyera distorsionado no tendria
 * donde mirar.
 *
 * Asi s_aud_hz es siempre la verdad -la misma constante que esta en el
 * descriptor- y lo que llega se acepta y se tira.
 */
static uint8_t s_aud_tira[3];

#define CDC_SET_LINE_CODING        0x20U
#define CDC_GET_LINE_CODING        0x21U
#define CDC_SET_CONTROL_LINE_STATE 0x22U

/* ------------------------------------------------------------------ *
 * Descriptores
 * ------------------------------------------------------------------ */

static const usb_desc_LANGID k_idioma = {
    .header = { .bLength = sizeof(usb_desc_LANGID),
                .bDescriptorType = USB_DESCTYPE_STR },
    .wLANGID = ENG_LANGID
};

#define STR(n) \
    { .header = { .bLength = (uint8_t)(2U + (2U * (sizeof(n) - 1U))), \
                  .bDescriptorType = USB_DESCTYPE_STR } }

/* Las cadenas van en UTF-16, o sea dos bytes por caracter. Se montan a mano
 * para no arrastrar un macro variadico. */
static const usb_desc_str k_fabricante = {
    .header = { .bLength = 2U + (2U * 7U), .bDescriptorType = USB_DESCTYPE_STR },
    .unicode_string = { 'D','E','E','P','S','D','R' }
};
/*
 * "DeepSDR Audio", a peticion del usuario el 09/10/2026.
 *
 * Es lo que sale entre parentesis en el panel de sonido de Windows y en la
 * lista de entradas de WSJT-X. Lo de delante -"Receptor de radio"- no sale
 * de aqui: lo pone Windows traduciendo el TIPO de terminal que declara el
 * descriptor (0x0710, Radio Receiver), y eso conviene dejarlo como esta
 * porque es lo que lo distingue de un microfono en una lista llena de
 * microfonos.
 *
 * La cuenta de la longitud va a mano y es facil equivocarse -son dos bytes
 * por caracter mas la cabecera-, asi que hay un aserto debajo que la saca
 * del propio array.
 */
#define PROD_TXT  { 'D','e','e','p','S','D','R',' ','A','u','d','i','o' }
static const usb_desc_str k_producto = {
    .header = { .bLength = 2U + (2U * 13U), .bDescriptorType = USB_DESCTYPE_STR },
    .unicode_string = PROD_TXT
};
_Static_assert(sizeof((uint16_t[])PROD_TXT) / sizeof(uint16_t) == 13U,
               "la longitud del nombre del producto no cuadra con el texto");
static const usb_desc_str k_serie = {
    .header = { .bLength = 2U + (2U * 8U), .bDescriptorType = USB_DESCTYPE_STR },
    .unicode_string = { '0','0','0','0','0','0','0','1' }
};

static void *const k_cadenas[] = {
    (void *)&k_idioma,
    (void *)&k_fabricante,
    (void *)&k_producto,
    (void *)&k_serie
};

static usb_desc k_desc = {
    .dev_desc    = (uint8_t *)usb_cdc_desc_dev,
    .config_desc = (uint8_t *)usb_cdc_desc_config,
    .bos_desc    = NULL,
    .strings     = k_cadenas
};

/* ------------------------------------------------------------------ *
 * Los endpoints, como estructura
 * ------------------------------------------------------------------ *
 *
 * usbd_ep_setup() quiere la estructura del fabricante, no bytes. Esto NO es
 * una segunda copia del descriptor: lo que sale por el cable sigue siendo
 * el array de usb_cdc_desc.c, y esto solo configura el periferico. Los
 * numeros salen de las mismas constantes, asi que no se pueden separar.
 */
static const usb_desc_ep k_ep_notif = {
    .header = { .bLength = 7U, .bDescriptorType = USB_DESCTYPE_EP },
    .bEndpointAddress = USB_CDC_EP_NOTIF,
    .bmAttributes     = 0x03U,
    .wMaxPacketSize   = 8U,
    .bInterval        = 0xFFU
};
static const usb_desc_ep k_ep_in = {
    .header = { .bLength = 7U, .bDescriptorType = USB_DESCTYPE_EP },
    .bEndpointAddress = USB_CDC_EP_IN,
    .bmAttributes     = 0x02U,
    .wMaxPacketSize   = USB_CDC_EP_TAM,
    .bInterval        = 0x00U
};
static const usb_desc_ep k_ep_aud = {
    .header = { .bLength = 9U, .bDescriptorType = USB_DESCTYPE_EP },
    .bEndpointAddress = USB_AUD_EP_IN,
    .bmAttributes     = 0x05U,          /* isocrono, asincrono */
    .wMaxPacketSize   = USB_AUD_EP_TAM,
    .bInterval        = 0x01U
};
static const usb_desc_ep k_ep_out = {
    .header = { .bLength = 7U, .bDescriptorType = USB_DESCTYPE_EP },
    .bEndpointAddress = USB_CDC_EP_OUT,
    .bmAttributes     = 0x02U,
    .wMaxPacketSize   = USB_CDC_EP_TAM,
    .bInterval        = 0x00U
};

/* ------------------------------------------------------------------ *
 * La clase
 * ------------------------------------------------------------------ */

static void audio_manda(usb_dev *udev);   /* definida mas abajo, junto al
                                           * resto de lo del audio */

static uint8_t cdc_init(usb_dev *udev, uint8_t config_index)
{
    (void)config_index;

    usbd_ep_setup(udev, &k_ep_notif);
    usbd_ep_setup(udev, &k_ep_in);
    usbd_ep_setup(udev, &k_ep_out);

    anillo_reset(&s_hacia_pc);
    anillo_reset(&s_desde_pc);
    anillo_reset(&s_audio);
    s_tx_ocupado = 0U;
    s_audio_on = 0U;
    cat_reset();

    /* Queda a la escucha. Sin esto el ordenador puede escribir y nadie lo
     * recoge: el endpoint se queda NAKeando y el puerto parece colgado. */
    usbd_ep_recev(udev, USB_CDC_EP_OUT, s_rx, USB_CDC_EP_TAM);
    return USBD_OK;
}

static uint8_t cdc_deinit(usb_dev *udev, uint8_t config_index)
{
    (void)config_index;

    usbd_ep_clear(udev, USB_CDC_EP_NOTIF);
    usbd_ep_clear(udev, USB_CDC_EP_IN);
    usbd_ep_clear(udev, USB_CDC_EP_OUT);
    return USBD_OK;
}

static uint8_t cdc_req(usb_dev *udev, usb_req *req)
{
    /*
     * Lo del audio va PRIMERO porque se distingue por el destinatario, no
     * por el numero: AUD_SET_CUR es 0x01, y 0x01 tambien es un numero de
     * peticion normal en otros contextos. Lo que lo identifica es que va
     * dirigido a un ENDPOINT (los dos bits de abajo del tipo valen 2) y no
     * a una interfaz.
     */
    if ((req->bmRequestType & 0x1FU) == 0x02U) {
        if ((uint8_t)(req->wValue >> 8) != AUD_CS_SAMPLING_FREQ) {
            return USBD_FAIL;
        }
        if (req->bRequest == AUD_GET_CUR) {
            udev->dev.transc_in[0].xfer_buf = s_aud_hz;
            udev->dev.transc_in[0].remain_len = 3U;
            return USBD_OK;
        }
        if (req->bRequest == AUD_SET_CUR) {
            /* Se acepta y se tira: solo hay una frecuencia, la que dice el
             * descriptor. Aceptarla es lo correcto - rechazarla hace que
             * algunos programas dejen de abrir la grabacion-, y hacerle
             * caso seria mentir, porque el reloj de aqui no se mueve. */
            udev->dev.transc_out[0].xfer_buf = s_aud_tira;
            udev->dev.transc_out[0].remain_len = 3U;
            return USBD_OK;
        }
        return USBD_FAIL;
    }

    switch (req->bRequest) {
    case CDC_SET_LINE_CODING:
        /* Los siete bytes llegan en la fase de datos; el nucleo los deja
         * en el buffer de control y avisa por ctlx_out. Aqui solo se dice
         * donde ponerlos. */
        udev->dev.transc_out[0].xfer_buf = s_linea;
        udev->dev.transc_out[0].remain_len = 7U;
        return USBD_OK;

    case CDC_GET_LINE_CODING:
        udev->dev.transc_in[0].xfer_buf = s_linea;
        udev->dev.transc_in[0].remain_len = 7U;
        return USBD_OK;

    case CDC_SET_CONTROL_LINE_STATE:
        /*
         * DTR y RTS. Un puerto de verdad los usaria para el control de
         * flujo; aqui no hay nada que controlar. Se acepta y ya.
         *
         * Lo que NO se hace es esperar a que el ordenador levante DTR para
         * empezar a contestar: hay programas que no lo tocan nunca, y una
         * radio que solo habla con DTR levantado parece muerta con la mitad
         * de ellos.
         */
        return USBD_OK;

    default:
        break;
    }
    return USBD_FAIL;
}

/* Lo que acaba de salir hacia el ordenador: se intenta mandar mas. */
static uint8_t cdc_in(usb_dev *udev, uint8_t ep_num)
{
    uint16_t n;

    /*
     * Dos endpoints de entrada llegan por aqui y hay que separarlos: el
     * bulk del puerto serie y el isocrono del audio. `ep_num` viene sin el
     * bit de direccion, asi que se compara con la parte baja.
     *
     * Sin esta separacion, una trama de audio terminada sacaria bytes del
     * anillo del CAT y los mandaria por el endpoint del audio: el puerto
     * perderia sus respuestas y el audio sonaria a texto.
     */
    if ((ep_num & 0x7FU) == (USB_AUD_EP_IN & 0x7FU)) {
        if (s_audio_on != 0U) { audio_manda(udev); }
        return USBD_OK;
    }

    (void)ep_num;
    n = anillo_saca(&s_hacia_pc, s_tx, USB_CDC_EP_TAM);
    if (n > 0U) {
        usbd_ep_send(udev, USB_CDC_EP_IN, s_tx, n);
    } else {
        s_tx_ocupado = 0U;
    }
    return USBD_OK;
}

/* Lo que acaba de llegar del ordenador. */
static uint8_t cdc_out(usb_dev *udev, uint8_t ep_num)
{
    uint32_t n = usbd_rxcount_get(udev, USB_CDC_EP_OUT);

    (void)ep_num;
    if (n > 0UL) {
        if (n > USB_CDC_EP_TAM) { n = USB_CDC_EP_TAM; }
        /*
         * Si no cabe, se tira lo que sobra y se cuenta. Es lo correcto: el
         * que escribe es un programa mandando ordenes de texto, y una orden
         * a medias la rechaza el analizador del CAT con "?;" en vez de
         * hacer algo raro. Lo que no se puede hacer es bloquear aqui: esto
         * es la interrupcion del USB.
         */
        uint16_t puestos = anillo_mete(&s_desde_pc, s_rx, (uint16_t)n);

        if (puestos < (uint16_t)n) {
            s_perdidos += (uint32_t)n - puestos;
        }
    }
    usbd_ep_recev(udev, USB_CDC_EP_OUT, s_rx, USB_CDC_EP_TAM);
    return USBD_OK;
}

/*
 * UNA TRAMA DE AUDIO HACIA EL ORDENADOR.
 *
 * Se manda SIEMPRE, haya datos o no. Un endpoint isocrono no se puede
 * saltar una trama: el anfitrion la pide cada milisegundo y lo que no se
 * conteste es un hueco en el flujo. Si el anillo esta vacio sale silencio y
 * se cuenta, que es muchisimo mejor que un chasquido - y el contador deja
 * dicho que paso, en vez de que el usuario oiga algo raro y no haya numero
 * que mirar.
 */
/* La trama nominal: 48.000 muestras por segundo, dos canales, dos bytes,
 * repartidos en mil tramas. Ya no se usa para mandar -ver audio_manda()-
 * pero se queda porque es el numero contra el que se comprueba que el
 * endpoint declarado tiene sitio de sobra. */
#define AUD_TRAMA  ((USB_AUD_HZ / 1000UL) * USB_AUD_CANALES * (USB_AUD_BITS / 8U))

/*
 * TODO LO QUE ENTRA Y SALE DEL ANILLO DEL AUDIO TIENE QUE SER MULTIPLO DE
 * UNA TRAMA ESTEREO. Si no, se desalinea medio sample y no se recupera
 * nunca: ver mete_entero(). Cuatro bytes = dos canales de 16 bits.
 */
#define AUD_MARCO  (USB_AUD_CANALES * (USB_AUD_BITS / 8U))
_Static_assert((USB_AUD_EP_TAM % AUD_MARCO) == 0U,
               "el endpoint de audio no es multiplo de una trama estereo");
_Static_assert((AUD_ANILLO_TAM % AUD_MARCO) == 0U,
               "el anillo del audio no es multiplo de una trama estereo");
_Static_assert(USB_AUD_EP_TAM >= AUD_TRAMA,
               "el endpoint declarado no cabe una trama de audio");

static void audio_manda(usb_dev *udev)
{
    /*
     * SE MANDA LO QUE HAYA, NO SIEMPRE 192 BYTES CON SILENCIO DETRAS.
     *
     * La primera version rellenaba de ceros hasta la trama entera. En la
     * grabacion de referencia eso salieron 20 rachas de ceros en nueve
     * segundos, la mayor de 9,33 ms: se oyen.
     *
     * Y rellenar era ademas lo contrario de lo que dice el descriptor. El
     * endpoint esta declarado ASINCRONO (bmAttributes 0x05), y eso
     * significa exactamente "el reloj lo pongo yo y mando lo que tenga": el
     * anfitrion esta obligado a aceptar tramas de tamaño distinto y a
     * acomodarse. Rellenando de ceros le estabamos mintiendo -diciendole
     * que el ritmo era perfecto- y metiendole silencio de verdad en medio.
     *
     * El reloj de la radio es su cristal de 12,288 MHz y el del ordenador
     * es otro: por muy iguales que sean, uno adelanta al otro y cada pocos
     * segundos sobra o falta una muestra. Eso es justo lo que el modo
     * asincrono existe para absorber.
     *
     * Se manda hasta el maximo declarado, no hasta la trama nominal: si el
     * ordenador va por detras, las tramas un poco mas largas son como se
     * recupera.
     */
    uint16_t n = anillo_saca(&s_audio, s_tx_aud, (uint16_t)USB_AUD_EP_TAM);

    /*
     * EL PRIMER ENVIO NO ES UN HUECO - 09/10/2026.
     *
     * Uno. Y era este: cdc_set_intf() vacia el anillo y llama aqui
     * inmediatamente, asi que la primera trama sale vacia POR
     * CONSTRUCCION. Hay que mandarla -en isocrono el flujo tiene que
     * empezar, si no el anfitrion se queda esperando una trama que nadie
     * arranca- pero contarla como corte es mentir.
     *
     * Y la mentira importaba: con ese 1 ahi, el colchon parecia justo
     * cuando en realidad no habia habido ni un corte en toda la sesion.
     * Estuve a punto de cambiar el audio a mono para ganar margen contra
     * un problema que no existia.
     *
     * Ahora solo se cuentan los huecos DESPUES de que haya salido audio de
     * verdad, que es cuando un hueco significa algo.
     */
    if (n != 0U)              { s_audio_vivo = 1U; }
    else if (s_audio_vivo != 0U) { s_audio_huecos++; }

    usbd_ep_send(udev, USB_AUD_EP_IN, s_tx_aud, n);
}

/*
 * EL AJUSTE ALTERNATIVO DE LA INTERFAZ DE AUDIO.
 *
 * El ordenador pide el ajuste 1 cuando alguien abre la grabacion y vuelve
 * al 0 al cerrarla. Mientras esta en 0 NO hay endpoint, y por eso el
 * isocrono se configura aqui y no en la inicializacion: configurarlo antes
 * seria reservar el ancho de banda desde que se enchufa el cable.
 *
 * El primer envio tambien sale de aqui. Sin el, el ordenador se queda
 * esperando una trama que nadie empieza: en isocrono no hay reintento ni
 * aviso, simplemente no suena nada y no hay nada que leer.
 */
static uint8_t cdc_set_intf(usb_dev *udev, usb_req *req)
{
    if ((req->wIndex & 0xFFU) != 0x03U) { return USBD_OK; }

    if ((req->wValue & 0xFFU) == 0x01U) {
        if (s_audio_on == 0U) {
            usbd_ep_setup(udev, &k_ep_aud);
            anillo_reset(&s_audio);
            s_audio_huecos = 0UL;
            s_audio_vivo = 0U;
            s_audio_on = 1U;
            audio_manda(udev);
        }
    } else {
        if (s_audio_on != 0U) {
            s_audio_on = 0U;
            usbd_ep_clear(udev, USB_AUD_EP_IN);
        }
    }
    return USBD_OK;
}

static usb_class_core k_cdc_class = {
    .init     = cdc_init,
    .deinit   = cdc_deinit,
    .req_proc = cdc_req,
    .set_intf = cdc_set_intf,
    .data_in  = cdc_in,
    .data_out = cdc_out
};

/* ------------------------------------------------------------------ *
 * Lo publico
 * ------------------------------------------------------------------ */

/*
 * APAGARLO Y VOLVERLO A ENCENDER, QUE ES LO QUE PIDE QUIEN LO USA.
 *
 * La primera version no se apagaba, y lo que escribi para justificarlo
 * -"el nucleo del fabricante no tiene un camino de vuelta probado"- era
 * pereza: el USB tiene una desconexion por software hecha justo para esto.
 *
 * usb_dev_disconnect() suelta la resistencia de subida de D+. Para el
 * ordenador es exactamente lo mismo que tirar del cable: cierra el puerto,
 * quita el COM, y lo que tuviera abierto se entera por donde se entera
 * siempre. No deja el periferico a medias -sigue inicializado- asi que
 * volver es una sola escritura.
 *
 * Lo que NO se hace es deinicializar el nucleo ni apagar el PLLSAI. Apagar
 * un PLL del que cuelga el reloj de 48 MHz y volverlo a levantar es el tipo
 * de cosa que funciona noventa y nueve veces; y el reloj no se gasta por
 * estar encendido.
 */
void usb_cdc_para(void)
{
    if (s_arrancado == 0U) { return; }

    /*
     * AQUI NO SE VACIAN LOS ANILLOS, Y LA PRIMERA VERSION SI LO HACIA.
     *
     * Esto corre en el bucle principal -sale de un toque en Informacion- y
     * los tres anillos los toca la interrupcion: la del USB mete en
     * s_desde_pc y saca de s_hacia_pc, y la del AUDIO mete en s_audio.
     * anillo_reset() escribe los DOS indices, asi que vaciarlos desde aqui
     * es meter un segundo escritor en una estructura que esta construida
     * para tener exactamente uno.
     *
     * No es teorico: la desconexion no para las interrupciones que ya
     * estan pendientes, y la del audio corre a prioridad mas alta que
     * cualquier cosa que haga el bucle.
     *
     * Y no hace falta: al volver a enchufar, usb_cdc_arranca() los vacia
     * CON EL USB YA DESCONECTADO -o sea sin nadie al otro lado- y ademas
     * cdc_init() los vuelve a vaciar cuando el ordenador reenumera.
     *
     * Lo unico que se toca aqui es la bandera del audio, y se pone a cero
     * ANTES de nada para que usb_cdc_audio() deje de meter.
     */
    s_audio_on = 0U;
    usb_dev_disconnect(&g_usb_cdc);
    s_parado = 1U;
}

uint8_t usb_cdc_encendido(void)
{
    /*
     * Hace falta una bandera y no vale mirar el contador de
     * interrupciones, que es lo que hacia la pantalla.
     *
     * Encendiendolo SIN cable, no entra ninguna interrupcion: el contador
     * se queda en cero y la fila decia "apagado" con el USB montado. Y
     * entonces el toque siguiente volvia a llamar a arranca() -que no hace
     * nada- en vez de pararlo, asi que no habia forma de llegar a
     * "desconectado" sin enchufar antes el cable.
     */
    return s_arrancado;
}

uint8_t usb_cdc_parado(void)
{
    return ((s_arrancado != 0U) && (s_parado != 0U)) ? 1U : 0U;
}

uint8_t usb_cdc_arranca(void)
{
    if (s_arrancado != 0U) {
        /* Ya estaba montado y solo estaba desconectado: se vuelve a
         * enchufar sin tocar nada mas. El ordenador lo enumera de nuevo
         * como si acabaras de meter el cable. */
        if (s_parado != 0U) {
            /*
             * Vaciar AQUI es seguro y vaciar en usb_cdc_para() no lo era:
             * en este punto el USB lleva desconectado desde entonces, o sea
             * que no hay ninguna interrupcion suya que pueda estar metiendo
             * o sacando. El orden importa: primero se vacia, despues se
             * enchufa.
             */
            anillo_reset(&s_hacia_pc);
            anillo_reset(&s_desde_pc);
            anillo_reset(&s_audio);
            cat_reset();
            s_tx_ocupado = 0U;
            s_parado = 0U;
            usb_dev_connect(&g_usb_cdc);
        }
        return 1U;
    }

    if (usb_cdc_hw_reloj() == 0U) { return 0U; }

    (void)anillo_pon(&s_hacia_pc, s_buf_hacia, sizeof s_buf_hacia);
    (void)anillo_pon(&s_desde_pc, s_buf_desde, sizeof s_buf_desde);
    (void)anillo_pon(&s_audio, s_buf_audio, AUD_ANILLO_TAM);
    s_tx_ocupado = 0U;
    s_audio_on = 0U;
    s_audio_huecos = 0UL;
    s_parado = 0U;
    s_isrs = 0UL;
    s_perdidos = 0UL;

    usbd_init(&g_usb_cdc, USB_CORE_ENUM_FS, &k_desc, &k_cdc_class);
    usb_intr_config();

    s_arrancado = 1U;
    return 1U;
}

uint8_t usb_cdc_conectado(void)
{
    return ((s_arrancado != 0U) && (s_parado == 0U)
            && (g_usb_cdc.dev.cur_status == (uint8_t)USBD_CONFIGURED))
           ? 1U : 0U;
}

/*
 * METER UN TROZO ENTERO, O NO METER NADA.
 *
 * Las dos cosas eran el mismo fallo. anillo_mete() mete LO QUE QUEPA y
 * devuelve cuanto metio, que para el puerto serie es lo correcto -una orden
 * cortada se rechaza y ya- pero aqui es veneno: si caben 100 bytes de 128,
 * mete 100. Y 100 no es multiplo de cuatro.
 *
 * A partir de ese momento el anillo esta desalineado medio sample: lo que
 * sale como canal izquierdo son los bytes altos de uno y los bajos del
 * siguiente. De ahi que los canales no coincidan y de ahi la basura de
 * alta frecuencia - cada muestra pasa a ser dos mitades de muestras
 * distintas, que es ruido blanco con la forma justa para colarse.
 *
 * Y no se arregla solo nunca: una vez corrido medio sample, sigue corrido.
 *
 * Asi que aqui es todo o nada. Si no cabe el trozo entero se tira entero,
 * que para el oido es lo mismo y deja el anillo en su sitio.
 */
static void mete_entero(const int16_t *trozo, uint16_t bytes)
{
    if (anillo_hueco(&s_audio) < bytes) { return; }
    (void)anillo_mete(&s_audio, (const uint8_t *)trozo, bytes);
}

void usb_cdc_audio(const int16_t *estereo, uint32_t n, uint32_t fs_hz)
{
    uint32_t paso, i, j = 0U;
    int16_t  trozo[64];

    /*
     * ESTO CORRE DENTRO DE LA INTERRUPCION DEL AUDIO, asi que no puede
     * bloquear, no puede dividir mas de lo imprescindible y tiene que
     * salirse enseguida cuando nadie esta grabando - que es casi siempre.
     */
    if ((s_audio_on == 0U) || (estereo == 0) || (n == 0UL)) { return; }

    /*
     * DIEZMADO ENTERO, Y POR QUE NO HACE FALTA FILTRAR.
     *
     * El audio sale a la frecuencia del I2S -96 kHz normalmente, 192 en FM
     * ancha, 48 en el modo alterno- y el USB lo quiere a 48. Normalmente
     * eso obligaria a filtrar antes de tirar muestras, porque lo que haya
     * por encima de 24 kHz se dobla hacia abajo.
     *
     * Aqui no hay nada por encima: lo que llega ya ha pasado por el filtro
     * de audio del demodulador, que como mucho deja 4 kHz. O sea que coger
     * una de cada dos es exacto, no una aproximacion.
     *
     * Si la frecuencia no es multiplo de 48 kHz no se manda nada en vez de
     * mandar algo a la velocidad equivocada: el ordenador no puede
     * enterarse de que el flujo va rapido -el formato dice 48000 y punto- y
     * lo que oiria seria la radio hablando como un pato.
     */
    if (fs_hz < USB_AUD_HZ) { return; }
    paso = fs_hz / USB_AUD_HZ;
    if ((paso * USB_AUD_HZ) != fs_hz) { return; }

    for (i = 0UL; i < n; i += paso) {
        trozo[j++] = estereo[2UL * i];
        trozo[j++] = estereo[(2UL * i) + 1UL];
        if (j >= (uint32_t)(sizeof trozo / sizeof trozo[0])) {
            mete_entero(trozo, (uint16_t)(j * 2U));
            j = 0U;
        }
    }
    if (j > 0U) {
        mete_entero(trozo, (uint16_t)(j * 2U));
    }
}

uint8_t  usb_cdc_audio_on(void)   { return s_audio_on; }
uint32_t usb_cdc_huecos(void)     { return s_audio_huecos; }
uint32_t usb_cdc_isrs(void)      { return s_isrs; }
uint32_t usb_cdc_perdidos(void)  { return s_perdidos; }

void usb_cdc_poll(void)
{
    uint8_t  c;
    uint8_t  salida[CAT_SALIDA_MAX];

    if (usb_cdc_conectado() == 0U) { return; }

    /* Lo que llego del ordenador, byte a byte al analizador del CAT. */
    while (anillo_saca(&s_desde_pc, &c, 1U) == 1U) {
        uint8_t n = cat_byte((char)c, (char *)salida);

        if (n > 0U) {
            if (anillo_mete(&s_hacia_pc, salida, n) < n) {
                s_perdidos += n;
            }
        }
    }

    /*
     * Y SE ARRANCA EL ENVIO SI ESTABA PARADO.
     *
     * Mientras hay envio en marcha, cada trama que termina dispara cdc_in()
     * y esa saca la siguiente sola. Pero cuando el anillo se vacia, cdc_in()
     * pone s_tx_ocupado a cero y ya no vuelve a entrar nadie: hace falta
     * que alguien lo re-arranque desde fuera, y ese alguien es esto.
     *
     * El orden importa: primero se marca ocupado y despues se manda. Al
     * reves, la trama podria terminar -y la interrupcion entrar- antes de
     * poner la marca, y entonces cdc_in() la pondria a cero y esto la
     * volveria a poner a uno con nada en marcha: el envio se quedaria
     * colgado para siempre.
     */
    if ((s_tx_ocupado == 0U) && (anillo_lleno(&s_hacia_pc) != 0U)) {
        uint16_t n = anillo_saca(&s_hacia_pc, s_tx, USB_CDC_EP_TAM);

        if (n > 0U) {
            s_tx_ocupado = 1U;
            usbd_ep_send(&g_usb_cdc, USB_CDC_EP_IN, s_tx, n);
        }
    }
}

void USBFS_IRQHandler(void);
void USBFS_IRQHandler(void)
{
    s_isrs++;
    usbd_isr(&g_usb_cdc);
}
