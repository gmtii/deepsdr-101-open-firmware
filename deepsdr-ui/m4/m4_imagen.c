/*
 * LO QUE CUESTAN LOS MODOS DE IMAGEN, EN INSTRUCCIONES DE CORTEX-M4.
 *
 * SSTV, WEFAX y Feld-Hell son los tres modos que NO entregan texto: entregan
 * pixeles. Y los tres comen audio MUESTRA A MUESTRA dentro de la
 * interrupcion, a 12.000 por segundo -DEMOD_DEC_FS_HZ-, no por bloques como
 * el camino de audio. Eso cambia como hay que leer el numero: aqui no vale
 * "instrucciones por bloque", lo que manda es lo que cuesta UNA muestra
 * multiplicado por doce mil.
 *
 * EL PORQUE DE ESTE BANCO. m4_audio.c y m4_demod.c pesan el camino de audio
 * -la cabecera de RF, el demodulador, el reductor de ruido-, y eso corre
 * SIEMPRE. Lo de aqui se suma encima cuando el usuario pone uno de estos
 * modos, y se suma dentro de la misma interrupcion. Lo que quede despues es
 * lo que tiene el bucle principal para pintar la cascada, y ya se sabe lo
 * que pasa con ese divisor: ver m4_vuelta.c.
 *
 * COMO SE LEE. Un tic del SysTick son 40 instrucciones y el GD32 va a 200
 * MHz (ver m4/LEEME.md). El tanto por ciento de cada segundo que se lleva
 * algo que ocurre N veces por segundo es instrucciones x N / 200.000.000. En
 * la radio de verdad lo medido se multiplica por ~1,7, que es el factor
 * calibrado del LEEME.
 *
 * COMO SE MIDE, QUE ES LA MITAD DEL VALOR.
 *
 * El audio se le da al decodificador en trozos de 64 muestras, que es lo que
 * hace el generador de sim/sstv_genera.h, y CADA TROZO SE CRONOMETRA POR
 * SEPARADO. Despues se mira en que estado estaba el decodificador antes del
 * trozo y que hizo durante el, y con eso los trozos se reparten en tres
 * montones:
 *
 *   - los que cayeron buscando la cabecera  -> el coste de BUSCAR
 *   - los que cayeron pintando y no cerraron linea -> el coste de PINTAR
 *   - los que cerraron una linea -> el de pintar MAS el cierre
 *
 * Y el cierre sale de restar: trozo con cierre menos trozo sin cierre. No
 * hace falta tocar sstv.c para sacar linea_cierra() a la luz -es estatica- y
 * no se toca.
 *
 * LA SENAL ES DE VERDAD. El SSTV se fabrica con sim/sstv_genera.h, que es el
 * mismo generador que usan los bancos del PC: saca la cabecera VIS, los
 * pulsos de sincronismo y los barridos con los tiempos que da sstv_plan(),
 * o sea los mismos que lee el decodificador. Con silencio el decodificador
 * no hace el mismo trabajo: no engancha, no cierra lineas y no reparte ni
 * una muestra en su cajon.
 *
 * El fax y el Hell no tienen generador compartido en sim/, asi que el suyo
 * esta aqui abajo. Son dos tonos y una envolvente; lo que importa es que el
 * brillo SE MUEVA, porque es lo que mete al decodificador por todas sus
 * ramas.
 *
 * ESTO NO ES UN VIGILANTE. No hay topes ni horquillas: nadie se pone rojo
 * porque un numero suba. Es una MEDIDA.
 */
#include "semi.h"

void pila_di(unsigned long maximo);

#include <stdint.h>
#include <string.h>
#include <math.h>

#include "sstv.h"
#include "wefax.h"
#include "hell.h"
#include "disc_fm.h"

int g_fallos;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* La tasa a la que les entra el audio. Los tres arrancan con
 * DEMOD_DEC_FS_HZ (ver main.c: sstv_start/wefax_start/hell_start), que son
 * 12 kHz. No es un numero de este banco: es el de la radio. */
#define FS 12000.0
#define FS_F 12000.0f

/* ==========================================================================
 * LOS TAPONES, QUE VIVEN AQUI Y NO EN EL ARBOL
 * ==========================================================================
 *
 * EL POZO DE RAM PRESTADA. Los tres modos guardan su estado y sus lineas en
 * la memoria de la cascada, prestada mientras el modo esta puesto (ver
 * User/hfdl_ram.h). hfdl_ram.c de verdad arrastra waterfall.h y con el el
 * dibujo entero, que aqui no pinta nada.
 *
 * Las firmas estan COPIADAS de User/hfdl_ram.h, no escritas de memoria: C no
 * comprueba firmas entre ficheros y un dia que se escribieron a ojo el
 * tamano llego como cero y el modo arranco sin memoria sin decir nada.
 */
static unsigned char g_ram_pozo[0xD4A0];
static unsigned      g_ram_cog, g_ram_sue;

void           hfdl_ram_coge(void)  { g_ram_cog++; }
unsigned char *hfdl_ram_todo(unsigned *cap)
{
    if (cap) { *cap = (unsigned)sizeof g_ram_pozo; }
    return g_ram_pozo;
}
void           hfdl_ram_suelta(void) { g_ram_sue++; }

/* El cerrojo de interrupciones que usa wefax.c para los tres indices del
 * anillo (WFX_CIERRA/WFX_ABRE). Aqui no hay interrupcion que cerrar, pero
 * las llamadas SI se pagan y por eso los tapones estan vacios y no
 * borrados: lo que se mide es el codigo del firmware con sus llamadas. */
void     __disable_irq(void) { }
void     __enable_irq(void)  { }
uint32_t __get_PRIMASK(void) { return 0UL; }
void     __set_PRIMASK(uint32_t v) { (void)v; }

/* ==========================================================================
 * LA CUENTA
 * ========================================================================== */

/* De tics acumulados a instrucciones por vez. 1 tic = 40 instrucciones. */
static unsigned long instr(unsigned long tics, unsigned long n)
{
    if (n == 0UL) { return 0UL; }
    return (tics * 40UL) / n;
}

/*
 * LO MISMO PERO EN CENTESIMAS, Y HACE FALTA.
 *
 * El cierre de linea sale de restar: lo que costo el trozo de 64 muestras
 * donde cayo el cierre menos lo que cuestan 64 muestras normales. Si el
 * coste por muestra se redondea a instrucciones enteras, ese redondeo se
 * multiplica por 64 y el error de la resta es de hasta 32 instrucciones.
 *
 * En SSTV da igual -el cierre son miles-, pero el del fax y el de la
 * columna de Hell son de decenas: con el redondeo entero, la medida valia
 * menos que lo medido. Por eso la resta se hace en centesimas.
 */
static unsigned long instr_x100(unsigned long tics, unsigned long n)
{
    if (n == 0UL) { return 0UL; }
    return (unsigned long)(((unsigned long long)tics * 4000ULL)
                           / (unsigned long long)n);
}

/*
 * LO QUE CUESTA UNA LINEA ENTERA, SIN EXTRAPOLAR.
 *
 * Es la suma de TODOS los tics de la ventana medida dividida entre las
 * lineas que se cerraron en ella. No hay estimacion ni redondeo que se
 * multiplique: la ventana empieza y acaba en linea entera, asi que esto es
 * la medida directa y es el numero que hay que creerse.
 */
static unsigned long por_linea(unsigned long tics_llano, unsigned long tics_cierre,
                               unsigned long n_lineas)
{
    return instr(tics_llano + tics_cierre, n_lineas);
}

/*
 * Y EL CIERRE SOLO, QUE ES LO QUE SOBRA.
 *
 * Linea entera menos lo que cuestan sus muestras al precio de las muestras
 * llanas. En SSTV el cierre son miles de instrucciones y la resta se ve
 * sola; en el fax y en Hell son decenas, o sea por debajo de lo que este
 * metodo distingue, y entonces lo honrado es decir eso y no un numero.
 *
 * El suelo del metodo: el coste por muestra se conoce con una centesima de
 * instruccion, asi que multiplicado por las muestras de una linea el error
 * es de +-N/100 instrucciones -unas 60 en el fax-. Por debajo de ahi, cero
 * significa "no se distingue", no "no cuesta nada".
 */
static unsigned long cierre_solo(unsigned long ins_linea, unsigned long ins_muestra_x100,
                                 unsigned long n_muestras)
{
    unsigned long muestras = (ins_muestra_x100 * n_muestras) / 100UL;
    return (ins_linea > muestras) ? (ins_linea - muestras) : 0UL;
}

static void suelo_di(unsigned long n_muestras)
{
    pr("        (el suelo de esta resta son +-");
    prn(n_muestras / 100UL);
    pr(" instrucciones)\n");
}

static void di(const char *que, unsigned long ins)
{
    pr("    "); pr(que); pr("  ");
    prn(ins); pr(" instrucciones  = ");
    prn(ins / 200UL); pr(",");
    prn(((ins % 200UL) * 10UL) / 200UL);
    pr(" us a 200 MHz\n");
}

/*
 * El tanto por ciento de cada segundo. `mil_veces` es cuantas veces ocurre
 * esto en MIL segundos, que es la unica forma de escribir "2,24 lineas por
 * segundo" con enteros sin perder los decimales que importan.
 *
 * Se saca con cuatro decimales porque aqui conviven dos escalas muy
 * distintas: lo que cuesta una muestra se lleva puntos enteros del segundo
 * y lo que cuesta cerrar una linea se lleva milesimas. Con dos decimales la
 * segunda salia "0,00" y eso no es una medida, es un hueco.
 */
static void porciento(unsigned long ins, unsigned long mil_veces)
{
    unsigned long long e6 = ((unsigned long long)ins * (unsigned long long)mil_veces)
                            / 200000ULL;
    unsigned long ent = (unsigned long)(e6 / 10000ULL);
    unsigned long dec = (unsigned long)(e6 % 10000ULL);

    pr("      se lleva ");
    prn(ent); pr(",");
    if (dec < 1000UL) { pr("0"); }
    if (dec < 100UL)  { pr("0"); }
    if (dec < 10UL)   { pr("0"); }
    prn(dec);
    pr(" % de cada segundo\n");
}

/* ==========================================================================
 * EL CRONOMETRO POR TROZOS, PARA SSTV
 * ========================================================================== */

/*
 * EL RESUMEN DE LA ULTIMA FILA.
 *
 * Los costes por muestra de cada modo, guardados segun se van midiendo,
 * para poder sacarlos juntos y ordenados al final. Leer una tabla es lo que
 * contesta "¿cual de los tres se lleva mas?", y con los bloques separados
 * hay que ir sumando a mano.
 */
#define RES_N 8U
static const char *g_res_que[RES_N];
static unsigned long g_res_ins[RES_N];
static unsigned g_res_n;

static void apunta(const char *que, unsigned long ins)
{
    if (g_res_n < RES_N) { g_res_que[g_res_n] = que; g_res_ins[g_res_n] = ins; g_res_n++; }
}

static unsigned long g_t_busca, g_n_busca;   /* tics y muestras buscando  */
static unsigned long g_t_pinta, g_n_pinta;   /* tics y muestras pintando  */
static unsigned long g_t_cierr, g_n_cierr;   /* tics y TROZOS con cierre  */
static uint8_t       g_mide;                 /* 0 mientras se calienta    */

static void sstv_ceros(void)
{
    g_t_busca = 0UL; g_n_busca = 0UL;
    g_t_pinta = 0UL; g_n_pinta = 0UL;
    g_t_cierr = 0UL; g_n_cierr = 0UL;
}

/*
 * El envoltorio. El generador de sim/ llama a sstv_process() desde dentro de
 * emite(); con el #define de mas abajo esa llamada entra aqui, se cronometra
 * y se reparte. El generador no se toca: es una cabecera de funciones
 * estaticas y lo unico que cambia es a quien llama.
 *
 * Mirar el estado cuesta -sstv_info() copia la estructura- y por eso se mira
 * FUERA de la ventana del cronometro, antes y despues.
 */
static void mi_sstv_process(const float *a, uint32_t n)
{
    sstv_info_t i0, i1;
    unsigned long t0, t1, d;

    if (!g_mide) { sstv_process(a, n); return; }

    sstv_info(&i0);
    t0 = tic();
    sstv_process(a, n);
    t1 = tic();
    d = (t0 - t1) & 0xFFFFFFUL;
    sstv_info(&i1);

    if (i0.estado == (uint8_t)SSTV_BUSCA) {
        g_t_busca += d; g_n_busca += n;
    } else if (i1.linea != i0.linea || i1.imagenes != i0.imagenes) {
        g_t_cierr += d; g_n_cierr++;
    } else {
        g_t_pinta += d; g_n_pinta += n;
    }
}

/* Y a partir de aqui, sstv_process() es el envoltorio. El generador se
 * incluye DESPUES a proposito. */
#define sstv_process mi_sstv_process
#include "sstv_genera.h"
#undef  sstv_process

/* ==========================================================================
 * LO QUE CUESTA EL DISCRIMINADOR SOLO
 * ==========================================================================
 *
 * disc_fm_paso() es la pieza que SSTV y WEFAX comparten, y es la unica que
 * se puede pesar aparte sin tocar nada: vive en su propio fichero objeto, no
 * se declara `inline` y no hay optimizacion de enlace, asi que dentro de
 * sstv_process() y de wefax_process() es una llamada de verdad y restarla
 * tiene sentido.
 *
 * Feld-Hell NO lo usa -no hay nada que discriminar: la informacion va en la
 * amplitud-, asi que ahi este reparto no existe.
 */
static disc_fm_t s_disc_solo;
static float     s_audio[512];
static volatile float s_sumidero;

static unsigned long mide_disc(void)
{
    unsigned long t0, t1;
    int r;
    uint32_t k;
    float s = 0.0f;

    disc_fm_init(&s_disc_solo, 1900.0f, FS_F);
    /* Unas pasadas antes de medir: los dos promediadores en cascada tienen
     * doce muestras de memoria cada uno y la primera vuelta no es la que se
     * repite doce mil veces por segundo. */
    for (k = 0U; k < 512U; k++) { s += disc_fm_paso(&s_disc_solo, s_audio[k]); }

    t0 = tic();
    for (r = 0; r < 20; r++) {
        for (k = 0U; k < 512U; k++) { s += disc_fm_paso(&s_disc_solo, s_audio[k]); }
    }
    t1 = tic();
    s_sumidero = s;
    return instr((t0 - t1) & 0xFFFFFFUL, 20UL * 512UL);
}

/* Un trozo de audio con el brillo moviendose, que es lo que de verdad llega:
 * un barrido de 1.500 a 2.300 Hz con ruido encima. Un tono quieto no vale
 * -el discriminador trabaja igual, pero el que lo lee no-. */
static void audio_llena(void)
{
    uint32_t k;
    double fase = 0.0;
    unsigned long sem = 0x2468ACEuL;

    for (k = 0U; k < 512U; k++) {
        double hz = 1500.0 + 800.0 * (double)(k % 128U) / 128.0;
        double n;
        sem = sem * 1103515245uL + 12345uL;
        n = (double)((sem >> 16) & 0x7FFF) / 32767.0 - 0.5;
        fase += 2.0 * M_PI * hz / FS;
        if (fase > 2.0 * M_PI) { fase -= 2.0 * M_PI; }
        s_audio[k] = (float)(sin(fase) + 0.08 * n);
    }
}

/* ==========================================================================
 * SSTV
 * ========================================================================== */

/*
 * Una tanda: cabecera VIS del modo que sea y despues unas cuantas lineas.
 *
 * `n_lineas` son lineas TRANSMITIDAS, no renglones de la foto: en la familia
 * PD cada linea transmitida trae dos renglones, y es la linea transmitida la
 * que lleva un sincronismo y un cierre.
 *
 * Las dos primeras no se miden. La rejilla de sincronismo se asienta con el
 * segundo pulso -ver s_t0_de_sync en sstv.c- y el primer cierre no es el que
 * se repite doscientas cincuenta y seis veces.
 */
static void sstv_tanda(const char *nombre, uint8_t modo, int n_lineas,
                       unsigned long ins_disc)
{
    tx_t t;
    sstv_plan_t pl;
    sstv_info_t inf;
    int i, y, paso;
    unsigned long i_busca, i_pinta, i_trozo, i_cierre;
    unsigned long n_linea_muestras, mil_lineas, ins_linea;
    uint16_t yy;

    sstv_plan(modo, &pl);
    paso = (pl.lineas_img == 2U) ? 2 : 1;

    sstv_stop();
    sstv_start(FS_F);
    sstv_auto(1U);          /* como arranca la radio con el modo en "auto" */

    t.fase = 0.0; t.ppm = 0.0; t.semilla = 0x13579BDuL; t.sigma = 0.05;
    emite_reinicia();
    g_max_y = -1;

    pr("\n  "); pr(nombre); pr("\n");

    /* 1. LA BUSQUEDA DE CABECERA. Es el estado en el que la radio esta
     *    mientras no hay foto, o sea casi todo el rato. */
    sstv_ceros();
    g_mide = 1U;
    cabecera_vis(&t, sstv_modo_vis(modo));
    g_mide = 0U;
    i_busca = instr(g_t_busca, g_n_busca);

    /* 2. Dos lineas de calentamiento y a medir. */
    y = 0;
    linea(&t, modo, y); y += paso;
    linea(&t, modo, y); y += paso;
    while (sstv_linea(&yy) != 0) { }

    sstv_ceros();
    g_mide = 1U;
    for (i = 0; i < n_lineas; i++) { linea(&t, modo, y); y += paso; }
    g_mide = 0U;

    i_pinta  = instr(g_t_pinta, g_n_pinta);
    i_trozo  = instr_x100(g_t_pinta, g_n_pinta);
    n_linea_muestras = (unsigned long)((double)pl.linea * FS + 0.5);
    ins_linea = por_linea(g_t_pinta, g_t_cierr, g_n_cierr);
    i_cierre  = cierre_solo(ins_linea, i_trozo, n_linea_muestras);

    sstv_info(&inf);
    pr("      modo enganchado: ");
    pr(inf.modo ? inf.modo : "ninguno");
    pr(",  renglones pintados ");
    prn((unsigned long)(g_max_y + 1));
    pr(",  cierres medidos ");
    prn(g_n_cierr);
    pr("\n");

    /* La linea transmitida, en muestras, de los tiempos del propio modo. */
    mil_lineas = (unsigned long)(1000.0 / (double)pl.linea + 0.5);

    apunta(nombre, i_pinta);
    di("por muestra, buscando cabecera ", i_busca);
    porciento(i_busca, 12000000UL);
    di("por muestra, pintando imagen   ", i_pinta);
    porciento(i_pinta, 12000000UL);
    pr("        de eso, el discriminador ");
    prn(ins_disc);
    pr(" y el resto de sstv.c ");
    prn((i_pinta > ins_disc) ? (i_pinta - ins_disc) : 0UL);
    pr("\n");
    di("cerrar UNA linea               ", i_cierre);
    porciento(i_cierre, mil_lineas);
    suelo_di(n_linea_muestras);

    di("una linea entera, con muestras ", ins_linea);
    porciento(ins_linea, mil_lineas);
    pr("        (la linea son ");
    prn(n_linea_muestras);
    pr(" muestras y llegan ");
    prn(mil_lineas / 1000UL); pr(",");
    prn((mil_lineas % 1000UL) / 100UL);
    pr(" por segundo)\n");

    sstv_stop();
}

/*
 * Y CUANTO DE LA BUSQUEDA ES LA DETECCION AUTOMATICA DE MODO.
 *
 * La radio arranca con ella puesta cuando el selector de modo esta en
 * "auto" (ver main.c), asi que la medida de arriba es la del caso normal.
 * Pero sstv_auto() se puede apagar, y entonces sstv_process() se salta
 * auto_pulso() y la cuenta de bajadas. Esto pesa la diferencia con la misma
 * cabecera, que es la unica forma de que la resta signifique algo.
 */
static unsigned long sstv_busca_sin_auto(void)
{
    tx_t t;

    sstv_stop();
    sstv_start(FS_F);
    sstv_auto(0U);

    t.fase = 0.0; t.ppm = 0.0; t.semilla = 0x13579BDuL; t.sigma = 0.05;
    emite_reinicia();

    sstv_ceros();
    g_mide = 1U;
    cabecera_vis(&t, sstv_modo_vis(0U));
    g_mide = 0U;

    sstv_stop();
    return instr(g_t_busca, g_n_busca);
}

/* ==========================================================================
 * WEFAX
 * ==========================================================================
 *
 * No hay generador de fax en sim/, asi que este es suyo. Es mas simple que
 * el de SSTV porque el formato lo es: no hay cabecera ni sincronismo, solo
 * un tono cuya frecuencia ES el brillo. 1500 Hz negro, 2300 blanco.
 *
 * El brillo se mueve -franjas y una rampa- porque es lo que mete al
 * decodificador por la rama de "este punto ha cambiado": con un gris fijo,
 * px no cambiaria nunca y el reparto en puntos no se pagaria.
 */
static unsigned long w_t_pinta, w_n_pinta, w_t_cierr, w_n_cierr;
static uint8_t       w_mide;

static void mi_wefax_process(const float *a, uint32_t n)
{
    wefax_info_t i0, i1;
    unsigned long t0, t1, d;

    if (!w_mide) { wefax_process(a, n); return; }

    wefax_info(&i0);
    t0 = tic();
    wefax_process(a, n);
    t1 = tic();
    d = (t0 - t1) & 0xFFFFFFUL;
    wefax_info(&i1);

    if (i1.lineas != i0.lineas) { w_t_cierr += d; w_n_cierr++; }
    else                        { w_t_pinta += d; w_n_pinta += n; }
}

static uint8_t w_recoge[WEFAX_ANCHO];

static void wefax_emite(double seg, double *fase, unsigned long *sem,
                        unsigned long *muestra)
{
    float buf[64];
    int j = 0;
    long n = (long)(seg * FS + 0.5);
    long k;

    for (k = 0; k < n; k++) {
        unsigned long m = *muestra;
        /* Una carta: rampa de gris a lo ancho, con barras cada tanto. */
        uint8_t gris = (uint8_t)(((m % 6000UL) * 255UL) / 6000UL);
        double hz, r;

        if (((m / 600UL) % 2UL) == 0UL && ((m % 600UL) < 60UL)) { gris = 255U; }
        hz = (double)wefax_hz_de_gris(gris);

        *sem = *sem * 1103515245uL + 12345uL;
        r = (double)((*sem >> 16) & 0x7FFF) / 32767.0 - 0.5;

        *fase += 2.0 * M_PI * hz / FS;
        if (*fase > 2.0 * M_PI) { *fase -= 2.0 * M_PI; }
        buf[j++] = (float)(sin(*fase) + 0.05 * r);
        (*muestra)++;

        if (j == 64) {
            mi_wefax_process(buf, 64U);
            j = 0;
            /* Y se vacia el anillo, que es lo que hace el bucle principal.
             * Sin esto el anillo se llena y la rama que se mide es la de
             * tirar la linea mas vieja, que no es la de todos los dias. */
            while (wefax_linea_en(w_recoge)) { }
        }
    }
}

static void wefax_banco(unsigned long ins_disc)
{
    double fase = 0.0;
    unsigned long sem = 0x5A5A5A5uL, muestra = 0UL;
    unsigned long i_pinta, i_trozo, i_cierre, ins_linea;
    unsigned long n_linea_muestras, mil_lineas;
    wefax_info_t inf;

    pr("\n  WEFAX, 120 lineas por minuto\n");

    wefax_stop();
    wefax_start(FS_F);
    wefax_set_lpm(120U);
    wefax_fuerza_imagen();

    /* Medio segundo de calentamiento: una linea entera, para que el reparto
     * en puntos y el anillo esten donde van a estar. */
    w_mide = 0U;
    wefax_emite(0.6, &fase, &sem, &muestra);

    w_t_pinta = w_n_pinta = w_t_cierr = w_n_cierr = 0UL;
    w_mide = 1U;
    wefax_emite(3.0, &fase, &sem, &muestra);   /* seis lineas a 120 lpm */
    w_mide = 0U;

    n_linea_muestras = (unsigned long)(FS * 60.0 / 120.0 + 0.5);   /* 6.000 */
    mil_lineas = 2000UL;                                           /* 2 por s */

    i_pinta   = instr(w_t_pinta, w_n_pinta);
    i_trozo   = instr_x100(w_t_pinta, w_n_pinta);
    ins_linea = por_linea(w_t_pinta, w_t_cierr, w_n_cierr);
    i_cierre  = cierre_solo(ins_linea, i_trozo, n_linea_muestras);

    wefax_info(&inf);
    pr("      estado "); prn((unsigned long)inf.estado);
    pr(",  lineas "); prn(inf.lineas);
    pr(",  cierres medidos "); prn(w_n_cierr);
    pr("\n");

    apunta("WEFAX 120 lpm", i_pinta);
    di("por muestra                    ", i_pinta);
    porciento(i_pinta, 12000000UL);
    pr("        de eso, el discriminador ");
    prn(ins_disc);
    pr(" y el resto de wefax.c ");
    prn((i_pinta > ins_disc) ? (i_pinta - ins_disc) : 0UL);
    pr("\n");
    di("cerrar UNA linea               ", i_cierre);
    porciento(i_cierre, mil_lineas);
    suelo_di(n_linea_muestras);

    di("una linea entera, con muestras ", ins_linea);
    porciento(ins_linea, mil_lineas);
    pr("        (la linea son ");
    prn(n_linea_muestras);
    pr(" muestras y llegan 2 por segundo)\n");

    wefax_stop();
}

/* ==========================================================================
 * FELD-HELL
 * ==========================================================================
 *
 * Manipulacion de portadora: el tono esta o no esta. El decodificador no
 * discrimina frecuencia, mira la ENVOLVENTE, asi que el generador solo tiene
 * que encender y apagar un tono al ritmo de los puntos -245 por segundo- con
 * un dibujo que no sea todo unos ni todo ceros: si no hay contraste, el
 * seguidor de pico y el de valle se juntan y el decodificador se va por la
 * rama corta de "no hay senal", que no es la que corre de verdad.
 */
static unsigned long h_t_punto, h_n_punto, h_t_col, h_n_col;
static uint8_t       h_mide;

static void mi_hell_process(const float *a, uint32_t n)
{
    hell_info_t i0, i1;
    unsigned long t0, t1, d;

    if (!h_mide) { hell_process(a, n); return; }

    hell_info(&i0);
    t0 = tic();
    hell_process(a, n);
    t1 = tic();
    d = (t0 - t1) & 0xFFFFFFUL;
    hell_info(&i1);

    if (i1.columnas != i0.columnas) { h_t_col += d; h_n_col++; }
    else                            { h_t_punto += d; h_n_punto += n; }
}

static void hell_emite(double seg, double *fase, unsigned long *sem,
                       unsigned long *muestra)
{
    float buf[64];
    int j = 0;
    long n = (long)(seg * FS + 0.5);
    long k;
    const double n_punto = FS / (double)HELL_PUNTOS_HZ;   /* 48,98 muestras */

    for (k = 0; k < n; k++) {
        unsigned long p = (unsigned long)((double)(*muestra) / n_punto);
        /* Un dibujo de letra: encendido y apagado sin periodo corto, para
         * que el pico y el valle se separen de verdad. */
        unsigned long c = p / 14UL;          /* columna */
        unsigned long f = p % 14UL;          /* punto dentro de la columna */
        int on = (int)(((c + f) % 5UL) < 3UL);
        double r;

        *sem = *sem * 1103515245uL + 12345uL;
        r = (double)((*sem >> 16) & 0x7FFF) / 32767.0 - 0.5;

        *fase += 2.0 * M_PI * 800.0 / FS;
        if (*fase > 2.0 * M_PI) { *fase -= 2.0 * M_PI; }
        buf[j++] = (float)((on ? 1.0 : 0.0) * sin(*fase) + 0.04 * r);
        (*muestra)++;

        if (j == 64) {
            mi_hell_process(buf, 64U);
            j = 0;
            while (hell_columna() != 0) { }
        }
    }
}

static void hell_banco(void)
{
    double fase = 0.0;
    unsigned long sem = 0x0BADC0DEuL, muestra = 0UL;
    unsigned long i_punto, i_trozo, i_cierre, i_col;
    hell_info_t inf;

    pr("\n  FELD-HELL\n");

    hell_stop();
    hell_start(FS_F);

    h_mide = 0U;
    hell_emite(0.5, &fase, &sem, &muestra);   /* los seguidores, en su sitio */

    h_t_punto = h_n_punto = h_t_col = h_n_col = 0UL;
    h_mide = 1U;
    hell_emite(2.0, &fase, &sem, &muestra);
    h_mide = 0U;

    i_punto  = instr(h_t_punto, h_n_punto);
    i_trozo  = instr_x100(h_t_punto, h_n_punto);
    /* Una columna son 14 puntos y un punto 48,98 muestras: 686 muestras. */
    i_col    = por_linea(h_t_punto, h_t_col, h_n_col);
    i_cierre = cierre_solo(i_col, i_trozo, 686UL);

    hell_info(&inf);
    pr("      columnas "); prn(inf.columnas);
    pr(",  cierres medidos "); prn(h_n_col);
    pr("\n");

    apunta("FELD-HELL", i_punto);
    di("por muestra                    ", i_punto);
    porciento(i_punto, 12000000UL);
    pr("        aqui no hay discriminador: la informacion va en la amplitud\n");
    di("cerrar UNA columna             ", i_cierre);
    porciento(i_cierre, 17500UL);     /* 17,5 columnas por segundo */
    suelo_di(686UL);
    di("una columna entera, 686 muestras", i_col);
    porciento(i_col, 17500UL);

    hell_stop();
}

/* ========================================================================== */

/*
 * LA TRAMPA DE DESALINEADO SE QUITA AQUI, Y HAY QUE DECIR POR QUE.
 *
 * m4/start.c pone UNALIGN_TRP, y para los bancos de dibujo esta muy bien:
 * ahi un acceso desalineado al bus del panel es un fallo de verdad. Pero
 * sstv_plan() se cae con el, y NO es un fallo de sstv.c.
 *
 * Lo que pasa: en sstv_modo_t los campos `ancho` y `alto` son dos uint16_t
 * seguidos y caen en los bytes 10 y 12 de una estructura de 40, o sea que
 * el par empieza en una direccion par pero no multiplo de cuatro. El
 * compilador junta las dos lecturas en UN `ldr` de palabra -es lo que hay
 * que hacer, son dos accesos menos- y esa palabra sale desalineada.
 *
 * En ARMv7-M eso es legal y el nucleo lo resuelve solo; cuesta unos ciclos
 * mas, no un fallo. Y la radio corre asi: ni boot/ ni User/ tocan
 * UNALIGN_TRP, o sea que lleva el valor de reinicio, que es apagado. Dejar
 * la trampa puesta aqui mediria un micro que no es el de la radio y, peor,
 * se pararia antes de medir nada.
 *
 * Se quita SOLO esta: la de dividir entre cero y los vectores con nombre se
 * quedan como estan.
 */
#define SCB_CCR_AQUI (*(volatile uint32_t*)0xE000ED14UL)

static void desalineado_permitido(void)
{
    SCB_CCR_AQUI &= ~(1UL << 3);
    __asm__ volatile("dsb");
    __asm__ volatile("isb");
}

int main(void)
{
    unsigned long ins_disc;

    desalineado_permitido();
    reloj_on();

    pr("\nLOS MODOS DE IMAGEN EN UN CORTEX-M4 DE VERDAD\n");
    pr("\n  Los tres comen audio a 12.000 muestras por segundo, muestra a\n");
    pr("  muestra y dentro de la interrupcion. Un tic son 40 instrucciones\n");
    pr("  y el GD32 va a 200 MHz; en la radio, por 1,7.\n");

    audio_llena();

    pr("\n  LA PIEZA COMPARTIDA\n");
    ins_disc = mide_disc();
    di("disc_fm_paso(), una muestra    ", ins_disc);
    porciento(ins_disc, 12000000UL);

    /* Uno rapido y uno lento. El coste POR MUESTRA es casi el mismo -el
     * bucle es el mismo-, pero el de linea no: Martin 1 cierra una linea
     * cada 446 ms y PD240 cada 1,2 s, y ademas PD cierra DOS renglones de
     * imagen de una vez con su conversion de color. */
    sstv_tanda("SSTV Martin 1 (indice 0)", 0U, 6, ins_disc);
    sstv_tanda("SSTV PD240 (indice 12)", 12U, 3, ins_disc);
    /* Y uno de la familia Scottie, que reparte la linea de otra manera. */
    sstv_tanda("SSTV Scottie 1 (indice 2)", 2U, 6, ins_disc);

    {
        unsigned long sin_auto = sstv_busca_sin_auto();
        pr("\n  LA DETECCION AUTOMATICA DE MODO, APARTE\n");
        di("por muestra, buscando SIN auto ", sin_auto);
        porciento(sin_auto, 12000000UL);
        pr("        si sale igual que con auto, es que no se nota: durante\n");
        pr("        la cabecera auto_pulso() solo se llama en los flancos\n");
        pr("        del tono de 1200 Hz, que son unos pocos en un segundo\n");
    }

    wefax_banco(ins_disc);
    hell_banco();

    /* Y la tabla, de mayor a menor. Los tres modos entran a la misma tasa
     * -12.000 muestras por segundo- asi que ordenar por instrucciones por
     * muestra es ordenar por tanto por ciento del segundo. */
    {
        unsigned i, j;
        pr("\n  DE MAYOR A MENOR, POR MUESTRA (los tres a 12.000 por segundo)\n");
        for (i = 0U; i < g_res_n; i++) {
            for (j = (unsigned)(i + 1U); j < g_res_n; j++) {
                if (g_res_ins[j] > g_res_ins[i]) {
                    unsigned long t = g_res_ins[i]; const char *q = g_res_que[i];
                    g_res_ins[i] = g_res_ins[j]; g_res_que[i] = g_res_que[j];
                    g_res_ins[j] = t;            g_res_que[j] = q;
                }
            }
        }
        for (i = 0U; i < g_res_n; i++) {
            pr("    "); pr(g_res_que[i]); pr("  ");
            prn(g_res_ins[i]); pr(" instrucciones por muestra\n");
            porciento(g_res_ins[i], 12000000UL);
        }
    }

    pr("\n");
    pila_di(0UL);
    pr("\n  prestamos de RAM: ");
    prn((unsigned long)g_ram_cog); pr(" cogidos, ");
    prn((unsigned long)g_ram_sue); pr(" sueltos\n\n");

    salir();
    return 0;
}
