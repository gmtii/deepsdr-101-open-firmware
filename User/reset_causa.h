#ifndef RESET_CAUSA_H
#define RESET_CAUSA_H

#include <stdint.h>

/*
 * POR QUE SE REINICIO LA RADIO - 08/10/2026.
 *
 * *** Un usuario, en la V4.01: "de forma random se le reinicia y no vuelve
 * al mismo modo en que estaba ni guarda la config de la pantalla". Y otra
 * vez: "estaba recibiendo FT8 y se le ha reiniciado". ***
 *
 * LO QUE YA SE SABIA ANTES DE ESCRIBIR UNA LINEA, Y QUE ESTRECHA MUCHO LA
 * BUSQUEDA:
 *
 *   - NO HAY PERRO GUARDIAN. Ni fwdgt ni wwdgt, en ningun sitio del
 *     firmware ni del cargador.
 *   - El HardFault_Handler (main.c) es un BUCLE INFINITO.
 *   - El unico reinicio por software de todo el arbol es
 *     dfu_pide_reinicio() (dfu_salto.c), y lo pide el dueño a mano desde
 *     la fila de DFU.
 *   - No hay WFI, ni WFE, ni standby, ni escritura de option bytes.
 *
 * Y de ahi sale la consecuencia que manda: en esta radio **un fallo de CPU
 * CUELGA, NO REINICIA**. Un puntero suelto, un indice fuera de sitio, una
 * pila desbordada: todo eso acaba en el bucle infinito del HardFault, con
 * la pantalla congelada y el audio todavia sonando -va por DMA-. No se ve
 * un arranque.
 *
 * Asi que un REINICIO de verdad solo puede ser una de estas cuatro, y no
 * hay mas:
 *
 *   1. La TENSION cayo por debajo del umbral del chip (bateria floja, o un
 *      pico de consumo). Reinicio por encendido/apagado interno.
 *   2. La PATILLA de reset se movio (ruido, un contacto).
 *   3. SOFTWARE: alguien llamo a NVIC_SystemReset().
 *   4. El PERRO GUARDIAN, que aqui no existe... salvo que el cargador o la
 *      ROM lo hayan dejado encendido, que es justo lo que esto comprueba
 *      sin suponerlo.
 *
 * Y EL CHIP LO SABE. RCU_RSTSCK guarda la causa del ultimo reinicio y la
 * conserva hasta que alguien la borra. Este firmware no la miraba nunca.
 *
 * O sea que llevabamos razonando sobre algo que la radio puede CONTAR. Es
 * la misma leccion de la fila del BUCLE y la del chip de la relacion del
 * CW: cuando no puedo ver lo que ve la radio, el numero tiene que salir en
 * la pantalla. Un reinicio por tension y uno por patilla se arreglan en
 * sitios distintos -uno es la bateria o el consumo, el otro es el cableado-
 * y sin este dato la unica forma de elegir es adivinar.
 *
 * El historial vive en .noinit y sobrevive al reinicio, asi que la radio
 * enseña las ultimas veces y no solo la ultima: "T T T" es una bateria que
 * se cae, "T P T" es otra cosa.
 *
 * LO QUE ESTO NO ARREGLA: nada. No evita ni un reinicio. Lo que hace es
 * convertir "se reinicia random" en una letra, que es lo que hacia falta
 * para no seguir arreglando a ciegas.
 */

#define RC_NADA         0U   /* ningun bit puesto: no deberia pasar */
#define RC_TENSION      1U   /* POR/BOR: la alimentacion se cayo */
#define RC_PATILLA      2U   /* la patilla NRST */
#define RC_SOFTWARE     3U   /* NVIC_SystemReset() */
#define RC_PERRO        4U   /* perro guardian (que aqui nadie enciende) */
#define RC_BAJO_CONSUMO 5U   /* salida de un modo de bajo consumo */

#define RC_HIST_N       8U   /* cuantos reinicios se recuerdan */

/*
 * Se llama UNA vez, lo antes posible en main(), y antes que nada que pueda
 * tocar el RCU. Lee la causa, la apunta en el historial y BORRA los bits
 * del chip: si no se borran, el segundo arranque sigue leyendo el primero.
 * Devuelve la causa de ESTE arranque.
 */
uint8_t reset_causa_mira(void);

uint8_t  reset_causa(void);         /* la de este arranque, ya leida */
uint16_t reset_causa_cuenta(void);  /* arranques desde el ultimo en frio */

/* Copia el historial, el mas nuevo primero. Devuelve cuantos ha copiado. */
uint8_t reset_causa_historial(uint8_t *fuera, uint8_t max);

/* Una letra por causa, para la fila de Informacion: T P S G B ? */
char reset_causa_letra(uint8_t c);

#endif /* RESET_CAUSA_H */
