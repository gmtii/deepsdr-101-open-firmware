/*
 * LA TABLA DE FIRMAS DEL MODO IDENT.
 *
 * Pedido por el usuario.
 *
 * ident_casa() es una funcion PURA -cinco numeros entran, una lista de
 * nombres sale- y por eso se puede probar la tabla entera sin fabricar una
 * sola señal. Lo que se prueba contra grabaciones de verdad es el motor
 * que saca esos cinco numeros, y eso ya lo hace sim/stanag_aire.c.
 *
 * LO QUE SE MIDE AQUI, y las tres cosas son distintas:
 *
 * 1. QUE CADA FIRMA SE PUEDA ACERTAR. Se le da a cada fila una medida
 *    puesta en el centro de sus propios rangos y se exige que salga ella.
 *    Una fila con un rango imposible -minimo mayor que maximo, o un
 *    criterio que pide una medida que la fila no rellena- no cuadraria
 *    NUNCA y nadie se enteraria: estaria en la tabla, ocupando sitio, sin
 *    poder salir jamas.
 *
 * 2. QUE LAS SEÑALES DE VERDAD DEN SU NOMBRE. Una tabla por valores de la
 *    norma: 170 Hz y 100 baudios tienen que dar SITOR, 106,67 ms tiene que
 *    dar 4285, 6 Hz tiene que dar WSPR. Esto es lo que de verdad importa y
 *    lo unico que se lee desde fuera.
 *
 * 3. QUE LAS PISTAS SE CALLEN. Las filas que orientan en vez de
 *    identificar -"modem estrecho sin identificar"- tienen rangos muy
 *    abiertos a proposito. Si salieran al lado de un nombre, la pantalla
 *    leeria "STANAG 4285 / podria ser HFDL", que es una duda que no
 *    existe.
 */
#include "ident.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fallos;
static ident_cad_t g_cad;   /* la cadencia que acompaña a la medida */

static void di(const char *que, int bien, const char *detalle)
{
    printf("    %-46s %s%s%s\n", que, bien ? "ok" : "*** MAL ***",
           detalle ? "  " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

/* Una medida con todo apagado, para rellenar solo lo que haga falta. */
static stanag_det_t vacia(void)
{
    stanag_det_t m;
    memset(&m, 0, sizeof m);
    return m;
}

/* El centro de un rango; si el rango esta a 0,0 devuelve `si_libre`. */
static uint16_t centro(uint16_t lo, uint16_t hi, uint16_t si_libre)
{
    if (lo == 0U && hi == 0U) { return si_libre; }
    return (uint16_t)((lo + hi) / 2U);
}

/* Fabrica la medida que esa firma deberia reconocer. */
static stanag_det_t medida_de(const ident_firma_t *f)
{
    stanag_det_t m = vacia();

    m.hay_esp = 1U;
    m.centro_hz = 1500U;
    m.ancho10_hz = centro(f->ancho_min, f->ancho_max, 500U);
    m.ciclo_pc = (uint8_t)centro(f->ciclo_min, f->ciclo_max, 50U);

    memset(&g_cad, 0, sizeof g_cad);
    if (f->cad_min != 0U || f->cad_max != 0U) {
        g_cad.vale = 1U;
        g_cad.firmeza = 80U;
        g_cad.periodo_ds = centro(f->cad_min, f->cad_max, 150U);
        g_cad.ocupa_pc = (uint8_t)centro(f->ocup_min, f->ocup_max, 84U);
    }

    if (f->sim_min != 0U || f->sim_max != 0U) {
        m.simb_x10 = centro(f->sim_min, f->sim_max, 234U);
        m.simb_q = 40U;
        m.tonos = (uint8_t)centro(f->ton_min, f->ton_max, 21U);
    }

    /*
     * Y el alfabeto de siete bits, si la fila lo pide. Ver cr7_min en
     * ident.h. Se da por encima del minimo, no justo en el: lo que esta
     * mirando este banco es que la FILA este bien escrita, no donde cae el
     * borde; de lo que vale el borde se encarga sim/cr7_test.c con señal.
     */
    if (f->cr7_min != 0U) {
        uint16_t pc = (uint16_t)f->cr7_min + 10U;
        m.cr7_pc = (uint8_t)((pc > 100U) ? 100U : pc);
        m.cr7_bd_x10 = (f->cr7_bd != 0U) ? f->cr7_bd : 500U;
    }

    switch (f->criterio) {
    case IDENT_POR_TONOS:
        m.hay_tonos = 1U;
        m.sep_hz = centro(f->sep_min, f->sep_max, 170U);
        m.baudios_x10 = centro(f->bd_min, f->bd_max, 1000U);
        break;
    case IDENT_POR_SEP:
        m.hay_tonos = 1U;
        m.sep_hz = centro(f->sep_min, f->sep_max, 1000U);
        break;
    case IDENT_POR_TRAMA:
        m.vale = 1U;
        m.periodo_us = centro(f->trama_min, f->trama_max, 10667U);
        m.calidad = 80U;
        break;
    case IDENT_POR_PICO:
        m.pico_pc = 57U;
        break;
    case IDENT_POR_SIMBOLO:
        /* Muchos tonos: no hay "dos tonos" que ver, y el ancho lo pone la
         * propia firma mas arriba. */
        m.pico_pc = 3U;
        break;
    case IDENT_POR_CADENCIA:
        /* El espectro no dice nada util en una banda llena: lo que
         * identifica es el reloj, y ya esta puesto arriba. */
        m.ancho10_hz = 2600U;
        m.pico_pc = 3U;
        break;
    default:   /* IDENT_POR_ANCHO */
        m.pico_pc = 2U;
        break;
    }
    return m;
}

static int sale(const stanag_det_t *m, const char *nombre)
{
    uint8_t cand[8];
    uint16_t n, i, total;
    const ident_firma_t *t = ident_firmas(&total);

    n = ident_casa(m, &g_cad, cand, 8U);
    for (i = 0U; i < n; i++) {
        if (strcmp(t[cand[i]].nombre, nombre) == 0) { return 1; }
    }
    return 0;
}

static void lista(const stanag_det_t *m)
{
    uint8_t cand[8];
    uint16_t n, i, total;
    const ident_firma_t *t = ident_firmas(&total);

    n = ident_casa(m, &g_cad, cand, 8U);
    printf("      ->");
    if (n == 0U) { printf(" (nada)"); }
    for (i = 0U; i < n; i++) { printf(" [%s]", t[cand[i]].nombre); }
    printf("\n");
}

int main(void)
{
    uint16_t total = 0U, i;
    const ident_firma_t *t = ident_firmas(&total);

    printf("\nLA TABLA DE FIRMAS DEL IDENT\n\n");
    printf("  %u firmas\n\n", (unsigned)total);

    /* 1. Cada firma tiene que poder salir. */
    printf("  cada firma se puede acertar\n");
    for (i = 0U; i < total; i++) {
        stanag_det_t m = medida_de(&t[i]);
        char q[80];
        snprintf(q, sizeof q, "%.44s", t[i].nombre);
        if (!sale(&m, t[i].nombre)) {
            di(q, 0, "no sale ni con su propia medida");
            lista(&m);
        }
        /* Y los rangos, del derecho. */
        if (t[i].sep_min > t[i].sep_max || t[i].bd_min > t[i].bd_max
            || t[i].trama_min > t[i].trama_max
            || t[i].ancho_min > t[i].ancho_max
            || t[i].ciclo_min > t[i].ciclo_max) {
            di(q, 0, "tiene un rango al reves");
        }
        /* Y una nota, que es la mitad del valor de la fila. */
        if (t[i].nota == 0 || t[i].nota[0] == '\0') {
            di(q, 0, "no tiene nota");
        }
    }
    printf("    %-46s %s\n", "todas, con su medida y sus rangos",
           fallos ? "*** MAL ***" : "ok");

    /* 2. Las señales de verdad, por valores de la norma. */
    printf("\n  las señales conocidas dan su nombre\n");
    {
        struct { const char *que; const char *espera; stanag_det_t m; } k[13];
        int n = 0, j;

        /* SITOR-B / NAVTEX: 170 Hz, 100,0 Bd, seguido. */
        k[n].que = "170 Hz y 100 Bd seguido";
        k[n].espera = "SITOR-B / NAVTEX / telex";
        k[n].m = vacia(); k[n].m.hay_esp = 1U; k[n].m.ancho10_hz = 205U;
        k[n].m.hay_tonos = 1U; k[n].m.sep_hz = 170U; k[n].m.baudios_x10 = 1000U;
        k[n].m.ciclo_pc = 98U; n++;

        /* El mismo pero a rafagas: SITOR-A / DSC. */
        k[n].que = "170 Hz y 100 Bd a rafagas";
        k[n].espera = "SITOR-A o DSC (a rafagas)";
        k[n].m = k[n-1].m; k[n].m.ciclo_pc = 30U; n++;

        /* STANAG 4481 FSK: 850 Hz, 75 Bd. */
        k[n].que = "850 Hz y 75 Bd";
        k[n].espera = "STANAG 4481 FSK (KG-84)";
        k[n].m = vacia(); k[n].m.hay_esp = 1U; k[n].m.ancho10_hz = 1000U;
        k[n].m.hay_tonos = 1U; k[n].m.sep_hz = 850U; k[n].m.baudios_x10 = 750U;
        k[n].m.ciclo_pc = 90U; n++;

        /*
         * FUG, la marina francesa en 13.418 kHz: 850 Hz y 50 baudios.
         *
         * Esta fila NO EXISTIA hasta el 05/10/2026 y es la razon de este
         * caso. 850 con 50 se escapaba entre la de 4481 -que pide 70..80
         * baudios- y la de RTTY comercial -que pide 400..500 Hz de salto-,
         * asi que la pantalla se caia a la pista del ancho y decia "SSTV o
         * fax meteorologico" con dos tonos limpios delante.
         *
         * Va pegado al de 850 y 75 a proposito: los dos tienen el mismo
         * salto y solo los separa la velocidad, que es justo lo que hay
         * que no confundir.
         */
        k[n].que = "850 Hz y 50 Bd (FUG)";
        k[n].espera = "FSK 50 Bd / 850 Hz (naval)";
        k[n].m = vacia(); k[n].m.hay_esp = 1U; k[n].m.ancho10_hz = 865U;
        k[n].m.hay_tonos = 1U; k[n].m.sep_hz = 850U; k[n].m.baudios_x10 = 500U;
        k[n].m.ciclo_pc = 95U; n++;

        /* RTTY de aficionado: 170 Hz, 45,45 Bd. */
        k[n].que = "170 Hz y 45,45 Bd";
        k[n].espera = "RTTY de aficionado";
        k[n].m = vacia(); k[n].m.hay_esp = 1U; k[n].m.ancho10_hz = 300U;
        k[n].m.hay_tonos = 1U; k[n].m.sep_hz = 170U; k[n].m.baudios_x10 = 454U;
        k[n].m.ciclo_pc = 90U; n++;

        /* APRS: 1200 y 2200 Hz, sin velocidad medible. */
        k[n].que = "tonos a 1000 Hz, sin velocidad";
        k[n].espera = "APRS / paquete AFSK 1200";
        k[n].m = vacia(); k[n].m.hay_esp = 1U; k[n].m.ancho10_hz = 1800U;
        k[n].m.hay_tonos = 1U; k[n].m.sep_hz = 1000U; k[n].m.ciclo_pc = 40U; n++;

        /* STANAG 4285: 106,67 ms. */
        k[n].que = "trama de 106,67 ms";
        k[n].espera = "STANAG 4285 o 4481 PSK";
        k[n].m = vacia(); k[n].m.hay_esp = 1U; k[n].m.ancho10_hz = 2531U;
        k[n].m.vale = 1U; k[n].m.periodo_us = 10667U; k[n].m.calidad = 70U;
        k[n].m.ciclo_pc = 95U; n++;

        /* STANAG 4529: 211,69 ms. */
        k[n].que = "trama de 211,69 ms";
        k[n].espera = "STANAG 4529";
        k[n].m = vacia(); k[n].m.hay_esp = 1U; k[n].m.ancho10_hz = 1240U;
        k[n].m.vale = 1U; k[n].m.periodo_us = 21169U; k[n].m.calidad = 70U;
        k[n].m.ciclo_pc = 95U; n++;

        /* WSPR: 6 Hz de ancho, seguido. */
        k[n].que = "6 Hz de ancho";
        k[n].espera = "WSPR";
        k[n].m = vacia(); k[n].m.hay_esp = 1U; k[n].m.ancho10_hz = 6U;
        k[n].m.ciclo_pc = 95U; k[n].m.pico_pc = 20U; n++;

        /* PSK31: 31 Hz. */
        k[n].que = "31 Hz de ancho";
        k[n].espera = "PSK31";
        k[n].m = vacia(); k[n].m.hay_esp = 1U; k[n].m.ancho10_hz = 31U;
        k[n].m.ciclo_pc = 90U; k[n].m.pico_pc = 20U; n++;

        /* FT8: 50 Hz. */
        k[n].que = "50 Hz de ancho (una señal sola)";
        k[n].espera = "FT8 (una sola)";
        k[n].m = vacia(); k[n].m.hay_esp = 1U; k[n].m.ancho10_hz = 50U;
        k[n].m.ciclo_pc = 84U; k[n].m.pico_pc = 10U; n++;

        /* Portadora: toda la potencia en la raya, y ahi siempre. */
        k[n].que = "una raya sola, siempre encendida";
        k[n].espera = "Portadora sola (heterodino)";
        k[n].m = vacia(); k[n].m.hay_esp = 1U; k[n].m.ancho10_hz = 18U;
        k[n].m.pico_pc = 57U; k[n].m.ciclo_pc = 100U; n++;

        /* CW: la misma raya, parpadeando. */
        k[n].que = "una raya sola, parpadeando";
        k[n].espera = "CW (Morse)";
        k[n].m = vacia(); k[n].m.hay_esp = 1U; k[n].m.ancho10_hz = 60U;
        k[n].m.pico_pc = 55U; k[n].m.ciclo_pc = 48U; n++;

        memset(&g_cad, 0, sizeof g_cad);
        for (j = 0; j < n; j++) {
            if (!sale(&k[j].m, k[j].espera)) {
                di(k[j].que, 0, k[j].espera);
                lista(&k[j].m);
            } else {
                di(k[j].que, 1, k[j].espera);
            }
        }

        /*
         * Y LA BANDA DE FT8 DE VERDAD, que es por donde empezo todo.
         *
         * Pedido por el usuario.
         *
         * En 7.074 no hay una señal de 50 Hz, hay cuarenta repartidas por
         * los 3 kHz del canal: el ancho medido es el del monton y la fila
         * del ancho no puede cuadrar. Lo que identifica es el reloj.
         */
        {
            stanag_det_t m = vacia();
            m.hay_esp = 1U; m.ancho10_hz = 2700U; m.centro_hz = 1500U;
            m.pico_pc = 3U; m.ciclo_pc = 90U;
            memset(&g_cad, 0, sizeof g_cad);
            g_cad.vale = 1U; g_cad.firmeza = 75U;
            g_cad.periodo_ds = 150U;   /* 15,0 s */
            g_cad.ocupa_pc = 84U;      /* 12,64 de 15 */
            di("una banda llena de FT8, por el reloj",
               sale(&m, "FT8 (o JS8 de 15 s)"), "7.074 kHz");
            lista(&m);

            /* Y el WSPR, que es el mismo instrumento con otro numero. */
            g_cad.periodo_ds = 1200U;  /* 120 s */
            g_cad.ocupa_pc = 92U;      /* 110,6 de 120 */
            m.ancho10_hz = 200U;
            di("una banda de WSPR, por el reloj",
               sale(&m, "WSPR"), "ciclo de 2 min");
            lista(&m);

            /* Y que una cadencia floja NO diga nada: un ciclo que apenas
             * destaca del ruido es una coincidencia, no un modo. */
            g_cad.periodo_ds = 150U; g_cad.ocupa_pc = 84U;
            g_cad.firmeza = 20U;
            di("una cadencia floja no identifica",
               !sale(&m, "FT8 (o JS8 de 15 s)"), "firmeza 20%");
            memset(&g_cad, 0, sizeof g_cad);
        }

        /*
         * Y las dos que NO pueden confundirse: la portadora no es CW.
         *
         * SE BUSCAN POR NOMBRE Y NO POR INDICE - 05/10/2026. Esto decia
         * k[10] y k[11], y el dia que se metio un caso nuevo en medio -el
         * de FUG, 850 Hz y 50 baudios- los dos indices se corrieron uno y
         * el banco empezo a comparar la portadora contra el WSPR. Dio
         * "MAL" en una comprobacion que no habia tocado nadie, que es la
         * peor forma de fallar: manda a buscar el fallo donde no esta.
         */
        {
            const stanag_det_t *port = 0, *cw = 0;
            for (j = 0; j < n; j++) {
                if (strcmp(k[j].que, "una raya sola, siempre encendida") == 0) {
                    port = &k[j].m;
                }
                if (strcmp(k[j].que, "una raya sola, parpadeando") == 0) {
                    cw = &k[j].m;
                }
            }
            if (!port || !cw) {
                di("estan los dos casos de la raya sola", 0, "se han renombrado");
            } else {
                di("una portadora no sale como CW",
                   !sale(port, "CW (Morse)"), 0);
                di("el CW no sale como portadora",
                   !sale(cw, "Portadora sola (heterodino)"), 0);
            }
        }
    }

    /* 3. Las pistas se callan cuando hay un nombre. */
    printf("\n  las pistas se callan cuando hay un nombre\n");
    {
        stanag_det_t m = vacia();
        uint8_t cand[8];
        uint16_t n, j;
        int pista = 0;

        /* Un 4285: 2531 Hz de ancho cuadra ademas con la pista de "modem
         * ancho", que es justo la que no tiene que salir. */
        memset(&g_cad, 0, sizeof g_cad);
        m.hay_esp = 1U; m.ancho10_hz = 2531U; m.ciclo_pc = 95U;
        m.vale = 1U; m.periodo_us = 10667U; m.calidad = 70U;
        n = ident_casa(&m, &g_cad, cand, 8U);
        for (j = 0U; j < n; j++) {
            if (strncmp(t[cand[j]].nombre, "Modem", 5) == 0
                || strncmp(t[cand[j]].nombre, "Podria", 6) == 0) { pista = 1; }
        }
        di("un 4285 no arrastra la pista del ancho", !pista, 0);
        lista(&m);

        /* Pero sola, la pista SI tiene que salir: si no, la supresion
         * estaria tapando la unica respuesta que habia. */
        m.vale = 0U; m.periodo_us = 0U;
        di("y sin trama, la pista sale",
           sale(&m, "Podria ser HFDL, o un modem de fase"), 0);
        lista(&m);
    }

    /* 4. Y el medidor de cadencia, de verdad: audio dentro, ciclo fuera. */
    printf("\n  el medidor de cadencia, con audio\n");
    {
        struct { const char *que; float ciclo_s; float on_s; uint16_t espera_ds;
                 uint8_t espera_pc; float segundos; } k[] = {
            { "FT8: 12,64 s de cada 15",   15.0f, 12.64f, 150U, 84U, 75.0f },
            { "FT4: 4,48 s de cada 7,5",    7.5f,  4.48f,  75U, 60U, 60.0f },
            { "WSPR: 110,6 s de cada 120",120.0f,110.6f, 1200U, 92U, 500.0f },
        };
        unsigned c;

        for (c = 0U; c < (sizeof k / sizeof k[0]); c++) {
            ident_cad_t r;
            float t = 0.0f;
            float buf[256];
            unsigned paso = 0U, i2;
            unsigned semilla = 12345U;

            ident_modo_borra();
            if (!ident_modo_start()) {
                di(k[c].que, 0, "no arranca: sin RAM prestada");
                continue;
            }
            while (t < k[c].segundos) {
                float en_ciclo = t - k[c].ciclo_s * (float)((int)(t / k[c].ciclo_s));
                int encendido = (en_ciclo < k[c].on_s);
                for (i2 = 0U; i2 < 256U; i2++) {
                    /* Ruido de fondo siempre, y 30 dB mas cuando emite:
                     * eso es lo que ve la radio en una banda de FT8. */
                    float r1;
                    semilla = semilla * 1103515245U + 12345U;
                    r1 = (float)((semilla >> 16) & 0x7FFFU) / 16384.0f - 1.0f;
                    buf[i2] = encendido ? (r1 * 1.0f) : (r1 * 0.03f);
                }
                ident_modo_mete(buf, 256U);
                t += 256.0f / 12000.0f;
                if (++paso >= 4U) { paso = 0U; ident_modo_poll(); }
            }
            ident_modo_poll();
            ident_cadencia(&r);
            ident_modo_stop();

            if (!r.vale) {
                di(k[c].que, 0, "no ha medido ciclo");
            } else {
                char d[64];
                int ok = (r.periodo_ds >= (uint16_t)(k[c].espera_ds * 9 / 10)
                          && r.periodo_ds <= (uint16_t)(k[c].espera_ds * 11 / 10)
                          && r.ocupa_pc + 15U >= k[c].espera_pc
                          && r.ocupa_pc <= (uint8_t)(k[c].espera_pc + 12U));
                snprintf(d, sizeof d, "mide %u,%u s  %u%%  firmeza %u%%",
                         (unsigned)(r.periodo_ds / 10U),
                         (unsigned)(r.periodo_ds % 10U),
                         (unsigned)r.ocupa_pc, (unsigned)r.firmeza);
                di(k[c].que, ok, d);
            }
        }
    }

    /*
     * Y EL MEDIDOR DE VELOCIDAD DE SIMBOLO, con audio: un MFSK sintetico de
     * veintiun tonos a 23,44 baudios, que es la señal que mando el usuario.
     *
     * Pedido por el usuario.
     */
    {
        ident_cad_t cd;
        stanag_det_t mm;
        float buf[256];
        unsigned i2;
        double fase = 0.0, t = 0.0;
        unsigned semilla = 2468U;
        int tono = 0;
        const double SEP = 23.4375, T = 1.0 / 23.4375, F0 = 905.0;

        ident_modo_borra();
        if (!ident_modo_start()) {
            di("MFSK de 21 tonos a 23,44 Bd", 0, "no arranca");
        } else {
            while (t < 30.0) {
                for (i2 = 0U; i2 < 256U; i2++) {
                    double ts = t + (double)i2 / 12000.0;
                    int s2 = (int)(ts / T);
                    if (s2 != tono) {
                        semilla = semilla * 1103515245U + 12345U;
                        tono = s2;
                    }
                    {
                        double f = F0 + SEP * (double)((semilla >> 16) % 21U);
                        fase += 2.0 * 3.14159265358979 * f / 12000.0;
                        buf[i2] = (float)(0.5 * sin(fase));
                    }
                }
                ident_modo_mete(buf, 256U);
                t += 256.0 / 12000.0;
                ident_modo_poll();
            }
            ident_medidas(&mm);
            ident_cadencia(&cd);
            memset(&g_cad, 0, sizeof g_cad);
            {
                char d[80];
                int ok = (mm.simb_x10 >= 225U && mm.simb_x10 <= 245U);
                snprintf(d, sizeof d, "mide %u,%u Bd (calidad %u%%), ~%u tonos",
                         (unsigned)(mm.simb_x10 / 10U), (unsigned)(mm.simb_x10 % 10U),
                         (unsigned)mm.simb_q, (unsigned)mm.tonos);
                di("MFSK de 21 tonos a 23,44 Bd", ok, d);
                di("y lo llama MFSK multitono",
                   sale(&mm, "MFSK multitono (Olivia y parecidos)"), 0);
                printf("        (dos tonos: %s, ancho %u)\n",
                       mm.hay_tonos ? "SI" : "no", (unsigned)mm.ancho10_hz);
                lista(&mm);
            }
            ident_modo_stop();
        }
    }

    /*
     * Y LA PRUEBA NEGATIVA, que es la que da valor a las tres de arriba:
     * lo que NO tiene ciclo no puede sacar uno. Un modem que emite seguido
     * y ruido de banda son los dos casos que de verdad se oyen todo el
     * rato, y si cualquiera de los dos sacara "FT8" el modo entero valdria
     * menos que nada.
     */
    {
        struct { const char *que; int continuo; } k[] = {
            { "una señal continua no saca ciclo", 1 },
            { "el ruido solo no saca ciclo",      0 },
        };
        unsigned c;

        for (c = 0U; c < (sizeof k / sizeof k[0]); c++) {
            ident_cad_t r;
            float buf[256];
            unsigned paso = 0U, i2, blo;
            unsigned semilla = 777U;
            float t = 0.0f;

            ident_modo_borra();
            if (!ident_modo_start()) { di(k[c].que, 0, "no arranca"); continue; }
            for (blo = 0U; t < 300.0f; blo++) {
                for (i2 = 0U; i2 < 256U; i2++) {
                    float r1;
                    semilla = semilla * 1103515245U + 12345U;
                    r1 = (float)((semilla >> 16) & 0x7FFFU) / 16384.0f - 1.0f;
                    buf[i2] = k[c].continuo ? r1 : (r1 * 0.03f);
                }
                ident_modo_mete(buf, 256U);
                t += 256.0f / 12000.0f;
                if (++paso >= 4U) { paso = 0U; ident_modo_poll(); }
            }
            ident_modo_poll();
            ident_cadencia(&r);
            ident_modo_stop();

            if (r.vale && r.firmeza >= 40U) {
                char d[64];
                snprintf(d, sizeof d, "saca %u,%u s con firmeza %u%%",
                         (unsigned)(r.periodo_ds / 10U),
                         (unsigned)(r.periodo_ds % 10U), (unsigned)r.firmeza);
                di(k[c].que, 0, d);
            } else {
                di(k[c].que, 1, 0);
            }
        }
    }

    /*
     * ==================================================================
     * EL ENCAJE - 08/10/2026
     * ==================================================================
     *
     * Pedido por el usuario.
     *
     * ident_encaje() es una funcion PURA, asi que aqui no se le mete señal:
     * se le meten medidas inventadas y se comprueba que se comporta como
     * dice su cabecera. Lo que se vigila no son valores concretos -eso seria
     * clavar el banco a la formula de hoy- sino las PROPIEDADES que la hacen
     * util, que son las que no pueden romperse sin que el numero deje de
     * significar nada:
     *
     *   - pegado al borde de un rango encaja MENOS que en mitad de el,
     *   - salvo si ese borde es un limite fisico (el 100% de ciclo), que
     *     es lo que hacia que un SITOR-B de libro saliera al 0%,
     *   - una fila sin comprobar en antena no pasa del 70%,
     *   - si cuadran varias, todas bajan,
     *   - y una medida fuera de rango da 0, nunca un numero amable.
     */
    printf("\n  el encaje (ident_encaje)\n");
    {
        uint16_t total2 = 0U;
        const ident_firma_t *t2 = ident_firmas(&total2);
        uint16_t i3;
        int16_t  i_sitor = -1, i_ciclo = -1, i_noprob = -1;

        /* Una fila con rango de ciclo que llega a 100, y una sin probar. */
        for (i3 = 0U; i3 < total2; i3++) {
            if (t2[i3].ciclo_max == 100U && t2[i3].ciclo_min > 0U
                && i_ciclo < 0) { i_ciclo = (int16_t)i3; }
            if (t2[i3].probada == 0U && i_noprob < 0) { i_noprob = (int16_t)i3; }
            if (t2[i3].criterio == IDENT_POR_TONOS && t2[i3].sep_min > 0U
                && i_sitor < 0) { i_sitor = (int16_t)i3; }
        }

        if (i_sitor < 0) {
            di("hay una fila de dos tonos que mirar", 0, "no la encuentro");
        } else {
            const ident_firma_t *f = &t2[i_sitor];
            stanag_det_t a2 = medida_de(f);
            stanag_det_t b2 = a2;
            uint8_t ea, eb;

            /* b2 con la separacion pegada al borde de arriba */
            b2.sep_hz = f->sep_max;
            ea = ident_encaje(&a2, &g_cad, (uint16_t)i_sitor, 1U);
            eb = ident_encaje(&b2, &g_cad, (uint16_t)i_sitor, 1U);
            {
                char d[80];
                snprintf(d, sizeof d, "centro %u%%, borde %u%%",
                         (unsigned)ea, (unsigned)eb);
                di("en mitad del rango encaja mas que en el borde",
                   (ea > eb), d);
            }

            /* fuera de rango: cero, no un numero amable */
            b2 = a2; b2.sep_hz = (uint16_t)(f->sep_max + 50U);
            {
                uint8_t e2 = ident_encaje(&b2, &g_cad, (uint16_t)i_sitor, 1U);
                char d[64];
                snprintf(d, sizeof d, "da %u%%", (unsigned)e2);
                di("fuera de rango da cero", e2 == 0U, d);
            }

            /* varias cuadran: baja */
            {
                uint8_t e1 = ident_encaje(&a2, &g_cad, (uint16_t)i_sitor, 1U);
                uint8_t e2 = ident_encaje(&a2, &g_cad, (uint16_t)i_sitor, 2U);
                uint8_t e4 = ident_encaje(&a2, &g_cad, (uint16_t)i_sitor, 4U);
                char d[80];
                snprintf(d, sizeof d, "1->%u%%  2->%u%%  4->%u%%",
                         (unsigned)e1, (unsigned)e2, (unsigned)e4);
                di("si cuadran varias, todas bajan",
                   (e2 <= e1) && (e4 <= e2) && (e4 < e1), d);
            }
        }

        /* El borde que es un limite fisico NO descuenta. */
        if (i_ciclo < 0) {
            di("hay una fila con el ciclo hasta 100", 0, "no la encuentro");
        } else {
            const ident_firma_t *f = &t2[i_ciclo];
            stanag_det_t a2 = medida_de(f);
            uint8_t e_med, e_tope;

            a2.ciclo_pc = (uint8_t)((f->ciclo_min + f->ciclo_max) / 2U);
            e_med = ident_encaje(&a2, &g_cad, (uint16_t)i_ciclo, 1U);
            a2.ciclo_pc = 100U;
            e_tope = ident_encaje(&a2, &g_cad, (uint16_t)i_ciclo, 1U);
            {
                char d[96];
                snprintf(d, sizeof d, "mitad %u%%, al 100%% de ciclo %u%%",
                         (unsigned)e_med, (unsigned)e_tope);
                di("emitir seguido no es 'por los pelos'",
                   e_tope >= e_med, d);
            }
        }

        /* Sin comprobar en antena: techo del 70%. */
        if (i_noprob < 0) {
            di("todas las filas estan probadas en antena", 1, "nada que mirar");
        } else {
            stanag_det_t a2 = medida_de(&t2[i_noprob]);
            uint8_t e2 = ident_encaje(&a2, &g_cad, (uint16_t)i_noprob, 1U);
            char d[64];
            snprintf(d, sizeof d, "da %u%%", (unsigned)e2);
            di("una fila sin ver en antena no pasa del 70%", e2 <= 70U, d);
        }

        /* Y que ninguna fila, con su propia medida, salga a cero: eso seria
         * una fila que no puede alcanzar su propio encaje. */
        {
            uint16_t malas = 0U, peor_i = 0U; uint8_t peor = 100U;
            for (i3 = 0U; i3 < total2; i3++) {
                stanag_det_t a2 = medida_de(&t2[i3]);
                uint8_t e2 = ident_encaje(&a2, &g_cad, i3, 1U);
                if (e2 == 0U) { malas++; }
                if (e2 < peor) { peor = e2; peor_i = i3; }
            }
            {
                char d[128];
                snprintf(d, sizeof d, "la peor es \"%s\" con %u%%",
                         ident_nombre(&t2[peor_i]), (unsigned)peor);
                di("ninguna fila saca cero con su propia medida",
                   malas == 0U, d);
            }
        }
    }

    /*
     * Y QUE NINGUNA FILA PIDA UN SALTO MAS ANCHO DEL QUE EL MOTOR PUBLICA.
     * 10/10/2026.
     *
     * stanag_det.c recorta el par de tonos a TONO_SEP_MAX = 1200 Hz, y ese
     * numero NO se eligio: sale de esta tabla, cuya fila mas ancha es el
     * APRS con 900..1100. Son dos sitios que tienen que decir lo mismo y
     * estan en ficheros distintos, o sea exactamente donde se cuela que uno
     * se quede atras: el dia que entre una firma de salto mas ancho, el
     * motor la recortaria y la fila no saldria NUNCA, sin un solo aviso.
     *
     * Asi que se comprueba aqui, que es donde vive la tabla.
     */
    {
        uint16_t n = 0U, i;
        const ident_firma_t *t = ident_firmas(&n);
        uint16_t peor = 0U;
        const char *quien = "";

        for (i = 0U; i < n; i++) {
            if (t[i].criterio != IDENT_POR_TONOS && t[i].criterio != IDENT_POR_SEP) {
                continue;
            }
            if (t[i].sep_max > peor) { peor = t[i].sep_max; quien = ident_nombre(&t[i]); }
        }
        printf("\n  el salto mas ancho de la tabla: %u Hz (%s)\n", peor, quien);
        di("cabe en el tope que publica stanag_det.c (1200 Hz)",
           peor <= 1200U, quien);
    }

    printf("\n%s\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO");
    return fallos ? 1 : 0;
}
