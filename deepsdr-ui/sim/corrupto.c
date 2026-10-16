/*
 * LA REGLA DEL BLOQUE CORRUPTO, MEDIDA. 10/10/2026.
 *
 * La interrupcion del DMA mira cada bloque de I/Q y, si "parece basura", lo
 * descarta. Descartar no da un error: deja la mitad del buffer de salida sin
 * refrescar y se REPITEN 2,67 ms de audio ya sonado. Una vez es un clic
 * inaudible; a ritmo de bloque son 375 por segundo, y eso suena a siseo.
 *
 * Ya se equivoco una vez asi. El 05/10, en WFM: la I/Q va sin mezclar a
 * 192 kHz, un tono a 75 kHz de desviacion da saltos de 1,88 veces la
 * amplitud entre muestras consecutivas, y con el umbral en 16000 bastaba una
 * amplitud del 26 % del fondo de escala para que CADA muestra lo cruzara. Se
 * arreglo apagando el chequeo en WFM, y el comentario que lo justifico dice:
 *
 *     "Para AM/SSB se queda encendido: alli la senal SI va mezclada a banda
 *      base y es estrecha comparada con la frecuencia de muestreo."
 *
 * ESO ULTIMO HAY QUE COMPROBARLO, Y ES LO QUE HACE ESTE BANCO. Porque la
 * mezcla a banda base -nco_mezcla()- ocurre DENTRO de demod_am_process_raw(),
 * o sea DESPUES de este chequeo. En la interrupcion la I/Q de AM/SSB esta
 * tan sin mezclar como la de WFM; lo unico que cambia es a que distancia del
 * centro queda la senal, y eso lo decide el aparcado del oscilador:
 *
 *     NCO_OFF_MIN_HZ   =  6.000 Hz
 *     NCO_OFF_MAX_96K  = 40.000 Hz
 *
 * La cuenta es la misma que la de WFM. Un tono a f de la frecuencia de
 * muestreo fs da |delta| = 2*A*sin(pi*f/fs) entre muestras consecutivas:
 *
 *      f       factor    A que dispara    % del fondo de escala
 *    6 kHz      0,390        41.000        no cabe en un int16
 *   12 kHz      0,765        20.915        64 %
 *   20 kHz      1,236        12.945        40 %
 *   30 kHz      1,732         9.238        28 %
 *   40 kHz      1,932         8.281        25 %
 *
 * O sea que a partir de unos 20 kHz de desplazamiento, una senal fuerte y
 * perfectamente limpia cruza el umbral en TODAS las muestras, y basta con
 * que lo crucen el 5 % para que el bloque se tire.
 *
 * Esto no demuestra que pase en antena -para eso hace falta la radio- pero
 * si demuestra que la regla NO MIDE LO QUE DICE MEDIR fuera de un margen
 * estrecho alrededor del centro. Los numeros de abajo son los que hay que
 * mirar antes de decidir que se hace.
 *
 *   make corrupto FIRMWARE=...
 */
#include "sdr_rx.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#define FS96   96000.0
#define N      256U

static int fallos;

static void di(const char *q, int ok, const char *det)
{
    printf("    %-54s %s%s%s\n", q, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

/* Un tono limpio a `off` Hz del centro, amplitud `amp`, I/Q entrelazada:
 * exactamente lo que le llega a la interrupcion en AM/SSB con el oscilador
 * aparcado a esa distancia. */
static void tono(int16_t *dst, uint32_t n, double off, double amp, double fs)
{
    uint32_t k;
    for (k = 0U; k < n; k++) {
        double ph = 2.0 * 3.14159265358979 * off * (double)k / fs;
        dst[2U * k]      = (int16_t)(amp * cos(ph));
        dst[2U * k + 1U] = (int16_t)(amp * sin(ph));
    }
}

/* Lo que la regla deberia cazar: muestras a saltos, como un ADC railado o
 * una I/Q descolocada. */
static void basura(int16_t *dst, uint32_t n, unsigned sem)
{
    uint32_t k;
    for (k = 0U; k < 2U * n; k++) {
        sem = sem * 1103515245u + 12345u;
        dst[k] = (int16_t)((int32_t)((sem >> 16) & 0xFFFFu) - 32768);
    }
}


/*
 * ==========================================================================
 * EL CANDIDATO QUE CASI VALE, Y POR QUE NO SE HA METIDO
 * ==========================================================================
 *
 * La regla de arriba mira CADA CARRIL por separado, y el carril de una senal
 * desplazada salta por mucho que la senal sea perfecta: el fasor gira. Lo
 * que NO gira es la ENVOLVENTE. Un tono limpio tiene I^2+Q^2 constante a
 * cualquier distancia del centro, y la basura no.
 *
 * Asi que se probo: salto de la potencia instantanea mayor que el 50 % del
 * maximo del bloque, en mas de una muestra de cada veinte. Y hace lo que la
 * de arriba prometia:
 *
 *   - pasa TODOS los tonos limpios, a las siete distancias y las cuatro
 *     amplitudes de la tabla;
 *   - caza la basura al azar;
 *   - y caza la I/Q descolocada UN SITIO, que es la averia que el FERR
 *     delataba y la que justifico que este chequeo existiera.
 *
 * PERO TIRA EL RUIDO SOLO. Medido, con ruido de receptor y sin senal:
 *
 *     amplitud   500   ->  TIRADO
 *     amplitud  2000   ->  pasa
 *     amplitud  8000   ->  TIRADO
 *     amplitud 25000   ->  TIRADO
 *
 * Y tiene que ser asi, porque a nivel de muestra el ruido del receptor y la
 * corrupcion SON LA MISMA COSA: valores independientes uno de otro. Lo unico
 * que los separa es de donde vienen, y eso no se ve aqui.
 *
 * Tirar los bloques de una frecuencia vacia es repetir audio todo el rato,
 * que es exactamente la enfermedad que se esta curando. Asi que el candidato
 * se queda AQUI, con sus numeros, y no en el firmware. Esta escrito para que
 * el que vuelva no repita la medida.
 */
static int env_candidato(const int16_t *iq, uint32_t n, int pc)
{
    uint32_t k, malas = 0U;
    int64_t pmax = 0, prev;

    if ((iq == 0) || (n < 2U)) { return 0; }
    for (k = 0U; k < n; k++) {
        int64_t p = (int64_t)iq[2U*k] * iq[2U*k] + (int64_t)iq[2U*k+1U] * iq[2U*k+1U];
        if (p > pmax) { pmax = p; }
    }
    if (pmax == 0) { return 0; }
    prev = (int64_t)iq[0] * iq[0] + (int64_t)iq[1] * iq[1];
    for (k = 1U; k < n; k++) {
        int64_t p = (int64_t)iq[2U*k] * iq[2U*k] + (int64_t)iq[2U*k+1U] * iq[2U*k+1U];
        int64_t d = p - prev;
        if (d < 0) { d = -d; }
        if (d * 100 > pmax * (int64_t)pc) { malas++; }
        prev = p;
    }
    return (malas * 20U > n) ? 1 : 0;
}

/* Ruido de receptor: lo que suena en una frecuencia vacia. */
static void ruido(int16_t *dst, uint32_t n, double amp, unsigned sem)
{
    uint32_t k;
    for (k = 0U; k < 2U * n; k++) {
        double u = 0.0; int q;
        for (q = 0; q < 4; q++) {
            sem = sem * 1103515245u + 12345u;
            u += ((double)((sem >> 16) & 0xFFFFu) / 65535.0 - 0.5);
        }
        dst[k] = (int16_t)(amp * u / 2.0);
    }
}

/* Y la averia de verdad: la I/Q descolocada un sitio sobre un tono limpio. */
static void descolocada(int16_t *dst, uint32_t n, double off, double amp)
{
    uint32_t k;
    tono(dst, n, off, amp, FS96);
    for (k = 1U; k < n; k++) {
        int16_t t = dst[2U*k+1U];
        dst[2U*k+1U] = dst[2U*(k-1U)+1U];
        dst[2U*(k-1U)+1U] = t;
    }
}

int main(void)
{
    static int16_t b[2U * N];
    double offs[] = { 0.0, 3000.0, 6000.0, 12000.0, 20000.0, 30000.0, 40000.0 };
    double amps[] = { 2000.0, 8000.0, 16000.0, 30000.0 };
    unsigned i, j;

    printf("\nLA REGLA DEL BLOQUE CORRUPTO\n");
    printf("============================\n\n");
    printf("  umbral de salto %d, y basta con el %u/%u de las muestras\n\n",
           SDR_RX_BLOCK_JUMP_THRESHOLD,
           (unsigned)SDR_RX_BLOCK_BAD_FRACTION_NUM,
           (unsigned)SDR_RX_BLOCK_BAD_FRACTION_DEN);

    printf("  UNA SENAL LIMPIA A 96 kHz, SEGUN DONDE QUEDE EL OSCILADOR\n");
    printf("  (el aparcado la deja entre 6.000 y 40.000 Hz del centro)\n\n");
    printf("      %-10s", "amplitud");
    for (i = 0U; i < sizeof offs / sizeof offs[0]; i++) {
        printf(" %7.0f", offs[i]);
    }
    printf("\n");

    for (j = 0U; j < sizeof amps / sizeof amps[0]; j++) {
        printf("      %-10.0f", amps[j]);
        for (i = 0U; i < sizeof offs / sizeof offs[0]; i++) {
            tono(b, N, offs[i], amps[j], FS96);
            printf(" %7s", sdr_rx_bloque_corrupto(b, N) ? "TIRADO" : "pasa");
        }
        printf("\n");
    }
    printf("\n");

    /*
     * LO QUE SE EXIGE. Y se exige poco a proposito: lo que este banco viene
     * a hacer hoy es ENSENAR LA TABLA DE ARRIBA, no congelar un
     * comportamiento que probablemente haya que cambiar. Se fija solo lo
     * que no admite discusion.
     */
    printf("  LO QUE NO ADMITE DISCUSION\n");

    /* 1. Basura es basura. Si esto fallara, la regla no sirve para nada. */
    basura(b, N, 1u);
    di("un bloque de muestras al azar se tira", sdr_rx_bloque_corrupto(b, N), 0);
    basura(b, N, 999u);
    di("y con otra semilla, tambien", sdr_rx_bloque_corrupto(b, N), 0);

    /* 2. Una senal centrada y limpia pasa, por fuerte que sea. Ahi el
     *    chequeo hace justo lo que promete. */
    tono(b, N, 0.0, 32000.0, FS96);
    di("un tono centrado a fondo de escala pasa", !sdr_rx_bloque_corrupto(b, N), 0);
    tono(b, N, 3000.0, 32000.0, FS96);
    di("y a 3 kHz del centro, tambien", !sdr_rx_bloque_corrupto(b, N), 0);

    /* 3. Los bordes, que son donde se cuelan los fallos tontos. */
    di("un bloque de una sola muestra no se tira", !sdr_rx_bloque_corrupto(b, 1U), 0);
    di("ni un bloque vacio", !sdr_rx_bloque_corrupto(b, 0U), 0);
    di("ni un puntero nulo", !sdr_rx_bloque_corrupto(0, N), 0);

    /*
     * 4. Y LA QUE DUELE: a la distancia MINIMA del aparcado -6 kHz, la mas
     *    favorable de todas las que usa la radio- una senal limpia tiene que
     *    pasar a cualquier amplitud. Si esto se pone rojo algun dia, es que
     *    alguien ha bajado el umbral sin hacer la cuenta.
     */
    tono(b, N, 6000.0, 32000.0, FS96);
    di("a 6 kHz (el aparcado mas cercano) a fondo de escala pasa",
       !sdr_rx_bloque_corrupto(b, N), 0);

    printf("\n  EL CANDIDATO DE LA ENVOLVENTE, PARA EL QUE VUELVA\n\n");
    printf("      %-10s", "amplitud");
    for (i = 0U; i < sizeof offs / sizeof offs[0]; i++) { printf(" %7.0f", offs[i]); }
    printf("\n");
    for (j = 0U; j < sizeof amps / sizeof amps[0]; j++) {
        printf("      %-10.0f", amps[j]);
        for (i = 0U; i < sizeof offs / sizeof offs[0]; i++) {
            tono(b, N, offs[i], amps[j], FS96);
            printf(" %7s", env_candidato(b, N, 50) ? "TIRADO" : "pasa");
        }
        printf("\n");
    }
    basura(b, N, 1u);
    printf("      basura al azar              -> %s\n",
           env_candidato(b, N, 50) ? "TIRADO (bien)" : "pasa (MAL)");
    descolocada(b, N, 20000.0, 16000.0);
    printf("      I/Q descolocada un sitio    -> %s\n",
           env_candidato(b, N, 50) ? "TIRADO (bien)" : "pasa (MAL)");
    { double ra[] = { 500.0, 2000.0, 8000.0, 25000.0 }; unsigned z;
      for (z = 0U; z < 4U; z++) {
          ruido(b, N, ra[z], 7u + z);
          printf("      ruido solo, amplitud %5.0f  -> %s\n", ra[z],
                 env_candidato(b, N, 50) ? "TIRADO (y por eso no entra)" : "pasa");
      } }

    /* Y una comprobacion sobre el candidato, que si se mete algun dia tiene
     * que seguir cumpliendo lo que le hizo candidato. */
    tono(b, N, 40000.0, 30000.0, FS96);
    di("el candidato deja pasar un tono limpio a 40 kHz",
       !env_candidato(b, N, 50), 0);
    descolocada(b, N, 20000.0, 16000.0);
    di("y caza la I/Q descolocada un sitio", env_candidato(b, N, 50), 0);

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}
