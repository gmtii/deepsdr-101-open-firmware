/*
 * ======================================================================
 * LO QUE CUESTA EL RDS DENTRO DE LA INTERRUPCION DE AUDIO - 26/10/2026
 * ======================================================================
 *
 * POR QUE EXISTE. En FM ancha, demod_wfm_process_raw() llama a rds_feed()
 * con 512 muestras a 192 kHz, o sea DENTRO de la interrupcion del DMA de
 * entrada (sdr_rx.c, DMA0_Channel3_IRQHandler -> el gancho), con un
 * presupuesto de 2,667 ms = 533.333 ciclos a 200 MHz. sim/rdstest.c mide
 * si DECODIFICA; nadie media lo que CUESTA, ni si ese coste depende de lo
 * que el decodificador este haciendo.
 *
 * Y esa segunda pregunta es la que importa para el zumbido: un coste fijo
 * se nota como menos margen, pero un coste que SUBE justo cuando la senal
 * se pone fea -que es cuando el decodificador deja de enganchar y se pone
 * a corregir y a buscar- es una perturbacion que sigue al audio.
 *
 * COMO SE MIDE, y por que no con el reloj del PC. El reloj del PC no dice
 * ciclos del Cortex-M4 (ver tools/isr_coste_check.py, que ya explica esto
 * para el corrector de espejo). Lo que SI es de la maquina que sea son los
 * SUCESOS: cuantas veces se llama a cada trozo del decodificador por
 * bloque de audio. Eso es algoritmo, no compilador.
 *
 * Asi que aqui se cuentan sucesos por bloque -y su maximo, que es lo que
 * decide si se llega a tiempo- y el numero de ciclos ARM sale de
 * multiplicarlos por el coste de cada trozo, medido aparte sobre el
 * desensamblado del firmware. Eso lo hace tools/rds_coste_check.py, que
 * compila rds.c y nco.c PARA LA RADIO y cuenta: confirma I_FIR, I_RESTO e
 * I_CORRIGE instruccion a instruccion, y a los dos que -O2 funde dentro de
 * rds_feed() -I_MUESTRA e I_BANDABASE- les pone un tope de crecimiento, que
 * es lo unico que se puede hacer con ellos. Ver alli, que lo explica entero.
 *
 * ESCENARIOS. Son los de la radio de verdad, no un laboratorio:
 *
 *   A  emisora con RDS limpio                  (lo que se espera)
 *   B  emisora SIN RDS, musica y estereo       (media banda de FM)
 *   C  solo ruido, sin emisora                 (entre emisoras)
 *   D  RDS con ruido, al borde del enganche    (el peor caso real:
 *                                              engancha y aun asi falla
 *                                              una correccion de cada tres)
 *   E  RDS que va y viene cada dos segundos    (desvanecimiento)
 */
#include "rds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define FS      192000.0
#define BAUD    1187.5
#define SUB     57000.0
#define NBLK    512                    /* SDR_RX_BLOCK_SAMPLES_WFM */
#define CICLOS_BLOQUE 533333.0         /* 2,667 ms a 200 MHz */

static int fallos = 0;

static void comprueba(int ok, const char *que)
{
    if (!ok) { fallos++; }
    printf("  %-62s %s\n", que, ok ? "ok" : "*** MAL ***");
}

/* ---------------------------------------------------------------- generador
 * Copiado de sim/rdstest.c a proposito: lo que aqui se mide es el COSTE, y
 * para eso la senal tiene que ser la misma con la que se midio que
 * decodifica. El CRC lo calcula rds_resto(), igual que alli. */
static uint32_t bloque(uint16_t datos, uint16_t off)
{
    uint32_t m = (uint32_t)datos << 10;
    uint16_t crc = rds_resto(m);
    return m | (uint32_t)(crc ^ off);
}

static void grupo(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint32_t *out)
{
    out[0] = bloque(a, RDS_OFF_A);
    out[1] = bloque(b, RDS_OFF_B);
    out[2] = bloque(c, RDS_OFF_C);
    out[3] = bloque(d, RDS_OFF_D);
}

static void grupo_0a(uint16_t pi, uint8_t seg, char c1, char c2, uint32_t *out)
{
    uint16_t b = (uint16_t)((0U << 12) | (0U << 11) | (5U << 5) | seg);
    grupo(pi, b, (uint16_t)0x1234,
          (uint16_t)(((uint8_t)c1 << 8) | (uint8_t)c2), out);
}

typedef struct {
    double fase_sub, fase_pil, fase_bit, fase_aud, fase_lr;
    double estereo;
    int    bit_ant;
    uint32_t *grupos;
    int    n_grupos, gi, bi, valor, arrancado;
} tx_t;

static void tx_init(tx_t *t, uint32_t *grupos, int n)
{
    memset(t, 0, sizeof *t);
    t->grupos = grupos;
    t->n_grupos = n;
    t->estereo = 1.0;
}

static int tx_siguiente_bit(tx_t *t)
{
    int b, k, pos;
    uint32_t blq;

    pos = t->bi;
    k = pos / 26;
    blq = t->grupos[t->gi * 4 + k];
    b = (int)((blq >> (25 - (pos % 26))) & 1U);
    t->bi++;
    if (t->bi >= 104) { t->bi = 0; t->gi = (t->gi + 1) % t->n_grupos; }
    t->bit_ant ^= b;
    return t->bit_ant;
}

/* `rds_amp` a cero = emisora SIN datos: todo lo demas (piloto, audio,
 * estereo) sigue estando, que es lo que de verdad llega. */
static double tx_muestra(tx_t *t, double rds_amp)
{
    double y, paso, bif;

    paso = BAUD / FS;
    if (!t->arrancado) { t->valor = tx_siguiente_bit(t) ? 1 : -1; t->arrancado = 1; }
    t->fase_bit += paso;
    if (t->fase_bit >= 1.0) {
        t->fase_bit -= 1.0;
        t->valor = tx_siguiente_bit(t) ? 1 : -1;
    }
    bif = (t->fase_bit < 0.5) ? (double)t->valor : (double)(-t->valor);

    t->fase_sub += 2.0 * M_PI * SUB / FS;
    t->fase_pil += 2.0 * M_PI * 19000.0 / FS;
    if (t->fase_sub > 2.0 * M_PI) { t->fase_sub -= 2.0 * M_PI; }
    if (t->fase_pil > 2.0 * M_PI) { t->fase_pil -= 2.0 * M_PI; }

    y  = rds_amp * bif * cos(t->fase_sub);
    y += 0.08 * sin(t->fase_pil);

    t->fase_aud += 2.0 * M_PI * 1000.0 / FS;
    t->fase_lr  += 2.0 * M_PI * 14000.0 / FS;
    if (t->fase_aud > 2.0 * M_PI) { t->fase_aud -= 2.0 * M_PI; }
    if (t->fase_lr  > 2.0 * M_PI) { t->fase_lr  -= 2.0 * M_PI; }
    y += 0.35 * sin(t->fase_aud);
    y += t->estereo * 0.30 * sin(t->fase_lr) * sin(2.0 * t->fase_pil);
    return y;
}

static unsigned long s_rnd = 12345UL;
static double ruido(void)
{
    double a, b;
    s_rnd = s_rnd * 1103515245UL + 12345UL;
    a = (double)((s_rnd >> 16) & 0x7FFF) / 32767.0 - 0.5;
    s_rnd = s_rnd * 1103515245UL + 12345UL;
    b = (double)((s_rnd >> 16) & 0x7FFF) / 32767.0 - 0.5;
    return a + b;
}

/* ------------------------------------------------- el contador de sucesos
 *
 * Lo que se cuenta y por que cada uno:
 *
 *   bloques        de 512 muestras: el reloj de la interrupcion
 *   malos          bloques de RDS que NO pasaron el resto a la primera.
 *                  Cada uno dispara una busqueda ENTERA de rafagas en
 *                  corrige_en_pos(), que es el trozo caro y condicional.
 *   corregidos     los que la busqueda si arreglo (busqueda parcial)
 *   buscando       simbolos procesados SIN sincronismo. Sin sincronismo
 *                  mete_bit() calcula el resto en CADA bit en vez de uno
 *                  de cada 26: veintiseis veces mas divisiones.
 */
typedef struct {
    long bloques;
    long ok, mal, corregidos;
    long simb_busca;         /* estimado por diferencia de enganche */
    long max_mal_bloque;     /* el peor bloque: cuantos malos cayeron en el */
    long enganches, perdidas;
    int  enganchado_final;
    /*
     * EL RELOJ DEL PC, Y PARA QUE SIRVE Y PARA QUE NO.
     *
     * NO dice ciclos del Cortex-M4; eso ya lo explica tools/
     * isr_coste_check.py y aqui vale igual. Lo que SI dice, con el mismo
     * binario y la misma maquina, es si el coste de rds_feed() CAMBIA de un
     * bloque a otro. Esa es la pregunta de este banco, y para contestarla
     * una medida relativa sirve: si el maximo se parece a la mediana, el
     * coste no depende de lo que el decodificador este haciendo.
     */
    double ns_min, ns_med, ns_max;
} cuenta_t;

/* Mediana por ordenacion; son unos pocos miles de numeros una vez. */
static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

static double ahora_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

/* Corre `segundos` y rellena `c`. `rds_amp` 0 = sin datos; `sigma` el ruido;
 * `corta` = segundos de RDS y segundos sin el, alternando (0 = no corta). */
static void corre(double segundos, double rds_amp, double sigma,
                  double corta, cuenta_t *c)
{
    static uint32_t grupos[4 * 4];
    tx_t t;
    long n = (long)(FS * segundos), k;
    float buf[NBLK];
    int j = 0;
    rds_info_t in;
    uint32_t prev_mal = 0UL, prev_ok = 0UL;
    long mal_este = 0;
    static double ns[64000];
    long nn = 0;

    grupo_0a(0x3141U, 0U, 'R', 'A', &grupos[0]);
    grupo_0a(0x3141U, 1U, 'D', 'I', &grupos[4]);
    grupo_0a(0x3141U, 2U, 'O', ' ', &grupos[8]);
    grupo_0a(0x3141U, 3U, 'E', 'S', &grupos[12]);

    memset(c, 0, sizeof *c);
    tx_init(&t, grupos, 4);
    rds_start((float)FS);

    for (k = 0; k < n; k++) {
        double amp = rds_amp;
        if (corta > 0.0) {
            double seg = (double)k / FS;
            if (fmod(seg, 2.0 * corta) >= corta) { amp = 0.0; }
        }
        buf[j++] = (float)(tx_muestra(&t, amp) + sigma * ruido());
        if (j == NBLK) {
            double t0 = ahora_ns();
            rds_feed(buf, (uint32_t)NBLK);
            if (nn < (long)(sizeof ns / sizeof ns[0])) {
                ns[nn++] = ahora_ns() - t0;
            }
            j = 0;
            c->bloques++;
            rds_info(&in);
            mal_este = (long)(in.bloques_mal - prev_mal);
            if (mal_este > c->max_mal_bloque) { c->max_mal_bloque = mal_este; }
            if (!in.enganchado) { c->simb_busca += 512 / 16; }
            prev_mal = in.bloques_mal;
            prev_ok = in.bloques_ok;
        }
    }
    (void)prev_ok;
    rds_info(&in);
    c->ok = (long)in.bloques_ok;
    c->mal = (long)in.bloques_mal;
    c->enganchado_final = in.enganchado;
    {
        uint32_t e, p;
        rds_cuentas_dbg(&e, &p);
        c->enganches = (long)e;
        c->perdidas = (long)p;
    }
    if (nn > 0) {
        qsort(ns, (size_t)nn, sizeof ns[0], cmp_d);
        c->ns_min = ns[0];
        c->ns_med = ns[nn / 2];
        /* El percentil 99,9 y no el maximo pelado: el maximo de un PC es
         * siempre el cambio de contexto del sistema, no este codigo. */
        c->ns_max = ns[(nn * 999) / 1000];
    }
}

/* ------------------------------------------------------------------ coste
 *
 * INSTRUCCIONES POR SUCESO, contadas sobre el desensamblado de rds.c y
 * nco.c compilados PARA LA RADIO con las opciones de reglas.mk. No son un
 * calculo a ojo, y desde el 26/10/2026 tampoco una promesa: las tres que
 * se pueden sacar del binario -I_FIR, I_RESTO e I_CORRIGE- las vuelve a
 * sacar tools/rds_coste_check.py en cada `make comprueba` y se pone rojo si
 * se mueven. Las otras dos las funde -O2 dentro de rds_feed() y no se
 * pueden separar; alli se vigila que la funcion entera no engorde.
 */
#define I_MUESTRA    59.0    /* bucle de rds_feed + nco_paso, por muestra */
#define I_FIR       448.0    /* 64 tomas x 7 instrucciones, por salida */
#define I_BANDABASE 150.0    /* Costas + reloj de simbolo, por salida */
/*
 * Y ESTOS DOS ESTABAN CONTADOS POR LA MITAD - 26/10/2026, revision a fondo.
 *
 * El dia que se escribio tools/rds_coste_check.py -que es el que esta
 * citado aqui arriba y hasta ese dia NO EXISTIA- lo primero que saco del
 * desensamblado fue que I_FIR eran exactamente 448, siete instrucciones por
 * toma por 64 tomas. Y que estos dos no.
 *
 * El bucle de rds_resto() son NUEVE instrucciones de cuerpo -prueba de bit,
 * resta del generador desplazado, fin de bucle- por 16 vueltas: 144, no 70.
 * El 70 salia de contar solo las vueltas en las que el bit es cero, o sea
 * de poner la media de unos datos concretos donde va un presupuesto. Un
 * presupuesto se hace con el peor caso: la media depende de lo que entre
 * por la antena y el peor caso no.
 *
 * Y I_CORRIGE es I_RESTO por el numero de divisiones de una busqueda entera
 * que falla, que con RDS_RAFAGA_MAX = 2 son 26 posiciones del patron de un
 * bit, 25 del de dos y el resto inicial del bloque: 52. 52 x 144 = 7488.
 * El 3700 de antes era 52 x 70, o sea el mismo error arrastrado.
 *
 * No mueve el veredicto -el peor bloque de los escenarios de abajo no llega
 * a una sola correccion fallida- pero el numero que se enseña es el doble, y
 * de eso trata este banco.
 */
#define I_RESTO     144.0    /* una division polinomica (16 vueltas) */
#define I_CORRIGE  7488.0    /* una busqueda ENTERA de rafagas que falla */

/*
 * UN SOLO ESCENARIO, PARA MEDIRLO CON callgrind.
 *
 *     valgrind --tool=callgrind ./out/rds_coste A
 *
 * El reloj del PC trae la basura del sistema operativo en la cola -se ve en
 * el p99,9 de la tabla de arriba, que se va a veintiocho veces la mediana
 * sin que este codigo haga nada distinto-. callgrind cuenta INSTRUCCIONES
 * ejecutadas, que no dependen de quien mas corra en la maquina: es la unica
 * forma de comparar dos escenarios y creerse la diferencia.
 */
static int uno_solo(const char *que)
{
    cuenta_t c;

    switch (que[0]) {
    case 'A': corre(15.0, 0.03,  0.000, 0.0, &c); break;
    case 'B': corre(15.0, 0.000, 0.010, 0.0, &c); break;
    case 'C': corre(15.0, 0.000, 0.300, 0.0, &c); break;
    case 'D': corre(15.0, 0.03,  0.300, 0.0, &c); break;
    case 'E': corre(15.0, 0.03,  0.020, 2.0, &c); break;
    default:  printf("escenario desconocido: %s (A..E)\n", que); return 1;
    }
    printf("escenario %c: %ld bloques, ok %ld, malos %ld, enganchado %d\n",
           que[0], c.bloques, c.ok, c.mal, c.enganchado_final);
    return 0;
}

int main(int argc, char **argv)
{
    cuenta_t a, b, cc, d, e;
    double base, ciclos_a, ciclos_d;

    if (argc > 1) { return uno_solo(argv[1]); }

    printf("\nLO QUE CUESTA rds_feed() EN LA INTERRUPCION DE WFM\n");
    printf("  bloque = %d muestras a %.0f Hz = %.3f ms = %.0f ciclos a 200 MHz\n",
           NBLK, FS, 1000.0 * NBLK / FS, CICLOS_BLOQUE);

    printf("\n1. sucesos por escenario (15 s cada uno)\n");
    corre(15.0, 0.03, 0.000, 0.0, &a);   /* A: RDS limpio */
    corre(15.0, 0.000, 0.010, 0.0, &b);  /* B: emisora SIN RDS */
    corre(15.0, 0.000, 0.300, 0.0, &cc); /* C: solo ruido */
    /*
     * EL BORDE ESTABA DONDE NO HABIA BORDE - 26/10/2026, revision a fondo.
     *
     * D se llama "al borde del enganche" y es el escenario del que sale el
     * unico numero de este banco que mide la correccion de errores: el peor
     * bloque por I_CORRIGE. Con 0,090 de ruido salian 676 bloques buenos y
     * CERO malos, o sea ninguna correccion fallida, o sea que ese numero era
     * un cero estructural y el veredicto de abajo no miraba nada.
     *
     * No era un borde: el diezmador de 64 tomas se come casi todo el ruido
     * de banda ancha antes de que llegue al detector. Barrido el 26/10 (15 s
     * cada uno, misma semilla):
     *
     *     0,090  ->  676 buenos,    0 malos, enganchado
     *     0,150  ->  670 buenos,    2 malos, enganchado
     *     0,200  ->  614 buenos,   58 malos, enganchado
     *     0,250  ->  527 buenos,  100 malos, enganchado
     *     0,300  ->  425 buenos,  201 malos, enganchado   <- este
     *     0,400  ->   27 buenos,   66 malos, SE SUELTA
     *
     * 0,300 es el borde de verdad: todavia engancha y ya falla una de cada
     * tres veces, que es cuando la correccion trabaja mas. 0,400 ya no vale
     * porque sin enganche no hay bloques que corregir.
     */
    corre(15.0, 0.03, 0.300, 0.0, &d);   /* D: RDS al borde */
    corre(15.0, 0.03, 0.020, 2.0, &e);   /* E: RDS a ratos */

    printf("    %-26s %8s %8s %8s %8s %8s\n",
           "", "bloques", "ok", "malos", "peorblq", "perdidas");
    printf("    %-26s %8ld %8ld %8ld %8ld %8ld\n", "A RDS limpio",
           a.bloques, a.ok, a.mal, a.max_mal_bloque, a.perdidas);
    printf("    %-26s %8ld %8ld %8ld %8ld %8ld\n", "B emisora sin RDS",
           b.bloques, b.ok, b.mal, b.max_mal_bloque, b.perdidas);
    printf("    %-26s %8ld %8ld %8ld %8ld %8ld\n", "C solo ruido",
           cc.bloques, cc.ok, cc.mal, cc.max_mal_bloque, cc.perdidas);
    printf("    %-26s %8ld %8ld %8ld %8ld %8ld\n", "D RDS al borde",
           d.bloques, d.ok, d.mal, d.max_mal_bloque, d.perdidas);
    printf("    %-26s %8ld %8ld %8ld %8ld %8ld\n", "E RDS a ratos",
           e.bloques, e.ok, e.mal, e.max_mal_bloque, e.perdidas);

    printf("\n1b. tiempo de rds_feed() por bloque (ns de este PC, relativo)\n");
    printf("    %-26s %10s %10s %10s %8s\n", "", "min", "mediana", "p99,9", "max/med");
    printf("    %-26s %10.0f %10.0f %10.0f %8.2f\n", "A RDS limpio",
           a.ns_min, a.ns_med, a.ns_max, a.ns_max / a.ns_med);
    printf("    %-26s %10.0f %10.0f %10.0f %8.2f\n", "B emisora sin RDS",
           b.ns_min, b.ns_med, b.ns_max, b.ns_max / b.ns_med);
    printf("    %-26s %10.0f %10.0f %10.0f %8.2f\n", "C solo ruido",
           cc.ns_min, cc.ns_med, cc.ns_max, cc.ns_max / cc.ns_med);
    printf("    %-26s %10.0f %10.0f %10.0f %8.2f\n", "D RDS al borde",
           d.ns_min, d.ns_med, d.ns_max, d.ns_max / d.ns_med);
    printf("    %-26s %10.0f %10.0f %10.0f %8.2f\n", "E RDS a ratos",
           e.ns_min, e.ns_med, e.ns_max, e.ns_max / e.ns_med);
    printf("    la mediana del peor escenario contra la del mejor: %.3f\n",
           (d.ns_med > b.ns_med ? d.ns_med : b.ns_med)
           / (d.ns_med < b.ns_med ? d.ns_med : b.ns_med));

    printf("\n2. el coste fijo, el que se paga siempre\n");
    base = NBLK * I_MUESTRA + (NBLK / 16) * (I_FIR + I_BANDABASE);
    printf("    %-44s %10.0f\n", "instrucciones por bloque, sin tocar bits", base);
    printf("    %-44s %9.1f%%\n", "del presupuesto del bloque (1 ciclo/instr)",
           100.0 * base / CICLOS_BLOQUE);
    printf("    %-44s %9.1f%%\n", "idem a 1,3 ciclos/instruccion",
           100.0 * base * 1.3 / CICLOS_BLOQUE);

    printf("\n3. lo que VARIA con lo que hace el decodificador\n");
    {
        /* Sin sincronismo se calcula el resto en cada bit: 1187,5 bits/s,
         * o sea 3,17 por bloque, contra uno cada 26 bits si engancha. */
        double bits_blq = BAUD * NBLK / FS;
        double extra_busca = bits_blq * I_RESTO - (bits_blq / 26.0) * I_RESTO;
        double extra_peor_a = a.max_mal_bloque * I_CORRIGE;
        double extra_peor_d = d.max_mal_bloque * I_CORRIGE;

        printf("    %-44s %10.0f\n", "buscando: instrucciones de mas/bloque",
               extra_busca);
        printf("    %-44s %9.3f%%\n", "   sobre el presupuesto",
               100.0 * extra_busca / CICLOS_BLOQUE);
        printf("    %-44s %10.0f\n", "peor bloque A: correcciones fallidas",
               extra_peor_a);
        printf("    %-44s %10.0f\n", "peor bloque D: correcciones fallidas",
               extra_peor_d);
        printf("    %-44s %9.3f%%\n", "   sobre el presupuesto (D)",
               100.0 * extra_peor_d / CICLOS_BLOQUE);

        ciclos_a = base + extra_peor_a;
        ciclos_d = base + extra_busca + extra_peor_d;
        printf("\n    %-44s %10.0f\n", "peor bloque total, A (enganchado)", ciclos_a);
        printf("    %-44s %10.0f\n", "peor bloque total, D (al borde)", ciclos_d);
        printf("    %-44s %9.2f\n", "razon peor/fijo", ciclos_d / base);

        printf("\n4. veredicto\n");
        comprueba(base < 0.25 * CICLOS_BLOQUE,
                  "el coste fijo cabe en el 25% del bloque");
        /*
         * ESTE VEREDICTO MEDIA CONTRA EL NUMERO EQUIVOCADO - 26/10/2026.
         *
         * Decia "lo condicional no llega al 15% del coste fijo", o sea
         * ciclos_d < 1,15 x base. Comparar lo que varia contra lo que no
         * varia es una proporcion sin significado fisico: lo que decide si
         * la interrupcion llega a tiempo no es cuanto se parece el peor
         * bloque al bloque medio, es cuanto ocupa el peor bloque DEL
         * PRESUPUESTO. Un decodificador el doble de barato con el mismo
         * peor caso pasaria de verde a rojo sin que nada fuera peor.
         *
         * Y estaba verde por una razon que no era buena: el escenario D
         * tenia tan poco ruido que no fallaba ni una correccion, asi que lo
         * condicional valia cero. Puesto el borde donde hay borde -ver el
         * barrido junto a la llamada- una correccion fallida son 7.488
         * instrucciones, 1,4% del bloque, y la proporcion vieja se va al
         * 16%: roja por estar bien medida.
         *
         * Asi que se mide contra el presupuesto, y a 1,3 ciclos por
         * instruccion, que es lo que se usa en el apartado 2. Medido el
         * 26/10: 57.270 instrucciones, 74.451 ciclos, 14,0% del bloque. El
         * tope en el 20% deja margen de verdad y dice algo que se entiende:
         * el peor bloque de RDS cabe en la quinta parte de la interrupcion.
         *
         * Y el peor bloque es el peor de verdad: solo puede caer UN bloque
         * de RDS en un bloque de audio -1.187,5 bits/s entre 26 son 45,7
         * bloques por segundo, contra 375 bloques de audio- asi que no hay
         * forma de acumular dos busquedas fallidas en el mismo.
         */
        comprueba(ciclos_d * 1.3 < 0.20 * CICLOS_BLOQUE,
                  "el peor bloque cabe en el 20% (a 1,3 ciclos/instr)");
        comprueba(a.enganchado_final,
                  "con RDS limpio acaba enganchado (la medida vale)");
        comprueba(!b.enganchado_final,
                  "sin RDS no engancha (escenario B de verdad)");
        comprueba(d.ns_med < 1.15 * b.ns_med && b.ns_med < 1.15 * d.ns_med,
                  "el tiempo por bloque no depende del escenario (15%)");
        /*
         * EL p99,9 NO SE COMPRUEBA, Y HAY QUE DECIR POR QUE.
         *
         * Se intento y se puso rojo en una maquina ociosa: la cola llega a
         * veintiocho veces la mediana mientras la MEDIANA entre escenarios
         * se mueve un 1,6%. Eso no es este codigo, es el planificador del
         * sistema operativo, y un banco que se pone rojo por eso no vale
         * nada - se aprende a ignorarlo y entonces ya no avisa de nada.
         *
         * La cola se mide con callgrind, que cuenta INSTRUCCIONES y no
         * segundos. Medido el 26/10/2026, dentro de rds_feed() y solo ahi
         * (--collect-atstart=no --toggle-collect=rds_feed), 5.625 bloques
         * por escenario:
         *
         *     A RDS limpio        299.407.093   53.228 por bloque
         *     B emisora sin RDS   301.149.546   53.538
         *     C solo ruido        301.295.729   53.564
         *     D RDS al borde      299.401.068   53.227
         *     E RDS a ratos       302.002.904   53.690
         *
         * Del mejor al peor hay un 0,87%. O sea que el coste del RDS dentro
         * de la interrupcion NO depende de lo que el decodificador este
         * haciendo: ni de que enganche, ni de que pierda, ni de que se pase
         * la vida buscando. Es un coste FIJO, y por eso no puede ser la
         * fuente de una perturbacion que va y viene.
         */
    }

    printf("\n%s (%d fallo%s)\n", fallos ? "*** MAL ***" : "todo bien",
           fallos, fallos == 1 ? "" : "s");
    return fallos ? 1 : 0;
}
