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
 * 3,3 MHz que en 14-, o sea que solo se mueve cuando el dueño mueve el dial.
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
         * seria tirar la estimacion cada vez que el dueño baja el volumen de
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

    if (!s_on || !s_hay) { return; }
    th = s_theta;
    g  = s_g;
    for (k = 0U; k < n; k++) {
        float v = g * ((float)crudo[2U * k + 1U] - th * (float)crudo[2U * k]);
        if (v >  32767.0f) { v =  32767.0f; }
        if (v < -32768.0f) { v = -32768.0f; }
        crudo[2U * k + 1U] = (int16_t)v;
    }
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
     * hay prueba. Lo que vio el dueño como "se ha ido el rechazo" era
     * esto.
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
             * donde lo dejo la prueba seria cambiarle la radio al dueño por
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
