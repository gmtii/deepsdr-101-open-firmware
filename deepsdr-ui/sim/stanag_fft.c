/*
 * LA TRANSFORMADA DEL DETECTOR, BIT A BIT CONTRA LA DE ANTES - 21/10/2026.
 *
 * POR QUE EXISTE ESTE FICHERO.
 *
 * El 19/10/2026 se le dio la vuelta a los dos bucles de fft() en
 * User/stanag_det.c, igual que a la del espectro, y se saco la primera
 * etapa aparte porque "no lleva giro". El comentario que quedo alli decia:
 *
 *     "Las mariposas son las mismas, en el mismo orden dentro de cada una,
 *      asi que el resultado es el mismo bit a bit - y lo comprueban
 *      identtest, identgrab y stanagdet, que no se mueven ni un acierto."
 *
 * Las dos mitades de esa frase no encajan. Que tres bancos de decodificacion
 * no pierdan un acierto dice que el cambio no rompe nada util; NO dice que
 * el resultado sea el mismo bit a bit. Un acierto de STANAG aguanta
 * diferencias de muchos bits sin moverse. Es la misma trampa que se corrigio
 * en WEFAX: una asercion mas ancha que el efecto que vigila no mide nada.
 *
 * Asi que aqui se mide de verdad, con las mariposas viejas dentro y
 * comparando patrones de bits.
 *
 * Y LO QUE SALE NO ES LO QUE DECIA EL COMENTARIO. En la del espectro el
 * giro cero vale exactamente 1 y exactamente cero, asi que desenrollar la
 * primera etapa es gratis. Aqui NO:
 *
 *   - wr = s_cos[0] = cosf(0) = 1.0f exacto. Hasta aqui bien.
 *   - wi sale de la MISMA tabla, con el truco sen(x) = cos(x - pi/2):
 *     s_cos[|t - NFFT/4|]. Para t = 0 eso es s_cos[NFFT/4] = cosf(pi/2),
 *     y cosf(pi/2) en coma flotante NO es cero: es -4,371139e-08.
 *
 * O sea que la primera etapa vieja multiplicaba por un giro que no era
 * exactamente 1+0i, y la nueva se la salta. El resultado cambia. Poco
 * -abajo esta medido- pero cambia, y el comentario decia que no.
 *
 * Este banco mide ese "poco" y lo deja clavado con un tope. No exige
 * igualdad bit a bit, porque seria mentira; exige que la diferencia siga
 * siendo del orden del redondeo y no crezca si alguien vuelve a tocar la
 * transformada.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "stanag_det.c"   /* a proposito: fft() y s_cos son static */

static float g_trabajo[STANAG_DET_FLOATS];
static float g_nue[2U * NFFT];
static float g_vie[2U * NFFT];
static float g_ent[2U * NFFT];

/*
 * LAS MARIPOSAS COMO ESTABAN ANTES DEL 19/10/2026: `paso` empieza en 2 -la
 * primera etapa NO va aparte-, el bucle de los bloques por fuera y el del
 * giro por dentro, con el giro leido de la tabla en cada mariposa.
 *
 * El barajado de bits de arriba no cambio, asi que no se repite: se llama a
 * fft() para nada y se compara solo la parte de las mariposas seria mas
 * enredado que copiarlo. Se copia.
 */
static void fft_viejo(float *z, uint8_t inv)
{
    uint32_t n = NFFT, i, j, paso;

    j = 0U;
    for (i = 1U; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) { j ^= bit; }
        j ^= bit;
        if (i < j) {
            float tr = z[2U*i], ti = z[2U*i+1U];
            z[2U*i] = z[2U*j]; z[2U*i+1U] = z[2U*j+1U];
            z[2U*j] = tr;      z[2U*j+1U] = ti;
        }
    }

    for (paso = 2U; paso <= n; paso <<= 1) {
        uint32_t medio = paso >> 1;
        uint32_t salto = n / paso;
        uint32_t k;

        for (i = 0U; i < n; i += paso) {
            uint32_t t = 0U;
            for (k = 0U; k < medio; k++, t += salto) {
                uint32_t a = i + k, b = a + medio;
                float wr = s_cos[t];
                float ws = s_cos[(t >= (NFFT/4U)) ? (t - (NFFT/4U)) : ((NFFT/4U) - t)];
                float wi = inv ? ws : -ws;
                float xr = z[2U*b]*wr - z[2U*b+1U]*wi;
                float xi = z[2U*b]*wi + z[2U*b+1U]*wr;
                z[2U*b]    = z[2U*a]    - xr;
                z[2U*b+1U] = z[2U*a+1U] - xi;
                z[2U*a]    += xr;
                z[2U*a+1U] += xi;
            }
        }
    }
}

static uint32_t s_sem = 987654321U;
static float ruido(void)
{
    s_sem = s_sem * 1103515245U + 12345U;
    return (float)((int32_t)(s_sem >> 8) % 20001) / 10000.0f - 1.0f;
}

struct caso { const char *nom; void (*llena)(float *z); };

static void c_cero(float *z)   { uint32_t n; for (n=0;n<2U*NFFT;n++) z[n]=0.0f; }
static void c_continua(float *z){ uint32_t n; for (n=0;n<2U*NFFT;n++) z[n]=(n&1U)?-0.5f:0.5f; }
static void c_impulso(float *z){ uint32_t n; for (n=0;n<2U*NFFT;n++) z[n]=0.0f; z[0]=1.0f; z[513]=-1.0f; }
static void c_tono(float *z)
{
    uint32_t n;
    for (n = 0; n < NFFT; n++) {
        double w = 2.0 * 3.14159265358979 * 37.0 * (double)n / (double)NFFT;
        z[2U*n] = (float)cos(w); z[2U*n+1U] = (float)sin(w);
    }
}
static void c_ruido(float *z)
{
    uint32_t n; s_sem = 987654321U;
    for (n = 0; n < 2U*NFFT; n++) { z[n] = ruido(); }
}
static void c_rellena_medio(float *z)
{
    /* como la usa el detector: la mitad con señal, la otra con ceros */
    uint32_t n;
    for (n = 0; n < NFFT; n++) {
        if (n < STANAG_DET_N) {
            double w = 2.0 * 3.14159265358979 * 123.0 * (double)n / (double)STANAG_DET_N;
            z[2U*n] = (float)(0.7 * cos(w)); z[2U*n+1U] = (float)(0.7 * sin(w));
        } else { z[2U*n] = 0.0f; z[2U*n+1U] = 0.0f; }
    }
}

static const struct caso casos[] = {
    { "ceros",            c_cero          },
    { "continua",         c_continua      },
    { "impulso",          c_impulso       },
    { "tono en el bin 37",c_tono          },
    { "ruido",            c_ruido         },
    { "medio relleno",    c_rellena_medio },
};
#define NCASOS ((int)(sizeof casos / sizeof casos[0]))

/*
 * El tope. Lo medido el 21/10/2026 va al lado. Es error RELATIVO contra el
 * mayor valor de la salida, que es lo unico que tiene sentido comparar en
 * una transformada: el bin grande manda sobre el ruido de redondeo.
 */
#define TOPE_REL 1.0e-5

int main(void)
{
    int malos = 0, c, inv;
    double peor_global = 0.0;

    printf("\nLA TRANSFORMADA DEL DETECTOR, CONTRA LA DE ANTES (NFFT=%u)\n\n",
           (unsigned)NFFT);

    if (!stanag_det_init(g_trabajo, STANAG_DET_FLOATS)) {
        printf("  *** MAL: stanag_det_init() no arranca ***\n");
        return 1;
    }

    /* 1. Los dos numeros de los que depende todo lo demas. */
    {
        float wr = s_cos[0];
        float ws = s_cos[NFFT / 4U];
        uint32_t br, bs;
        memcpy(&br, &wr, 4); memcpy(&bs, &ws, 4);
        printf("  el giro cero: cos 0x%08X (%.9g)   sen 0x%08X (%.9g)\n",
               br, (double)wr, bs, (double)ws);
        if (wr != 1.0f) {
            printf("    *** MAL: el coseno del giro cero no es 1 exacto ***\n");
            malos++;
        }
        if (ws == 0.0f) {
            printf("    el seno SI es cero exacto: desenrollar la primera\n");
            printf("    etapa seria gratis y este banco sobra  (revisar)\n");
        } else {
            printf("    el seno NO es cero (%.3e): desenrollar la primera\n",
                   (double)ws);
            printf("    etapa CAMBIA el resultado. Por eso aqui no se exige\n");
            printf("    igualdad bit a bit sino un tope de redondeo.\n");
        }
    }

    /* 2. Cuanto cambia, caso por caso, ida y vuelta. */
    for (inv = 0; inv <= 1; inv++) {
        printf("\n  %s:\n", inv ? "inversa" : "directa");
        for (c = 0; c < NCASOS; c++) {
            uint32_t n, iguales = 1U;
            double peor = 0.0, mx = 0.0;

            casos[c].llena(g_ent);

            memcpy(g_nue, g_ent, sizeof g_nue);
            fft(g_nue, (uint8_t)inv);

            memcpy(g_vie, g_ent, sizeof g_vie);
            fft_viejo(g_vie, (uint8_t)inv);

            for (n = 0; n < 2U * NFFT; n++) {
                uint32_t ia, ib;
                double d;
                memcpy(&ia, &g_nue[n], 4); memcpy(&ib, &g_vie[n], 4);
                if (ia != ib) { iguales = 0U; }
                d = fabs((double)g_nue[n] - (double)g_vie[n]);
                if (d > peor) { peor = d; }
                if (fabs((double)g_vie[n]) > mx) { mx = fabs((double)g_vie[n]); }
            }
            if (mx > 0.0) { peor /= mx; }
            if (peor > peor_global) { peor_global = peor; }

            printf("    %-20s %s   peor diferencia relativa %.3e",
                   casos[c].nom, iguales ? "iguales bit a bit" : "NO son iguales ",
                   peor);
            if (peor > TOPE_REL) { printf("   *** MAL ***\n"); malos++; }
            else { printf("   ok\n"); }
        }
    }

    printf("\n  la peor de todas: %.3e  (tope %.1e)\n", peor_global, TOPE_REL);

    if (malos) {
        printf("\n  HAY FALLOS (%d)\n", malos);
        printf("  Ver la cabecera de este fichero y la de fft() en User/stanag_det.c.\n");
        return 1;
    }
    printf("\n  TODO CORRECTO\n");
    return 0;
}
