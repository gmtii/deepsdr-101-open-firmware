/*
 * ======================================================================
 * LOS INDICATIVOS DEL MAPA, EL RAYO Y LA FICHA - 12/10/2026
 * ======================================================================
 *
 * LO QUE AQUI SE MIDE, Y POR QUE CADA COSA:
 *
 *   1) DE DONDE SALE EL INDICATIVO. "CQ DX JA5AFW PM64" tiene que dar
 *      JA5AFW y no DX; "CQ PM64" no tiene que dar nada. Esto es el
 *      cimiento: si falla, el mapa pone rotulos que dicen "CQ".
 *
 *   2) EL TOPE SUBE CON EL ZOOM, porque es lo unico que hace que a escala
 *      1 no sea un borron y a escala 8 no sea un mapa vacio.
 *
 *   3) EL QUE LLEGA TARDE SE CALLA. Quince estaciones pegadas y solo
 *      entran las que caben, y son LAS MAS NUEVAS. Se comprueba por
 *      nombre, no por cuantas: que entren cinco no dice nada si son las
 *      cinco de hace media hora.
 *
 *   4) LA MISMA ESTACION, UNA VEZ. Un CQ repetido seis veces es una
 *      etiqueta y un "x6", no seis etiquetas encima.
 *
 *   5) EL RAYO ES UN ARCO Y NO UNA RAYA, y esto se mide EN PIXELES
 *      PINTADOS: Madrid-Tokio por la ortodromica pasa muy al norte de la
 *      raya recta. Si alguien cambiara el slerp por una interpolacion de
 *      longitud y latitud -el error clasico- el dibujo seguiria saliendo
 *      bonito y este banco se pondria rojo.
 *
 *   6) Y NO CRUZA LA PANTALLA. Un arco que pasa por el meridiano 180 se
 *      proyecta con dos puntos en extremos opuestos; unirlos pinta una
 *      raya de lado a lado que no es ningun camino. Se cuenta cuantos
 *      pixeles pinta cada fila.
 *
 *   7) EL TOQUE DEVUELVE EL DE DEBAJO, el de al lado no, y tocar el mar
 *      suelta.
 *
 *   8) LA SELECCION SOBREVIVE A QUE LA LISTA CAMBIE DE ORDEN, que es lo
 *      que pasa cada quince segundos en FT8.
 *
 *   9) LOS NUMEROS DE LA FICHA: kilometros contra una distancia conocida,
 *      azimut contra un rumbo conocido, y la vuelta de la medianoche.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "mapa.h"
#include "mapa_eti.h"
#include "dxcc.h"
#include "idioma.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos;

static void exige(int cond, const char *que)
{
    printf("    %-62s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

/* El hueco de verdad: el panel de FT8 menos su cabecera. */
#define HUECO_X 0
#define HUECO_Y 0
#define HUECO_W 800
#define HUECO_H 288

static mapa_caja_t g_caja;

/* Un renglon de FT8 como los que arma ft8_decoder.c:
 * "HH:MM \t Hz \t dB \t DT \t mensaje \t km \t pais". */
static char g_lin[32][96];

static const char *linea(const char *hhmm, int hz, int db, const char *msg)
{
    static int n;
    char *b = g_lin[n % 32];

    n++;
    snprintf(b, sizeof g_lin[0], "%s\t%d\t%+d\t+0,1\t%s\t\t", hhmm, hz, db, msg);
    return b;
}

/* ------------------------------------------------------------------ */
/* Mete un CQ en la lista, como hace main.c.                          */
static uint8_t mete(const char *call, const char *loc, int16_t db, uint16_t min)
{
    mapa_eti_dx_t dx;

    memset(&dx, 0, sizeof dx);
    /* memcpy y no snprintf: con -Wall el compilador avisa de que un "%s"
     * podria no caber en los doce bytes del indicativo, y tiene razon. */
    { size_t n = strlen(call); if (n > sizeof dx.call - 1U) { n = sizeof dx.call - 1U; }
      memcpy(dx.call, call, n); dx.call[n] = '\0'; }
    { size_t n = strlen(loc); if (n > sizeof dx.loc - 1U) { n = sizeof dx.loc - 1U; }
      memcpy(dx.loc, loc, n); dx.loc[n] = '\0'; }
    dx.snr = db;
    dx.minuto = min;
    return mapa_eti_pon(&dx);
}

/* ------------------------------------------------------------------ */
/* El dibujo, para poder contar pixeles.                              */
static void pinta(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(0x000000UL));
    mapa_eti_dibuja(s);
}

void pinta_con_mapa(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    mapa_dibuja(s, &g_caja);
    mapa_eti_dibuja(s);
    mapa_zoom_botones(s, &g_caja);
    {
        int16_t px, py;

        if (mapa_loc_a_px(&g_caja, "IN80", &px, &py)) {
            mapa_casa(s, &g_caja, px, py);
        }
    }
    mapa_eti_ficha(s, 703U);
}

static void pinta_todo(void)
{
    lcd_sim_reset(0x0000);
    gfx2_render(HUECO_X, HUECO_Y, HUECO_W, HUECO_H, pinta, 0);
}

/* Cuantos pixeles de esta fila no son el fondo. */
static int fila_pintada(int y)
{
    const uint16_t *fb = lcd_sim_fb();
    int x, n = 0;

    for (x = 0; x < HUECO_W; x++) {
        if (fb[(size_t)y * LCD_W + (size_t)x] != 0U) { n++; }
    }
    return n;
}

/* La y del pixel pintado mas alto de esta columna, o -1. */
static int columna_arriba(int x)
{
    const uint16_t *fb = lcd_sim_fb();
    int y;

    for (y = 0; y < HUECO_H; y++) {
        if (fb[(size_t)y * LCD_W + (size_t)x] != 0U) { return y; }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
int main(void)
{
    char buf[80];   /* EL MISMO QUE mapa_eti_ficha(). Ver el banco de abajo. */
    int16_t px_mad, py_mad, px_tok, py_tok;

    mapa_pon(&g_caja, HUECO_X, HUECO_Y, HUECO_W, HUECO_H);

    printf("\n== LOS INDICATIVOS DEL MAPA ==\n");

    /* ---------------------------------------------------------------- */
    printf("\n  1) de donde sale el indicativo\n\n");
    {
        static const struct { const char *msg; const char *espera; } k[] = {
            { "CQ EA1ABC IN80",      "EA1ABC" },
            { "CQ DX JA5AFW PM64",   "JA5AFW" },
            { "CQ NA K1ABC FN42",    "K1ABC"  },
            { "CQ POTA W7XYZ DM41",  "W7XYZ"  },
            { "CQ IN80",             ""       },   /* sin indicativo */
            { "CQ EA1ABC",           ""       },   /* sin localizador */
            { "CQ EA1ABC RR73",      ""       },   /* RR73 no es localizador */
            { "EA1ABC JA5AFW -10",   ""       },   /* no es un CQ con loc */
            { "CQ DX EA1ABC IN80HJ", "EA1ABC" },   /* localizador de seis */
            /*
             * Y LAS RESPUESTAS, QUE SON LA MAYORIA - 23/10/2026.
             *
             * Avisado por un usuario con una foto de la lista: de siete
             * renglones con localizador, SEIS eran respuestas y uno era
             * el CQ. El segundo mensaje de un ciclo de FT8 es "LLAMADO
             * QUIENCONTESTA LOCATOR", asi que trae localizador igual que
             * un CQ.
             *
             * Y lo que hay que sacar de ahi es el SEGUNDO indicativo, que
             * es quien transmite; el primero es a quien llama. Poner el
             * primero seria pintar en el mapa a quien NO esta hablando, y
             * encima en el sitio del otro.
             *
             * Sale bien sin tocar nada porque mapa_ind_de_linea() cuenta
             * desde el final -la palabra anterior al localizador-, pero
             * eso no estaba medido: la tabla solo traia CQ.
             */
            { "EP2AES YC2ULK OI52",  "YC2ULK" },
            { "3B8GL IW4DLP JN54",   "IW4DLP" },
            { "PP1WW UA3DTS KO95",   "UA3DTS" },
            { "M1DOZ 5Z4RS -04",     ""       },   /* informe: sin loc */
            { "M1DOZ 5Z4RS RR73",    ""       },   /* despedida: tampoco */
            { "UG4P 5Z4RS R-11",     ""       }
        };
        unsigned i;
        int bien = 1;

        for (i = 0; i < sizeof k / sizeof k[0]; i++) {
            char ind[16];
            const char *l = linea("12:30", 1200, -8, k[i].msg);
            uint8_t hay = mapa_ind_de_linea(l, ind);

            if (!hay) { ind[0] = '\0'; }
            printf("     %-24s -> %-8s %s\n", k[i].msg,
                   ind[0] ? ind : "(nada)",
                   (strcmp(ind, k[i].espera) == 0) ? "" : "   <-- esperaba otra cosa");
            if (strcmp(ind, k[i].espera) != 0) { bien = 0; }
        }
        exige(bien, "el indicativo es la palabra anterior al localizador");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  2) el tope es el del buffer, en todas las escalas\n\n");
    {
        /*
         * *** ESTE APARTADO EXIGIA "a mas zoom, mas etiquetas" HASTA EL
         * 28/10/2026. ***
         *
         * Protegia el "4 + escala" de mapa_eti_tope(), y ese numero se ha
         * quitado: con el mapamundi puesto daba CINCO etiquetas, el
         * usuario tenia doce aviones en la lista y contaba cinco nombres
         * en el mapa. Quien reparte el sitio es el choque de seis
         * colocaciones de mapa_eti_pon() -ver el apartado 3, que es el
         * que de verdad lo mide-, y encima de el ese tope solo tiraba
         * etiquetas que SI cabian.
         *
         * Lo que se exige ahora es lo que queda siendo verdad: el unico
         * tope es el del array, y es el mismo en todas las escalas. Un
         * banco que siguiera pidiendo que suba con el zoom estaria
         * protegiendo lo que se acaba de quitar.
         */
        static const uint8_t k_esc[] = { 1U, 2U, 4U, 8U };
        unsigned i;
        int bien = 1;

        for (i = 0; i < sizeof k_esc / sizeof k_esc[0]; i++) {
            mapa_caja_t c = g_caja;
            uint8_t t;

            c.esc = k_esc[i];
            t = mapa_eti_tope(&c);
            printf("     escala %u: caben %u etiquetas\n",
                   (unsigned)k_esc[i], (unsigned)t);
            if (t != (uint8_t)MAPA_ETI_MAX) { bien = 0; }
        }
        exige(bien, "el tope es MAPA_ETI_MAX en todas las escalas");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  3) el que llega tarde se calla, pero no el segundo\n\n");
    {
        /*
         * Quince estaciones en el MISMO cuadrado de localizador, o sea
         * todas en el mismo pixel. Se ofrecen de la mas nueva a la mas
         * vieja, que es como lo hace main.c.
         *
         * ESTE BANCO EXIGIA "SOLO SE PINTA UNA" HASTA EL 13/10/2026, y lo
         * que estaba protegiendo era medio cierto: lo correcto es que no
         * se amontonen y que gane la mas nueva, NO que se pinte una sola.
         * Pintar una sola es lo que hizo que el usuario viera tres CQ en la
         * lista y solo dos en el mapa, con dos estaciones inglesas en
         * IO92. Desde que hay seis colocaciones posibles entran las que
         * quepan sin pisarse, y el resto -esas si- se callan.
         *
         * Asi que lo que se mide ahora son las tres cosas de verdad:
         * entran VARIAS, ninguna pisa a otra, y no entran las quince.
         */
        char call[16];
        unsigned i;
        int entradas = 0;

        mapa_eti_empieza(&g_caja, "IN80");
        for (i = 0; i < 15; i++) {
            snprintf(call, sizeof call, "EA%uAAA", i);
            if (mete(call, "JO20", -10, 600U)) { entradas++; }
        }
        mapa_eti_cierra();
        printf("     quince estaciones en el mismo cuadrado -> %u etiqueta(s)\n",
               (unsigned)mapa_eti_n());
        exige(mapa_eti_n() == (uint8_t)entradas,
              "entran las que caben y las demas se callan");
        exige(mapa_eti_n() > 1U && mapa_eti_n() < 15U,
              "varias, pero no las quince: el sitio se acaba");
        {
            uint8_t a, b, pisan = 0U;
            for (a = 0U; a < mapa_eti_n(); a++) {
                int16_t xa, ya, wa;
                mapa_eti_caja_dbg(a, &xa, &ya, &wa);
                for (b = (uint8_t)(a + 1U); b < mapa_eti_n(); b++) {
                    int16_t xb, yb, wb;
                    mapa_eti_caja_dbg(b, &xb, &yb, &wb);
                    if (xa + wa > xb && xb + wb > xa
                        && ya + 16 > yb && yb + 16 > ya) { pisan++; }
                }
            }
            exige(pisan == 0U, "y ninguna de las que entran pisa a otra");
        }

        /* Y es LA PRIMERA que se ofrecio, o sea la mas nueva. */
        mapa_eti_empieza(&g_caja, "IN80");
        (void)mete("NUEVA1", "JO20", -5, 700U);
        (void)mete("VIEJA1", "JO20", -5, 600U);
        mapa_eti_cierra();
        (void)mapa_eti_toca(0, 0);   /* suelta lo que hubiera */
        mapa_eti_empieza(&g_caja, "IN80");
        (void)mete("NUEVA1", "JO20", -5, 700U);
        (void)mete("VIEJA1", "JO20", -5, 600U);
        mapa_eti_cierra();
        {
            int16_t px, py;
            uint8_t gano;

            (void)mapa_loc_a_px(&g_caja, "JO20", &px, &py);
            gano = mapa_eti_toca(px, py);
            printf("     la que gana el sitio es %s\n", mapa_eti_sel());
            exige(gano && strcmp(mapa_eti_sel(), "NUEVA1") == 0,
                  "gana la mas nueva, no la que llego despues");
            mapa_eti_suelta();
        }
    }

    /* ---------------------------------------------------------------- */
    printf("\n  4) la misma estacion, una sola etiqueta\n\n");
    {
        unsigned i;

        mapa_eti_empieza(&g_caja, "IN80");
        for (i = 0; i < 6; i++) { (void)mete("JA5AFW", "PM64", -8, (uint16_t)(700 - i)); }
        (void)mete("K1ABC", "FN42", -3, 690U);
        mapa_eti_cierra();
        printf("     seis CQ de JA5AFW y uno de K1ABC -> %u etiquetas\n",
               (unsigned)mapa_eti_n());
        exige(mapa_eti_n() == 2U, "el CQ repetido no pinta seis rotulos");

        {
            int16_t px, py;

            (void)mapa_loc_a_px(&g_caja, "PM64", &px, &py);
            (void)mapa_eti_toca(px, py);
            (void)mapa_eti_texto(buf, (uint8_t)sizeof buf, 700U);
            printf("     la ficha dice: %s\n", buf);
            exige(strstr(buf, "x6") != 0, "y lo cuenta: sale el x6");
            mapa_eti_suelta();
        }
    }

    /* ---------------------------------------------------------------- */
    printf("\n  5) el rayo es un arco, no una raya\n\n");
    {
        int y_arco, y_recta, x_medio;

        (void)mapa_loc_a_px(&g_caja, "IN80", &px_mad, &py_mad);
        (void)mapa_loc_a_px(&g_caja, "PM64", &px_tok, &py_tok);
        x_medio = (px_mad + px_tok) / 2;
        y_recta = (py_mad + py_tok) / 2;

        mapa_eti_empieza(&g_caja, "IN80");
        (void)mete("JA5AFW", "PM64", -8, 700U);
        mapa_eti_cierra();
        pinta_todo();

        y_arco = columna_arriba(x_medio);
        printf("     Madrid %d,%d   Tokio %d,%d\n",
               (int)px_mad, (int)py_mad, (int)px_tok, (int)py_tok);
        printf("     en la columna de en medio (x=%d): la raya recta pasaria\n"
               "     por y=%d y el arco pintado pasa por y=%d\n",
               x_medio, y_recta, y_arco);
        printf("     o sea %d pixeles mas al NORTE\n\n", y_recta - y_arco);
        /*
         * La ortodromica Madrid-Tokio sube hasta unos 70 grados norte; la
         * raya recta se quedaria en la latitud media, unos 38. En este
         * mapa eso son del orden de setenta pixeles. Se exige la mitad,
         * que es de sobra para separar un arco de una raya y no tan justo
         * como para romperse si alguien afina la proyeccion.
         */
        exige(y_arco > 0 && (y_recta - y_arco) > 35,
              "el arco pasa muy al norte de la raya recta");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  6) y no cruza la pantalla por el meridiano 180\n\n");
    {
        int y, peor = 0, peor_y = -1;

        /* Tokio -> Los Angeles: la ortodromica cruza el Pacifico y pasa
         * por el meridiano 180. */
        mapa_eti_empieza(&g_caja, "PM95");
        (void)mete("K6XYZ", "DM04", -12, 700U);
        mapa_eti_cierra();
        pinta_todo();

        for (y = 0; y < HUECO_H; y++) {
            int n = fila_pintada(y);

            if (n > peor) { peor = n; peor_y = y; }
        }
        printf("     la fila con mas pixeles pintados es la %d, con %d\n",
               peor_y, peor);
        printf("     (una raya de lado a lado pintaria del orden de %d)\n\n",
               HUECO_W);
        exige(peor < HUECO_W / 3,
              "ninguna fila lleva una raya que cruce el mapa entero");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  7) el toque\n\n");
    {
        int16_t px, py;

        mapa_eti_empieza(&g_caja, "IN80");
        (void)mete("JA5AFW", "PM64", -8, 700U);
        (void)mete("K1ABC", "FN42", -3, 699U);
        mapa_eti_cierra();

        (void)mapa_loc_a_px(&g_caja, "FN42", &px, &py);
        exige(mapa_eti_toca(px, py) && strcmp(mapa_eti_sel(), "K1ABC") == 0,
              "tocar la marca de Boston elige K1ABC");

        (void)mapa_loc_a_px(&g_caja, "PM64", &px, &py);
        exige(mapa_eti_toca(px, py) && strcmp(mapa_eti_sel(), "JA5AFW") == 0,
              "y tocar la de Japon cambia a JA5AFW");

        /* Mitad del Atlantico sur: ahi no hay nadie. */
        (void)mapa_loc_a_px(&g_caja, "ID00", &px, &py);
        exige(mapa_eti_toca(px, py) && mapa_eti_sel()[0] == '\0',
              "tocar el mar suelta el que hubiera");

        /*
         * EL BLANCO DEL DEDO - 13/10/2026.
         *
         * Era 24 x 24 alrededor del punto. Este proyecto se exige 47 px
         * para un blanco de dedo en los ajustes -lo mide sim/config.c- y
         * aqui se conformaba con la mitad.
         */
        (void)mapa_loc_a_px(&g_caja, "FN42", &px, &py);
        {
            static const struct { int16_t dx, dy; int espera; const char *que; } k[] = {
                {   0,   0, 1, "justo encima del punto" },
                { -18,   0, 1, "18 px a la izquierda" },
                {   0,  18, 1, "18 px por debajo" },
                { -15, -15, 1, "en diagonal, a 15 de cada lado" },
                { -60,   0, 0, "60 px a la izquierda: eso ya no es" }
            };
            unsigned j;
            int bien = 1;

            for (j = 0; j < sizeof k / sizeof k[0]; j++) {
                int r;

                mapa_eti_suelta();
                r = (mapa_eti_toca((int16_t)(px + k[j].dx), (int16_t)(py + k[j].dy))
                     && strcmp(mapa_eti_sel(), "K1ABC") == 0) ? 1 : 0;
                printf("     %-32s %s\n", k[j].que, r ? "elige" : "no");
                if (r != k[j].espera) { bien = 0; }
            }
            exige(bien, "se acierta con el dedo, no con la punta de un palillo");
        }

        /*
         * Y CON DOS PEGADAS, GANA LA MAS CERCANA. Con los blancos
         * agrandados dos vecinas se solapan, y quedarse con la primera de
         * la lista es quedarse con la mas reciente en vez de con la que se
         * ha tocado: tocas una y se elige la de al lado.
         */
        {
            int16_t ax, ay;

            mapa_eti_suelta();
            mapa_eti_empieza(&g_caja, "IN80");
            /*
             * JO29 y JO20: misma longitud y nueve grados de latitud, que a
             * escala 1 son VEINTE PIXELES. Lo justo para que las dos
             * etiquetas quepan sin pisarse -miden dieciseis de alto- y
             * para que el dedo caiga dentro del blanco de las dos.
             */
            (void)mete("NUEVA", "JO29", -5, 700U);
            (void)mete("VIEJA", "JO20", -9, 699U);
            mapa_eti_cierra();
            printf("     entran %u etiquetas\n", (unsigned)mapa_eti_n());
            (void)mapa_loc_a_px(&g_caja, "JO20", &ax, &ay);
            printf("     dos estaciones pegadas; se toca la de JO20\n");
            exige(mapa_eti_toca(ax, ay) && strcmp(mapa_eti_sel(), "VIEJA") == 0,
                  "gana la mas cercana al dedo, no la mas nueva");
            mapa_eti_suelta();
        }
    }

    /* ---------------------------------------------------------------- */
    /*
     * 7b) EL GESTO: ARRASTRAR O TOCAR.
     *
     * Y no pasaba, por dos cosas que vivian sueltas en main.c donde
     * ningun banco llegaba:
     *
     *   - LAS COORDENADAS DE LA SUELTA SON (0,0). touch_read() devuelve 0
     *     cuando no hay dedo y no escribe x ni y; quien llama las tiene a
     *     cero. O sea que el toque se resolvia contra la esquina de
     *     arriba a la izquierda, donde no hay ningun indicativo.
     *   - DOS PIXELES NO SEPARAN UN TOQUE DE UN ARRASTRE. Con el temblor
     *     del panel resistivo contando como arrastre, el toque no llegaba
     *     nunca.
     *
     * Por eso el reparto se saco a mapa.c: para poder escribir esto.
     */
    printf("\n  7b) arrastrar o tocar, con el panel que hay\n\n");
    {
        mapa_gesto_t g;
        int16_t tx = 0, ty = 0, dx = 0, dy = 0;

        /* Un toque limpio. */
        mapa_gesto_empieza(&g, 400, 200);
        exige(mapa_gesto_suelta(&g, &tx, &ty) && tx == 400 && ty == 200,
              "un toque quieto se suelta como toque, y en su sitio");

        /*
         * UN TOQUE CON EL TEMBLOR DEL PANEL. Tres lecturas de tres
         * pixeles cada una -que es lo que hace un dedo en un panel
         * resistivo- siguen siendo un toque, aunque el mapa se haya
         * movido un poco por el camino.
         */
        mapa_gesto_empieza(&g, 400, 200);
        (void)mapa_gesto_mueve(&g, 403, 201, &dx, &dy);
        (void)mapa_gesto_mueve(&g, 401, 204, &dx, &dy);
        (void)mapa_gesto_mueve(&g, 404, 203, &dx, &dy);
        exige(mapa_gesto_suelta(&g, &tx, &ty) && tx == 400 && ty == 200,
              "y con el temblor del panel tambien, y en el sitio de origen");

        /* Un arrastre de verdad NO es un toque. */
        mapa_gesto_empieza(&g, 400, 200);
        (void)mapa_gesto_mueve(&g, 430, 200, &dx, &dy);
        (void)mapa_gesto_mueve(&g, 500, 210, &dx, &dy);
        exige(!mapa_gesto_suelta(&g, &tx, &ty),
              "un arrastre de cien pixeles no es un toque");

        /* Y el arrastre entrega el desplazamiento, no la posicion. */
        mapa_gesto_empieza(&g, 400, 200);
        dx = 0; dy = 0;
        exige(mapa_gesto_mueve(&g, 420, 190, &dx, &dy) && dx == 20 && dy == -10,
              "arrastrar entrega cuanto se ha movido");
        exige(!mapa_gesto_mueve(&g, 421, 190, &dx, &dy),
              "y un pixel no mueve nada: el mapa no vibra con el dedo quieto");

        /*
         * Y AHORA EL FALLO ENTERO, de punta a punta: tocar el indicativo
         * de Japon con un panel que al soltar dice (0,0).
         */
        {
            int16_t px, py;

            mapa_eti_empieza(&g_caja, "IN80");
            (void)mete("JA5AFW", "PM64", -8, 700U);
            mapa_eti_cierra();
            mapa_eti_suelta();

            (void)mapa_loc_a_px(&g_caja, "PM64", &px, &py);
            mapa_gesto_empieza(&g, px, py);
            (void)mapa_gesto_mueve(&g, (int16_t)(px + 2), py, &dx, &dy);
            /* La suelta: el panel dice que no hay dedo y deja x,y a cero. */
            tx = 0; ty = 0;
            if (mapa_gesto_suelta(&g, &tx, &ty)) { (void)mapa_eti_toca(tx, ty); }
            printf("     toque en %d,%d, suelta en 0,0 -> elegido \"%s\"\n",
                   (int)px, (int)py, mapa_eti_sel());
            exige(strcmp(mapa_eti_sel(), "JA5AFW") == 0,
                  "tocar el indicativo lo elige aunque la suelta diga (0,0)");
            mapa_eti_suelta();
        }
    }

    /* ---------------------------------------------------------------- */
    printf("\n  8) la seleccion sobrevive a que la lista cambie\n\n");
    {
        int16_t px, py;

        mapa_eti_empieza(&g_caja, "IN80");
        (void)mete("JA5AFW", "PM64", -8, 700U);
        (void)mete("K1ABC", "FN42", -3, 699U);
        mapa_eti_cierra();
        (void)mapa_loc_a_px(&g_caja, "PM64", &px, &py);
        (void)mapa_eti_toca(px, py);

        /* Llega una ranura nueva: otro orden y una estacion mas. */
        mapa_eti_empieza(&g_caja, "IN80");
        (void)mete("LU1XX", "FF95", -15, 701U);
        (void)mete("K1ABC", "FN42", -3, 701U);
        (void)mete("JA5AFW", "PM64", -9, 700U);
        mapa_eti_cierra();
        printf("     tras la ranura nueva sigue elegido: %s\n", mapa_eti_sel());
        exige(strcmp(mapa_eti_sel(), "JA5AFW") == 0,
              "el elegido se guarda por indicativo, no por posicion");

        /* Y si deja de oirse, se suelta: una ficha de alguien que ya no
         * esta en la lista es una pantalla que afirma lo que no es. */
        mapa_eti_empieza(&g_caja, "IN80");
        (void)mete("K1ABC", "FN42", -3, 702U);
        mapa_eti_cierra();
        exige(mapa_eti_sel()[0] == '\0',
              "y si deja de oirse, la ficha se va sola");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  9) los numeros de la ficha\n\n");
    {
        int16_t px, py;

        mapa_eti_empieza(&g_caja, "IN80");          /* Madrid */
        (void)mete("JA5AFW", "PM64", -8, 700U);     /* Japon */
        mapa_eti_cierra();
        (void)mapa_loc_a_px(&g_caja, "PM64", &px, &py);
        (void)mapa_eti_toca(px, py);

        (void)mapa_eti_texto(buf, (uint8_t)sizeof buf, 703U);
        printf("     %s\n\n", buf);
        /*
         * Madrid (IN80, 40,5N 3,0O) a PM64 (34,5N 133,0E) son 10.400 km
         * largos por la ortodromica y el rumbo inicial ronda los 40
         * grados -por encima de Siberia-. Los margenes son anchos a
         * proposito: lo que se comprueba es que la cuenta es la de la
         * esfera y no otra, no la ultima cifra.
         */
        {
            const char *p = strstr(buf, " km");
            long km = 0;

            if (p != 0) {
                const char *q = p;
                while (q > buf && q[-1] >= '0' && q[-1] <= '9') { q--; }
                km = atol(q);
            }
            printf("     kilometros leidos: %ld\n", km);
            exige(km > 9800L && km < 11000L, "la distancia es la de la esfera");
        }
        {
            const char *p = strstr(buf, "AZ ");
            long az = (p != 0) ? atol(p + 3) : -1L;

            printf("     azimut leido: %ld\n", az);
            exige(az > 20L && az < 60L, "y el azimut apunta al nordeste");
        }
        exige(strstr(buf, "-8 dB") != 0, "el dB es el del mensaje");
        exige(strstr(buf, "3 min") != 0, "y dice hace cuanto se oyo");

        /* La vuelta de la medianoche: mensaje a las 23:58, son las 00:03. */
        mapa_eti_empieza(&g_caja, "IN80");
        (void)mete("JA5AFW", "PM64", -8, (uint16_t)(23U * 60U + 58U));
        mapa_eti_cierra();
        (void)mapa_eti_texto(buf, (uint8_t)sizeof buf, 3U);
        printf("     a las 00:03, un mensaje de las 23:58: %s\n", buf);
        exige(strstr(buf, "5 min") != 0,
              "la medianoche no convierte cinco minutos en -1.435");
    }

    /* ---------------------------------------------------------------- */
    /*
     * ==================================================================
     * DOS ESTACIONES EN EL MISMO CUADRADO - 13/10/2026
     * ==================================================================
     *
     * En la lista estaban M7BSI (IO92), EA4AXM (IN80) y G8KHF (IO92). Dos
     * de los tres comparten LOCALIZADOR, o sea el mismo pixel: la chapa
     * del segundo caia justo encima de la del primero, chocaba, y el que
     * llegaba tarde se callaba ENTERO -sin chapa, sin punto y sin rayo-.
     *
     * Un cuadrado Maidenhead son dos grados de longitud por uno de
     * latitud, mas de cien kilometros de lado: que dos estaciones
     * distintas caigan en el mismo no es un caso raro, es lo normal en
     * una banda europea un domingo por la manana.
     *
     * Lo que se mide es que las TRES entran, y aparte que las chapas no
     * se pisan - que es el motivo por el que se rechazaba una-.
     */
    printf("\n  10) dos estaciones en el mismo cuadrado\n\n");
    {
        uint8_t a, b, c;

        mapa_eti_empieza(&g_caja, "IN80");
        a = mete("M7BSI",  "IO92", 15, 702U);
        b = mete("EA4AXM", "IN80",  8, 702U);
        c = mete("G8KHF",  "IO92",  9, 703U);
        mapa_eti_cierra();
        printf("     M7BSI IO92 -> %u, EA4AXM IN80 -> %u, G8KHF IO92 -> %u"
               "   etiquetas: %u\n",
               (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)mapa_eti_n());
        exige(a && b && c, "las tres entran, aunque dos compartan cuadrado");
        exige(mapa_eti_n() == 3U, "y se pintan las tres");

        {
            uint8_t i, j, pisan = 0U;
            int16_t x[3], y[3], w[3];

            for (i = 0U; i < 3U; i++) {
                mapa_eti_caja_dbg(i, &x[i], &y[i], &w[i]);
                printf("       chapa %u:  %4d..%4d   y %4d\n",
                       (unsigned)i, (int)x[i], (int)(x[i] + w[i]), (int)y[i]);
            }
            for (i = 0U; i < 3U; i++) {
                for (j = (uint8_t)(i + 1U); j < 3U; j++) {
                    if (x[i] + w[i] > x[j] && x[j] + w[j] > x[i]
                        && y[i] + 16 > y[j] && y[j] + 16 > y[i]) { pisan++; }
                }
            }
            exige(pisan == 0U, "y ninguna chapa se pisa con otra");
        }

        /* Y CON CUATRO: lo que no quepa se cae, pero no antes de haberlo
         * intentado por los cuatro sitios. */
        mapa_eti_empieza(&g_caja, "IN80");
        (void)mete("M7BSI", "IO92", 15, 702U);
        (void)mete("G8KHF", "IO92",  9, 702U);
        (void)mete("G0ABC", "IO92",  5, 702U);
        (void)mete("M0XYZ", "IO92",  3, 702U);
        mapa_eti_cierra();
        printf("     cuatro en IO92 -> %u etiquetas\n", (unsigned)mapa_eti_n());
        exige(mapa_eti_n() >= 3U,
              "con cuatro en el mismo cuadrado entran al menos tres");
    }

    /* ---------------------------------------------------------------- */
    /*
     * ==================================================================
     * LOS AVIONES DE HFDL - 13/10/2026
     * ==================================================================
     *
     * Un avion no tiene localizador Maidenhead: manda latitud y longitud.
     * Asi que mapa_eti_dx_t aprendio a aceptar la posicion a pelo.
     *
     * Y la ficha de un avion NO es la de una estacion, que es donde esta
     * la miga de verdad:
     *
     *   - no lleva "DX" delante, porque un avion no es un DX;
     *   - no lleva PAIS, y esto no es cosmetico: dxcc_pais() mira el
     *     prefijo del indicativo, y "UAL123" -United Airlines- empieza
     *     por "UA", que en la tabla es UCRANIA. Un pais equivocado con
     *     cara de dato es peor que ningun pais;
     *   - no lleva dB ni "hace N min", porque de un avion no se sabe ni
     *     con cuanta senal llego ni cuando: la tabla guarda cuantas veces
     *     se le ha oido y nada mas. Inventarlos saldria creible, que es
     *     lo peor que puede pasar.
     *
     * Lo que SI lleva: quien es, a cuanto esta y en que rumbo.
     */
    printf("\n  11) los aviones de HFDL: posicion sin localizador\n\n");
    {
        mapa_eti_dx_t av;
        int16_t px, py;

        mapa_eti_empieza(&g_caja, "IN80");          /* Madrid */

        /* Un avion sobre el Atlantico norte, a 45,0 N 30,0 W. */
        memset(&av, 0, sizeof av);
        av.call[0] = 'I'; av.call[1] = 'B'; av.call[2] = 'E';
        av.call[3] = '6'; av.call[4] = '2'; av.call[5] = '7'; av.call[6] = '\0';
        av.hay_pos = 1U; av.avion = 1U;
        av.lat_c = 4500L; av.lon_c = -3000L;
        exige(mapa_eti_pon(&av) == 1U, "un avion entra con su latitud y longitud");

        /* Y otro que lleva el nombre trampa: "UAL123" empieza por "UA". */
        memset(&av, 0, sizeof av);
        av.call[0] = 'U'; av.call[1] = 'A'; av.call[2] = 'L';
        av.call[3] = '1'; av.call[4] = '2'; av.call[5] = '3'; av.call[6] = '\0';
        av.hay_pos = 1U; av.avion = 1U;
        av.lat_c = 5000L; av.lon_c = -2000L;
        exige(mapa_eti_pon(&av) == 1U, "y otro, en otro sitio");
        mapa_eti_cierra();

        printf("     %u etiquetas de avion\n", (unsigned)mapa_eti_n());
        exige(mapa_eti_n() == 2U, "las dos salen");

        /* La ficha del primero. */
        (void)mapa_e4_a_px(&g_caja, -3000L * 100L, 4500L * 100L, &px, &py);
        exige(mapa_eti_toca(px, py) == 1U, "y se pueden tocar");
        (void)mapa_eti_texto(buf, (uint8_t)sizeof buf, 700U);
        printf("     %s\n", buf);
        exige(strstr(buf, "IBE627") != 0, "la ficha dice quien es");
        exige(strncmp(buf, "DX ", 3) != 0, "y NO pone \"DX\" delante de un avion");
        exige(strstr(buf, " km") != 0, "lleva los kilometros");
        exige(strstr(buf, "AZ ") != 0, "y el rumbo");
        exige(strstr(buf, " dB") == 0, "pero NO se inventa un dB");
        exige(strstr(buf, "min") == 0, "ni un \"hace N min\"");

        /* La trampa del pais. */
        (void)mapa_e4_a_px(&g_caja, -2000L * 100L, 5000L * 100L, &px, &py);
        (void)mapa_eti_toca(px, py);
        (void)mapa_eti_texto(buf, (uint8_t)sizeof buf, 700U);
        printf("     %s\n", buf);
        {
            const char *ua = dxcc_pais("UAL123");

            printf("     (dxcc_pais(\"UAL123\") diria \"%s\")\n",
                   ua ? ua : "nada");
            exige(ua == 0 || strstr(buf, ua) == 0,
                  "a un avion no se le pone el pais de su prefijo");
        }

        /* Y los kilometros tienen que ser los de la esfera: Madrid (IN80,
         * 40,5 N 3,0 O) a 45,0 N 30,0 O son unos 2.200 km. */
        (void)mapa_e4_a_px(&g_caja, -3000L * 100L, 4500L * 100L, &px, &py);
        (void)mapa_eti_toca(px, py);
        (void)mapa_eti_texto(buf, (uint8_t)sizeof buf, 700U);
        {
            const char *q = strstr(buf, " km");
            long km = 0;

            if (q != 0) {
                const char *r = q;
                while (r > buf && r[-1] >= '0' && r[-1] <= '9') { r--; }
                km = atol(r);
            }
            printf("     kilometros al avion: %ld\n", km);
            exige(km > 1900L && km < 2500L,
                  "y la distancia sale de la esfera, no del localizador");
        }

        /*
         * ==============================================================
         * LA OACI Y EL MODELO EN LA FICHA - 19/10/2026
         * ==============================================================
         *
         * Avisado por el usuario: en la vista de mapa, la ficha decia
         * "CCA845 - 864 km - AZ 58" y nada del avion, mientras que la
         * LISTA del mismo modo enseña la OACI y el modelo en sus dos
         * primeras columnas. Los dos datos estaban guardados; lo que no
         * habia era por donde pasarlos a la ficha.
         *
         * Se mide aqui el PEOR caso que puede llegar: el nombre de modelo
         * mas largo que genera tools/icao24.py (23 caracteres) detras de
         * una OACI de seis, con cinco cifras de kilometros y tres de
         * rumbo. Si eso no cabe en el hueco de la ficha, se recorta por
         * la cola y el modelo desaparece a medias.
         */
        {
            mapa_eti_dx_t a2;
            int16_t qx, qy;

            mapa_eti_empieza(&g_caja, "IN80");
            memset(&a2, 0, sizeof a2);
            a2.call[0] = 'C'; a2.call[1] = 'C'; a2.call[2] = 'A';
            a2.call[3] = '8'; a2.call[4] = '4'; a2.call[5] = '5';
            a2.call[6] = '\0';
            a2.hay_pos = 1U; a2.avion = 1U;
            a2.lat_c = 4440L; a2.lon_c = 560L;
            a2.extra = "781F2E A359 Airbus A350-941";
            exige(mapa_eti_pon(&a2) == 1U, "entra un avion con modelo");
            mapa_eti_cierra();

            (void)mapa_e4_a_px(&g_caja, 560L * 100L, 4440L * 100L, &qx, &qy);
            exige(mapa_eti_toca(qx, qy) == 1U, "se toca");
            (void)mapa_eti_texto(buf, (uint8_t)sizeof buf, 700U);
            printf("     %s\n", buf);
            exige(strstr(buf, "CCA845") != 0, "la ficha sigue diciendo el vuelo");
            exige(strstr(buf, "781F2E") != 0, "y ahora tambien la OACI");
            exige(strstr(buf, "Airbus A350-941") != 0, "y el modelo entero");
            exige(strstr(buf, " km") != 0, "sin perder los kilometros");
            exige(strstr(buf, "AZ ") != 0, "ni el rumbo");

            /* Y que el peor caso quepa de ancho. El hueco se mide igual
             * que en el apartado de los indicativos, mas abajo. */
            {
                char peor[96];
                int16_t w;
                int16_t hueco_av = mapa_eti_ficha_hueco_dbg();

                snprintf(peor, sizeof peor,
                         "%s - %s - %u km - AZ %u",
                         "CCA845", "781F2E ABCD Aerospatiale/BAC Concorde",
                         19904U, 359U);
                w = gfx2_text_w(peor, &font_ui_14);
                printf("     el peor de un avion: %s\n", peor);
                printf("     mide %d px de los %d que hay\n",
                       (int)w, (int)hueco_av);
                exige(w <= hueco_av,
                      "el peor modelo cabe en la ficha de un avion");
            }
        }
    }

    /* ---------------------------------------------------------------- */
    /*
     * ==================================================================
     * EL PAIS EN LA FICHA, Y QUE SIGA CABIENDO - 13/10/2026
     * ==================================================================
     *
     * Dos cosas, y la segunda es la que tiene miga:
     *
     *   1) QUE ESTE, Y DONDE DIJO: detras del localizador y DELANTE de
     *      los kilometros.
     *
     *   2) QUE SIGA CABIENDO. gfx2_text_in() recorta por la derecha sin
     *      avisar, asi que un nombre largo no se nota como un fallo: lo
     *      que desaparece es el final de la linea -el "hace N min"- y la
     *      ficha sigue pareciendo correcta. Y el texto acaba de crecer
     *      hasta catorce letras de golpe.
     *
     * Se recorre LA TABLA ENTERA de dxcc.c, en los dos idiomas, con el
     * peor caso de todo lo demas: indicativo largo, localizador de seis,
     * cinco cifras de kilometros, azimut de tres, dB de dos y "x99". Una
     * lista de "los paises mas largos" escrita a mano aqui se quedaria
     * vieja el dia que se anada uno, que es justo la clase de lista que
     * este proyecto no tiene.
     */
    printf("\n  12) el pais, y que la ficha siga cabiendo\n\n");
    {
        int16_t px, py;
        int16_t hueco = mapa_eti_ficha_hueco_dbg();

        mapa_eti_empieza(&g_caja, "IN80");
        (void)mete("F1BHB", "JN09", 9, 700U);
        mapa_eti_cierra();
        (void)mapa_loc_a_px(&g_caja, "JN09", &px, &py);
        (void)mapa_eti_toca(px, py);
        (void)mapa_eti_texto(buf, (uint8_t)sizeof buf, 701U);
        printf("     %s\n", buf);
        {
            const char *pais = dxcc_pais("F1BHB");
            const char *pp = (pais != 0) ? strstr(buf, pais) : 0;
            const char *kk = strstr(buf, " km");

            exige(pais != 0 && pp != 0, "el pais sale en la ficha");
            exige(pp != 0 && kk != 0 && pp < kk,
                  "y va DELANTE de los kilometros, como se pidio");
        }

        /* Un prefijo que la tabla no conoce: hueco, no invento. */
        mapa_eti_empieza(&g_caja, "IN80");
        (void)mete("1A0KM", "JN61", -5, 700U);
        mapa_eti_cierra();
        (void)mapa_loc_a_px(&g_caja, "JN61", &px, &py);
        (void)mapa_eti_toca(px, py);
        (void)mapa_eti_texto(buf, (uint8_t)sizeof buf, 701U);
        if (dxcc_pais("1A0KM") == 0) {
            printf("     prefijo desconocido: %s\n", buf);
            exige(strstr(buf, "1A0KM") != 0 && strstr(buf, " km") != 0,
                  "sin pais conocido la ficha sale igual, sin huecos raros");
        }

        /*
         * Y QUE NO SE SALGA DEL BUFFER, QUE ES OTRA COSA - 16/10/2026.
         *
         * mapa_eti_ficha() le pasa un char buf[80] DE LA PILA. Si
         * mapa_eti_texto() escribe un byte de mas, lo que machaca son las
         * variables de al lado, y eso no se ve: ni se cuelga, ni pinta
         * raro, ni sale en una foto. Un banco que solo mira la cadena que
         * sale tampoco lo ve.
         *
         * Asi que aqui se le pasa un buffer con CENTINELAS detras y se
         * comprueba que siguen enteros. Se probo que se pone rojo: con
         * pon_u32() sin tope -como estaba hasta hoy- este bloque cantaba
         * tres bytes pisados con ZL4AA en RE78, SNR -25, x12 y 600
         * minutos, que es justo lo que AddressSanitizer decia.
         */
        {
            static const struct { const char *call, *loc; int snr, veces;
                                  unsigned min, ahora; } k_duros[] = {
                { "ZL4AA",        "RE78", -25, 12, 100U, 700U },
                { "PJ2/DL1ABC",   "RF80", -19,  9,   1U, 1399U },
                { "VP8/SQ1ABCDE", "RF80", -25, 99,   1U, 1399U },
                { "3Y0J",         "RF80",  -9,  2,   1U, 1399U },
            };
            unsigned c, idi2;
            int pisados = 0;

            for (idi2 = 0U; idi2 < 2U; idi2++) {
                idioma_pon(idi2 ? IDIOMA_EN : IDIOMA_ES);
                for (c = 0U; c < sizeof k_duros / sizeof k_duros[0]; c++) {
                    char caja[80 + 16];
                    unsigned v, j2;

                    memset(caja, 0x5A, sizeof caja);
                    mapa_eti_empieza(&g_caja, "IN80");
                    for (v = 0U; v < (unsigned)k_duros[c].veces; v++) {
                        (void)mete(k_duros[c].call, k_duros[c].loc,
                                   (int16_t)k_duros[c].snr,
                                   (uint16_t)(k_duros[c].min + v));
                    }
                    mapa_eti_cierra();
                    (void)mapa_loc_a_px(&g_caja, k_duros[c].loc, &px, &py);
                    (void)mapa_eti_toca(px, py);
                    (void)mapa_eti_texto(caja, 80U,
                                         (uint16_t)k_duros[c].ahora);
                    for (j2 = 80U; j2 < sizeof caja; j2++) {
                        if (caja[j2] != 0x5A) { pisados++; }
                    }
                }
            }
            idioma_pon(IDIOMA_ES);
            exige(pisados == 0,
                  "la ficha no escribe ni un byte detras de su buffer");
            if (pisados != 0) {
                printf("       %d bytes pisados detras de los 80\n", pisados);
            }
        }

        /* Y EL PEOR CASO, pais a pais y en los dos idiomas. */
        {
            unsigned k, n = dxcc_cuantos();
            int16_t peor = 0;
            const char *peor_txt = "";
            static char peor_buf[80];
            int idi;

            printf("     hueco de la ficha: %d px, %u prefijos en la tabla\n",
                   (int)hueco, n);
            for (idi = 0; idi < 2; idi++) {
                idioma_pon(idi ? IDIOMA_EN : IDIOMA_ES);
                for (k = 0; k < n; k++) {
                    const char *pre = dxcc_prefijo(k);
                    char call[16];
                    unsigned j = 0U;
                    int16_t w;

                    if (pre == 0) { continue; }
                    while (pre[j] != '\0' && j < 6U) { call[j] = pre[j]; j++; }
                    /* Un indicativo largo de verdad detras del prefijo. */
                    call[j++] = '9'; call[j++] = 'W'; call[j++] = 'W';
                    call[j++] = 'W'; call[j] = '\0';

                    mapa_eti_empieza(&g_caja, "IN80");
                    /* RF80 desde Madrid son casi veinte mil kilometros:
                     * cinco cifras, que es el peor caso del numero. */
                    (void)mete(call, "RF80", -19, 700U);
                    (void)mete(call, "RF80", -19, 699U);  /* para el "x2" */
                    mapa_eti_cierra();
                    (void)mapa_loc_a_px(&g_caja, "RF80", &px, &py);
                    (void)mapa_eti_toca(px, py);
                    (void)mapa_eti_texto(buf, (uint8_t)sizeof buf, 1399U);
                    w = gfx2_text_w(buf, &font_ui_14);
                    if (w > peor) {
                        peor = w;
                        peor_txt = dxcc_pais(call);
                        snprintf(peor_buf, sizeof peor_buf, "%s", buf);
                    }
                }
            }
            idioma_pon(IDIOMA_ES);
            printf("     el peor: %s\n", peor_buf);
            printf("     mide %d px de los %d que hay\n", (int)peor, (int)hueco);
            (void)peor_txt;
            exige(hueco > 0 && peor <= hueco,
                  "el nombre mas largo de la tabla cabe en la ficha");
        }
    }

    /* ---------------------------------------------------------------- */
    /*
     * Y UNA FOTO PARA MIRARLA, con el mapa de verdad debajo. Un banco
     * puede decir que el rotulo esta donde dice que esta y aun asi
     * quedar feo; eso no lo mide un numero.
     */
    {
        extern void pinta_con_mapa(gfx2_surf_t *s, void *ctx);
        int16_t px, py;

        mapa_eti_empieza(&g_caja, "IN80");
        (void)mete("JA5AFW", "PM64", -8, 700U);
        (void)mete("K1ABC",  "FN42", -3, 700U);
        (void)mete("LU1XX",  "FF95", -15, 699U);
        (void)mete("VK6ZZ",  "OF88", -19, 699U);
        (void)mete("ZL2AB",  "RF80", -21, 698U);
        (void)mete("KL7AA",  "BP51", -11, 698U);
        mapa_eti_cierra();
        (void)mapa_loc_a_px(&g_caja, "PM64", &px, &py);
        (void)mapa_eti_toca(px, py);

        lcd_sim_reset(0x0000);
        gfx2_render(HUECO_X, HUECO_Y, HUECO_W, HUECO_H, pinta_con_mapa, 0);
        lcd_sim_write_ppm("out/mapa_indicativos.ppm");
        printf("\n  pintado en out/mapa_indicativos.ppm\n");
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
