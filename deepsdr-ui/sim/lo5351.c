/*
 * EL OSCILADOR DE LA RADIO, COMPROBADO SIN RADIO.
 *
 * POR QUE EXISTE (19/10/2026). User/ms5351.c genera el oscilador local de
 * TODA la radio, de 300 kHz a 180 MHz, y no tenia ni un banco. Su vecino de
 * abajo -lo_gen_gd32.c, el de por debajo de 300 kHz- si lo tiene.
 *
 * Y es un fichero con tres averias de hardware en su historial: un divisor
 * de salida que se escribia recortado a 18 bits en silencio, un fVCO que se
 * salia del VCO durante tramos enteros de una zona (sintoma: a 4,5 MHz no
 * habia seNal NINGUNA) y la maniobra de los 90 grados cambiada de sentido
 * tres veces en un mismo dia. Las tres se encontraron en antena.
 *
 * QUE COMPRUEBA. Hace pasar al ms5351.c DE VERDAD por un I2C de mentira que
 * se queda con todo lo que escribe, y despues deshace la cuenta de los
 * registros para saber que le ha dicho al chip:
 *
 *   - la frecuencia que sale es la que se pidio;
 *   - el fVCO se queda dentro del VCO, en TODA la banda y no solo en los
 *     extremos de cada zona;
 *   - los dos canales llevan el mismo divisor, que es lo que sostiene los
 *     90 grados mientras se sintoniza dentro de una zona;
 *   - ningun P1 se escribe recortado;
 *   - y el angulo de cuadratura que implican los registros escritos, que es
 *     lo que de verdad decide el rechazo de imagen por debajo de 4,8 MHz.
 *
 * QUE NO COMPRUEBA, dicho claro. El nivel de salida del VCO, la corriente
 * que le llega al mezclador y el ruido de fase son del chip, no del
 * firmware: de eso aqui no se puede decir nada y no se dice.
 *
 * LA ESPERA. La maniobra de los 90 grados bloquea 62,5 ms de tiempo REAL.
 * Aqui SystemCoreClock se deja a cero a proposito, con lo que esa espera
 * termina en la primera vuelta: no escribe ningun registro, y lo que este
 * banco mide son registros. El angulo se calcula de los divisores, que es
 * de donde sale.
 */
#include "ms5351.h"
#include "i2c_bitbang.h"
#include "rf_lpf.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* --- lo que el firmware espera encontrar fuera de ms5351.c ------------ */
typedef struct { uint32_t CYCCNT; uint32_t CTRL; uint32_t LAR; } dwt_falso_t;
dwt_falso_t dwt_falso;
typedef struct { uint32_t DEMCR; } coredebug_falso_t;
coredebug_falso_t coredebug_falso;
uint32_t SystemCoreClock = 0U;      /* ver LA ESPERA, arriba */

void    debug_print(const char *s)                    { (void)s; }
void    debug_print_dec(const char *s, uint32_t v)    { (void)s; (void)v; }
void    debug_print_hex(const char *s, uint32_t v)    { (void)s; (void)v; }
void    rf_lpf_init(void)                              { }
uint8_t rf_lpf_select(uint32_t f)                      { (void)f; return 1U; }
void    i2c_bitbang_init(void)                         { }

/* --- el I2C de mentira ------------------------------------------------ */
#define FRAC_C_B 0xFFFFFUL

static uint8_t  s_reg[256];
static uint32_t s_escrituras;

/* Cada bloque de multisynth que se escribe, en orden: con esto se ve la
 * maniobra de los 90 grados por dentro sin tocar el firmware. */
#define LOG_MAX 64
static struct { uint8_t base; double div; } s_log[LOG_MAX];
static int s_nlog;
static double divisor(const uint8_t *r, int *p1_recortado);

uint8_t i2c_write(uint8_t addr, const uint8_t *buf, uint8_t len)
{
    uint8_t i;
    (void)addr;
    if (len < 1U) { return 0U; }
    for (i = 1U; i < len; i++) { s_reg[(uint8_t)(buf[0] + i - 1U)] = buf[i]; }
    s_escrituras++;
    if (len == 9U && s_nlog < LOG_MAX) {
        s_log[s_nlog].base = buf[0];
        s_log[s_nlog].div  = divisor(&s_reg[buf[0]], 0);
        s_nlog++;
    }
    return 1U;
}

uint8_t i2c_read(uint8_t addr, uint8_t *val, uint8_t len)
{
    (void)addr; (void)len;
    if (val) { *val = 0U; }
    return 1U;
}

/* --- deshacer la cuenta de un bloque P1/P2/P3 ------------------------- */
static double divisor(const uint8_t *r, int *p1_recortado)
{
    /* P3 son VEINTE bits y no dieciseis: el nibble de arriba viaja en el
     * mismo byte que el de P2 (ver wr_ms()). Leerlo a 16 da 65535 en vez
     * de 1048575 y entonces TODO lo demas sale mal. */
    uint32_t p3 = ((uint32_t)(r[5] & 0xF0U) << 12) | ((uint32_t)r[0] << 8) | r[1];
    uint32_t p1 = ((uint32_t)(r[2] & 0x03U) << 16) | ((uint32_t)r[3] << 8) | r[4];
    uint32_t p2 = ((uint32_t)(r[5] & 0x0FU) << 16) | ((uint32_t)r[6] << 8) | r[7];
    uint32_t a, f128, b;

    if (p3 == 0U) { return 0.0; }
    a    = (p1 + 512U) / 128U;
    f128 = (p1 + 512U) % 128U;
    b    = (p2 + p3 * f128) / 128U;
    /* P1 se escribe enmascarado a 18 bits: un divisor que no quepa sale
     * recortado y en silencio. Es una de las averias del historial. */
    if (p1_recortado) { *p1_recortado = (a > 2051U) ? 1 : 0; }
    return (double)a + (double)b / (double)p3;
}

/* El cristal NO son 26 MHz redondos: la placa lleva el suyo calibrado y
 * el firmware lo puede mover desde CONFIG.CSV. Se le pregunta al modulo
 * en vez de escribirlo aqui, que es como este banco se invento un error
 * de 3 ppm la primera vez que se corrio. */
#define XTAL ((double)ms5351_get_xtal_hz())

static double g_fvco, g_ms0, g_ms1, g_salida;
static int    g_recorte;

static void lee_estado(void)
{
    int r0 = 0, r1 = 0, rp = 0;
    double pll = divisor(&s_reg[34], &rp);
    g_ms0 = divisor(&s_reg[42], &r0);
    g_ms1 = divisor(&s_reg[50], &r1);
    g_fvco = XTAL * pll;
    g_salida = (g_ms0 > 0.0) ? g_fvco / g_ms0 : 0.0;
    g_recorte = r0 || r1 || rp;
}

/* --- la contabilidad del banco ---------------------------------------- */
static int fallos;
static void di(const char *que, int ok, const char *det)
{
    printf("    %-54s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

#define VMIN 600000000.0
#define VMAX 908000000.0
#define HANDOFF 4800000UL

int main(void)
{
    char det[160];

    printf("\nEL OSCILADOR LOCAL, DE 300 kHz A 30 MHz\n");
    printf("=======================================\n");

    ms5351_init();

    /* ---------------------------------------------------------------- */
    printf("\n  1. la frecuencia que sale es la que se pide\n");
    {
        unsigned long f;
        double peor_err = 0.0; unsigned long peor_f = 0;
        int fuera_vco = 0; unsigned long fuera_f = 0; double fuera_v = 0.0;
        int distintos = 0, recortes = 0, negados = 0;
        unsigned long paso;

        for (f = 300000UL; f <= 30000000UL; f += paso) {
            paso = (f < 5000000UL) ? 1000UL : 10000UL;
            /* Los registros NO se borran entre sintonias: el chip de
             * verdad los conserva, y el firmware cuenta con ello -dentro
             * de una zona solo reescribe la realimentacion de la PLL-. */
            if (!ms5351_set_lo_freq((uint32_t)f)) { negados++; continue; }
            lee_estado();
            if (fabs(g_salida - (double)f) > fabs(peor_err)) {
                peor_err = g_salida - (double)f; peor_f = f;
            }
            /* La tolerancia es UN escalon del sintetizador (xtal/FRAC_C,
             * unos 25 Hz): el suelo de la zona 1 cae JUSTO en el limite
             * del VCO y el divisor fraccionario redondea hacia abajo, asi
             * que ahi el fVCO real queda unos hercios por debajo de 600
             * MHz. Eso es el redondeo, no un fallo. Que SEIS de las siete
             * zonas esten diseñadas con cero margen contra los dos topes
             * es otra cosa, y sale en la seccion 2. */
            if (g_fvco < VMIN - 50.0 || g_fvco > VMAX + 50.0) {
                if (!fuera_vco) { fuera_f = f; fuera_v = g_fvco; }
                fuera_vco++;
            }
            if (fabs(g_ms0 - g_ms1) > 1e-9) { distintos++; }
            if (g_recorte) { recortes++; }
        }
        snprintf(det, sizeof det, "peor %+.3f Hz en %.3f MHz",
                 peor_err, (double)peor_f / 1e6);
        di("la frecuencia sintetizada no se desvia ni un hercio",
           fabs(peor_err) < 1.0, det);

        if (fuera_vco) {
            snprintf(det, sizeof det, "%d puntos, el primero %.4f MHz -> fVCO %.3f MHz",
                     fuera_vco, (double)fuera_f / 1e6, fuera_v / 1e6);
        } else {
            snprintf(det, sizeof det, "en toda la banda");
        }
        di("el fVCO se queda dentro del VCO", !fuera_vco, det);

        snprintf(det, sizeof det, "%d sitios", distintos);
        di("los dos canales llevan el mismo divisor", distintos == 0, det);
        snprintf(det, sizeof det, "%d sitios", recortes);
        di("ningun divisor se escribe recortado", recortes == 0, det);
        snprintf(det, sizeof det, "%d frecuencias", negados);
        di("no se niega ninguna frecuencia de la banda", negados == 0, det);
    }

    /* ---------------------------------------------------------------- */
    printf("\n  2. donde se queda el fVCO dentro de su margen\n\n");
    printf("      %-22s %9s %9s %8s %8s\n",
           "tramo", "fVCO ini", "fVCO fin", "margen-", "margen+");
    {
        static const struct { const char *n; unsigned long a, b; } t[] = {
            { "zona 1",   300000UL,   453999UL },
            { "zona 2",   454000UL,   686837UL },
            { "zona 3",   686838UL,  1038900UL },
            { "zona 4",  1038901UL,  1570933UL },
            { "zona 5",  1570934UL,  2376962UL },
            { "zona 6",  2376963UL,  3588931UL },
            { "zona 7",  3588932UL,  4799999UL },
            { "alto, div 126", 4800000UL,  7206349UL },
            { "alto, div 124", 7206350UL,  7322580UL },
            { "alto, 14 MHz",  14000000UL, 14100000UL },
        };
        unsigned k;
        double peor_techo = 1e18;
        for (k = 0; k < sizeof t / sizeof *t; k++) {
            double v0, v1;
            ms5351_set_lo_freq((uint32_t)t[k].a); lee_estado(); v0 = g_fvco;
            ms5351_set_lo_freq((uint32_t)t[k].b); lee_estado(); v1 = g_fvco;
            printf("      %-22s %8.1fM %8.1fM %7.1fM %7.1fM\n", t[k].n,
                   v0 / 1e6, v1 / 1e6,
                   (fmin(v0, v1) - VMIN) / 1e6, (VMAX - fmax(v0, v1)) / 1e6);
            if (k < 8U) {
                double m = VMAX - fmax(v0, v1);
                if (m < peor_techo) { peor_techo = m; }
            }
        }
        printf("\n      Las seis primeras zonas barren el VCO entero. La septima\n");
        printf("      -de 3,589 a 4,8 MHz, la del aviso- se queda 100 MHz por\n");
        printf("      debajo del techo, y el tramo de arriba pegado a ella hace\n");
        printf("      lo mismo. Eso es lo que hay que medir en antena; de aqui\n");
        printf("      no sale, porque el nivel de salida del VCO es del chip.\n");
        (void)peor_techo;
    }

    /* ---------------------------------------------------------------- */
    printf("\n  3. el angulo de cuadratura que implican los registros\n\n");
    printf("      %-10s %10s %10s %9s %9s\n",
           "zona", "df pedida", "df real", "angulo", "espejo");
    {
        /* La espera y la df viven en ms5351.c atadas por un _Static_assert:
         * t = 1/(4*df). Aqui se repiten para poder hacer la cuenta. */
        const double DF_NOM = 4.0, T_S = 0.0625;
        static const unsigned long entra[7] = {
            300000UL, 454000UL, 686838UL, 1038901UL,
            1570934UL, 2376963UL, 3588932UL
        };
        unsigned z;
        double peor_esp = 1e9;
        for (z = 0; z < 7U; z++) {
            double d_lento, d_rapido, f_lento, f_rapido, df, ang, esp;
            /* Forzar el cambio de zona: subir arriba de 4,8 y volver. */
            ms5351_set_lo_freq(10000000UL);
            s_nlog = 0;
            ms5351_set_lo_freq((uint32_t)entra[z]);
            if (s_nlog < 4) { di("la maniobra escribe sus cuatro bloques", 0, 0); continue; }
            /* orden: PLLB, MS0(freq-df), MS1(freq-df), MS1(freq), MS0(freq), PLLB */
            d_lento  = s_log[1].div;
            d_rapido = s_log[3].div;
            lee_estado();
            f_lento  = g_fvco / d_lento;
            f_rapido = g_fvco / d_rapido;
            df  = f_rapido - f_lento;
            ang = 360.0 * df * T_S;
            esp = 20.0 * log10(1.0 / tan(fabs(ang - 90.0) * M_PI / 360.0));
            printf("      %-10u %9.4f %9.4f %8.3f%s %7.1f dB\n",
                   z + 1U, DF_NOM, df, ang, "\u00b0", esp);
            if (esp < peor_esp) { peor_esp = esp; }
        }
        snprintf(det, sizeof det, "el peor deja %.0f dB de rechazo", peor_esp);
        di("la maniobra clava los 90 grados en las siete zonas",
           peor_esp > 45.0, det);
    }

    /* ---------------------------------------------------------------- */
    printf("\n  4. la maniobra se rehace cuando toca, y solo cuando toca\n");
    {
        int n_dentro, n_cruza, n_vuelve, n_baja;

        /* Dentro de una zona: solo la realimentacion de la PLL. */
        ms5351_set_lo_freq(4000000UL);
        s_nlog = 0; ms5351_set_lo_freq(4100000UL); n_dentro = s_nlog;

        /* Cruzando de la 7 a la 6 hacia abajo. */
        s_nlog = 0; ms5351_set_lo_freq(3000000UL); n_cruza = s_nlog;

        /* Subiendo por encima del cruce y volviendo a bajar. */
        ms5351_set_lo_freq(10000000UL);
        s_nlog = 0; ms5351_set_lo_freq(3000000UL); n_vuelve = s_nlog;

        /* Y bajando dentro de la misma zona 6, otra vez solo la PLL. */
        s_nlog = 0; ms5351_set_lo_freq(2500000UL); n_baja = s_nlog;

        snprintf(det, sizeof det, "%d bloque(s)", n_dentro);
        di("sintonizar dentro de una zona no rehace la maniobra",
           n_dentro == 1, det);
        snprintf(det, sizeof det, "%d bloques", n_cruza);
        di("cruzar de zona SI la rehace", n_cruza >= 5, det);
        snprintf(det, sizeof det, "%d bloques", n_vuelve);
        di("volver de la banda alta tambien la rehace", n_vuelve >= 5, det);
        snprintf(det, sizeof det, "%d bloque(s)", n_baja);
        di("y bajando dentro de la zona, tampoco se rehace",
           n_baja == 1, det);
    }

    /* ---------------------------------------------------------------- */
    printf("\n  5. la zona 7 (3,589 - 4,8 MHz): los 22 multiplicadores\n\n");
    printf("      %-5s %9s %9s %7s %7s %9s %9s\n",
           "mult", "fVCO ini", "fVCO fin", "marg-", "marg+", "angulo", "espejo");
    {
        const double T_S = 0.0625;
        uint32_t m;
        uint32_t mejor = 0U; double mejor_esp = -1e9; double esp_defecto = 0.0;
        double marg_defecto_abajo = 0.0, marg_defecto_arriba = 0.0;
        uint32_t por_omision = ms5351_lowband_z7_mult();

        for (m = 168U; m <= 189U; m++) {
            double pll, fv, df, ang, esp, v0, v1;
            while (ms5351_lowband_z7_mult() != m) {
                (void)ms5351_lowband_z7_siguiente();
            }
            ms5351_set_lo_freq(10000000UL);
            s_nlog = 0;
            ms5351_set_lo_freq(3588932UL);
            if (s_nlog < 4) { continue; }
            lee_estado();
            pll = g_fvco; fv = pll;
            df  = fv / s_log[3].div - fv / s_log[1].div;
            ang = 360.0 * df * T_S;
            esp = 20.0 * log10(1.0 / tan(fabs(ang - 90.0) * M_PI / 360.0));
            v0 = 3588932.0 * (double)m;
            v1 = 4800000.0 * (double)m;
            printf("      %-5u %8.1fM %8.1fM %6.1fM %6.1fM %8.3f%s %7.1f dB%s\n",
                   m, v0 / 1e6, v1 / 1e6,
                   (v0 - VMIN) / 1e6, (VMAX - v1) / 1e6, ang, "\u00b0", esp,
                   (m == por_omision) ? "   <- el de la tabla" : "");
            /* El mejor es el que mas lejos esta de los DOS topes y con buen
             * angulo; aqui se guarda el mejor angulo entre los que dejan al
             * menos 20 MHz de margen por los dos lados. */
            if ((v0 - VMIN) > 20e6 && (VMAX - v1) > 20e6 && esp > mejor_esp) {
                mejor_esp = esp; mejor = m;
            }
            if (m == por_omision) {
                esp_defecto = esp;
                marg_defecto_abajo = v0 - VMIN;
                marg_defecto_arriba = VMAX - v1;
            }
        }
        while (ms5351_lowband_z7_mult() != por_omision) {
            (void)ms5351_lowband_z7_siguiente();
        }

        printf("\n");
        snprintf(det, sizeof det, "%.0f MHz abajo, %.0f MHz arriba",
                 marg_defecto_abajo / 1e6, marg_defecto_arriba / 1e6);
        di("el valor de la tabla deja margen por los dos lados",
           marg_defecto_abajo > 20e6 && marg_defecto_arriba > 20e6, det);
        snprintf(det, sizeof det, "%u, con %.0f dB", mejor, mejor_esp);
        di("y es el de mejor angulo entre los que lo dejan",
           mejor == por_omision, det);
        (void)esp_defecto;
    }

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "TODO CORRECTO");
    return fallos ? 1 : 0;
}
