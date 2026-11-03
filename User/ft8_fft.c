#include "ft8_fft.h"
#include "ft8_shared_ram.h"

/* Real memory union with the main waterfall's own pixel buffer - see
 * ft8_shared_ram.h for the full "why" and the safety argument (these
 * arrays are never touched while the main waterfall is showing, and
 * vice versa). Storage itself is defined once, in waterfall.c -
 * these are just typed views into it via macros, so every line below
 * that already said s_re/s_im/etc. keeps working unchanged. */
#ifdef FT8_FFT_COMPLEJA
/* La version vieja, la compleja de 2.048 puntos, SOLO existe para que
 * sim/ft8_fftreal.c pueda comparar. Pide el doble de sitio y ya no lo hay
 * en la union, asi que se lo pone ella: ese binario es un banco de
 * anfitrion y no entra en la radio. */
static float s_re_c[FT8_FFT_SIZE];
static float s_im_c[FT8_FFT_SIZE];
#define s_re           s_re_c
#define s_im           s_im_c
#else
#define s_re           (g_ft8_shared_ram.ft8.fft_re)
#define s_im           (g_ft8_shared_ram.ft8.fft_im)
#endif
/*
 * LAS CUATRO TABLAS VIVEN EN FLASH DESDE EL 10/10/2026, no aqui dentro.
 *
 * Estaban en g_ft8_shared_ram -la memoria prestada de la cascada- y
 * ft8_fft_init() las reconstruia en cada entrada a FT8 porque cada
 * salida las borraba. Son 10.240 bytes y son funcion pura de FT8_FFT_SIZE,
 * o sea unos numeros que no cambian nunca ocupando la memoria mas disputada
 * de la radio.
 *
 * Ahora las genera tools/gen_ft8_tablas.py en User/ft8_tablas.c, con LA
 * MISMA aproximacion de Bhaskara-I que habia aqui: comprobado bit a bit,
 * 0 diferencias en 2.048 valores (ver sim/ft8_tablas.c).
 *
 * Y desde el 26/10/2026 son TRES: la del orden de bits invertido se
 * calcula con `rbit`, una instruccion de un ciclo, y eran 2.048 bytes mas
 * de flash interna. Ver ft8_bitrev() en ft8_fft.h.
 *
 * Lo que se gana con esto NO es RAM directamente -la union la fija la
 * cascada, que es el miembro mayor- sino SITIO DENTRO de la union, y ahi
 * entraron los seis buffers que FT8 tenia fuera. Ver ft8_shared_ram.h.
 */
/*
 * ======================================================================
 * LA VENTANA DE HANN SE CALCULA, YA NO SE GUARDA - 28/10/2026
 * ======================================================================
 *
 * *** POR QUE: hacen falta bytes de flash, y estos son los mas baratos
 * del arbol. ***
 *
 * Eran 1.024 floats -4.096 bytes- de una funcion que no depende de nada:
 * w[n] = 0,5 - 0,5*cos(2*pi*n/(N-1)). Guardarlos tiene sentido cuando
 * calcularlos cuesta; aqui no cuesta.
 *
 * LO QUE CUESTA, MEDIDO y no supuesto: una transformada pasa de 17,97 a
 * 22,72 microsegundos en el PC del simulador, o sea **+4,75 us, un 26%**.
 * Y se hace UNA TRANSFORMADA CADA 160 ms. Aunque el Cortex-M4 fuera
 * treinta veces mas lento que ese PC, serian 142 us de cada 160.000:
 * **nueve centesimas de uno por ciento de la CPU**.
 *
 * Y SALE BIT A BIT LO MISMO, que es lo que permite hacerlo sin volver a
 * medir la sensibilidad: la tabla la generaba tools/gen_ft8_tablas.py con
 * ESTA MISMA aproximacion de Bhaskara-I en float de 32 bits, paso a paso,
 * precisamente para que coincidiera con la que habia aqui dentro. Al
 * quitarla se comprobaron las 1.024 entradas una por una: **cero
 * distintas**. No es "casi igual", es la misma.
 *
 * (Las tablas de giro NO se tocan y siguen en flash: esas si dependen del
 * tamano de la FFT de una forma que no se reduce a una linea, y ademas se
 * leen dos veces por mariposa, no una vez por muestra.)
 *
 * EL INDICE VIENE DEL ORDEN DE BITS INVERTIDO, o sea que las muestras no
 * se visitan en orden. Por eso esto es una funcion de `n` y no una
 * recurrencia del coseno, que seria mas barata pero necesitaria ir
 * seguido.
 */
#define FT8_PI 3.14159265358979323846f

/*
 * Bhaskara-I, la misma que uso ft8_fft_init() mientras las tablas se
 * construian aqui dentro. No se sustituye por sinf() de la biblioteca a
 * proposito: sinf() daria OTROS numeros -mejores, pero otros- y entonces
 * esto dejaria de ser un cambio sin consecuencias para pasar a ser uno que
 * hay que volver a medir entero.
 */
static float sinf_aprox(float x)
{
    float num, den, x2;
    int neg = 0;

    while (x >  FT8_PI) { x -= 2.0f * FT8_PI; }
    while (x < -FT8_PI) { x += 2.0f * FT8_PI; }
    if (x < 0.0f) { x = -x; neg = 1; }
    num = (16.0f * x) * (FT8_PI - x);
    den = (5.0f * (FT8_PI * FT8_PI)) - ((4.0f * x) * (FT8_PI - x));
    x2 = num / den;
    return neg ? -x2 : x2;
}

float ft8_fft_hann(uint32_t n)
{
    float ang = ((2.0f * FT8_PI) * (float)n) / (float)(FT8_FFT_SIZE - 1U);

    return 0.5f - 0.5f * sinf_aprox(ang + (FT8_PI / 2.0f));
}

/* El radix-2 de aqui abajo -y ft8_bitrev()- dan por hecho que el tamaño es
 * potencia de dos. Si deja de serlo, esto no compila en vez de reordenar
 * mal en silencio. Los bits que hay que invertir los deduce FT8_FFT_BITS en
 * la cabecera, que es donde se cambia el tamaño. */
_Static_assert((FT8_FFT_SIZE & (FT8_FFT_SIZE - 1U)) == 0U,
               "FT8_FFT_SIZE tiene que ser potencia de dos");
_Static_assert((1U << FT8_FFT_BITS) == FT8_FFT_SIZE,
               "FT8_FFT_BITS no cuadra con FT8_FFT_SIZE");

/*
 * AQUI ESTABAN LAS DOS APROXIMACIONES DE SENO Y COSENO (Bhaskara-I).
 *
 * Solo las usaban las cuatro tablas, que desde el 10/10/2026 viven en flash
 * (User/ft8_tablas.c). tools/gen_ft8_tablas.py las reproduce en Python,
 * operacion a operacion y redondeando a float de 32 bits en cada paso, para
 * que los numeros salgan IGUALES BIT A BIT: comprobado, 0 diferencias en
 * 3.072 valores. Ver sim/ft8_tablas.c, que es quien lo comprueba en cada
 * compilacion.
 */

static float log2_approx(float x)
{
    union { float f; uint32_t i; } vx;
    float y;
    if (x <= 0.0f) {
        x = 1.0e-9f;
    }
    vx.f = x;
    y = (float)vx.i;
    y *= 1.1920929e-7f;
    return y - 126.94269504f;
}

/*
 * *** AQUI SE CONSTRUIAN LAS CUATRO TABLAS, Y YA NO HACE FALTA. ***
 *
 * Desde el 10/10/2026 viven en flash (User/ft8_tablas.c). La funcion SE
 * QUEDA, vacia, por dos razones que no son la misma:
 *
 *  1. la llama ft8_modo_start() y no tiene sentido obligar a tocar ese
 *     fichero por esto;
 *  2. y sobre todo, si algun dia vuelve a hacer falta inicializar algo de
 *     la FFT, el sitio ya esta y con su historia encima.
 *
 * Con ella se va el SELLO, que era lo que de verdad costaba: fft_sello
 * existia porque estas tablas vivian en memoria que la cascada pisaba, y
 * sin construir fft_bitrev contenia indices de paleta QUE SE USABAN COMO
 * INDICE -s_hann[65535], 256 kB fuera de una tabla de 4 kB-. La radio se
 * colgaba con un pitido continuo la primera vez que se entraba en FT8. En
 * flash eso no puede pasar: nadie puede borrarlas.
 */
void ft8_fft_init(void)
{
}

/*
 * ======================================================================
 * LA ENTRADA ES REAL, ASI QUE LA MITAD DE LA TRANSFORMADA SOBRABA
 * 02/11/2026
 * ======================================================================
 *
 * Esto hacia una transformada COMPLEJA de 2.048 puntos con la parte
 * imaginaria puesta a cero, y luego tiraba la mitad de la salida. Dos
 * desperdicios del mismo sitio:
 *
 *   - 16.384 bytes de la memoria mas disputada de la radio (re e im de
 *     2.048 floats cada uno) para guardar algo que es simetrico;
 *   - y el doble de mariposas de las que hacen falta.
 *
 * LO QUE SE HACE AHORA es el reparto de siempre para entrada real: las
 * muestras pares van a la parte real y las impares a la imaginaria de una
 * transformada de 1.024 puntos, y al final se desenreda:
 *
 *     Xe[k] = (Z[k] + conj(Z[M-k])) / 2
 *     Xo[k] = (Z[k] - conj(Z[M-k])) / 2i
 *     X[k]  = Xe[k] + e^(-i2pi k/N) * Xo[k]
 *
 * con N = 2.048 y M = 1.024. El giro de fuera sale de la MISMA tabla de
 * cuarto de vuelta que ya habia -k_ft8_cuarto, en flash- porque esta
 * hecha para N y aqui el indice es k, no 2k.
 *
 * QUE SE GANA: 8.192 bytes, que es lo que hacia falta para empezar a
 * pagar el submuestreo en frecuencia (ver FT8_ADAPTER_FREQ_OSR), y
 * aproximadamente la mitad del tiempo de la FFT, que tambien importa:
 * mientras se decodifica una ranura la siguiente se esta escribiendo en
 * el desvio, y lo que no cabe ahi se pierde.
 *
 * QUE NO CAMBIA: la salida. Lo vigila sim/ft8_fftreal.c, que corre las
 * dos contra las mismas muestras y compara bin a bin.
 */
#define FT8_FFT_M  (FT8_FFT_SIZE / 2U)   /* los puntos que de verdad se transforman */

/* Invertir los bits que hacen falta para M, que son uno menos que los de
 * FT8_FFT_SIZE. Misma instruccion `rbit`, otro desplazamiento. */
static inline uint32_t ft8_bitrev_m(uint32_t i)
{
    return ft8_bitrev(i << 1U);
}

static void fft_run_m(void)
{
    uint32_t i, j;
    uint32_t half_size, step;

    for (half_size = 1U, step = FT8_FFT_SIZE / 2U; half_size < FT8_FFT_M;
         half_size <<= 1U, step >>= 1U) {
        for (i = 0; i < FT8_FFT_M; i += (half_size << 1U)) {
            for (j = 0; j < half_size; j++) {
                uint32_t tw_idx = j * step;
                float tre = ft8_cos_giro(tw_idx);
                float tim = ft8_sen_giro(tw_idx);
                uint32_t a = i + j;
                uint32_t b = a + half_size;
                float br = s_re[b] * tre - s_im[b] * tim;
                float bi = s_re[b] * tim + s_im[b] * tre;
                s_re[b] = s_re[a] - br;
                s_im[b] = s_im[a] - bi;
                s_re[a] = s_re[a] + br;
                s_im[a] = s_im[a] + bi;
            }
        }
    }
}

#ifdef FT8_FFT_COMPLEJA
static void fft_run(void)
{
    uint32_t i, j;
    uint32_t half_size, step;

    for (half_size = 1U, step = FT8_FFT_SIZE / 2U; half_size < FT8_FFT_SIZE;
         half_size <<= 1U, step >>= 1U) {
        for (i = 0; i < FT8_FFT_SIZE; i += (half_size << 1U)) {
            for (j = 0; j < half_size; j++) {
                uint32_t tw_idx = j * step;
                float tre = ft8_cos_giro(tw_idx);
                float tim = ft8_sen_giro(tw_idx);
                uint32_t a = i + j;
                uint32_t b = a + half_size;
                float br = s_re[b] * tre - s_im[b] * tim;
                float bi = s_re[b] * tim + s_im[b] * tre;
                s_re[b] = s_re[a] - br;
                s_im[b] = s_im[a] - bi;
                s_re[a] = s_re[a] + br;
                s_im[a] = s_im[a] + bi;
            }
        }
    }
}
#endif /* FT8_FFT_COMPLEJA */

/*
 * SOLO LOS BINS QUE SE VAN A LEER - 28/10/2026.
 *
 * *** POR QUE: con la FFT en 2.048 puntos la salida son 1.024 bins
 * utiles, y el adaptador lee UNO DE CADA DOS (ver
 * FT8_ADAPTER_RAW_BIN_STEP). Guardar los 1.024 costaba 4.096 bytes de la
 * memoria mas disputada de la radio para tirar la mitad, y ademas pagaba
 * 1.024 logaritmos por subbloque en vez de 512. ***
 *
 * `base` es el primer bin crudo que interesa, `paso` cada cuantos y
 * `n_out` cuantos. El que llama guarda n_out valores, no 1.024.
 */
/*
 * De decibelios a cuartos de decibelio, con tope. Ver FT8_FFT_DB_ESCALA:
 * el tope no se alcanza con la formula de la potencia, pero un valor raro
 * -una entrada a cero da logaritmo de cero- no puede salirse del entero.
 */
static inline int16_t ft8_fft_db_entero(float db)
{
    float v = db * FT8_FFT_DB_ESCALA;

    if (v >  32767.0f) { return  32767; }
    if (v < -32768.0f) { return -32768; }
    return (int16_t)(v + (v >= 0.0f ? 0.5f : -0.5f));
}

void ft8_fft_compute_db_rango(const int16_t *samples, int16_t *db_out,
                              uint32_t base, uint32_t paso, uint32_t n_out)
{
    uint32_t n;

    /*
     * AQUI SE MIRABA EL SELLO, Y YA NO HACE FALTA - 10/10/2026.
     *
     * Decia: ¿son tablas o son restos de la cascada? El orden de bits se
     * usaba como INDICE de s_hann y de samples, y cuando vivia en la memoria
     * prestada de la cascada contenia, sin construir, indices de paleta:
     * s_hann[65535] cae 256 kB fuera de una tabla de 4 kB, fallo de bus,
     * radio colgada y un pitido continuo. Paso de verdad el 25/09/2026, la
     * primera vez que se entro en FT8.
     *
     * Las tablas estan en flash: no se pueden borrar, asi que no hay nada
     * que comprobar. El cuelgue que este sello tapaba ya no existe, en vez
     * de estar tapado.
     */

#ifdef FT8_FFT_COMPLEJA
    for (n = 0; n < FT8_FFT_SIZE; n++) {
        uint16_t src = ft8_bitrev(n);   /* `rbit`: antes era una tabla de 2 kB */
        s_re[n] = (float)samples[src] * ft8_fft_hann(src);
        s_im[n] = 0.0f;
    }

    fft_run();

    for (n = 0; n < n_out; n++) {
        uint32_t k = base + (n * paso);
        float power = s_re[k] * s_re[k] + s_im[k] * s_im[k];
        db_out[n] = ft8_fft_db_entero(3.0103f * log2_approx(power));
    }
#else
    /* Las pares a la parte real y las impares a la imaginaria, ya en el
     * orden invertido que pide el radix-2. La ventana de Hann se aplica
     * con el indice de la MUESTRA, no con el de la pareja. */
    for (n = 0; n < FT8_FFT_M; n++) {
        uint32_t par = ft8_bitrev_m(n) << 1U;
        s_re[n] = (float)samples[par]      * ft8_fft_hann(par);
        s_im[n] = (float)samples[par + 1U] * ft8_fft_hann(par + 1U);
    }

    fft_run_m();

    for (n = 0; n < n_out; n++) {
        uint32_t k  = base + (n * paso);
        uint32_t kr = (FT8_FFT_M - k) & (FT8_FFT_M - 1U);
        float er =  0.5f * (s_re[k] + s_re[kr]);
        float ei =  0.5f * (s_im[k] - s_im[kr]);
        float orr =  0.5f * (s_im[k] + s_im[kr]);
        float oi = -0.5f * (s_re[k] - s_re[kr]);
        float c  = ft8_cos_giro(k);
        float sg = ft8_sen_giro(k);
        float xr = er + (c * orr - sg * oi);
        float xi = ei + (c * oi  + sg * orr);
        float power = xr * xr + xi * xi;

        db_out[n] = ft8_fft_db_entero(3.0103f * log2_approx(power));
    }
#endif
}

/* La de siempre: todos los bins utiles, de uno en uno. La usan los bancos
 * y cualquiera que quiera el espectro entero. */
void ft8_fft_compute_db(const int16_t *samples, int16_t *db_out)
{
    ft8_fft_compute_db_rango(samples, db_out, 0U, 1U, FT8_FFT_BINS_USEFUL);
}
