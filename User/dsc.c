/*
 * DSC de onda corta. Ver dsc.h para el porqué de cada pieza y
 * docs/DSC_FORMATO.md para la cita del estándar y la comprobación contra
 * señal real.
 */
#include "dsc.h"
#include "nco.h"
#include "hfdl_ram.h"
#include "idioma.h"
#include "rtty_scope.h"
#include <string.h>
#ifdef DSC_DEBUG
#include <stdio.h>
#endif

/* ==========================================================================
 * LOS NÚMEROS DEL ESTÁNDAR
 * ========================================================================== */

/* §3: el enganche. DX siempre 125; RX cuenta atrás de 111 a 104. */
#define SIM_DX        125U
#define RX_PRIMERO    111U
#define RX_ULTIMO     104U

/* §9: fin de secuencia. */
#define EOS_RQ        117U
#define EOS_BQ        122U
#define EOS_OTRAS     127U

/* §4: especificadores de formato. */
#define FMT_SOCORRO   112U
#define FMT_TODAS     116U
#define FMT_INDIV     120U
#define FMT_GRUPO     114U
#define FMT_ZONA      102U
#define FMT_INDIV_AUT 123U

/* ==========================================================================
 * ESTADO - todo en la RAM prestada
 * ==========================================================================
 *
 * La RAM permanente está a unos dos kilobytes libres, y sólo las cuatro
 * tablas del correlador son tres. Así que esto vive en el pozo que presta
 * hfdl_ram.c, igual que el Feld-Hell: el modo es uno solo, así que el HFDL
 * y esto nunca están activos a la vez.
 *
 * Y se devuelve. Un dsc_start() sin su dsc_stop() se queda los 54 kB y deja
 * al HFDL sin sitio, que es un fallo que no se ve mirando la pantalla - por
 * eso el banco cuenta las peticiones y las sueltas.
 */
#define SPB_MAX   192U     /* 120 muestras por bit a 12 kHz y 100 Bd */
#define MSG_MAX    40U
#define ANILLO     16U

#ifdef __GNUC__
#define TCMRAM_BSS __attribute__((section(".tcmram")))
#else
#define TCMRAM_BSS
#endif

typedef struct {
    /* --- cañón de tonos --- */
    nco_t    nco_b, nco_y;          /* B = tono ALTO = cero ; Y = bajo = uno */
    float    bi[SPB_MAX], bq[SPB_MAX];
    float    yi[SPB_MAX], yq[SPB_MAX];
    float    sbi, sbq, syi, syq;
    uint16_t hidx, spb, llenado;
    float    nivel;

    /* --- centro medido por cruces por cero --- */
    uint16_t cruces, cruces_n;
    uint8_t  signo_ant;
    float    centro_med;

    /* --- reloj de bit --- */
    uint32_t fase, inc, inc_nom;
    float    err_int, d_medio, d_ant;

    /* --- caracteres --- */
    uint16_t reg;                   /* los últimos 10 bits */
    uint32_t nbits;
    uint8_t  punt[10];              /* caracteres de enganche vistos, por fase */
    uint8_t  tiene_fase;
    uint8_t  fase_car;              /* fase de 10 bits elegida */
    uint8_t  cuenta;                /* bits desde la última frontera */
    uint8_t  ranura;                /* 0 = DX, 1 = RX */
    uint8_t  tiene_ranura;

    /* --- mensaje --- */
    uint8_t  estado;                /* 0 busca, 1 enganche, 2 mensaje, 3 cola */
    uint8_t  msg[MSG_MAX];
    uint8_t  ok[MSG_MAX];
    uint8_t  n_dx;
    uint8_t  cola;                  /* pares que faltan tras el ECC */
    uint8_t  visto_eos;

    /* --- salida --- */
    char     lin[DSC_LINEAS][DSC_LARGO];
    uint8_t  n_lin;
    uint32_t llamadas, malas;
} dsc_st_t;

static uint8_t   s_on;
static float     s_fs;
static float     s_centro_fijo;   /* 0 = buscar solo */
static dsc_st_t *s_st;

/* ==========================================================================
 * EL CARÁCTER DE DIEZ BITS
 * ==========================================================================
 *
 * §2: siete bits de información con el MENOS significativo primero, y tres
 * de comprobación con el MÁS significativo primero, que son la CUENTA de
 * elementos B (ceros) que hay en los siete.
 *
 * El orden asimétrico es el detalle que no se acierta de memoria, y es
 * justo el que hace que esto enganche o no enganche.
 */
static uint8_t car_saca(uint16_t reg, uint8_t *sim_out)
{
    uint8_t i, sim = 0U, ceros = 0U, chk;

    for (i = 0U; i < 7U; i++) {
        uint8_t b = (uint8_t)((reg >> i) & 1U);
        sim = (uint8_t)(sim | (uint8_t)(b << i));
        if (!b) { ceros++; }
    }
    chk = (uint8_t)(((reg >> 7) & 1U) * 4U + ((reg >> 8) & 1U) * 2U + ((reg >> 9) & 1U));
    *sim_out = sim;
    return (uint8_t)(chk == ceros);
}

static uint8_t es_rx(uint8_t s) { return (uint8_t)(s >= RX_ULTIMO && s <= RX_PRIMERO); }

/* ==========================================================================
 * EL TEXTO DE LA LLAMADA
 * ========================================================================== */
static void pon(uint8_t fila, const char *a, const char *b)
{
    uint8_t i = 0U;
    if (fila >= DSC_LINEAS) { return; }
    while (a[i] != '\0' && i < (uint8_t)(DSC_LARGO - 1U)) { s_st->lin[fila][i] = a[i]; i++; }
    if (b) {
        uint8_t j = 0U;
        while (b[j] != '\0' && i < (uint8_t)(DSC_LARGO - 1U)) { s_st->lin[fila][i++] = b[j++]; }
    }
    s_st->lin[fila][i] = '\0';
    if (fila >= s_st->n_lin) { s_st->n_lin = (uint8_t)(fila + 1U); }
}

static void pon_mmsi(uint8_t fila, const char *rot, const uint8_t *c)
{
    char t[16];
    uint8_t i, p = 0U;

    for (i = 0U; i < 5U; i++) {
        /* Cada carácter de la dirección son DOS CIFRAS (§5), así que un
         * símbolo por encima de 99 no es una dirección: es basura. Se pinta
         * como tal en vez de salir un número que parece bueno. */
        if (c[i] > 99U) { t[p++] = '-'; t[p++] = '-'; }
        else {
            t[p++] = (char)('0' + (c[i] / 10U));
            t[p++] = (char)('0' + (c[i] % 10U));
        }
    }
    t[9] = '\0';            /* §5: nueve cifras y un cero de relleno */
    pon(fila, rot, t);
}

static const char *fmt_txt(uint8_t f)
{
    switch (f) {
    case FMT_SOCORRO:   return tr("SOCORRO", "DISTRESS");
    case FMT_TODAS:     return tr("todas las estaciones", "all ships");
    case FMT_INDIV:     return tr("individual", "individual");
    case FMT_GRUPO:     return tr("grupo", "group");
    case FMT_ZONA:      return tr("zona geografica", "geographic area");
    case FMT_INDIV_AUT: return tr("individual automatico", "individual auto");
    default:            return tr("formato ?", "format ?");
    }
}

static const char *cat_txt(uint8_t c)
{
    switch (c) {
    case 100U: return tr("rutina", "routine");
    case 106U: return tr("ACS", "ACS");
    case 108U: return tr("seguridad", "safety");
    case 110U: return tr("urgencia", "urgency");
    case 112U: return tr("SOCORRO", "DISTRESS");
    default:   return tr("categoria ?", "category ?");
    }
}

/*
 * §8.1.1. Naturaleza del socorro. ESTA RAMA NO ESTA PROBADA CON SEÑAL REAL:
 * no hay grabación de una alerta y ojalá siga sin haberla. Los códigos están
 * leídos del estándar y el entramado se prueba con trama sintética.
 */
static const char *nat_txt(uint8_t n)
{
    switch (n) {
    case 100U: return tr("incendio o explosion", "fire or explosion");
    case 101U: return tr("via de agua", "flooding");
    case 102U: return tr("abordaje", "collision");
    case 103U: return tr("varada", "grounding");
    case 104U: return tr("escora, peligro de zozobrar", "listing, may capsize");
    case 105U: return tr("hundimiento", "sinking");
    case 106U: return tr("sin gobierno, a la deriva", "disabled and adrift");
    case 107U: return tr("sin especificar", "undesignated");
    case 108U: return tr("abandono del buque", "abandoning ship");
    case 109U: return tr("pirateria o robo", "piracy or robbery");
    case 110U: return tr("hombre al agua", "man overboard");
    default:   return tr("naturaleza ?", "nature ?");
    }
}

static void u2s(char *d, uint32_t v)
{
    char t[12]; uint8_t n = 0U, i;
    if (v == 0U) { t[n++] = '0'; }
    while (v) { t[n++] = (char)('0' + (v % 10U)); v /= 10U; }
    for (i = 0U; i < n; i++) { d[i] = t[n - 1U - i]; }
    d[n] = '\0';
}

/*
 * §10: el ECC es la paridad vertical par de los SIETE bits de información de
 * todos los caracteres de información. Cuentan el especificador de formato y
 * el de fin de secuencia; NO cuentan los de enganche ni los RX.
 */
static uint8_t ecc_de(const uint8_t *m, uint8_t n)
{
    uint8_t i = 0U, r = 0U;

    /*
     * EL ESPECIFICADOR DE FORMATO CUENTA UNA VEZ, NO DOS.
     *
     * Se EMITE dos veces (§4), y la primera versión de esto metía las dos
     * copias en la suma. Fallaba siempre. Con las dos secuencias reales
     * delante salió solo:
     *
     *   GMDSS_1   XOR de los 22 = 45    45 ^ 120 = 85  = el ECC recibido
     *   GMDSS_3   XOR de los 22 = 28    28 ^ 120 = 100 = el ECC recibido
     *
     * Las dos, con el mismo 120 de más. Así que se salta la primera copia.
     * Esto NO se habría acertado de memoria, y el documento no lo dice con
     * estas palabras: lo dicen las dos grabaciones.
     */
    if (n >= 2U && m[1] == m[0]) { i = 1U; }
    for (; i < n; i++) { r = (uint8_t)(r ^ (m[i] & 0x7FU)); }
    return r;
}

static void llamada_pinta(void)
{
    uint8_t *m = s_st->msg;
    uint8_t  n = s_st->n_dx;
    uint8_t  k, fmt, cat, eccv, eccq;
    char     t[16];

    s_st->n_lin = 0U;
    if (n < 12U) { return; }

    fmt = m[0];
    /* §4: el especificador va dos veces. Si las dos copias no coinciden se
     * dice, en vez de elegir una y callar. */
    k = (uint8_t)((m[1] == fmt) ? 2U : 1U);

    pon(0, "DSC ", fmt_txt(fmt));
    if (m[1] != fmt) { pon(0, "DSC ", tr("formato dudoso", "format unsure")); }

    /*
     * EL SOCORRO NO TIENE LA MISMA FORMA QUE LOS DEMAS, y darselo fue un
     * fallo que pillo el banco: detras del especificador NO va una direccion
     * ni una categoria -a quien va dirigida una alerta es a todo el mundo, y
     * su categoria es ser una alerta-. Va la AUTOIDENTIFICACION del barco que
     * la manda y, pegada, la naturaleza del socorro.
     *
     * Con el reparto generico salia "a MMSI 215322000 / de MMSI ---------":
     * el barco en peligro puesto como destinatario y el remitente en blanco.
     * En una alerta de socorro eso no es un detalle de formato.
     *
     * SIGUE SIN COMPROBARSE EN EL AIRE - ver nat_txt().
     */
    if (fmt == FMT_SOCORRO) {
        if ((uint8_t)(k + 5U) <= n) {
            pon_mmsi(1, tr("de  MMSI ", "fm  MMSI "), &m[k]);
        }
        if ((uint8_t)(k + 5U) < n) {
            pon(2, tr("SOCORRO: ", "DISTRESS: "), nat_txt(m[k + 5U]));
        }
        pon(3, tr("categoria: ", "category: "), cat_txt(FMT_SOCORRO));
    } else {
        if ((uint8_t)(k + 5U) <= n) { pon_mmsi(1, tr("a   MMSI ", "to  MMSI "), &m[k]); }
        cat = ((uint8_t)(k + 5U) < n) ? m[k + 5U] : 0U;
        if ((uint8_t)(k + 11U) <= n) { pon_mmsi(2, tr("de  MMSI ", "fm  MMSI "), &m[k + 6U]); }
        pon(3, tr("categoria: ", "category: "), cat_txt(cat));
    }

    /* Fin de secuencia y ECC. El ECC es el último carácter; el EOS, el
     * anterior. */
    eccv = m[n - 1U];
    eccq = ecc_de(m, (uint8_t)(n - 1U));
    for (k = 0U; k < n; k++) {
        if (m[k] == EOS_RQ)   { pon(4, tr("fin: acuse pedido", "end: ack requested"), 0); break; }
        if (m[k] == EOS_BQ)   { pon(4, tr("fin: acuse dado", "end: ack given"), 0); break; }
        if (m[k] == EOS_OTRAS){ pon(4, tr("fin: sin acuse", "end: no ack"), 0); break; }
    }

#ifdef DSC_DEBUG
    { uint8_t z; printf("    [dbg] n=%u  ", n);
      for (z=0; z<n; z++) printf("%u%s ", m[z], s_st->ok[z]?"":"?");
      printf("\n      ecc recibido=%u  calculado(todos)=%u\n", eccv, eccq); }
#endif
    u2s(t, (uint32_t)n);
    if (eccv == eccq) {
        s_st->llamadas++;
        pon(5, tr("comprobacion correcta, ", "check OK, "), t);
    } else {
        s_st->malas++;
        pon(5, tr("COMPROBACION MAL, ", "CHECK FAILED, "), t);
    }
}

/* ==========================================================================
 * EL ENTRAMADO
 * ==========================================================================
 *
 * §1.2.1: cada carácter que no sea de enganche se emite DOS veces, separadas
 * por cuatro caracteres - o sea dos PAREJAS. En la ranura RX de la pareja n
 * viaja el carácter DX de la pareja n-2.
 *
 * Eso es lo que hace robusto a este modo, y hay que usarlo: un carácter DX
 * que no pase su cuenta de ceros se rellena con su copia RX, que llega
 * 400 ms más tarde y probablemente no la pilló la misma ráfaga de ruido.
 */
static void simbolo(uint8_t sim, uint8_t valido, uint8_t ranura_rx)
{
    if (!ranura_rx) {
        /* --- ranura DX --- */
        if (s_st->estado == 1U) {
            /* En enganche el DX es siempre 125. El primero que no lo sea y
             * valga, es el especificador de formato: empieza la llamada. */
            if (valido && sim != SIM_DX) {
                s_st->estado = 2U;
                s_st->n_dx = 0U;
                s_st->visto_eos = 0U;
            } else {
                return;
            }
        }
        if (s_st->estado != 2U) { return; }
        if (s_st->n_dx >= MSG_MAX) {
            /*
             * EL BUFFER LLENO TAMBIEN TIENE QUE SOLTAR LA FASE - revision
             * del 08/10/2026.
             *
             * Ponia estado = 0 y se dejaba tiene_fase = 1, y con eso no
             * habia vuelta: el unico sitio que devuelve el estado a 1 es
             * la busqueda de fase de bit_mete(), que solo corre si
             * !tiene_fase; y dsc_poll() se vuelve atras en cuanto ve
             * tiene_fase, asi que tampoco rebuscaba el centro. El modo
             * quedaba SORDO hasta salir y volver a entrar.
             *
             * Se dispara solo: basta que el caracter de fin de secuencia
             * llegue corrompido una vez -no se marca visto_eos, no se
             * entra en estado 3- y el decodificador siga metiendo ruido en
             * la ranura DX. Cuarenta simbolos a 100 Bd son ocho segundos.
             * O sea que en la practica se leia UNA llamada por entrada al
             * modo.
             *
             * Es el mismo reinicio que ya hace el camino bueno al pintar
             * una llamada.
             */
            s_st->estado = 0U;
            s_st->tiene_fase = 0U;
            s_st->tiene_ranura = 0U;
            s_st->n_dx = 0U;
            s_st->cola = 0U;
            s_st->visto_eos = 0U;
            memset(s_st->punt, 0, sizeof s_st->punt);
            return;
        }
        s_st->msg[s_st->n_dx] = valido ? sim : 0U;
        s_st->ok[s_st->n_dx]  = valido;
        s_st->n_dx++;
        if (valido && (sim == EOS_RQ || sim == EOS_BQ || sim == EOS_OTRAS)) {
            s_st->visto_eos = 1U;
        } else if (s_st->visto_eos) {
            /* El que viene detrás del fin de secuencia es el ECC: ya está
             * todo. Se esperan dos parejas más para que lleguen las copias
             * RX de los dos últimos, y entonces se pinta. */
            s_st->estado = 3U;
            s_st->cola = 3U;
        }
    } else {
        /* --- ranura RX: rellena el DX de dos parejas antes --- */
        if (valido && (s_st->estado == 2U || s_st->estado == 3U) && s_st->n_dx >= 3U) {
            uint8_t i = (uint8_t)(s_st->n_dx - 3U);
            if (!s_st->ok[i]) { s_st->msg[i] = sim; s_st->ok[i] = 1U; }
        }
        if (s_st->estado == 3U) {
            if (s_st->cola) { s_st->cola--; }
            if (!s_st->cola) {
                llamada_pinta();
                s_st->estado = 0U;
                s_st->tiene_fase = 0U;
                s_st->tiene_ranura = 0U;
                memset(s_st->punt, 0, sizeof s_st->punt);
            }
        }
    }
}

void dsc_mete_simbolo(uint8_t sim, uint8_t valido, uint8_t ranura_rx)
{
    if (!s_st) { return; }
    if (s_st->estado == 0U) { s_st->estado = 1U; }
    simbolo(sim, valido, ranura_rx);
}

static void bit_mete(uint8_t b)
{
    uint8_t sim, valido;

    s_st->reg = (uint16_t)(((uint16_t)(s_st->reg >> 1) & 0x1FFU) | ((uint16_t)b << 9));
    s_st->nbits++;
    if (s_st->nbits < 10U) { return; }

    valido = car_saca(s_st->reg, &sim);

    if (!s_st->tiene_fase) {
        /*
         * La fase de diez bits se busca con los caracteres de ENGANCHE, que
         * son los únicos cuyo valor se conoce de antemano: 125 y la cuenta
         * atrás de 111 a 104. Un carácter cualquiera que cuadre por azar no
         * cuenta; uno de enganche sí.
         */
        uint8_t f = (uint8_t)(s_st->nbits % 10U);
        if (valido && (sim == SIM_DX || es_rx(sim))) {
            if (s_st->punt[f] < 255U) { s_st->punt[f]++; }
            if (s_st->punt[f] >= 4U) {
                s_st->tiene_fase = 1U;
                s_st->fase_car = f;
                s_st->estado = 1U;
                /*
                 * La ranura la dice el carácter que acaba de cuadrar... pero
                 * ESE se consume aquí y no llega a simbolo(). Así que lo que
                 * se guarda es la ranura del SIGUIENTE, o sea la contraria.
                 *
                 * Estaba al revés y costó una tanda: la cuenta atrás del
                 * enganche (109, 108, 107, 106) entraba como si fuera la
                 * dirección, y salía un MMSI de ":9:8:7:6:".
                 */
                s_st->ranura = (uint8_t)(es_rx(sim) ? 0U : 1U);
                s_st->tiene_ranura = 1U;
                s_st->cuenta = 0U;
            }
        }
        return;
    }
    if ((uint8_t)(s_st->nbits % 10U) != s_st->fase_car) { return; }

    simbolo(sim, valido, s_st->ranura);
    s_st->ranura = (uint8_t)(s_st->ranura ? 0U : 1U);
}

/* ==========================================================================
 * EL CAÑÓN - mismo diseño que navtex.c, que lleva semanas funcionando
 * ========================================================================== */
#define SIM_KP  0.012f
#define SIM_KI  0.0009f
#define CRUCES_VENT 12000U
#define CENTRO_MIN   700.0f
#define CENTRO_MAX  2800.0f

static void tonos_fija(void)
{
    float mitad = DSC_SHIFT_HZ * 0.5f;
    float c = (s_centro_fijo > 0.0f) ? s_centro_fijo
            : ((s_st->centro_med > 0.0f) ? s_st->centro_med : DSC_CENTRO_HZ);

    /* §1: la frecuencia ALTA es el estado B (cero) y la BAJA el Y (uno). */
    nco_freq(&s_st->nco_b, c + mitad, s_fs);
    nco_freq(&s_st->nco_y, c - mitad, s_fs);
}

static void cruces_mete(float x)
{
    uint8_t sg = (x >= 0.0f) ? 1U : 0U;

    if (sg != s_st->signo_ant) { s_st->cruces++; }
    s_st->signo_ant = sg;
    s_st->cruces_n++;
    if (s_st->cruces_n < CRUCES_VENT) { return; }

    if (!s_st->tiene_fase && s_centro_fijo <= 0.0f) {
        float f = (float)s_st->cruces * s_fs / (2.0f * (float)CRUCES_VENT);
        float d = f - s_st->centro_med;

        if (d < 0.0f) { d = -d; }
        /* Igual que en el NAVTEX: sólo si cambia de verdad. Aplicar una
         * medida que baila unas decenas de hercios cada segundo vuelve a
         * poner el reloj de bit a cero una vez por segundo, y así no se
         * asienta nunca. */
        if (f >= CENTRO_MIN && f <= CENTRO_MAX
            && (s_st->centro_med <= 0.0f || d > 40.0f)) {
            s_st->centro_med = f;
            tonos_fija();
            s_st->err_int = 0.0f;
            s_st->inc = s_st->inc_nom;
            memset(s_st->punt, 0, sizeof s_st->punt);
        }
    }
    s_st->cruces = 0U;
    s_st->cruces_n = 0U;
}

static void discriminador(float d)
{
    uint32_t fase_ant = s_st->fase;

    s_st->fase += s_st->inc;

    /* A mitad de vuelta la ventana está a caballo de dos bits: ahí mide
     * Gardner. En la vuelta cubre un bit justo: ahí se decide. El porqué
     * entero está en navtex.c, que es de donde sale este lazo. */
    if (fase_ant < 0x80000000UL && s_st->fase >= 0x80000000UL) {
        s_st->d_medio = d;
    }
    if (s_st->fase < fase_ant) {
        float e = s_st->d_medio * (d - s_st->d_ant);
        float den = s_st->nivel * s_st->nivel;

        if (den > 1e-12f) {
            e /= den;
            if (e > 1.0f)  { e = 1.0f; }
            if (e < -1.0f) { e = -1.0f; }
            s_st->err_int += SIM_KI * e;
            if (s_st->err_int >  0.02f) { s_st->err_int =  0.02f; }
            if (s_st->err_int < -0.02f) { s_st->err_int = -0.02f; }
            s_st->inc = (uint32_t)((float)s_st->inc_nom
                                   * (1.0f + SIM_KP * e + s_st->err_int));
        }
        s_st->d_ant = d;
        /* d > 0 es que gana el tono ALTO, o sea B, o sea CERO. */
        bit_mete((uint8_t)(d > 0.0f ? 0U : 1U));
    }
}

/* ==========================================================================
 * LO DE FUERA
 * ========================================================================== */
void dsc_start(float fs_hz)
{
    uint8_t *b; uint32_t cap = 0U, spb;

    s_on = 0U; s_st = 0;
    if (fs_hz <= 0.0f) { return; }
    s_fs = fs_hz;

    hfdl_ram_coge();
    b = hfdl_ram_todo(&cap);
    if (b == 0 || cap < (uint32_t)sizeof(dsc_st_t)) {
        /* Sin préstamo no se arranca, y se dice que no. */
        hfdl_ram_suelta();
        return;
    }
    s_st = (dsc_st_t *)(void *)b;
    memset(s_st, 0, sizeof *s_st);

    spb = (uint32_t)(fs_hz / DSC_BAUD + 0.5f);
    if (spb < 8U)      { spb = 8U; }
    if (spb > SPB_MAX) { spb = SPB_MAX; }
    s_st->spb = (uint16_t)spb;

    nco_init();
    nco_fase_cero(&s_st->nco_b);
    nco_fase_cero(&s_st->nco_y);
    s_st->centro_med = 0.0f;
    s_st->signo_ant = 1U;
    tonos_fija();

    s_st->inc_nom = (uint32_t)((double)DSC_BAUD / (double)fs_hz * 4294967296.0 + 0.5);
    s_st->inc = s_st->inc_nom;
    s_st->nivel = 1e-9f;
    s_on = 1U;
}

void dsc_stop(void)
{
    if (!s_on) { return; }
    s_on = 0U;
    s_st = 0;
    hfdl_ram_suelta();
}

/* ==========================================================================
 * ENCONTRAR LOS TONOS SOLO
 * ==========================================================================
 *
 * El DSC no vive en un sitio fijo del audio: depende de donde tenga el dial
 * quien escucha. En las cuatro grabaciones del dueño los centros salieron en
 * 995, 1700 y 2214 Hz.
 *
 * La busqueda por cruces por cero que usa el NAVTEX NO VALE AQUI, y esta
 * medido: sobre esas mismas grabaciones se equivocaba en 500 y 700 Hz. Al
 * NAVTEX le funciona porque la emisora manda sin parar; estas llamadas duran
 * un segundo y el resto es silencio y otras cosas, asi que la media de cruces
 * mide el ruido.
 *
 * Asi que se busca en el ANALIZADOR DE SINTONIA (rtty_scope), que es una FFT
 * de 512 puntos sobre el mismo audio de 12 kHz que ve este decodificador:
 * 23,4 Hz por casilla, y 170 Hz son 7,3 casillas - de sobra para separarlos.
 *
 * Y se eligen POR PAREJAS, no cogiendo el pico mas alto y luego el segundo.
 * Esa leccion es de rtty_auto.c y costo una tarde alli: con una grabacion
 * hecha con el movil delante del altavoz, el segundo pico mas alto era la
 * DIFERENCIA de los dos tonos, que la distorsion saca y que en el promedio
 * abulta mas que la marca desvanecida. Lo que si es cierto de una pareja de
 * tonos de DSC es que se separan 170 Hz y que miden parecido, porque son el
 * mismo transmisor alternando.
 */
#define BUSCA_PICOS_MAX   8U
#define BUSCA_VECES       4.0f    /* cuanto tiene que sacarle un pico al fondo */
#define BUSCA_TOL         0.12f   /* 170 Hz +-12 %, igual que rtty_auto */

static float s_busca_hz;          /* lo ultimo que encontro la busqueda */

static void busca_centro(void)
{
    const float *f = rtty_scope_frame_peek();
    float hzb = rtty_scope_hz_per_bin();
    uint16_t i, n = 256U;        /* media banda: 0..3 kHz, que es donde cabe */
    uint16_t pk[BUSCA_PICOS_MAX]; float pv[BUSCA_PICOS_MAX];
    uint8_t  np = 0U, a, b, ma = 0U, mb = 0U;
    float fondo = 0.0f, mejor = 0.0f;

    if (!f || hzb <= 0.0f) { return; }
    for (i = 12U; i < n; i++) { fondo += f[i]; }
    fondo /= (float)(n - 12U);
    if (fondo <= 0.0f) { return; }

    for (i = 13U; i + 1U < n; i++) {
        float v = f[i];
        if (v < fondo * BUSCA_VECES) { continue; }
        if (v < f[i - 1U] || v < f[i + 1U]) { continue; }
        if (np < BUSCA_PICOS_MAX) { pk[np] = i; pv[np] = v; np++; }
        else {
            uint8_t peor = 0U, k;
            for (k = 1U; k < np; k++) { if (pv[k] < pv[peor]) { peor = k; } }
            if (v > pv[peor]) { pk[peor] = i; pv[peor] = v; }
        }
    }
    if (np < 2U) { return; }

    for (a = 0U; a < np; a++) {
        for (b = (uint8_t)(a + 1U); b < np; b++) {
            float fa = (float)pk[a] * hzb, fb = (float)pk[b] * hzb;
            float sep = (fb > fa) ? (fb - fa) : (fa - fb);
            float d = (sep - DSC_SHIFT_HZ) / DSC_SHIFT_HZ;
            float eq, alt, p;

            if (d < 0.0f) { d = -d; }
            if (d > BUSCA_TOL) { continue; }
            eq  = (pv[a] < pv[b]) ? (pv[a] / pv[b]) : (pv[b] / pv[a]);
            alt = (pv[a] + pv[b]) / fondo;
            p   = (1.0f - d) * eq * alt;
            if (p > mejor) { mejor = p; ma = a; mb = b; }
        }
    }
    if (mejor <= 0.0f) { return; }
    {
        float c = ((float)pk[ma] + (float)pk[mb]) * 0.5f * hzb;
        float mov = c - dsc_get_centro_hz();
        if (mov < 0.0f) { mov = -mov; }
        /* Mismo criterio que el NAVTEX: mover el centro reinicia el reloj de
         * bit, asi que solo se mueve si cambia de verdad. */
        if (c > 400.0f && c < 2800.0f && mov > 25.0f) {
            s_busca_hz = c;
            dsc_set_centro_hz(c);
        }
    }
}

/*
 * Se llama desde el bucle principal, no desde la interrupcion: la FFT del
 * analizador ya corre alli y esto solo lee su resultado.
 */
void dsc_poll(void)
{
    if (!s_on || !s_st) { return; }
    /* Mientras haya enganche no se toca nada: la señal ya esta donde tiene
     * que estar y rebuscar solo puede estropearlo. */
    if (s_st->tiene_fase) { return; }
    busca_centro();
}

float dsc_busca_hz(void) { return s_busca_hz; }

uint8_t dsc_activo(void) { return s_on; }

void dsc_set_centro_hz(float hz)
{
    s_centro_fijo = (hz > 300.0f && hz < 2900.0f) ? hz : 0.0f;
    if (s_on && s_st) {
        tonos_fija();
        s_st->tiene_fase = 0U;
        s_st->estado = 0U;
        memset(s_st->punt, 0, sizeof s_st->punt);
    }
}

float dsc_get_centro_hz(void)
{
    if (s_centro_fijo > 0.0f) { return s_centro_fijo; }
    if (s_on && s_st && s_st->centro_med > 0.0f) { return s_st->centro_med; }
    return DSC_CENTRO_HZ;
}

void dsc_process(const float *audio, uint32_t n)
{
    uint32_t k;

    if (!s_on || !s_st || !audio) { return; }

    for (k = 0U; k < n; k++) {
        float x = audio[k];
        float sn, cs, bi, bq, yi, yq, pb, py;

        cruces_mete(x);

        nco_paso(&s_st->nco_b, &sn, &cs);
        bi = x * cs;  bq = -x * sn;
        nco_paso(&s_st->nco_y, &sn, &cs);
        yi = x * cs;  yq = -x * sn;

        s_st->sbi += bi - s_st->bi[s_st->hidx];  s_st->bi[s_st->hidx] = bi;
        s_st->sbq += bq - s_st->bq[s_st->hidx];  s_st->bq[s_st->hidx] = bq;
        s_st->syi += yi - s_st->yi[s_st->hidx];  s_st->yi[s_st->hidx] = yi;
        s_st->syq += yq - s_st->yq[s_st->hidx];  s_st->yq[s_st->hidx] = yq;
        s_st->hidx++;
        if (s_st->hidx >= s_st->spb) { s_st->hidx = 0U; }

        if (s_st->llenado < s_st->spb) { s_st->llenado++; continue; }

        pb = s_st->sbi * s_st->sbi + s_st->sbq * s_st->sbq;
        py = s_st->syi * s_st->syi + s_st->syq * s_st->syq;

        s_st->nivel += 0.0005f * ((pb + py) - s_st->nivel);
        if (s_st->nivel < 1e-12f) { s_st->nivel = 1e-12f; }

        discriminador(pb - py);
    }
}

uint8_t dsc_lineas(void) { return (s_on && s_st) ? s_st->n_lin : 0U; }

const char *dsc_linea(uint8_t i)
{
    if (!s_on || !s_st || i >= s_st->n_lin) { return ""; }
    return s_st->lin[i];
}

void dsc_info(dsc_info_t *out)
{
    if (!out) { return; }
    memset(out, 0, sizeof *out);
    if (!s_on || !s_st) { return; }
    out->enganchado = s_st->tiene_fase;
    out->en_mensaje = (uint8_t)(s_st->estado >= 2U);
    out->llamadas   = s_st->llamadas;
    out->malas      = s_st->malas;
    out->desvio_hz  = (int16_t)((s_st->centro_med > 0.0f)
                                ? (s_st->centro_med - DSC_CENTRO_HZ) : 0.0f);
}
