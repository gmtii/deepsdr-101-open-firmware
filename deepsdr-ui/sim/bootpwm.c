/*
 * QUE LAS ESCRITURAS A MANO DEL CARGADOR SEAN LAS DE LA BIBLIOTECA.
 *
 * El cargador de arranque se salia de sus 32 KB por 136 bytes. Lo que lo
 * sacaba eran seis funciones de Firmware/Source/gd32f4xx_timer.c que el
 * enlazador trae enteras -con su switch de catorce temporizadores o de
 * cuatro canales- para configurar UN canal de UN temporizador: 896 bytes
 * para encender la retroiluminacion. boot/retro_pwm.h las sustituye por
 * las escrituras a registro que esas funciones hacen de verdad.
 *
 * El riesgo evidente de eso es leer mal el fuente del fabricante y dejar
 * la radio arrancando a oscuras, que es la averia que no se puede
 * arreglar desde la propia radio. Asi que esto no comprueba nada "a ojo":
 *
 *   1. sim/gd32shim/gd32f4xx.h cambia REG32() por un mapa de registros en
 *      memoria. Con eso, gd32f4xx_timer.c del fabricante compila en el PC
 *      SIN TOCARLE UNA COMA.
 *   2. Se ejecuta la secuencia vieja -las seis llamadas, con los mismos
 *      argumentos que tenia boot_main.c- y se guarda el mapa.
 *   3. Se borra el mapa y se ejecuta retro_pwm_timer4(), la de verdad,
 *      incluida de boot/retro_pwm.h.
 *   4. Se exige que los dos mapas sean identicos: mismas direcciones
 *      tocadas y mismo valor en cada una. Ni una de mas ni una de menos.
 *
 * Si algun dia alguien toca retro_pwm.h, esto se entera.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gd32f4xx.h"
#include "retro_pwm.h"

/* --- el mapa de registros de mentira -------------------------------- */

#define MAPA_MAX 64U

static struct { unsigned long dir; uint32_t val; } s_mapa[MAPA_MAX];
static unsigned s_n;

unsigned g_reset_cuenta[14];

uint32_t *reg_ptr(unsigned long addr)
{
    unsigned i;
    for (i = 0U; i < s_n; i++) {
        if (s_mapa[i].dir == addr) { return &s_mapa[i].val; }
    }
    if (s_n >= MAPA_MAX) {
        printf("FALLO: el mapa de registros se queda corto\n");
        exit(1);
    }
    s_mapa[s_n].dir = addr;
    s_mapa[s_n].val = 0U;
    return &s_mapa[s_n++].val;
}

void reg_borra(void)
{
    s_n = 0U;
    memset(s_mapa, 0, sizeof(s_mapa));
    memset(g_reset_cuenta, 0, sizeof(g_reset_cuenta));
}

void rcu_periph_reset_enable(rcu_periph_reset_enum p)
{
    if ((unsigned)p < 14U) { g_reset_cuenta[(unsigned)p]++; }
}

void rcu_periph_reset_disable(rcu_periph_reset_enum p) { (void)p; }

/* --- lo que hacia boot_main.c antes, llamada por llamada ------------- */

static void secuencia_vieja(void)
{
    timer_parameter_struct    t;
    timer_oc_parameter_struct oc;

    timer_deinit(TIMER4);
    timer_struct_para_init(&t);
    t.prescaler         = 7U;
    t.alignedmode       = TIMER_COUNTER_EDGE;
    t.counterdirection  = TIMER_COUNTER_UP;
    t.clockdivision     = TIMER_CKDIV_DIV1;
    t.period            = 375U;
    t.repetitioncounter = 0U;
    gd32_timer_init(TIMER4, &t);

    timer_channel_output_struct_para_init(&oc);
    oc.outputstate = TIMER_CCX_ENABLE;
    oc.ocpolarity  = TIMER_OC_POLARITY_HIGH;
    timer_channel_output_config(TIMER4, TIMER_CH_3, &oc);
    timer_channel_output_mode_config(TIMER4, TIMER_CH_3, TIMER_OC_MODE_PWM0);
    timer_channel_output_pulse_value_config(TIMER4, TIMER_CH_3, 188U);

    timer_auto_reload_shadow_enable(TIMER4);
    timer_enable(TIMER4);
}

/* --- comparar ------------------------------------------------------- */

static struct { unsigned long dir; uint32_t val; } s_copia[MAPA_MAX];
static unsigned s_copia_n;
static unsigned s_copia_reset[14];

static void guarda(void)
{
    memcpy(s_copia, s_mapa, sizeof(s_copia));
    memcpy(s_copia_reset, g_reset_cuenta, sizeof(s_copia_reset));
    s_copia_n = s_n;
}

static const char *nombre(unsigned long dir)
{
    unsigned long o = dir - (unsigned long)TIMER4;
    switch (o) {
    case 0x00U: return "CTL0";
    case 0x04U: return "CTL1";
    case 0x14U: return "SWEVG";
    case 0x18U: return "CHCTL0";
    case 0x1CU: return "CHCTL1";
    case 0x20U: return "CHCTL2";
    case 0x24U: return "CNT";
    case 0x28U: return "PSC";
    case 0x2CU: return "CAR";
    case 0x30U: return "CREP";
    case 0x40U: return "CH3CV";
    default:    return "?";
    }
}

static uint32_t busca(unsigned long dir, int *hay)
{
    unsigned i;
    for (i = 0U; i < s_n; i++) {
        if (s_mapa[i].dir == dir) { *hay = 1; return s_mapa[i].val; }
    }
    *hay = 0;
    return 0U;
}

int main(void)
{
    unsigned i;
    int fallos = 0;

    printf("El PWM de la retroiluminacion del cargador\n");
    printf("==========================================\n\n");

    reg_borra();
    secuencia_vieja();
    guarda();

    printf("  la biblioteca del fabricante toca %u registros:\n", s_copia_n);
    for (i = 0U; i < s_copia_n; i++) {
        printf("    TIMER4+0x%02lX %-6s = 0x%08X\n",
               s_copia[i].dir - (unsigned long)TIMER4,
               nombre(s_copia[i].dir), (unsigned)s_copia[i].val);
    }
    printf("    y resetea el TIMER4 %u vez/veces\n\n", s_copia_reset[4]);

    reg_borra();
    retro_pwm_timer4();

    printf("  retro_pwm_timer4() toca %u registros\n\n", s_n);

    /* 1. todo lo que tocaba la biblioteca, con el mismo valor */
    for (i = 0U; i < s_copia_n; i++) {
        int hay;
        uint32_t v = busca(s_copia[i].dir, &hay);
        if (!hay) {
            printf("  FALLO: no se escribe TIMER4+0x%02lX (%s)\n",
                   s_copia[i].dir - (unsigned long)TIMER4, nombre(s_copia[i].dir));
            fallos++;
        } else if (v != s_copia[i].val) {
            printf("  FALLO: TIMER4+0x%02lX (%s) = 0x%08X, deberia ser 0x%08X\n",
                   s_copia[i].dir - (unsigned long)TIMER4, nombre(s_copia[i].dir),
                   (unsigned)v, (unsigned)s_copia[i].val);
            fallos++;
        }
    }

    /* 2. y nada de mas: un registro extra tocado tambien es un cambio */
    for (i = 0U; i < s_n; i++) {
        unsigned j;
        int esta = 0;
        for (j = 0U; j < s_copia_n; j++) {
            if (s_copia[j].dir == s_mapa[i].dir) { esta = 1; break; }
        }
        if (!esta) {
            printf("  FALLO: se escribe TIMER4+0x%02lX (%s) y la biblioteca no\n",
                   s_mapa[i].dir - (unsigned long)TIMER4, nombre(s_mapa[i].dir));
            fallos++;
        }
    }

    /* 3. y el reset del periferico, que no es un registro del temporizador */
    for (i = 0U; i < 14U; i++) {
        if (g_reset_cuenta[i] != s_copia_reset[i]) {
            printf("  FALLO: el TIMER%u se resetea %u veces y antes %u\n",
                   i, g_reset_cuenta[i], s_copia_reset[i]);
            fallos++;
        }
    }

    /* 4. y que los numeros sean los de la pantalla, no otros cualesquiera */
    {
        int hay;
        uint32_t car  = busca((unsigned long)TIMER4 + 0x2CU, &hay);
        uint32_t psc  = busca((unsigned long)TIMER4 + 0x28U, &hay);
        uint32_t ccr  = busca((unsigned long)TIMER4 + 0x40U, &hay);
        uint32_t ctl0 = busca((unsigned long)TIMER4 + 0x00U, &hay);
        uint32_t ch2  = busca((unsigned long)TIMER4 + 0x20U, &hay);
        uint32_t ch1  = busca((unsigned long)TIMER4 + 0x1CU, &hay);

        if (car != 375U)  { printf("  FALLO: periodo %u, no 375\n", car);  fallos++; }
        if (psc != 7U)    { printf("  FALLO: prescaler %u, no 7\n", psc);  fallos++; }
        if (ccr != 188U)  { printf("  FALLO: pulso %u, no 188\n", ccr);    fallos++; }
        if ((ctl0 & 1U) == 0U) { printf("  FALLO: el TIMER4 no arranca\n"); fallos++; }
        if ((ctl0 & (1U << 7)) == 0U) { printf("  FALLO: sin sombra del ARR\n"); fallos++; }
        if ((ch2 & (1U << 12)) == 0U) { printf("  FALLO: canal 3 no habilitado\n"); fallos++; }
        if ((ch2 & (1U << 13)) != 0U) { printf("  FALLO: polaridad baja\n"); fallos++; }
        if (((ch1 >> 12) & 7U) != 6U) { printf("  FALLO: el canal 3 no esta en PWM0\n"); fallos++; }
    }

    if (fallos == 0) {
        printf("  bien: los dos caminos dejan el TIMER4 igual,\n");
        printf("        31,9 kHz al 50%% por PA3, y 896 bytes menos.\n");
        return 0;
    }
    printf("\n  %d fallo(s)\n", fallos);
    return 1;
}
