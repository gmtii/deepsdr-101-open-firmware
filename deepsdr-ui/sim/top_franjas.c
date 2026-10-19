/*
 * LA CABECERA: QUE UN REPINTADO PARCIAL NO BORRE A OTRO - 28/09/2026.
 *
 * *** QUINTA VEZ. En SAM, el error de PLL no se veia NUNCA: lo borraba
 * ui_top_draw_rds(), que limpia la franja de la marquesina -una zona FIJA-
 * y el numero cae dentro. ***
 *
 * Las cuatro anteriores de esta misma familia fueron en el panel digital -el
 * tercer boton con la barra, el cuarto con la chapa, y el del mapa con
 * solo_chip(), esa tres veces seguidas-. sim/digi.c las vigila desde
 * entonces. La cabecera no tenia nada equivalente, y por eso la quinta tardo
 * dos meses en salir: no la caza leer el codigo, porque las dos funciones
 * son correctas por separado.
 *
 * COMO SE COMPRUEBA, y es la unica forma que no depende de que yo me acuerde
 * de mirar un elemento concreto: se pinta la cabecera ENTERA, se llama a un
 * repintado parcial con EL MISMO estado, y se cuenta cuantos pixeles han
 * cambiado. Un repintado parcial correcto es idempotente: tiene que dejar la
 * pantalla exactamente igual. Si borra algo y no lo repone, los pixeles de
 * ese algo cambian y aqui se ven.
 *
 * Asi no hay que enumerar las victimas. El dia que alguien meta un elemento
 * nuevo dentro de una franja, esto lo caza sin tocar el banco.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_top.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

static void fondo(gfx2_surf_t *s, void *c)
{
    (void)c;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

/* Una copia del marco, para comparar antes y despues. */
static uint16_t s_antes[LCD_W * LCD_H];

static void guarda(void)
{
    memcpy(s_antes, lcd_sim_fb(), sizeof s_antes);
}

static int cambiados(void)
{
    const uint16_t *fb = lcd_sim_fb();
    int n = 0;
    size_t i;

    for (i = 0U; i < (size_t)LCD_W * (size_t)LCD_H; i++) {
        if (fb[i] != s_antes[i]) { n++; }
    }
    return n;
}

static void mira(const char *que, const ui_top_state_t *st,
                 void (*parcial)(const ui_top_state_t *))
{
    int n;

    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);
    ui_top_draw(st);
    guarda();
    parcial(st);
    n = cambiados();
    printf("    %-46s %6d px   %s\n", que, n, (n == 0) ? "ok " : "*** MAL ***");
    if (n != 0) { fallos++; }
}

int main(void)
{
    ui_top_state_t st;

    printf("\nLA CABECERA: LOS REPINTADOS PARCIALES NO BORRAN NADA\n");

    memset(&st, 0, sizeof st);
    st.freq       = "14.090,00";
    st.freq_digit = -1;
    st.mode       = "SAM";
    st.band       = "20 m";
    st.clock      = "17:31";
    st.s_units    = 7U;
    st.agc        = "FST";
    st.bw         = "0-1,8k";
    st.knob_name  = "Sintonía";
    st.knob_value = "1 kHz";

    st.batt_pct   = 80U;

    /*
     * PRIMERO LA PREGUNTA DIRECTA: ¿SE VE?
     *
     * *** Y esto lo aprendi montando el banco. La primera version solo
     * comprobaba IDEMPOTENCIA -pintar entero, repintar la franja, contar
     * pixeles cambiados- y contra el ui_top.c averiado daba CERO y decia
     * "ok". Claro: ui_top_draw() pinta el PPM y DESPUES llama a
     * ui_top_draw_rds(), asi que para cuando llega el repintado parcial el
     * numero ya estaba borrado. Repintar encima de un hueco es
     * perfectamente idempotente. ***
     *
     * La idempotencia caza que un repintado parcial ROMPA lo que hay. No
     * caza que el dibujado COMPLETO ya viniera roto. Son dos preguntas y
     * hacen falta las dos.
     *
     * Esta es la directa: se pinta la cabecera entera con el dato y sin el
     * dato, y tienen que salir pantallas distintas. Si no, el dato no se ve
     * y da igual lo demas. La huella del PPM son unos 300 px; se exige mas
     * de 100 para no depender del texto exacto.
     */
    printf("\n  lo primero: ¿se VE el error de PLL de SAM?\n");
    {
        ui_top_state_t con = st, sin = st;
        int n;

        con.sam_ppm = "-12,3 ppm";
        sin.sam_ppm = 0;

        lcd_sim_reset(0x0000);
        gfx2_render_screen(fondo, 0);
        ui_top_draw(&sin);
        guarda();
        lcd_sim_reset(0x0000);
        gfx2_render_screen(fondo, 0);
        ui_top_draw(&con);
        n = cambiados();
        printf("    %-46s %6d px   %s\n",
               "el dibujado completo lo ENSEÑA", n,
               (n > 100) ? "ok " : "*** MAL ***");
        if (n <= 100) {
            printf("      (con %d px no se ve: algo lo esta borrando dentro\n"
                   "       del propio ui_top_draw)\n", n);
            fallos++;
        }
    }

    /*
     * EL CASO DE LA QUINTA VEZ. En SAM se enseña el error de PLL, y en SAM
     * tambien corre el repintado de la franja de la marquesina -lo llaman
     * emis_marq_poll() y emisoras_tick() en cualquier modo que no sea WFM-.
     */
    printf("\n  SAM, con el error de PLL puesto\n");
    st.sam_ppm = "-12,3 ppm";
    mira("repintar la franja de la marquesina", &st, ui_top_draw_rds);

    /*
     * Y con RDS de verdad encima, que es cuando la franja lleva contenido y
     * no solo el degradado.
     */
    printf("\n  con nombre y marquesina de RDS\n");
    st.rds_nombre = "RADIO 3";
    st.rds        = "AHORA SUENA ALGO MUY LARGO QUE SE MUEVE";
    mira("repintar la franja de la marquesina", &st, ui_top_draw_rds);

    /* Sin PPM: la franja tiene que seguir siendo idempotente igual. */
    printf("\n  sin error de PLL (los demas modos)\n");
    st.sam_ppm = 0;
    mira("repintar la franja de la marquesina", &st, ui_top_draw_rds);

    /*
     * QUE ESTE BANCO SABE FALLAR. Si `mira()` comparase mal -o si
     * ui_top_draw_rds() no pintara nada- todo saldria "ok" sin probar nada.
     * Aqui se le da un estado DISTINTO al repintado parcial: tiene que
     * cambiar pixeles. Sin esto, un banco que siempre dice 0 aprueba igual.
     */
    printf("\n  y que sabe fallar\n");
    {
        ui_top_state_t otro = st;
        int n;

        otro.rds_nombre = "OTRA COSA";
        lcd_sim_reset(0x0000);
        gfx2_render_screen(fondo, 0);
        ui_top_draw(&st);
        guarda();
        ui_top_draw_rds(&otro);          /* estado distinto: DEBE cambiar */
        n = cambiados();
        printf("    %-46s %6d px   %s\n",
               "con otro texto SI cambia la pantalla", n,
               (n > 0) ? "ok " : "*** MAL ***");
        if (n <= 0) { fallos++; }
    }

    /*
     * Y EL REPINTADO DEL S-METRO, que es el otro parcial de esta franja
     * desde el 30/09/2026.
     *
     * Pedido por el usuario.
     * Con señal la barra de S no para quieta, y repintar los 800 px de la
     * franja entera en cada fotograma costaba 32.051 accesos al bus. Ahora
     * se repinta solo la parte que se mueve. Lo que hay que comprobar es lo
     * de siempre en esta familia: que no borra nada de lo que deja fuera.
     */
    printf("\n  el S-metro, que se repinta solo en cada fotograma con señal\n");
    st.rds = 0; st.rds_nombre = 0; st.sam_ppm = "+1,4 PPM";
    mira("repintar solo el S-metro", &st, ui_top_draw_smeter);
    st.s_units = 12U; st.over_db = 60U;   /* S9+60: la lectura mas ancha */
    mira("repintar solo el S-metro, con S9+60", &st, ui_top_draw_smeter);
    st.s_units = 7U; st.over_db = 0U;
    st.dbm = -127; st.dbm_valid = 1U;
    mira("repintar solo el S-metro, con dBm", &st, ui_top_draw_smeter);
    st.dbm_valid = 0U;                    /* sin dBm: el hueco se queda vacio */
    mira("repintar solo el S-metro, sin lectura en dBm", &st, ui_top_draw_smeter);
    /*
     * Y CON LA SOBRECARGA, QUE DESDE EL 19/10/2026 VIVE EN LA PASTILLA DEL
     * MANDO - o sea, FUERA de donde acababa esta ventana hasta hoy.
     *
     * Es justo la razon por la que ST_MOVIL_W crecio: el aviso depende de
     * la señal igual que la barra de S, asi que si la ventana del repintado
     * parcial no lo alcanza, aparece y desaparece con retraso o se queda
     * pegado. Esta fila lo comprueba de la unica forma que vale: pintando
     * la franja entera y la ventana recortada y comparando pixel a pixel.
     */
    st.ovr = 1U;
    mira("repintar solo el S-metro, con SOBRECARGA", &st, ui_top_draw_smeter);
    st.dbm_valid = 1U;
    mira("repintar solo el S-metro, sobrecarga y dBm", &st, ui_top_draw_smeter);
    st.ovr = 0U; st.dbm_valid = 1U;

    /*
     * EL PPM DEL SAM, QUE ES EL TERCER PARCIAL - 04/10/2026.
     *
     * Pedido por el usuario.
     *
     * Mismo cuento que el S-metro, un mes despues y en la otra franja: el
     * error del PLL son DECIMAS y cambian casi en cada fotograma, y por cada
     * cambio se repintaba la cabecera entera mas la franja de estado entera
     * -83.200 accesos al bus-. Ahora solo su ventana.
     */
    printf("\n  el PPM del SAM, que tampoco para quieto\n");
    st.rds = 0; st.rds_nombre = 0;
    st.sam_ppm = "+1,4 PPM";
    mira("repintar solo el hueco del PPM", &st, ui_top_draw_sam);
    st.sam_ppm = "-199,9 PPM";            /* la lectura mas ancha */
    mira("repintar solo el hueco del PPM, con -199,9", &st, ui_top_draw_sam);
    st.sam_ppm = 0;                       /* y fuera de SAM, que no ensucie */
    mira("repintar ese hueco sin PPM (fuera de SAM)", &st, ui_top_draw_sam);
    st.sam_ppm = "+1,4 PPM";

    /*
     * Y EL DE LA FRECUENCIA, QUE ES EL CUARTO PARCIAL - 05/10/2026.
     *
     * Y el que mas corre de los cuatro: uno por CLIC DEL MANDO. Antes
     * llamaba a ui_top_draw(), o sea cabecera entera mas marquesina mas
     * franja de estado entera. Ahora solo desde el borde izquierdo hasta
     * donde empieza la marquesina.
     *
     * Lo de siempre en esta familia: que no borre nada de lo que deja
     * fuera. Con marquesina puesta y sin ella, porque el limite de la
     * ventana SALE de donde empieza la marquesina.
     */
    printf("\n  el repintado de la frecuencia, uno por clic del mando\n");
    st.sam_ppm = "+1,4 PPM";
    st.rds_nombre = "RADIO EUROPA";
    st.rds        = "HOL - NL - 00:00-24:00";
    mira("repintar solo el hueco de la frecuencia", &st, ui_top_draw_freq);
    st.freq = "7.265,00"; st.freq_digit = 4;
    mira("con otra frecuencia mas corta", &st, ui_top_draw_freq);
    st.band = "41 m";
    mira("y con otra banda", &st, ui_top_draw_freq);
    st.rds = 0; st.rds_nombre = 0;
    mira("y sin marquesina", &st, ui_top_draw_freq);
    st.freq = "14.090,00"; st.freq_digit = -1; st.band = "20 m";

    /*
     * Y QUE LA LECTURA DEL PPM CABE EN SU HUECO. Va alineada a la DERECHA,
     * asi que lo que se sale lo hace por la izquierda - y por la izquierda
     * esta la marquesina. Si un dia la lectura creciera -mas cifras, otra
     * fuente, otro idioma- se metaria encima de ella otra vez.
     *
     * Las medidas van contra UI_TOP_SAM_W, no contra un numero escrito aqui:
     * un banco que lleve su propia copia de la geometria aprueba la copia.
     */
    printf("\n  y que el PPM cabe en el suyo\n");
    {
        const char *peor[] = { "-199,9 PPM", "+123,4 PPM", "-9999,9 PPM",
                               "buscando", "searching" };
        int16_t may = 0;
        unsigned i;

        for (i = 0U; i < sizeof peor / sizeof peor[0]; i++) {
            int16_t w = gfx2_text_w(peor[i], &font_ui_14);
            if (w > may) { may = w; }
        }
        printf("    %-46s %6d px   %s\n",
               "el texto mas ancho cabe en UI_TOP_SAM_W",
               (int)may, (may <= UI_TOP_SAM_W) ? "ok " : "*** MAL ***");
        printf("      hueco: %d px\n", (int)UI_TOP_SAM_W);
        if (may > UI_TOP_SAM_W) { fallos++; }
    }

    /*
     * ===================================================================
     * QUE EL PPM Y LA MARQUESINA NO COMPARTAN NI UN PIXEL - 05/10/2026.
     * ===================================================================
     *
     * Pedido por el usuario.
     *
     * Y la foto era cierto: "+23,7 PPM" escrito encima de "HOL - NL -
     * 00:00-24:00". Lo que este banco NO comprobaba es justo eso. Vigilaba
     * que un repintado parcial no BORRARA a otro -la quinta vez de esa
     * familia- y que el PPM se VIERA. Pero "se ve" se cumple igual cuando
     * se ve ENCIMA de otra cosa: los dos textos se pintan, gana el ultimo,
     * y el contador de pixeles dice que hay dato.
     *
     * Asi que falta la tercera pregunta, y es esta: cada cosa, ¿esta en SU
     * sitio? Se mide sin enumerar victimas, igual que la idempotencia:
     *
     *   - se pinta la cabecera con SOLO la marquesina y se apunta que
     *     pixeles cambian respecto al fondo: esa es su huella;
     *   - se pinta con SOLO el PPM y se apunta la suya;
     *   - las dos huellas no pueden tener ni un pixel en comun.
     *
     * El dia que alguien mueva cualquiera de los dos encima del otro, esto
     * lo dice sin que nadie se acuerde de mirar la cabecera.
     */
    printf("\n  cada cosa en su sitio: huellas que no se tocan\n");
    {
        static uint8_t hm[LCD_W * LCD_H];    /* huella de la marquesina */
        static uint8_t hp[LCD_W * LCD_H];    /* huella del PPM */
        static uint16_t limpio[LCD_W * LCD_H];
        ui_top_state_t solo;
        size_t i;
        int comunes = 0, nm = 0, np = 0;
        const uint16_t *fb;

        /* el fondo de referencia: la cabecera sin ninguna de las dos */
        solo = st;
        solo.rds = 0; solo.rds_nombre = 0; solo.sam_ppm = 0;
        lcd_sim_reset(0x0000);
        gfx2_render_screen(fondo, 0);
        ui_top_draw(&solo);
        memcpy(limpio, lcd_sim_fb(), sizeof limpio);

        /* solo la marquesina */
        solo.rds_nombre = "RADIO EUROPA";
        solo.rds        = "HOL - NL - 00:00-24:00";
        lcd_sim_reset(0x0000);
        gfx2_render_screen(fondo, 0);
        ui_top_draw(&solo);
        fb = lcd_sim_fb();
        for (i = 0U; i < (size_t)LCD_W * (size_t)LCD_H; i++) {
            hm[i] = (uint8_t)(fb[i] != limpio[i]);
            nm += hm[i];
        }

        /* solo el PPM */
        solo.rds = 0; solo.rds_nombre = 0;
        solo.sam_ppm = "+123,4 PPM";
        lcd_sim_reset(0x0000);
        gfx2_render_screen(fondo, 0);
        ui_top_draw(&solo);
        fb = lcd_sim_fb();
        for (i = 0U; i < (size_t)LCD_W * (size_t)LCD_H; i++) {
            hp[i] = (uint8_t)(fb[i] != limpio[i]);
            np += hp[i];
            if (hp[i] && hm[i]) { comunes++; }
        }

        printf("      marquesina %d px, PPM %d px\n", nm, np);
        printf("    %-46s %6d px   %s\n",
               "pixeles que se disputan los dos", comunes,
               (comunes == 0) ? "ok " : "*** MAL ***");
        if (comunes != 0) { fallos++; }

        /* Y que las dos tienen huella: si una saliera vacia, "no se tocan"
         * se cumpliria sola y esto no estaria comprobando nada. */
        printf("    %-46s %6d px   %s\n",
               "las dos se ven de verdad", (nm < np) ? nm : np,
               (nm > 100 && np > 100) ? "ok " : "*** MAL ***");
        if (nm <= 100 || np <= 100) { fallos++; }
    }

    /*
     * Y LO MISMO CON EL RELOJ, que es el otro vecino del hueco nuevo. El
     * PPM se mudo a la derecha de la marquesina, o sea justo a la izquierda
     * del reloj: si un dia el reloj creciera -segundos, otra fuente- se
     * encontrarian. Ademas del dibujo, se comprueba la zona de TOQUE: el
     * reloj es tocable y el PPM no, asi que el PPM no puede meterse dentro
     * de lo que el reloj considera suyo.
     */
    printf("\n  el PPM y el reloj tampoco\n");
    {
        static uint8_t hr[LCD_W * LCD_H];
        static uint16_t limpio[LCD_W * LCD_H];
        ui_top_state_t solo;
        size_t i;
        int comunes = 0, nr = 0, np = 0;
        const uint16_t *fb;
        int toca;

        solo = st;
        solo.rds = 0; solo.rds_nombre = 0; solo.sam_ppm = 0; solo.clock = "";
        lcd_sim_reset(0x0000);
        gfx2_render_screen(fondo, 0);
        ui_top_draw(&solo);
        memcpy(limpio, lcd_sim_fb(), sizeof limpio);

        solo.clock = "17:31";
        lcd_sim_reset(0x0000);
        gfx2_render_screen(fondo, 0);
        ui_top_draw(&solo);
        fb = lcd_sim_fb();
        for (i = 0U; i < (size_t)LCD_W * (size_t)LCD_H; i++) {
            hr[i] = (uint8_t)(fb[i] != limpio[i]);
            nr += hr[i];
        }

        solo.clock = "";
        solo.sam_ppm = "+123,4 PPM";
        lcd_sim_reset(0x0000);
        gfx2_render_screen(fondo, 0);
        ui_top_draw(&solo);
        fb = lcd_sim_fb();
        for (i = 0U; i < (size_t)LCD_W * (size_t)LCD_H; i++) {
            if ((fb[i] != limpio[i])) { np++; if (hr[i]) { comunes++; } }
        }

        printf("      reloj %d px, PPM %d px\n", nr, np);
        printf("    %-46s %6d px   %s\n",
               "pixeles que se disputan los dos", comunes,
               (comunes == 0) ? "ok " : "*** MAL ***");
        if (comunes != 0) { fallos++; }

        /* La zona de toque del reloj, con el reloj ya dibujado. */
        solo.clock = "17:31";
        lcd_sim_reset(0x0000);
        gfx2_render_screen(fondo, 0);
        ui_top_draw(&solo);
        toca = ui_top_clock_hit((uint16_t)(UI_TOP_SAM_X + UI_TOP_SAM_W - 1),
                                (uint16_t)(UI_TOP_SAM_Y + 4));
        printf("    %-46s %6s   %s\n",
               "el borde derecho del PPM no es toque de reloj",
               toca ? "si" : "no", (!toca) ? "ok " : "*** MAL ***");
        if (toca) { fallos++; }
    }

    /*
     * Y QUE LA LECTURA CABE EN LA VENTANA RECORTADA, con numeros y no
     * mirando: si el texto se pasa, la parte de fuera no se repinta y queda
     * el resto de la vez anterior. "S9+60" mas "AGC activo" acaba justo en
     * donde empieza el destino del mando, o sea con CERO margen; de ahi los
     * seis pixeles de aire de ST_MOVIL_W. Esto los vigila.
     */
    printf("\n  y que la lectura cabe en esa ventana\n");
    {
        int16_t x2 = (int16_t)(ui_top_read_x_dbg()
                               + gfx2_text_w("S9+60", &font_ui_18b) + 12);
        struct { const char *t; const gfx2_font_t *f; } peor[] = {
            { "AGC activo", &font_ui_14 }, { "AGC active", &font_ui_14 },
            { "dBm",        &font_ui_14 },
        };
        int16_t fin = 0, i;
        int16_t num = (int16_t)(x2 + gfx2_text_w("-127", &font_ui_14b) + 3
                                + gfx2_text_w("dBm", &font_ui_14));

        for (i = 0; i < (int16_t)(sizeof peor / sizeof peor[0]); i++) {
            int16_t e = (int16_t)(x2 + gfx2_text_w(peor[i].t, peor[i].f));
            if (e > fin) { fin = e; }
        }
        if (num > fin) { fin = num; }
        printf("    %-46s %6d px   %s\n",
               "la lectura mas ancha acaba antes del borde", fin,
               (fin <= ui_top_movil_w_dbg()) ? "ok " : "*** MAL ***");
        printf("      ventana del repintado: %d px\n", ui_top_movil_w_dbg());
        if (fin > ui_top_movil_w_dbg()) { fallos++; }
    }

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
