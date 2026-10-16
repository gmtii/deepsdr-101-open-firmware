/*
 * BANCO DEL ENGANCHE AL PICO - etapa 21, 22/09/2026.
 *
 * Compila el spec_snap.c de verdad, el mismo que se copia a User/, y le da
 * espectros inventados con señales en sitios conocidos. La pregunta que
 * responde es la unica que importa: si toco AQUI, ¿a que frecuencia caigo?
 *
 * Los casos no son bonitos a proposito: dedo torcido, dos señales pegadas,
 * ruido solo, señal debil, señal en el borde de la pantalla. Un enganche que
 * solo funciona apuntando bien no sirve en un tactil resistivo.
 */
#include "spec_snap.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define N      256
#define HZ_BIN 375.0f          /* 96 kHz / 256 bins */

static float g_db[N];
static int fallos = 0;

static unsigned s_rnd = 1u;
static float ruido(void)
{
    s_rnd = s_rnd * 1103515245u + 12345u;
    return ((float)((s_rnd >> 16) & 0x7FFF) / 32767.0f - 0.5f) * 6.0f;  /* +/-3 dB */
}

static void limpia(float suelo)
{
    int i;
    for (i = 0; i < N; i++) { g_db[i] = suelo + ruido(); }
}

/* Una señal gaussiana de `alto` dB sobre el suelo, centrada en `centro`
 * bins y de `ancho` bins de sigma. */
static void pon(float centro, float alto, float ancho)
{
    int i;
    for (i = 0; i < N; i++) {
        float d = ((float)i - centro) / ancho;
        g_db[i] += alto * expf(-0.5f * d * d);
    }
}

static void caso(const char *que, float tap, snap_modo_t modo,
                 float espera, float tol)
{
    uint8_t ok = 0U;
    float r = spec_snap_bin(g_db, N, tap, modo, &ok);
    float err_hz = (r - espera) * HZ_BIN;
    int bien = (fabsf(r - espera) <= tol);

    printf("  %-38s dedo %6.1f -> %6.2f  (%+5.0f Hz)  %s%s\n",
           que, tap, r, err_hz, ok ? "engancha" : "libre  ",
           bien ? "" : "   MAL");
    if (!bien) { fallos++; }
}

static void caso_libre(const char *que, float tap, snap_modo_t modo)
{
    uint8_t ok = 1U;
    float r = spec_snap_bin(g_db, N, tap, modo, &ok);
    int bien = (!ok && r == tap);
    printf("  %-38s dedo %6.1f -> %6.2f  %s%s\n", que, tap, r,
           ok ? "engancha" : "libre", bien ? "" : "   MAL (deberia quedarse libre)");
    if (!bien) { fallos++; }
}

int main(void)
{
    printf("enganche al pico: %d bins de %.0f Hz, ventana +/-%d bins (+/-%.0f Hz)\n",
           N, (double)HZ_BIN, SNAP_VENTANA_BINS, (double)(SNAP_VENTANA_BINS * HZ_BIN));

    /* 1. Portadora de AM clara, dedo torcido a un lado y a otro. */
    limpia(-100.0f); pon(128.0f, 40.0f, 1.2f);
    caso("portadora, dedo justo encima",        128.0f, SNAP_PICO, 128.0f, 0.15f);
    caso("portadora, dedo 4 bins a la derecha", 132.0f, SNAP_PICO, 128.0f, 0.15f);
    caso("portadora, dedo 5 bins a la izq.",    123.0f, SNAP_PICO, 128.0f, 0.15f);

    /*
     * 2. Tono de CW ENTRE bins: es donde el redondeo al bin entero mas duele,
     *    porque 187 Hz de error se oyen perfectamente.
     *
     *    Aqui no vale una sola prueba: el error depende del ruido que caiga
     *    en los tres bins de la parabola. Se repite con 32 ruidos distintos
     *    y se mira el error MEDIO y el PEOR, que es lo que de verdad se va a
     *    notar en el aire.
     */
    {
        const float sitios[] = {128.4f, 127.5f, 130.7f};
        int j;
        for (j = 0; j < 3; j++) {
            float suma = 0.0f, peor = 0.0f;
            int k;
            for (k = 0; k < 32; k++) {
                uint8_t ok;
                float r, e;
                limpia(-100.0f); pon(sitios[j], 35.0f, 0.9f);
                r = spec_snap_bin(g_db, N, sitios[j] + 2.0f, SNAP_PICO, &ok);
                e = fabsf(r - sitios[j]) * HZ_BIN;
                suma += e;
                if (e > peor) { peor = e; }
            }
            printf("  CW en el bin %6.1f   error medio %3.0f Hz, peor %3.0f Hz   %s\n",
                   (double)sitios[j], (double)(suma / 32.0f), (double)peor,
                   (peor < 150.0f) ? "" : "  MAL");
            if (peor >= 150.0f) { fallos++; }
        }
        printf("  (sin interpolar, caer al bin entero da hasta 187 Hz siempre)\n");
    }

    /* 3. Dos señales pegadas: tiene que coger la que esta bajo el dedo, no
     *    la mas fuerte de al lado. */
    limpia(-100.0f); pon(120.0f, 45.0f, 1.0f); pon(128.0f, 30.0f, 1.0f);
    caso("dos juntas, dedo sobre la debil",     128.0f, SNAP_PICO, 128.0f, 0.3f);
    caso("dos juntas, dedo sobre la fuerte",    120.0f, SNAP_PICO, 120.0f, 0.2f);

    /* 4. Banda lateral: el pico esta en medio de la voz y sintonizar ahi
     *    deja la mitad fuera del filtro. Hay que caer en el BORDE. */
    limpia(-100.0f);
    { int i; for (i = 120; i < 132; i++) { g_db[i] += 30.0f; } }
    caso("voz en USB, cae en el borde de abajo", 126.0f, SNAP_USB, 119.5f, 1.0f);
    caso("voz en LSB, cae en el borde de arriba",126.0f, SNAP_LSB, 131.5f, 1.0f);

    /* 5. Ruido solo: NO puede inventarse una señal. */
    limpia(-100.0f);
    caso_libre("ruido solo",                     128.0f, SNAP_PICO);

    /* 6. Señal que no llega al umbral: tampoco. */
    limpia(-100.0f); pon(128.0f, 5.0f, 1.2f);
    caso_libre("señal 5 dB, por debajo del umbral", 128.0f, SNAP_PICO);

    /* 7. En el borde de la pantalla, sin salirse del array. */
    limpia(-100.0f); pon(1.0f, 40.0f, 1.0f);
    caso("señal en el bin 1",                      2.0f, SNAP_PICO, 1.0f, 0.5f);
    limpia(-100.0f); pon((float)(N - 2), 40.0f, 1.0f);
    caso("señal en el ultimo bin",       (float)(N - 3), SNAP_PICO, (float)(N - 2), 0.5f);

    /* 8. Dedo fuera del array: no puede leer fuera ni reventar. */
    limpia(-100.0f); pon(128.0f, 40.0f, 1.2f);
    { uint8_t ok; (void)spec_snap_bin(g_db, N, -50.0f, SNAP_PICO, &ok);
      (void)spec_snap_bin(g_db, N, (float)(N + 50), SNAP_PICO, &ok);
      printf("  %-38s sin desbordar\n", "dedo fuera de la pantalla"); }

    printf("  %s\n", fallos ? "HAY FALLOS" : "engancha donde debe y no se inventa señales");
    return fallos ? 1 : 0;
}
