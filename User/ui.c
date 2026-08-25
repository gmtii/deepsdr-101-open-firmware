#include <string.h>
#include "ui.h"

void ui_panel_draw(const ui_panel_t *panel)
{
    gfx_fill_rect(panel->x, panel->y, panel->w, panel->h, panel->bg);
    if (panel->border != panel->bg) {
        gfx_rect(panel->x, panel->y, panel->w, panel->h, panel->border);
    }
}

void ui_button_draw(const ui_button_t *btn)
{
    uint16_t text_w, text_h, text_x, text_y;
    uint16_t border_color = btn->pressed ? btn->fg : btn->border;
    uint16_t bg = btn->bg;
    uint16_t fg = btn->fg;

    if (!btn->enabled) {
        /* deshabilitado: mismo layout, sin distincion de color especial
         * todavia (no hay paleta de "disabled" definida) - dejamos el
         * borde en gris oscuro para que al menos se note al tacto visual
         * que no responde. Ampliar si hace falta mas adelante. */
        border_color = GFX_COLOR_DARKGRAY;
    }

    gfx_fill_rect(btn->x, btn->y, btn->w, btn->h, bg);
    gfx_rect(btn->x, btn->y, btn->w, btn->h, border_color);
    if (btn->pressed && btn->w > 2 && btn->h > 2) {
        /* segundo borde interior para dar sensacion de "hundido" */
        gfx_rect((uint16_t)(btn->x + 1), (uint16_t)(btn->y + 1),
                 (uint16_t)(btn->w - 2), (uint16_t)(btn->h - 2), border_color);
    }

    if (btn->label != NULL) {
        text_w = gfx_text_width(btn->label, btn->text_scale);
        text_h = gfx_text_height(btn->text_scale);
        text_x = (uint16_t)(btn->x + (btn->w > text_w ? (btn->w - text_w) / 2 : 0));
        text_y = (uint16_t)(btn->y + (btn->h > text_h ? (btn->h - text_h) / 2 : 0));
        gfx_text(text_x, text_y, btn->label, fg, bg, btn->text_scale);
    }
}

uint8_t ui_button_hit(const ui_button_t *btn, uint16_t px, uint16_t py)
{
    return (px >= btn->x) && (px < (uint16_t)(btn->x + btn->w)) &&
           (py >= btn->y) && (py < (uint16_t)(btn->y + btn->h));
}

void ui_label_draw(const ui_label_t *label)
{
    if (label->text == NULL) {
        return;
    }
    gfx_text(label->x, label->y, label->text, label->fg, label->bg, label->text_scale);
}

/* --- ui_screen_t: registro de widgets + despacho de toques --- */

void ui_screen_init(ui_screen_t *screen)
{
    memset(screen, 0, sizeof(*screen));
    screen->active_index = -1;
}

static uint8_t ui_screen_add(ui_screen_t *screen, ui_widget_type_t type, void *widget)
{
    if (screen->count >= UI_SCREEN_MAX_WIDGETS) {
        return 0;
    }
    screen->widgets[screen->count].type = type;
    screen->widgets[screen->count].widget = widget;
    screen->count++;
    return 1;
}

uint8_t ui_screen_add_panel(ui_screen_t *screen, ui_panel_t *panel)
{
    return ui_screen_add(screen, UI_WIDGET_PANEL, panel);
}

uint8_t ui_screen_add_label(ui_screen_t *screen, ui_label_t *label)
{
    return ui_screen_add(screen, UI_WIDGET_LABEL, label);
}

uint8_t ui_screen_add_button(ui_screen_t *screen, ui_button_t *button)
{
    /* OJO: enabled NO se fuerza a 1 aqui - si se te olvida ponerlo en el
     * literal de inicializacion del ui_button_t, se queda a 0 (C
     * inicializa a cero) y el boton se pintara pero ui_screen_touch()
     * lo ignorara siempre. Ver ejemplo de literal en main.c. */
    return ui_screen_add(screen, UI_WIDGET_BUTTON, button);
}

void ui_screen_draw(ui_screen_t *screen)
{
    uint8_t i;

    for (i = 0; i < screen->count; i++) {
        switch (screen->widgets[i].type) {
        case UI_WIDGET_PANEL:
            ui_panel_draw((ui_panel_t *)screen->widgets[i].widget);
            break;
        case UI_WIDGET_LABEL:
            ui_label_draw((ui_label_t *)screen->widgets[i].widget);
            break;
        case UI_WIDGET_BUTTON:
            ui_button_draw((ui_button_t *)screen->widgets[i].widget);
            break;
        }
    }
}

static void ui_button_set_pressed(ui_button_t *btn, uint8_t pressed, ui_event_t event, void *user_data_for_cb)
{
    if (btn->pressed == pressed) {
        return; /* sin cambio visual, no redibujar */
    }
    btn->pressed = pressed;
    ui_button_draw(btn);
    if (btn->on_event != NULL) {
        btn->on_event(btn, event, user_data_for_cb);
    }
}

void ui_screen_touch(ui_screen_t *screen, uint16_t x, uint16_t y, uint8_t pressed)
{
    if (pressed) {
        if (screen->active_index < 0) {
            /* Toque nuevo: busca el boton bajo (x,y). Recorre en orden
             * inverso de insercion para que, si hay solape, "gane" el
             * ultimo añadido (el que quedaria visualmente encima). */
            int8_t i;
            for (i = (int8_t)screen->count - 1; i >= 0; i--) {
                if (screen->widgets[i].type != UI_WIDGET_BUTTON) {
                    continue;
                }
                {
                    ui_button_t *btn = (ui_button_t *)screen->widgets[i].widget;
                    if (btn->enabled && ui_button_hit(btn, x, y)) {
                        screen->active_index = i;
                        ui_button_set_pressed(btn, 1, UI_EVENT_PRESS, btn->user_data);
                        break;
                    }
                }
            }
        } else {
            /* Ya habia un boton "activo" desde el press anterior: solo
             * nos importa si el dedo sigue dentro o se ha salido, para
             * dar feedback visual de arrastre sin volver a hacer hit-test
             * contra el resto de widgets (el boton activo no cambia
             * hasta soltar). */
            ui_button_t *btn = (ui_button_t *)screen->widgets[screen->active_index].widget;
            uint8_t inside = ui_button_hit(btn, x, y);
            ui_button_set_pressed(btn, inside, inside ? UI_EVENT_PRESS : UI_EVENT_CANCEL, btn->user_data);
        }
    } else {
        /* Dedo levantado: si habia un boton activo, decide RELEASE
         * (si el dedo seguia dentro) o CANCEL (si se habia salido) segun
         * el ultimo estado visual conocido (btn->pressed). */
        if (screen->active_index >= 0) {
            ui_button_t *btn = (ui_button_t *)screen->widgets[screen->active_index].widget;
            ui_event_t final_event = btn->pressed ? UI_EVENT_RELEASE : UI_EVENT_CANCEL;

            /* Redibuja a "sin pulsar" siempre, pero el callback debe
             * reflejar RELEASE solo si el dedo seguia dentro. Como
             * ui_button_set_pressed() solo dispara callback si hay
             * cambio de estado visual, y aqui SIEMPRE toca soltar,
             * lo forzamos directamente en vez de reusarla. */
            if (btn->pressed != 0) {
                btn->pressed = 0;
                ui_button_draw(btn);
            }
            if (btn->on_event != NULL) {
                btn->on_event(btn, final_event, btn->user_data);
            }
            screen->active_index = -1;
        }
    }
}
/* =========================================================================
 * MODIFICA PER PANADAPTER KENWOOD TS-50 (Modalità IF / Diretta)
 * ========================================================================= */

// 0 = Ricezione Diretta SDR, 1 = Modalità IF TS-50 (73.045 MHz - Freq)
uint8_t panadapter_mode = 0; 

// Funzione Callback: viene chiamata ogni volta che tocchi il pulsante sullo schermo
void ui_if_mode_button_callback(struct ui_button_t *btn, ui_event_t event, void *user_data)
{
    // Reagiamo solo quando l'utente solleva il dito dal pulsante (Click completato)
    if (event == UI_EVENT_RELEASE) {
        if (panadapter_mode == 0) {
            panadapter_mode = 1;         // Attiva modalità Kenwood TS-50
            btn->label = "TS-50 IF";     // Cambia la scritta sul display
        } else {
            panadapter_mode = 0;         // Torna in modalità SDR classica
            btn->label = "DIRECT";       // Ripristina la scritta originale
        }
        
        // Forza il ridisegno immediato del pulsante per mostrare la nuova scritta
        ui_button_draw(btn);
    }
}
