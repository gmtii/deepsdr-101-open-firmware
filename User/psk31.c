#include "psk31.h"
#include <math.h>
#include <string.h>

/*
 * ===================================================================
 *  EL CAMINO, DE PRINCIPIO A FIN
 * ===================================================================
 *
 *   12000 Hz  ->  girar un fasor a -f0  ->  I,Q en banda base
 *             ->  sumar de doce en doce ->  1000 Hz
 *             ->  suma corrida de 32    ->  filtro adaptado
 *             ->  buscar el hueco       ->  reloj de simbolo
 *             ->  z[k] * conj(z[k-1])   ->  el bit
 *             ->  varicode              ->  el caracter
 *
 * Las tres decisiones que importan, y por que:
 *
 * 1. NO HAY RECUPERACION DE PORTADORA. La deteccion es DIFERENCIAL: se
 *    compara cada simbolo con el anterior, y lo que se mira es si la fase
 *    ha dado la vuelta. Eso es justo lo que PSK31 codifica, asi que no
 *    hace falta saber cual es la fase absoluta - ni engancharla, ni
 *    mantenerla, ni perderla cuando la ionosfera la mueve. El precio es
 *    quedarse a ~3 dB de lo que daria una deteccion coherente perfecta, y
 *    aguantar solo +-7,8 Hz de error de frecuencia.
 *
 * 2. EL FILTRO ADAPTADO ES UNA SUMA DE UN SIMBOLO EXACTO. 32 muestras a
 *    1000 Hz son 1/31,25 s clavados. Una suma corrida de esa longitud
 *    tiene CEROS en +-31,25 Hz, +-62,5 Hz..., o sea justo donde estan las
 *    emisiones de al lado: en 14.070 hay una cada 50 Hz. Y cuesta O(1) por
 *    muestra -entra una, sale otra-, no 32 multiplicaciones.
 *
 * 3. EL RELOJ LO SACA UN GARDNER, NO UN HISTOGRAMA DE ENERGIA.
 *
 *    PSK31 no salta de fase de golpe: la amplitud baja a cero con forma de
 *    coseno, da la vuelta abajo, y sube. Eso deja un hueco de energia justo
 *    en la frontera entre simbolos, y la primera version de esto buscaba
 *    ese hueco promediando la energia en dieciseis casillas dentro del
 *    simbolo y quedandose con la mas floja.
 *
 *    NO FUNCIONO, y el motivo merece quedarse escrito porque es el tipico
 *    que no se ve mirando el codigo: al corregir la fase, las casillas
 *    seguian donde estaban. O sea que despues de mover el reloj un paso,
 *    el promedio acumulado seguia enseñando el minimo en el sitio VIEJO, y
 *    el lazo volvia a empujar en la misma direccion. Se iba solo, y cada
 *    pocos caracteres se comia o repetia un simbolo entero. En el banco se
 *    veia clavado: la tira de bits recibida era la mandada con un bit
 *    quitado en el sitio 4, luego otro metido mas adelante. El texto salia
 *    con los espacios en su sitio -esos son un solo bit- y las letras
 *    cambiadas.
 *
 *    El Gardner no tiene ese problema porque no acumula nada: mira tres
 *    muestras -el centro de este simbolo, el del anterior y el punto medio
 *    entre los dos- y de ahi sale el error de fase directamente:
 *
 *        e = Re{ (y[k] - y[k-1]) * conj(y[k-1/2]) }
 *
 *    Si se esta muestreando en el sitio, el punto medio cae en el hueco y
 *    vale cero, asi que e vale cero pase lo que pase con los datos. Si se
 *    va tarde o pronto, el punto medio recoge parte de la transicion y e
 *    sale con el signo del desvio. Y no hace falta saber que bit es: por
 *    eso vale igual con datos que con el reposo.
 */

#define FS_HZ        12000.0f
#define DIEZMA       12U                  /* 12000 / 12 = 1000 Hz */
#define FS_BAJA      1000.0f
#define SPS          32U                  /* 1000 / 31,25 = 32 clavados */
#define BAUDIO       (FS_BAJA / (float)SPS)

/*
 * EL VARICODE, Y POR QUE ESTA GUARDADO COMO UN SOLO NUMERO.
 *
 * Cada caracter es una tira de unos y ceros de longitud variable -de 1 bit
 * el espacio a 10 bits los caracteres de control- que NUNCA lleva dos
 * ceros seguidos y siempre empieza y acaba en uno. El separador entre
 * caracteres es precisamente "00", que por eso no puede aparecer dentro.
 * Las letras frecuentes son cortas: 'e' son dos bits, ' ' uno solo.
 *
 * Guardar la tira como texto costaria once bytes por caracter. Aqui va
 * como un entero con un uno de centinela delante:
 *
 *     clave = (1 << longitud) | valor
 *
 * que es unico -lo comprueba el banco- y ademas es EXACTAMENTE lo que se
 * va formando solo en el registro del receptor segun entran los bits. O
 * sea que descodificar es formar el numero y buscarlo: no hay que contar
 * longitudes ni recortar nada.
 *
 * La tabla esta comprobada identica en fldigi (src/psk/pskvaricode.cxx),
 * SDRangel (sdrbase/util/psk31.cpp) y LinPSK (src/constants.h).
 */
static const uint16_t k_varicode[128] = {
    0x6ABU, 0x6DBU, 0x6EDU, 0x777U,   /* 00-03 */
    0x6EBU, 0x75FU, 0x6EFU, 0x6FDU,   /* 04-07 */
    0x6FFU, 0x1EFU, 0x03DU, 0x76FU,   /* 08-0B */
    0x6DDU, 0x03FU, 0x775U, 0x7ABU,   /* 0C-0F */
    0x6F7U, 0x6F5U, 0x7ADU, 0x7AFU,   /* 10-13 */
    0x75BU, 0x76BU, 0x76DU, 0x757U,   /* 14-17 */
    0x77BU, 0x77DU, 0x7B7U, 0x755U,   /* 18-1B */
    0x75DU, 0x7BBU, 0x6FBU, 0x77FU,   /* 1C-1F */
    0x003U, 0x3FFU, 0x35FU, 0x3F5U,   /* 20-23 */
    0x3DBU, 0x6D5U, 0x6BBU, 0x37FU,   /* 24-27 */
    0x1FBU, 0x1F7U, 0x36FU, 0x3DFU,   /* 28-2B */
    0x0F5U, 0x075U, 0x0D7U, 0x3AFU,   /* 2C-2F */
    0x1B7U, 0x1BDU, 0x1EDU, 0x1FFU,   /* 30-33 */
    0x377U, 0x35BU, 0x36BU, 0x3ADU,   /* 34-37 */
    0x3ABU, 0x3B7U, 0x1F5U, 0x3BDU,   /* 38-3B */
    0x3EDU, 0x0D5U, 0x3D7U, 0x6AFU,   /* 3C-3F */
    0x6BDU, 0x0FDU, 0x1EBU, 0x1ADU,   /* 40-43 */
    0x1B5U, 0x0F7U, 0x1DBU, 0x1FDU,   /* 44-47 */
    0x355U, 0x0FFU, 0x3FDU, 0x37DU,   /* 48-4B */
    0x1D7U, 0x1BBU, 0x1DDU, 0x1ABU,   /* 4C-4F */
    0x1D5U, 0x3DDU, 0x1AFU, 0x0EFU,   /* 50-53 */
    0x0EDU, 0x357U, 0x3B5U, 0x35DU,   /* 54-57 */
    0x375U, 0x37BU, 0x6ADU, 0x3F7U,   /* 58-5B */
    0x3EFU, 0x3FBU, 0x6BFU, 0x36DU,   /* 5C-5F */
    0x6DFU, 0x01BU, 0x0DFU, 0x06FU,   /* 60-63 */
    0x06DU, 0x007U, 0x07DU, 0x0DBU,   /* 64-67 */
    0x06BU, 0x01DU, 0x3EBU, 0x1BFU,   /* 68-6B */
    0x03BU, 0x07BU, 0x01FU, 0x00FU,   /* 6C-6F */
    0x07FU, 0x3BFU, 0x035U, 0x037U,   /* 70-73 */
    0x00DU, 0x077U, 0x0FBU, 0x0EBU,   /* 74-77 */
    0x1DFU, 0x0DDU, 0x3D5U, 0x6B7U,   /* 78-7B */
    0x3BBU, 0x6B5U, 0x6D7U, 0x7B5U    /* 7C-7F */
};

/* ------------------------------------------------------------------ */

#define SALIDA_N 64U                      /* anillo de caracteres */

static uint8_t  s_on;

/* el fasor que baja a banda base, girado por multiplicacion */
static float s_osc_r, s_osc_i;            /* e^(-j w t) */
static float s_rot_r, s_rot_i;            /* e^(-j w)   */
static float s_centro_hz = 1000.0f;       /* lo que pide el usuario */
static float s_afc_hz;                    /* lo que le suma el seguimiento */
static uint16_t s_renorm;

/* suma de doce muestras -> una a 1000 Hz */
static float    s_ac_r, s_ac_i;
static uint8_t  s_ac_n;

/*
 * FILTRO ADAPTADO: UNA SUMA CORRIDA DE 32, Y ESO SE MIDIO.
 *
 * 32 muestras a 1000 Hz son 1/31,25 s clavados. La suma corrida de esa
 * longitud tiene CEROS en +-31,25 Hz, +-62,5 Hz..., o sea justo donde estan
 * las emisiones de al lado -en 14.070 hay una cada 50 Hz- y cuesta O(1) por
 * muestra: entra una, sale otra.
 *
 * PERO NO ES EL FILTRO ADAPTADO DE VERDAD, y conviene dejar escrito por que
 * se queda igual. El pulso de PSK31 dura DOS simbolos -0,5*(1 - cos(pi*u))
 * con u de 0 a 2, una ventana de Hann de 2T, y los simbolos se solapan medio
 * pulso-, asi que el filtro adaptado es esa misma ventana de 64 muestras.
 *
 * Se midio, el 30/09/2026, con sim/psk31_aire.c en modo `medir`: la misma
 * emision, el mismo ruido y el mismo vecino, cuatro tandas de semillas
 * distintas para tener barra de error. Tres formas:
 *
 *                              sensibilidad (50 %)    vecino a +31,25 Hz
 *   rectangulo de 32 (esta)      -11,85 dB              +4,7 dB
 *   coseno de 32                 -11,3  dB              -0,7 dB
 *   coseno de 64 (el adaptado)   -11,97 dB              +5,0 dB
 *
 * El coseno de 32 es CLARAMENTE peor en las dos cosas: no es ningun filtro
 * adaptado, es una ventana que no cuadra con nada.
 *
 * Y el adaptado de verdad gana 0,12 dB de sensibilidad -consistente: las
 * cuatro tandas le dan el mismo signo- y unos 0,3 dB de rechazo al vecino,
 * que ya baila de signo entre tandas y por tanto no se puede afirmar. 0,12
 * dB son un 3 % en amplitud. En onda corta el desvanecimiento mueve la señal
 * decenas de dB en segundos.
 *
 * O sea que cuesta 64 multiplicaciones por muestra en vez de dos sumas, mas
 * 256 bytes de RAM, para ganar algo que no se puede notar. Se queda el
 * rectangulo. Esto no es "no se hizo", es "se midio y no compensaba", y por
 * eso el numero esta aqui: para no volver a abrirlo.
 */
static float    s_anillo_r[SPS], s_anillo_i[SPS];
static uint8_t  s_anillo_p;
static float    s_suma_r, s_suma_i;

/*
 * RELOJ DE SIMBOLO. `s_mu` es la fase dentro del simbolo (0..1) y `s_inc`
 * lo que avanza en cada muestra de 1000 Hz. El nominal es 1/32 clavado;
 * el lazo lo mueve para seguir la deriva del reloj del otro, que en HF
 * puede irse unas decenas de ppm.
 */
static float s_mu;
static float s_inc = 1.0f / (float)SPS;
static float s_med_r, s_med_i;      /* y[k-1/2]: el punto medio */
static float s_cur_r, s_cur_i;      /* y[k-1]: el simbolo anterior */
static uint8_t s_arranca;           /* 0 hasta que hay dos simbolos */

/* deteccion diferencial */
static float s_ant_r, s_ant_i;
static float s_cal;                       /* 0..1, lo redonda que es la fase */
/*
 * Y LA SEGUNDA PUERTA, QUE MIRA OTRA COSA. `s_vc` es la parte de los
 * codigos formados que existen de verdad en el varicode. Hace falta
 * porque hay un caso en que la fase sale PERFECTA y el texto es basura:
 * cuando la sintonia esta a medio baudio (15,6 Hz), la vuelta de fase de
 * la portadora imita exactamente la vuelta de fase de los datos, y los
 * dos unicos valores que PSK31 admite se intercambian. La calidad de
 * fase no puede distinguirlo -las dos cosas se ven igual de limpias- y en
 * el banco salia `cal 99` escribiendo "Pe ete_h T eDea e ".
 *
 * El varicode si lo distingue, y gratis: una tira de bits que no es
 * varicode forma claves que no estan en la tabla. Con señal buena casi
 * todas estan; con la sintonia en el sitio equivocado, casi ninguna.
 *
 * Empieza en 1 -optimista- para que las primeras letras de una emision no
 * se pierdan esperando a que suba.
 */
static float s_vc = 1.0f;

/* seguimiento de frecuencia */
static float   s_afc_r, s_afc_i, s_afc_m;
static uint8_t s_afc_n;
static float   s_afc_err_hz;

/* varicode en curso */
static uint16_t s_acc = 1U;
static uint8_t  s_ceros = 2U;

/* salida */
static char    s_salida[SALIDA_N];
static uint8_t s_sal_mete, s_sal_saca;

/* ------------------------------------------------------------------ */

static void rot_calcula(void)
{
    float w = 6.283185307f * (s_centro_hz + s_afc_hz) / FS_HZ;

    s_rot_r = cosf(w);
    s_rot_i = -sinf(w);
}

static void limpia(void)
{
    uint8_t i;

    s_osc_r = 1.0f; s_osc_i = 0.0f; s_renorm = 0U;
    s_ac_r = 0.0f;  s_ac_i = 0.0f;  s_ac_n = 0U;
    for (i = 0U; i < (uint8_t)SPS; i++) { s_anillo_r[i] = 0.0f; s_anillo_i[i] = 0.0f; }
    s_anillo_p = 0U; s_suma_r = 0.0f; s_suma_i = 0.0f;
    s_mu = 0.0f; s_inc = 1.0f / (float)SPS;
    s_med_r = 0.0f; s_med_i = 0.0f;
    s_cur_r = 0.0f; s_cur_i = 0.0f;
    s_arranca = 0U;
    s_ant_r = 0.0f; s_ant_i = 0.0f; s_cal = 0.0f; s_vc = 1.0f;
    s_afc_r = 0.0f; s_afc_i = 0.0f; s_afc_m = 0.0f; s_afc_n = 0U;
    s_afc_hz = 0.0f; s_afc_err_hz = 0.0f;
    s_acc = 1U; s_ceros = 2U;
    s_sal_mete = 0U; s_sal_saca = 0U;
    rot_calcula();
}

void psk31_start(float fs_hz)
{
    /* Se pasa la frecuencia para COMPROBARLA, no para adaptarse: las
     * constantes de arriba (doce, treinta y dos) solo cuadran a 12 kHz. */
    if (fs_hz < 11999.0f || fs_hz > 12001.0f) { return; }
    limpia();
    s_on = 1U;
}

void psk31_stop(void)      { s_on = 0U; }
uint8_t psk31_activo(void) { return s_on; }

void psk31_set_centro_hz(float hz)
{
    if (hz < 200.0f)  { hz = 200.0f; }
    if (hz > 3000.0f) { hz = 3000.0f; }
    s_centro_hz = hz;
    s_afc_hz = 0.0f;       /* mover el centro a mano anula el seguimiento */
    rot_calcula();
}

float psk31_get_centro_hz(void) { return s_centro_hz; }

void psk31_diag(uint8_t *cal, int16_t *hz_e100)
{
    if (cal != (uint8_t *)0) {
        float v = s_cal * 100.0f;

        if (v < 0.0f)   { v = 0.0f; }
        if (v > 100.0f) { v = 100.0f; }
        *cal = (uint8_t)v;
    }
    if (hz_e100 != (int16_t *)0) {
        float v = (s_afc_hz + s_afc_err_hz) * 100.0f;

        if (v < -32000.0f) { v = -32000.0f; }
        if (v >  32000.0f) { v =  32000.0f; }
        *hz_e100 = (int16_t)v;
    }
}

uint8_t psk31_get_char(char *out)
{
    if (s_sal_saca == s_sal_mete) { return 0U; }
    if (out != (char *)0) { *out = s_salida[s_sal_saca]; }
    s_sal_saca = (uint8_t)((s_sal_saca + 1U) % SALIDA_N);
    return 1U;
}

static void saca(char c)
{
    uint8_t n = (uint8_t)((s_sal_mete + 1U) % SALIDA_N);

    if (n == s_sal_saca) { return; }     /* lleno: se tira, no se pisa */
    s_salida[s_sal_mete] = c;
    s_sal_mete = n;
}

/*
 * UN BIT MAS. El separador "00" es lo unico que hay: cuando llega el
 * segundo cero seguido, lo que se haya formado en s_acc es el caracter -
 * menos el PRIMER cero, que ya es del separador y hay que devolverlo.
 */
static void bit_mete(uint8_t b)
{
#ifdef PSK31_DIAG_BITS
    /* Igual que CONFIG_RTTY_DIAG_ENABLED: apagado en la radio, encendido
     * en el banco cuando hay que ver la tira de bits en crudo. */
    extern void psk31_diag_bit(uint8_t b);
    psk31_diag_bit(b);
#endif
    if (b) {
        s_acc = (uint16_t)((s_acc << 1) | 1U);
        if (s_acc > 0x7FFU) { s_acc = 1U; }   /* mas largo que el varicode: basura */
        s_ceros = 0U;
        return;
    }

    if (s_ceros == 0U) {
        s_acc = (uint16_t)(s_acc << 1);
        /*
         * OJO CON EL LIMITE DE AQUI: no es el mismo que el de la rama del
         * uno. Este cero todavia no se sabe si es del codigo o del hueco,
         * y se mete igual porque no hay forma de saberlo hasta que llegue
         * el siguiente bit. Asi que el registro admite UN BIT MAS que el
         * codigo mas largo: diez bits de codigo son una clave de once, y
         * con el cero de prestado, doce.
         *
         * Con el limite de la otra rama (0x7FF) se comia la ultima letra
         * de cada mensaje siempre que fuera de las largas: en el banco
         * salia "CQ CQ DE EA4XY" donde se habia metido "...EA4XYZ", y
         * solo la Z, que son diez bits, faltaba. Las letras frecuentes
         * -que son cortas- no lo destapaban.
         */
        if (s_acc > 0xFFFU) { s_acc = 1U; }
        s_ceros = 1U;
    } else if (s_ceros == 1U) {
        uint16_t clave = (uint16_t)(s_acc >> 1);   /* fuera el cero del hueco */

        s_ceros = 2U;
        s_acc = 1U;
        if (clave > 1U) {
            uint8_t i;
            uint8_t hay = 0U;

            for (i = 0U; i < 128U; i++) {
                if (k_varicode[i] == clave) { hay = 1U; break; }
            }
            s_vc += ((hay ? 1.0f : 0.0f) - s_vc) * 0.15f;
            /*
             * El caracter SOLO sale si ademas el varicode viene
             * cuadrando. Ver el comentario de s_vc: sin esto, una
             * sintonia a medio baudio escribe texto con toda la pinta de
             * estar bien.
             */
            if (hay && s_vc >= 0.5f) { saca((char)i); }
        }
    } else {
        /* reposo: mas ceros seguidos, nada que hacer */
    }
}

/*
 * UN SIMBOLO. Aqui se decide el bit, se mide la calidad y se corrige la
 * frecuencia. Treinta y una veces por segundo.
 */
static void simbolo(float zr, float zi)
{
    float dr = zr * s_ant_r + zi * s_ant_i;      /* Re{z * conj(z_ant)} */
    float di = zi * s_ant_r - zr * s_ant_i;      /* Im{...}             */
    float m  = sqrtf(dr * dr + di * di);

    s_ant_r = zr; s_ant_i = zi;

    if (m > 1e-9f) {
        float q = fabsf(dr) / m;

        /*
         * LA CALIDAD. Para un PSK31 de verdad el giro entre simbolos es 0
         * o 180 grados, asi que |Re| / |z| vale 1. Para ruido, el angulo
         * es cualquiera y el promedio de |cos| sale 2/pi = 0,637. De ahi
         * el corte en PSK31_CAL_MIN: por debajo, lo que hay no es PSK31.
         */
        s_cal += (q - s_cal) * 0.05f;

        /*
         * Y LA FRECUENCIA. Elevar al cuadrado borra la modulacion: los
         * dos valores que admite PSK31 (0 y 180) se convierten en el
         * mismo (0 y 360). Lo que queda del angulo es el error de
         * frecuencia, dos veces. Se acumula sin normalizar a proposito,
         * para que los simbolos fuertes pesen mas que los debiles.
         */
        s_afc_r += dr * dr - di * di;
        s_afc_i += 2.0f * dr * di;
        s_afc_m += m * m;
    }

    if (s_cal * 100.0f >= (float)PSK31_CAL_MIN) {
        bit_mete((uint8_t)(dr >= 0.0f ? 1U : 0U));
    } else {
        s_acc = 1U; s_ceros = 2U;    /* sin señal, el caracter a medias se tira */
    }

    s_afc_n++;
    if (s_afc_n >= 32U) {
        /*
         * hypotf() Y NO sqrtf(a*a + b*b) - 30/09/2026.
         *
         * Los dos acumuladores llevan |z|^4 sumado treinta y dos veces, y
         * |z| es la salida del filtro adaptado: treinta y dos muestras que
         * a su vez son sumas de doce. Con el audio a la escala de la radio
         * (AGC_TARGET = 18000) eso son |z| ~ 6,9e6, |d| ~ 4,8e13 y los
         * acumuladores ~7,3e28 - que en float caben-, pero el CUADRADO de
         * uno de ellos son 5,3e57 y FLT_MAX es 3,4e38.
         *
         * O sea que `mod` salia +inf y `coh = mod / s_afc_m` tambien, asi
         * que la puerta `coh > 0,55` de mas abajo pasaba SIEMPRE. Dos
         * consecuencias, las dos malas: el seguimiento de frecuencia
         * corregia tambien con ruido o con voz, hasta los topes de +-12 Hz,
         * y la rama que devuelve la sintonia a donde dijo el usuario
         * quedaba INALCANZABLE. Es exactamente la regresion que el
         * comentario de aqui abajo dice haber arreglado ("en el banco llego
         * a -17 Hz escuchando ruido puro"): escuchabas ruido un rato y
         * cuando aparecia una emision de verdad el receptor arrancaba
         * desintonizado, con psk31_diag() informando de un desvio inventado.
         *
         * En el banco no se veia porque alli la emision vale amplitud ~1 y
         * el ruido ~1: |z| ~ 400 y los cuadrados caben de sobra. El fallo
         * solo existe a la escala de la radio.
         *
         * hypotf() escala antes de elevar al cuadrado, que es justo lo que
         * hace falta, y esto corre una vez cada treinta y dos simbolos - o
         * sea una vez por segundo-, asi que lo que cueste da igual.
         */
        float mod = hypotf(s_afc_r, s_afc_i);
        float coh = (s_afc_m > 1e-12f) ? (mod / s_afc_m) : 0.0f;
        float ang = atan2f(s_afc_i, s_afc_r);
        float err = ang * BAUDIO * 0.25f / 3.14159265f;   /* ang/(4*pi*T) */

        s_afc_err_hz = err;
        /*
         * LA PUERTA DE AQUI NO PUEDE SER LA CALIDAD, y esto costo un rato
         * de banco. La primera version solo corregia la frecuencia cuando
         * `s_cal` ya pasaba del corte - y `s_cal` esta BAJA precisamente
         * cuando hay error de frecuencia, porque lo que mide es cuanto se
         * aparta el giro de 0 o 180 grados y el desvio de sintonia lo
         * aparta. O sea: no corregia hasta estar sintonizado, que es
         * justo cuando ya no hace falta. En el banco se veia clavado: a
         * 3 Hz entraba (cal 99), a 6 Hz no (cal 35), y el error MEDIDO
         * era correcto en los dos casos: -5,99 Hz con 6 Hz de desvio.
         * Sabia lo que pasaba y no lo arreglaba.
         *
         * Lo que si sirve de puerta es la COHERENCIA: cuanto se parecen
         * entre si los giros de los ultimos treinta y dos simbolos una vez
         * quitada la modulacion. Con señal, todos apuntan al mismo sitio
         * -haya el desvio que haya- y el modulo de la suma se acerca a la
         * suma de los modulos. Con ruido, apuntan a cualquier parte y se
         * cancelan. No depende de la sintonia, que es la unica forma de
         * que sirva para arreglarla.
         */
        if (coh > 0.55f) {
            s_afc_hz += err * 0.5f;
            if (s_afc_hz >  12.0f) { s_afc_hz =  12.0f; }
            if (s_afc_hz < -12.0f) { s_afc_hz = -12.0f; }
            rot_calcula();
        } else if (s_afc_hz != 0.0f) {
            /*
             * SIN SEÑAL, VOLVER A DONDE DIJO EL USUARIO. Sin esto el
             * seguimiento se queda donde le pillo la ultima racha de
             * ruido -en el banco llego a -17 Hz escuchando ruido puro-, y
             * cuando por fin aparece una emision el receptor arranca
             * desintonizado de salida. Vuelve despacio: un 3% cada
             * treinta y dos simbolos, o sea unos tres segundos para
             * deshacer diez hercios.
             */
            s_afc_hz *= 0.97f;
            if (s_afc_hz < 0.01f && s_afc_hz > -0.01f) { s_afc_hz = 0.0f; }
            rot_calcula();
        }
        s_afc_r = 0.0f; s_afc_i = 0.0f; s_afc_m = 0.0f; s_afc_n = 0U;
    }
}

/* Una muestra del filtro adaptado, ya a 1000 Hz. */
static void baja(float zr, float zi)
{
    float ant_mu = s_mu;

    /* suma corrida: entra una, sale la de hace 32. O(1) por muestra. */
    s_suma_r += zr - s_anillo_r[s_anillo_p];
    s_suma_i += zi - s_anillo_i[s_anillo_p];
    s_anillo_r[s_anillo_p] = zr;
    s_anillo_i[s_anillo_p] = zi;
    s_anillo_p = (uint8_t)((s_anillo_p + 1U) % SPS);

    s_mu += s_inc;

    /* el punto medio entre dos simbolos: ahi es donde esta el hueco */
    if (ant_mu < 0.5f && s_mu >= 0.5f) {
        s_med_r = s_suma_r; s_med_i = s_suma_i;
    }

    if (s_mu >= 1.0f) {
        float nr = s_suma_r, ni = s_suma_i;

        s_mu -= 1.0f;

        if (s_arranca) {
            float dr = nr - s_cur_r;
            float di = ni - s_cur_i;
            float e  = dr * s_med_r + di * s_med_i;   /* Re{(y[k]-y[k-1]) conj(y[k-1/2])} */
            float p  = nr * nr + ni * ni + s_cur_r * s_cur_r + s_cur_i * s_cur_i;

            if (p > 1e-9f) {
                e /= p;      /* normalizado: el lazo no puede depender del volumen */
                if (e >  0.5f) { e =  0.5f; }
                if (e < -0.5f) { e = -0.5f; }
                /*
                 * Proporcional para coger la fase e integral para seguir la
                 * deriva. El integral va mil veces mas flojo porque lo que
                 * persigue -el error de reloj del otro- no cambia.
                 */
                s_mu  += 0.10f * e;
                s_inc += 0.0002f * e;
                if (s_inc < (1.0f / (float)SPS) * 0.99f) { s_inc = (1.0f / (float)SPS) * 0.99f; }
                if (s_inc > (1.0f / (float)SPS) * 1.01f) { s_inc = (1.0f / (float)SPS) * 1.01f; }
            }
            simbolo(nr, ni);
        } else {
            s_arranca = 1U;
        }
        s_cur_r = nr; s_cur_i = ni;
    }
}

void psk31_process(const float *audio, uint32_t n)
{
    uint32_t i;

    if (!s_on || audio == (const float *)0) { return; }

    for (i = 0U; i < n; i++) {
        float v = audio[i];
        float nr, ni;

        /* girar el fasor una vuelta de w: cuatro multiplicaciones */
        nr = s_osc_r * s_rot_r - s_osc_i * s_rot_i;
        ni = s_osc_r * s_rot_i + s_osc_i * s_rot_r;
        s_osc_r = nr; s_osc_i = ni;

        s_renorm++;
        if (s_renorm >= 256U) {
            /* El giro por multiplicacion va perdiendo modulo poco a poco.
             * Esto lo devuelve a uno sin raices: primer orden de Newton. */
            float k = 1.5f - 0.5f * (s_osc_r * s_osc_r + s_osc_i * s_osc_i);

            s_osc_r *= k; s_osc_i *= k;
            s_renorm = 0U;
        }

        s_ac_r += v * s_osc_r;
        s_ac_i += v * s_osc_i;
        s_ac_n++;
        if (s_ac_n >= (uint8_t)DIEZMA) {
            baja(s_ac_r, s_ac_i);
            s_ac_r = 0.0f; s_ac_i = 0.0f; s_ac_n = 0U;
        }
    }
}
