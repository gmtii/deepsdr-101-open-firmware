#include "hora_rbu.h"
#include "hora_comun.h"
#include <math.h>

/* ---------------------------------------------------------------------------
 * CONSTANTES. Barridas en sim/rbutest.c; el barrido esta anotado al lado.
 * ------------------------------------------------------------------------- */

#define TONO_0_HZ  100.0f     /* un cero */
#define TONO_1_HZ  312.5f     /* un uno */

/*
 * Paso alto sobre la fase, antes de nada.
 *
 * La fase que llega trae DOS cosas encima de los tonos: un salto de +-pi
 * cada vez que el angulo da la vuelta, y una rampa que crece sin parar
 * porque el oscilador local nunca esta exactamente en frecuencia. Lo primero
 * se quita desenrollando (quedandose con el incremento y metiendolo en
 * (-pi,pi]); lo segundo se quita con este paso alto.
 *
 * Y se hace de una forma concreta que merece explicacion: en vez de
 * acumular la fase y restarle un paso bajo -que obliga a guardar un numero
 * que crece sin limite y que en coma flotante de 32 bits pierde resolucion
 * a las pocas horas-, se integra el incremento CON FUGA. Sale exactamente lo
 * mismo (v = (1-a)*(v + d)) y el numero nunca se va.
 *
 * 20 Hz deja los 100 Hz al 98% y los 312,5 al 99,8%: el desequilibrio entre
 * los dos tonos es del 2%, que no llega a mover ninguna decision.
 */
#define FASE_HP_HZ 20.0f

/*
 * Cada cuantos elementos se revisa cual de las diez hipotesis de alineacion
 * va mejor. 50 elementos son 5 segundos: suficiente para que la buena
 * destaque, y poco para no tardar en enganchar.
 */
#define HIP_VENTANA 50U

/* Y cuanto tiene que ganarle la nueva a la que esta puesta para que valga la
 * pena cambiarse. Sin este margen, dos hipotesis parecidas se turnarian y la
 * trama no se completaria nunca. */
#define HIP_MARGEN 1.10f

/* Por debajo de este contraste no hay RBU, hay ruido. Ver como se calcula en
 * rbu_info(): 0 es "los bits fijos salen al azar" y 1 es "salen todos". */
#define CONTRASTE_MIN 0.25f

#define N_HIP 10U

/* ---------------------------------------------------------------------------
 * ESTADO
 * ------------------------------------------------------------------------- */

static float s_fs = 3000.0f;

/* Frente de fase */
static float s_fase_prev;
static float s_v;            /* fase con fuga: la señal que ven los tonos */
static float s_a_fuga;
static uint8_t s_primera;

/* Goertzel: [hipotesis][tono] */
static float s_g1[N_HIP][2], s_g2[N_HIP][2];
static float s_coef[2];

static uint32_t s_n_paso;     /* muestras por paso de 10 ms */
static uint32_t s_n_en_paso;
static uint8_t  s_paso;       /* 0..9, cual de los diez pasos del elemento */

/* Eleccion de hipotesis */
static float   s_punt[N_HIP];   /* puntuacion acumulada de cada una */
static uint32_t s_n_punt;
static uint8_t s_hip;           /* la que esta puesta */
static uint8_t s_hip_hecha;     /* ya se ha elegido alguna vez */

/* Trama */
static uint8_t  s_reg;          /* ultimos cinco bits de la hipotesis puesta */
static uint32_t s_idx;          /* elemento actual contando desde el minuto */
static uint8_t  s_enganchado;
static uint8_t  s_d1[60], s_d2[60];

/* Diagnostico. Los bits fijos se cuentan en DOS grupos, los que valen 0 y
 * los que valen 1, y NO juntos. La razon esta en el comentario de donde se
 * calcula el contraste: juntos mienten. */
static uint16_t s_f0_ok, s_f0_n, s_f1_ok, s_f1_n;
static float    s_contraste;
static uint16_t s_ms_marca, s_ms_ciclo, s_raras;
static uint32_t s_n_desde_marca;   /* muestras, para medir el ciclo */

static dcf_estado_t s_estado;
static uint16_t s_ok, s_malas;

static uint8_t s_h, s_m, s_d, s_mes, s_a, s_ds, s_ver;
static uint8_t s_prev_valida, s_prev_h, s_prev_m, s_prev_d;

/* ---------------------------------------------------------------------------
 * ARRANQUE
 * ------------------------------------------------------------------------- */

static void limpia_trama(void)
{
    uint8_t k;
    for (k = 0U; k < 60U; k++) { s_d1[k] = 0U; s_d2[k] = 0U; }
}

void rbu_start(float fs_hz)
{
    uint8_t h;

    s_fs = (fs_hz > 100.0f) ? fs_hz : 100.0f;

    s_coef[0] = 2.0f * cosf(6.2831853f * TONO_0_HZ / s_fs);
    s_coef[1] = 2.0f * cosf(6.2831853f * TONO_1_HZ / s_fs);

    /* Diez pasos de 10 ms por elemento de 100 ms. */
    s_n_paso = (uint32_t)(s_fs / 100.0f + 0.5f);
    if (s_n_paso < 2U) { s_n_paso = 2U; }
    s_n_en_paso = 0U;
    s_paso = 0U;

    s_a_fuga = 6.2831853f * FASE_HP_HZ / s_fs;
    if (s_a_fuga > 1.0f) { s_a_fuga = 1.0f; }
    s_fase_prev = 0.0f;
    s_v = 0.0f;
    s_primera = 1U;

    for (h = 0U; h < N_HIP; h++) {
        s_g1[h][0] = 0.0f; s_g2[h][0] = 0.0f;
        s_g1[h][1] = 0.0f; s_g2[h][1] = 0.0f;
        s_punt[h] = 0.0f;
    }
    s_n_punt = 0U;
    s_hip = 0U;
    s_hip_hecha = 0U;

    s_reg = 0U;
    s_idx = 0U;
    s_enganchado = 0U;
    limpia_trama();

    s_f0_ok = 0U; s_f0_n = 0U; s_f1_ok = 0U; s_f1_n = 0U;
    s_contraste = 0.0f;
    s_ms_marca = 0U; s_ms_ciclo = 0U; s_raras = 0U;
    s_n_desde_marca = 0U;

    s_estado = DCF_BUSCANDO;
    s_ok = 0U; s_malas = 0U;
    s_prev_valida = 0U;
    s_prev_h = 0U; s_prev_m = 0U; s_prev_d = 0U;
}

/* ---------------------------------------------------------------------------
 * DECODIFICACION
 * ------------------------------------------------------------------------- */

/* Valor de un campo del bit de datos 1, dando los segundos y sus pesos. En
 * RBU los pesos van del MAS significativo al menos, al reves que en DCF77. */
static uint8_t campo1(const uint8_t *seg, const uint8_t *peso, uint8_t n)
{
    uint8_t k, v = 0U;
    for (k = 0U; k < n; k++) { if (s_d1[seg[k]]) { v = (uint8_t)(v + peso[k]); } }
    return v;
}

/* Paridad PAR sobre el bit de datos 1, de ini a fin, contra el bit p. */
static uint8_t paridad_ok(uint8_t ini, uint8_t fin, uint8_t p)
{
    uint8_t k, n = 0U;
    for (k = ini; k <= fin; k++) { n = (uint8_t)(n + s_d1[k]); }
    return (uint8_t)((n & 1U) == p);
}

static uint8_t paridad2_ok(uint8_t ini, uint8_t fin, uint8_t p)
{
    uint8_t k, n = 0U;
    for (k = ini; k <= fin; k++) { n = (uint8_t)(n + s_d2[k]); }
    return (uint8_t)((n & 1U) == p);
}

static uint8_t decodifica(void)
{
    static const uint8_t k_min_s[7] = { 53U, 54U, 55U, 56U, 57U, 58U, 59U };
    static const uint8_t k_min_p[7] = { 40U, 20U, 10U,  8U,  4U,  2U,  1U };
    static const uint8_t k_hor_s[6] = { 47U, 48U, 49U, 50U, 51U, 52U };
    static const uint8_t k_hor_p[6] = { 20U, 10U,  8U,  4U,  2U,  1U };
    static const uint8_t k_dia_s[6] = { 41U, 42U, 43U, 44U, 45U, 46U };
    static const uint8_t k_dia_p[6] = { 20U, 10U,  8U,  4U,  2U,  1U };
    static const uint8_t k_sem_s[3] = { 38U, 39U, 40U };
    static const uint8_t k_sem_p[3] = {  4U,  2U,  1U };
    static const uint8_t k_mes_s[5] = { 33U, 34U, 35U, 36U, 37U };
    static const uint8_t k_mes_p[5] = { 10U,  8U,  4U,  2U,  1U };
    static const uint8_t k_ann_s[8] = { 25U, 26U, 27U, 28U, 29U, 30U, 31U, 32U };
    static const uint8_t k_ann_p[8] = { 80U, 40U, 20U, 10U,  8U,  4U,  2U,  1U };
    static const uint8_t k_dut_s[5] = { 19U, 20U, 21U, 22U, 23U };
    static const uint8_t k_dut_p[5] = { 10U,  8U,  4U,  2U,  1U };
    /* Los del bit de datos 1 que valen siempre cero. */
    static const uint8_t k_cero1[8] = { 1U, 2U, 8U, 9U, 10U, 16U, 17U, 24U };

    uint8_t m, h, d, mes, anno, ds, k, verano;
    uint8_t dut, dut_neg;
    int8_t  desp;

    /* --- Lo que no puede valer otra cosa ------------------------------- */
    if (s_d1[0] != 1U || s_d2[0] != 1U) { return 0U; }
    for (k = 0U; k < sizeof(k_cero1); k++) {
        if (s_d1[k_cero1[k]] != 0U) { return 0U; }
    }
    if (s_d2[17] != 0U || s_d2[51] != 0U || s_d2[52] != 0U || s_d2[59] != 0U) {
        return 0U;
    }
    for (k = 34U; k <= 48U; k++) { if (s_d2[k] != 0U) { return 0U; } }

    /* --- Las ocho paridades pares -------------------------------------- */
    /* Seis sobre los campos de hora y fecha... */
    if (!paridad_ok(18U, 24U, s_d2[53])) { return 0U; }   /* P3, diferencia con UTC */
    if (!paridad_ok(25U, 32U, s_d2[54])) { return 0U; }   /* P4, año */
    if (!paridad_ok(33U, 40U, s_d2[55])) { return 0U; }   /* P5, mes y dia de semana */
    if (!paridad_ok(41U, 46U, s_d2[56])) { return 0U; }   /* P6, dia del mes */
    if (!paridad_ok(47U, 52U, s_d2[57])) { return 0U; }   /* P7, hora */
    if (!paridad_ok(53U, 59U, s_d2[58])) { return 0U; }   /* P8, minuto */
    /* ...y dos sobre la fecha juliana, que no se usa pero cuya paridad es
     * gratis y cierra dieciseis bits mas de la trama. */
    if (!paridad2_ok(18U, 25U, s_d2[49])) { return 0U; }  /* P1 */
    if (!paridad2_ok(26U, 33U, s_d2[50])) { return 0U; }  /* P2 */

    /* --- Los campos ---------------------------------------------------- */
    m    = campo1(k_min_s, k_min_p, 7U);
    h    = campo1(k_hor_s, k_hor_p, 6U);
    d    = campo1(k_dia_s, k_dia_p, 6U);
    ds   = campo1(k_sem_s, k_sem_p, 3U);
    mes  = campo1(k_mes_s, k_mes_p, 5U);
    anno = campo1(k_ann_s, k_ann_p, 8U);
    dut     = campo1(k_dut_s, k_dut_p, 5U);
    dut_neg = s_d1[18];

    if (m > 59U) { return 0U; }
    if (h > 23U) { return 0U; }
    if (mes < 1U || mes > 12U) { return 0U; }
    if (d < 1U || d > hora_dias_del_mes(mes, anno)) { return 0U; }
    if (ds < 1U || ds > 7U) { return 0U; }
    if (dut > 12U) { return 0U; }   /* nadie esta a mas de 12 horas de UTC */

    /* El dia de la semana que dice la trama tiene que ser el que le toca a
     * esa fecha. Es una comprobacion gratis y bastante fuerte: deja pasar
     * una de cada siete fechas mal decodificadas, no todas. */
    if (ds != hora_dia_semana(d, mes, anno)) { return 0U; }

    /*
     * De hora de Moscu a hora de pared española, en dos pasos y usando la
     * diferencia QUE VIENE EN LA TRAMA, no un +3 escrito a mano: si Rusia la
     * cambia otra vez -ya lo hizo en 2011 y en 2014- esto sigue bien solo.
     */
    desp = (int8_t)(dut_neg ? (int8_t)dut : (int8_t)(-(int8_t)dut));
    {
        uint8_t hh = h, dd = d, mm = mes, aa = anno, dsx = ds;
        hora_suma_horas(desp, &hh, &dd, &mm, &aa, &dsx);   /* ahora es UTC */
        verano = hora_verano_espanna(dd, mm, aa, hh);
        hora_suma_horas((int8_t)(verano ? 2 : 1), &hh, &dd, &mm, &aa, &dsx);
        s_h = hh; s_d = dd; s_mes = mm; s_a = aa; s_ds = dsx;
    }
    s_m = m;
    s_ver = verano;
    return 1U;
}

static uint8_t es_el_minuto_siguiente(void)
{
    uint8_t m = (uint8_t)(s_prev_m + 1U);
    uint8_t h = s_prev_h;

    if (m > 59U) { m = 0U; h = (uint8_t)(h + 1U); }
    if (h > 23U) { h = 0U; if (s_prev_d != s_d) { return (uint8_t)(s_m == m && s_h == h); } }
    return (uint8_t)(s_m == m && s_h == h && s_d == s_prev_d);
}

static void cierra_trama(void)
{
    if (!decodifica()) {
        s_malas++;
        s_prev_valida = 0U;
        s_estado = DCF_LEYENDO;
        return;
    }
    s_ok++;
    if (s_prev_valida && es_el_minuto_siguiente()) {
        s_estado = DCF_LISTO;
    } else {
        s_estado = DCF_CONFIRMANDO;
    }
    s_prev_valida = 1U;
    s_prev_h = s_h; s_prev_m = s_m; s_prev_d = s_d;
}

/* ---------------------------------------------------------------------------
 * FRENTE: de la fase a los bits
 * ------------------------------------------------------------------------- */

/* Llega la racha de cinco unos que marca el minuto. El minuto empieza en el
 * CUARTO de los cinco, asi que el elemento que acaba de llegar -el quinto- es
 * el numero 1 desde el principio del minuto. */
static void marca_minuto(void)
{
    uint32_t ms = (uint32_t)((float)s_n_desde_marca * 1000.0f / s_fs);

    s_ms_marca = 500U;   /* cinco elementos de 100 ms: eso es la marca */
    if (s_enganchado) {
        /* El ciclo que se enseña es el del SEGUNDO, no el del minuto: un
         * numero de 60.000 no cabe en el campo y ademas "1000" se lee de un
         * vistazo. */
        s_ms_ciclo = (uint16_t)((ms / 60U > 65535U) ? 65535U : (ms / 60U));
        if (s_idx == 601U) {
            cierra_trama();
        } else {
            s_malas++;
            s_prev_valida = 0U;
            s_estado = DCF_LEYENDO;
        }
    } else {
        s_enganchado = 1U;
        s_estado = DCF_LEYENDO;
    }
    s_n_desde_marca = 0U;

    limpia_trama();
    /* Los dos primeros bits del minuto son los dos ultimos de la racha, y
     * valen 1 los dos por definicion. */
    s_d1[0] = 1U;
    s_d2[0] = 1U;
    s_idx = 2U;
}

/* Un elemento decodificado de la hipotesis que esta puesta. */
static void elemento(uint8_t bit)
{
    uint8_t seg, e;

    s_reg = (uint8_t)(((s_reg << 1) | bit) & 0x1FU);
    if (s_reg == 0x1FU) {
        marca_minuto();
        return;
    }
    if (!s_enganchado) { return; }

    seg = (uint8_t)(s_idx / 10U);
    e   = (uint8_t)(s_idx % 10U);
    s_idx++;

    if (seg >= 60U) {
        /* Ya deberia haber llegado la marca. Se sigue contando para que
         * marca_minuto() vea que la cuenta no cuadra y tire la trama. */
        if (s_idx > 640U) {
            s_enganchado = 0U;
            s_estado = DCF_PERDIDO;
            s_prev_valida = 0U;
            s_raras++;
        }
        return;
    }

    if (e == 0U) {
        s_d1[seg] = bit;
    } else if (e == 1U) {
        s_d2[seg] = bit;
    } else {
        /* Elementos fijos: 2..8 valen 0 (salvo 7 y 8 del segundo 59, que son
         * la marca) y el 9 vale 1. Cuadre o no, esto NO tira la trama: para
         * eso estan las ocho paridades. Lo que hace es alimentar el numero
         * que dice en pantalla si lo que se esta oyendo es RBU o es ruido. */
        uint8_t esperado = (uint8_t)((e == 9U) ? 1U
                                   : ((seg == 59U && (e == 7U || e == 8U)) ? 1U : 0U));
        if (esperado) { s_f1_n++; if (bit == 1U) { s_f1_ok++; } }
        else          { s_f0_n++; if (bit == 0U) { s_f0_ok++; } }

        if (s_f1_n >= 10U) {
            /*
             * EL CONTRASTE SE MIDE POR SEPARADO EN LOS CEROS Y EN LOS UNOS, Y
             * DESPUES SE PROMEDIA. Contarlos juntos parece lo mismo y no lo
             * es, y la diferencia es justo la clase de fallo que este
             * proyecto ya ha pagado: una pantalla que dice que hay señal
             * cuando no la hay.
             *
             * De los ocho elementos fijos de cada segundo, SIETE valen 0 y
             * uno vale 1. Y resulta que con ruido puro el detector no da
             * ceros y unos al azar: la fase integrada con fuga tiene mas
             * energia abajo que arriba, o sea mas en 100 Hz que en 312,5, asi
             * que casi siempre dice 0. Contando juntos, eso acierta el 87% de
             * los bits fijos y el contraste salia 0,65 CON RUIDO PURO -medido
             * el 23/09/2026 en sim/rbutest.c-, o sea que la pantalla habria
             * dicho que estaba leyendo la trama mirando ruido.
             *
             * Promediando los dos grupos, un detector que siempre diga 0
             * acierta el 100% de los ceros y el 0% de los unos: promedio 0,5,
             * contraste 0. Que es la verdad.
             */
            float p0 = (s_f0_n > 0U) ? ((float)s_f0_ok / (float)s_f0_n) : 0.0f;
            float p1 = (float)s_f1_ok / (float)s_f1_n;
            float c  = (p0 + p1) - 1.0f;      /* 2*media - 1 */
            s_contraste = (c < 0.0f) ? 0.0f : ((c > 1.0f) ? 1.0f : c);
            s_f0_ok = 0U; s_f0_n = 0U; s_f1_ok = 0U; s_f1_n = 0U;
        }
    }
}

/* Cierra la ventana de una hipotesis y devuelve su bit; deja en *punt lo bien
 * que ha separado los dos tonos. */
static uint8_t cierra_hipotesis(uint8_t h, float *punt)
{
    float m0, m1, a, b;

    a = s_g1[h][0]; b = s_g2[h][0];
    m0 = a * a + b * b - s_coef[0] * a * b;
    a = s_g1[h][1]; b = s_g2[h][1];
    m1 = a * a + b * b - s_coef[1] * a * b;
    if (m0 < 0.0f) { m0 = 0.0f; }
    if (m1 < 0.0f) { m1 = 0.0f; }
    m0 = sqrtf(m0);
    m1 = sqrtf(m1);

    /*
     * La puntuacion no es la energia sino la DIFERENCIA entre los dos tonos.
     * Con la ventana bien puesta, los 80 ms llevan un tono solo y la
     * diferencia es grande; con la ventana a caballo entre dos elementos
     * lleva un trozo de cada uno y los dos suben a la vez. La energia sola
     * casi no distingue -cae solo un 23%- y la diferencia si.
     */
    *punt = (m0 > m1) ? (m0 - m1) : (m1 - m0);
    return (uint8_t)(m1 > m0);
}

void rbu_feed(float fase)
{
    float d, x;
    uint8_t h, excl_a, excl_b;

    /* --- 1. Desenrollar e integrar con fuga ---------------------------- */
    if (s_primera) { s_fase_prev = fase; s_primera = 0U; return; }
    d = fase - s_fase_prev;
    s_fase_prev = fase;
    while (d >  3.14159265f) { d -= 6.28318531f; }
    while (d < -3.14159265f) { d += 6.28318531f; }
    s_v = (1.0f - s_a_fuga) * (s_v + d);
    x = s_v;

    s_n_desde_marca++;

    /* --- 2. Frontera de paso: cerrar una hipotesis y abrir otra --------- */
    if (++s_n_en_paso >= s_n_paso) {
        uint8_t h_cierra, h_abre;
        s_n_en_paso = 0U;
        s_paso = (uint8_t)((s_paso + 1U) % 10U);

        h_cierra = (uint8_t)((s_paso + 1U) % 10U);
        h_abre   = (uint8_t)((s_paso + 9U) % 10U);

        {
            float punt;
            uint8_t bit = cierra_hipotesis(h_cierra, &punt);
            s_punt[h_cierra] += punt;
            if (s_hip_hecha && h_cierra == s_hip) { elemento(bit); }
        }

        s_g1[h_abre][0] = 0.0f; s_g2[h_abre][0] = 0.0f;
        s_g1[h_abre][1] = 0.0f; s_g2[h_abre][1] = 0.0f;

        /* --- 3. Cada HIP_VENTANA elementos, revisar cual va mejor ------- */
        if (++s_n_punt >= HIP_VENTANA * N_HIP) {
            uint8_t mejor = 0U;
            for (h = 1U; h < N_HIP; h++) {
                if (s_punt[h] > s_punt[mejor]) { mejor = h; }
            }
            if (!s_hip_hecha) {
                s_hip = mejor;
                s_hip_hecha = 1U;
            } else if (mejor != s_hip
                       && s_punt[mejor] > s_punt[s_hip] * HIP_MARGEN) {
                /* Cambiar de alineacion invalida la trama a medias: los bits
                 * de antes y los de despues no son del mismo sitio. */
                s_hip = mejor;
                s_enganchado = 0U;
                s_reg = 0U;
                s_estado = DCF_BUSCANDO;
                s_prev_valida = 0U;
            }
            for (h = 0U; h < N_HIP; h++) { s_punt[h] = 0.0f; }
            s_n_punt = 0U;
        }
    }

    /* --- 4. Acumular en las ocho hipotesis que estan dentro de ventana -- */
    excl_a = s_paso;
    excl_b = (uint8_t)((s_paso + 1U) % 10U);
    for (h = 0U; h < N_HIP; h++) {
        float s0, s1;
        if (h == excl_a || h == excl_b) { continue; }
        s0 = x + s_coef[0] * s_g1[h][0] - s_g2[h][0];
        s_g2[h][0] = s_g1[h][0]; s_g1[h][0] = s0;
        s1 = x + s_coef[1] * s_g1[h][1] - s_g2[h][1];
        s_g2[h][1] = s_g1[h][1]; s_g1[h][1] = s1;
    }
}

void rbu_info(dcf_info_t *out)
{
    out->estado = (s_contraste < CONTRASTE_MIN && s_estado == DCF_LEYENDO)
                  ? DCF_BUSCANDO : s_estado;
    out->hora = s_h; out->minuto = s_m;
    out->dia = s_d; out->mes = s_mes; out->anno = s_a;
    out->dia_semana = s_ds;
    out->verano = s_ver;
    out->bits = (uint8_t)((s_idx / 10U > 59U) ? 59U : (s_idx / 10U));
    out->tramas_ok = s_ok;
    out->tramas_malas = s_malas;
    out->nivel = 0.0f;    /* lo pone main.c desde el medidor de señal: esta
                           * emisora va por fase y no mide la envolvente */
    out->contraste = s_contraste;
    out->ms_marca = s_ms_marca;
    out->ms_ciclo = s_ms_ciclo;
    out->marcas_raras = s_raras;
}
