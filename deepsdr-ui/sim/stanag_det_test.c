/*
 * Banco del detector de tramas (User/stanag_det.c del firmware).
 *
 * LO QUE SE MIDE AQUI, Y LO QUE NO
 * --------------------------------
 * NO se mide si sabemos demodular STANAG: eso vendra despues y necesita
 * las tablas de la norma. Se mide una cosa mucho mas concreta y que no
 * depende de ninguna tabla: SI LE PONEMOS DELANTE UNA SEÑAL QUE REPITE SU
 * ESTRUCTURA CADA X MILISEGUNDOS, ¿DICE X?
 *
 * Y el banco escribe la regla aparte: la periodicidad la elige el, en
 * simbolos, y la convierte a muestras por su cuenta. No le pregunta al
 * firmware cual deberia ser el retardo; lo calcula y compara.
 *
 * LA SEÑAL DE PRUEBA NO ES UN 4285 DE VERDAD, y es importante decirlo: su
 * preambulo son 80 simbolos fijos inventados, no los de la norma. Para
 * esto da igual -lo que crea el pico de autocorrelacion es que el
 * preambulo se REPITA, no cual sea- y asi este banco no depende de datos
 * que todavia no tengo confirmados. El dia que esten, el demodulador
 * tendra su propio banco con los de verdad.
 */
#include "stanag_det.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FS        12000U
#define SPS       5U        /* 12000 / 2400 baudios */
#define PORTADORA 1800.0f

static int fallos = 0;
static void di(const char *que, int ok)
{
    printf("  %-56s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

/* Ruido gaussiano barato y repetible: suma de doce uniformes menos seis.
 * Repetible a proposito - un banco que da un resultado distinto cada vez
 * no sirve para decidir nada. */
static unsigned long s_sem = 1234567u;
static float uni(void)
{
    s_sem = s_sem * 1103515245u + 12345u;
    return (float)((s_sem >> 16) & 0x7FFFu) / 32767.0f;
}
static float gauss(void)
{
    int i; float s = 0.0f;
    for (i = 0; i < 12; i++) { s += uni(); }
    return s - 6.0f;
}

/*
 * Una señal con trama: `pre` simbolos SIEMPRE IGUALES seguidos de `dat`
 * simbolos al azar, repetido. Eso es lo que hace el 4285 y lo que hace que
 * tenga un pico de autocorrelacion donde lo tiene.
 */
static void genera(float *x, uint32_t n, uint32_t pre, uint32_t dat, float sigma)
{
    uint32_t i, k = 0U;
    float fase = 0.0f;
    static uint8_t preamb[512];
    uint32_t trama = pre + dat;

    /*
     * El preambulo, SIN PERIODICIDAD PROPIA. Aqui ponia i*5%8, que se
     * repite cada ocho simbolos, y eso metia su propio pico en la
     * autocorrelacion: el banco daba 325 donde esperaba 320 y la culpa era
     * del banco, no del detector -se vio poniendole impulsos cada 320
     * muestras exactas, que los clava-. Un preambulo de verdad es una
     * secuencia pseudoaleatoria larga, asi que aqui tambien.
     */
    {
        unsigned long guarda = s_sem;
        s_sem = 0xACE1u;                     /* semilla fija: el banco repite */
        for (i = 0U; i < pre && i < 512U; i++) {
            preamb[i] = (uint8_t)(uni() * 7.99f);
        }
        s_sem = guarda;
    }

    while (k < n) {
        uint32_t s;
        for (s = 0U; s < trama && k < n; s++) {
            uint8_t simb = (s < pre) ? preamb[s] : (uint8_t)(((uint32_t)(uni()*8.0f)) & 7U);
            float ph = (2.0f * 3.14159265f * (float)simb) / 8.0f;
            uint32_t m;
            for (m = 0U; m < SPS && k < n; m++) {
                float t = (float)k / (float)FS;
                x[k] = cosf(2.0f*3.14159265f*PORTADORA*t + ph) + sigma * gauss();
                k++;
            }
        }
        (void)fase;
    }
}

/* Mete la señal por trozos y devuelve la ultima medida que haya salido. */
static stanag_det_t corre(const float *x, uint32_t n)
{
    stanag_det_t r, ult;
    uint32_t i;

    memset(&ult, 0, sizeof ult);
    /*
     * Desde el 02/10/2026 stanag_det_paso() devuelve 1 en CADA ventana
     * procesada, tenga trama o no -lo necesita la radio para saber que
     * esto esta vivo y repintar-. Asi que aqui hay que quedarse con la
     * ultima que ademas VALGA, no con la ultima a secas.
     */
    for (i = 0U; i + 64U <= n; i += 64U) {
        stanag_det_mete(&x[i], 64U);
        if (stanag_det_paso(&r) && r.vale) { ult = r; }
    }
    return ult;
}

int main(void)
{
    static float trabajo[STANAG_DET_FLOATS];
    static float x[FS * 4U];
    stanag_det_t r;

    printf("\n=== El detector de tramas por autocorrelacion ===\n\n");

    printf("  --- una trama de 256 simbolos a 2400 baudios (lo del 4285)\n");
    {
        /* La cuenta del banco, hecha aparte: 256 simbolos a 2400 baudios
         * son 106,667 ms, y a 3 kHz de muestreo eso son 320 muestras. */
        uint32_t simbolos = 256U;
        float ms = (float)simbolos * 1000.0f / 2400.0f;
        uint32_t esperado = (uint32_t)((ms * (float)STANAG_DET_HZ / 1000.0f) + 0.5f);

        printf("      %u simbolos = %.3f ms = retardo %u a %u Hz\n",
               (unsigned)simbolos, ms, (unsigned)esperado, (unsigned)STANAG_DET_HZ);

        di("el init coge la memoria", stanag_det_init(trabajo, STANAG_DET_FLOATS) == 1U);
        genera(x, FS * 4U, 80U, 176U, 0.0f);
        r = corre(x, FS * 4U);
        di("encuentra una trama", r.vale == 1U);
        di("y cae donde dice la cuenta del banco",
           r.vale && (r.retardo >= esperado - 1U) && (r.retardo <= esperado + 1U));
        printf("      dice retardo %u, periodo %u,%02u ms, %u picos, calidad %u\n",
               (unsigned)r.retardo, (unsigned)(r.periodo_us/100U),
               (unsigned)(r.periodo_us%100U), (unsigned)r.picos, (unsigned)r.calidad);
        di("y ve mas de un multiplo, no un pico suelto", r.picos >= 2U);
    }

    printf("\n  --- la MITAD de velocidad: la misma trama a 1200 baudios\n");
    {
        /* Se simula alargando la trama al doble de simbolos, que a 2400
         * baudios dura lo mismo que 256 a 1200. Lo que se comprueba es que
         * el detector no tiene ningun numero metido a fuego. */
        uint32_t simbolos = 512U;
        float ms = (float)simbolos * 1000.0f / 2400.0f;
        uint32_t esperado = (uint32_t)((ms * (float)STANAG_DET_HZ / 1000.0f) + 0.5f);

        stanag_det_init(trabajo, STANAG_DET_FLOATS);
        genera(x, FS * 4U, 160U, 352U, 0.0f);
        r = corre(x, FS * 4U);
        di("tambien la encuentra", r.vale == 1U);
        di("y en el retardo que toca, no en el de antes",
           r.vale && (r.retardo >= esperado - 2U) && (r.retardo <= esperado + 2U));
        printf("      dice retardo %u y el banco esperaba %u\n",
               (unsigned)r.retardo, (unsigned)esperado);
    }

    printf("\n  --- con ruido encima\n");
    {
        uint32_t esperado = 320U;
        stanag_det_init(trabajo, STANAG_DET_FLOATS);
        genera(x, FS * 4U, 80U, 176U, 1.0f);   /* ruido del orden de la señal */
        r = corre(x, FS * 4U);
        di("sigue encontrandola con el ruido a la par",
           r.vale && (r.retardo >= esperado - 2U) && (r.retardo <= esperado + 2U));
        printf("      retardo %u, calidad %u\n",
               (unsigned)r.retardo, (unsigned)r.calidad);
    }

    printf("\n  --- y lo que NO tiene que encontrar\n");
    {
        uint32_t i;
        stanag_det_init(trabajo, STANAG_DET_FLOATS);
        for (i = 0U; i < (FS * 4U); i++) { x[i] = gauss(); }
        r = corre(x, FS * 4U);
        di("con ruido solo, no se inventa una trama", r.vale == 0U);
        if (r.vale) {
            printf("      se ha inventado el retardo %u con calidad %u\n",
                   (unsigned)r.retardo, (unsigned)r.calidad);
        }
    }

    {
        /* Una portadora sola NO tiene trama. Es el caso que mas facil
         * engaña a una autocorrelacion: correlaciona consigo misma a TODOS
         * los retardos. */
        uint32_t i;
        stanag_det_init(trabajo, STANAG_DET_FLOATS);
        for (i = 0U; i < (FS * 4U); i++) {
            x[i] = cosf(2.0f*3.14159265f*PORTADORA*((float)i/(float)FS));
        }
        r = corre(x, FS * 4U);
        di("y con una portadora sola, tampoco", r.vale == 0U);
        if (r.vale) {
            printf("      se ha inventado el retardo %u con calidad %u\n",
                   (unsigned)r.retardo, (unsigned)r.calidad);
        }
    }

    printf("\n  %s\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO");
    return fallos ? 1 : 0;
}
