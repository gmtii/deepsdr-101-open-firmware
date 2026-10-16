/* Render de la pantalla de arranque: varios fotogramas a tiempos distintos. */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "splash.h"
#include "font_ui_14.h"
#include <stdio.h>
#include <stdlib.h>
const palette_t *g_pal = &k_pal_oscura;

int main(int argc, char **argv)
{
    uint32_t t = 0, hasta = (argc > 1) ? (uint32_t)atoi(argv[1]) : 1200;
    char nombre[64];

    /* segundo argumento: 1 = paleta contrastada, para ver que el arranque
     * tambien cambia con ella */
    if (argc > 2 && atoi(argv[2]) == 1) { g_pal = &k_pal_contraste; }
    /*
     * LA MEMORIA DE LAS COLUMNAS, PRESTADA IGUAL QUE EN LA RADIO.
     *
     * Alli sale del buffer de la cascada (ver splash_screen.c); aqui de un
     * array de este banco. splash.c no sabe de donde viene ninguna de las
     * dos, que es justo lo que le permite compilar tal cual en el PC.
     *
     * Con el segundo argumento a 2 se le pasa CERO, para ver el camino de
     * "no hay memoria que prestar": tiene que salir la pantalla fija, no
     * una pantalla rota.
     */
    static uint8_t ram[4096];
    void *presta = ram;
    uint32_t presta_n = (uint32_t)sizeof ram;

    if (argc > 2 && atoi(argv[2]) == 2) { presta = 0; presta_n = 0U; }
    if (presta_n != 0U && presta_n < splash_ram_bytes()) {
        printf("el banco presta %u bytes y la lluvia pide %u   MAL\n",
               (unsigned)presta_n, (unsigned)splash_ram_bytes());
        return 1;
    }

    lcd_sim_reset(0x0000);
    splash_reinicia(presta, presta_n);
    while (t <= hasta && splash_paso(t)) { t += 10; }
    snprintf(nombre, sizeof nombre, "out/splash_%lu%s.ppm", (unsigned long)hasta,
             (argc > 2 && atoi(argv[2]) == 1) ? "c"
             : ((argc > 2 && atoi(argv[2]) == 2) ? "f" : ""));
    lcd_sim_write_ppm(nombre);
    {
        lcd_cost_t c = lcd_sim_cost();
        printf("t=%lu ms  ventanas=%llu  accesos=%llu  (%.1f ms de bus en total)\n",
               (unsigned long)hasta, (unsigned long long)c.windows,
               (unsigned long long)c.bus_writes, lcd_sim_est_us(7, 168.0) / 1000.0);
    }

    /*
     * Y LO QUE UNA IMAGEN NO DICE SOLA: QUE HAYA ALGO PINTADO.
     *
     * Un .ppm todo negro se abre igual de bien que uno correcto, y quien
     * mira cuatro ficheros mira el que tiene algo y da por buenos los
     * demas. Asi que se cuentan los pixeles que no son el fondo:
     *
     *   - con lluvia y antes del titulo (900 ms), la pantalla tiene que
     *     estar razonablemente poblada: si el puerto de la animacion se
     *     rompiera, saldria negra y esto lo dice;
     *   - sin memoria prestada tiene que salir la pantalla FIJA, que
     *     tambien pinta - lo que no puede es salir negra ni rota-.
     */
    {
        const uint16_t *fb = lcd_sim_fb();
        long n = 0, total = (long)GFX2_W * (long)GFX2_H, i;
        int ok;
        uint16_t fondo;

        /*
         * El fondo NO es 0x0000: es PAL_SP_BG, y cambia con la paleta. La
         * primera version comparaba con negro y contaba el 100 % de los
         * pixeles como "pintados" en las cuatro imagenes, incluida la que
         * no tiene lluvia. Un banco que dice ok siempre es peor que no
         * tenerlo.
         *
         * Asi que el fondo se AVERIGUA: es el color que mas se repite. No
         * hay que escribirlo en ningun sitio y sigue valiendo si manana se
         * cambia la paleta del arranque.
         */
        {
            long mejor = -1;
            uint16_t cand[8];
            long cuenta[8];
            int nc = 0, k;

            /* Basta con mirar unos pocos colores distintos: en esta
             * pantalla hay seis y el fondo es el mas repetido con mucho. */
            for (i = 0; i < total; i += 7) {
                for (k = 0; k < nc; k++) { if (cand[k] == fb[i]) { cuenta[k]++; break; } }
                if (k == nc && nc < 8) { cand[nc] = fb[i]; cuenta[nc] = 1; nc++; }
            }
            fondo = cand[0];
            for (k = 0; k < nc; k++) { if (cuenta[k] > mejor) { mejor = cuenta[k]; fondo = cand[k]; } }
        }

        for (i = 0; i < total; i++) { if (fb[i] != fondo) { n++; } }
        printf("  pintados %ld de %ld pixeles (%.1f%%)\n", n, total,
               100.0 * (double)n / (double)total);

        /*
         * Dos umbrales, porque son dos pantallas distintas. Con lluvia hay
         * 44 columnas cayendo y sale un 3,6 % ya a los 900 ms; sin ella
         * solo estan el rotulo y los creditos, que son un 1,4 %. Medir las
         * dos con la misma vara haria que una de las dos mintiera.
         *
         * Los dos son holgados a proposito: miden "hay algo", no "hay
         * esto". Un umbral apretado se rompe al tocar la densidad de la
         * lluvia y entonces el banco estorba en vez de ayudar.
         */
        ok = (presta_n != 0U) ? (n > total / 50) : (n > total / 200);
        if (!ok) {
            printf("  la pantalla esta practicamente vacia   MAL\n");
            return 1;
        }
        /* Y al reves: sin lluvia NO puede salir tan poblada como con ella.
         * Si saliera, es que el camino de "sin memoria prestada" esta
         * pintando lluvia en memoria de otro. */
        if (presta_n == 0U && n > total / 50) {
            printf("  sin memoria prestada hay lluvia: esta escribiendo donde no debe   MAL\n");
            return 1;
        }
    }

    /* Los rotulos, medidos con la fuente con la que se pintan: gfx2_text_in
     * recorta por la derecha SIN AVISAR, asi que una linea que se pase no se
     * ve como un fallo, se ve como una linea mas corta. */
    if (hasta >= 3000U) {
        static const char *k_cred =
            "Kifo (EA4HEW) sobre el firmware de EA8DGL · UA6YKK · EA7GIB";
        int16_t w = gfx2_text_w(k_cred, &font_ui_14);

        printf("  los creditos miden %d px de los %d que hay   %s\n",
               w, GFX2_W, (w <= GFX2_W - 8) ? "ok" : "MAL");
        if (w > GFX2_W - 8) { return 1; }
    }
    return 0;
}
