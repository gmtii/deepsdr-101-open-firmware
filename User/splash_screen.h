#ifndef SPLASH_SCREEN_H
#define SPLASH_SCREEN_H

/*
 * Pantalla de arranque: lluvia de codigo verde sobre negro y el titulo
 * "DeepSDR reload". Ver splash.c para como esta hecha y cuanto dura.
 *
 * Llamar UNA VEZ en main(), con la pantalla ya inicializada y ANTES de
 * radio_screen_draw(). BLOQUEA mientras dura la animacion (~3,6 s): el
 * arranque no tiene nada mas que hacer en ese momento.
 */
void splash_screen_draw(void);

#endif /* SPLASH_SCREEN_H */
