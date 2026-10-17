#ifndef MANDO_MAPA_H
#define MANDO_MAPA_H

#include <stdint.h>

/*
 * ======================================================================
 * DE QUIEN ES EL MANDO CON EL MAPA PUESTO - 15/10/2026
 * ======================================================================
 *
 * *** De un informe: con el mapa en pantalla, los controles dejan de
 * responder. ***
 *
 * Y era verdad. El despacho del mando en main.c decia:
 *
 *     if (s_ft8_mapa && modo_tiene_mapa() && !long_press && detents != 0
 *         && s_encoder_target != ENCODER_TARGET_VOLUME) {
 *         mapa_zoom_pasos(detents);
 *         return;
 *     }
 *
 * O sea que con el mapa delante el zoom se quedaba TODOS los giros menos
 * los del volumen. Si el usuario habia enganchado el mando al ancho de
 * banda con el boton Func -o al squelch, o al NR, o a la ganancia, o a
 * cualquier ajuste de la lista-, giraba y lo que pasaba era que el mapa
 * hacia zoom. Desde fuera eso es exactamente "los controles no funcionan":
 * la botonera sigue en pantalla, el Func sigue diciendo a que esta
 * enganchado, y el mando no hace lo que dice.
 *
 * LA EXCEPCION DEL VOLUMEN YA ESTABA, Y SU PROPIO COMENTARIO EXPLICA POR
 * QUE ESTO ES UN FALLO:
 *
 *     "Mirando el mapa se puede querer bajar el volumen igual que en
 *      cualquier otra pantalla, y el mapa no pierde nada: tiene sus
 *      botones de zoom en la esquina."
 *
 * Ese argumento no tiene nada de particular con el volumen. Vale IGUAL
 * para el ancho de banda, para el squelch y para los demas. Lo que se
 * escribio fue el caso que alguien reclamo, no la regla.
 *
 * LA REGLA, Y ES LA TERCERA VEZ QUE SE PAGA ESTO. Los comentarios de
 * main.c cuentan que el mismo fallo se pago el 05/10 con la rejilla y otra
 * vez con el teclado numerico, las dos veces parcheando el volumen. Esta
 * es la tercera. Asi que aqui no se parchea un destino mas: se escribe la
 * regla entera, y se escribe en un fichero propio para que tenga banco.
 *
 *     Si el usuario ha enganchado el mando a algo, el mando es SUYO.
 *     El mapa solo se queda los giros cuando el mando esta en su destino
 *     por omision -la sintonia-, que es donde nadie ha pedido nada.
 *
 * Y el mapa no pierde la funcion que pidio el usuario -zoom con el mando
 * en las ventanas de mapa-: en el estado normal, que es como se mira un
 * mapa, el giro sigue haciendo zoom.
 * Ademas de los dos botones de la esquina, que no se tocan.
 *
 * POR QUE NO SE TOCAN LA REJILLA NI EL TECLADO. Son vistas MODALES que
 * usan el mando para lo suyo -mover el cursor-, y ahi el mando no tiene
 * donde ir: la botonera no esta delante. El mapa no es eso: es un panel
 * dentro de la pantalla normal, con la botonera entera debajo y a la
 * vista. Por eso en el mapa duele y en las otras dos no. Queda escrito
 * para que quien lea esto no lo "arregle" tambien alli sin medirlo.
 */

/*
 * 1 si el giro del mando se lo queda el zoom del mapa.
 *
 * `destino` es el encoder_target_t de main.c. No se incluye su enum a
 * proposito -vive dentro de main.c, que no se puede enlazar en un banco-:
 * lo unico que hace falta saber es cual es el valor de la sintonia, y eso
 * se pasa. Asi este fichero se compila solo y el banco puede recorrer los
 * doce destinos sin arrastrar un megabyte detras.
 */
uint8_t mando_mapa_gira(uint8_t mapa_visible, uint8_t modo_con_mapa,
                        uint8_t long_press, int32_t detentes,
                        int destino, int destino_sintonia);

uint8_t mando_mapa_selftest(void);

#endif /* MANDO_MAPA_H */
