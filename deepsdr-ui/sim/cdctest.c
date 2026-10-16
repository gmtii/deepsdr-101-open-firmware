/*
 * LOS DESCRIPTORES DEL USB, RECORRIDOS COMO LOS RECORRE UN ANFITRION.
 *
 * POR QUE ESTE BANCO EXISTE
 * -------------------------
 * Un descriptor mal NO da un error. El ordenador pide la configuracion, no
 * le cuadra, y lo que se ve es "Dispositivo desconocido" en el
 * administrador de Windows, o nada en absoluto. No hay codigo que leer, no
 * hay traza, y desde la radio no se distingue de un cable malo.
 *
 * Y son 174 bytes escritos a mano en los que SEIS campos dicen el tamaño o
 * el contenido de otra cosa: la longitud de cada descriptor, el total de la
 * configuracion, el numero de interfaces, los endpoints de cada una, el
 * total de la parte de control de audio, y el tamaño del endpoint isocrono
 * -que tiene que cuadrar con la frecuencia y los canales declarados tres
 * descriptores mas arriba-.
 *
 * Esto los recorre igual que el anfitrion, siguiendo el campo de longitud,
 * y comprueba que todo lo que apunta a otra cosa cuadre con lo que de
 * verdad hay. Compila el usb_cdc_desc.c DEL FIRMWARE, no una copia.
 */
#include "usb_cdc_desc.h"
#include <stdio.h>
#include <string.h>

static int fallos = 0;

static void ok(const char *que, int bien, const char *det)
{
    printf("  %-50s %s%s%s\n", que, bien ? "ok" : "MAL",
           det ? "   " : "", det ? det : "");
    if (!bien) { fallos++; }
}

static void ok_n(const char *que, long vale, long debe)
{
    char det[90];

    if (vale == debe) { snprintf(det, sizeof det, "%ld", vale); }
    else { snprintf(det, sizeof det, "es %ld y deberia ser %ld", vale, debe); }
    ok(que, vale == debe, det);
}

/* Lo que se va encontrando al recorrer. */
#define MAX_IF  8
typedef struct {
    int numero;
    int alt;
    int clase;
    int subclase;
    int dice_ep;    /* el bNumEndpoints DECLARADO */
    int hay_ep;     /* los que de verdad vienen detras */
} iface_t;

static iface_t s_if[MAX_IF];
static int s_nif;

int main(void)
{
    const uint8_t *c = usb_cdc_desc_config;
    unsigned total = (unsigned)(c[2] | (c[3] << 8));

    printf("\nLOS DESCRIPTORES DEL USB: PUERTO SERIE + TARJETA DE SONIDO\n");

    /* ---------------- el dispositivo ---------------- */
    printf("\n  el dispositivo\n");
    {
        const uint8_t *d = usb_cdc_desc_dev;

        ok_n("bLength dice 18", d[0], 18);
        ok_n("y el array mide lo mismo", (long)sizeof usb_cdc_desc_dev, 18);
        ok_n("bDescriptorType es 1", d[1], 1);
        ok_n("bcdUSB es 2.00", (long)(d[2] | (d[3] << 8)), 0x0200);
        /*
         * CON DOS FUNCIONES, LA CLASE TIENE QUE SER 0xEF/0x02/0x01.
         *
         * Es lo que le dice al anfitrion que mire los descriptores de
         * asociacion. Dejandola en 0x02 (CDC) -que es lo que valia cuando
         * aqui solo habia puerto serie- Windows no monta el COM, y no da
         * ningun error.
         */
        ok("la clase dice que hay asociaciones (0xEF/0x02/0x01)",
           (d[4] == 0xEFU) && (d[5] == 0x02U) && (d[6] == 0x01U), NULL);
        ok("bMaxPacketSize0 es 8, 16, 32 o 64",
           (d[7] == 8U) || (d[7] == 16U) || (d[7] == 32U) || (d[7] == 64U),
           NULL);
        ok("hay fabricante y producto",
           ((d[8] | (d[9] << 8)) != 0) && ((d[10] | (d[11] << 8)) != 0), NULL);
        ok_n("una sola configuracion", d[17], 1);
    }

    /* ---------------- el recorrido ---------------- */
    printf("\n  la configuracion, descriptor a descriptor\n");
    {
        unsigned i = 0U;
        int truncado = 0, n_func_cdc = 0, n_iad = 0;
        int iad_cdc_ok = 0, iad_aud_ok = 0;
        int cur = -1;                /* indice en s_if del actual */
        int ac_dice = -1, ac_mide = 0, en_ac = 0;
        int iso_tam = -1, iso_len = 0;
        int ep_en_if = 0;      /* ya ha venido un endpoint en esta interfaz */
        int cs_ep_mal = 0;     /* un 0x25 antes de su endpoint */
        int fmt_hz = -1, fmt_ch = -1, fmt_bits = -1;
        int union_ok = 0;
        char det[120];

        ok_n("wTotalLength cuadra con el array",
             (long)total, (long)sizeof usb_cdc_desc_config);

        while (i < total) {
            unsigned len = c[i], tipo = c[i + 1U];

            if ((len == 0U) || ((i + len) > total)) { truncado = 1; break; }

            switch (tipo) {
            case 0x0BU:              /* asociacion de interfaces */
                n_iad++;
                if ((c[i + 2U] == 0U) && (c[i + 3U] == 2U)
                    && (c[i + 4U] == 0x02U)) { iad_cdc_ok = 1; }
                if ((c[i + 2U] == 2U) && (c[i + 3U] == 2U)
                    && (c[i + 4U] == 0x01U)) { iad_aud_ok = 1; }
                printf("      asociacion: interfaces %u..%u, clase 0x%02X\n",
                       c[i + 2U], c[i + 2U] + c[i + 3U] - 1U, c[i + 4U]);
                break;

            case 0x04U:              /* interfaz */
                if (s_nif < MAX_IF) {
                    cur = s_nif++;
                    s_if[cur].numero   = (int)c[i + 2U];
                    s_if[cur].alt      = (int)c[i + 3U];
                    s_if[cur].dice_ep  = (int)c[i + 4U];
                    s_if[cur].clase    = (int)c[i + 5U];
                    s_if[cur].subclase = (int)c[i + 6U];
                    s_if[cur].hay_ep   = 0;
                }
                /* La parte de control de audio es la que lleva su propio
                 * total; se empieza a contar al entrar en ella. */
                en_ac = ((c[i + 5U] == 0x01U) && (c[i + 6U] == 0x01U)) ? 1 : 0;
                ep_en_if = 0;   /* interfaz nueva, endpoints de cero */
                printf("      interfaz %u.%u  clase 0x%02X/%02X  "
                       "dice %u endpoints\n",
                       c[i + 2U], c[i + 3U], c[i + 5U], c[i + 6U], c[i + 4U]);
                break;

            case 0x25U:              /* clase, colgado de un endpoint */
                /*
                 * UN 0x25 SOLO PUEDE IR DETRAS DE SU ENDPOINT.
                 *
                 * 0x24 (CS_INTERFACE) y 0x25 (CS_ENDPOINT) se parecen
                 * muchisimo: en audio los dos miden 7 bytes y los dos
                 * llevan el subtipo 0x01. Pero el general de FLUJO va
                 * dentro de la interfaz y antes del endpoint, y el general
                 * de ENDPOINT va detras y habla de el.
                 *
                 * Poniendo 0x25 en el primero, el anfitrion ve un
                 * descriptor de endpoint sin ningun endpoint al que
                 * pegarse, abandona la configuracion y NO DA ERROR: el
                 * equipo se queda a medio enumerar y no sale ningun
                 * puerto. Paso de verdad, y costo una version entera.
                 */
                if (ep_en_if == 0) { cs_ep_mal = 1; }
                break;

            case 0x05U:              /* endpoint */
                if (cur >= 0) { s_if[cur].hay_ep++; }
                ep_en_if = 1;
                if (c[i + 2U] == USB_AUD_EP_IN) {
                    iso_tam = (int)(c[i + 4U] | (c[i + 5U] << 8));
                    iso_len = (int)len;
                }
                break;

            case 0x24U:              /* funcional de clase, en la interfaz */
                if (en_ac != 0) {
                    ac_mide += (int)len;
                    if (c[i + 2U] == 0x01U) {        /* cabecera de control */
                        ac_dice = (int)(c[i + 5U] | (c[i + 6U] << 8));
                    }
                } else if (s_if[cur].clase == 0x02) { /* los de CDC */
                    n_func_cdc++;
                    if (c[i + 2U] == 0x06U) {
                        union_ok = ((c[i + 3U] == 0U) && (c[i + 4U] == 1U));
                    }
                } else if (c[i + 2U] == 0x02U) {     /* formato de audio */
                    fmt_ch   = (int)c[i + 4U];
                    fmt_bits = (int)c[i + 6U];
                    fmt_hz   = (int)(c[i + 8U] | (c[i + 9U] << 8)
                                     | (c[i + 10U] << 16));
                }
                break;

            default:
                break;
            }
            i += len;
        }

        ok("ningun descriptor se sale ni mide cero", truncado == 0, NULL);
        ok_n("la suma de longitudes cae justo en el total", (long)i,
             (long)total);

        /* --- las interfaces --- */
        {
            int numeros[MAX_IF], nn = 0, k, j, visto;

            for (k = 0; k < s_nif; k++) {
                visto = 0;
                for (j = 0; j < nn; j++) {
                    if (numeros[j] == s_if[k].numero) { visto = 1; }
                }
                if (!visto) { numeros[nn++] = s_if[k].numero; }
            }
            /*
             * CUATRO INTERFACES, PERO CINCO DESCRIPTORES DE INTERFAZ.
             *
             * La 3 aparece dos veces, con ajuste alternativo 0 y 1. Eso NO
             * son dos interfaces: bNumInterfaces cuenta numeros distintos,
             * no descriptores. Contarlos mal es la forma de poner 5 ahi y
             * que el anfitrion busque una interfaz 4 que no existe.
             */
            snprintf(det, sizeof det, "%d numeros en %d descriptores",
                     nn, s_nif);
            ok_n("bNumInterfaces cuenta NUMEROS distintos", c[4], nn);
            ok("y son cuatro", nn == 4, det);
        }

        /* --- lo declarado contra lo que hay --- */
        {
            int k, mal = 0;

            for (k = 0; k < s_nif; k++) {
                if (s_if[k].dice_ep != s_if[k].hay_ep) {
                    printf("      la %d.%d dice %d endpoints y trae %d\n",
                           s_if[k].numero, s_if[k].alt,
                           s_if[k].dice_ep, s_if[k].hay_ep);
                    mal++;
                }
            }
            /*
             * El anfitrion usa bNumEndpoints para saber cuantos
             * descriptores leer detras de la interfaz. Si dice de mas, se
             * come el descriptor de la interfaz SIGUIENTE creyendo que es
             * un endpoint y rechaza la configuracion entera.
             */
            ok("cada interfaz declara los endpoints que trae", mal == 0, NULL);
        }

        ok("ningun descriptor de clase de endpoint va antes que el suyo",
           cs_ep_mal == 0, NULL);
        ok_n("hay dos asociaciones", n_iad, 2);
        ok("la del puerto serie cubre la 0 y la 1", iad_cdc_ok, NULL);
        ok("la del audio cubre la 2 y la 3", iad_aud_ok, NULL);
        ok_n("los cuatro funcionales de CDC estan", n_func_cdc, 4);
        ok("el funcional Union apunta a la 0 y a la 1", union_ok, NULL);

        /* --- el ajuste alternativo 0 del audio --- */
        {
            int k, hay_alt0 = 0, hay_alt1 = 0;

            for (k = 0; k < s_nif; k++) {
                if ((s_if[k].numero == 3) && (s_if[k].alt == 0)
                    && (s_if[k].hay_ep == 0)) { hay_alt0 = 1; }
                if ((s_if[k].numero == 3) && (s_if[k].alt == 1)
                    && (s_if[k].hay_ep == 1)) { hay_alt1 = 1; }
            }
            /*
             * Sin el ajuste 0 -una interfaz de audio SIN endpoints- el
             * equipo reservaria su ancho de banda isocrono desde que se
             * enchufa, en vez de cuando alguien graba. Un concentrador con
             * otras cosas colgando puede rechazarlo entero por eso.
             */
            ok("el audio tiene un ajuste 0 sin endpoints", hay_alt0, NULL);
            ok("y un ajuste 1 con el isocrono", hay_alt1, NULL);
        }

        /* --- las dos cuentas cruzadas, que son las que importan --- */
        printf("\n  las referencias cruzadas\n");
        snprintf(det, sizeof det, "dice %d, miden %d", ac_dice, ac_mide);
        ok("el total de la parte de control de audio cuadra",
           (ac_dice > 0) && (ac_dice == ac_mide), det);

        {
            /*
             * EL TAMAÑO DEL ISOCRONO CONTRA EL FORMATO DECLARADO.
             *
             * 48000 x 2 canales x 2 bytes / 1000 tramas = 192 bytes por
             * trama. El endpoint tiene que caber eso, y conviene que sobre
             * una muestra: los relojes de la radio y del ordenador no son
             * el mismo, y cuando el nuestro va un pelo rapido hay tramas de
             * 49 muestras.
             *
             * Esta es la comprobacion que ata el formato con el endpoint.
             * Sin ella, cambiar la frecuencia de muestreo y olvidarse del
             * endpoint da un equipo que enumera y suena a chasquidos.
             */
            int minimo = (fmt_hz * fmt_ch * (fmt_bits / 8)) / 1000;

            snprintf(det, sizeof det,
                     "%d Hz x %d ch x %d bits = %d B/trama, endpoint %d",
                     fmt_hz, fmt_ch, fmt_bits, minimo, iso_tam);
            ok("el isocrono cabe el formato que se declara",
               (iso_tam >= minimo) && (iso_tam <= 1023), det);
            ok("y sobra al menos una muestra para la deriva de reloj",
               iso_tam >= (minimo + (fmt_ch * (fmt_bits / 8))), NULL);
            /*
             * UN ENDPOINT DE AUDIO MIDE NUEVE BYTES, NO SIETE: lleva
             * bRefresh y bSynchAddress de mas. Poner 7 es el error clasico
             * -se copia del bulk- y lo que pasa es que el anfitrion lee los
             * dos bytes siguientes como el principio del descriptor de
             * despues.
             */
            ok_n("el endpoint de audio mide 9 bytes y no 7", iso_len, 9);
        }

        /* --- y lo que tumba a Windows --- */
        {
            int k, clase_mal = 0, bulk_mal = 0;

            for (k = 0; k < s_nif; k++) {
                if ((s_if[k].numero == 0)
                    && ((s_if[k].clase != 0x02) || (s_if[k].subclase != 0x02))) {
                    clase_mal = 1;
                }
                if ((s_if[k].numero == 1) && (s_if[k].clase != 0x0A)) {
                    clase_mal = 1;
                }
                if ((s_if[k].numero >= 2) && (s_if[k].clase != 0x01)) {
                    clase_mal = 1;
                }
            }
            ok("las clases de las cuatro interfaces son las que toca",
               clase_mal == 0, NULL);

            i = 0U;
            while (i < total) {
                unsigned len = c[i], tipo = c[i + 1U];

                if (len == 0U) { break; }
                if (tipo == 0x05U) {
                    unsigned tam = (unsigned)(c[i + 4U] | (c[i + 5U] << 8));

                    /* Un bulk de velocidad plena solo puede medir 8, 16, 32
                     * o 64. Poner 512 -copiado de un ejemplo de alta
                     * velocidad- hace que el equipo enumere y no
                     * transfiera. */
                    if ((c[i + 3U] & 0x03U) == 0x02U) {
                        if ((tam != 8U) && (tam != 16U) && (tam != 32U)
                            && (tam != 64U)) { bulk_mal = 1; }
                    }
                    if ((c[i + 3U] & 0x03U) == 0x03U) {
                        if (tam > 64U) { bulk_mal = 1; }
                    }
                }
                i += len;
            }
            ok("ningun bulk pide un tamaño de alta velocidad",
               bulk_mal == 0, NULL);
        }
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO",
           fallos);
    return fallos ? 1 : 0;
}
