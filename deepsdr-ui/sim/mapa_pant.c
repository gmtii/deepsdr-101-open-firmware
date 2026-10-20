/*
 * EL MAPA DEL MUNDO, PARA MIRARLO ANTES DE GASTARLE UN FLASHEO.
 *
 * Un mapa no se comprueba compilando. Esto lo pinta en un fichero que se
 * puede abrir, con estaciones de verdad repartidas por el mundo, y ademas
 * mide lo que mirando NO se ve:
 *
 *   - que un localizador conocido cae donde tiene que caer, en pixeles,
 *     con la tolerancia de medio cuadrado;
 *   - que la costa NO lleva ninguna raya que cruce el mapa entero, que es
 *     lo que pasa siempre la primera vez con el meridiano 180;
 *   - que un localizador imposible no pinta nada en vez de pintar un
 *     punto en mitad del oceano.
 */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "mapa.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos;

static void exige(int cond, const char *que)
{
    printf("    %-58s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

/* OJO: la constante de altura NO se puede llamar MAPA_H. User/mapa.h usa ese
 * mismo nombre como guarda de inclusion, asi que definirlo aqui funciona solo
 * mientras mapa.h se incluya ANTES; el dia que alguien reordene los #include,
 * mapa.h se salta entera y en silencio. Por eso lleva prefijo. */
#define MAPA_X   0
#define MAPA_Y   0
#define MAPA_W   800
#define PANT_H   318

/*
 * La caja, puesta con mapa_pon() y no escrita a mano: desde el 28/09/2026
 * lleva escala y desplazamiento, y un literal de cuatro campos los dejaria
 * a cero - que funciona por casualidad, porque el codigo trata la escala 0
 * como 1-. Un banco que depende de una casualidad deja de medir el dia que
 * la casualidad cambia.
 */
static mapa_caja_t k_caja;

/* Estaciones de verdad, repartidas a proposito por los sitios donde un
 * mapa plano se rompe: los dos bordes, el ecuador y las dos esquinas. */
static const struct { const char *loc; const char *quien; uint8_t viva; } k_est[] = {
    { "IN80",   "EA4 (Madrid)",        1U },
    { "JO20HI", "ON4WS (Belgica)",     1U },
    { "IO90",   "G4VNM (Inglaterra)",  1U },
    { "JO01FJ", "G6CKK (Kent)",        0U },
    { "FN42",   "K1 (Boston)",         1U },
    { "PM95",   "JA1 (Tokio)",         0U },
    { "RF80",   "ZL (Nueva Zelanda)",  0U },
    { "FF95",   "LU (Buenos Aires)",   1U },
    { "KM56",   "SV (Creta)",          0U },
    { "OF88",   "VK6 (Perth)",         0U },
    { "BP51",   "KL7 (Alaska)",        1U },
    { "AP50",   "cerca del borde O",   0U },
    { "RP50",   "cerca del borde E",   0U },
    { "JJ00",   "ecuador y Greenwich", 1U }
};

/* Una caja mas pequeña que la pantalla, para ver si el dibujo se sale. */
static mapa_caja_t g_caja_chica;

static void pinta_chica(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    /*
     * El fondo se pinta AQUI DENTRO y no con lcd_sim_reset(): gfx2_render
     * le da a cada banda un buffer limpio y la vuelca entera, asi que lo
     * que hubiera antes en la pantalla no sobrevive. La primera version de
     * esta prueba pintaba el fondo fuera y contaba los 204.000 pixeles de
     * alrededor como "pintados por el mapa" en las cuatro escalas: un
     * banco que falla siempre no mide nada.
     */
    gfx2_fill(s, 0, 0, GFX2_W, GFX2_H, gfx2_rgb(0xFF00FFUL));
    mapa_dibuja(s, &g_caja_chica);
}

static void pinta(gfx2_surf_t *s, void *ctx)
{
    unsigned i;
    int16_t px, py;

    (void)ctx;
    mapa_dibuja(s, &k_caja);
    mapa_zoom_botones(s, &k_caja);
    for (i = 0; i < sizeof k_est / sizeof k_est[0]; i++) {
        if (mapa_loc_a_px(&k_caja, k_est[i].loc, &px, &py)) {
            mapa_marca(s, &k_caja, px, py, k_est[i].viva,
                       gfx2_rgb(k_est[i].viva ? 0x46E0A0UL : 0x2E8C68UL));
        }
    }
    if (mapa_loc_a_px(&k_caja, "IN80", &px, &py)) {
        mapa_casa(s, &k_caja, px, py);
    }
}

/* El hueco de verdad en la radio, para las pruebas de zoom: el panel de
 * FT8 menos su cabecera. Ver FT8_MAPA_Y/FT8_MAPA_H en main.c. */
#define RADIO_Y  134
#define RADIO_H  288

int main(void)
{
    mapa_pon(&k_caja, MAPA_X, MAPA_Y, MAPA_W, PANT_H);

    printf("\n  EL MAPA DEL MUNDO\n");

    printf("\n  la tabla de la costa\n");
    printf("    %u lineas, %u puntos, %u bytes en el desvan\n",
           (unsigned)k_mapa_n_lineas, (unsigned)k_mapa_n_puntos,
           (unsigned)(4U * k_mapa_n_puntos + 2U * k_mapa_n_lineas));
    exige(k_mapa_n_lineas > 50U && k_mapa_n_puntos > 1000U,
          "la costa esta y tiene detalle");

    printf("\n  donde cae cada sitio\n");
    {
        /*
         * Sitios comprobables a mano. El pixel esperado sale de la
         * proyeccion, que es una regla de tres - y por eso se comprueba:
         * si alguien cambia el recorte de latitud o el ancho, esto se
         * cae en vez de salir un mapa sutilmente torcido.
         */
        static const struct { const char *loc; double lon, lat; const char *q; } k[] = {
            { "JJ00", 1.0,  0.5, "JJ00: ecuador, al este de Greenwich" },
            { "IN80", -3.0, 40.5, "IN80: Madrid (centro del cuadrado)" },
            { "FN42", -71.0, 42.5, "FN42: Boston" },
            { "PM95", 139.0, 35.5, "PM95: Tokio" },
            { "AA00", -179.0, -89.5, "AA00: la esquina suroeste" },
            { "RR99", 179.0, 89.5, "RR99: la esquina noreste" }
        };
        unsigned i;

        for (i = 0; i < sizeof k / sizeof k[0]; i++) {
            int16_t px = 0, py = 0;
            int16_t ex, ey;
            double lat = k[i].lat;
            double tope = k_mapa_lat_tope_c / 100.0;
            int ok;

            if (lat >  tope) { lat =  tope; }
            if (lat < -tope) { lat = -tope; }
            ex = (int16_t)(MAPA_X + (k[i].lon + 180.0) * MAPA_W / 360.0);
            ey = (int16_t)(MAPA_Y + (tope - lat) * PANT_H / (2.0 * tope));
            ok = mapa_loc_a_px(&k_caja, k[i].loc, &px, &py);
            printf("    %-42s (%3d,%3d) esperado (%3d,%3d)\n",
                   k[i].q, px, py, ex, ey);
            exige(ok && px >= ex - 3 && px <= ex + 3
                     && py >= ey - 3 && py <= ey + 3, k[i].q);
        }
    }

    printf("\n  posiciones en grados por diez mil (HFDL y AIS)\n");
    {
        /*
         * Los aviones de HFDL y los barcos de AIS no dan localizador: dan
         * coordenadas medidas, en grados por diez mil. Se comprueba que
         * caen en el mismo pixel que el localizador equivalente -si no,
         * un avion y una estacion de FT8 en el mismo sitio saldrian en
         * sitios distintos- y que lo que cae fuera del recorte se
         * rechaza en vez de pintarse pegado al borde.
         */
        static const struct { int32_t lon, lat; int fuera; const char *q; } k[] = {
            {  -37000,  404000, 0, "Madrid (-3,70 / 40,40)" },
            {       0,       0, 0, "el cruce del ecuador con Greenwich" },
            { 1399000,  356000, 0, "Tokio" },
            { -740000,  406000, 0, "Nueva York" },
            {       0,  850000, 1, "el polo norte queda fuera del recorte" },
            {       0, -850000, 1, "y el sur tambien" }
        };
        unsigned i;

        for (i = 0; i < sizeof k / sizeof k[0]; i++) {
            int16_t px = -1, py = -1;
            uint8_t hay = mapa_e4_a_px(&k_caja, k[i].lon, k[i].lat, &px, &py);

            if (k[i].fuera) {
                exige(!hay, k[i].q);
            } else {
                int16_t ex = (int16_t)(MAPA_X + (k[i].lon / 10000.0 + 180.0)
                                       * MAPA_W / 360.0);
                double tope = k_mapa_lat_tope_c / 100.0;
                int16_t ey = (int16_t)(MAPA_Y + (tope - k[i].lat / 10000.0)
                                       * PANT_H / (2.0 * tope));

                exige(hay && px >= ex - 1 && px <= ex + 1
                          && py >= ey - 1 && py <= ey + 1, k[i].q);
            }
        }

        /* Y que coincide con el localizador del mismo sitio. */
        {
            int16_t ax, ay, bx, by;

            /* JO20HI, el cuadrado de ON4WS: 4,29 E / 50,35 N mas o menos */
            (void)mapa_loc_a_px(&k_caja, "JO20HI", &ax, &ay);
            (void)mapa_e4_a_px(&k_caja, 42900, 503500, &bx, &by);
            exige(ax >= bx - 2 && ax <= bx + 2 && ay >= by - 2 && ay <= by + 2,
                  "un localizador y sus coordenadas caen en el mismo sitio");
        }
    }

    printf("\n  y lo que NO puede pasar\n");
    {
        int16_t px, py;

        exige(!mapa_loc_a_px(&k_caja, "ZZ99", &px, &py),
              "un localizador imposible no pinta nada");
        exige(!mapa_loc_a_px(&k_caja, "IN8", &px, &py),
              "tres caracteres tampoco");
        exige(!mapa_loc_a_px(&k_caja, "IN80X", &px, &py),
              "ni cinco");
        exige(mapa_loc_a_px(&k_caja, "in80", &px, &py),
              "pero minusculas si");
    }

    printf("\n  sacar el localizador de un renglon de FT8\n");
    {
        /*
         * Los renglones de verdad del panel: Hora, Hz, dB, DT, Mensaje,
         * km, Pais, separados por tabulador.
         */
        static const struct { const char *l; const char *esp; const char *q; } k[] = {
            { "07:15\t1240\t-08\t+0,2\tCQ EA4XYZ IN80\t0\tEspaña", "IN80",
              "una CQ con localizador" },
            { "07:15\t1240\t-08\t+0,2\tCQ DX G0ABC IO91\t1450\tInglaterra", "IO91",
              "una CQ DX, que lleva una palabra mas" },
            { "07:15\t1240\t-08\t+0,2\tEA4XYZ G0ABC JO20HI\t1450\tBelgica", "JO20HI",
              "un localizador de seis" },
            { "07:15\t1240\t-08\t+0,2\tEA4XYZ G0ABC RR73\t1450\tInglaterra", "",
              "RR73 NO es un localizador, es como acaba el contacto" },
            { "07:15\t1240\t-08\t+0,2\tEA4XYZ G0ABC RR72\t1450\tInglaterra", "",
              "RR72 tampoco" },
            { "07:15\t1240\t-08\t+0,2\tEA4XYZ G0ABC -12\t1450\tInglaterra", "",
              "un informe de señal tampoco" },
            { "07:15\t1240\t-08\t+0,2\tEA4XYZ G0ABC R-08\t1450\tInglaterra", "",
              "ni un informe con R delante" },
            { "07:15\t1240\t-08\t+0,2\tEA4XYZ G0ABC 73\t1450\tInglaterra", "",
              "ni un 73 suelto" },
            { "07:15\t1240\t-08\t+0,2\tCQ EA4XYZ\t0\tEspaña", "",
              "una CQ sin localizador no inventa uno" },
            { "07:15\t1240", "", "un renglon a medias no revienta" },
            { "", "", "y uno vacio tampoco" }
        };
        unsigned i;

        for (i = 0; i < sizeof k / sizeof k[0]; i++) {
            char out[8];
            uint8_t hay = mapa_loc_de_linea(k[i].l, out);

            exige((k[i].esp[0] == '\0') ? (hay == 0U)
                                        : (hay != 0U && strcmp(out, k[i].esp) == 0),
                  k[i].q);
        }
    }

    printf("\n  la raya que cruza el mapa entero (meridiano 180)\n");
    {
        /*
         * EN UN MAPA PLANO, una costa que pasa por el meridiano 180
         * tiene un punto en el borde derecho y el siguiente en el
         * izquierdo. Unirlos pinta una raya horizontal de lado a lado
         * que se lee como un fallo del dibujo.
         *
         * Con ESTOS datos no pasa: Natural Earth ya trae esas costas
         * partidas en dos lineas. O sea que el corte de mapa.c es un
         * seguro, no un arreglo - y conviene que quede escrito, porque
         * si no, el dia que alguien cambie de fuente de datos y salga la
         * raya, no habra nada que diga que esto ya se penso.
         *
         * Asi que se comprueban las dos cosas: que los datos de ahora no
         * lo necesitan, y -mirando el dibujo de verdad, no la tabla- que
         * no ha salido ninguna raya larga.
         */
        const int16_t *p = k_mapa_pts;
        unsigned linea, largos = 0, trazos = 0;

        for (linea = 0; linea < k_mapa_n_lineas; linea++) {
            int16_t n = *p++;
            int16_t k;
            int32_t alon = 0;

            for (k = 0; k < n; k++) {
                int32_t lon = *p++;
                (void)*p++;
                if (k > 0) {
                    int32_t d = (lon > alon) ? (lon - alon) : (alon - lon);

                    trazos++;
                    if (d >= 18000L) { largos++; }
                }
                alon = lon;
            }
        }
        printf("      %u trazos, %u saltan de un borde al otro\n", trazos, largos);
        exige(largos == 0,
              "estos datos vienen ya partidos por el meridiano 180");
    }

    /*
     * ------------------------------------------------------------------
     * ZOOM Y ARRASTRE - 28/09/2026
     *
     * ------------------------------------------------------------------
     */
    /*
     * EL LOCALIZADOR DE JTTY: EN CUALQUIER PALABRA DEL MENSAJE.
     *
     * En FT8 el localizador es siempre la ultima palabra y por eso
     * mapa_loc_de_linea() mira solo esa. En JTTY un mensaje son varios
     * atomos pegados y el localizador viene en el suyo, que puede caer en
     * medio: "CQ K1ABC CQ FN42 599 123". Con la regla de FT8, el mapa se
     * quedaria vacio justo en los mensajes que si dicen donde esta el otro.
     *
     * Y el campo tambien cambia: las columnas de JTTY son cuatro
     * (Hora, Desvio, Cal., Mensaje) y las de FT8 siete.
     */
    printf("\n  el localizador dentro de un mensaje de JTTY\n");
    {
        static const struct { const char *linea, *esp; } k[] = {
            { "23:59\t+12 Hz\t88\tCQ K1ABC CQ FN42 599 123", "FN42" },
            { "23:59\t+12 Hz\t88\tFN42",                      "FN42" },
            { "23:59\t+12 Hz\t88\tCQ K1ABC CQ JO20HI",        "JO20HI" },
            { "23:59\t+12 Hz\t88\tCQ K1ABC CQ",               ""       },
            { "23:59\t+12 Hz\t88\t599 123 RR73",              ""       },
            { "23:59\t+12 Hz\t88\t",                          ""       },
            { "23:59\t+12 Hz\t88\tHELLO IN80 AGN?",           "IN80"   }
        };
        unsigned i, bien = 0U;

        for (i = 0U; i < sizeof k / sizeof k[0]; i++) {
            char loc[8];
            uint8_t hay = mapa_loc_de_campo(k[i].linea, 4U, loc);

            if (!hay) { loc[0] = '\0'; }
            if (strcmp(loc, k[i].esp) == 0) { bien++; }
            else { printf("      \"%s\" -> \"%s\", deberia \"%s\"   MAL\n",
                          k[i].linea, loc, k[i].esp); }
        }
        exige(bien == sizeof k / sizeof k[0],
              "lo encuentra este donde este, y RR73 sigue sin contar");

        /* Y que NO se le haya cambiado la regla a FT8 de paso: alli el
         * localizador es la ultima palabra y solo esa. */
        {
            char loc[8];
            const char *ft8 =
                "05:17\t0263\t+12\t+0,2\tCQ AI0Y EN08 KM39\t4191\tTurquia";

            exige(mapa_loc_de_linea(ft8, loc) && strcmp(loc, "KM39") == 0,
                  "y FT8 sigue cogiendo la ULTIMA, que es su regla");
        }
    }

    /*
     * EL LOCALIZADOR DE WSPR: EN SU PROPIA COLUMNA.
     *
     * Aqui es el caso facil de los tres y conviene que se vea por que: las
     * columnas de WSPR son Hora, Indicativo, Loc., dBm, Desvio y Cal., o
     * sea que el localizador tiene COLUMNA PROPIA -el campo 3- y no hay
     * que adivinarlo. Lo que si hay que aguantar es que VENGA VACIO: los
     * mensajes de WSPR de tipo 2 llevan indicativo y potencia y nada mas.
     *
     * Ese renglon no tiene que poner punto, y sobre todo no tiene que
     * coger el campo de al lado: si la columna vacia dejara pasar el
     * "dBm", un +30 se leeria como cualquier cosa. Por eso esta la prueba
     * de "" y las de los numeros.
     */
    printf("\n  el localizador de WSPR, en su columna\n");
    {
        static const struct { const char *linea, *esp; } k[] = {
            /* lo normal: cuatro caracteres */
            { "05:17\tEA4XYZ\tIN80\t+37\t+12,3\t88", "IN80" },
            { "05:17\tG0ABC\tIO91\t+23\t-04,1\t72",  "IO91" },
            /* de seis, que tambien los hay */
            { "05:17\tEA4XYZ\tIN80DJ\t+37\t+12,3\t88", "IN80DJ" },
            /* y SIN localizador: la columna viene vacia */
            { "05:17\tEA4XYZ\t\t+37\t+12,3\t88", "" },
            /* un indicativo con barra en la columna del indicativo no
             * tiene que confundirse con un localizador */
            { "05:17\tEA4XYZ/P\t\t+37\t+12,3\t88", "" },
            /* ni el renglon entero vacio */
            { "05:17\t\t\t\t\t", "" }
        };
        unsigned i, bien = 0U;

        for (i = 0U; i < sizeof k / sizeof k[0]; i++) {
            char loc[8];
            uint8_t hay = mapa_loc_de_campo(k[i].linea, 3U, loc);

            if (!hay) { loc[0] = '\0'; }
            if (strcmp(loc, k[i].esp) == 0) { bien++; }
            else { printf("      \"%s\" -> \"%s\", deberia \"%s\"   MAL\n",
                          k[i].linea, loc, k[i].esp); }
        }
        exige(bien == sizeof k / sizeof k[0],
              "coge el campo 3, y si viene vacio no coge el de al lado");

        /*
         * Y QUE NO SE COJA EL DE OTRA COLUMNA. Esto es lo que de verdad
         * puede fallar al reutilizar la funcion: un dBm o un desvio no
         * pueden parecer un localizador. Se prueba pidiendo esos campos a
         * proposito.
         */
        {
            char loc[8];
            const char *l = "05:17\tEA4XYZ\tIN80\t+37\t+12,3\t88";

            exige(!mapa_loc_de_campo(l, 4U, loc), "el dBm no es un localizador");
            exige(!mapa_loc_de_campo(l, 5U, loc), "ni el desvio");
            exige(!mapa_loc_de_campo(l, 6U, loc), "ni la calidad");
            exige(!mapa_loc_de_campo(l, 2U, loc), "ni el indicativo");
        }
    }

    printf("\n  el hueco de la radio: 800 x 288\n");
    {
        mapa_caja_t c;
        int16_t px, py;

        mapa_pon(&c, 0, RADIO_Y, 800, RADIO_H);
        printf("      el mundo mide %ld x %ld y se ven %d x %d\n",
               (long)mapa_mundo_w(&c), (long)mapa_mundo_h(&c), c.w, c.h);
        exige(mapa_mundo_w(&c) == 800,
              "el mapa ocupa los 800: no quedan franjas laterales");
        exige(mapa_mundo_h(&c) > RADIO_H,
              "y sobra alto, que es por donde se recorta ahora");
        exige(c.off_y == (int16_t)((mapa_mundo_h(&c) - RADIO_H) / 2),
              "el recorte se reparte arriba y abajo, no en un polo");

        /*
         * LO QUE SE PROMETIO: que el norte de Escandinavia no se PIERDE,
         * solo hay que ir a buscarlo. A escala 1 queda fuera del hueco, y
         * arrastrando hacia abajo tiene que entrar.
         */
        exige(!mapa_loc_a_px(&c, "KP27", &px, &py) || py < c.y,
              "a escala 1, Tromso (KP27) se queda fuera por arriba");
        mapa_arrastra(&c, 0, 60);        /* el dedo baja: entra lo de arriba */
        exige(mapa_loc_a_px(&c, "KP27", &px, &py) && py >= c.y && py < c.y + c.h,
              "y arrastrando hacia abajo aparece");
        exige(c.off_y == 0,
              "el arrastre se para en el borde del mundo, no sigue");
    }

    printf("\n  el zoom\n");
    {
        mapa_caja_t c;
        int16_t ax, ay, bx, by, cx, cy;

        mapa_pon(&c, 0, RADIO_Y, 800, RADIO_H);
        (void)mapa_loc_a_px(&c, "IN80", &ax, &ay);

        /*
         * El ancla: se acerca sobre el punto donde esta Madrid y Madrid
         * tiene que quedarse donde estaba. Si el zoom entrara por el
         * centro, lo que estabas mirando se iria de la pantalla.
         */
        exige(mapa_zoom(&c, 2U, ax, ay) == 2U, "x2 entra");
        (void)mapa_loc_a_px(&c, "IN80", &bx, &by);
        printf("      IN80 estaba en (%d,%d) y al x2 esta en (%d,%d)\n",
               ax, ay, bx, by);
        exige(bx >= ax - 2 && bx <= ax + 2 && by >= ay - 2 && by <= ay + 2,
              "lo que estabas mirando no se mueve al acercar");

        exige(mapa_zoom(&c, 1U, ax, ay) == 1U, "y se puede volver");
        (void)mapa_loc_a_px(&c, "IN80", &cx, &cy);
        exige(cx >= ax - 2 && cx <= ax + 2 && cy >= ay - 2 && cy <= ay + 2,
              "ir y volver deja el mapa donde estaba");

        exige(mapa_zoom(&c, 1U, ax, ay) == 1U, "y no se aleja mas de x1");
        exige(mapa_zoom(&c, 99U, ax, ay) == (uint8_t)MAPA_ESC_MAX,
              "ni se acerca mas del tope");

        /*
         * ==============================================================
         * Y EL ZOOM A PASOS, QUE ES EL QUE USAN EL MANDO Y LOS BOTONES
         * ==============================================================
         *
         * mapa_zoom_paso() existe para que el mando y los botones de la
         * esquina NO tengan cada uno su copia de "multiplica por dos y
         * topa". Lo que se mide aqui es justo eso: que la misma cantidad
         * de pasos lleve siempre a la misma escala, venga de donde venga,
         * y que en los topes no se acumule nada.
         *
         * Lo ultimo importa mas de lo que parece: si al topar en x8 se
         * guardaran los giros de mas, habria que deshacerlos antes de que
         * el mapa reaccionara, y desde fuera eso se vive como un mando
         * que se ha quedado colgado.
         */
        {
            mapa_caja_t p;

            mapa_pon(&p, MAPA_X, MAPA_Y, MAPA_W, PANT_H);
            exige(mapa_zoom_paso(&p, 1, ax, ay) == 2U, "un paso: x1 -> x2");
            exige(mapa_zoom_paso(&p, 1, ax, ay) == 4U, "otro:    x2 -> x4");
            exige(mapa_zoom_paso(&p, -1, ax, ay) == 2U, "y atras: x4 -> x2");

            mapa_pon(&p, MAPA_X, MAPA_Y, MAPA_W, PANT_H);
            exige(mapa_zoom_paso(&p, 3, ax, ay) == 8U, "tres de golpe: x1 -> x8");
            exige(mapa_zoom_paso(&p, 9, ax, ay) == (uint8_t)MAPA_ESC_MAX,
                  "y nueve mas no pasan del tope");
            /* EL QUE DE VERDAD IMPORTA: despues de pasarse, UN solo paso
             * hacia atras tiene que notarse. */
            exige(mapa_zoom_paso(&p, -1, ax, ay) == 4U,
                  "y un paso atras desde el tope baja de verdad");

            mapa_pon(&p, MAPA_X, MAPA_Y, MAPA_W, PANT_H);
            exige(mapa_zoom_paso(&p, -9, ax, ay) == 1U, "por abajo topa en x1");
            exige(mapa_zoom_paso(&p, 1, ax, ay) == 2U,
                  "y un paso adelante desde el tope sube de verdad");
            exige(mapa_zoom_paso(&p, 0, ax, ay) == 2U, "cero pasos no mueve nada");

            /* Y QUE LOS DOS CAMINOS COINCIDAN: cuatro toques del boton
             * tienen que dejar la misma escala que cuatro detentes. */
            {
                mapa_caja_t a, b;
                int k;

                mapa_pon(&a, MAPA_X, MAPA_Y, MAPA_W, PANT_H);
                mapa_pon(&b, MAPA_X, MAPA_Y, MAPA_W, PANT_H);
                for (k = 0; k < 4; k++) { (void)mapa_zoom_paso(&a, 1, ax, ay); }
                (void)mapa_zoom_paso(&b, 4, ax, ay);
                exige(a.esc == b.esc, "cuatro de uno en uno == cuatro de golpe");
            }
        }
    }

    /*
     * QUE EL MAPA NO SE SALGA DE SU CAJA, AL ZOOM QUE SEA.
     *
     * Esto es lo que de verdad hay que medir del recorte. gfx2_line
     * recorta contra la BANDA que se esta componiendo, no contra la caja
     * del mapa: mientras el mapa ocupo el hueco entero eran lo mismo y no
     * se noto, pero con zoom hay mundo de sobra por los cuatro lados y un
     * trazo de costa que entra y sale de la caja se pintaria tambien por
     * fuera, encima de lo que hubiera al lado.
     *
     * Asi que se pinta una caja MAS PEQUEÑA que la pantalla, con el fondo
     * de un color que el mapa no usa, y se cuenta cuantos pixeles de fuera
     * han cambiado. Tienen que ser cero, a las cuatro escalas y con el
     * mapa movido a mala idea.
     */
    /*
     * LOS DOS MANDOS: que esten donde se dijo y que respondan donde estan.
     *
     * Lo segundo es lo que importa. El dibujo y el reparto de toques usan
     * la misma geometria por construccion -las dos funciones viven en
     * mapa.c y preguntan a las mismas dos estaticas-, pero eso es un
     * argumento, y un argumento no es una medida: aqui se toca en el
     * centro de cada mando y se comprueba que contesta el que es.
     */
    printf("\n  los dos mandos del zoom\n");
    {
        mapa_caja_t c;
        int16_t y;

        mapa_pon(&c, 0, RADIO_Y, 800, RADIO_H);
        y = (int16_t)(c.y + c.h - 8 - MAPA_MZ_LADO / 2);
        /*
         * 30/09/2026: el orden se invirtio por el usuario - el MENOS a la
         * izquierda y el MAS a la derecha, como en ui_det.c, en la pantalla
         * de QTH y en cualquier escala. Este banco defendia el orden viejo
         * y fallo al cambiarlo, que es justo lo que tenia que hacer: el
         * dibujo y el toque salen de la misma mz_x(), asi que si alguien
         * invierte uno solo, esto lo canta.
         */
        exige(mapa_zoom_hit(&c, (int16_t)(c.x + 8 + MAPA_MZ_LADO / 2), y) == 2U,
              "el de abajo a la izquierda del todo ALEJA");
        exige(mapa_zoom_hit(&c, (int16_t)(c.x + 8 + MAPA_MZ_LADO + 8
                                          + MAPA_MZ_LADO / 2), y) == 1U,
              "y el de al lado ACERCA");
        exige(mapa_zoom_hit(&c, (int16_t)(c.x + c.w / 2), (int16_t)(c.y + c.h / 2)) == 0U,
              "en medio del mapa no hay mando: eso es arrastrar");
        exige(mapa_zoom_hit(&c, (int16_t)(c.x + 8 + MAPA_MZ_LADO / 2),
                            (int16_t)(c.y + 20)) == 0U,
              "ni arriba del todo, aunque coincida la columna");
        /* Y que quepan dentro del mapa: un mando medio fuera no se toca. */
        exige((int16_t)(c.y + c.h - 8 - MAPA_MZ_LADO) >= c.y
              && (int16_t)(8 + 2 * MAPA_MZ_LADO + 8) < c.w,
              "los dos caben dentro del hueco");
    }

    printf("\n  el dibujo no se sale de la caja\n");
    {
        static const uint8_t k_esc[] = { 1U, 2U, 4U, 8U };
        unsigned e;
        const uint16_t k_fondo = 0xF81F;      /* magenta: no esta en el mapa */
        long peor = 0;

        for (e = 0U; e < sizeof k_esc; e++) {
            const uint16_t *fb;
            int x, y;
            long fuera = 0;

            mapa_pon(&g_caja_chica, 100, 60, 600, 300);
            (void)mapa_zoom(&g_caja_chica, k_esc[e], 400, 210);
            /* Movido a mala idea: con el mapa centrado, al zoom alto lo
             * que se ve es oceano y no se sale nada aunque este roto. */
            mapa_arrastra(&g_caja_chica, -220, 90);

            lcd_sim_reset(0x0000);
            gfx2_render(0, 0, GFX2_W, GFX2_H, pinta_chica, 0);
            fb = lcd_sim_fb();
            for (y = 0; y < GFX2_H; y++) {
                for (x = 0; x < GFX2_W; x++) {
                    if (x >= 100 && x < 700 && y >= 60 && y < 360) { continue; }
                    if (fb[y * GFX2_W + x] != k_fondo) { fuera++; }
                }
            }
            printf("      x%u: %ld pixeles pintados fuera de la caja\n",
                   (unsigned)k_esc[e], fuera);
            if (fuera > peor) { peor = fuera; }
        }
        /* Una sola linea de veredicto, y que diga la verdad: la primera
         * version ponia exige(1, ...) y contaba los fallos aparte, o sea
         * que imprimia "bien" mientras sumaba fallos. */
        exige(peor == 0, "ni un pixel fuera, a ninguna de las cuatro escalas");
        /* Y una imagen del zoom para mirarla. */
        mapa_pon(&g_caja_chica, 0, RADIO_Y, 800, RADIO_H);
        (void)mapa_zoom(&g_caja_chica, 4U, 400, (int16_t)(RADIO_Y + RADIO_H / 2));
        lcd_sim_reset(0x0000);
        gfx2_render(0, 0, GFX2_W, GFX2_H, pinta_chica, 0);
        lcd_sim_write_ppm("out/mapa_zoom4.ppm");
    }

    lcd_sim_reset(0x0000);
    gfx2_render(MAPA_X, MAPA_Y, MAPA_W, PANT_H, pinta, 0);
    lcd_sim_write_ppm("out/mapa_mundo.ppm");
    printf("\n  pintado en out/mapa_mundo.ppm\n");

    printf("\n  y en el dibujo de verdad: ninguna raya larga\n");
    {
        /*
         * Se mira el fotograma pixel a pixel buscando rachas
         * horizontales del color de la costa. La rejilla es vertical y
         * los paralelos son de otro color, asi que una racha larga de
         * color de costa solo puede ser la raya del meridiano - o una
         * costa de verdad que fuera perfectamente horizontal y midiera
         * medio mundo, que no existe.
         */
        const uint16_t *fb = lcd_sim_fb();
        uint16_t costa = gfx2_to565(0x3E, 0x7F, 0xA8);
        int y, x, peor = 0;

        for (y = MAPA_Y; y < MAPA_Y + PANT_H; y++) {
            int racha = 0;

            for (x = MAPA_X; x < MAPA_X + MAPA_W; x++) {
                if (fb[y * GFX2_W + x] == costa) {
                    racha++;
                    if (racha > peor) { peor = racha; }
                } else {
                    racha = 0;
                }
            }
        }
        printf("      la racha horizontal de costa mas larga mide %d px\n", peor);
        exige(peor < 120, "ninguna raya cruza el mapa");
    }

    /*
     * LO QUE CUESTA UN SOLO FOTOGRAMA DE ARRASTRE - 11/10/2026.
     *
     * Cada movimiento del dedo repinta el mapa ENTERO, y ese repintado es
     * lo que marca cada cuanto se vuelve a leer el tactil: el bucle
     * principal lee el dedo UNA vez por vuelta. O sea que este numero no
     * es un lujo de rendimiento, es el HUECO ENTRE LECTURAS - y el hueco
     * entre lecturas es lo que decide cuantos pixeles ha recorrido el
     * dedo de una a otra, que es justo lo que el filtro de touch.c mide
     * para creerse o no el contacto. Ver sim/tactil_arrastre.c.
     */
    printf("\n  lo que cuesta repintar el mapa una vez\n");
    {
        lcd_cost_t c;

        lcd_sim_reset(0x0000);
        lcd_sim_cost_reset();
        gfx2_render(MAPA_X, MAPA_Y, MAPA_W, PANT_H, pinta, 0);
        c = lcd_sim_cost();

        printf("      %llu accesos al bus, %llu pixeles, %llu ventanas\n",
               (unsigned long long)c.bus_writes,
               (unsigned long long)c.pixels,
               (unsigned long long)c.windows);
        printf("      con los tiempos lentos del EXMC (21 ciclos a 200 MHz): %.1f ms\n",
               lcd_sim_est_us(21, 200.0) / 1000.0);
        printf("      con los rapidos (6 ciclos): %.1f ms\n",
               lcd_sim_est_us(6, 200.0) / 1000.0);
        printf("      y eso es SOLO pintar: la vuelta del bucle lleva ademas\n"
               "      audio, demodulacion y el resto del despacho.\n");
        exige(c.bus_writes > 0, "el repintado del mapa se ha medido");
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
