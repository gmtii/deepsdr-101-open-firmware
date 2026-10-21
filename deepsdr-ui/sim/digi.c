/*
 * Render del panel de modos digitales (etapa 20) con el ui_digi.c que se
 * copia a User/.
 *
 * Comprueba ademas lo que solo se ve midiendo: que un renglon lleno no se
 * sale por la derecha, que los ocho caben en el alto del panel, y que la
 * zona del boton de borrar responde donde se dibuja y no fuera.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "ui_digi.h"
#include "font_ui_14b.h"
#include "rotulos_tabla.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

#define PANEL_Y 214
#define PANEL_H (UDG_HDR_H + UDG_ROWS * UDG_LINE_H)

static void fondo(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(PAL_SURF_0));
}

/* Un QSO de verdad de 20 m, tal y como sale del decodificador. */
static const char *const k_qso[UDG_ROWS] = {
    "CQ CQ DE EA4XYZ EA4XYZ K",
    "EA4XYZ DE DL1ABC DL1ABC KN",
    "DL1ABC DE EA4XYZ GM OM TNX FER CALL = UR RST 579 579",
    "NAME PEDRO PEDRO QTH MADRID MADRID = HW? AR DL1ABC DE EA4XYZ KN",
    "EA4XYZ DE DL1ABC R FB PEDRO = UR RST 589 589 NAME HANS QTH BERLIN",
    "= RIG IS FT991A PWR 100W ANT DIPOLE = HW? AR EA4XYZ DE DL1ABC KN",
    "DL1ABC DE EA4XYZ R TNX FB QSO HANS = 73 ES CUL AR",
    "DL1ABC DE EA4XYZ SK E E",
};

int main(int argc, char **argv)
{
    ui_digi_state_t st = {0};
    uint8_t i;
    int pulsado = (argc > 1 && argv[1][0] == '1');

    memset(&st, 0, sizeof st);
    st.y = PANEL_Y;
    st.h = PANEL_H;
    for (i = 0U; i < UDG_ROWS; i++) { st.fila[i] = k_qso[i]; }
    st.chip = "21 ppm";
    st.enganchado = 1U;
    st.btn = "Borrar";
    st.btn_press = (uint8_t)pulsado;

    lcd_sim_reset(0x0000);
    gfx2_render_screen(fondo, 0);
    ui_digi_draw_texto(&st);
    ui_digi_draw_chip(&st);
    ui_digi_draw_boton(&st);

    /*
     * QUE TODOS LOS ROTULOS DEL SEGUNDO BOTON CABEN EN EL - 24/09/2026
     *
     * La lista NO esta copiada aqui: la saca tools/gen_rotulos.py de las
     * tablas de main.c en cada "make comprueba". Una copia a mano seguiria
     * diciendo que todo cabe el dia que alguien alargue un rotulo, que es
     * justo el dia en que hace falta que salte.
     *
     * Un rotulo que no cabe no avisa: gfx2_text_in() lo centra en el ancho
     * que se le da y lo que sobra se pinta fuera, encima de lo de al lado.
     */
    {
        unsigned r;
        int hueco = UDG_BTN2_W - 6;   /* 3 px de aire a cada lado */
        for (r = 0U; r < sizeof k_rotulos_btn2 / sizeof k_rotulos_btn2[0]; r++) {
            int w = gfx2_text_w(k_rotulos_btn2[r], &font_ui_14b);
            int ok = w <= hueco;
            printf("  boton2 %-18s %3d px en %3d   %s\n",
                   k_rotulos_btn2[r], w, hueco, ok ? "ok" : "SE SALE");
            if (!ok) { fallos++; }
        }
    }

    /*
     * LA CHAPA DE CW, EN SU PEOR CASO - 09/10/2026.
     *
     * La velocidad ya salia; el margen entre las dos medias del umbral no,
     * y se le añadio. Con eso la chapa de CW pasa a tener TRES numeros
     * -velocidad, relacion aprendida y margen- y crece hacia la izquierda
     * desde el borde derecho. Aqui se mide que en el peor caso no llega a
     * invadir el segundo boton, que en CW es "Aprende".
     *
     * Se mide y no se cuenta: la fuente es proporcional, asi que contar
     * caracteres no dice nada. Es la misma leccion del pie de Informacion.
     */
    {
        const char *peor[] = { "45 ppm", "45 ppm 99,9", "45 ppm 99,9 99 dB",
                               "45 ppm 99 dB" };
        /* Hasta donde puede crecer sin tocar el segundo boton: la chapa
         * empieza en GFX2_W - UDG_PAD_X - (texto + 34), y a su izquierda
         * esta el borde derecho de btn2 mas su separacion. */
        int tope = GFX2_W - 6 - 34 - (UDG_BTN2_X + UDG_BTN2_W + UDG_BTN2_GAP);
        unsigned r;

        for (r = 0U; r < sizeof peor / sizeof peor[0]; r++) {
            int w = gfx2_text_w(peor[r], &font_ui_14b);
            int ok = w <= tope;
            printf("  chapa CW %-20s %3d px en %3d   %s\n",
                   peor[r], w, tope, ok ? "ok" : "SE SALE");
            if (!ok) { fallos++; }
        }
    }

    /*
     * LA CHAPA QUE ENCOGE - 24/09/2026
     *
     * La chapa de estado va pegada al borde derecho y su ancho lo decide el
     * texto, asi que al acortarse el texto la chapa ENCOGE Y SE MUEVE a la
     * derecha. Cuando solo se repintaba su propio rectangulo, lo que
     * quedaba a su izquierda no lo borraba nadie: al aparecer la señal de
     * SSTV, "Modo no soportado, VIS 123" pasa a "Martin 1 - 001/256" y
     * dejaba 75 px de rectangulo redondeado colgados en mitad de la barra.
     *
     * No se comprueba leyendo el codigo, se comprueba MIRANDO LOS PIXELES:
     * se pinta la larga, se pinta la corta encima, y se exige que entre el
     * ultimo boton y la chapa nueva no quede un solo pixel distinto del
     * fondo. Da igual como se implemente el arreglo mientras eso se cumpla.
     */
    {
        const char *larga = "Modo no soportado, VIS 123";
        const char *corta = "Martin 1 - 001/256";
        int16_t by = (int16_t)(PANEL_Y + 3);
        int16_t x0 = (int16_t)(UDG_BTN2_X + UDG_BTN2_W + UDG_BTN2_GAP);
        int16_t xchapa;
        uint16_t fondo_rgb;
        int sucios = 0, x, y;

        st.btn2 = "BMP 24 bits";
        ui_digi_draw_boton2(&st);
        st.chip = larga;
        ui_digi_draw_chip(&st);
        st.chip = corta;
        ui_digi_draw_chip(&st);

        /* Donde empieza la chapa corta, con la misma cuenta que ui_digi.c. */
        xchapa = (int16_t)(GFX2_W - 6 - (gfx2_text_w(corta, &font_ui_14b) + 34));
        fondo_rgb = lcd_sim_fb()[by * LCD_W + x0];   /* el fondo, recien limpiado */

        for (y = by; y < by + UDG_BTN_H; y++) {
            for (x = x0; x < xchapa; x++) {
                if (lcd_sim_fb()[y * LCD_W + x] != fondo_rgb) { sucios++; }
            }
        }
        printf("  chapa larga -> corta: %d px de resto entre x=%d y x=%d   %s\n",
               sucios, x0, xchapa, sucios == 0 ? "ok" : "MAL");
        if (sucios != 0) { fallos++; }

        /* Y que el arreglo no se haya llevado por delante los botones. */
        {
            int pintados = 0;
            for (y = by; y < by + UDG_BTN_H; y++) {
                for (x = UDG_BTN2_X; x < UDG_BTN2_X + UDG_BTN2_W; x++) {
                    if (lcd_sim_fb()[y * LCD_W + x] != fondo_rgb) { pintados++; }
                }
            }
            printf("  el segundo boton sigue dibujado: %d px   %s\n",
                   pintados, pintados > 200 ? "ok" : "MAL");
            if (pintados <= 200) { fallos++; }
        }


        /*
         * EL TERCER BOTON TIENE QUE SOBREVIVIR A UN REPINTADO DE LA BARRA.
         *
         * No estaba porque lo borraba el repintado de la barra: solo_barra()
         * limpiaba UDG_BAR_W fijo -300 px- y con la barra acortada a 170
         * esos 300 incluyen el hueco del boton. En HFDL la barra cambia cada
         * dos por tres, asi que el boton salia y desaparecia al instante.
         *
         * Esto lo caza sin entender nada: se dibuja la cabecera entera, se
         * repinta SOLO la barra, y se cuentan los pixeles del boton. Si el
         * repintado se lo lleva, salen cero.
         */
        {
            ui_digi_state_t h = st;
            int pintados = 0;
            int16_t bx, bw;

            h.barra_on = 1U; h.barra_v = 2U; h.barra_max = 3U;
            h.barra_w  = 170; h.btn3 = "Frec."; h.barra_txt = "ENGANCHADO";

            bx = (int16_t)(UDG_BTN2_X + h.barra_w + UDG_BTN2_GAP);
            bw = (int16_t)(UDG_BTN2_X + 300 - bx);

            ui_digi_draw_texto(&h);   /* cabecera entera, boton incluido */
            ui_digi_draw_barra(&h);   /* y ahora SOLO la barra */

            for (y = by; y < by + UDG_BTN_H; y++) {
                for (x = bx; x < bx + bw; x++) {
                    if (lcd_sim_fb()[y * LCD_W + x] != fondo_rgb) { pintados++; }
                }
            }
            printf("  el boton de frecuencias sobrevive a la barra: %d px   %s\n",
                   pintados, pintados > 200 ? "ok" : "MAL");
            if (pintados <= 200) { fallos++; }
        }

        /*
         * Y EL DEL MAPA, AL REPINTADO DE LA CHAPA - 28/09/2026.
         *
         * Respondia al dedo -la zona de toque estaba bien- pero no se
         * dibujaba. Lo borraba solo_chip(), que limpia una franja FIJA
         * desde el final de la barra hasta el borde derecho, y el boton
         * nuevo vive dentro de esa franja. En FT8 la chapa es el contador
         * de los quince segundos: se repinta una vez por segundo.
         *
         * Es la TERCERA vez que pasa lo mismo -el tercer boton con la
         * barra, el cuarto con la chapa, y ahora el quinto-, asi que esto
         * se mide igual que aquellos: se pinta la cabecera entera, se
         * repinta SOLO la chapa, y se cuentan los pixeles del boton.
         */
        {
            ui_digi_state_t h = st;
            int pintados = 0;
            int16_t bx, bw;

            h.chip = "9/15 ";
            h.btn3 = 0;
            h.btn4 = "14.074";
            h.btn5 = "Mapa";
            h.barra_on = 1U; h.barra_v = 9U; h.barra_max = 15U; h.barra_w = 0;

            bx = ui_digi_btn5_x_dbg(&h);
            bw = ui_digi_btn5_w_dbg(&h);

            ui_digi_draw_texto(&h);   /* cabecera entera, boton incluido */
            ui_digi_draw_chip(&h);    /* y ahora SOLO la chapa */

            for (y = by; y < by + UDG_BTN_H; y++) {
                for (x = bx; x < bx + bw; x++) {
                    if (lcd_sim_fb()[y * LCD_W + x] != fondo_rgb) { pintados++; }
                }
            }
            printf("  el boton del mapa sobrevive a la chapa: %d px   %s\n",
                   pintados, pintados > 200 ? "ok" : "MAL");
            if (pintados <= 200) { fallos++; }

            /* Y al repintado del cuarto boton, que es su vecino. */
            pintados = 0;
            ui_digi_draw_texto(&h);
            ui_digi_draw_boton4(&h);
            for (y = by; y < by + UDG_BTN_H; y++) {
                for (x = bx; x < bx + bw; x++) {
                    if (lcd_sim_fb()[y * LCD_W + x] != fondo_rgb) { pintados++; }
                }
            }
            printf("  y al repintado del boton de al lado: %d px   %s\n",
                   pintados, pintados > 200 ? "ok" : "MAL");
            if (pintados <= 200) { fallos++; }
        }
        st.chip = "21 ppm";
        st.btn2 = 0;
    }

    printf("panel digital: cabecera %d px + %d renglones de %d, y=%d..%d\n",
           UDG_HDR_H, UDG_ROWS, UDG_LINE_H, PANEL_Y, PANEL_Y + PANEL_H);

    /* Ningun renglon puede salirse por la derecha. */
    for (i = 0U; i < UDG_ROWS; i++) {
        int16_t w = gfx2_text_w(k_qso[i], &font_ui_14b);
        if (w + 6 > GFX2_W) {
            printf("  renglon %u mide %d px y la pantalla da %d  SE SALE\n", i, w + 6, GFX2_W);
            fallos++;
        }
    }
    {
        /*
         * Cuantas "W" seguidas caben de verdad. 66 -el tope de la rejilla-
         * no caben: son 996 px en una pantalla de 800, y por eso
         * rtty_text_putc() salta de linea POR ANCHO MEDIDO y no solo por
         * numero de columnas. Aqui se comprueba que ese limite existe y
         * donde cae, que es el numero que hay que mirar si un dia se cambia
         * la fuente del panel.
         */
        char peor[67];
        int n = 0;
        while (n < 66) {
            memset(peor, 'W', (size_t)(n + 1)); peor[n + 1] = '\0';
            if (gfx2_text_w(peor, &font_ui_14b) + 12 > GFX2_W) { break; }
            n++;
        }
        printf("  caben %d mayusculas anchas de las 66 de la rejilla\n", n);
        if (n < 40) { printf("  caben muy pocas  MAL\n"); fallos++; }
    }

    /* La zona del boton: dentro si, fuera no. */
    {
        uint16_t cx = (uint16_t)(UDG_BTN_X + UDG_BTN_W / 2);
        uint16_t cy = (uint16_t)(PANEL_Y + 3 + UDG_BTN_H / 2);
        if (!ui_digi_boton_hit(&st, cx, cy))       { printf("  el centro del boton no responde  MAL\n"); fallos++; }
        if (ui_digi_boton_hit(&st, 400U, cy))      { printf("  responde lejos del boton  MAL\n"); fallos++; }
        if (ui_digi_boton_hit(&st, cx, (uint16_t)(PANEL_Y + 100))) { printf("  responde sobre el texto  MAL\n"); fallos++; }
        /* Y, lo que importa: NO puede responder dentro del trazo, que es de
         * donde se ha mudado. */
        if (ui_digi_boton_hit(&st, cx, 120U))      { printf("  todavia responde dentro del trazo  MAL\n"); fallos++; }
    }

    /*
     * EL QUINTO BOTON: EL MAPA - 28/09/2026.
     *
     * Ese hueco es real -el cuarto boton empieza en 430 y no pasa de 150
     * px, y la chapa empieza pasado el 700- pero "es real" era un
     * razonamiento mio leyendo constantes. Aqui se MIDE, con las chapas y
     * los rotulos de los tres modos que tienen mapa, que son los que de
     * verdad lo van a llevar.
     *
     * Y se comprueba lo otro: que responde donde se dibuja, y que no le
     * quita el sitio al cuarto boton.
     */
    {
        static const struct { const char *chip; const char *b3, *b4; const char *modo; }
        k[] = {
            { "9/15 ",  0,        "14.074", "FT8"  },
            { "1200 ",  "Frec.",  "11.184", "HFDL" },
            { "9600 ",  0,        "161.975", "AIS" },
            /* JTTY desde el 28/09: su chapa es la cuenta de atomos y su
             * cuarto boton la frecuencia de la lista de WSJT-X. */
            { "1234 ",  0,        "14.090",  "JTTY" },
            /*
             * Y LOS CASOS QUE FALTABAN, que son los que enseño la radio.
             *
             * Esta lista tenia UNA chapa por modo, y con una sola chapa
             * por modo la pregunta "cambia el boton cuando cambia la
             * chapa" no se puede ni hacer. Las chapas de verdad crecen y
             * se encogen solas: la de JTTY va de una cifra a cuatro y la
             * de HFDL lleva un porcentaje de una o dos.
             */
            { "3",             0, "14.090",  "JTTY 1" },
            { "3724",          0, "14.090",  "JTTY 4" },
            { "5% 12/34",      0, "11.184",  "HFDL 1" },
            { "95% 12/34",     0, "11.184",  "HFDL 2" },
            { "100% 999/999",  0, "11.184",  "HFDL X" },
            /*
             * WSPR desde el 28/09 por la tarde. Pedido por el usuario. Su chapa es "N oidas",
             * que cambia de ancho con el numero Y con el plural.
             */
            { "1 oida",        0, "14.095",  "WSPR 1" },
            { "999 oidas",     0, "14.095",  "WSPR N" },
            { "sin hora",      0, "14.095",  "WSPR -" },
        };
        unsigned i;
        int16_t fijo_x = -1, fijo_w = -1;

        printf("\n  el boton del mapa, debajo de BW\n");
        for (i = 0U; i < sizeof k / sizeof k[0]; i++) {
            ui_digi_state_t m = st;
            int16_t bx, bw, b4x, b4w;
            uint16_t cy = (uint16_t)(PANEL_Y + 3 + UDG_BTN_H / 2);

            m.chip = k[i].chip;
            m.btn3 = k[i].b3;
            m.btn4 = k[i].b4;
            m.btn5 = "Mapa";
            m.btn5_press = (uint8_t)(i & 1U);

            bx = ui_digi_btn5_x_dbg(&m);
            bw = ui_digi_btn5_w_dbg(&m);
            printf("    %-5s Mapa en x=%d..%d (%d px)   %s\n", k[i].modo,
                   bx, bx + bw, bw, (bw > 0) ? "ok" : "MAL");
            if (bw <= 0) { fallos++; continue; }

            /* El rotulo tiene que caber DENTRO, no recortarse: gfx2_text_in
             * corta por la derecha sin avisar. */
            if (gfx2_text_w("Mapa", &font_ui_14b) + 12 > bw) {
                printf("      el rotulo no cabe  MAL\n"); fallos++;
            }
            /* Responde en su centro y no fuera. */
            if (!ui_digi_boton5_hit(&m, (uint16_t)(bx + bw / 2), cy)) {
                printf("      el centro no responde  MAL\n"); fallos++;
            }
            if (ui_digi_boton5_hit(&m, (uint16_t)(bx - 20), cy)) {
                printf("      responde 20 px a su izquierda  MAL\n"); fallos++;
            }
            /*
             * Y no se monta encima del cuarto ni de la chapa. El ancho del
             * cuarto se mide TOCANDO, no leyendo su geometria: asi la
             * prueba no depende de constantes internas de ui_digi.c, que
             * es lo que se esta comprobando.
             */
            b4x = 0; b4w = 0;
            {
                int16_t xx;
                for (xx = 300; xx < bx; xx++) {
                    if (ui_digi_boton4_hit(&m, (uint16_t)xx, cy)) {
                        if (b4w == 0) { b4x = xx; }
                        b4w++;
                    }
                }
            }
            if (ui_digi_boton4_hit(&m, (uint16_t)(bx + bw / 2), cy)) {
                printf("      el cuarto boton responde debajo del quinto  MAL\n");
                fallos++;
            }
            if (ui_digi_chip_hit(&m, (uint16_t)(bx + bw / 2), cy)) {
                printf("      la chapa responde debajo del quinto  MAL\n");
                fallos++;
            }
            if (k[i].b4 != 0 && b4w <= 0) {
                printf("      el quinto se ha comido al cuarto  MAL\n");
                fallos++;
            } else if (k[i].b4 != 0) {
                printf("      y el cuarto sigue en x=%d con %d px\n", b4x, b4w);
            }

            /*
             * Y LO QUE DE VERDAD FALTABA: QUE EL RECTANGULO SEA EL MISMO.
             *
             * Esto se comprobaba "no se solapa" y "cabe el rotulo", que
             * las dos cosas pasaban mientras el boton se movia 27 px cada
             * vez que el contador cambiaba de cifra. Un boton que no se
             * solapa con nadie y se mueve solo sigue siendo un boton que
             * no se encuentra con el dedo.
             *
             * No es "parecido": es EL MISMO. Si algun dia hace falta que
             * cambie, este exige salta y hay que venir a decir por que.
             */
            if (fijo_x < 0) { fijo_x = bx; fijo_w = bw; }
            else if (bx != fijo_x || bw != fijo_w) {
                printf("      el boton se ha movido: x%d w%d en vez de"
                       " x%d w%d  MAL\n", bx, bw, fijo_x, fijo_w);
                fallos++;
            }
        }
        printf("    el rectangulo del mapa es x=%d w=%d en los %u casos\n",
               fijo_x, fijo_w, (unsigned)(sizeof k / sizeof k[0]));
    }

    /*
     * Y CON QUINTO BOTON, LA CHAPA NO LO INVADE POR LARGO QUE SEA EL TEXTO.
     *
     * La chapa se ensancha con su texto, y el que escribe ese texto esta
     * en otro fichero y no sabe nada de botones. Asi que el tope tiene que
     * estar AQUI: con quinto boton, la caja mide UDG_CHIP_RES y punto, y
     * lo que no quepa se recorta. Acortar la frase del otro lado tambien
     * se hizo, pero eso arregla un caso; esto arregla todos.
     */
    {
        static const char *k[] = {
            "5", "s100 e3 +58",
            "llenando el entrelazador 180/392",
            "esta forma de onda solo se identifica, no se demodula", 0 };
        unsigned i;
        int16_t fijo = -1;

        printf("\n  con quinto boton la chapa no se sale de lo reservado\n");
        for (i = 0U; k[i]; i++) {
            ui_digi_state_t m = st;
            int16_t cw, cx, b5x;

            m.chip = k[i];
            m.btn3 = "600"; m.btn4 = "Largo"; m.btn5 = "ASCII";
            cw = ui_digi_chip_w_dbg(&m);
            cx = ui_digi_chip_x_dbg(&m);
            b5x = ui_digi_btn5_x_dbg(&m);
            printf("    \"%-34.34s\" caja %3d en x=%3d   boton5 en x=%3d  %s\n",
                   k[i], cw, cx, b5x, (cx >= b5x) ? "ok" : "SE LO COME");
            if (cx < b5x) { fallos++; }
            if (fijo < 0) { fijo = cw; }
            else if (cw != fijo) {
                printf("      la caja ha cambiado de ancho: %d vs %d  MAL\n", cw, fijo);
                fallos++;
            }
        }
    }

    /*
     * EL BOTON "BORRAR" DEL PANEL DE CW, DESPUES DE MUCHO TEXTO - 05/10/2026.
     *
     * El panel de texto de CW se pinta de dos maneras: entero
     * -ui_digi_draw_texto(), que incluye la cabecera y por tanto el boton- o
     * renglon a renglon -ui_digi_draw_fila(), que es el camino normal segun
     * van llegando caracteres-. Mas la chapa de las palabras por minuto, que
     * se repinta sola cuando cambia el numero.
     *
     * Si el boton desaparece "con mucho texto", el sospechoso es alguno de
     * los dos caminos parciales: el de los renglones o el de la chapa. Esto
     * los ejercita mil veces seguidas -mas de lo que da una sesion larga- y
     * cuenta los pixeles del boton al final. No entiende de cursores ni de
     * desplazamiento: mira si el boton sigue ahi.
     *
     * Si esto sale en verde, el fallo NO esta en ui_digi.c y hay que
     * buscarlo en quien ordena los repintados, que es main.c. Un banco en
     * verde que acota donde NO esta el fallo tambien sirve.
     */
    {
        ui_digi_state_t c = st;
        const char *chapas[] = { "18 ppm", "21 ppm", "9 ppm", "24 ppm" };
        int antes = 0, despues = 0;
        unsigned paso;
        int16_t by = (int16_t)(PANEL_Y + 3);
        int16_t x, y;
        uint16_t fondo_rgb;

        c.barra_on = 0U; c.barra_w = 0;
        c.btn3 = 0; c.btn4 = 0; c.btn5 = 0;
        c.btn = "Borrar";

        lcd_sim_reset(0x0000);
        gfx2_render_screen(fondo, 0);
        fondo_rgb = lcd_sim_fb()[(PANEL_Y + 200) * LCD_W + 400];
        ui_digi_draw_texto(&c);
        ui_digi_draw_chip(&c);
        ui_digi_draw_boton(&c);
        for (y = by; y < by + UDG_BTN_H; y++) {
            for (x = UDG_BTN_X; x < UDG_BTN_X + UDG_BTN_W; x++) {
                if (lcd_sim_fb()[y * LCD_W + x] != fondo_rgb) { antes++; }
            }
        }

        for (paso = 0U; paso < 1000U; paso++) {
            ui_digi_draw_fila(&c, (uint8_t)(paso % UDG_ROWS));
            if ((paso % 7U) == 0U) {
                c.chip = chapas[(paso / 7U) % 4U];
                ui_digi_draw_chip(&c);
            }
        }
        for (y = by; y < by + UDG_BTN_H; y++) {
            for (x = UDG_BTN_X; x < UDG_BTN_X + UDG_BTN_W; x++) {
                if (lcd_sim_fb()[y * LCD_W + x] != fondo_rgb) { despues++; }
            }
        }
        printf("\n  el boton Borrar sobrevive a 1000 renglones y 143 chapas\n");
        printf("    antes %d px, despues %d px   %s\n", antes, despues,
               (despues >= antes) ? "ok" : "*** MAL ***");
        if (despues < antes) { fallos++; }
    }

    /*
     * UN BOTON PUESTO TIENE QUE TENER SITIO DONDE DIBUJARSE - 05/10/2026.
     *
     * El panel de RTTY estreno dos botones -desplazamiento y velocidad- y
     * NO SALIAN. No era el rotulo ni el repintado: btn3_w() de ui_digi.c
     * se calcula como "lo que queda entre el final de la barra y la
     * chapa", y bar_w() devuelve la barra ENTERA cuando barra_w vale 0,
     * que es lo que vale en un panel sin barra de progreso. El hueco salia
     * negativo, el ancho se quedaba en 0, y los dos botones vivian en la
     * estructura sin un pixel asignado.
     *
     * Poner el rotulo y acortar la barra son DOS cosas, y quien añade un
     * boton se acuerda de la primera y se olvida de la segunda. Ya habia
     * un aviso escrito en la rama de STANAG y aun asi se tropezo igual, o
     * sea que el aviso no bastaba: esto lo mide.
     *
     * La regla es la que se puede comprobar sin saber de que modo se
     * trata: si st->btn3 o st->btn4 apuntan a algo, su ancho no puede ser
     * cero.
     */
    {
        struct { const char *que; int16_t barra; const char *b3, *b4; int espera; } k[] = {
            { "RTTY con la barra entera (el fallo)",   0, "850 Hz", "50 Bd", 0 },
            { "RTTY con la barra acortada",          170, "850 Hz", "50 Bd", 1 },
            { "FT8 con la barra a 190 y el CQ",      190, "CQ",     "7.074", 1 },
            { "STANAG, que ya lo hacia bien",        170, "600",    "Largo", 1 },
            { "sin botones, barra entera",             0, 0,        0,       1 },
        };
        unsigned i;

        printf("\n  un boton con rotulo tiene sitio donde dibujarse\n");
        for (i = 0U; i < sizeof k / sizeof k[0]; i++) {
            ui_digi_state_t m = st;
            int16_t w3, w4;
            int bien;

            m.barra_on = 0U;
            m.barra_w  = k[i].barra;
            m.btn3 = k[i].b3;
            m.btn4 = k[i].b4;
            m.btn5 = 0;
            w3 = ui_digi_btn3_w_dbg(&m);
            w4 = ui_digi_btn4_w_dbg(&m);
            bien = ((k[i].b3 == 0) || (w3 > 0)) && ((k[i].b4 == 0) || (w4 > 0));
            printf("    %-36s btn3 %3d  btn4 %3d  %s\n",
                   k[i].que, w3, w4,
                   (bien == k[i].espera) ? "ok" : "*** MAL ***");
            if (bien != k[i].espera) { fallos++; }
        }
    }

    /*
     * LO QUE PONE DENTRO DE LA BARRA TIENE QUE CABER DENTRO DE LA BARRA.
     * 13/10/2026.
     *
     * La barra de FT8 baja de 300 a 200 px para hacerle sitio al boton de
     * CQ, y dentro lleva CUATRO numeros de diagnostico. Acortar una barra
     * con texto dentro sin medir el texto es como se consigue que en el
     * unico momento en que esos numeros importan -cuando son grandes,
     * porque algo va mal- salgan cortados.
     *
     * El caso peor no se inventa: sale de los topes que pone ft8_chapa()
     * en main.c -999 candidatos, 9999 ms, 99 perdidos y +-9,9 de DT- y de
     * la tipografia con la que los pinta pinta_barra(), que es
     * font_ui_14b.
     */
    {
        /*
         * Los textos son los que arma ft8_chapa() DESPUES de la limpieza
         * del 13/10/2026: los candidatos y el DT siempre, y uno solo de
         * los dos de diagnostico cuando toca. El de los cuatro juntos ya
         * no existe -medía 243 px- y por eso no esta aqui.
         */
        static const struct { const char *txt; const char *que; } k[] = {
            { "999c \xC2\xB7 9999 ms \xC2\xB7 +9,9 s", "FT8, el peor: decodificacion lenta" },
            { "999c \xC2\xB7 99p \xC2\xB7 -9,9 s",     "FT8, el peor: perdiendo audio" },
            { "8/15 \xC2\xB7 999c \xC2\xB7 9999 ms \xC2\xB7 +9,9 s", "FT8, el peor de todos con el segundo delante" },
            { "8/15 \xC2\xB7 24c \xC2\xB7 +0,2 s",       "FT8, un rato normal" },
            { "0c \xC2\xB7 +0,0 s",                    "FT8 sin nada que decodificar" },
            { "pon el reloj en hora",                  "FT8 sin reloj (el mas largo en espanol)" },
            { "set the clock first",                   "FT8 sin reloj, en ingles" }
        };
        /*
         * El hueco de verdad son los 336 de la barra MENOS los 20 que se
         * reserva la lucecita de la derecha: el texto se centra en lo que
         * queda, no en la barra entera. Medir contra 336 diria que cabe
         * un texto que saldria por debajo del punto.
         */
        const int16_t barra = 336 - 20;
        unsigned i;

        printf("\n  el texto de la barra de FT8 cabe en sus %d px\n", (int)barra);
        for (i = 0U; i < sizeof k / sizeof k[0]; i++) {
            int16_t w = gfx2_text_w(k[i].txt, &font_ui_14b);

            printf("    %-44s %3d px   %s\n", k[i].que, (int)w,
                   (w <= barra - 8) ? "ok" : "*** MAL ***");
            if (w > barra - 8) { fallos++; }
        }
        printf("    (8 px de aire: la barra lleva su contorno de 1 px y el\n"
               "     texto va centrado, asi que 4 px por lado)\n");
    }

    /*
     * LA SEGUNDA DISPOSICION: BOTONES A LA IZQUIERDA, BARRA A LA DERECHA.
     * 13/10/2026.
     *
     * Cinco cajas en una franja de 800 px, puestas por cinco funciones
     * distintas. Lo que hay que medir no es que cada una tenga sitio -eso
     * ya lo dice el _Static_assert de ui_digi.c- sino que NO SE PISAN: la
     * primera version mandaba la barra a la derecha y dejaba el quinto
     * boton en su sitio fijo, y "Mapa" salia pintado ENCIMA de la barra.
     * Se vio en la foto antes de flashear, que para eso esta.
     */
    {
        ui_digi_state_t d;
        struct { const char *que; int16_t x, w; } caja[5];
        unsigned i;
        int bien = 1;

        memset(&d, 0, sizeof d);
        d.y = PANEL_Y; d.h = PANEL_H;
        d.barra_dcha = 1U; d.barra_on = 1U; d.barra_max = 15U; d.barra_v = 8U;
        d.btn = "Borrar"; d.btn3 = "CQ"; d.btn4 = "7.074"; d.btn5 = "Mapa";
        d.chip = 0;

        caja[0].que = "Borrar"; caja[0].x = UDG_BTN_X; caja[0].w = UDG_BTN_W;
        caja[1].que = "CQ";     caja[1].x = ui_digi_btn3_x_dbg(&d);
                                caja[1].w = ui_digi_btn3_w_dbg(&d);
        caja[2].que = "7.074";  caja[2].x = ui_digi_btn4_x_dbg(&d);
                                caja[2].w = ui_digi_btn4_w_dbg(&d);
        caja[3].que = "Mapa";   caja[3].x = ui_digi_btn5_x_dbg(&d);
                                caja[3].w = ui_digi_btn5_w_dbg(&d);
        caja[4].que = "barra";  caja[4].x = ui_digi_barra_x_dbg(&d);
                                caja[4].w = ui_digi_barra_w_dbg(&d);

        printf("\n  la cabecera de FT8: botones a la izquierda, barra a la derecha\n");
        for (i = 0; i < 5; i++) {
            printf("    %-8s  %3d .. %3d  (%3d px)\n", caja[i].que,
                   (int)caja[i].x, (int)(caja[i].x + caja[i].w), (int)caja[i].w);
            if (caja[i].w <= 0) { bien = 0; }
            if (i > 0 && caja[i].x < caja[i - 1].x + caja[i - 1].w) { bien = 0; }
        }
        if (caja[4].x + caja[4].w > GFX2_W - 6) { bien = 0; }   /* 6 = UDG_PAD_X */
        printf("    %s\n", bien ? "ninguna se pisa y la ultima no se sale   ok"
                                 : "*** MAL: se pisan o se salen ***");
        if (!bien) { fallos++; }

        /*
         * Y EL TOQUE QUE CUADRA LA RANURA, que se mudo de la chapa a la
         * barra. Tiene que responder DENTRO de la barra y no encima
         * del boton del mapa, que es el vecino. Ese error exacto ya pasó
         * una vez con la chapa y el quinto boton (28/09/2026).
         */
        {
            int16_t bx = caja[4].x, bw = caja[4].w;
            int16_t yb = (int16_t)(PANEL_Y + 6);
            int dentro = ui_digi_barra_hit(&d, (uint16_t)(bx + bw / 2), (uint16_t)yb);
            int borde  = ui_digi_barra_hit(&d, (uint16_t)(bx + 1), (uint16_t)yb);
            int mapa   = ui_digi_barra_hit(&d,
                            (uint16_t)(caja[3].x + caja[3].w / 2), (uint16_t)yb);
            int fuera  = ui_digi_barra_hit(&d, (uint16_t)(UDG_BTN_X + 2), (uint16_t)yb);

            printf("    el toque de cuadrar la ranura: centro %s, borde %s,\n"
                   "      encima de \"Mapa\" %s, encima de \"Borrar\" %s\n",
                   dentro ? "si" : "NO", borde ? "si" : "NO",
                   mapa ? "SI" : "no", fuera ? "SI" : "no");
            if (!dentro || !borde || mapa || fuera) { fallos++; }
            else { printf("    responde donde se dibuja y en ningun sitio mas   ok\n"); }
        }

        /*
         * LA BANDA QUE SE REPINTA NO SE PUEDE SALIR DE LA PANTALLA.
         * 13/10/2026.
         *
         * Y se veia como una persiana de rayas naranjas. ui_digi_draw_barra()
         * pedia una ventana de barra + hueco + tercer boton porque en la
         * disposicion de siempre el tercero va a la derecha de la barra. En
         * esta esta al otro lado, asi que la cuenta pedia 450 px desde el
         * 458: hasta la columna 908 de una pantalla de 800. El controlador
         * no tiene columna 908 y cada fila se derrama en la siguiente.
         *
         * Esto NO se podia ver en el PPM de mas abajo: el simulador dibuja
         * en un lienzo y recorta, no tiene el bus de la radio. Lo que se
         * mide entonces es el NUMERO que se le pide al bus, que es donde
         * estaba el fallo.
         */
        {
            int16_t vx = ui_digi_barra_x_dbg(&d);
            int16_t vw = ui_digi_barra_ventana_w_dbg(&d);
            int cabe = (vx >= 0) && (vx + vw <= GFX2_W);

            printf("    la banda que se repinta: %d .. %d de %d   %s\n",
                   (int)vx, (int)(vx + vw), (int)GFX2_W,
                   cabe ? "ok" : "*** MAL: se sale de la pantalla ***");
            if (!cabe) { fallos++; }
        }
    }

    /*
     * ======================================================================
     * EL "8/15" PEGADO AL PUNTO VERDE - 13/10/2026
     * ======================================================================
     *
     * Estaba al principio de un renglon centrado, o sea en el extremo
     * CONTRARIO al punto que cuenta lo mismo que el. Ahora va en su propio
     * texto -barra_txt_dcha- y se dibuja pegado a la derecha.
     *
     * Y ESTO SE MIDE EN PIXELES PINTADOS, no repitiendo la formula de
     * pinta_barra() en el banco: una cuenta copiada da verde aunque el
     * dibujo use otra. Se pinta la barra sola en el lienzo y se busca la
     * columna mas a la derecha con tinta, sin contar el punto.
     *
     * La barra se pinta VACIA (barra_v = 0) a proposito: asi el interior
     * es de un solo color y cualquier pixel distinto es letra.
     */
    {
        ui_digi_state_t d;
        int16_t bx, bw, punto_x, i_y0, i_y1;
        uint16_t fondo565 = gfx2_to565(gfx2_rgb(PAL_SURF_2).r,
                                       gfx2_rgb(PAL_SURF_2).g,
                                       gfx2_rgb(PAL_SURF_2).b);

        memset(&d, 0, sizeof d);
        d.y = PANEL_Y; d.h = PANEL_H;
        d.barra_dcha = 1U; d.barra_on = 1U;
        d.barra_max = 15U; d.barra_v = 0U;
        d.btn = "Borrar"; d.btn3 = "CQ"; d.btn4 = "7.074"; d.btn5 = "Mapa";
        d.enganchado = 1U;

        bx = ui_digi_barra_x_dbg(&d);
        bw = ui_digi_barra_w_dbg(&d);
        punto_x = (int16_t)(bx + bw - 16);   /* el borde izquierdo del punto */
        /* La fila de botones empieza en st->y + 3 (btn_y() en ui_digi.c) y
         * mide UDG_BTN_H. Se mira por dentro del contorno de 1 px. */
        i_y0 = (int16_t)(PANEL_Y + 3 + 3);
        i_y1 = (int16_t)(PANEL_Y + 3 + UDG_BTN_H - 3);

        printf("\n  el 8/15 pegado al punto verde\n");

        {
            /* Columna mas a la derecha con tinta, mirando solo hasta el
             * punto: lo que buscamos es el final del TEXTO. */
            struct { const char *dcha; const char *centro; const char *que; }
            caso[2] = {
                { "8/15", "24c \xC2\xB7 +0,2 s", "con el 8/15 a la derecha" },
                { 0,      "24c \xC2\xB7 +0,2 s", "sin el (todo centrado)" }
            };
            int16_t fin[2] = { -1, -1 };
            unsigned c;

            for (c = 0; c < 2; c++) {
                int16_t x, y;

                d.barra_txt      = caso[c].centro;
                d.barra_txt_dcha = caso[c].dcha;

                lcd_sim_reset(0x0000);
                gfx2_render_screen(fondo, 0);
                ui_digi_draw_barra(&d);

                for (x = (int16_t)(punto_x - 1); x > bx; x--) {
                    int hay = 0;
                    for (y = i_y0; y < i_y1; y++) {
                        if (lcd_sim_fb()[y * LCD_W + x] != fondo565) { hay = 1; break; }
                    }
                    if (hay) { fin[c] = x; break; }
                }
                printf("    %-26s la tinta acaba en %4d, el punto empieza en %d"
                       "  (%d px de hueco)\n",
                       caso[c].que, (int)fin[c], (int)punto_x,
                       (int)(punto_x - fin[c]));
            }

            /* Pegado: entre la ultima letra y el punto, el aire de la
             * separacion y poco mas. Doce pixeles es un margen generoso
             * sobre los diez de UDG_BAR_AIRE y sigue siendo la mitad de lo
             * que mide un "8". */
            if (fin[0] > 0 && punto_x - fin[0] <= 12) {
                printf("    el 8/15 va pegado al punto                           ok\n");
            } else {
                printf("    *** MAL: el 8/15 no esta pegado al punto ***\n");
                fallos++;
            }

            /* Y QUE SEA EL CAMPO NUEVO QUIEN LO HACE, y no que el texto
             * centrado llegue hasta alli por largo: sin el, la tinta tiene
             * que acabar MUCHO antes. Sin esta segunda medida, el banco
             * daria verde con el codigo viejo el dia que el texto centrado
             * creciera. */
            if (fin[1] > 0 && punto_x - fin[1] >= 40) {
                printf("    y sin el campo, el texto se centra y no llega        ok\n");
            } else {
                printf("    *** MAL: la medida no distingue el campo nuevo ***\n");
                fallos++;
            }
        }

        /* Y QUE NO SE PISEN. Con el texto centrado mas largo que admite la
         * barra, entre los dos bloques tiene que quedar hueco en blanco. */
        {
            int16_t x, y, blancas = 0, max_blancas = 0;

            d.barra_txt      = "999c \xC2\xB7 99p \xC2\xB7 -9,9 s";
            d.barra_txt_dcha = "15/15";
            lcd_sim_reset(0x0000);
            gfx2_render_screen(fondo, 0);
            ui_digi_draw_barra(&d);

            for (x = (int16_t)(bx + 2); x < punto_x; x++) {
                int hay = 0;
                for (y = i_y0; y < i_y1; y++) {
                    if (lcd_sim_fb()[y * LCD_W + x] != fondo565) { hay = 1; break; }
                }
                if (hay) { blancas = 0; }
                else if (++blancas > max_blancas) { max_blancas = blancas; }
            }
            printf("    con el texto mas largo, el mayor hueco en blanco son %d px\n",
                   (int)max_blancas);
            if (max_blancas >= 6) {
                printf("    los dos bloques se leen separados                    ok\n");
            } else {
                printf("    *** MAL: el 8/15 y los numeros se tocan ***\n");
                fallos++;
            }
        }
    }

    /*
     * LA CHAPA SIN QUINTO BOTON SE PEGA A SU TEXTO... HASTA UN TOPE.
     *
     * SSTV y wefax enseñan frases enteras -"Esperando el tono de arranque -
     * 120 lpm"- y su caja se pega al texto: la reserva de UDG_CHIP_RES es
     * solo para los modos que llevan boton de mapa. Si eso se rompe, esas
     * dos pantallas se quedan con la frase recortada.
     *
     * PERO NO PUEDE CRECER SIN FIN, y eso faltaba. 11/10/2026: este mismo
     * bloque comprobaba la FORMULA -caja == texto+34- y daba verde mientras
     * la consecuencia era que el CUARTO BOTON desaparecia. Una chapa de
     * "llenando el entrelazador 180/392" dejaba btn4 en ancho CERO, porque
     * btn4_w() se mide contra chip_x(). O sea: el banco miraba el numero
     * que no era.
     *
     * Ahora se comprueban las dos cosas: que se pega al texto mientras cabe,
     * y que al pasarse se topa. Y debajo, lo que de verdad importaba.
     */
    {
        static const char *k[] = { "5", "Esperando una imagen",
                                   "Recibiendo - 120 lpm +150",
                                   "llenando el entrelazador 180/392", 0 };
        unsigned i;

        printf("\n  la chapa de los modos sin mapa se pega a su texto, con tope\n");
        for (i = 0U; k[i]; i++) {
            ui_digi_state_t m = st;
            int16_t cw, esperado, tope;
            ui_digi_state_t t = st;

            /* El tope NO se copia aqui: se le pregunta al modulo con un
             * texto absurdamente largo. Una constante repetida en el banco
             * es una constante que un dia dice otra cosa que el firmware, y
             * entonces el banco aprueba lo que no debe. */
            t.chip = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
            t.btn3 = 0; t.btn4 = 0; t.btn5 = 0;
            tope = ui_digi_chip_w_dbg(&t);

            m.chip = k[i]; m.btn3 = 0; m.btn4 = 0; m.btn5 = 0;
            cw = ui_digi_chip_w_dbg(&m);
            esperado = (int16_t)(gfx2_text_w(k[i], &font_ui_14b) + 34);
            if (esperado > tope) { esperado = tope; }
            printf("    \"%-34s\" caja %3d, espera %3d   %s\n",
                   k[i], cw, esperado, (cw == esperado) ? "ok" : "MAL");
            if (cw != esperado) { fallos++; }
        }
    }

    /*
     * ...Y EL CUARTO BOTON SOBREVIVE A CUALQUIER CHAPA.
     *
     * Esto es lo que habia que medir desde el principio. El fallo de "un
     * boton que se va" ha vuelto tres veces en este proyecto -el tercero,
     * el quinto, y ahora el cuarto sin quinto- y siempre por lo mismo: algo
     * de ancho variable que le come el sitio a algo que tiene que estar.
     *
     * Antes del arreglo del 11/10, con btn5 apagado:
     *     "Esperando una imagen"              btn4 = 112
     *     "Modo no soportado, VIS 123"        btn4 = 106   se estrechaba
     *     "llenando el entrelazador 180/392"  btn4 =   0   DESAPARECIA
     */
    {
        static const char *k[] = { "45 bd", "100% 999/999",
                                   "Martin 1 - 001/256",
                                   "Esperando una imagen",
                                   "Modo no soportado, VIS 123",
                                   "llenando el entrelazador 180/392", 0 };
        unsigned i;

        printf("\n  el cuarto boton no se encoge con la chapa, haya quinto o no\n");
        for (i = 0U; k[i]; i++) {
            ui_digi_state_t m = st;
            int16_t sin5, con5;

            m.chip = k[i]; m.btn3 = "Frec."; m.btn4 = "13.312";
            m.btn5 = 0;      sin5 = ui_digi_btn4_w_dbg(&m);
            m.btn5 = "Mapa"; con5 = ui_digi_btn4_w_dbg(&m);
            printf("    \"%-34s\" sin quinto %3d, con quinto %3d   %s\n",
                   k[i], sin5, con5,
                   (sin5 == con5 && sin5 > 0) ? "ok" : "MAL");
            if (sin5 != con5 || sin5 <= 0) { fallos++; }
        }
    }

    /*
     * Y LA CABECERA DE FT8 TAL COMO QUEDA, para mirarla: la barra
     * acortada, el boton de CQ, el de frecuencia, el del mapa y la chapa
     * de la ranura. Un banco puede decir que los cinco tienen sitio y aun
     * asi quedar apretado, y eso no lo mide un numero.
     */
    {
        ui_digi_state_t f;

        memset(&f, 0, sizeof f);
        f.y = PANEL_Y; f.h = PANEL_H;
        for (i = 0U; i < UDG_ROWS; i++) { f.fila[i] = k_qso[i]; }
        f.btn = "Borrar";
        f.barra_dcha = 1U;
        f.barra_on = 1U; f.barra_w = 0;
        f.barra_v = 8U; f.barra_max = 15U;
        f.barra_txt = "24c \xC2\xB7 +0,2 s";
        f.barra_txt_dcha = "8/15";
        f.btn3 = "CQ"; f.btn3_press = 1U;
        f.btn4 = "7.074";
        f.btn5 = "Mapa"; f.btn5_press = 1U;
        f.chip = 0; f.enganchado = 1U;

        lcd_sim_reset(0x0000);
        gfx2_render_screen(fondo, 0);
        ui_digi_draw_texto(&f);
        ui_digi_draw_chip(&f);
        ui_digi_draw_boton(&f);
        lcd_sim_write_ppm("out/digi_ft8.ppm");
        printf("\n  la cabecera de FT8 con el boton de CQ: out/digi_ft8.ppm\n");
    }

    /*
     * ==================================================================
     * UN DIBUJO QUE NO SE HACE TIENE QUE DECIRLO - 13/10/2026
     * ==================================================================
     *
     * *** Por el fallo conocido: "el boton Borrar del panel de CW
     * desaparece despues de mucho rato decodificando". ***
     *
     * gfx2 compone en UNA banda que se coge y se suelta. Si esta cogida
     * -la captura de pantalla se la queda veinticinco segundos- el dibujo
     * no se hace. Eso siempre fue asi; lo que estaba mal es que no se
     * notaba: ui_digi_draw_texto() era void, quien la llamaba bajaba su
     * bandera de "hay que repintar el panel entero" y el panel se quedaba
     * con lo que hubiera debajo hasta el siguiente cambio de modo.
     *
     * Aqui se mide LO QUE SE PUEDE MEDIR desde el simulador: que la
     * funcion dice la verdad. Que quien la llama la mire lo comprueba
     * tools/repinta_check.py leyendo main.c, que es donde vive esa parte
     * y donde ningun banco llega.
     */
    {
        ui_digi_state_t d;
        uint16_t *banda;
        uint8_t  con, sin;

        memset(&d, 0, sizeof d);
        d.y = PANEL_Y; d.h = PANEL_H;
        d.btn = "Borrar";
        for (i = 0U; i < UDG_ROWS; i++) { d.fila[i] = k_qso[i]; }

        printf("\n  el panel dice si de verdad ha pintado\n");

        con = ui_digi_draw_texto(&d);

        /* Y ahora con la banda cogida por otro, que es lo que hace la
         * captura de pantalla durante veinticinco segundos. */
        banda = gfx2_banda_coge();
        sin   = ui_digi_draw_texto(&d);
        if (banda != 0) { gfx2_banda_suelta(); }

        printf("    con la banda libre devuelve %u, con la banda cogida %u\n",
               (unsigned)con, (unsigned)sin);
        if (banda == 0 || con != 1U || sin != 0U) {
            printf("    *** MAL: un dibujo que no se hace se esta callando ***\n");
            fallos++;
        } else {
            printf("    sin poder pintar lo DICE en vez de callarse        ok\n");
        }
        /* Y que al soltarla vuelve a pintar: un cero no puede dejar el
         * panel inutilizado para siempre. */
        if (ui_digi_draw_texto(&d) != 1U) {
            printf("    *** MAL: al soltar la banda ya no pinta ***\n");
            fallos++;
        } else {
            printf("    y al soltar la banda vuelve a pintar               ok\n");
        }
    }

    lcd_sim_write_ppm(pulsado ? "out/digi1.ppm" : "out/digi0.ppm");
    printf("  %s\n", fallos ? "HAY FALLOS" : "todo cabe y el boton responde donde se dibuja");
    return fallos ? 1 : 0;
}
