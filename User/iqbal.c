/* Ver iqbal.h para el porque. Aqui solo esta el como. */
#include "iqbal.h"
/*
 * Para que la prueba A/B apague y encienda LOS DOS correctores. Si solo
 * tocara el plano, la prueba compararia "sin plano pero con el de banda
 * ancha" contra "con los dos", que no es la pregunta que se esta haciendo y
 * ademas daria una diferencia pequeña siempre.
 */
/*
 * ESO ERA VERDAD HASTA LA V3.95 - revision a fondo del 08/10/2026.
 *
 * Desde que el corrector de banda ancha salio de la interrupcion del ADC
 * -costaba nueve veces y media lo que el plano y la radio perdia
 * pulsaciones; ver el comentario largo de sdr_rx.c, el del 08/10/2026-,
 * iqa_bloque() NO LO LLAMA NADIE. El codigo de iqancho.c sigue compilado y
 * su banco sigue pasando, pero ninguna muestra pasa ya por el.
 *
 * O sea que los iqa_set_on() de aqui abajo no cambian nada en la señal: lo
 * que encienden y apagan es un corrector que no esta en el camino. Se dejan
 * puestos A PROPOSITO, no por descuido: el dia que el ancho vuelva al camino
 * -a mano, o cuando haya sitio- la prueba A/B tiene que seguir comparando
 * los dos correctores contra ninguno, y descubrir entonces que la prueba se
 * habia quedado midiendo solo medio sistema seria una averia mucho mas cara
 * de encontrar que estas cuatro llamadas que hoy no hacen nada.
 *
 * Lo que hoy mide la prueba A/B, por tanto, es el corrector plano y solo el.
 * Que es ademas el que arregla el espejo de verdad.
 */
#include "iqancho.h"
#include <math.h>

/*
 * Constante de tiempo del promedio. A 96 kHz con bloques de 256 muestras son
 * 375 bloques por segundo; con 1/512 el promedio tarda algo mas de un
 * segundo en llegar y varios en asentarse del todo.
 *
 * Lenta a proposito. El desequilibrio del hardware no cambia con el tiempo,
 * cambia con la FRECUENCIA -el reparto del clon de reloj no es el mismo en
 * 3,3 MHz que en 14-, o sea que solo se mueve cuando el usuario mueve el dial.
 * Lo unico rapido que hay aqui es la señal, y la señal es justo de lo que
 * esto NO se tiene que enterar.
 */
#define IQB_ALFA   (1.0f / 512.0f)

/* Debajo de esto el promedio es ruido de cuantizacion del ADC y no hay nada
 * que medir: con la entrada muerta, E[I*Q] vale cero por casualidad y theta
 * saldria de dividir dos ceros. */
#define IQB_POT_MIN   100.0f

/*
 * EL ESTADO VIVE EN EL TCM, NO EN LA SRAM - 07/10/2026.
 *
 * Son veintiocho bytes y la SRAM estaba llena: al meter este fichero, el
 * enlazado se quedo corto por exactamente esa cantidad. El TCM tiene sitio,
 * es la RAM rapida del nucleo -que viene bien, porque de estas variables se
 * lee una vez por muestra dentro de la interrupcion del DMA- y su unica
 * limitacion es que el DMA no llega ahi. Aqui no hace falta que llegue: lo
 * que el DMA escribe es el buffer de muestras, que sigue donde estaba.
 *
 * Ver el comentario de la seccion .tcmram en GD32F450VE_FLASH.ld.
 */
#ifdef __GNUC__
#define TCMRAM_BSS __attribute__((section(".tcmram")))
#else
#define TCMRAM_BSS
#endif

/*
 * s_on ES VOLATIL - revision a fondo del 08/10/2026. Ver el bloque de las
 * s_ab_* de mas abajo: la prueba A/B lo escribe desde el bucle principal
 * (iqbal_ab_lanza(), iqbal_ab_paso()) y TAMBIEN desde la interrupcion del
 * DMA (iqbal_bloque() -> ab_aborta() cuando vence el plazo), y ademas lo
 * lee la propia interrupcion una vez por bloque para decidir si corrige.
 * Sin volatil el compilador puede guardarselo en un registro a los dos
 * lados y perderse lo que escribio el otro.
 */
static volatile uint8_t s_on  TCMRAM_BSS;
static uint8_t  s_hay         TCMRAM_BSS;  /* ya hay una estimacion creible */
static float    s_ii TCMRAM_BSS, s_qq TCMRAM_BSS, s_iq TCMRAM_BSS;
static float    s_theta TCMRAM_BSS, s_g TCMRAM_BSS;
static uint32_t s_bloques     TCMRAM_BSS;
static uint8_t  s_turno       TCMRAM_BSS;
static float    s_esp_db      TCMRAM_BSS;
static int16_t  s_esp_bin     TCMRAM_BSS;

#define IQB_AB_ESPERA  20U
#define IQB_AB_MIDE    25U

#define AB_PARADA      0U
#define AB_OFF_ESPERA  1U
#define AB_OFF_MIDE    2U
#define AB_ON_ESPERA   3U
#define AB_ON_MIDE     4U

/*
 * TODAS VOLATILES - revision a fondo del 08/10/2026.
 *
 * Este puñado de variables lo escriben DOS SITIOS A LA VEZ: el bucle
 * principal, desde iqbal_ab_lanza() e iqbal_ab_paso(), y la interrupcion
 * del DMA, que desde iqbal_bloque() llama a ab_aborta() cuando vence el
 * plazo. Hasta hoy ninguna era volatil, o sea que el compilador tenia
 * permiso para leerlas una vez, trabajar en registros y escribirlas al
 * final: cualquier cosa que hubiese hecho la interrupcion por el camino
 * se perdia sin dejar rastro, y la que mas duele es s_on, que es la que
 * decide si el corrector de espejo esta puesto.
 *
 * Volatil no es un cerrojo y no pretende serlo -para eso haria falta
 * apagar la interrupcion, y este fichero se compila tal cual en el banco
 * del ordenador, donde no hay interrupciones que apagar-. Lo que hace es
 * obligar a que cada lectura y cada escritura vaya de verdad a memoria,
 * que es lo que convierte "el bucle principal no se entera nunca" en "el
 * bucle principal se entera salvo en una ventana de dos instrucciones".
 */
static volatile uint8_t  s_ab_est     TCMRAM_BSS;
static volatile uint8_t  s_ab_n       TCMRAM_BSS;
static volatile uint8_t  s_ab_antes   TCMRAM_BSS;  /* como estaba el corrector */
static volatile uint8_t  s_ab_hay     TCMRAM_BSS;
static volatile uint8_t  s_ab_c_off   TCMRAM_BSS;
static volatile uint8_t  s_ab_c_on    TCMRAM_BSS;
static volatile float    s_ab_s_off   TCMRAM_BSS;
static volatile float    s_ab_s_on    TCMRAM_BSS;
static volatile float    s_ab_off     TCMRAM_BSS;
static volatile float    s_ab_on      TCMRAM_BSS;
/*
 * El plazo, en bloques de audio. 375 por segundo, asi que 2.500 son algo mas
 * de seis segundos y medio: la prueba entera son noventa cuadros, unos tres,
 * o sea el doble de margen. Se cuenta en la interrupcion del DMA, que es lo
 * unico de esta radio que no se salta nunca. Ver iqbal.h.
 */
#define IQB_AB_PLAZO  2500U
/* Volatil por lo mismo que las de arriba: la cuenta la lleva la
 * interrupcion y el bucle principal la pone a cero. */
static volatile uint16_t s_ab_plazo   TCMRAM_BSS;

static void ab_aborta(void);


void iqbal_init(void)
{
    s_on      = 1U;
    s_hay     = 0U;
    s_ii      = 0.0f;
    s_qq      = 0.0f;
    s_iq      = 0.0f;
    s_theta   = 0.0f;
    s_g       = 1.0f;
    s_bloques = 0U;
    s_turno   = 0U;
    s_esp_db  = 0.0f;
    s_esp_bin = 0;
    s_ab_est  = AB_PARADA;
    s_ab_hay  = 0U;
    s_ab_off  = 0.0f;
    s_ab_on   = 0.0f;
}

void iqbal_set_on(uint8_t on) { s_on = on ? 1U : 0U; }
uint8_t iqbal_get_on(void)    { return s_on; }

void iqbal_mide(const int16_t *i, const int16_t *q, uint32_t n)
{
    float ii = 0.0f, qq = 0.0f, iq = 0.0f;
    float inv, q1q1, th, g;
    uint32_t k;

    if (n == 0U) { return; }

    /*
     * LA CONTINUA SE QUITA ANTES DE MEDIR, y no es un refinamiento: es la
     * diferencia entre medir y no medir. Con una componente continua en I y
     * otra en Q -que las hay siempre, el desplazamiento del ADC- las tres
     * medias se llenan con el producto de las dos continuas, que no tiene
     * nada que ver con el desequilibrio. El banco lo pilla con la portadora
     * puesta en el centro exacto: sin esto, la ganancia se iba al tope.
     */
    {
        float mi = 0.0f, mq = 0.0f;
        for (k = 0U; k < n; k++) {
            mi += (float)i[k];
            mq += (float)q[k];
        }
        inv = 1.0f / (float)n;
        mi *= inv; mq *= inv;
        for (k = 0U; k < n; k++) {
            float a = (float)i[k] - mi;
            float b = (float)q[k] - mq;
            ii += a * a;
            qq += b * b;
            iq += a * b;
        }
    }
    ii *= inv; qq *= inv; iq *= inv;

    s_ii += IQB_ALFA * (ii - s_ii);
    s_qq += IQB_ALFA * (qq - s_qq);
    s_iq += IQB_ALFA * (iq - s_iq);
    s_bloques++;

    if (s_ii < IQB_POT_MIN || s_qq < IQB_POT_MIN) {
        /* Sin señal no se toca lo que ya habia. Poner theta a cero aqui
         * seria tirar la estimacion cada vez que el usuario baja el volumen de
         * la antena o cambia de banda a un hueco vacio. */
        return;
    }

    /*
     * Y SI SE SALE DE LOS TOPES, NO SE ACEPTA. Recortar y quedarse con el
     * tope es lo peor de los dos mundos: una medida que ya se sabe mala se
     * aplica igual, y encima con el valor mas extremo posible. Un numero
     * fuera de rango aqui solo puede querer decir que lo que se esta
     * midiendo no es el desequilibrio del hardware -una continua grande, un
     * bloque recortado, una portadora justo en el centro-, y ante eso lo
     * correcto es no tocar nada y esperar al bloque siguiente.
     */
    th = s_iq / s_ii;
    if (th > IQB_THETA_MAX || th < -IQB_THETA_MAX) { return; }

    /* E[Q1*Q1] = E[QQ] - 2*th*E[IQ] + th*th*E[II], sin tener que volver a
     * recorrer el bloque: sale de las tres medias que ya hay. */
    q1q1 = s_qq - 2.0f * th * s_iq + th * th * s_ii;
    if (q1q1 < IQB_POT_MIN) { return; }

    g = sqrtf(s_ii / q1q1);
    if (g > IQB_G_MAX || g < IQB_G_MIN) { return; }

    s_theta = th;
    s_g     = g;
    s_hay   = 1U;
}

void iqbal_aplica(int16_t *i, int16_t *q, uint32_t n)
{
    uint32_t k;
    float th, g;

    if (!s_on || !s_hay) { return; }
    th = s_theta;
    g  = s_g;

    for (k = 0U; k < n; k++) {
        float v = g * ((float)q[k] - th * (float)i[k]);
        /* El entero de 16 bits se satura, no se envuelve. Envolver convierte
         * un pico en un salto de escala entera, que en la FFT es un suelo de
         * ruido nuevo repartido por toda la pantalla - mucho peor que el
         * espejo que estamos quitando. */
        if (v >  32767.0f) { v =  32767.0f; }
        if (v < -32768.0f) { v = -32768.0f; }
        q[k] = (int16_t)v;
    }
}

/* ======================================================================
 * CALIBRACION POR PORTADORAS IDENTIFICADAS, Y EL FIR QUE SALE DE ELLA
 * ======================================================================
 * El porque entero esta en iqbal.h. Aqui solo el como.
 */

/* Cuanto tiene que sobresalir un bin sobre la potencia media para que se le
 * considere una portadora: 20 dB. Mas bajo entran picos de ruido, y un pico
 * de ruido comparado con otro pico de ruido da un numero perfectamente
 * formado que no significa nada. */
#define IQB_PICO_SOBRE   100.0f

/* Bins pegados a la continua que se saltan: ahi viven el desplazamiento del
 * ADC y la fuga del propio LO, que no son señal y cuyo "espejo" son ellos
 * mismos. Es el mismo numero que usa iqbal_mira_espectro() mas abajo, y por
 * la misma razon; se escribe aparte porque aquel vive en el array YA
 * desplazado (centro en n/2) y este en el orden natural de la FFT. */
#define IQB_CAL_GUARDA   4U

/* Y el bin de enfrente tiene que estar al menos 6 dB por debajo. Si no, lo
 * que hay ahi no es una imagen: son dos emisoras, y medirlas contamina el
 * ajuste. Es uno de los dos fallos que el banco encontro en el estimador. */
#define IQB_ESPEJO_BAJO  0.25f

/* Medidas minimas en una ranura para fiarse de ella, y ranuras minimas para
 * poder resolver. Cuatro incognitas piden al menos cuatro puntos; se exigen
 * seis para que el ajuste no pase exactamente por todos y copie su ruido. */
#define IQB_CAL_MIN_N    4U
#define IQB_RANURAS_MIN  5U

/*
 * LAS RANURAS VIVEN EN LA SRAM Y EL FIR EN EL TCM, y no es indiferente: de
 * las ranuras se escribe una vez por FFT -en el bucle de pantalla- y del FIR
 * se lee UNA VEZ POR MUESTRA dentro de la interrupcion del DMA. El TCM es la
 * RAM rapida del nucleo y es donde tiene que estar lo segundo. Son 288 bytes
 * de SRAM y 72 de TCM; ver el comentario de arriba sobre por que no se
 * acumula un numero por bin.
 */
static float    s_cal_nre[IQB_RANURAS];
static float    s_cal_nim[IQB_RANURAS];
static float    s_cal_den[IQB_RANURAS];
static float    s_cal_fs[IQB_RANURAS];   /* suma de frecuencias, ciclos/muestra */
static uint16_t s_cal_n[IQB_RANURAS];

static float   s_fir_re[IQB_FIR_TOMAS] TCMRAM_BSS;
static float   s_fir_im[IQB_FIR_TOMAS] TCMRAM_BSS;
static float   s_hre[IQB_FIR_TOMAS]    TCMRAM_BSS;  /* historia entre bloques */
static float   s_him[IQB_FIR_TOMAS]    TCMRAM_BSS;
static uint8_t s_fir_hay               TCMRAM_BSS;

uint8_t iqbal_fir_hay(void) { return s_fir_hay; }

void iqbal_fir_olvida(void)
{
    uint8_t m;
    for (m = 0U; m < (uint8_t)IQB_RANURAS; m++) {
        s_cal_nre[m] = 0.0f; s_cal_nim[m] = 0.0f;
        s_cal_den[m] = 0.0f; s_cal_fs[m] = 0.0f; s_cal_n[m] = 0U;
    }
    for (m = 0U; m < (uint8_t)IQB_FIR_TOMAS; m++) {
        s_fir_re[m] = 0.0f; s_fir_im[m] = 0.0f;
        s_hre[m] = 0.0f;    s_him[m] = 0.0f;
    }
    s_fir_hay = 0U;
}

uint8_t iqbal_cal_ranuras(void)
{
    uint8_t m, c = 0U;
    for (m = 0U; m < (uint8_t)IQB_RANURAS; m++) {
        if (s_cal_n[m] >= (uint16_t)IQB_CAL_MIN_N) { c++; }
    }
    return c;
}

void iqbal_calibra(const float *re, const float *im, uint32_t n)
{
    uint32_t k, mitad;
    float media = 0.0f, umbral;

    if (re == 0 || im == 0 || n < 64U) { return; }
    mitad = n / 2U;

    /*
     * EL SUELO, CON LA MEDIA RECORTADA - y no con la media a secas, que fue
     * el primer intento y no encontraba NI UNA portadora.
     *
     * Con cinco emisoras fuertes dentro de 256 bins, las propias emisoras
     * levantan la media hasta que ninguna la supera por 20 dB: en el banco
     * salian cero ranuras y el FIR no llegaba a existir. iqbal_mira_espectro()
     * ya tropezo con esto en su dia y lo resolvio con una mediana aproximada
     * por biseccion; aqui sale mas barato -esto corre en CADA FFT, aquello
     * una vez por cuadro- con dos pasadas: la media de todo, y despues la
     * media de lo que no pase de diez veces la primera. Las portadoras se
     * caen de la segunda y lo que queda es el suelo de ruido.
     */
    for (k = 0U; k < n; k++) {
        media += re[k] * re[k] + im[k] * im[k];
    }
    media /= (float)n;
    if (media <= 0.0f) { return; }
    {
        float tope = media * 10.0f, suma = 0.0f;
        uint32_t cuenta = 0U;
        for (k = 0U; k < n; k++) {
            float p = re[k] * re[k] + im[k] * im[k];
            if (p <= tope) { suma += p; cuenta++; }
        }
        if (cuenta >= (n / 2U)) { media = suma / (float)cuenta; }
    }
    umbral = media * IQB_PICO_SOBRE;

    /* Se empieza en 2 y se acaba en n-2 para poder mirar los vecinos sin
     * salirse; los bins de la continua se saltan aparte, mas abajo. */
    for (k = 2U; k + 2U < n; k++) {
        float pk = re[k] * re[k] + im[k] * im[k];
        float pa, pb, pe, dr, di, fq;
        uint32_t ki, r;

        if (pk < umbral) { continue; }

        /* SOLO EL PICO. Las faldas de la ventana a los lados de una
         * portadora pasan el umbral de sobra, y lo que miden es basura. */
        pa = re[k - 1U] * re[k - 1U] + im[k - 1U] * im[k - 1U];
        pb = re[k + 1U] * re[k + 1U] + im[k + 1U] * im[k + 1U];
        if (pk < pa || pk < pb) { continue; }

        ki = (n - k) % n;                 /* el bin espejo */
        /* La continua y sus vecinos no son señal, y su "espejo" son ellos
         * mismos. */
        if (k < IQB_CAL_GUARDA || (n - k) < IQB_CAL_GUARDA) { continue; }
        if (ki < 2U || ki + 2U >= n) { continue; }

        pe = re[ki] * re[ki] + im[ki] * im[ki];
        if (pe > pk * IQB_ESPEJO_BAJO) { continue; }   /* enfrente hay señal */

        /*
         * W(-k) = -Z(-k)/conj(Z(k)) = -Z(-k)*Z(k)/|Z(k)|^2.
         * Se acumulan numerador y denominador por separado para que el
         * promedio entre medidas sea COHERENTE: promediar cocientes le da
         * el mismo peso a una portadora fuerte y a una que apenas pasa el
         * umbral, y la segunda trae mucho mas ruido.
         */
        dr = -(re[ki] * re[k] - im[ki] * im[k]);
        di = -(re[ki] * im[k] + im[ki] * re[k]);

        r = (ki * (uint32_t)IQB_RANURAS) / n;
        if (r >= (uint32_t)IQB_RANURAS) { r = (uint32_t)IQB_RANURAS - 1U; }

        /* La frecuencia del bin espejo, en ciclos por muestra y con signo. */
        fq = (ki < mitad) ? ((float)ki / (float)n)
                          : (((float)ki - (float)n) / (float)n);

        s_cal_nre[r] += dr;
        s_cal_nim[r] += di;
        s_cal_den[r] += pk;
        s_cal_fs[r]  += fq;
        if (s_cal_n[r] < 60000U) { s_cal_n[r]++; }
    }
}

/*
 * El sistema de IQB_FIR_TOMAS incognitas complejas, por eliminacion de Gauss
 * con pivoteo parcial. Cuatro por cuatro: no merece una biblioteca, y una
 * biblioteca de algebra lineal en un firmware de 475 kB tampoco.
 */
static uint8_t resuelve_sistema(float ar[IQB_FIR_TOMAS][IQB_FIR_TOMAS + 1U],
                                float ai[IQB_FIR_TOMAS][IQB_FIR_TOMAS + 1U],
                                float *xr, float *xi)
{
    const uint8_t L = (uint8_t)IQB_FIR_TOMAS;
    uint8_t c, f, p;

    for (c = 0U; c < L; c++) {
        float mejor = -1.0f;
        p = c;
        for (f = c; f < L; f++) {
            float v = ar[f][c] * ar[f][c] + ai[f][c] * ai[f][c];
            if (v > mejor) { mejor = v; p = f; }
        }
        /* Un pivote a cero quiere decir que las medidas no dan para tantas
         * incognitas -por ejemplo, todas las portadanas en el mismo sitio-.
         * No se inventa una solucion: se dice que no. */
        if (mejor <= 1e-20f) { return 0U; }
        if (p != c) {
            for (f = 0U; f <= L; f++) {
                float t = ar[c][f]; ar[c][f] = ar[p][f]; ar[p][f] = t;
                t = ai[c][f]; ai[c][f] = ai[p][f]; ai[p][f] = t;
            }
        }
        {
            float dr = ar[c][c], di = ai[c][c];
            float inv = 1.0f / (dr * dr + di * di);
            for (f = c; f <= L; f++) {
                float zr = ar[c][f], zi = ai[c][f];
                ar[c][f] = (zr * dr + zi * di) * inv;
                ai[c][f] = (zi * dr - zr * di) * inv;
            }
        }
        for (f = 0U; f < L; f++) {
            float mr, mi;
            if (f == c) { continue; }
            mr = ar[f][c]; mi = ai[f][c];
            if (mr == 0.0f && mi == 0.0f) { continue; }
            {
                uint8_t j;
                for (j = c; j <= L; j++) {
                    ar[f][j] -= mr * ar[c][j] - mi * ai[c][j];
                    ai[f][j] -= mr * ai[c][j] + mi * ar[c][j];
                }
            }
        }
    }
    for (c = 0U; c < L; c++) { xr[c] = ar[c][L]; xi[c] = ai[c][L]; }
    return 1U;
}

uint8_t iqbal_resuelve(void)
{
    const uint8_t L = (uint8_t)IQB_FIR_TOMAS;
    float ar[IQB_FIR_TOMAS][IQB_FIR_TOMAS + 1U];
    float ai[IQB_FIR_TOMAS][IQB_FIR_TOMAS + 1U];
    float xr[IQB_FIR_TOMAS], xi[IQB_FIR_TOMAS];
    uint8_t r, m, j, usadas = 0U;

    for (m = 0U; m < L; m++) {
        for (j = 0U; j <= L; j++) { ar[m][j] = 0.0f; ai[m][j] = 0.0f; }
    }

    for (r = 0U; r < (uint8_t)IQB_RANURAS; r++) {
        float wr, wi, fq, peso;
        if (s_cal_n[r] < (uint16_t)IQB_CAL_MIN_N) { continue; }
        if (s_cal_den[r] <= 0.0f) { continue; }
        usadas++;

        wr = s_cal_nre[r] / s_cal_den[r];
        wi = s_cal_nim[r] / s_cal_den[r];
        fq = s_cal_fs[r] / (float)s_cal_n[r];
        peso = (float)s_cal_n[r];

        /*
         * Fila de las ecuaciones normales: A^H W A h = A^H W d, con
         * A[m] = exp(-j 2 pi f m) (CAUSAL; ver iqbal.h para por que no
         * centrado). Se acumula directamente, sin montar A entera.
         */
        for (m = 0U; m < L; m++) {
            float am_r = cosf(-6.2831853f * fq * (float)m);
            float am_i = sinf(-6.2831853f * fq * (float)m);
            for (j = 0U; j < L; j++) {
                float aj_r = cosf(-6.2831853f * fq * (float)j);
                float aj_i = sinf(-6.2831853f * fq * (float)j);
                /* conj(A[m]) * A[j] * peso */
                ar[m][j] += peso * (am_r * aj_r + am_i * aj_i);
                ai[m][j] += peso * (am_r * aj_i - am_i * aj_r);
            }
            /* conj(A[m]) * d * peso */
            ar[m][L] += peso * (am_r * wr + am_i * wi);
            ai[m][L] += peso * (am_r * wi - am_i * wr);
        }
    }

    if (usadas < (uint8_t)IQB_RANURAS_MIN) { return 0U; }

    /*
     * SI YA NO HAY ESPEJO, NO SE TOCA NADA - y esto no es prudencia, es un
     * fallo que el banco encontro.
     *
     * Con el desequilibrio PLANO, el corrector de un coeficiente deja el
     * espejo en 78 dB. El FIR, ajustado sobre cinco puntos ruidosos, metia
     * SU PROPIO ruido y lo dejaba en 59: diecinueve dB peor que no hacer
     * nada. El estimador tiene un techo de precision y, por encima de el,
     * corregir es estropear.
     *
     * Asi que primero se mira CUANTO espejo queda: |W| es la relacion
     * imagen/señal medida.
     *
     * EL UMBRAL ES -42 dB, y conviene saber que NO basta. El estimador
     * tiene su propio suelo -FFT de 256 puntos y señal cuantizada a 16
     * bits-, y en el caso plano, con el espejo real en 78 dB, sigue
     * midiendo residuo por encima de esto. O sea que la guarda no llega a
     * dispararse ahi y el FIR mete algo de ruido igualmente: 78 -> 74,6 dB
     * medido en el banco.
     *
     * Se deja asi a proposito, con el numero delante: 74 dB de rechazo
     * siguen estando muy por encima de lo que se ve en pantalla (45-50), y
     * a cambio los casos que de verdad duelen -22 dB- suben a 38. Subir el
     * umbral a -35 arregla menos de lo que estropea: el lazo se para antes
     * y el caso con pendiente se queda en 34,5 en vez de 38,5. Medido
     * tambien.
     */
    {
        float suma = 0.0f, pesos = 0.0f;
        for (r = 0U; r < (uint8_t)IQB_RANURAS; r++) {
            float wr, wi, pw;
            if (s_cal_n[r] < (uint16_t)IQB_CAL_MIN_N) { continue; }
            if (s_cal_den[r] <= 0.0f) { continue; }
            wr = s_cal_nre[r] / s_cal_den[r];
            wi = s_cal_nim[r] / s_cal_den[r];
            pw = (float)s_cal_n[r];
            suma  += pw * (wr * wr + wi * wi);
            pesos += pw;
        }
        /* (10^(-42/20))^2 = 6,3e-5 */
        if (pesos <= 0.0f || (suma / pesos) < 6.3e-5f) { return 0U; }
    }

    if (!resuelve_sistema(ar, ai, xr, xi)) { return 0U; }

    /*
     * EL TOPE. Lo mismo que hacen theta y g mas arriba, y por lo mismo: un
     * coeficiente disparatado solo puede querer decir que lo que se ha
     * medido no es el desequilibrio. Una imagen a 0 dB de la señal seria
     * |w| = 1; por encima de 0,5 ya no hay nada que corregir, hay otra cosa.
     */
    for (m = 0U; m < L; m++) {
        float mag = xr[m] * xr[m] + xi[m] * xi[m];
        if (!(mag < 0.25f)) { return 0U; }
    }

    /*
     * SE SUMA, no se sustituye: el panadaptador mide sobre lo ya corregido,
     * asi que esto es el residuo. Ver iqbal.h.
     *
     * Y SE SUMA A MEDIAS. El paso completo mete de golpe todo el ruido de
     * una sola estimacion; con medio paso, el lazo tarda una vuelta mas en
     * llegar -son dos segundos- y el ruido de cada estimacion se promedia
     * con el de las siguientes en vez de acumularse. Es amortiguacion de
     * toda la vida, y aqui se nota porque el lazo corre muchas veces y la
     * prisa no sirve de nada.
     */
    for (m = 0U; m < L; m++) {
        s_fir_re[m] += 0.5f * xr[m];
        s_fir_im[m] += 0.5f * xi[m];
    }
    s_fir_hay = 1U;

    /* Y las ranuras a cero: la proxima vuelta tiene que medir el residuo
     * nuevo, no volver a sumar el de antes. */
    for (r = 0U; r < (uint8_t)IQB_RANURAS; r++) {
        s_cal_nre[r] = 0.0f; s_cal_nim[r] = 0.0f;
        s_cal_den[r] = 0.0f; s_cal_fs[r] = 0.0f; s_cal_n[r] = 0U;
    }
    return 1U;
}

/*
 * El FIR sobre el conjugado, en el sitio y sobre el bloque entrelazado.
 *
 *     y(n) = z(n) + suma_m w[m] * conj(z(n-m))
 *
 * La historia se guarda entre bloques: sin eso, las IQB_FIR_TOMAS-1 primeras
 * muestras de cada bloque se corregirian con ceros y el resultado seria un
 * chasquido cada 2,7 ms, que es justo la clase de fallo que se oye y no se
 * ve en ninguna medida de rechazo.
 */
static void fir_aplica(int16_t *crudo, uint32_t n)
{
    const uint8_t L = (uint8_t)IQB_FIR_TOMAS;
    uint32_t k;
    uint8_t m;

    for (k = 0U; k < n; k++) {
        float zr = (float)crudo[2U * k];
        float zi = (float)crudo[2U * k + 1U];
        float yr, yi;

        /* La mas nueva entra la primera; las demas se corren. */
        for (m = (uint8_t)(L - 1U); m > 0U; m--) {
            s_hre[m] = s_hre[m - 1U];
            s_him[m] = s_him[m - 1U];
        }
        s_hre[0] = zr; s_him[0] = zi;

        yr = zr; yi = zi;
        for (m = 0U; m < L; m++) {
            /* w[m] * conj(h[m]) */
            yr += s_fir_re[m] * s_hre[m] + s_fir_im[m] * s_him[m];
            yi += s_fir_im[m] * s_hre[m] - s_fir_re[m] * s_him[m];
        }

        if (yr >  32767.0f) { yr =  32767.0f; }
        if (yr < -32768.0f) { yr = -32768.0f; }
        if (yi >  32767.0f) { yi =  32767.0f; }
        if (yi < -32768.0f) { yi = -32768.0f; }
        crudo[2U * k]      = (int16_t)yr;
        crudo[2U * k + 1U] = (int16_t)yi;
    }
}

/* Uno de cada ocho. Ver iqbal.h. */
#define IQB_UNO_DE   8U

void iqbal_bloque(int16_t *crudo, uint32_t n)
{
    uint32_t k;
    float th, g;

    if (n == 0U) { return; }

    /* El plazo de la prueba A/B. Aqui y no en iqbal_ab_paso(), porque el
     * fallo que lo trajo era justo que a iqbal_ab_paso() no la llamaba
     * nadie. Ver iqbal.h. */
    if (s_ab_est != AB_PARADA) {
        s_ab_plazo++;
        if (s_ab_plazo >= IQB_AB_PLAZO) { ab_aborta(); }
    }

    if (s_turno == 0U) {
        /*
         * La medida sobre el entrelazado. Es la misma cuenta que
         * iqbal_mide() -incluido quitar la continua- pero recorriendo de dos
         * en dos, para no tener que copiar el bloque a dos arrays sueltos
         * dentro de la interrupcion.
         */
        float mi = 0.0f, mq = 0.0f, ii = 0.0f, qq = 0.0f, iq = 0.0f;
        float inv = 1.0f / (float)n;

        for (k = 0U; k < n; k++) {
            mi += (float)crudo[2U * k];
            mq += (float)crudo[2U * k + 1U];
        }
        mi *= inv; mq *= inv;
        for (k = 0U; k < n; k++) {
            float a = (float)crudo[2U * k]      - mi;
            float b = (float)crudo[2U * k + 1U] - mq;
            ii += a * a; qq += b * b; iq += a * b;
        }
        ii *= inv; qq *= inv; iq *= inv;

        s_ii += IQB_ALFA * (ii - s_ii);
        s_qq += IQB_ALFA * (qq - s_qq);
        s_iq += IQB_ALFA * (iq - s_iq);
        s_bloques++;

        if (s_ii >= IQB_POT_MIN && s_qq >= IQB_POT_MIN) {
            th = s_iq / s_ii;
            if (th <= IQB_THETA_MAX && th >= -IQB_THETA_MAX) {
                float q1q1 = s_qq - 2.0f * th * s_iq + th * th * s_ii;
                if (q1q1 >= IQB_POT_MIN) {
                    g = sqrtf(s_ii / q1q1);
                    if (g <= IQB_G_MAX && g >= IQB_G_MIN) {
                        s_theta = th;
                        s_g     = g;
                        s_hay   = 1U;
                    }
                }
            }
        }
    }
    s_turno = (uint8_t)((s_turno + 1U) % IQB_UNO_DE);

    if (!s_on) { return; }

    if (s_hay) {
        th = s_theta;
        g  = s_g;
        for (k = 0U; k < n; k++) {
            float v = g * ((float)crudo[2U * k + 1U] - th * (float)crudo[2U * k]);
            if (v >  32767.0f) { v =  32767.0f; }
            if (v < -32768.0f) { v = -32768.0f; }
            crudo[2U * k + 1U] = (int16_t)v;
        }
    }

    /*
     * Y DESPUES EL FIR, sobre lo que el de un coeficiente ha dejado.
     *
     * El orden importa y es el que hace que esto sea un añadido y no un
     * reemplazo: el corrector plano quita la parte del desequilibrio que es
     * igual en toda la banda -que es la mayor- y el FIR se ocupa de lo que
     * varia con la frecuencia, que es lo que aquel no puede. Mientras no
     * haya calibracion, s_fir_hay vale cero y la radio se comporta
     * EXACTAMENTE como antes.
     */
    if (s_fir_hay) { fir_aplica(crudo, n); }
}

/* --- el espejo leido del espectro (ver iqbal.h) ------------------------- */

/* Bins pegados al centro que se saltan: ahi viven la continua y la fuga del
 * propio LO, que no son señal y cuyo "espejo" son ellos mismos. */
#define IQB_GUARDA   4

/* Cuanto tiene que sobresalir el pico sobre el suelo para que la medida
 * signifique algo. Comparar un pico de ruido con otro pico de ruido da un
 * numero perfectamente formado que no dice nada. */
#define IQB_SOBRE_DB 12.0f

void iqbal_mira_espectro(const float *db, uint16_t n)
{
    uint16_t c, k, mejor = 0U;
    float pico = -1e30f, suelo, dif;

    if (db == 0 || n < 32U) { return; }
    c = (uint16_t)(n / 2U);

    /* El pico mas alto que no sea el centro. */
    for (k = 0U; k < n; k++) {
        int32_t d = (int32_t)k - (int32_t)c;
        if (d < 0) { d = -d; }
        if (d < IQB_GUARDA) { continue; }
        /* y que su espejo este dentro del array */
        if ((uint16_t)(2U * c) < k || (2U * c - k) >= n) { continue; }
        if (db[k] > pico) { pico = db[k]; mejor = k; }
    }
    if (pico < -1e29f) { return; }

    /*
     * El suelo, por MEDIANA aproximada y no por media: con una portadora
     * fuerte dentro, la media del espectro la arrastra y el "sobresale 12 dB"
     * deja de medir lo que dice. Se hace contando cuantos bins quedan por
     * debajo de un valor que se va ajustando, que sale mas barato que ordenar
     * doscientos cincuenta y seis numeros en el bucle de pantalla.
     */
    {
        float lo = 1e30f, hi = -1e30f, m;
        uint16_t it;
        for (k = 0U; k < n; k++) {
            if (db[k] < lo) { lo = db[k]; }
            if (db[k] > hi) { hi = db[k]; }
        }
        for (it = 0U; it < 12U; it++) {
            uint16_t bajo = 0U;
            m = 0.5f * (lo + hi);
            for (k = 0U; k < n; k++) { if (db[k] < m) { bajo++; } }
            if (bajo * 2U < n) { lo = m; } else { hi = m; }
        }
        suelo = 0.5f * (lo + hi);
    }

    if (pico < suelo + IQB_SOBRE_DB) {
        /*
         * EL COMENTARIO MENTIA Y EL BIN SE QUEDABA COLGADO - revision a
         * fondo del 08/10/2026.
         *
         * Aqui ponia "se deja el anterior y se marca que no hay", y el
         * codigo no deja el anterior: lo pisa con cero. El codigo es el
         * que tiene razon -el cero ES la marca de "no hay", y la prueba
         * A/B se apoya en ella con el `if (s_esp_db > 0.0f)` de
         * iqbal_ab_paso() para no promediar cuadros sin señal-, asi que lo
         * que se arregla es el texto, no la cuenta.
         *
         * Y faltaba la otra mitad: s_esp_bin no se tocaba. La pagina de
         * Informacion enseña los dos numeros JUNTOS, asi que salia "0 dB
         * de rechazo" al lado del bin de un pico de hace minutos y de otra
         * frecuencia, que es una pareja que no ha existido nunca. Un cero
         * en los dos dice lo que pasa: no hay medida.
         */
        s_esp_db  = 0.0f;
        s_esp_bin = 0;
        return;
    }

    /*
     * Y el espejo: el bin simetrico respecto al centro. Se cogen tres y se
     * queda el mas alto, porque el pico de la señal y el de su imagen no
     * caen siempre en el mismo bin exacto -el promediado de pantalla los
     * desplaza medio bin- y quedarse corto aqui inventaria rechazo de mas.
     */
    {
        uint16_t e = (uint16_t)(2U * c - mejor);
        float esp = -1e30f;
        int16_t j;
        for (j = -1; j <= 1; j++) {
            int32_t x = (int32_t)e + j;
            if (x < 0 || x >= (int32_t)n) { continue; }
            if (db[x] > esp) { esp = db[x]; }
        }
        /* Si el espejo esta en el suelo, lo que se mide es el suelo: el
         * rechazo es "por lo menos esto", y se dice con ese valor. */
        if (esp < suelo) { esp = suelo; }
        dif = pico - esp;
    }

    if (dif < 0.0f) { dif = 0.0f; }
    s_esp_db  = dif;
    s_esp_bin = (int16_t)((int32_t)mejor - (int32_t)c);
}

/* --- la prueba A/B (ver iqbal.h) ---------------------------------------- */

/*
 * Cuadros que se esperan antes de empezar a medir en cada fase. La pantalla
 * promedia dentro del cuadro Y ADEMAS mezcla cada cuadro con el anterior, asi
 * que la medida de justo despues de tocar el corrector todavia lleva dentro
 * lo de antes. Veinte cuadros son dos tercios de segundo: de sobra para que
 * no quede nada de la fase anterior.
 */
void iqbal_ab_lanza(void)
{
    if (s_ab_est != AB_PARADA) { return; }   /* ya hay una en marcha */
    s_ab_antes = s_on;
    s_ab_n     = 0U;
    s_ab_c_off = 0U; s_ab_c_on = 0U;
    s_ab_s_off = 0.0f; s_ab_s_on = 0.0f;
    s_ab_hay   = 0U;
    s_ab_plazo = 0U;
    s_on       = 0U;
    iqa_set_on(0U);
    /*
     * Y EL ESTADO, EL ULTIMO - revision a fondo del 08/10/2026.
     *
     * Aqui s_ab_est se ponia el PRIMERO, y eso abria la prueba en canal
     * antes de que estuviera preparada. La interrupcion del DMA mira
     * exactamente esa variable para decidir si cuenta el plazo, asi que
     * entre esa linea y el `s_ab_plazo = 0U` de abajo habia unos cuantos
     * bloques de audio de ventana en los que la interrupcion contaba el
     * plazo CON EL CONTADOR DE LA PRUEBA ANTERIOR. Si aquella habia
     * vencido por plazo, el contador valia ya 2.500 y el primer bloque que
     * entrase abortaba esta nada mas lanzarla.
     *
     * Y abortar entonces no es solo perder la medida: ab_aborta() devuelve
     * s_on a como estaba, y acto seguido este codigo sigue por donde iba y
     * escribe s_on = 0 y iqa_set_on(0) para empezar la fase de "apagado"
     * de una prueba que ya no existe. Resultado: EL CORRECTOR DE ESPEJO
     * APAGADO PARA SIEMPRE, sin nadie que lo vuelva a encender y sin que
     * la pantalla diga nada, porque iqbal_ab_corriendo() contesta que no
     * hay prueba. Lo que se veia como un rechazo que desaparecia
     * era esto.
     *
     * Poniendo el estado al final, la interrupcion no puede ver la prueba
     * en marcha hasta que todo lo demas -plazo incluido- ya esta puesto.
     */
    s_ab_est   = AB_OFF_ESPERA;
}

/* Aborta la prueba y devuelve el corrector a como estaba. Se llama sola
 * cuando vence el plazo; no deja resultado a proposito, porque una prueba
 * que no ha terminado no ha medido nada. */
static void ab_aborta(void)
{
    s_on     = s_ab_antes;
    iqa_set_on(s_ab_antes);
    s_ab_est = AB_PARADA;
    s_ab_n   = 0U;
    s_ab_hay = 0U;
    /*
     * Y EL PLAZO A CERO - revision a fondo del 08/10/2026.
     *
     * Faltaba, y era la mitad de la averia que cuenta iqbal_ab_lanza():
     * una prueba que vencia por plazo dejaba el contador clavado en 2.500,
     * y como solo lo ponia a cero iqbal_ab_lanza() -y encima despues de
     * abrir la prueba-, la siguiente se abortaba sola al primer bloque.
     * Dejarlo limpio aqui, en el mismo sitio que apaga la prueba, es lo
     * que hace que esto no dependa del orden en que escriba el otro lado.
     */
    s_ab_plazo = 0U;
}

uint8_t iqbal_ab_corriendo(void)
{
    if (s_ab_est == AB_OFF_ESPERA || s_ab_est == AB_OFF_MIDE) { return 1U; }
    if (s_ab_est == AB_ON_ESPERA  || s_ab_est == AB_ON_MIDE)  { return 2U; }
    return 0U;
}

uint8_t iqbal_ab_hay(void) { return s_ab_hay; }
float   iqbal_ab_off(void) { return s_ab_off; }
float   iqbal_ab_on(void)  { return s_ab_on;  }

void iqbal_ab_paso(void)
{
    if (s_ab_est == AB_PARADA) { return; }
    s_ab_n++;

    switch (s_ab_est) {
    case AB_OFF_ESPERA:
        if (s_ab_n >= IQB_AB_ESPERA) { s_ab_est = AB_OFF_MIDE; s_ab_n = 0U; }
        break;

    case AB_OFF_MIDE:
        /* Solo cuentan los cuadros en los que habia una señal clara: ver
         * iqbal_mira_espectro(), que devuelve 0 cuando no la hay. Promediar
         * ceros rebajaria el resultado por no haber nada que medir, que es
         * la forma mas facil de hacer creer que el corrector gana. */
        if (s_esp_db > 0.0f) { s_ab_s_off += s_esp_db; s_ab_c_off++; }
        if (s_ab_n >= IQB_AB_MIDE) {
            s_ab_est = AB_ON_ESPERA; s_ab_n = 0U;
            s_on = 1U; iqa_set_on(1U);
        }
        break;

    case AB_ON_ESPERA:
        if (s_ab_n >= IQB_AB_ESPERA) { s_ab_est = AB_ON_MIDE; s_ab_n = 0U; }
        break;

    case AB_ON_MIDE:
        if (s_esp_db > 0.0f) { s_ab_s_on += s_esp_db; s_ab_c_on++; }
        if (s_ab_n >= IQB_AB_MIDE) {
            /*
             * Y el corrector vuelve a como estaba, pase lo que pase. Dejarlo
             * donde lo dejo la prueba seria cambiarle la radio al usuario por
             * haber pedido una medida.
             */
            s_on     = s_ab_antes;
            iqa_set_on(s_ab_antes);
            s_ab_est = AB_PARADA;
            s_ab_n   = 0U;
            /* Sin cuadros validos en alguna de las dos fases no hay
             * resultado: media de nada es cero, y un cero aqui se leeria
             * como "cero dB de rechazo". */
            if (s_ab_c_off >= 3U && s_ab_c_on >= 3U) {
                s_ab_off = s_ab_s_off / (float)s_ab_c_off;
                s_ab_on  = s_ab_s_on  / (float)s_ab_c_on;
                s_ab_hay = 1U;
            } else {
                s_ab_hay = 0U;
            }
        }
        break;

    default:
        s_ab_est = AB_PARADA;
        break;
    }
}

float   iqbal_espejo_db(void)  { return s_esp_db; }
int16_t iqbal_espejo_bin(void) { return s_esp_bin; }

float iqbal_theta(void) { return s_theta; }
float iqbal_g(void)     { return s_g; }

/*
 * DE LOS DOS COEFICIENTES A LOS DOS NUMEROS QUE SE ENTIENDEN.
 *
 * Lo que entra por la antena, con el convenio de siempre -toda la culpa en
 * Q-, es I = cos(wt) y Q = a*sin(wt+phi). Entonces
 *
 *     theta = E[IQ]/E[II]       = a*sin(phi)
 *     Q1    = Q - theta*I       = a*cos(phi)*sin(wt)
 *     g     = raiz(E[II]/E[Q1Q1]) = 1 / (a*cos(phi))
 *
 * y de ahi salen los dos de vuelta, EXACTOS y no aproximados:
 *
 *     tan(phi) = theta * g          -> phi
 *     a        = raiz(theta^2 + 1/g^2)
 *
 * Esto no es un detalle de presentacion. La primera version devolvia
 * atan(theta) a secas, que es a*sin(phi) y no sin(phi): con 3,00 grados de
 * verdad y 0,8 dB de ganancia, enseñaba 3,91. El numero que sale en la
 * pantalla de Informacion tiene que ser el que uno pueda comparar con la
 * hoja de caracteristicas de un mezclador, no uno parecido.
 */
float iqbal_fase_grados(void)
{
    return atanf(s_theta * s_g) * (180.0f / 3.14159265358979f);
}

float iqbal_gan_db(void)
{
    float a;
    if (s_g <= 0.0f) { return 0.0f; }
    a = sqrtf(s_theta * s_theta + 1.0f / (s_g * s_g));
    if (a <= 0.0f) { return 0.0f; }
    return 20.0f * log10f(a);
}

float iqbal_rechazo_db(void)
{
    /*
     * Con un error de ganancia e (lineal, g = 1+e) y uno de fase phi, el
     * espejo queda en
     *
     *     IRR = (e^2 + phi^2) / 4        (en potencia, para errores pequeños)
     *
     * que es la aproximacion clasica y vale de sobra en el margen que nos
     * importa -por debajo de cinco grados y un dB-. Se devuelve en dB y
     * positivo: cuanto mas alto, menos espejo.
     */
    float e   = sqrtf(s_theta * s_theta + 1.0f / (s_g * s_g)) - 1.0f;
    float phi = atanf(s_theta * s_g);
    float p   = (e * e + phi * phi) * 0.25f;

    if (!s_hay) { return 0.0f; }
    if (p < 1e-12f) { p = 1e-12f; }
    return -10.0f * log10f(p);
}

uint32_t iqbal_bloques(void) { return s_bloques; }
