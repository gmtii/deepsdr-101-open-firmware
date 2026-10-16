/*
 * Banco de la aritmetica del reloj (User/reloj.c).
 *
 * Compila el fichero DEL FIRMWARE, no una copia: si la formula cambia alli,
 * esto la mide. Ver el comentario de cabecera de User/reloj.h.
 *
 * Lo que se busca aqui es lo que no se ve leyendo: la vuelta por medianoche,
 * el contador de arranque que llega a 2^32 y da la vuelta, el anclaje pedido
 * antes de que hayan pasado los milisegundos de atraso, y el desplazamiento
 * horario del RDS sacando los minutos fuera del dia por los dos lados.
 */
#include "reloj.h"
#include <stdio.h>
#include <stdint.h>

static int fallos = 0;

static void comprueba(int ok, const char *que)
{
    printf("  %-62s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/* Lo que marcaria el reloj, en milisegundos del dia, con ese desfase. */
static uint32_t marca(uint32_t ahora, uint32_t desf)
{
    return reloj_ms_del_dia(ahora, desf);
}

int main(void)
{
    printf("banco del reloj\n");

    /* ------------------------------------------------------------------ */
    printf("\n1. poner la hora a mano (solo minutos)\n");
    {
        /* Un arranque cualquiera con una fase dentro del minuto que NO es
         * cero: es justo lo que tiene que sobrevivir. */
        uint32_t ahora = 1234U * 60000U + 37123U;   /* ...y 37,123 s */
        uint32_t d = reloj_desfase_minuto(ahora, 21U, 47U);
        uint32_t m = marca(ahora, d);

        printf("    arranque %lu ms -> desfase %lu ms -> marca %02lu:%02lu:%02lu,%03lu\n",
               (unsigned long)ahora, (unsigned long)d,
               (unsigned long)(m / 3600000UL), (unsigned long)((m / 60000UL) % 60UL),
               (unsigned long)((m / 1000UL) % 60UL), (unsigned long)(m % 1000UL));

        comprueba(m / 60000UL == 21UL * 60UL + 47UL, "el minuto sale exacto");
        comprueba(d % 60000UL == 0UL, "el desfase es multiplo de un minuto");
        comprueba(m % 60000UL == ahora % 60000UL,
                  "y la fase dentro del minuto se queda donde estaba");
    }
    {
        /* Vuelta por medianoche: son las 23:50 de arranque y se pide 00:10. */
        uint32_t ahora = (23UL * 60UL + 50UL) * 60000UL;
        uint32_t d = reloj_desfase_minuto(ahora, 0U, 10U);
        comprueba(marca(ahora, d) / 60000UL == 10UL,
                  "cruzar medianoche hacia adelante");
        ahora = (0UL * 60UL + 10UL) * 60000UL;
        d = reloj_desfase_minuto(ahora, 23U, 50U);
        comprueba(marca(ahora, d) / 60000UL == 23UL * 60UL + 50UL,
                  "y hacia atras");
    }

    /* ------------------------------------------------------------------ */
    printf("\n2. anclar al segundo\n");
    {
        uint32_t ahora = 500000UL;      /* 500 s desde el arranque */
        uint32_t atraso = 1234UL;       /* el borde fue hace 1,234 s */
        int32_t  mins = 21 * 60 + 47;
        uint32_t d = reloj_desfase_anclado(ahora, mins, atraso);

        /* La prueba de verdad: en el INSTANTE del borde, el reloj tiene que
         * marcar las 21:47:00,000 clavadas. */
        uint32_t en_el_borde = marca(ahora - atraso, d);
        uint32_t ahora_marca = marca(ahora, d);

        printf("    en el borde marca %02lu:%02lu:%02lu,%03lu\n",
               (unsigned long)(en_el_borde / 3600000UL),
               (unsigned long)((en_el_borde / 60000UL) % 60UL),
               (unsigned long)((en_el_borde / 1000UL) % 60UL),
               (unsigned long)(en_el_borde % 1000UL));
        printf("    y ahora       %02lu:%02lu:%02lu,%03lu\n",
               (unsigned long)(ahora_marca / 3600000UL),
               (unsigned long)((ahora_marca / 60000UL) % 60UL),
               (unsigned long)((ahora_marca / 1000UL) % 60UL),
               (unsigned long)(ahora_marca % 1000UL));

        comprueba(en_el_borde == (uint32_t)mins * 60000UL,
                  "en el borde marca el minuto en punto, al milisegundo");
        comprueba(ahora_marca == (uint32_t)mins * 60000UL + atraso,
                  "y ahora marca ese minuto mas el atraso");
    }
    {
        /* Anclar ANTES de que hayan pasado los milisegundos de atraso: pasa
         * si se ancla en los primeros segundos de vida del aparato. La resta
         * se va por debajo de cero y en enteros sin signo el reloj se iria 49
         * dias sin que nada avisara. */
        uint32_t d = reloj_desfase_anclado(500UL, 10 * 60, 2000UL);
        uint32_t m = marca(500UL, d);
        /* El borde cayo 1,5 s ANTES del arranque, que es un instante que el
         * contador no puede ni representar. Asi que lo que se comprueba es lo
         * unico representable y es justo lo que la funcion promete: ahora
         * mismo el reloj marca el minuto en punto mas el atraso entero. */
        comprueba(m == 10UL * 3600000UL + 2000UL,
                  "el atraso mayor que el tiempo encendido no rompe nada");
    }
    {
        /* Minutos fuera de rango por los dos lados: el desplazamiento horario
         * del RDS puede sacarlos (UTC-11 o UTC+14 con la hora al borde). */
        uint32_t d1 = reloj_desfase_anclado(100000UL, -60, 0UL);     /* -1 h */
        uint32_t d2 = reloj_desfase_anclado(100000UL, 1440 + 60, 0UL); /* +25 h */
        comprueba(marca(100000UL, d1) == 23UL * 3600000UL,
                  "minutos negativos entran en el dia (-1h -> 23:00)");
        comprueba(marca(100000UL, d2) == 1UL * 3600000UL,
                  "y los que se pasan tambien (+25h -> 01:00)");
    }
    {
        /* El contador de arranque cerca de la vuelta de 32 bits (49,7 dias).
         * Aqui es donde una suma en 32 bits se iria a paseo en silencio. */
        uint32_t ahora = 0xFFFFF000UL;
        uint32_t d = reloj_desfase_anclado(ahora, 12 * 60, 1000UL);
        comprueba(marca(ahora - 1000UL, d) == 12UL * 3600000UL,
                  "a 49 dias de encendido, el anclaje sigue exacto");
        comprueba(d < 86400000UL, "y el desfase se queda dentro del dia");
    }

    /* ------------------------------------------------------------------ */
    printf("\n3. barrido: mil anclajes al azar\n");
    {
        unsigned long r = 987654321UL;
        int i, mal = 0;
        for (i = 0; i < 1000; i++) {
            uint32_t ahora, atraso, d, m;
            int32_t mins;
            r = r * 1103515245UL + 12345UL; ahora  = (uint32_t)(r >> 1);
            r = r * 1103515245UL + 12345UL; atraso = (uint32_t)((r >> 16) % 5000UL);
            r = r * 1103515245UL + 12345UL; mins   = (int32_t)((r >> 16) % 1440UL);

            d = reloj_desfase_anclado(ahora, mins, atraso);
            m = marca(ahora - atraso, d);
            if (m != (uint32_t)mins * 60000UL) { mal++; }
            if (d >= 86400000UL) { mal++; }
        }
        printf("    %d fallos de 1000\n", mal);
        comprueba(mal == 0, "todos caen clavados en el minuto en punto");
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}
