/*
 * LOS CONSUMIDORES DEL CAMINO DE AUDIO, DE MENTIRA Y CONTADOS. 10/10/2026.
 *
 * demod_am.c reparte: coge la I/Q cruda del bloque, la demodula, y va
 * dejando copias por el camino a veintitantos decodificadores -CW, RTTY,
 * NAVTEX, SSTV, HFDL, FT8, RDS, APRS...- y al final manda el audio al I2S.
 *
 * Enlazar los veintitantos de verdad seria arrastrar medio firmware:
 * pantallas, ajustes, la flash. Y no haria falta para lo que este banco
 * viene a medir, que es EL REPARTO, no los decodificadores -cada uno ya
 * tiene el suyo-.
 *
 * Asi que aqui estan todos, vacios, contando:
 *
 *   - cuantas veces los han llamado;
 *   - cuantas muestras les han dado en total;
 *   - y con que puntero, que es lo que permite mirar la union s_via desde
 *     fuera sin tocar demod_am.c (ver el banco).
 *
 * Y se pueden encender y apagar uno a uno, que es como se comprueba que el
 * reparto va donde dice: con CW encendido y RTTY apagado, las muestras
 * tienen que acabar en CW y no en RTTY.
 *
 * LO QUE ESTO NO ES: una prueba de que los decodificadores funcionan. Mide
 * el fontanero, no el agua.
 */
#include <string.h>
#include <stdint.h>
#include "demod_stubs.h"
#include "gd32f4xx.h"
#include "dcf77.h"

consumidor_t g_cons[CONS_N];

static const char *NOMBRES[CONS_N] = {
    "ais", "ale", "analizador", "aprs", "cw", "dcf77", "dsc", "ft8",
    "hell", "hfdl", "ident", "jtty", "navtex", "psk31", "rds", "rtty",
    "rtty_scope", "sstv", "stanag", "wefax", "wspr", "bip", "i2s"
};

const char *cons_nombre(int i) { return (i >= 0 && i < CONS_N) ? NOMBRES[i] : "?"; }

void cons_reinicia(void)
{
    int i;
    for (i = 0; i < CONS_N; i++) {
        g_cons[i].llamadas = 0u;
        g_cons[i].muestras = 0u;
        g_cons[i].ultimo   = 0;
        g_cons[i].ultimo_n = 0u;
    }
}

void cons_todos_apagados(void)
{
    int i;
    for (i = 0; i < CONS_N; i++) { g_cons[i].activo = 0u; }
}

static void anota(int i, const void *p, uint32_t n)
{
    g_cons[i].llamadas++;
    g_cons[i].muestras += n;
    g_cons[i].ultimo    = (uintptr_t)p;
    g_cons[i].ultimo_n  = n;
}

/* Un par por consumidor: el "¿estas encendido?" y el "toma muestras".
 * Se escriben con macros porque son cincuenta y tres y escribirlos a mano
 * es cincuenta y tres sitios donde poner el indice equivocado. */
#define PAR_N32(IDX, ACT, MET)                                            \
    uint8_t ACT(void) { return g_cons[IDX].activo; }                      \
    void MET(const float *a, uint32_t n) { anota(IDX, a, n); }
#define PAR_N16(IDX, ACT, MET)                                            \
    uint8_t ACT(void) { return g_cons[IDX].activo; }                      \
    void MET(const float *a, uint16_t n) { anota(IDX, a, (uint32_t)n); }

PAR_N16(CONS_AIS,    ais_modo_activo,    ais_modo_mete)
PAR_N16(CONS_ALE,    ale_modo_activo,    ale_modo_mete)
PAR_N32(CONS_ANALIZ, analiz_activo,      analiz_feed)
PAR_N32(CONS_APRS,   ax25_activo,        ax25_process)
PAR_N32(CONS_DSC,    dsc_activo,         dsc_process)
PAR_N16(CONS_FT8,    ft8_modo_activo,    ft8_modo_come)
PAR_N32(CONS_HELL,   hell_activo,        hell_process)
PAR_N16(CONS_IDENT,  ident_modo_activo,  ident_modo_mete)
PAR_N16(CONS_JTTY,   jtty_modo_activo,   jtty_modo_mete)
PAR_N32(CONS_NAVTEX, navtex_activo,      navtex_process)
PAR_N32(CONS_PSK31,  psk31_activo,       psk31_process)
PAR_N32(CONS_RDS,    rds_activo,         rds_feed)
PAR_N32(CONS_SSTV,   sstv_activo,        sstv_process)
PAR_N16(CONS_STANAG, stanag_modo_activo, stanag_modo_mete)
PAR_N32(CONS_WEFAX,  wefax_activo,       wefax_process)
PAR_N16(CONS_WSPR,   wspr_modo_activo,   wspr_modo_mete)

/* Los que no siguen el patron, uno a uno. */

uint8_t cw_get_enabled(void) { return g_cons[CONS_CW].activo; }
void    cw_init(void) { }
void    cw_process(const float *a, uint32_t n) { anota(CONS_CW, a, n); }
void    cw_set_pitch_hz(float hz) { (void)hz; }
void    cw_set_wpm_hint(float w) { (void)w; }

uint8_t rtty_get_enabled(void) { return g_cons[CONS_RTTY].activo; }
void    rtty_init(void) { }
void    rtty_process(const float *a, uint32_t n) { anota(CONS_RTTY, a, n); }
void    rtty_scope_init(void) { }
void    rtty_scope_feed(const float *a, uint32_t n) { anota(CONS_SCOPE, a, n); }

/* HFDL come I y Q a la vez: se anota con el puntero de I. */
uint8_t hfdl_modo_activo(void) { return g_cons[CONS_HFDL].activo; }
void    hfdl_modo_mete(const float *i_in, const float *q_in, uint32_t n)
{ (void)q_in; anota(CONS_HFDL, i_in, n); }

/*
 * DCF77 y familia. Aqui hay un detalle que importa: demod_am.c llama a
 * dcf77_usa_fase() para decidir si alimenta por FASE o por ENVOLVENTE, y
 * esa decision es la de ALS162 contra las demas emisoras horarias. El
 * banco la mueve con cons_dcf77_fase() para poder recorrer los dos
 * caminos; devolver siempre 0 dejaria la mitad sin pisar.
 */
static uint8_t s_dcf_fase;
static int     s_dcf_submuestras = 1;

void cons_dcf77_fase(uint8_t si) { s_dcf_fase = si ? 1u : 0u; }
void cons_dcf77_submuestras(int n) { s_dcf_submuestras = n; }

/*
 * CON -DDCF77_REAL ESTOS DOS NO SE DEFINEN - 30/10/2026.
 *
 * sim/msf_cadena.c enlaza el User/dcf77.c DE VERDAD detras de este
 * demod_am.c para medir la cadena ENTERA: del I/Q crudo del ADC a la
 * hora decodificada. Con los sucedaneos de aqui eso no se puede, porque
 * dcf77_feed() tira la muestra y solo la cuenta.
 *
 * Lo demas del fichero se queda igual: lo unico que cambia es quien pone
 * estos dos simbolos.
 */
#ifndef DCF77_REAL
uint8_t dcf77_activo(void) { return g_cons[CONS_DCF77].activo; }
void    dcf77_feed(float m) { anota(CONS_DCF77, (const void *)0, 1u); (void)m; }
#endif
/* Con DCF77_REAL los pone el dcf77.c de verdad, los dos: el banco enciende
 * el decodificador con dcf77_start() y eso ya hace que dcf77_activo()
 * conteste que si. */
#ifndef DCF77_REAL
float   dcf77_fase_iq(float i, float q) { (void)i; (void)q; return 0.0f; }
uint8_t dcf77_usa_fase(hora_emisora_t e) { (void)e; return s_dcf_fase; }
uint8_t dcf77_submuestras(hora_emisora_t e)
{ (void)e; return (uint8_t)s_dcf_submuestras; }
hora_emisora_t dcf77_emisora(void) { return (hora_emisora_t)0; }
#endif

/*
 * EL SUMIDERO DE AUDIO, QUE ES EL PUNTO DE OBSERVACION BUENO.
 *
 * gd32_i2s_stream_write_half() YA LLEVA LONGITUD, desde el 24/10/2026, y
 * precisamente porque lo que decia aqui se cumplio por segunda vez: la
 * cuenta del I2S y el tamaño del buffer del demodulador se desalinearon
 * -04/08/2026 y 23/10/2026, las dos contadas en gd32_i2s.h-. Lo que el
 * que llama DICE traer se guarda en g_i2s_tramas, y el banco lo compara
 * con lo que de verdad produce cada tasa.
 *
 * Aqui solo se guarda EL PUNTERO. Y con el puntero basta para mirar la
 * union s_via desde fuera: el camino ancho entrega &s_via.wfm.audio_out y
 * el estrecho &s_via.est.audio_out, que son dos sitios del MISMO objeto a
 * distancias conocidas. El banco compara las dos direcciones y comprueba
 * que la union solapa como dice que solapa, sin tocar demod_am.c y sin
 * copiarse su declaracion.
 */
static int16_t  s_cap[4096];
static uint32_t s_cap_n;
static uint32_t s_paso_ciclos;

void cons_captura_tramas(uint32_t tramas) { s_cap_n = tramas; }
void cons_paso_ciclos(uint32_t c) { s_paso_ciclos = c; }
const int16_t *cons_audio(void) { return s_cap; }

uint32_t g_i2s_tramas;    /* las que ha DICHO el que llama, para el banco */

void gd32_i2s_stream_write_half(const int16_t *stereo_frames, uint32_t n_frames)
{
    g_i2s_tramas = n_frames;
    anota(CONS_I2S, stereo_frames, s_cap_n);
    /*
     * Se copia lo que el banco DIGA que mide media, no lo que adivine este
     * fichero: la funcion de verdad tampoco lleva longitud -el modulo del
     * I2S tiene su propia cuenta, puesta por gd32_i2s_stream_reconfigure()-
     * y fue justamente ahi donde se rompio el 04/08/2026, cuando aquella
     * cuenta no se movio con el tamaño del buffer. Un tapon que se invente
     * la longitud estaria repitiendo el fallo que viene a vigilar.
     */
    if (s_cap_n && s_cap_n * 2u <= (uint32_t)(sizeof s_cap / sizeof s_cap[0])) {
        memcpy(s_cap, stereo_frames, s_cap_n * 2u * sizeof(int16_t));
    }
    /*
     * Y se mueve el contador de ciclos de mentira AQUI, que es justo antes
     * de que demod_am.c lo lea por segunda vez (lineas 2914 y 4080: el
     * `DWT->CYCCNT - cyc_start` va despues de esta llamada). Asi la resta
     * que publica demod_am_get_last_cycles() da un numero que el banco
     * conoce de antemano, y se puede comprobar que resta y no suma.
     */
    dwt_falso.CYCCNT += s_paso_ciclos;
}

void bip_mete(int16_t *estereo, uint32_t n, float fs_hz)
{ (void)fs_hz; anota(CONS_BIP, estereo, n); }

/*
 * EL BLOQUE CORROMPIDO. Lo pregunta demod_am.c al principio de cada
 * bloque; en la radio lo decide sdr_rx.c mirando el DMA. Aqui lo mueve el
 * banco, que es como se puede recorrer esa rama a proposito.
 */
static uint8_t s_corrupto;
void cons_bloque_corrupto(uint8_t si) { s_corrupto = si ? 1u : 0u; }
uint8_t sdr_rx_last_block_corrupted(void) { return s_corrupto; }

/* El contador de ciclos de mentira, declarado en hostinc/gd32f4xx.h. */
dwt_falso_t dwt_falso;

/*
 * La patilla de medida y su preparacion: aqui no hay patillas.
 *
 * gd32_pines.c ya define estas tres, pero ese fichero encamina los pines
 * al W25Q16 simulado y arrastra w25q_sim.c entero. Un banco del camino de
 * audio no tiene nada que ver con la flash, asi que se repiten vacias aqui
 * y no se enlaza aquel. Si algun dia un banco necesitara los dos a la vez,
 * el enlazador lo dira con un simbolo duplicado, que es como tiene que
 * avisar: a gritos y no en silencio.
 */
void gpio_bit_set(uint32_t port, uint32_t pin) { (void)port; (void)pin; }
void gpio_bit_reset(uint32_t port, uint32_t pin) { (void)port; (void)pin; }
void gpio_mode_set(uint32_t port, uint32_t mode, uint32_t pupd, uint32_t pin)
{ (void)port; (void)mode; (void)pupd; (void)pin; }
void gpio_output_options_set(uint32_t port, uint32_t otype, uint32_t speed,
                             uint32_t pin)
{ (void)port; (void)otype; (void)speed; (void)pin; }
void rcu_periph_clock_enable(uint32_t periph) { (void)periph; }
