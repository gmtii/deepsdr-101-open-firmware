#include "rds.h"
#include "nco.h"
#include "rds_fir.h"
#include <string.h>
#include <math.h>

/* ==========================================================================
 * NUMEROS DEL FORMATO
 * ========================================================================== */
#define RDS_SUB_HZ    57000.0f     /* la subportadora */
#define RDS_DEC       16U          /* 192 kHz -> 12 kHz */
#define RDS_BAUD      1187.5f      /* = 57000 / 48, exacto */

/* Polinomio generador: x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + 1 */
#define RDS_GEN       0x5B9UL

uint16_t rds_resto(uint32_t bloque26)
{
    int i;
    /* Division larga, sin tabla: son dieciseis vueltas de nada y se lee lo
     * que hace. Una tabla aqui seria optimizar lo que se ejecuta una vez por
     * bloque, o sea 47 veces por segundo. */
    for (i = 25; i >= 10; i--) {
        if (bloque26 & (1UL << i)) {
            bloque26 ^= RDS_GEN << (i - 10);
        }
    }
    return (uint16_t)(bloque26 & 0x3FFUL);
}

/*
 * ==========================================================================
 * CORRECCION DE ERRORES
 * ==========================================================================
 * El codigo del RDS no solo detecta: es ciclico y puede CORREGIR rafagas
 * cortas. Hasta hoy solo se usaba para detectar, y en el aire eso significa
 * tirar la mayoria de los bloques por un bit suelto.
 *
 * COMO. El resto es lineal, o sea que resto(r ^ e) = resto(r) ^ resto(e).
 * Si el bloque recibido r deberia dar la palabra `off`, el error e tiene que
 * cumplir resto(e) == resto(r) ^ off. Asi que se prueban todas las rafagas
 * posibles -posicion y patron- y se mira cual da ese resto. Son 26 posiciones
 * por 2^(max_raf-1) patrones: con rafagas de hasta dos bits, 52 pruebas de
 * dieciseis vueltas cada una. Eso es nada, y a cambio se lee lo que hace sin
 * tener que confiar en un decodificador de Meggitt escrito de memoria.
 *
 * EL PRECIO, que es lo que decide hasta donde llegar. Cada patron que se
 * admite amplia el conjunto de palabras que se dan por buenas. Con rafagas
 * de hasta 5 bits son 417 patrones contra las 1024 combinaciones del resto:
 * cuatro de cada diez bloques de basura colarian. Con rafagas de hasta 2
 * son 53 de 1024, un 5%. Los numeros medidos estan en sim/rdstest.c y son
 * los que fijan RDS_RAFAGA_MAX.
 */
uint8_t rds_corrige(uint32_t *bloque26, uint16_t off, uint8_t max_raf)
{
    uint16_t objetivo;
    uint32_t m;
    uint8_t p, anchura;

    objetivo = (uint16_t)(rds_resto(*bloque26) ^ off);
    if (objetivo == 0U) {
        return 1U;              /* ya estaba bien */
    }
    if (max_raf == 0U || max_raf > 10U) {
        return 0U;
    }

    /* Patrones de rafaga: impares (el primer bit del error es un 1 por
     * definicion) y de menos de max_raf bits de ancho. */
    for (m = 1UL; m < (1UL << max_raf); m += 2UL) {
        anchura = 0U;
        while ((m >> anchura) != 0UL) { anchura++; }
        for (p = 0U; p <= (uint8_t)(26U - anchura); p++) {
            uint32_t e = m << p;
            if (rds_resto(e) == objetivo) {
                *bloque26 ^= e;
                return 1U;
            }
        }
    }
    return 0U;
}

/* ==========================================================================
 * ESTADO
 * ========================================================================== */
static uint8_t s_on = 0U;
static float   s_fs = 192000.0f;

/* --- 1. mezcla y diezmado --- */
static nco_t   s_nco;
/* Historial doble para que el producto escalar salga contiguo: se escribe
 * cada muestra en dos sitios (idx e idx+N) y asi nunca hay que dar la vuelta
 * al indice dentro del bucle caliente. Cuesta un almacenamiento de mas por
 * muestra y ahorra un modulo por coeficiente. */
static float   s_hi[2U * K_RDS_DEC_TAPS];
static float   s_hq[2U * K_RDS_DEC_TAPS];
static uint16_t s_hidx;
static uint16_t s_dec_cnt;

/* --- 1b. control de ganancia de la banda base ---
 * Los dos lazos que vienen despues -portadora y reloj- tienen ganancias
 * fijas, asi que su comportamiento depende del NIVEL de la senal. En el
 * banco la subportadora va al 3%; en el aire puede ir a la mitad o al doble
 * segun la emisora y lo que marque el S-meter. Sin esto, el mismo codigo
 * engancharia en dos segundos con una emisora y en veinte con otra, y eso se
 * vive como "el RDS no va". Normalizando aqui, los dos lazos ven siempre la
 * misma amplitud y sus constantes significan algo. */
static float s_agc;

/* --- 2. lazo de Costas --- */
static float   s_cos_fase;     /* radianes */
static float   s_cos_frec;     /* radianes por muestra a 12 kHz */

/* --- 3. reloj de simbolo --- */
static uint32_t s_sim_fase;    /* vuelta completa = 2^32 = un simbolo */
static uint32_t s_sim_inc;

/*
 * Lazo del reloj de simbolo. KP mueve la FASE (engancha) y KI mueve la
 * FRECUENCIA (persigue la deriva entre el cuarzo de la emisora y el
 * nuestro).
 *
 * OJO CON LA ESTRUCTURA, que la primera version la tenia mal: el integrador
 * ES la correccion de frecuencia, no algo que se SUMA a ella en cada
 * simbolo. Sumarlo hacia un doble integrador, y con el integrador saturado
 * el incremento se iba un 1% por segundo: el banco lo enseno enseguida -la
 * senal limpia empezaba al 71% de bloques buenos y acababa en el 0% al cabo
 * de diez segundos-. Un lazo que empeora con el tiempo siempre es esto.
 */
#define SIM_KP      0.010f    /* fraccion de simbolo por unidad de error */
#define SIM_KI      0.0000030f /* correccion RELATIVA de frecuencia */
#define SIM_KI_TOPE 0.0015f   /* +/-1500 ppm de margen, de sobra */

static float    s_sim_err_int;
static uint32_t s_sim_inc_nom;

static float   s_acc1, s_acc2; /* integrales de la primera y segunda mitad */
static uint16_t s_n1, s_n2;
static float   s_medio;        /* la muestra que cayo en mitad del simbolo */
static uint8_t s_vi_medio;

/* --- 4. bits y bloques --- */
static uint8_t  s_bit_ant;
static uint8_t  s_hay_bit_ant;
static uint32_t s_reg;         /* los ultimos 26 bits */
static uint8_t  s_reg_n;
static uint8_t  s_sync;        /* 1 = enganchado */
static uint8_t  s_pos;         /* 0=A 1=B 2=C 3=D */
static uint8_t  s_cuenta;      /* bits desde el ultimo bloque */
static uint8_t  s_cand;        /* 1 = hay un bloque candidato esperando confirmacion */
static uint8_t  s_cand_pos;
static uint16_t s_malos_seguidos;
static uint32_t s_busca_simbolos;  /* simbolos sin engancharse */
static uint8_t  s_medio_simbolo;   /* 0/1: cual de las dos fases se prueba */

static uint32_t s_ok, s_mal, s_corregidos;
static uint32_t s_enganches, s_perdidas;   /* diagnostico, ver rds_lazos_dbg() */

/* --- 5. grupo en curso --- */
static uint16_t s_blq[4];
static uint8_t  s_blq_ok[4];
static uint8_t  s_blq_corr[4];
static uint16_t s_pi_ant;
static uint8_t  s_pi_visto;   /* 1 = ese bloque llego roto y se arreglo */

/* --- 6. lo decodificado --- */
static rds_info_t s_info;

/*
 * EL RELOJ DE MUESTRAS. 24/09/2026.
 *
 * Cuenta muestras de BANDA BASE (12 kHz), o sea una de cada RDS_DEC que
 * entran por rds_feed(). Con eso, 83 microsegundos de resolucion y la vuelta
 * al contador cada 99 horas.
 *
 * Existe para poder decir CUANDO paso algo, y no solo QUE paso. Lo unico que
 * lo usa de momento es la hora: la norma dice que el borde de minuto cae a
 * menos de 0,1 s del FINAL del grupo 4A, asi que anotando en que muestra
 * acaba ese grupo se sabe donde estaba el borde de minuto - que es lo que
 * hace falta para poner el reloj con el segundo, no solo con el minuto.
 *
 * Va aqui y no en main.c con g_msticks a proposito: g_msticks avanza con el
 * SysTick y estas muestras avanzan con el reloj del codec, que es el que
 * marca el tiempo real de la senal. Entre el bit que acaba el grupo y el
 * momento en que main.c se entera pueden pasar varios milisegundos de cola
 * de bloques; contando muestras eso no se pierde.
 */
static uint32_t s_muestras;

/* ==========================================================================
 * ARRANQUE
 * ========================================================================== */
static void limpia_cadena(char *s, uint16_t n)
{
    uint16_t i;
    for (i = 0U; i < n; i++) { s[i] = ' '; }
    s[n] = '\0';
}

void rds_start(float fs_hz)
{
    float fs_bb;

    if (fs_hz <= 0.0f) { return; }
    s_fs = fs_hz;
    fs_bb = fs_hz / (float)RDS_DEC;

    nco_init();
    nco_fase_cero(&s_nco);
    nco_freq(&s_nco, RDS_SUB_HZ, fs_hz);

    memset(s_hi, 0, sizeof s_hi);
    memset(s_hq, 0, sizeof s_hq);
    s_hidx = 0U;
    s_dec_cnt = 0U;

    s_cos_fase = 0.0f;
    s_cos_frec = 0.0f;
    s_agc = 1e-4f;

    /* Un simbolo por vuelta del acumulador: el incremento es baudios/Fs. */
    s_sim_fase = 0U;
    s_sim_inc_nom = (uint32_t)((double)RDS_BAUD / (double)fs_bb * 4294967296.0 + 0.5);
    s_sim_inc = s_sim_inc_nom;
    s_sim_err_int = 0.0f;
    s_acc1 = s_acc2 = 0.0f;
    s_n1 = s_n2 = 0U;
    s_medio = 0.0f;
    s_vi_medio = 0U;

    s_bit_ant = 0U;
    s_hay_bit_ant = 0U;
    s_reg = 0UL;
    s_reg_n = 0U;
    s_sync = 0U;
    s_pos = 0U;
    s_cuenta = 0U;
    s_cand = 0U;
    s_cand_pos = 0U;
    s_malos_seguidos = 0U;
    s_busca_simbolos = 0UL;
    s_medio_simbolo = 0U;
    s_ok = s_mal = s_corregidos = 0UL;
    s_enganches = s_perdidas = 0UL;
    s_muestras = 0UL;

    memset(&s_info, 0, sizeof s_info);
    limpia_cadena(s_info.ps, 8U);
    limpia_cadena(s_info.rt, 64U);
    memset(s_blq_ok, 0, sizeof s_blq_ok);
    memset(s_blq_corr, 0, sizeof s_blq_corr);
    s_pi_ant = 0U;
    s_pi_visto = 0U;

    s_on = 1U;
}

void rds_stop(void) { s_on = 0U; }
uint8_t rds_activo(void) { return s_on; }

void rds_info(rds_info_t *out)
{
    if (!out) { return; }
    s_info.enganchado = s_sync;
    s_info.bloques_ok = s_ok;
    s_info.bloques_mal = s_mal;
    s_info.calidad = (s_ok + s_mal) ? ((float)s_ok / (float)(s_ok + s_mal)) : 0.0f;
    *out = s_info;
}

/* ==========================================================================
 * LOS GRUPOS
 * ========================================================================== */

/*
 * El RDS tiene su propia tabla de caracteres, que NO es ASCII a partir de
 * 0x80 y que ademas mete algunas letras acentuadas por debajo. Aqui se
 * traduce lo justo: lo imprimible de ASCII pasa tal cual y lo demas se
 * sustituye por un espacio. Meter la tabla entera son 128 bytes y un lio de
 * codificacion con las fuentes del panel, y para un nombre de emisora
 * espanola no aporta casi nada: las que usan acentos suelen mandar el nombre
 * sin ellos precisamente por esto.
 */
static char rds_car(uint8_t c)
{
    return (c >= 0x20U && c < 0x7FU) ? (char)c : ' ';
}

/*
 * De dia juliano modificado a dia/mes/ano. La formula es la que da la propia
 * norma del RDS, con las dos constantes que dependen de si el mes es enero o
 * febrero.
 */
static void mjd_a_fecha(uint32_t mjd, uint8_t *dia, uint8_t *mes, uint8_t *anno2)
{
    uint32_t yp = (uint32_t)(((double)mjd - 15078.2) / 365.25);
    uint32_t mp = (uint32_t)((((double)mjd - 14956.1) - (double)((uint32_t)((double)yp * 365.25))) / 30.6001);
    uint32_t k;
    uint32_t d, m, y;

    d = mjd - 14956UL - (uint32_t)((double)yp * 365.25)
        - (uint32_t)((double)mp * 30.6001);
    k = (mp == 14UL || mp == 15UL) ? 1UL : 0UL;
    y = yp + k;
    m = mp - 1UL - k * 12UL;

    *dia = (uint8_t)d;
    *mes = (uint8_t)m;
    *anno2 = (uint8_t)((y + 1900UL) % 100UL);
}

static void grupo_decodifica(void)
{
    uint8_t tipo, version;

    /* El bloque A es el identificador de programa, y solo vale si su resto
     * salio bien. */
    /*
     * El identificador de programa solo se da por bueno cuando se ha visto
     * DOS VECES el mismo. Llega en cada grupo -once veces por segundo-, asi
     * que no cuesta nada esperar; y a cambio deja de poder aparecer un
     * codigo inventado por un bloque que se colo, que es justo lo que se vio
     * en el banco con ruido (salia un 0x2449 que no existia).
     */
    if (s_blq_ok[0]) {
        if (s_pi_visto && s_pi_ant == s_blq[0]) {
            s_info.pi = s_blq[0];
            s_info.tiene_pi = 1U;
        }
        s_pi_ant = s_blq[0];
        s_pi_visto = 1U;
    }
    if (!s_blq_ok[1]) {
        return;   /* sin el bloque B no se sabe ni que grupo es */
    }

    tipo = (uint8_t)((s_blq[1] >> 12) & 0x0FU);
    version = (uint8_t)((s_blq[1] >> 11) & 0x01U);

    /* --- 0A / 0B: el nombre de la emisora, de dos en dos caracteres --- */
    if (tipo == 0U) {
        uint8_t seg = (uint8_t)(s_blq[1] & 0x03U);
        if (s_blq_ok[3]) {
            s_info.ps[seg * 2U]      = rds_car((uint8_t)(s_blq[3] >> 8));
            s_info.ps[seg * 2U + 1U] = rds_car((uint8_t)(s_blq[3] & 0xFFU));
            s_info.tiene_ps = 1U;
        }
        return;
    }

    /* --- 2A / 2B: el radiotexto --- */
    if (tipo == 2U) {
        uint8_t seg = (uint8_t)(s_blq[1] & 0x0FU);
        if (version == 0U) {
            /* 2A: cuatro caracteres por grupo, en C y D */
            if (s_blq_ok[2]) {
                s_info.rt[seg * 4U]      = rds_car((uint8_t)(s_blq[2] >> 8));
                s_info.rt[seg * 4U + 1U] = rds_car((uint8_t)(s_blq[2] & 0xFFU));
                s_info.tiene_rt = 1U;
            }
            if (s_blq_ok[3]) {
                s_info.rt[seg * 4U + 2U] = rds_car((uint8_t)(s_blq[3] >> 8));
                s_info.rt[seg * 4U + 3U] = rds_car((uint8_t)(s_blq[3] & 0xFFU));
                s_info.tiene_rt = 1U;
            }
        } else {
            /* 2B: solo dos, en D, y el texto es de 32 */
            if (s_blq_ok[3] && seg < 16U) {
                s_info.rt[seg * 2U]      = rds_car((uint8_t)(s_blq[3] >> 8));
                s_info.rt[seg * 2U + 1U] = rds_car((uint8_t)(s_blq[3] & 0xFFU));
                s_info.tiene_rt = 1U;
            }
        }
        return;
    }

    /* --- 4A: la hora --- */
    if (tipo == 4U && version == 0U) {
        uint32_t mjd;
        uint8_t hh, mm, signo, medias;

        /*
         * La hora va repartida entre los bloques B, C y D, asi que los tres
         * tienen que estar bien. Y no se admite "casi": una hora a medias no
         * es una hora, es un numero que parece una hora. Esto es exactamente
         * lo que ya se aprendio con DCF77 y compania.
         */
        if (!s_blq_ok[2] || !s_blq_ok[3]) {
            return;
        }

        mjd = ((uint32_t)(s_blq[1] & 0x0003U) << 15)
            | ((uint32_t)s_blq[2] >> 1);
        hh = (uint8_t)(((s_blq[2] & 0x0001U) << 4) | (s_blq[3] >> 12));
        mm = (uint8_t)((s_blq[3] >> 6) & 0x3FU);
        signo = (uint8_t)((s_blq[3] >> 5) & 0x01U);
        medias = (uint8_t)(s_blq[3] & 0x1FU);

        /* Comprobaciones de cordura: el formato no impide mandar un 47 en la
         * hora, y sin esto un bloque con un fallo no detectado pondria el
         * reloj en marciano. */
        if (hh > 23U || mm > 59U || mjd < 40000UL || mjd > 80000UL) {
            return;
        }

        mjd_a_fecha(mjd, &s_info.dia, &s_info.mes, &s_info.anno2);
        if (s_info.mes < 1U || s_info.mes > 12U || s_info.dia < 1U || s_info.dia > 31U) {
            return;
        }
        /*
         * DOS CONDICIONES MAS, y no son adorno.
         *
         * La hora viaja repartida entre los bloques B, C y D, asi que los
         * tres tienen que estar bien; eso ya se ha comprobado arriba. Pero
         * desde que se corrige, "estar bien" incluye "se ha arreglado", y
         * una correccion equivocada en C o en D pondria un reloj atomico en
         * marciano. Asi que ademas:
         *
         *   - El bloque A, que lleva el identificador de la emisora, tiene
         *     que estar bien Y coincidir con el que ya teniamos. Son
         *     dieciseis bits de acuerdo con algo que ya sabiamos: eso hunde
         *     la probabilidad de que un grupo de basura pase por bueno.
         *
         *   - El identificador tiene que estar ya establecido. Si es la
         *     primera vez que vemos uno, no hay contra que comparar.
         *
         * Y encima de todo esto, main.c exige que DOS horas seguidas sean
         * coherentes entre si antes de tocar el reloj. Ver rds_poll() alli.
         */
        if (s_blq_ok[0]) {
            /* Si el bloque A ha llegado, TIENE que ser de la emisora que
             * creemos. Si dice otra cosa, o es otra emisora o el bloque esta
             * mal: en los dos casos, esta hora no vale. */
            if (!s_info.tiene_pi || s_blq[0] != s_info.pi) {
                return;
            }
        }
        /*
         * Y si NO ha llegado, la hora se admite igual pero NUNCA como
         * limpia: tendra que confirmarla la del minuto siguiente.
         *
         * Exigir siempre el bloque A costaba una cuarta parte de las horas
         * -son cuatro bloques independientes al 75% cada uno- y no anadia
         * nada que la confirmacion no cubra ya. El bloque A no lleva ni un
         * bit de la hora; solo sirve para reconocer la emisora, y para eso
         * ya vale la vez que si llega.
         */

        s_info.hora = hh;
        s_info.minuto = mm;
        s_info.desp_medias = signo ? (int8_t)(-(int8_t)medias) : (int8_t)medias;
        s_info.tiene_hora = 1U;
        /* "Limpia" es el nivel de confianza de antes de que existiera la
         * correccion: los CUATRO bloques presentes y ninguno arreglado. */
        s_info.hora_limpia = (uint8_t)(s_blq_ok[0]
                                       && !s_blq_corr[0] && !s_blq_corr[1]
                                       && !s_blq_corr[2] && !s_blq_corr[3]);
        /* DONDE acaba este grupo. Se llega aqui justo despues de meter el
         * ultimo bit del bloque D, asi que s_muestras es el final del grupo
         * -mas el retardo propio del filtro diezmador, que main.c descuenta
         * con RDS_CT_RETARDO_MS: ver alli, esta medido en el banco-. */
        s_info.hora_muestra = s_muestras;
        s_info.hora_seq++;
    }
}

/* ==========================================================================
 * SINCRONISMO DE BLOQUE
 * ========================================================================== */

/*
 * HASTA DONDE SE CORRIGE. Medido en sim/rdstest.c, no elegido a ojo:
 *
 *     rafagas de hasta 1 bit    rescata el 100%   cuela el 2,65% de la basura
 *     rafagas de hasta 2 bits   rescata el 100%   cuela el 5,08%
 *     rafagas de hasta 3 bits   rescata el 100%   cuela el 9,78%
 *     rafagas de hasta 5 bits   rescata el 100%   cuela el 35,85%
 *
 * (sin corregir nada, la basura ya se cuela una de cada 1024, o sea 0,1%)
 *
 * El codigo admite hasta 5 y la norma lo contempla, pero con 5 se dan por
 * buenos mas de un tercio de los bloques de ruido: eso no es corregir, es
 * inventar. Dos bits rescata todo lo que rescata cinco en la practica -los
 * errores de un receptor de FM son casi siempre de uno o dos bits- por una
 * decima parte del riesgo.
 *
 * Y aun asi, un 5% es demasiado para la HORA, asi que la hora lleva ademas
 * sus propias condiciones - ver grupo_decodifica().
 */
#define RDS_RAFAGA_MAX  2U

/* La palabra de desplazamiento que toca en cada posicion del grupo. C y C'
 * son la misma posicion con dos palabras distintas: la norma deja elegir, y
 * hay que aceptar las dos. */
static uint8_t resto_encaja(uint16_t r, uint8_t pos)
{
    switch (pos) {
    case 0U: return (uint8_t)(r == RDS_OFF_A);
    case 1U: return (uint8_t)(r == RDS_OFF_B);
    case 2U: return (uint8_t)(r == RDS_OFF_C || r == RDS_OFF_CP);
    default: return (uint8_t)(r == RDS_OFF_D);
    }
}

/* Intenta arreglar el bloque sabiendo en que posicion del grupo estamos. La
 * tercera admite DOS palabras (C y C'), asi que se prueban las dos: probar
 * solo una dejaria sin corregir la mitad de las emisoras. */
static uint8_t corrige_en_pos(uint32_t *blq, uint8_t pos)
{
    uint32_t t;

    switch (pos) {
    case 0U: return rds_corrige(blq, RDS_OFF_A, RDS_RAFAGA_MAX);
    case 1U: return rds_corrige(blq, RDS_OFF_B, RDS_RAFAGA_MAX);
    case 2U:
        t = *blq;
        if (rds_corrige(&t, RDS_OFF_C, RDS_RAFAGA_MAX)) { *blq = t; return 1U; }
        t = *blq;
        if (rds_corrige(&t, RDS_OFF_CP, RDS_RAFAGA_MAX)) { *blq = t; return 1U; }
        return 0U;
    default: return rds_corrige(blq, RDS_OFF_D, RDS_RAFAGA_MAX);
    }
}

static void mete_bit(uint8_t bit)
{
    uint16_t r;

    s_reg = ((s_reg << 1) | (uint32_t)bit) & 0x3FFFFFFUL;
    if (s_reg_n < 26U) {
        s_reg_n++;
        return;
    }

    if (!s_sync) {
        /*
         * BUSCANDO. Cada bit nuevo se mira a ver si los ultimos 26 forman un
         * bloque valido.
         *
         * NO BASTA CON QUE LO PAREZCA UNA VEZ. El resto son diez bits, asi
         * que una cadena de bits cualquiera lo acierta una de cada 1024
         * veces; con cuatro posiciones posibles, una de cada 256. A 1187
         * bits por segundo eso son casi CINCO falsas alarmas por segundo. Y
         * una falsa alarma no es inocente: engancha, decodifica un grupo de
         * basura y puede escribir un identificador de emisora inventado - se
         * vio en el banco, con ruido salia un PI 0x2449 que no existia.
         *
         * Asi que al encontrar uno se ANOTA y se exige que 26 bits despues
         * venga el bloque que toca en la secuencia A->B->C->D->A. Dos
         * aciertos seguidos son una de cada 65.000, o sea una falsa alarma
         * cada veinte minutos en vez de cinco por segundo.
         */
        uint8_t p;

        if (s_cand) {
            s_cuenta++;
            if (s_cuenta < 26U) { return; }
            s_cuenta = 0U;
            p = (uint8_t)((s_cand_pos + 1U) & 3U);
            if (resto_encaja(rds_resto(s_reg), p)) {
                s_sync = 1U;
                s_enganches++;
                s_pos = p;
                s_malos_seguidos = 0U;
                memset(s_blq_ok, 0, sizeof s_blq_ok);
                memset(s_blq_corr, 0, sizeof s_blq_corr);
                s_blq[p] = (uint16_t)(s_reg >> 10);
                s_blq_ok[p] = 1U;
                s_ok += 2U;   /* el candidato y este */
                s_cand = 0U;
                if (p == 3U) {
                    grupo_decodifica();
                    memset(s_blq_ok, 0, sizeof s_blq_ok);
                    memset(s_blq_corr, 0, sizeof s_blq_corr);
                }
                return;
            }
            /* No era: se sigue buscando desde aqui mismo. */
            s_cand = 0U;
        }

        r = rds_resto(s_reg);
        for (p = 0U; p < 4U; p++) {
            if (resto_encaja(r, p)) {
                s_cand = 1U;
                s_cand_pos = p;
                s_cuenta = 0U;
                return;
            }
        }
        return;
    }

    /* ENGANCHADO: se lee de 26 en 26. */
    s_cuenta++;
    if (s_cuenta < 26U) {
        return;
    }
    s_cuenta = 0U;
    s_pos = (uint8_t)((s_pos + 1U) & 3U);

    r = rds_resto(s_reg);
    if (resto_encaja(r, s_pos)) {
        s_blq[s_pos] = (uint16_t)(s_reg >> 10);
        s_blq_ok[s_pos] = 1U;
        s_ok++;
        s_malos_seguidos = 0U;
    } else if (corrige_en_pos(&s_reg, s_pos)) {
        /* Se ha arreglado. Se cuenta aparte de los que llegaron ya bien: la
         * diferencia entre los dos numeros es, literalmente, cuanto esta
         * trabajando la correccion, y eso hay que poder verlo. */
        s_blq[s_pos] = (uint16_t)(s_reg >> 10);
        s_blq_ok[s_pos] = 1U;
        s_blq_corr[s_pos] = 1U;
        s_ok++;
        s_corregidos++;
        s_malos_seguidos = 0U;
    } else {
        s_blq_ok[s_pos] = 0U;
        s_mal++;
        s_malos_seguidos++;
        /*
         * Cuando se pierde de verdad hay que volver a buscar, pero no a la
         * primera: un bloque malo suelto es un desvanecimiento, y tirar el
         * sincronismo por eso obligaria a reengancharse constantemente. Tres
         * grupos enteros seguidos sin un solo bloque bueno ya no es ruido.
         */
        if (s_malos_seguidos >= 12U) {
            s_sync = 0U;
            s_perdidas++;
            s_cand = 0U;
            s_reg_n = 0U;
            memset(s_blq_ok, 0, sizeof s_blq_ok);
            memset(s_blq_corr, 0, sizeof s_blq_corr);
        }
    }

    if (s_pos == 3U) {
        grupo_decodifica();
        memset(s_blq_ok, 0, sizeof s_blq_ok);
        memset(s_blq_corr, 0, sizeof s_blq_corr);
    }
}

/* ==========================================================================
 * SIMBOLOS
 * ========================================================================== */
static void simbolo(float v)
{
    uint8_t bit;

    /*
     * Decodificacion DIFERENCIAL. El transmisor manda cada bit sumado
     * (modulo 2) al anterior, asi que aqui se resta. Ademas de ser lo que
     * manda la norma, esto resuelve gratis la ambiguedad de 180 grados que
     * el lazo de Costas no puede resolver: si la fase se engancha del reves,
     * TODOS los bits salen invertidos, y al restar dos consecutivos la
     * inversion se cancela.
     */
    bit = (uint8_t)(v > 0.0f);
    if (!s_hay_bit_ant) {
        s_bit_ant = bit;
        s_hay_bit_ant = 1U;
        return;
    }
    mete_bit((uint8_t)(bit ^ s_bit_ant));
    s_bit_ant = bit;

    /*
     * LA OTRA AMBIGUEDAD, la que si hay que resolver a mano: el reloj de
     * simbolo se engancha a la transicion de mitad de simbolo, pero una
     * senal bifase tiene transiciones tambien en los bordes, asi que el lazo
     * puede quedarse medio simbolo corrido. En esa posicion los bits salen
     * de mezclar dos simbolos y no se engancha nunca. Si despues de un buen
     * rato buscando no hay sincronismo, se corre medio simbolo y se prueba
     * la otra.
     */
    if (!s_sync) {
        s_busca_simbolos++;
        if (s_busca_simbolos > 2400U) {      /* unos dos segundos */
            s_busca_simbolos = 0U;
            s_medio_simbolo ^= 1U;
            s_sim_fase += 0x80000000UL;      /* medio simbolo */
            s_hay_bit_ant = 0U;
            s_reg_n = 0U;
        }
    } else {
        s_busca_simbolos = 0U;
    }
}

/* ==========================================================================
 * UNA MUESTRA EN BANDA BASE (12 kHz)
 * ========================================================================== */
#define COSTAS_KP   0.008f
#define COSTAS_KI   0.0000020f

static void banda_base(float i_in, float q_in)
{
    float c, s, ir, qr, g;
    uint32_t fase_ant;

    /* --- normalizar el nivel --- */
    {
        float m = (i_in >= 0.0f ? i_in : -i_in) + (q_in >= 0.0f ? q_in : -q_in);
        s_agc += 0.002f * (m - s_agc);
        if (s_agc < 1e-7f) { s_agc = 1e-7f; }
        g = 1.0f / s_agc;
        /* Un tope: sin senal, la ganancia se dispararia y el ruido entraria
         * a tope en los lazos. Con el tope, sin senal simplemente no hay
         * nada que enganchar, que es lo correcto. */
        if (g > 1.0e6f) { g = 1.0e6f; }
        i_in *= g;
        q_in *= g;
    }

    /* --- lazo de Costas: girar por la fase estimada --- */
    c = cosf(s_cos_fase);
    s = sinf(s_cos_fase);
    ir =  i_in * c + q_in * s;
    qr = -i_in * s + q_in * c;

    {
        /*
         * Detector de error para BPSK: el signo de la componente en fase
         * multiplicado por la componente en cuadratura. Con la fase bien, la
         * energia esta toda en I y Q vale cero pase lo que pase con los
         * datos; el signo de I quita la modulacion.
         */
        float e = (ir >= 0.0f ? qr : -qr);
        /* Recortar el error evita que un pico de ruido tire del lazo de
         * golpe; sin esto un chasquido descoloca la fase durante medio
         * segundo. */
        if (e > 1.0f) { e = 1.0f; }
        if (e < -1.0f) { e = -1.0f; }
        s_cos_frec += COSTAS_KI * e;
        if (s_cos_frec > 0.02f) { s_cos_frec = 0.02f; }
        if (s_cos_frec < -0.02f) { s_cos_frec = -0.02f; }
        s_cos_fase += COSTAS_KP * e + s_cos_frec;
        if (s_cos_fase > 3.14159265f)  { s_cos_fase -= 6.28318531f; }
        if (s_cos_fase < -3.14159265f) { s_cos_fase += 6.28318531f; }
    }

    /* --- reloj de simbolo e integracion por mitades --- */
    fase_ant = s_sim_fase;
    s_sim_fase += s_sim_inc;

    if (fase_ant < 0x80000000UL && s_sim_fase >= 0x80000000UL) {
        /* justo en mitad del simbolo: aqui la senal bifase cruza por cero
         * SIEMPRE, y eso es lo que sirve de referencia de reloj */
        s_medio = ir;
        s_vi_medio = 1U;
    }

    if (s_sim_fase >= 0x80000000UL) {
        s_acc2 += ir; s_n2++;
    } else {
        s_acc1 += ir; s_n1++;
    }

    if (s_sim_fase < fase_ant) {
        /* ha dado la vuelta: fin de simbolo */
        float v = 0.0f;
        if (s_n1 && s_n2) {
            v = s_acc1 / (float)s_n1 - s_acc2 / (float)s_n2;
        }

        /*
         * Error de reloj. Si el instante que creemos que es la mitad del
         * simbolo estuviera bien, la senal valdria cero ahi. Lo que valga
         * dice cuanto nos hemos pasado, y el signo del simbolo dice hacia
         * que lado.
         */
        if (s_vi_medio) {
            float e = (v >= 0.0f ? -s_medio : s_medio);
            if (e > 1.0f) { e = 1.0f; }
            if (e < -1.0f) { e = -1.0f; }
            s_sim_err_int += SIM_KI * e;
            if (s_sim_err_int > SIM_KI_TOPE)  { s_sim_err_int = SIM_KI_TOPE; }
            if (s_sim_err_int < -SIM_KI_TOPE) { s_sim_err_int = -SIM_KI_TOPE; }
            /*
             * La fase, de golpe - pero SIN dejar que se pase de cero hacia
             * atras. Esto costo encontrarlo: la correccion se aplica justo
             * despues de que el acumulador haya dado la vuelta, o sea cuando
             * vale casi nada, y una correccion negativa un poco grande lo
             * metia por debajo de cero. Al ser un entero sin signo, "por
             * debajo de cero" es "casi 2^32", asi que el simbolo siguiente
             * tardaba una vuelta ENTERA en llegar: un simbolo perdido, la
             * alineacion de 26 bits rota, y el sincronismo de bloque al
             * suelo. Se veia como "va perfecto siete segundos y de repente
             * se cae", con los dos lazos enganchados y sin una sola
             * explicacion a la vista.
             */
            {
                int32_t corr = (int32_t)(SIM_KP * e * 4294967296.0f * 0.01f);
                if (corr < 0 && (uint32_t)(-corr) > s_sim_fase) {
                    corr = -(int32_t)s_sim_fase;
                }
                s_sim_fase = (uint32_t)((int32_t)s_sim_fase + corr);
            }
            /* Y la frecuencia, SIEMPRE calculada del nominal - no acumulada
             * encima de si misma. Ver el comentario de SIM_KP. */
            s_sim_inc = (uint32_t)((int32_t)s_sim_inc_nom
                                   + (int32_t)((float)s_sim_inc_nom * s_sim_err_int));
        }

        simbolo(v);
        s_acc1 = s_acc2 = 0.0f;
        s_n1 = s_n2 = 0U;
        s_vi_medio = 0U;
    }
}

/* ==========================================================================
 * LA ENTRADA
 * ========================================================================== */
void rds_feed(const float *mpx, uint32_t n)
{
    uint32_t k;

    if (!s_on || !mpx) { return; }

    for (k = 0U; k < n; k++) {
        float sn, cs, vi, vq;

        /* 1. bajar los 57 kHz a cero */
        nco_paso(&s_nco, &sn, &cs);
        vi = mpx[k] * cs;
        vq = -mpx[k] * sn;

        /* 2. meter en el historial (dos veces, ver s_hi) */
        s_hi[s_hidx] = vi;
        s_hi[s_hidx + K_RDS_DEC_TAPS] = vi;
        s_hq[s_hidx] = vq;
        s_hq[s_hidx + K_RDS_DEC_TAPS] = vq;
        s_hidx++;
        if (s_hidx >= K_RDS_DEC_TAPS) { s_hidx = 0U; }

        /* 3. una salida de cada RDS_DEC: solo entonces se hace el producto */
        s_dec_cnt++;
        if (s_dec_cnt >= RDS_DEC) {
            const float *pi = &s_hi[s_hidx];
            const float *pq = &s_hq[s_hidx];
            float ai = 0.0f, aq = 0.0f;
            uint16_t t;

            s_dec_cnt = 0U;
            for (t = 0U; t < K_RDS_DEC_TAPS; t++) {
                ai += k_rds_dec[t] * pi[t];
                aq += k_rds_dec[t] * pq[t];
            }
            s_muestras++;
            banda_base(ai, aq);
        }
    }
}

/*
 * CUANTOS MILISEGUNDOS HACE QUE ACABO EL ULTIMO GRUPO DE HORA.
 *
 * La resta se hace en enteros sin signo a proposito: cuando s_muestras da la
 * vuelta (a las 99 horas), la diferencia sigue saliendo bien sola.
 *
 * Devuelve 0 si todavia no ha llegado ninguna hora. Quien llame tiene que
 * mirar antes tiene_hora, que para eso esta.
 */
uint32_t rds_hora_atraso_ms(void)
{
    float fs_bb = s_fs / (float)RDS_DEC;
    uint32_t d;

    if (!s_on || !s_info.tiene_hora || fs_bb <= 0.0f) { return 0UL; }
    d = s_muestras - s_info.hora_muestra;
    return (uint32_t)(((float)d * 1000.0f) / fs_bb + 0.5f);
}

/* La tasa a la que corre hora_muestra: la del multiplex dividida por el
 * diezmado. Se expone para que nadie tenga que escribir el 16 por su cuenta -
 * el mismo numero en dos sitios es el fallo que mas veces ha mordido en este
 * proyecto. */
float rds_fs_bb(void)
{
    return s_fs / (float)RDS_DEC;
}

/* Solo para el banco: la muestra de banda base en la que va el decodificador
 * ahora mismo. Con esto y con hora_muestra se mide el retardo propio del
 * decodificador sin tener que fiarse de ninguna cuenta hecha a mano. */
uint32_t rds_muestras_dbg(void)
{
    return s_muestras;
}

void rds_cuentas_dbg(uint32_t *enganches, uint32_t *perdidas)
{
    if (enganches) { *enganches = s_enganches; }
    if (perdidas)  { *perdidas = s_perdidas; }
}

void rds_lazos_dbg(float *cos_frec_hz, float *sim_err_ppm, float *nivel)
{
    if (cos_frec_hz) {
        *cos_frec_hz = s_cos_frec * (s_fs / (float)RDS_DEC) / 6.28318531f;
    }
    if (sim_err_ppm) { *sim_err_ppm = s_sim_err_int * 1.0e6f; }
    if (nivel)       { *nivel = s_agc; }
}
