#include "ui.h"

/*
 * Lo que queda de ui.c - 22/09/2026.
 *
 * Este fichero era el sistema de interfaz del firmware de antes del
 * rediseno: paneles, botones, etiquetas, una "pantalla" donde se
 * registraban y un reparto de toques que los recorria. De todo eso solo
 * sobrevive el panel, que es un rectangulo con marco, y lo usan los cuatro
 * marcos de la pantalla principal.
 *
 * El resto se fue porque ya no lo llamaba nadie: los botones los dibujan y
 * los resuelven ui_act.c, ui_cfg.c, ui_det.c, ui_grid.c, ui_kbd.c, ui_digi.c
 * y ui_top.c, cada uno con su propia funcion de acierto y su propio estado,
 * y ninguno necesita registrarse en ninguna lista. Se comprobo antes de
 * borrarlo: el enlazador ya estaba descartando ui_button_draw(),
 * ui_button_hit(), ui_label_draw(), ui_screen_add_*() y ui_screen_touch()
 * por inalcanzables.
 */
void ui_panel_draw(const ui_panel_t *panel)
{
    if (panel->hidden) {
        return; /* ver ui_panel_t::hidden en ui.h */
    }
    gfx_fill_rect(panel->x, panel->y, panel->w, panel->h, panel->bg);
    if (panel->border != panel->bg) {
        gfx_rect(panel->x, panel->y, panel->w, panel->h, panel->border);
    }
}
