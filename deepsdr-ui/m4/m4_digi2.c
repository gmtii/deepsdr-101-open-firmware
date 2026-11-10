/*
 * LO QUE CUESTAN LOS SEIS MODOS DE TRAMA, EN INSTRUCCIONES DE CORTEX-M4.
 *
 * EL PORQUE. m4_digi1.c midio los decodificadores de tonos -RTTY, NAVTEX,
 * DSC, PSK31, CW, JTTY-, que son todos la misma forma: una maquina barata
 * muestra a muestra a 12 kHz y nada mas. Los de aqui NO son esa forma, y
 * por tres motivos distintos que valia la pena separar:
 *
 *   1. DOS DE ELLOS NO VAN A 12 kHz. El APRS y el AIS cuelgan de la salida
 *      del discriminador de FM, o sea del audio EN CRUDO a la tasa de
 *      radiofrecuencia: 96.000 muestras por segundo, OCHO VECES mas que
 *      los de banda lateral. Una instruccion por muestra ahi cuesta ocho
 *      veces lo que cuesta en el RTTY, y eso no se ve mirando el bucle.
 *
 *   2. TRES DE ELLOS TIENEN TRABAJO DE BUCLE PRINCIPAL ADEMAS DEL DE
 *      MUESTRA, y es el caro: el espectrograma del WSPR -274 Goertzel de
 *      256 muestras por paso-, su decodificador de Fano al cerrar la
 *      ventana, y las dos transformadas de 1024 puntos del IDENT. Eso no
 *      sale en el "por muestra" y es lo que se lleva el tiempo.
 *
 *   3. UNO DE ELLOS CUESTA MAS SIN SEÑAL QUE CON ELLA. El ALE busca el
 *      principio de palabra UNA fase por simbolo mientras no esta
 *      enganchado, y deja de buscar en cuanto engancha. O sea que el caso
 *      caro es el que pasa el 99 % del tiempo: el modo puesto y el aire
 *      vacio.
 *
 * LAS TASAS, que son la mitad de la respuesta (ver demod_am.c):
 *
 *     APRS      ax25_process(s_env, 256)          96.000 muestras/s
 *     AIS       ais_modo_mete(s_env, 256)         96.000 muestras/s
 *     PKT300    ax25_process(s_ssb_dec, 32)       12.000 muestras/s
 *     WSPR      wspr_modo_mete(s_ssb_dec, 32)     12.000 muestras/s
 *     ALE       ale_modo_mete(s_ssb_dec, 32)      12.000 muestras/s
 *     IDENT     ident_modo_mete(s_ssb_dec, 32)    12.000 muestras/s
 *
 *     % de cada segundo = ins/muestra x tasa / 2.000.000
 *
 * A 96 kHz, DIEZ instrucciones por muestra son el 0,48 % de la CPU; a
 * 12 kHz son el 0,06 %. Es el mismo bucle y es ocho veces el precio.
 *
 * LA ENTRADA ES DE VERDAD. Nada se mide con ceros:
 *
 *     APRS y PKT300   tramas AX.25 montadas con ax25_fcs() -la del propio
 *                     firmware-, con relleno de HDLC, NRZI y banderas, y
 *                     moduladas en FSK de fase continua.
 *     AIS             rafagas GMSK con su preambulo de 24 bits, su CRC y
 *                     su pulso gaussiano de BT=0,4 sobre tres bits.
 *     WSPR            una emision entera de 110,6 s codificada con
 *                     wspr_empaqueta() y wspr_codifica(), con su ruido.
 *     ALE             palabras de sondeo pasadas por ale_golay_codifica(),
 *                     invertidas, entrelazadas, repetidas a nivel de bit y
 *                     mapeadas a los ocho tonos por Gray.
 *     IDENT           FSK de 170 Hz a 100 baudios, que es lo que el motor
 *                     sabe medir y lo que mas se oye en onda corta.
 *
 * Y se deja correr una pasada antes de cronometrar, para que los lazos de
 * reloj, las referencias de nivel y los enganches esten donde estan cuando
 * la radio lleva un rato escuchando.
 *
 * LO QUE NO SE MIDE AQUI, dicho antes de que nadie lo busque:
 *
 *   - wspr_modo_poll() COMPLETO contra el reloj de la radio. El ciclo de
 *     dos minutos lo manda el RTC, que aqui es un tapon; lo que se mide es
 *     lo que ese poll llama, que es donde esta todo el coste:
 *     wspr_rx_pasos_pendientes() y wspr_rx_decodifica().
 *   - El reparto de IDENT entre sus tres caminos NO sale de mirar dentro
 *     -cad_calcula() y stanag_det_paso() son privados-: sale de cronometrar
 *     cada vuelta de ident_modo_poll() por separado y repartirlas por
 *     coste. Las CUENTAS de cada grupo se imprimen para que se pueda
 *     comprobar que cuadran con las frecuencias esperadas.
 *
 * COMPILACION (desde deepsdr-ui; el orden de los -I importa, el CMSIS
 * delante de hostinc):
 *
 *   arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 \
 *     -mfloat-abi=hard -O2 -ffreestanding -ffunction-sections \
 *     -fdata-sections -nostartfiles -Wl,--gc-sections -Tm4/link.ld -Im4 \
 *     -DGD32F450 -DUSE_STDPERIPH_DRIVER -DHXTAL_VALUE=12288000U \
 *     -DDEBUG_UART_ENABLED=0 -DARM_MATH_DSP=1 -DARM_MATH_CM4 \
 *     -I../CMSIS/Include -I../CMSIS/DSP/Include -I../CMSIS/DSP/PrivateInclude \
 *     -Igfx2 -I../User -I../Ft8Lib -Ihostinc -Isim \
 *     m4/m4_digi2.c m4/start.c \
 *     ../User/ax25.c ../User/nco.c ../User/nco_tabla.c \
 *     ../User/ais_rx.c ../User/ais_msg.c ../User/ais_modo.c \
 *     ../User/wspr_rx.c ../User/wspr_msg.c ../User/wspr_fano.c \
 *     ../User/wspr_modo.c ../User/ale_rx.c ../User/ale_golay.c \
 *     ../User/ale_modo.c ../User/ident.c ../User/stanag_det.c \
 *     -o out/m4_digi2.elf -lc -lm -lgcc
 *
 *   qemu-system-arm -M mps2-an386 -cpu cortex-m4 -nographic \
 *     -semihosting-config enable=on,target=native -icount shift=0 \
 *     -kernel out/m4_digi2.elf
 *
 * Tarda cinco segundos y sale exactamente lo mismo las dos veces: con
 * -icount shift=0 el tiempo virtual avanza por instrucciones y aqui no
 * hay nada que dependa del reloj de pared.
 *
 * LA ULTIMA CIFRA BAILA ENTRE COMPILACIONES, Y NO ES RUIDO DE MEDIDA. El
 * SysTick cuenta de cuarenta en cuarenta instrucciones, asi que cada
 * cronometrada se redondea al tic y el resto depende de en que fase del
 * contador se empiece - o sea de cuanto codigo hay ANTES. Anadir una linea
 * de texto en un bloque mueve la ultima cifra del siguiente: medido, un
 * 0,01 % arriba o abajo. Entre dos corridas del MISMO binario no se mueve
 * ni un digito.
 */
#include "semi.h"
void pila_di(unsigned long maximo);

#include <math.h>
#include <string.h>

#include "ax25.h"
#include "ais_rx.h"
#include "ais_msg.h"
#include "ais_modo.h"
#include "wspr_rx.h"
#include "wspr_msg.h"
#include "wspr_modo.h"
#include "ale_rx.h"
#include "ale_golay.h"
#include "ale_modo.h"
#include "ident.h"
#include "stanag_det.h"
#include "rtc_hw.h"

int g_fallos;

/* ======================================================================
 * LOS TAPONES PARA ENLAZAR
 *
 * Van aqui dentro a proposito: este banco no toca User/ ni sim/ ni nada
 * mas. Son las cuatro cosas que estos seis modulos le piden a la radio y
 * que fuera de la radio no existen.
 * ====================================================================== */

/*
 * LA MEMORIA PRESTADA. En la radio son los 54 kB que se reparten FT8,
 * HFDL, WSPR, AIS, ALE, JTTY e IDENT -de uno en uno, nunca a la vez-. El
 * que mas pide es el WSPR: wspr_rx_ram_necesaria() son 47.056 del
 * espectrograma mas el estado del receptor, y encima los renglones. Aqui
 * se da de sobra y en .bss, que con (NOLOAD) no cuesta flash.
 *
 * Y se da DE UNO EN UNO igual que en la radio: cada bloque de medida
 * arranca su modo y lo para antes de que arranque el siguiente, asi que
 * los seis usan el mismo trozo y ninguno pisa al otro.
 */
static uint8_t s_prestamo[131072] __attribute__((aligned(8)));
uint8_t *hfdl_ram_todo(uint32_t *capacity_out)
{
    if (capacity_out) { *capacity_out = (uint32_t)sizeof s_prestamo; }
    return s_prestamo;
}
void hfdl_ram_coge(void)   { }
void hfdl_ram_suelta(void) { }

/* La cascada no la tiene nadie: aqui no hay cascada. */
uint8_t waterfall_prestada(void) { return 0U; }

/* El traductor de los renglones del IDENT. Aqui siempre en castellano. */
const char *tr(const char *es, const char *en) { (void)en; return es; }

/*
 * EL RELOJ. wspr_modo.c lo usa para abrir y cerrar la ventana de dos
 * minutos. Aqui no se mide ese ciclo -ver la cabecera-, asi que basta con
 * que conteste algo coherente: la hora la mueve el banco a mano cuando le
 * hace falta.
 */
static rtc_hw_datetime_t s_hora = { 2026U, 11U, 8U, 12U, 0U, 0U };
void rtc_hw_get(rtc_hw_datetime_t *out) { if (out) { *out = s_hora; } }
bool rtc_hw_has_ever_synced(void) { return true; }

/*
 * LOS AGUJEROS DE LA BIBLIOTECA C. ident.c arma sus renglones con
 * snprintf(), y eso arrastra la maquinaria reentrante de newlib, que pide
 * un sistema operativo debajo. Aqui no hay ninguno y no hace falta: estas
 * diez funciones existen solo para que el enlazador cierre. Si alguna se
 * llegase a llamar de verdad, devuelven error en vez de callarse.
 */
void *_sbrk(int incr)   { (void)incr; return (void *)-1; }
int   _write(int f, const char *p, int n) { (void)f; (void)p; (void)n; return -1; }
int   _read(int f, char *p, int n)        { (void)f; (void)p; (void)n; return -1; }
int   _close(int f)     { (void)f; return -1; }
int   _lseek(int f, int o, int w) { (void)f; (void)o; (void)w; return -1; }
int   _fstat(int f, void *st)     { (void)f; (void)st; return -1; }
int   _isatty(int f)    { (void)f; return 0; }
int   _kill(int pid, int sig)     { (void)pid; (void)sig; return -1; }
int   _getpid(void)     { return 1; }
void  _exit(int c)      { (void)c; for (;;) { } }

/* ======================================================================
 * LA MEDIDA
 * ====================================================================== */

/*
 * Instrucciones POR MUESTRA con dos decimales, igual que en m4_digi1.c y
 * por la misma razon: los numeros van de una a trescientas instrucciones
 * por muestra y redondeando a entero se pierden justo las diferencias que
 * interesan -si tener el modo puesto sin señal cuesta lo mismo que con
 * ella, por ejemplo-.
 *
 * Devuelve instrucciones x 100.
 */
static unsigned long mide_m(const char *que, unsigned long ciclos,
                            unsigned long n)
{
    unsigned long long t = (unsigned long long)ciclos * 4000ULL;
    unsigned long c = (unsigned long)(t / (unsigned long long)n);

    pr("    "); pr(que); pr("  ");
    prn(c / 100UL); pr(",");
    if ((c % 100UL) < 10UL) { pr("0"); }
    prn(c % 100UL);
    pr(" ins/muestra");
    return c;
}

/* Y lo que eso se lleva de cada segundo a la tasa a la que le entra. */
static void seg(unsigned long c100, unsigned long tasa)
{
    unsigned long long ins_s = (unsigned long long)c100 * (unsigned long long)tasa / 100ULL;
    unsigned long us   = (unsigned long)(ins_s / 200ULL);
    unsigned long cien = (unsigned long)(ins_s / 20000ULL);

    pr("   ->  "); prn((unsigned long)ins_s); pr(" ins/s = ");
    prn(us); pr(" us de cada segundo = ");
    prn(cien / 100UL); pr(",");
    if ((cien % 100UL) < 10UL) { pr("0"); }
    prn(cien % 100UL);
    pr(" %\n");
}

/* Un suceso suelto: instrucciones y microsegundos con dos decimales. */
static unsigned long mide_s(const char *que, unsigned long ciclos,
                            unsigned long n)
{
    unsigned long long t = (unsigned long long)ciclos * 40ULL;
    unsigned long ins = (n ? (unsigned long)(t / (unsigned long long)n) : 0UL);
    unsigned long us100 = ins / 2UL;

    pr("    "); pr(que); pr("  ");
    prn(ins); pr(" ins = ");
    prn(us100 / 100UL); pr(",");
    if ((us100 % 100UL) < 10UL) { pr("0"); }
    prn(us100 % 100UL);
    pr(" us\n");
    /*
     * Y QUE SE SEPA SI EL CONTADOR HA DADO LA VUELTA. El SysTick cuenta
     * 24 bits -16.777.216 tics, o sea 671 millones de instrucciones- y
     * aqui hay UN suceso que se acerca: el decodificador de WSPR cuando no
     * encuentra nada. Si alguna vez lo pasa, el numero que sale es menor
     * que el de verdad y no hay nada en la pantalla que lo diga. Esto lo
     * dice. No es un tope: no pone nada rojo, avisa.
     */
    if (n == 1UL && ciclos > 15000000UL) {
        pr("      ojo: "); prn(ciclos);
        pr(" tics de 16.777.216; cerca de la vuelta del SysTick\n");
    }
    return ins;
}

/* Lo que se lleva de cada segundo algo que pasa `veces` veces por segundo.
 * `veces` va en MILESIMAS de vez por segundo, que es lo que hace falta para
 * un decodificador de WSPR que corre una vez cada dos minutos. */
static void seg_ev(unsigned long ins, unsigned long milesimas_s)
{
    unsigned long long ins_s = (unsigned long long)ins
                             * (unsigned long long)milesimas_s / 1000ULL;
    unsigned long cien = (unsigned long)(ins_s / 20000ULL);

    pr("      -> ");
    prn((unsigned long)ins_s); pr(" ins/s = ");
    prn(cien / 100UL); pr(",");
    if ((cien % 100UL) < 10UL) { pr("0"); }
    prn(cien % 100UL);
    pr(" % de cada segundo\n");
}

/* ----------------------------------------------------------- el resumen */
/*
 * Cada bloque apunta aqui lo suyo y main() lo saca ordenado de mayor a
 * menor, que es como se mira: lo primero que hay que contestar es "cual de
 * estos me quita mas radio", y eso con los numeros repartidos por seis
 * pantallas no se ve.
 *
 * Se guarda en MILESIMAS DE TANTO POR CIENTO del segundo, no en
 * instrucciones por muestra: aqui conviven tasas de 12 y de 96 kHz y
 * comparar "ins/muestra" entre las dos no dice nada.
 */
#define RES_MAX 16U
typedef struct {
    const char   *que;
    unsigned long tasa;      /* muestras por segundo a las que le entra */
    unsigned long sin_mil;   /* milesimas de % del segundo, sin señal */
    unsigned long con_mil;   /* idem, con señal */
} res_t;
static res_t   s_res[RES_MAX];
static unsigned s_res_n;

static unsigned long mil_de(unsigned long c100, unsigned long tasa)
{
    unsigned long long ins_s = (unsigned long long)c100 * (unsigned long long)tasa / 100ULL;
    return (unsigned long)(ins_s / 2000ULL);   /* % x 1000 */
}

static void apunta_res(const char *que, unsigned long tasa,
                       unsigned long sin_mil, unsigned long con_mil)
{
    if (s_res_n < RES_MAX) {
        s_res[s_res_n].que = que;
        s_res[s_res_n].tasa = tasa;
        s_res[s_res_n].sin_mil = sin_mil;
        s_res[s_res_n].con_mil = con_mil;
        s_res_n++;
    }
}

static void pc3(unsigned long mil)
{
    unsigned long ent = mil / 1000UL;

    if (ent < 100UL) { pr(" "); }
    if (ent < 10UL)  { pr(" "); }
    prn(ent); pr(",");
    prn((mil / 100UL) % 10UL);
    prn((mil / 10UL) % 10UL);
    prn(mil % 10UL);
    pr(" %");
}

static void resumen(void)
{
    unsigned i, j;

    pr("\n  RESUMEN, DE MAYOR A MENOR (lo que se lleva de CADA SEGUNDO)\n\n");
    pr("                                  tasa      sin señal      con señal\n");
    for (i = 0U; i < s_res_n; i++) {
        unsigned mejor = i;
        for (j = i + 1U; j < s_res_n; j++) {
            unsigned long a = (s_res[j].con_mil > s_res[j].sin_mil)
                            ? s_res[j].con_mil : s_res[j].sin_mil;
            unsigned long b = (s_res[mejor].con_mil > s_res[mejor].sin_mil)
                            ? s_res[mejor].con_mil : s_res[mejor].sin_mil;
            if (a > b) { mejor = j; }
        }
        if (mejor != i) {
            res_t t = s_res[i]; s_res[i] = s_res[mejor]; s_res[mejor] = t;
        }
        pr("    "); pr(s_res[i].que); pr("  ");
        if (s_res[i].tasa >= 10000UL) {
            prn(s_res[i].tasa / 1000UL); pr(" kHz  ");
        } else if (s_res[i].tasa > 0UL) {
            prn(s_res[i].tasa); pr(" Hz   ");
        } else {
            pr("   --   ");   /* no va por muestra: va por vuelta o por ranura */
        }
        pc3(s_res[i].sin_mil);
        pr("     ");
        pc3(s_res[i].con_mil);
        pr("\n");
    }
}

/* ======================================================================
 * LA SEÑAL
 * ====================================================================== */

static uint32_t s_rnd;
static void rnd_pon(uint32_t s) { s_rnd = s; }
static float uni(void)
{
    s_rnd = s_rnd * 1664525u + 1013904223u;
    return (float)((s_rnd >> 9) & 0x7FFFFFu) * (1.0f / 8388608.0f) - 0.5f;
}
/* Cuatro uniformes sumadas: campana de sobra para lo que esto mide, y
 * reproducible, que es lo que hace falta para correr dos veces y comparar. */
static float ruido(void) { return (uni() + uni() + uni() + uni()) * 0.6f; }

/*
 * EL BUFFER GRANDE. 131.072 muestras son 1,365 s a 96 kHz -tres tramas de
 * APRS- y 10,9 s a 12 kHz -seis de PKT300, veintisiete palabras de ALE-.
 * Son 512 kB de .bss y aqui sobran; en la radio no cabrian, pero la radio
 * no necesita guardar audio: lo procesa segun llega.
 */
#define NBUF  131072U
static float s_buf[NBUF];

static void gen_ruido(float amp)
{
    unsigned i;
    for (i = 0U; i < NBUF; i++) { s_buf[i] = amp * ruido(); }
}

/* ----------------------------------------------------- tramas AX.25 */
/*
 * El transmisor de paquete, igual que el de sim/ax25test.c: direcciones
 * corridas un bit, comprobacion con ax25_fcs() -la del propio firmware, no
 * una copia, para que una trama mal montada y un decodificador mal montado
 * no se cancelen-, relleno de HDLC, NRZI y banderas.
 *
 * Aqui no se transmite muestra a muestra como alli: primero se saca la
 * lista de NIVELES NRZI -uno por periodo de bit- y despues se modula de una
 * vez. Es la misma señal y deja el buffer entero disponible para
 * cronometrar sobre el varias veces sin volver a fabricarlo.
 */
#define NNIV 8192U
static uint8_t  s_niv[NNIV];
static unsigned s_nniv;
static int      s_nrzi, s_unos;

static void niv_bit(int bit)
{
    if (!bit) { s_nrzi = !s_nrzi; }          /* NRZI: un cero CAMBIA */
    if (s_nniv < NNIV) { s_niv[s_nniv++] = (uint8_t)s_nrzi; }
}
static void niv_dato(int bit)
{
    niv_bit(bit);
    if (bit) { s_unos++; if (s_unos == 5) { niv_bit(0); s_unos = 0; } }
    else     { s_unos = 0; }
}
static void niv_bandera(void)
{
    int i;
    niv_bit(0);
    for (i = 0; i < 6; i++) { niv_bit(1); }
    niv_bit(0);
    s_unos = 0;
}

/* Una direccion AX.25: seis caracteres corridos un bit y el SSID. */
static void ax_dir(uint8_t *d, const char *ind, int ssid, int ultima)
{
    int i, fin = 0;
    for (i = 0; i < 6; i++) {
        char c;
        if (!fin && ind[i] == '\0') { fin = 1; }
        c = fin ? ' ' : ind[i];
        d[i] = (uint8_t)((uint8_t)c << 1);
    }
    d[6] = (uint8_t)(0x60U | (uint8_t)((ssid & 0x0F) << 1) | (ultima ? 1U : 0U));
}

static uint8_t  s_trama[AX25_TRAMA_MAX];
static uint16_t s_ntrama;

static void ax_monta(const char *info)
{
    uint16_t n = 0U;
    uint16_t fcs;

    ax_dir(&s_trama[n], "APRS", 0, 0); n = (uint16_t)(n + 7U);
    ax_dir(&s_trama[n], "EA4HEW", 9, 0); n = (uint16_t)(n + 7U);
    ax_dir(&s_trama[n], "WIDE1", 1, 1); n = (uint16_t)(n + 7U);
    s_trama[n++] = 0x03U;                  /* UI */
    s_trama[n++] = 0xF0U;                  /* sin capa 3 */
    while (*info) { s_trama[n++] = (uint8_t)*info++; }

    fcs = ax25_fcs(s_trama, n);
    /* Se transmite complementado y con el byte bajo primero: asi la trama
     * entera pasada por el molino da siempre la misma constante. */
    fcs = (uint16_t)(fcs ^ 0xFFFFU);
    s_trama[n++] = (uint8_t)(fcs & 0xFFU);
    s_trama[n++] = (uint8_t)(fcs >> 8);
    s_ntrama = n;
}

/* Niveles NRZI de una trama con sus banderas delante y detras. */
static void ax_niveles(void)
{
    uint16_t k;
    int i, b;

    s_nniv = 0U; s_nrzi = 0; s_unos = 0;
    for (i = 0; i < 8; i++) { niv_bandera(); }   /* la cabecera que manda un TNC */
    s_unos = 0;
    for (k = 0U; k < s_ntrama; k++) {
        for (b = 0; b < 8; b++) { niv_dato((s_trama[k] >> b) & 1); }
    }
    for (i = 0; i < 4; i++) { niv_bandera(); }
}

/*
 * Y el audio: FSK de fase continua, con los niveles repitiendose hasta
 * llenar el buffer. Un canal de paquete ocupado es exactamente esto -una
 * trama detras de otra- y es el caso que hay que medir: el barato ya se
 * mide con ruido.
 */
static void ax_modula(float fs, float baud, float marca, float espacio,
                      float amp, float nz)
{
    unsigned i;
    float fase = 0.0f;
    float k = baud / fs;

    for (i = 0U; i < NBUF; i++) {
        unsigned ib = (unsigned)((float)i * k) % s_nniv;
        float f = s_niv[ib] ? espacio : marca;

        fase += 6.2831853f * f / fs;
        if (fase > 6.2831853f) { fase -= 6.2831853f; }
        s_buf[i] = amp * sinf(fase) + nz * ruido();
    }
}

/* ------------------------------------------------------- rafagas AIS */
/*
 * Un informe de posicion de tipo 1, su CRC, el relleno de HDLC, las dos
 * banderas, el preambulo de 24 bits y el pulso gaussiano de BT=0,4 sobre
 * tres bits. Misma receta que sim/ais_aire.c, que es la que el banco del
 * simulador ya usa para comprobar que el decodificador acierta.
 */
#define AIS_SPB   10U            /* 96.000 / 9.600 = 10 clavadas */
#define AIS_NBIT  512U
static uint8_t s_abits[AIS_NBIT];

static void ais_pon(uint8_t *b, uint16_t p, uint8_t n, uint32_t v)
{
    uint8_t i;
    for (i = 0U; i < n; i++) {
        uint16_t k = (uint16_t)(p + i);
        uint32_t bit = (v >> (n - 1U - i)) & 1U;
        if (bit) { b[k >> 3] = (uint8_t)(b[k >> 3] |  (0x80U >> (k & 7U))); }
        else     { b[k >> 3] = (uint8_t)(b[k >> 3] & ~(0x80U >> (k & 7U))); }
    }
}
static uint8_t ais_lee(const uint8_t *b, uint16_t k)
{
    return (uint8_t)((b[k >> 3] >> (7U - (k & 7U))) & 1U);
}
static uint16_t ais_fcs(const uint8_t *bits, uint16_t n)
{
    uint16_t crc = 0xFFFFU, i;
    for (i = 0U; i < n; i++) {
        uint16_t x = (uint16_t)((crc ^ ais_lee(bits, i)) & 1U);
        crc >>= 1;
        if (x) { crc ^= 0x8408U; }
    }
    return (uint16_t)~crc;
}

/* La rafaga en bits sueltos (un bit por byte). Devuelve cuantos. */
static uint16_t ais_rafaga(uint8_t *sal, uint32_t mmsi, int lat_e4, int lon_e4)
{
    static uint8_t msg[32];
    uint16_t n = 0U, i, f;
    uint8_t  unos = 0U;

    memset(msg, 0, sizeof msg);
    ais_pon(msg,   0U, 6U, 1U);                 /* tipo 1 */
    ais_pon(msg,   8U, 30U, mmsi);
    ais_pon(msg,  42U, 8U, 0x80U);              /* giro: no disponible */
    ais_pon(msg,  50U, 10U, 123U);              /* 12,3 nudos */
    ais_pon(msg,  60U, 1U, 1U);
    ais_pon(msg,  61U, 28U, (uint32_t)(int32_t)(lon_e4 * 60));
    ais_pon(msg,  89U, 27U, (uint32_t)(int32_t)(lat_e4 * 60));
    ais_pon(msg, 116U, 12U, 900U);              /* rumbo 90,0 */
    ais_pon(msg, 128U, 9U, 511U);
    ais_pon(msg, 137U, 6U, 30U);
    f = ais_fcs(msg, 168U);

    for (i = 0U; i < 24U; i++) { sal[n++] = (uint8_t)(i & 1U); }     /* preambulo */
    for (i = 0U; i < 8U; i++)  { sal[n++] = (uint8_t)((0x7EU >> i) & 1U); }
    for (i = 0U; i < 184U; i++) {
        uint8_t b = (i < 168U) ? ais_lee(msg, i)
                               : (uint8_t)((f >> (i - 168U)) & 1U);
        sal[n++] = b;
        if (b) { unos++; if (unos == 5U) { sal[n++] = 0U; unos = 0U; } }
        else   { unos = 0U; }
    }
    for (i = 0U; i < 8U; i++)  { sal[n++] = (uint8_t)((0x7EU >> i) & 1U); }
    for (i = 0U; i < 24U; i++) { sal[n++] = (uint8_t)(i & 1U); }
    return n;
}

/*
 * Y de bits a discriminador: NRZI y el pulso gaussiano. El discriminador
 * de FM devuelve la desviacion instantanea, asi que lo que sale de aqui es
 * directamente lo que ais_rx_mete() espera.
 */
static void ais_modula(float *dst, const uint8_t *bits, uint16_t n, float nz)
{
    static float nrz[AIS_NBIT * AIS_SPB + 64U];
    static float g[3U * AIS_SPB];
    uint32_t i, k, m = 0U;
    int nivel = 0;
    float suma = 0.0f;
    float s = 0.83255f / 0.4f;      /* sqrt(ln2)/(2*pi*BT), en periodos de bit */

    s = sqrtf(logf(2.0f)) / (2.0f * 3.14159265f * 0.4f);
    for (i = 0U; i < n; i++) {
        if (!bits[i]) { nivel = !nivel; }
        for (k = 0U; k < AIS_SPB; k++) { nrz[m++] = nivel ? 1.0f : -1.0f; }
    }
    for (i = 0U; i < 3U * AIS_SPB; i++) {
        float t = ((float)i - (3.0f * AIS_SPB - 1.0f) / 2.0f) / (float)AIS_SPB;
        g[i] = expf(-(t * t) / (2.0f * s * s));
        suma += g[i];
    }
    for (i = 0U; i < 3U * AIS_SPB; i++) { g[i] /= suma; }

    for (i = 0U; i < m; i++) {
        float y = 0.0f;
        uint32_t j;
        for (j = 0U; j < 3U * AIS_SPB; j++) {
            int32_t p = (int32_t)i - (int32_t)j + (3 * (int32_t)AIS_SPB) / 2;
            if ((p >= 0) && (p < (int32_t)m)) { y += g[j] * nrz[p]; }
        }
        dst[i] = y + nz * ruido();
    }
}

/* El buffer entero lleno de rafagas con hueco entre ellas, que es lo que
 * se oye en un puerto. Devuelve cuantas ha puesto. */
static unsigned ais_llena(float nz)
{
    unsigned pos = 0U, cuantas = 0U;
    uint32_t mmsi = 224000001UL;

    gen_ruido(nz);
    for (;;) {
        uint16_t nb = ais_rafaga(s_abits, mmsi, 433700 + (int)cuantas * 37,
                                 -34100 - (int)cuantas * 53);
        unsigned nm = (unsigned)nb * AIS_SPB;

        if (pos + nm + 2048U >= NBUF) { break; }
        ais_modula(&s_buf[pos], s_abits, nb, nz);
        pos += nm + 2048U;     /* hueco: las rafagas no van pegadas */
        cuantas++;
        mmsi++;
    }
    return cuantas;
}

/* ------------------------------------------------------ palabras ALE */
#define ALE_NSIM 196U            /* cuatro palabras de 49 simbolos */
#define ALE_SPS  96U
static const uint8_t k_gray8[8] = { 0U, 1U, 3U, 2U, 6U, 7U, 5U, 4U };
static const float   k_ale_hz[8] = { 750.0f, 1000.0f, 1250.0f, 1500.0f,
                                     1750.0f, 2000.0f, 2250.0f, 2500.0f };
static uint8_t s_asim[ALE_NSIM];

static uint8_t ale_rango(uint8_t sim)
{
    uint8_t r;
    for (r = 0U; r < 8U; r++) { if (k_gray8[r] == sim) { return r; } }
    return 0U;
}

/* Una palabra de 24 bits -> 49 simbolos, con las siete convenciones. */
static void ale_pal_sim(uint32_t w24, uint8_t *sim, unsigned nsim)
{
    uint8_t  tx[49];
    uint32_t A = ale_golay_codifica((uint16_t)((w24 >> 12) & 0xFFFU));
    uint32_t B = ale_golay_codifica((uint16_t)(w24 & 0xFFFU));
    unsigned i;
    int k;

    B ^= 0x00000FFFUL;      /* la paridad de la mitad B viaja invertida */
    for (k = 0; k < 24; k++) {
        tx[2 * k]      = (uint8_t)((A >> (23 - k)) & 1U);
        tx[2 * k + 1]  = (uint8_t)((B >> (23 - k)) & 1U);
    }
    tx[48] = 0U;            /* S49, el relleno */
    /* Tres copias a nivel de BIT, no de simbolo: aire[i] = tx[i % 49]. */
    for (i = 0U; i < nsim; i++) {
        sim[i] = (uint8_t)((tx[(3U * i)      % 49U] << 2) |
                           (tx[(3U * i + 1U) % 49U] << 1) |
                           (tx[(3U * i + 2U) % 49U]));
    }
}

static uint32_t ale_palabra(uint8_t pre, const char *t)
{
    return (((uint32_t)pre & 7U) << 21) |
           (((uint32_t)(uint8_t)t[0] & 0x7FU) << 14) |
           (((uint32_t)(uint8_t)t[1] & 0x7FU) << 7) |
           ((uint32_t)(uint8_t)t[2] & 0x7FU);
}

/* El buffer entero lleno de sondeo: 8-FSK de fase continua a 125 baudios. */
static void ale_llena(float amp, float nz)
{
    unsigned i, s = 0U;
    float fase = 0.0f;

    ale_pal_sim(ale_palabra(2U, "EA4"), s_asim, ALE_NSIM);
    for (i = 0U; i < NBUF; i++) {
        float f;

        s = (i / ALE_SPS) % ALE_NSIM;
        f = k_ale_hz[ale_rango(s_asim[s])];
        fase += 6.2831853f * f / 12000.0f;
        if (fase > 6.2831853f) { fase -= 6.2831853f; }
        s_buf[i] = amp * sinf(fase) + nz * ruido();
    }
}

/* ======================================================================
 * EL BANCO
 * ====================================================================== */

/* Mete el buffer entero en trozos de `t` muestras, que es como se lo pasa
 * demod_am.c: 256 desde el discriminador, 32 desde la banda lateral. */
static unsigned long mete_ax25(unsigned t)
{
    unsigned i;
    unsigned long a, b, acu = 0UL;
    for (i = 0U; i + t <= NBUF; i += t) {
        a = tic(); ax25_process(&s_buf[i], t); b = tic();
        acu += (a - b) & 0xFFFFFFUL;
    }
    return acu;
}
static unsigned long mete_ais(unsigned t)
{
    unsigned i;
    unsigned long a, b, acu = 0UL;
    for (i = 0U; i + t <= NBUF; i += t) {
        a = tic(); ais_modo_mete(&s_buf[i], (uint16_t)t); b = tic();
        acu += (a - b) & 0xFFFFFFUL;
    }
    return acu;
}
static unsigned long mete_ale(unsigned t)
{
    unsigned i;
    unsigned long a, b, acu = 0UL;
    for (i = 0U; i + t <= NBUF; i += t) {
        a = tic(); ale_modo_mete(&s_buf[i], (uint16_t)t); b = tic();
        acu += (a - b) & 0xFFFFFFUL;
    }
    return acu;
}
static unsigned long mete_ident(unsigned t)
{
    unsigned i;
    unsigned long a, b, acu = 0UL;
    for (i = 0U; i + t <= NBUF; i += t) {
        a = tic(); ident_modo_mete(&s_buf[i], (uint16_t)t); b = tic();
        acu += (a - b) & 0xFFFFFFUL;
    }
    return acu;
}

static unsigned vacia_ax25(void)
{
    char c; unsigned n = 0U;
    while (ax25_get_char(&c)) { n++; }
    return n;
}

/* ---------------------------------------------------------------- AX.25 */
static void banco_ax25(void)
{
    ax25_info_t inf;
    unsigned long v, sin96, con96, sin12, con12;
    unsigned long a, b;
    int r;

    pr("\n  1. AX.25 - APRS a 1200 baudios (del discriminador de FM)\n");
    pr("     ax25.c + nco.c, bloques de 256 muestras a 96 kHz\n\n");

    ax_monta("!4025.12N/00341.45W>Prueba de carga del M4");
    ax_niveles();

    /* --- sin señal: el modo puesto y el aire vacio --- */
    rnd_pon(1234u);
    gen_ruido(0.30f);
    ax25_start(96000.0f);
    (void)mete_ax25(256U);              /* una pasada para asentar */
    (void)vacia_ax25();
    v = mete_ax25(256U);
    sin96 = mide_m("APRS, SOLO RUIDO            ", v, NBUF);
    seg(sin96, 96000UL);

    /* --- con señal --- */
    rnd_pon(1234u);
    ax_modula(96000.0f, 1200.0f, AX25_MARCA_HZ, AX25_ESPACIO_HZ, 1.0f, 0.15f);
    ax25_start(96000.0f);
    (void)mete_ax25(256U);
    (void)vacia_ax25();
    v = mete_ax25(256U);
    con96 = mide_m("APRS, CON TRAMAS            ", v, NBUF);
    seg(con96, 96000UL);
    ax25_info(&inf);
    pr("      tramas buenas "); prn(inf.tramas_ok);
    pr("  malas "); prn(inf.tramas_mal);
    pr("  banderas "); prn(inf.banderas);
    pr("  caracteres sacados "); prn(vacia_ax25()); pr("\n");
    apunta_res("APRS 1200 bd   x muestra  ", 96000UL,
                   mil_de(sin96, 96000UL), mil_de(con96, 96000UL));

    /*
     * Y EL CAMINO DIRECTO DE 9.600, QUE ES EL MISMO FICHERO POR OTRA
     * PUERTA - 09/11/2026.
     *
     * No hay dos correladores de tono: hay un seguidor de continua, una
     * media movil de un bit y el mismo lazo de reloj. O sea que tiene que
     * salir MUCHO mas barato por muestra que el de 1200, aunque corra a la
     * misma tasa. Esto lo dice con un numero.
     *
     * Se mide con ruido y no con tramas a proposito: lo que interesa es lo
     * que cuesta TENERLO PUESTO, que es lo que hace la radio el 99% del
     * tiempo. Con trama encima cuesta lo mismo: el trabajo por muestra no
     * depende de lo que traiga la senal, y eso tambien se mide.
     */
    {
        unsigned long g_sin, g_con;

        rnd_pon(1234u);
        gen_ruido(0.30f);
        if (!ax25_start_g3ruh(96000.0f)) {
            pr("    *** ax25_start_g3ruh(96000) dice que no: no mido ***\n");
            g_fallos++;
        } else {
            (void)mete_ax25(256U);
            (void)vacia_ax25();
            v = mete_ax25(256U);
            g_sin = mide_m("9600 G3RUH, SOLO RUIDO      ", v, NBUF);
            seg(g_sin, 96000UL);

            /* Con senal encima. No es G3RUH de verdad -no hay
             * aleatorizador aqui- y da igual: lo que se mide es el coste
             * por muestra, que no mira lo que traen los bits. */
            rnd_pon(1234u);
            ax_modula(96000.0f, 9600.0f, 3000.0f, -3000.0f, 1.0f, 0.15f);
            (void)ax25_start_g3ruh(96000.0f);
            (void)mete_ax25(256U);
            (void)vacia_ax25();
            v = mete_ax25(256U);
            g_con = mide_m("9600 G3RUH, CON SENAL       ", v, NBUF);
            seg(g_con, 96000UL);

            apunta_res("9600 G3RUH     x muestra  ", 96000UL,
                       mil_de(g_sin, 96000UL), mil_de(g_con, 96000UL));

            /* Y cuanto mas barato sale que el de dos tonos, en decimas,
             * que es el numero que contesta "¿cabe?" de un vistazo. */
            if (g_sin > 0UL) {
                pr("      sale ");
                prn((sin96 * 10UL) / g_sin);
                pr(" decimas de veces lo que cuesta el de 1200\n");
            }
        }
    }
    /*
     * Y DE DONDE SALEN ESAS 156 INSTRUCCIONES, que es lo unico que sirve
     * para bajarlas: de las DOS llamadas a nco_paso() que ax25_process()
     * hace por muestra, una por tono. Desensamblada, nco_paso() son 26
     * instrucciones, y catorce de ellas no son la cuenta: el push y el pop
     * del marco, y los dos vstr que escriben el seno y el coseno en
     * memoria para devolverlos por puntero.
     *
     * nco.h ya tiene la misma cuenta EN LINEA -nco_sen_cos_fase(), con su
     * comentario explicando que a una muestra por interrupcion el prologo
     * cuesta tanto como la cuenta-, y ax25.c no la usa: usa nco_paso() de
     * nco.c, que es la version con llamada. No se toca nada aqui, que esto
     * es un banco; queda apuntado con el numero delante.
     */
    pr("      de esas, ~52 son las DOS llamadas a nco_paso() por muestra\n");
    pr("      (26 instrucciones cada una, catorce de marco y de vstr)\n");

    /* --- y a 48 kHz, que es la otra tasa que puede tener la radio --- */
    rnd_pon(1234u);
    ax_modula(48000.0f, 1200.0f, AX25_MARCA_HZ, AX25_ESPACIO_HZ, 1.0f, 0.15f);
    ax25_start(48000.0f);
    (void)mete_ax25(128U);
    (void)vacia_ax25();
    v = mete_ax25(128U);
    v = mide_m("APRS a 48 kHz, con tramas   ", v, NBUF);
    seg(v, 48000UL);
    (void)vacia_ax25();

    pr("\n  2. AX.25 - PKT300 a 300 baudios (de la banda lateral)\n");
    pr("     el MISMO ax25.c con otros tres numeros, bloques de 32 a 12 kHz\n\n");

    rnd_pon(1234u);
    gen_ruido(0.30f);
    ax25_start_fsk(PKT_HF_FS_HZ, PKT_HF_BAUD, PKT_HF_MARCA_HZ, PKT_HF_ESPACIO_HZ);
    (void)mete_ax25(32U);
    (void)vacia_ax25();
    v = mete_ax25(32U);
    sin12 = mide_m("PKT300, SOLO RUIDO          ", v, NBUF);
    seg(sin12, 12000UL);

    rnd_pon(1234u);
    ax_modula(PKT_HF_FS_HZ, PKT_HF_BAUD, PKT_HF_MARCA_HZ, PKT_HF_ESPACIO_HZ, 1.0f, 0.15f);
    ax25_start_fsk(PKT_HF_FS_HZ, PKT_HF_BAUD, PKT_HF_MARCA_HZ, PKT_HF_ESPACIO_HZ);
    (void)mete_ax25(32U);
    (void)vacia_ax25();
    v = mete_ax25(32U);
    con12 = mide_m("PKT300, CON TRAMAS          ", v, NBUF);
    seg(con12, 12000UL);
    ax25_info(&inf);
    pr("      tramas buenas "); prn(inf.tramas_ok);
    pr("  malas "); prn(inf.tramas_mal);
    pr("  banderas "); prn(inf.banderas);
    pr("  caracteres sacados "); prn(vacia_ax25()); pr("\n");
    apunta_res("PKT300 300 bd  x muestra  ", 12000UL,
                   mil_de(sin12, 12000UL), mil_de(con12, 12000UL));

    /*
     * EL SUCESO CARO: CERRAR UNA TRAMA.
     *
     * Son dos cosas y las dos estan en trama_fin(): la comprobacion sobre
     * la trama ENTERA -ax25_fcs(), bit a bit, ocho vueltas por byte- y el
     * formateo del texto -ax25_trama_dbg(), que es la misma trama_texto()
     * que llama el decodificador-. Se cronometran las dos juntas porque
     * juntas es como pasan, y pasan DENTRO de la interrupcion del audio.
     */
    pr("\n     el suceso caro: CERRAR UNA TRAMA (dentro de la interrupcion)\n");
    ax25_start(96000.0f);
    a = tic();
    for (r = 0; r < 200; r++) { (void)ax25_fcs(s_trama, s_ntrama); }
    b = tic();
    (void)mide_s("la comprobacion de 51 bytes ", (a - b) & 0xFFFFFFUL, 200UL);
    a = tic();
    for (r = 0; r < 200; r++) { ax25_trama_dbg(s_trama, s_ntrama); (void)vacia_ax25(); }
    b = tic();
    pr("      (el vaciado del anillo no se cuenta aparte: va dentro)\n");
    (void)mide_s("y el texto de esa trama     ", (a - b) & 0xFFFFFFUL, 200UL);
    ax25_stop();
}

/* ------------------------------------------------------------------ AIS */
static void banco_ais(void)
{
    ais_rx_info_t inf;
    unsigned long v, sin96, con96;
    unsigned long a, b, acu;
    unsigned cuantas, r;

    pr("\n  3. AIS - GMSK a 9600 bits/s (del discriminador de FM)\n");
    pr("     ais_rx.c + ais_msg.c + ais_modo.c, bloques de 256 a 96 kHz\n\n");

    ais_modo_tasa_pon(96000.0f);
    if (!ais_modo_start()) {
        pr("    *** no arranca: no hay memoria prestada, no se mide ***\n");
        return;
    }

    rnd_pon(777u);
    gen_ruido(0.30f);
    (void)mete_ais(256U);
    ais_modo_poll();
    v = mete_ais(256U);
    sin96 = mide_m("AIS, SOLO RUIDO             ", v, NBUF);
    seg(sin96, 96000UL);

    rnd_pon(777u);
    cuantas = ais_llena(0.08f);
    ais_modo_borra();
    ais_rx_cuentas_cero();
    (void)mete_ais(256U);
    ais_modo_poll();
    v = mete_ais(256U);
    con96 = mide_m("AIS, CON RAFAGAS            ", v, NBUF);
    seg(con96, 96000UL);
    ais_rx_info(&inf);
    pr("      "); prn(cuantas); pr(" rafagas metidas, banderas "); prn(inf.banderas);
    pr(", tramas cerradas "); prn(inf.tramas);
    pr(", con el CRC bueno "); prn(inf.buenas); pr("\n");
    apunta_res("AIS GMSK       x muestra  ", 96000UL,
                   mil_de(sin96, 96000UL), mil_de(con96, 96000UL));

    /*
     * EL SUCESO CARO: ais_modo_poll(), que es lo unico de AIS que corre en
     * el bucle principal. Saca del anillo hasta cuatro tramas, las
     * desempaqueta campo a campo -seis bits cada uno- y actualiza la tabla
     * de barcos. Se mide con el anillo LLENO (el caso que importa) y
     * VACIO (el que pasa el 99 % de las vueltas).
     */
    pr("\n     el suceso caro: ais_modo_poll() desde el bucle principal\n");
    acu = 0UL;
    {
        unsigned long n = 0UL;
        /* Se vuelve a meter la señal para que el anillo tenga tramas, y se
         * cronometra SOLO el poll. */
        rnd_pon(777u);
        (void)ais_llena(0.08f);
        ais_modo_borra();
        {
            unsigned i;
            for (i = 0U; i + 256U <= NBUF; i += 256U) {
                ais_modo_mete(&s_buf[i], 256U);
                if (((i / 256U) % 6U) == 0U) {
                    a = tic(); ais_modo_poll(); b = tic();
                    acu += (a - b) & 0xFFFFFFUL;
                    n++;
                }
            }
        }
        (void)mide_s("una vuelta de poll, media   ", acu, n);
        pr("      sobre "); prn(n); pr(" vueltas, con "); prn(ais_modo_barcos());
        pr(" barcos en la tabla y "); prn(ais_modo_tramas()); pr(" tramas buenas\n");
    }
    a = tic();
    for (r = 0U; r < 1000U; r++) { ais_modo_poll(); }
    b = tic();
    (void)mide_s("una vuelta con el anillo vacio", (a - b) & 0xFFFFFFUL, 1000UL);
    seg_ev(((unsigned long long)((a - b) & 0xFFFFFFUL) * 40ULL) / 1000ULL, 62500UL);

    ais_modo_stop();
}

/* ------------------------------------------------------------------ ALE */
static void banco_ale(void)
{
    ale_rx_info_t inf;
    unsigned long v, sinS, conS;
    unsigned long a, b;
    unsigned r;

    pr("\n  4. ALE 2G - 8-FSK a 125 baudios (de la banda lateral)\n");
    pr("     ale_rx.c + ale_golay.c + ale_modo.c, bloques de 32 a 12 kHz\n\n");

    if (!ale_modo_start()) {
        pr("    *** no arranca: no hay memoria prestada, no se mide ***\n");
        return;
    }

    rnd_pon(4242u);
    gen_ruido(0.30f);
    (void)mete_ale(32U);
    ale_modo_poll();
    v = mete_ale(32U);
    sinS = mide_m("ALE, SOLO RUIDO (BUSCANDO)  ", v, NBUF);
    seg(sinS, 12000UL);
    ale_rx_info(&inf);
    pr("      simbolos "); prn(inf.simbolos); pr(", enganches "); prn(inf.enganches);
    pr(", palabras "); prn(inf.palabras); pr("\n");

    rnd_pon(4242u);
    ale_llena(1.0f, 0.10f);
    ale_rx_cuentas_cero();
    (void)mete_ale(32U);
    ale_modo_poll();
    v = mete_ale(32U);
    conS = mide_m("ALE, CON SONDEO (ENGANCHADO)", v, NBUF);
    seg(conS, 12000UL);
    ale_rx_info(&inf);
    pr("      simbolos "); prn(inf.simbolos); pr(", enganches "); prn(inf.enganches);
    pr(", palabras "); prn(inf.palabras);
    pr(", enganchado "); prn(inf.enganchado);
    pr(", calidad "); prn(inf.calidad); pr("/48\n");
    apunta_res("ALE 2G         x muestra  ", 12000UL,
                   mil_de(sinS, 12000UL), mil_de(conS, 12000UL));

    /*
     * EL BARRIDO: lo que cuesta BUSCAR el principio de palabra.
     *
     * Mientras no hay enganche se prueba UNA fase por simbolo -125 veces
     * por segundo-; con enganche se prueba una cada 49 simbolos -2,5 veces
     * por segundo-. La diferencia entre las dos pasadas de arriba,
     * repartida entre los simbolos que han entrado, es lo que cuesta un
     * barrido. Es una COTA, no una medida fina: las dos pasadas llevan
     * audio distinto y los ocho Goertzel no toman exactamente las mismas
     * ramas.
     */
    pr("\n     el barrido: buscar el principio de palabra\n");
    if (sinS > conS) {
        unsigned long d = ((sinS - conS) * ALE_SPS) / 100UL;   /* ins por simbolo */
        pr("    una prueba de fase, cota por diferencia  "); prn(d);
        pr(" ins = "); prn(d / 200UL); pr(",");
        prn(((d % 200UL) * 100UL / 200UL) / 10UL);
        prn(((d % 200UL) * 100UL / 200UL) % 10UL);
        pr(" us\n");
        pr("      (buscando cuesta un ");
        prn(((sinS - conS) * 1000UL / conS) / 10UL); pr(",");
        prn(((sinS - conS) * 1000UL / conS) % 10UL);
        pr(" % mas que enganchado)\n");
    } else {
        pr("    buscando NO sale mas caro que enganchado en esta medida\n");
    }

    pr("\n     y ale_modo_poll(), desde el bucle principal\n");
    a = tic();
    for (r = 0U; r < 1000U; r++) { ale_modo_poll(); }
    b = tic();
    (void)mide_s("una vuelta, sin nada que sacar", (a - b) & 0xFFFFFFUL, 1000UL);
    seg_ev(((unsigned long long)((a - b) & 0xFFFFFFUL) * 40ULL) / 1000ULL, 62500UL);

    ale_modo_stop();
}

/* ---------------------------------------------------------------- IDENT */
/*
 * Las vueltas de ident_modo_poll(), cronometradas UNA A UNA y repartidas
 * despues por coste. Ver la cabecera: dentro de ese poll hay tres caminos
 * muy distintos -la vuelta vacia, la ventana del detector y la cadencia- y
 * ninguno de los tres es publico. Lo que si es comprobable son sus
 * FRECUENCIAS, y por eso se imprimen las cuentas.
 */
#define POLL_MAX 2048U
static unsigned long s_poll[POLL_MAX];
static unsigned      s_poll_n;

static void banco_ident(void)
{
    unsigned long v, sinS, conS;
    unsigned long a, b;
    unsigned i;

    pr("\n  5. IDENT - el motor de \"que señal es\" (de la banda lateral)\n");
    pr("     ident.c + stanag_det.c, bloques de 32 a 12 kHz\n\n");

    if (!ident_modo_start()) {
        pr("    *** no arranca: no hay memoria prestada, no se mide ***\n");
        return;
    }

    /* --- el trabajo POR MUESTRA --- */
    rnd_pon(31337u);
    gen_ruido(0.30f);
    (void)mete_ident(32U);
    v = mete_ident(32U);
    sinS = mide_m("IDENT por muestra, SOLO RUIDO", v, NBUF);
    seg(sinS, 12000UL);

    /* FSK de 170 Hz a 100 baudios: un SITOR, que es lo que mas se oye y lo
     * que el motor sabe medir entero -ancho, dos tonos, separacion y
     * velocidad-. */
    rnd_pon(31337u);
    {
        float fase = 0.0f;
        unsigned k = 0U;
        uint32_t reg = 0xACE1u;
        int bit = 0;

        for (i = 0U; i < NBUF; i++) {
            float f;
            if ((i % 120U) == 0U) {                 /* 12.000/100 = 120 */
                reg = (reg >> 1) ^ (uint32_t)((-(int32_t)(reg & 1U)) & 0xB400u);
                bit = (int)(reg & 1U);
            }
            f = bit ? 1785.0f : 1615.0f;            /* 170 Hz de separacion */
            fase += 6.2831853f * f / 12000.0f;
            if (fase > 6.2831853f) { fase -= 6.2831853f; }
            s_buf[i] = sinf(fase) + 0.10f * ruido();
            k++;
        }
    }
    (void)mete_ident(32U);
    v = mete_ident(32U);
    conS = mide_m("IDENT por muestra, CON SEÑAL ", v, NBUF);
    seg(conS, 12000UL);
    apunta_res("IDENT          x muestra  ", 12000UL,
                   mil_de(sinS, 12000UL), mil_de(conS, 12000UL));

    /*
     * --- el trabajo de BUCLE PRINCIPAL ---
     *
     * Se alimenta como la radio -32 muestras por bloque- y se llama a
     * ident_modo_poll() cada seis bloques, o sea cada 16 ms, que es lo que
     * tarda una vuelta del bucle principal. Cada vuelta se cronometra
     * sola.
     */
    pr("\n     el trabajo de BUCLE PRINCIPAL: ident_modo_poll()\n");
    s_poll_n = 0U;
    {
        unsigned pase;
        for (pase = 0U; pase < 3U; pase++) {        /* 32,7 s de audio */
            for (i = 0U; i + 32U <= NBUF; i += 32U) {
                ident_modo_mete(&s_buf[i], 32U);
                if (((i / 32U) % 6U) == 0U && s_poll_n < POLL_MAX) {
                    a = tic(); ident_modo_poll(); b = tic();
                    s_poll[s_poll_n++] = (a - b) & 0xFFFFFFUL;
                }
            }
        }
    }
    {
        unsigned long mn = 0xFFFFFFFFUL, tot = 0UL;
        unsigned long ne_mn = 0xFFFFFFFFUL, ne_mx = 0UL;
        unsigned long s_vacia = 0UL, s_a = 0UL, s_b = 0UL;
        unsigned n_vacia = 0U, n_a = 0U, n_b = 0U;
        unsigned long corte_ne, corte_ab;

        for (i = 0U; i < s_poll_n; i++) {
            if (s_poll[i] < mn) { mn = s_poll[i]; }
            tot += s_poll[i];
        }
        /*
         * LOS CORTES SALEN DE LOS PROPIOS DATOS, no de un numero escrito a
         * mano. Cuatro veces la vuelta mas barata separa "no ha pasado
         * nada" de "ha pasado algo"; y las que SI han hecho algo se parten
         * por la mitad de su recorrido, que aqui es un abismo -una es
         * cuarenta veces la otra, no hay donde equivocarse-.
         */
        corte_ne = (mn + 1UL) * 4UL;
        for (i = 0U; i < s_poll_n; i++) {
            if (s_poll[i] < corte_ne) { continue; }
            if (s_poll[i] < ne_mn) { ne_mn = s_poll[i]; }
            if (s_poll[i] > ne_mx) { ne_mx = s_poll[i]; }
        }
        corte_ab = (ne_mn + ne_mx) / 2UL;
        for (i = 0U; i < s_poll_n; i++) {
            if (s_poll[i] < corte_ne)      { s_vacia += s_poll[i]; n_vacia++; }
            else if (s_poll[i] < corte_ab) { s_a += s_poll[i]; n_a++; }
            else                           { s_b += s_poll[i]; n_b++; }
        }
        pr("      "); prn(s_poll_n);
        pr(" vueltas cronometradas sobre 32,7 s de audio\n");
        /*
         * Y QUIEN ES QUIEN SE DECIDE POR LA CUENTA, NO POR EL COSTE.
         *
         * Dentro de ident_modo_poll() hay dos trabajos y ninguno es
         * publico, asi que no se puede preguntar. Pero sus FRECUENCIAS si
         * se saben y no se parecen en nada: la ventana del detector sale
         * cada 256 muestras de las de 3 kHz -85 ms- y la cadencia cada
         * ocho anotaciones -4 s-. O sea cuarenta y siete a una. El grupo
         * numeroso es la ventana y el raro es la cadencia, y las cuentas
         * se imprimen para que se pueda comprobar que cuadran.
         */
        pr("\n");
        (void)mide_s("la vuelta vacia             ", s_vacia, n_vacia ? n_vacia : 1UL);
        pr("        "); prn(n_vacia); pr(" veces\n");
        {
            unsigned long s_vent = (n_a >= n_b) ? s_a : s_b;
            unsigned      n_vent = (n_a >= n_b) ? n_a : n_b;
            unsigned long s_cad  = (n_a >= n_b) ? s_b : s_a;
            unsigned      n_cad  = (n_a >= n_b) ? n_b : n_a;

            if (n_vent) {
                unsigned long w = mide_s("una VENTANA del detector    ", s_vent, n_vent);
                pr("        "); prn(n_vent); pr(" veces en 32,7 s = una cada ");
                prn(32700UL / n_vent); pr(" ms  (lo esperado: 85)\n");
                seg_ev(w, (unsigned long)((unsigned long long)n_vent * 1000ULL / 32ULL));
            }
            if (n_cad) {
                unsigned long w = mide_s("la CADENCIA de encendidos   ", s_cad, n_cad);
                pr("        "); prn(n_cad); pr(" veces en 32,7 s = una cada ");
                prn(32700UL / n_cad); pr(" ms  (lo esperado: 4000)\n");
                seg_ev(w, (unsigned long)((unsigned long long)n_cad * 1000ULL / 32ULL));
            }
        }
        {
            unsigned long ins_v = (unsigned long)(((unsigned long long)tot * 40ULL)
                                  / (unsigned long long)(s_poll_n ? s_poll_n : 1U));
            unsigned long ins_s = (unsigned long)(((unsigned long long)tot * 40ULL)
                                  / 32ULL);     /* 32,7 s redondeados a 32 */
            unsigned long cien = ins_s / 20000UL;

            pr("\n      TODO el bucle principal del IDENT junto: ");
            prn(ins_s); pr(" ins/s = ");
            prn(cien / 100UL); pr(",");
            if ((cien % 100UL) < 10UL) { pr("0"); }
            prn(cien % 100UL); pr(" % de cada segundo\n");
            pr("      (media por vuelta "); prn(ins_v); pr(" ins)\n");
            apunta_res("IDENT          bucle ppal ", 0UL, ins_s / 2000UL, ins_s / 2000UL);
        }
    }

    /*
     * Y LA COMPROBACION, QUE ES LO QUE HACE QUE EL REPARTO DE ARRIBA VALGA:
     * stanag_det_paso() cronometrado SOLO, sin ident_modo_poll() alrededor.
     * Si sale lo mismo que el grupo numeroso, el reparto esta bien hecho;
     * si no, el reparto es una suposicion y hay que decirlo.
     */
    pr("\n     comprobacion: stanag_det_paso() cronometrado a solas\n");
    {
        stanag_det_t r;
        unsigned long acu = 0UL, n = 0UL;

        for (i = 0U; i + 32U <= NBUF; i += 32U) {
            stanag_det_mete(&s_buf[i], 32U);
            if (((i / 32U) % 6U) == 0U) {
                a = tic();
                if (stanag_det_paso(&r)) { b = tic(); acu += (a - b) & 0xFFFFFFUL; n++; }
                else { b = tic(); }
            }
        }
        if (n) {
            (void)mide_s("una ventana, medida directa ", acu, n);
            pr("        "); prn(n); pr(" ventanas en 10,9 s\n");
        } else {
            pr("    no ha salido ninguna ventana: la comprobacion no dice nada\n");
        }
    }

    pr("      lo que ha decidido: ");
    pr(ident_modo_linea(0)); pr("\n");

    ident_modo_stop();
}

/* ----------------------------------------------------------------- WSPR */
/*
 * Una emision de WSPR dura 110,6 s y la ventana de captura 114,7. Aqui se
 * fabrica entera -1.376.256 muestras a 12 kHz- y se le da al receptor en
 * trozos de 600, como hace sim/wspr_aire.c, cronometrando POR SEPARADO:
 *
 *   wspr_rx_mete()              la interrupcion: mezcla y diezma
 *   wspr_rx_pasos_pendientes()  el bucle principal: el espectrograma
 *   wspr_rx_decodifica()        una vez cada dos minutos: buscar y Fano
 *
 * Que son exactamente los tres sitios por los que esto se reparte en la
 * radio (ver demod_am.c y wspr_modo_poll()).
 */
#define WSPR_T_SIM  (8192.0f / 12000.0f)
#define WSPR_BAUDIO (12000.0f / 8192.0f)

/*
 * UNA RANURA ENTERA DE WSPR, fabricada muestra a muestra y dada al
 * receptor en trozos de 600 como hace sim/wspr_aire.c. `sim` a cero es la
 * ranura SIN SEÑAL: solo ruido, que es el estado en el que esta esta radio
 * la mayor parte de las noches.
 *
 * Devuelve por los punteros los tres relojes separados, que son los tres
 * sitios por los que WSPR se reparte en la radio.
 */
static void wspr_ranura(const uint8_t *sim, unsigned long *t_mete,
                        unsigned long *n_mete, unsigned long *t_paso,
                        unsigned long *n_paso)
{
    unsigned long a, b;
    long n_total, n;
    float trozo[600];
    int j = 0;
    float fase = 0.0f;

    *t_mete = 0UL; *n_mete = 0UL; *t_paso = 0UL; *n_paso = 0UL;
    wspr_rx_reinicia();
    n_total = (long)((float)WSPR_RX_PASOS / 2.0f * WSPR_T_SIM * 12000.0f);

    for (n = 0; n < n_total; n++) {
        float v = 1.0f * ruido();

        if (sim != (const uint8_t *)0) {
            float t = (float)n / 12000.0f - ((float)WSPR_RX_ANTES_MS / 1000.0f);

            if (t >= 0.0f && t < (float)WSPR_SIMBOLOS * WSPR_T_SIM) {
                int k = (int)(t / WSPR_T_SIM);

                if (k >= 0 && k < (int)WSPR_SIMBOLOS) {
                    float f = (float)WSPR_RX_CENTRO_HZ
                            + (float)sim[k] * WSPR_BAUDIO;
                    fase += 6.2831853f * f / 12000.0f;
                    if (fase > 6.2831853f) { fase -= 6.2831853f; }
                    v += 1.4f * cosf(fase);
                }
            }
        }
        trozo[j++] = v;
        if (j == 600) {
            a = tic(); wspr_rx_mete(trozo, 600U); b = tic();
            *t_mete += (a - b) & 0xFFFFFFUL; *n_mete += 600UL;
            a = tic(); j = (int)wspr_rx_pasos_pendientes(); b = tic();
            *t_paso += (a - b) & 0xFFFFFFUL;
            *n_paso += (unsigned long)((j > 0) ? j : 0);
            j = 0;
        }
    }
    if (j > 0) { wspr_rx_mete(trozo, (uint16_t)j); }
    (void)wspr_rx_pasos_pendientes();
}

static void banco_wspr(void)
{
    wspr_msg_t m;
    uint8_t bits[7];
    static uint8_t sim[WSPR_SIMBOLOS];
    unsigned long a, b;
    unsigned long tm_s, nm_s, tp_s, np_s, td_s;   /* sin señal */
    unsigned long tm_c, nm_c, tp_c, np_c, td_c;   /* con señal */
    unsigned long v_sin, v_con, w_sin, w_con, d_sin, d_con;

    pr("\n  6. WSPR - 4-FSK a 1,46 baudios (de la banda lateral)\n");
    pr("     wspr_rx.c + wspr_msg.c + wspr_fano.c + wspr_modo.c, a 12 kHz\n\n");

    if (!wspr_modo_start()) {
        pr("    *** no arranca: no hay memoria prestada, no se mide ***\n");
        return;
    }

    /*
     * LO PRIMERO, QUE ES LO QUE PASA CASI SIEMPRE: el modo puesto y la
     * ventana CERRADA. wspr_modo_mete() se va sin hacer nada - pero se le
     * llama 375 veces por segundo igual, asi que no es cero.
     */
    rnd_pon(99u);
    gen_ruido(0.30f);
    {
        unsigned i;
        unsigned long acu = 0UL;

        for (i = 0U; i + 32U <= NBUF; i += 32U) {
            a = tic(); wspr_modo_mete(&s_buf[i], 32U); b = tic();
            acu += (a - b) & 0xFFFFFFUL;
        }
        v_sin = mide_m("WSPR, VENTANA CERRADA       ", acu, NBUF);
        seg(v_sin, 12000UL);
        apunta_res("WSPR cerrado   x muestra  ", 12000UL,
                   mil_de(v_sin, 12000UL), mil_de(v_sin, 12000UL));
    }

    /* El mensaje, codificado con el propio firmware. */
    memset(&m, 0, sizeof m);
    m.indicativo[0] = 'E'; m.indicativo[1] = 'A'; m.indicativo[2] = '4';
    m.indicativo[3] = 'H'; m.indicativo[4] = 'E'; m.indicativo[5] = 'W';
    m.indicativo[6] = '\0';
    m.locutor[0] = 'I'; m.locutor[1] = 'N'; m.locutor[2] = '8';
    m.locutor[3] = '0'; m.locutor[4] = '\0';
    m.dbm = 37U;
    if (!wspr_empaqueta(&m, bits)) {
        pr("    *** no se ha podido empaquetar el mensaje, no se mide ***\n");
        wspr_modo_stop();
        return;
    }
    wspr_codifica(bits, sim);

    /* --- la ranura SIN SEÑAL: 114,7 s de ruido --- */
    rnd_pon(99u);
    wspr_ranura((const uint8_t *)0, &tm_s, &nm_s, &tp_s, &np_s);
    v_sin = mide_m("WSPR capturando, SOLO RUIDO ", tm_s, nm_s);
    seg(v_sin, 12000UL);
    {
        wspr_msg_t r; int16_t hz = 0; uint8_t q = 0U, ok;

        memset(&r, 0, sizeof r);
        a = tic(); ok = wspr_rx_decodifica(&r, &hz, &q); b = tic();
        td_s = (a - b) & 0xFFFFFFUL;
        pr("      y al cerrar: ");
        pr(ok ? "ha salido algo (con ruido puro no deberia)"
              : "no sale nada, que es lo que tiene que pasar");
        pr("\n");
        (void)q; (void)hz;
    }

    /* --- y la ranura CON SEÑAL --- */
    rnd_pon(99u);
    wspr_ranura(sim, &tm_c, &nm_c, &tp_c, &np_c);
    v_con = mide_m("WSPR capturando, CON EMISION", tm_c, nm_c);
    seg(v_con, 12000UL);
    apunta_res("WSPR capturando x muestra ", 12000UL,
               mil_de(v_sin, 12000UL), mil_de(v_con, 12000UL));

    pr("\n     el bucle principal: wspr_rx_pasos_pendientes()\n");
    w_sin = mide_s("un paso, solo ruido         ", tp_s, np_s);
    w_con = mide_s("un paso, con emision        ", tp_c, np_c);
    pr("        "); prn(np_c);
    pr(" pasos en la ventana (274 Goertzel de 256 muestras cada uno)\n");
    /* Un paso cada medio simbolo: 2/0,6827 = 2,93 pasos por segundo. */
    seg_ev(w_con, 2930UL);
    apunta_res("WSPR espectrogr bucle ppal", 0UL,
               (unsigned long)(((unsigned long long)w_sin * 2930ULL) / 2000000ULL),
               (unsigned long)(((unsigned long long)w_con * 2930ULL) / 2000000ULL));

    pr("\n     el suceso caro: CERRAR LA RANURA Y DECODIFICAR\n");
    {
        wspr_msg_t r; int16_t hz = 0; uint8_t q = 0U, ok;

        memset(&r, 0, sizeof r);
        a = tic(); ok = wspr_rx_decodifica(&r, &hz, &q); b = tic();
        td_c = (a - b) & 0xFFFFFFUL;
        /*
         * Y AQUI ESTA LA SORPRESA DE TODO ESTE BANCO.
         *
         * Decodificar CON emision es barato: el Fano baja por el camino
         * bueno y sale. SIN emision no hay camino bueno, asi que se gasta
         * WSPR_PASOS_MAX entero -200.000 pasos, ver wspr_fano.c- antes de
         * poder decir que no. Y el caso sin emision no es el raro: es el
         * normal, una de cada dos ventanas en una noche buena y todas en
         * una mala.
         *
         * Esto NO corre en la interrupcion -corre en wspr_modo_poll(), o
         * sea en el bucle principal-, asi que no se oye; se VE, porque el
         * bucle principal es el que lee el tactil.
         */
        d_sin = mide_s("decodificar SIN emision     ", td_s, 1UL);
        d_con = mide_s("decodificar CON emision     ", td_c, 1UL);
        pr("        sin emision se agotan los 200.000 pasos de Fano, a ");
        prn(d_sin / 200000UL); pr(" instrucciones el paso\n");
        pr("        con emision ha salido: ");
        if (ok) { pr(r.indicativo); pr(" "); pr(r.locutor); pr(", cuadra "); prn(q); }
        else    { pr("nada, cuadra "); prn(q); }
        pr("\n");
        /* Una vez cada 120 s = 8,33 milesimas de vez por segundo. */
        seg_ev(d_con, 8UL);
        apunta_res("WSPR decodifica 1 cada 2m ", 0UL,
                   (unsigned long)(((unsigned long long)d_sin * 8ULL) / 2000000ULL),
                   (unsigned long)(((unsigned long long)d_con * 8ULL) / 2000000ULL));
    }

    pr("\n     LA RANURA ENTERA, sumando los tres sitios:\n");
    {
        unsigned long long t_s = (unsigned long long)(tm_s + tp_s + td_s) * 40ULL;
        unsigned long long t_c = (unsigned long long)(tm_c + tp_c + td_c) * 40ULL;
        unsigned long c_s = (unsigned long)(t_s / (115ULL * 20000ULL));
        unsigned long c_c = (unsigned long)(t_c / (115ULL * 20000ULL));

        pr("      sin señal  "); prn((unsigned long)t_s);
        pr(" ins en 114,7 s = ");
        prn(c_s / 100UL); pr(",");
        if ((c_s % 100UL) < 10UL) { pr("0"); }
        prn(c_s % 100UL); pr(" %\n");
        pr("      con señal  "); prn((unsigned long)t_c);
        pr(" ins en 114,7 s = ");
        prn(c_c / 100UL); pr(",");
        if ((c_c % 100UL) < 10UL) { pr("0"); }
        prn(c_c % 100UL); pr(" %\n");
        pr("      (y eso solo mientras captura: 114,7 s de cada 120)\n");
    }

    wspr_modo_stop();
}

/* ====================================================================== */
int main(void)
{
    /*
     * SE QUITA UNALIGN_TRP, Y SOLO ESE BIT.
     *
     * m4/start.c lo pone -junto con DIV_0_TRP- para cazar accesos
     * desalineados, y para el dibujo esta muy bien: en el EXMC del panel un
     * acceso desalineado es un fallo de bus de verdad.
     *
     * Aqui no vale, y no por culpa del firmware: el compilador junta dos
     * `strb` seguidos a una posicion impar -el byte de control y el de
     * protocolo de la trama AX.25 que monta este mismo banco- en un
     * `strh.w` a direccion impar. Eso en el GD32F450 es LEGAL y es lo que
     * corre en la radio: el arranque del firmware no pone este bit (se ha
     * comprobado: no aparece en boot/ ni en User/), asi que la radio
     * ejecuta ese mismo `strh.w` sin inmutarse.
     *
     * O sea que dejarlo puesto no cazaria un fallo: cazaria una diferencia
     * entre el banco y la radio. DIV_0_TRP se queda, que ese si vale.
     */
    *(volatile uint32_t *)0xE000ED14UL &= ~(1UL << 3);
    __asm__ volatile("dsb");
    __asm__ volatile("isb");

    reloj_on();

    pr("\n");
    pr("LO QUE CUESTAN AX.25, AIS, ALE, IDENT Y WSPR EN UN CORTEX-M4\n");
    pr("1 tic = 40 instrucciones; 200 MHz; en la radio real, x1,7\n");

    banco_ax25();
    banco_ais();
    banco_ale();
    banco_ident();
    banco_wspr();

    resumen();

    pr("\n");
    pila_di(0UL);
    pr("\n");
    if (g_fallos) { pr("  HAY FALLOS\n\n"); } else { pr("  TODO CORRECTO\n\n"); }
    salir();
    return 0;
}
