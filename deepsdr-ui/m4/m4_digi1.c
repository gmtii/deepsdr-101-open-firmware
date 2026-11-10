/*
 * LO QUE CUESTAN LOS DECODIFICADORES DIGITALES DE TONOS, EN INSTRUCCIONES.
 *
 * EL PORQUE. En esta radio el cuadro no se pinta en el tiempo que sobra: se
 * pinta en lo que queda DESPUES del audio -ver m4_vuelta.c-. Y un
 * decodificador digital no es "una cosa mas del bucle principal": vive
 * DENTRO de la cadena de audio, muestra a muestra, a 12 kHz, en el mismo
 * sitio donde demod_am.c reparte s_ssb_dec entre sus oyentes. O sea que
 * cada instruccion que gaste sale directamente del divisor del cuadro.
 *
 * Y hay una segunda cosa, que es la que de verdad hacia falta medir: un
 * modo digital PUESTO Y SIN NADA QUE DECODIFICAR es el estado mas comun de
 * esta radio. Nadie tiene una emision delante el 90 % del tiempo. Si eso
 * cuesta lo mismo que con señal -y aqui se vera que casi siempre cuesta lo
 * mismo, porque ninguno de estos modulos tiene puerta de ruido- entonces la
 * carga no es "lo que cuesta decodificar", es "lo que cuesta tener el modo
 * puesto", y eso se paga todo el rato.
 *
 * LAS TASAS. Todos estos cuelgan del mismo sitio: demod_am.c, rama de banda
 * lateral, sobre s_ssb_dec, que ya esta a DEMOD_DEC_FS_HZ = 12.000 muestras
 * por segundo. El bloque son 32 muestras a 96 kHz -64 a 48 kHz, troceado-.
 * O sea que el numero que manda es INSTRUCCIONES POR MUESTRA x 12.000:
 *
 *     % de cada segundo = ins/muestra x 12.000 / 2.000.000
 *
 * Diez instrucciones por muestra son el 6 % de la CPU. Es mucho mas de lo
 * que parece al escribir el bucle.
 *
 * Los que NO van por muestra -dsc_poll(), rtty_auto_poll(),
 * rtty_scope_poll(), jtty_rx_saca()- corren desde el bucle principal, una
 * vez por vuelta, y se miden por suceso.
 *
 * LA ENTRADA ES DE VERDAD. Aqui no se mide con ceros: se sintetizan los
 * tonos con su velocidad y su desplazamiento -FSK para RTTY, NAVTEX, DSC y
 * JTTY, giro de fase para PSK31, manipulacion Morse para CW- y se deja
 * correr una pasada entera antes de cronometrar, para que los lazos de
 * reloj, las referencias de nivel y los enganches esten donde estan cuando
 * la radio lleva un rato escuchando.
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
 *     m4/m4_digi1.c m4/start.c ../User/rtty.c ../User/rtty_auto.c \
 *     ../User/rtty_scope.c ../User/navtex.c ../User/dsc.c ../User/psk31.c \
 *     ../User/cw.c ../User/jtty_rx.c ../User/jtty_fec.c ../User/jtty_msg.c \
 *     ../User/nco.c ../User/fft.c -o out/m4_digi1.elf -lc -lm -lgcc
 */
#include "semi.h"
void pila_di(unsigned long maximo);

#include <math.h>
#include <string.h>

#include "rtty.h"
#include "rtty_auto.h"
#include "rtty_scope.h"
#include "navtex.h"
#include "dsc.h"
#include "psk31.h"
#include "cw.h"
#include "jtty_rx.h"
#include "jtty_fec.h"

int g_fallos;

/* ======================================================================
 * LOS TAPONES PARA ENLAZAR
 *
 * Van aqui dentro a proposito: este banco no toca User/ ni sim/ ni nada
 * mas. Son dos cosas que el DSC pide a la radio y que fuera de la radio no
 * existen.
 * ====================================================================== */

/* La memoria prestada. En la radio salen de los 54 kB que se reparten FT8,
 * HFDL, WSPR, AIS, ALE y JTTY; aqui es un trozo de .bss y de sobra. Si no
 * cupiese, dsc_start() se saldria sin arrancar y el banco lo diria -se
 * comprueba con dsc_activo() despues de arrancar-. */
static uint8_t s_prestamo[65536] __attribute__((aligned(8)));
uint8_t *hfdl_ram_todo(uint32_t *capacity_out)
{
    if (capacity_out) { *capacity_out = (uint32_t)sizeof s_prestamo; }
    return s_prestamo;
}
void hfdl_ram_coge(void)   { }
void hfdl_ram_suelta(void) { }

/* El traductor de los renglones del DSC. Aqui siempre en castellano. */
const char *tr(const char *es, const char *en) { (void)en; return es; }

/* ======================================================================
 * LA MEDIDA
 * ====================================================================== */

#define FS_HZ     12000.0f
#define TASA      12000UL        /* muestras por segundo que le entran */
/*
 * 5,46 segundos de audio. No es capricho: con dos segundos el SITOR-B no
 * llega a pasar de la fase al mensaje -cuarenta posiciones de fase son
 * cuatro segundos solo de alternancia- y el Morse a 20 PPM no da ni tres
 * letras, asi que el banco media el enganche y no la decodificacion. Son
 * 256 kB en .bss y aqui sobran.
 */
#define NBUF      65536U

static float s_buf[NBUF];

/*
 * Instrucciones POR MUESTRA con dos decimales. Los numeros de aqui son de
 * diez a trescientas instrucciones por muestra: redondeados a entero se
 * pierden las diferencias que importan -por ejemplo si tener el modo puesto
 * sin señal cuesta lo mismo que con ella-.
 *
 * Devuelve instrucciones x 100.
 */
static unsigned long mide_m(const char *que, unsigned long a, unsigned long b,
                            unsigned long n)
{
    unsigned long long t = (unsigned long long)((a - b) & 0xFFFFFFUL) * 4000ULL;
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
    unsigned long us   = (unsigned long)(ins_s / 200ULL);     /* us de cada segundo */
    unsigned long cien = (unsigned long)(ins_s / 20000ULL);   /* % x 100 */

    pr("   ->  "); prn((unsigned long)ins_s); pr(" ins/s = ");
    prn(us); pr(" us de cada segundo = ");
    prn(cien / 100UL); pr(",");
    if ((cien % 100UL) < 10UL) { pr("0"); }
    prn(cien % 100UL);
    pr(" %\n");
}

/*
 * EL RESUMEN DEL FINAL. Cada banco apunta aqui lo suyo y main() lo saca
 * ordenado de mayor a menor, que es como se mira: lo primero que hay que
 * contestar es "cual de estos modos me quita mas radio", y eso con los
 * numeros repartidos por seis pantallas no se ve.
 */
#define RES_MAX 12U
typedef struct { const char *que; unsigned long sin_s; unsigned long con_s; } res_t;
static res_t s_res[RES_MAX];
static unsigned s_res_n;

static void apunta(const char *que, unsigned long sin_s, unsigned long con_s)
{
    if (s_res_n < RES_MAX) {
        s_res[s_res_n].que = que;
        s_res[s_res_n].sin_s = sin_s;
        s_res[s_res_n].con_s = con_s;
        s_res_n++;
    }
}

static void tanto(unsigned long c100)
{
    unsigned long long ins_s = (unsigned long long)c100 * (unsigned long long)TASA / 100ULL;
    unsigned long cien = (unsigned long)(ins_s / 20000ULL);

    prn(cien / 100UL); pr(",");
    if ((cien % 100UL) < 10UL) { pr("0"); }
    prn(cien % 100UL); pr(" %");
}

static void resumen(void)
{
    unsigned i, j;

    pr("\n  RESUMEN, DE MAYOR A MENOR (coste por muestra a 12 kHz)\n\n");
    pr("                            ins/muestra    sin señal     con señal\n");
    for (i = 0U; i < s_res_n; i++) {
        unsigned mejor = i;
        for (j = i + 1U; j < s_res_n; j++) {
            if (s_res[j].con_s > s_res[mejor].con_s) { mejor = j; }
        }
        if (mejor != i) {
            res_t t = s_res[i];
            s_res[i] = s_res[mejor];
            s_res[mejor] = t;
        }
        pr("    "); pr(s_res[i].que); pr("  ");
        if (s_res[i].con_s < 10000UL)  { pr(" "); }
        if (s_res[i].con_s < 100000UL) { pr(" "); }
        prn(s_res[i].con_s / 100UL); pr(",");
        if ((s_res[i].con_s % 100UL) < 10UL) { pr("0"); }
        prn(s_res[i].con_s % 100UL);
        pr("       ");
        tanto(s_res[i].sin_s);
        pr("      ");
        tanto(s_res[i].con_s);
        pr("\n");
    }
}

/* Un suceso suelto: instrucciones y microsegundos con dos decimales. */
static unsigned long mide_s(const char *que, unsigned long a, unsigned long b,
                            unsigned long n)
{
    unsigned long long t = (unsigned long long)((a - b) & 0xFFFFFFUL) * 40ULL;
    unsigned long ins = (unsigned long)(t / (unsigned long long)n);
    unsigned long us100 = ins / 2UL;   /* ins/200 us, x100 */

    pr("    "); pr(que); pr("  ");
    prn(ins); pr(" ins = ");
    prn(us100 / 100UL); pr(",");
    if ((us100 % 100UL) < 10UL) { pr("0"); }
    prn(us100 % 100UL);
    pr(" us\n");
    return ins;
}

/* Y lo que se lleva de cada segundo si pasa `veces` veces por segundo. */
static void seg_ev(unsigned long ins, unsigned long veces)
{
    unsigned long long ins_s = (unsigned long long)ins * (unsigned long long)veces;
    unsigned long cien = (unsigned long)(ins_s / 20000ULL);

    pr("      a "); prn(veces); pr(" por segundo: ");
    prn(cien / 100UL); pr(",");
    if ((cien % 100UL) < 10UL) { pr("0"); }
    prn(cien % 100UL);
    pr(" % de cada segundo\n");
}

/*
 * LO QUE ANADE UN CARACTER, POR DIFERENCIA.
 *
 * "Cerrar un caracter" no se puede cronometrar solo: la maquina de estados
 * de estos modulos vive repartida por toda la pasada y no hay una funcion
 * a la que ponerle el reloj delante. Lo que se hace es restar la pasada
 * SIN caracteres de la pasada CON texto y repartir la diferencia entre los
 * caracteres que salieron.
 *
 * Y es una COTA, no una medida fina: las dos pasadas llevan audio distinto
 * y el resto del camino -el Goertzel, el discriminador, los lazos- no toma
 * exactamente las mismas ramas. Lo que esto contesta bien es la pregunta
 * que importa: si cerrar caracteres SE NOTA o no se nota.
 */
static void diferencial(unsigned long v_con, unsigned long v_sin, unsigned nc)
{
    if (nc == 0U) {
        pr("    un caracter entero: no salio ninguno, no hay con que\n");
        return;
    }
    if (v_con <= v_sin) {
        pr("    un caracter entero: POR DEBAJO del ruido de la medida\n");
        pr("      (la pasada con texto no sale mas cara que la pasada sin el)\n");
        return;
    }
    {
        unsigned long d = ((v_con - v_sin) * NBUF) / (100UL * (unsigned long)nc);
        unsigned long pm = ((v_con - v_sin) * 1000UL) / v_sin;

        pr("    un caracter entero, cota por diferencia  "); prn(d);
        pr(" ins = "); prn(d / 200UL); pr(" us\n");
        pr("      (toda la pasada con texto cuesta un "); prn(pm / 10UL); pr(",");
        prn(pm % 10UL); pr(" % mas que sin el)\n");
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
/* Cuatro uniformes sumadas: suficiente campana para lo que esto mide, y
 * reproducible, que es lo que hace falta para correr dos veces y comparar. */
static float ruido(void) { return (uni() + uni() + uni() + uni()) * 0.6f; }

static void gen_ruido(float amp)
{
    unsigned i;
    for (i = 0U; i < NBUF; i++) { s_buf[i] = amp * ruido(); }
}

/* Un tono quieto: el reposo de marca del RTTY, que es lo que suena entre
 * caracter y caracter en una emision de verdad. */
static void gen_tono(float hz, float amp, float nz)
{
    unsigned i;
    float fase = 0.0f, inc = 6.2831853f * hz / FS_HZ;
    for (i = 0U; i < NBUF; i++) {
        fase += inc;
        if (fase > 6.2831853f) { fase -= 6.2831853f; }
        s_buf[i] = amp * sinf(fase) + nz * ruido();
    }
}

/* El flujo de bits que se modula, y su longitud. */
#define NBITS 2048U
static uint8_t  s_bits[NBITS];
static unsigned s_nbits;

/* FSK de dos tonos con fase continua: RTTY, NAVTEX y DSC. */
static void gen_fsk(float f0, float f1, float baud, float amp, float nz)
{
    unsigned i;
    float fase = 0.0f;
    float k = baud / FS_HZ;

    for (i = 0U; i < NBUF; i++) {
        unsigned ib = (unsigned)((float)i * k);
        float f = s_bits[ib % s_nbits] ? f1 : f0;

        fase += 6.2831853f * f / FS_HZ;
        if (fase > 6.2831853f) { fase -= 6.2831853f; }
        s_buf[i] = amp * sinf(fase) + nz * ruido();
    }
}

/* ======================================================================
 * EL BANCO
 * ====================================================================== */

/* Mete el buffer entero en trozos de 32 muestras, que es como se lo pasa
 * demod_am.c (DEC_BLOCK_SAMPLES_96K). RTTY y CW ADEMAS lo exigen. */
#define TROZO 32U

static void mete_rtty(void)
{
    unsigned i;
    for (i = 0U; i < NBUF; i += TROZO) { rtty_process(&s_buf[i], TROZO); }
}
static void mete_cw(void)
{
    unsigned i;
    for (i = 0U; i < NBUF; i += TROZO) { cw_process(&s_buf[i], TROZO); }
}
static void mete_navtex(void)
{
    unsigned i;
    for (i = 0U; i < NBUF; i += TROZO) { navtex_process(&s_buf[i], TROZO); }
}
static void mete_dsc(void)
{
    unsigned i;
    for (i = 0U; i < NBUF; i += TROZO) { dsc_process(&s_buf[i], TROZO); }
}
static void mete_psk31(void)
{
    unsigned i;
    for (i = 0U; i < NBUF; i += TROZO) { psk31_process(&s_buf[i], TROZO); }
}
static void mete_scope(void)
{
    unsigned i;
    for (i = 0U; i < NBUF; i += TROZO) { rtty_scope_feed(&s_buf[i], TROZO); }
}

/* Vacia los anillos de salida sin cronometrar, y cuenta. */
static unsigned vacia_rtty(void)   { char c; unsigned n = 0U; while (rtty_get_char(&c))   { n++; } return n; }
static unsigned vacia_navtex(void) { char c; unsigned n = 0U; while (navtex_get_char(&c)) { n++; } return n; }
static unsigned vacia_psk31(void)  { char c; unsigned n = 0U; while (psk31_get_char(&c))  { n++; } return n; }
static unsigned vacia_cw(void)     { char c; unsigned n = 0U; while (cw_get_char(&c))     { n++; } return n; }

/* ---------------------------------------------------------------- RTTY */

/* Un caracter de Baudot tal y como va por el aire: arranque a espacio,
 * cinco bits de datos del menos al mas significativo, y parada a marca. */
static void bits_baudot(const char *txt)
{
    unsigned n = 0U;
    const char *p;

    /* Reposo delante: marca continua, que es lo que manda una emisora entre
     * una cosa y otra. */
    for (p = txt; *p && n + 7U < NBITS; p++) {
        unsigned k;
        uint8_t cod = (uint8_t)(*p & 0x1F);

        s_bits[n++] = 0U;                       /* arranque */
        for (k = 0U; k < 5U; k++) { s_bits[n++] = (uint8_t)((cod >> k) & 1U); }
        s_bits[n++] = 1U;                       /* parada */
    }
    s_nbits = n;
}

static void banco_rtty(void)
{
    unsigned long a, b, v, vq, vs;
    unsigned nc;

    pr("\n  RTTY  (rtty.c, 45,45 baudios, desplazamiento 170 Hz, 12 kHz)\n");

    rtty_init();
    rtty_set_enabled(1U);
    rtty_set_mark_space_hz(2295.0f, 2125.0f);
    rtty_set_baud(45.45f);

    /* 1. apagado: lo que cuesta no hacer nada, para restarlo */
    rtty_set_enabled(0U);
    gen_ruido(0.02f);
    mete_rtty();
    a = tic(); mete_rtty(); b = tic();
    v = mide_m("apagado del todo              ", a, b, NBUF); seg(v, TASA);

    /* 2. puesto y SIN SEÑAL: solo ruido. El estado mas comun de la radio. */
    rtty_set_enabled(1U);
    rnd_pon(0x1234567u);
    gen_ruido(0.02f);
    mete_rtty(); (void)vacia_rtty();
    a = tic(); mete_rtty(); b = tic();
    vs = mide_m("puesto, SIN SEÑAL (ruido)     ", a, b, NBUF); seg(vs, TASA);
    (void)vacia_rtty();

    /* 3. puesto y en reposo de marca: hay señal pero no hay caracteres */
    rnd_pon(0x1234567u);
    gen_tono(2295.0f, 0.5f, 0.02f);
    mete_rtty(); (void)vacia_rtty();
    a = tic(); mete_rtty(); b = tic();
    vq = mide_m("con señal, reposo de marca    ", a, b, NBUF); seg(vq, TASA);
    (void)vacia_rtty();

    /* 4. puesto y decodificando texto de verdad */
    rnd_pon(0x1234567u);
    bits_baudot("RYRYRYRY DE EA1ABC EA1ABC PSE K ");
    gen_fsk(2125.0f, 2295.0f, 45.45f, 0.5f, 0.02f);
    mete_rtty(); (void)vacia_rtty();
    a = tic(); mete_rtty(); b = tic();
    v = mide_m("con señal, texto decodificando", a, b, NBUF); seg(v, TASA);
    nc = vacia_rtty();
    pr("      caracteres sacados en la pasada: "); prn(nc); pr("\n");

    diferencial(v, vq, nc);

    /* 5. el reenganche: recalcular los coeficientes del Goertzel. Es lo que
     *    corre cuando la inversion se detecta sola, cuando rtty_auto.c pone
     *    tonos nuevos y cuando el usuario mueve el desplazamiento. */
    a = tic();
    { int r; for (r = 0; r < 200; r++) { rtty_set_mark_space_hz(2295.0f + (float)(r & 1), 2125.0f); } }
    b = tic();
    (void)mide_s("reenganche (tonos nuevos)     ", a, b, 200UL);

    a = tic();
    { int r; for (r = 0; r < 200; r++) { rtty_set_baud((r & 1) ? 45.45f : 50.0f); } }
    b = tic();
    (void)mide_s("cambiar de velocidad          ", a, b, 200UL);
    rtty_set_baud(45.45f);
    rtty_set_mark_space_hz(2295.0f, 2125.0f);

    a = tic();
    { int r; char c; for (r = 0; r < 500; r++) { (void)rtty_get_char(&c); } }
    b = tic();
    (void)mide_s("sacar un caracter (anillo vacio)", a, b, 500UL);

    apunta("RTTY       (rtty.c)       ", vs, v);
    pr("      salud "); prn((unsigned long)rtty_salud_pc());
    pr(" %, marcos "); prn((unsigned long)rtty_marcos_n());
    pr(", vueltas solas "); prn((unsigned long)rtty_auto_inv_veces()); pr("\n");
}

/* ------------------------------------------------- RTTY AUTO y la lupa */

static void banco_rtty_auto(void)
{
    unsigned long a, b, v;

    pr("\n  LA LUPA DE SINTONIA Y EL AJUSTE SOLO  (rtty_scope.c, rtty_auto.c)\n");

    rtty_scope_init();
    rnd_pon(0x2244668u);
    bits_baudot("RYRYRYRY DE EA1ABC ");
    gen_fsk(2125.0f, 2295.0f, 45.45f, 0.5f, 0.02f);

    /* La lupa come audio a 12 kHz como todos los demas, pero solo guarda:
     * la transformada corre luego, desde el bucle principal. */
    mete_scope();
    a = tic(); mete_scope(); b = tic();
    v = mide_m("rtty_scope_feed (guardar)     ", a, b, NBUF); seg(v, TASA);
    apunta("la lupa de sintonia, feed ", v, v);

    /* Y la transformada: 512 puntos. Una ventana son 512 muestras a 12 kHz,
     * o sea 23,4 por segundo como mucho. */
    {
        int r;
        unsigned long ins;
        a = tic();
        for (r = 0; r < 30; r++) {
            rtty_scope_feed(s_buf, 512U);   /* deja una ventana lista */
            rtty_scope_poll();              /* y la transforma */
        }
        b = tic();
        ins = mide_s("rtty_scope_poll (FFT de 512)  ", a, b, 30UL);
        seg_ev(ins, 23UL);
    }

    /* El ajuste automatico: corre una vez por vuelta del bucle principal y
     * promedia el espectro de la lupa SIEMPRE, vaya bien o mal. */
    rtty_auto_set(1U);
    rtty_auto_reset();
    /*
     * CON UN CUADRO NUEVO EN CADA VUELTA, que es lo que hace falta para que
     * se mida algo: prom_mete() se sale en seco si el cuadro de la lupa es
     * el mismo de la vuelta anterior, asi que midiendolo sin alimentar la
     * lupa salen cuarenta instrucciones y una conclusion falsa. La lupa da
     * 23 cuadros por segundo y el bucle principal da unos 30, o sea que el
     * caso de verdad esta entre los dos; aqui se mide el caro.
     */
    {
        int r;
        unsigned long acc = 0UL, ins;

        for (r = 0; r < 10; r++) {
            rtty_scope_feed(s_buf, 512U); rtty_scope_poll(); rtty_auto_poll();
        }
        for (r = 0; r < 100; r++) {
            rtty_scope_feed(s_buf, 512U);
            rtty_scope_poll();
            a = tic(); rtty_auto_poll(); b = tic();
            acc += ((a - b) & 0xFFFFFFUL);
        }
        ins = (acc * 40UL) / 100UL;
        pr("    rtty_auto_poll (cuadro nuevo)   "); prn(ins);
        pr(" ins = "); prn(ins / 200UL); pr(" us\n");
        seg_ev(ins, 23UL);
    }
    rtty_auto_set(0U);
    pr("      tonos puestos solos "); prn((unsigned long)rtty_auto_tonos_veces());
    pr(", velocidades "); prn((unsigned long)rtty_auto_baud_veces()); pr("\n");
}

/* -------------------------------------------------------------- NAVTEX */

/*
 * SITOR-B: codigos de SIETE bits con cuatro unos, b1 primero, y cada
 * caracter dos veces -la copia va cinco posiciones despues-. Delante, la
 * alternancia de fase que manda una emisora antes del mensaje.
 */
/*
 * Posiciones de fase delante del mensaje. Una emisora manda muchas mas -en
 * el banco del simulador son 160, once segundos-, pero aqui el buffer son
 * 5,46 s y hacen falta para el mensaje: con 30 (2,1 s) el enganche de
 * caracter se hace igual y queda sitio para veinte caracteres y sus copias.
 */
#define FASE_N 30U

static void bits_sitor(const char *txt)
{
    static uint8_t cod[320];
    unsigned n = 0U, i, total;
    const char *p;
    unsigned nc = 0U;

    for (p = txt; *p && nc < 120U; p++) {
        cod[nc++] = navtex_car_a_cod(*p, 0U);
    }
    total = FASE_N + 2U * nc + 5U;
    if (total > (unsigned)(sizeof cod)) { total = (unsigned)(sizeof cod); }
    /* se reconstruye encima: primero el relleno, luego las dos copias */
    {
        static uint8_t tr7[320];
        for (i = 0U; i < total; i++) { tr7[i] = (uint8_t)NAVTEX_COD_ALFA; }
        for (i = 0U; i < FASE_N; i++) {
            tr7[i] = (uint8_t)((i & 1U) ? NAVTEX_COD_ALFA : NAVTEX_COD_REP);
        }
        for (i = 0U; i < nc; i++) {
            if (FASE_N + 2U * i + 5U < total) {
                tr7[FASE_N + 2U * i]      = cod[i];
                tr7[FASE_N + 2U * i + 5U] = cod[i];
            }
        }
        for (i = 0U; i < total && n + 7U <= NBITS; i++) {
            unsigned k;
            for (k = 0U; k < 7U; k++) { s_bits[n++] = (uint8_t)((tr7[i] >> k) & 1U); }
        }
    }
    s_nbits = n;
}

static void banco_navtex(void)
{
    unsigned long a, b, v, vq;
    unsigned nc;

    pr("\n  NAVTEX  (navtex.c, SITOR-B 100 baudios, 170 Hz, centro 1700)\n");

    /* 1. sin señal */
    navtex_start(FS_HZ);
    navtex_set_centro_hz(1700.0f);
    rnd_pon(0x1234567u);
    gen_ruido(0.02f);
    mete_navtex(); (void)vacia_navtex();
    a = tic(); mete_navtex(); b = tic();
    vq = mide_m("puesto, SIN SEÑAL (ruido)     ", a, b, NBUF); seg(vq, TASA);
    (void)vacia_navtex();

    /* 2. con señal de verdad */
    navtex_stop(); navtex_start(FS_HZ); navtex_set_centro_hz(1700.0f);
    rnd_pon(0x1234567u);
    bits_sitor("ZCZC FA01 SECURITE NAVAREA I 123 ");
    gen_fsk(1615.0f, 1785.0f, 100.0f, 0.5f, 0.02f);
    mete_navtex(); (void)vacia_navtex();
    a = tic(); mete_navtex(); b = tic();
    v = mide_m("con señal, decodificando      ", a, b, NBUF); seg(v, TASA);
    nc = vacia_navtex();
    pr("      caracteres sacados en la pasada: "); prn(nc); pr("\n");

    diferencial(v, vq, nc);
    apunta("NAVTEX     (navtex.c)     ", vq, v);

    /* 3. el reenganche: mover el centro reinicia el reloj de bit */
    a = tic();
    { int r; for (r = 0; r < 200; r++) { navtex_set_centro_hz(1700.0f + (float)(r & 1)); } }
    b = tic();
    (void)mide_s("reenganche (mover el centro)  ", a, b, 200UL);
    navtex_set_centro_hz(1700.0f);

    {
        navtex_info_t in;
        navtex_info(&in);
        pr("      reloj de bit "); prn((unsigned long)in.bit_sync);
        pr(", caracter "); prn((unsigned long)in.car_sync);
        pr(", codigos buenos "); prn((unsigned long)in.car_ok);
        pr(", malos "); prn((unsigned long)in.car_mal); pr("\n");
    }
}

/* ------------------------------------------------------------------ DSC */

/* Lo mismo que hace una estacion: el caracter n en la ranura DX y el n-2 en
 * la RX. Sacado del formato, no de dsc.c. */
static uint8_t ecc_de_local(const uint8_t *m, uint8_t n)
{
    uint8_t i = (uint8_t)((n >= 2U && m[1] == m[0]) ? 1U : 0U), r = 0U;
    for (; i < n; i++) { r = (uint8_t)(r ^ (m[i] & 0x7FU)); }
    return r;
}

static void dsc_emite(const uint8_t *m, uint8_t n)
{
    uint8_t i;
    for (i = 0U; i < (uint8_t)(n + 3U); i++) {
        if (i < n) { dsc_mete_simbolo(m[i], 1U, 0U); }
        else       { dsc_mete_simbolo(0U, 0U, 0U); }
        if (i >= 2U && (uint8_t)(i - 2U) < n) { dsc_mete_simbolo(m[i - 2U], 1U, 1U); }
        else                                  { dsc_mete_simbolo(104U, 1U, 1U); }
    }
}

static void banco_dsc(void)
{
    unsigned long a, b, v, vq;
    uint8_t m[] = {120,120,0,46,34,6,0,108,21,53,22,0,0,118,
                   126,126,126,126,126,126,126,117,0};
    uint8_t n = (uint8_t)(sizeof m / sizeof m[0]);

    pr("\n  DSC  (dsc.c, 100 baudios, 170 Hz, centro 1700)\n");

    dsc_start(FS_HZ);
    if (!dsc_activo()) {
        pr("    *** no arranca: el prestamo de RAM se ha quedado corto ***\n");
        g_fallos++;
        return;
    }
    dsc_set_centro_hz(1700.0f);

    /* 1. sin señal */
    rnd_pon(0x1234567u);
    gen_ruido(0.02f);
    mete_dsc();
    a = tic(); mete_dsc(); b = tic();
    vq = mide_m("puesto, SIN SEÑAL (ruido)     ", a, b, NBUF); seg(vq, TASA);

    /* 2. con los dos tonos encima, a 100 baudios */
    dsc_stop(); dsc_start(FS_HZ); dsc_set_centro_hz(1700.0f);
    rnd_pon(0x1234567u);
    {
        unsigned i;
        for (i = 0U; i < 210U; i++) { s_bits[i] = (uint8_t)((i * 7U + (i >> 2)) & 1U); }
        s_nbits = 210U;
    }
    gen_fsk(1615.0f, 1785.0f, 100.0f, 0.5f, 0.02f);
    mete_dsc();
    a = tic(); mete_dsc(); b = tic();
    v = mide_m("con señal (tonos y reloj)     ", a, b, NBUF); seg(v, TASA);
    apunta("DSC        (dsc.c)        ", vq, v);

    /* 3. CERRAR UNA LLAMADA ENTERA. Por los simbolos, que es por donde
     *    entran una vez el reloj de bit los ha sacado: 26 parejas DX/RX y,
     *    al final, el parseo y los renglones de la pantalla. */
    m[n - 1U] = ecc_de_local(m, (uint8_t)(n - 1U));
    dsc_stop(); dsc_start(FS_HZ);
    dsc_emite(m, n);                       /* una de calentamiento */
    {
        int r;
        unsigned long ins;
        a = tic();
        for (r = 0; r < 20; r++) { dsc_stop(); dsc_start(FS_HZ); dsc_emite(m, n); }
        b = tic();
        ins = mide_s("cerrar una llamada entera     ", a, b, 20UL);
        pr("      (incluye los 52 simbolos, la diversidad, el ECC y pintar\n"
           "       los renglones; una llamada dura ~6 s en el aire)\n");
        (void)ins;
    }
    {
        dsc_info_t in;
        dsc_info(&in);
        pr("      llamadas buenas "); prn((unsigned long)in.llamadas);
        pr(", malas "); prn((unsigned long)in.malas);
        pr(", renglones "); prn((unsigned long)dsc_lineas()); pr("\n");
    }

    /* 4. LA BUSQUEDA DE TONOS, que es el reenganche de este modulo: corre
     *    desde el bucle principal mientras NO haya enganche, y se lee la
     *    FFT de la lupa de sintonia. */
    dsc_stop(); dsc_start(FS_HZ);
    rtty_scope_init();
    rnd_pon(0x3355779u);
    gen_fsk(1615.0f, 1785.0f, 100.0f, 0.5f, 0.02f);
    rtty_scope_feed(s_buf, 512U);
    rtty_scope_poll();
    /* Mal sintonizado a proposito: asi la busqueda encuentra algo y se
     * mueve, que es el caso que cuesta. */
    dsc_set_centro_hz(1200.0f);
    { int r; for (r = 0; r < 5; r++) { dsc_poll(); } }
    {
        int r;
        unsigned long ins;
        a = tic(); for (r = 0; r < 100; r++) { dsc_poll(); } b = tic();
        ins = mide_s("dsc_poll (buscar los tonos)   ", a, b, 100UL);
        seg_ev(ins, 30UL);
    }
    pr("      centro encontrado: "); prn((unsigned long)dsc_busca_hz()); pr(" Hz\n");
}

/* ---------------------------------------------------------------- PSK31 */

/*
 * BPSK31: 31,25 simbolos por segundo, inversion de fase para el cero y
 * fase quieta para el uno, con la envolvente de coseno alzado que usa
 * cualquier emisora -sin ella el espectro se ensancha y lo que se mediria
 * seria otra cosa-.
 */
static void gen_psk31(float hz, float amp, float nz)
{
    unsigned i;
    float fase = 0.0f, inc = 6.2831853f * hz / FS_HZ;
    float sps = FS_HZ / 31.25f;     /* 384 muestras por simbolo */
    float signo = 1.0f, sig_sig = 1.0f, sig_ant = 1.0f;
    unsigned sim_ant = 0U;

    for (i = 0U; i < NBUF; i++) {
        unsigned sim = (unsigned)((float)i / sps);
        float dentro = (float)i / sps - (float)sim;   /* 0..1 dentro del simbolo */
        float env, s;

        if (sim != sim_ant) {
            sim_ant = sim;
            sig_ant = signo;
            signo = sig_sig;
            /* No es el varicode de verdad, pero si su estadistica: mas unos
             * que ceros y nunca dos inversiones seguidas. Para lo que se
             * mide aqui -el coste por muestra- lo que importa es que haya
             * inversiones a 31,25 baudios, no que el texto diga algo. */
            sig_sig = ((sim * 2654435761u) & 0x30000u) ? signo : -signo;
        }
        /*
         * La envolvente solo baja donde hay inversion de fase: medio
         * simbolo de coseno alzado antes del cambio y medio despues. Es lo
         * que hace una emisora de PSK31 y es lo que mantiene la señal en
         * sus 31 Hz de ancho.
         */
        env = 1.0f;
        if (signo != sig_sig && dentro > 0.5f) {
            env = 0.5f + 0.5f * cosf(6.2831853f * (dentro - 0.5f));
        } else if (signo != sig_ant && dentro < 0.5f) {
            env = 0.5f + 0.5f * cosf(6.2831853f * dentro);
        }
        fase += inc;
        if (fase > 6.2831853f) { fase -= 6.2831853f; }
        s = signo * env * sinf(fase);
        s_buf[i] = amp * s + nz * ruido();
    }
}

static void banco_psk31(void)
{
    unsigned long a, b, v, vq;
    unsigned nc;

    pr("\n  PSK31  (psk31.c, BPSK 31,25 baudios, centro 1000 Hz)\n");

    psk31_start(FS_HZ);
    if (!psk31_activo()) { pr("    *** no arranca a 12 kHz ***\n"); g_fallos++; return; }
    psk31_set_centro_hz(1000.0f);

    /* 1. sin señal */
    rnd_pon(0x1234567u);
    gen_ruido(0.02f);
    mete_psk31(); (void)vacia_psk31();
    a = tic(); mete_psk31(); b = tic();
    vq = mide_m("puesto, SIN SEÑAL (ruido)     ", a, b, NBUF); seg(vq, TASA);
    (void)vacia_psk31();

    /* 2. con señal */
    rnd_pon(0x1234567u);
    gen_psk31(1000.0f, 0.5f, 0.02f);
    mete_psk31(); (void)vacia_psk31();
    a = tic(); mete_psk31(); b = tic();
    v = mide_m("con señal (fase girando)      ", a, b, NBUF); seg(v, TASA);
    apunta("PSK31      (psk31.c)      ", vq, v);
    nc = vacia_psk31();
    pr("      caracteres sacados en la pasada: "); prn(nc); pr("\n");

    /* 3. el reenganche: mover el centro recalcula el giro del oscilador */
    a = tic();
    { int r; for (r = 0; r < 200; r++) { psk31_set_centro_hz(1000.0f + (float)(r & 1)); } }
    b = tic();
    (void)mide_s("reenganche (mover el centro)  ", a, b, 200UL);
    psk31_set_centro_hz(1000.0f);

    a = tic();
    { int r; char c; for (r = 0; r < 500; r++) { (void)psk31_get_char(&c); } }
    b = tic();
    (void)mide_s("sacar un caracter (anillo vacio)", a, b, 500UL);
}

/* ------------------------------------------------------------------- CW */

static const struct { char c; const char *m; } k_morse[] = {
    {'A',".-"},   {'B',"-..."}, {'C',"-.-."}, {'D',"-.."},  {'E',"."},
    {'F',"..-."}, {'G',"--."},  {'H',"...."}, {'I',".."},   {'J',".---"},
    {'K',"-.-"},  {'L',".-.."}, {'M',"--"},   {'N',"-."},   {'O',"---"},
    {'P',".--."}, {'Q',"--.-"}, {'R',".-."},  {'S',"..."},  {'T',"-"},
    {'U',"..-"},  {'V',"...-"}, {'W',".--"},  {'X',"-..-"}, {'Y',"-.--"},
    {'Z',"--.."},
    {'0',"-----"},{'1',".----"},{'2',"..---"},{'3',"...--"},{'4',"....-"},
    {'5',"....."},{'6',"-...."},{'7',"--..."},{'8',"---.."},{'9',"----."},
    {'/',"-..-."},{'=',"-...-"}
};

static const char *morse_de(char c)
{
    unsigned i;
    for (i = 0U; i < sizeof k_morse / sizeof k_morse[0]; i++) {
        if (k_morse[i].c == c) { return k_morse[i].m; }
    }
    return 0;
}

/*
 * Morse de verdad: 1/3 para punto y raya, 1/3/7 para los huecos, y flancos
 * de coseno alzado de 5 ms. Sin los flancos el banco saldria a favor -un
 * escalon es mas facil de detectar que una rampa- y ademas no se parece a
 * ninguna emisora decente.
 */
static void gen_cw(const char *txt, float wpm, float pitch, float amp, float nz)
{
    unsigned punto = (unsigned)(14400.0f / wpm);   /* muestras de un punto */
    unsigned subida = (unsigned)(0.005f * FS_HZ);
    unsigned pos = punto * 4U, i;
    const char *p;
    static float env[NBUF];
    float fase = 0.0f, inc = 6.2831853f * pitch / FS_HZ;

    if (subida > punto / 4U) { subida = punto / 4U; }
    if (subida < 2U) { subida = 2U; }
    for (i = 0U; i < NBUF; i++) { env[i] = 0.0f; }

    for (p = txt; *p; p++) {
        const char *m;
        unsigned k;

        if (*p == ' ') { pos += punto * 4U; continue; }   /* 7 - 3 ya puestos */
        m = morse_de(*p);
        if (!m) { continue; }
        for (k = 0U; m[k]; k++) {
            unsigned largo = (m[k] == '-') ? (punto * 3U) : punto;
            unsigned j;
            for (j = 0U; j < largo && pos + j < NBUF; j++) {
                float e = 1.0f;
                if (j < subida) {
                    e = 0.5f - 0.5f * cosf(3.14159265f * (float)j / (float)subida);
                } else if (j + subida >= largo) {
                    e = 0.5f - 0.5f * cosf(3.14159265f * (float)(largo - j) / (float)subida);
                }
                env[pos + j] = e;
            }
            pos += largo + punto;        /* el hueco entre elementos */
        }
        pos += punto * 2U;               /* hasta los 3 del hueco de letra */
        if (pos >= NBUF) { break; }
    }

    for (i = 0U; i < NBUF; i++) {
        fase += inc;
        if (fase > 6.2831853f) { fase -= 6.2831853f; }
        s_buf[i] = amp * env[i] * sinf(fase) + nz * ruido();
    }
}

static void banco_cw(void)
{
    unsigned long a, b, v, vq;
    unsigned nc;

    pr("\n  CW  (cw.c, 13 sondas de Goertzel, ventana de 64, salto de 32)\n");

    cw_init();
    cw_set_enabled(1U);
    cw_set_pitch_hz(700.0f);
    cw_set_wpm_hint(20.0f);

    /*
     * 1. CON EL INTERRUPTOR APAGADO, Y SALE LO MISMO. No es un error del
     *    banco: cw_process() NO mira cw_get_enabled(), lo mira demod_am.c
     *    antes de llamarla (cw_process_chunks(), la rama de banda lateral).
     *    O sea que el interruptor ahorra la llamada entera, pero quien lo
     *    apaga desde dentro del modulo no ahorra nada.
     */
    cw_set_enabled(0U);
    gen_ruido(0.02f);
    mete_cw();
    a = tic(); mete_cw(); b = tic();
    v = mide_m("con cw_set_enabled(0)         ", a, b, NBUF); seg(v, TASA);

    /* 2. puesto y sin señal */
    cw_set_enabled(1U);
    rnd_pon(0x1234567u);
    gen_ruido(0.02f);
    mete_cw(); (void)vacia_cw();
    a = tic(); mete_cw(); b = tic();
    vq = mide_m("puesto, SIN SEÑAL (ruido)     ", a, b, NBUF); seg(vq, TASA);
    (void)vacia_cw();

    /* 3. puesto y decodificando */
    rnd_pon(0x1234567u);
    gen_cw("CQ CQ DE EA1ABC EA1ABC K ", 20.0f, 700.0f, 0.6f, 0.02f);
    mete_cw(); (void)vacia_cw();
    a = tic(); mete_cw(); b = tic();
    v = mide_m("con señal, 20 PPM decodificando", a, b, NBUF); seg(v, TASA);
    nc = vacia_cw();
    pr("      caracteres sacados en la pasada: "); prn(nc);
    pr(", decodificando "); prn((unsigned long)cw_get_decode_ok());
    pr(", calidad "); prn((unsigned long)(cw_get_quality() * 100.0f)); pr("\n");

    diferencial(v, vq, nc);
    apunta("CW         (cw.c)         ", vq, v);

    /* 4. con el enganche de tono apagado, para ver que ponen las 13 sondas */
    cw_set_autotune(0U);
    mete_cw(); (void)vacia_cw();
    a = tic(); mete_cw(); b = tic();
    v = mide_m("con señal, sin autoenganche   ", a, b, NBUF); seg(v, TASA);
    apunta("CW sin autoenganche       ", v, v);
    (void)vacia_cw();
    cw_set_autotune(1U);

    a = tic();
    { int r; char c; for (r = 0; r < 500; r++) { (void)cw_get_char(&c); } }
    b = tic();
    (void)mide_s("sacar un caracter (anillo vacio)", a, b, 500UL);

    pr("      velocidad medida "); prn((unsigned long)cw_get_wpm());
    pr(" PPM, tono "); prn((unsigned long)cw_get_detect_hz()); pr(" Hz\n");
}

/* ----------------------------------------------------------------- JTTY */

static jtty_rx_ram_t s_jtty_ram;

/*
 * 4-GFSK a 31,25 baudios: trece simbolos de sincronismo y cuarenta y seis
 * de datos, con los tonos donde la ventana de busqueda los espera -bin b
 * del medio-bin de 15,625 Hz, centrada en 1.500-.
 */
static void gen_jtty(uint8_t base_bin, float amp, float nz)
{
    static uint8_t tonos[JTTY_SYNC_SIM + JTTY_SIMBOLOS];
    uint8_t carga[JTTY_CARGA_BITS];
    unsigned i;
    float fase = 0.0f;
    unsigned sps = (unsigned)(FS_HZ / 31.25f);     /* 384 */
    unsigned lead = 768U;

    for (i = 0U; i < (unsigned)JTTY_CARGA_BITS; i++) {
        carga[i] = (uint8_t)((i * 5U + 3U) & 1U);
    }
    carga[JTTY_BIT_RESERVADO - 1U] = 0U;
    for (i = 0U; i < (unsigned)JTTY_SYNC_SIM; i++) { tonos[i] = k_jtty_sync[i]; }
    jtty_fec_codifica(carga, &tonos[JTTY_SYNC_SIM]);

    for (i = 0U; i < NBUF; i++) {
        float f;
        if (i < lead) {
            f = 0.0f;
        } else {
            unsigned s = (i - lead) / sps;
            if (s >= (unsigned)(JTTY_SYNC_SIM + JTTY_SIMBOLOS)) { f = 0.0f; }
            else {
                unsigned bin = (unsigned)base_bin + 2U * (unsigned)tonos[s];
                f = 1500.0f + ((float)bin - 32.0f) * 15.625f;
            }
        }
        if (f > 0.0f) {
            fase += 6.2831853f * f / FS_HZ;
            if (fase > 6.2831853f) { fase -= 6.2831853f; }
            s_buf[i] = amp * sinf(fase) + nz * ruido();
        } else {
            s_buf[i] = nz * ruido();
        }
    }
}

/*
 * Mete un trozo del buffer SIN vaciar la cola de candidatos: es lo que hace
 * la interrupcion del audio, que solo apunta.
 */
static void mete_jtty_n(unsigned desde, unsigned cuantas)
{
    unsigned i;
    for (i = desde; i < desde + cuantas; i += TROZO) { jtty_rx_mete(&s_buf[i], TROZO); }
}

/*
 * Y esto es la vuelta entera de la radio: la interrupcion apunta y el bucle
 * principal resuelve, unas treinta veces por segundo. Hace falta hacerlo
 * asi para medir el Viterbi, porque un candidato que se queda en la cola
 * mas de 160 filas -2,6 s- se sale del anillo del espectrograma y
 * jtty_rx_saca() lo tira sin probarlo. Sin vaciar, lo que se mediria seria
 * la cola desbordada, que es justo lo que en la radio no pasa.
 *
 * Deja en s_saca_tics los tics que se han ido en jtty_rx_saca(), y en
 * s_saca_n los candidatos que de verdad llegaron al Viterbi.
 */
static unsigned long s_saca_tics;
static unsigned long s_saca_n;

static void mete_jtty_vuelta(void)
{
    unsigned i;
    jtty_rx_r_t r;

    s_saca_tics = 0UL;
    s_saca_n = 0UL;
    for (i = 0U; i < NBUF; i += 384U) {      /* 32 ms de audio por vuelta */
        mete_jtty_n(i, 384U);
        {
            uint32_t p0 = jtty_rx_probados();
            unsigned long a = tic();
            int k;
            for (k = 0; k < 3; k++) { (void)jtty_rx_saca(&r); }
            s_saca_tics += ((a - tic()) & 0xFFFFFFUL);
            s_saca_n += (unsigned long)(jtty_rx_probados() - p0);
        }
    }
}

static void banco_jtty(void)
{
    unsigned long a, b, v, vs;
    uint32_t p0;

    pr("\n  JTTY  (jtty_rx.c, 4-GFSK 31,25 Bd, ventana de 64 medios bines)\n");

    /*
     * 1. SIN SEÑAL. Media pasada: con ruido puro salen unos cuatro
     *    candidatos por segundo (ver el umbral 0,45 en jtty_rx.c) y la cola
     *    son veinticuatro, asi que en 2,7 s no se desborda y lo que se mide
     *    es el camino de siempre y no el de "no cabe".
     */
    jtty_rx_start(&s_jtty_ram, FS_HZ);
    rnd_pon(0x1234567u);
    gen_ruido(0.02f);
    mete_jtty_vuelta();
    p0 = jtty_rx_probados();
    a = tic(); mete_jtty_n(0U, NBUF / 2U); b = tic();
    vs = mide_m("puesto, SIN SEÑAL (ruido)     ", a, b, NBUF / 2U); seg(vs, TASA);
    pr("      candidatos de ruido apuntados: ");
    prn((unsigned long)(jtty_rx_probados() - p0)); pr(" (en 2,7 s)\n");

    /* 2. con una trama de verdad delante */
    jtty_rx_stop();
    jtty_rx_start(&s_jtty_ram, FS_HZ);
    rnd_pon(0x1234567u);
    gen_jtty(24U, 0.5f, 0.02f);
    mete_jtty_vuelta();
    a = tic(); mete_jtty_n(0U, NBUF / 2U); b = tic();
    v = mide_m("con señal (trama entera)      ", a, b, NBUF / 2U); seg(v, TASA);
    apunta("JTTY       (jtty_rx.c)    ", vs, v);

    /*
     * 3. RESOLVER UN CANDIDATO: el Viterbi de lista 4 sobre 512 estados, el
     *    CRC de 12 bits y la gramatica. Corre desde el bucle principal, no
     *    desde la interrupcion, y es con diferencia lo mas caro de este
     *    modo.
     */
    jtty_rx_stop();
    jtty_rx_start(&s_jtty_ram, FS_HZ);
    rnd_pon(0x1234567u);
    gen_jtty(24U, 0.5f, 0.02f);
    mete_jtty_vuelta();                      /* calentar */
    mete_jtty_vuelta();                      /* y esta es la que cuenta */
    if (s_saca_n > 0UL) {
        unsigned long ins = (s_saca_tics * 40UL) / s_saca_n;
        unsigned long us100 = ins / 2UL;

        pr("    resolver un candidato (Viterbi)  "); prn(ins);
        pr(" ins = "); prn(us100 / 100UL); pr(",");
        if ((us100 % 100UL) < 10UL) { pr("0"); }
        prn(us100 % 100UL); pr(" us\n");
        seg_ev(ins, 4UL);   /* 4,3 candidatos/s con ruido puro, ver jtty_rx.c */
    } else {
        pr("    resolver un candidato: no hubo ninguno que llegase al Viterbi\n");
    }
    pr("      probados "); prn((unsigned long)jtty_rx_probados());
    pr(", buenos "); prn((unsigned long)jtty_rx_buenos());
    pr(", perdidos "); prn((unsigned long)jtty_rx_perdidos()); pr("\n");
    /*
     * BUENOS 0 Y NO ES UN FALLO DE LA MEDIDA. La carga que se sintetiza
     * aqui son 34 bits inventados: pasan el CRC -se codifican con el- pero
     * jtty_msg_lee() los tira despues, porque no son un mensaje de los que
     * el modo admite. No cambia ni una instruccion de lo medido: el Viterbi
     * corre entero antes de esa criba, y el bloque de aqui abajo lo
     * confirma midiendolo suelto con una trama que SI sale.
     */

    /*
     * 4. EL VITERBI SOLO, SIN EL RESTO DE jtty_rx_saca(). Sirve de
     *    comprobacion cruzada de lo de arriba -si casi todo el coste de
     *    resolver un candidato es esto, el numero de arriba es creible- y
     *    separa los dos casos que de verdad se dan: la trama que sale, y la
     *    que no sale, que es la que corre las cuatro listas enteras y
     *    ninguna cumple el CRC. En la banda, con cuatro candidatos de ruido
     *    por segundo, el caso normal es el segundo.
     */
    {
        static float en[JTTY_SIMBOLOS][4];
        uint8_t carga[JTTY_CARGA_BITS], tonos[JTTY_SIMBOLOS], sal[JTTY_CARGA_BITS];
        unsigned i, t;
        int r, ok;

        for (i = 0U; i < (unsigned)JTTY_CARGA_BITS; i++) {
            carga[i] = (uint8_t)((i * 5U + 3U) & 1U);
        }
        carga[JTTY_BIT_RESERVADO - 1U] = 0U;
        jtty_fec_codifica(carga, tonos);
        for (i = 0U; i < (unsigned)JTTY_SIMBOLOS; i++) {
            for (t = 0U; t < 4U; t++) {
                en[i][t] = (tonos[i] == (uint8_t)t) ? 1.0f : 0.05f;
            }
        }
        a = tic();
        ok = 0;
        for (r = 0; r < 5; r++) {
            ok = jtty_fec_descodifica((const float (*)[4])en, &s_jtty_ram.fec, 4U, sal);
        }
        b = tic();
        (void)mide_s("el Viterbi solo, trama limpia ", a, b, 5UL);
        pr("      sale el mensaje: "); prn((unsigned long)ok); pr("\n");

        rnd_pon(0x5577991u);
        for (i = 0U; i < (unsigned)JTTY_SIMBOLOS; i++) {
            for (t = 0U; t < 4U; t++) { en[i][t] = 0.5f + uni(); }
        }
        a = tic();
        for (r = 0; r < 5; r++) {
            ok = jtty_fec_descodifica((const float (*)[4])en, &s_jtty_ram.fec, 4U, sal);
        }
        b = tic();
        (void)mide_s("el Viterbi solo, puro ruido   ", a, b, 5UL);
        pr("      sale el mensaje: "); prn((unsigned long)ok); pr("\n");
    }

    /* Y lo que cuesta una llamada en vacio, que es lo que pasa casi
     * siempre: el bucle principal pregunta y no hay nada. */
    {
        jtty_rx_r_t r;
        int k;
        while (jtty_rx_saca(&r)) { }
        a = tic(); for (k = 0; k < 500; k++) { (void)jtty_rx_saca(&r); } b = tic();
        (void)mide_s("jtty_rx_saca sin candidatos   ", a, b, 500UL);
    }
    jtty_rx_stop();
}

/* ====================================================================== */

int main(void)
{
    reloj_on();

    /*
     * UNALIGN_TRP FUERA.
     *
     * m4/start.c lo pone a proposito, y para el dibujo esta bien: una
     * lectura desalineada en el bus del panel es un fallo de bus en la
     * radio. Pero estos modulos no tocan el EXMC: trabajan sobre .bss y
     * sobre memoria prestada, donde el Cortex-M4 PERMITE el acceso
     * desalineado -lento, pero legal- y el compilador lo usa. Con la trampa
     * puesta, un acceso que en la radio funciona tira el banco con un
     * UsageFault y lo que se mide es nada.
     *
     * Y no es hipotetico: con la trampa puesta este banco se cae en seco en
     * rtty_process(), en el corrimiento del historial de 128 flotantes
     * (memmove + memcpy, que el compilador saca como cargas de palabra
     * desalineadas). CFSR 0x01000000, "acceso DESALINEADO". En la radio eso
     * funciona desde el primer dia: es RAM normal, no el EXMC del panel.
     *
     * Se quita SOLO ese bit (bit 3 del CCR), y se dejan DIV_0_TRP y los
     * tres manejadores con nombre, que si valen aqui.
     */
    *(volatile uint32_t *)0xE000ED14UL &= ~(1UL << 3);
    __asm__ volatile("dsb"); __asm__ volatile("isb");

    pr("\nLOS DECODIFICADORES DIGITALES DE TONOS, EN UN CORTEX-M4 DE VERDAD\n");
    pr("\n  Todos cuelgan de demod_am.c sobre s_ssb_dec, a 12.000 muestras\n");
    pr("  por segundo. 1 ins/muestra = 12.000 ins/s = 0,6 % de la CPU.\n");

    banco_rtty();
    banco_rtty_auto();
    banco_navtex();
    banco_dsc();
    banco_psk31();
    banco_cw();
    banco_jtty();

    resumen();

    pr("\n");
    pila_di(0UL);
    pr("\n");
    if (g_fallos) { pr("  HAY FALLOS\n\n"); } else { pr("  TODO CORRECTO\n\n"); }
    salir();
    return 0;
}
