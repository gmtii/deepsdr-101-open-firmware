/*
 * EL CARGADOR DE ARRANQUE NUESTRO: LA PARTE QUE TOCA HARDWARE.
 *
 * Todo lo que DECIDE esta en User/cargador.c y se prueba en el PC contra el
 * update4.bin de verdad (ver sim/cargador.c y `make cargador`). Aqui solo
 * hay lo que no se puede probar sin placa: el reloj, la pantalla, el FMC y
 * el salto. Y a proposito, nada de esto decide nada.
 *
 * LA PANTALLA. *** El dueno: "al entrar en modo update la pantalla podria
 * decirnos datos, como el chip que tiene, la flash que tiene y cosas asi".
 * *** Y resulta que el chip lo dice el solo: en 0x1FFF7A20 hay una palabra
 * de solo lectura, puesta en fabrica, con la flash en los 16 bits altos y
 * la SRAM en los bajos, las dos en kilobytes (manual, 1.6.1).
 *
 * Eso convierte esta pantalla en la herramienta del cambio de chip: sueldas
 * un VIT6, enciendes, y te dice si de verdad tienes 2048/512 o te han
 * vendido otra cosa. Sin depurador y sin compilar nada.
 *
 * Y AVISA de lo que importa: si la flash es de mas de 512 kB, parte de la
 * aplicacion cae fuera de la zona de cero esperas y va a ir lenta. El
 * cargador lo sabe -tiene la cifra delante- asi que lo dice.
 */
#include "gd32f4xx.h"
#include "cargador.h"
#include "spi_flash.h"
#include "rm68120_exmc.h"
#include "panel_serie.h"
#include "disco.h"
#include "drv_usb_hw.h"
#include "usbd_msc_core.h"

extern usb_core_driver g_usb;   /* en soporte.c, compartido con usb_it.c */
#include "gfx2_font.h"
#include "font_ui_18b.h"
#include <string.h>

#define NEGRO   0x0000U
#define BLANCO  0xFFFFU
#define VERDE   0x07E0U
#define ROJO    0xF800U
#define AMBAR   0xFD20U
#define AZUL    0x04BFU
#define GRIS    0x8410U

/*
 * LA PALETA, Y POR QUE NO ES LA DE ANTES - 01/10/2026.
 *
 * *** El dueno, dos veces: "es infinitamente feo por cierto" y "hay que
 * tunearlo porque sigue siendo feo". *** Tenia razon las dos.
 *
 * Los colores de arriba son los primitivos del principio -azul puro, gris
 * medio, blanco- y puestos uno al lado de otro no forman nada. Estos salen
 * del tema de la propia radio: un fondo que no es negro del todo, un azul
 * de acento, un ambar para lo que hay que leer, y DOS grises, uno para las
 * etiquetas y otro para los valores. La jerarquia la hace el contraste
 * entre esos dos, no el tamano de la letra.
 */
#define FONDO     0x0861U   /* casi negro, con una pizca de azul */
#define BARRA     0x18C3U   /* la banda del titulo */
#define ACENTO    0x3D7FU   /* azul claro, para el titulo */
#define ETIQUETA  0x6B6DU   /* gris apagado: la columna de la izquierda */
#define VALOR     0xE73CU   /* casi blanco: la columna de la derecha */
#define SUAVE     0x4A69U   /* separadores */

/* --- texto: la fuente proporcional del firmware --------------------- */

/*
 * *** El dueno: "es infinitamente feo por cierto" ... "no tienes otra
 * fuente?" ... "cambia la puta fuente ya". ***
 *
 * La tenia delante. El firmware lleva seis fuentes proporcionales con
 * antialias a 4 bits (gfx2_font.h), generadas de DejaVu Sans Condensed, y
 * el cargador estaba usando una de 8x12 de un bit escalada x2 a lo bruto:
 * de ahi las letras con escalones del tamano de un ladrillo.
 *
 * Aqui se usa font_ui_18b -negrita de 19 px, 9.910 bytes de bitmap- y se
 * pinta mezclando tinta y fondo segun los 16 niveles de cobertura de cada
 * pixel. El fondo es plano, asi que no hace falta leer la pantalla: se
 * interpola cada canal del RGB565 por separado y se escribe.
 *
 * Sitio hay de sobra: el cargador ocupa 22 kB de los 128 kB que tiene
 * reservados.
 */
static const gfx2_glyph_t *glifo(const gfx2_font_t *f, uint16_t cp)
{
    uint8_t i;
    for (i = 0U; i < f->n_ranges; i++) {
        if (cp >= f->ranges[i].lo && cp <= f->ranges[i].hi) {
            return &f->glyphs[f->ranges[i].first + (cp - f->ranges[i].lo)];
        }
    }
    return (const gfx2_glyph_t *)0;
}

/* Mezcla de dos colores RGB565 con cobertura de 0 a 15, canal a canal. */
static uint16_t mezcla(uint16_t fondo, uint16_t tinta, uint8_t a)
{
    uint32_t rf = (fondo >> 11) & 0x1FU, gf = (fondo >> 5) & 0x3FU, bf = fondo & 0x1FU;
    uint32_t rt = (tinta >> 11) & 0x1FU, gt = (tinta >> 5) & 0x3FU, bt = tinta & 0x1FU;
    uint32_t r = (rf * (15U - a) + rt * a) / 15U;
    uint32_t g = (gf * (15U - a) + gt * a) / 15U;
    uint32_t b = (bf * (15U - a) + bt * a) / 15U;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

#define FUENTE   (&font_ui_18b)
#define ALTO_LIN 23U      /* line_h de font_ui_18b */

static uint16_t letra(uint16_t x, uint16_t y, char c, uint16_t tinta, uint16_t fondo)
{
    const gfx2_font_t  *f = FUENTE;
    const gfx2_glyph_t *g = glifo(f, (uint16_t)(unsigned char)c);
    uint16_t fil, col, x0, y0;
    uint32_t fuera;

    if (g == (const gfx2_glyph_t *)0) { g = glifo(f, (uint16_t)'?'); }
    if (g == (const gfx2_glyph_t *)0) { return x; }
    if (g->w == 0U || g->h == 0U)     { return (uint16_t)(x + g->adv); }

    x0 = (uint16_t)((int32_t)x + g->bx);
    y0 = (uint16_t)((int32_t)y + g->by);

    rm68120_set_window(x0, y0, (uint16_t)(x0 + g->w - 1U), (uint16_t)(y0 + g->h - 1U));

    /* Cada fila ocupa (w+1)/2 bytes: dos pixeles por byte, nibble alto el
     * de la izquierda. Una fila de ancho impar lleva medio byte de relleno. */
    fuera = ((uint32_t)g->w + 1U) >> 1;
    for (fil = 0U; fil < g->h; fil++) {
        const uint8_t *r = &f->bitmap[g->off + (uint32_t)fil * fuera];
        for (col = 0U; col < g->w; col++) {
            uint8_t a = (col & 1U) ? (uint8_t)(r[col >> 1] & 0x0FU)
                                   : (uint8_t)(r[col >> 1] >> 4);
            rm68120_write_data(a == 0U ? fondo
                             : (a == 15U ? tinta : mezcla(fondo, tinta, a)));
        }
    }
    return (uint16_t)(x + g->adv);
}

/*
 * *** El dueno: "algo has tocao que las letras se ven raras". *** Y era
 * esto: la fuente es de 4 bits de cobertura, asi que cada pixel del borde
 * de una letra se MEZCLA con el fondo. Si se pinta sobre la barra del
 * titulo pero se mezcla contra el fondo general, cada glifo arrastra un
 * rectangulo del color que no es y se ve un halo alrededor de cada letra.
 *
 * Con la fuente de un bit de antes esto no podia pasar: un pixel estaba o
 * no estaba. Es el precio del antialias, y se paga diciendo siempre sobre
 * que color se pinta.
 */
static void texto_sobre(uint16_t x, uint16_t y, const char *s,
                        uint16_t tinta, uint16_t fondo)
{
    while (*s != '\0' && x < 790U) {
        x = letra(x, y, *s++, tinta, fondo);
    }
}

static void texto(uint16_t x, uint16_t y, const char *s, uint16_t tinta)
{
    texto_sobre(x, y, s, tinta, FONDO);
}

/*
 * BORRAR UNA LINEA, PINTANDO, NO CON ESPACIOS - 01/10/2026.
 *
 * *** El dueno: "hay texto que se solapa". *** Culpa de la fuente nueva y
 * mia: antes, con la de 8x12, un espacio medía exactamente lo mismo que
 * una letra, asi que para borrar una linea bastaba con escribir encima una
 * ristra de espacios. Con una fuente proporcional el espacio es la mitad
 * de ancho que una letra media, asi que la ristra ya no tapa nada y lo
 * viejo asoma por debajo de lo nuevo.
 *
 * Esto pinta la banda entera de fondo. Es lo que habria que haber hecho
 * desde el principio.
 */
/*
 * BORRAR UNA LINEA, PINTANDO, NO CON ESPACIOS - 01/10/2026.
 *
 * *** El dueno: "hay texto que se solapa". *** Culpa de la fuente nueva y
 * mia: antes, con la de 8x12, un espacio medía exactamente lo mismo que
 * una letra, asi que para borrar una linea bastaba con escribir encima una
 * ristra de espacios. Con una fuente proporcional el espacio es la mitad
 * de ancho que una letra media, asi que la ristra ya no tapa nada y lo
 * viejo asoma por debajo de lo nuevo.
 *
 * Esto pinta la banda entera de fondo. Es lo que habria que haber hecho
 * desde el principio.
 */
static void cabecera(const char *titulo, const char *pie);

static void rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    uint32_t i, n = (uint32_t)w * (uint32_t)h;

    if (w == 0U || h == 0U) { return; }
    rm68120_set_window(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    for (i = 0U; i < n; i++) { rm68120_write_data(c); }
}

static void borra_linea(uint16_t y)
{
    rect(0U, y, 800U, (uint16_t)(ALTO_LIN + 1U), FONDO);
}

/*
 * Una fila de ficha: etiqueta a la izquierda en gris apagado, valor en la
 * columna fija de la derecha en casi blanco. La columna fija es lo que
 * hace que se lean como una tabla y no como un parrafo, que era medio
 * problema de lo feo.
 */
#define COL_VALOR  150U

/*
 * LA VERSION DEL CARGADOR - 01/10/2026.
 *
 * *** El dueno: "en el modo actualizacion en la barra de arriba, a la
 * derecha hay que poner la version del bootloader". *** Y hace falta por
 * algo mas que por orden: hoy han salido dieciseis binarios distintos y la
 * unica forma de saber cual estaba grabado era mirar el sha256 en el chat.
 * Con esto se mira la radio y ya esta.
 *
 * SUBIRLA al tocar cualquier cosa del cargador. Si no se sube, miente, y
 * un numero de version que miente es peor que no tenerlo.
 */
#define CARGADOR_VERSION  "v1.1"

/*
 * *** El dueno, viendo el tuneo: "Numero de s54486357 04333833". *** La
 * etiqueta era mas larga que la columna y el valor la pisaba. Ahora la
 * etiqueta se corta sola al llegar a la columna, asi que una etiqueta
 * larga quedara fea pero NUNCA ilegible, que es la diferencia que importa.
 *
 * Y el separador de la cabecera vuelve a ser un guion ASCII: puse un "·",
 * que en UTF-8 son DOS bytes, y letra() busca un glifo por byte -el
 * primero no existe y sale como "?"-. La fuente es ISO-8859-1 de una sola
 * tabla; aqui no hay UTF-8 que valga.
 */
/* El ancho que ocupara una cadena, para poder alinearla a la derecha. Con
 * una fuente proporcional no se puede contar letras: hay que sumar los
 * avances de cada glifo. */
static uint16_t ancho_texto(const char *s)
{
    const gfx2_font_t *f = FUENTE;
    uint16_t w = 0U;

    while (*s != '\0') {
        const gfx2_glyph_t *g = glifo(f, (uint16_t)(unsigned char)*s++);
        if (g != (const gfx2_glyph_t *)0) { w = (uint16_t)(w + g->adv); }
    }
    return w;
}

static void texto_derecha(uint16_t borde, uint16_t y, const char *s,
                          uint16_t tinta, uint16_t fondo)
{
    uint16_t w = ancho_texto(s);
    texto_sobre((uint16_t)((borde > w) ? (borde - w) : 0U), y, s, tinta, fondo);
}

static void fila(uint16_t y, const char *etiq, const char *valor)
{
    uint16_t x = 24U;

    while (*etiq != '\0' && x < (COL_VALOR - 10U)) {
        x = letra(x, y, *etiq++, ETIQUETA, FONDO);
    }
    texto(COL_VALOR, y, valor, VALOR);
}


/* --- numeros sin printf (no hay libc aqui) -------------------------- */

static char *u2s(char *p, uint32_t v)
{
    char t[12]; int n = 0;
    if (v == 0U) { *p++ = '0'; *p = '\0'; return p; }
    while (v > 0U) { t[n++] = (char)('0' + (v % 10U)); v /= 10U; }
    while (n > 0) { *p++ = t[--n]; }
    *p = '\0';
    return p;
}

static char *x2s(char *p, uint32_t v, int cifras)
{
    static const char k[] = "0123456789ABCDEF";
    int i;
    for (i = cifras - 1; i >= 0; i--) { *p++ = k[(v >> (i * 4)) & 0xFU]; }
    *p = '\0';
    return p;
}

static char *pega(char *p, const char *s) { while (*s) { *p++ = *s++; } *p = '\0'; return p; }

/* --- la flash interna de verdad ------------------------------------- */

static void fmc_lee(uint32_t addr, uint8_t *buf, uint32_t len)
{
    memcpy(buf, (const void *)addr, len);
}

static uint8_t fmc_borra(uint32_t addr)
{
    uint32_t sector;

    /*
     * El mapa del manual (tabla 2-1): los sectores 5, 6 y 7 son los de
     * 128 kB que empiezan en 0x08020000, 0x08040000 y 0x08060000.
     *
     * Las dos puertas, y las dos importan:
     *  - por abajo, nunca el sector del propio cargador ni los de antes;
     *  - por arriba, nada mas alla de 0x08080000. Hoy no se llama con eso,
     *    pero el dia que la aplicacion crezca en un chip mas grande, sin
     *    esta linea un 0x08080000 caeria en el "else if" de arriba y
     *    borraria el sector 7 otra vez, en silencio.
     */
    if (addr >= CARGA_BORRA_HASTA) { return 0U; }
    if      (addr >= 0x08060000UL) { sector = CTL_SECTOR_NUMBER_7; }
    else if (addr >= 0x08040000UL) { sector = CTL_SECTOR_NUMBER_6; }
    else if (addr >= 0x08020000UL) { sector = CTL_SECTOR_NUMBER_5; }
    else                           { return 0U; }   /* nunca el del cargador */

    return (fmc_sector_erase(sector) == FMC_READY) ? 1U : 0U;
}

/*
 * LA BARRA DE PROGRESO - 01/10/2026.
 *
 * *** El dueno: "tiene que salir una barra de progreso cuando graba
 * update4.bin". *** Y sale de aqui, que es el unico sitio donde se sabe
 * por donde va: cargador_arranca() no vuelve hasta que termina, pero
 * llama a fmc_escribe() una vez por bloque y le pasa la direccion.
 *
 * Asi que el progreso se pinta desde el propio camino de escritura y no
 * hay que tocar cargador.c, que es el fichero clonado del de serie y el
 * que menos ganas tengo de mover.
 *
 * Se repinta solo cuando cambia el porcentaje -una de cada ~3.200
 * llamadas- porque pintar en cada bloque multiplicaria por varias veces
 * lo que tarda la grabacion.
 */
static uint32_t s_grabar_total;    /* bytes de la imagen, 0 = no pintar */
static uint8_t  s_grabar_pct;
static uint16_t s_grabar_px;       /* ancho ya relleno, para pintar solo lo nuevo */

static void progreso(uint32_t hechos)
{
    uint8_t pct;
    char t[8], *q;

    if (s_grabar_total == 0U) { return; }
    pct = (uint8_t)((hechos >= s_grabar_total) ? 100U
                    : (hechos * 100U) / s_grabar_total);
    if (pct == s_grabar_pct) { return; }
    s_grabar_pct = pct;

    /*
     * *** El dueno: "en la barra de progreso sale como una marca de azul
     * oscuro conforme avanza, una marca inclinada". ***
     *
     * Era el repintado a medias. Yo borraba la barra entera -752 x 28, mas
     * de veinte mil pixeles- y la rellenaba otra vez en cada 1%. Como el
     * panel se escribe fila a fila, el ojo pilla el relleno a mitad y el
     * borde se ve como un escalon diagonal. Cien veces seguidas.
     *
     * Ahora solo se pinta EL TROZO NUEVO. No hay nada que repintar, asi
     * que no hay nada que coger a medias, y de paso se escriben unos pocos
     * cientos de pixeles por paso en vez de veintiun mil: la barra deja de
     * robarle tiempo a la grabacion.
     */
    {
        uint16_t px = (uint16_t)((748U * pct) / 100U);
        if (px > s_grabar_px) {
            rect((uint16_t)(26U + s_grabar_px), 342U,
                 (uint16_t)(px - s_grabar_px), 24U, ACENTO);
            s_grabar_px = px;
        }
    }

    q = u2s(t, pct); q = pega(q, " %");
    borra_linea(380U);
    texto(24U, 380U, t, VALOR);
}

static uint8_t fmc_escribe(uint32_t addr, const uint8_t *d, uint32_t len)
{
    uint32_t i;

    if (addr < CARGA_APP_BASE) { return 0U; }
    progreso(addr - CARGA_APP_BASE);        /* jamas sobre el cargador */
    for (i = 0U; i < len; i += 4U) {
        uint32_t w = 0xFFFFFFFFUL;
        uint32_t n = (len - i >= 4U) ? 4U : (len - i);
        memcpy(&w, &d[i], n);                        /* lo que falte, a 0xFF */
        if (n < 4U) {
            uint32_t k;
            for (k = n; k < 4U; k++) { ((uint8_t *)&w)[k] = 0xFFU; }
        }
        if (fmc_word_program(addr + i, w) != FMC_READY) { return 0U; }
    }
    return 1U;
}

static const carga_fmc_t k_fmc = { fmc_lee, fmc_borra, fmc_escribe };

/* --- la ficha del equipo -------------------------------------------- */

static uint16_t s_flash_kb, s_sram_kb;

static void lee_el_chip(void)
{
    uint32_t d = *(volatile uint32_t *)0x1FFF7A20UL;
    s_flash_kb = (uint16_t)(d >> 16);
    s_sram_kb  = (uint16_t)(d & 0xFFFFU);
}

static void cabecera(const char *titulo, const char *pie)
{
    rect(0U, 0U, 800U, 46U, BARRA);
    rect(0U, 46U, 800U, 2U, ACENTO);
    texto_sobre(24U, 12U, titulo, ACENTO, BARRA);
    texto_derecha(776U, 12U, CARGADOR_VERSION, ETIQUETA, BARRA);
    if (pie != (const char *)0) { texto(24U, 58U, pie, ETIQUETA); }
}

static void ficha(void)
{
    char l[80], *p;
    spi_flash_jedec_id_t id;
    uint32_t f1, rz, dt, nc;
    uint16_t y = 100U;

    p = pega(l, "flash "); p = u2s(p, s_flash_kb);
    p = pega(p, " kB     SRAM "); p = u2s(p, s_sram_kb); p = pega(p, " kB");
    fila(y, "Chip", l); y = (uint16_t)(y + 28U);

    p = pega(l, ""); p = x2s(p, *(volatile uint32_t *)0x1FFF7A10UL, 8);
    p = pega(p, " "); p = x2s(p, *(volatile uint32_t *)0x1FFF7A14UL, 8);
    p = pega(p, " "); p = x2s(p, *(volatile uint32_t *)0x1FFF7A18UL, 8);
    fila(y, "Serie", l); y = (uint16_t)(y + 28U);

    p = pega(l, ""); p = u2s(p, SystemCoreClock / 1000000UL);
    p = pega(p, " MHz     depuracion "); p = x2s(p, *(volatile uint32_t *)0xE0042000UL, 8);
    fila(y, "Reloj", l); y = (uint16_t)(y + 28U);

    p = pega(l, "rama "); *p++ = (char)g_panel_rama; *p = '\0';
    p = pega(p, "     0x000A="); p = x2s(p, g_panel_id_0a, 4);
    p = pega(p, "  0x3A00=");    p = x2s(p, g_panel_id_3a, 4);
    fila(y, "Panel", l); y = (uint16_t)(y + 28U);

    spi_flash_read_jedec_id(&id);
    p = pega(l, ""); p = x2s(p, id.manufacturer_id, 2);
    p = pega(p, " "); p = x2s(p, id.memory_type, 2);
    p = pega(p, " "); p = x2s(p, id.capacity_code, 2);
    p = pega(p, "     "); p = u2s(p, (1UL << id.capacity_code) / 1024UL);
    p = pega(p, " kB");
    fila(y, "Flash SPI", l); y = (uint16_t)(y + 28U);

    if (spi_flash_geometria(&f1, &rz, &dt, &nc)) {
        p = pega(l, "FAT12     "); p = u2s(p, nc);
        p = pega(p, " clusters     raiz en el sector "); p = u2s(p, rz);
    } else {
        p = pega(l, "no se reconoce");
    }
    fila(y, "Volumen", l); y = (uint16_t)(y + 28U);

    rect(24U, (uint16_t)(y + 6U), 752U, 1U, SUAVE);

    if (s_flash_kb > 512U) {
        texto(24U, (uint16_t)(y + 18U),
              "AVISO: mas de 512 kB de flash; parte de la aplicacion puede caer "
              "fuera de la zona sin esperas.", AMBAR);
    }
}

/* --- los pines de la placa, copiados del cargador de serie ----------- */

/*
 * LOS SIETE PINES QUE EL CARGADOR DE SERIE PONE Y NADIE MAS - 01/10/2026.
 *
 * *** El dueno, tras grabar el cargador nuestro: "la radio se oye pero no
 * se ve". Y despues, con razon: "estaria bien que con esta cosa tan
 * delicada NO ADIVINASES". ***
 *
 * Habia adivinado dos veces -que era la retroiluminacion de PA3- y las dos
 * le costaron una grabacion. Asi que esto no sale de razonar: sale de
 * desensamblar el boot.bin de serie, que lo tenemos volcado entero.
 *
 * En 0x08002DD0 hay una funcion que enciende los relojes de GPIOH, A, E, D,
 * C y B, y acto seguido pone SIETE pines, escribiendo primero el nivel y
 * configurando despues la salida -que es el orden correcto, para que no
 * haya un pulso del valor que hubiera-:
 *
 *   0x08002E46  gpio_bit_write(GPIOC, 0x0080, 0)   PC7 = 0
 *   0x08002E50  gpio_bit_write(GPIOA, 0x0100, 0)   PA8 = 0
 *   0x08002E60  gpio_bit_write(GPIOD, 0x0040, 1)   PD6 = 1
 *   0x08002E6E  gpio_bit_write(GPIOB, 0x0040, 1)   PB6 = 1
 *   0x08002E7C  gpio_bit_write(GPIOB, 0x0001, 1)   PB0 = 1
 *   0x08002E86  gpio_bit_write(GPIOB, 0x0002, 0)   PB1 = 0
 *   0x08002E90  gpio_bit_write(GPIOC, 0x0020, 0)   PC5 = 0
 *
 * y luego los declara salida empujada, velocidad minima, sin resistencia
 * (0x08002EAA en adelante), mas PC9 como entrada con bajada, que es el
 * boton del mando y ese ya lo tenemos.
 *
 * DE ESOS SIETE, LA APLICACION SOLO TOCA UNO: PA8, que es el LED
 * (led_gpio_init(), main.c 20906). Los otros SEIS -PB0, PB1, PB6, PC5, PC7
 * y PD6- no aparecen en todo el firmware. O sea que son responsabilidad
 * exclusiva del cargador y la aplicacion los hereda puestos, igual que
 * hereda el panel: el mismo contrato que main.c linea 912 deja escrito para
 * el rm68120_init() y que yo no mire antes de escribir un cargador nuevo.
 *
 * NO SE CUAL ES CUAL, y no hace falta saberlo para hacer esto bien. Uno
 * sera la retroiluminacion, otro probablemente la alimentacion del panel, y
 * PD6 cae justo al lado del CS del EXMC (PD7). Se ponen LOS SIETE, en el
 * MISMO ORDEN y con LOS MISMOS VALORES. Es la misma disciplina que con
 * cargador.c: copiar lo que hace, no lo que uno cree que deberia hacer.
 *
 * Y van lo primero de main(), antes de leer el chip y antes del panel: si
 * algo mas abajo se cuelga, al menos la pantalla esta encendida y se ve
 * hasta donde llego.
 */
static void pines_de_la_placa(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOE);

    /* Primero los niveles, con los pines todavia en su estado de reset. */
    gpio_bit_reset(GPIOC, GPIO_PIN_7);
    gpio_bit_reset(GPIOA, GPIO_PIN_8);
    gpio_bit_set  (GPIOD, GPIO_PIN_6);
    gpio_bit_set  (GPIOB, GPIO_PIN_6);
    gpio_bit_set  (GPIOB, GPIO_PIN_0);
    gpio_bit_reset(GPIOB, GPIO_PIN_1);
    gpio_bit_reset(GPIOC, GPIO_PIN_5);

    /* Y ahora salidas, en el mismo orden que el de serie. */
    gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_7);
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_7);

    gpio_mode_set(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_8);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_8);

    gpio_mode_set(GPIOD, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_6);
    gpio_output_options_set(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_6);

    gpio_mode_set(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_6);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_6);

    gpio_mode_set(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                  GPIO_PIN_0 | GPIO_PIN_1);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ,
                  GPIO_PIN_0 | GPIO_PIN_1);

    gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_5);
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_5);

    /*
     * Y LA RETROILUMINACION, TAMBIEN COPIADA - 01/10/2026.
     *
     * Antes puse aqui PA3 como salida digital a cero razonando que
     * "activa a nivel bajo" = a cero es el maximo. Era razonamiento mio, y
     * se vio apagada. Esto ya no lo es: sale de 0x08002F80 del boot.bin.
     *
     *   0x08002F8C  prescaler = 7
     *   0x08002F92  periodo (ARR) = 375
     *   0x08002FC0  modo = 0x60 -> PWM1 de ST, que es PWM0 en GigaDevice
     *   0x08002FC4  pulso (CCR) = 188, polaridad ALTA
     *   0x08002FCA  canal = 12 -> canal 4 de ST = TIMER_CH_3 aqui
     *   0x08002FD8  y su MspPostInit pone PA3 en AF2 = TIMER4_CH3
     *
     * O sea: una onda cuadrada al 50% de ciclo de trabajo (188 de 376),
     * no un nivel continuo. Con el reloj del cargador a 192 MHz el TIMER4
     * ve 96, con el prescaler a 7 son 12 MHz, y entre 376 salen 31,9 kHz.
     *
     * Y eso explica por que mi nivel continuo no valia: si el driver de
     * los LEDs es un elevador con entrada de PWM, un nivel fijo no lo hace
     * conmutar y da lo que de. No es que yo pusiera mal la polaridad; es
     * que la señal no era una señal.
     *
     * La aplicacion luego le pone SU PWM encima (TIMER1 por AF1,
     * backlight.c). Si ese camino funciona, esto no estorba. Si no
     * funciona -backlight.h avisa de que ese mapeo nunca se verifico en
     * esta placa- entonces esto es justo lo que faltaba, porque el TIMER4
     * se queda corriendo y la aplicacion solo le quita el pin.
     */
    {
        timer_parameter_struct    t;
        timer_oc_parameter_struct oc;

        rcu_periph_clock_enable(RCU_TIMER4);

        gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_3);
        gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_3);
        gpio_af_set(GPIOA, GPIO_AF_2, GPIO_PIN_3);

        timer_deinit(TIMER4);
        timer_struct_para_init(&t);
        t.prescaler         = 7U;
        t.alignedmode       = TIMER_COUNTER_EDGE;
        t.counterdirection  = TIMER_COUNTER_UP;
        t.clockdivision     = TIMER_CKDIV_DIV1;
        t.period            = 375U;
        t.repetitioncounter = 0U;
        gd32_timer_init(TIMER4, &t);

        timer_channel_output_struct_para_init(&oc);
        oc.outputstate = TIMER_CCX_ENABLE;
        oc.ocpolarity  = TIMER_OC_POLARITY_HIGH;
        timer_channel_output_config(TIMER4, TIMER_CH_3, &oc);
        timer_channel_output_mode_config(TIMER4, TIMER_CH_3, TIMER_OC_MODE_PWM0);
        timer_channel_output_pulse_value_config(TIMER4, TIMER_CH_3, 188U);

        timer_auto_reload_shadow_enable(TIMER4);
        timer_enable(TIMER4);
    }
}

/* --- el boton del mando --------------------------------------------- */

/*
 * PC9, activo ALTO, con resistencia de bajada. Es el mismo pin y la misma
 * configuracion que encoder_init() del firmware (User/encoder.c, lineas 9,
 * 10 y 98); si algun dia cambia ahi, cambia aqui.
 *
 * *** El dueno: "teniendo pulsado el encoder al encender". *** Se lee
 * VARIAS veces a lo largo de 30 ms en vez de una: un pin recien
 * configurado, con la radio arrancando y la pantalla encendiendose al
 * lado, puede dar un rebote. Entrar en modo actualizacion por un rebote
 * seria molesto; no entrar teniendolo pulsado, peor. Por eso se exige que
 * salga pulsado en las DIECISEIS lecturas.
 */
extern void delay_ms(uint32_t ms);   /* la del driver del panel */

static uint8_t boton_pulsado(void)
{
    uint32_t i, si = 0U;

    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_mode_set(GPIOC, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, GPIO_PIN_9);

    for (i = 0U; i < 16U; i++) {
        if (gpio_input_bit_get(GPIOC, GPIO_PIN_9) == SET) { si++; }
        delay_ms(2U);
    }
    return (si == 16U) ? 1U : 0U;
}

/* --- la puerta de atras ---------------------------------------------
 *
 * El salto a la ROM de fabrica vivia aqui. Se ha mudado a atajo_dfu.c, que
 * corre desde SystemInit(), antes que el reloj y antes que main(): es el
 * unico sitio desde el que la ROM arranca bien, y el unico que sigue
 * sirviendo si este cargador se cuelga mas adelante.
 */

/* --- modo actualizacion: la pantalla y el disco ---------------------- */

static void modo_actualizacion(void)
{
    char l[80], *p;

    rm68120_fill_screen(FONDO);
    cabecera("MODO ACTUALIZACION",
             "La radio es un disco USB. Copia update4.bin y reinicia.");
    ficha();

    /*
     * AQUI HABIA UNA CUENTA ATRAS DE 3 s PARA ENTRAR EN DFU, Y SE HA
     * QUITADO - 01/10/2026.
     *
     * No podia funcionar, y es la misma historia de las doce versiones del
     * boton de la aplicacion: a estas alturas el PLL lleva rato a 199,68
     * MHz, y la ROM de fabrica mide el cristal dando por hecho que arranca
     * desde el reset. Ademas le faltaban el NVIC limpio, el VTOR a cero y
     * el tiron de DP a masa. O sea: el camino correcto es el de
     * atajo_dfu.c, que corre ANTES de que nada de esto haya pasado.
     *
     * Una sola puerta, y la que esta probada. Dos puertas de las cuales una
     * no abre es peor que una.
     */
    texto(16U, 330U,
          "DFU de fabrica: al encender, manten el mando pulsado Y GIRALO.",
          GRIS);

    p = pega(l, "Disco  "); p = u2s(p, disco_bloques());
    p = pega(p, " bloques de 512  = "); p = u2s(p, disco_bloques() / 2U);
    p = pega(p, " kB");
    texto(16U, 290U, l, BLANCO);

    /*
     * AQUI VA EL USB, y todavia no esta. La clase MSC se apoya en
     * disco_lee()/disco_escribe() de User/disco.c, que YA estan escritas y
     * probadas en el PC (sim/disco.c: escribir un bloque de 512 no se lleva
     * por delante los otros siete de su sector de 4 kB, en los ocho
     * desplazamientos). Lo que falta es solo el transporte.
     *
     * Mientras no este, esta pantalla sirve igual para lo que importa hoy:
     * comprobar que el boton se detecta y que la ficha del chip es correcta.
     */
    /*
     * Y AQUI SE ENCIENDE EL USB. La secuencia es la del ejemplo msc_udisk
     * de GigaDevice y no me la he inventado: GPIO, reloj, temporizador de
     * retardos, usbd_init() y las interrupciones.
     *
     * El nucleo es el USBFS (PA11/PA12). No es una suposicion: el gestor
     * original carga 0x50000000 -la base del USBFS- en 0x08004616, y la
     * base del USBHS no aparece en todo el binario.
     */
    texto(16U, 360U, "USB: conectando...", GRIS);

    usb_gpio_config();
    usb_rcu_config();
    usb_timer_init();
    usbd_init(&g_usb, USB_CORE_ENUM_FS, &msc_desc, &msc_class);
    usb_intr_config();

    borra_linea(360U);
    texto(16U, 360U, "USB: listo. Copia update4.bin y reinicia.", VERDE);
    texto(16U, 390U, "Para arrancar la radio: apaga y enciende SIN pulsar.", GRIS);

    /*
     * AQUI HABIA UNA LINEA DE DIAGNOSTICO, Y SE HA QUITADO - 01/10/2026.
     *
     * Pintaba "int / est / lee / esc / blq / mal" cuatro veces por segundo:
     * interrupciones del USB atendidas, estado de la pila, peticiones de
     * la clase servidas y fallos.
     *
     * Fue lo que resolvio el atasco del disco. El "blq 4096" en un disco de
     * 2048 bloques delato que la clase MSC pasa DIRECCIONES EN BYTES y no
     * numeros de bloque, despues de media tarde de teorias mias sobre el
     * reloj y la velocidad del SPI que ya estaban bien. Pero ya cumplio, y
     * una pantalla que funciona no tiene que enseñar sus tripas.
     *
     * Los contadores se quedan en disco_msc.c y usb_it.c: cuestan diecisiete
     * bytes y son el unico instrumento que hay aqui dentro. Si el disco
     * vuelve a fallar algun dia, se vuelve a pintar esa linea y la discusion
     * se acaba en treinta segundos en vez de en media tarde.
     */
    while (1) { }
}

/* --- el salto -------------------------------------------------------- */

/*
 * EL SALTO A LA APLICACION, CON LO APRENDIDO LA NOCHE DEL 01/10/2026.
 *
 * La primera version solo hacia __disable_irq(), VTOR y MSP. Y eso es
 * exactamente el fallo que nos costo doce versiones en el salto a la ROM:
 *
 *   - __disable_irq() solo pone la MASCARA. Las interrupciones siguen
 *     HABILITADAS en el NVIC y pueden estar apuntadas como pendientes. La
 *     aplicacion hace __enable_irq() en su main(), y en ese instante se come
 *     todo lo que este cargador hubiera dejado armado, contra SU tabla de
 *     vectores. La aplicacion no tiene manejador del USBFS ni del TIMER2:
 *     caeria en el manejador por defecto, que es un bucle infinito.
 *
 *   - El SysTick lo enciende este cargador para su delay_ms(). Hay que
 *     pararlo: si no, sigue latiendo mientras la aplicacion monta el suyo.
 *
 * Son las dos mismas lecciones del salto a la ROM, aplicadas aqui antes de
 * que nos muerdan.
 */
static void salta_a_la_app(void)
{
    uint32_t sp = *(volatile uint32_t *)CARGA_APP_BASE;
    uint32_t pc = *(volatile uint32_t *)(CARGA_APP_BASE + 4UL);
    void (*app)(void) = (void (*)(void))pc;
    uint32_t i;

    __disable_irq();

    SysTick->CTRL = 0UL;
    SysTick->LOAD = 0UL;
    SysTick->VAL  = 0UL;

    for (i = 0U; i < 8U; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    SCB->VTOR = CARGA_APP_BASE;
    __DSB();
    __ISB();

    __set_MSP(sp);
    app();
    while (1) { }                 /* no se vuelve de ahi */
}

int main(void)
{
    carga_r_t r;

    extern void reloj_de_milisegundos(void);

    /*
     * LO PRIMERO DE TODO, Y CASI SE ME ESCAPA.
     *
     * El cargador enlaza el MISMO system_gd32f4xx.c que la aplicacion, y ese
     * SystemInit() termina con "SCB->VTOR = 0x08020000". O sea que al llegar
     * aqui la tabla de vectores apunta a la tabla de la APLICACION, que
     * todavia no se ha comprobado y puede ser basura. La primera interrupcion
     * -el SysTick que se enciende tres lineas mas abajo- saltaria ahi.
     *
     * Se devuelve a 0x08000000, que es donde esta la nuestra.
     */
    SCB->VTOR = 0x08000000UL;
    __DSB();

    /*
     * EL REGULADOR, ANTES DE CORRER A 192 MHz - 01/10/2026.
     *
     * *** El dueno: "quiero estar 100% seguro de que no nos falta nada que
     * el original active". *** Pues faltaba esto, y salio de rastrear el
     * codigo del de serie desde el vector de reset anotando cada escritura
     * a un periferico. El PMU era el unico que aparecia y que nosotros no
     * tocabamos:
     *
     *   0x08003E4C   RCU_APB1EN |= 0x10000000   el reloj del PMU
     *   0x08003E60   PMU_CTL    |= 0x4000       LDOVS, el voltaje del LDO
     *
     * El de serie pone el bit 14 y corre a 144 MHz. Nosotros corremos a
     * 192, asi que el LDO tiene que dar su tension ALTA -los dos bits,
     * 15 y 14-. Si el valor de reset ya los trae puestos esto no cambia
     * nada y no estorba; si no, estabamos corriendo el nucleo por debajo
     * de lo que pide su frecuencia, que es la clase de cosa que no se ve
     * hasta que un dia se ve.
     *
     * Va lo primero, antes de que SystemInit haya... no: SystemInit ya ha
     * corrido cuando llegamos aqui. Por eso se pone HIGH y se espera a la
     * bandera, en vez de darlo por hecho.
     */
    rcu_periph_clock_enable(RCU_PMU);
    pmu_ldo_output_select(PMU_LDOVS_HIGH);
    { uint32_t t; for (t = 0U; t < 100000U; t++) { __NOP(); } }

    pines_de_la_placa();

    lee_el_chip();
    reloj_de_milisegundos();   /* delay_ms() del driver del panel lo necesita */

    rm68120_exmc_gpio_init();
    rm68120_exmc_bus_init();
    rm68120_hw_reset();
    /*
     * La puesta en marcha del panel va por panel_serie.c, NO por
     * rm68120_init(). Ver la cabecera de panel_serie.c: la secuencia del
     * NT35510 que usa rm68120_init() deja el canal rojo a tope -el NEGRO
     * sale rojo, el VERDE amarillo- y encima hay TRES secuencias segun el
     * identificador del panel, no una.
     */
    panel_serie_init();
    rm68120_fill_screen(FONDO);

    /*
     * EL ARRANQUE NORMAL NO ENSEÑA NADA - 01/10/2026.
     *
     * *** El dueno: "el arranque sin tocar nada tiene que ser transparente,
     * no se tiene que ver absolutamente nada". *** Y tiene razon: un
     * cargador que va bien no tiene nada que contar. Enseñar la ficha en
     * cada encendido es ponerle al usuario delante las tripas de algo que
     * funciona.
     *
     * El panel SI se arranca siempre -no es opcional: la aplicacion no lo
     * inicializa y depende de encontrarselo hecho, ver main.c linea 912-,
     * pero se deja en negro y no se escribe una letra.
     *
     * La pantalla sale en tres casos, y los tres son "esto no es un
     * arranque normal":
     *
     *   1. mando pulsado        -> modo actualizacion, que es una pantalla
     *                              entera y tiene que verse;
     *   2. hay update4.bin      -> grabar tarda segundos y en silencio
     *                              pareceria colgada;
     *   3. algo va mal          -> el motivo, que es justo cuando hace
     *                              falta leerlo.
     */
    spi_flash_init();

    if (boton_pulsado()) {
        cabecera("DEEPSDR 101  -  cargador de arranque",
                 "Copia update4.bin al disco USB y reinicia para actualizar");
        ficha();
        modo_actualizacion();                      /* no vuelve */
    }

    /*
     * Y aqui, en silencio. cargador_arranca() solo tarda si hay algo que
     * grabar; si no, son unas lecturas del sistema de ficheros y ya.
     *
     * El aviso de "grabando" se pinta ANTES de llamar, porque mientras
     * graba no se vuelve: son 320 kB a traves del bus de la flash y la
     * pantalla no se refresca hasta que termina.
     */
    {
        uint32_t cl = 0U;
        s_grabar_total = cargador_busca_update4(&cl);
    }
    if (s_grabar_total != 0U) {
        cabecera("DEEPSDR 101  -  actualizando",
                 "No apagues la radio hasta que termine.");
        ficha();
        texto(24U, 300U, "Grabando update4.bin en la flash interna...", AMBAR);
        /* El marco de la barra, una sola vez. A partir de aqui solo se
         * pinta lo que avanza. */
        rect(24U, 340U, 752U, 28U, BARRA);
        s_grabar_pct = 255U;
        s_grabar_px  = 0U;
        progreso(0U);
    }

    fmc_unlock();
    r = cargador_arranca(&k_fmc);
    fmc_lock();

    if (r == CARGA_ARRANCA || r == CARGA_GRABADA) {
        salta_a_la_app();                          /* no vuelve */
    }

    /*
     * Solo se llega aqui si algo fue mal, y entonces si hay que contarlo:
     * si no, la radio se quedaria en negro sin decir por que.
     */
    rm68120_fill_screen(FONDO);
    cabecera("DEEPSDR 101  -  no se puede arrancar", (const char *)0);
    ficha();
    texto(24U, 300U, cargador_porque_txt(r), ROJO);
    texto(24U, 330U, "Copia un update4.bin valido al disco USB: enciende con "
                     "el mando pulsado.", ETIQUETA);

    /* Como el de siempre: si la imagen no vale, aqui se queda. El mensaje
     * esta en pantalla y el siguiente arranque vuelve a mirar el disco. */
    while (1) { }
}
