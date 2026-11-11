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

/* "El proximo volcado, entero". Vive aqui arriba y no al lado de s_bucle
 * porque waterfall_presta() y waterfall_init(), que la ponen, estan antes.
 * El porque esta donde se usa, en waterfall_blit(). */
static uint8_t s_todo = 1U;

/*
 * LINEAS EMPUJADAS DESDE EL ULTIMO VOLCADO - 08/11/2026, revision a fondo.
 *
 * Y ES UN FALLO MIO DE LA V6.65. En "Muy rapida" el bucle principal empuja
 * DOS lineas y vuelca UNA sola vez (ver el camino `dos` de main.c, que
 * reparte el fotograma en dos mitades). La cascada desplazada no se entera
 * porque repinta las 72 filas; la de bucle dibujaba solo la ultima, asi que
 * LA MITAD DE LAS LINEAS NO LLEGABA A LA PANTALLA.
 *
 * Asi que se cuenta cuantas van desde el ultimo volcado y se dibujan todas.
 * Se acota a las filas que hay: si alguna vez se empujaran mas de 72 entre
 * dos volcados, dibujarlas todas es exactamente repintar la cascada entera.
 */
static uint8_t s_pend = 0U;

void waterfall_presta(uint8_t si)
{
    s_prestada = (uint8_t)(si ? 1U : 0U);
    s_todo = 1U;   /* al devolverlo, lo de debajo ya no vale */
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
    s_todo = 1U;   /* ver waterfall_entero() */
    s_pend = 0U;
}

void waterfall_push_line(const uint8_t *line)
{
    /* Prestado a FT8: escribir aqui seria corromperle las magnitudes. */
    if (s_prestada) { return; }

    /* Retrocede el head (la fila que era la mas antigua pasa a ser la
     * nueva fila 0) y escribe encima. Solo 1 fila copiada. */
    s_head = (uint16_t)((s_head + WATERFALL_ROWS - 1U) % WATERFALL_ROWS);
    memcpy(&s_buf[s_head][0], line, WATERFALL_WIDTH);
    if (s_pend < (uint8_t)WATERFALL_ROWS) { s_pend++; }
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

/*
 * LA CASCADA DENTRO DE UNA BANDA QUE TRAE OTRO - 05/10/2026.
 *
 * Para la captura de pantalla. Aqui no hay IPA ni DMA que valgan -el destino
 * es un trozo de memoria, no el panel-, asi que la conversion de indice de
 * paleta a RGB565 la hace la CPU, que es exactamente lo que hace el camino
 * de siempre cuando el IPA no esta. Son 54.432 busquedas en una tabla, una
 * vez por captura: no corre prisa.
 *
 * Prestada a FT8 no se pinta: lo que hay ahi dentro no son indices.
 */
void waterfall_pinta_en(gfx2_surf_t *sf, int16_t px, int16_t py,
                        const uint16_t *lut)
{
    int16_t y0, y1, r, dx;

    if (sf == 0 || lut == 0 || s_prestada) { return; }

    y0 = (int16_t)(sf->y - py);
    if (y0 < 0) { y0 = 0; }
    y1 = (int16_t)((int32_t)sf->y + sf->h - py);
    if (y1 > (int16_t)WATERFALL_ROWS) { y1 = (int16_t)WATERFALL_ROWS; }
    if (y1 <= y0) { return; }

    dx = (int16_t)(px - sf->x);
    if (dx < 0 || (int32_t)dx + WATERFALL_WIDTH > sf->w) { return; }

    for (r = y0; r < y1; r++) {
        uint16_t fila = (uint16_t)((s_head + (uint16_t)r) % WATERFALL_ROWS);
        const uint8_t *src = &s_buf[fila][0];
        int16_t dy = (int16_t)((int32_t)py + r - sf->y);
        uint16_t *dst;
        uint16_t c;

        if (dy < 0 || dy >= sf->h) { continue; }
        dst = &sf->px[(int32_t)dy * sf->w + dx];
        for (c = 0U; c < (uint16_t)WATERFALL_WIDTH; c++) {
            dst[c] = lut[src[c]];
        }
    }
}

/*
 * LA CASCADA EN BUCLE - 06/11/2026.
 *
 * La de siempre se DESPLAZA: la fila i de la pantalla enseña una fila
 * distinta del historial en cada fotograma, asi que al llegar UNA linea hay
 * que volcar LAS SETENTA Y DOS. Son 54.432 escrituras al panel para enseñar
 * 756 puntos.
 *
 * En bucle no se desplaza nada: la fila i de la pantalla es siempre la fila
 * i del anillo, y la linea nueva se escribe en el sitio que le toca. Una
 * fila, 756 escrituras. **Setenta y dos veces menos.**
 *
 * POR QUE ESTO Y NO IR MAS DESPACIO. Medido en la radio: el siseo de la FM
 * escala con los volcados por segundo y no con lo que tarda cada
 * escritura - un barrido del tiempo de dato del bus de 4 a 15 ciclos dejo
 * la vuelta un 45% mas lenta y el siseo igual -. Lo que hay que bajar es la
 * cuenta de escrituras.
 *
 * LO QUE SE PAGA ES COMO SE VE: en vez de deslizarse, la imagen se queda
 * quieta y hay una linea de escritura que la recorre, dejando el salto de
 * tiempo a su paso. Es como lo hacen muchos equipos, pero no es lo mismo, y
 * por eso va con interruptor y no puesto a la fuerza. Vive en la ventana de
 * Informacion, con los otros interruptores de experimento -IPA, DMA, bus,
 * pendiente-, que es donde van las cosas que hay que poder encender y
 * apagar escuchando.
 */
/*
 * Y ARRANCA APAGADO OTRA VEZ - 11/11/2026.
 *
 * La V6.66 lo puso a 1 a la fuerza, razonando que un interruptor que hay
 * que ir a buscar es un arreglo que no arregla nada. El razonamiento era
 * bueno y la decision mala, porque se llevo por delante COMO SE VE la
 * cascada, que es lo que el usuario mira todo el rato.
 *
 * En bucle la imagen no se desliza: se queda quieta y hay una linea de
 * escritura que la recorre, dejando el salto de tiempo a su paso. Eso esta
 * escrito ahi arriba desde el primer dia y se acepto como "lo que se
 * paga". Pues no se paga: un usuario lo conto como que la radio iba lenta
 * y que se veia escanear. No iba lenta -en coste el bucle es mas barato,
 * 756 escrituras por cuadro contra 54.432- pero daba igual, porque lo que
 * el ve es una cascada que ya no corre.
 *
 * El interruptor se queda donde estaba, en la ventana de Informacion, para
 * quien quiera el bucle. Lo que vuelve a su sitio es lo que la radio hace
 * sin que nadie toque nada.
 */
static uint8_t s_bucle = 0U;

/*
 * Y UN VOLCADO ENTERO PENDIENTE - 06/11/2026, encontrado por sim/captura.
 *
 * La cascada desplazada tiene una propiedad que no se ve hasta que se
 * quita: como repinta las 72 filas en cada linea, **se cura sola**. Si algo
 * pinta encima -un cartel, la captura de pantalla, volver de FT8- el
 * destrozo dura un fotograma.
 *
 * En bucle no: solo se escribe la fila nueva, asi que lo que haya quedado
 * encima se queda ahi hasta que la linea de escritura pase por debajo, y
 * eso son hasta 72 lineas. El banco de captura lo canto a la primera, con
 * 53.676 pixeles distintos - que son, casi exactos, las 72 filas -.
 *
 * Asi que hay una bandera de "esta vez, entero". La ponen el arranque, el
 * cambio de modo y devolver la RAM prestada de FT8, que son los tres sitios
 * donde se sabe que lo de debajo ya no vale.
 *
 * LO QUE ESTO NO CUBRE, y va dicho: un cartel que se pinte encima sin pasar
 * por ninguno de esos tres. Ahi el destrozo sigue hasta que la linea pase.
 * La cascada desplazada no tiene ese agujero. Es parte del precio del modo
 * y por eso el interruptor sigue estando.
 */
void waterfall_entero(void) { s_todo = 1U; }

uint8_t waterfall_bucle(void)          { return s_bucle; }
void    waterfall_bucle_pon(uint8_t on){ s_bucle = (uint8_t)(on ? 1U : 0U); s_todo = 1U; }

void waterfall_blit(uint16_t x, uint16_t y, const uint16_t *lut)
{
    /* Prestado a FT8: lo que hay ahi no son indices de paleta. */
    if (s_prestada) { return; }

    /*
     * En bucle, UNA fila: la que acaba de escribir waterfall_push_line(),
     * que es la fisica s_head, y va en la fila s_head de la pantalla
     * -porque aqui la correspondencia es directa y no se mueve-.
     */
    if (s_bucle && !s_todo) {
        /* TODAS las que se hayan empujado desde el volcado anterior, no
         * solo la ultima. Ver s_pend. La mas reciente es la fisica s_head
         * y las anteriores van subiendo de indice. */
        uint8_t k;
        for (k = 0U; k < s_pend; k++) {
            uint16_t f = (uint16_t)((s_head + k) % WATERFALL_ROWS);
            gfx2_wf_fila((int16_t)x, (int16_t)(y + f), WATERFALL_WIDTH,
                         &s_buf[f][0], lut);
        }
        s_pend = 0U;
        return;
    }
    s_todo = 0U;
    s_pend = 0U;

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
