#include "ft8_fft1024.h"
#include "ft8_shared_ram.h"

/* Real memory union with the main waterfall's own pixel buffer - see
 * ft8_shared_ram.h for the full "why" and the safety argument (these
 * arrays are never touched while the main waterfall is showing, and
 * vice versa). Storage itself is defined once, in waterfall.c -
 * these are just typed views into it via macros, so every line below
 * that already said s_re/s_im/etc. keeps working unchanged. */
#define s_re           (g_ft8_shared_ram.ft8.fft_re)
#define s_im           (g_ft8_shared_ram.ft8.fft_im)
/*
 * LAS CUATRO TABLAS VIVEN EN FLASH DESDE EL 10/10/2026, no aqui dentro.
 *
 * Estaban en g_ft8_shared_ram -la memoria prestada de la cascada- y
 * ft8_fft1024_init() las reconstruia en cada entrada a FT8 porque cada
 * salida las borraba. Son 10.240 bytes y son funcion pura de FT8_FFT_SIZE,
 * o sea unos numeros que no cambian nunca ocupando la memoria mas disputada
 * de la radio.
 *
 * Ahora las genera tools/gen_ft8_tablas.py en User/ft8_tablas.c, con LA
 * MISMA aproximacion de Bhaskara-I que habia aqui: comprobado bit a bit,
 * 0 diferencias en 3.072 valores (ver sim/ft8_tablas.c).
 *
 * Lo que se gana con esto NO es RAM directamente -la union la fija la
 * cascada, que es el miembro mayor- sino SITIO DENTRO de la union, y ahi
 * entraron los seis buffers que FT8 tenia fuera. Ver ft8_shared_ram.h.
 */
#define s_twiddle_cos  k_ft8_twiddle_cos
#define s_twiddle_sin  k_ft8_twiddle_sin
#define s_hann         k_ft8_hann
#define s_bitrev       k_ft8_bitrev

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
void ft8_fft1024_init(void)
{
}

static void fft_run(void)
{
    uint32_t i, j;
    uint32_t half_size, step;

    for (half_size = 1U, step = FT8_FFT_SIZE / 2U; half_size < FT8_FFT_SIZE;
         half_size <<= 1U, step >>= 1U) {
        for (i = 0; i < FT8_FFT_SIZE; i += (half_size << 1U)) {
            for (j = 0; j < half_size; j++) {
                uint32_t tw_idx = j * step;
                float tre = s_twiddle_cos[tw_idx];
                float tim = s_twiddle_sin[tw_idx];
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

void ft8_fft1024_compute_db(const int16_t *samples, float *db_out)
{
    uint32_t n;

    /*
     * AQUI SE MIRABA EL SELLO, Y YA NO HACE FALTA - 10/10/2026.
     *
     * Decia: "¿son tablas o son restos de la cascada?". s_bitrev se usa como
     * INDICE de s_hann y de samples, y cuando vivia en la memoria prestada
     * de la cascada contenia, sin construir, indices de paleta: s_hann[65535]
     * cae 256 kB fuera de una tabla de 4 kB, fallo de bus, radio colgada y un
     * pitido continuo. Paso de verdad el 25/09/2026, la primera vez que se
     * entro en FT8.
     *
     * Las tablas estan en flash: no se pueden borrar, asi que no hay nada
     * que comprobar. El cuelgue que este sello tapaba ya no existe, en vez
     * de estar tapado.
     */

    for (n = 0; n < FT8_FFT_SIZE; n++) {
        uint16_t src = s_bitrev[n];
        s_re[n] = (float)samples[src] * s_hann[src];
        s_im[n] = 0.0f;
    }

    fft_run();

    for (n = 0; n < FT8_FFT_BINS_USEFUL; n++) {
        float power = s_re[n] * s_re[n] + s_im[n] * s_im[n];
        db_out[n] = 3.0103f * log2_approx(power);
    }
}
