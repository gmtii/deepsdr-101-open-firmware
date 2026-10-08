/*
 * POR QUE SE REINICIO LA RADIO. Ver reset_causa.h para el porque.
 * Aqui solo esta el como.
 */
#include "reset_causa.h"
#include "gd32f4xx.h"

/*
 * El historial vive en .noinit, igual que el testigo del DFU
 * (dfu_salto.c): un reinicio NO borra la RAM, solo el arranque de C la
 * pondria a cero, y .noinit se queda fuera de eso. Asi un reinicio puede
 * contarle al siguiente lo que le paso.
 *
 * Y lleva su propia magia porque en un arranque EN FRIO lo que hay aqui es
 * basura: sin la magia, la primera vez despues de enchufar la radio se
 * leeria un historial inventado y eso es peor que no tener historial.
 */
#define RC_MAGIA  0x52435B01UL   /* 'RC' + version del formato */

__attribute__((section(".noinit"))) static uint32_t s_magia;
__attribute__((section(".noinit"))) static uint8_t  s_hist[RC_HIST_N];
__attribute__((section(".noinit"))) static uint8_t  s_hist_n;
__attribute__((section(".noinit"))) static uint16_t s_cuenta;

static uint8_t s_ahora = RC_NADA;

uint8_t reset_causa_mira(void)
{
    uint32_t r = RCU_RSTSCK;
    uint8_t  c = RC_NADA;

    /*
     * EL ORDEN IMPORTA, Y NO ES CAPRICHO.
     *
     * El chip deja PUESTOS varios bits a la vez: un reinicio por tension
     * enciende PORRSTF *y* EPRSTF, porque al caer la alimentacion la
     * patilla de reset tambien se va abajo. Si se mirara EPRSTF primero,
     * TODA caida de tension se contaria como "alguien toco la patilla", que
     * es justo la confusion que este fichero existe para deshacer.
     *
     * Asi que se mira de la causa mas "de raiz" hacia la mas superficial:
     * tension primero, patilla despues.
     */
    if (r & RCU_RSTSCK_PORRSTF)        { c = RC_TENSION; }
    else if (r & RCU_RSTSCK_SWRSTF)    { c = RC_SOFTWARE; }
    else if (r & RCU_RSTSCK_FWDGTRSTF) { c = RC_PERRO; }
    else if (r & RCU_RSTSCK_WWDGTRSTF) { c = RC_PERRO; }
    else if (r & RCU_RSTSCK_LPRSTF)    { c = RC_BAJO_CONSUMO; }
    else if (r & RCU_RSTSCK_EPRSTF)    { c = RC_PATILLA; }

    /*
     * Y SE LIMPIAN, QUE SI NO ESTO MIENTE A PARTIR DEL SEGUNDO ARRANQUE.
     * Los bits se quedan puestos hasta que alguien los borra: sin esto, un
     * arranque en frio seguido de diez reinicios por patilla seguiria
     * diciendo "tension" las once veces.
     */
    rcu_all_reset_flag_clear();

    if (s_magia != RC_MAGIA) {
        /* Arranque en frio de verdad: lo que habia en .noinit era basura. */
        s_magia  = RC_MAGIA;
        s_hist_n = 0U;
        s_cuenta = 0U;
    }

    s_ahora = c;
    if (s_cuenta < 0xFFFFU) { s_cuenta++; }

    /* El historial, el mas nuevo primero. */
    {
        uint8_t i = (s_hist_n < RC_HIST_N) ? s_hist_n : (uint8_t)(RC_HIST_N - 1U);
        while (i > 0U) { s_hist[i] = s_hist[i - 1U]; i--; }
        s_hist[0] = c;
        if (s_hist_n < RC_HIST_N) { s_hist_n++; }
    }

    return c;
}

uint8_t reset_causa(void)       { return s_ahora; }
uint16_t reset_causa_cuenta(void) { return (s_magia == RC_MAGIA) ? s_cuenta : 0U; }

uint8_t reset_causa_historial(uint8_t *fuera, uint8_t max)
{
    uint8_t n, i;

    if (fuera == 0 || max == 0U || s_magia != RC_MAGIA) { return 0U; }
    n = (s_hist_n < max) ? s_hist_n : max;
    for (i = 0U; i < n; i++) { fuera[i] = s_hist[i]; }
    return n;
}

char reset_causa_letra(uint8_t c)
{
    switch (c) {
    case RC_TENSION:      return 'T';   /* Tension */
    case RC_PATILLA:      return 'P';   /* Patilla de reset */
    case RC_SOFTWARE:     return 'S';   /* Software */
    case RC_PERRO:        return 'G';   /* perro Guardian */
    case RC_BAJO_CONSUMO: return 'B';   /* Bajo consumo */
    default:              return '?';
    }
}
