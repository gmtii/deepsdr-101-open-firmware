#include <string.h>
#include "waterfall.h"
#include "gfx2.h"
#include "ipa_blit.h"
#include "gfx.h"
#include "rm68120_exmc.h"

/* .bss, RAM principal (0x20000000). WATERFALL_WIDTH*WATERFALL_ROWS*1 byte,
 * ver presupuesto documentado en waterfall.h antes de subir WATERFALL_ROWS.
 *
 * MODELO DE ANILLO (reescrito 30/07/2026): la version anterior hacia un
 * memmove de (ROWS-1)*WIDTH*2 = ~94KB en CADA push_line para desplazar
 * fisicamente las filas. Ahora el buffer es un anillo: s_head apunta a
 * la fila LOGICA 0 (la mas reciente) y push_line solo retrocede el
 * indice y copia la fila nueva (1.6KB) - el "scroll" es contabilidad,
 * no movimiento de memoria. El coste se paga (barato) en el blit, que
 * vuelca el anillo en dos tramos contiguos.
 */
/* ---------------------------------------------------------------------
 * MEMORIA COMPARTIDA CON FT8 (24/09/2026)
 * ---------------------------------------------------------------------
 * Aqui vivia "static uint8_t s_buf[WATERFALL_ROWS][WATERFALL_WIDTH]".
 * Sigue viviendo, byte por byte: lo unico que cambia es que ahora es un
 * miembro de una union declarada en ft8_shared_ram.h, y el otro miembro
 * de esa union es todo el area de trabajo del decodificador de FT8.
 *
 * POR QUE. FT8 necesita 47.360 bytes de tablas y magnitudes. Libres solo
 * quedaban unos 19.000. La cascada ya tenia 54.432 apartados para ella
 * sola. Como FT8 y la cascada NO pueden estar activos a la vez -en FT8 la
 * pantalla ensena la lista de mensajes, no la cascada- prestarle el
 * buffer sale gratis: no se pide ni un byte nuevo de RAM.
 *
 * LA TRAMPA, ESCRITA AQUI PARA QUE NO SE OLVIDE: "no a la vez" no es una
 * opinion, es una condicion que hay que cumplir. Si alguna vez se pinta
 * la cascada mientras FT8 esta decodificando, lo que salga en pantalla
 * seran las magnitudes de FT8 interpretadas como colores, y lo que FT8
 * decodifique sera basura. Ninguna de las dos cosas dara un fallo: las
 * dos "funcionaran" mal y en silencio. Por eso waterfall_init() se llama
 * al salir de FT8 y ft8_decoder_init() al entrar, y por eso cualquier
 * modo nuevo que quiera dibujar cascada tiene que mirar antes si FT8
 * esta en marcha.
 *
 * El nombre s_buf se mantiene con un #define para que el resto del
 * fichero -el anillo, el blit, todo- siga exactamente igual que estaba.
 */
#include "ft8_shared_ram.h"

ft8_shared_ram_t g_ft8_shared_ram;

#define s_buf (g_ft8_shared_ram.waterfall_buf)
static uint16_t s_head = 0; /* indice fisico de la fila logica 0 */

/* 1 mientras FT8 tiene el buffer. Ver waterfall.h para por que esto es un
 * cerrojo aqui dentro y no una regla que cumplir fuera. */
static uint8_t s_prestada = 0U;

void waterfall_presta(uint8_t si)
{
    s_prestada = (uint8_t)(si ? 1U : 0U);
}

uint8_t waterfall_prestada(void) { return s_prestada; }

void waterfall_init(void)
{
    /*
     * LA UNION ENTERA, no solo el miembro de la cascada - 25/09/2026.
     *
     * Esto borraba s_buf, que son los bytes de la cascada. Y mientras FT8
     * cupiera dentro de ellos daba igual... pero eso era una coincidencia de
     * tamanos, no una garantia, y estaba sostenida por un _Static_assert que
     * saltaba en cuanto FT8 pedia una ventana mas ancha.
     *
     * El problema real que evitaba ese aserto: las tablas de la FFT de FT8
     * llevan un sello que dice "estas tablas son buenas" (ver
     * ft8_shared_ram.h). Ese sello solo sirve si se borra EXACTAMENTE cuando
     * se borran las tablas que avala. Si el memset cubriera unas y el sello
     * no -o al reves-, el sello mentiria, y un sello que miente es peor que
     * no tener ninguno: la radio se colgo una vez justo por usar esas tablas
     * sin construir.
     *
     * Borrando la union entera la garantia deja de depender de que un miembro
     * sea mayor que el otro. Devolver el buffer es devolverlo todo, y lo que
     * hubiera dentro -pixeles o magnitudes- se va junto. Cuesta lo mismo:
     * este memset ya barria los 54 kB.
     */
    memset(&g_ft8_shared_ram, 0, sizeof g_ft8_shared_ram);
    s_head = 0;
}

void waterfall_push_line(const uint8_t *line)
{
    /* Prestado a FT8: escribir aqui seria corromperle las magnitudes. */
    if (s_prestada) { return; }

    /* Retrocede el head (la fila que era la mas antigua pasa a ser la
     * nueva fila 0) y escribe encima. Solo 1 fila copiada. */
    s_head = (uint16_t)((s_head + WATERFALL_ROWS - 1U) % WATERFALL_ROWS);
    memcpy(&s_buf[s_head][0], line, WATERFALL_WIDTH);
}

/* ---------------------------------------------------------------------
 * EL VOLCADO POR IPA. 24/09/2026.
 * ---------------------------------------------------------------------
 * Hace lo mismo que gfx2_wf_blit() pero la conversion de indice de paleta a
 * RGB565 la hace el acelerador grafico en vez de la CPU. Ver ipa_blit.h.
 *
 * El anillo obliga a partir cada tira en dos como mucho: las filas logicas
 * 0..n van seguidas en memoria hasta que se da la vuelta por el final del
 * buffer, y a partir de ahi siguen al principio. O sea, uno o dos envios al
 * IPA por tira, nunca uno por fila.
 *
 * El segundo tramo -empujar la banda al panel- sigue siendo de la CPU: el
 * IPA no sabe escribir en una direccion fija. Eso le toca a un DMA, que va
 * aparte.
 *
 * Devuelve 0 si el IPA no pudo con alguna tira. En ese caso NO se ha
 * escrito nada en el panel todavia -la comprobacion se hace antes de abrir
 * ninguna ventana- y el que llama repite por el camino de siempre. */
static uint8_t wf_blit_ipa(uint16_t x, uint16_t y, const uint16_t *lut)
{
    /* Dos medias bandas, por lo mismo que en spectrum.c: el IPA convierte
     * una mientras el DMA envia la otra. Ver gfx_blit_arranca(). */
    uint16_t *banda  = gfx2_banda_coge();
    uint16_t  altura = gfx2_banda_filas();
    uint16_t *buf[2];
    uint8_t   cual = 0U, vuela = 0U;
    int16_t   hechas = 0;

    if (banda == (uint16_t *)0) {
        return 0U;   /* que lo haga el camino de siempre */
    }
    {
        uint32_t caben = gfx2_banda_pixeles() / (2UL * (uint32_t)WATERFALL_WIDTH);

        if (caben < (uint32_t)altura) { altura = (uint16_t)caben; }
        if (altura == 0U) { altura = 1U; }
    }
    buf[0] = banda;
    buf[1] = banda + ((uint32_t)altura * WATERFALL_WIDTH);

    while (hechas < (int16_t)WATERFALL_ROWS) {
        int16_t tira = (int16_t)(WATERFALL_ROWS - hechas);
        uint16_t p0, n1;

        if (tira > (int16_t)altura) { tira = (int16_t)altura; }

        p0 = (uint16_t)((s_head + (uint16_t)hechas) % WATERFALL_ROWS);
        n1 = (uint16_t)(WATERFALL_ROWS - p0);
        if (n1 > (uint16_t)tira) { n1 = (uint16_t)tira; }

        if (!ipa_blit_l8(&s_buf[p0][0], WATERFALL_WIDTH,
                         buf[cual], WATERFALL_WIDTH,
                         WATERFALL_WIDTH, n1, lut)) {
            if (vuela) { (void)gfx_blit_espera(); }
            gfx2_banda_suelta();
            return 0U;
        }
        if (n1 < (uint16_t)tira) {
            if (!ipa_blit_l8(&s_buf[0][0], WATERFALL_WIDTH,
                             buf[cual] + ((int32_t)n1 * WATERFALL_WIDTH), WATERFALL_WIDTH,
                             WATERFALL_WIDTH, (uint16_t)(tira - (int16_t)n1), lut)) {
                if (vuela) { (void)gfx_blit_espera(); }
                gfx2_banda_suelta();
                return 0U;
            }
        }

        /* Se espera al envio ANTERIOR aqui, que es lo ultimo posible: entre
         * medias ha cabido la conversion entera de esta tira. */
        if (vuela) { (void)gfx_blit_espera(); vuela = 0U; }
        vuela = gfx_blit_arranca(x, (uint16_t)(y + hechas),
                                 WATERFALL_WIDTH, (uint16_t)tira, buf[cual]);
        cual = (uint8_t)(cual ^ 1U);

        hechas = (int16_t)(hechas + tira);
    }
    if (vuela) { (void)gfx_blit_espera(); }
    gfx2_banda_suelta();
    return 1U;
}

void waterfall_blit(uint16_t x, uint16_t y, const uint16_t *lut)
{
    /* Prestado a FT8: lo que hay ahi no son indices de paleta. */
    if (s_prestada) { return; }

    /* gfx2_wf_blit() recorre el historial circular desde s_head, convierte
     * cada indice con la LUT dentro de la banda compositora y saca cada
     * trozo con UNA sola ventana del panel. Sustituye a los dos gfx_blit()
     * que hacian falta antes para dar la vuelta al buffer circular.
     *
     * Desde el 24/09/2026 hay ademas un camino por IPA. Se intenta ese
     * primero y, si no puede, se cae al de siempre - que no es un apano:
     * es el que hay que poder comparar contra el otro sin recompilar. */
    if (ipa_blit_hay()) {
        if (wf_blit_ipa(x, y, lut)) {
            return;
        }
    }
    gfx2_wf_blit((int16_t)x, (int16_t)y, WATERFALL_WIDTH, WATERFALL_ROWS,
                 &s_buf[0][0], WATERFALL_WIDTH, (int16_t)s_head, lut);
}

uint8_t *waterfall_row(uint16_t row)
{
    if (row >= WATERFALL_ROWS) {
        return NULL;
    }
    return &s_buf[(row + s_head) % WATERFALL_ROWS][0];
}
