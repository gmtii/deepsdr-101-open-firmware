/*
 * BANCO DEL RDS (User/rds.c).
 *
 * Fabrica una senal multiplex de FM de verdad -piloto de 19 kHz, audio, y la
 * subportadora de RDS a 57 kHz con sus grupos bien construidos- y se la da al
 * decodificador para ver que saca.
 *
 * EL CRC LO CALCULA EL PROPIO rds.c. El banco no lleva su copia de la
 * division polinomica: llama a rds_resto(), que es la misma funcion que usa
 * el decodificador. Comprobar que dos copias de una formula coinciden no
 * demuestra nada; lo que aqui se mide es si la CADENA entera -mezclar,
 * filtrar, enganchar la portadora, enganchar el reloj, decodificar- saca los
 * datos que se metieron.
 *
 * Y lo que mas importa, igual que en DCF77, MSF, ALS162, RBU, WWV y BPM: que
 * con ruido solo NO se invente una hora.
 */
#include "rds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FS      192000.0
#define BAUD    1187.5
#define SUB     57000.0

static int fallos = 0;

static void comprueba(int ok, const char *que)
{
    if (!ok) { fallos++; }
    printf("  %-62s %s\n", que, ok ? "ok" : "*** MAL ***");
}

/* ---------------------------------------------------------------- generador */

/* Un bloque: 16 bits de datos + el resto de dividirlos, sumado a la palabra
 * de desplazamiento. Se usa rds_resto() a proposito - ver la cabecera. */
static uint32_t bloque(uint16_t datos, uint16_t off)
{
    uint32_t m = (uint32_t)datos << 10;
    uint16_t crc = rds_resto(m);
    return m | (uint32_t)(crc ^ off);
}

/* Un grupo son cuatro bloques seguidos: A, B, C, D. */
static void grupo(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint32_t *out)
{
    out[0] = bloque(a, RDS_OFF_A);
    out[1] = bloque(b, RDS_OFF_B);
    out[2] = bloque(c, RDS_OFF_C);
    out[3] = bloque(d, RDS_OFF_D);
}

/* El grupo 0A: nombre de emisora, dos caracteres por grupo. */
static void grupo_0a(uint16_t pi, uint8_t seg, char c1, char c2, uint32_t *out)
{
    uint16_t b = (uint16_t)((0U << 12) | (0U << 11) | (0U << 10) | (5U << 5) | seg);
    grupo(pi, b, (uint16_t)0x1234, (uint16_t)(((uint8_t)c1 << 8) | (uint8_t)c2), out);
}

/* El grupo 4A: la hora. */
static void grupo_4a(uint16_t pi, uint32_t mjd, uint8_t hh, uint8_t mm,
                     int8_t desp_medias, uint32_t *out)
{
    uint16_t b, c, d;
    uint8_t signo = (uint8_t)(desp_medias < 0);
    uint8_t medias = (uint8_t)(desp_medias < 0 ? -desp_medias : desp_medias);

    b = (uint16_t)((4U << 12) | (0U << 11) | (0U << 10) | (5U << 5)
                   | (uint16_t)((mjd >> 15) & 0x03U));
    c = (uint16_t)(((mjd & 0x7FFFU) << 1) | ((hh >> 4) & 0x01U));
    d = (uint16_t)(((uint16_t)(hh & 0x0FU) << 12) | ((uint16_t)mm << 6)
                   | ((uint16_t)signo << 5) | medias);
    grupo(pi, b, c, d, out);
}

/* ------------------------------------------------------- la senal de radio */

typedef struct {
    double fase_sub;     /* fase de la subportadora de 57 kHz */
    double fase_pil;     /* la del piloto de 19 kHz */
    double fase_bit;     /* 0..1 dentro del simbolo */
    double fase_aud;     /* el tono de audio mono */
    double fase_lr;      /* el tono del canal de diferencia (estereo) */
    double estereo;      /* 0 = mono, 1 = con subportadora de estereo */
    int    bit_ant;      /* para el codificado diferencial */
    uint32_t *grupos;    /* lista de grupos ya montados */
    int    n_grupos;
    int    gi, bi;       /* grupo y bit dentro del grupo */
    int    valor;        /* +1/-1 del simbolo en curso */
    int    arrancado;
    /* --- para medir el anclaje al segundo (prueba 6) --- */
    long   n_muestra;    /* muestras de multiplex emitidas */
    long   fin_4a;       /* en cual acabo el ultimo grupo 4A, -1 si ninguno */
    int    es_4a[16];    /* que grupos de la lista son de hora */
} tx_t;

static void tx_init(tx_t *t, uint32_t *grupos, int n)
{
    memset(t, 0, sizeof *t);
    t->grupos = grupos;
    t->n_grupos = n;
    t->estereo = 1.0;
    t->fin_4a = -1;
}

/* El siguiente bit del flujo, ya codificado diferencialmente. */
static int tx_siguiente_bit(tx_t *t)
{
    int b, k, pos;
    uint32_t blq;

    pos = t->bi;                  /* 0..103 dentro del grupo */
    k = pos / 26;
    blq = t->grupos[t->gi * 4 + k];
    b = (int)((blq >> (25 - (pos % 26))) & 1U);

    t->bi++;
    if (t->bi >= 104) {
        /* El grupo t->gi acaba de terminar: el bit anterior era su ultimo, y
         * estamos en la muestra en la que ese bit se acaba. Si era de hora,
         * ESTE es el instante que la norma llama borde de minuto. */
        if (t->es_4a[t->gi]) { t->fin_4a = t->n_muestra; }
        t->bi = 0;
        t->gi = (t->gi + 1) % t->n_grupos;
    }

    /* diferencial: se transmite el acumulado */
    t->bit_ant ^= b;
    return t->bit_ant;
}

/*
 * Una muestra de multiplex. Lleva:
 *   - el piloto de 19 kHz, al 8% (lo que manda la norma)
 *   - un poco de audio, para que no sea un laboratorio
 *   - el RDS: bifase a 1187,5 baudios sobre 57 kHz, al 3% de desviacion
 * La amplitud relativa del RDS es la de verdad, no una comoda: si el
 * decodificador solo funcionara con el RDS a tope, el banco no diria nada.
 */
static double tx_muestra(tx_t *t, double fs, double err_ppm)
{
    double y, paso;

    /* reloj de bit */
    t->n_muestra++;
    paso = BAUD * (1.0 + err_ppm * 1e-6) / fs;
    if (!t->arrancado) {
        t->valor = tx_siguiente_bit(t) ? 1 : -1;
        t->arrancado = 1;
    }
    t->fase_bit += paso;
    if (t->fase_bit >= 1.0) {
        t->fase_bit -= 1.0;
        t->valor = tx_siguiente_bit(t) ? 1 : -1;
    }

    /* bifase: primera mitad el valor, segunda la contraria */
    {
        double bif = (t->fase_bit < 0.5) ? (double)t->valor : (double)(-t->valor);
        t->fase_sub += 2.0 * M_PI * SUB * (1.0 + err_ppm * 1e-6) / fs;
        t->fase_pil += 2.0 * M_PI * 19000.0 * (1.0 + err_ppm * 1e-6) / fs;
        if (t->fase_sub > 2.0 * M_PI) { t->fase_sub -= 2.0 * M_PI; }
        if (t->fase_pil > 2.0 * M_PI) { t->fase_pil -= 2.0 * M_PI; }

        y  = 0.03 * bif * cos(t->fase_sub);     /* RDS, 3% */
        y += 0.08 * sin(t->fase_pil);           /* piloto, 8% */

        /*
         * AUDIO Y ESTEREO - lo que hace que esto se parezca a una emisora.
         *
         * El mono va en banda base; el estereo (L-R) va en doble banda
         * lateral sobre 38 kHz, o sea ocupando de 23 a 53 kHz. Esa banda
         * llega hasta 4 kHz de la subportadora de RDS, y ademas la FM aplica
         * PREENFASIS: los agudos salen levantados hasta +17 dB a 15 kHz, que
         * es justo el extremo que mas se acerca al RDS.
         *
         * Por eso el banco lleva un tono de 14 kHz en el canal de diferencia:
         * no es rebuscado, es el peor caso NORMAL de cualquier emisora que
         * ponga musica. Sin esto, el banco medía un laboratorio.
         */
        t->fase_aud += 2.0 * M_PI * 1000.0 / fs;
        t->fase_lr  += 2.0 * M_PI * 14000.0 / fs;
        if (t->fase_aud > 2.0 * M_PI) { t->fase_aud -= 2.0 * M_PI; }
        if (t->fase_lr  > 2.0 * M_PI) { t->fase_lr  -= 2.0 * M_PI; }
        y += 0.35 * sin(t->fase_aud);                        /* mono */
        y += t->estereo * 0.30 * sin(t->fase_lr)
                        * sin(2.0 * t->fase_pil);            /* L-R sobre 38 kHz */
    }
    return y;
}

static unsigned long s_rnd = 12345UL;
static double ruido(void)
{
    /* dos uniformes sumadas: suficiente para lo que se mide aqui */
    double a, b;
    s_rnd = s_rnd * 1103515245UL + 12345UL;
    a = (double)((s_rnd >> 16) & 0x7FFF) / 32767.0 - 0.5;
    s_rnd = s_rnd * 1103515245UL + 12345UL;
    b = (double)((s_rnd >> 16) & 0x7FFF) / 32767.0 - 0.5;
    return a + b;
}

/* ------------------------------------------------------------------ pruebas */

static void prueba_resto(void)
{
    printf("\n1. la division polinomica y las palabras de desplazamiento\n");
    {
        uint32_t b = bloque(0x1234U, RDS_OFF_A);
        printf("    bloque de prueba 0x1234 con la palabra A -> 0x%07X\n", b);
        comprueba(rds_resto(b) == RDS_OFF_A, "un bloque bien construido da su propia palabra");
        comprueba(rds_resto(b ^ 1UL) != RDS_OFF_A, "un bit cambiado ya no la da");
        comprueba(rds_resto(b ^ 0x2000UL) != RDS_OFF_A, "otro bit cambiado, tampoco");
    }
    {
        /* que las cinco palabras se distingan entre si */
        uint16_t p[5] = { RDS_OFF_A, RDS_OFF_B, RDS_OFF_C, RDS_OFF_CP, RDS_OFF_D };
        int i, j, ok = 1;
        for (i = 0; i < 5; i++) {
            for (j = i + 1; j < 5; j++) { if (p[i] == p[j]) { ok = 0; } }
        }
        comprueba(ok, "las cinco palabras de desplazamiento son distintas");
    }
}

/* Corre `segundos` de senal y devuelve lo decodificado. */
static void corre(tx_t *t, double segundos, double sigma, double err_ppm,
                  rds_info_t *out)
{
    long n = (long)(FS * segundos);
    long k;
    float buf[512];
    int j = 0;

    for (k = 0; k < n; k++) {
        buf[j++] = (float)(tx_muestra(t, FS, err_ppm) + sigma * ruido());
        if (j == 512) { rds_feed(buf, 512U); j = 0; }
    }
    rds_info(out);
}

int main(void)
{
    static uint32_t grupos[8 * 4];
    tx_t tx;
    rds_info_t info;

    printf("banco del RDS\n");
    prueba_resto();

    /* Ocho grupos en bucle: cuatro de nombre y cuatro de hora, que es mas o
     * menos lo que manda una emisora de verdad (la hora va una vez por
     * minuto, pero aqui interesa medir, no imitar el reparto). */
    grupo_0a(0xE2A1U, 0U, 'R', 'A', &grupos[0]);
    grupo_0a(0xE2A1U, 1U, 'D', 'I', &grupos[4]);
    grupo_0a(0xE2A1U, 2U, 'O', ' ', &grupos[8]);
    grupo_0a(0xE2A1U, 3U, 'E', 'S', &grupos[12]);
    /* MJD 61306 = 23/09/2026; 21:47 UTC; +2 medias horas = +1 h */
    grupo_4a(0xE2A1U, 61306UL, 21U, 47U, 2, &grupos[16]);
    grupo_0a(0xE2A1U, 0U, 'R', 'A', &grupos[20]);
    grupo_4a(0xE2A1U, 61306UL, 21U, 47U, 2, &grupos[24]);
    grupo_0a(0xE2A1U, 1U, 'D', 'I', &grupos[28]);

    printf("\n2. senal limpia\n");
    rds_start((float)FS);
    tx_init(&tx, grupos, 8);
    {
        int seg;
        uint32_t ok_ant = 0, mal_ant = 0;
        for (seg = 0; seg < 10; seg++) {
            uint32_t dok, dmal;
            corre(&tx, 1.0, 0.0, 0.0, &info);
            dok = info.bloques_ok - ok_ant; dmal = info.bloques_mal - mal_ant;
            ok_ant = info.bloques_ok; mal_ant = info.bloques_mal;
            {
                float cf, se, nv; uint32_t en, pe;
                rds_lazos_dbg(&cf, &se, &nv);
                rds_cuentas_dbg(&en, &pe);
                printf("    segundo %2d: %3u buenos %3u malos  (%3.0f%%)   "
                       "port %+6.2f Hz  reloj %+7.1f ppm  eng %u perd %u\n",
                       seg + 1, dok, dmal,
                       (dok + dmal) ? 100.0 * dok / (dok + dmal) : 0.0,
                       (double)cf, (double)se, en, pe);
            }
        }
    }
    printf("    enganchado %u   calidad %.3f   bloques %u buenos / %u malos\n",
           info.enganchado, (double)info.calidad, info.bloques_ok, info.bloques_mal);
    printf("    PI 0x%04X   PS \"%s\"\n", info.pi, info.ps);
    printf("    hora %02u:%02u UTC del %02u/%02u/%02u, desplazamiento %+d medias horas\n",
           info.hora, info.minuto, info.dia, info.mes, info.anno2, info.desp_medias);
    comprueba(info.enganchado, "se engancha");
    comprueba(info.tiene_pi && info.pi == 0xE2A1U, "el identificador de programa sale bien");
    comprueba(info.tiene_ps && strcmp(info.ps, "RADIO ES") == 0, "el nombre sale bien");
    comprueba(info.tiene_hora && info.hora == 21U && info.minuto == 47U,
              "la hora sale bien");
    comprueba(info.dia == 23U && info.mes == 9U && info.anno2 == 26U,
              "la fecha sale bien");
    comprueba(info.desp_medias == 2, "el desplazamiento local sale bien");
    rds_stop();

    /* ------------------------------------------------------------------ */
    printf("\n3. con ruido\n");
    printf("    (la subportadora va al 3%% de desviacion, que es lo normal;\n"
           "     el ruido se da como multiplo de esa amplitud)\n");
    {
        static const double k_sig[] = { 0.003, 0.01, 0.03, 0.06, 0.12, 0.30 };
        unsigned i;
        for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) {
            rds_start((float)FS);
            tx_init(&tx, grupos, 8);
            s_rnd = 999UL + i * 7717UL;
            corre(&tx, 8.0, k_sig[i], 0.0, &info);
            printf("    ruido %5.1f%% de la RDS  ->  calidad %.3f   PS \"%s\"   hora %s\n",
                   k_sig[i] / 0.03 * 100.0, (double)info.calidad, info.ps,
                   info.tiene_hora ? "si" : "no");
            if (k_sig[i] <= 0.03 && !(info.tiene_hora && info.hora == 21U
                                      && info.minuto == 47U)) {
                printf("      *** con este ruido deberia sacar la hora ***\n");
                fallos++;
            }
            if (info.tiene_hora && (info.hora != 21U || info.minuto != 47U
                                    || info.dia != 23U || info.mes != 9U)) {
                printf("      *** ha sacado una hora EQUIVOCADA ***\n");
                fallos++;
            }
            rds_stop();
        }
    }

    /* ------------------------------------------------------------------ */
    printf("\n4. con el reloj de muestreo desviado\n");
    printf("    (el cuarzo del codec y el de la emisora nunca son el mismo)\n");
    {
        static const double k_ppm[] = { -200.0, -50.0, 50.0, 200.0 };
        unsigned i;
        for (i = 0; i < sizeof k_ppm / sizeof k_ppm[0]; i++) {
            float se;
            rds_start((float)FS);
            tx_init(&tx, grupos, 8);
            corre(&tx, 8.0, 0.005, k_ppm[i], &info);
            rds_lazos_dbg(0, &se, 0);
            printf("    %+7.1f ppm  ->  calidad %.3f   el lazo mide %+7.1f ppm   hora %s\n",
                   k_ppm[i], (double)info.calidad, (double)se,
                   info.tiene_hora ? "si" : "no");
            comprueba(info.tiene_hora && info.hora == 21U && info.minuto == 47U,
                      "sigue sacando la hora buena");
            rds_stop();
        }
    }

    /* ------------------------------------------------------------------ */
    printf("\n5. SOLO RUIDO - lo que de verdad importa\n");
    printf("    (sin subportadora ninguna: si aqui sale una hora, es inventada)\n");
    {
        static const double k_sig[] = { 0.01, 0.1, 1.0 };
        unsigned i;
        for (i = 0; i < sizeof k_sig / sizeof k_sig[0]; i++) {
            long k;
            float buf[512];
            int j = 0;
            rds_start((float)FS);
            s_rnd = 4242UL + i * 31337UL;
            for (k = 0; k < (long)(FS * 20.0); k++) {
                buf[j++] = (float)(k_sig[i] * ruido());
                if (j == 512) { rds_feed(buf, 512U); j = 0; }
            }
            rds_info(&info);
            printf("    ruido %.2f, 20 segundos  ->  enganchado %u   hora %s   "
                   "bloques %u buenos / %u malos\n",
                   k_sig[i], info.enganchado, info.tiene_hora ? "SI" : "no",
                   info.bloques_ok, info.bloques_mal);
            comprueba(!info.tiene_hora, "no se inventa una hora");
            comprueba(!info.tiene_pi, "ni un identificador de emisora");
            comprueba(!info.tiene_ps, "ni un nombre");
            rds_stop();
        }
    }

    /* ------------------------------------------------------------------ */
    printf("\n6. bloques corrompidos en el grupo de la hora\n");
    printf("    (lo que NUNCA puede pasar es que salga una hora EQUIVOCADA)\n");
    {
        static uint32_t g2[2 * 4];
        static const struct { const char *que; uint32_t mascara; uint8_t debe_salir; } k_casos[] = {
            { "un bit suelto (bit 25)",      1UL << 25,                     1U },
            { "un bit suelto (bit 12)",      1UL << 12,                     1U },
            { "un bit suelto (bit 0)",       1UL << 0,                      1U },
            { "una rafaga de 2 (bits 7-8)",  (1UL << 7) | (1UL << 8),       1U },
            { "una rafaga de 3 (bits 3-5)",  (7UL << 3),                    0U },
            { "cuatro bits dispersos",       (1UL << 1) | (1UL << 9)
                                             | (1UL << 17) | (1UL << 24),   0U },
        };
        unsigned c;
        for (c = 0; c < sizeof k_casos / sizeof k_casos[0]; c++) {
            grupo_0a(0xE2A1U, 0U, 'R', 'A', &g2[0]);
            grupo_4a(0xE2A1U, 61306UL, 21U, 47U, 2, &g2[4]);
            g2[7] ^= k_casos[c].mascara;     /* el bloque D del grupo 4A */
            rds_start((float)FS);
            tx_init(&tx, g2, 2);
            corre(&tx, 6.0, 0.0, 0.0, &info);
            printf("    %-28s -> hora %s%s\n", k_casos[c].que,
                   info.tiene_hora ? "si" : "no",
                   info.tiene_hora ? (info.hora == 21U && info.minuto == 47U
                                      ? " y es la BUENA" : " y es OTRA") : "");
            /* Lo innegociable: si sale una hora, tiene que ser la de verdad. */
            comprueba(!info.tiene_hora || (info.hora == 21U && info.minuto == 47U
                                           && info.dia == 23U && info.mes == 9U),
                      "si sale una hora, es la correcta");
            /* Y lo que se espera de la correccion: hasta dos bits seguidos,
             * el grupo se rescata entero. */
            if (k_casos[c].debe_salir) {
                comprueba(info.tiene_hora, "y con uno o dos bits seguidos, se rescata");
            }
            rds_stop();
        }
    }

    /* ------------------------------------------------------------------ */
    printf("\n7. corregir errores: lo que se gana y lo que se arriesga\n");
    {
        static const uint8_t k_raf[] = { 1U, 2U, 3U, 5U };
        unsigned r;

        printf("    por cada tamano de rafaga admitido, dos numeros:\n");
        printf("      RESCATA  de los bloques rotos con una rafaga real, cuantos se arreglan\n");
        printf("      COLADA   de bloques de BASURA, cuantos se dan por buenos\n");
        printf("    (la basura sin corregir ya se cuela 1 de cada 1024, o sea 0,1%%)\n\n");

        for (r = 0; r < sizeof k_raf / sizeof k_raf[0]; r++) {
            unsigned long rescatados = 0, intentos = 0, coladas = 0, basuras = 0;
            unsigned long i;
            uint8_t L = k_raf[r];

            /* --- rescate: bloques buenos con una rafaga real encima --- */
            s_rnd = 777UL;
            for (i = 0; i < 20000UL; i++) {
                uint32_t bueno, roto, arreglado;
                uint16_t datos;
                uint8_t pos, len, k;
                uint32_t e;

                s_rnd = s_rnd * 1103515245UL + 12345UL;
                datos = (uint16_t)((s_rnd >> 12) & 0xFFFFU);
                bueno = bloque(datos, RDS_OFF_B);

                /* una rafaga de longitud 1..L en una posicion cualquiera */
                s_rnd = s_rnd * 1103515245UL + 12345UL;
                len = (uint8_t)(1U + ((s_rnd >> 16) % L));
                s_rnd = s_rnd * 1103515245UL + 12345UL;
                pos = (uint8_t)((s_rnd >> 16) % (27U - len));
                e = 1UL;                              /* el primer bit siempre */
                for (k = 1U; k + 1U < len; k++) {
                    s_rnd = s_rnd * 1103515245UL + 12345UL;
                    if ((s_rnd >> 20) & 1UL) { e |= (1UL << k); }
                }
                if (len > 1U) { e |= (1UL << (len - 1U)); }
                roto = bueno ^ (e << pos);

                intentos++;
                arreglado = roto;
                if (rds_corrige(&arreglado, RDS_OFF_B, L) && arreglado == bueno) {
                    rescatados++;
                }
            }

            /* --- colada: palabras completamente al azar --- */
            s_rnd = 31415UL;
            for (i = 0; i < 200000UL; i++) {
                uint32_t basura, tmp;
                s_rnd = s_rnd * 1103515245UL + 12345UL;
                basura = ((s_rnd >> 8) & 0x1FFFUL);
                s_rnd = s_rnd * 1103515245UL + 12345UL;
                basura |= ((s_rnd >> 8) & 0x1FFFUL) << 13;
                basuras++;
                tmp = basura;
                if (rds_corrige(&tmp, RDS_OFF_B, L)) { coladas++; }
            }

            printf("    rafagas de hasta %u bits:   rescata %5.1f%%   cuela %5.2f%% de la basura\n",
                   L, 100.0 * rescatados / intentos, 100.0 * coladas / basuras);
        }
    }

    /* ------------------------------------------------------------------ */
    /*
     * 6. EL ANCLAJE AL SEGUNDO.
     *
     * La norma dice que el borde de minuto cae a menos de 0,1 s del FINAL del
     * grupo 4A. O sea que si anotamos en que muestra acaba ese grupo, tenemos
     * el borde de minuto - y con el, el segundo, que la hora del RDS no trae.
     *
     * Lo que NO se puede hacer es dar por bueno ese instante sin mas: entre
     * el ultimo bit que entra por la antena y el momento en que el
     * decodificador da el grupo por bueno hay un retardo propio (el filtro
     * diezmador, la integracion por mitades del simbolo, lo que el lazo de
     * reloj se aparte del centro). Ese retardo es el que se mide aqui, con el
     * transmisor del propio banco diciendo la verdad sobre cuando emitio el
     * ultimo bit.
     *
     * Se alimenta MUESTRA A MUESTRA, no de 512 en 512: con bloques, el
     * instante en que se consulta el decodificador y el instante del
     * transmisor se separan hasta 2,7 ms, que es del orden de lo que se
     * quiere medir. El banco seria mas rapido y la medida valdria menos.
     */
    printf("\n6. anclaje al segundo: retardo propio del decodificador\n");
    {
        static uint32_t g6[4 * 4];
        double err_ppm;
        unsigned e;
        static const double k_err[] = { 0.0, -50.0, 50.0 };

        /* Tres de nombre y uno de hora: la hora es el ultimo del ciclo, asi
         * que su final esta bien separado del principio del siguiente. */
        grupo_0a(0xE2A1U, 0U, 'R', 'A', &g6[0]);
        grupo_0a(0xE2A1U, 1U, 'D', 'I', &g6[4]);
        grupo_0a(0xE2A1U, 2U, 'O', ' ', &g6[8]);
        grupo_4a(0xE2A1U, 61306UL, 21U, 47U, 2, &g6[12]);

        for (e = 0; e < sizeof k_err / sizeof k_err[0]; e++) {
            tx_t t6;
            long k;
            uint32_t seq_ant = 0;
            double suma = 0.0, min = 1e9, max = -1e9;
            int n = 0;

            err_ppm = k_err[e];
            rds_start((float)FS);
            tx_init(&t6, g6, 4);
            t6.es_4a[3] = 1;

            for (k = 0; k < (long)(FS * 8.0); k++) {
                float m = (float)(tx_muestra(&t6, FS, err_ppm) + 0.01 * ruido());
                rds_feed(&m, 1U);
                rds_info(&info);
                if (info.tiene_hora && info.hora_seq != seq_ant) {
                    seq_ant = info.hora_seq;
                    if (t6.fin_4a >= 0) {
                        /* El decodificador cuenta en muestras de banda base
                         * (12 kHz) y el transmisor en muestras de multiplex
                         * (192 kHz): los dos a milisegundos y se restan. */
                        double t_dec = 1000.0 * info.hora_muestra / rds_fs_bb();
                        double t_tx  = 1000.0 * t6.fin_4a / FS;
                        double d = t_dec - t_tx;
                        /* Los dos primeros se descartan: mientras el lazo de
                         * reloj todavia esta tirando, el retardo no es el de
                         * regimen. */
                        if (n >= 0) { suma += d; if (d < min) { min = d; } if (d > max) { max = d; } }
                        n++;
                    }
                }
            }
            if (n > 0) {
                printf("    error de reloj %+5.0f ppm:  %2d medidas   "
                       "retardo medio %6.2f ms   (de %5.2f a %5.2f)\n",
                       err_ppm, n, suma / n, min, max);
                comprueba(n >= 5, "llegan varias horas que medir");
                comprueba(suma / n > -5.0 && suma / n < 20.0,
                          "el retardo propio del decodificador es de unos pocos ms");
                comprueba(max - min < 5.0,
                          "y es estable: no cambia de un grupo a otro");
            } else {
                comprueba(0, "no llego ninguna hora que medir");
            }
            rds_stop();
        }
        printf("\n    Lo que sale de aqui es RDS_CT_RETARDO_MS en User/main.c.\n"
               "    A esto hay que sumarle lo que la propia norma admite:\n"
               "    el borde de minuto cae a +/-0,1 s del final del grupo, y\n"
               "    eso no lo arregla ningun banco.\n");
    }

    printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}
