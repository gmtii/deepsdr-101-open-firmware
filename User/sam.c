#include "sam.h"
#include "nco.h"
#include "rapido.h"   /* rapido_atan2(): ver sam_nucleo() */
#include <math.h>

#define SAM_TPI (2.0f * 3.14159265358979323846f)

/* De radianes a la rejilla de fase de 32 bits: 2^32 / (2*pi). */
#define SAM_K_FASE  683565275.0f

/* Y el umbral de enganche, en UN solo sitio. 0,6 sale de medir: con
 * portadora limpia sam_enganche() pasa de 0,9; con ruido solo se queda por
 * debajo de 0,1. Ver sim/samtest.c. */
/*
 * Los dos umbrales del si/no. Ver sam_t::eng en sam.h.
 *
 * El de entrada es el de siempre (0,6). El de salida se pone en 0,45 y no
 * mas abajo porque por debajo de ahi el detector ya esta repartido por media
 * vuelta y el numero de PPM deja de significar nada - que es justo lo que
 * esta medida existe para no enseñar.
 */
#define SAM_ENG_ENTRA  0.6f
#define SAM_ENG_SALE   0.45f

void sam_init(sam_t *s, float32_t sample_rate_hz, float32_t pll_fmax_hz,
        float32_t omega_n_hz, float32_t zeta)
{
    s->pll_fmax = pll_fmax_hz;
    s->zeta = zeta;
    s->omegaN = omega_n_hz;

    /* Matches sam_variables_init()'s own omega_min/omega_max/g1/g2
     * formulas exactly. El termino DR del original NO esta aqui dentro
     * sino en sam_nucleo(), donde se ve por que: ver alli. */
    /* La tabla del NCO tiene que existir antes de la primera muestra. Es
     * idempotente y main.c ya la llama al arrancar, pero sam_init() no se
     * puede fiar de quien le llame: sin tabla, sen y cos salen cero y el
     * modo enmudece sin decir por que. */
    nco_init();

    s->omega_min = -SAM_TPI * pll_fmax_hz / sample_rate_hz;
    s->omega_max =  SAM_TPI * pll_fmax_hz / sample_rate_hz;

    s->g1 = 1.0f - expf(-2.0f * omega_n_hz * zeta / sample_rate_hz);
    s->g2 = -s->g1 + 2.0f * (1.0f - expf(-omega_n_hz * zeta / sample_rate_hz) *
                                     cosf(omega_n_hz / sample_rate_hz *
                                          sqrtf(1.0f - zeta * zeta)));

    /* La media de |det| corre al ritmo del detector, o sea fs/SAM_DR, y su
     * constante de tiempo es medio segundo: lo bastante lenta para que un
     * desvanecimiento corto no diga "se ha desenganchado" y lo bastante
     * rapida para que cambiar de emisora se note antes de leer el numero. */
    s->a_det = (float32_t)SAM_DR / (sample_rate_hz * 0.5f);

    /* fade leveler - tauR/tauI values match sam_variables_init()'s own
     * comments ("original 0.02"/"original 1.4"). */
    s->tauR = 0.02f;
    s->tauI = 1.4f;
    s->mtauR = expf(-1.0f / (sample_rate_hz * s->tauR));
    s->onem_mtauR = 1.0f - s->mtauR;
    s->mtauI = expf(-1.0f / (sample_rate_hz * s->tauI));
    s->onem_mtauI = 1.0f - s->mtauI;
    s->dc = 0.0f;
    s->dc_insert = 0.0f;
    s->fade_leveler = 1u;

    sam_reset(s);
}

/* Ver el comentario largo de sam.h: esto es lo unico que cambia el margen,
 * y no toca ni las ganancias ni el estado del lazo. El recorte del
 * integrador es lo que hace que estrechar el margen ESTANDO el lazo fuera
 * del nuevo tope lo traiga de vuelta en el acto en vez de dejarlo colgado
 * en un valor que ya no es alcanzable. */
void sam_margen_pon(sam_t *s, float32_t pll_fmax_hz, float32_t sample_rate_hz)
{
    s->pll_fmax = pll_fmax_hz;
    s->omega_min = -SAM_TPI * pll_fmax_hz / sample_rate_hz;
    s->omega_max =  SAM_TPI * pll_fmax_hz / sample_rate_hz;
    if (s->omega2 < s->omega_min)      { s->omega2 = s->omega_min; }
    else if (s->omega2 > s->omega_max) { s->omega2 = s->omega_max; }
}

void sam_reset(sam_t *s)
{
    s->det = 0.0f;
    s->fil_out = 0.0f;
    s->del_out = 0.0f;
    s->omega2 = 0.0f;
    s->fase = 0U;
    s->inc = 0;
    s->acum0 = 0.0f;
    s->acum1 = 0.0f;
    s->cuenta = (uint8_t)SAM_DR;
    /* Arranca DESENGANCHADO, no enganchado: pi/2 es lo que da el detector
     * cuando no hay nada. Al reves, la pantalla diria "enganchado" durante
     * el medio segundo que tarda la media en enterarse de que no lo esta. */
    s->det_med = SAM_TPI * 0.25f;
    s->eng = 0U;
    s->corr0 = 0.0f;
    s->corr1 = 0.0f;
    s->audio = 0.0f;
    s->carrier_hz_raw = 0.0f;
    s->carrier_hz = 0.0f;
    s->tope = 0U;
    s->tope_lado = 0U;
    s->tope_prom = 0.0f;
}

/*
 * ===========================================================================
 * EL CUERPO. Una sola copia, dos puertas: sam_step() y sam_bloque().
 * ===========================================================================
 *
 * QUE CORRE POR MUESTRA Y QUE NO, Y POR QUE NO CAMBIA EL LAZO.
 *
 * Por muestra corre lo que no se puede saltar: el oscilador -porque el audio
 * sale de multiplicar por el, muestra a muestra-, las cuatro multiplicaciones
 * de la mezcla y el nivelador de desvanecimiento.
 *
 * Una de cada SAM_DR corre el resto: el arcotangente del detector y el filtro
 * de lazo. Esto es lo que el original llamaba DR y esta version habia puesto
 * a 1. Que se puede hacer no es una opinion: el lazo tiene un ancho de banda
 * de 200 Hz y el detector pasa de medir a 96.000 veces por segundo a medirlo
 * 24.000, que siguen siendo CIENTO VEINTE veces el ancho del lazo. Lo que
 * decide de verdad es sim/samtest.c, que corre este PLL y el exacto por
 * muestra sobre la misma señal y compara enganche y audio.
 *
 * COMO SE ESCALAN LAS GANANCIAS, que es donde esto se puede estropear en
 * silencio:
 *
 *   - g1 (el camino proporcional) NO se toca. Aporta un adelanto de fase de
 *     g1*det por muestra; entre dos medidas se aplica SAM_DR veces, que es
 *     justo lo que aportarian SAM_DR medidas seguidas con el mismo det.
 *   - g2 (el integrador) SI: omega2 subia g2*det por muestra, o sea
 *     SAM_DR*g2*det entre dos medidas. Por eso aqui se suma de una vez
 *     SAM_DR*g2*det. Los topes omega_min/omega_max no cambian, porque
 *     omega2 sigue estando en radianes POR MUESTRA.
 *
 * Y EL DETECTOR NO MIRA UNA MUESTRA SUELTA, mira la SUMA de las SAM_DR. Sale
 * gratis -dos sumas por muestra- y es mejor que coger la ultima: el angulo de
 * la suma es el angulo medio del tramo, con el ruido promediado. Lo que pone
 * el tope a SAM_DR es cuanto gira ese fasor dentro del tramo; esta en sam.h.
 */
/*
 * LA MEZCLA, Y POR QUE ENTRA AQUI Y NO FUERA - 22/10/2026
 *
 * Avisado por un usuario del repositorio. Sin enganche, SAM de una sola
 * banda volvia a la envolvente, o sea a AM, o sea a oir las DOS bandas: la
 * intrusa de la que se huia volvia entera. Lo que se quiere en su lugar es
 * banda lateral de frecuencia fija - el oscilador quieto en la sintonia -,
 * que es lo que hace el modo USB/LSB de siempre.
 *
 * `mez` cruza entre las dos SIN UN SEGUNDO BUFFER: 1 saca lo desrotado por
 * el lazo, 0 saca la entrada tal cual, y lo de en medio es la mezcla. Un
 * par de multiplicaciones por muestra contra 2 kB de RAM que esta radio no
 * tiene -quedan 5.676 libres-.
 *
 * Y se puede mezclar AQUI, antes del Hilbert y de los diezmadores, porque
 * todo lo que viene detras es lineal: mezclar antes da lo mismo que mezclar
 * despues. Si algun dia deja de ser lineal, esto deja de valer.
 *
 * EL LAZO CORRE IGUAL CON mez=0. Eso no es un detalle: es lo que permite
 * que el enganche se note y se vuelva solo a la demodulacion sincrona en
 * cuanto la portadora entre en el margen. Un lazo parado no se entera de
 * nada.
 */
static void sam_nucleo(sam_t *s, const float32_t *i_in, const float32_t *q_in,
                       float32_t *salida, float32_t *cuad, uint32_t n,
                       float32_t mez)
{
    /* Todo en locales: es la otra mitad del arreglo. Ver sam_bloque(). */
    uint32_t fase = s->fase;
    int32_t  inc  = s->inc;
    float32_t om2 = s->omega2;
    float32_t fil = s->fil_out;
    float32_t del = s->del_out;
    float32_t a0  = s->acum0;
    float32_t a1  = s->acum1;
    float32_t dc  = s->dc;
    float32_t dci = s->dc_insert;
    float32_t dm  = s->det_med;
    float32_t det = s->det;
    const float32_t g1 = s->g1;
    const float32_t g2dr = s->g2 * (float32_t)SAM_DR;
    const float32_t omin = s->omega_min, omax = s->omega_max;
    const float32_t mR = s->mtauR, nR = s->onem_mtauR;
    const float32_t mI = s->mtauI, nI = s->onem_mtauI;
    const float32_t a_det = s->a_det;
    const uint8_t nivela = s->fade_leveler;
    uint8_t cuenta = s->cuenta;
    uint32_t k;
    const float32_t unm = 1.0f - mez;
    float32_t c0 = s->corr0, c1 = s->corr1, au = s->audio;

    for (k = 0U; k < n; k++) {
        float32_t sen, cos_, i_v = i_in[k], q_v = q_in[k];

        /* De la tabla del NCO, en linea y con la fase ya en su rejilla:
         * ver nco_sen_cos_fase() en nco.h. */
        nco_sen_cos_fase(fase, &sen, &cos_);
        fase += (uint32_t)inc;

        /* Same quadrature mixing + phase-detector combination as the
         * original SAM()'s ai/bi/aq/bq/corr[0]/corr[1] - Hilbert-derived
         * ai_ps/bi_ps/aq_ps/bq_ps terms omitted (SAML/SAMU only, not this
         * mode - see sam.h's own top comment). */
        c0 =  cos_ * i_v + sen * q_v;
        c1 = -sen  * i_v + cos_ * q_v;

        /* DEMOD_SAM case only (both sidebands) - matches the original
         * switch's `case DEMOD_SAM: audio = corr[0];` exactly. */
        au = c0;
        if (nivela) {
            dc  = mR * dc  + nR * au;
            dci = mI * dci + nI * c0;
            au  = au + dci - dc;
        }
        salida[k] = mez * au + unm * i_v;
        /*
         * LA COMPONENTE EN CUADRATURA, PARA QUIEN QUIERA UNA SOLA BANDA -
         * 11/10/2026.
         *
         * c1 ya se calcula aqui -lo necesita el detector de fase del PLL-
         * y hasta hoy solo vivia dentro de este bucle. Sacarlo no cuesta
         * nada y es LA MITAD QUE FALTA: con c0 y c1 se tiene la senal
         * desrotada por la portadora recuperada, que es exactamente lo que
         * come la cadena de banda lateral de demod_am.c. Elegir banda es
         * entonces el metodo de fases de siempre, c0 +- Hilbert(c1), con el
         * Hilbert que esta radio ya tiene verificado.
         *
         * La cabecera de sam.h decia desde agosto que esto se dejaba para
         * "cuando haya coeficientes Hilbert verificados". Los hay desde que
         * existe el modo de banda lateral; lo que faltaba era darse cuenta.
         *
         * NULL cuando no se quiere: el modo de dos bandas no paga ni una
         * escritura.
         */
        if (cuad != (float32_t *)0) { cuad[k] = mez * c1 + unm * q_v; }

        a0 += c0;
        a1 += c1;

        if (--cuenta == 0U) {
            cuenta = (uint8_t)SAM_DR;

            /* El arcotangente sale de rapido.c y no de la biblioteca: el
             * error que mete -seis decimas de grado- se lo come el
             * integrador del lazo, y sim/samtest.c lo demuestra corriendo
             * este mismo PLL con las dos versiones sobre la misma señal. */
            det = rapido_atan2(a1, a0);
            a0 = 0.0f;
            a1 = 0.0f;

            dm += a_det * ((det < 0.0f ? -det : det) - dm);

            /* Loop filter + phase accumulator - misma estructura que el
             * original (del_out/omega2/fil_out), con g2 escalado por SAM_DR:
             * ver el comentario de aqui arriba. */
            del = fil;
            om2 = om2 + g2dr * det;
            if (om2 < omin)      { om2 = omin; }
            else if (om2 > omax) { om2 = omax; }
            fil = g1 * det + om2;

            /* del esta en radianes por muestra; a la rejilla de la fase. */
            inc = (int32_t)(del * SAM_K_FASE);
        }
    }

    s->fase = fase;
    s->inc = inc;
    s->omega2 = om2;
    s->fil_out = fil;
    s->del_out = del;
    s->acum0 = a0;
    s->acum1 = a1;
    s->dc = dc;
    s->dc_insert = dci;
    s->det_med = dm;
    s->det = det;
    s->cuenta = cuenta;
    s->corr0 = c0;
    s->corr1 = c1;
    s->audio = au;
}

/*
 * LA LECTURA DE FRECUENCIA, UNA VEZ POR LLAMADA Y NO POR MUESTRA.
 *
 * Antes estaba dentro del bucle, con una DIVISION en coma flotante incluida
 * -catorce ciclos- para un numero que solo mira una persona en una pantalla
 * que se repinta treinta veces por segundo. Ahora sale una vez, y el
 * suavizado es de verdad: ver carrier_hz en sam.h.
 *
 * DF (el factor extra de diezmado del proyecto original) es 1 aqui, asi que
 * se omite en vez de arrastrarse como una multiplicacion por uno.
 */
static void sam_lectura(sam_t *s, uint32_t n, float32_t sample_rate_hz)
{
    float32_t a;

    s->carrier_hz_raw = (s->omega2 * sample_rate_hz) * (1.0f / SAM_TPI);

    a = (float32_t)n / (sample_rate_hz * SAM_TAU_S);
    if (a > 1.0f) { a = 1.0f; }
    s->carrier_hz += a * (s->carrier_hz_raw - s->carrier_hz);

    /* Y el si/no, con sus dos umbrales. Va aqui y no en sam_enganchado()
     * porque aquella la llama la pantalla -y una consulta no puede cambiar
     * el estado de nada-, mientras que esto corre una vez por bloque, que es
     * al ritmo al que el dato se produce. */
    {
        float32_t v = sam_enganche(s);
        if (s->eng) { if (v < SAM_ENG_SALE)  { s->eng = 0U; } }
        else        { if (v >= SAM_ENG_ENTRA) { s->eng = 1U; } }
    }

    /*
     * Y SI ESTA CLAVADO CONTRA EL TOPE. Ver sam_tope() en sam.h: esto es
     * lo que separa "no hay portadora" de "la hay y no llego".
     *
     * El 0,98 no es un margen de seguridad numerico, es lo que hace que
     * cuente como tope estar PEGADO a el: el integrador recortado se queda
     * exactamente en omega_max, pero el que viene empujando desde fuera
     * oscila un poco por debajo. Medido con una portadora a 300 Hz y
     * margen 250: se queda en +244, o sea al 97,6 %.
     *
     * Medio segundo de bloques seguidos -la misma escala que la media del
     * detector- antes de darlo por bueno. Un lazo buscando toca el tope de
     * pasada constantemente.
     */
    {
        /*
         * EL 0,90 Y LA MEDIA SALEN DE MEDIR, Y LA PRIMERA VERSION FALLABA.
         *
         * Pedia estar pegado al 98 % del tope durante medio segundo
         * SEGUIDO, y se le escapaba justo el caso que mas lejos esta.
         * Medido -cuatro segundos por desvio, % del tiempo pegado-:
         *
         *     desvio  300 Hz   >=99,9%: 100 %
         *     desvio  400 Hz   >=99,9%: 100 %
         *     desvio  500 Hz   >=99,9%:  30 %   >=90%: 100 %
         *     desvio 1000 Hz   >=99,9%:   7 %   >=90%: 100 %
         *     solo ruido                        >=90%: 3,4 %
         *
         * Cuanto MAS lejos esta la portadora, menos se queda clavado: el
         * detector de fase gira tan deprisa que el lazo rebota alrededor
         * del tope en vez de apoyarse en el. O sea que la racha seguida
         * medía al reves de como se comporta esto.
         *
         * Con el 90 % y una MEDIA en vez de una racha, los cuatro desvios
         * dan el 100 % y el ruido el 3,4 %: separa de sobra.
         */
        const float32_t cerca = 0.90f;
        uint8_t donde = 0U;
        float32_t ind;

        /*
         * CON VUELO NO SE MIRA, y no es un detalle: el lazo esta congelado
         * donde lo dejo la emisora, y si la dejo cerca del tope -una
         * portadora a 240 con margen 250- la fila diria "fuera de alcance"
         * cuando lo que ha pasado es que la señal se ha desvanecido. Son
         * dos motivos distintos con el mismo sintoma, y el que sabe cual
         * es, es este.
         */
        if (s->omega2 >= s->omega_max * cerca)      { donde = 1U; }
        else if (s->omega2 <= s->omega_min * cerca) { donde = 2U; }

        /* Cambiar de lado tira la media: son dos cosas distintas y
         * promediarlas juntas daria un numero que no es ninguna. */
        if ((donde != s->tope_lado) && (donde != 0U)) {
            s->tope_prom = 0.0f;
        }
        s->tope_lado = donde;

        ind = (donde != 0U) ? 1.0f : 0.0f;
        {
            /* Medio segundo de constante de tiempo. Se calcula con la
             * tasa y el tamaño de bloque porque sam.c no conoce ninguno
             * de los dos de antemano. */
            float32_t a = (float32_t)n / (sample_rate_hz * 0.5f);
            if (a > 1.0f) { a = 1.0f; }
            s->tope_prom += a * (ind - s->tope_prom);
        }

        /* Histeresis, por lo mismo que la tiene el si/no del enganche: un
         * cartel que parpadea no se lee, se ignora. */
        if (s->tope != 0U) {
            if ((s->tope_prom < 0.5f) || (donde == 0U && s->tope_prom < 0.5f)) {
                s->tope = 0U;
            } else if (donde != 0U) {
                s->tope = donde;
            }
        } else if ((s->tope_prom >= 0.8f) && (donde != 0U)) {
            s->tope = donde;
        }
    }
}

void sam_bloque(sam_t *s, const float32_t *i_in, const float32_t *q_in,
                float32_t *audio, uint32_t n, float32_t sample_rate_hz)
{
    if (n == 0U) { return; }
    sam_nucleo(s, i_in, q_in, audio, (float32_t *)0, n, 1.0f);
    sam_lectura(s, n, sample_rate_hz);
}

/*
 * Lo mismo, pero sacando tambien la cuadratura. MISMO CUERPO: no hay una
 * segunda copia del PLL, igual que sam_step() y sam_bloque() tampoco la
 * tienen. Ver el comentario de `cuad` dentro de sam_nucleo().
 */
void sam_bloque_cuad(sam_t *s, const float32_t *i_in, const float32_t *q_in,
                     float32_t *audio, float32_t *cuad, uint32_t n,
                     float32_t sample_rate_hz)
{
    sam_bloque_cuad_mez(s, i_in, q_in, audio, cuad, n, sample_rate_hz, 1.0f);
}

/*
 * Lo mismo, cruzando hacia la entrada SIN DESROTAR segun `mez`. Ver el
 * comentario de `mez` encima de sam_nucleo(): una sola copia del lazo,
 * tambien para esto.
 */
void sam_bloque_cuad_mez(sam_t *s, const float32_t *i_in, const float32_t *q_in,
                         float32_t *audio, float32_t *cuad, uint32_t n,
                         float32_t sample_rate_hz, float32_t mez)
{
    if (n == 0U) { return; }
    if (mez > 1.0f) { mez = 1.0f; }
    if (mez < 0.0f) { mez = 0.0f; }
    sam_nucleo(s, i_in, q_in, audio, cuad, n, mez);
    sam_lectura(s, n, sample_rate_hz);
}

float32_t sam_step(sam_t *s, float32_t i_in, float32_t q_in, float32_t sample_rate_hz)
{
    float32_t fuera;

    sam_nucleo(s, &i_in, &q_in, &fuera, (float32_t *)0, 1U, 1.0f);
    sam_lectura(s, 1U, sample_rate_hz);
    return fuera;
}

float32_t sam_enganche(const sam_t *s)
{
    /* pi/2 es lo que da la media de |det| sin portadora; de ahi para abajo
     * es señal. Se normaliza para que lo que salga sea 0..1 y no un angulo
     * que haya que saber interpretar. */
    float32_t v = 1.0f - s->det_med / (SAM_TPI * 0.25f);

    if (v < 0.0f) { v = 0.0f; }
    if (v > 1.0f) { v = 1.0f; }
    return v;
}

uint8_t sam_enganchado(const sam_t *s)
{
    return s->eng;
}

int8_t sam_tope(const sam_t *s)
{
    if (s->tope == 1U) { return  1; }
    if (s->tope == 2U) { return -1; }
    return 0;
}
