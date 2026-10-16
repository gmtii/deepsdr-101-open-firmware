/*
 * EL VOLTIMETRO CONTRA LA REFERENCIA INTERNA (User/battery.c).
 *
 * Pedido por el usuario, 12/10/2026.
 *
 * LO QUE SE MIDE AQUI, y por que este banco es el que decide si el
 * arreglo vale:
 *
 *  1. Que con el rail en su sitio la cuenta da lo que tiene que dar. Sin
 *     esto, lo demas no significa nada.
 *
 *  2. QUE LA DISPERSION BAJA DE VERDAD. Esto es el banco. Se simula una
 *     bateria FIJA de 3,9 V leida por diez radios cuyo regulador anda
 *     entre 2,85 y 3,15 V, y se compara el ABANICO de lecturas que da la
 *     version de antes -constante 3300- con el que da la de ahora. Si el
 *     abanico no se estrecha, el arreglo no sirve y da igual lo elegante
 *     que sea la cuenta.
 *
 *  3. Y que lo que queda -la tolerancia de la propia referencia interna-
 *     esta acotado y se puede quitar con el ajuste del usuario.
 *
 *  4. Los topes: que una lectura imposible de la referencia no se
 *     propague, que no haya division por cero, y que un ajuste negativo
 *     con la bateria a cero no de un numero enorme al volver a sin signo.
 */
#include "battery.h"
#include <stdio.h>
#include <stdlib.h>

static int fallos;
static void di(const char *q, int bien, const char *extra)
{
    printf("    %-52s %s%s%s\n", q, bien ? "ok" : "*** MAL ***",
           extra ? "  " : "", extra ? extra : "");
    if (!bien) { fallos++; }
}

/* El crudo que daria el ADC para una tension en el pin, con un rail dado. */
static uint16_t crudo(double pin_mv, double rail_mv)
{
    double v = pin_mv * 4095.0 / rail_mv;

    if (v < 0.0)    { v = 0.0; }
    if (v > 4095.0) { v = 4095.0; }
    return (uint16_t)(v + 0.5);
}

/* La version de ANTES, tal cual estaba: constante 3300 y diodo de 400. */
static uint16_t como_antes(uint16_t raw)
{
    uint32_t adc_mv = ((uint32_t)raw * 3300UL) / 4095UL;

    return (uint16_t)(adc_mv * 4UL + 400UL);
}

int main(void)
{
    printf("\nEL VOLTIMETRO CONTRA LA REFERENCIA INTERNA (User/battery.c)\n");

    /* ---------------------------------------------------------------- */
    printf("\n  1. CON EL RAIL EN SU SITIO, LA CUENTA CUADRA\n");
    {
        /* Rail exactamente 3,000 V: la referencia de 1,2 V da este crudo. */
        uint16_t rawv = crudo(1200.0, 3000.0);
        uint16_t vdda = battery_vdda_de_raw(rawv);
        char t[64];

        sprintf(t, "%u mV", (unsigned)vdda);
        di("el rail medido sale 3000 mV (+-5)",
           vdda >= 2995U && vdda <= 3005U, t);
    }
    {
        /* Bateria de 3900 mV: el pin ve (3900 - 700) / 4 = 800 mV. */
        uint16_t raw = crudo((3900.0 - 700.0) / 4.0, 3000.0);
        uint16_t mv  = battery_mv_de_raw(raw, 3000U, 0);
        char t[64];

        sprintf(t, "%u mV", (unsigned)mv);
        di("una bateria de 3900 mV se lee 3900 (+-10)",
           mv >= 3890U && mv <= 3910U, t);
    }

    /* ---------------------------------------------------------------- */
    printf("\n  2. Y LA DISPERSION BAJA: DIEZ RADIOS, LA MISMA BATERIA\n");
    {
        const double bat = 3900.0;
        const double pin = (bat - 700.0) / 4.0;
        int i;
        unsigned ant_min = 65535U, ant_max = 0U;
        unsigned hoy_min = 65535U, hoy_max = 0U;

        printf("      rail      antes      ahora\n");
        for (i = 0; i < 10; i++) {
            double rail = 2850.0 + (double)i * (300.0 / 9.0);
            uint16_t rb = crudo(pin, rail);
            uint16_t rv = crudo(1200.0, rail);
            unsigned a = como_antes(rb);
            unsigned h = battery_mv_de_raw(rb, battery_vdda_de_raw(rv), 0);

            printf("      %4.0f mV   %5u mV   %5u mV\n", rail, a, h);
            if (a < ant_min) { ant_min = a; }
            if (a > ant_max) { ant_max = a; }
            if (h < hoy_min) { hoy_min = h; }
            if (h > hoy_max) { hoy_max = h; }
        }
        {
            unsigned ant = ant_max - ant_min, hoy = hoy_max - hoy_min;
            char t[96];

            sprintf(t, "abanico %u mV -> %u mV", ant, hoy);
            di("el abanico de lecturas se estrecha", hoy < ant, t);
            di("y se estrecha MUCHO, no un poco", hoy * 8U < ant, t);

            /*
             * NO se exige que ANTES fallaran todas, porque no es verdad y
             * la primera version de este banco lo exigia: con el rail
             * cerca de 3017 mV los dos errores -la referencia 300 mV alta
             * y el diodo 300 mV bajo- se cancelan y la lectura sale
             * clavada. Esa es justo la trampa que tenia el ajuste "a ojo"
             * del diodo: en UNA radio acertaba.
             *
             * Lo que se exige es lo que de verdad importa: que el error
             * dependiera del rail, o sea que cada radio leyera distinto.
             */
            sprintf(t, "antes %u..%u mV", ant_min, ant_max);
            di("antes cada radio leia una cosa (mas de 300 mV de abanico)",
               ant > 300U, t);
            sprintf(t, "ahora %u..%u mV", hoy_min, hoy_max);
            di("y ahora todas aciertan dentro de 10 mV",
               hoy_min >= 3890U && hoy_max <= 3910U, t);
        }
    }

    /* ---------------------------------------------------------------- */
    printf("\n  3. LO QUE QUEDA: LA TOLERANCIA DE LA PROPIA REFERENCIA\n");
    {
        /*
         * La referencia interna tambien se mueve de chip a chip. Si en
         * ESTA unidad valiera 1,24 V en vez de 1,20, la radio creeria que
         * el rail es mas bajo de lo que es y leeria la bateria baja. Se
         * mide cuanto, y que el ajuste del usuario lo quita.
         */
        const double bat = 3900.0, rail = 3000.0;
        uint16_t rb = crudo((bat - 700.0) / 4.0, rail);
        uint16_t rv = crudo(1240.0, rail);      /* la referencia real, +3,3% */
        uint16_t mv = battery_mv_de_raw(rb, battery_vdda_de_raw(rv), 0);
        int err = (int)mv - (int)bat;
        char t[64];

        sprintf(t, "%d mV", err);
        di("con la referencia un 3,3% alta, el error cabe en 150 mV",
           err < 0 && err > -150, t);
        {
            uint16_t ok = battery_mv_de_raw(rb, battery_vdda_de_raw(rv),
                                            (int16_t)(-err));
            sprintf(t, "%u mV", (unsigned)ok);
            di("y el ajuste del usuario lo deja clavado",
               ok >= 3895U && ok <= 3905U, t);
        }
    }

    /* ---------------------------------------------------------------- */
    printf("\n  4. LOS TOPES, QUE ES DONDE ESTO SE ROMPE\n");
    di("raw 0 no divide por cero: sale el nominal",
       battery_vdda_de_raw(0U) == 3000U, 0);
    di("raw 1 tampoco", battery_vdda_de_raw(1U) == 3000U, 0);
    di("raw 4095 tampoco", battery_vdda_de_raw(4095U) == 3000U, 0);
    di("una lectura de referencia absurda no se propaga",
       battery_vdda_de_raw(100U) == 3000U, 0);
    di("y una creible si se usa",
       battery_vdda_de_raw(crudo(1200.0, 3300.0)) > 3200U, 0);
    {
        /* Bateria a cero y ajuste negativo: sin suelo, esto daba 65036. */
        uint16_t mv = battery_mv_de_raw(0U, 3000U, (int16_t)-500);

        di("bateria a cero con ajuste negativo no da un numero enorme",
           mv < 1000U, 0);
    }
    {
        uint16_t mv = battery_mv_de_raw(4095U, 3000U, 500);

        di("y el tope de arriba tampoco se desborda", mv <= 65535U && mv > 0U, 0);
    }

    /* ---------------------------------------------------------------- */
    printf("\n  5. EL TERMOMETRO, QUE LLEVABA EL MISMO ERROR\n");
    {
        /* 1450 mV en el sensor son 25 grados, por la hoja de datos. */
        uint16_t raw = crudo(1450.0, 3000.0);
        int16_t  t25 = battery_temp_de_raw(raw, 3000U);
        int16_t  mal = battery_temp_de_raw(raw, 3300U);
        char t[64];

        sprintf(t, "%d C", (int)t25);
        di("con el rail medido, 1450 mV son 25 grados (+-1)",
           t25 >= 24 && t25 <= 26, t);
        /*
         * MEDIDO, no supuesto: la primera version de este banco exigia
         * "mas de 20 grados" porque un 10 % de error en el voltaje sonaba
         * a mucho. Son CUATRO. La pendiente del sensor es de 4,1 mV por
         * grado y el error de voltaje cae sobre 1450 mV, no sobre el
         * fondo de escala, asi que el arrastre es mucho menor de lo que
         * parece. Cuatro grados en un termometro que ya avisa de que no
         * esta calibrado no es el titular de este arreglo - el titular
         * son los 348 mV de la bateria- pero se arregla de paso y se deja
         * escrito cuanto es, no cuanto me parecia.
         */
        sprintf(t, "%d C con el 3300 de antes, %d con el rail medido",
                (int)mal, (int)t25);
        di("y con la constante de antes se iba unos 4 grados",
           (t25 - mal) >= 3 && (t25 - mal) <= 6, t);
    }

    printf(fallos ? "\n  HAY FALLOS: %d\n\n" : "\n  TODO CORRECTO\n\n", fallos);
    return fallos ? 1 : 0;
}
