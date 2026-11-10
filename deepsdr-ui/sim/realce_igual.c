/*
 * QUE realce() SIGA DANDO EL MISMO BIT QUE ANTES DE ACELERARLA.
 *
 * POR QUE EXISTE - 08/11/2026.
 *
 * realce() de User/stanag_det.c era, con la banda vacia, el 79 % de lo que
 * costaba una ventana del detector: 6,56 millones de instrucciones de los
 * 8,27 de la ventana, repartidos en 564 llamadas. Se reescribio para que
 * no recorra las 413 casillas del entorno preguntandole a cada una a que
 * distancia esta -eso no depende de los datos, solo del indice- sino para
 * que calcule los bordes de los dos tramos una vez y los recorra seguidos.
 * La ventana paso de 8,27 a 3,61 millones.
 *
 * PERO ESTO NO ES UN ADORNO QUE SE PUEDA CAMBIAR A OJO. Lo que devuelve
 * realce() decide, con un umbral de 9 dB, si una raya del espectro es un
 * tono o es ruido; de ahi sale cuantas señales hay, cual es el salto de la
 * FSK y, al final, lo que la radio escribe en pantalla. Una decima de dB
 * de diferencia en el sitio justo cambia una deteccion.
 *
 * Asi que aqui esta la version de ANTES, copiada palabra por palabra, y se
 * exige que las dos den EL MISMO BIT -no un parecido, no una diferencia
 * menor que tanto- sobre espectros de todas las formas que se le ocurren a
 * uno: ruido plano, una raya sola, dos rayas, un escalon de ochenta dB,
 * ceros, y la frecuencia barriendo de punta a punta incluidos los bordes
 * donde el entorno se sale del espectro.
 *
 * Si algun dia alguien toca una de las dos, este banco se pone rojo y dice
 * con que frecuencia y sobre que espectro empezaron a separarse.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "stanag_det.c"

static float g_trabajo[STANAG_DET_FLOATS];

/*
 * LA VERSION DE ANTES, TAL CUAL ESTABA. No se toca ni para arreglarle el
 * estilo: si se toca, deja de ser la referencia.
 */
static float realce_viejo(float fc)
{
    float pico = 0.0f, suma = 0.0f;
    uint32_t n = 0U;
    int32_t k, k0, k1;
    /* +2 de holgura por el redondeo de los dos extremos. */
    const int32_t semi = (int32_t)(TONO_ENTORNO / HZ_CASILLA) + 2;

    k0 = K_DE_HZ(fc) - semi; if (k0 < K_MIN) { k0 = K_MIN; }
    k1 = K_DE_HZ(fc) + semi; if (k1 > K_MAX) { k1 = K_MAX; }

    for (k = k0; k <= k1; k++) {
        uint32_t i = I_DE_K(k);
        float f = casilla_a_audio(i);
        float d = (f > fc) ? (f - fc) : (fc - f);

        if (d <= TONO_ANCHO) {
            if (s_fft[2U*i] > pico) { pico = s_fft[2U*i]; }
        } else if (d > TONO_HUECO && d <= TONO_ENTORNO) {
            suma += s_fft[2U*i];
            n++;
        }
    }
    if (n == 0U || suma <= 0.0f || pico <= 0.0f) { return 0.0f; }
    return 10.0f * log10f(pico / (suma / (float)n));
}

/* Mismo bit, no "casi igual": se comparan los cuatro bytes. Asi dos NaN
 * con la misma carga tambien cuentan como iguales, que es lo que se quiere
 * -que las dos rutas hagan exactamente lo mismo- y no lo que diria ==. */
static int mismo_bit(float a, float b)
{
    return memcmp(&a, &b, sizeof a) == 0;
}

static uint32_t s_rnd = 20261108u;
static float az(void)
{
    s_rnd = s_rnd * 1664525u + 1013904223u;
    return (float)((s_rnd >> 8) & 0xFFFFFu) * (1.0f / 1048576.0f);
}

/* Las formas de espectro que se prueban. El indice decide cual. */
static const char *const k_forma[] = {
    "ruido plano",
    "ruido con una raya de 40 dB",
    "dos rayas juntas",
    "un escalon de 80 dB a mitad de banda",
    "todo ceros",
    "una sola casilla distinta de cero",
    "ruido con rango dinamico enorme"
};
#define N_FORMA ((int)(sizeof k_forma / sizeof k_forma[0]))

static void pinta(int forma)
{
    uint32_t i;

    for (i = 0U; i < NFFT; i++) {
        float v;
        switch (forma) {
        case 0:  v = az() + 0.01f; break;
        case 1:  v = az() + 0.01f; break;
        case 2:  v = az() + 0.01f; break;
        case 3:  v = (i < NFFT / 2U) ? (az() * 1e-4f) : (az() * 1e4f); break;
        case 4:  v = 0.0f; break;
        case 5:  v = 0.0f; break;
        default: v = az() * powf(10.0f, az() * 12.0f - 6.0f); break;
        }
        s_fft[2U*i]    = v;
        s_fft[2U*i+1U] = 0.0f;
    }
    if (forma == 1) { s_fft[2U*700U] = 1.0e4f; }
    if (forma == 2) { s_fft[2U*700U] = 1.0e4f; s_fft[2U*740U] = 8.0e3f; }
    if (forma == 5) { s_fft[2U*123U] = 1.0f; }
}

int main(void)
{
    int forma, fallos = 0, vueltas = 0;

    if (!stanag_det_init(g_trabajo, STANAG_DET_FLOATS)) {
        printf("  el detector no arranca: no comparo nada   MAL\n");
        return 1;
    }

    printf("realce(): la version de antes y la de ahora, bit a bit\n");

    for (forma = 0; forma < N_FORMA; forma++) {
        int rep;
        int malos = 0;
        float peor_fc = 0.0f, peor_a = 0.0f, peor_b = 0.0f;

        for (rep = 0; rep < 8; rep++) {
            int paso;

            pinta(forma);
            /* De 0 a 3100 Hz en pasos de 0,37 Hz: un cuarto de casilla, o
             * sea que cada borde se cruza cuatro veces. Y se sale por
             * arriba y por abajo a proposito, para pisar los recortes
             * contra K_MIN y K_MAX. */
            for (paso = 0; paso <= 8400; paso++) {
                float fc = (float)paso * 0.369140625f;
                float a, b;

                /* El orden importa: las dos leen s_fft[] y ninguna la
                 * escribe, pero que se note si algun dia una lo hace. */
                a = realce_viejo(fc);
                b = realce(fc);
                vueltas++;
                if (!mismo_bit(a, b)) {
                    if (malos == 0) { peor_fc = fc; peor_a = a; peor_b = b; }
                    malos++;
                }
            }
        }

        printf("  %-38s  %s\n", k_forma[forma],
               malos ? "MAL" : "igual");
        if (malos) {
            printf("      %d de %d frecuencias se separan\n", malos, 8401 * 8);
            printf("      la primera, a %.4f Hz: antes %.9g, ahora %.9g\n",
                   (double)peor_fc, (double)peor_a, (double)peor_b);
            fallos++;
        }
    }

    printf("  %d comparaciones\n", vueltas);
    if (fallos) {
        printf("\n  realce() decide con un umbral de 9 dB si una raya es un\n");
        printf("  tono. Si las dos versiones no dan el mismo bit, la nueva\n");
        printf("  no es la misma medida mas rapida: es otra medida.\n");
        printf("REALCE: HAY FALLOS (%d formas)\n", fallos);
        return 1;
    }
    printf("REALCE: todo correcto\n");
    return 0;
}
