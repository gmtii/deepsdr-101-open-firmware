#include "wspr_modo.h"

#if WSPR_MODO

#include "wspr_rx.h"
#include "wspr_msg.h"
#include "hfdl_ram.h"
#include "ft8_shared_ram.h"
#include "waterfall.h"
#include "rtc_hw.h"
#include <string.h>

extern volatile uint32_t g_msticks;

static uint8_t  s_on;
static uint8_t  s_capturando;
static uint8_t  s_ult_min;        /* el minuto en que se arranco */
static uint16_t s_buenas;
static uint8_t  s_hay_hora;

/*
 * QUE EL ESPECTROGRAMA CABE EN LA MEMORIA PRESTADA.
 *
 * Esto ya fallo una vez, y fallo EN SILENCIO: wspr_modo_start() devolvia 0
 * por falta de sitio, el modo se ponia igual, la pantalla salia entera y no
 * decodificaba nunca. Desde fuera se ve como "WSPR no funciona", que es
 * justo el sintoma que no dice donde mirar.
 *
 * Aqui el mismo fallo es un error de compilacion con los bytes delante. Si
 * algun dia se ensancha la ventana de busqueda (WSPR_RX_BINS) o se alarga
 * (WSPR_RX_PASOS), salta aqui y no en la radio.
 */
_Static_assert((WSPR_RX_RAM + (WSPR_MODO_LINEAS * WSPR_MODO_LARGO))
               <= sizeof(ft8_shared_ram_t),
               "el espectrograma de WSPR no cabe en la memoria de la cascada");
static uint16_t s_espera;
static uint8_t  s_q;          /* corroboracion del ultimo intento, 0 = ninguno */
/*
 * LA HORA A LA QUE EMPEZO LA EMISION, no a la que acabamos de decodificar.
 *
 * Se guarda al ABRIR la captura porque es lo que hay que ensenar, y no
 * coinciden: la captura abre en el segundo 59 del minuto IMPAR, la emision
 * empieza en el par siguiente, y cuando cerramos y decodificamos ya estamos
 * otra vez en un minuto impar - el de despues.
 *
 * Lo enseno una foto de la radio: "05:53" en la fila con el reloj marcando
 * 05:54. La emision era la de las 05:52. Asi TODOS los reportes salian con
 * minuto impar, y WSPR se etiqueta siempre por el minuto PAR en que
 * empieza: es lo que se manda a wsprnet y con lo que se compara con otros.
 * Un minuto de mas no rompe nada, pero convierte cada linea en algo que no
 * casa con ninguna otra lista del mundo.
 */
static uint8_t  s_h_emi, s_m_emi;
/*
 * LOS RENGLONES VIVEN EN LA MEMORIA PRESTADA, NO EN SRAM PROPIA.
 * 27/09/2026, al llegar AIS.
 *
 * Son 12 * 72 = 864 bytes, y cuando AIS entro quedaban 844 libres de
 * 196.608 en toda la radio. WSPR ya tiene cogidos 54 kB de la cascada para
 * su espectrograma y le sobran 7.376: tener ademas 864 en SRAM propia,
 * ocupados solo mientras el modo esta puesto, era pagar dos veces.
 *
 * Van al FINAL de la region prestada y a wspr_rx.c se le dice que la suya
 * acaba antes. Asi el espectrograma sigue empezando en el byte cero -que es
 * lo que espera- y no hay que tocarlo.
 */
typedef char wspr_lineas_t[WSPR_MODO_LINEAS][WSPR_MODO_LARGO];
static wspr_lineas_t *s_linp;
#define s_lin (*s_linp)
static uint32_t s_n;

/* Un entero a texto, sin printf. */
static uint8_t pon_u(char *d, uint32_t v)
{
    char t[12];
    uint8_t n = 0U, i;

    do { t[n++] = (char)('0' + (v % 10U)); v /= 10U; } while (v != 0U);
    for (i = 0U; i < n; i++) { d[i] = t[n - 1U - i]; }
    return n;
}

uint8_t wspr_modo_activo(void)     { return s_on; }
uint8_t wspr_modo_capturando(void) { return s_capturando; }
uint16_t wspr_modo_buenas(void)    { return s_buenas; }
uint32_t wspr_modo_lineas(void)    { return s_n; }
uint8_t  wspr_modo_hay_hora(void)  { return s_hay_hora; }
uint16_t wspr_modo_espera_s(void)  { return s_espera; }
uint8_t  wspr_modo_q(void)         { return s_q; }

const char *wspr_modo_linea(uint32_t i)
{
    if (s_linp == (wspr_lineas_t *)0) { return ""; }
    return (i < s_n) ? s_lin[i] : "";
}

uint8_t wspr_modo_por_ciento(void)
{
    uint32_t p = wspr_rx_pasos();

    if (!s_capturando) { return 0U; }
    return (uint8_t)((p * 100UL) / WSPR_RX_PASOS);
}

void wspr_modo_borra(void)
{
    s_n = 0U;
    s_buenas = 0U;
}

uint8_t wspr_modo_start(void)
{
    uint8_t *ram;
    uint32_t cap = 0UL;

    if (s_on) { return 1U; }
    if (waterfall_prestada()) { return 0U; }   /* otro modo la tiene */

    hfdl_ram_coge();
    /*
     * LA REGION ENTERA, NO LA DE DESPUES DE LA CABECERA.
     *
     * hfdl_scope_get_hfdl_ram() devuelve 45.216 bytes -se guarda 9.216 para
     * los estados de HFDL- y el espectrograma pide 47.056. Con esa se
     * quedaba 1.840 bytes corto y esta funcion devolvia 0 SIEMPRE: el modo
     * se ponia, la pantalla salia, y no pasaba nada nunca. Ver
     * hfdl_ram_todo(), que explica por que WSPR si puede usar la cabecera.
     */
    ram = hfdl_ram_todo(&cap);
    if ((ram == (uint8_t *)0) ||
        (cap < (WSPR_RX_RAM + (uint32_t)sizeof(wspr_lineas_t)))) {
        hfdl_ram_suelta();
        return 0U;
    }
    /*
     * El reparto: el espectrograma por delante, los renglones al final.
     * Si no cupieran los dos, manda el espectrograma - sin el no hay modo,
     * y sin renglones solo falta la pantalla.
     */
    cap -= (uint32_t)sizeof(wspr_lineas_t);
    s_linp = (wspr_lineas_t *)(void *)(ram + cap);
    memset(s_linp, 0, sizeof *s_linp);

    wspr_rx_init(ram, cap);
    s_on = 1U;
    s_capturando = 0U;
    s_ult_min = 0xFFU;
    wspr_modo_borra();
    return 1U;
}

void wspr_modo_stop(void)
{
    if (!s_on) { return; }
    s_on = 0U;
    s_capturando = 0U;
    s_linp = (wspr_lineas_t *)0;
    s_n = 0U;              /* los renglones se van con la memoria prestada */
    hfdl_ram_suelta();
}

void wspr_modo_mete(const float *audio, uint16_t n)
{
    if (!s_on || !s_capturando) { return; }
    wspr_rx_mete(audio, n);
}

/* Una linea de la tabla: hora, indicativo, localizador, potencia y
 * desvio. Las columnas las pone ui_digi.c; aqui van tabuladas. */
static void apunta(const wspr_msg_t *m, int16_t hz, uint8_t q,
                   uint8_t hh, uint8_t mm)
{
    char *l;
    uint8_t p = 0U, i;

    if (s_n >= WSPR_MODO_LINEAS) {
        for (i = (uint8_t)(WSPR_MODO_LINEAS - 1U); i > 0U; i--) {
            memcpy(s_lin[i], s_lin[i - 1U], WSPR_MODO_LARGO);
        }
        s_n = WSPR_MODO_LINEAS;
    } else {
        for (i = (uint8_t)s_n; i > 0U; i--) {
            memcpy(s_lin[i], s_lin[i - 1U], WSPR_MODO_LARGO);
        }
        s_n++;
    }
    l = s_lin[0];

    l[p++] = (char)('0' + ((hh / 10U) % 10U)); l[p++] = (char)('0' + (hh % 10U));
    l[p++] = ':';
    l[p++] = (char)('0' + ((mm / 10U) % 10U)); l[p++] = (char)('0' + (mm % 10U));
    l[p++] = '\t';

    for (i = 0U; m->indicativo[i] != '\0' && i < 7U; i++) { l[p++] = m->indicativo[i]; }
    l[p++] = '\t';
    for (i = 0U; m->locutor[i] != '\0' && i < 5U; i++) { l[p++] = m->locutor[i]; }
    l[p++] = '\t';

    p += pon_u(&l[p], m->dbm);
    l[p++] = '\t';

    if (hz < 0) { l[p++] = '-'; hz = (int16_t)(-hz); }
    else        { l[p++] = '+'; }
    p += pon_u(&l[p], (uint32_t)(hz / 100));
    l[p++] = ',';
    l[p++] = (char)('0' + ((hz / 10) % 10));
    l[p++] = '\t';

    p += pon_u(&l[p], q);
    l[p] = '\0';
}

/*
 * CUANTO FALTA PARA LA SIGUIENTE CAPTURA, en segundos.
 *
 * La captura abre en el segundo 59 de un minuto IMPAR, o sea un segundo
 * antes del minuto par en el que empieza la emision (ver WSPR_RX_ANTES_MS).
 * Desde un minuto impar faltan 59-segundo; desde uno par hay que cruzar el
 * minuto entero, o sea 119-segundo.
 *
 * Esto es lo unico que la pantalla puede ensenar durante los dos minutos en
 * los que no pasa nada, y por eso existe: una barra parada y sin numero se
 * lee como un cuelgue.
 *
 * VA EN SU FUNCION Y SE LLAMA AL FINAL, no al principio. Estaba arriba, y
 * calculado antes de tocar la maquina de estados decia 0 -"estoy
 * capturando"- en el mismo instante en que la captura acababa de cerrarse:
 * un tic entero ensenando "empieza en 0:00" con la oreja ya cerrada. Se ve
 * poco y se entiende menos; lo cazo sim/wspr_ciclo.c comprobando que la
 * cuenta atras baja de uno en uno.
 */
static void espera_calcula(const rtc_hw_datetime_t *h)
{
    if (s_capturando) {
        s_espera = 0U;
    } else if ((h->minute & 1U) != 0U) {
        s_espera = (h->second < 59U) ? (uint16_t)(59U - h->second) : 0U;
    } else {
        s_espera = (uint16_t)(119U - h->second);
    }
}

void wspr_modo_poll(void)
{
    rtc_hw_datetime_t h;

    if (!s_on) { return; }
    rtc_hw_get(&h);

    /*
     * "HAY HORA" SE MIDE IGUAL QUE EN FT8 - y las dos estaban mal.
     *
     * 30/09/2026. Lo de antes miraba si la FECHA seguia siendo 2026-01-01,
     * la de arranque en frio, y decia: "No es elegante, pero es el unico
     * dato que hay". Eso era FALSO, y ahi estaba el fallo: desde 09/2026
     * existe rtc_hw_has_ever_synced(), que contesta exactamente esta
     * pregunta y lo hace bien. El comentario afirmaba que no habia
     * alternativa sin haberlo comprobado, y al leerlo nadie la buscaba.
     *
     * El sintoma, con una radio nueva delante: pones la hora con el
     * teclado -que NO toca la fecha, ver rtc_pon_hora() en main.c-, FT8
     * decodifica de maravilla porque el reloj esta bien, y encima del
     * panel sigue poniendo "pon el reloj en hora". El aviso mentia, no el
     * decodificador.
     *
     * rtc_hw_has_ever_synced() lo marca rtc_hw_set(), por donde pasan
     * TODOS los caminos que ponen el reloj -teclado, RDS y DCF77-, y vive
     * en un registro de respaldo que aguanta el apagado con la pila. Solo
     * se borra si se pierde la VBAT, que es justo cuando el calendario
     * deja de ser de fiar.
     */
    (void)h;
    s_hay_hora = (uint8_t)(rtc_hw_has_ever_synced() ? 1U : 0U);


    /*
     * ARRANCAR. Una emision empieza un segundo despues del minuto par, y
     * la captura tiene que empezar WSPR_RX_ANTES_MS antes que eso - o
     * sea, un segundo ANTES del minuto par. Ver WSPR_RX_ANTES_MS: lo que
     * llego antes de empezar a capturar no esta, y la busqueda no puede
     * ir hacia atras.
     */
    if (!s_capturando) {
        /*
         * EL SIGUIENTE MINUTO ES PAR, o sea: ESTE es impar.
         *
         * 27/09/2026. Aqui ponia "(h.minute + 1U) & 1U" con el comentario
         * "el siguiente es par", y eso es exactamente lo contrario: si el
         * minuto es 1, el siguiente es 2, y 2 & 1 vale CERO. La condicion
         * se cumplia en los minutos PARES, o sea que la captura abria un
         * minuto y un segundo ANTES de tiempo.
         *
         * Lo que pasaba entonces: la ventana de captura son 114,7 s
         * (WSPR_RX_PASOS medios simbolos) y la emision dura 110,6. Abriendo
         * 61 s antes, la ventana se cierra cuando la emision lleva 53 s, o
         * sea con menos de la mitad dentro. NO DECODIFICA NUNCA, y desde
         * fuera se ve igual que no haber propagacion.
         *
         * Lo cazo sim/wspr_ciclo.c a la primera: tres aperturas donde
         * tenian que salir dos, y todas en minuto par. En la radio habrian
         * hecho falta semanas para sospechar del reloj en vez de la antena.
         */
        uint8_t impar = (uint8_t)(h.minute & 1U);

        if (impar && h.second >= 59U && h.minute != s_ult_min) {
            s_ult_min = h.minute;
            /*
             * La emision empieza en el minuto SIGUIENTE, que es el par.
             * Se apunta ahora porque al cerrar ya no se puede saber.
             */
            s_m_emi = (uint8_t)((h.minute + 1U) % 60U);
            s_h_emi = (s_m_emi == 0U) ? (uint8_t)((h.hour + 1U) % 24U)
                                      : h.hour;
            wspr_rx_reinicia();
            s_capturando = 1U;
        }
        espera_calcula(&h);
        return;
    }

    /*
     * EL ESPECTROGRAMA SE LLENA AQUI, no en la interrupcion.
     *
     * Cada medio simbolo la interrupcion deja apuntado un paso; esto los
     * hace. Son 274 Goertzel de 256 muestras, unos cuatro milisegundos, y
     * cuatro milisegundos dentro del manejador del audio son mas de un
     * bloque entero: el conversor repite lo que tenga y sale un tono.
     * Aqui no molestan a nadie.
     */
    (void)wspr_rx_pasos_pendientes();

    /* CERRAR Y DECODIFICAR. */
    if (wspr_rx_listo()) {
        wspr_msg_t m;
        int16_t hz = 0;
        uint8_t q = 0U;

        s_capturando = 0U;
        memset(&m, 0, sizeof m);
        if (wspr_rx_decodifica(&m, &hz, &q)) {
            s_buenas++;
            apunta(&m, hz, q, s_h_emi, s_m_emi);
        }
        /*
         * Y SE GUARDA LA CORROBORACION AUNQUE NO HAYA SALIDO NADA.
         *
         * Es el unico numero que dice por que no sale. Medido en el banco:
         * una emision de verdad da 93-100 y el ruido puro 74-83, y el
         * corte esta en 88. Asi que con la tabla vacia:
         *
         *   q por los 70   no hay senal en la ventana
         *   q por los 80   la hay y llega justa; es propagacion
         *   q bajo y saltando   ni se encuentra el sincronismo, y eso
         *                       apunta al reloj, no a la antena
         *
         * Sin este numero las tres se ven igual y se pierde la tarde
         * cambiando de banda.
         */
        wspr_rx_diag(&s_q, (int16_t *)0);
    }
    espera_calcula(&h);
}

#endif /* WSPR_MODO */
