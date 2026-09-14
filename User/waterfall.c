#include <string.h>
#include "waterfall.h"
#include "gfx.h"
#include "ft8_shared_ram.h"

/* .bss, RAM principal (0x20000000). WATERFALL_WIDTH*WATERFALL_ROWS*2 bytes,
 * ver presupuesto documentado en waterfall.h antes de subir WATERFALL_ROWS.
 *
 * MODELO DE ANILLO (reescrito 30/07/2026): la version anterior hacia un
 * memmove de (ROWS-1)*WIDTH*2 = ~94KB en CADA push_line para desplazar
 * fisicamente las filas. Ahora el buffer es un anillo: s_head apunta a
 * la fila LOGICA 0 (la mas reciente) y push_line solo retrocede el
 * indice y copia la fila nueva (1.6KB) - el "scroll" es contabilidad,
 * no movimiento de memoria. El coste se paga (barato) en el blit, que
 * vuelca el anillo en dos tramos contiguos.
 *
 * UNION REAL CON FT8 (09/2026) - este buffer ya NO es una declaracion
 * privada: es el miembro que le da tamano a g_ft8_shared_ram (ver
 * ft8_shared_ram.h para el porque completo). La definicion (el
 * almacenamiento real, no un extern) vive aqui porque este es
 * historicamente el buffer mas grande y el motivo original de todo
 * esto - ningun otro cambio en este fichero fue necesario, s_buf sigue
 * siendo el mismo nombre en el mismo sitio para el resto del codigo de
 * abajo.
 */
ft8_shared_ram_t g_ft8_shared_ram;
#define s_buf (g_ft8_shared_ram.waterfall_buf)
static uint16_t s_head = 0; /* indice fisico de la fila logica 0 */

void waterfall_init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_head = 0;
}

void waterfall_push_line(const uint16_t *line)
{
    /* Retrocede el head (la fila que era la mas antigua pasa a ser la
     * nueva fila 0) y escribe encima. Solo 1 fila copiada. */
    s_head = (uint16_t)((s_head + WATERFALL_ROWS - 1U) % WATERFALL_ROWS);
    memcpy(&s_buf[s_head][0], line, WATERFALL_WIDTH * sizeof(uint16_t));
}

void waterfall_blit(uint16_t x, uint16_t y)
{
    /* Fila logica 0 (mas reciente) arriba: fisicamente es
     * s_buf[s_head..ROWS-1] seguido de s_buf[0..s_head-1]. Dos blits
     * contiguos (o uno si el anillo esta alineado). */
    uint16_t first_rows = (uint16_t)(WATERFALL_ROWS - s_head);

    gfx_blit(x, y, WATERFALL_WIDTH, first_rows, &s_buf[s_head][0]);
    if (s_head != 0U) {
        gfx_blit(x, (uint16_t)(y + first_rows), WATERFALL_WIDTH, s_head,
                 &s_buf[0][0]);
    }
}

uint16_t *waterfall_row(uint16_t row)
{
    if (row >= WATERFALL_ROWS) {
        return NULL;
    }
    return &s_buf[(row + s_head) % WATERFALL_ROWS][0];
}
