#include "hfdl_modo.h"
#include "hfdl_ram.h"
#include "hfdl_demod_chain.h"
#include "hfdl_preamble_sync.h"
#include "hfdl_payload_decode.h"
#include "hfdl_data_segment.h"
#include "hfdl_acars.h"
#include "hfdl_payload_decode.h"
#include "hfdl_preamble_seq.h"
#include "hfdl_framer.h"
#include "hfdl_equalizer.h"
#include "hfdl_pdu.h"
#include "hfdl_hfnpdu.h"
#include "hfdl_aircraft.h"
#include "icao24_db.h"
#include "waterfall.h"

#include "rtc_hw.h"

#include <string.h>

/*
 * ======================================================================
 * 48 Y 48, COMO FT8 - 28/10/2026
 * ======================================================================
 *
 * *** Pedido por el usuario: "amplia a 48 aceptando que se borre al
 * salir". ***
 *
 * Eran 14 renglones y 20 aviones. El numero que importaba era el de
 * AVIONES, porque la tabla no la lee solo el panel: la lee EL MAPA (ver
 * hfdl_modo_aviones() y el bucle del mapa en main.c), asi que el mapa no
 * podia enseñar mas de veinte por llena que estuviera la banda. Es la
 * misma razon por la que FT8 paso de 16 a 48 renglones.
 *
 * El porque de 48 y no mas es el de FT8, palabra por palabra: mas filas no
 * darian mas informacion util.
 *
 * DONDE VIVEN AHORA, que es lo que lo hacia imposible: en la memoria
 * prestada, no en .bss. Ver HFDL_RAM_AVIONES en hfdl_ram.h para la cuenta
 * entera y para el comentario viejo que lo frenaba.
 */
#define HFDL_MODO_LINEAS 48U
#define HFDL_MODO_LARGO  72U
#define HFDL_MODO_AVIONES 48U

/*
 * *** Y DESDE EL 28/10/2026 NO SON ESTATICAS: viven en la memoria
 * prestada, como los estados. ***
 *
 * Aqui ponia esto, y la ultima frase es la que lo sostenia todo:
 *
 *   "Las lineas SI son estaticas, al contrario que los estados. Son 1.008
 *    bytes y tienen que sobrevivir a hfdl_modo_stop(): si vivieran en la
 *    RAM prestada, salir del modo para mirar otra cosa borraria lo que
 *    acababa de salir. Lo mismo hace FT8 con las suyas."
 *
 * Era verdad cuando se escribio y hoy no lo es: las lineas de FT8 viven
 * dentro de la union (ver `lineas` en ft8_shared_ram.h) y se borran al
 * salir, igual que todo lo demas. O sea que HFDL pagaba 2.048 bytes de
 * SRAM permanente por una garantia que su propia referencia ya no da.
 *
 * El usuario acepto el precio -la lista se borra al salir- a cambio de
 * pasar de 20 aviones a 48 en el mapa. Ver HFDL_RAM_AVIONES en hfdl_ram.h.
 *
 * *** Y SE PROBO A DEVOLVERLAS A .bss EL MISMO DIA, Y NO CABEN. ***
 *
 * Por la tarde, buscando por que la radio sacaba menos aviones que una
 * version anterior, se intento sacarlas de aqui para que la lista
 * sobreviviera al salir del modo. Los 48 aviones y los 48 renglones son
 * 5.952 bytes de .bss y el enlazador NO LOS DA: la radio va al 98,2 % de
 * su SRAM y quedan 3.544 libres. No es una preferencia, es que no hay.
 * Queda escrito para que nadie lo vuelva a intentar sin mirar el mapa de
 * memoria antes.
 *
 * LOS DOS TROZOS VAN AL FINAL DE LA CABECERA, detras del preambulo y de
 * los estados, que es donde no estorban a nadie. Los offsets salen de
 * hfdl_ram.h restando desde el final, asi que crecer cualquiera de los dos
 * no mueve a los otros.
 */
#define HFDL_OFF_AVIONES  (HFDL_RAM_CABECERA - HFDL_RAM_AVIONES - HFDL_RAM_LINEAS)
#define HFDL_OFF_LINEAS   (HFDL_RAM_CABECERA - HFDL_RAM_LINEAS)

_Static_assert((uint32_t)HFDL_MODO_AVIONES * sizeof(hfdl_ac_rec_t)
                   <= (uint32_t)HFDL_RAM_AVIONES,
               "los aviones de HFDL no caben en lo que hfdl_ram.h les reserva");
_Static_assert((uint32_t)HFDL_MODO_LINEAS * (uint32_t)HFDL_MODO_LARGO
                   <= (uint32_t)HFDL_RAM_LINEAS,
               "los renglones de HFDL no caben en lo que hfdl_ram.h les reserva");

/*
 * Los dos punteros a la region prestada. Devuelven NULL fuera del modo, y
 * eso NO es una comodidad: es lo que impide escribir sobre los pixeles de
 * la cascada o sobre lo que FT8 tenga ahi. Quien llama lo mira.
 */
static hfdl_ac_rec_t *aviones_ram(void)
{
    uint8_t *cab = hfdl_ram_cabecera();

    if (cab == (uint8_t *)0) { return (hfdl_ac_rec_t *)0; }
    return (hfdl_ac_rec_t *)(void *)(cab + HFDL_OFF_AVIONES);
}

static char *linea_ram(uint32_t i)
{
    uint8_t *cab = hfdl_ram_cabecera();

    if (cab == (uint8_t *)0 || i >= (uint32_t)HFDL_MODO_LINEAS) { return (char *)0; }
    return (char *)(void *)(cab + HFDL_OFF_LINEAS + (i * (uint32_t)HFDL_MODO_LARGO));
}
/*
 * UNA FILA POR AVION, NO UNA POR MENSAJE. 25/09/2026.
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
static hfdl_ac_table_t s_tabla;
/*
 * Y el renglon del volcado: "[46] FF D1 5A ..." con veinte bytes. Son
 * 1 del 0x01 + 5 de la longitud + 60 del hexadecimal + 1 del cero.
 */

/*
 * EL ANILLO DE CARGAS EN CRUDO - 14/10/2026.
 *
 * Diez LPDU de datos de usuario, todos de BAJADA -o sea del avion- y cero
 * posiciones. El decodificador busca el tipo 0xD1 y lo exige en el byte 0
 * o detras de un 0xFF, y eso sale de UNA sola trama real mirada a mano
 * hace un mes. Mientras no se vean los bytes, cualquier arreglo es
 * adivinar.
 *
 * NO VA EN hfdl_ac_rec_t, que es donde se intento primero: veinte bytes
 * por cada uno de los veinte aviones son 480, y al enlazar salio
 * "region `RAM' overflowed by 40 bytes". O sea que no cabia por 40, y
 * medirlo costo un enlace. Un anillo de ocho cuesta 224 y da de sobra:
 * lo que se quiere ver son las ultimas, y en la pantalla estrecha
 * habia CINCO aviones mandando datos.
 *
 * Veinte bytes porque en aquella trama el tipo estaba en el 10: deja el
 * doble de margen.
 */
#define HFDL_CRUDO_N      8U
#define HFDL_CRUDO_TXT   58U
typedef struct {
    uint32_t icao;      /* 0 si no se sabe */
    uint8_t  ac_id, gs_id;
    uint8_t  usada;     /* 0 = hueco libre */
    /*
     * EL RENGLON YA HECHO, no los bytes crudos. Se monta al recibir, que
     * es el unico momento en que esta la carga ENTERA: guardar veinte
     * bytes y descifrarlos al pintar dejaba el texto del ACARS fuera, y el
     * texto es justo lo que hay que leer.
     *
     * Empieza por 0x01, que para ui_digi.c significa "este renglon va
     * entero, sin columnas". Asi lo que se guarda es exactamente lo que se
     * pinta y no hay un segundo buffer que mantener.
     */
    char     txt[HFDL_CRUDO_TXT];
} hfdl_crudo_t;
static hfdl_crudo_t s_crudo[HFDL_CRUDO_N];
static uint8_t      s_crudo_sig;

static void pon_hex(char *d, uint8_t *p, const uint8_t *b, uint32_t n, uint32_t tope)
{
    static const char k_hex[] = "0123456789ABCDEF";
    uint32_t c;
    for (c = 0U; c < n && (uint32_t)(*p) + 3U < tope; c++) {
        d[(*p)++] = k_hex[(b[c] >> 4) & 0x0FU];
        d[(*p)++] = k_hex[b[c] & 0x0FU];
        d[(*p)++] = ' ';
    }
}

static void pon_txt(char *d, uint8_t *p, const char *s2, uint32_t tope)
{
    uint32_t c;
    for (c = 0U; s2[c] != '\0' && (uint32_t)(*p) + 1U < tope; c++) {
        d[(*p)++] = s2[c];
    }
}

/*
 * Y LAS MISMAS DOS, CON TOPE - 16/10/2026. ESTO SE SALIA DEL BUFFER.
 *
 * pon_txt() recibe `tope` y lo respeta desde el primer dia. Los otros dos
 * modos de escribir en la misma linea no: `l[p++] = c` escribe donde
 * caiga, y aj_u2s_local(&l[p], v) mete tantos digitos como tenga el
 * numero y un '\0' detras.
 *
 * Donde se veia: hfdl_modo_linea_crc() arma el renglon de diagnostico
 * sobre un static char l[HFDL_CRUDO_TXT] de 58 bytes, y los numeros que
 * mete son CONTADORES DE TRAMAS -s_crc_ok[], s_crc_tot[]-, que en una
 * tarde de escucha pasan de mil sin despeinarse. Con dos modos activos y
 * cuatro cifras en cada cuenta, la linea se sale. Medido con
 * AddressSanitizer: escritura 0 bytes detras de `l`.
 *
 * La guarda que habia -if (p > HFDL_CRUDO_TXT - 16) break;- estaba al
 * FINAL de la vuelta, o sea que se preguntaba si habia sitio despues de
 * haberlo gastado. Ahora va al principio, y ademas sobra: con estas dos
 * ya no se puede escribir de mas.
 */
static void pon_car(char *d, uint8_t *p, char c, uint32_t tope)
{
    if ((uint32_t)(*p) + 1U < tope) { d[(*p)++] = c; }
}

static void pon_u32t(char *d, uint8_t *p, uint32_t v, uint32_t tope)
{
    char t[10];
    uint8_t n = 0U;

    if (v == 0U) { pon_car(d, p, '0', tope); return; }
    while (v != 0U && n < (uint8_t)sizeof t) {
        t[n++] = (char)('0' + v % 10U);
        v /= 10U;
    }
    while (n > 0U) { pon_car(d, p, t[--n], tope); }
}

/*
 * Monta el renglon de una carga de datos. Si es ACARS -tipo 0xFF- sale
 * descifrado; si no, en hexadecimal, que es lo que hace falta para
 * averiguar que es lo que no se entiende.
 */
static void crudo_texto(char *d, const uint8_t *b, uint32_t n)
{
    uint8_t p = 0U;
    uint32_t cont = 0U;
    uint8_t tipo;

    d[p++] = '\x01';
    tipo = hfdl_hfnpdu_tipo(b, n, &cont);
    if (tipo == 0xFFU && n > cont) {
        hfdl_acars_t a;
        if (hfdl_acars_parse(&b[cont], n - cont, &a)) {
            pon_txt(d, &p, a.matricula, HFDL_CRUDO_TXT);
            pon_txt(d, &p, " ", HFDL_CRUDO_TXT);
            pon_txt(d, &p, a.etiqueta, HFDL_CRUDO_TXT);
            pon_txt(d, &p, a.ack ? "/ACK " : "/NAK ", HFDL_CRUDO_TXT);
            pon_txt(d, &p, a.texto, HFDL_CRUDO_TXT);
            d[p] = '\0';
            return;
        }
    }
    /* Lo que no se entiende, en crudo: es lo unico que permite averiguarlo
     * sin adivinar, y adivinar es lo que ya costo un mes aqui. */
    pon_hex(d, &p, b, (n > 18U) ? 18U : n, HFDL_CRUDO_TXT);
    d[p] = '\0';
}

/*
 * VACIA EL ANILLO DE CARGAS CRUDAS - 29/10/2026, revision a fondo.
 *
 * No lo vaciaba nadie: ni start(), ni stop(), ni borra(). Vive en .bss, o
 * sea que sobrevive a todo mientras la radio este encendida. Y desde la
 * V6.12 la tabla de aviones SI se borra al salir -vive en la memoria
 * prestada-, asi que las dos mitades del mismo renglon dejaron de ir
 * juntas.
 *
 * El cuadro: crudo_de() empareja renglon y volcado por OACI, y si no la
 * hay por el par (estacion, identificador). Ese identificador lo reparte
 * la estacion de tierra por sesion y LO RECICLA. Asi que el avion 59 de
 * la estacion 7 de hace media hora le deja su texto ACARS pegado al avion
 * 59 de la estacion 7 de ahora, que es otro. En la vista de Bytes, que es
 * justo la pantalla que se abre para no adivinar.
 *
 * La regla, otra vez: lo que borra hfdl_modo_borra() lo tiene que borrar
 * tambien hfdl_modo_start(), y lo que vive en la memoria prestada obliga a
 * mirar quien mas guarda lo mismo fuera de ella.
 */
static void crudo_vacia(void)
{
    memset(s_crudo, 0, sizeof s_crudo);
    s_crudo_sig = 0U;
}

/* Guarda una carga. La clave es la OACI si se sabe, y si no el par
 * (estacion, identificador), que es exactamente como empareja
 * hfdl_ac_update(): asi la fila y su volcado hablan del mismo avion. */
static void crudo_pon(uint32_t icao, uint8_t ac_id, uint8_t gs_id,
                      const uint8_t *d, uint32_t n)
{
    hfdl_crudo_t *c;
    uint8_t i, k = HFDL_CRUDO_N;

    if (d == (const uint8_t *)0 || n == 0U) { return; }
    /* Si ese avion ya tiene sitio se le pisa el suyo, que si no ocho
     * mensajes del mismo llenan el anillo y tapan a los demas. */
    for (i = 0U; i < HFDL_CRUDO_N; i++) {
        if (!s_crudo[i].usada) { continue; }
        if (icao != 0UL) { if (s_crudo[i].icao == icao) { k = i; break; } }
        else if (s_crudo[i].icao == 0UL && s_crudo[i].ac_id == ac_id
                 && s_crudo[i].gs_id == gs_id) { k = i; break; }
    }
    if (k == HFDL_CRUDO_N) {
        k = s_crudo_sig;
        s_crudo_sig = (uint8_t)((s_crudo_sig + 1U) % HFDL_CRUDO_N);
    }
    c = &s_crudo[k];
    c->icao = icao; c->ac_id = ac_id; c->gs_id = gs_id;
    c->usada = 1U;
    crudo_texto(c->txt, d, n);
}

static const hfdl_crudo_t *crudo_de(const hfdl_ac_rec_t *r)
{
    uint8_t i;
    for (i = 0U; i < HFDL_CRUDO_N; i++) {
        if (!s_crudo[i].usada) { continue; }
        if ((r->flags & HFDL_AC_R_ICAO) != 0U) {
            if (s_crudo[i].icao == r->icao) { return &s_crudo[i]; }
        } else if (s_crudo[i].icao == 0UL
                   && s_crudo[i].ac_id == r->ac_id
                   && s_crudo[i].gs_id == r->gs_id) {
            return &s_crudo[i];
        }
    }
    return (const hfdl_crudo_t *)0;
}
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
    /*
     * LA PARTE EN CUADRATURA DE ESA MISMA CORRELACION - 14/10/2026.
     *
     * La correlacion contra la secuencia de entrenamiento usaba SOLO la I
     * y se quedaba con el signo. Eso resuelve la ambiguedad de +-180 y
     * para BPSK es TODA la ambiguedad que hay, por eso funciona. Pero en
     * 4-PSK la constelacion puede estar girada +-90 y en 8-PSK +-45, y un
     * signo no puede decir eso: si esta girada un paso, todos los
     * simbolos dan bits equivocados y el CRC no pasa jamas.
     *
     * Con la I y la Q, la correlacion es un numero COMPLEJO y su ANGULO
     * es el giro que sobra. El multiplo de 2pi/M mas cercano es lo que
     * hay que deshacer. Para M=2 eso es el signo de la I, o sea lo de
     * antes: la formula de siempre es el caso particular de esta.
     */
    float                corr_tq;
    /*
     * LA ENERGIA DE LOS MISMOS SIMBOLOS, para poder normalizar.
     * corr_t/corr_tq solos no dicen nada: valen lo que valga la amplitud
     * que haya puesto el AGC. Divididos por la energia de los simbolos
     * que los produjeron, salen entre 0 y 1 y ya se pueden comparar con
     * un umbral fijo. Ver portero_entrenamiento().
     */
    float                ener_t;
    uint16_t             n_t;
    uint8_t              en_segmento;
    uint8_t              polaridad;
    uint8_t              polaridad_fijada;
    /* El giro absoluto ya resuelto, para deshacerlo en cada simbolo de
     * datos. Solo se usa con M > 2; en BPSK manda `polaridad`, que es el
     * camino de siempre y no se toca. */
    float                giro_cos, giro_sen;
    /* La aridad que dice el M1, guardada hasta que empiecen los datos:
     * el entrenamiento es BPSK y el detector de fase tiene que saberlo. */
    uint8_t              aridad_trama;
    uint8_t              decodificando;   /* ver simbolo_de_segmento() */
} hfdl_estado_t;

/*
 * VOLATIL DESDE EL 29/10/2026. El comentario que viene justo debajo dice
 * que todo lo que cruza con la interrupcion de audio va marcado volatile
 * 'y aqui faltaba casi todo' - y esta, que es la que decide si la
 * interrupcion entra o no, se quedo fuera una linea por encima de su
 * propia regla. La escribe el bucle principal (start/stop) y la leen
 * hfdl_modo_mete() y hfdl_modo_simbolo(), que corren en la interrupcion.
 *
 * En el binario de hoy no hace daño -hfdl_modo_stop() escribe el cero
 * antes de soltar la memoria y el compilador no lo reordena- pero es la
 * misma clase de contrato no escrito que s_hay en iqbal.c.
 */
static volatile uint8_t  s_activo;
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
 * Las trae hfdl_systable.c DEL PROYECTO PADRE (aqui habia una copia y se
 * borro el 10/10/2026 por aportar cero al binario), pero detras de un
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
/*
 * ...Y DE ESAS, CUANTAS ERAN DEL GIRO 7. 10/10/2026.
 *
 * Hay DOS motivos distintos detras de ese numero y se arreglan de formas
 * opuestas (ver hfdl_ram.h con las cuentas de los ocho giros):
 *
 *   giro 6, 1200 bps doble: pide 51.088 y hay 45.216. Faltan 5.872 bytes,
 *           y en la radio quedan 19.412 libres. SE PUEDE ARREGLAR.
 *   giro 7, 1800 bps doble: pide 76.288 en una union de 54 kB.
 *           NO SE ARREGLA ensanchando nada razonable.
 *
 * O sea que "X7" puede valer un ensanchado de RAM o no valer nada, y por
 * la pantalla de hoy no hay forma de saber cual de los dos es.
 *
 * NO SE ENSANCHA NADA A CIEGAS. Primero sale el numero. Como el desglose
 * entero no cabe en la chapa -se midio: "99% 999/999 X99/99" son 152 px de
 * 140- lo que cambia es LA LETRA, que no cuesta un pixel mas:
 *
 *   X  la mayoria eran del giro 6  -> ensanchar la RAM las recupera
 *   Z  la mayoria eran del giro 7  -> ensanchar no sirve de nada
 *
 * Con que el usuario diga "pone X7" o "pone Z7" ya se sabe si gastar los
 * 5.872 bytes. Un numero antes de mover un limite, no al reves.
 */
static volatile uint32_t s_no_cupo_7;

/*
 * ======================================================================
 * EL CERROJO DE LA MEMORIA PRESTADA - 10/10/2026
 * ======================================================================
 *
 * Sale de una revision a fondo. ESTO NO LO HABIA VISTO NADIE y es real.
 *
 * EL PROBLEMA. Dos sitios distintos escriben la MISMA memoria:
 *
 *   hfdl_modo_mete()  corre EN LA INTERRUPCION del audio y, cuando el
 *                     preambulo engancha, llama a begin_segment() -que
 *                     reconfigura el desentrelazador entero- y luego mete
 *                     simbolos.
 *   hfdl_modo_poll()  corre EN EL BUCLE PRINCIPAL y llama a finish_ex(),
 *                     que pasa el Viterbi y escribe su salida en
 *                     ram + sizeof(estado), que es EXACTAMENTE donde
 *                     begin_segment() pone la tabla del desentrelazador.
 *
 * Y entre los dos no habia nada. Ni cerrojo, ni interrupciones quitadas,
 * ni una bandera que uno mirara antes de empezar.
 *
 * COMO SE DISPARA, paso a paso:
 *
 *   1. Acaba una rafaga: s_finish_pendiente = 1 y -dos lineas mas abajo-
 *      hfdl_preamble_sync_reset(), o sea que el correlador vuelve a buscar
 *      preambulo YA.
 *   2. El bucle principal tarda en llegar aqui. Puede tardar MUCHO: en ese
 *      mismo bucle hay escrituras a la flash SPI, y borrar un sector son
 *      decenas o cientos de milisegundos. El preambulo de HFDL dura unos
 *      27 ms, asi que da tiempo de sobra a que entre otra rafaga.
 *   3. La interrupcion abre el segmento nuevo y empieza a llenar la tabla.
 *   4. AHORA entra poll(): el Viterbi lee un cursor que le han reseteado
 *      por debajo y escribe su salida encima de la tabla que la
 *      interrupcion esta usando.
 *
 * Se pierden LAS DOS: la que se remataba sale corrupta y la nueva se queda
 * con la tabla pisada. Con el CRC no pasa ninguna, asi que no se ve un
 * fallo: se ve una radio que decodifica menos de lo que deberia. Que es
 * justo de lo que se quejaba el usuario.
 *
 * (No he podido demostrar que se salga de los limites: toda la aritmetica
 * de indices se vuelve a acotar aguas abajo. Lo que si esta demostrado es
 * que los dos contextos escriben los mismos bytes sin coordinarse.)
 *
 * EL ARREGLO: un cerrojo de un byte. Mientras el bucle principal esta
 * rematando, la interrupcion NO abre segmento nuevo.
 *
 * Y EL ORDEN IMPORTA, porque s_finish_pendiente NO SIRVE de cerrojo: poll()
 * lo pone a cero en su segunda linea y el trabajo largo viene DESPUES, o
 * sea que durante todo el remate la bandera ya vale 0. Por eso el cerrojo
 * se COGE ANTES de soltar la bandera: entre las dos escrituras las dos
 * valen 1, y la interrupcion ve una u otra pase lo que pase. No queda
 * hueco.
 *
 * Lo que cuesta: la rafaga nueva se pierde. Pero se pierde LIMPIA y
 * CONTADA, en vez de llevarse por delante tambien la anterior.
 */
static volatile uint8_t  s_remate;    /* 1 = el bucle principal usa la RAM */
static volatile uint32_t s_pisada;    /* rafagas descartadas por eso */

/*
 * CUANTOS USER DATA Y CUANTAS POSICIONES - 10/10/2026.
 *
 * Y no habia forma de contestarle. La radio contaba tramas buenas y totales
 * -"19/117"- y nada mas, asi que no salir coordenadas no se podia distinguir
 * de no haber llegado ninguna. Son dos averias distintas con dos
 * arreglos distintos, y sin estos dos numeros se contestaba a ojo.
 *
 * En HFDL la posicion SOLO viaja dentro de un LPDU de datos de usuario, y
 * solo si lo que lleva dentro es un HFNPDU de tipo 0xD1. Todo lo demas
 * -LOGON, RESUME, CONFIRM, LOGOFF- es gestion del enlace. Asi que:
 *
 *   s_ud   cuantos LPDU de datos de usuario han entrado
 *   s_pos  cuantas posiciones se han sacado, VENGAN DE DONDE VENGAN
 *
 * Con los dos delante: 0 y 0 es "no ha llegado nada que pueda llevar
 * posicion, ten paciencia o cambia de frecuencia"; 12 y 0 es "estan
 * llegando y no se estan leyendo", que es un fallo nuestro.
 *
 * Y EL SEGUNDO NO ES UN SUBCONJUNTO DEL PRIMERO - corregido el
 * 29/10/2026, revision a fondo. Este comentario decia "de esos, cuantos
 * han dado una posicion", y era verdad el dia que se escribio. Hoy s_pos
 * sube en CUATRO sitios y solo uno de ellos es un LPDU de datos de
 * usuario: tambien suben la posicion rescatada de un LPDU con el CRC malo
 * y la que viene dentro de un LOGON REQUEST -que es, dicho por el propio
 * codigo, la trama mas frecuente que manda un avion-. Asi que s_pos puede
 * ser MAYOR que s_ud, y leerlo como una razon no significa nada.
 */
static volatile uint32_t s_ud;
static volatile uint32_t s_pos;
/*
 * BUENAS Y TOTALES POR VELOCIDAD DE MODULACION - 14/10/2026.
 *
 * Setenta y una sincronizadas y quince buenas: se pierde el 79 % en el
 * CRC. Para saber DONDE hay que separarlas, y la sospecha tiene nombre y
 * esta escrita en hfdl_costas.c: durante los datos la correccion de fase
 * de cada simbolo supone BPSK SIEMPRE. En 4-PSK y 8-PSK eso no es una
 * aproximacion, es falso.
 *
 * Indice = bits por simbolo: 1 BPSK, 2 4-PSK, 3 8-PSK. Si la sospecha
 * vale, las buenas se amontonaran en el 1. Y si no vale, esto lo dice
 * igual y me ahorra construir lo que no hace falta.
 */
/*
 * LA ESPIA DE SIMBOLOS - 14/10/2026.
 *
 * El banco de audio -sim/hfdl_extremo.c- mete una rafaga por la cadena
 * entera y el CRC falla con canal perfecto, pero llegan los 2160 simbolos
 * exactos. O sea que el encuadre esta bien y el contenido no, y desde
 * fuera no hay forma de saber si llegan girados, escalados, corridos o
 * hechos papilla.
 *
 * Esto deja que un banco se lleve una copia de lo que entra al
 * decodificador para compararlo con lo que se transmitio. Es la misma
 * clase de puerta que ya tienen hfdl_payload_decode_tx_simbolos() y
 * hfdl_payload_decode_selftest_fase(): existe para que se pueda MEDIR, y
 * en la radio esta apagada -los punteros valen cero- asi que no cuesta ni
 * un ciclo.
 */
static float    *s_espia_i;
static float    *s_espia_q;
static uint32_t  s_espia_cap;
static uint32_t  s_espia_n;

void hfdl_modo_espia(float *i_out, float *q_out, uint32_t cap)
{
    s_espia_i = i_out; s_espia_q = q_out; s_espia_cap = cap; s_espia_n = 0U;
}

uint32_t hfdl_modo_espia_n(void) { return s_espia_n; }

static volatile uint32_t s_crc_ok[4];
static volatile uint32_t s_crc_tot[4];

void hfdl_modo_datos(uint32_t *ud, uint32_t *pos)
{
    if (ud)  { *ud  = s_ud; }
    if (pos) { *pos = s_pos; }
}

/* La ultima estacion de tierra oida. Su pantalla la pone arriba ("GS 3
 * REYKJAVIK") y no en cada fila, y tiene razon: estas en una frecuencia, o
 * sea en una estacion. Repetirla catorce veces era gastar una columna. */
static volatile uint8_t s_gs_ultima = 0xFFU;

/*
 * EL PORTERO: NO BUSCAR PREAMBULO CUANDO NO HAY NADA QUE BUSCAR.
 * 25/09/2026.
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
/*
 * ======================================================================
 * Y EL RELOJ DEL PORTERO SE MIDE EN TIEMPO, NO EN BLOQUES - 29/10/2026,
 * revision a fondo.
 * ======================================================================
 *
 * Estas cuatro constantes estaban en BLOQUES, y el bloque no dura siempre
 * lo mismo: son 32 muestras a 12 kHz cuando la radio va a 96 o 192 kHz
 * -2,667 ms- y 64 cuando va a 48 kHz -5,333 ms-. O sea que el portero que
 * protege al buscador de preambulo tenia DOS comportamientos distintos
 * segun un ajuste que no es de HFDL, y el doble de lento en el peor caso:
 *
 *                       96/192 kHz      48 kHz
 *   cebado                 0,53 s       1,07 s
 *   el suelo baja        tau  53 ms   tau 107 ms
 *   el suelo sube        tau 5,3 s    tau 10,7 s
 *   harto (abre solo)       53 s        107 s
 *
 * Los numeros de la columna de la izquierda son los que se midieron y los
 * que funcionan, asi que se quedan EXACTAMENTE como estaban: lo que se
 * hace es escribirlos en milisegundos y derivar los bloques del tamaño de
 * bloque que llega de verdad. A 96 kHz sale el mismo numero entero que
 * habia; a 48 kHz, por fin, el mismo TIEMPO.
 *
 * El tamaño de bloque lo trae hfdl_modo_mete() en su parametro n, asi que
 * no hay que preguntarle a nadie ni duplicar una constante de demod_am.c.
 */
#define HFDL_SQ_CEBADO_MS    533U   /* mirando antes de fiarse del suelo */
#define HFDL_SQ_HARTO_MS   53333U   /* cerrado seguidos -> abre solo */
#define HFDL_SQ_FS_HZ      12000U   /* la tasa a la que entra I/Q en HFDL */

/*
 * Y LA VALVULA ABRE DE VERDAD, NO UN BLOQUE - 29/10/2026.
 *
 * La cabecera promete que si lleva mucho rato sin dejar pasar a nadie,
 * abre solo, y que uno que se queda abierto devuelve el ruido que habia
 * antes. No se quedaba abierto: al bloque siguiente entraba por la rama de
 * "estoy abierto" y, con la razon por debajo del umbral, volvia a cerrar;
 * y s_sq_cerrado ya se habia puesto a cero, asi que arrancaba otros 53
 * segundos. O sea una ventana de 2,67 ms cada 53 s: la valvula estaba
 * practicamente muerta y no cubria el fallo que dice cubrir.
 *
 * CUANTO SE QUEDA ABIERTA, Y POR QUE NO MEDIO SEGUNDO. La primera version
 * de esta tarde puso 533 ms razonando que "lo unico que pasa es que entra
 * medio segundo de ruido". Eso choca de frente con la unica medida que
 * este fichero tiene sobre el asunto, sesenta lineas mas abajo, del banco
 * de aire real:
 *
 *     sin portero:  11 enganches, 11 tramas, 2 buenas, 1 renglon
 *
 * y la conclusion que va con ella: lo que el portero tira ES ruido, y
 * dejarlo pasar no solo gasta tiempo, se come rafagas BUENAS que llegan
 * mientras la cadena esta ocupada con la basura - apagarlo costaba cuatro
 * buenas de seis. Y peor: si el ruido engancha en falso, la cadena entra
 * en segmento y el portero YA NO la puede abortar, por decision escrita.
 *
 * Ademas, medio segundo no sirve para lo que se decia: una rafaga de HFDL
 * dura de 1,2 a 2,8 s, asi que no cabe entera de todos modos; y el suelo
 * se actualiza en CADA bloque, este el porton abierto o cerrado, asi que
 * la valvula no hace falta para que el suelo se ponga al dia.
 *
 * O sea que la valvula solo existe para "mejor abierto que mudo", y para
 * eso basta con que dure mas de un bloque. Se le dan 150 ms -unas tres
 * constantes de tiempo del suelo bajando- que es bastante para que se note
 * y poco para lo que cuesta. Si alguna vez el porton se queda cerrado de
 * verdad, lo que hay que arreglar es el umbral, no la valvula.
 */
#define HFDL_SQ_FORZADO_MS   150U

static volatile float    s_suelo;
static volatile float    s_razon;
static volatile uint32_t s_sq_visto;
static volatile uint32_t s_sq_cerrado;
static volatile uint8_t  s_sq_abierto;
static volatile uint8_t  s_sq_forzado;  /* 1 = se abrio por hartazgo, no por senal */

/* Devuelve 1 si hay que dejar pasar el bloque al sincronismo. */
/*
 * Los relojes del portero en bloques, derivados del tamaño de bloque que
 * llega. Se recalculan solo cuando n cambia, que es casi nunca.
 */
static uint32_t s_sq_cebado  = 200U;    /* 533 ms con bloques de 32 */
static uint32_t s_sq_harto   = 20000U;  /* 53,3 s idem */
static uint32_t s_sq_forzado_n = 200U;  /* lo que dura la valvula abierta */
static uint32_t s_sq_bloque    = 0U;    /* el ultimo n visto */
static uint32_t s_sq_abierta_n = 0U;    /* bloques que lleva forzada */

static void porton_relojes(uint32_t n)
{
    uint32_t por_s;

    if (n == 0U || n == s_sq_bloque) { return; }
    s_sq_bloque = n;
    por_s = HFDL_SQ_FS_HZ / n;                 /* bloques por segundo */
    /* Redondeando, no truncando: con bloques de 32 los milisegundos de
     * arriba tienen que devolver EXACTAMENTE los 200 y los 20.000 que
     * habia escritos, no 199 y 19.999. Asi este cambio no mueve ni un
     * bloque el comportamiento que esta medido. */
    s_sq_cebado    = (HFDL_SQ_CEBADO_MS  * por_s + 500U) / 1000U;
    s_sq_harto     = (HFDL_SQ_HARTO_MS   * por_s + 500U) / 1000U;
    s_sq_forzado_n = (HFDL_SQ_FORZADO_MS * por_s + 500U) / 1000U;
    if (s_sq_cebado    == 0U) { s_sq_cebado    = 1U; }
    if (s_sq_harto     == 0U) { s_sq_harto     = 1U; }
    if (s_sq_forzado_n == 0U) { s_sq_forzado_n = 1U; }
}

/*
 * EL NIVEL DE ENTRADA HA CAMBIADO: EL SUELO QUE HABIA YA NO VALE.
 * 29/10/2026, cuarta pasada.
 *
 * Desde la V6.15 entrar en HFDL sube el PGA de 20,0 a 35,0 dB. Y el orden
 * real de la rejilla de modos es:
 *
 *   1. apply_demod_mode(USB)      todavia a 20,0 dB
 *   2. hfdl_modo_start()          el portero siembra su suelo A 20,0 dB
 *   3. pga_zona_sync()            +15 dB de golpe
 *
 * El portero mide la potencia ANTES del AGC del modo -eso esta escrito en
 * hfdl_agc.h-, asi que esos 15 dB multiplican p por treinta y uno mientras
 * el suelo sigue siendo el de antes. s_razon se va a treinta, el porton se
 * queda abierto de par en par, y el suelo solo sube con alfa 0,0005 por
 * bloque: hacen falta unos mil trescientos bloques, o sea TRES SEGUNDOS Y
 * MEDIO a 96 kHz y SIETE a 48, con todo el ruido entrando al buscador de
 * preambulo. Justo al entrar en el modo, que es cuando uno mira si la
 * frecuencia tiene trafico.
 *
 * Y lo que cuesta dejar pasar ruido esta medido aqui mismo: sin portero,
 * once enganches para dos tramas buenas y un renglon.
 *
 * Esto lo arregla por donde hay que arreglarlo: cuando el nivel de entrada
 * cambia por una decision de la radio, el suelo aprendido deja de valer y
 * el portero vuelve a su estado de cebado -pasa todo medio segundo y
 * vuelve a aprender-, que es exactamente lo que hace al arrancar y por el
 * mismo motivo: todavia no se sabe donde esta el suelo.
 *
 * NO se llama desde el lazo del AGC de radiofrecuencia, que mueve la
 * ganancia cada 50 ms: eso dejaria el portero cebandose para siempre. Solo
 * desde pga_zona_sync(), o sea cuando se cruza de zona.
 */
void hfdl_modo_nivel_cambiado(void)
{
    if (!s_activo) { return; }
    s_suelo   = 0.0f;
    s_razon   = 0.0f;
    s_sq_visto = 0U;
    s_sq_cerrado = 0U;
    s_sq_abierto = 1U;
    s_sq_forzado = 0U;
    s_sq_abierta_n = 0U;
}

static uint8_t porton(float p)
{
    if (p <= 0.0f) { p = 1e-12f; }

    /* Baja rapido, sube despacio: el suelo persigue los valles y se deja
     * ignorar por los picos, que es justo lo contrario de una media. */
    if (s_suelo <= 0.0f)   { s_suelo = p; }
    else if (p < s_suelo)  { s_suelo += (p - s_suelo) * 0.05f; }
    else                   { s_suelo += (p - s_suelo) * 0.0005f; }

    s_razon = p / (s_suelo + 1e-12f);

    if (s_sq_visto < s_sq_cebado) {
        /* Todavia no se sabe donde esta el suelo: pasa todo. Cerrar aqui
         * seria decidir con lo que no se sabe. */
        s_sq_visto++;
        s_sq_abierto = 1U;
        return 1U;
    }

    if (s_sq_abierto) {
        /*
         * Y si esta abierta A LA FUERZA, se le respeta su medio segundo:
         * cerrar al bloque siguiente es lo que dejaba la valvula en una
         * ventana de 2,67 ms. Ver HFDL_SQ_FORZADO_MS.
         */
        if (s_sq_forzado) {
            if (++s_sq_abierta_n >= s_sq_forzado_n) {
                s_sq_forzado = 0U;
                s_sq_abierta_n = 0U;
            }
        } else if (s_razon < HFDL_SQ_CIERRA) {
            s_sq_abierto = 0U;
        }
    } else {
        if (s_razon > HFDL_SQ_ABRE)   { s_sq_abierto = 1U; s_sq_forzado = 0U; }
    }

    if (s_sq_abierto) {
        s_sq_cerrado = 0U;
    } else if (++s_sq_cerrado > s_sq_harto) {
        /* Ver la cabecera: mejor abierto de mas que mudo. */
        s_sq_abierto = 1U;
        s_sq_forzado = 1U;
        s_sq_abierta_n = 0U;
        s_sq_cerrado = 0U;
    }
    return s_sq_abierto;
}        /* rafagas con el CRC bueno - ver hfdl_modo_poll() */
/* Ráfagas tiradas por el portero: enganches que no llevaban detras una
 * secuencia de entrenamiento. Ver portero_entrenamiento(). */
static volatile uint32_t s_rechazadas;

static float    s_pk_ri, s_pk_rq, s_pk_p;   /* los acumuladores de la ventana */
static float    s_pk_zi, s_pk_zq;           /* el simbolo anterior */
static uint8_t  s_pk_n;                     /* simbolos metidos en esta ventana */
static uint8_t  s_pk_racha;                 /* ventanas seguidas por encima del umbral */
static uint8_t  s_pk_dentro;                /* 1 mientras dure un pre-key, para contarlo una vez */
static volatile uint32_t s_pk_vistos;       /* cuantos pre-key van */
static volatile float    s_pk_desvio_hz;    /* el desvio que dijo el ultimo */
static float             s_pk_w;            /* el mismo, en radianes por simbolo */
static uint8_t           s_pk_pendiente;    /* hay un empujon por dar */
static uint32_t          s_pk_edad = 0xFFFFFFFFu; /* simbolos desde el ultimo pre-key */
static volatile uint32_t s_pk_sin_armar;    /* enganches llegados sin pre-key delante */
/*
 * ======================================================================
 * Y CUANTOS DE ESOS TONOS LLEVABAN RAFAGA DETRAS - 14/10/2026
 * ======================================================================
 *
 * *** Esto corrige un numero MIO, no uno de la radio. ***
 *
 * "P14 es han pasado catorce ráfagas por la antena, pase lo que pase
 * despues" -lo escribi yo ahi abajo esta misma manana- ERA FALSO, y
 * encima era el numero sobre el que se estaba decidiendo donde mirar.
 *
 * Medido sobre los 214 segundos de la grabacion por CAT: 43 tonos,
 * y solo 18 llevan preambulo detras. Los otros 25 son portadoras limpias
 * de 0,2 a 0,36 segundos, al mismo nivel y en las mismas frecuencias, y
 * detras no hay NADA: ni A1, ni la pareja A1/A2 repetida a 127 simbolos,
 * ni a ningun desvio de +-120 Hz, ni a ninguna fase de muestreo, ni
 * girado 45, 90 o 135 grados. Comprobado tres veces y la ultima fuera
 * del firmware entero, en numpy, para no creerme mi propio codigo: en
 * las buenas la secuencia que sale del aire coincide BIT A BIT con mi
 * tabla; en las otras no hay secuencia.
 *
 * O sea que "46 ráfagas y solo 10 buenas" nunca existio. Son 18 ráfagas,
 * se enganchan 16 y salen 10. Eso es otra averia, mucho mas pequeña y en
 * otro sitio, y medio dia de buscar las dieciocho que faltaban era medio
 * dia buscando algo que no estaba.
 *
 * Asi que la pantalla pasa a decir las dos cosas: P<tonos>/<con ráfaga>.
 */
static volatile uint32_t s_pk_con_rafaga;
static uint8_t           s_pk_ya_contado;   /* este tono ya sumo */
/* DIAGNOSTICO: la edad del pre-key en el instante de cada enganche. Es
 * el numero que hace falta para entender por que filtrar A1 por la
 * franja dejo el banco a cero dos veces. */
static volatile uint32_t s_pk_edad_min = 0xFFFFFFFFu;
static volatile uint32_t s_pk_edad_max;
/* DIAGNOSTICO: separacion entre pre-key consecutivos, en simbolos. Si
 * salen muchos muy juntos, el detector esta partiendo un mismo tono en
 * dos y el numero P de la pantalla miente por exceso. */
static uint32_t          s_pk_desde_ultimo;
static volatile uint32_t s_pk_juntos;     /* separados por menos de media ranura */
/* CONTESTADO Y RETIRADO DE LA RADIO - 14/10/2026. La primera sospecha
 * sobre los 46 tonos fue que el detector partiera uno en dos. Medido: de
 * 46, UNO cae a menos de un tono del anterior. No era eso, asi que estos
 * dos contadores se quedan solo en el banco y le devuelven 8 bytes a la
 * RAM, que va al 99,9%. */
#if HFDL_DIAG
static volatile uint32_t s_pk_mismo_tono;
static volatile uint32_t s_pk_gap_min = 0xFFFFFFFFu;
#endif

/*
 * ======================================================================
 * QUE PASA CON LOS PRE-KEY QUE NO LLEGAN A ENGANCHAR - 14/10/2026
 * ======================================================================
 *
 * Sobre la grabacion de referencia: 46 tonos de verdad, 28 enganches de A1.
 * Dieciocho ráfagas que el pre-key ve y el preambulo no. La primera
 * sospecha -que el detector partiera un tono en dos- esta DESCARTADA
 * midiendo: de los 46, uno solo cae a menos de un tono del anterior.
 *
 * Asi que la pregunta buena es otra: en esos dieciocho, ¿cuanto sube la
 * correlacion de A1? Si se queda en 0,30 con el umbral en 0,36, el
 * umbral es el problema y se baja. Si no sube de 0,10, ahi NO hay
 * preambulo que valga y el tono es de otra cosa -otra estacion, otro
 * canal, una ráfaga que se desvanece-, y entonces los 46 no son 46
 * ráfagas perdidas y no hay nada que arreglar.
 *
 * Esto no entra en la radio: son cuatro contadores y un maximo que solo
 * sirven para contestar a esa pregunta, y la RAM esta al 99,9%.
 */
#if HFDL_DIAG
static uint8_t  s_dg_mirando;      /* dentro de la franja de un pre-key */
static float    s_dg_mejor;        /* maximo |correlacion| de A1 en la franja */
static volatile uint32_t s_dg_pk_con_a1;   /* pre-key que acabaron enganchando */
static volatile uint32_t s_dg_banda[4];    /* los que no: <0,15 / <0,25 / <0,36 / >=0,36 */
static volatile uint32_t s_dg_ok[4];       /* los que SI, por el mismo maximo */
static volatile float    s_dg_coh_si, s_dg_coh_no;  /* coherencia media del tono */
static volatile uint32_t s_dg_nsi, s_dg_nno;
static float             s_dg_coh_pk;      /* la del tono que se esta siguiendo */
static volatile float    s_dg_pico_si, s_dg_pico_no;  /* maximo medio de A1 */
static volatile float    s_dg_hz_si, s_dg_hz_no;      /* |desvio| medio del tono */
static float             s_dg_hz_pk;
/* La misma correlacion de A1, pero sobre la toma de MEDIO SIMBOLO que la
 * cadena ya produce para el ecualizador T/2. Si en las ráfagas que fallan
 * gana esta, lo que falla es el instante de muestreo y no la señal. */
const float32_t *hfdl_preamble_sync_ref_a(void);
static hfdl_framer_t     s_dg_half;
/* La misma A1, girada. La coherencia de un tono NO distingue el giro, asi
 * que un lazo enganchado a 45 o a 90 grados da tono perfecto y
 * correlacion de ruido: exactamente lo que se esta viendo. */
static hfdl_framer_t     s_dg_giro[7];
static float             s_dg_mejor_g; static uint8_t s_dg_cual_g;
static uint8_t           s_dg_half_listo;
static float             s_dg_mejor_h;
static volatile float    s_dg_picoh_si, s_dg_picoh_no;
static volatile uint32_t s_dg_gana_half;   /* ráfagas donde media gana a entera */
/* Y EL LISTADO, que es lo unico que contesta de verdad: los 46 tonos uno
 * a uno, con su coherencia, su desvio y lo que subio A1. Un promedio de
 * dos poblaciones distintas no dice cual es cual. */
#define HFDL_DG_LISTA 64U
static volatile uint16_t s_dg_n_lista;
static volatile float    s_dg_l_coh[HFDL_DG_LISTA];
static volatile float    s_dg_l_hz[HFDL_DG_LISTA];
static volatile float    s_dg_l_a1[HFDL_DG_LISTA];
static volatile uint8_t  s_dg_l_ok[HFDL_DG_LISTA];
static volatile uint16_t s_dg_l_dur[HFDL_DG_LISTA];  /* simbolos que duro el tono */
static volatile float    s_dg_l_giro[HFDL_DG_LISTA];
static volatile uint8_t  s_dg_l_qgiro[HFDL_DG_LISTA];
static uint16_t          s_dg_dur_vivo;
static int16_t           s_dg_idx_vivo = -1;
/* QUE CLASE DE TRAMA LLEGA. Las posiciones las manda el AVION, o sea que
 * viajan en BAJADA. Si en una grabacion entera no hay ni una bajada, "cero
 * coordenadas" no es una averia del decodificador: es que no ha pasado
 * ninguna por la antena. Hasta hoy no habia forma de distinguirlo. */
static volatile uint32_t s_dg_squit, s_dg_subida, s_dg_bajada;
static volatile uint32_t s_dg_acars, s_dg_acars_pos;   /* ACARS leidos / con coordenada */
static volatile uint32_t s_dg_lpdu[8];   /* por tipo, agrupado por kind */
#define HFDL_DG_DESDE 150U
#define HFDL_DG_HASTA 700U
#endif

/* Posiciones recogidas de un LPDU que no paso su CRC. Ver
 * posicion_de_confianza(): se cuentan aparte a proposito, para poder
 * mirar en antena si ese camino aporta o si mete basura. */
static volatile uint32_t s_pos_sin_crc;

/*
 * CUANTOS PARTES DE POSICION HAN LLEGADO DE VERDAD - 14/10/2026.
 *
 * "U7/1" dice siete cargas de usuario y una posicion sacada, pero no
 * distingue las dos averias posibles, que son muy distintas:
 *
 *   - no llega ningun parte de posicion (la frecuencia esta llena de
 *     RESUME y LOGON, que no llevan coordenadas). No hay nada que
 *     arreglar: hay que esperar a que pase un avion que las mande.
 *   - llegan y se pierden. Entonces si hay un fallo y hay que ir a por el.
 *
 * s_d1_vistos cuenta las cargas cuyo HFNPDU es del tipo 0xD1 -parte de
 * rendimiento, el que lleva latitud y longitud-, se saque posicion o no.
 * Con eso los dos casos se distinguen de un vistazo:
 *
 *   U7/1 D0  -> de siete cargas, ninguna era un parte de posicion
 *   U7/1 D5  -> cinco lo eran y solo salio una: ahi hay fallo
 */
static volatile uint32_t s_d1_vistos;

/* Posiciones que se sacaron y se perdieron al no poder atribuirlas a
 * ningun avion. Ver el return de apunta_trama(). */
static volatile uint32_t s_pos_huerfanas;

/*
 * TRAMAS BUENAS QUE NO PINTAN RENGLON - 14/10/2026.
 *
 * Contando su ultima pantalla: BPSK 19/31 y los contadores de los
 * renglones suman once. Ocho tramas BUENAS no pintan nada, y hasta ahora
 * no habia forma de verlo desde fuera: la chapa cuenta tramas y la lista
 * cuenta aviones, pero nadie contaba la diferencia.
 */
static volatile uint32_t s_sin_renglon;
#if HFDL_DIAG
/*
 * DE QUE ES CADA "/n" - 15/10/2026.
 *
 * `s_sin_renglon` cuenta tramas que PASARON EL CRC y aun asi no pintaron
 * fila en la tabla de aviones, y ese numero sale en la chapa como "/n".
 * Un numero asi, sin repartir, no se puede interpretar: 12 puede ser el
 * decodificador rompiendo cabeceras o puede ser la frecuencia llena de
 * trafico de tierra, que no lleva avion ninguno y es perfectamente
 * normal. Son dos diagnosticos opuestos con el mismo numero delante.
 *
 * Asi que se reparte en los TRES sitios que lo incrementan, y solo con
 * HFDL_DIAG: en la radio no cuesta ni un byte, y en el banco de aire
 * dice cual manda.
 */
static volatile uint32_t s_sr_cabecera;  /* el MPDU no se pudo leer */
static volatile uint32_t s_sr_sin_lpdu;  /* se leyo y no traia ningun LPDU */
static volatile uint32_t s_sr_sin_avion; /* bueno, pero no habla de un avion */
#endif

/* Ver hfdl_modo_poll(): los bytes de la ultima ráfaga rematada, buena o
 * mala. Diagnostico, para sim/hfdl_aire.c. */
static const uint8_t *s_ult_carga;
static uint32_t       s_ult_carga_n;
static const uint8_t *s_ultimo_crudo;
static uint32_t       s_ultimo_crudo_n;

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

    /*
     * Que quepan los estados en la cabecera se comprueba al compilar, no al
     * arrancar: si un struct del padre crece y se pasa, esto no compila en
     * vez de escribir encima de lo de payload_decode.
     *
     * *** Y SE MIDE CONTRA LOS AVIONES, NO CONTRA EL FINAL - 28/10/2026.
     *
     * Hasta hoy ponia "<= HFDL_RAM_CABECERA" y eso era CORRECTO mientras
     * los estados fueran lo ultimo de la cabecera. Al meter detras la
     * tabla de aviones y los renglones, la cabecera paso de 9.216 a 15.168
     * y este aserto se volvio inutil de golpe: seguia en verde aunque los
     * estados crecieran 6 kB y se comieran la tabla entera.
     *
     * Un aserto que deja de proteger lo que protegia, y encima en silencio
     * y en el mismo cambio que puso ahi lo que hay que proteger. Ahora se
     * mide contra HFDL_OFF_AVIONES, que es el primer byte que NO es suyo.
     *
     * El margen de verdad son 240 bytes: 4.608 de preambulo + 4.368 de
     * estados = 8.976, y los aviones empiezan en 9.216. (El comentario de
     * hfdl_ram.h decia 396 con un sizeof de 4.212; hoy son 4.368, medido
     * con una sonda de compilacion.) ***
     */
    _Static_assert(sizeof(hfdl_estado_t) + HFDL_RAM_PREAMBULO <= HFDL_OFF_AVIONES,
                   "los estados de HFDL se comen la tabla de aviones");

    memset(e, 0, sizeof *e);
    hfdl_demod_chain_init(&e->cadena);
    hfdl_preamble_sync_init(&e->preambulo);

    /* La tabla, sobre la region prestada que se acaba de coger. Antes del
     * 28/10/2026 esto apuntaba a un array estatico; ver linea_ram(). */
    hfdl_ac_init(&s_tabla, aviones_ram(),
                 (uint32_t)HFDL_MODO_AVIONES * sizeof(hfdl_ac_rec_t));
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
    s_n_a1 = 0U; s_n_a2 = 0U; s_n_m1 = 0U; s_no_cupo = 0U; s_no_cupo_7 = 0U;
    s_pisada = 0U; s_remate = 0U;
    /*
     * Y LA SEGUNDA MITAD DE LA MISMA HISTORIA - 14/10/2026.
     *
     * El arreglo de arriba dejo a medias exactamente el mismo fallo: el
     * embudo y las ráfagas se reiniciaban al entrar al modo, pero P, D,
     * /n y ! no, porque nacieron despues y nadie volvio a mirar esta
     * funcion. En la radio eso significa que al salir y volver se lee
     * "28 enganches, 115 pre-key": numeros de dos sesiones distintas
     * puestos uno al lado del otro, que es peor que no tener numero.
     *
     * Y en el banco de aire era peor todavia, porque ahi SI me creia la
     * cuenta: las seis grabaciones comparten contador de pre-key y
     * ninguna comparte el de enganches. La diferencia "46 pre-key para
     * 28 enganches" que llevo medio dia persiguiendo esta medida con dos
     * reglas distintas. Un banco que mezcla escalas miente igual que uno
     * que mide una copia.
     */
    s_rechazadas = 0U;
    s_pos_sin_crc = 0U;
    s_d1_vistos = 0U; s_pos_huerfanas = 0U; s_sin_renglon = 0U;
#if HFDL_DIAG
    s_sr_cabecera = 0U; s_sr_sin_lpdu = 0U; s_sr_sin_avion = 0U;
#endif
    s_pk_vistos = 0U; s_pk_racha = 0U; s_pk_dentro = 0U; s_pk_pendiente = 0U;
    s_pk_edad = 0xFFFFFFFFu; s_pk_sin_armar = 0U;
    s_pk_con_rafaga = 0U; s_pk_ya_contado = 0U;
    s_pk_edad_min = 0xFFFFFFFFu; s_pk_edad_max = 0U;
    s_pk_juntos = 0U; s_pk_desde_ultimo = 0xFFFFFFF0u;
#if HFDL_DIAG
    s_pk_mismo_tono = 0U; s_pk_gap_min = 0xFFFFFFFFu;
#endif
    s_pk_ri = 0.0f; s_pk_rq = 0.0f; s_pk_p = 0.0f; s_pk_n = 0U;
    s_pk_desvio_hz = 0.0f; s_pk_w = 0.0f;
#if HFDL_DIAG
    s_dg_mirando = 0U; s_dg_mejor = 0.0f; s_dg_pk_con_a1 = 0U;
    s_dg_banda[0] = 0U; s_dg_banda[1] = 0U; s_dg_banda[2] = 0U; s_dg_banda[3] = 0U;
    s_dg_ok[0] = 0U; s_dg_ok[1] = 0U; s_dg_ok[2] = 0U; s_dg_ok[3] = 0U;
    s_dg_coh_si = 0.0f; s_dg_coh_no = 0.0f; s_dg_nsi = 0U; s_dg_nno = 0U;
    s_dg_pico_si = 0.0f; s_dg_pico_no = 0.0f; s_dg_hz_si = 0.0f; s_dg_hz_no = 0.0f;
    s_dg_picoh_si = 0.0f; s_dg_picoh_no = 0.0f; s_dg_gana_half = 0U;
    s_dg_squit = 0U; s_dg_subida = 0U; s_dg_bajada = 0U;
    s_dg_acars = 0U; s_dg_acars_pos = 0U;
    { uint8_t zz; for (zz = 0U; zz < 8U; zz++) { s_dg_lpdu[zz] = 0U; } }
    s_dg_n_lista = 0U; s_dg_dur_vivo = 0U; s_dg_idx_vivo = -1;
#endif
    { uint8_t z; for (z = 0U; z < 4U; z++) { s_crc_ok[z] = 0U; s_crc_tot[z] = 0U; } }
    /*
     * Y LOS DOS DE LA U, QUE SE QUEDARON FUERA POR TERCERA VEZ -
     * 29/10/2026, revision a fondo.
     *
     * hfdl_modo_borra() si los reinicia; esta funcion no. O sea que salias
     * de HFDL, volvias a entrar en otra frecuencia, y en cuanto enganchaba
     * el primer A1 la barra decia "1>0>0 U37/4": treinta y siete LPDU de
     * datos y cuatro posiciones de la SESION ANTERIOR, pegados a un embudo
     * que empieza de cero. Dos numeros de escalas distintas en el mismo
     * renglon.
     *
     * Es el mismo olvido que este fichero ya arreglo con s_total y con los
     * contadores P/D//n/!. La regla es: lo que borra hfdl_modo_borra()
     * tiene que borrarlo tambien hfdl_modo_start().
     */
    s_ud = 0U;
    s_pos = 0U;
    crudo_vacia();
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
    /*
     * LA TABLA SE SUELTA ANTES QUE LA MEMORIA - 28/10/2026.
     *
     * s_tabla.rec apunta DENTRO de la region prestada desde que los
     * aviones viven ahi. En cuanto se devuelve, esos bytes vuelven a ser
     * pixeles de la cascada, y hfdl_modo_aviones() -que lee s_tabla.count
     * y la lee EL MAPA- seguiria diciendo que hay aviones y entregando
     * punteros a ellos.
     *
     * Dejarla vacia aqui es lo que convierte "la lista se borra al salir"
     * en una frase verdadera en vez de en un puntero colgando.
     */
    s_tabla.rec = (hfdl_ac_rec_t *)0;
    s_tabla.count = 0;
    s_tabla.cap = 0;
    s_nlineas = 0U;
    /* Y el anillo de cargas crudas, que vive FUERA del prestamo y por eso
     * sobrevivia a esta funcion: ver crudo_vacia(). */
    crudo_vacia();
    /* Y las referencias de preambulo, que SI viven dentro: ver
     * hfdl_preamble_sync_fin(). Mismo motivo que s_tabla.rec de arriba. */
    hfdl_preamble_sync_fin();
    hfdl_ram_suelta();
}

uint8_t hfdl_modo_activo(void) { return s_activo; }
uint32_t hfdl_modo_total(void) { return s_total; }
uint32_t hfdl_modo_buenas(void) { return s_buenas; }
uint32_t hfdl_modo_no_cupo(void) { return s_no_cupo; }
uint32_t hfdl_modo_no_cupo_7(void) { return s_no_cupo_7; }
uint32_t hfdl_modo_pisada(void)   { return s_pisada; }

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
        e->ener_t += i_sym * i_sym;
        e->n_t++;
    }
}

/* La misma correlacion, en cuadratura. Ver corr_tq. */
static void mide_polaridad_q(hfdl_estado_t *e, float q_sym)
{
    uint32_t t = hfdl_data_segment_get_t_idx(&e->segmento);
    if (t < HFDL_T_LEN) {
        e->corr_tq += q_sym * HFDL_T_SEQ[0][t];
        e->ener_t += q_sym * q_sym;
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
/*
 * ¿Se parecen a HFDL_T_SEQ los 135 simbolos de entrenamiento que acaban
 * de pasar? Ver el comentario grande de simbolo_de_segmento(). Devuelve
 * 1 si si -o si no hay con que decidir, que es lo prudente: el portero
 * solo esta para tirar lo que SEGURO no vale-.
 */
/*
 * EL UMBRAL, Y POR QUE ESTA TAN BAJO - 14/10/2026, por la noche.
 *
 * Empezo en 0,30 y ESO FUE UN ERROR MIO que le costo renglones. El
 * numero salio del banco sintetico, donde una ráfaga de verdad da de
 * 0,84 a 0,89, asi que 0,30 parecia holgadisimo. En su antena no lo era:
 * el video enseña el embudo pasando de 96>27>23 a 136>38>27 en cincuenta
 * segundos -cuatro enganches nuevos- y NI UNA trama nueva. El portero se
 * las estaba comiendo todas.
 *
 * Lo que dice el aire real, medido con sim/hfdl_aire.c sobre una
 * grabacion de 54 segundos, enganche a enganche:
 *
 *     ruido:        0,040  0,046  0,053  0,062  0,074  0,101
 *     ráfaga real:  0,444  0,536  0,621  0,679  0,724  0,779
 *                   0,811  0,833  0,874  0,911  0,916
 *
 * El suelo teorico del ruido es 1/sqrt(135) = 0,086 y lo peor que se vio
 * fue 0,101. Asi que el sitio honrado del umbral NO es la mitad del
 * hueco: es justo por encima del ruido.
 *
 * Y es que el coste no es simetrico, que es lo que no pense la primera
 * vez:
 *
 *   - tirar una ráfaga buena cuesta UN RENGLON QUE NO VUELVE. El avion
 *     no repite.
 *   - dejar pasar un enganche de ruido cuesta entre 1,2 y 2,8 segundos
 *     de sordera, y la radio lleva años contandolos en la chapa.
 *
 * Con esa cuenta, un portero dudoso tiene que dejar pasar. 0,15 es 1,7
 * veces el suelo teorico y 1,5 veces lo peor que se ha visto de ruido, y
 * sigue estando tres veces por debajo de la peor ráfaga real medida.
 *
 * Si algun dia esto hay que volver a tocarlo, el numero sale de
 * `make aire` con el detalle puesto, no de la cabeza de nadie.
 */
/*
 * ======================================================================
 * EL PORTERO DEL ENTRENAMIENTO VUELVE, Y AHORA CON LA MEDIDA DELANTE
 * ======================================================================
 *
 * Estaba en 0,15 y DESCONECTADO con un "if (0)" desde que se midio que a
 * 0,30 se comia ráfagas buenas. Aquella medida se hizo cuando A1
 * combinaba ocho bloques y enganchaba 395 veces en esta grabacion: con
 * ese ruido entrando, cualquier puerta parecia que tiraba cosas buenas.
 *
 * Hoy A1 correla los 127 simbolos de una vez y engancha 28. Rehecha la
 * medida sobre las siete grabaciones, el barrido entero:
 *
 *     umbral   total buenas    radio_oscar (buenas / renglones)
 *      0,00        26              10 / 8      <- apagado, como estaba
 *      0,15        26              10 / 8
 *      0,28        26              10 / 8
 *      0,30        27              11 / 9
 *      0,32        27              11 / 9      <- aqui
 *      0,35        27              11 / 9
 *      0,40        26              10 / 8
 *      0,65        24               10 / 8     <- y Hf-acars cae de 9 a 7
 *
 * Una MESETA de 0,30 a 0,35 con 26 a los dos lados, no un pico: se coge
 * el centro. Lo que gana son tres segmentos de ruido que ya no se abren,
 * y cada uno dejaba la cadena sorda entre 1,8 y 4,2 segundos.
 *
 * Y lo que esto enseña de verdad no es el numero: es que una decision
 * tomada con una medida vieja hay que volver a medirla cuando cambia lo
 * que habia debajo. El "if (0)" llevaba razon con el A1 de entonces.
 */
#ifndef HFDL_COH_MINIMA
#define HFDL_COH_MINIMA 0.32f
#endif

static volatile float s_coh_ultima;   /* para los bancos y la chapa */

static uint8_t s_porton_vale = 1U;
static uint8_t portero_entrenamiento(hfdl_estado_t *e)
{
    float num, den;
    uint8_t vale;

    if (e->n_t < 60U || e->ener_t <= 0.0f) {
        return 1U; /* sin suficientes simbolos no se juzga */
    }
    num = sqrtf(e->corr_t * e->corr_t + e->corr_tq * e->corr_tq);
    den = sqrtf((float)e->n_t * e->ener_t);
    if (den <= 0.0f) { return 1U; }
    s_coh_ultima = num / den;
    vale = (uint8_t)((s_coh_ultima >= HFDL_COH_MINIMA) ? 1U : 0U);
    /* Se cuenta lo que SE HABRIA tirado, aunque ya no se tire: ese numero
     * es el que un dia permitira decidir si merece la pena encenderlo. */
    if (!vale) { s_rechazadas++; }
    return vale;
}

float hfdl_modo_coherencia(void) { return s_coh_ultima; }

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
        /*
         * SOBRE EL SIMBOLO CRUDO, NO SOBRE LA SALIDA DEL ECUALIZADOR.
         * 14/10/2026, por la tarde, despues de medirlo.
         *
         * *** Medido en la radio con 192 tramas: BPSK 58/147, 4PSK 0/29,
         * 8PSK 0/16. Cero de cuarenta y cinco. ***
         *
         * Aqui ponia `mide_polaridad(e, yi)`, o sea medir la correlacion
         * contra HFDL_T_SEQ sobre la SALIDA DEL LMS. Y el LMS esta, en
         * esa misma linea de abajo, empujando esa salida HACIA
         * HFDL_T_SEQ. O sea: se medi­a una señal contra la referencia a la
         * que alguien la esta forzando a parecerse.
         *
         * Al cabo de unas decenas de simbolos yi ~ T_SEQ y yq ~ 0 pase lo
         * que pase en la antena, asi que corr_t tiende a +|T|^2 y corr_tq
         * a cero INDEPENDIENTEMENTE DEL GIRO REAL. Medido con una sonda
         * contra este mismo codigo: con la entrada girada 90 grados, el
         * angulo de la correlacion sale 2,2 grados; con la señal invertida
         * 180, corr_t NO se vuelve negativo. El giro absoluto que se
         * entrego por la mañana era inerte: devolvia cero siempre.
         *
         * El simbolo crudo de Costas -si, sq- no lo toca nadie, asi que
         * su correlacion contra T_SEQ si lleva el giro de verdad.
         *
         * Y esto ya estaba escrito en el banco sim/hfdl_polaridad.c desde
         * hace semanas: "el ecualizador NO conserva la ambiguedad: LA
         * RESUELVE". En vez de arreglar la medida, se cambio el banco
         * para que exigiera el resultado degenerado.
         */
        mide_polaridad(e, si);
        mide_polaridad_q(e, sq);
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
    /*
     * ======================================================================
     * EL PORTERO: ¿ESTO ES UNA RAFAGA O ES RUIDO? - 14/10/2026
     * ======================================================================
     *
     * LO QUE SE MIDIO, en deepsdr-ui/sim/hfdl_m1.c, el 14/10/2026:
     *
     *  a) Cuando el enganche cae en el simbolo que le toca, la rotacion M1
     *     elegida es la BUENA 130 de 130, 108 de 108 y 106 de 106 veces, a
     *     tres niveles de ruido. Ni un fallo.
     *  b) TODAS las elecciones equivocadas -140 de 946- vienen de
     *     enganches corridos, y corridos mucho: de -300 a -287 simbolos.
     *     O sea enganches ocurridos ANTES de que empezara la señal, en el
     *     ruido de delante.
     *  c) Con ruido y NADA MAS que ruido, la maquina A1->A2->M1 engancha
     *     entre 31 y 47 veces por minuto.
     *
     * Y eso ultimo no cuesta una ráfaga: cuesta TODAS las que lleguen
     * mientras dura. Un enganche falso abre un segmento de 72 o 168
     * ventanas -de 1,2 a 2,8 segundos a 1800 baudios- durante las cuales
     * este receptor esta SORDO. La ráfaga de verdad llega, el preambulo
     * esta ahi, y no lo mira nadie porque la cadena esta ocupada
     * decodificando ruido. Eso es exactamente oir tramas y no ver subir
     * los renglones.
     *
     * EL PORTERO. Al preambulo le siguen nueve ventanas de entrenamiento:
     * 135 simbolos que son HFDL_T_SEQ y que se saben de memoria. Si lo que
     * llega no se parece a HFDL_T_SEQ, no es una ráfaga, y se puede saber
     * ANTES de gastar los dos segundos y medio.
     *
     * La medida ya estaba hecha -corr_t y corr_tq, sobre el simbolo CRUDO
     * de Costas, que es lo unico que no esta tocado por el LMS- y lo unico
     * que faltaba era normalizarla por la energia de esos mismos simbolos
     * para que saliera un numero entre 0 y 1 comparable con un umbral
     * fijo, pase lo que pase con el AGC.
     *
     *     coherencia = |corr| / sqrt(n * energia)
     *
     * Vale 1 si los 135 simbolos son HFDL_T_SEQ exacto, y 1/sqrt(135) =
     * 0,086 si son ruido. El umbral se eligio MIDIENDO la coherencia de
     * ráfagas de verdad en sim/hfdl_extremo.c, no a ojo.
     *
     * NO TOCA EL CAMINO NORMAL: una ráfaga de verdad pasa el portero
     * siempre -su entrenamiento ES HFDL_T_SEQ- asi que esto no puede
     * quitar ni una trama buena. Lo unico que quita es el tiempo muerto.
     */
    /*
     * EL PORTERO MIDE PERO YA NO TIRA - 14/10/2026, de madrugada.
     *
     * Y su embudo lo decia con numeros: 49>22>18 enganches y 9 tramas.
     * NUEVE DE DIECIOCHO se las comia esto.
     *
     * El portero se puso hoy mismo para no gastar 1,2 segundos de sordera
     * detras de un enganche de ruido. La idea sigue siendo buena, pero
     * NUNCA SE DEMOSTRO QUE ESA SORDERA LE COSTARA NADA: antes de que
     * existiera, la radio llevaba 187 enganches a 178 tramas en 45
     * minutos. O sea que el tiempo muerto no era el cuello de botella, y
     * yo cambie un problema que no estaba medido por uno que si: se
     * perdian renglones.
     *
     * Asi que se queda la MEDIDA y se quita la DECISION. Sigue contando
     * lo que habria tirado -hfdl_modo_rechazadas()-, y cuando ese numero
     * demuestre en antena que las que tira son ruido y solo ruido, se
     * vuelve a encender con un umbral sacado de ahi. Mientras tanto, una
     * trama dudosa pasa.
     *
     * RESTAURADO MEDIA HORA DESPUES, porque el banco dijo que no.
     * Apagarlo sobre la grabacion real de 54 segundos da esto:
     *
     *     con portero:  18 enganches, 12 tramas,  6 buenas, 3 renglones
     *     sin portero:  11 enganches, 11 tramas,  2 buenas, 1 renglon
     *
     * O sea que lo que tira ES ruido, y dejarlo pasar no solo gasta
     * tiempo: se come ráfagas BUENAS que llegan mientras la cadena esta
     * ocupada con la basura. Apagarlo costaba cuatro buenas de seis.
     *
     * Queda encendido. Lo que se aprende de esto no es "el portero es
     * bueno" sino que el razonamiento de arriba -"el que duda deja
     * pasar"- estaba incompleto: dejar pasar ruido TAMBIEN tira ráfagas
     * buenas, solo que las de despues, y por eso no se ven. Sin el banco
     * de aire real esto no se podia saber, y por eso se mide antes de
     * decidir.
     */
    if (!e->polaridad_fijada && antes == HFDL_DSEG_EQ_TRAIN &&
        (ahora == HFDL_DSEG_DATA_1 || ahora == HFDL_DSEG_DATA_2)) {
        /* Ver "LOS DOS PORTEROS SE RETIRAN": mide y cuenta, no decide. */
        s_porton_vale = portero_entrenamiento(e);
    }
    if (!s_porton_vale) {
        s_porton_vale = 1U;
        /* No era una ráfaga. A buscar otra vez AHORA, no dentro de dos
         * segundos y medio. */
        hfdl_data_segment_init(&e->segmento, 1U, 0);  /* lo deja en DONE, sin datos */
        hfdl_preamble_sync_reset(&e->preambulo);
        e->cadena.bits_por_simbolo = 1U;
        e->en_segmento = 0U;
        e->decodificando = 0U;
        e->polaridad_fijada = 0U;
        e->corr_t = 0.0f;
        e->corr_tq = 0.0f;
        e->ener_t = 0.0f;
        e->n_t = 0U;
        e->giro_cos = 1.0f; e->giro_sen = 0.0f;
        return;
    }

    if (!e->polaridad_fijada && antes == HFDL_DSEG_EQ_TRAIN &&
        (ahora == HFDL_DSEG_DATA_1 || ahora == HFDL_DSEG_DATA_2)) {
        /* Este simbolo todavia es de entrenamiento: se abre el segmento
         * pero NO se mete. El primero de datos es el siguiente. */
        e->polaridad = (uint8_t)((e->corr_t < 0.0f) ? 1U : 0U);
        /*
         * Y EL GIRO ABSOLUTO, PARA 4-PSK Y 8-PSK - 14/10/2026.
         *
         * El signo de arriba resuelve +-180, que en BPSK es toda la
         * ambiguedad. En 4-PSK sobra un giro de +-90 y en 8-PSK de +-45,
         * y eso un signo no lo dice. El ANGULO de la correlacion compleja
         * contra la secuencia de entrenamiento si: el multiplo de 2pi/M
         * mas cercano es lo que hay que deshacer.
         *
         * BPSK NO PASA POR AQUI a proposito. Rotar por -0 o -pi es
         * multiplicar por +-1, o sea exactamente lo que ya hace
         * `polaridad`, pero cos(pi) y sen(pi) en coma flotante dejan una
         * miga en la Q. Ahi hay un 30 % que funciona y no se toca por una
         * elegancia.
         */
        hfdl_costas_giro_absoluto(e->corr_t, e->corr_tq,
                                  e->cadena.bits_por_simbolo,
                                  &e->giro_cos, &e->giro_sen);
        e->polaridad_fijada = 1U;
        /* Y AHORA SI la aridad de la trama: lo que viene son datos. Ver
         * el comentario del LOCKED. */
        e->cadena.bits_por_simbolo = e->aridad_trama;
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
        {
            uint32_t m1i = (uint32_t)hfdl_preamble_sync_get_m1_index(&e->preambulo);

            if (s_remate || s_finish_pendiente) {
                /* LA MEMORIA ES DEL BUCLE PRINCIPAL AHORA MISMO. Ver el
                 * comentario de s_remate: abrir aqui se lleva por delante
                 * la trama que se esta rematando Y esta tambien. Mejor
                 * perder una, limpia y contada. */
                e->decodificando = 0U;
                s_no_cupo++;
                s_pisada++;
            } else {
                e->decodificando = (uint8_t)hfdl_payload_decode_begin_segment(m1i);
                if (!e->decodificando) {
                    s_no_cupo++;
                    /* Ver s_no_cupo_7: el 7 es el unico que no se arregla
                     * ensanchando. El 6 si, y por eso se cuentan aparte. */
                    if (m1i == 7U) { s_no_cupo_7++; }
                }
            }
        }
    }

    if (e->decodificando &&
        (antes == HFDL_DSEG_DATA_1 || antes == HFDL_DSEG_DATA_2)) {
        /*
         * NI POLARIDAD NI GIRO: EL ECUALIZADOR YA LO HA HECHO.
         * 14/10/2026, por la tarde, MEDIDO de extremo a extremo.
         *
         * Hoy se entregaron dos correcciones aqui -el bit de polaridad y
         * un giro de constelacion- y las DOS sobraban. El banco nuevo
         * sim/hfdl_extremo.c, que monta una rafaga entera y mira si sale
         * el CRC, lo dice sin ambiguedad:
         *
         *   aplicando la polaridad   BPSK invertida 180 grados -> 0 buenas
         *   sin aplicarla            BPSK invertida 180 grados -> 1 buena
         *   sin aplicar el giro      4-PSK y 8-PSK girados     -> 1 buena
         *
         * El motivo es el que ya estaba escrito en septiembre y yo no me
         * crei: el LMS entrena su salida HACIA HFDL_T_SEQ[0], asi que la
         * deja en el marco bueno venga como venga la entrada. Corregir
         * encima es corregir DOS VECES, y eso es lo que tumbo BPSK del
         * 39 % al 22 % entre la V4.82 y la V4.85.
         *
         * LA MEDIDA SE QUEDA, la aplicacion no. Saber que el giro de
         * entrada era de 180 grados es util -lo vigila
         * sim/hfdl_polaridad.c, que por fin vuelve a distinguir 0 de 1-,
         * pero el que lo deshace es el ecualizador.
         */
        if (s_espia_i != (float *)0 && s_espia_n < s_espia_cap) {
            s_espia_i[s_espia_n] = yi;
            s_espia_q[s_espia_n] = yq;
            s_espia_n++;
        }
        hfdl_payload_decode_feed_symbol(yi, yq, 0U);
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
        /*
         * Y LA CADENA VUELVE A BPSK. El preambulo de la rafaga siguiente
         * SIEMPRE es BPSK, asi que dejar puesto el 8-PSK de la anterior
         * seria estropear justo la parte que si funciona. Es el mismo
         * cuidado que el de poner s_anillos_n a cero al coser una
         * ventana: un estado que sobrevive a lo que lo creo es una mina.
         */
        e->cadena.bits_por_simbolo = 1U;

        /* La rafaga se acabo: a buscar preambulo otra vez. Lo dice su propia
         * cabecera - este modulo no toca el sincronismo, es cosa de quien
         * llama, o sea de aqui. */
        hfdl_preamble_sync_reset(&e->preambulo);
        e->en_segmento = 0U;
        e->polaridad_fijada = 0U;
        e->corr_t = 0.0f;
        e->corr_tq = 0.0f;
        e->ener_t = 0.0f;
        e->n_t = 0U;
        e->giro_cos = 1.0f; e->giro_sen = 0.0f;
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
/*
 * ======================================================================
 * UNA POSICION SIN CRC, PERO NO SIN PRUEBAS - 14/10/2026
 * ======================================================================
 *
 * LO QUE SE MIDIO. Se cogio una trama real de las que no daban
 * coordenadas y se siguio byte a byte (sim/hfdl_aire.c saca ahora los
 * bytes de cada ráfaga, pase el CRC o no):
 *
 *   - la cabecera MPDU pasaba su CRC,
 *   - la carga estaba PERFECTA: llamando al parser de posicion a mano
 *     contesto "vuelo AC0090, 14,2222 N, 60,6165 W, 08:02:08, estacion 7",
 *   - y el CRC DEL LPDU fallaba, por un bit en la cola, a treinta bytes
 *     de los campos de posicion.
 *
 * Con el CRC malo, hfdl_pdu.c marca el LPDU como invalido y tira la
 * carga entera. O sea que la posicion existia, era buena, y se perdia.
 *
 * POR QUE ESTO NO ES "RELAJAR EL CRC". Un CRC de 16 bits se equivoca una
 * vez de cada 65.536. Si se quita, hay que poner otras pruebas que sumen
 * una confianza parecida, y tienen que ser INDEPENDIENTES de los bytes
 * de la posicion -si no, no prueban nada-. La norma regala dos:
 *
 *   1. El parte de posicion repite dentro LA ESTACION DE TIERRA (octeto
 *      18 de la Figura 9). Ya sabemos cual es por la cabecera del MPDU,
 *      que SI ha pasado su CRC. Que cuadren es 1 entre 128.
 *   2. El identificativo de vuelo son seis caracteres con el bit 8 a
 *      cero (octetos 3 a 8 de la misma figura). Seis caracteres
 *      imprimibles de verdad son 1 entre 4.000 largos.
 *
 * Mas lo que el propio parser ya exige: que quepan 46 bytes detras, que
 * la latitud este en +-90, la longitud en +-180 y la hora sea una hora.
 *
 * Todo junto queda por debajo de una entre medio millon, o sea MEJOR que
 * el CRC que se ha caido. Y no es un calculo de salon: las dos pruebas
 * nuevas miran bytes que estan a quince y a treinta posiciones de los de
 * la posicion, asi que un error que corrompa la posicion no los deja
 * cuadrar.
 *
 * LO QUE NO SE HACE: pintar la trama. Esto NO devuelve el renglon a la
 * lista ni apunta el suceso. Devuelve SOLO la posicion, que es lo unico
 * que se ha comprobado. El resto de esa trama sigue siendo lo que el
 * Viterbi creyo ver, y sigue sin merecer un sitio en pantalla.
 */
static uint8_t posicion_de_confianza(const uint8_t *carga, uint32_t n,
                                     uint8_t gs_esperada,
                                     hfdl_hfnpdu_perf_data_t *pd)
{
    uint8_t c;

    if (carga == (const uint8_t *)0 || n == 0U) { return 0U; }
    if (!hfdl_hfnpdu_parse_performance_data(carga, n, pd)) { return 0U; }

    /* 1. La estacion que el parte dice, contra la que dice la cabecera. */
    if (pd->gs_id != gs_esperada) { return 0U; }

    /* 2. Seis caracteres de vuelo de verdad. La norma pone el bit 8 a
     *    cero en los seis, asi que un byte >= 0x80 ya delata corrupcion. */
    for (c = 0U; c < 6U; c++) {
        uint8_t v = (uint8_t)pd->flight_id[c];
        if (v < 0x20U || v > 0x5FU) { return 0U; }
    }

    return 1U;
}

static void apunta_trama(const uint8_t *datos, uint32_t largo)
{
    hfdl_mpdu_header_t hdr;
    /*
     * QUINCE Y NO CUATRO - 29/10/2026, revision a fondo.
     *
     * El campo lpdu_cnt de la cabecera son CUATRO BITS (ver hfdl_pdu.h),
     * o sea hasta quince LPDU en una trama. Aqui habia sitio para cuatro
     * y hfdl_mpdu_extract_lpdus() recibia ese 4 como capacidad: a partir
     * del quinto no se apuntaba ni la posicion, ni el tipo de suceso, ni
     * el volcado en crudo. No habia desborde -extract_lpdu_list() respeta
     * la capacidad que le dan- pero se perdia en silencio, y la fila del
     * avion decia "lo ultimo que hizo" mirando solo el principio de la
     * trama.
     *
     * Quince es el maximo EXACTO que permite el formato, no un numero
     * redondeado: el mismo razonamiento que hay escrito encima de
     * HFDL_MAX_LPDUS_TOTAL. Cuesta 352 bytes mas de pila en el peor caso
     * (32 bytes por entrada en el ARM), y esta funcion la llama
     * hfdl_modo_poll() desde el BUCLE PRINCIPAL, no desde la
     * interrupcion. El margen de pila se mide en el banco.
     */
    hfdl_lpdu_result_t lpdu[15];
    hfdl_ac_event_t ev;
    hfdl_hfnpdu_perf_data_t pd;
    rtc_hw_datetime_t h;
    uint32_t nl = 0U, k;
    uint8_t  pide_modelo = 0U;
    /* Subida con mas de un avion: no se sabe de quien es cada LPDU, asi que
     * no se le cuelga nada a nadie. Ver la rama de la cabecera de subida. */
    uint8_t  varios = 0U;
    /* Cuantos LPDU de esta trama traen identificacion PROPIA (direccion
     * OACI). Con uno solo no hay ambiguedad posible; con dos o mas, el
     * unico ev de la trama los mezcla. Ver la salida de abajo. */
    uint8_t  n_ident = 0U;

    /*
     * EL SQUITTER VA A SU PROPIO RENGLON - 14/10/2026.
     *
     * Y es que lo mas frecuente que hay en la frecuencia -la ranura 0 de
     * cada trama de 32 segundos- no llegaba hasta aqui: se tiraba antes,
     * en el CRC, porque el SPDU lleva el FCS en otro sitio que el MPDU y
     * aqui no habia caso para el (ver hfdl_pdu.h). Ahora pasa el CRC, y
     * lo que falta es que SE VEA: un renglon por estacion de tierra cuya
     * cuenta sube cada treinta y dos segundos.
     *
     * Va antes de parsear MPDU porque un squitter NO es un MPDU: si se
     * dejara caer ahi abajo, hfdl_mpdu_parse_header() se inventaria una
     * cabecera y saldria cualquier cosa.
     */
    {
        hfdl_spdu_t sq;
        if (hfdl_payload_decode_es_spdu(&sq)) {
            hfdl_ac_event_t evs;
            rtc_hw_datetime_t hs;
            memset(&evs, 0, sizeof evs);
            evs.ac_id = HFDL_AC_ID_NONE;
            evs.gs_id = sq.gs_id;
            evs.flags = HFDL_AC_EV_GS | HFDL_AC_EV_UPLINK;
            rtc_hw_get(&hs);
            evs.hh = hs.hour; evs.mm = hs.minute; evs.ss = hs.second;
            { uint8_t sin_modelo = 0U; hfdl_ac_update(&s_tabla, &evs, &sin_modelo); }
            /*
             * Y LA CABECERA DICE QUE ESTACION ES - 29/10/2026.
             *
             * Esta rama sale con return antes de la linea que alimenta a
             * hfdl_modo_estacion(), que es lo que pinta "> SHANNON" arriba
             * del panel. Y el squitter es EL mensaje en el que la estacion
             * dice quien es -es literalmente sq.gs_id, ya validado con un
             * CRC de 16 bits- y lo unico que se oye en una frecuencia
             * tranquila.
             *
             * O sea que en una frecuencia donde llegaban squitters pero
             * todavia ninguna trama, la lista ya enseñaba renglones de esa
             * estacion y la cabecera seguia con el texto de relleno. Dos
             * partes de la misma pantalla diciendo cosas distintas.
             */
            s_gs_ultima = sq.gs_id;
#if HFDL_DIAG
            s_dg_squit++;
#endif
            return;
        }
    }

    if (!hfdl_mpdu_parse_header(datos, largo, &hdr)) {
#if HFDL_DIAG
        s_sr_cabecera++;
#endif
        s_sin_renglon++; return;
    }
    nl = hfdl_mpdu_extract_lpdus(datos, largo, &hdr, lpdu,
                                 sizeof lpdu / sizeof lpdu[0]);
    if (nl == 0U) {
#if HFDL_DIAG
        s_sr_sin_lpdu++;
#endif
        s_sin_renglon++; return;
    }

    memset(&ev, 0, sizeof ev);
    ev.ac_id = HFDL_AC_ID_NONE;
    rtc_hw_get(&h);
    ev.hh = h.hour; ev.mm = h.minute; ev.ss = h.second;
    ev.lpdu_type = lpdu[0].type_octet;

#if HFDL_DIAG
    if (hdr.direction == HFDL_PDU_DIR_DOWNLINK) { s_dg_bajada++; } else { s_dg_subida++; }
    { uint32_t kk; for (kk = 0U; kk < nl; kk++) {
        uint32_t ki = (uint32_t)lpdu[kk].kind; if (ki < 8U) { s_dg_lpdu[ki]++; }
        } }
#endif
    if (hdr.direction == HFDL_PDU_DIR_DOWNLINK) {
        ev.ac_id = hdr.src_id;      /* habla el avion */
        ev.gs_id = hdr.dst_id;
    } else {
        ev.flags |= HFDL_AC_EV_UPLINK;
        ev.gs_id = hdr.src_gs_id;
        /*
         * ==================================================================
         * EN SUBIDA, CON VARIOS AVIONES NO SE ATRIBUYE NADA - 29/10/2026,
         * revision a fondo de HFDL.
         * ==================================================================
         *
         * Aqui ponia "si hay varios se apunta el primero, que es de quien
         * habla el primer LPDU", y eso era verdad del PRIMER LPDU y de
         * ninguno mas. Un MPDU de subida admite hasta OCHO aviones y la
         * estacion de tierra junta varios destinatarios en la misma ranura
         * como cosa normal.
         *
         * Y la informacion se pierde una capa mas abajo:
         * hfdl_lpdu_result_t no lleva ningun campo que diga a que avion
         * pertenece el LPDU, y hfdl_mpdu_extract_lpdus() APLANA los
         * segmentos por avion en una sola lista. Desde aqui no hay forma de
         * saber cual es de quien.
         *
         * El cuadro, con dos aviones A y B en la misma trama: ev.ac_id
         * queda en A; el LPDU de datos de usuario de B archiva su volcado
         * bajo A y rellena ev.lat/lon/vuelo con los de B; y al salir del
         * bucle hay UNA sola hfdl_ac_update() que lo funde todo en la ficha
         * de A. Resultado: la fila de A sale con el distintivo, la posicion
         * y el texto de B, y el mapa pinta a A donde esta B. No es un
         * renglon de mas: es un dato FALSO que no se distingue de uno
         * bueno.
         *
         * Hasta que haya un suceso por avion -que pide cambiar hfdl_pdu.c
         * para que cada LPDU diga de quien es-, con varios aviones NO se
         * atribuye a nadie: ev.ac_id se queda en HFDL_AC_ID_NONE y la
         * guarda de trama anonima de mas abajo tira el renglon. Lo que SI
         * sigue entrando es lo que viene identificado dentro de su propio
         * LPDU -los LOGON_* traen su ac_id y su OACI-, que es el unico dato
         * de esa trama del que se sabe el dueño.
         *
         * Se pierde un renglon de "a este avion le han mandado algo". Se
         * gana no inventarse donde esta.
         */
        if (hdr.aircraft_cnt == 1U) {
            ev.ac_id = hdr.aircraft[0].dst_id;
        } else if (hdr.aircraft_cnt > 1U) {
            varios = 1U;
        }
    }

    for (k = 0U; k < nl; k++) {
        /*
         * Y SI EL LPDU NO PASO SU CRC, LA POSICION TODAVIA PUEDE VALER.
         * Ver posicion_de_confianza(): no se pinta la trama, solo se
         * recoge la posicion, y solo si pasa pruebas independientes.
         */
        if (!lpdu[k].crc_ok && lpdu[k].datos_crudos != (const uint8_t *)0) {
            hfdl_hfnpdu_perf_data_t pdx;
            if (posicion_de_confianza(lpdu[k].datos_crudos,
                                      lpdu[k].datos_crudos_len,
                                      ev.gs_id, &pdx)) {
                uint8_t c2;
                if (!varios) { s_pos++; }
                s_pos_sin_crc++;
                ev.lat_e4 = (int32_t)(pdx.location.lat * 10000.0);
                ev.lon_e4 = (int32_t)(pdx.location.lon * 10000.0);
                ev.flags |= (uint8_t)(HFDL_AC_EV_POS | HFDL_AC_EV_PERF);
                for (c2 = 0U; c2 < 6U; c2++) { ev.flight[c2] = pdx.flight_id[c2]; }
                ev.flight[6] = '\0';
                if (ev.flight[0] > 32) { ev.flags |= HFDL_AC_EV_FLIGHT; }
            }
        }
        switch (lpdu[k].kind) {
        case HFDL_LPDU_KIND_LOGON_CONFIRM:
            /* La estacion acaba de asignarle un identificador a esa
             * direccion OACI: es el apunte que ata las dos cosas, y el que
             * hace que los mensajes sueltos de despues dejen de ser
             * anonimos. */
            ev.icao = lpdu[k].icao_address;
            ev.ac_id = lpdu[k].ac_id;
            ev.flags |= (uint8_t)(HFDL_AC_EV_ICAO | HFDL_AC_EV_ASSIGN); n_ident++;
            break;
        case HFDL_LPDU_KIND_LOGON_REQUEST:
            ev.icao = lpdu[k].icao_address;
            ev.flags |= HFDL_AC_EV_ICAO; n_ident++;
            /*
             * Y LA POSICION QUE VIENE DENTRO DEL LOGON. Ver hfdl_pdu.c,
             * "Y DETRAS DE LA DIRECCION OACI VIENE UN HFNPDU": el logon
             * de verdad trae 55 bytes y dentro va el mismo HFNPDU que los
             * datos de rendimiento, con tipo 0xD5. Es la trama mas
             * frecuente que manda un avion y se estaba tirando entera.
             */
            if (lpdu[k].user_data != (const uint8_t *)0 &&
                hfdl_hfnpdu_parse_performance_data(lpdu[k].user_data,
                                                   lpdu[k].user_data_len, &pd)) {
                uint8_t cl;
                if (!varios) { s_pos++; }
                s_d1_vistos++;
                ev.lat_e4 = (int32_t)(pd.location.lat * 10000.0);
                ev.lon_e4 = (int32_t)(pd.location.lon * 10000.0);
                ev.flags |= (uint8_t)(HFDL_AC_EV_POS | HFDL_AC_EV_PERF);
                for (cl = 0U; cl < 6U; cl++) { ev.flight[cl] = pd.flight_id[cl]; }
                ev.flight[6] = '\0';
                if (ev.flight[0] > 32) { ev.flags |= HFDL_AC_EV_FLIGHT; }
            }
            break;
        case HFDL_LPDU_KIND_LOGOFF_OR_DENIED:
            ev.icao = lpdu[k].icao_address;
            ev.flags |= HFDL_AC_EV_ICAO; n_ident++;
            break;
        case HFDL_LPDU_KIND_USER_DATA:
            s_ud++;        /* ver hfdl_modo_datos() */
            /*
             * LOS BYTES CRUDOS, ANTES DE INTENTAR ENTENDERLOS. 14/10/2026.
             *
             * Se guardan SIEMPRE, salga posicion o no, que es justo el
             * caso que hay que mirar: lo que interesa son las tramas que
             * el decodificador NO entiende. Ver ud[] en hfdl_aircraft.h.
             */
            {
                uint32_t ct = 0U;
                if (lpdu[k].user_data != (const uint8_t *)0 &&
                    hfdl_hfnpdu_tipo(lpdu[k].user_data,
                                     lpdu[k].user_data_len, &ct) == 0xD1U) {
                    s_d1_vistos++;
                }
            }
            /* Con varios aviones en la trama no se sabe de quien es esta
             * carga: archivarla bajo el primero es la mitad del mismo
             * fallo. Ver la variable "varios". */
            if (!varios)
            crudo_pon(((ev.flags & HFDL_AC_EV_ICAO) != 0U) ? ev.icao : 0UL,
                      ev.ac_id, ev.gs_id,
                      lpdu[k].user_data, lpdu[k].user_data_len);
            /* DIAGNOSTICO: la ultima carga de usuario, entera. Dos
             * punteros; es lo unico que permite ver POR QUE una posicion
             * que esta en los bytes no sale. Ver hfdl_modo_ultima_carga(). */
            s_ult_carga = lpdu[k].user_data;
            s_ult_carga_n = lpdu[k].user_data_len;
            if (lpdu[k].user_data != (const uint8_t *)0 &&
                hfdl_hfnpdu_parse_performance_data(lpdu[k].user_data,
                                                   lpdu[k].user_data_len, &pd)) {
                uint8_t c;

                if (!varios) { s_pos++; }

                ev.lat_e4 = (int32_t)(pd.location.lat * 10000.0);
                ev.lon_e4 = (int32_t)(pd.location.lon * 10000.0);
                ev.flags |= (uint8_t)(HFDL_AC_EV_POS | HFDL_AC_EV_PERF);
                for (c = 0U; c < 6U; c++) { ev.flight[c] = pd.flight_id[c]; }
                ev.flight[6] = '\0';
                if (ev.flight[0] > 32) { ev.flags |= HFDL_AC_EV_FLIGHT; }
            } else if (lpdu[k].user_data != (const uint8_t *)0) {
                /*
                 * Y SI NO ES DATOS DE RENDIMIENTO, PUEDE SER ACARS.
                 * 14/10/2026.
                 *
                 * El volcado en crudo de la pantalla lo contesto: las
                 * tramas que no daban posicion empiezan por FF FF, o sea
                 * sobre y tipo 0xFF, que es ENVELOPED_DATA: un ACARS. Este
                 * firmware solo entendia el 0xD1 y las tiraba todas. El
                 * otro las saca porque lleva un decodificador de ACARS.
                 *
                 * No todas traen posicion -las de etiqueta "Q0" son
                 * enlace, y las dos tramas reales que hay son de esas-,
                 * asi que esto AÑADE y no quita: lo que ya salia, sigue
                 * saliendo por la rama de arriba.
                 */
                uint32_t cont = 0U;
                uint8_t tipo = hfdl_hfnpdu_tipo(lpdu[k].user_data,
                                                lpdu[k].user_data_len, &cont);
                if (tipo == 0xFFU && lpdu[k].user_data_len > cont) {
                    hfdl_acars_t ac;
                    int32_t la = 0, lo = 0;
                    uint8_t leido = hfdl_acars_parse(&lpdu[k].user_data[cont],
                                         lpdu[k].user_data_len - cont, &ac);
#if HFDL_DIAG
                    if (leido) {
                        s_dg_acars++;
                        if (hfdl_acars_posicion(&ac, &la, &lo)) { s_dg_acars_pos++; }
                        la = 0; lo = 0;
                    }
#endif
                    if (leido && hfdl_acars_posicion(&ac, &la, &lo)) {
                        if (!varios) { s_pos++; }
                        ev.lat_e4 = la;
                        ev.lon_e4 = lo;
                        ev.flags |= HFDL_AC_EV_POS;
                    }
                }
            }
            break;
        case HFDL_LPDU_KIND_INVALID:
            /*
             * EL CRC DEL LPDU HA FALLADO, PERO EL DE LA CABECERA NO.
             * 14/10/2026.
             *
             * Hasta ahora esto caia en el `default` y no hacia nada: la
             * trama entera se perdia sin dejar renglon. Y eso NO esta
             * justificado, porque las dos cosas no tienen la misma
             * confianza:
             *
             *   - la CABECERA del MPDU tiene su propio CRC de 16 bits y
             *     LO HA PASADO. De ella salen el identificador del avion,
             *     el de la estacion y el sentido. Esos campos son tan
             *     fiables como los de cualquier trama que si entra.
             *   - el CRC que ha fallado es el del LPDU, o sea el de la
             *     CARGA. Lo que no vale son los datos de dentro.
             *
             * Se demostro el 14/10/2026 con una trama real destripada byte
             * a byte: cabecera buena, carga legible -el parser de posicion
             * contesto "AC0090, 14,2222 N, 60,6165 W"- y un bit malo en la
             * cola tirandolo todo.
             *
             * Asi que el renglon se pinta: "este avion ha transmitido, a
             * esta hora, por esta estacion", que es verdad y es justo lo
             * que el usuario lleva tres dias sin ver. Lo que NO se apunta es
             * el contenido, que para eso esta el volcado en crudo de
             * debajo y la posicion con sus pruebas aparte.
             */
            if (lpdu[k].datos_crudos != (const uint8_t *)0) {
                /* Con varios aviones en la trama no se sabe de quien es esta
                 * carga: archivarla bajo el primero es la mitad del mismo
                 * fallo. Ver la variable "varios". */
                if (!varios)
                crudo_pon(((ev.flags & HFDL_AC_EV_ICAO) != 0U) ? ev.icao : 0UL,
                          ev.ac_id, ev.gs_id,
                          lpdu[k].datos_crudos, lpdu[k].datos_crudos_len);
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
        /* Si ademas se habia sacado una posicion, se apunta que se ha
         * perdido: un contador que diga "la saque y no supe de quien era"
         * es la diferencia entre un fallo y una frecuencia tranquila. */
        if ((ev.flags & HFDL_AC_EV_POS) != 0U) { s_pos_huerfanas++; }
#if HFDL_DIAG
        s_sr_sin_avion++;
#endif
        /*
         * Y ESTO NO SUBE EL "/n" - 15/10/2026.
         *
         * Hasta hoy esta rama sumaba al mismo contador que las dos de
         * arriba, y por eso "/n" no se podia leer: juntaba una AVERIA
         * -la cabecera no se deja leer, o no trae ni un LPDU- con algo
         * PERFECTAMENTE NORMAL -una trama buena que no habla de ningun
         * avion-. Dos diagnosticos opuestos detras del mismo numero, asi
         * que el numero no servia para decidir nada: un 12 podia ser el
         * decodificador roto o la frecuencia tranquila.
         *
         * Ahora "/n" cuenta SOLO lo anomalo. Si sale en la chapa, hay
         * algo que mirar; si no sale, no lo hay. Lo normal se sigue
         * contando, pero solo con HFDL_DIAG, que es donde hace falta.
         *
         * MEDIDO antes de tocarlo: en las seis grabaciones de sim/aire
         * -31 ráfagas con el CRC bueno- "/n" vale CERO en todas. O sea
         * que este reparto no cambia ni un numero de los que hoy salen
         * verdes; cambia lo que significara el dia que salga uno.
         */
        return;
    }

    {
        hfdl_ac_rec_t *r;

        /*
         * ==================================================================
         * CON VARIOS AVIONES, NI POSICION NI DISTINTIVO - 29/10/2026, tarde,
         * tercera pasada. ESTO LE FALTABA AL ARREGLO DE ESTA MISMA TARDE.
         * ==================================================================
         *
         * La version de hace un rato dejaba ev.ac_id en HFDL_AC_ID_NONE y se
         * apoyaba ENTERA en que la guarda de trama anonima de arriba tirara
         * el evento. Y esa guarda es una Y:
         *
         *     sin OACI  Y  sin identificador  ->  se tira
         *
         * asi que basta con que UN LPDU de la trama levante HFDL_AC_EV_ICAO
         * -y lo levantan LOGON_CONFIRM, LOGON_REQUEST y LOGOFF- para que la
         * guarda no salte y vuelva a pasar exactamente lo que se queria
         * evitar: una subida con un LOGON para el avion A y datos de usuario
         * para el B funde la posicion de B en la ficha de A, y el mapa pinta
         * a A donde esta B.
         *
         * Y no es rebuscado: un LOGON CONFIRM en una trama de subida es el
         * mensaje mas natural que hay.
         *
         * *** Y LA SEGUNDA VERSION, DE HACE UN RATO, TAMPOCO BASTABA. ***
         *
         * Aquella quitaba las banderas de posicion y de distintivo pero
         * dejaba pasar ev.icao y ev.ac_id, razonando que "lo identificado
         * viene DENTRO de su propio LPDU". Eso es cierto POR LPDU y falso
         * para el evento: no hay un ev por avion, hay UNO para la trama, y
         * el bucle va escribiendo encima.
         *
         * El cuadro que faltaba: un LOGON CONFIRM del avion A pone
         * ev.icao = OACI de A y ev.ac_id = 5; un LOGOFF del avion B, dos
         * LPDU mas adelante, pone ev.icao = OACI de B y NO toca ev.ac_id.
         * El evento sale con la OACI de B y el identificador de A, y
         * hfdl_ac_update() hace dos destrozos con eso: le estampa la OACI
         * -y despues el MODELO- de B a la ficha de A, y de paso le quita el
         * identificador 5 a la ficha legitima de A, con lo que las bajadas
         * siguientes de A abren renglon nuevo.
         *
         * O sea que lo que hay que aceptar es mas simple de lo que me
         * costo verlo: **con varios aviones en la trama, esa trama no
         * entra en la tabla**. Ni posicion, ni distintivo, ni OACI, ni
         * identificador. No hay en ella un solo dato del que se sepa el
         * dueño sin ambiguedad, porque la lista de LPDU viene aplanada y
         * nadie dice de quien es cada uno.
         *
         * Lo que SI se queda es s_gs_ultima, que se apunta mas arriba: la
         * estacion emisora si es inequivoca, viene en la cabecera.
         *
         * El dia que hfdl_pdu.c diga de que avion es cada LPDU, esto se
         * convierte en un suceso por avion y se recupera todo. Hasta
         * entonces, un renglon de menos antes que un renglon falso.
         */
        /*
         * *** Y LA TERCERA VERSION, QUE ES LA BUENA - 29/10/2026, quinta
         * pasada. La de hace un rato se pasaba de frenada en dos sitios. ***
         *
         * 1. SE TIRABA TAMBIEN LO QUE SI ESTABA IDENTIFICADO. El LOGON
         *    CONFIRM lleva la direccion OACI *y* el identificador DENTRO DE
         *    SU PROPIA CARGA (ver hfdl_lpdu_result_t: icao_address y ac_id,
         *    "valid when kind == LOGON_CONFIRM only"). Ese par es
         *    inequivoco vaya la trama dirigida a uno o a ocho aviones: no
         *    sale de la lista de la cabecera, sale de dentro del LPDU.
         *
         *    El riesgo que describe el comentario de arriba -un LOGOFF de B
         *    pisa ev.icao y deja el ev.ac_id de A- no nace de que el dato
         *    sea ambiguo: nace de que hay UN SOLO ev por trama. O sea que
         *    solo se materializa si en la misma trama hay DOS O MAS LPDU
         *    identificadores. Con uno, no hay nada que mezclar.
         *
         *    Y tirarlo cuesta caro: esa confirmacion es la que ata la OACI
         *    con el identificador. Sin ella, todas las bajadas de ese avion
         *    salen como "ID 5" en vez de con su direccion, no se consulta
         *    la base de aviones y la columna del modelo se queda vacia toda
         *    la sesion.
         *
         *    Asi que el criterio es contar: n_ident == 1 entra, y entra sin
         *    posicion ni distintivo, que la mascara de arriba sigue puesta.
         *
         * 2. Y NO SE SUMA A "/n". s_sin_renglon cuenta, con todas las
         *    letras, tramas que PASARON EL CRC y venian ROTAS -"si esto
         *    sale, hay algo que mirar"-. Una subida a varios aviones es
         *    cosa normal, no una averia; lo dice el comentario de la propia
         *    cabecera de subida. Sumarla ahi vuelve a juntar una averia con
         *    algo perfectamente normal, que es el reparto que se deshizo a
         *    proposito el 15/10. Un numero en esa casilla tiene que seguir
         *    queriendo decir UNA sola cosa.
         */
        if (varios && (n_ident != 1U)) {
            return;
        }
        r = hfdl_ac_update(&s_tabla, &ev, &pide_modelo);

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
    l = linea_ram(i);
    if (l == (char *)0) { return ""; }

    /* --- quien es: la OACI si se sabe, y si no el identificador --- */
    /*
     * Y SI EL RENGLON ES UNA ESTACION DE TIERRA, SU NOMBRE - 29/10/2026,
     * revision a fondo de HFDL.
     *
     * HFDL_AC_R_GS existe desde el 14/10 con el comentario "este renglon ES
     * una estacion de tierra, no un avion", lo escribe hfdl_ac_update()...
     * y NO LO LEIA NADIE en todo el arbol. Los squitters entran en la misma
     * tabla que los aviones con ac_id = HFDL_AC_ID_NONE, o sea 255, asi que
     * la fila salia como
     *
     *     ID 255 | - | - | - | < SQUITTER | 142
     *
     * En el aire el squitter es lo mas frecuente que hay -una ranura cada
     * 32 segundos por estacion-, asi que ese avion fantasma esta SIEMPRE,
     * se lleva uno de los renglones visibles y uno de los 48 huecos de la
     * tabla por cada estacion que se oiga.
     *
     * Y es justo lo que apunta_trama() dice haber arreglado en su propio
     * comentario -"lo unico que no pasa es que aparezca un avion que no
     * existe"-: el squitter entra por otra puerta, antes de esa guarda.
     *
     * El nombre ya estaba a mano, en estacion_nombre().
     */
    if ((r->flags & HFDL_AC_R_GS) != 0U) {
        const char *nom = estacion_nombre(r->gs_id);

        if (nom != 0 && nom[0] != '\0') {
            while ((nom[0] != '\0') && (p < (uint8_t)(HFDL_MODO_LARGO - 12U))) {
                l[p++] = *nom++;
            }
        } else {
            l[p++] = 'G'; l[p++] = 'S'; l[p++] = ' ';
            aj_u2s_local(&l[p], r->gs_id);
            while (l[p] != '\0') { p++; }
        }
    } else if ((r->flags & HFDL_AC_R_ICAO) != 0U) {
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
    /* SE COGE EL CERROJO ANTES DE SOLTAR LA BANDERA. Ver s_remate: al reves
     * queda un hueco de un par de instrucciones en el que la interrupcion
     * no ve ninguna de las dos y abre segmento encima de este remate. */
    s_remate = 1U;
    s_finish_pendiente = 0U;

    ram = hfdl_scope_get_hfdl_ram((void *)0);
    if (ram == (uint8_t *)0) { s_remate = 0U; return; }

    hfdl_payload_decode_finish_ex(ram, &datos, &largo);
    hfdl_payload_decode_get_crc_status(&ok, &valido);

    /*
     * LOS BYTES DE LA ULTIMA RAFAGA, PASE EL CRC O NO - 14/10/2026.
     *
     * Hace falta para contestar a la pregunta que parte el problema en
     * dos: cuando una ráfaga bien sincronizada falla el CRC, ¿salen bytes
     * con pinta de trama y un par de bits mal -o sea ruido de canal- o
     * sale papilla uniforme -o sea un fallo de logica o de alineacion-?
     * Son dos averias distintas y se arreglan en sitios distintos.
     *
     * Solo son dos punteros: no copia nada. El buffer es de la region
     * prestada y vive hasta la ráfaga siguiente, que es justo lo que el
     * banco necesita para mirarlo. Nadie mas los usa.
     */
    s_ultimo_crudo = datos;
    s_ultimo_crudo_n = largo;

    {
        uint8_t ar = hfdl_payload_decode_arity();
        if (ar < 4U) {
            s_crc_tot[ar]++;
            if (ok && valido) { s_crc_ok[ar]++; }
        }
    }

    s_total++;

    /*
     * SOLO SE APUNTAN LAS QUE PASAN EL CRC. 25/09/2026.
     *
     * Una trama con el CRC malo no dice NADA: sus bytes son lo que el
     * Viterbi creyo ver, y ya se sabe que se equivoco. Ponerla en la lista no
     * es informacion, es tapar la que si vale.
     *
     * Lo que si dice algo es CUANTAS hay, y eso va a la chapa: "3/42" son
     * tres buenas de cuarenta y dos rafagas. Ese numero contesta a "va bien
     * la sintonia?" mucho mejor que once renglones de basura.
     */
#if HFDL_DIAG
    /* MEDIDA: una cabecera MPDU son 7 bytes y su CRC cubre solo esos 7.
     * Si un error de rafaga los toca, se tira la trama ENTERA aunque los
     * LPDU de dentro -que llevan su propio CRC-16- esten intactos.
     *
     * MEDIDO Y CERRADO - 14/10/2026: en las siete grabaciones hay ONCE
     * tramas con la cabecera mala, y de esas once NINGUNA lleva dentro un
     * solo LPDU con su CRC bueno. O sea que cuando el error toca la
     * cabecera, ha tocado el resto tambien, y no hay nada que rescatar.
     *
     * Queda el contador porque la pregunta es razonable y alguien -yo- la
     * volveria a hacer; lo que no queda es codigo que rescate nada,
     * porque no hay nada que rescatar. Solo habla cuando encuentra algo. */
    if (valido && !ok && datos != (const uint8_t *)0 && largo != 0U) {
        hfdl_mpdu_header_t hx;
        extern void hfdl_dg_cabecera_mala(uint32_t n_lpdu, uint32_t n_ok);
        if (hfdl_mpdu_parse_header(datos, largo, &hx)) {
            hfdl_lpdu_result_t lx[HFDL_MAX_LPDUS_TOTAL];
            uint32_t nx = hfdl_mpdu_extract_lpdus(datos, largo, &hx, lx,
                                                  HFDL_MAX_LPDUS_TOTAL);
            uint32_t z, nok = 0U;
            for (z = 0U; z < nx; z++) { if (lx[z].crc_ok) { nok++; } }
            hfdl_dg_cabecera_mala(nx, nok);
        } else {
            hfdl_dg_cabecera_mala(0U, 0U);
        }
    }
#endif
    if (valido && ok && datos != (const uint8_t *)0 && largo != 0U) {
        s_buenas++;
        /* DENTRO del cerrojo a proposito: `datos` apunta a la RAM prestada,
         * asi que leerlo despues de soltar seria leer lo que la
         * interrupcion ya este escribiendo. */
        apunta_trama(datos, largo);
    }
    s_remate = 0U;
}

uint32_t hfdl_modo_lineas(void)
{
    uint32_t n = s_tabla.count;
    return (n > HFDL_MODO_LINEAS) ? HFDL_MODO_LINEAS : n;
}
uint32_t hfdl_modo_simbolos_datos(void) { return s_simbolos_datos; }

const char *hfdl_modo_linea(uint32_t i) { return fila_avion(i); }

/*
 * EL VOLCADO EN CRUDO DEL ULTIMO LPDU DE DATOS DE ESE AVION. 14/10/2026.
 *
 * Diez LPDU de datos, todos de bajada, cero posiciones. El decodificador
 * busca el tipo 0xD1 y lo exige en el byte 0 o detras de un 0xFF, y eso
 * sale de UNA trama real mirada a mano hace un mes. Mientras no se vean
 * los bytes, cualquier arreglo es adivinar; y adivinar es justo lo que no
 * se hace aqui.
 *
 * Devuelve "" si ese avion no ha mandado datos, para que el que pinta sepa
 * que no hay renglon que poner.
 *
 * EL 0x01 DE DELANTE dice "pintame entero, sin columnas". Ver renglon() en
 * ui_digi.c: setenta y dos caracteres de hexadecimal no caben en ninguna
 * columna y partirlos estropea lo unico que dicen.
 */
/*
 * EL RENGLON DE CABECERA DE LA VISTA DE BYTES: donde se pierde el CRC.
 * 14/10/2026. Ver s_crc_ok[].
 */
const char *hfdl_modo_linea_crc(void)
{
    static char l[HFDL_CRUDO_TXT];
    static const char *const k_nom[4] = { "?", "BPSK", "4PSK", "8PSK" };
    uint8_t p = 0U, a;

    pon_car(l, &p, '\x01', HFDL_CRUDO_TXT);

    /*
     * EL NUMERO QUE FALTABA: CUANTAS RAFAGAS HAY DE VERDAD - 14/10/2026.
     *
     * Y hasta ahora NO HABIA FORMA DE SABER QUIEN ERA CIERTO. El embudo
     * cuenta enganches, y un enganche puede ser ruido; la chapa cuenta
     * tramas rematadas. Ninguno de los dos dice cuantas ráfagas DE VERDAD
     * ha habido, asi que oir diez y que salgan dos, y que solo hayan
     * pasado dos, se veian igual en pantalla.
     *
     * El pre-key si lo dice: son 249 ms de tono puro que la norma pone
     * delante de cada ranura, y ni el ruido ni los datos los imitan (ver
     * prekey_simbolo()). "P14" es "han pasado catorce ráfagas por la
     * antena", pase lo que pase despues.
     *
     * Con eso, los tres numeros de la pantalla contestan solos:
     *   P14 y BPSK 6/12  -> se detectan todas y fallan la mitad: demodular
     *   P40 y BPSK 6/12  -> se nos escapan: detectar
     * Son dos averias distintas y hasta hoy no se podian distinguir.
     */
    {
        uint32_t pk = hfdl_modo_prekeys();
        if (pk != 0U) {
            float dv = s_pk_desvio_hz;
            int32_t hz = (int32_t)((dv < 0.0f) ? (dv - 0.5f) : (dv + 0.5f));
            uint32_t cr = s_pk_con_rafaga;
            pon_car(l, &p, 'P', HFDL_CRUDO_TXT);
            pon_u32t(l, &p, (pk > 999U) ? 999U : pk, HFDL_CRUDO_TXT);
            /* Y cuantos de esos tonos llevaban ráfaga. Ver
             * s_pk_con_rafaga: sin este segundo numero, el primero
             * engaña por exceso -y me engaño a mi medio dia-. */
            pon_car(l, &p, '/', HFDL_CRUDO_TXT);
            pon_u32t(l, &p, (cr > 999U) ? 999U : cr, HFDL_CRUDO_TXT);
            /*
             * Y EL DESVIO QUE MIDIO EL ULTIMO, que es el numero que
             * contesta a "¿esta la radio en frecuencia?". Sale del tono
             * puro del pre-key, asi que no es una estimacion del lazo:
             * es una medida. A 17.967 kHz un cristal con 20 ppm son 359
             * Hz, y hasta hoy cualquier cosa por encima de 40 no
             * decodificaba. Si aqui sale un numero grande y constante,
             * la averia no esta en el decodificador.
             */
            pon_car(l, &p, ' ', HFDL_CRUDO_TXT);
            if (hz < 0) { pon_car(l, &p, '-', HFDL_CRUDO_TXT); hz = -hz; }
            else        { pon_car(l, &p, '+', HFDL_CRUDO_TXT); }
            pon_u32t(l, &p, (uint32_t)((hz > 9999) ? 9999 : hz), HFDL_CRUDO_TXT);
            pon_car(l, &p, 'H', HFDL_CRUDO_TXT); pon_car(l, &p, 'z', HFDL_CRUDO_TXT);
            pon_car(l, &p, ' ', HFDL_CRUDO_TXT); pon_car(l, &p, ' ', HFDL_CRUDO_TXT);
            /* D = partes de posicion que han llegado. Ver s_d1_vistos. */
            pon_car(l, &p, 'D', HFDL_CRUDO_TXT);
            pon_u32t(l, &p, (s_d1_vistos > 999U) ? 999U : s_d1_vistos, HFDL_CRUDO_TXT);
            if (s_sin_renglon != 0U) {
                /* "/n" = tramas que PASARON EL CRC y venian rotas: la
                 * cabecera del MPDU no se deja leer, o no trae ni un
                 * LPDU. Si esto sale, hay algo que mirar. Una trama
                 * buena que simplemente no habla de ningun avion NO
                 * suma aqui desde el 15/10/2026: ver apunta_trama(). */
                pon_car(l, &p, '/', HFDL_CRUDO_TXT);
                pon_u32t(l, &p, (s_sin_renglon > 99U) ? 99U : s_sin_renglon, HFDL_CRUDO_TXT);
            }
            if (s_pos_huerfanas != 0U) {
                pon_car(l, &p, '!', HFDL_CRUDO_TXT);   /* se saco una posicion y no se supo de quien */
                pon_u32t(l, &p, (s_pos_huerfanas > 99U) ? 99U : s_pos_huerfanas, HFDL_CRUDO_TXT);
            }
            pon_car(l, &p, ' ', HFDL_CRUDO_TXT); pon_car(l, &p, ' ', HFDL_CRUDO_TXT);
        }
    }

    for (a = 1U; a < 4U; a++) {
        if (s_crc_tot[a] == 0U) { continue; }
        /* ANTES de escribir, no despues: si no queda sitio para el modo
         * mas corto que se puede poner, se para aqui. */
        if ((uint32_t)p + 16U >= (uint32_t)HFDL_CRUDO_TXT) { break; }
        pon_txt(l, &p, k_nom[a], HFDL_CRUDO_TXT);
        pon_car(l, &p, ' ', HFDL_CRUDO_TXT);
        pon_u32t(l, &p, s_crc_ok[a], HFDL_CRUDO_TXT);
        pon_car(l, &p, '/', HFDL_CRUDO_TXT);
        pon_u32t(l, &p, s_crc_tot[a], HFDL_CRUDO_TXT);
        pon_car(l, &p, ' ', HFDL_CRUDO_TXT); pon_car(l, &p, ' ', HFDL_CRUDO_TXT);
    }
    if (p <= 1U) { return ""; }     /* todavia no ha entrado ninguna */
    if ((uint32_t)p >= (uint32_t)HFDL_CRUDO_TXT) { p = (uint8_t)(HFDL_CRUDO_TXT - 1U); }
    l[p] = '\0';
    return l;
}

const char *hfdl_modo_linea_bytes(uint32_t i)
{
    const hfdl_ac_rec_t *r;
    const hfdl_crudo_t *cr;

    if (i >= s_tabla.count || i >= HFDL_MODO_LINEAS) { return ""; }
    r = &s_tabla.rec[i];
    cr = crudo_de(r);
    if (cr == (const hfdl_crudo_t *)0) { return ""; }
    return cr->txt;
}

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

/*
 * COMO SE LLAMA EL AVION `i`, PARA LA ETIQUETA DEL MAPA - 13/10/2026.
 *
 * Y lo que un avion tiene de "indicativo" son dos cosas distintas:
 *
 *   EL DISTINTIVO DE VUELO ("DLH494"), que es el que dice la torre, el
 *   que sale en los paneles del aeropuerto y el que cualquiera reconoce.
 *   Se manda en algunos mensajes y no en todos.
 *
 *   LA OACI de 24 bits ("3C6545"), que la lleva SIEMPRE el avion y es
 *   unica, pero que no le dice nada a nadie mirando un mapa.
 *
 * Asi que gana el distintivo si se sabe, y si no el hexadecimal. Es el
 * mismo orden que usa fila_avion() para la primera columna, pero al reves
 * -alli manda la OACI porque la columna se llama "OACI"- y por eso son dos
 * funciones y no una: la tabla identifica, el mapa nombra.
 *
 * Devuelve 0 si no hay ni una cosa ni la otra, y entonces ese avion se
 * queda con su punto y sin etiqueta, que es mejor que una etiqueta vacia.
 */
uint8_t hfdl_modo_ident(uint32_t i, char *out, uint8_t tope)
{
    const hfdl_ac_rec_t *r;
    uint8_t p = 0U;

    if (out == 0 || tope < 8U) { return 0U; }
    out[0] = '\0';
    if (i >= (uint32_t)s_tabla.count || s_tabla.rec == 0) { return 0U; }
    r = &s_tabla.rec[i];

    if ((r->flags & HFDL_AC_R_FLIGHT) != 0U && r->flight[0] > ' ') {
        uint8_t c;
        for (c = 0U; c < 6U && p < (uint8_t)(tope - 1U)
                  && r->flight[c] > ' ' && r->flight[c] < 127; c++) {
            out[p++] = r->flight[c];
        }
    } else if ((r->flags & HFDL_AC_R_ICAO) != 0U) {
        p = (uint8_t)(p + hex24(&out[p], r->icao));
    }
    out[p] = '\0';
    return (uint8_t)(p > 0U);
}

/*
 * ======================================================================
 * LA OACI Y EL MODELO, PARA LA FICHA DEL MAPA - 19/10/2026
 * ======================================================================
 *
 * Avisado por el usuario: en la vista de mapa, la ficha de abajo dice
 * "CCA845 · 864 km · AZ 58" y no el modelo del avion ni su direccion
 * OACI, que la LISTA si enseña en sus dos primeras columnas.
 *
 * Y no era que no se supieran: es que no se le pasaban. La ficha del mapa
 * se arma con mapa_eti_dx_t, que para un avion solo llevaba un campo de
 * nombre -hfdl_modo_ident(), que da el distintivo de vuelo O la OACI, uno
 * de los dos y nunca los dos-. El modelo ya esta guardado en la tabla
 * desde que se consulto la base (ver icao24_db_lookup() mas arriba): solo
 * faltaba una puerta para sacarlo.
 *
 * QUE ESCRIBE, Y POR QUE ESE ORDEN. "781F2E A359 Airbus A350-941": la
 * direccion OACI primero y el modelo detras, que es el mismo orden de las
 * columnas de la lista, para que quien mire las dos pantallas vea lo
 * mismo en el mismo sitio.
 *
 * Y NO SE REPITE LA OACI. Si el avion no ha dicho todavia su distintivo
 * de vuelo, hfdl_modo_ident() ya devuelve la OACI, y entonces el nombre
 * de la ficha ES la OACI: ponerla otra vez aqui saldria "781F2E ·
 * 781F2E · A359...". Por eso esta decision vive en este fichero y no en
 * quien llama: es aqui donde se sabe cual de los dos dio ident().
 *
 * Devuelve 0 y deja la cadena vacia cuando no hay nada que añadir, que es
 * lo honrado: un avion recien oido no tiene ni modelo ni nada.
 */
uint8_t hfdl_modo_ficha(uint32_t i, char *out, uint8_t tope)
{
    const hfdl_ac_rec_t *r;
    uint8_t p = 0U;

    if (out == 0 || tope < 8U) { return 0U; }
    out[0] = '\0';
    if (i >= (uint32_t)s_tabla.count || s_tabla.rec == 0) { return 0U; }
    r = &s_tabla.rec[i];

    /* La OACI, solo si el nombre de la ficha no es ya ella. Ver arriba. */
    if ((r->flags & HFDL_AC_R_ICAO) != 0U
        && (r->flags & HFDL_AC_R_FLIGHT) != 0U && r->flight[0] > ' '
        && p + 6U < (uint8_t)tope) {
        p = (uint8_t)(p + hex24(&out[p], r->icao));
    }

    if (r->model[0] != '\0') {
        uint8_t c;

        if (p > 0U && p < (uint8_t)(tope - 1U)) { out[p++] = ' '; }
        for (c = 0U; c < 23U && r->model[c] >= 32 && r->model[c] < 127
                  && p < (uint8_t)(tope - 1U); c++) {
            out[p++] = r->model[c];
        }
    }

    out[p] = '\0';
    return (uint8_t)(p > 0U);
}

/* Cuantos aviones hay en la TABLA, que son mas que los renglones. */
uint32_t hfdl_modo_aviones(void) { return (uint32_t)s_tabla.count; }

void hfdl_modo_borra(void)
{
    /*
     * CON EL MODO APAGADO NO HAY DONDE: el boton Borrar y la salida del
     * modo pueden llegar aqui con el prestamo ya devuelto, y entonces
     * aviones_ram() es NULL. Reiniciar la tabla sobre NULL la deja vacia,
     * que es lo correcto; lo que no se puede es escribir en ella.
     */
    {
        hfdl_ac_rec_t *av = aviones_ram();

        if (av != (hfdl_ac_rec_t *)0) {
            hfdl_ac_init(&s_tabla, av,
                         (uint32_t)HFDL_MODO_AVIONES * sizeof(hfdl_ac_rec_t));
        } else {
            /* Sin prestamo no hay donde: se deja vacia a mano en vez de
             * pasarle un NULL a hfdl_ac_init(), que lo pondria a cero. */
            s_tabla.rec = (hfdl_ac_rec_t *)0;
            s_tabla.cap = 0U;
            s_tabla.count = 0U;
        }
    }
    s_nlineas = 0U;
    s_total   = 0U;
    s_buenas  = 0U;
    s_ud      = 0U;
    s_pos     = 0U;
    crudo_vacia();   /* las dos mitades del renglon van juntas - ver su cabecera */
    { uint8_t z; for (z = 0U; z < 4U; z++) { s_crc_ok[z] = 0U; s_crc_tot[z] = 0U; } }
    s_rechazadas = 0U;
    s_pos_sin_crc = 0U;
    s_d1_vistos = 0U; s_pos_huerfanas = 0U; s_sin_renglon = 0U;
#if HFDL_DIAG
    s_sr_cabecera = 0U; s_sr_sin_lpdu = 0U; s_sr_sin_avion = 0U;
#endif
    s_pk_vistos = 0U; s_pk_racha = 0U; s_pk_dentro = 0U; s_pk_pendiente = 0U;
    s_pk_edad = 0xFFFFFFFFu; s_pk_sin_armar = 0U;
    s_pk_con_rafaga = 0U; s_pk_ya_contado = 0U;
    s_pk_edad_min = 0xFFFFFFFFu; s_pk_edad_max = 0U;
    s_pk_juntos = 0U; s_pk_desde_ultimo = 0xFFFFFFF0u;
#if HFDL_DIAG
    s_pk_mismo_tono = 0U; s_pk_gap_min = 0xFFFFFFFFu;
#endif
    s_pk_ri = 0.0f; s_pk_rq = 0.0f; s_pk_p = 0.0f; s_pk_n = 0U;
#if HFDL_DIAG
    s_dg_mirando = 0U; s_dg_mejor = 0.0f; s_dg_pk_con_a1 = 0U;
    s_dg_banda[0] = 0U; s_dg_banda[1] = 0U; s_dg_banda[2] = 0U; s_dg_banda[3] = 0U;
    s_dg_ok[0] = 0U; s_dg_ok[1] = 0U; s_dg_ok[2] = 0U; s_dg_ok[3] = 0U;
    s_dg_coh_si = 0.0f; s_dg_coh_no = 0.0f; s_dg_nsi = 0U; s_dg_nno = 0U;
    s_dg_pico_si = 0.0f; s_dg_pico_no = 0.0f; s_dg_hz_si = 0.0f; s_dg_hz_no = 0.0f;
    s_dg_picoh_si = 0.0f; s_dg_picoh_no = 0.0f; s_dg_gana_half = 0U;
    s_dg_squit = 0U; s_dg_subida = 0U; s_dg_bajada = 0U;
    s_dg_acars = 0U; s_dg_acars_pos = 0U;
    { uint8_t zz; for (zz = 0U; zz < 8U; zz++) { s_dg_lpdu[zz] = 0U; } }
    s_dg_n_lista = 0U; s_dg_dur_vivo = 0U; s_dg_idx_vivo = -1;
#endif
    s_no_cupo = 0U;
    s_no_cupo_7 = 0U;
    s_pisada  = 0U;
    s_remate  = 0U;
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
        uint8_t pasa;

        /* Los relojes del portero, en tiempo y no en bloques: el bloque
         * mide 32 muestras a 96/192 kHz y 64 a 48. Ver porton_relojes(). */
        porton_relojes(n);
        pasa = porton(hfdl_demod_chain_get_agc_power_avg(&e->cadena));

        if (!pasa && !e->en_segmento) { return; }
    }

    for (k = 0U; k < ns; k++) {
        hfdl_modo_simbolo(hi[k], hq[k], si[k], sq[k]);
    }
}

/*
 * ======================================================================
 * EL PRE-KEY: 249 ms DE TONO PURO DELANTE DE CADA RAFAGA - 14/10/2026
 * ======================================================================
 *
 * Y era cierto. en lo que importa: si se oyen bien, no es señal debil, y
 * entonces el cuello de botella esta en DETECTARLAS. Su embudo lo dice:
 * 49>22>18. Dieciocho enganches donde una frecuencia con trece ranuras
 * cada 32 segundos deberia dar muchos mas.
 *
 * LO QUE LA NORMA REGALA Y NO USABAMOS. ARINC 635-3, apartado 2.2.2.1.1:
 *
 *   "During the prekey segment, the modulator shall output on the
 *    assigned frequency 448 2-PSK symbols with a constant phase
 *    modulation of 180 degrees, and the duration of the prekey segment
 *    shall be 249 ms."
 *
 * O sea: 448 simbolos de PORTADORA PURA antes del preambulo. Y la
 * descripcion de un decodificador comercial dice que cada ranura se
 * detecta por su pre-key caracteristica, un tono solo de 1440 Hz.
 * Nosotros buscabamos el preambulo a ciegas
 * contra el ruido, con un correlador de 127 simbolos, sin saber siquiera
 * que venia algo.
 *
 * COMO SE MIDE, sin trigonometria y sin tabla:
 *
 *   R = suma de  z[n] * conj(z[n-1])     sobre una ventana
 *   P = suma de  |z[n]|^2                sobre la misma
 *   coherencia = |R| / P
 *
 * Para una portadora pura, z[n] = A*e^{j(wn+f)} y cada producto vale
 * A^2*e^{jw}: todos EN FASE, asi que |R| = P y la coherencia vale 1,
 * PASE LO QUE PASE CON EL DESVIO DE FRECUENCIA -que es justo lo que no
 * conseguia una media a secas-. Para datos BPSK los signos saltan al
 * azar y los productos se cancelan: coherencia ~ 1/raiz(N). Para ruido,
 * igual.
 *
 * Y de regalo, el ANGULO de R es el desvio de frecuencia por simbolo,
 * que es exactamente lo que al lazo de Costas le cuesta trabajo
 * encontrar. Eso no se usa todavia - ver abajo.
 *
 * POR AHORA SOLO CUENTA. Esto no decide nada: no arma la busqueda, no
 * toca el Costas, no tira nada. Cuenta cuantos pre-key hay y se enseña
 * al lado del embudo. Si salen muchos mas pre-key que enganches, la
 * deteccion ES el problema y hay por donde entrar; si salen los mismos,
 * el problema esta en otro sitio y me dejo de teorias. Medir antes de
 * tocar, que es lo que hoy ha funcionado cada vez que lo he hecho y ha
 * fallado cada vez que no.
 */
#define HFDL_PREKEY_N      24U     /* simbolos de ventana: 13 ms, corto para que un desvio grande no la rompa */
#define HFDL_PREKEY_COH    0.80f   /* ruido y datos dan ~1/raiz(24) = 0,20 */
#define HFDL_PREKEY_SEGUID 6U      /* ventanas seguidas que hacen falta: 80 ms de tono */

static void prekey_simbolo(float zi, float zq)
{
    float coh;

    /* z[n] * conj(z[n-1]) */
    s_pk_ri += zi * s_pk_zi + zq * s_pk_zq;
    s_pk_rq += zq * s_pk_zi - zi * s_pk_zq;
    s_pk_p  += zi * zi + zq * zq;
    s_pk_zi = zi; s_pk_zq = zq;

    if (++s_pk_n < HFDL_PREKEY_N) { return; }

    coh = (s_pk_p > 1e-9f)
        ? (sqrtf(s_pk_ri * s_pk_ri + s_pk_rq * s_pk_rq) / s_pk_p)
        : 0.0f;

    if (coh >= HFDL_PREKEY_COH) {
#if HFDL_DIAG
        if (s_pk_dentro) { s_dg_dur_vivo = (uint16_t)(s_dg_dur_vivo + HFDL_PREKEY_N); }
#endif
        if (++s_pk_racha >= HFDL_PREKEY_SEGUID && !s_pk_dentro) {
            s_pk_dentro = 1U;
            s_pk_vistos++;
            s_pk_pendiente = 1U;
            if (s_pk_desde_ultimo < 2200U) { s_pk_juntos++; }
#if HFDL_DIAG
            if (s_pk_desde_ultimo < 500U)  { s_pk_mismo_tono++; }
            if (s_pk_desde_ultimo < s_pk_gap_min) { s_pk_gap_min = s_pk_desde_ultimo; }
#endif
            s_pk_desde_ultimo = 0U;
            s_pk_edad = 0U;
            s_pk_ya_contado = 0U;
            /* El angulo de R es el avance de fase por simbolo. A 1800
             * baudios, desvio = angulo * 1800 / 2pi. */
#if HFDL_DIAG
            s_dg_dur_vivo = (uint16_t)(HFDL_PREKEY_N * HFDL_PREKEY_SEGUID);
            s_dg_idx_vivo = (int16_t)((s_dg_n_lista < HFDL_DG_LISTA) ? s_dg_n_lista : -1);
            s_dg_coh_pk = coh;
            s_dg_hz_pk  = s_pk_w * (1800.0f / 6.28318530718f);
#endif
            s_pk_w = atan2f(s_pk_rq, s_pk_ri);
            s_pk_desvio_hz = s_pk_w * (1800.0f / 6.28318530718f);
        }
    } else {
#if HFDL_DIAG
        if (s_pk_dentro && s_dg_idx_vivo >= 0) {
            s_dg_l_dur[s_dg_idx_vivo] = s_dg_dur_vivo;
            s_dg_idx_vivo = -1;
        }
#endif
        s_pk_racha = 0U;
        s_pk_dentro = 0U;   /* se acabo el tono: el siguiente sera otro */
    }

    s_pk_ri = 0.0f; s_pk_rq = 0.0f; s_pk_p = 0.0f; s_pk_n = 0U;
}

/*
 * ======================================================================
 * LA PUERTA DEL PRE-KEY - 14/10/2026
 * ======================================================================
 *
 * Lleva razon, y el numero que lo explica es otro: ciento once enganches
 * para SETENTA Y OCHO ráfagas reales. Treinta y tres de cada ciento once
 * son ruido, y cada uno abre un segmento de entre 1,2 y 2,8 segundos
 * durante el cual la cadena esta SORDA. La ráfaga buena que llegue
 * mientras tanto se pierde entera, y desde fuera eso se ve exactamente
 * como "escucho tramas y no suben las lineas".
 *
 * El portero del entrenamiento ya tiraba parte de esos, pero TARDE: al
 * final del preambulo, con 150 simbolos gastados. El pre-key permite
 * cerrar la puerta antes de abrirla.
 *
 * LA REGLA: un preambulo de verdad viene SIEMPRE detras de su pre-key, a
 * menos de 900 simbolos (448 de tono mas 381 de preambulo, con holgura).
 * Un enganche sin tono delante no es una ráfaga.
 *
 * Y LA RED DE SEGURIDAD, que no es opcional: si NUNCA se ha visto un
 * pre-key -antena floja, banda rara, o un fallo en el propio detector- la
 * puerta se queda abierta. Un filtro capaz de dejar la radio sorda del
 * todo tiene que llevar siempre una salida, porque su modo de fallo es
 * mucho peor que lo que arregla.
 */
#define HFDL_PK_VENTANA 900U

static uint8_t prekey_arma(void)
{
    if (s_pk_vistos == 0U) { return 1U; }   /* ninguno visto: puerta abierta */
    return (uint8_t)((s_pk_edad < HFDL_PK_VENTANA) ? 1U : 0U);
}

static void prekey_envejece(void)
{
    if (s_pk_edad != 0xFFFFFFFFu) { s_pk_edad++; }
    if (s_pk_desde_ultimo < 0xFFFFFFF0u) { s_pk_desde_ultimo++; }
}

/*
 * PROBADO Y RETIRADO EN EL ACTO - 14/10/2026.
 *
 * La idea era usar la distancia que fija la norma -A1 acaba 575 simbolos
 * despues de que empiece el tono- para BAJAR el umbral de A1 dentro de
 * esa franja y recuperar los preambulos flojos. Sobre el papel es un
 * filtro adaptado aprovechando el tiempo de llegada, que es lo que debe
 * hacer un receptor con un preambulo precedido de tono.
 *
 * En el banco de aire real fue una catastrofe inmediata:
 *
 *     antes:  Hf-acars  58> 21> 18   12 tramas,  6 buenas
 *     con:    Hf-acars 134> 22> 16    1 trama,   0 buenas
 *
 * Y el motivo, una vez visto, es obvio: la franja dura seiscientos
 * simbolos y dentro de ella TAMBIEN hay ruido -el final del tono, los
 * bordes-. Con el umbral bajo, A1 dispara antes de tiempo, la maquina de
 * estados se va a A2_SEARCH con la ventana corrida, y cuando llega el
 * preambulo de verdad ya no lo mira nadie. Es el mismo fallo que el de la
 * rotacion M1 de esta mañana: enganchar con LO PRIMERO que cruza en vez
 * de con el MAXIMO.
 *
 * Lo que si tendria sentido es elegir el PICO dentro de la franja en vez
 * de la primera cruce, pero eso pide reestructurar el buscador y no se
 * hace a base de tocar un umbral desde fuera. Queda escrito para que
 * nadie -yo el primero- lo vuelva a intentar por la via facil.
 */

uint32_t hfdl_modo_sin_armar(void)   { return s_pk_sin_armar; }
uint32_t hfdl_modo_prekeys_juntos(void) { return s_pk_juntos; }
uint32_t hfdl_modo_prekeys_con_rafaga(void) { return s_pk_con_rafaga; }
#if HFDL_DIAG
uint32_t hfdl_modo_prekeys_mismo_tono(void) { return s_pk_mismo_tono; }
uint32_t hfdl_modo_prekey_gap_min(void) { return s_pk_gap_min; }
#endif
#if HFDL_DIAG
uint32_t hfdl_modo_dg_con_a1(void) { return s_dg_pk_con_a1; }
uint32_t hfdl_modo_dg_banda(uint32_t k) { return (k < 4U) ? s_dg_banda[k] : 0U; }
uint32_t hfdl_modo_dg_ok(uint32_t k) { return (k < 4U) ? s_dg_ok[k] : 0U; }
float hfdl_modo_dg_pico(uint8_t si) {
    return si ? ((s_dg_nsi != 0U) ? (s_dg_pico_si / (float)s_dg_nsi) : 0.0f)
              : ((s_dg_nno != 0U) ? (s_dg_pico_no / (float)s_dg_nno) : 0.0f);
}
float hfdl_modo_dg_picoh(uint8_t si) {
    return si ? ((s_dg_nsi != 0U) ? (s_dg_picoh_si / (float)s_dg_nsi) : 0.0f)
              : ((s_dg_nno != 0U) ? (s_dg_picoh_no / (float)s_dg_nno) : 0.0f);
}
uint32_t hfdl_modo_dg_gana_half(void) { return s_dg_gana_half; }
void hfdl_modo_dg_clases(uint32_t *sq, uint32_t *sub, uint32_t *baj)
{ *sq = s_dg_squit; *sub = s_dg_subida; *baj = s_dg_bajada; }
void hfdl_modo_dg_acars(uint32_t *n, uint32_t *con_pos)
{ *n = s_dg_acars; *con_pos = s_dg_acars_pos; }
uint32_t hfdl_modo_dg_lpdu(uint32_t k) { return (k < 8U) ? s_dg_lpdu[k] : 0U; }
uint32_t hfdl_modo_dg_lista(uint32_t k, float *coh, float *hz, float *a1)
{
    if (k >= s_dg_n_lista) { return 0xFFFFFFFFu; }
    *coh = s_dg_l_coh[k]; *hz = s_dg_l_hz[k]; *a1 = s_dg_l_a1[k];
    return s_dg_l_ok[k];
}
float    hfdl_modo_dg_giro(uint32_t k, uint32_t *cual)
{ if (k >= s_dg_n_lista) { *cual = 0U; return 0.0f; }
  *cual = s_dg_l_qgiro[k]; return s_dg_l_giro[k]; }
uint32_t hfdl_modo_dg_dur(uint32_t k) { return (k < s_dg_n_lista) ? s_dg_l_dur[k] : 0U; }
uint32_t hfdl_modo_dg_n_lista(void) { return s_dg_n_lista; }
float hfdl_modo_dg_hz(uint8_t si) {
    return si ? ((s_dg_nsi != 0U) ? (s_dg_hz_si / (float)s_dg_nsi) : 0.0f)
              : ((s_dg_nno != 0U) ? (s_dg_hz_no / (float)s_dg_nno) : 0.0f);
}
float    hfdl_modo_dg_coh(uint8_t si) {
    return si ? ((s_dg_nsi != 0U) ? (s_dg_coh_si / (float)s_dg_nsi) : 0.0f)
              : ((s_dg_nno != 0U) ? (s_dg_coh_no / (float)s_dg_nno) : 0.0f);
}
#endif
void hfdl_modo_edad_prekey(uint32_t *min, uint32_t *max)
{
    if (min) { *min = s_pk_edad_min; }
    if (max) { *max = s_pk_edad_max; }
}
uint32_t hfdl_modo_prekeys(void)     { return s_pk_vistos; }
float    hfdl_modo_prekey_desvio(void) { return s_pk_desvio_hz; }

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

        /* El pre-key se mide sobre el MISMO simbolo que ve el buscador,
         * para que los dos numeros del embudo hablen de lo mismo. */
        prekey_simbolo(i_sym, q_sym);
        prekey_envejece();

        /*
         * Y SI EL PRE-KEY HA DICHO DONDE ESTA LA PORTADORA, SE LE DICE AL
         * LAZO. Ver hfdl_costas_empuja_freq(): el lazo arranca en cero
         * cada ráfaga y no le da tiempo a integrar un desvio grande en los
         * 127 simbolos de A1. El pre-key se lo da medido, y encima antes
         * de que empiece el preambulo, que es cuando sirve.
         *
         * Solo una vez por ráfaga -s_pk_pendiente- y solo mientras se
         * busca A1: una vez enganchado, mandar el lazo a otro sitio seria
         * estropear justo lo que ya va bien.
         */
        /*
         * PROBADO Y RETIRADO - 14/10/2026.
         *
         * El pre-key son 448 simbolos sin una sola transicion, y un lazo
         * de sincronismo de simbolo se alimenta de transiciones: parecia
         * claro que durante el tono estaria integrando ruido y llegaria
         * al preambulo con el instante de muestreo corrido. liquid-dsp
         * trae lock()/unlock() justo para eso.
         *
         * Medido sobre los tres minutos de la grabacion por el CAT de
         * su propia radio: 10 tramas buenas pasaron a 9. Peor. Y en la
         * otra grabacion larga, igual. Asi que o el lazo ya se porta bien
         * con la portadora -tiene sentido: una portadora pura es una
         * señal perfectamente predecible, no ruido- o congelarlo le quita
         * una convergencia que si estaba aprovechando.
         *
         * Fuera. Queda escrito porque la idea es razonable y alguien
         * -yo- la volveria a tener.
         */
        if (s_pk_pendiente) {
            s_pk_pendiente = 0U;
            if (hfdl_preamble_sync_get_state(&e->preambulo) == HFDL_PREAMBLE_A1_SEARCH) {
                hfdl_costas_empuja_freq(&e->cadena.costas, s_pk_w);
            }
        }

        /*
         * PROBADO Y RETIRADO, Y ESTA ES LA SEGUNDA VEZ - 14/10/2026.
         *
         * La idea era no dejar que A1 saltara fuera de la franja donde el
         * pre-key dice que puede haber preambulo. Sobre el papel ataca el
         * numero que peor pinta tiene de toda la pantalla del usuario:
         * doscientos cincuenta y seis enganches de A1 para cincuenta y
         * dos ráfagas reales.
         *
         * En el banco de aire real: TODO A CERO. Y no era el ancho de la
         * franja -se probo con 6.000 simbolos, siete veces lo que dura
         * una ráfaga entera, y seguia a cero-, asi que prekey_arma()
         * contesta que no mucho mas a menudo de lo que deberia y ahi hay
         * otro bicho que no esta entendido.
         *
         * Es el segundo intento de usar la franja del pre-key para filtrar
         * A1 -el primero, bajando el umbral dentro de ella, tambien dejo
         * el banco a cero- y las dos veces ha salido mal por motivos
         * distintos. Queda retirado hasta que alguien entienda por que
         * prekey_arma() dice que no, y eso se mide contando cuando y por
         * que, no probando variantes.
         *
         * LO QUE SI FUNCIONA y se queda es la puerta en el ENGANCHE (ver
         * prekey_arma() donde se usa de verdad, unas lineas mas abajo):
         * ahi el filtro llega despues del preambulo, cuesta los simbolos
         * del preambulo y nada mas, y esta medido que no quita nada bueno.
         */
#if HFDL_DIAG
        /* Ver arriba: cuanto sube A1 en la franja de cada pre-key. */
        if (previo == HFDL_PREAMBLE_A1_SEARCH) {
            if (!s_dg_half_listo) {
                uint8_t gg;
                hfdl_framer_init(&s_dg_half, hfdl_preamble_sync_ref_a(), 127u, 1u, 9.0f, 9.0f);
                for (gg = 0U; gg < 7U; gg++) {
                    hfdl_framer_init(&s_dg_giro[gg], hfdl_preamble_sync_ref_a(), 127u, 1u, 9.0f, 9.0f);
                }
                s_dg_half_listo = 1U;
            }
            (void)hfdl_framer_process_symbol(&s_dg_half, ih, qh);
            {   /* giros de 45 grados: 1..7 por 45 */
                static const float cs[7] = { 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f, 0.0f, 0.70710678f };
                static const float sn[7] = { 0.70710678f, 1.0f,  0.70710678f,  0.0f, -0.70710678f,-1.0f,-0.70710678f };
                uint8_t gg;
                for (gg = 0U; gg < 7U; gg++) {
                    (void)hfdl_framer_process_symbol(&s_dg_giro[gg],
                            i_sym * cs[gg] - q_sym * sn[gg],
                            i_sym * sn[gg] + q_sym * cs[gg]);
                }
            }
            if (!s_dg_mirando && s_pk_edad == HFDL_DG_DESDE) {
                s_dg_mirando = 1U; s_dg_mejor = 0.0f; s_dg_mejor_h = 0.0f;
                s_dg_mejor_g = 0.0f; s_dg_cual_g = 0U;
            } else if (s_dg_mirando) {
                float32_t sc2 = hfdl_framer_get_last_score(&e->preambulo.a_framer);
                float32_t mg2 = (sc2 < 0.0f) ? -sc2 : sc2;
                float32_t sch = hfdl_framer_get_last_score(&s_dg_half);
                float32_t mgh = (sch < 0.0f) ? -sch : sch;
                if (mgh > s_dg_mejor_h) { s_dg_mejor_h = mgh; }
                if (mg2 > s_dg_mejor) { s_dg_mejor = mg2; }
                { uint8_t gg;
                  for (gg = 0U; gg < 7U; gg++) {
                      float32_t sg = hfdl_framer_get_last_score(&s_dg_giro[gg]);
                      float32_t mg = (sg < 0.0f) ? -sg : sg;
                      if (mg > s_dg_mejor_g) { s_dg_mejor_g = mg; s_dg_cual_g = (uint8_t)(gg + 1U); }
                  } }
                if (s_pk_edad >= HFDL_DG_HASTA) {
                    s_dg_mirando = 0U;
                    s_dg_coh_no += s_dg_coh_pk; s_dg_nno++;
                    s_dg_pico_no += s_dg_mejor;
                    s_dg_picoh_no += s_dg_mejor_h;
                    if (s_dg_mejor_h > s_dg_mejor) { s_dg_gana_half++; }
                    if (s_dg_n_lista < HFDL_DG_LISTA) {
                        s_dg_l_coh[s_dg_n_lista] = s_dg_coh_pk;
                        s_dg_l_hz[s_dg_n_lista]  = s_dg_hz_pk;
                        s_dg_l_a1[s_dg_n_lista]  = s_dg_mejor;
                        s_dg_l_giro[s_dg_n_lista] = s_dg_mejor_g;
                        s_dg_l_qgiro[s_dg_n_lista] = s_dg_cual_g;
                        s_dg_l_ok[s_dg_n_lista]  = 0U;
                        s_dg_n_lista++;
                    }
                    s_dg_hz_no += (s_dg_hz_pk < 0.0f) ? -s_dg_hz_pk : s_dg_hz_pk;
                    s_dg_banda[(s_dg_mejor < 0.15f) ? 0 :
                               (s_dg_mejor < 0.25f) ? 1 :
                               (s_dg_mejor < HFDL_CORR_THRESHOLD_A1) ? 2 : 3]++;
                }
            }
        } else if (s_dg_mirando) {
            /* salio de A1: engancho. El maximo que alcanzo es el numero
             * con el que hay que comparar los de los que no enganchan. */
            float32_t pico = e->preambulo.a1_mejor;
            s_dg_mirando = 0U; s_dg_pk_con_a1++;
            s_dg_ok[(pico < 0.15f) ? 0 : (pico < 0.25f) ? 1 :
                    (pico < HFDL_CORR_THRESHOLD_A1) ? 2 : 3]++;
            s_dg_coh_si += s_dg_coh_pk; s_dg_nsi++;
            s_dg_pico_si += pico;
            s_dg_picoh_si += s_dg_mejor_h;
            if (s_dg_n_lista < HFDL_DG_LISTA) {
                s_dg_l_coh[s_dg_n_lista] = s_dg_coh_pk;
                s_dg_l_hz[s_dg_n_lista]  = s_dg_hz_pk;
                s_dg_l_a1[s_dg_n_lista]  = pico;
                s_dg_l_giro[s_dg_n_lista] = s_dg_mejor_g;
                s_dg_l_qgiro[s_dg_n_lista] = s_dg_cual_g;
                s_dg_l_ok[s_dg_n_lista]  = 1U;
                s_dg_n_lista++;
            }
            s_dg_hz_si += (s_dg_hz_pk < 0.0f) ? -s_dg_hz_pk : s_dg_hz_pk;
        }
#endif
        st = hfdl_preamble_sync_process_symbol(&e->preambulo, i_sym, q_sym);
        s_estado = (uint8_t)st;

        /* Se cuenta el SALTO, no el estado: quedarse en A2_SEARCH doscientos
         * simbolos es un enganche, no doscientos. */
        if (st != previo) {
            if (st == HFDL_PREAMBLE_A2_SEARCH) {
                s_n_a1++;
                /* Ver s_pk_con_rafaga: el tono solo cuenta como ráfaga
                 * cuando aparece el preambulo que la norma pone detras. */
                if (!s_pk_ya_contado && s_pk_edad < HFDL_PK_VENTANA) {
                    s_pk_ya_contado = 1U;
                    s_pk_con_rafaga++;
                }
            }
            else if (st == HFDL_PREAMBLE_M1_SEARCH) { s_n_a2++; }
            else if (st == HFDL_PREAMBLE_LOCKED) { s_n_m1++; }
        }

        /* Ver prekey_arma(): un enganche sin tono delante no es una
         * ráfaga, y abrirle segmento deja la radio sorda dos segundos. */
        if (st == HFDL_PREAMBLE_LOCKED) {
            if (s_pk_edad < s_pk_edad_min) { s_pk_edad_min = s_pk_edad; }
            if (s_pk_edad != 0xFFFFFFFFu && s_pk_edad > s_pk_edad_max) {
                s_pk_edad_max = s_pk_edad;
            }
        }
        /*
         * ======================================================================
         * LOS DOS PORTEROS SE RETIRAN - 14/10/2026, por la tarde
         * ======================================================================
         *
         * Esa frase describe un sintoma muy concreto: algo que al
         * arrancar esta abierto y luego se cierra. Y lo era, literalmente:
         *
         *     prekey_arma() devuelve 1 mientras s_pk_vistos == 0.
         *
         * O sea que al encender, sin ningun tono detectado todavia, la
         * puerta esta abierta y pasa TODO. En cuanto se detecta el primer
         * pre-key, empieza a filtrar, y cualquier ráfaga cuyo tono no haya
         * visto -una mas floja, una que empiece durante un segmento- se
         * tira. Exactamente lo que el describe.
         *
         * Y LO PEOR: a estas alturas los dos porteros ya no sirven para
         * nada. Se construyeron hoy mismo, cuando A1 disparaba CINCUENTA Y
         * OCHO veces por catorce ráfagas reales y habia que tirar el ruido
         * como fuera. Desde que A1 correla coherente (ver HFDL_A1_BLOQUES
         * en hfdl_preamble_sync.c) dispara CATORCE veces por catorce
         * ráfagas. No queda ruido que tirar.
         *
         * MEDIDO antes de quitarlos, sobre las dos grabaciones largas:
         *
         *                        con porteros    sin porteros
         *     radio_oscar        14 tramas, 10     15 tramas, 10
         *     Hf-acars           14 tramas,  9     13 tramas,  9
         *
         * Las mismas buenas. Cero beneficio y un modo de fallo propio.
         *
         * SE QUEDAN LAS MEDIDAS, que no cuestan nada y sirven de aviso: si
         * algun dia el embudo vuelve a dispararse, hfdl_modo_rechazadas()
         * y hfdl_modo_sin_armar() siguen contando lo que SE HABRIA tirado,
         * y con esos numeros se decide de nuevo. Lo que se va es la
         * decision, no el instrumento. Es la misma leccion de esta
         * mañana con el giro absoluto: medir siempre, actuar solo cuando
         * la medida lo pida.
         */
        if (st == HFDL_PREAMBLE_LOCKED && !prekey_arma()) {
            s_pk_sin_armar++;   /* solo se apunta: ya no se tira */
        }
        if (0) {
            s_pk_sin_armar++;
            hfdl_preamble_sync_reset(&e->preambulo);
            return;
        }
        if (st == HFDL_PREAMBLE_LOCKED) {
            /*
             * UN PRE-KEY, UN ENGANCHE - 14/10/2026.
             *
             * Setenta y tres enganches para cincuenta y dos ráfagas
             * reales: veintiuno se colaban. El motivo es que la ventana
             * del pre-key dura 900 simbolos y sigue abierta DESPUES de
             * que su ráfaga se haya enganchado y decodificado; cualquier
             * enganche de ruido que caiga en lo que queda de ventana pasa
             * la puerta como si fuera el bueno.
             *
             * Cada tono anuncia UNA ranura. Una vez usado, se invalida.
             */
            s_pk_edad = 0xFFFFFFFFu;
        }
    }

    if (st == HFDL_PREAMBLE_LOCKED) {
        const hfdl_frame_params_t *fp =
            hfdl_preamble_sync_get_frame_params(&e->preambulo);

        hfdl_data_segment_init_retardo(&e->segmento, fp->mod_arity,
                                       fp->data_segment_cnt,
                                       hfdl_preamble_sync_retardo(&e->preambulo));
        /*
         * Y SE LE DICE A LA CADENA A QUE VELOCIDAD VA ESTA TRAMA, que es
         * lo que decide con que detector de fase corregir cada simbolo.
         * Hasta el 14/10/2026 corregia como BPSK siempre y las tramas de
         * 4-PSK y 8-PSK no pasaban el CRC nunca: cero de seis, medido en
         * la radio. Ver hfdl_costas_step_mpsk().
         *
         * El M1 es quien lo dice, y se sabe justo aqui: el preambulo
         * -que siempre es BPSK- ya ha terminado.
         */
        /*
         * BPSK HASTA QUE EMPIECEN LOS DATOS - 14/10/2026, por la tarde.
         *
         * Aqui se ponia la aridad de la trama en el LOCKED, o sea ANTES
         * del M2_SKIP y de las nueve ventanas de entrenamiento. Y esos
         * simbolos son HFDL_T_SEQ, que es real +-1: BPSK.
         *
         * En una trama de 8-PSK, eso hacia correr un detector con
         * regiones de decision de +-22,5 grados sobre 150 simbolos de
         * BPSK al principio y sobre las 72 (o 168) ventanas de 15
         * simbolos que van ENTRE las tramas de datos. Un simbolo BPSK
         * limpio tambien es un punto valido de 8-PSK, asi que sin ruido
         * no se nota -por eso ningun banco lo veia-; con ruido, cada
         * simbolo que se desvie mas de 22,5 grados se asigna al vecino y
         * el lazo tira hacia un enganche a 45 grados. Un salto de ciclo a
         * mitad de rafaga destruye el resto de la trama y no lo detecta
         * nadie.
         *
         * La aridad de verdad se pone al entrar en los datos, mas abajo.
         */
        e->cadena.bits_por_simbolo = 1U;
        e->aridad_trama = (uint8_t)fp->mod_arity;
        /*
         * EL mu DEL CONMUTADOR, NO UN LITERAL - 14/10/2026.
         *
         * Aqui ponia 0.1f a pelo "el mismo ancho de banda que dumphfdl",
         * y eso tenia dos consecuencias malas a la vez: el conmutador de
         * compilacion que documenta hfdl_equalizer.h estaba MUERTO para la
         * recepcion real -sólo cambiaba el valor de la autoprueba-, y el
         * valor usado era justo el que la historia de ese modulo dice,
         * medido en captura real, que DIVERGE.
         *
         * Ademas el 0,1 de dumphfdl es para LMS NORMALIZADO, y aqui el
         * NLMS esta apagado: con la entrada a potencia unidad el divisor
         * seria ~15, asi que 0,1 sin normalizar se comporta como un paso
         * de 1,5 y el limite de estabilidad es 2.
         *
         * Medido con una sonda sobre este mismo lazo, con ISI y ruido:
         * mu=0,10 deja 22,9 grados de fase residual; mu=0,03 deja 13,1.
         */
        hfdl_equalizer_init(&e->ecu, HFDL_EQ_ACTIVE_MU);
        /*
         * EL SINCRONISMO DE SIMBOLO NO SE REINICIA AQUI, Y SE PROBO.
         * 14/10/2026.
         *
         * Una auditoria propuso reiniciarlo por rafaga: su `rate` es un
         * integrador libre que se alimenta con ruido mientras el porton
         * esta cerrado, y medido se va a 2,99762 desde 3,0 tras seis
         * millones de muestras, que son 2,7 simbolos de deslizamiento.
         *
         * Se hizo, se entrego, y en antena BPSK bajo del 39 % al 22 %. El
         * argumento tiene la otra mitad que no se penso: reiniciar TIRA
         * una estimacion ya convergida y obliga a readquirir en los 150
         * simbolos de preambulo mas entrenamiento, y la cabecera del
         * propio modulo dice que su lazo todavia derivaba despues de
         * 3.000. No hay presupuesto para readquirir.
         *
         * Queda fuera hasta que el banco de audio -sim/hfdl_extremo.c,
         * apartado 4- funcione y pueda decidirlo con un numero.
         */
        s_simbolos_datos = 0U;
        s_polaridad_hubo = 0U;
        e->corr_t = 0.0f;
        e->corr_tq = 0.0f;
        e->ener_t = 0.0f;
        e->n_t = 0U;
        e->giro_cos = 1.0f; e->giro_sen = 0.0f;
        e->polaridad_fijada = 0U;
        e->decodificando = 0U;
        e->en_segmento = 1U;
    }
}

uint32_t hfdl_modo_rechazadas(void)    { return s_rechazadas; }
uint32_t hfdl_modo_pos_sin_crc(void)   { return s_pos_sin_crc; }
uint32_t hfdl_modo_d1_vistos(void)     { return s_d1_vistos; }
uint32_t hfdl_modo_pos_huerfanas(void) { return s_pos_huerfanas; }
uint32_t hfdl_modo_sin_renglon(void)   { return s_sin_renglon; }
#if HFDL_DIAG
void hfdl_modo_dg_sin_renglon(uint32_t *cabecera, uint32_t *sin_lpdu,
                              uint32_t *sin_avion)
{
    if (cabecera  != 0) { *cabecera  = s_sr_cabecera; }
    if (sin_lpdu  != 0) { *sin_lpdu  = s_sr_sin_lpdu; }
    if (sin_avion != 0) { *sin_avion = s_sr_sin_avion; }
}
#endif

const uint8_t *hfdl_modo_ultima_carga(uint32_t *n)
{
    if (n != (uint32_t *)0) { *n = s_ult_carga_n; }
    return s_ult_carga;
}

const uint8_t *hfdl_modo_ultimo_crudo(uint32_t *n)
{
    if (n != (uint32_t *)0) { *n = s_ultimo_crudo_n; }
    return s_ultimo_crudo;
}
uint8_t hfdl_modo_polaridad(void)      { return s_polaridad_ultima; }
uint8_t hfdl_modo_polaridad_lista(void) { return s_polaridad_hubo; }
