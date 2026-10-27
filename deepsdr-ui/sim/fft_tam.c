/*
 * LA FFT DE 256 PUNTOS SACADA DE LAS TABLAS DE 512, MIRADA DE FRENTE.
 *
 * POR QUE EXISTE - 24/10/2026
 * ---------------------------
 * El dia anterior se puso la transformada del panadaptador en 512 PARA
 * TODOS, para devolver los 375 Hz por bin a 192 kHz. A 96 y a 48 kHz el
 * bloque de la captura son 256 muestras, asi que habia que juntar dos
 * bloques por ventana, y eso dejo la radio inservible: el tick del espectro
 * volvia a la mitad de las pasadas sin llegar a la puerta que decide si
 * toca dibujar -la cadencia de cuadros, partida por dos-, y los dos bloques
 * ni siquiera eran seguidos, asi que la ventana llevaba un salto de fase en
 * medio. El usuario aviso de que el espectro iba muy lento y de que la
 * radio tardaba mucho en arrancar.
 *
 * La reparacion es que la ventana sea UN BLOQUE y la transformada se adapte
 * a el: 512 puntos a 192 kHz y 256 a 96 y a 48. Y para no pagar un segundo
 * juego de tablas -que es justo la memoria que acababa de costar trabajo
 * encontrar-, las de 512 se usan para 256:
 *
 *   - los giros, tal cual: el indice de la columna sale del tamaño de la
 *     TABLA y no del de la transformada;
 *   - la ventana de Hann, las muestras pares;
 *   - la tabla de inversion de bits, desplazada uno.
 *
 * Eso son tres afirmaciones sobre indices, que es exactamente el tipo de
 * cosa que parece evidente en el papel y sale al reves en el codigo. Esto
 * las comprueba.
 *
 * CONTRA QUE SE COMPARA, Y POR QUE ASI
 * ------------------------------------
 * Contra User/fft.c OTRA VEZ, compilado nativo a 256 puntos con sus propias
 * tablas (ver sim/fft_ref256.c). No contra una transformada escrita aqui:
 * una reimplementacion puede equivocarse en lo mismo, y ademas obligaria a
 * copiar la aproximacion del seno y la del logaritmo, que son las dos
 * fuentes de error que ensucian cualquier comparacion con coma flotante.
 *
 * LO UNICO QUE NO SALE IGUAL, dicho antes de que alguien lo descubra
 * leyendo un numero raro: la VENTANA. Hann nativa de 256 es
 * 0,5 - 0,5*cos(2*pi*k/255), y la que sale de coger las pares de la de 512
 * es 0,5 - 0,5*cos(2*pi*k/255,5). Son ventanas distintas, del orden de una
 * milesima de diferencia, asi que la comparacion es con tolerancia y no
 * patron de bits. La tolerancia se exige sobre los bins que tienen señal;
 * sesenta decibelios por debajo del pico lo que se compara es ruido de
 * redondeo contra ruido de redondeo y eso no dice nada.
 */
#include "fft.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* El otro juego, compilado nativo a 256. Ver sim/fft_ref256.c. */
void ref_fft_init(void);
void ref_fft_compute_db_iq_ex(const int16_t *i, const int16_t *q,
                              float *db, uint8_t crudo);
void ref_fft_potencias(const float *m, float *pot);

static int fallos = 0;

static void di(const char *que, int bien, const char *detalle)
{
    printf("    %-54s %s%s%s\n", que, bien ? "bien" : "*** MAL ***",
           detalle ? "  " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

/* Una escena de I/Q: un tono en cada lado del oscilador y nada mas. Las
 * amplitudes son distintas para que el espejo no se confunda con el bueno
 * si algun dia los indices salen al reves. */
static void escena(int16_t *i, int16_t *q, int n,
                   double f1, double a1, double f2, double a2)
{
    int k;
    for (k = 0; k < n; k++) {
        double t = (double)k;
        double re = a1 * cos(2.0 * M_PI * f1 * t / (double)n)
                  + a2 * cos(2.0 * M_PI * f2 * t / (double)n);
        double im = a1 * sin(2.0 * M_PI * f1 * t / (double)n)
                  + a2 * sin(2.0 * M_PI * f2 * t / (double)n);
        i[k] = (int16_t)re;
        q[k] = (int16_t)im;
    }
}

static int pico(const float *db, int n)
{
    int k, mejor = 0;
    for (k = 1; k < n; k++) { if (db[k] > db[mejor]) { mejor = k; } }
    return mejor;
}

int main(void)
{
    char t[160];
    static int16_t ri[512], rq[512];
    static float a[512], b[512];
    int k;

    printf("\n== La FFT de 256 sacada de las tablas de 512 ==\n");

    fft_init();
    ref_fft_init();

    /*
     * 1. EL TAMAÑO POR DEFECTO NO CAMBIA NADA. Antes de tocar nada, la
     *    transformada del panadaptador tiene que seguir siendo la de 512.
     */
    printf("\n  de serie sigue siendo la de siempre\n");
    snprintf(t, sizeof t, "%u puntos", (unsigned)fft_tam_iq_actual());
    di("arranca en FFT_SIZE", fft_tam_iq_actual() == FFT_SIZE, t);

    /*
     * 2. LA GUARDA. Lo que no sea potencia de dos dentro del rango se
     *    ignora y se sigue con el de antes: es preferible seguir con el
     *    tamaño bueno que transformar con uno imposible.
     */
    printf("\n  lo que no vale se ignora, no se obedece\n");
    {
        static const uint32_t malos[] = { 0U, 1U, 3U, 300U, 1024U, 32U, 513U };
        unsigned m;
        int ok = 1;

        fft_tam_iq(FFT_SIZE);
        for (m = 0; m < sizeof malos / sizeof malos[0]; m++) {
            fft_tam_iq(malos[m]);
            if (fft_tam_iq_actual() != FFT_SIZE) { ok = 0; }
        }
        di("ni cero, ni impar, ni mayor que la tabla, ni menor que 64", ok, 0);

        fft_tam_iq(256U);
        snprintf(t, sizeof t, "%u puntos", (unsigned)fft_tam_iq_actual());
        di("y 256 si se acepta", fft_tam_iq_actual() == 256U, t);
        fft_tam_iq(128U);
        di("y 128 tambien", fft_tam_iq_actual() == 128U, 0);
        fft_tam_iq(64U);
        di("y 64, que es el suelo", fft_tam_iq_actual() == 64U, 0);
    }

    /*
     * 3. EL NUCLEO: 256 puntos con las tablas de 512, contra 256 puntos con
     *    las suyas. Misma entrada, mismo fichero, distintas tablas.
     */
    printf("\n  256 puntos: tablas de 512 contra tablas de 256\n");
    {
        /* Bins 40 y 203 de 256: uno a cada lado del oscilador, y ninguno
         * de los dos en el centro ni en el borde. */
        escena(ri, rq, 256, 40.0, 9000.0, 203.0, 2500.0);

        fft_tam_iq(256U);
        fft_compute_db_iq_ex(ri, rq, a, 0U);
        ref_fft_compute_db_iq_ex(ri, rq, b, 0U);

        {
            int pa = pico(a, 256), pb = pico(b, 256);
            snprintf(t, sizeof t, "el pico en %d y %d", pa, pb);
            di("el pico cae en el mismo bin", pa == pb, t);
            /* 40 bins por encima de la continua, y el array sale
             * desplazado con el oscilador en el centro: 128 + 40. */
            snprintf(t, sizeof t, "bin %d, esperado %d", pa, 128 + 40);
            di("y es el bin del tono fuerte", pa == 128 + 40, t);
        }
        {
            /* 203 de 256 es -53, o sea 128 - 53 = 75 en el array
             * desplazado. Que el flojo salga donde toca es lo que
             * distingue "los indices estan bien" de "hay un pico". */
            int bueno = (a[75] > a[74]) && (a[75] > a[76]);
            snprintf(t, sizeof t, "%.1f dB contra %.1f y %.1f",
                     (double)a[75], (double)a[74], (double)a[76]);
            di("el tono flojo esta en SU bin, al otro lado", bueno, t);
        }
        {
            float tope = -1e30f, peor = 0.0f, alto = -1e30f;
            int peor_k = -1, alto_k = -1;

            for (k = 0; k < 256; k++) { if (a[k] > tope) { tope = a[k]; } }

            /* DONDE HAY SEÑAL -cuarenta decibelios por debajo del pico
             * arriba- las dos tienen que coincidir de verdad. */
            for (k = 0; k < 256; k++) {
                float d;
                if (a[k] < tope - 40.0f || b[k] < tope - 40.0f) { continue; }
                d = a[k] - b[k];
                if (d < 0.0f) { d = -d; }
                if (d > peor) { peor = d; peor_k = k; }
            }
            snprintf(t, sizeof t, "%.3f dB en el bin %d (pico %.1f dB)",
                     (double)peor, peor_k, (double)tope);
            di("las dos salidas coinciden donde hay señal", peor < 0.5f, t);

            /* Y LA DIFERENCIA DE LAS VENTANAS, ACOTADA. Las dos Hann no son
             * la misma, asi que las faldas de fuga SI se separan. Lo que
             * hay que exigir no es que no se separen, sino que eso pase
             * siempre muy por debajo: si algun dia un indice sale mal, la
             * discrepancia aparecera arriba y esta linea se pondra roja. */
            for (k = 0; k < 256; k++) {
                float d = a[k] - b[k];
                float m = (a[k] > b[k]) ? a[k] : b[k];
                if (d < 0.0f) { d = -d; }
                if (d > 0.5f && m > alto) { alto = m; alto_k = k; }
            }
            snprintf(t, sizeof t, "la mas alta, %.1f dB bajo el pico (bin %d)",
                     (double)(tope - alto), alto_k);
            di("y donde no, es siempre falda de fuga", alto < tope - 50.0f, t);
        }
    }

    /*
     * 4. Y QUE EL EJE NO SE HAYA ENCOGIDO. Un barrido de tonos: cada uno
     *    tiene que caer en su bin, no solo el de la prueba de arriba.
     */
    printf("\n  el eje entero, con 256 puntos\n");
    {
        static const int bins[5] = { 8, 37, 64, 100, 127 };
        int m, ok = 1, falla = -1;

        fft_tam_iq(256U);
        for (m = 0; m < 5; m++) {
            int esperado = 128 + bins[m];
            escena(ri, rq, 256, (double)bins[m], 9000.0, 0.0, 0.0);
            fft_compute_db_iq_ex(ri, rq, a, 0U);
            if (pico(a, 256) != esperado) { ok = 0; falla = bins[m]; }
        }
        snprintf(t, sizeof t, "falla el bin %d", falla);
        di("cinco tonos, cinco bins", ok, ok ? 0 : t);
    }

    /*
     * 5. Y QUE A 512 TODO SIGA COMO ESTABA. El camino de 192 kHz es el que
     *    motivo el cambio de tamaño; no puede haberse movido.
     */
    printf("\n  y a 512 puntos, como siempre\n");
    {
        fft_tam_iq(FFT_SIZE);
        escena(ri, rq, 512, 70.0, 9000.0, 0.0, 0.0);
        fft_compute_db_iq_ex(ri, rq, a, 0U);
        snprintf(t, sizeof t, "bin %d, esperado %d", pico(a, 512), 256 + 70);
        di("el pico cae donde dice el eje", pico(a, 512) == 256 + 70, t);
    }

    /*
     * 6. EL OSCILOSCOPIO DE LOS NUEVE MODOS NO SE ENTERA. fft_potencias()
     *    es de FFT_SIZE pase lo que pase, porque su eje de frecuencias esta
     *    escrito en rtty_scope.h. Si alguien hiciera que siguiera al
     *    panadaptador, RTTY, CW, NAVTEX, WEFAX, SSTV, FT8, PSK31, DSC y
     *    APRS se irian de frecuencia a la vez y en silencio.
     */
    printf("\n  y el osciloscopio sigue siendo de %u, pase lo que pase\n",
           (unsigned)FFT_SIZE);
    {
        static float m[512], p1[256], p2[256];
        int pa, pb;

        for (k = 0; k < (int)FFT_SIZE; k++) {
            m[k] = (float)(8000.0 * sin(2.0 * M_PI * 64.0
                                        * (double)k / (double)FFT_SIZE));
        }
        fft_tam_iq(FFT_SIZE);
        fft_potencias(m, p1);
        fft_tam_iq(256U);          /* cambia el panadaptador, no el otro */
        fft_potencias(m, p2);

        pa = 0; pb = 0;
        for (k = 1; k < (int)FFT_BINS_USEFUL; k++) {
            if (p1[k] > p1[pa]) { pa = k; }
            if (p2[k] > p2[pb]) { pb = k; }
        }
        snprintf(t, sizeof t, "bin %d, esperado 64", pa);
        di("el tono sale en su bin", pa == 64, t);
        snprintf(t, sizeof t, "antes %d, despues %d", pa, pb);
        di("y no se mueve al cambiar el del panadaptador", pa == pb, t);
        di("ni cambia una sola potencia",
           memcmp(p1, p2, sizeof p1) == 0, 0);
    }

    fft_tam_iq(FFT_SIZE);

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
