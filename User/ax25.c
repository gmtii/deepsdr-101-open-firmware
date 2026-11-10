/*
 * Paquete AX.25 / APRS. Ver ax25.h para el porque de cada pieza.
 */
#include "ax25.h"
#include "nco.h"
#include <string.h>

#ifdef __GNUC__
#define TCMRAM_BSS __attribute__((section(".tcmram")))
#else
#define TCMRAM_BSS
#endif

/* Muestras por bit, como mucho. A 96 kHz y 1200 baudios son 80; a 12 kHz
 * y 300 baudios (paquete de HF) son 40, asi que el mismo buffer sirve para
 * los dos y no hace falta RAM nueva. */
#define SPB_MAX 96U

/* ==========================================================================
 * LA COMPROBACION (FCS)
 * ==========================================================================
 *
 * Es el CRC-16 del X.25: polinomio 0x1021 al reves (0x8408), empieza en
 * 0xFFFF y se procesa bit a bit empezando por el de menos peso, que es el
 * orden en que viajan los bits por la radio.
 *
 * El receptor NO compara dos numeros: pasa por el molino la trama ENTERA,
 * los dos bytes de comprobacion incluidos, y lo que tiene que salir es una
 * constante, 0xF0B8. Es la misma cuenta escrita de la forma que no necesita
 * separar la trama de su comprobacion, o sea una forma menos de equivocarse.
 */
#define FCS_BUENO 0xF0B8U

uint16_t ax25_fcs(const uint8_t *d, uint16_t n)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t  b;

    for (i = 0U; i < n; i++) {
        uint8_t v = d[i];
        for (b = 0U; b < 8U; b++) {
            uint16_t x = (uint16_t)((crc ^ v) & 1U);
            crc >>= 1;
            if (x) { crc ^= 0x8408U; }
            v = (uint8_t)(v >> 1);
        }
    }
    return crc;
}

/* ==========================================================================
 * ESTADO
 * ========================================================================== */
static uint8_t  s_on;
static float    s_fs;
static float    s_baud, s_marca_hz, s_espacio_hz;

/* --- 1. los dos tonos --- */
static nco_t    s_nco_m, s_nco_e;
static TCMRAM_BSS float s_mi[SPB_MAX], s_mq[SPB_MAX];
static TCMRAM_BSS float s_ei[SPB_MAX], s_eq[SPB_MAX];
static uint16_t s_hidx, s_spb, s_llenado;
static float    s_smi, s_smq, s_sei, s_seq;
static float    s_nivel;

/* --- 2. el reloj de bit --- */
/*
 * UN LAZO DE PRIMER ORDEN, SIN INTEGRADOR DE FRECUENCIA.
 *
 * La primera version copio el lazo del NAVTEX: detector de Gardner mas un
 * integrador que persigue el error de velocidad del transmisor. En el NAVTEX
 * eso es lo correcto, porque la emisora manda sin parar y el lazo tiene todo
 * el tiempo del mundo para asentarse. Aqui no: el paquete va a RAFAGAS, con
 * silencio entre trama y trama, y un integrador que corre suelto durante el
 * silencio se pasea. Medido: con un transmisor PERFECTO, el lazo se iba a
 * +2500 ppm despues de dos tramas y la tercera y la cuarta ya no pasaban la
 * comprobacion.
 *
 * Asi que se hace lo que hace cualquier controlador de paquete: no estimar la
 * velocidad, solo corregir la FASE en cada cruce por cero. La velocidad se da
 * por sabida -1200 baudios son 1200 baudios- y lo unico que se persigue es
 * donde cae el borde. Sin integrador no hay nada que se pueda pasear, y el
 * relleno de bits del HDLC garantiza un cruce cada seis bits como mucho, o
 * sea que nunca esta mucho rato a ciegas.
 *
 * La ganancia es un cuarto del error: medida contra el banco, es la que
 * aguanta mas ruido sin perder tramas seguidas.
 */
#define DPLL_G 2            /* desplazamiento: la correccion es err >> 2 */
static uint32_t s_fase, s_inc;
static uint8_t  s_sig_ant;

/*
 * ======================================================================
 * EL CAMINO DIRECTO: 9600 G3RUH, SIN TONOS - 09/11/2026.
 * ======================================================================
 *
 * En 145,825 no solo esta la ISS: KNACKSAT-2 digipite ahi a 9.600 bits por
 * segundo, y eso NO es dos tonos. Es FSK directa con la banda base
 * aleatorizada: el transmisor desvia la portadora +-3 kHz de pico al ritmo
 * de los bits -el maximo que marca la hoja del modem de G3RUH- asi que lo
 * que sale del discriminador no es "suena 1200 o suena 2200" sino un nivel
 * positivo o uno negativo. Los dos correladores de arriba no tienen nada
 * que correlar.
 *
 * PERO TODO LO DEMAS YA ESTA AQUI. Detras del detector de tonos este
 * fichero lleva el lazo de reloj, el NRZI, el HDLC con su relleno de bits,
 * la comprobacion y el troceado de la trama, y nada de eso sabe ni le
 * importa a que velocidad ha llegado -lo dice ax25.h desde que entro el
 * paquete de HF-. Asi que el camino directo no es un decodificador nuevo:
 * es OTRA PUERTA DE ENTRADA a este mismo.
 *
 * Lo que cambia son tres cosas y nada mas:
 *
 *   1. QUITAR LA CONTINUA. El discriminador da la desviacion instantanea
 *      MAS el desvio de sintonia: si la radio esta 500 Hz fuera, toda la
 *      senal se va para un lado y el cortador de cero decide siempre lo
 *      mismo. Es el mismo razonamiento que en ais_rx.c, y por el mismo
 *      motivo, porque es el mismo tipo de senal.
 *   2. UNA MEDIA MOVIL DE UN BIT en vez de los dos correladores. Para NRZ
 *      -que es lo que es esto- esa media ES el filtro adaptado optimo, no
 *      una aproximacion: aqui encaja mejor incluso que en el AIS, que es
 *      GMSK y reparte cada bit entre sus vecinos.
 *   3. EL DESALEATORIZADOR. G3RUH pasa los bits por un registro de
 *      desplazamiento de 17 etapas antes de transmitirlos, para que una
 *      tira larga de ceros no deje al lazo de reloj sin flancos. Se
 *      deshace con tres XOR y se engancha solo: no hay nada que
 *      sincronizar.
 *
 * LA VENTANA NO CUESTA RAM: se reusa s_mi[], que en este camino no se usa
 * para nada porque no hay correlador de marca. Son diez muestras de las
 * noventa y seis que ya estaban reservadas.
 */
static uint8_t  s_g3ruh;      /* 1 = camino directo, 0 = los dos tonos */
static float    s_dc;         /* la continua que se le quita */
static uint32_t s_scram;      /* los ultimos bits recibidos, sin desaleatorizar */

/*
 * EL POLINOMIO, QUE ES EL DE G3RUH Y NO OTRO: 1 + x^12 + x^17.
 *
 * Esta copiado de la hoja de su modem, que lo describe como "uno de los
 * ocho generadores de longitud maxima posibles con un registro de 17 bits
 * y una sola toma". Una toma distinta no es un ajuste fino: no sale ni una
 * trama, y lo comprueba deepsdr-ui/sim/g3ruh.c moviendo un solo bit.
 *
 * Es AUTOSINCRONIZANTE, y eso es la mitad de la gracia: el receptor no
 * tiene que saber en que estado esta el registro del transmisor, porque lo
 * que sale de aqui depende solo de los diecisiete bits RECIBIDOS. Los
 * diecisiete primeros bits de cada rafaga salen mal y los demas bien, y a
 * eso le sobra con el preambulo de banderas.
 */
#define G3RUH_T1  11U   /* x^12: el bit recibido hace doce, o sea el 11 */
#define G3RUH_T2  16U   /* x^17: el de hace diecisiete */

static uint8_t descram(uint8_t b)
{
    uint8_t sale = (uint8_t)((b ^ (uint8_t)(s_scram >> G3RUH_T1)
                                ^ (uint8_t)(s_scram >> G3RUH_T2)) & 1U);
    s_scram = (s_scram << 1) | (uint32_t)b;
    return sale;
}

/* --- 3. NRZI y HDLC --- */
static uint8_t  s_nrzi_ant;
static uint8_t  s_hay_ant;
static uint8_t  s_reg;        /* los ultimos 8 bits crudos, para las banderas */
static uint8_t  s_unos;
static uint8_t  s_en_trama;
static uint8_t  s_byte;
static uint8_t  s_bbits;
static uint16_t s_n;
static uint8_t  s_trama[AX25_TRAMA_MAX];
static uint32_t s_ult_bandera;
static uint32_t s_muestra;

/* --- 4. salida --- */
#define RING_N 512U
static char     s_ring[RING_N];
static uint16_t s_cab, s_col;

static ax25_info_t s_info;

/* ==========================================================================
 * ARRANQUE
 * ========================================================================== */
void ax25_start(float fs_hz)
{
    ax25_start_fsk(fs_hz, AX25_BAUD, AX25_MARCA_HZ, AX25_ESPACIO_HZ);
}

void ax25_start_fsk(float fs_hz, float baud, float marca_hz, float espacio_hz)
{
    uint32_t spb;

    if (fs_hz <= 0.0f || baud <= 0.0f) { return; }
    s_fs = fs_hz;
    s_baud = baud; s_marca_hz = marca_hz; s_espacio_hz = espacio_hz;

    spb = (uint32_t)(fs_hz / s_baud + 0.5f);
    if (spb < 8U)      { spb = 8U; }
    if (spb > SPB_MAX) { spb = SPB_MAX; }
    s_spb = (uint16_t)spb;

    nco_init();
    nco_fase_cero(&s_nco_m);
    nco_fase_cero(&s_nco_e);
    nco_freq(&s_nco_m, s_marca_hz, fs_hz);
    nco_freq(&s_nco_e, s_espacio_hz, fs_hz);

    memset(s_mi, 0, sizeof s_mi); memset(s_mq, 0, sizeof s_mq);
    memset(s_ei, 0, sizeof s_ei); memset(s_eq, 0, sizeof s_eq);
    s_hidx = 0U; s_llenado = 0U;
    s_smi = s_smq = s_sei = s_seq = 0.0f;
    s_nivel = 1e-6f;

    s_fase = 0U;
    s_inc = (uint32_t)((double)s_baud / (double)fs_hz * 4294967296.0 + 0.5);
    s_sig_ant = 0U;

    s_nrzi_ant = 0U; s_hay_ant = 0U;
    s_reg = 0U; s_unos = 0U; s_en_trama = 0U;
    s_byte = 0U; s_bbits = 0U; s_n = 0U;
    s_ult_bandera = 0UL; s_muestra = 0UL;

    s_cab = s_col = 0U;
    memset(&s_info, 0, sizeof s_info);
    s_g3ruh = 0U; s_dc = 0.0f; s_scram = 0UL;
    s_on = 1U;
}

/*
 * Y LA PUERTA DEL CAMINO DIRECTO. Ver el bloque de s_g3ruh.
 *
 * DEVUELVE SI HA PODIDO, y no es un adorno: a 9.600 baudios hacen falta
 * diez muestras por bit, o sea 96 kHz. La radio tambien corre a 48 -es la
 * casilla RATE de Ajustes- y ahi salen cinco. ax25_start_fsk() recorta
 * cualquier cosa por debajo de ocho a ocho, asi que con 48 kHz el lazo se
 * configuraria para ocho muestras sobre una senal que trae cinco: no
 * decodificaria ni una trama y el panel se quedaria vacio, que es
 * exactamente igual que "no hay trafico". Un fallo disfrazado de que no
 * pasa nada es el peor de todos, y en el AIS ya paso una vez (ver el
 * comentario de la tasa en ais_modo.c).
 *
 * Asi que aqui se dice que no y el que llama avisa.
 */
uint8_t ax25_start_g3ruh(float fs_hz)
{
    if (fs_hz < (AX25_G3RUH_BAUD * 8.0f)) { return 0U; }

    ax25_start_fsk(fs_hz, AX25_G3RUH_BAUD, 0.0f, 0.0f);
    if (!s_on) { return 0U; }

    /*
     * LA CONSTANTE DE LA CONTINUA. Tiene que seguir al desvio de sintonia
     * -que se mueve despacio- y NO a los datos, que cambian cada bit. Una
     * centesima por muestra da una memoria de cien muestras, o sea diez
     * bits: suficientemente lenta para no comerse una tira de unos y
     * suficientemente rapida para enganchar dentro del preambulo.
     */
    s_dc = 0.0f;
    s_scram = 0UL;
    s_g3ruh = 1U;
    return 1U;
}

void ax25_stop(void)      { s_on = 0U; }
uint8_t ax25_activo(void) { return s_on; }

/* Si lo que esta puesto es el camino directo. Lo pregunta la tabla de
 * modos para distinguir la fila de 1200 de la de 9600: las dos llevan NFM
 * y ax25=1. */
uint8_t ax25_g3ruh_activo(void) { return (uint8_t)(s_on && s_g3ruh); }

/*
 * LOS DOS TONOS QUE ESTA MIRANDO AHORA MISMO - 19/10/2026.
 *
 * Existen porque el osciloscopio de sintonia los pintaba con AX25_MARCA_HZ
 * y AX25_ESPACIO_HZ escritos a mano, o sea 1200 y 2200, que son los del
 * APRS. En PKT300 los tonos son otros -1600 y 1800- y las dos rayas caian
 * donde no hay nada. Y la unica cosa que se hace con ese osciloscopio es
 * poner los picos encima de las rayas.
 *
 * Asi que los da quien los sabe, que es este fichero: son los mismos
 * numeros con los que ax25_start_fsk() afina los dos correladores, no una
 * copia al lado.
 *
 * Y NO se derivan de los NCO con nco_freq_real(), que seria mas "de
 * verdad": esos dos valores ya existen en .bss -los usa el arranque- asi
 * que devolverlos no cuesta memoria, y lo que ganaria la otra forma es el
 * redondeo al incremento entero del NCO, que a 12 kHz son tres
 * millonesimas de hercio. No se ve en una raya de 756 pixeles para
 * 6.000 Hz, donde un pixel son ocho hercios.
 */
float ax25_marca_hz(void)   { return s_marca_hz; }
float ax25_espacio_hz(void) { return s_espacio_hz; }

/* ==========================================================================
 * SALIDA
 * ========================================================================== */
static void push(char c)
{
    uint16_t sig = (uint16_t)((s_cab + 1U) % RING_N);
    if (sig == s_col) { return; }
    s_ring[s_cab] = c;
    s_cab = sig;
}

uint8_t ax25_get_char(char *out)
{
    if (s_col == s_cab) { return 0U; }
    if (out) { *out = s_ring[s_col]; }
    s_col = (uint16_t)((s_col + 1U) % RING_N);
    return 1U;
}

void ax25_info(ax25_info_t *out)
{
    if (!out) { return; }
    *out = s_info;
    /* "Enganchado" es haber visto una bandera hace menos de diez segundos.
     * En paquete no hay portadora continua: entre trama y trama no hay NADA,
     * asi que "esta llegando señal" no es una pregunta que se pueda contestar
     * mirando el instante de ahora. */
    out->enganchado = (uint8_t)(s_on && s_ult_bandera != 0UL
                                && (s_muestra - s_ult_bandera) < (uint32_t)(10.0f * s_fs));
    out->nivel = s_nivel;
}

void ax25_lazo_dbg(float *fase, float *nivel)
{
    if (fase)  { *fase = (float)s_fase / 4294967296.0f; }
    if (nivel) { *nivel = s_nivel; }
}

/* ==========================================================================
 * FORMATEAR UNA TRAMA
 * ==========================================================================
 *
 * Las direcciones van con cada letra CORRIDA UN BIT a la izquierda, que es
 * como AX.25 se guarda un bit por byte para marcar donde acaba la lista. Asi
 * que hay que devolverlas a su sitio antes de poder leerlas.
 */
static uint16_t dir_saca(const uint8_t *d, char *out)
{
    uint8_t i, ssid, n = 0U;

    for (i = 0U; i < 6U; i++) {
        char c = (char)(d[i] >> 1);
        if (c != ' ' && c != '\0') { out[n++] = c; }
    }
    ssid = (uint8_t)((d[6] >> 1) & 0x0FU);
    if (ssid != 0U) {
        out[n++] = '-';
        if (ssid >= 10U) { out[n++] = '1'; ssid = (uint8_t)(ssid - 10U); }
        out[n++] = (char)('0' + ssid);
    }
    out[n] = '\0';
    return (uint16_t)(d[6] & 1U);   /* 1 = esta es la ultima direccion */
}

static void trama_texto(const uint8_t *d, uint16_t n)
{
    char dest[12], orig[12], via[12];
    uint16_t p = 0U;
    uint8_t ultima;
    const char *q;

    if (n < 14U) { return; }

    ultima = (uint8_t)dir_saca(&d[0], dest);   p = 7U;
    (void)ultima;
    ultima = (uint8_t)dir_saca(&d[7], orig);   p = 14U;

    for (q = orig; *q != '\0'; q++) { push(*q); }
    push('>');
    for (q = dest; *q != '\0'; q++) { push(*q); }

    /* Los repetidores por los que ha pasado, hasta ocho. */
    while (!ultima && p + 7U <= n && p < 7U * 10U) {
        ultima = (uint8_t)dir_saca(&d[p], via);
        p = (uint16_t)(p + 7U);
        push(',');
        for (q = via; *q != '\0'; q++) { push(*q); }
    }

    push(':');

    /*
     * Y los datos. Se saltan el byte de control y el de protocolo: son
     * maquinaria del enlace, no informacion, y lo que se quiere leer en
     * pantalla es lo que la estacion ha querido decir.
     *
     * Los dos ultimos bytes son la comprobacion y tampoco se enseñan.
     */
    if (p + 2U < n) {
        uint16_t i;
        p = (uint16_t)(p + 2U);
        for (i = p; i + 2U < n; i++) {
            uint8_t c = d[i];
            /* Lo que no sea imprimible sale como un punto: una trama con un
             * byte raro no puede descolocar la rejilla de texto. */
            push((c >= 32U && c < 127U) ? (char)c : '.');
        }
    }
    push('\n');
}

void ax25_trama_dbg(const uint8_t *d, uint16_t n) { trama_texto(d, n); }

/* ==========================================================================
 * HDLC
 * ========================================================================== */
static void trama_fin(void)
{
    if (s_n >= AX25_TRAMA_MIN) {
        if (ax25_fcs(s_trama, s_n) == FCS_BUENO) {
            s_info.tramas_ok++;
            trama_texto(s_trama, s_n);
        } else {
            s_info.tramas_mal++;
        }
    }
    s_n = 0U; s_byte = 0U; s_bbits = 0U;
}

static void hdlc_bit(uint8_t bit)
{
    /* Los bits viajan empezando por el de menos peso, asi que el registro se
     * llena por arriba y se desplaza hacia abajo. */
    s_reg = (uint8_t)((s_reg >> 1) | (bit ? 0x80U : 0U));

    if (s_reg == 0x7EU) {
        /* Bandera. Cierra lo que hubiera y abre lo siguiente. */
        s_info.banderas++;
        s_ult_bandera = s_muestra;
        trama_fin();
        s_en_trama = 1U;
        s_unos = 0U;
        s_bbits = 0U;
        return;
    }

    /*
     * Quitar el relleno. Despues de cinco unos el transmisor mete un cero a
     * la fuerza para que nunca puedan salir seis seguidos dentro de los
     * datos - que es lo que hace que la bandera sea reconocible sin
     * ambiguedad-. Ese cero no es informacion y se tira.
     */
    if (bit == 0U && s_unos == 5U) {
        s_unos = 0U;
        return;
    }
    s_unos = bit ? (uint8_t)(s_unos + 1U) : 0U;

    /* Siete unos seguidos es un aborto: la estacion ha cancelado la trama. */
    if (s_unos >= 7U) {
        s_en_trama = 0U;
        s_n = 0U; s_bbits = 0U; s_unos = 0U;
        return;
    }

    if (!s_en_trama) { return; }

    s_byte = (uint8_t)((s_byte >> 1) | (bit ? 0x80U : 0U));
    s_bbits++;
    if (s_bbits >= 8U) {
        s_bbits = 0U;
        if (s_n < AX25_TRAMA_MAX) {
            s_trama[s_n] = s_byte;
            s_n++;
        } else {
            /* Mas larga de lo que puede ser una trama: no lo es. */
            s_en_trama = 0U;
            s_n = 0U;
        }
        s_byte = 0U;
    }
}

/* ==========================================================================
 * NRZI
 * ==========================================================================
 *
 * Lo que significa un bit no es su nivel sino si ha CAMBIADO respecto al
 * anterior: un cero es un cambio y un uno es quedarse igual.
 *
 * Eso tiene una consecuencia practica que se agradece: si el receptor oye los
 * dos tonos al reves -algo que pasa segun por donde se sintonice-, TODOS los
 * bits salen invertidos, y al comparar cada uno con el anterior la inversion
 * se cancela sola. Por eso aqui no hace falta el conmutador de polaridad que
 * si necesita el NAVTEX.
 */
static void nrzi_bit(uint8_t crudo)
{
    if (!s_hay_ant) {
        s_nrzi_ant = crudo;
        s_hay_ant = 1U;
        return;
    }
    hdlc_bit((uint8_t)(crudo == s_nrzi_ant));
    s_nrzi_ant = crudo;
}

/* ==========================================================================
 * EL RELOJ DE BIT
 * ========================================================================== */
static void discriminador(float d)
{
    uint32_t fase_ant = s_fase;
    uint8_t  sig = (uint8_t)(d > 0.0f);

    s_fase += s_inc;

    /*
     * La vuelta del acumulador es donde se DECIDE, y se comprueba antes de
     * corregir para que una correccion no pueda inventarse un bit de mas.
     *
     * Por que ahi y no a mitad: la media movil es CAUSAL, o sea que su valor
     * resume el ultimo bit y no el bit centrado en ahora. En la vuelta la
     * ventana cubre exactamente un bit. Esto mismo, escrito al reves, costo
     * una tarde en el NAVTEX.
     */
    if (s_fase < fase_ant) {
        /* En el camino directo los bits vienen aleatorizados; en el de dos
         * tonos no. Una bifurcacion por BIT, no por muestra: a 9.600 bits
         * son 9.600 al segundo contra las 96.000 muestras. */
        nrzi_bit(s_g3ruh ? descram(sig) : sig);
    }

    /*
     * Y un cruce por cero es una referencia de reloj. Por el mismo retraso de
     * media ventana, un cambio de bit no se ve en el borde sino en MITAD del
     * siguiente, asi que hacia ahi es hacia donde se tira.
     *
     * El error se mide con signo sobre el acumulador: la resta da un numero
     * que ya sale positivo o negativo segun por que lado se haya pasado, sin
     * ningun caso aparte para la vuelta por cero. Esa es toda la gracia de
     * usar un acumulador de fase en vez de un contador.
     */
    if (sig != s_sig_ant) {
        int32_t err = (int32_t)(s_fase - 0x80000000UL);
        /*
         * LA RESTA, SIN SIGNO - 09/10/2026. Estaba escrita como
         * `(int32_t)s_fase - (err >> G)`, y eso se DESBORDA: cuando el
         * flanco cae con el acumulador justo por debajo de 0x80000000,
         * (int32_t)s_fase vale casi INT32_MAX y el error es negativo, asi
         * que la resta se sale del rango de int. UBSan lo canta:
         * "2147483647 - -1 cannot be represented in type 'int'".
         *
         * En este compilador y en este micro da la vuelta y sale el numero
         * que se queria, asi que nunca se noto. Pero es comportamiento
         * indefinido y aqui no se compila con -fwrapv: el dia que el
         * optimizador decida que eso no puede pasar, el que decide es el.
         *
         * Y QUE CONSTE QUE NO LO HA CAZADO NINGUN BANCO. Salio leyendo, no
         * midiendo, y "make basura" con 400 semillas sigue en verde porque
         * la ventana es de UN SOLO VALOR del acumulador: haciendo la cuenta,
         * solo desborda con s_fase = 0x7FFFFFFF exacto -uno entre 2^32- y
         * justo en un flanco. O sea que esto no es un fallo que se este
         * viendo: es uno que no se puede descartar y que cuesta cero
         * arreglar. Decir que el banco lo ha pillado seria mentir.
         * Hecha sin signo, la vuelta por cero esta DEFINIDA y es justo la
         * que un acumulador de fase quiere.
         */
        s_fase = s_fase - (uint32_t)(err >> DPLL_G);
        s_sig_ant = sig;
    }
}

/* ==========================================================================
 * LA ENTRADA
 * ========================================================================== */
/*
 * ======================================================================
 * *** LOS DOS OSCILADORES, EN LINEA Y CON LA FASE EN UN REGISTRO.
 *     08/11/2026. ***
 * ======================================================================
 *
 * APRS a 1200 baudios es el segundo decodificador mas caro de la radio
 * -7,52 % de cada segundo, medido en deepsdr-ui/m4/m4_digi2.c- y no
 * porque haga nada raro: es que corre a 96 kHz, no a 12.
 *
 * De las 157 instrucciones por muestra, unas 52 se iban en LLAMAR a
 * nco_paso() dos veces. La cuenta de dentro es barata -dos consultas a la
 * tabla y dos interpolaciones- pero la llamada no: prologo, epilogo, y
 * sobre todo devolver dos floats POR PUNTERO, o sea escribirlos a memoria
 * y volverlos a leer, dos veces por muestra.
 *
 * Eso ya estaba escrito en nco.h cuando se hizo nco_sen_cos_fase():
 * "el que la usa la llama una vez por muestra dentro de la interrupcion de
 * audio, y a ese ritmo el prologo y el epilogo de una llamada, mas los
 * cuatro accesos a memoria para devolver dos floats por puntero, cuestan
 * tanto como la cuenta". Lo que pasa es que ax25.c no se habia enterado.
 *
 * ES LA MISMA CUENTA, NO UNA PARECIDA. nco_paso() no hace mas que llamar a
 * nco_sen_cos_fase() con o->fase y luego sumarle o->inc; aqui se hace
 * exactamente eso, con la fase en una local que se devuelve a su sitio al
 * salir. Ni un bit de diferencia, y lo comprueba sim/ax25test.c, que
 * decodifica tramas de verdad.
 */
/*
 * EL CAMINO DIRECTO, muestra a muestra. Ver el bloque de s_g3ruh.
 *
 * Es deliberadamente igual de corto que el otro y hace exactamente lo
 * mismo que ais_rx_mete(), porque es el mismo problema: quitar la
 * continua, promediar un bit y darselo al lazo de reloj.
 */
static void proceso_directo(const float *disc, uint32_t n)
{
    uint32_t k;

    for (k = 0U; k < n; k++) {
        float x = disc[k];
        float y;

        s_muestra++;

        s_dc += 0.01f * (x - s_dc);
        y = x - s_dc;

        /* La media movil de un bit, sobre la ventana del correlador de
         * marca: aqui no hay marca que correlar y esa memoria esta libre. */
        s_smi += y - s_mi[s_hidx];
        s_mi[s_hidx] = y;
        s_hidx++;
        if (s_hidx >= s_spb) { s_hidx = 0U; }

        if (s_llenado < s_spb) { s_llenado++; continue; }

        {
            float a = (s_smi < 0.0f) ? -s_smi : s_smi;
            s_nivel += 0.0005f * ((a * 300.0f) - s_nivel);
            if (s_nivel < 0.0f) { s_nivel = 0.0f; }
        }

        discriminador(s_smi);
    }
}

void ax25_process(const float *audio, uint32_t n)
{
    uint32_t k;
    uint32_t fm, fe, im, ie;

    if (!s_on || !audio) { return; }
    if (s_g3ruh) { proceso_directo(audio, n); return; }

    fm = s_nco_m.fase; im = s_nco_m.inc;
    fe = s_nco_e.fase; ie = s_nco_e.inc;

    for (k = 0U; k < n; k++) {
        float x = audio[k];
        float sn, cs, mi, mq, ei, eq, pm, pe;

        s_muestra++;

        nco_sen_cos_fase(fm, &sn, &cs);  fm += im;
        mi = x * cs;  mq = -x * sn;
        nco_sen_cos_fase(fe, &sn, &cs);  fe += ie;
        ei = x * cs;  eq = -x * sn;

        s_smi += mi - s_mi[s_hidx];  s_mi[s_hidx] = mi;
        s_smq += mq - s_mq[s_hidx];  s_mq[s_hidx] = mq;
        s_sei += ei - s_ei[s_hidx];  s_ei[s_hidx] = ei;
        s_seq += eq - s_eq[s_hidx];  s_eq[s_hidx] = eq;
        s_hidx++;
        if (s_hidx >= s_spb) { s_hidx = 0U; }

        if (s_llenado < s_spb) { s_llenado++; continue; }

        pm = s_smi * s_smi + s_smq * s_smq;
        pe = s_sei * s_sei + s_seq * s_seq;

        s_nivel += 0.0005f * ((pm + pe) - s_nivel);
        if (s_nivel < 1e-12f) { s_nivel = 1e-12f; }

        discriminador(pm - pe);
    }

    s_nco_m.fase = fm;
    s_nco_e.fase = fe;
}
