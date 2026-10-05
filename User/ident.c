#include "ident.h"
#include "hfdl_ram.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Ver ident.h para el porque. Aqui el como, y la tabla. */

/*
 * ==========================================================================
 * LA TABLA
 * ==========================================================================
 *
 * Cada fila es una firma. Los rangos a 0,0 no se miran. Una fila cuadra si
 * cuadran todos los rangos que no sean 0,0.
 *
 * EL ORDEN IMPORTA PARA LEERLA, NO PARA DECIDIR: se recorre entera siempre
 * y salen todas las que cuadren. Si eso son tres, se dicen las tres.
 *
 * `probada` a 1 es "esto se comprueba contra una grabacion en el banco".
 * A 0 es "el numero viene de la norma o del catalogo, pero NADIE lo ha
 * visto salir de esta radio". La pantalla lo marca con un interrogante, y
 * eso no es adorno: es la diferencia entre una medida y una cita.
 */
static const ident_firma_t k_firmas[] = {

/* --- portadoras y silencio ------------------------------------------- */
{
    "Portadora sola (heterodino)", IDENT_POR_PICO,
    0U,0U, 0U,0U, 0U,0U, 0U,120U, 81U,100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "No lleva datos. Medida: 57% de la potencia en +-8 Hz."
},
/*
 * EL CW ES LA MISMA RAYA, PERO PARPADEANDO - 05/10/2026.
 *
 * *** El dueño: "implementa la identificacion de todas las señales que
 * conocemos". ***
 *
 * Un tono de Morse y un heterodino son LO MISMO para el espectro: toda la
 * potencia en una raya estrecha. Lo unico que los separa con lo que aqui se
 * mide es el ciclo de trabajo: una portadora esta siempre, y el Morse se
 * enciende y se apaga.
 *
 * Por eso la fila de la portadora, que hasta hoy no miraba el ciclo, ahora
 * pide 81% o mas: sin eso se quedaba con el Morse tambien y esta fila no
 * habria cuadrado nunca.
 *
 * El 15..80 no sale de ninguna norma -el Morse no tiene una- sino de la
 * aritmetica: con la proporcion clasica de 1 a 3 entre punto y raya y los
 * espacios de la norma ITU-R M.1677, el texto corriente sale alrededor del
 * 50%, y los extremos son uno todo rayas y otro todo puntos. Por eso va sin
 * comprobar: hace falta una grabacion.
 */
{
    "CW (Morse)", IDENT_POR_PICO,
    0U,0U, 0U,0U, 0U,0U, 0U,200U, 15U,80U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "Un tono que se enciende y se apaga. Ponlo en CW."
},

/* --- FSK de dos tonos ------------------------------------------------- */
/*
 * LAS CUATRO DE 100 BAUDIOS Y 170 Hz SON LA MISMA MODULACION y por eso van
 * en filas distintas separadas SOLO por el ciclo de trabajo. No hay otra
 * forma de distinguirlas sin decodificar:
 *   SITOR-B / NAVTEX  emiten seguido        -> ciclo alto
 *   SITOR-A / DSC     emiten a rafagas      -> ciclo bajo
 * Y dentro de cada pareja, ya no se pueden separar por la señal: NAVTEX es
 * un SERVICIO que va en SITOR-B, no una modulacion distinta.
 */
{
    "SITOR-B / NAVTEX / telex", IDENT_POR_TONOS,
    150U,190U, 950U,1050U, 0U,0U, 0U,0U, 70U,100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "Emite seguido. Ponlo en NAVTEX: sale texto en claro."
},
{
    "SITOR-A o DSC (a rafagas)", IDENT_POR_TONOS,
    150U,190U, 950U,1050U, 0U,0U, 0U,0U, 0U,69U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "Misma modulacion que el NAVTEX pero a rafagas. La radio no lo decodifica."
},
{
    "STANAG 4481 FSK (KG-84)", IDENT_POR_TONOS,
    780U,920U, 700U,800U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "Cifrado con KG-84. Se demodula, no se lee."
},
{
    "RTTY de aficionado", IDENT_POR_TONOS,
    150U,190U, 440U,470U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "45,45 baudios. Ponlo en RTTY-L o RTTY-U."
},
/*
 * COMPROBADA EN ANTENA - 05/10/2026, la segunda.
 *
 * Grabacion de 34,9 s del dueño. El motor midio salto 446 Hz y 49,7 Bd
 * -la fila pide 400..500 y 48,0..52,0- y dijo "RTTY comercial 50 Bd".
 * Y no se quedo en que lo pareciera: la grabacion se demodulo aparte,
 * FSK de dos tonos a 50 baudios con la marca en el tono BAJO, y salio
 * texto ITA2 limpio:
 *
 *     HUMBER (53.5N   2.3E) WT: 17 C
 *     DI  6. 00Z: W      3-4        0.5 M
 *     SA 10. 00Z: W-NW     4        VT M
 *
 * Es el boletin maritimo del DWD: zona de mar, posicion, temperatura del
 * agua, y por dias viento y altura de ola. "SA" y "DI" son sabado y
 * martes en aleman, que es lo que acaba de decidir de quien es.
 *
 * Lo unico que hay que saber para oirlo: la marca es el tono bajo, o sea
 * que si sale basura hay que invertir el desplazamiento.
 */
{
    "RTTY comercial 50 Bd", IDENT_POR_TONOS,
    400U,500U, 480U,520U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "50 baudios, 450 Hz. Ponlo en RTTY."
},
/*
 * APRS. Los dos tonos y la velocidad salen de nuestro propio ax25.c
 * -AX25_MARCA_HZ 1200, AX25_ESPACIO_HZ 2200, AX25_BAUD 1200-, que es la
 * fuente mas firme que hay para esta radio: es lo que el decodificador
 * espera de verdad.
 *
 * Va por separacion y no por velocidad A PROPOSITO: 1200 baudios a 3 kHz
 * son dos muestras y media por simbolo, y el medidor solo llega a 600. Ver
 * IDENT_POR_SEP en ident.h.
 */
{
    "APRS / paquete AFSK 1200", IDENT_POR_SEP,
    900U,1100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "Tonos de 1200 y 2200 Hz. Ponlo en APRS."
},

/* --- PSK, por el periodo de la trama ---------------------------------- */
/*
 * Los cinco periodos estan MEDIDOS contra grabaciones reales, no copiados:
 * ver el banco sim/stanag_aire.c, que los contrasta uno a uno. El margen
 * del 1% viene de ahi: el peor de los cinco quedo a 0,03% de la norma.
 */
{
    "STANAG 4285 o 4481 PSK", IDENT_POR_TRAMA,
    0U,0U, 0U,0U, 10560U,10774U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "106,67 ms. Ponlo en STANAG: a 600 bps y entrelazado largo hay texto."
},
{
    "STANAG 4529", IDENT_POR_TRAMA,
    0U,0U, 0U,0U, 20957U,21381U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "211,69 ms. Es media banda: 1,24 kHz. No se demodula todavia."
},
{
    "STANAG 4415", IDENT_POR_TRAMA,
    0U,0U, 0U,0U, 6613U,6749U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "66,81 ms. Para canales muy malos. No se demodula todavia."
},
{
    "STANAG 4539 / MIL-188-110B", IDENT_POR_TRAMA,
    0U,0U, 0U,0U, 11841U,12081U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "119,61 ms, que son 287 simbolos. No se demodula todavia."
},
{
    "STANAG 4538 / MIL-188-141B", IDENT_POR_TRAMA,
    0U,0U, 0U,0U, 1320U,1347U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "13,33 ms y a rafagas: es el 3G ALE, busca enlace."
},

/* --- los tres estrechos de aficionado, por el ancho -------------------- */
/*
 * AQUI EL ANCHO SI IDENTIFICA, y es el unico sitio donde lo hace.
 *
 * Los tres son modos digitales de aficionado que no llevan ni dos tonos
 * que contar ni una trama que se repita dentro de los 256 ms que alcanza
 * la autocorrelacion. Lo que SI tienen es un ancho de banda fijado por la
 * norma y muy distinto entre ellos, y una casilla del espectro mide 1,46
 * Hz: de sobra para separar 6 de 31 y de 50.
 *
 *   WSPR   4-FSK con los tonos a 1,4648 Hz -el numero esta en nuestro
 *          wspr_msg.h- : cuatro tonos son 5,9 Hz. Emite 110,6 s de cada
 *          ventana de 120, o sea que mientras esta, esta entero.
 *   PSK31  BPSK a 31,25 baudios -PSK31_BAUDIO_X100 en nuestro psk31.h-,
 *          con la envolvente en coseno que le quita las colas: el lobulo
 *          util son los mismos 31 Hz de la velocidad.
 *   FT8    8-FSK con los tonos a 6,25 Hz -ver ft8_waterfall_adapter.h-:
 *          ocho tonos son 50 Hz. Emite 12,64 s de cada 15.
 *
 * Los margenes son anchos a proposito por abajo: el ancho a -10 dB de una
 * señal debil se come las faldas y mide de menos.
 *
 * Las tres van SIN COMPROBAR hasta que haya grabacion de cada una. Los
 * numeros salen de la norma y de nuestros propios decodificadores, que es
 * lo que pide la regla, pero medir no es lo mismo que calcular.
 */
{
    "WSPR", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 1U,14U, 80U,100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "4-FSK de 6 Hz. Ponlo en WSPR: tarda dos minutos por ciclo."
},
{
    "PSK31", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 15U,40U, 30U,100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "31,25 baudios en BPSK. Ponlo en PSK31."
},
/*
 * ESTA FILA ES PARA UNA SEÑAL SOLA, no para una banda de FT8. En 7.074 hay
 * cuarenta a la vez y el ancho medido es el del monton: ahi quien
 * identifica es la cadencia, tres filas mas abajo. Esta sirve cuando se
 * ha estrechado el filtro sobre una, o cuando hay una suelta fuera de
 * banda.
 */
{
    "FT8 (una sola)", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 41U,80U, 50U,100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "8-FSK de 50 Hz. Ponlo en FT8: emite 12,6 s de cada 15."
},

/* --- los que se reconocen por el reloj --------------------------------- */
/*
 * *** El dueño: "lo primero que he probado es ft8 en 7074, y no es capaz de
 * identificarlo". ***
 *
 * Estas tres filas son la respuesta, y no miran el espectro: miran el
 * RELOJ. Ver ident_cad_t en ident.h para por que hacia falta otro
 * instrumento.
 *
 * Los tres periodos son de la norma de cada modo y estan en la
 * documentacion de WSJT-X:
 *
 *   FT8   ciclo de 15 s, emision de 12,64 -> 84%
 *   FT4   ciclo de 7,5 s, emision de 4,48 -> 60%
 *   WSPR  ciclo de 120 s, emision de 110,6 -> 92%
 *
 * Los margenes son anchos por abajo en la ocupacion a proposito: si la
 * señal es debil, el medidor cuenta como apagado el principio y el final de
 * cada emision y la ocupacion sale de menos. Por arriba se cierran, porque
 * algo que ocupa el 98% de su ciclo no se esta apagando, esta siempre.
 *
 * El WSPR necesita CUATRO MINUTOS de escucha para que entren dos ciclos. La
 * pantalla dice cuanto lleva, que si no parece que no hace nada.
 */
/*
 * COMPROBADA EN ANTENA el 05/10/2026: el dueño puso el modo en 7.074 kHz y
 * la radio midio "ciclo 15,5 s, ocupa 86%" contra los 15,0 y 84% de la
 * norma. Medio segundo era una anotacion del cuaderno, y por eso el
 * periodo pasa a promediarse: ver cad_calcula().
 */
{
    "FT8 (o JS8 de 15 s)", IDENT_POR_CADENCIA,
    0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 140U,160U, 60U,95U, 0U,0U, 0U,0U, 1U,
    "Todas empiezan a la vez cada 15 s. Ponlo en FT8."
},
{
    "FT4", IDENT_POR_CADENCIA,
    0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 68U,82U, 40U,80U, 0U,0U, 0U,0U, 0U,
    "Ciclo de 7,5 s. Esta radio no lo decodifica todavia."
},
{
    "WSPR", IDENT_POR_CADENCIA,
    0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1150U,1250U, 80U,98U, 0U,0U, 0U,0U, 0U,
    "Ciclo de dos minutos. Ponlo en WSPR."
},

/* --- los que se reconocen por el ritmo del simbolo --------------------- */
/*
 * *** El dueño, con una grabacion de una banda de aficionado: "esto que
 * podria ser?". Era un MFSK de VEINTIUN tonos separados 23,44 Hz y la radio
 * dijo "podria ser ALE 2G" mirando solo el ancho. ***
 *
 * Estas filas son la respuesta. No miran cuantos tonos hay ni donde estan
 * -eso no se puede medir con esta ventana, ver SIMB_W en stanag_det.c-,
 * miran CADA CUANTO CAMBIA DE TONO, que es una medida sola y vale para
 * cualquier cosa que tenga simbolos.
 *
 * Y las tres piden expresamente que NO se hayan visto dos tonos: un RTTY
 * tambien tiene velocidad de simbolo, pero ese ya lo identifican las filas
 * de dos tonos, que son mas firmes. Estas son para cuando hay muchos.
 */
{
    "MFSK multitono (Olivia y parecidos)", IDENT_POR_SIMBOLO,
    0U,0U, 0U,0U, 0U,0U, 150U,2600U, 0U,0U, 0U,0U, 0U,0U, 150U,400U, 4U,60U, 0U,
    "Separacion = velocidad. Divide el ancho entre ella y salen los tonos."
},
{
    "ALE 2G (MIL-188-141)", IDENT_POR_SIMBOLO,
    0U,0U, 0U,0U, 0U,0U, 1400U,2400U, 0U,0U, 0U,0U, 0U,0U, 1150U,1350U, 0U,0U, 0U,
    "125 baudios, 8 tonos de 750 a 2500 Hz. Ponlo en ALE."
},
{
    "Teletipo de 100 Bd sin dos tonos claros", IDENT_POR_SIMBOLO,
    0U,0U, 0U,0U, 0U,0U, 100U,900U, 0U,0U, 0U,0U, 0U,0U, 950U,1050U, 0U,0U, 0U,
    "100 baudios. Si fuera SITOR saldria arriba; prueba NAVTEX igual."
},

/* --- imagen por subportadora de FM ------------------------------------ */
/*
 * SSTV Y FAX SON LA MISMA FORMA DE ONDA y esta radio NO LOS SEPARA. Los
 * dos mandan la imagen como frecuencia dentro de la misma ventana -1500 Hz
 * el negro, 2300 el blanco- y los dos van seguidos. Lo unico que los
 * distingue de verdad es cuanto dura un renglon: 500 ms clavados el fax, y
 * entre 428 y 446 ms los modos de SSTV que lleva esta radio.
 *
 * Y ESO NO SE PUEDE MEDIR AQUI: la autocorrelacion mira hasta 768 muestras
 * a 3 kHz, o sea 256 ms. Los dos se salen por arriba.
 *
 * Asi que la fila dice las dos y lo dice. Prometer una de las dos a cara o
 * cruz seria exactamente lo que la cabecera de ident.h dice que este modo
 * no va a hacer nunca.
 */
{
    "SSTV o fax meteorologico", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 600U,1300U, 85U,100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "Imagen en FM, 1500-2300 Hz. Prueba SSTV y luego WEFAX."
},

/* --- por el ancho, y solo como pista ---------------------------------- */
/*
 * ESTAS NO IDENTIFICAN, ORIENTAN. En cada tramo de ancho de banda hay
 * decenas de cosas distintas, asi que lo que dicen es "mira por aqui", y
 * su texto lo deja claro. Estan porque un ancho medido ya descarta el 90%
 * del catalogo, y eso vale mas que un renglon en blanco.
 *
 * Y SE CALLAN CUANDO HABLA ALGUIEN CON MAS AUTORIDAD: ver ident_casa().
 */
{
    "Modem estrecho sin identificar", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 81U,599U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "Entre 0,1 y 0,6 kHz. Teletipo o ARQ: hay decenas."
},
{
    "Podria ser ALE 2G", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 1400U,2200U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "8-FSK de 750 a 2500 Hz a rafagas cortas. Prueba ALE."
},
{
    "Podria ser HFDL, o un modem de fase", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 2201U,3200U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "Ocupa el canal entero. Prueba HFDL y mira el periodo de abajo."
},
};
#define N_FIRMAS ((uint16_t)(sizeof k_firmas / sizeof k_firmas[0]))

const ident_firma_t *ident_firmas(uint16_t *n)
{
    if (n) { *n = N_FIRMAS; }
    return k_firmas;
}

/*
 * ¿Esta fila es solo una pista? Lo dice su propio nombre, que es lo que lee
 * el dueño: las que orientan empiezan por "Modem" o por "Podria". Ver el
 * final de ident_casa().
 */
static uint8_t es_pista(const ident_firma_t *f)
{
    return (uint8_t)(strncmp(f->nombre, "Modem", 5) == 0
                     || strncmp(f->nombre, "Podria", 6) == 0);
}

/* Un rango a 0,0 no se mira; si no, el valor tiene que caer dentro. */
static uint8_t dentro(uint16_t v, uint16_t lo, uint16_t hi)
{
    if (lo == 0U && hi == 0U) { return 1U; }
    return (uint8_t)(v >= lo && v <= hi);
}

uint16_t ident_casa(const stanag_det_t *m, const ident_cad_t *c,
                    uint8_t *salen, uint16_t max)
{
    uint16_t i, n = 0U;
    ident_cad_t sin_cad;

    if (m == 0 || salen == 0 || max == 0U) { return 0U; }
    if (c == 0) { memset(&sin_cad, 0, sizeof sin_cad); c = &sin_cad; }

    for (i = 0U; i < N_FIRMAS; i++) {
        const ident_firma_t *f = &k_firmas[i];

        /*
         * LO PRIMERO, QUE LA MEDIDA QUE PIDE LA FIRMA EXISTA. Una firma de
         * dos tonos no puede cuadrar si no se han visto dos tonos, por
         * mucho que los demas rangos esten a cero: sin esto, una fila con
         * todos los rangos abiertos cuadraria siempre.
         */
        switch (f->criterio) {
        case IDENT_POR_TONOS:
            if (!m->hay_tonos || m->baudios_x10 == 0U) { continue; }
            break;
        case IDENT_POR_CADENCIA:
            /* Sin cadencia medida no hay nada que comparar. Y con poca
             * firmeza tampoco: un ciclo que apenas destaca del ruido es
             * una coincidencia, no un modo. */
            if (!c->vale || c->periodo_ds == 0U || c->firmeza < 40U) { continue; }
            break;
        case IDENT_POR_SIMBOLO:
            /*
             * Aqui NO se exige que no haya dos tonos, aunque lo parezca.
             * El buscador de dos tonos coge las DOS RAYAS MAS ALTAS, y en
             * un MFSK de veintiuna siempre hay dos mas altas que las
             * demas: con un MFSK sintetico de 21 tonos dice que si. O sea
             * que "hay dos tonos" no significa "es una FSK de dos".
             *
             * Quien manda sobre estas filas no es esa bandera, es el
             * RESULTADO: si alguna fila de dos tonos ha cuadrado de verdad
             * -con su separacion y su velocidad- esa gana y estas se caen.
             * Se hace al final, con las pistas del ancho.
             */
            if (m->simb_x10 == 0U || m->simb_q < 12U) { continue; }
            break;
        case IDENT_POR_SEP:
            /* Dos tonos, y nada mas: la velocidad no se le pide porque a
             * 1200 baudios el medidor no llega. Ver IDENT_POR_SEP. */
            if (!m->hay_tonos || m->sep_hz == 0U) { continue; }
            break;
        case IDENT_POR_TRAMA:
            if (!m->vale || m->periodo_us == 0U) { continue; }
            break;
        case IDENT_POR_PICO:
            /* Una raya sola: mas de la mitad de la potencia en +-8 Hz. Con
             * 57% medido en la grabacion del dueño y 2% en un modem suyo,
             * el 40% parte la diferencia de sobra. */
            if (!m->hay_esp || m->pico_pc < 40U) { continue; }
            break;
        case IDENT_POR_ANCHO:
        default:
            if (!m->hay_esp || m->ancho10_hz == 0U) { continue; }
            /* Y que no sea una portadora: esa ya tiene su fila. */
            if (m->pico_pc >= 40U) { continue; }
            break;
        }

        if (!dentro(m->sep_hz,      f->sep_min,   f->sep_max))   { continue; }
        if (!dentro(m->baudios_x10, f->bd_min,    f->bd_max))    { continue; }
        if (!dentro(m->periodo_us,  f->trama_min, f->trama_max)) { continue; }
        if (!dentro(m->ancho10_hz,  f->ancho_min, f->ancho_max)) { continue; }
        if (!dentro((uint16_t)m->ciclo_pc, (uint16_t)f->ciclo_min,
                    (uint16_t)f->ciclo_max)) { continue; }
        if (!dentro(c->periodo_ds, f->cad_min, f->cad_max)) { continue; }
        if (!dentro((uint16_t)c->ocupa_pc, (uint16_t)f->ocup_min,
                    (uint16_t)f->ocup_max)) { continue; }
        if (!dentro(m->simb_x10, f->sim_min, f->sim_max)) { continue; }
        if (!dentro((uint16_t)m->tonos, (uint16_t)f->ton_min,
                    (uint16_t)f->ton_max)) { continue; }

        salen[n++] = (uint8_t)i;
        if (n >= max) { break; }
    }

    /*
     * LAS PISTAS SE CALLAN CUANDO HAY UN NOMBRE - 05/10/2026.
     *
     * Las filas de IDENT_POR_ANCHO que son pistas -"modem estrecho sin
     * identificar", "podria ser ALE"- cuadran por rangos muy abiertos, y
     * eso esta bien mientras sean lo unico que hay. En cuanto una fila con
     * un criterio fuerte -dos tonos, una trama que se repite, una raya
     * sola- ha dicho un nombre, la pista no añade nada y resta: la
     * pantalla ensena tres renglones y un "STANAG 4285" seguido de "podria
     * ser HFDL" se lee como una duda que no existe.
     *
     * Las tres filas de ancho que SI identifican -WSPR, PSK31, FT8- y la de
     * la imagen no son pistas: se quedan. Se distinguen porque su rango de
     * ancho es estrecho, pero fiarse de eso seria fragil, asi que lo que se
     * mira es el texto que ya tienen: una pista empieza por "Modem" o por
     * "Podria".
     *
     * Se hace aqui y no al construir la tabla porque es una regla sobre el
     * RESULTADO, no sobre las firmas: la misma fila es pista o no segun con
     * quien venga.
     */
    if (n > 1U) {
        uint16_t j, fuertes = 0U, dedos = 0U, k;

        for (j = 0U; j < n; j++) {
            uint8_t c2 = k_firmas[salen[j]].criterio;
            if (c2 == IDENT_POR_TONOS || c2 == IDENT_POR_SEP) { dedos++; }
            /* Una fila de ritmo de simbolo tambien es un nombre, asi que
             * tambien calla a las pistas del ancho. */
            if (c2 != IDENT_POR_ANCHO) { fuertes++; }
        }
        /*
         * Y si una fila de DOS TONOS ha cuadrado, las de ritmo de simbolo
         * se caen: un RTTY tiene velocidad de simbolo igual que un MFSK,
         * pero "170 Hz y 45,45 baudios" es un nombre y "algo que cambia 45
         * veces por segundo" es una familia. Gana el nombre.
         */
        k = 0U;
        for (j = 0U; j < n; j++) {
            const ident_firma_t *f = &k_firmas[salen[j]];
            if (fuertes > 0U && f->criterio == IDENT_POR_ANCHO && es_pista(f)) { continue; }
            if (dedos > 0U && f->criterio == IDENT_POR_SIMBOLO) { continue; }
            salen[k++] = salen[j];
        }
        n = k;
    }

    return n;
}

/*
 * ==========================================================================
 * EL MODO
 * ==========================================================================
 */
static uint8_t  s_activo;
static char     s_lin[IDENT_LINEAS][IDENT_LARGO];
static uint8_t  s_nlin;
static stanag_det_t s_m;
static uint32_t s_medidas;

/*
 * ==========================================================================
 * EL MEDIDOR DE CADENCIA
 * ==========================================================================
 *
 * *** El dueño: "lo primero que he probado es ft8 en 7074, y no es capaz de
 * identificarlo". ***
 *
 * Un cuaderno de energia: una anotacion cada medio segundo, 512 de ellas,
 * o sea los ULTIMOS CUATRO MINUTOS Y PICO. Con eso se ve lo que no cabe en
 * una ventana de 341 ms: que la banda entera se enciende y se apaga a
 * compas.
 *
 * Medio segundo por anotacion no es un numero redondo cualquiera: el ciclo
 * mas corto que hay que reconocer son los 7,5 s del FT4, o sea quince
 * anotaciones. Con una por segundo serian siete y medio, y medio ciclo de
 * error en un periodo de siete no lo distingue de nada.
 *
 * Y 1024 anotaciones -512 segundos- por el WSPR, que tarda 120 s en
 * repetirse. El periodo se mide contando el hueco entre subidas, y para
 * tener DOS huecos que comparar hacen falta tres subidas, o sea 360 s. Con
 * 256 s solo habria uno, y un hueco suelto no es un ciclo, es un salto.
 * Ocho minutos y medio dan cuatro ciclos y margen de sobra.
 *
 * La memoria sale del mismo prestamo que el motor -ver ram_coge()-, no de
 * .bss: dos kilobytes en una radio con mil novecientos bytes libres no se
 * piden al enlazador, se piden prestados a quien no esta.
 */
#define CAD_N       1024U       /* anotaciones: 1024 x 0,5 s = 512 s */
#define CAD_MS       500U       /* cada cuanto se anota */
#define CAD_MUESTRAS (12000U / 2U)   /* 0,5 s a 12 kHz */
#define CAD_MIN      16U        /* lo minimo para intentar medir: 8 s */
#define CAD_HUECOS   16U        /* huecos entre subidas que se guardan */

static float   *s_cad;          /* CAD_N energias, en el prestamo */
static uint16_t s_cad_n;        /* cuantas hay escritas, tope CAD_N */
static uint16_t s_cad_w;        /* donde va la siguiente */
static uint32_t s_cad_acu_n;    /* muestras acumuladas en la de ahora */
static float    s_cad_acu;      /* y su suma de cuadrados */
static uint16_t s_cad_desde;    /* anotaciones desde el ultimo calculo */
static ident_cad_t s_cad_r;

/* Una anotacion cada CAD_MUESTRAS muestras. Corre en la interrupcion de
 * audio, asi que aqui no hay mas que una multiplicacion y una suma. */
static void cad_mete(const float *a, uint16_t n)
{
    uint16_t i;

    if (s_cad == 0) { return; }
    for (i = 0U; i < n; i++) {
        s_cad_acu += a[i] * a[i];
        s_cad_acu_n++;
        if (s_cad_acu_n >= (uint32_t)CAD_MUESTRAS) {
            s_cad[s_cad_w] = s_cad_acu / (float)s_cad_acu_n;
            s_cad_w = (uint16_t)((s_cad_w + 1U) % CAD_N);
            if (s_cad_n < (uint16_t)CAD_N) { s_cad_n++; }
            s_cad_desde++;
            s_cad_acu = 0.0f;
            s_cad_acu_n = 0U;
        }
    }
}

/*
 * Y el calculo, que corre en el bucle principal.
 *
 * SE MIDE EL HUECO ENTRE SUBIDAS, NO LA AUTOCORRELACION. La primera
 * version correlacionaba la secuencia de encendidos consigo misma, que es
 * lo que se hace siempre, y con el FT8 y el FT4 funcionaba. Con el WSPR
 * daba 4 segundos.
 *
 * Y daba 4 segundos por un motivo que no es un error de programacion sino
 * de instrumento: el WSPR esta encendido el 92% del ciclo, o sea que sus
 * apagones duran 9 s y sus encendidos 110. Una onda cuadrada con tramos
 * tan largos correlaciona FORTISIMO consigo misma desplazada un poco -el
 * tramo se solapa con su propio tramo- y el maximo sale en los retardos
 * cortos, no en el periodo. La autocorrelacion mide "cuanto se parece a si
 * misma", y una señal que esta casi siempre encendida se parece a si misma
 * en casi cualquier desplazamiento.
 *
 * Lo que define el ciclo de estos modos no es el parecido: son las
 * SUBIDAS. Todas las emisiones empiezan a la vez, cada 15 s el FT8 y cada
 * 120 el WSPR, y eso se mide contando cuantas anotaciones hay de una
 * subida a la siguiente. Es mas simple, mas barato -no hay bucle de 256
 * retardos- y, sobre todo, no se deja engañar por la duracion del tramo.
 *
 * Tres pasos:
 *
 *  1. El umbral. En lineal no vale: entre el silencio y una emision fuerte
 *     hay cuarenta decibelios y la media lineal la manda el pico. El punto
 *     medio EN DECIBELIOS entre el maximo y el minimo es, en lineal, la
 *     media geometrica de los dos: sqrt(mx*mn). Asi que sale de una raiz y
 *     no de quinientos logaritmos.
 *
 *  2. Las subidas, con rebote. Una anotacion suelta por encima del umbral
 *     en mitad de un apagon es ruido, no una emision: hace falta que dos
 *     seguidas digan lo mismo para cambiar de estado. Sin esto, una banda
 *     con QRM cuenta subidas a docenas.
 *
 *  3. La MEDIANA de los huecos, no la media. Si una emision se pierde, ese
 *     hueco mide el doble y la media se va; la mediana no se entera. Y la
 *     firmeza sale de lo que se separan los huecos entre si: huecos todos
 *     iguales es un ciclo, huecos dispares es casualidad.
 */
static void cad_calcula(void)
{
    uint16_t i, n = s_cad_n;
    float mx = 0.0f, mn = 1e30f, umbral;
    uint32_t on = 0U;
    uint16_t hue[CAD_HUECOS];
    uint8_t  nh = 0U;
    uint16_t ult = 0U;
    uint8_t  hay_ult = 0U;
    uint8_t  est = 0U;      /* el estado con rebote */
    uint8_t  cand = 0U, cuenta = 0U;

    s_cad_r.vale = 0U;
    s_cad_r.segundos = (uint16_t)(((uint32_t)n * CAD_MS) / 1000U);
    if (n < (uint16_t)CAD_MIN || s_cad == 0) { return; }

    for (i = 0U; i < n; i++) {
        uint16_t k = (uint16_t)((s_cad_w + (uint16_t)CAD_N - n + i) % CAD_N);
        float e = s_cad[k];
        if (e < 1e-20f) { e = 1e-20f; }
        if (e > mx) { mx = e; }
        if (e < mn) { mn = e; }
    }
    /* Menos de 6 dB de recorrido es que esto no se apaga: no hay cadencia
     * que medir. 10^(6/10) = 3,98. */
    if (!(mx > mn * 3.98f)) { return; }
    umbral = sqrtf(mx * mn);

    for (i = 0U; i < n; i++) {
        uint16_t k = (uint16_t)((s_cad_w + (uint16_t)CAD_N - n + i) % CAD_N);
        uint8_t  b = (uint8_t)((s_cad[k] > umbral) ? 1U : 0U);

        if (b) { on++; }

        if (b == est) { cuenta = 0U; cand = est; continue; }
        if (b != cand) { cand = b; cuenta = 1U; continue; }
        cuenta++;
        if (cuenta < 2U) { continue; }

        est = b;
        cuenta = 0U;
        if (est == 1U) {                      /* una subida */
            if (hay_ult && nh < (uint8_t)CAD_HUECOS) {
                hue[nh++] = (uint16_t)(i - ult);
            }
            ult = i;
            hay_ult = 1U;
        }
    }

    s_cad_r.ocupa_pc = (uint8_t)((((float)on * 100.0f) / (float)n) + 0.5f);
    if (nh < 2U) { return; }   /* con un solo hueco no hay ciclo, hay un salto */

    /* La mediana, por ordenacion directa: son como mucho dieciseis. */
    for (i = 0U; i < (uint16_t)nh; i++) {
        uint16_t j;
        for (j = (uint16_t)(i + 1U); j < (uint16_t)nh; j++) {
            if (hue[j] < hue[i]) {
                uint16_t t = hue[i]; hue[i] = hue[j]; hue[j] = t;
            }
        }
    }
    {
        uint16_t med = hue[nh / 2U];
        uint16_t dentro = 0U;
        uint32_t suma = 0UL;

        if (med < 8U) { return; }   /* menos de 4 s: ningun modo de estos */

        /*
         * LA MEDIANA DECIDE QUIEN VALE; LA MEDIA DE LOS QUE VALEN DA EL
         * NUMERO - 05/10/2026.
         *
         * *** La radio, en 7.074: "ciclo 15,5 s ocupa 86%". El FT8 va a
         * 15,0. ***
         *
         * Medio segundo de mas, que es EXACTAMENTE una anotacion: el
         * cuaderno apunta cada 0,5 s, asi que un hueco medido solo puede
         * valer 15,0 o 15,5, y la mediana se queda con uno de los dos.
         *
         * Pero los huecos no se equivocan todos en la misma direccion: uno
         * sale largo y el siguiente corto, porque el error es donde cae el
         * corte, no el periodo. Promediando CUATRO huecos el error se
         * reparte y la resolucion baja de medio segundo a un octavo.
         *
         * Se promedian solo los que caen cerca de la mediana, que son los
         * mismos que cuentan para la firmeza: si una emision se pierde, ese
         * hueco mide el doble y la media se iria. La mediana elige, la
         * media afina.
         */
        for (i = 0U; i < (uint16_t)nh; i++) {
            uint16_t d = (hue[i] > med) ? (uint16_t)(hue[i] - med)
                                        : (uint16_t)(med - hue[i]);
            if ((uint32_t)d * 100UL <= (uint32_t)med * 15UL) {
                dentro++;
                suma += hue[i];
            }
        }
        s_cad_r.firmeza = (uint8_t)(((uint32_t)dentro * 100UL) / nh);
        /* x0,5 s -> decimas de segundo, con el redondeo dentro. */
        s_cad_r.periodo_ds = (uint16_t)(((suma * 5UL) + (dentro / 2U)) / dentro);
        s_cad_r.vale = 1U;
    }
}

static void pon(const char *t)
{
    if (s_nlin >= IDENT_LINEAS) { return; }
    (void)snprintf(s_lin[s_nlin], IDENT_LARGO, "%s", t);
    s_nlin++;
}

static void ponf(const char *fmt, int a, int b, int c)
{
    if (s_nlin >= IDENT_LINEAS) { return; }
    (void)snprintf(s_lin[s_nlin], IDENT_LARGO, fmt, a, b, c);
    s_nlin++;
}

static float *ram_coge(uint32_t floats)
{
    uint8_t *b; uint32_t cap = 0U;
    hfdl_ram_coge();
    b = hfdl_ram_todo(&cap);
    if (b == 0 || cap < (floats * 4U)) { hfdl_ram_suelta(); return 0; }
    return (float *)(void *)b;
}

uint8_t ident_modo_start(void)
{
    float *t;

    if (s_activo) { return 1U; }
    ident_modo_borra();
    t = ram_coge(STANAG_DET_FLOATS + CAD_N);
    if (t == 0) { return 0U; }
    if (!stanag_det_init(t, STANAG_DET_FLOATS)) {
        hfdl_ram_suelta();
        return 0U;
    }
    /* El cuaderno de la cadencia va DETRAS del motor, en el mismo
     * prestamo: ver su comentario. */
    s_cad = &t[STANAG_DET_FLOATS];
    s_cad_n = 0U; s_cad_w = 0U; s_cad_acu = 0.0f; s_cad_acu_n = 0U;
    s_cad_desde = 0U;
    memset(&s_cad_r, 0, sizeof s_cad_r);
    s_medidas = 0UL;
    memset(&s_m, 0, sizeof s_m);
    s_activo = 1U;
    return 1U;
}

void ident_modo_stop(void)
{
    if (!s_activo) { return; }
    s_activo = 0U;          /* PRIMERO: calla a la interrupcion */
    s_cad = 0;              /* y el cuaderno, que vive en ese prestamo */
    hfdl_ram_suelta();
}

uint8_t ident_modo_activo(void) { return s_activo; }

void ident_modo_mete(const float *audio, uint16_t n)
{
    if (!s_activo) { return; }
    stanag_det_mete(audio, (uint32_t)n);
    cad_mete(audio, n);
}

void ident_medidas(stanag_det_t *out) { if (out) { *out = s_m; } }
void ident_cadencia(ident_cad_t *out) { if (out) { *out = s_cad_r; } }

/*
 * EL RENGLON DE ARRIBA, QUE ES EL QUE SE LEE.
 *
 * Si cuadra una firma, su nombre. Si cuadran varias, todas separadas por
 * barras mientras quepan. Si no cuadra ninguna, se dice -y debajo quedan
 * los numeros, que es con lo que se puede buscar en el catalogo-.
 */
static void escribe(void)
{
    uint8_t cand[8];
    uint16_t n, i;
    char fila[IDENT_LARGO];

    s_nlin = 0U;

    if (s_medidas == 0UL && !s_cad_r.vale) {
        pon("escuchando...");
        if (s_cad_r.segundos > 0U) {
            ponf("llevo %d s (el WSPR pide 360)",
                 (int)s_cad_r.segundos, 0, 0);
        }
        return;
    }

    n = ident_casa(&s_m, &s_cad_r, cand, 8U);

    if (n == 0U) {
        pon("No se parece a nada que conozca");
    } else {
        for (i = 0U; i < n && s_nlin < 3U; i++) {
            const ident_firma_t *f = &k_firmas[cand[i]];
            (void)snprintf(fila, IDENT_LARGO, "%s%s",
                           f->nombre, f->probada ? "" : " (?)");
            pon(fila);
        }
        /* La nota de la PRIMERA, que es la que mas probabilidades tiene de
         * ser la buena: las demas son el aviso de que no esta cerrado. */
        pon(k_firmas[cand[0]].nota);
        if (n > 1U) {
            pon("(varias cuadran: la señal no las separa)");
        }
    }

    /* Y siempre los numeros, cuadre o no: son lo unico que no opina. */
    ponf("ancho %d  centro %d  pico %d%%",
         (int)s_m.ancho10_hz, (int)s_m.centro_hz, (int)s_m.pico_pc);
    if (s_m.hay_tonos) {
        ponf("tonos %d y %d  salto %d Hz",
             (int)s_m.tono_bajo, (int)s_m.tono_alto, (int)s_m.sep_hz);
    } else {
        pon("un solo tono o fase: no es FSK de dos");
    }
    /*
     * "activo" y no "ciclo": en la pantalla de la radio salia "ciclo 5%" en
     * este renglon y "ciclo 15,5 s" dos mas abajo, y son dos cosas que no
     * tienen nada que ver. Esta mide, dentro de una ventana de 341 ms, que
     * parte del tiempo hay señal por encima del ruido; la otra, cada cuanto
     * se enciende la banda entera. Dos medidas distintas con el mismo
     * nombre son peor que una sin nombre.
     */
    if (s_m.simb_x10 && !s_m.hay_tonos) {
        ponf("simbolo %d,%d Bd  ~%d tonos",
             (int)(s_m.simb_x10 / 10U), (int)(s_m.simb_x10 % 10U),
             (int)s_m.tonos);
    }
    ponf("%d,%d Bd   activo %d%%",
         (int)(s_m.baudios_x10 / 10U), (int)(s_m.baudios_x10 % 10U),
         (int)s_m.ciclo_pc);
    if (s_m.vale) {
        ponf("trama %d,%02d ms  calidad %d%%",
             (int)(s_m.periodo_us / 100U), (int)(s_m.periodo_us % 100U),
             (int)s_m.calidad);
    } else {
        pon("sin trama que se repita");
    }
    /*
     * Y la cadencia, que es la medida LENTA: cada cuanto se enciende y se
     * apaga toda la banda. Siempre sale el tiempo que lleva escuchando,
     * tambien cuando aun no hay ciclo, porque cuatro minutos mirando una
     * pantalla que no dice nada se parecen mucho a una radio rota.
     */
    if (s_cad_r.vale) {
        ponf("ciclo %d,%d s  ocupa %d%%",
             (int)(s_cad_r.periodo_ds / 10U), (int)(s_cad_r.periodo_ds % 10U),
             (int)s_cad_r.ocupa_pc);
    } else {
        ponf("sin ciclo (llevo %d s; WSPR pide 360)", (int)s_cad_r.segundos, 0, 0);
    }
}

void ident_modo_poll(void)
{
    stanag_det_t r;
    uint8_t hay_nuevo = 0U;

    if (!s_activo) { return; }

    /*
     * La cadencia, cada ocho anotaciones -cuatro segundos-. No en cada
     * vuelta: son 512 logaritmos y una autocorrelacion de 256 retardos, y
     * eso cuatro veces por segundo seria tirar el bucle principal por una
     * medida que se mueve cada minuto.
     */
    if (s_cad_desde >= 8U) {
        s_cad_desde = 0U;
        cad_calcula();
        hay_nuevo = 1U;
    }

    if (stanag_det_paso(&r)) {
        s_m = r;
        s_medidas++;
        hay_nuevo = 1U;
    }

    if (hay_nuevo) { escribe(); }
}

uint8_t     ident_modo_lineas(void) { return s_nlin; }

const char *ident_modo_linea(uint8_t i)
{
    if (i >= IDENT_LINEAS) { return ""; }
    return s_lin[i];
}

void ident_modo_borra(void)
{
    uint8_t i;
    for (i = 0U; i < IDENT_LINEAS; i++) { s_lin[i][0] = '\0'; }
    s_nlin = 0U;
    s_medidas = 0UL;
    memset(&s_m, 0, sizeof s_m);
    memset(&s_cad_r, 0, sizeof s_cad_r);
    s_cad_n = 0U; s_cad_w = 0U; s_cad_acu = 0.0f; s_cad_acu_n = 0U;
    s_cad_desde = 0U;
}
