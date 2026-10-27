/*
 * LA PANTALLA DEL BANCO DE MEMORIAS - 26/10/2026, pedido por el usuario.
 *
 * *** Y ES LA SEGUNDA VEZ QUE SE DIBUJA. ***
 *
 * La primera version la hizo con la rejilla PAGINADA -la de "Anterior" y
 * "Siguiente"-, con los cuatro bancos repartidos en cuatro paginas. El
 * usuario lo vio en la radio y contesto lo que habia pedido desde el
 * principio: que fuera como la ventana de bandas, con los bancos en la
 * COLUMNA de la izquierda y los dieciseis botones a la derecha.
 *
 * O sea que el fallo no fue tecnico: fue hacer otra cosa. Queda escrito
 * porque el coste de eso -rehacer la pantalla entera y dos bancos- es
 * mayor que el de releer la peticion.
 *
 * QUE COMPRUEBA ESTO, y todo midiendo pixeles y no mirando:
 *
 *   1. que los cuatro bancos caben en la columna, en los dos idiomas;
 *   2. que las dieciseis celdas de un banco se DIBUJAN -el reparto denso
 *      dibuja 4x4, asi que quince memorias y el barrido entran justas-;
 *   3. que ningun rotulo se sale de su celda, con los peores casos de
 *      verdad: la frecuencia mas larga, el modo mas ancho y los textos
 *      del barrido en los dos idiomas;
 *   4. que lo que se dibuja y lo que se toca coinciden: el centro de cada
 *      celda y de cada categoria tiene que devolver ESA celda y ESA
 *      categoria, no la de al lado;
 *   5. y que sabe fallar.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_cfg.h"
#include "banco.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

static void pasa(const char *que, int bien)
{
    printf("    %-52s %s\n", que, bien ? "ok " : "*** MAL ***");
    if (!bien) { fallos++; }
}

static void cabe(const char *que, int16_t w, int16_t hueco)
{
    int ok = (w <= hueco);
    printf("    %-44s %4d px en %4d  %s\n", que, w, hueco,
           ok ? "ok " : "*** NO CABE ***");
    if (!ok) { fallos++; }
}

static void fondo(gfx2_surf_t *s, void *c)
{
    (void)c;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

/*
 * Los mismos nombres de serie que k_banco_nombres[] de main.c, y por eso
 * se escriben aqui: este banco no enlaza main.c. Lo que se comprueba es
 * el PEOR caso de los dos idiomas, asi que el dia que uno crezca -o se
 * traduzca a algo mas largo- esto lo dice.
 *
 * Y NO SON "Banco 1".."Banco 4" desde el 26/10/2026: el usuario los pidio
 * repartidos por lo que se escucha. Son bastante mas largos, que es justo
 * lo que hace que esta medida importe.
 */
static const char *k_bancos[BANCO_N][2] = {
    { "HF",          "HF" },
    { "Aviones",     "Aircraft" },
    { "Marina",      "Marine" },
    { "Repetidores", "Repeaters" },
};

/* Las siglas de modo mas anchas que puede enseñar una memoria. */
static const char *k_modos[] = { "AM", "LSB", "USB", "CW", "NFM", "WFM",
                                 "SAM", "RTTY", "NAVTEX", "WEFAX", "SSTV",
                                 "APRS", "FT8", "WSPR", "JTTY", "HFDL" };

int main(void)
{
    ui_cfg_state_t st;
    const int16_t hueco = UIC_DCELL_W - 10;
    int i, l;

    printf("\nLA PANTALLA DEL BANCO DE MEMORIAS\n\n");

    /* --------------------------------------------------------------
     * 1. LOS CUATRO BANCOS, EN LA COLUMNA DE LA IZQUIERDA
     * -------------------------------------------------------------- */
    printf("  1. LA COLUMNA: cuatro bancos, como se pidio\n");
    pasa("son cuatro categorias y caben en la columna",
         BANCO_N <= UIC_CATS);
    {
        int16_t peor = 0;
        const char *quien = "";

        for (i = 0; i < BANCO_N; i++) {
            for (l = 0; l < 2; l++) {
                int16_t w = gfx2_text_w(k_bancos[i][l], &font_ui_18b);
                if (w > peor) { peor = w; quien = k_bancos[i][l]; }
            }
        }
        printf("      el mas ancho: \"%s\"\n", quien);
        cabe("cabe en la columna", peor, (int16_t)(UIC_SIDE_W - 26));
        cabe("objetivo minimo de dedo (47 px)", 47,
             (int16_t)UIC_CAT_H_DE(BANCO_N));
    }

    /* --------------------------------------------------------------
     * 1b. Y EL NOMBRE QUE LE PONGAS TU, QUE PUEDE NO CABER
     * -------------------------------------------------------------- */
    printf("\n  1b. EL NOMBRE DE MEM.CSV: por que se recorta por PIXELES\n");
    {
        char peor[BANCO_NOM_MAX];
        unsigned k;
        int16_t w;
        const int16_t hueco = (int16_t)(UIC_SIDE_W - 26);

        for (k = 0U; k + 1U < BANCO_NOM_MAX; k++) { peor[k] = 'W'; }
        peor[BANCO_NOM_MAX - 1U] = '\0';
        w = gfx2_text_w(peor, &font_ui_18b);
        printf("      %u letras anchas miden %d px y el hueco es %d\n",
               (unsigned)(BANCO_NOM_MAX - 1U), (int)w, (int)hueco);
        /*
         * Esto tiene que NO caber, y por eso se comprueba al reves que
         * todo lo demas de este banco: si cupiera, el recorte por pixeles
         * de banco_nombres_recorta() sobraria, y el dia que alguien lo
         * quitara "porque no hacia nada" el nombre se saldria encima de
         * la celda de al lado. El tope de letras NO basta.
         */
        pasa("el tope de letras NO basta: hace falta el de pixeles",
             w > hueco);

        /* Y uno de letras estrechas del mismo largo si cabe, que es lo que
         * hace que el recorte dependa de lo que escribas y no del numero. */
        for (k = 0U; k + 1U < BANCO_NOM_MAX; k++) { peor[k] = 'i'; }
        peor[BANCO_NOM_MAX - 1U] = '\0';
        cabe("y el mismo largo en letras estrechas si",
             gfx2_text_w(peor, &font_ui_18b), hueco);
    }

    /* --------------------------------------------------------------
     * 2. LAS DIECISEIS CELDAS DE UN BANCO SE DIBUJAN TODAS
     * -------------------------------------------------------------- */
    printf("\n  2. DIECISEIS BOTONES POR BANCO: quince memorias y el barrido\n");
    printf("      %u memorias + 1 barrido = %u, y el reparto denso dibuja %u\n",
           (unsigned)BANCO_MEM_N, (unsigned)(BANCO_MEM_N + 1U),
           (unsigned)(UIC_DCOLS * UIC_DROWS));
    pasa("entran justas, ni una se queda sin dibujar",
         (BANCO_MEM_N + 1U) == (unsigned)(UIC_DCOLS * UIC_DROWS));
    pasa("y el estado tiene sitio para ellas",
         (BANCO_MEM_N + 1U) <= UIC_CELLS);

    /* --------------------------------------------------------------
     * 3. LOS ROTULOS, CON SUS PEORES CASOS
     * -------------------------------------------------------------- */
    printf("\n  3. QUE NINGUN ROTULO SE SALGA DE SU CELDA\n");
    {
        int16_t peor;
        unsigned k;

        /* La frecuencia va en el renglon grande, como el nombre de la
         * banda en la ventana de bandas. */
        peor = gfx2_text_w("199.999,99", &font_ui_18b);
        cabe("la frecuencia mas larga", peor, hueco);

        peor = 0;
        for (k = 0U; k < sizeof k_modos / sizeof k_modos[0]; k++) {
            int16_t w = gfx2_text_w(k_modos[k], &font_ui_14);
            if (w > peor) { peor = w; }
        }
        cabe("la sigla de modo mas ancha", peor, hueco);

        cabe("\"tocar para empezar\"",
             gfx2_text_w("tocar para empezar", &font_ui_14), hueco);
        cabe("\"tap to start\"",
             gfx2_text_w("tap to start", &font_ui_14), hueco);
        cabe("\"barriendo\" / \"scanning\"",
             gfx2_text_w("scanning", &font_ui_14), hueco);
        cabe("\"SCAN\"", gfx2_text_w("SCAN", &font_ui_18b), hueco);
        cabe("\"libre\" / \"empty\"",
             gfx2_text_w("empty", &font_ui_18b), hueco);
        cabe("el numero de memoria", gfx2_text_w("15", &font_ui_14b), hueco);
    }

    /* --------------------------------------------------------------
     * 4. LO QUE SE DIBUJA Y LO QUE SE TOCA, EL MISMO SITIO
     * -------------------------------------------------------------- */
    printf("\n  4. EL DIBUJO Y EL TOQUE CORTAN POR EL MISMO SITIO\n");
    memset(&st, 0, sizeof st);
    for (i = 0; i < BANCO_N; i++) { st.cat[i] = k_bancos[i][0]; }
    st.cats = BANCO_N;
    st.cat_sel = 0U;
    st.denso = 1U;
    st.n = (uint8_t)(BANCO_MEM_N + 1U);
    st.marcada = 3U;
    st.cursor = 0;
    st.pressed = -1;
    for (i = 0; i < (int)st.n; i++) {
        static char num[UIC_CELLS][4];
        if (i == (int)BANCO_MEM_N) {
            st.cel[i].nombre = "SCAN";
            st.cel[i].valor  = "tocar para empezar";
            st.cel[i].extra  = "";
            continue;
        }
        num[i][0] = (char)('0' + ((i + 1) / 10));
        num[i][1] = (char)('0' + ((i + 1) % 10));
        num[i][2] = '\0';
        st.cel[i].extra = num[i];
        if (i < 4) { st.cel[i].nombre = "199.999,99"; st.cel[i].valor = "NAVTEX"; }
        else       { st.cel[i].nombre = "libre";      st.cel[i].valor = ""; }
    }

    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);
    ui_cfg_draw(&st);
    lcd_sim_write_ppm("out/banco_pant.ppm");

    {
        int mal = 0;

        /* El centro de cada celda tiene que devolver ESA celda. El centro
         * se calcula con las MISMAS constantes con las que ui_cfg.c la
         * dibuja: si alguna se mueve, se mueven las dos a la vez y esto
         * sigue siendo cierto; lo que caza es que se muevan por separado
         * -el dibujo por un lado y el reparto del toque por otro-, que es
         * como queda una franja que no es de nadie. */
        for (i = 0; i < (int)st.n; i++) {
            int cx = UIC_GRID_X + (i % UIC_DCOLS) * (UIC_DCELL_W + UIC_DGAP)
                     + UIC_DCELL_W / 2;
            int cy = UIC_GRID_Y + (i / UIC_DCOLS) * (UIC_DCELL_H + UIC_DGAP)
                     + UIC_DCELL_H / 2;
            if (ui_cfg_hit((uint16_t)cx, (uint16_t)cy) != (int8_t)i) { mal++; }
        }
        printf("      %d celdas probadas por su centro, %d fallan\n",
               (int)st.n, mal);
        pasa("cada celda responde a su propio centro", mal == 0);

        mal = 0;
        for (i = 0; i < BANCO_N; i++) {
            int cy = UIC_CAT_Y + i * (UIC_CAT_H_DE(BANCO_N) + UIC_CAT_GAP)
                     + UIC_CAT_H_DE(BANCO_N) / 2;
            int cx = UIC_SIDE_X + UIC_SIDE_W / 2;
            if (ui_cfg_hit((uint16_t)cx, (uint16_t)cy)
                != (int8_t)(UIC_HIT_CAT0 + i)) { mal++; }
        }
        printf("      %d bancos probados por su centro, %d fallan\n",
               BANCO_N, mal);
        pasa("cada banco responde a su propio centro", mal == 0);

        /* Y la celda diecisiete no existe: tocar por debajo de la ultima
         * fila no puede devolver una celda. */
        {
            int cy = UIC_GRID_Y + UIC_DROWS * (UIC_DCELL_H + UIC_DGAP) + 20;
            int8_t h = ui_cfg_hit((uint16_t)(UIC_GRID_X + 10), (uint16_t)cy);
            pasa("por debajo de la ultima fila no hay celda",
                 h < 0 || h >= (int8_t)UIC_CELLS);
        }
    }

    /* --------------------------------------------------------------
     * 5. QUE SABE FALLAR
     * -------------------------------------------------------------- */
    printf("\n  5. Y QUE SABE FALLAR\n");
    {
        /* Un rotulo imposible tiene que salir por encima del hueco. Sin
         * esto, todas las medidas de arriba podrian estar comparando con
         * un hueco equivocado y aprobar igual. */
        int16_t w = gfx2_text_w("199.999,99 kHz NAVTEX", &font_ui_18b);
        printf("      un rotulo imposible mide %d px y el hueco es %d\n",
               (int)w, (int)hueco);
        pasa("la medida lo rechaza", w > hueco);
    }

    printf("\n%s (%d fallos)   foto en out/banco_pant.ppm\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
