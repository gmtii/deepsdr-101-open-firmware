#include <string.h>
#include "waterfall.h"
#include "gfx2.h"

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
 */
static uint8_t s_buf[WATERFALL_ROWS][WATERFALL_WIDTH];
static uint16_t s_head = 0; /* indice fisico de la fila logica 0 */

void waterfall_init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_head = 0;
}

void waterfall_push_line(const uint8_t *line)
{
    /* Retrocede el head (la fila que era la mas antigua pasa a ser la
     * nueva fila 0) y escribe encima. Solo 1 fila copiada. */
    s_head = (uint16_t)((s_head + WATERFALL_ROWS - 1U) % WATERFALL_ROWS);
    memcpy(&s_buf[s_head][0], line, WATERFALL_WIDTH);
}

void waterfall_blit(uint16_t x, uint16_t y, const uint16_t *lut)
{
    /* gfx2_wf_blit() recorre el historial circular desde s_head, convierte
     * cada indice con la LUT dentro de la banda compositora y saca cada
     * trozo con UNA sola ventana del panel. Sustituye a los dos gfx_blit()
     * que hacian falta antes para dar la vuelta al buffer circular. */
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
