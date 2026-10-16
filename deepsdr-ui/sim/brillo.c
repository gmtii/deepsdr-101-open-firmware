/*
 * EL BRILLO: LA CUENTA DEL CCR, PROBADA SOBRE EL backlight.c DE VERDAD.
 *
 * POR QUE EXISTE (25/09/2026). Lo pidio el usuario, y al mirarlo
 * aparecieron dos cosas: que el 100% dejaba el pin sin un solo flanco, y
 * que el valor de arranque llevaba en 50 desde julio por una causa que ya
 * no existia.
 *
 * Los dos arreglos viven en una sola formula de tres lineas. Esto la
 * ejecuta - la de backlight.c, no una copia - y mira el CCR que sale.
 *
 * COMO. backlight.c escribe el CCR llamando a
 * timer_channel_output_pulse_value_config(). Aqui se define esa funcion y
 * se guarda lo que le llega. Es la unica forma de mirar un valor que el
 * firmware manda al hardware sin reimplementar la cuenta, que seria probar
 * la copia y no el codigo.
 */
#include <stdio.h>
#include <stdint.h>
#include "gd32f4xx.h"
#include "backlight.h"

#define ARR   4991U          /* BACKLIGHT_PWM_PERIOD, de backlight.c */
#define PASOS 4992U          /* ARR+1: los pasos que tiene el periodo   */

static uint32_t s_ccr;
static int      s_escrituras;

/* --- los sucedaneos que backlight.c necesita para enlazar --- */
void timer_channel_output_pulse_value_config(uint32_t t, uint16_t ch, uint32_t pulse)
{ (void)t; (void)ch; s_ccr = pulse; s_escrituras++; }
void timer_deinit(uint32_t t) { (void)t; }
void timer_struct_para_init(timer_parameter_struct *p) { (void)p; }
void gd32_timer_init(uint32_t t, timer_parameter_struct *p) { (void)t; (void)p; }
void timer_channel_output_struct_para_init(timer_oc_parameter_struct *p) { (void)p; }
void timer_channel_output_config(uint32_t t, uint16_t ch, timer_oc_parameter_struct *p)
{ (void)t; (void)ch; (void)p; }
void timer_channel_output_mode_config(uint32_t t, uint16_t ch, uint16_t m)
{ (void)t; (void)ch; (void)m; }
void timer_auto_reload_shadow_enable(uint32_t t) { (void)t; }
void timer_channel_output_shadow_config(uint32_t t, uint16_t ch, uint16_t s)
{ (void)t; (void)ch; (void)s; }
void timer_enable(uint32_t t) { (void)t; }
void gpio_mode_set(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{ (void)a; (void)b; (void)c; (void)d; }
void gpio_output_options_set(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{ (void)a; (void)b; (void)c; (void)d; }
void gpio_af_set(uint32_t a, uint32_t b, uint32_t c) { (void)a; (void)b; (void)c; }
void rcu_periph_clock_enable(uint32_t p) { (void)p; }

static int fallos;

static void exige(int cond, const char *que)
{
    if (!cond) { printf("    FALLO: %s\n", que); fallos++; }
}

int main(void)
{
    uint32_t anterior;
    int p;

    backlight_init();

    printf("  el brillo al arrancar, que es lo que manda cuando no hay\n"
           "  CONFIG.CSV con un valor guardado:\n");
    printf("    %u%%  ->  CCR %lu de %u  (%.2f%% del tiempo encendido)\n",
           backlight_get_percent(), (unsigned long)s_ccr, PASOS,
           100.0 * (double)s_ccr / (double)PASOS);
    exige(backlight_get_percent() >= 80U,
          "el arranque se ha quedado por debajo del 80% otra vez");

    printf("\n  la cuenta, de punta a punta:\n");
    printf("    %6s  %8s  %10s  %s\n", "pedido", "aplicado", "CCR", "encendido");
    for (p = 0; p <= 100; p += 10) {
        backlight_set_percent((uint8_t)p);
        printf("    %5d%%  %7u%%  %10lu  %8.2f%%\n",
               p, backlight_get_percent(), (unsigned long)s_ccr,
               100.0 * (double)s_ccr / (double)PASOS);
    }

    printf("\n  comprobaciones:\n");

    /*
     * EL TECHO, Y ESTE BANCO EXIGIA LO CONTRARIO DE LO QUE HAY QUE EXIGIR.
     *
     * Pedido por el usuario.
     *
     * Aqui ponia esto:
     *
     *     exige(s_ccr <= ARR, "el 100% vuelve a dejar el pin sin flancos");
     *     exige(100.0 * (PASOS - s_ccr) / PASOS < 0.1,
     *           "el techo nuevo cuesta mas luz de la que se dijo");
     *
     * O sea que OBLIGABA al recorte del CCR, y la segunda comprobacion
     * -la que tenia que cazar que costara luz- media el CICLO DE TRABAJO
     * y lo llamaba luz. Con eso no podia fallar nunca: yo habia calculado
     * que 4991/4992 es el 99,98% del tiempo, y el banco comprobaba
     * exactamente ese mismo calculo. Lo que no mide ninguna cuenta de
     * aqui es lo que hace el driver de la retroiluminacion con un hueco
     * de 10 ns por periodo, que es lo que de verdad decidia el brillo.
     *
     * Un banco escrito a la vez que el codigo comparte sus puntos ciegos.
     * Es la cuarta vez en este proyecto -el del splash comparaba con el
     * fondo equivocado, el de TEXT5 tenia la regla al reves, y hubo un
     * exige() que no podia fallar- y esta es la que mas cara salio: dos
     * meses de pantalla a medio gas en la version anterior por el valor de
     * arranque, y luego esto.
     *
     * AHORA SE EXIGE LO CONTRARIO: que el 100% sea 100% de verdad, o sea
     * CCR por encima del ARR, que en modo PWM0 deja la salida activa todo
     * el periodo. Es lo que hace el proyecto padre y es lo que corre en la
     * radio de Esteban.
     */
    backlight_set_percent(100U);
    printf("    el 100%% da CCR %lu con ARR %u\n", (unsigned long)s_ccr, ARR);
    exige(s_ccr > ARR,
          "el 100% no llega a CCR > ARR: no es 100% de verdad, y se ve");
    exige(s_ccr == PASOS, "el 100% no da los pasos enteros del periodo");

    /* EL SUELO. Nunca negro, por peticion expresa. */
    backlight_set_percent(0U);
    exige(backlight_get_percent() == 10U, "el suelo del 10% no se aplica");
    exige(s_ccr > 0U, "al 0% la pantalla se queda a oscuras del todo");

    /* Y que dormir SI apague, saltandose el suelo - son cosas distintas y
     * es facil arreglar una y romper la otra. */
    backlight_sleep();
    exige(s_ccr == 0U, "dormir ya no apaga la retroiluminacion");
    backlight_wake();
    exige(backlight_get_percent() == 10U && s_ccr > 0U,
          "despertar no devuelve el brillo que habia");

    /* MONOTONA. Si subir el mando alguna vez bajara la luz, esto lo dice. */
    backlight_set_percent(10U);
    anterior = s_ccr;
    for (p = 11; p <= 100; p++) {
        backlight_set_percent((uint8_t)p);
        if (s_ccr < anterior) {
            printf("    FALLO: de %d%% a %d%% el CCR BAJA (%lu -> %lu)\n",
                   p - 1, p, (unsigned long)anterior, (unsigned long)s_ccr);
            fallos++;
            break;
        }
        anterior = s_ccr;
    }

    /*
     * QUE EL BANCO FALLE CUANDO DEBE, ahora al reves que antes.
     *
     * Esto comprobaba que la comprobacion del techo saltaria con la cuenta
     * SIN limitar -que es la buena- y por eso el banco entero era una
     * trampa coherente consigo misma. Lo que hay que comprobar es lo otro:
     * que si alguien vuelve a poner el recorte, esto salta.
     *
     * Se simula el recorte a mano y se mira que la regla de arriba lo
     * rechace. Sin esto, "TODO CORRECTO" no distingue entre que el 100%
     * este bien y que la prueba no sepa mirarlo.
     */
    {
        uint32_t con_recorte = PASOS * 100U / 100U;

        if (con_recorte > ARR) { con_recorte = ARR; }   /* el fallo de antes */
        exige(!(con_recorte > ARR),
              "el banco no detectaria el recorte: revisar esta prueba");
        printf("    con el recorte de antes el 100%% daba CCR %lu (<= ARR %u):"
               " este banco lo caza\n", (unsigned long)con_recorte, ARR);
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
