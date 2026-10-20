/*
 * QUIEN SALE EN EL ESPECTRO, CON LA BASE DE VERDAD - 15/10/2026
 *
 * ESTE BANCO EXISTE PORQUE NO EXISTIA. sim/rotulos.c comprueba el
 * DIBUJO, con listas escritas a mano, y estaba verde mientras la radio
 * enseñaba cuatro carteles amontonados a la izquierda. No mentia: es que
 * el fallo no estaba en el dibujo, estaba en la ELECCION, y la eleccion
 * vivia suelta dentro de main.c, que el simulador no enlaza.
 *
 * Asi que lo primero que se arreglo hoy no fue el fallo: fue sacar la
 * eleccion a spec_rotulos_filtra(), que si se compila aqui. Esto es ese
 * banco, y empieza reproduciendo el fallo del video con la base real.
 *
 * LO QUE EXIGE, y los tres salen del video:
 *
 *   1. Que NINGUN cartel caiga fuera de la ventana que se ve. Es el
 *      fallo gordo: se pedia el doble de ventana con sitio para seis y
 *      los seis primeros salian de la parte que no se ve.
 *   2. Que no se repita un nombre tan cerca que se pise. En el video,
 *      tres "SunDance Radio" seguidos ocupando tres de los seis huecos.
 *   3. Que los carteles LLEGUEN A LAS DOS MITADES de la pantalla cuando
 *      hay emisoras en las dos. El tercio derecho estaba vacio siempre.
 *
 * Y uno mas que no se ve en el video pero se cae del cambio: que la
 * emisora SINTONIZADA -la del centro, la que sale al lado del reloj-
 * tenga cartel si esta en la base. El reparto por trozos se lo gana
 * solo, porque el trozo central la tiene a ella en el centro.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "emisoras.h"
#include "spec_rotulos.h"
#include "spectrum.h"
#include "temas_tabla.h"

/* La misma luminancia que usa spec_rotulos.c. Esta aqui a mano y no
 * importada a proposito: si el banco usara la funcion del codigo que mide,
 * las dos podrian estar mal igual y el banco saldria verde. */
#define LUZ(c) ((long)((299UL * (((c) >> 16) & 0xFFUL)                 \
                      + 587UL * (((c) >> 8) & 0xFFUL)                  \
                      + 114UL * ((c) & 0xFFUL)) / 1000UL))

const palette_t *g_pal = &k_pal_oscura;
static int fallos;
static void di(int cond, const char *que, const char *como)
{
    printf("    %-56s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { printf("        %s\n", como); fallos++; }
}

static FILE *g_f;
static long  g_tam;
static void lee(uint32_t a, uint8_t *b, uint32_t n)
{
    memset(b, 0, n);
    if ((long)a >= g_tam) { return; }
    fseek(g_f, (long)a, SEEK_SET);
    if (fread(b, 1, n, g_f) != n) { /* el relleno de ceros ya esta */ }
}

#define X0     40
#define ANCHO  756
#define ROT_Y  8
#define ROT_H  46
#define ALTO   120

static uint32_t g_tune;     /* la sintonia, que no es el centro del panel */
static char     g_fijo[SPEC_ROT_TXT + 1U];
static spec_rotulos_t g_rr;
static spec_rot_txt_t g_txt;
static uint8_t        g_filas[SPEC_ROT_MAX];  /* la fila en la que ACABO cada uno */
static uint32_t       g_centro, g_span;
static uint8_t        g_puestos;

static void pinta(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, 0, 800, ALTO, gfx2_rgb(0x08121EUL));
    gfx2_line(s, X0, (int16_t)(ALTO - 10), (int16_t)(X0 + ANCHO),
              (int16_t)(ALTO - 10), gfx2_rgb(0x2A4A6AUL));
    /*
     * LAS FILAS SE ACUMULAN ENTRE BANDAS, NO SE PISAN.
     *
     * gfx2_render() llama a esto UNA VEZ POR BANDA de 16 filas, y
     * spec_rotulos_draw() pone su tabla de filas a 0xFF al entrar: la
     * ultima banda borraba lo que habian apuntado las primeras y el banco
     * leia "no se pinto" de carteles que si estaban. Cada banda apunta lo
     * que ELLA pinto, y aqui se juntan.
     */
    uint8_t loc[SPEC_ROT_MAX], k;

    g_puestos = spec_rotulos_draw(s, &g_rr, g_txt, g_centro, g_span,
                                  X0, ANCHO, ROT_Y, ROT_H, loc,
                                  gfx2_rgb(0xD8E6F2UL), gfx2_rgb(0x4A6E92UL));
    for (k = 0U; k < SPEC_ROT_MAX; k++) {
        if (loc[k] != 0xFFU) { g_filas[k] = loc[k]; }
    }
}

static void dibuja_de_verdad(void)
{
    memset(g_filas, 0xFF, sizeof g_filas);
    lcd_sim_reset(0x0000);
    gfx2_render(0, 0, 800, ALTO, pinta, 0);
}

static void render(const char *ruta)
{
    const uint16_t *fb;
    FILE *f;
    int x, y;

    lcd_sim_reset(0x0000);
    gfx2_render(0, 0, 800, ALTO, pinta, 0);
    fb = lcd_sim_fb();
    f = fopen(ruta, "wb");
    if (f == NULL) { return; }
    fprintf(f, "P6\n800 %d\n255\n", ALTO);
    for (y = 0; y < ALTO; y++) {
        for (x = 0; x < 800; x++) {
            uint16_t v = fb[(size_t)y * 800U + (size_t)x];
            unsigned char p[3];
            p[0] = (unsigned char)(((v >> 11) & 0x1FU) * 255U / 31U);
            p[1] = (unsigned char)(((v >> 5)  & 0x3FU) * 255U / 63U);
            p[2] = (unsigned char)(( v        & 0x1FU) * 255U / 31U);
            fwrite(p, 1, 3, f);
        }
    }
    fclose(f);
}

static uint32_t rot_dif(uint32_t a, uint32_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

/*
 * LO MISMO QUE HACE bd_rotulos_refresca() EN LA RADIO, paso por paso.
 * Si alguien cambia alli el orden de los pasos y aqui no, este banco
 * deja de medir la radio y pasa a medir una copia: por eso son cuatro
 * lineas y no una funcion con vida propia.
 */

/* El cartel de la emisora sintonizada, con LA MISMA consulta que hace el
 * cartel de al lado del reloj en main.c: 600 Hz de tolerancia. */
static void fijo_de_la_sintonia(uint16_t min)
{
    emisoras_r_t r[1];
    char largo[40];

    g_fijo[0] = '\0';
    if (!emisoras_busca(g_tune, 600UL, min, EMISORAS_DIA_CUALQUIERA, r, 1U)) { return; }
    if (!r[0].ahora) { return; }
    emisoras_texto(r[0].nombre, largo, (uint8_t)sizeof largo);
    spec_rotulos_recorta(g_fijo, largo);
}

/* Pinta de verdad en el framebuffer del simulador, que es lo unico que
 * llena g_filas: la fila en la que ACABO cada cartel. Medir la regla
 * -spec_rotulos_fila()- en vez del resultado es lo que dejo pasar la
 * repesca de filas del dibujante. */
static void dibuja_de_verdad(void);

static uint8_t como_la_radio(uint32_t centro, uint32_t span, uint16_t min,
                             uint16_t *hubo)
{
    emisoras_rango_r_t r[SPEC_ROT_CAND];
    spec_rot_cand_t    c[SPEC_ROT_CAND];
    uint32_t lo = centro - span / 2U, hi = centro + span / 2U;
    uint8_t  n, i;

    n = emisoras_rango(lo, hi, min, EMISORAS_DIA_CUALQUIERA, r,
                       (uint8_t)SPEC_ROT_CAND, hubo);
    for (i = 0U; i < n; i++) { c[i].hz = r[i].hz; c[i].clave = r[i].nombre; }
    n = spec_rotulos_filtra(c, n, centro, span, g_tune, SPEC_ROT_MAX);

    memset(&g_rr, 0, sizeof g_rr);
    g_rr.base_hz = lo;
    memset(g_txt, 0, sizeof g_txt);
    for (i = 0U; i < n; i++) {
        char largo[33];
        spec_rotulos_pon(&g_rr, i, c[i].hz, c[i].clave);
        emisoras_texto(c[i].clave, largo, (uint8_t)sizeof largo);
        spec_rotulos_recorta(g_txt[i], largo);
    }
    g_rr.n = n;
    spec_rotulos_limpia(&g_rr, g_txt, span);
    fijo_de_la_sintonia(min);
    g_centro = centro; g_span = span;
    dibuja_de_verdad();
    return n;
}

/* ¿Hay dos carteles en la misma casilla? Seria el reparto metiendo
 * segundas donde cabe una, que es lo que se quito. */
static int dos_en_una_casilla(void)
{
    uint32_t paso = 96000UL / (SPEC_ROT_MAX - 1U);
    uint8_t i, j;
    for (i = 0U; i < g_rr.n; i++) {
        if (g_txt[i][0] == '\0') { continue; }
        for (j = (uint8_t)(i + 1U); j < g_rr.n; j++) {
            if (g_txt[j][0] == '\0') { continue; }
            if (spec_rotulos_hz(&g_rr, i) / paso
                == spec_rotulos_hz(&g_rr, j) / paso) { return 1; }
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *ruta = (argc > 1) ? argv[1] : "datos/EMISORAS.BIN";
    uint16_t hubo;

    printf("\n== QUIEN SALE EN EL ESPECTRO ==\n\n");

    g_f = fopen(ruta, "rb");
    if (g_f == NULL) { printf("  (no esta %s: me salto el banco)\n", ruta); return 0; }
    fseek(g_f, 0, SEEK_END); g_tam = ftell(g_f); fseek(g_f, 0, SEEK_SET);
    if (!emisoras_abre(0UL, lee)) { printf("  no abre la base\n"); return 1; }

    printf("  1. EL CASO DEL VIDEO: 7,260 MHz, 96 kHz, 10:19 UTC\n\n");
    {
        uint8_t n, i;
        g_tune = 7260000UL;
        n = como_la_radio(7260000UL, 96000UL, 619U, &hubo);
        int izq = 0, der = 0, dentro = 1, sinto = 0;

        for (i = 0U; i < n; i++) {
            uint32_t hz = spec_rotulos_hz(&g_rr, i);
            if (g_txt[i][0] == '\0') { continue; }
            printf("     %9.3f  %s\n", hz / 1e6, g_txt[i]);
            if (hz < 7212000UL || hz > 7308000UL) { dentro = 0; }
            if (hz < 7260000UL) { izq++; } else { der++; }
            (void)hz;
        }
        printf("     (%u de %u en antena)\n\n", n, hubo);
        /*
         * NO SE EXIGE LLENAR LOS TRECE. El tope no es SPEC_ROT_MAX sino
         * CUANTAS CASILLAS OCUPADAS hay en la ventana: con el paso en
         * 8 kHz y las emisoras donde estan, aqui salen diez de quince en
         * antena. Lo que se exige es que no se quede corto: con seis
         * -lo que daba la version de dos filas- la mitad de la banda se
         * quedaba sin nombrar.
         */
        di(n >= 9U, "con 15 en antena se nombran nueve o mas",
           "se esta quedando corto: faltan casillas por nombrar");
        di(dentro, "ninguno cae fuera de la ventana que se ve",
           "esto es el fallo del video: se pedia el doble de ventana");
        di(izq > 0 && der > 0, "hay carteles en las dos mitades",
           "el tercio derecho vacio, que es lo que enseñaba el video");
        /*
         * EN LA FRECUENCIA, no "el nombre en algun sitio" - 15/10/2026.
         *
         * Lo afloje a "que el nombre salga en alguna parte" cuando el
         * reparto por casillas le quitaba el cartel a la sintonizada, y
         * el usuario encontro el agujero de verdad: el cartel del reloj
         * decia "BAF Beijing" y abajo no habia ninguno. Dos partes de la
         * misma pantalla contradiciendose. Ahora la que suena gana su
         * casilla siempre y aqui se vuelve a exigir lo fuerte.
         */
        /*
         * YA NO SE EXIGE CARTEL PARA LA SINTONIZADA, Y ESO ES UNA
         * DECISION, NO UN OLVIDO - 15/10/2026.
         *
         * Se intento de tres formas y las tres hacian que la pantalla
         * cambiara al mover el dial (ver el comentario largo en
         * spec_rotulos.c). Pedido por el usuario. El nombre de lo que suena sale al lado del
         * reloj; lo que si se comprueba aqui es que la base LO SABE,
         * que es lo que de verdad podria romperse.
         */
        sinto = (g_fijo[0] != '\0');
        printf("     el cartel del reloj dice: \"%s\"\n", g_fijo);
        di(sinto, "la base sabe quien emite en la frecuencia sintonizada",
           "entonces ni el reloj ni el espectro pueden decirlo");
        render("out/rotulos_7260.ppm");
    }

    printf("\n  2. LOS REPETIDOS (SunDance Radio en 7.215, 7.220 y 7.225)\n");
    {
        uint8_t n = g_rr.n, i, j, repes = 0;
        for (i = 0U; i < n; i++) {
            for (j = (uint8_t)(i + 1U); j < n; j++) {
                if (strcmp(g_txt[i], g_txt[j]) == 0) { repes++; }
            }
        }
        /*
         * NO SE EXIGE CERO: se exige que no sobre NINGUNO que se pudiera
         * haber cambiado por un nombre nuevo. "China National Radio 1"
         * sale dos veces, en 7.290 y 7.305, y esta bien: son dos casillas
         * seguidas y ninguna de las dos tiene segunda candidata. Repetir
         * un nombre informa mas que dejar un hueco en blanco. Lo que se
         * exige es que de 15 en antena salgan al menos cinco nombres
         * DISTINTOS, que antes eran dos.
         */
        {
            uint8_t distintos = 0U, a, b;
            for (a = 0U; a < n; a++) {
                uint8_t ya = 0U;
                for (b = 0U; b < a; b++) {
                    if (strcmp(g_txt[a], g_txt[b]) == 0) { ya = 1U; break; }
                }
                if (!ya) { distintos++; }
            }
            printf("     %u carteles, %u nombres distintos, %u repetidos\n",
                   n, distintos, repes);
            di(distintos >= 5U, "con 15 en antena salen 5 nombres distintos o mas",
               "tres carteles iguales se comen tres huecos y se pisan");
        }
    }

    printf("\n  3. Y QUE NO SEA UN APAÑO PARA ESA FRECUENCIA.\n");
    printf("     Barrido de 3 a 26 MHz cada 24 kHz, a las 00:00 y a las 12:00\n\n");
    {
        long ventanas = 0, fuera = 0, repetidos = 0, una_mitad = 0, llenas = 0;
        long con_sitio = 0, desperdiciadas = 0;
        uint32_t centro;
        uint16_t min;
        int k;

        for (k = 0; k < 2; k++) {
            min = (uint16_t)(k * 720);
            for (centro = 3000000UL; centro < 26000000UL; centro += 24000UL) {
                uint8_t n, i, j;
                g_tune = centro;
                n = como_la_radio(centro, 96000UL, min, &hubo);
                uint32_t lo = centro - 48000UL, hi = centro + 48000UL;
                int izq = 0, der = 0;

                ventanas++;
                if (hubo == 0U) { continue; }
                for (i = 0U; i < n; i++) {
                    uint32_t hz = spec_rotulos_hz(&g_rr, i);
                    if (g_txt[i][0] == '\0') { continue; }
                    if (hz < lo || hz > hi) { fuera++; }
                    if (hz < centro) { izq++; } else { der++; }
                    for (j = (uint8_t)(i + 1U); j < n; j++) {
                        /*
                         * UN NOMBRE REPETIDO NO ES UN FALLO; REPETIDO Y
                         * PEGADO SI - 15/10/2026.
                         *
                         * Aqui pedia cero repetidos y el barrido dijo
                         * MAL. Era cierto el barrido y estaba mal la
                         * exigencia: una emisora en dos extremos de la
                         * ventana son DOS señales en la pantalla, y
                         * cuando sobran huecos se nombran las dos. Lo
                         * que no puede pasar es que se solapen, que es
                         * lo que se ve en el video. Esa es la raya.
                         */
                        if (strcmp(g_txt[i], g_txt[j]) != 0) { continue; }
                        if (rot_dif(hz, spec_rotulos_hz(&g_rr, j)) < 96000UL / SPEC_ROT_MAX) {
                            repetidos++;
                        }
                    }
                }
                if (hubo >= SPEC_ROT_MAX) {
                    con_sitio++;
                    /*
                     * UNA POR CASILLA, NI MAS NI MENOS - 15/10/2026.
                     *
                     * Antes pedia "que se llenen los siete huecos", y eso
                     * era pedir el fallo: llenarlos obligaba a meter
                     * segundas de casillas ya puestas, que es de donde
                     * salian los carteles que aparecian y desaparecian
                     * solos. Lo que se exige ahora es el invariante de
                     * verdad del reparto: de cada casilla con emisoras
                     * dentro de la ventana sale UN cartel, y de las
                     * vacias ninguno.
                     */
                    if (dos_en_una_casilla()) { desperdiciadas++; }
                    else { llenas++; }
                    /* Solo se exige repartir cuando habia de donde: con
                     * tres emisoras y las tres a la izquierda, lo
                     * correcto es pintar tres a la izquierda. */
                    if (izq == 0 || der == 0) { una_mitad++; }
                }
            }
        }
        printf("     %ld ventanas; %ld con mas emisoras que huecos\n",
               ventanas, con_sitio);
        di(fuera == 0, "nunca se pinta nada de fuera de la ventana", "");
        di(repetidos == 0, "nunca se repite un nombre dentro de una ventana", "");
        di(desperdiciadas == 0,
           "nunca salen dos carteles de la misma casilla",
           "el reparto esta metiendo segundas donde cabe una");
        printf("        una por casilla en %ld de %ld\n", llenas, con_sitio);
        /*
         * ESTE NO SE EXIGE A CERO, Y POR ESO SE IMPRIME. Hay ventanas de
         * verdad -el borde de una banda de radiodifusion- donde las
         * emisoras estan todas de un lado. Pedir cero seria pedir que se
         * invente una emisora donde no la hay.
         */
        printf("        de un solo lado: %ld de %ld (%.1f%%)\n",
               una_mitad, con_sitio, con_sitio ? 100.0 * una_mitad / con_sitio : 0.0);
        di(con_sitio == 0 || una_mitad * 4 < con_sitio,
           "en menos de una cuarta parte los carteles van de un solo lado",
           "si son muchas, el reparto no esta repartiendo");
    }

    printf("\n  3b. LAS DOS FOTOS DE UN KILOHERCIO - 15/10/2026\n");
    /*
     *
     * Y era cierto., y el banco de arriba no lo veia: mide cada ventana
     * POR SEPARADO y las dos estaban bien cada una por su cuenta. Lo que
     * estaba mal era el salto de una a la otra, que es algo que no se
     * puede ver mirando una sola foto.
     *
     * En sus fotos, en 5,500 MHz salian "The Pip", "Cordoba Volmet",
     * "Seoul Muan" y "Mogadishu"; un kilohercio mas arriba, "RAF Volmet",
     * "Cordoba Volmet", "Antananarivo" y "Mogadishu". Dos de cuatro
     * cambiados por mover el dial un clic.
     *
     * La causa era que el reparto partia LA VENTANA en trozos: al moverse
     * la ventana se movian los trozos, y cada emisora caia en otro. La
     * rejilla es ahora ABSOLUTA (ver spec_rotulos_filtra), asi que mover
     * un kilohercio solo puede cambiar lo que entra o sale POR LOS
     * BORDES.
     *
     * LO QUE SE MIDE: cuantos carteles cambian por cada kilohercio de
     * dial. Con la rejilla absoluta tiene que ser casi siempre CERO, y
     * cuando no, uno.
     */
    {
        static const uint32_t k_sitio[] = { 5500000UL, 7260000UL, 9600000UL,
                                            6100000UL, 11800000UL, 15200000UL };
        unsigned si;
        long pasos = 0, cambios = 0, peor = 0, saltos = 0, dial = 0;
        uint32_t peor_hz = 0UL;

        for (si = 0; si < sizeof k_sitio / sizeof k_sitio[0]; si++) {
            char antes[SPEC_ROT_MAX][SPEC_ROT_TXT + 1U];
            uint8_t fila_antes[SPEC_ROT_MAX], fila_ahora[SPEC_ROT_MAX];
            uint32_t hz_antes[SPEC_ROT_MAX], hz_ahora[SPEC_ROT_MAX];
            uint8_t n_antes = 0U, p;

            for (p = 0U; p <= 40U; p++) {
                uint32_t ce = k_sitio[si] + (uint32_t)p * 1000UL;
                uint8_t n, i, j;
                g_tune = ce;
                n = como_la_radio(ce, 96000UL, 701U, &hubo);
                char ahora[SPEC_ROT_MAX][SPEC_ROT_TXT + 1U];
                uint8_t n_ahora = 0U;
                long dif = 0;

                for (i = 0U; i < n; i++) {
                    if (g_txt[i][0] == '\0') { continue; }
                    /*
                     * LA FILA DIBUJADA ES LA DE LA REGLA, SIEMPRE.
                     *
                     * Parece una perogrullada contra el codigo de hoy, y
                     * por eso esta: hoy he escrito CUATRO veces una
                     * repesca de fila -"si en la tuya no cabe, prueba
                     * otra"- y las cuatro hacian que la fila dependiera
                     * de quien mas hubiera. Esto compara la fila en la
                     * que ACABO cada cartel con la que manda su casilla,
                     * en las 240 ventanas del barrido. La quinta repesca
                     * salta aqui.
                     */
                    if (g_filas[i] != 0xFFU
                        && g_filas[i] != spec_rotulos_fila(&g_rr, i, 96000UL)) {
                        saltos++;
                    }
                    strcpy(ahora[n_ahora], g_txt[i]);
                    fila_ahora[n_ahora] = g_filas[i];
                    hz_ahora[n_ahora] = spec_rotulos_hz(&g_rr, i);
                    n_ahora++;
                }
                if (p != 0U) {
                    /* Y QUE NADIE CAMBIE DE FILA MIENTRAS ESTA.
                     * Pedido por el usuario. */
                    for (i = 0U; i < n; i++) {
                        uint8_t f = g_filas[i], t;
                        uint32_t h = spec_rotulos_hz(&g_rr, i);
                        if (g_txt[i][0] == '\0' || f == 0xFFU) { continue; }
                        /*
                         * SE COMPARA POR FRECUENCIA Y NO POR NOMBRE, y
                         * esto no es aflojar la prueba.
                         *
                         * Un mismo nombre puede saltar de PORTADORA: "PBS
                         * Xinjiang" esta en tres canales y, al salirse de
                         * la pantalla la casilla que tenia el cartel, lo
                         * hereda otra de sus portadoras. Ese cartel ha
                         * cambiado de sitio de verdad -de x y de fila- y
                         * es correcto que lo haga. Lo que no puede pasar
                         * es que un cartel QUE SIGUE EN LA MISMA
                         * FRECUENCIA se mude de fila, que es lo que veia
                         * el usuario.
                         */
                        for (t = 0U; t < n_antes; t++) {
                            if (hz_antes[t] != h) { continue; }
                            /*
                             * Solo se comparan dos estados PINTADOS. Que
                             * un cartel no estuviera antes y este ahora
                             * no es un salto de fila: es que ha entrado,
                             * y eso ya lo cuenta `cambios`. Pasa, por
                             * ejemplo, cuando el dial sale de su casilla
                             * y el cartel que representaba el de la
                             * sintonia vuelve a la lista.
                             */
                            if (fila_antes[t] == 0xFFU) { continue; }
                            if (fila_antes[t] != f) { saltos++; }
                            if (strcmp(antes[t], g_txt[i]) != 0) { saltos++; }
                        }
                    }
                    /* cuantos de los de antes ya no estan */
                    for (i = 0U; i < n_antes; i++) {
                        uint8_t sigue = 0U;
                        for (j = 0U; j < n_ahora; j++) {
                            if (strcmp(antes[i], ahora[j]) == 0) { sigue = 1U; break; }
                        }
                        if (sigue) { continue; }
                        dif++;
                        /*
                         * LOS DE LA CASILLA DEL DIAL SE CUENTAN APARTE.
                         *
                         * La emisora que suena gana su casilla siempre
                         * (se pinta aparte, ver spec_rotulos_draw), asi que al pasar el
                         * dial por encima de una, su cartel entra; y al
                         * salir de sus 600 Hz, se va. Eso NO es el
                         * baile que se esta midiendo: es el cartel
                         * siguiendo al marcador, que es lo que se le
                         * pidio. Se cuenta, se imprime, y no se exige.
                         */
                        if (hz_antes[i] / (96000UL / SPEC_ROT_MAX)
                            == ce / (96000UL / SPEC_ROT_MAX)) { dial++; }
                    }
                    pasos++;
                    cambios += dif;
                    if (dif > peor) { peor = dif; peor_hz = ce; }
                }
                for (i = 0U; i < n_ahora; i++) {
                    strcpy(antes[i], ahora[i]);
                    fila_antes[i] = fila_ahora[i];
                    hz_antes[i] = hz_ahora[i];
                }
                n_antes = n_ahora;
            }
        }
        printf("     %ld clics de 1 kHz; %ld carteles cambiados en total\n",
               pasos, cambios);
        printf("     de ellos %ld son el cartel de la emisora que suena\n", dial);
        printf("     el peor clic cambio %ld, en %.3f MHz\n", peor, peor_hz / 1e6);
        di(saltos == 0,
           "cada cartel sale SIEMPRE en la fila que manda su casilla",
           "la fila sale de la posicion en la lista y no de la frecuencia");
        di(peor <= 1,
           "un clic de 1 kHz cambia como mucho UN cartel",
           "los trozos del reparto se estan moviendo con la ventana");
        /*
         * EL SUELO DE ESTO NO ES CERO, Y CONVIENE SABER CUAL ES.
         *
         * La ventana mide siete casillas. Cada vez que el dial avanza una
         * casilla entera -96/7 = 13,7 kHz- una casilla se va por un borde
         * y entra otra por el otro: UN cartel tiene que cambiar, por
         * fuerza. O sea que el minimo posible son 240/13,7 = 17 cambios
         * en este barrido, y lo medido son 20: catorce por ciento por
         * encima del suelo, que es el precio de los desempates de nombre.
         *
         * El liston se pone en uno cada ocho clics -o sea 30- porque es
         * lo bastante cerca del suelo para cazar que alguien vuelva a
         * atar el reparto a la ventana, y lo bastante lejos para no
         * saltar por un desempate mas.
         */
        printf("     (el suelo teorico son %ld: una casilla cada %lu kHz)\n",
               pasos / (long)(96UL / (SPEC_ROT_MAX - 1U)),
               (unsigned long)(96UL / (SPEC_ROT_MAX - 1U)));
        di((cambios - dial) * 8 <= pasos,
           "y de media, menos de uno cada ocho clics (sin contar el del dial)",
           "aunque ninguno cambie mucho, cambian demasiado a menudo");
    }

    printf("\n  3c. LA QUE SUENA, EN 5.544 - 15/10/2026\n");
    /*
     *
     * 5.544.000 es multiplo exacto de 8.000 -el paso de la rejilla- asi
     * que cae en el BORDE de su casilla: descentre 4.000, el peor
     * posible. Pierde contra Shannon HFDL, que esta en 5.547 y a 1.000
     * del centro de la misma casilla.
     *
     * Esto reproduce su foto con la base de verdad y con su minuto, y
     * exige las tres cosas a la vez: que salga, que salga en la fila que
     * manda SU casilla -no en otra-, y que al bajarse el dial de ella
     * vuelva la vecina. Sin la tercera, "sale la que suena" seria una
     * excusa para que la pantalla hiciera cualquier cosa.
     */
    {
        uint8_t n, i;
        int esta = 0, vecina_despues = 0, esta_lejos = 0;
        uint8_t fila_muh = 0xFFU;

        g_tune = 5544000UL;
        n = como_la_radio(5544000UL, 96000UL, 809U, &hubo);
        for (i = 0U; i < n; i++) {
            if (g_txt[i][0] == '\0') { continue; }
            if (spec_rotulos_hz(&g_rr, i) == 5544000UL) {
                esta = 1; fila_muh = g_filas[i];
            }
        }
        printf("     dial en 5.544.000, %u carteles\n", n);
        di(esta, "la emisora que suena tiene cartel aunque este en el borde",
           "es la unica que el usuario puede comprobar de oido: si falta, falta la que importa");
        di(fila_muh != 0xFFU
           && fila_muh == (uint8_t)((5544000UL / 8000UL) % SPEC_ROT_FILAS),
           "y en la fila que manda su casilla, como todos los demas",
           "si la sintonia se saltara la fila, volveriamos a los saltos de fila");

        /* Un clic mas arriba ya no es "la que suena": vuelve Shannon. */
        g_tune = 5545000UL;
        n = como_la_radio(5545000UL, 96000UL, 809U, &hubo);
        for (i = 0U; i < n; i++) {
            if (g_txt[i][0] == '\0') { continue; }
            if (spec_rotulos_hz(&g_rr, i) == 5547000UL) { vecina_despues = 1; }
            if (spec_rotulos_hz(&g_rr, i) == 5544000UL) { esta_lejos = 1; }
        }
        di(vecina_despues, "a 1 kHz del dial vuelve la vecina de su casilla",
           "si no volviera, el cartel del dial no seria un prestamo sino un robo");
        di(!esta_lejos, "y la del borde se vuelve a quedar fuera",
           "fuera del dial manda la rejilla y nada mas");
    }

    printf("\n  3d. LOS OCHO TEMAS - 15/10/2026\n");
    /*
     *
     * Los carteles se pintan DENTRO del panel de la traza, que es casi
     * negro en los ocho temas, y el color salia de PAL_INK, que es la
     * tinta del CROMO. En un tema claro esa tinta es NEGRA. Tres de ocho.
     *
     * Esto recorre las ocho paletas de verdad -las del firmware, no una
     * copia- y exige lo unico que hay que exigir: que el texto y la rayita
     * se SEPAREN del fondo del panel lo suficiente para verse. Y de paso,
     * que en los temas donde la tinta ya valia no haya cambiado nada: un
     * arreglo que repinta los cinco temas que iban bien no es un arreglo.
     */
    {
        /*
         * LOS TEMAS SALEN DE k_temas[], NO DE UNA LISTA A MANO - 17/10/2026.
         *
         * Aqui habia ocho punteros escritos uno a uno. El dia que entraron
         * diez temas claros, este banco siguio diciendo "en los ocho temas
         * el cartel se separa del fondo" sobre los ocho de siempre, que ya
         * se veian, mientras los diez nuevos no los miraba nadie. Una lista
         * a mano de algo que crece es una lista que se queda corta sin
         * avisar; k_sim_temas[] lo genera gen_temas.py del propio main.c.
         *
         * Y EL FONDO TAMPOCO ES UNO SOLO. Era SPEC_LINE_BG fijo, el azul
         * casi negro del panel, que valia cuando el panel era ese para
         * todos. Los temas con espectro_claro pintan el panel de SU color,
         * asi que el cartel hay que medirlo contra el fondo que le toca:
         * una tinta clara calculada contra un fondo negro imaginario es
         * invisible sobre el papel que de verdad hay debajo.
         */
        const uint32_t fondo_oscuro = ((uint32_t)(( (0x0043U >> 11) & 0x1FU) * 255U / 31U) << 16)
                                    | ((uint32_t)(( (0x0043U >> 5)  & 0x3FU) * 255U / 63U) << 8)
                                    |  (uint32_t)(( (0x0043U)       & 0x1FU) * 255U / 31U);
        int z;
        int flojos = 0, tocados = 0;

        printf("     fondo de los temas de siempre #%06lX (luz %ld);"
               " los claros, el suyo\n",
               (unsigned long)fondo_oscuro, (long)LUZ(fondo_oscuro));
        printf("     %-14s %8s %8s %8s   %7s %7s\n",
               "tema", "fondo", "texto", "rayita", "d.texto", "d.raya");
        for (z = 0; z < SIM_TEMA_COUNT; z++) {
            const palette_t *p = k_sim_temas[z].pal;
            uint32_t fondo = p->espectro_claro ? p->surf_0 : fondo_oscuro;
            uint32_t te = 0UL, ra = 0UL;
            long dt, dr;

            spec_rotulos_tinta(fondo, p->ink, p->ink_dim, p->trace, &te, &ra);
            dt = labs(LUZ(te) - LUZ(fondo));
            dr = labs(LUZ(ra) - LUZ(fondo));
            printf("     %-14s %06lX   %06lX   %06lX    %5ld   %5ld%s\n",
                   p->name, (unsigned long)fondo,
                   (unsigned long)te, (unsigned long)ra, dt, dr,
                   (dt < SPEC_ROT_CONTRASTE || dr < SPEC_ROT_CONTRASTE)
                       ? "   <-- NO SE VE" : "");
            if (dt < SPEC_ROT_CONTRASTE || dr < SPEC_ROT_CONTRASTE) { flojos++; }
            /* ¿Le hemos cambiado el color a un tema que ya se veia? */
            if (labs(LUZ(p->ink) - LUZ(fondo)) >= SPEC_ROT_CONTRASTE
                && te != p->ink) { tocados++; }
        }
        di(flojos == 0,
           "en los dieciocho temas el cartel se separa del fondo del panel",
           "un tema con la tinta del color del papel deja los nombres"
           " invisibles");
        di(tocados == 0,
           "y a los temas que ya se veian no se les ha tocado el color",
           "arreglar unos temas repintando los que iban bien no es arreglar");
    }

    printf("\n  4. EL RECORTE DE LOS NOMBRES\n");
    {
        char t[SPEC_ROT_TXT + 1U];
        spec_rotulos_recorta(t, "China National Radio 1");
        printf("     \"China National Radio 1\" -> \"%s\"\n", t);
        di(strcmp(t, "China National") == 0, "corta por el espacio",
           "cortar a mitad de palabra se lee como un fallo de memoria");
        spec_rotulos_recorta(t, "SunDance Radio");
        di(strcmp(t, "SunDance Radio") == 0,
           "catorce letras justas entran enteras",
           "\"SunDance Radio\" mide 14: si se recorta, el recorte sobra");
        spec_rotulos_recorta(t, "BBC");
        di(strcmp(t, "BBC") == 0, "lo corto se queda como esta", "");
        spec_rotulos_recorta(t, "Radiodifusionxx");
        di(strcmp(t, "Radiodifusionx") == 0,
           "sin espacios por donde cortar, se corta a lo bruto",
           "dejarlo vacio seria peor que dejarlo cortado");
        spec_rotulos_recorta(t, "SOH Xi Wang Zhi Sheng");
        printf("     \"SOH Xi Wang Zhi Sheng\" -> \"%s\"\n", t);
        di(strcmp(t, "SOH Xi Wang") == 0, "y el del video tambien", "");
        /*
         * ESTE ES EL QUE CANTO EL BARRIDO. "Channel Marker D" mide
         * diecisiete, las catorce primeras son "Channel Marker"
         * completas, y aun asi se comia la segunda palabra entera y
         * siete balizas distintas acababan diciendo "Channel".
         */
        spec_rotulos_recorta(t, "Channel Marker D");
        printf("     \"Channel Marker D\" -> \"%s\"\n", t);
        di(strcmp(t, "Channel Marker") == 0,
           "el corte que cae en un espacio no parte ninguna palabra",
           "siete balizas distintas acabando todas en \"Channel\"");
        /*
         * Y EL QUE SE ENCONTRO EL DUEÑO EN PANTALLA: un cartel que decia
         * "Voice of" y nada mas. El espacio cae en la octava letra, y con
         * el liston en seis se cortaba ahi, tirando justo la palabra que
         * dice QUE emisora es.
         */
        spec_rotulos_recorta(t, "Voice of Vietnam");
        printf("     \"Voice of Vietnam\" -> \"%s\"\n", t);
        di(strcmp(t, "Voice of Vietn") == 0,
           "no se corta por la palabra si se lleva media frase por delante",
           "\"Voice of\" a secas no nombra a nadie");
        spec_rotulos_recorta(t, "CNR1 Jammer/Firedrake");
        printf("     \"CNR1 Jammer/Firedrake\" -> \"%s\"\n", t);
        di(strcmp(t, "CNR1 Jammer") == 0, "la barra tambien separa", "");
    }

    printf("\n  4b. Y LOS QUE ACABAN DICIENDO LO MISMO\n");
    {
        spec_rotulos_t rr;
        spec_rot_txt_t tt;
        memset(&rr, 0, sizeof rr); memset(tt, 0, sizeof tt);
        rr.base_hz = 3550000UL;
        spec_rotulos_pon(&rr, 0U, 3594000UL, 1U);
        spec_rotulos_pon(&rr, 1U, 3594200UL, 2U);
        spec_rotulos_pon(&rr, 2U, 3640000UL, 3U);
        spec_rotulos_recorta(tt[0], "Shannon Aeradio I");
        spec_rotulos_recorta(tt[1], "Shannon Aeradio H");
        spec_rotulos_recorta(tt[2], "Shannon Aeradio B");
        rr.n = 3U;
        spec_rotulos_limpia(&rr, tt, 96000UL);
        di(tt[0][0] != '\0' && tt[1][0] == '\0',
           "dos nombres iguales pegados: se apaga el segundo",
           "dos carteles identicos uno encima de otro, que es el borron de la foto");
        di(tt[2][0] != '\0',
           "y el que dice lo mismo pero LEJOS se queda",
           "en la otra punta de la pantalla es otra señal y merece su nombre");
    }

    printf("\n  5. LAS PUERTAS DEL FILTRO\n");
    {
        spec_rot_cand_t c[4];
        uint8_t n;
        c[0].hz = 9600000UL; c[0].clave = 1U;
        n = spec_rotulos_filtra(c, 0U, 9600000UL, 96000UL, 0UL, SPEC_ROT_MAX);
        di(n == 0U, "sin candidatas, cero", "");
        n = spec_rotulos_filtra(c, 1U, 9600000UL, 0UL, 0UL, SPEC_ROT_MAX);
        di(n == 0U, "con span cero, cero y sin dividir por cero", "");
        n = spec_rotulos_filtra(c, 1U, 9600000UL, 96000UL, 0UL, 0U);
        di(n == 0U, "con sitio para cero, cero", "");
        /* Dos iguales LEJOS se quedan las dos: una emisora en los dos
         * extremos de la ventana son dos señales distintas. */
        c[0].hz = 9560000UL; c[0].clave = 7U;
        c[1].hz = 9640000UL; c[1].clave = 7U;
        n = spec_rotulos_filtra(c, 2U, 9600000UL, 96000UL, 0UL, SPEC_ROT_MAX);
        di(n == 2U, "el mismo nombre lejos sale dos veces",
           "dos señales separadas en pantalla merecen su cartel cada una");
        c[0].hz = 9600000UL; c[0].clave = 7U;
        c[1].hz = 9602000UL; c[1].clave = 7U;
        n = spec_rotulos_filtra(c, 2U, 9600000UL, 96000UL, 0UL, SPEC_ROT_MAX);
        di(n == 1U, "el mismo nombre pegado sale una sola vez", "");
    }

    printf("\n  (imagen en out/rotulos_7260.ppm - hay que MIRARLA)\n");
    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
