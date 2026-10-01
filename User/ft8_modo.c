#include "ft8_modo.h"
#include "idioma.h"
#include "ft8_decimator.h"
#include "ft8_waterfall_adapter.h"
#include "ft8_fft1024.h"
#include "ft8_decoder.h"
#include "rtc_hw.h"
#include "waterfall.h"
#include <string.h>

/* El contador de milisegundos del SysTick. Se declara aqui, como hace el
 * resto del arbol, porque no tiene cabecera propia. */
extern volatile uint32_t g_msticks;

/*
 * Ver ft8_modo.h para el reparto de trabajo entre la interrupcion y el
 * bucle principal, que es lo que manda en como esta escrito esto.
 */

#define FT8_RANURA_S   15U    /* una ranura de FT8 dura 15 segundos exactos */

static uint8_t  s_on;
static uint8_t  s_seg_ant;
static uint8_t  s_seg;
static uint8_t  s_hay_hora;

/*
 * CUADRE MANUAL DE LA RANURA - 25/09/2026.
 *
 * *** Por el dueno del proyecto: "deberia de permitirme reiniciar el
 * contador si le doy a la chapa de la derecha, para poder cuadrar a mano la
 * tanda", despues de ver que "va desfasada con 6". ***
 *
 * Las ranuras de FT8 empiezan en los segundos 0, 15, 30 y 45 de cada minuto
 * UTC, y hasta aqui la radio daba eso por supuesto: s_seg = segundo del RTC
 * modulo 15, y a correr. Eso vale si el RTC esta en hora de verdad. Si esta
 * corrido unos segundos -que es lo normal cuando se pone a mano-, la radio
 * captura a destiempo y decodifica peor o no decodifica.
 *
 * Poner el RTC entero en hora al segundo es un via crucis con un teclado. En
 * cambio, para FT8 la hora no importa: importa EL SEGUNDO DENTRO DE LA
 * RANURA. Asi que este desfase dice "cuando yo toque, eso es el cero", y con
 * un toque en el momento bueno la ranura queda cuadrada sin tocar el reloj.
 *
 * NO se borra al apagar y encender FT8: quien lo cuadro lo cuadro para esta
 * radio y este cristal, no para esta sesion de escucha.
 */
static uint8_t  s_desfase;   /* 0..FT8_RANURA_S-1, segundos enteros */

/*
 * Y LA PARTE DE DEBAJO DEL SEGUNDO - 25/09/2026.
 *
 * *** Por el dueno del proyecto, viendo la columna DT llena de "+0,8":
 * "arregla lo de promediar el dt". ***
 *
 * El RTC solo da segundos enteros, asi que el desfase de arriba no puede
 * afinar mas. Pero el sesgo que se veia en antena era de 0,8 s, o sea
 * justamente por debajo de lo que ese desfase sabe corregir.
 *
 * Esto es la fraccion: cuando el reloj da el borde de ranura, la captura no
 * arranca en ese instante sino s_ajuste_ms despues, contados con g_msticks.
 * Entre los dos cubren cualquier fase.
 */
static uint16_t s_ajuste_ms;      /* 0..999 */
static uint32_t s_arranca_ms;     /* cuando toca arrancar, si esta pendiente */
static uint8_t  s_pendiente;
static int16_t  s_dt_visto;       /* el ultimo DT medio, para la pantalla */
static uint8_t  s_decodificando;
static uint32_t s_ms_ini;
static uint16_t s_ms_dec;   /* lo que tardo la ultima decodificacion */

static char     s_lin[FT8_MODO_LINEAS][FT8_MODO_LARGO];
static uint32_t s_total;
static uint8_t  s_ultima;

/* Convertir de float a int16 de 256 en 256: la escala del audio de banda
 * lateral es la del ADC (+/-32768, ver demod_am.c), asi que es un recorte y
 * un redondeo, no una conversion de verdad. El trozo es para no pedir un
 * buffer del tamano del bloque, que no se sabe de antemano. */
#define TROZO 256U

/*
 * ¿HAY HORA? Y POR QUE ESTO ES UNA APROXIMACION, DICHO CLARO.
 *
 * FT8 necesita saber cuando empieza cada ranura de 15 s, o sea que necesita
 * los SEGUNDOS bien, con un margen de uno o dos. Y el firmware no puede
 * saber si el reloj esta bien: solo puede saber si alguien lo ha puesto.
 *
 * rtc_hw.c arranca un reloj nuevo en 2026-01-01 00:00:00 y deja una marca en
 * el dominio de respaldo, de forma que reflashear no lo pisa. Asi que "sigue
 * en el dia 1 de enero de 2026" quiere decir "nadie lo ha puesto en hora
 * nunca", y eso si se puede afirmar.
 *
 * Lo que NO se puede afirmar es que un reloj puesto este bien: si esta dos
 * segundos corrido, FT8 no decodificara y aqui pondra "escuchando" tan
 * contento. Por eso el panel ensena ademas el segundo dentro de la ranura -
 * si no cuadra con lo que dice un reloj de verdad, ahi esta el problema.
 */
/*
 * "HAY RELOJ" = ALGUIEN LO HA PUESTO, no "la fecha no es la de fabrica".
 *
 * 30/09/2026, avisado por el dueno: radio nueva, reloj puesto a mano, y FT8
 * seguia diciendo "pon el reloj en hora".
 *
 * La comprobacion de antes era esta:
 *
 *     s_hay_hora = !(t.year == 2026 && t.month == 1 && t.day == 1);
 *
 * o sea, mirar si la FECHA sigue siendo la de arranque en frio. Y el
 * teclado de la hora no toca la fecha: rtc_pon_hora() en main.c escribe
 * hora, minuto y segundo y deja el dia como estaba, con su comentario
 * explicando por que -"la fecha no la sabe nadie aqui: el teclado solo pide
 * horas y minutos"-. Las dos decisiones son razonables por separado y se
 * contradicen en el borde: pones el reloj, la fecha se queda en 2026-01-01,
 * y esto sigue creyendo que no hay hora.
 *
 * Ahora se pregunta a quien lo sabe. rtc_hw_has_ever_synced() existe desde
 * 09/2026 y responde exactamente esto: lo marca rtc_hw_set() -por donde
 * pasan TODOS los caminos que ponen el reloj: el teclado, el RDS, el DCF77-
 * y vive en un registro de respaldo que aguanta el apagado con la pila y
 * que solo se borra si se pierde la VBAT, que es justo cuando el calendario
 * deja de ser de fiar.
 *
 * El caso que se escapaba no era raro: era el de cualquier radio recien
 * salida de fabrica en la que pones la hora a mano. Nunca lo vimos porque
 * en las de aqui la fecha ya venia movida de alguna prueba anterior.
 */
static void mira_reloj(void)
{
    rtc_hw_datetime_t t;

    rtc_hw_get(&t);
    s_hay_hora = (uint8_t)(rtc_hw_has_ever_synced() ? 1U : 0U);
    s_seg = (uint8_t)(((uint16_t)t.second + FT8_RANURA_S
                       - (uint16_t)s_desfase) % FT8_RANURA_S);
}

void ft8_modo_start(float fs_hz)
{
    (void)fs_hz;   /* el decimador lleva la suya compilada: ver FT8_DECIM_INPUT_RATE_HZ */

    memset(s_lin, 0, sizeof s_lin);
    s_total = 0UL;
    s_ultima = 0U;
    s_decodificando = 0U;

    /*
     * LO PRIMERO, ANTES DE ESCRIBIR UN SOLO BYTE: pedir el buffer.
     *
     * Las tres llamadas de aqui debajo ya escriben dentro de la memoria que
     * la cascada cree suya (ver la union de ft8_shared_ram.h). Marcarlo
     * despues dejaria una rendija -corta, pero real- en la que el bucle
     * principal podria colarse a pintar. Se pide antes, y asi no hay rendija
     * que razonar.
     */
    waterfall_presta(1U);
    ft8_decimator_reset_perdidos();
    s_ms_dec = 0U;

    /*
     * LAS TABLAS DE LA FFT, LAS PRIMERAS. Viven dentro de la memoria que
     * acabamos de pedir prestada (ft8_shared_ram.h), o sea que al salir de
     * FT8 se borran y hay que rehacerlas en CADA arranque - no una vez al
     * encender la radio.
     *
     * Esta llamada faltaba en la primera version, y el resultado fue que la
     * radio se colgaba con un pitido continuo en cuanto se entraba en FT8:
     * la tabla de bits invertidos se usa como indice, sin construir contiene
     * pixeles de cascada, y leer con ese indice se sale de la SRAM.
     */
    ft8_fft1024_init();

    ft8_decimator_reset();
    ft8_waterfall_reset();
    ft8_decoder_init();

    mira_reloj();
    s_seg_ant = s_seg;
    s_on = 1U;
}

void ft8_modo_stop(void)
{
    if (!s_on) { return; }
    s_on = 0U;

    /*
     * Y la cascada, de vuelta. NO es un detalle de limpieza: FT8 y el
     * historial de la cascada son el MISMO trozo de memoria (ver la union
     * de ft8_shared_ram.h). Al salir de FT8 ahi dentro hay magnitudes, no
     * pixeles, y si no se borra lo primero que se ve al volver es la tabla
     * de FT8 interpretada como colores.
     */
    waterfall_presta(0U);   /* devuelto: a partir de aqui vuelven a ser pixeles */
    waterfall_init();       /* ...y este memset es lo que los hace pixeles */
}

/*
 * AUTOCUADRE POR EL DT MEDIO.
 *
 * El DT de UNA estacion no dice nada: cada operador tiene su reloj y el suyo
 * puede estar corrido. Lo que habla es que TODAS coincidan - quince
 * estaciones de nueve paises no se ponen de acuerdo para salir todas 0,8 s
 * tarde. Ese sesgo comun es NUESTRO.
 *
 * EL SIGNO, razonado y no probado a ver: time_offset es el bloque de NUESTRA
 * captura donde empieza la emision, asi que DT = (instante en que empezo la
 * emision) - (instante en que empezo nuestra captura). Un DT de +0,8 quiere
 * decir que empezamos 0,8 s ANTES que la emision, o sea que hay que arrancar
 * 0,8 s mas tarde: el ajuste SUMA el DT medio.
 *
 * Si estuviera al reves, se veria enseguida y sin ambiguedad: el DT se
 * doblaria en vez de irse a cero. Por eso el numero sale en la barra.
 *
 * LAS DOS CAUTELAS:
 *  - Hacen falta al menos FT8_DT_MINIMO mensajes. Con uno o dos, lo que se
 *    estaria corrigiendo es el reloj de una persona concreta.
 *  - Por debajo de FT8_DT_ZONA no se toca nada. Sin esa zona muerta, el
 *    ajuste bailaria decima arriba decima abajo en cada tanda persiguiendo
 *    el redondeo, y un reloj que no para quieto es peor que uno corrido.
 */
#define FT8_DT_MINIMO  3     /* mensajes que hacen falta para fiarse */
#define FT8_DT_ZONA  200     /* ms por debajo de los cuales no se toca */

static void cuadra_sola(void)
{
    int dt;
    int32_t total;

    if (ft8_decoder_dt_cuantos() < FT8_DT_MINIMO) { return; }

    dt = ft8_decoder_dt_medio_ms();
    s_dt_visto = (int16_t)dt;

    if (dt > -FT8_DT_ZONA && dt < FT8_DT_ZONA) { return; }

    /* Un solo numero con toda la fase, y luego se reparte en segundos y
     * milisegundos. Asi una correccion negativa que se pase por abajo da la
     * vuelta sola en vez de tener que pensarla en dos trozos. */
    total = ((int32_t)s_desfase * 1000) + (int32_t)s_ajuste_ms + (int32_t)dt;
    total %= (int32_t)(FT8_RANURA_S * 1000);
    if (total < 0) { total += (int32_t)(FT8_RANURA_S * 1000); }

    s_desfase   = (uint8_t)(total / 1000);
    s_ajuste_ms = (uint16_t)(total % 1000);
}

void ft8_modo_cuadra(void)
{
    rtc_hw_datetime_t t;

    /* El cero es AHORA. Se lee el RTC otra vez en vez de usar el s_seg que
     * dejo la ultima vuelta del bucle: entre una cosa y la otra puede haber
     * pasado una decodificacion entera, que son tres o cuatro decimas, y
     * este ajuste va justo de decimas. */
    rtc_hw_get(&t);
    s_desfase = (uint8_t)(t.second % FT8_RANURA_S);
    s_ajuste_ms = 0U;   /* a mano es "ahora", sin fracciones */
    s_pendiente = 0U;

    /* Y se empieza de cero: lo que hubiera capturado a medias pertenece a la
     * ranura vieja, que acaba de dejar de existir. */
    if (s_on) {
        ft8_decimator_reset();
        ft8_waterfall_reset();
    }
    mira_reloj();
    s_seg_ant = s_seg;
}

uint8_t ft8_modo_desfase(void)  { return s_desfase; }
int16_t ft8_modo_dt_ms(void)    { return s_dt_visto; }
uint8_t ft8_modo_activo(void)   { return s_on; }
uint8_t ft8_modo_hay_hora(void) { return s_hay_hora; }
uint8_t ft8_modo_segundo(void)  { return s_seg; }
float ft8_modo_ventana_hz(void)
{
    /* Los bins que salen de la FFT estan a SAMPLE_RATE/NFFT de separacion, y
     * la ventana coge NUM_BINS bins "nativos" de FT8, cada uno de
     * RAW_BIN_STEP de esos. Ver ft8_waterfall_adapter.h. */
    return ((float)FT8_ADAPTER_SAMPLE_RATE_HZ / (float)FT8_ADAPTER_NFFT)
           * (float)(FT8_ADAPTER_NUM_BINS * FT8_ADAPTER_RAW_BIN_STEP);
}

void ft8_modo_borra(void)
{
    memset(s_lin, 0, sizeof s_lin);
    s_ultima = 0U;
    /* s_total NO se pone a cero: es el contador de la sesion, y ademas lo
     * usa main.c para saber si ha llegado algo nuevo. Ponerlo a cero aqui
     * haria que el panel creyera que lo siguiente que llegue es "menos" que
     * lo que ya vio, que es justo la trampa que ft8_poll() ya tuvo que
     * esquivar al arrancar. */
}

uint16_t ft8_modo_ms(void)        { return s_ms_dec; }
uint16_t ft8_modo_perdidos(void)
{
    /* Los dos sitios por donde se puede perder audio, sumados: el
     * remuestreador si el bucle principal tarda mas de 320 ms, y el desvio
     * si la decodificacion tarda mas de 3,84 s. Un solo numero en pantalla
     * porque la respuesta es la misma en los dos casos: si no es cero, a la
     * captura le faltan trozos. */
    return (uint16_t)(ft8_decimator_perdidos() + ft8_waterfall_desbordes());
}

uint16_t ft8_modo_candidatos(void)
{
    int n = ft8_decoder_get_last_num_candidates();
    if (n < 0) { n = 0; }
    if (n > 65535) { n = 65535; }
    return (uint16_t)n;
}

void ft8_modo_db(int16_t *lo, int16_t *hi)
{
    float a = 0.0f, b = 0.0f;

    ft8_waterfall_get_db_range(&a, &b);
    /* Al arrancar, antes de la primera ranura, el adaptador tiene el minimo
     * en +1e9 y el maximo en -1e9 (ver ft8_waterfall_reset()). Eso pintado
     * en pantalla son dos numeros absurdos de diez cifras; aqui se convierte
     * en un cero honrado. */
    if (a > b) { a = 0.0f; b = 0.0f; }
    if (lo) { *lo = (int16_t)a; }
    if (hi) { *hi = (int16_t)b; }
}

uint32_t ft8_modo_total(void)   { return s_total; }
uint8_t  ft8_modo_ultima(void)  { return s_ultima; }

const char *ft8_modo_linea(uint8_t i)
{
    return (i < (uint8_t)FT8_MODO_LINEAS) ? s_lin[i] : "";
}

const char *ft8_modo_estado_txt(void)
{
    if (!s_on)        { return tr("apagado", "off"); }
    if (!s_hay_hora)  { return tr("sin hora", "no clock"); }
    if (s_decodificando) { return tr("decodificando", "decoding"); }
    return tr("escuchando", "listening");
}

void ft8_modo_come(const float *muestras, uint16_t n)
{
    int16_t trozo[TROZO];
    uint16_t hechas = 0U;

    if (!s_on || (muestras == 0) || (n == 0U)) { return; }

    while (hechas < n) {
        uint16_t k = (uint16_t)(n - hechas);
        uint16_t j;

        if (k > (uint16_t)TROZO) { k = (uint16_t)TROZO; }
        for (j = 0U; j < k; j++) {
            float v = muestras[hechas + j];

            if (v >  32767.0f) { v =  32767.0f; }
            if (v < -32768.0f) { v = -32768.0f; }
            trozo[j] = (int16_t)v;
        }
        ft8_decimator_feed_block(trozo, k);
        hechas = (uint16_t)(hechas + k);
    }
}

/* Cuantos milisegundos se le dejan a la decodificacion en cada vuelta del
 * bucle principal. El colchon del remuestreador son 320 ms; 8 es el 2,5%
 * de eso y menos de lo que tarda un cuadro de pantalla, asi que no se nota
 * en la interfaz. Ver el comentario largo en ft8_modo_poll(). */
#define FT8_PASO_MS 8UL

/* Cuantas ranuras se han perdido por estar todavia decodificando la
 * anterior. Era invisible y es justo el numero que dice si esto va bien. */
static uint16_t s_perdidas;

uint16_t ft8_modo_perdidas(void) { return s_perdidas; }

/* Mete una linea nueva arriba y empuja las demas hacia abajo. Doce lineas,
 * asi que mover el bloque entero es mas barato que llevar un indice de
 * anillo y traducirlo cada vez que se pinta. */
static void linea_nueva(const char *txt)
{
    uint8_t i;

    for (i = (uint8_t)(FT8_MODO_LINEAS - 1U); i > 0U; i--) {
        memcpy(s_lin[i], s_lin[i - 1U], FT8_MODO_LARGO);
    }
    (void)memset(s_lin[0], 0, FT8_MODO_LARGO);
    for (i = 0U; (i < (uint8_t)(FT8_MODO_LARGO - 1U)) && (txt[i] != '\0'); i++) {
        s_lin[0][i] = txt[i];
    }
}

void ft8_modo_tick(void)
{
    if (!s_on) { return; }

    /* Lo que la interrupcion dejo a medias: remuestrear y, cuando hay un
     * subbloque entero, su FFT. Barato si no hay nada listo. */
    ft8_decimator_poll();

    mira_reloj();

    /* Principio de ranura: los segundos dan la vuelta de 14 a 0. Se arma
     * aqui y no se toca hasta que el adaptador diga que esta lleno. */
    /*
     * El borde de ranura del reloj no arranca la captura: la PROGRAMA. Lo
     * que arranca de verdad es el bloque de abajo, s_ajuste_ms despues. Con
     * el ajuste a cero se comporta igual que antes, porque entonces el
     * instante programado es este mismo.
     */
    if (s_seg < s_seg_ant) {
        s_arranca_ms = g_msticks + (uint32_t)s_ajuste_ms;
        s_pendiente = 1U;
    }
    s_seg_ant = s_seg;

    if (s_pendiente && ((int32_t)(g_msticks - s_arranca_ms) >= 0)) {
        s_pendiente = 0U;
        ft8_decimator_reset();
        ft8_waterfall_reset();
    }

    /* Mientras se decodifica NO se arma nada nuevo: la captura ya se rearmo
     * al empezar, y is_full() mira la captura viva, no la foto. */
    if (s_decodificando && ft8_waterfall_is_full()) {
        /* La ranura nueva esta lista y la anterior sigue decodificando: se
         * pierde. Se cuenta, que antes no se veia por ningun lado. */
        if (s_perdidas < 0xFFFFU) { s_perdidas++; }
    }
    if (!s_decodificando && ft8_waterfall_is_full()) {
        /*
         * El orden importa y no es el evidente: PRIMERO la foto y volver a
         * armar, DESPUES decodificar. Decodificar se lleva sus buenos
         * cientos de milisegundos, y si se hiciera antes de rearmar, ese
         * rato saldria de la ranura siguiente y la radio se iria quedando
         * atras un poco mas en cada vuelta. Esto es del proyecto padre y
         * esta explicado en el comentario de mag_snapshot en
         * ft8_shared_ram.h - lo descubrieron midiendo contra un servidor de
         * hora, y se cree.
         */
        /*
         * Cerrar la ranura DESVIA la captura a un buffer aparte y deja mag[]
         * congelado para el decodificador. Antes esto copiaba mag[] entero
         * -23.808 bytes- y aqui no se copia nada: ver el comentario de
         * `lado` en ft8_shared_ram.h.
         *
         * Y NO se llama a ft8_waterfall_reset(): reiniciar ahora pondria la
         * captura a escribir en mag[0], que es justo lo que se va a leer.
         * De eso se encarga el desvio.
         */
        ft8_waterfall_cierra_ranura();

        /*
         * Y AQUI NO SE DECODIFICA: aqui se EMPIEZA. Ver el comentario largo
         * de ft8_decoder_slot_empieza().
         *
         * Decodificar entero de una sentada bloqueaba el bucle principal
         * varios segundos, y mientras nadie vaciaba el remuestreador, que
         * solo aguanta 320 ms. A la ranura siguiente le faltaba audio del
         * principio, no llegaba a sus 93 bloques, se reiniciaba en el borde
         * de ranura y no decodificaba. Se veia como "cinco lineas y luego
         * nada": solo salia adelante una tanda de cada varias.
         */
        s_decodificando = 1U;
        s_ms_ini = g_msticks;
        ft8_decoder_slot_empieza();
    }

    /*
     * Un candidato por vuelta, con el remuestreador vaciado ANTES de cada
     * uno. Ese orden es el arreglo entero: lo primero que se hace en cada
     * vuelta es no perder audio; lo segundo, avanzar el trabajo.
     */
    if (s_decodificando) {
        /*
         * VARIOS CANDIDATOS POR VUELTA, CON RELOJ. 27/09/2026.
         *
         * *** El dueno: "revisar ft8 parece que pinta cada 2 rondas". ***
         * Y pintaba cada dos, o cada tres.
         *
         * Aqui se hacia UN candidato por vuelta del bucle principal. El
         * trabajo por candidato es pequeno, pero el bucle principal no va
         * suelto: lo marca el repintado del espectro, unos 69 ms por
         * vuelta. Asi que la decodificacion avanzaba a 14 candidatos por
         * segundo pasara lo que pasara, y con la ventana de 1.600 Hz salen
         * del orden de cien o mas. Cien candidatos a 69 ms son SIETE
         * SEGUNDOS; doscientos, catorce.
         *
         * Y el borde de ranura de arriba dice "si se esta decodificando,
         * no se abre ranura nueva". O sea que en cuanto la decodificacion
         * se pasa de los 15 segundos de una ranura, la siguiente se pierde
         * ENTERA: sale una tanda si y otra no. Que es exactamente lo que
         * se ve.
         *
         * El limite de verdad no es el bucle, es el remuestreador: aguanta
         * 320 ms sin que nadie lo vacie. Asi que en vez de un candidato se
         * hacen los que quepan en 8 ms -un 2,5% de ese colchon, y menos de
         * un cuadro de pantalla-, vaciandolo entre medias igual que antes.
         * Son unos ocho candidatos por vuelta en lugar de uno.
         *
         * Lo que NO se hace es quitar el guardia del borde de ranura:
         * saltarse una ranura de vez en cuando es mejor que decodificar
         * con audio a medias, y ahora casi nunca hace falta.
         */
        uint32_t t0 = g_msticks;
        uint8_t  sigue = 1U;

        ft8_decimator_poll();
        while (sigue && ((uint32_t)(g_msticks - t0) < FT8_PASO_MS)) {
            sigue = ft8_decoder_slot_paso();
            ft8_decimator_poll();
        }

        if (!sigue) {
            ft8_decoded_msg_t m;
            uint8_t n = 0U;

            /* Se acabo: lo capturado a un lado vuelve al principio de mag[]
             * y la captura sigue ahi como si no hubiera pasado nada. */
            ft8_waterfall_reintegra();

            s_decodificando = 0U;
            s_ms_dec = (uint16_t)((g_msticks - s_ms_ini) > 65535UL
                                  ? 65535UL : (g_msticks - s_ms_ini));

            while (ft8_decoder_get_message(&m)) {
                linea_nueva(m.line);
                n++;
            }
            s_ultima = n;
            s_total  = ft8_decoder_get_total_count();

            cuadra_sola();
        }
    }
}
