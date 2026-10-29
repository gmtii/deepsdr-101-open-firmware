#ifndef PUMP_H
#define PUMP_H

/*
 * EL LATIDO QUE SE CUELA DENTRO DE LO QUE TARDA MUCHO.
 *
 * *** 29/10/2026. El usuario: que en STANAG y en IDENT el tactil responde
 * LENTAMENTE y que insistiendo acaba entrando. Y la propia radio lo midio:
 * el primer renglon del panel de STANAG puso "BUCLE 999 ms la vuelta,
 * 181 ms poll". ***
 *
 * POR QUE HACE FALTA
 * ------------------
 * Un toque necesita DOS lecturas del tactil con el dedo puesto, y la
 * segunda solo cuenta si han pasado TOUCH_CONF_MS desde que se poso. Con
 * un toque de 150 ms eso da un techo duro: si entre dos miradas pasan mas
 * de 75 ms, el dedo se levanta antes de la segunda y el toque NO EXISTE.
 * Medido en el banco contra el touch.c de verdad: a 75 ms entran los 20 de
 * 20; a 100 ms, 10 de 20.
 *
 * El bucle principal mira el tactil tres veces por vuelta, pero las tres
 * estan FUERA de los decodificadores. Un poll de 181 ms es una ventana de
 * 181 ms sin mirar, o sea dos toques de cada tres perdidos.
 *
 * Y no se arregla llamando mas veces desde fuera: lo que hay que hacer es
 * mirar DESDE DENTRO del trabajo largo.
 *
 * ESTO NO ES NUEVO EN ESTE PROYECTO
 * ---------------------------------
 * Es exactamente el mismo patron que gfx2_set_pump(), que existe desde
 * hace meses por el mismo motivo -un repintado largo dejaba de mirar el
 * tactil- y con el mismo razonamiento escrito: el compositor no tiene por
 * que saber del tactil, asi que recibe un puntero a funcion y lo llama de
 * vez en cuando.
 *
 * Aqui igual: s4415.c, stanag_rx.c y stanag_det.c no saben que existe un
 * tactil. Llaman a pump() en los sitios donde su trabajo se puede cortar
 * sin consecuencias, y main.c decide que es pump().
 *
 * LO QUE NO ES
 * ------------
 * No es un planificador ni una espera. pump() se llama con el trabajo a
 * medias, asi que lo que se le cuelgue NO puede tocar el estado del modulo
 * que lo llama ni repintar nada. Hoy se le cuelga touch_latido(), que solo
 * lee el bus del tactil y apunta un contacto para que el bucle lo recoja
 * -y que ademas se sale sola si no han pasado sus 8 ms-.
 *
 * Y POR QUE VIVE ENTERO EN LA CABECERA
 * -----------------------------------
 * El gancho se define DEBIL aqui dentro, asi que cada fichero que incluya
 * esta cabecera trae su propia definicion y el enlazador las funde en una
 * sola. No es por listeza: es que los bancos del simulador compilan
 * stanag_det.c, s4415.c, stanag_rx.c e ident.c en veinte objetivos
 * distintos, cada uno con su lista de fuentes escrita a mano. Con un .c
 * aparte habria que acordarse de anadirlo a los veinte, y el que se
 * olvidara no fallaria al compilar sino AL ENLAZAR, en un banco que a lo
 * mejor nadie toca en un mes.
 *
 * Asi, un banco que no sabe del tactil enlaza sin tocar nada y pump() no
 * hace nada, que es justo lo que un banco quiere.
 */

/* El gancho. Volatil porque main() lo escribe y los decodificadores lo
 * leen; debil por lo que dice la cabecera de arriba. */
__attribute__((weak)) void (*volatile g_pump)(void) = 0;

/* Cuelga la funcion que se llamara desde dentro del trabajo largo. Un
 * puntero nulo la desengancha. La llama main() al arrancar. */
static inline void pump_pon(void (*fn)(void))
{
    g_pump = fn;
}

/*
 * La llaman los que tardan. Sin nada colgado no hace nada.
 *
 * El puntero se copia a una local antes de comprobarlo y llamarlo, igual
 * que sdr_rx.c con su gancho de bloque: sin esa copia hay una ventana
 * entre el "if" y la llamada en la que otro puede dejarlo a cero, y lo
 * que se ejecuta es un salto a la direccion cero.
 */
static inline void pump(void)
{
    void (*fn)(void) = g_pump;

    if (fn != 0) { fn(); }
}

#endif /* PUMP_H */
