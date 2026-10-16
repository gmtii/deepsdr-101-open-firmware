/*
 * EL BANCO DEL 4415 / 110A A 75 bps.
 *
 * Pedido por el usuario.
 *
 * QUE TIENE QUE DEMOSTRAR ESTE BANCO, y en este orden:
 *
 *   1. Que sobre una señal GENERADA AQUI, con la que se sabe la verdad
 *      simbolo a simbolo, el modulo de la radio saca los veinticuatro
 *      segmentos, la cuenta atras entera y los dibits que se metieron.
 *   2. Que lo sigue haciendo con la portadora corrida, con la fase de
 *      muestreo en el peor sitio y con ruido.
 *   3. Que sobre la GRABACION DEL DUEÑO saca lo mismo que saco el analisis
 *      en Python: 24 segmentos, cuenta 23->0 sin huecos, D1=D2=5.
 *   4. Y que NO engancha donde no hay nada: ruido blanco, y Robser-s.mp3,
 *      que ya se midio que no es 75 bps de 110A.
 *
 * EL PUNTO 4 ES EL QUE HACE QUE ESTO SEA UN BANCO. Un detector que diga
 * "enganchado" sobre ruido blanco no vale nada por muy bien que lea la
 * grabacion buena, y un banco que no lo compruebe no esta probando: esta
 * mirando.
 *
 * LA FASE DE MUESTREO, que es el punto 2 y la razon de que el anillo de
 * s4415.c vaya a 12 kHz. Si se decima por cinco con un contador fijo, entre
 * la fase que toca y la que sale puede haber media muestra... o dos y media,
 * que es MEDIO SIMBOLO. El generador de aqui pone el retardo donde quiere,
 * con decimales, justamente para poder ponerlo en el peor sitio y ver si se
 * cae.
 */
#include "s4415.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fallos = 0;
static void di(const char *que, int ok, const char *det)
{
    printf("    %-52s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

/* --- el prestamo de la cascada, igual de grande que en la radio --------- */
#define PRESTAMO 54432U
static float g_ram[PRESTAMO / 4U];

/* --- las tablas, otra vez, A PROPOSITO ---------------------------------- *
 * Se vuelven a escribir aqui en vez de sacarlas de s4415.c. Si el generador
 * y el decodificador comparten la tabla, una tabla equivocada da un banco
 * verde con una radio sorda. Estas salen del estandar, no del codigo.      */
static const uint8_t t_xvii[8] = { 0x00, 0x55, 0x33, 0x66, 0x0F, 0x5A, 0x3C, 0x69 };
static const uint8_t t_scr[32] = {
    7,4,3,0,5,1,5,0,2,2,1,1,5,7,4,3,5,0,2,6,2,1,6,2,0,0,5,0,5,2,6,6
};
static uint8_t trib(uint8_t p, int i) { return ((p >> (7 - (i & 7))) & 1u) ? 4u : 0u; }

/* El aleatorizador de datos, figura 6: 12 etapas, 0xBAD, ocho pasos. */
static void gen_scr(uint8_t *out, int n)
{
    uint8_t b[13]; int k, j, p;
    static const char ini[12] = { '1','0','1','1','1','0','1','0','1','1','0','1' };
    for (j = 0; j < 12; j++) { b[12 - j] = (uint8_t)(ini[j] - '0'); }
    b[0] = 0;
    for (k = 0; k < n; k++) {
        for (p = 0; p < 8; p++) {
            uint8_t fb = b[12], nb[13];
            nb[0] = 0; nb[1] = fb;
            nb[2] = (uint8_t)(b[1] ^ fb);
            nb[3] = b[2]; nb[4] = b[3];
            nb[5] = (uint8_t)(b[4] ^ fb);
            nb[6] = b[5];
            nb[7] = (uint8_t)(b[6] ^ fb);
            for (j = 8; j < 13; j++) { nb[j] = b[j - 1]; }
            for (j = 0; j < 13; j++) { b[j] = nb[j]; }
        }
        out[k] = (uint8_t)((b[3] << 2) | (b[2] << 1) | b[1]);
    }
}

/* --- el generador -------------------------------------------------------- */

#define FS     12000.0
#define SPS    5
#define PORT   1800.0
#define ALFA   0.35
#define SPAN   6

static double rrc(double t)
{
    double d;
    if (fabs(t) < 1e-9) { return 1.0 - ALFA + 4.0 * ALFA / M_PI; }
    d = 1.0 - (4.0 * ALFA * t) * (4.0 * ALFA * t);
    if (fabs(d) < 1e-7) {
        return (ALFA / sqrt(2.0)) *
               ((1.0 + 2.0 / M_PI) * sin(M_PI / (4.0 * ALFA)) +
                (1.0 - 2.0 / M_PI) * cos(M_PI / (4.0 * ALFA)));
    }
    return (sin(M_PI * t * (1.0 - ALFA)) +
            4.0 * ALFA * t * cos(M_PI * t * (1.0 + ALFA))) / (M_PI * t * d);
}

/*
 * DE TEXTO A DIBITS: la cadena del transmisor entera, escrita aqui a partir
 * del estandar y NO sacada de s4415.c. Si el generador y el decodificador
 * comparten el codigo, lo unico que se prueba es que una funcion es inversa
 * de si misma.
 *
 *   texto -> bits ASCII (8, el menos significativo primero)
 *         -> convolucional K=7 rate 1/2, T1 = x6+x4+x3+x+1, T2 = x6+x5+x4+x3+1
 *         -> entrelazador 20 x 36, carga con la fila de 7 en 7 modulo 20
 *         -> lectura con la columna bajando de 7 en 7 modulo 36
 *         -> pares de bits = dibits, 360 por bloque
 *         -> el set 360 de cada bloque -el ultimo- va con el patron
 *            excepcional (tabla XIVb)
 */
#define ENT_F 20
#define ENT_C 36
#define ENT_N (ENT_F * ENT_C)        /* 720 bits codificados por bloque */
#define ENT_D (ENT_N / 2)            /* 360 bits de datos por bloque */

static uint32_t par32(uint32_t x)
{ x^=x>>16; x^=x>>8; x^=x>>4; x^=x>>2; x^=x>>1; return x & 1u; }

/* Devuelve cuantos sets ha escrito en `sets` (indice 0..7, >=4 excepcional) */
static int texto_a_sets(const char *txt, uint8_t *sets, int max_sets)
{
    static uint8_t bits[8 * ENT_D];
    static uint8_t cod[ENT_N];
    static uint8_t lec[ENT_N];
    int nb = 0, nbloques, b, k, i;
    uint32_t reg = 0;
    const char *q;

    for (q = txt; *q && nb + 8 <= (int)sizeof bits; q++) {
        for (i = 0; i < 8; i++) { bits[nb++] = (uint8_t)((*q >> i) & 1); }
    }
    /* el EOM del estandar, 4B65A5B2, con el bit mas alto primero */
    {
        uint32_t eom = 0x4B65A5B2UL;
        for (i = 31; i >= 0 && nb < (int)sizeof bits; i--) {
            bits[nb++] = (uint8_t)((eom >> i) & 1u);
        }
    }
    /* el vaciado del estandar: 144 bits a cero detras del EOM, mas los que
     * hagan falta para completar el bloque (5.3.2.3.2) */
    for (i = 0; i < 144 && nb < (int)sizeof bits; i++) { bits[nb++] = 0; }
    while ((nb % ENT_D) != 0 && nb < (int)sizeof bits) { bits[nb++] = 0; }
    nbloques = nb / ENT_D;

    for (b = 0; b < nbloques; b++) {
        /* codificar: el codigo es CONTINUO, el registro no se reinicia */
        for (i = 0; i < ENT_D; i++) {
            uint32_t r7 = (reg << 1) | bits[b * ENT_D + i];
            cod[2 * i]     = (uint8_t)par32(r7 & 0x6du);
            cod[2 * i + 1] = (uint8_t)par32(r7 & 0x4fu);
            reg = r7 & 0x3Fu;
        }
        /* entrelazar: cargar y leer */
        {
            static uint8_t mat[ENT_F][ENT_C];
            for (i = 0; i < ENT_N; i++) {
                mat[(7 * (i % ENT_F)) % ENT_F][i / ENT_F] = cod[i];
            }
            for (k = 0; k < ENT_N; k++) {
                int r = k % ENT_F;
                int c = ((k / ENT_F) + ENT_C * 7 - 7 * r) % ENT_C;
                lec[k] = mat[r][c];
            }
        }
        for (k = 0; k < ENT_D; k++) {
            int idx = b * ENT_D + k;
            int dib;
            if (idx >= max_sets) { return idx; }
            dib = (lec[2 * k] << 1) | lec[2 * k + 1];   /* alto primero */
            sets[idx] = (uint8_t)(dib + ((k == ENT_D - 1) ? 4 : 0));
        }
    }
    return nbloques * ENT_D;
}

/*
 * Construye los tribits de una emision: 24 segmentos de preambulo con la
 * cuenta atras de verdad, y detras `n_dib` sets de datos.
 */
static int haz_tribits(uint8_t *tb, int max, const uint8_t *dib, int n_dib,
                       uint8_t d1, uint8_t d2, int n_seg)
{
    static uint8_t scr[160];
    int n = 0, s, j, i;
    gen_scr(scr, 160);

    for (s = 0; s < n_seg; s++) {
        int cuenta = n_seg - 1 - s;
        uint8_t sim[15] = { 0,1,3,0,1,3,1,2,0, 0,0,0,0,0, 0 };
        sim[9]  = d1;
        sim[10] = d2;
        /* tabla XVI: dos bits con un 1 delante -> simbolo 4..7 */
        sim[11] = (uint8_t)(4 + ((cuenta >> 4) & 3));
        sim[12] = (uint8_t)(4 + ((cuenta >> 2) & 3));
        sim[13] = (uint8_t)(4 + ( cuenta       & 3));
        for (j = 0; j < 15; j++) {
            for (i = 0; i < 32; i++) {
                if (n >= max) { return n; }
                tb[n++] = (uint8_t)((trib(t_xvii[sim[j]], i) + t_scr[i]) % 8);
            }
        }
    }
    for (j = 0; j < n_dib; j++) {
        for (i = 0; i < 32; i++) {
            int si = (j * 32 + i) % 160;
            if (n >= max) { return n; }
            tb[n++] = (uint8_t)((trib(t_xvii[dib[j] & 7], i) + scr[si]) % 8);
        }
    }
    return n;
}

/*
 * Modula: PSK de 1800 Hz con conformado RRC, con un retardo `tau` en
 * muestras (con decimales), un error de portadora `df` en hercios y ruido.
 */
static float *modula(const uint8_t *tb, int n_tb, double tau, double df,
                     double snr_db, uint32_t *n_out)
{
    int n = (n_tb + 2 * SPAN) * SPS + 600;   /* cola, para que el ultimo set entre entero */
    float *x = (float *)calloc((size_t)n, sizeof *x);
    double *br = (double *)calloc((size_t)n, sizeof *br);
    double *bi = (double *)calloc((size_t)n, sizeof *bi);
    int m, k;
    double pot = 0.0, sig;
    unsigned rnd = 12345u;

    for (m = 0; m < n_tb; m++) {
        double a = M_PI / 4.0 * (double)tb[m];
        double ar = cos(a), ai = sin(a);
        int c = m * SPS + SPAN * SPS;
        for (k = -SPAN * SPS; k <= SPAN * SPS; k++) {
            int p = c + k;
            double h;
            if (p < 0 || p >= n) { continue; }
            h = rrc(((double)k - tau) / (double)SPS);
            br[p] += ar * h; bi[p] += ai * h;
        }
    }
    for (k = 0; k < n; k++) {
        double w = 2.0 * M_PI * (PORT + df) * (double)k / FS;
        double v = br[k] * cos(w) - bi[k] * sin(w);
        x[k] = (float)v;
        pot += v * v;
    }
    sig = sqrt(pot / (double)n) * pow(10.0, -snr_db / 20.0);
    if (snr_db < 90.0) {
        for (k = 0; k < n; k++) {
            double u1, u2, g;
            rnd = rnd * 1103515245u + 12345u; u1 = ((rnd >> 8) & 0xFFFFFu) / 1048576.0 + 1e-9;
            rnd = rnd * 1103515245u + 12345u; u2 = ((rnd >> 8) & 0xFFFFFu) / 1048576.0;
            g = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
            x[k] += (float)(sig * g);
        }
    }
    free(br); free(bi);
    *n_out = (uint32_t)n;
    return x;
}

/* --- una pasada entera por el modulo de la radio ------------------------- */

typedef struct {
    char    texto[512];
    char    vistas[12][220];
    int     n_vistas;
    int     bloques, bits_dec, eom, desalineos, sin_marca, excep, fin_tx;
    int     exc_sitio[4];
    int     segmentos;
    int     cuentas[64];
    int     n_cuentas;
    uint8_t d1, d2;
    uint8_t bps_idx, largo;
    int     n_dib;
    uint8_t dib[4096];
    unsigned perdidas;
    int      corr_max;
} res_t;

static uint8_t g_fmt = S4415_ASCII;

static res_t pasada(const float *x, uint32_t n)
{
    res_t r; uint32_t i; uint16_t k; uint8_t ant_seg = 0;
    memset(&r, 0, sizeof r);
    if (!s4415_init(g_ram, (uint32_t)(sizeof g_ram / sizeof g_ram[0]))) {
        printf("    s4415_init() dijo que no\n"); return r;
    }
    s4415_formato(g_fmt);

    for (i = 0; i + 256U <= n; i += 256U) {
        s4415_est_t e;
        uint8_t buf[64];
        uint16_t got;
        s4415_mete(&x[i], 256U);
        (void)s4415_paso();
        s4415_estado(&e);
        if (e.segmentos != ant_seg) {
            ant_seg = (uint8_t)e.segmentos;
            if (r.n_cuentas < 64) { r.cuentas[r.n_cuentas++] = e.cuenta; }
            r.d1 = e.d1; r.d2 = e.d2;
            r.bps_idx = e.bps_idx; r.largo = e.largo;
            if (e.corr > r.corr_max) { r.corr_max = e.corr; }
        }
        if ((int)e.bloques != r.bloques) {
            r.bloques = (int)e.bloques;
            if (r.n_vistas < 12) {
                uint8_t li;
                char *v = r.vistas[r.n_vistas++];
                v[0] = '\0';
                for (li = 0U; li < s4415_nlineas(); li++) {
                    strncat(v, s4415_linea(li), 219U - strlen(v));
                }
            }
        }
        got = s4415_saca_dibits(buf, 64U);
        for (k = 0U; k < got; k++) {
            if (r.n_dib < 4096) { r.dib[r.n_dib] = buf[k]; }
            r.n_dib++;
        }
    }
    {
        s4415_est_t e; s4415_estado(&e);
        uint8_t li;
        r.segmentos  = e.segmentos;
        r.bloques    = e.bloques;
        r.bits_dec   = e.bits;
        r.eom        = e.eom;
        r.fin_tx     = e.fin_tx;
        r.desalineos = e.desalineos;
        r.sin_marca  = e.sin_marca;
        r.excep      = e.excepcionales;
        for (li = 0U; li < 4U; li++) { r.exc_sitio[li] = (int)s4415_excepcional(li); }
        r.texto[0] = '\0';
        for (li = 0U; li < s4415_nlineas(); li++) {
            strncat(r.texto, s4415_linea(li), sizeof r.texto - strlen(r.texto) - 2U);
            strncat(r.texto, "\n", sizeof r.texto - strlen(r.texto) - 1U);
        }
    }
    r.perdidas = (unsigned)s4415_perdidas();
    s4415_fin();
    return r;
}

static int cuenta_baja(const res_t *r, int desde)
{
    int k;
    if (r->n_cuentas < desde + 1) { return 0; }
    for (k = 0; k <= desde; k++) {
        if (r->cuentas[k] != desde - k) { return 0; }
    }
    return 1;
}

/*
 * Lo mismo pero sin exigir por donde empieza. Una grabacion de verdad puede
 * arrancar con el preambulo ya empezado, y entonces faltan los primeros
 * segmentos sin que nada este mal. Lo que NO puede fallar es que la cuenta
 * baje de uno en uno y termine en cero.
 */
static int cuenta_sana(const res_t *r)
{
    int k;
    if (r->n_cuentas < 3) { return 0; }
    if (r->cuentas[r->n_cuentas - 1] != 0) { return 0; }
    for (k = 1; k < r->n_cuentas; k++) {
        if (r->cuentas[k] != r->cuentas[k - 1] - 1) { return 0; }
    }
    return 1;
}

static void pinta_cuenta(const res_t *r)
{
    int k;
    printf("      cuenta atras:");
    for (k = 0; k < r->n_cuentas; k++) { printf(" %d", r->cuentas[k]); }
    printf("\n");
}

static void pinta_dibits(const res_t *r)
{
    int k, c[4] = { 0, 0, 0, 0 };
    for (k = 0; k < r->n_dib && k < 4096; k++) { c[r->dib[k] & 3]++; }
    if (r->n_dib == 0) { return; }
    printf("      dibits: %d   reparto 00/01/10/11: %d%% %d%% %d%% %d%%\n",
           r->n_dib,
           100 * c[0] / (r->n_dib < 4096 ? r->n_dib : 4096),
           100 * c[1] / (r->n_dib < 4096 ? r->n_dib : 4096),
           100 * c[2] / (r->n_dib < 4096 ? r->n_dib : 4096),
           100 * c[3] / (r->n_dib < 4096 ? r->n_dib : 4096));
}

/* --- lector de wav ------------------------------------------------------- */

static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

static float *lee_wav(const char *ruta, uint32_t *n_out, uint32_t *hz_out)
{
    FILE *f = fopen(ruta, "rb");
    uint8_t cab[12];
    uint16_t canales = 1U, bits = 16U;
    uint32_t hz = 0UL;
    if (!f) { return 0; }
    if (fread(cab, 1, 12, f) != 12U) { fclose(f); return 0; }
    for (;;) {
        uint8_t ch[8]; uint32_t len;
        if (fread(ch, 1, 8, f) != 8U) { break; }
        len = le32(&ch[4]);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fm[16];
            uint32_t q = (len < 16U) ? len : 16U;
            if (fread(fm, 1, q, f) != q) { break; }
            canales = le16(&fm[2]); hz = le32(&fm[4]); bits = le16(&fm[14]);
            if (len > 16U) { fseek(f, (long)(len - 16U), SEEK_CUR); }
        } else if (memcmp(ch, "data", 4) == 0) {
            uint32_t m = len / (uint32_t)((bits / 8U) * canales), k;
            int16_t *raw = (int16_t *)malloc(len);
            float *x;
            if (!raw) { break; }
            if (fread(raw, 1, len, f) != len) { free(raw); break; }
            x = (float *)malloc((size_t)m * sizeof *x);
            for (k = 0U; k < m; k++) { x[k] = (float)raw[k * canales] / 32768.0f; }
            free(raw);
            *n_out = m; *hz_out = hz;
            fclose(f);
            return x;
        } else { fseek(f, (long)len, SEEK_CUR); }
        if (len & 1U) { fseek(f, 1L, SEEK_CUR); }
    }
    fclose(f);
    return 0;
}

/* --- las pruebas --------------------------------------------------------- */

static uint8_t g_tb[800000];
static uint8_t g_sets[2048];

/* El texto que va dentro. Si sale esto, la cadena entera esta bien: el
 * desensanchado, el desentrelazado, el Viterbi y los caracteres. */
static const char k_mensaje[] =
    "PRUEBA DEEPSDR RELOAD 4415 75 BPS ENTRELAZADO LARGO. "
    "SI LEES ESTO EL VITERBI Y EL DESENTRELAZADOR ESTAN BIEN PUESTOS. ";

static void sintetica(const char *nombre, double tau, double df, double snr,
                      int exige_texto)
{
    uint32_t n = 0; float *x; res_t r; char det[200];
    int n_tb, n_sets;

    n_sets = texto_a_sets(k_mensaje, g_sets, (int)(sizeof g_sets));
    n_tb = haz_tribits(g_tb, (int)(sizeof g_tb), g_sets, n_sets, 5U, 5U, 24);
    x = modula(g_tb, n_tb, tau, df, snr, &n);

    printf("  %s\n", nombre);
    r = pasada(x, n);
    free(x);

    snprintf(det, sizeof det, "(%d segmentos, corr max %d%%)", r.segmentos, r.corr_max);
    di("    los 24 segmentos del preambulo", r.segmentos == 24, det);
    di("    la cuenta baja 23..0 sin un hueco", cuenta_baja(&r, 23), 0);
    snprintf(det, sizeof det, "(D1=%u D2=%u -> %s bps, %s)", r.d1, r.d2,
             s4415_bps_txt(r.bps_idx), r.largo ? "largo" : "corto");
    di("    D1=5 D2=5 -> 75 bps, entrelazado largo",
       r.d1 == 5U && r.d2 == 5U && r.bps_idx == 5U && r.largo == 1U, det);
    snprintf(det, sizeof det, "(%u muestras)", r.perdidas);
    di("    el barrido no se dejo ninguna muestra", r.perdidas == 0U, det);

    snprintf(det, sizeof det, "(%d sets excepcionales en %d %d %d %d)", r.excep,
             r.exc_sitio[0], r.exc_sitio[1], r.exc_sitio[2], r.exc_sitio[3]);
    di("    el set excepcional cae en el 359 de cada bloque",
       r.excep >= 1 && r.exc_sitio[0] == ENT_D - 1, det);
    snprintf(det, sizeof det, "(%d desalineos, %d bloques sin marca)",
             r.desalineos, r.sin_marca);
    di("    y cuadra con el bloque que dice el preambulo",
       r.desalineos == 0 && r.sin_marca == 0, det);

    if (exige_texto) {
        snprintf(det, sizeof det, "(%d bloques, %d bits)", r.bloques, r.bits_dec);
        di("    sale texto del Viterbi", r.bloques >= 2 && r.bits_dec > 0, det);
        {
            int k, p1 = 0, p2 = 0;
            for (k = 0; k < r.n_vistas; k++) {
                if (strstr(r.vistas[k], "PRUEBA DEEPSDR RELOAD 4415")) { p1 = 1; }
                if (strstr(r.vistas[k], "ESTAN BIEN PUESTOS")) { p2 = 1; }
            }
            snprintf(det, sizeof det, "(%d vistas; primera: %.60s)",
                     r.n_vistas, r.n_vistas ? r.vistas[0] : "");
            di("    y es el mensaje que se metio, de principio a fin",
               p1 && p2, det);
        }
        snprintf(det, sizeof det, "(%d)", r.eom);
        di("    sale el EOM 4B65A5B2 del estandar", r.eom >= 1, det);
        di("    y la emision se da por terminada ahi", r.fin_tx == 1, 0);
    }
    printf("\n");
}

/*
 * ===================================================================
 * CUANTO CUESTA. No si acierta: CUANTO CUESTA.
 *
 * Pedido por el usuario, 07/10/2026.
 *
 * La primera version de este banco comprobaba que el modulo ACIERTA, y
 * acertaba. Lo que no comprobaba es que la radio pueda con el, y la radio no
 * podia: la interrupcion de audio hacia dos llamadas a la libreria de
 * matematicas y un filtro de 41 tomas POR MUESTRA, o sea veinticuatro mil
 * llamadas por segundo dentro de la interrupcion. Un banco en un PC de 3 GHz
 * no ve eso jamas si solo mira el resultado.
 *
 * Asi que ahora el banco mide dos cosas mas, y las dos pueden fallar:
 *
 *   1. s4415_mete() -lo que corre DENTRO de la interrupcion- tiene que ser
 *      practicamente gratis. Es una copia a una cola y nada mas.
 *   2. Cuantas veces se dice "hay noticia" por segundo de audio. Cada una de
 *      esas repinta el panel entero en la radio. La primera version decia
 *      que habia noticia en cada dibit: SETENTA Y CINCO POR SEGUNDO.
 * ===================================================================
 */
static double ahora(void)
{
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static void coste(const char *nombre, const float *x, uint32_t n)
{
    double tm = 0.0, tp = 0.0, t0, dur = (double)n / 12000.0;
    uint32_t i, noticias = 0U;
    char det[160];

    if (!s4415_init(g_ram, (uint32_t)(sizeof g_ram / sizeof g_ram[0]))) { return; }
    for (i = 0U; i + 256U <= n; i += 256U) {
        t0 = ahora(); s4415_mete(&x[i], 256U); tm += ahora() - t0;
        t0 = ahora(); if (s4415_paso()) { noticias++; } tp += ahora() - t0;
    }
    s4415_fin();

    printf("  %s\n", nombre);
    snprintf(det, sizeof det, "(%.2f%% del tiempo real; el resto de la radio vive ahi)",
             100.0 * tm / dur);
    /*
     * 0,1% Y NO 2%, Y EL NUMERO IMPORTA. La version que colgaba la radio
     * medía 0,27% aqui -dos llamadas a la libreria de matematicas y un
     * filtro de 41 tomas por muestra-; la que solo copia mide 0,01%. Un tope
     * flojo habria dejado pasar la mala. El tope tiene que estar entre las
     * dos, no por encima de las dos.
     */
    di("    la interrupcion de audio solo copia", (tm / dur) < 0.001, det);
    snprintf(det, sizeof det, "(%.2f%% del tiempo real en un PC; la radio va ~15x mas despacio)",
             100.0 * (tm + tp) / dur);
    di("    y el total cabe de sobra en el bucle principal",
       ((tm + tp) / dur) < 0.01, det);
    snprintf(det, sizeof det, "(%.1f por segundo; cada una repinta el panel entero)",
             (double)noticias / dur);
    di("    no se dice 'hay noticia' mas de diez veces por segundo",
       ((double)noticias / dur) < 10.0, det);
    printf("\n");
}

int main(int argc, char **argv)
{
    printf("\nSTANAG 4415 / MIL-STD-188-110A a 75 bps\n");
    printf("=======================================\n\n");
    printf("  (el estado pide %u bytes del prestamo de la cascada, que tiene %u)\n\n",
           (unsigned)s4415_ram_bytes(), (unsigned)PRESTAMO);
    if (s4415_ram_bytes() > PRESTAMO) {
        printf("  *** NO CABE EN EL PRESTAMO ***\n");
        return 1;
    }

    /* 1 — la señal perfecta, para saber que el camino esta bien puesto */
    sintetica("limpia, sin retardo ni error de portadora", 0.0, 0.0, 99.0, 1);

    /* 2 — la fase de muestreo en el peor sitio posible: media muestra */
    sintetica("fase de muestreo a 2,5 muestras (medio simbolo)", 2.5, 0.0, 99.0, 1);

    /* 3 — portadora corrida 20 Hz, que es lo normal sintonizando a oido */
    sintetica("portadora +20 Hz y fase 1,3 muestras", 1.3, 20.0, 99.0, 1);

    /* 4 — con ruido */
    sintetica("ruido a 6 dB de relacion señal-ruido", 1.3, -12.0, 6.0, 1);

    /* 4b — CUANTO CUESTA, sobre una emision de verdad y sobre ruido */
    {
        uint32_t n = 0; float *x;
        int n_tb, n_sets;
        n_sets = texto_a_sets(k_mensaje, g_sets, (int)(sizeof g_sets));
        n_tb = haz_tribits(g_tb, (int)(sizeof g_tb), g_sets, n_sets, 5U, 5U, 24);
        x = modula(g_tb, n_tb, 1.3, 0.0, 99.0, &n);
        coste("cuanto cuesta, sobre una emision entera", x, n);
        free(x);
    }

    /* 5 — RUIDO BLANCO SOLO. Aqui NO puede enganchar. */
    {
        uint32_t n = 240000U, k; unsigned rnd = 99u;
        float *x = (float *)malloc((size_t)n * sizeof *x);
        res_t r; char det[96];
        for (k = 0U; k < n; k++) {
            double u1, u2;
            rnd = rnd * 1103515245u + 12345u; u1 = ((rnd >> 8) & 0xFFFFFu) / 1048576.0 + 1e-9;
            rnd = rnd * 1103515245u + 12345u; u2 = ((rnd >> 8) & 0xFFFFFu) / 1048576.0;
            x[k] = (float)(sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2) * 0.2);
        }
        coste("cuanto cuesta, sobre ruido (que es lo que hay en el dial)", x, n);
        printf("  ruido blanco, 20 segundos  (EL CONTROL: aqui no puede enganchar)\n");
        r = pasada(x, n);
        free(x);
        snprintf(det, sizeof det, "(%d segmentos)", r.segmentos);
        di("    no engancha nada", r.segmentos == 0, det);
        printf("\n");
    }

    /* 6 — las grabaciones de verdad */
    for (; argc > 1; argc--, argv++) {
        const char *ruta = argv[1];
        uint32_t n = 0, hz = 0;
        float *x = lee_wav(ruta, &n, &hz);
        const char *base = strrchr(ruta, '/');
        int es_4415;
        res_t r; char det[160];

        base = base ? base + 1 : ruta;
        es_4415 = (strstr(base, "4415") != 0);
        if (!x) { printf("  no puedo abrir %s\n", ruta); fallos++; continue; }
        if (hz != 12000U) {
            printf("  %s va a %u Hz y hacen falta 12000\n", base, hz);
            free(x); fallos++; continue;
        }
        printf("  %s  (%.1f s)%s\n", base, (double)n / 12000.0,
               es_4415 ? "" : "   (EL CONTROL: ya se midio que no es 75 bps de 110A)");
        r = pasada(x, n);
        free(x);

        if (es_4415) {
            pinta_cuenta(&r);
            pinta_dibits(&r);
            snprintf(det, sizeof det, "(%d segmentos, corr max %d%%)", r.segmentos, r.corr_max);
            di("    el preambulo largo, veinte segmentos o mas", r.segmentos >= 20, det);
            snprintf(det, sizeof det, "(%d valores, termina en %d)", r.n_cuentas,
                     r.n_cuentas ? r.cuentas[r.n_cuentas - 1] : -1);
            di("    la cuenta baja de uno en uno y acaba en cero", cuenta_sana(&r), det);
            snprintf(det, sizeof det, "(D1=%u D2=%u -> %s bps, %s)", r.d1, r.d2,
                     s4415_bps_txt(r.bps_idx), r.largo ? "largo" : "corto");
            di("    D1=5 D2=5 -> 75 bps, entrelazado largo",
               r.d1 == 5U && r.d2 == 5U && r.bps_idx == 5U && r.largo == 1U, det);
            snprintf(det, sizeof det, "(%d dibits)", r.n_dib);
            di("    sale el tramo de datos detras del preambulo", r.n_dib > 100, det);
            printf("      sets excepcionales: %d  en %d %d %d %d\n", r.excep,
                   r.exc_sitio[0], r.exc_sitio[1], r.exc_sitio[2], r.exc_sitio[3]);
            printf("      bloques %d, bits %d, EOM %d, fin %d, desalineos %d, sin marca %d\n",
                   r.bloques, r.bits_dec, r.eom, r.fin_tx, r.desalineos, r.sin_marca);
            if (r.texto[0]) { printf("      texto: %.200s\n", r.texto); }
            /*
             * Y lo mismo en HEX, que es lo unico honesto cuando lo que sale
             * no es texto: los bytes tal cual, sin decir que son otra cosa.
             */
            {
                res_t h;
                float *x2 = lee_wav(ruta, &n, &hz);
                g_fmt = S4415_HEX;
                h = pasada(x2, n);
                g_fmt = S4415_ASCII;
                free(x2);
                printf("      hex:   %.200s\n", h.texto);
            }
        } else {
            snprintf(det, sizeof det, "(%d segmentos)", r.segmentos);
            di("    no engancha", r.segmentos == 0, det);
        }
        printf("\n");
    }

    printf("  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}
