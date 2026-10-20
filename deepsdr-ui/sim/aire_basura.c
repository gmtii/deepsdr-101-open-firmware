/*
 * BASURA POR LA PUERTA DEL AIRE.
 *
 * POR QUE ESTO Y NO GREP
 * ----------------------
 * La pregunta "¿hay algun indice calculado con datos del aire que escriba
 * fuera de su sitio?" no se contesta leyendo. Son dieciseis decodificadores,
 * cada uno con su maquina de estados, sus anillos y sus contadores, y el
 * indice malo casi nunca esta en la linea que lo escribe: esta tres
 * funciones mas arriba, en un contador que solo se desborda con una
 * secuencia de entrada que a nadie se le ocurre escribir a mano.
 *
 * Grep encuentra los candidatos. Lo que dice si el candidato es un fallo es
 * METERLE LA ENTRADA Y MIRAR. Y para mirar estan el detector de
 * direcciones (-fsanitize=address) y el de comportamiento indefinido
 * (-fsanitize=undefined), que ven lo que ningun ojo ve: la escritura un
 * byte por encima del array, el indice negativo, el desplazamiento de mas
 * de 31 bits, el desbordamiento de un entero con signo.
 *
 * Es el mismo oficio que los bancos _aire.c -meter una grabacion por la
 * misma puerta que el audio de la radio- pero al reves: alli la señal es
 * de verdad y se mira si DECODIFICA; aqui la señal es hostil a proposito y
 * se mira si SOBREVIVE.
 *
 * QUE LE METE
 * -----------
 * Siete clases de entrada, y cada una existe por un motivo distinto:
 *
 *   ruido       ruido uniforme a media escala. El caso base.
 *   fuerte      ruido al limite del margen (+-1,0). Busca desbordes en las
 *               cuentas de energia y en los cuadrados.
 *   recortado   onda cuadrada a +-1,0. El peor caso para cualquier
 *               detector de flancos: flanco en CADA muestra posible.
 *   silencio    todo ceros. Busca divisiones por cero y medias de nada.
 *   continua    una constante distinta de cero. Busca lo mismo que el
 *               silencio pero con energia, que es peor: hay nivel y no hay
 *               informacion, y ahi es donde un estimador de umbral se
 *               divide por su propio rango.
 *   tonos       un tono que salta de frecuencia cada pocas muestras. Imita
 *               una FSK enloquecida: hace trabajar las maquinas de estado
 *               en vez de dejarlas en reposo, que es donde estan los
 *               contadores.
 *   rafagas     silencio y ruido fuerte alternandose con duraciones
 *               aleatorias. Imita el desvanecimiento y los chasquidos, y es
 *               la que mas veces hace entrar y salir de sincronia.
 *
 * Y LOS BLOQUES SON DE TAMAÑO VARIABLE, que no es un detalle: la radio
 * llama siempre con el mismo tamaño de bloque, asi que un fallo que
 * dependa de que el bloque se parta por un sitio raro no sale nunca en
 * funcionamiento normal... hasta que sale. Aqui el tamaño es aleatorio
 * entre 1 y el doble del habitual, incluido 1, que es el que rompe los
 * bucles que dan por hecho que cabe al menos una ventana.
 *
 * LO QUE ESTO NO ES
 * -----------------
 * No es un fuzzer de verdad: no guia la busqueda por cobertura ni guarda
 * casos. Es un barrido ciego con semilla fija, o sea REPETIBLE: si sale
 * rojo, sale rojo otra vez con la misma semilla y se puede depurar.
 *
 * Y NO PRUEBA QUE NO HAYA FALLOS. Prueba que con estas siete entradas y
 * estas semillas no hay ninguno, que es bastante mas de lo que habia antes
 * -que era nada- y bastante menos que una demostracion.
 *
 * SE HA VISTO FALLAR: metiendole a mano un "s_buf[n] = x" con n = el tamaño
 * del array en cw.c, el detector de direcciones lo canta en la primera
 * vuelta con el nombre de la variable y la linea.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "cw.h"
#include "rtty.h"
#include "navtex.h"
#include "dsc.h"
#include "psk31.h"
#include "ais_rx.h"
#include "ale_rx.h"
#include "s4415.h"
#include "stanag_rx.h"
#include "wspr_rx.h"
#include "jtty_rx.h"
#include "ax25.h"
#include "sstv.h"
#include "wefax.h"
#include "hell.h"
#include "hfdl_modo.h"
#include "hfdl_demod_chain.h"   /* el tope de entrada: ver mete_hfdl() */
#include "ft8_modo.h"
/*
 * *** LA SEGUNDA MITAD - 09/10/2026. ***
 *
 * Esto cubria quince decodificadores y dejaba fuera nueve puertas por las
 * que tambien entra el aire. Y no eran las menos peligrosas: stanag_det.c
 * es el motor que mide TODO lo que no se sabe lo que es -o sea, el que mas
 * veces va a ver una señal para la que no fue escrito-, y dcf77.c lleva una
 * maquina de minuto entera manejada por una envolvente ruidosa.
 *
 * Las nueve que entran ahora, y por que cada una:
 *
 *   stanag_det   el motor de medida de STANAG y del modo Ident. Indices de
 *                bin, autocorrelacion y un cuaderno de cadencia, todo
 *                calculado con datos del aire.
 *   dcf77        la maquina de minuto por ENVOLVENTE. Sesenta ranuras y un
 *                indice de segundo que sale de contar flancos.
 *   msf          Anthorn. NO es "DCF77 en otra frecuencia": lleva maquina
 *                propia -cierra_segundo_msf(), decodifica_msf()- porque su
 *                trama va bit a bit y no en ranuras. ENTRO DESPUES QUE LAS
 *                DEMAS, al comprobar a proposito que el banco pudiera
 *                fallar funcion por funcion: con DCF77 y ALS162 puestos, un
 *                fallo plantado a mano en cierra_segundo_msf() salia VERDE.
 *                O sea que esa rama entera no se estaba probando, y el
 *                banco lo decia "ok". De eso sirve verlo fallar.
 *   als162       la misma de DCF77, pero por FASE: otra rama entera.
 *   rbu          fase con tonos - hora_rbu.c, que dcf77.c llama por dentro.
 *   wwv          onda corta - hora_wwv.c, idem. Con BPM van las cuatro
 *                emisoras de la tabla cubiertas.
 *   rds          el unico que no come audio de 12 kHz sino el multiplex de
 *                192: bloques, sincronismo de grupo y el cuaderno de la
 *                hora.
 *   nr_ss        no es un decodificador, pero es la unica cosa del camino
 *                del audio que hace una FFT con lo que le llega y vuelve a
 *                escribir encima. Si algo indexa mal con ruido, aqui.
 *   anotch       el notch automatico: busca el tono mas fuerte y mueve un
 *                filtro con esa medida.
 *
 * LAS DE "HORA" SE METEN POR dcf77_feed(), que es por donde pasan de
 * verdad: hora_rbu.c y hora_wwv.c no se alimentan solas en la radio, las
 * llama dcf77.c segun la emisora. Probarlas por su puerta trasera seria
 * probar algo que nadie llama asi.
 */
#include "stanag_det.h"
#include "dcf77.h"
#include "rds.h"
#include "nr_ss.h"
#include "anotch.h"
#include "wspr_msg.h"

#define FS        12000.0f
#define SEGUNDOS  6          /* por clase de entrada y por decodificador */
#define BLOQUE_MAX 512U

/* ---- numeros aleatorios propios, para que el banco sea repetible ---- */
static uint32_t g_s;
static uint32_t azar(void)
{
    g_s ^= g_s << 13; g_s ^= g_s >> 17; g_s ^= g_s << 5;
    return g_s;
}
static float unif(void)   /* -1,0 .. +1,0 */
{
    return ((float)(azar() & 0xFFFFU) / 32767.5f) - 1.0f;
}

enum { E_RUIDO = 0, E_FUERTE, E_RECORTADO, E_SILENCIO, E_CONTINUA,
       E_TONOS, E_RAFAGAS, E_N };

static const char *k_nombre[E_N] = {
    "ruido", "fuerte", "recortado", "silencio", "continua", "tonos", "rafagas"
};

/* Estado de los generadores que tienen memoria entre bloques. */
static double s_fase;
static float  s_tono = 800.0f;
static uint32_t s_rafaga_quedan;
static uint8_t  s_rafaga_alta;

static void genera(int clase, float *x, uint32_t n)
{
    uint32_t i;

    switch (clase) {
    case E_RUIDO:
        for (i = 0U; i < n; i++) { x[i] = unif() * 0.3f; }
        break;
    case E_FUERTE:
        for (i = 0U; i < n; i++) { x[i] = unif(); }
        break;
    case E_RECORTADO:
        for (i = 0U; i < n; i++) { x[i] = ((azar() & 1U) != 0U) ? 1.0f : -1.0f; }
        break;
    case E_SILENCIO:
        for (i = 0U; i < n; i++) { x[i] = 0.0f; }
        break;
    case E_CONTINUA:
        for (i = 0U; i < n; i++) { x[i] = 0.75f; }
        break;
    case E_TONOS:
        for (i = 0U; i < n; i++) {
            if ((azar() & 0x3FU) == 0U) {
                /* salto de frecuencia, incluidos extremos absurdos */
                s_tono = 50.0f + (float)(azar() % 5900U);
            }
            s_fase += (2.0 * 3.14159265358979 * (double)s_tono) / (double)FS;
            if (s_fase > 6.283185307) { s_fase -= 6.283185307; }
            x[i] = (float)sin(s_fase) * 0.8f;
        }
        break;
    case E_RAFAGAS:
    default:
        for (i = 0U; i < n; i++) {
            if (s_rafaga_quedan == 0U) {
                s_rafaga_alta = (uint8_t)(azar() & 1U);
                s_rafaga_quedan = 1U + (azar() % 2000U);
            }
            s_rafaga_quedan--;
            x[i] = s_rafaga_alta ? unif() : 0.0f;
        }
        break;
    }
}

/* ---- los quince, cada uno con su arranque y su puerta de entrada ---- */
/*
 * Cada decodificador arranca a su manera -unos con _init(void), otros con
 * _start(fs), y cuatro con un bloque de RAM prestado- asi que cada uno
 * lleva su envoltorio. Los bloques prestados se piden del tamaño EXACTO que
 * pide su cabecera y no mas: si uno escribe un byte por encima del bloque
 * que le dieron, el detector de direcciones lo tiene que cantar, y con un
 * bloque de sobra no lo cantaria.
 */
typedef void (*mete_fn)(const float *, uint32_t);
typedef void (*arranca_fn)(void);

static float    s_ram_4415[S4415_FLOATS];
static float    s_ram_stanag[STANAG_RX_FLOATS];
static uint8_t  s_ram_wspr[WSPR_RX_RAM];
static jtty_rx_ram_t s_ram_jtty;

static void a_cw(void)     { cw_init(); }
static void a_rtty(void)   { rtty_init(); }
static void a_navtex(void) { navtex_start(FS); }
static void a_dsc(void)    { dsc_start(FS); }
static void a_psk31(void)  { psk31_start(FS); }
static void a_ais(void)    { ais_rx_start(FS); }
static void a_ale(void)    { ale_rx_start(FS); }
static void a_ax25(void)   { ax25_start(FS); }
static void a_sstv(void)   { sstv_start(FS); }
static void a_wefax(void)  { wefax_start(FS); }
static void a_hell(void)   { hell_start(FS); }
static void a_4415(void)   { (void)s4415_init(s_ram_4415, S4415_FLOATS); }
static void a_stanag(void) { (void)stanag_rx_init(s_ram_stanag, STANAG_RX_FLOATS); }
static void a_wspr(void)   { wspr_rx_init(s_ram_wspr, (uint32_t)WSPR_RX_RAM); }
static void a_jtty(void)   { jtty_rx_start(&s_ram_jtty, FS); }

static float s_ram_det[STANAG_DET_FLOATS];

static void a_det(void)    { (void)stanag_det_init(s_ram_det, STANAG_DET_FLOATS); }
static void a_nr(void)     { nr_ss_init(); nr_ss_set_strength(100U); }
static void a_notch(void)  { anotch_init(FS); anotch_set_activo(1U); anotch_set_fuerza(100U); }
static void a_rds(void)    { rds_start(192000.0f); }

/*
 * ======================================================================
 * LOS DOS QUE FALTABAN: HFDL Y FT8 - 10/10/2026
 * ======================================================================
 *
 * Y son justo los dos mas grandes. HFDL lleva una cadena entera de demodulo
 * -mezclador, filtro adaptado, sincronismo de simbolo, Costas, ecualizador,
 * Viterbi, desentrelazado, CRC- y FT8 arrastra la biblioteca ft8_lib con su
 * busqueda de candidatos y su LDPC. Son los que mas indices calculan con
 * datos del aire, o sea los que mas falta hacia meter aqui.
 *
 * POR QUE SE HABIAN QUEDADO FUERA: los dos viven dentro de la memoria
 * prestada de la cascada (ver ft8_shared_ram.h), y meterlos pedia el tapon
 * de sim/waterfall_prestamo.c... que YA ESTABA en este banco desde el 09/10,
 * porque WSPR lo necesita. O sea que no habia ninguna razon tecnica: se
 * quedaron fuera y nadie volvio.
 *
 * DOS DETALLES QUE NO SON OBVIOS:
 *
 *  1. HFDL NO COME AUDIO MONO: come I y Q. Aqui se le da la misma basura en
 *     los dos canales, que ademas es mas hostil que dos independientes -una
 *     señal perfectamente correlada entre I y Q es una recta en la
 *     constelacion, o sea el caso degenerado del ecualizador y del Costas-.
 *
 *  2. LOS DOS SE PISAN LA MEMORIA. hfdl_ram.c reparte el mismo bloque que
 *     usa FT8, con un cerrojo. Asi que hay que PARAR uno antes de arrancar
 *     el otro, y por eso cada arranque para los dos primero: si no, el
 *     segundo no coge la memoria y el banco se quedaria probando una
 *     maquina de estados que no ha arrancado - verde y sin probar nada, que
 *     es el peor resultado posible.
 */
static void a_hfdl(void)
{
    ft8_modo_stop();
    hfdl_modo_stop();
    (void)hfdl_modo_start();
}

static void a_ft8(void)
{
    hfdl_modo_stop();
    ft8_modo_stop();
    ft8_modo_start(FS);
}

/*
 * ======================================================================
 * LA BASURA QUE NO ENTRABA - 11/10/2026
 * ======================================================================
 *
 * Y el primero estaba aqui dentro.
 *
 * Este banco genera bloques de longitud ALEATORIA entre 1 y 512 -eso es a
 * proposito y es bueno: una longitud rara es justo lo que rompe los
 * indices-. Pero tres de los decodificadores de la tabla tienen CONTRATO
 * DE TAMANO FIJO y rechazan en silencio cualquier otra longitud:
 *
 *     cw_process()    n != 32  -> return   (cw.c:1478)
 *     rtty_process()  n != 32  -> return   (rtty.c:528)
 *     hfdl_modo_mete() n > 64  -> return   (hfdl_modo.c:1112)
 *
 * Para nr_ss SI se escribio un troceador -mete_nr(), aqui arriba, con el
 * contrato citado en su comentario-. Para estos tres no. Medido metiendo
 * un contador en este mismo fichero y corriendolo de verdad:
 *
 *     llamadas totales    53.210
 *     muestras totales    13.608.000
 *     n==32  (cw, rtty)      107 llamadas (0,201 %),   3.424 muestras (0,025 %)
 *     n<=64  (hfdl)        6.717 llamadas (12,62 %), 218.544 muestras (1,61 %)
 *
 * O sea que al 99,97 % de la basura destinada a cw y a rtty se le daba con
 * la puerta en las narices, y al 98,4 % de la de HFDL. Y el banco imprimia
 * "13 millones de muestras metidas, ni una escritura fuera de sitio" y un
 * "ok" por cada clase de entrada.
 *
 * Lo grave no es el numero: es QUE decodificadores eran. La cabecera de
 * este fichero presume de HFDL y FT8 diciendo que son "los que mas indices
 * calculan con datos del aire, o sea los que mas falta hacia meter aqui".
 * HFDL arrastra mezclador, filtro adaptado, symsync, Costas, ecualizador y
 * Viterbi, y llevaba desde el 10/10 declarandose cubierto habiendo visto
 * el 1,6 % de la entrada.
 */
/*
 * LO QUE DE VERDAD LE LLEGA AL MODULO.
 *
 * Esta es la parte que habria cazado lo de arriba el primer dia, y por eso
 * se queda puesta para siempre: cada envoltorio apunta cuantas muestras ha
 * conseguido meter DENTRO, y al final de cada decodificador se compara con
 * las que se generaron. Un banco que no comprueba que su propio estimulo
 * llega no esta probando nada - solo esta gastando tiempo.
 *
 * Es el mismo fallo que el del banco del NR (sim/nr_curva.c, 25/09): violar
 * el contrato del modulo que mides no da error, da NUMEROS.
 */
static int fallos;
static uint64_t g_entregadas;

/* navtex y dsc aceptan cualquier longitud -son de muestra a muestra, ver
 * navtex.c:883 y dsc.c:734-, asi que aqui el envoltorio solo apunta. Pero
 * se pone igual: si manana a uno de los dos le sale un contrato de tamano,
 * la cuenta lo dice en vez de callarselo. */
/*
 * Los que aceptan cualquier longitud -todos estos son de muestra a
 * muestra, no llevan ventana de bloque- pasan por un envoltorio que solo
 * apunta. Para ellos la cuenta sale siempre al 100 % y no mide nada del
 * modulo: esta puesta como ALAMBRE TRAMPA, para el dia en que uno de ellos
 * crezca un contrato de tamano y alguien se olvide de trocear. Ese dia la
 * cuenta se cae y lo dice, en vez de pintar "ok" como pasaba con cw, rtty
 * y hfdl.
 */
#define DIRECTO(envoltorio, funcion)                         \
    static void envoltorio(const float *x, uint32_t n)       \
    {                                                        \
        funcion(x, n);                                       \
        g_entregadas += n;                                   \
    }

DIRECTO(mete_navtex, navtex_process)
DIRECTO(mete_dsc,    dsc_process)
DIRECTO(mete_psk31,  psk31_process)
DIRECTO(mete_ais,    ais_rx_mete)
DIRECTO(mete_ale,    ale_rx_mete)
DIRECTO(mete_4415,   s4415_mete)
DIRECTO(mete_stanag, stanag_rx_mete)
DIRECTO(mete_jtty,   jtty_rx_mete)
DIRECTO(mete_ax25,   ax25_process)
DIRECTO(mete_sstv,   sstv_process)
DIRECTO(mete_wefax,  wefax_process)
DIRECTO(mete_hell,   hell_process)
DIRECTO(mete_det,    stanag_det_mete)
DIRECTO(mete_rds,    rds_feed)

static void mete_cw(const float *x, uint32_t n)
{
    uint32_t hecho = 0U;
    /* Se trocea a lo que el modulo admite, igual que hace demod_am.c con
     * su cw_process_chunks(). Lo que sobra del bloque se tira: este banco
     * mide escrituras fuera de sitio, no continuidad. */
    while ((n - hecho) >= CW_BLOCK_SAMPLES) {
        cw_process(&x[hecho], CW_BLOCK_SAMPLES);
        hecho += CW_BLOCK_SAMPLES;
        g_entregadas += CW_BLOCK_SAMPLES;
    }
}

static void mete_rtty(const float *x, uint32_t n)
{
    uint32_t hecho = 0U;
    while ((n - hecho) >= RTTY_BLOCK_SAMPLES) {
        rtty_process(&x[hecho], RTTY_BLOCK_SAMPLES);
        hecho += RTTY_BLOCK_SAMPLES;
        g_entregadas += RTTY_BLOCK_SAMPLES;
    }
}

static void mete_hfdl(const float *x, uint32_t n)
{
    uint32_t hecho = 0U;
    /* La misma basura por I y por Q: ver el punto 1 de arriba.
     *
     * Y troceada al tope del modulo. Aqui NO se tira el resto: el tope es
     * un maximo, no un tamano exacto, asi que el ultimo trozo entra con lo
     * que quede - que ademas es la longitud mas rara de todas, y por eso
     * la mas interesante. */
    while (hecho < n) {
        uint32_t t = n - hecho;
        if (t > (uint32_t)HFDL_DEMOD_CHAIN_MAX_INPUT_BLOCK) {
            t = (uint32_t)HFDL_DEMOD_CHAIN_MAX_INPUT_BLOCK;
        }
        hfdl_modo_mete(&x[hecho], &x[hecho], t);
        hecho += t;
        g_entregadas += t;
    }
}

static void mete_ft8(const float *x, uint32_t n)
{
    /* ft8_modo_come() toma uint16_t: se parte, que es ademas mas hostil. */
    uint32_t hecho = 0U;
    while (hecho < n) {
        uint32_t t = n - hecho; if (t > 1000U) { t = 1000U; }
        ft8_modo_come(&x[hecho], (uint16_t)t);
        hecho += t;
        g_entregadas += t;
    }
}

static void p_hfdl(void)
{
    hfdl_modo_poll();
    /* Y que se lean las filas: el panel indexa con un contador que sube con
     * las tramas, o sea con datos del aire. */
    {
        uint32_t i;
        for (i = 0U; i < 4U; i++) { (void)hfdl_modo_linea(i); }
    }
    /*
     * Y LA LINEA DE DIAGNOSTICO - 16/10/2026. La arma a mano sobre un
     * buffer de 58 bytes y hasta hoy no la llamaba nadie desde aqui, asi
     * que el camino del aire no la tocaba nunca. Los extremos los mide
     * sim/hfdl_crc_linea.c; esto solo se asegura de que entre tambien por
     * el camino de verdad.
     */
    (void)hfdl_modo_linea_crc();
}

static void p_ft8(void)
{
    /* tick() es quien cierra la ranura y llama al decodificador entero:
     * busqueda de candidatos, LDPC y desempaquetado del mensaje. */
    ft8_modo_tick();
    {
        uint8_t i;
        for (i = 0U; i < 4U; i++) { (void)ft8_modo_linea(i); }
    }
}

/*
 * LA TASA DE dcf77_feed() ES LA DE LA RADIO, no FS.
 *
 * A dcf77.c no le llega el audio: le llega UNA muestra (o unas pocas) por
 * bloque de 256 a 96 kHz, o sea 375 Hz por submuestra. Arrancarlo a 12.000
 * seria decirle que el segundo dura 12.000 muestras cuando en la radio dura
 * 375, y entonces la maquina de minuto no llegaria a dar una vuelta en todo
 * el banco: se probaria el arranque y nada mas.
 *
 * Con la tasa de verdad, los 72.000 valores de cada tanda son 192 segundos
 * -tres minutos largos-, asi que la maquina de minuto entra, se rompe y se
 * vuelve a sincronizar varias veces por tanda, que es donde estan los
 * contadores que interesan.
 */
#define DCF_FS(e)  (96000.0f / 256.0f * (float)dcf77_submuestras(e))

static void a_dcf(void)    { dcf77_start(HORA_DCF77,  DCF_FS(HORA_DCF77));  }
static void a_msf(void)    { dcf77_start(HORA_MSF,    DCF_FS(HORA_MSF));    }
static void a_als(void)    { dcf77_start(HORA_ALS162, DCF_FS(HORA_ALS162)); }
static void a_rbu(void)    { dcf77_start(HORA_RBU,    DCF_FS(HORA_RBU));    }
static void a_wwv(void)    { dcf77_start(HORA_WWV10,  DCF_FS(HORA_WWV10));  }
static void a_bpm(void)    { dcf77_start(HORA_BPM,    DCF_FS(HORA_BPM));    }

/* dcf77_feed() va muestra a muestra: el envoltorio solo desenrolla. */
static void mete_dcf(const float *x, uint32_t n)
{
    uint32_t i;
    for (i = 0U; i < n; i++) { dcf77_feed(x[i]); }
    g_entregadas += n;
}

/*
 * nr_ss y anotch ESCRIBEN ENCIMA de lo que se les da, asi que el bloque se
 * copia antes: el generador tiene memoria entre bloques y dejarle el
 * buffer pisado cambiaria la señal siguiente, que es justo lo que hace que
 * un banco con semilla fija deje de ser repetible.
 *
 * Y nr_ss_process() exige EXACTAMENTE NR_SS_BLOCK_SAMPLES por llamada (ver
 * su cabecera), asi que aqui se trocea igual que lo hace demod_am.c. Lo que
 * sobra del bloque se tira: el banco mide escrituras fuera de sitio, no
 * continuidad.
 */
static float s_copia[BLOQUE_MAX];

static void mete_nr(const float *x, uint32_t n)
{
    uint32_t hecho = 0U;
    memcpy(s_copia, x, n * sizeof(float));
    while ((n - hecho) >= NR_SS_BLOCK_SAMPLES) {
        nr_ss_process(&s_copia[hecho], NR_SS_BLOCK_SAMPLES);
        hecho += NR_SS_BLOCK_SAMPLES;
        g_entregadas += NR_SS_BLOCK_SAMPLES;
    }
}

static void mete_notch(const float *x, uint32_t n)
{
    memcpy(s_copia, x, n * sizeof(float));
    anotch_process(s_copia, n);
    g_entregadas += n;
}

/*
 * ==========================================================================
 * *** LA MITAD QUE ESTE BANCO NO PROBABA: EL BUCLE PRINCIPAL - 09/10/2026 ***
 * ==========================================================================
 *
 * ESTO NO ES UNA AMPLIACION, ES UN AGUJERO TAPADO. Y lo encontre al
 * comprobar que el banco pudiera fallar en cada funcion nueva: con
 * stanag_det metido y el banco diciendo "ok", un fallo plantado a mano en
 * mide_espectro(), en mide_simbolos(), en cr7_pasada() y en mide_rachas()
 * -las CUATRO funciones que miden- salia VERDE. Las cuatro.
 *
 * La razon vale para casi todos los decodificadores de esta radio, no solo
 * para stanag_det: estan partidos en dos mitades a proposito.
 *
 *   la mitad de la INTERRUPCION   *_mete() / *_process(): barata, mete en
 *                                 un anillo y como mucho decide si hay algo
 *   la mitad del BUCLE PRINCIPAL  *_paso() / *_saca() / *_poll(): cara, y
 *                                 es donde estan la FFT de 1024, el Viterbi
 *                                 de 512 estados, el Fano, el ecualizador y
 *                                 las correlaciones
 *
 * El banco llamaba SOLO a la primera. O sea que el codigo con mas indices
 * calculados de toda la radio -justo el que esto existe para vigilar- no
 * habia visto nunca una entrada hostil, y la pantalla decia "ok" en verde.
 *
 * Un banco que mide media cosa y dice "todo correcto" es peor que no
 * tenerlo, porque el verde se cree.
 *
 * SE LLAMAN AL RITMO DE LA RADIO: una vez por bloque metido, que es
 * exactamente lo que hace el bucle principal de main.c. Llamarlas menos
 * dejaria el anillo desbordarse (que tambien es una entrada valida, pero
 * no la que se quiere probar aqui); llamarlas mas no cuesta nada porque
 * todas se van de vacio si no hay trabajo.
 */
static void p_det(void)
{
    stanag_det_t m;
    (void)stanag_det_paso(&m);
}

static void p_wspr(void)
{
    /* Los pasos de la ventana y, cuando se cierra, el Fano entero. */
    (void)wspr_rx_pasos_pendientes();
    if (wspr_rx_listo()) {
        wspr_msg_t m; int16_t hz; uint8_t q;
        (void)wspr_rx_decodifica(&m, &hz, &q);
    }
}

static void p_jtty(void)
{
    jtty_rx_r_t r;
    uint8_t i;
    /* Dos por pasada, igual que jtty_modo.c. */
    for (i = 0U; i < 2U && jtty_rx_saca(&r); i++) { }
}

static void p_ale(void)
{
    uint32_t palabra; uint8_t cal;
    while (ale_rx_saca(&palabra, &cal)) { }
}

static void p_ais(void)
{
    static uint8_t bits[AIS_BYTES_MAX];
    uint16_t nbits;
    while (ais_rx_saca(bits, (uint16_t)AIS_BYTES_MAX, &nbits)) { }
}

static void p_stanag(void) { (void)stanag_rx_paso(); }
static void p_4415(void)   { (void)s4415_paso();     }
static void p_dsc(void)    { dsc_poll();             }

static void mete_wspr(const float *x, uint32_t n)
{
    /* wspr_rx_mete() toma uint16_t: se parte, que es ademas una entrada
     * mas hostil que un bloque entero. */
    uint32_t hecho = 0U;
    while (hecho < n) {
        uint32_t t = n - hecho; if (t > 1000U) { t = 1000U; }
        wspr_rx_mete(&x[hecho], (uint16_t)t);
        hecho += t;
        g_entregadas += t;
    }
}

typedef void (*poll_fn)(void);
/* `poll` a 0 = este decodificador no tiene mitad de bucle principal. */
/*
 * `segs` a 0 = los SEGUNDOS de todos. Hoy no lo usa nadie, y la historia
 * de por que esta aqui vale mas que el campo:
 *
 * EL FANO DE WSPR NO SE PROBABA. Su ventana son 110,6 segundos y hasta que
 * no se cierra, wspr_rx_listo() dice que no y wspr_rx_decodifica() se va de
 * vacio por la puerta. Con los 6 segundos de todos, un fallo plantado a
 * mano dentro de wspr_rx_decodifica() salia VERDE: el decodificador entero
 * -el Fano, la busqueda sobre 324 pasos, el desentrelazado- no lo veia
 * nadie.
 *
 * El primer arreglo fue este campo, con wspr a 130 segundos. Y MEDIDO sale
 * mal: son siete ventanas por semilla, y a -O1 con los dos detectores eso
 * pasa de dos minutos POR SEMILLA. El banco de "comprueba" va con veinte.
 *
 * El arreglo bueno estaba ya escrito y apagado. sim/wspr_aire.c tiene un
 * caso -"lo que NO puede pasar: inventarse una baliza"- que mete OCHO
 * ventanas enteras de ruido puro por wspr_rx_mete() y llama a
 * wspr_rx_decodifica() en cada una. Es exactamente esta prueba, y estaba
 * compilandose SIN los detectores. Encenderselos cuesta DOS SEGUNDOS (17 ->
 * 19) y cubre lo que aqui costaba minutos. Ver out/wspraire en local.mk.
 *
 * El campo se queda porque la pregunta "¿cuanta señal hace falta para que
 * esta puerta llegue a abrirse?" se la va a volver a hacer el siguiente
 * decodificador con ventana larga, y entonces la respuesta esta aqui.
 */
typedef struct { const char *nombre; arranca_fn ini; mete_fn mete;
                 poll_fn poll; uint16_t segs; } deco_t;

static const deco_t k_deco[] = {
    { "cw",        a_cw,     mete_cw,         0         },
    { "rtty",      a_rtty,   mete_rtty,       0         },
    { "navtex",    a_navtex, mete_navtex,     0         },
    { "dsc",       a_dsc,    mete_dsc,        p_dsc     },
    { "psk31",     a_psk31,  mete_psk31,      0         },
    { "ais",       a_ais,    mete_ais,        p_ais     },
    { "ale",       a_ale,    mete_ale,        p_ale     },
    { "s4415",     a_4415,   mete_4415,       p_4415    },
    { "stanag_rx", a_stanag, mete_stanag,     p_stanag  },
    { "wspr",      a_wspr,   mete_wspr,       p_wspr    },
    { "jtty",      a_jtty,   mete_jtty,       p_jtty    },
    { "ax25",      a_ax25,   mete_ax25,       0         },
    { "sstv",      a_sstv,   mete_sstv,       0         },
    { "wefax",     a_wefax,  mete_wefax,      0         },
    { "hell",      a_hell,   mete_hell,       0         },
    /* --- la segunda mitad, 09/10/2026. Ver los includes de arriba. --- */
    { "stanag_det", a_det,   mete_det,        p_det     },
    { "dcf77",     a_dcf,    mete_dcf,        0         },
    { "msf",       a_msf,    mete_dcf,        0         },
    { "als162",    a_als,    mete_dcf,        0         },
    { "rbu",       a_rbu,    mete_dcf,        0         },
    { "wwv",       a_wwv,    mete_dcf,        0         },
    { "bpm",       a_bpm,    mete_dcf,        0         },
    { "rds",       a_rds,    mete_rds,        0         },
    { "nr_ss",     a_nr,     mete_nr,         0         },
    { "anotch",    a_notch,  mete_notch,      0         },
    /* --- los dos grandes, 10/10/2026. Ver sus adaptadores arriba. --- */
    { "hfdl",      a_hfdl,   mete_hfdl,       p_hfdl    },
    { "ft8",       a_ft8,    mete_ft8,        p_ft8     },
};

int main(int argc, char **argv)
{
    static float x[BLOQUE_MAX];
    const uint32_t nd = (uint32_t)(sizeof k_deco / sizeof k_deco[0]);
    uint32_t d, c, semilla, nsem = 3U;
    uint64_t total = 0U;

    if (argc > 1) { nsem = (uint32_t)atoi(argv[1]); if (nsem == 0U) { nsem = 1U; } }

    printf("\nBASURA POR LA PUERTA DEL AIRE\n");
    printf("%u decodificadores x %u clases de entrada x %u semillas,"
           " %d s cada uno\n", (unsigned)nd, (unsigned)E_N, (unsigned)nsem,
           SEGUNDOS);
    printf("con -fsanitize=address,undefined: si algo escribe fuera, aborta"
           " aqui con el nombre y la linea\n\n");

    for (d = 0U; d < nd; d++) {
        uint64_t gen_d = 0U;

        printf("  %-10s", k_deco[d].nombre);
        fflush(stdout);
        g_entregadas = 0U;
        for (c = 0U; c < (uint32_t)E_N; c++) {
            for (semilla = 0U; semilla < nsem; semilla++) {
                uint16_t segs = k_deco[d].segs ? k_deco[d].segs
                                               : (uint16_t)SEGUNDOS;
                uint32_t quedan = (uint32_t)(FS * (float)segs);

                g_s = 0x1234567U + semilla * 2654435761U
                      + d * 40503U + c * 97U;
                if (g_s == 0U) { g_s = 1U; }
                s_fase = 0.0; s_tono = 800.0f;
                s_rafaga_quedan = 0U; s_rafaga_alta = 0U;

                /* Reinicio entre tandas: un decodificador que solo se
                 * rompe arrancando en medio de una rafaga tiene que
                 * tener la oportunidad de hacerlo. */
                k_deco[d].ini();

                while (quedan > 0U) {
                    uint32_t n = 1U + (azar() % BLOQUE_MAX);
                    if (n > quedan) { n = quedan; }
                    genera((int)c, x, n);
                    k_deco[d].mete(x, n);
                    /* Y la mitad cara, al ritmo de la radio: una vez por
                     * bloque, igual que el bucle principal de main.c. Ver
                     * el comentario de p_det() y compania. */
                    if (k_deco[d].poll != 0) { k_deco[d].poll(); }
                    quedan -= n;
                    total += n;
                    gen_d += n;
                }
            }
            printf(" %s", k_nombre[c]);
            fflush(stdout);
        }
        /*
         * Y LO ULTIMO, QUE ES LO QUE DE VERDAD SUJETA: que la basura haya
         * ENTRADO. Ver el comentario de g_entregadas.
         *
         * El tope es el 90 %: los troceadores tiran el resto del bloque
         * que no llega a un trozo entero -eso es correcto y esta
         * explicado arriba-, y con bloques de 1 a 512 contra trozos de 32
         * eso son unas 16 muestras de media por bloque, bastante menos del
         * 10 %. Si alguna vez baja de ahi es que alguien ha vuelto a
         * saltarse un contrato, y entonces lo dice en vez de pintar "ok".
         */
        {
            double pct = (gen_d > 0U)
                       ? (100.0 * (double)g_entregadas / (double)gen_d) : 0.0;
            if (pct < 90.0) {
                printf("   MAL: solo le ha entrado el %.2f %% de la basura"
                       " (%llu de %llu muestras)\n", pct,
                       (unsigned long long)g_entregadas,
                       (unsigned long long)gen_d);
                fallos++;
            } else {
                printf("   ok  (entra el %.1f %%)\n", pct);
            }
        }
    }

    printf("\n%llu millones de muestras metidas, ni una escritura fuera de"
           " sitio\n", (unsigned long long)(total / 1000000U));
    if (fallos != 0) {
        printf("BASURA: %d FALLOS\n\n", fallos);
        return 1;
    }
    printf("BASURA: todo correcto\n\n");
    return 0;
}
