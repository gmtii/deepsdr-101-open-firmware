/*
 * SSTV DE VERDAD: UNA GRABACION DEL AIRE POR EL DECODIFICADOR DE LA RADIO.
 *
 * POR QUE EXISTE - 06/10/2026.
 *
 * El banco sintetico (sstvtest.c) FABRICA la señal con el mismo plan con el
 * que el decodificador la lee, asi que comprueba que las dos mitades estan
 * de acuerdo - pero no que ninguna de las dos se parezca a lo que sale de
 * una radio. Y lo que llega por antena trae tres cosas que el banco no
 * tiene: la sintonia desviada, el ruido, y el desvanecimiento.
 *
 * Esto coge un fichero de muestras crudas -f32, mono, 12 kHz, que es
 * exactamente lo que le llega a sstv_process() en la radio- y lo mete por
 * el decodificador de verdad, el mismo sstv.c que se compila para el
 * GD32. Lo que imprime es por donde va el detector de cabecera paso a paso,
 * el desvio que ha medido y cuantas lineas ha sacado.
 *
 * NO LLEVA VALORES ESPERADOS y por eso no esta en `make comprueba`: es una
 * herramienta de medir, no una comprobacion. Lo que diga hay que leerlo.
 *
 *   ./out/sstvaire fichero.f32 [tasa_hz]
 */
#include "sstv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * EL POZO DE RAM PRESTADA, PARA EL BANCO - 07/10/2026.
 *
 * Desde hoy la cola de salida de sstv.c no es un array permanente sino un
 * prestamo de hfdl_ram.h: once kilobytes y medio que solo son de SSTV
 * mientras SSTV esta puesto. Aqui se le da un pozo del tamaño real.
 *
 * Las firmas estan COPIADAS DE hfdl_ram.h, no escritas de memoria. Un dia se
 * escribieron de memoria en helltest.c, enlazaron sin rechistar -C no
 * comprueba firmas entre ficheros- y hell_start() leyo capacidad cero: el
 * banco decia "0 columnas de 105" y nada apuntaba a la causa.
 *
 * Y se cuentan los prestamos: si sstv_start() coge y sstv_stop() no suelta,
 * el siguiente modo que lo pida se queda sin memoria y no hay forma de que
 * eso salga en una prueba que solo mire la foto.
 */
static unsigned g_ram_cog, g_ram_sue;
static unsigned char g_ram_pozo[0xD4A0];
void           hfdl_ram_coge(void) { g_ram_cog++; }
unsigned char *hfdl_ram_todo(unsigned *cap)
{
    if (cap) { *cap = (unsigned)sizeof g_ram_pozo; }
    return g_ram_pozo;
}
void           hfdl_ram_suelta(void) { g_ram_sue++; }

#define BLOQUE 12U   /* 1 ms: el detector de cabecera cambia de paso en 4 ms */

int main(int argc, char **argv)
{
    const char *ruta;
    float fs = 12000.0f;
    FILE *f;
    long n_total;
    float *x;
    uint32_t i, n;
    uint8_t est_ant = 0xFFU, est;
    uint32_t pasos[8] = {0};
    uint32_t lider, c1200;
    float hz;
    sstv_info_t in;
    uint32_t lineas = 0UL, img_ant = 0UL;
    const uint8_t *l;
    uint16_t y, y_max = 0U;
    double t;

    if (argc < 2) { printf("uso: sstvaire fichero.f32 [tasa_hz]\n"); return 2; }
    ruta = argv[1];
    if (argc > 2) { fs = (float)atof(argv[2]); }

    f = fopen(ruta, "rb");
    if (!f) { printf("no puedo abrir %s\n", ruta); return 2; }
    fseek(f, 0, SEEK_END);
    n_total = ftell(f) / 4L;
    fseek(f, 0, SEEK_SET);
    x = (float *)malloc((size_t)n_total * sizeof(float));
    if (!x || fread(x, sizeof(float), (size_t)n_total, f) != (size_t)n_total) {
        printf("no puedo leer %s\n", ruta); return 2;
    }
    fclose(f);

    printf("  %s: %ld muestras, %.1f s a %.0f Hz\n",
           ruta, n_total, (double)n_total / (double)fs, (double)fs);

    sstv_start(fs);

    for (i = 0UL; i + BLOQUE <= (uint32_t)n_total; i += BLOQUE) {
        sstv_process(&x[i], BLOQUE);
        t = (double)(i + BLOQUE) / (double)fs;

        /* Por donde va el detector de cabecera: solo cuando CAMBIA, que si
         * no son miles de renglones iguales. */
        sstv_vis_dbg(&est, &lider, &c1200, &hz);
        if (est != est_ant) {
            if (est < 8U) { pasos[est]++; }
            /* Solo los pasos que importan, y solo las primeras veces: el
             * V_LIDER1 <-> V_CORTE puede rebotar cientos de veces con ruido y
             * eso llena la pantalla sin decir nada nuevo. */
            if (est >= 2U || pasos[est] <= 3U) {
                printf("    %7.3f s  paso VIS %u -> %u   lider=%lu  c1200=%lu  "
                       "hz=%.0f  desvio=%+.0f\n",
                       t, (unsigned)est_ant, (unsigned)est,
                       (unsigned long)lider, (unsigned long)c1200,
                       (double)hz, (double)sstv_desvio_hz());
            }
            est_ant = est;
        }

        {   /* ventana de observacion fina, para mirar un tramo concreto */
            const char *v0 = getenv("SSTV_V0"), *v1 = getenv("SSTV_V1");
            if (v0 && v1 && t >= atof(v0) && t <= atof(v1)) {
                printf("      %7.4f s  hz=%7.1f  estVIS=%u  lider=%lu  c1200=%lu\n",
                       t, (double)hz, (unsigned)est,
                       (unsigned long)lider, (unsigned long)c1200);
            }
        }

        sstv_info(&in);
        if (in.imagenes != img_ant) {
            printf("    %7.3f s  *** IMAGEN %lu: VIS %u (%s), %u lineas, "
                   "desvio %+.0f Hz\n",
                   t, (unsigned long)in.imagenes, (unsigned)in.vis,
                   in.modo ? in.modo : "?", (unsigned)in.alto,
                   (double)sstv_desvio_hz());
            img_ant = in.imagenes;
        }

        n = 0UL;
        while ((l = sstv_linea(&y)) != 0) {
            (void)l; lineas++; if (y > y_max) { y_max = y; }
            if (++n > 64UL) { break; }
        }
    }

    sstv_info(&in);
    printf("  --------------------------------------------------\n");
    printf("  estado final    %u  (0 parado, 1 busca, 2 imagen)\n", (unsigned)in.estado);
    printf("  VIS             %u\n", (unsigned)in.vis);
    printf("  modo            %s\n", in.modo ? in.modo : "(ninguno)");
    printf("  imagenes        %lu\n", (unsigned long)in.imagenes);
    printf("  lineas sacadas  %lu   (la mas alta, y=%u)\n",
           (unsigned long)lineas, (unsigned)y_max);
    printf("  sincs malos     %u\n", (unsigned)in.sincs_malos);
    printf("  desvio medido   %+.0f Hz\n", (double)sstv_desvio_hz());
    printf("  nivel           %.3f\n", (double)in.nivel);
    printf("  veces en cada paso del VIS:  LIDER1 %lu  CORTE %lu  LIDER2 %lu  "
           "ARRANQUE %lu  BITS %lu\n",
           (unsigned long)pasos[0], (unsigned long)pasos[1],
           (unsigned long)pasos[2], (unsigned long)pasos[3],
           (unsigned long)pasos[4]);
    free(x);
    return 0;
}
