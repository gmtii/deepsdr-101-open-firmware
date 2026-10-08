#include "hfdl_modo.h"
#include "hfdl_ram.h"
#include "hfdl_demod_chain.h"
#include "hfdl_preamble_sync.h"
#include "hfdl_payload_decode.h"
#include "hfdl_data_segment.h"
#include "hfdl_preamble_seq.h"
#include "hfdl_equalizer.h"
#include "hfdl_pdu.h"
#include "hfdl_hfnpdu.h"
#include "hfdl_aircraft.h"
#include "icao24_db.h"
#include "waterfall.h"

#include "rtc_hw.h"

#include <string.h>

#define HFDL_MODO_LINEAS 14U
#define HFDL_MODO_LARGO  72U
#define HFDL_MODO_AVIONES 20U

/* Las lineas SI son estaticas, al contrario que los estados.
 *
 * Son 1.008 bytes y tienen que sobrevivir a hfdl_modo_stop(): si vivieran en
 * la RAM prestada, salir del modo para mirar otra cosa borraria lo que
 * acababa de salir. Lo mismo hace FT8 con las suyas. */
/*
 * UNA FILA POR AVION, NO UNA POR MENSAJE. 25/09/2026.
 *
 * *** Idea de la pantalla del firmware de Esteban, que el dueno mando:
 * "14 AC / 18 FR", una tabla donde cada avion va juntando lo que se sabe de
 * el. ***
 *
 * Y es MUCHO mejor que una lista de mensajes. Un avion no se presenta una
 * vez: hace logon -y ahi da su direccion OACI-, manda partes de posicion
 * -y ahi da distintivo y coordenadas-, y entre medias manda datos sueltos
 * que por si solos no dicen quien es. En una lista eso son cinco renglones
 * inconexos de los que solo uno lleva la matricula. En una tabla es un
 * avion del que cada vez se sabe mas.
 *
 * La correlacion la hace hfdl_aircraft.c, del proyecto padre: es el CUARTO
 * modulo de este repo que existe, funciona y no lo llamaba nadie.
 *
 * DE EL SE USA LA TABLA, NO EL FORMATO. Su hfdl_ac_format_row() saca una
 * linea de 62 caracteres rellena de espacios, que se lee perfecta en una
 * monoespaciada. La nuestra es proporcional y las columnas las coloca
 * ui_digi.c por pixeles, asi que los espacios saldrian torcidos. Lo que
 * vale de su modulo es que sabe que el "ID 59" de un mensaje y el
 * "4BB0F3" de otro son el mismo avion; el dibujo es cosa nuestra.
 */
static hfdl_ac_rec_t   s_aviones[HFDL_MODO_AVIONES];
static hfdl_ac_table_t s_tabla;
static char     s_lineas[HFDL_MODO_LINEAS][HFDL_MODO_LARGO];
static uint32_t s_nlineas;

/*
 * Ver hfdl_modo.h.
 *
 * LOS ESTADOS NO SON ESTATICOS. Son 4.056 bytes y en la SRAM principal
 * quedan 6.896: cabrian, pero estarian ocupados SIEMPRE, tambien en AM y en
 * FT8, para un modo que se usa a ratos. Viven en la cabecera reservada de la
 * RAM prestada (ver HFDL_RAM_CABECERA), que es memoria que solo existe
 * mientras el modo esta puesto.
 *
 * El precio es que hay que ir a buscarlos con un puntero en vez de
 * nombrarlos, y que si alguien llama aqui con el modo parado se encuentra un
 * NULL. Por eso cada funcion publica lo comprueba: un NULL comprobado es un
 * no-op, y un NULL sin comprobar son los pixeles de la cascada interpretados
 * como un lazo de Costas.
 */
typedef struct {
    hfdl_demod_chain_t   cadena;
    hfdl_preamble_sync_t preambulo;
    hfdl_data_segment_t  segmento;
    hfdl_equalizer_t     ecu;
    float                corr_t;     /* ver polaridad() */
    uint8_t              en_segmento;
    uint8_t              polaridad;
    uint8_t              polaridad_fijada;
    uint8_t              decodificando;   /* ver simbolo_de_segmento() */
} hfdl_estado_t;

static uint8_t  s_activo;
static uint32_t s_total;
/*
 * REVISION A FONDO DEL 08/10/2026: TODO LO QUE CRUZA CON LA INTERRUPCION
 * DE AUDIO VA MARCADO VOLATILE, Y AQUI FALTABA CASI TODO.
 *
 * hfdl_modo_mete() corre en la INTERRUPCION DE AUDIO y escribe el estado
 * del embudo -s_estado, s_n_a1/a2/m1, s_simbolos_datos, s_no_cupo,
 * s_gs_ultima, s_polaridad_ultima/hubo- y el del portero -s_suelo,
 * s_razon y los cuatro s_sq_*-; el bucle principal los lee desde
 * hfdl_modo_poll() y desde la docena larga de funciones de consulta que
 * usa el panel. s_finish_pendiente ya estaba marcado -es el unico que se
 * mira en un bucle de espera-, pero los demas no, y el compilador puede
 * cachear cualquiera de ellos en un registro durante una funcion entera.
 * El sintoma de eso es un panel que se queda con numeros viejos o que
 * ensena un portero cerrado con la senal entrando, y ninguno de los dos
 * apunta a la causa: se buscaria en el demodulador, que es donde no esta.
 */
static volatile uint8_t  s_estado;
static volatile uint8_t s_finish_pendiente;   /* ver hfdl_modo_poll() */

/* Cuantos simbolos se le han metido al decodificador en la rafaga en curso.
 * Tiene que salir data_segment_cnt * 30 exactos; el banco lo comprueba, que
 * es como se caza un desfase de un simbolo antes de que llegue a la antena.
 * Ver hfdl_modo_simbolos_datos(). */
static volatile uint32_t s_simbolos_datos;

/* La polaridad DE LA ULTIMA RAFAGA, que sobrevive al cierre.
 *
 * Los accesores de abajo miraban el estado en vuelo, y ese se borra al
 * acabar la rafaga - asi que preguntarlo despues devolvia siempre "sin
 * decidir". Lo caz el banco, que preguntaba justo despues de la rafaga
 * entera. Para un dato de diagnostico lo util es el ultimo valor, no el que
 * haya ahora mismo a mitad de nada. */
/*
 * EL EMBUDO DEL SINCRONISMO. 25/09/2026.
 *
 * *** Idea sacada de la pantalla de la radio comercial que el dueno
 * mando de referencia: abajo del todo lleva "START 25 > CONFIRM 20 > MODE 20 >
 * FRAME 11". ***
 *
 * Es lo mas util de toda su pantalla y no ocupa una columna. Cuenta cuantas
 * veces se pasa cada escalon del preambulo, y la FORMA del embudo dice donde
 * esta el problema sin tener que entender nada:
 *
 *   muchos A1 y pocos A2  -> son enganches falsos con ruido, no hay senal
 *   A1 y A2 parejos pero pocos M1 -> hay senal pero llega rota
 *   M1 y tramas parejos, pocas buenas -> la cadena va; es propagacion
 *   todo a cero -> la frecuencia esta muerta
 *
 * Cuatro numeros que contestan a "que hago ahora", que es la pregunta de
 * verdad cuando estas barriendo una banda.
 */
static volatile uint32_t s_n_a1, s_n_a2, s_n_m1;
static uint32_t s_buenas;

/*
 * TRAMAS QUE NO CUPIERON. 25/09/2026.
 *
 * hfdl_payload_decode_begin_segment() devuelve false cuando la trama que
 * viene no cabe en la RAM prestada, y hasta hoy ese false se tiraba con un
 * (void). La trama seguia contando como rafaga y no salia nunca en la lista:
 * indistinguible de un CRC malo.
 *
 * Y no es un caso raro. Medido con los ocho giros (ver hfdl_ram.h): el giro
 * 6 -1200 bps en ranura doble- pide 51.088 bytes y hay 45.216. El 7 pide
 * 76.288 y no cabra jamas en una union de 54 kB.
 *
 * (Aqui ponia 49.824 hasta el 28/09/2026. Eso es la union menos SOLO las
 * tablas de preambulo, o sea una cabecera que no existe. Lo que el
 * decodificador ve de verdad es lo que devuelve hfdl_scope_get_hfdl_ram():
 * 54.432 - HFDL_RAM_CABECERA = 45.216, y asi lo dice hfdl_ram.h en los tres
 * sitios donde lo repite. La conclusion no cambia -el giro 6 no cabe- pero
 * el margen del giro 3, que si cabe, es de 12.128 bytes y no de 16.736, y
 * ese es el numero con el que alguien decidira si se puede ensanchar algo.)
 *
 * O sea que dos de los ocho tipos de trama no se decodifican, y el unico
 * sintoma es que el contador de buenas sube menos de lo que deberia. Este
 * contador es lo que separa "no llega bien" de "no cabe", que son dos
 * problemas distintos con dos arreglos distintos.
 */
/*
 * LOS NOMBRES DE LAS DIECISEIS ESTACIONES DE TIERRA.
 *
 * Las trae hfdl_systable.c del proyecto padre, pero detras de un
 * HFDL_AIRCRAFT_TABLE que por defecto esta a 0 y que no se puede encender
 * desde aqui sin tocar el Makefile -protegido- o su cabecera. Y encenderlo
 * entero cuesta 2.310 bytes, porque arrastra ademas el mapa de las 131
 * frecuencias, que aqui no hace falta: las nuestras ya estan en main.c.
 *
 * Asi que van solo los nombres, que es lo unico que se usa. 16 filas, unos
 * 250 bytes. Los ids son los del protocolo, no un indice: el 12 no existe.
 */
static const struct { uint8_t id; const char *nombre; } k_hfdl_estaciones[] = {
    {  1, "SAN FRANCISCO" }, {  2, "MOLOKAI" },     {  3, "REYKJAVIK" },
    {  4, "RIVERHEAD" },     {  5, "AUCKLAND" },    {  6, "HAT YAI" },
    {  7, "SHANNON" },       {  8, "JOHANNESBURG" },{  9, "BARROW" },
    { 10, "MUAN" },          { 11, "ALBROOK" },     { 13, "SANTA CRUZ" },
    { 14, "KRASNOYARSK" },   { 15, "AL MUHARRAQ" }, { 16, "AGANA" },
    { 17, "CANARIAS" }
};

static const char *estacion_nombre(uint8_t id)
{
    uint32_t i;

    for (i = 0U; i < sizeof k_hfdl_estaciones / sizeof k_hfdl_estaciones[0]; i++) {
        if (k_hfdl_estaciones[i].id == id) { return k_hfdl_estaciones[i].nombre; }
    }
    return (const char *)0;
}

static volatile uint32_t s_no_cupo;

/* La ultima estacion de tierra oida. Su pantalla la pone arriba ("GS 3
 * REYKJAVIK") y no en cada fila, y tiene razon: estas en una frecuencia, o
 * sea en una estacion. Repetirla catorce veces era gastar una columna. */
static volatile uint8_t s_gs_ultima = 0xFFU;

/*
 * EL PORTERO: NO BUSCAR PREAMBULO CUANDO NO HAY NADA QUE BUSCAR.
 * 25/09/2026.
 *
 * *** De la radio del dueno: el embudo marcaba "63>17>14>13". Sesenta y tres
 * enganches de A1 y solo diecisiete confirmados: tres de cada cuatro eran
 * ruido. Y del proyecto padre, que resuelve esto mismo con un
 * hfdl_scope_burst_active(). ***
 *
 * POR QUE NO SE PORTA EL SUYO. El suyo hace una FFT de 256 puntos sobre la
 * senal, con su ventana, sus tablas de giro y su historial: unos 7.168 bytes
 * de RAM. En esta radio quedan 1.104 libres. Podrian salir de la union
 * prestada, pero eso obliga a reescribir sus arrays como punteros a un
 * bloque, y son diez sitios en un fichero que hasta ahora no hemos tocado.
 *
 * QUE SE HACE EN SU LUGAR. La cadena YA calcula la energia de la senal: el
 * control de ganancia la necesita para su propio trabajo y la deja a mano en
 * hfdl_demod_chain_get_agc_power_avg(). Con ella y veinte bytes de estado
 * sale el mismo portero:
 *
 *   - un suelo de ruido que BAJA rapido y SUBE despacio, para que una rafaga
 *     -que sube la energia- no se lo lleve consigo;
 *   - la razon energia/suelo, que es la senal en veces;
 *   - y un umbral con histeresis, para que no parpadee en el borde.
 *
 * Lo que se pierde frente al suyo es la FORMA del espectro: el no solo sabe
 * que hay algo, sabe a cuantos hercios esta. Eso es lo que le permite ensenar
 * el "-9 Hz" de desvio. Aqui no, y esta bien dicho para que nadie lo busque.
 *
 * FALLA ABIERTO, A PROPOSITO. Un portero que se queda cerrado por error deja
 * la radio muda y parece una averia del decodificador; uno que se queda
 * abierto solo devuelve el ruido que habia antes. Por eso, si lleva mucho
 * rato sin dejar pasar a nadie, abre solo. Ver porton().
 */
#define HFDL_SQ_ABRE      3.0f    /* veces sobre el suelo para abrir */
#define HFDL_SQ_CIERRA    2.0f    /* y para cerrar - histeresis */
#define HFDL_SQ_CEBADO    200U    /* bloques mirando antes de fiarse del suelo */
#define HFDL_SQ_HARTO   20000U    /* bloques cerrado seguidos -> abre solo */

static volatile float    s_suelo;
static volatile float    s_razon;
static volatile uint32_t s_sq_visto;
static volatile uint32_t s_sq_cerrado;
static volatile uint8_t  s_sq_abierto;
static volatile uint8_t  s_sq_forzado;  /* 1 = se abrio por hartazgo, no por senal */

/* Devuelve 1 si hay que dejar pasar el bloque al sincronismo. */
static uint8_t porton(float p)
{
    if (p <= 0.0f) { p = 1e-12f; }

    /* Baja rapido, sube despacio: el suelo persigue los valles y se deja
     * ignorar por los picos, que es justo lo contrario de una media. */
    if (s_suelo <= 0.0f)   { s_suelo = p; }
    else if (p < s_suelo)  { s_suelo += (p - s_suelo) * 0.05f; }
    else                   { s_suelo += (p - s_suelo) * 0.0005f; }

    s_razon = p / (s_suelo + 1e-12f);

    if (s_sq_visto < HFDL_SQ_CEBADO) {
        /* Todavia no se sabe donde esta el suelo: pasa todo. Cerrar aqui
         * seria decidir con lo que no se sabe. */
        s_sq_visto++;
        s_sq_abierto = 1U;
        return 1U;
    }

    if (s_sq_abierto) {
        if (s_razon < HFDL_SQ_CIERRA) { s_sq_abierto = 0U; }
    } else {
        if (s_razon > HFDL_SQ_ABRE)   { s_sq_abierto = 1U; s_sq_forzado = 0U; }
    }

    if (s_sq_abierto) {
        s_sq_cerrado = 0U;
    } else if (++s_sq_cerrado > HFDL_SQ_HARTO) {
        /* Ver la cabecera: mejor abierto de mas que mudo. */
        s_sq_abierto = 1U;
        s_sq_forzado = 1U;
        s_sq_cerrado = 0U;
    }
    return s_sq_abierto;
}        /* rafagas con el CRC bueno - ver hfdl_modo_poll() */
static volatile uint8_t s_polaridad_ultima;
static volatile uint8_t s_polaridad_hubo;

static hfdl_estado_t *estado(void)
{
    uint8_t *cab = hfdl_ram_cabecera();

    if (cab == (uint8_t *)0) { return (hfdl_estado_t *)0; }
    /* Detras de las tablas del preambulo - ver hfdl_ram.h. */
    return (hfdl_estado_t *)(void *)(cab + HFDL_RAM_PREAMBULO);
}

uint8_t hfdl_modo_start(void)
{
    hfdl_estado_t *e;

    if (s_activo) { return 1U; }
    if (waterfall_prestada()) { return 0U; }   /* FT8 la tiene cogida */

    /* El orden importa: primero el prestamo, porque estado() devuelve NULL
     * hasta que la region existe. */
    hfdl_ram_coge();

    e = estado();
    if (e == (hfdl_estado_t *)0) { hfdl_ram_suelta(); return 0U; }

    /* Que quepan los estados en la cabecera se comprueba al compilar, no al
     * arrancar: si un struct del padre crece y se pasa, esto no compila en
     * vez de escribir encima de lo de payload_decode. */
    _Static_assert(sizeof(hfdl_estado_t) + HFDL_RAM_PREAMBULO <= HFDL_RAM_CABECERA,
                   "los estados de HFDL ya no caben en la cabecera reservada");

    memset(e, 0, sizeof *e);
    hfdl_demod_chain_init(&e->cadena);
    hfdl_preamble_sync_init(&e->preambulo);

    hfdl_ac_init(&s_tabla, s_aviones, sizeof s_aviones);
    s_estado = 0U;
    s_gs_ultima = 0xFFU;
    /*
     * TODOS los contadores, no solo las buenas. 27/09/2026.
     *
     * Faltaba s_total -y con el s_simbolos_datos y la polaridad-, asi que
     * al salir del modo y volver la chapa arrancaba diciendo "0/42": cero
     * buenas de cuarenta y dos intentos que no habian pasado. Y esa
     * fraccion es justo la que se mira para saber si la sintonia va bien.
     * hfdl_modo_borra() si los reiniciaba todos; el que se quedo a medias
     * fue este.
     */
    s_buenas = 0U; s_total = 0U; s_simbolos_datos = 0U;
    s_polaridad_ultima = 0U; s_polaridad_hubo = 0U;
    s_n_a1 = 0U; s_n_a2 = 0U; s_n_m1 = 0U; s_no_cupo = 0U;
    s_suelo = 0.0f; s_razon = 0.0f;
    s_sq_visto = 0U; s_sq_cerrado = 0U; s_sq_abierto = 1U; s_sq_forzado = 0U;
    s_finish_pendiente = 0U;
    s_activo = 1U;
    return 1U;
}

void hfdl_modo_stop(void)
{
    if (!s_activo) { return; }
    s_activo = 0U;
    hfdl_ram_suelta();
}

uint8_t hfdl_modo_activo(void) { return s_activo; }
uint32_t hfdl_modo_total(void) { return s_total; }
uint32_t hfdl_modo_buenas(void) { return s_buenas; }
uint32_t hfdl_modo_no_cupo(void) { return s_no_cupo; }

/*
 * La senal en tantos por ciento, para la pantalla. No son dB ni esta
 * calibrado contra nada: es la razon energia/suelo aplastada a 0-99, con el
 * mismo espiritu de "sin calibrar pero util" que la escala de dB del
 * espectro. Sirve para comparar una frecuencia con otra, que es para lo que
 * se mira.
 */
uint8_t hfdl_modo_senal(void)
{
    float r = s_razon;

    if (r <= 1.0f) { return 0U; }
    r = (r - 1.0f) * 12.0f;          /* 1x -> 0, ~9x -> 99 */
    if (r > 99.0f) { r = 99.0f; }
    return (uint8_t)r;
}

uint8_t hfdl_modo_porton_forzado(void) { return s_sq_forzado; }

const char *hfdl_modo_estacion(void)
{
    const char *n = estacion_nombre(s_gs_ultima);
    return n ? n : "";
}

void hfdl_modo_embudo(uint32_t *a1, uint32_t *a2, uint32_t *m1)
{
    if (a1) { *a1 = s_n_a1; }
    if (a2) { *a2 = s_n_a2; }
    if (m1) { *m1 = s_n_m1; }
}
uint8_t hfdl_modo_estado(void) { return s_estado; }

/*
 * LA POLARIDAD GLOBAL: EL BIT QUE NADIE CALCULABA.
 *
 * hfdl_payload_decode_feed_symbol() pide un `polarity_bitmask` y lo usa asi:
 *
 *     sign = (bit_del_descifrador ^ (polarity_bitmask & 1)) ? -1 : +1
 *
 * O sea UN BIT: la ambiguedad de 180 grados que tiene cualquier BPSK, porque
 * un "1" y un "0" solo se distinguen por el signo y el receptor no sabe cual
 * de los dos extremos ha cogido. Si se acierta, sale el mensaje; si se
 * falla, salen todos los bits al reves y el CRC no cuadra jamas.
 *
 * EN TODO EL CODIGO PORTADO ESE BIT SE CONSUME EN TRES SITIOS Y NO LO
 * CALCULA NADIE. No es un descuido: su propia cabecera lo dice con todas las
 * letras -"this needs its own design work, not a direct port"-. dumphfdl lo
 * saca del SIGNO de la correlacion de A1, pero nuestro detector de preambulo
 * combina los bloques de forma NO COHERENTE y resuelve el signo por separado
 * en cada bloque, tirandolo despues (ver hfdl_framer.h). Nos dio tolerancia
 * a la deriva de fase y nos quito la respuesta.
 *
 * DE DONDE SE SACA AQUI. De las secuencias de entrenamiento, que estan justo
 * delante de los datos y son conocidas: entre el preambulo y la primera
 * trama van NUEVE ventanas de quince simbolos (135 en total) cuyo contenido
 * esta en HFDL_T_SEQ. Y mirando la tabla se ve que HFDL_T_SEQ[1] es la
 * negacion exacta de HFDL_T_SEQ[0], asi que no hay que probar dos
 * correlaciones: basta el SIGNO de una.
 *
 * Se acumula la correlacion durante todo el entrenamiento y, al entrar en la
 * primera trama de datos, el signo decide. Ciento treinta y cinco simbolos
 * de referencia conocida para decidir un bit es muchisimo margen.
 *
 * SI ESTO ESTUVIERA MAL, el sintoma seria claro y no silencioso: ni un solo
 * CRC bueno, nunca. Que es exactamente lo que pasaria tambien si no se
 * calculara -asi que peor que ahora no se puede estar-.
 */
static void mide_polaridad(hfdl_estado_t *e, float i_sym)
{
    uint32_t t = hfdl_data_segment_get_t_idx(&e->segmento);

    /* i_sym es la salida YA ECUALIZADA - ver simbolo_de_segmento(). Medirlo
     * sobre el simbolo crudo tambien funcionaba, pero lo que va a decodificar
     * es lo ecualizado, y el bit tiene que corresponder a eso. */
    if (t < HFDL_T_LEN) {
        e->corr_t += i_sym * HFDL_T_SEQ[0][t];
    }
}

/*
 * EL ECUALIZADOR, QUE SE ME QUEDO FUERA.
 *
 * *** Sintoma en antena, 25/09/2026: engancha el preambulo sin problema,
 * cuarenta y dos rafagas completas, y NI UN SOLO CRC BUENO. ***
 *
 * La causa era esta: hfdl_payload_decode_feed_symbol() pide `eq_i, eq_q` -
 * ECUALIZADOS- y yo le estaba metiendo los simbolos crudos de Costas. El
 * modulo existia, con su autoprueba pasando, y no lo llamaba nadie.
 *
 * QUE HACE. Quince tomas de un filtro adaptativo que corrige lo que el
 * camino de onda corta le hace a la senal: como la misma emision llega por
 * varios saltos con retardos distintos, cada simbolo se le monta encima al
 * siguiente. Sin deshacer eso, los simbolos llegan emborronados y el Viterbi
 * decodifica ruido - que es literalmente lo que salia en la pantalla.
 *
 * COMO SE ENTRENA. Con las mismas nueve ventanas de entrenamiento que ya
 * usabamos para la polaridad: se sabe lo que deberia salir (HFDL_T_SEQ) y se
 * corrigen las tomas segun el error. Despues, en los datos, se aplica sin
 * seguir aprendiendo.
 *
 * DOS MUESTRAS POR SIMBOLO. El ecualizador va a T/2 y por eso se le meten
 * DOS: primero la de la mitad -que es anterior en el tiempo- y luego la
 * principal. Esa media muestra ya salia de hfdl_demod_chain_process() y yo
 * la estaba tirando.
 *
 * SE ENTRENA SIEMPRE CONTRA HFDL_T_SEQ[0] y nunca contra el [1], aunque la
 * polaridad todavia no se sepa. Puede hacerse porque el [1] es la negacion
 * exacta del [0]: entrenar contra el equivocado converge al mismo filtro con
 * el signo cambiado, y ese signo lo recoge despues la medida de polaridad,
 * que ahora mira la salida ECUALIZADA y no la cruda. El huevo y la gallina
 * se resuelven solos.
 */
static void simbolo_de_segmento(hfdl_estado_t *e, float ih, float qh,
                                float si, float sq)
{
    hfdl_data_segment_state_t antes = hfdl_data_segment_get_state(&e->segmento);
    hfdl_data_segment_state_t ahora;
    float yi = 0.0f, yq = 0.0f;

    /* Las dos muestras del simbolo, en orden cronologico. */
    hfdl_equalizer_push(&e->ecu, ih, qh);
    hfdl_equalizer_push(&e->ecu, si, sq);
    hfdl_equalizer_execute(&e->ecu, &yi, &yq);

    if (antes == HFDL_DSEG_EQ_TRAIN) {
        uint32_t t = hfdl_data_segment_get_t_idx(&e->segmento);

        /* Primero se mide con la salida de AHORA -calculada con las tomas de
         * antes de corregirlas, que es lo que pide el LMS- y luego se
         * corrigen. El orden al reves entrenaria contra su propio resultado. */
        mide_polaridad(e, yi);
        if (t < HFDL_T_LEN) {
            hfdl_equalizer_step(&e->ecu, HFDL_T_SEQ[0][t], 0.0f, yi, yq);
        }
    }

    hfdl_data_segment_process_symbol(&e->segmento, si, sq);
    ahora = hfdl_data_segment_get_state(&e->segmento);

    /*
     * LOS SIMBOLOS SE CLASIFICAN POR EL ESTADO DE ANTES, NO POR EL DE
     * DESPUES. 25/09/2026, y esto es lo que tenia todos los CRC malos.
     *
     * *** Sintoma: con el ecualizador ya puesto, seguia sin salir ni un CRC
     * bueno. ***
     *
     * Su maquina de estados cambia de fase EN EL ULTIMO SIMBOLO de cada
     * ventana - lo dice su cabecera, "act on the LAST symbol of the current
     * window" - asi que el simbolo que acabas de meter pertenece al estado
     * que habia ANTES de meterlo. La ficha del campo t_idx lo deja escrito
     * con todas las letras: "read before feeding this symbol, THEN this
     * increments".
     *
     * Yo lo leia bien para el entrenamiento y MAL para los datos. Con eso:
     *
     *   - el ultimo simbolo de entrenamiento se colaba como si fuera de
     *     datos, porque en el es cuando el estado salta a DATA_1;
     *   - y el ultimo simbolo de datos de verdad se perdia, porque en el
     *     el estado ya habia saltado a DONE.
     *
     * O sea que la tira de bits que le llegaba al desentrelazador estaba
     * corrida un sitio y con un simbolo de entrenamiento metido delante.
     * Desentrelazar una tira corrida da ruido perfectamente uniforme, y el
     * Viterbi lo decodifica tan contento: por eso salian mensajes de
     * longitudes creibles y basura dentro.
     *
     * Un simbolo de 4.050. Suficiente.
     */
    if (!e->polaridad_fijada && antes == HFDL_DSEG_EQ_TRAIN &&
        (ahora == HFDL_DSEG_DATA_1 || ahora == HFDL_DSEG_DATA_2)) {
        /* Este simbolo todavia es de entrenamiento: se abre el segmento
         * pero NO se mete. El primero de datos es el siguiente. */
        e->polaridad = (uint8_t)((e->corr_t < 0.0f) ? 1U : 0U);
        e->polaridad_fijada = 1U;
        s_polaridad_ultima = e->polaridad;
        s_polaridad_hubo = 1U;
        /*
         * SE INTENTA UNA VEZ POR RAFAGA, NO UNA POR VENTANA. 25/09/2026.
         *
         * *** La radio marcaba "23>13>12>12 X336": trescientas treinta y seis
         * tramas sin sitio de doce tramas totales. Un imposible. ***
         *
         * La primera version ponia polaridad_fijada = 0 al fallar, para que
         * no se le metieran simbolos al decodificador. Pero esa bandera es
         * justo la que impide volver a entrar aqui: al borrarla, la ventana
         * de entrenamiento SIGUIENTE -y hay setenta y dos por rafaga, una
         * entre cada trama de datos- volvia a intentarlo y a fallar. El
         * contador contaba INTENTOS y los vendia como tramas.
         *
         * Ahora la polaridad se queda fijada pase lo que pase, que es lo
         * correcto -se ha medido, cupo o no cupo- y si abrio el segmento se
         * dice en otra bandera. Dos cosas distintas en dos sitios distintos:
         * juntarlas fue el error.
         *
         * La leccion, otra vez: un contador que puede pasarse de su propio
         * maximo esta mal. 336 de 12 no hacia falta medirlo para saberlo.
         */
        e->decodificando = (uint8_t)hfdl_payload_decode_begin_segment(
                (uint32_t)hfdl_preamble_sync_get_m1_index(&e->preambulo));
        if (!e->decodificando) { s_no_cupo++; }
    }

    if (e->decodificando &&
        (antes == HFDL_DSEG_DATA_1 || antes == HFDL_DSEG_DATA_2)) {
        hfdl_payload_decode_feed_symbol(yi, yq, (uint32_t)e->polaridad);
        s_simbolos_datos++;
    }

    if (ahora == HFDL_DSEG_DONE) {
        /* AQUI NO SE LLAMA A finish(). Ver hfdl_modo_poll(): dentro va el
         * Viterbi, y esto se ejecuta en la interrupcion del audio.
         *
         * Y solo si el segmento llego a abrirse: rematar uno que no existe
         * seria pedirle al Viterbi que decodifique un buffer que nadie
         * lleno, y lo que saliera contaria como trama con el CRC malo. */
        if (e->decodificando) { s_finish_pendiente = 1U; }
        e->decodificando = 0U;

        /* La rafaga se acabo: a buscar preambulo otra vez. Lo dice su propia
         * cabecera - este modulo no toca el sincronismo, es cosa de quien
         * llama, o sea de aqui. */
        hfdl_preamble_sync_reset(&e->preambulo);
        e->en_segmento = 0U;
        e->polaridad_fijada = 0U;
        e->corr_t = 0.0f;
    }
}

/* Decimal sin signo. main.c tiene el suyo pero es estatico de alli, y
 * exportarlo por un numero seria acoplar dos ficheros para nada. */
static void aj_u2s_local(char *d, uint32_t v)
{
    char t[10]; uint8_t n = 0U, i;

    if (v == 0U) { d[0] = '0'; d[1] = '\0'; return; }
    while (v != 0U && n < sizeof t) { t[n++] = (char)('0' + v % 10U); v /= 10U; }
    for (i = 0U; i < n; i++) { d[i] = t[n - 1U - i]; }
    d[n] = '\0';
}

/* Un numero en hexadecimal de seis cifras, para la direccion OACI. */
static uint8_t hex24(char *d, uint32_t v)
{
    static const char k[] = "0123456789ABCDEF";
    uint8_t i;

    for (i = 0U; i < 6U; i++) {
        d[i] = k[(v >> (20U - 4U * i)) & 0xFU];
    }
    return 6U;
}

/*
 * DE BYTES CRUDOS A UNA LINEA QUE SE LEE. 25/09/2026.
 *
 * *** Por el dueno del proyecto, viendo el primer CRC bueno con un
 * "UZB2013" flotando entre puntos: "hazlo". ***
 *
 * Lo que sale del Viterbi es un MPDU: una cabecera con quien habla con quien,
 * y dentro uno o varios LPDU con el asunto. Volcar esos bytes tal cual
 * -que es lo que hacia antes- deja el distintivo del vuelo escondido entre
 * campos binarios que salen como puntos. Los modulos que lo desempaquetan ya
 * estaban en el repo sin que los llamara nadie, igual que paso con el
 * ecualizador.
 *
 * LA LINEA SALE ASI:
 *
 *   14:36   aire>SHANNON   4CA1FB   Logon
 *   14:36   SHANNON>aire   --       Datos  UZB2013...
 *
 * El sentido primero porque es lo que ordena el resto: en bajada habla un
 * avion y en subida contesta la estacion. La direccion OACI solo existe en
 * los logon -en los datos va implicita en el enlace ya establecido- y por eso
 * sale un "--" en vez de un cero, que se leeria como una direccion.
 */
/*
 * De una trama decodificada a un apunte en la tabla de aviones. Ver
 * s_aviones: aqui solo se rellena el parte y hfdl_ac_update() decide a que
 * avion pertenece y que campos mejora.
 */
static void apunta_trama(const uint8_t *datos, uint32_t largo)
{
    hfdl_mpdu_header_t hdr;
    hfdl_lpdu_result_t lpdu[4];
    hfdl_ac_event_t ev;
    hfdl_hfnpdu_perf_data_t pd;
    rtc_hw_datetime_t h;
    uint32_t nl = 0U, k;
    uint8_t  pide_modelo = 0U;

    if (!hfdl_mpdu_parse_header(datos, largo, &hdr)) { return; }
    nl = hfdl_mpdu_extract_lpdus(datos, largo, &hdr, lpdu,
                                 sizeof lpdu / sizeof lpdu[0]);
    if (nl == 0U) { return; }

    memset(&ev, 0, sizeof ev);
    ev.ac_id = HFDL_AC_ID_NONE;
    rtc_hw_get(&h);
    ev.hh = h.hour; ev.mm = h.minute; ev.ss = h.second;
    ev.lpdu_type = lpdu[0].type_octet;

    if (hdr.direction == HFDL_PDU_DIR_DOWNLINK) {
        ev.ac_id = hdr.src_id;      /* habla el avion */
        ev.gs_id = hdr.dst_id;
    } else {
        ev.flags |= HFDL_AC_EV_UPLINK;
        ev.gs_id = hdr.src_gs_id;
        /* En subida el destino es el avion; si hay varios se apunta el
         * primero, que es de quien habla el primer LPDU. */
        if (hdr.aircraft_cnt != 0U) { ev.ac_id = hdr.aircraft[0].dst_id; }
    }

    for (k = 0U; k < nl; k++) {
        switch (lpdu[k].kind) {
        case HFDL_LPDU_KIND_LOGON_CONFIRM:
            /* La estacion acaba de asignarle un identificador a esa
             * direccion OACI: es el apunte que ata las dos cosas, y el que
             * hace que los mensajes sueltos de despues dejen de ser
             * anonimos. */
            ev.icao = lpdu[k].icao_address;
            ev.ac_id = lpdu[k].ac_id;
            ev.flags |= (uint8_t)(HFDL_AC_EV_ICAO | HFDL_AC_EV_ASSIGN);
            break;
        case HFDL_LPDU_KIND_LOGON_REQUEST:
        case HFDL_LPDU_KIND_LOGOFF_OR_DENIED:
            ev.icao = lpdu[k].icao_address;
            ev.flags |= HFDL_AC_EV_ICAO;
            break;
        case HFDL_LPDU_KIND_USER_DATA:
            if (lpdu[k].user_data != (const uint8_t *)0 &&
                hfdl_hfnpdu_parse_performance_data(lpdu[k].user_data,
                                                   lpdu[k].user_data_len, &pd)) {
                uint8_t c;

                ev.lat_e4 = (int32_t)(pd.location.lat * 10000.0);
                ev.lon_e4 = (int32_t)(pd.location.lon * 10000.0);
                ev.flags |= (uint8_t)(HFDL_AC_EV_POS | HFDL_AC_EV_PERF);
                for (c = 0U; c < 6U; c++) { ev.flight[c] = pd.flight_id[c]; }
                ev.flight[6] = '\0';
                if (ev.flight[0] > 32) { ev.flags |= HFDL_AC_EV_FLIGHT; }
            }
            break;
        default:
            break;
        }
    }

    s_gs_ultima = ev.gs_id;

    /*
     * LAS TRAMAS ANONIMAS NO ENTRAN EN LA TABLA. 25/09/2026.
     *
     * *** El dueno, mirando la pantalla: "siguen saliendo los mismos
     * datos". Y en la foto, "ID 255" DOS VECES. ***
     *
     * 255 no es un avion: es HFDL_AC_ID_NONE, o sea "no se sabe". Pasa con
     * las tramas de subida que la estacion manda sin destinatario
     * (aircraft_cnt == 0) y sin ningun LPDU que traiga una direccion OACI.
     * Ahi no sabemos ni quien es ni el numero que le puso la estacion.
     *
     * Y hfdl_ac_update() no tiene forma de emparejarlas con nada -busca por
     * OACI o por (estacion, identificador), y no hay ninguno de los dos-,
     * asi que hace lo unico que puede: ABRIR UNA FILA NUEVA. Cada trama de
     * esas. La tabla se iba llenando de filas "ID 255" identicas que
     * empujaban fuera a los aviones de verdad, y desde fuera eso se ve
     * como que la pantalla se ha quedado pegada.
     *
     * No es un fallo de su modulo: es que le estabamos dando algo que no
     * es un avion. El sitio de arreglarlo es aqui, antes de dárselo.
     *
     * La trama sigue contando como buena y la estacion se sigue apuntando
     * -eso es la linea de arriba-; lo unico que no pasa es que aparezca un
     * avion que no existe.
     */
    if (((ev.flags & HFDL_AC_EV_ICAO) == 0U) && (ev.ac_id == HFDL_AC_ID_NONE)) {
        return;
    }

    {
        hfdl_ac_rec_t *r = hfdl_ac_update(&s_tabla, &ev, &pide_modelo);

        /*
         * EL MODELO, UNA VEZ POR AVION. hfdl_ac_update() levanta
         * pide_modelo solo cuando acaba de aprender la direccion OACI de un
         * avion del que todavia no se sabe el modelo - asi la base de datos
         * se consulta una vez por avion y no una por mensaje, que importa
         * porque cada consulta son una veintena de lecturas por el bus SPI
         * a patadas (busqueda binaria sobre cien mil registros).
         */
        if (r != (hfdl_ac_rec_t *)0 && pide_modelo) {
            (void)icao24_db_lookup(r->icao, r->model, sizeof r->model);
        }
    }
}

/*
 * Una fila de la tabla, en nuestras columnas. Ver s_aviones para por que no
 * se usa su hfdl_ac_format_row().
 */
static const char *fila_avion(uint32_t i)
{
    const hfdl_ac_rec_t *r;
    char *l;
    uint8_t p = 0U;

    if (i >= s_tabla.count || i >= HFDL_MODO_LINEAS) { return ""; }
    r = &s_tabla.rec[i];
    l = s_lineas[i];

    /* --- quien es: la OACI si se sabe, y si no el identificador --- */
    if ((r->flags & HFDL_AC_R_ICAO) != 0U) {
        p = (uint8_t)(p + hex24(&l[p], r->icao));
    } else {
        l[p++] = 'I'; l[p++] = 'D'; l[p++] = ' ';
        aj_u2s_local(&l[p], r->ac_id);
        while (l[p] != '\0') { p++; }
    }
    l[p++] = '\t';

    /*
     * --- distintivo del vuelo, y el modelo detras en la misma columna ---
     *
     * "DLH494 A320". Sin columna propia a proposito: son dos datos del
     * mismo avion y los dos cortos; separarlos gastaria ancho en otra
     * cabecera para ganar nada. Solo el codigo de tipo -la primera
     * palabra- porque "Boeing 737-800" no cabe y "B738" ya lo dice.
     *
     * EL GUION SOLO SI NO HAY NADA. 25/09/2026: con la base de aviones
     * cargada, un avion sin distintivo salia como "- B788", y ese guion
     * suelto delante del modelo parecia un signo menos pegado al codigo.
     * El guion esta para que una casilla vacia se vea vacia a proposito,
     * asi que cuando hay modelo ya no hace falta y estorba.
     */
    /*
     * --- EL MODELO, EN SU PROPIA COLUMNA ENTRE OACI Y VUELO ---
     *
     * *** El dueno: "el modelo del avion no lo pone" y, cuando se le
     * pregunto donde, "entre OACI y vuelo". ***
     *
     * Antes iba pegado detras del distintivo -"DLH494 A320"- y recortado
     * a cinco letras, que es el codigo de tipo y no el modelo. Ahora la
     * base guarda el nombre entero -"B788 Boeing 787-8"- sin gastar un
     * byte mas, porque la entrada de la tabla mide 24 diga lo que diga, y
     * aqui tiene sitio propio.
     *
     * El codigo va DELANTE en la base a proposito: es lo que siempre
     * cabe, asi que si la columna se queda corta lo que se pierde es la
     * cola y no el dato. Los 196 px de la columna se midieron con la
     * fuente de verdad (font_ui_14b) contra el nombre mas largo que
     * genera tools/icao24.py, no a ojo.
     */
    if (r->model[0] != '\0') {
        uint8_t c;
        for (c = 0U; c < 23U && r->model[c] >= 32 && r->model[c] < 127 &&
                     p < (HFDL_MODO_LARGO - 30U); c++) {
            l[p++] = r->model[c];
        }
    } else {
        l[p++] = '-';
    }
    l[p++] = '\t';

    /* --- distintivo del vuelo --- */
    if ((r->flags & HFDL_AC_R_FLIGHT) != 0U) {
        uint8_t c;
        for (c = 0U; c < 6U && r->flight[c] >= 32 && r->flight[c] < 127; c++) {
            l[p++] = r->flight[c];
        }
    } else {
        l[p++] = '-';
    }
    l[p++] = '\t';

    /* --- posicion --- */
    if ((r->flags & HFDL_AC_R_POS) != 0U) {
        #define COORD(v, pos, neg) do { \
            int32_t vv = (v) / 1000; char sg = (vv < 0) ? (neg) : (pos); \
            if (vv < 0) { vv = -vv; } \
            aj_u2s_local(&l[p], (uint32_t)(vv / 10)); \
            while (l[p] != '\0') { p++; } \
            l[p++] = ','; l[p++] = (char)('0' + (vv % 10)); l[p++] = sg; \
        } while (0)
        COORD(r->lat_e4, 'N', 'S');
        l[p++] = ' ';
        COORD(r->lon_e4, 'E', 'W');
        #undef COORD
    } else {
        l[p++] = '-';
    }
    l[p++] = '\t';

    /* --- que hizo la ultima vez, y por donde --- */
    l[p++] = ((r->flags & HFDL_AC_R_UPLINK) != 0U) ? '<' : '>';
    l[p++] = ' ';
    {
        const char *q = hfdl_ac_event_name(r->event);
        uint8_t c;
        for (c = 0U; q[c] != '\0' && c < 14U && p < (HFDL_MODO_LARGO - 8U); c++) {
            l[p++] = q[c];
        }
    }
    l[p++] = '\t';

    /* --- cuantas veces se le ha oido --- */
    aj_u2s_local(&l[p], r->count);
    while (l[p] != '\0') { p++; }
    l[p] = '\0';
    return l;
}

void hfdl_modo_poll(void)
{
    const uint8_t *datos = (const uint8_t *)0;
    uint32_t largo = 0U;
    uint8_t  ok = 0U, valido = 0U;
    uint8_t *ram;

    if (!s_activo || !s_finish_pendiente) { return; }
    s_finish_pendiente = 0U;

    ram = hfdl_scope_get_hfdl_ram((void *)0);
    if (ram == (uint8_t *)0) { return; }

    hfdl_payload_decode_finish_ex(ram, &datos, &largo);
    hfdl_payload_decode_get_crc_status(&ok, &valido, (void *)0, (void *)0);

    s_total++;

    /*
     * SOLO SE APUNTAN LAS QUE PASAN EL CRC. 25/09/2026.
     *
     * *** Por el dueno del proyecto: catorce renglones y once eran CRC malo,
     * asi que las buenas se iban de la pantalla enseguida. ***
     *
     * Una trama con el CRC malo no dice NADA: sus bytes son lo que el
     * Viterbi creyo ver, y ya se sabe que se equivoco. Ponerla en la lista no
     * es informacion, es tapar la que si vale.
     *
     * Lo que si dice algo es CUANTAS hay, y eso va a la chapa: "3/42" son
     * tres buenas de cuarenta y dos rafagas. Ese numero contesta a "va bien
     * la sintonia?" mucho mejor que once renglones de basura.
     */
    if (valido && ok && datos != (const uint8_t *)0 && largo != 0U) {
        s_buenas++;
        apunta_trama(datos, largo);
    }
}

uint32_t hfdl_modo_lineas(void)
{
    uint32_t n = s_tabla.count;
    return (n > HFDL_MODO_LINEAS) ? HFDL_MODO_LINEAS : n;
}
uint32_t hfdl_modo_simbolos_datos(void) { return s_simbolos_datos; }

const char *hfdl_modo_linea(uint32_t i) { return fila_avion(i); }

uint8_t hfdl_modo_pos(uint32_t i, int32_t *lat_e4, int32_t *lon_e4)
{
    const hfdl_ac_rec_t *r;

    /*
     * VA SOBRE LA TABLA ENTERA (s_tabla.count) y no sobre los renglones
     * que caben en la pantalla: en el mapa caben todos los aviones que se
     * hayan oido, no solo los que se ven en la lista.
     */
    if (i >= (uint32_t)s_tabla.count || s_tabla.rec == 0) { return 0U; }
    r = &s_tabla.rec[i];
    if ((r->flags & HFDL_AC_R_POS) == 0U) { return 0U; }
    if (lat_e4 != 0) { *lat_e4 = r->lat_e4; }
    if (lon_e4 != 0) { *lon_e4 = r->lon_e4; }
    return 1U;
}

/* Cuantos aviones hay en la TABLA, que son mas que los renglones. */
uint32_t hfdl_modo_aviones(void) { return (uint32_t)s_tabla.count; }

void hfdl_modo_borra(void)
{
    hfdl_ac_init(&s_tabla, s_aviones, sizeof s_aviones);
    s_nlineas = 0U;
    s_total   = 0U;
    s_buenas  = 0U;
    s_no_cupo = 0U;
    s_n_a1 = 0U; s_n_a2 = 0U; s_n_m1 = 0U;
}

void hfdl_modo_mete(const float *i_in, const float *q_in, uint32_t n)
{
    hfdl_estado_t *e = estado();
    float    si[HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT];
    float    sq[HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT];
    float    hi[HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT];
    float    hq[HFDL_DEMOD_CHAIN_MAX_SYMBOLS_OUT];
    uint32_t ns = 0U, k;

    if (!s_activo || e == (hfdl_estado_t *)0) { return; }
    if (n > HFDL_DEMOD_CHAIN_MAX_INPUT_BLOCK) { return; }

    hfdl_demod_chain_process(&e->cadena, i_in, q_in, n, si, sq, hi, hq, &ns);

    /*
     * El portero se consulta SIEMPRE -asi el suelo de ruido sigue
     * aprendiendo- pero solo manda cuando no hay una rafaga en curso: una
     * senal que baje a la mitad a mitad de trama no puede abortarla, porque
     * los simbolos que faltan no vuelven.
     */
    {
        uint8_t pasa = porton(hfdl_demod_chain_get_agc_power_avg(&e->cadena));

        if (!pasa && !e->en_segmento) { return; }
    }

    for (k = 0U; k < ns; k++) {
        hfdl_modo_simbolo(hi[k], hq[k], si[k], sq[k]);
    }
}

void hfdl_modo_simbolo(float ih, float qh, float i_sym, float q_sym)
{
    hfdl_estado_t *e = estado();
    hfdl_preamble_state_t st;

    if (!s_activo || e == (hfdl_estado_t *)0) { return; }

    if (e->en_segmento) {
        simbolo_de_segmento(e, ih, qh, i_sym, q_sym);
        return;
    }

    {
        hfdl_preamble_state_t previo = hfdl_preamble_sync_get_state(&e->preambulo);

        st = hfdl_preamble_sync_process_symbol(&e->preambulo, i_sym, q_sym);
        s_estado = (uint8_t)st;

        /* Se cuenta el SALTO, no el estado: quedarse en A2_SEARCH doscientos
         * simbolos es un enganche, no doscientos. */
        if (st != previo) {
            if (st == HFDL_PREAMBLE_A2_SEARCH) { s_n_a1++; }
            else if (st == HFDL_PREAMBLE_M1_SEARCH) { s_n_a2++; }
            else if (st == HFDL_PREAMBLE_LOCKED) { s_n_m1++; }
        }
    }

    if (st == HFDL_PREAMBLE_LOCKED) {
        const hfdl_frame_params_t *fp =
            hfdl_preamble_sync_get_frame_params(&e->preambulo);

        hfdl_data_segment_init(&e->segmento, fp->mod_arity,
                               fp->data_segment_cnt);
        /* mu = 0,1: el mismo "ancho de banda" que dumphfdl le pone a su
         * eqlms_cccf. Ver la cabecera de hfdl_equalizer.h. */
        hfdl_equalizer_init(&e->ecu, 0.1f);
        s_simbolos_datos = 0U;
        s_polaridad_hubo = 0U;
        e->corr_t = 0.0f;
        e->polaridad_fijada = 0U;
        e->decodificando = 0U;
        e->en_segmento = 1U;
    }
}

uint8_t hfdl_modo_polaridad(void)      { return s_polaridad_ultima; }
uint8_t hfdl_modo_polaridad_lista(void) { return s_polaridad_hubo; }
