#ifndef IRQ_PRIO_H
#define IRQ_PRIO_H

/*
 * PRIORIDADES DE INTERRUPCION. Etapa 29, 24/09/2026.
 *
 * QUE ES ESTO, en una frase: cuando dos interrupciones coinciden, la de
 * numero MAS BAJO gana - y ademas puede cortar a la otra a media ejecucion.
 * Eso segundo es lo importante y se llama expropiacion.
 *
 * ======================================================================
 * LO QUE HABIA ANTES, Y POR QUE ERA UN PROBLEMA
 * ======================================================================
 *
 * Nada de esto estaba puesto, y "nada puesto" no significa "los valores por
 * defecto razonables": significa tres cosas distintas y ninguna buena.
 *
 * 1. EL RELOJ ERA LO MENOS IMPORTANTE DE LA RADIO.
 *
 *    SysTick_Config() -la funcion de CMSIS que arranca el tic de 1 ms- pone
 *    ese tic en la prioridad MAS BAJA que existe. Es su comportamiento
 *    documentado y tiene sentido para un programa cualquiera; aqui no.
 *
 *    El manejador del audio corre en la interrupcion del DMA y hace TODO el
 *    procesado: demodular, filtrar, reducir ruido y, desde las ultimas
 *    etapas, tambien el NAVTEX, el fax, el SSTV y el paquete. Su presupuesto
 *    es de 2,67 ms por bloque. Con el reloj por debajo de el, cada vez que
 *    ese manejador pase de 1 ms, el tic que tenia que haber saltado se queda
 *    esperando; si pasa de 2, el segundo tic se PIERDE, porque el aviso de
 *    "hay un tic pendiente" es un solo bit y no cuenta cuantos van.
 *
 *    Un tic perdido es un milisegundo que el reloj de la radio no cuenta. Y
 *    ese reloj es el que la etapa 24 acaba de anclar al segundo con el RDS:
 *    no tiene ningun sentido medir el borde de minuto al milisegundo y
 *    despues dejar que el contador pierda milisegundos por el camino.
 *
 * 2. EL NUMERO QUE SE PEDIA NO ERA EL QUE SE OBTENIA.
 *
 *    Los cuatro bits de prioridad que tiene este micro se reparten entre
 *    "puede expropiar" y "desempata", y ese reparto se elige con
 *    nvic_priority_group_set(). Si no se elige, el registro se queda en cero
 *    - que NO es ninguno de los cinco repartos que la biblioteca del
 *    fabricante reconoce-. Asi que su nvic_irq_enable() cae en el caso "no se
 *    lo que hay" y, en silencio, establece dos bits para cada cosa.
 *
 *    Con dos bits, el valor maximo que se puede pedir es 3. El audio pedia 6:
 *    se sale, y el desplazamiento lo saca fuera del byte. Lo que quedaba
 *    escrito en el registro era 0x80.
 *
 *    El tactil pedia 2, que no se sale, y acababa TAMBIEN en 0x80.
 *
 *    O sea que el codigo decia "el tactil es mas urgente que el audio" -2
 *    frente a 6- y el aparato hacia "son igual de urgentes". No se notaba
 *    nada, porque con la misma prioridad ninguno corta al otro y el tactil es
 *    corto; pero el codigo y la maquina llevaban meses diciendo cosas
 *    distintas.
 *
 * 3. Y AL NO ESTAR ESCRITO EN NINGUN SITIO, no habia nada que leer para
 *    enterarse de cual era el plan. Este fichero es ese sitio.
 *
 * ======================================================================
 * EL PLAN
 * ======================================================================
 *
 * Reparto: los CUATRO bits para expropiar y ninguno para desempatar. Con
 * tres interrupciones en toda la radio, el desempate sobra; lo que hace falta
 * es poder decir con claridad quien corta a quien.
 *
 * El orden sale de una sola regla: MANDA LO CORTO Y URGENTE, NO LO LARGO.
 * Una interrupcion que dura microsegundos no molesta a nadie aunque corte a
 * todo el mundo; una que dura milisegundos no puede estar por encima de nada
 * que tenga que ser puntual.
 *
 *   0  SysTick        El tic de 1 ms. Suma uno a un contador y mira el mando:
 *                     unos pocos microsegundos. Es la base de tiempo de todo
 *                     el aparato, asi que va la primera. Lo que cuesta ponerla
 *                     arriba es que corta al audio una vez por milisegundo,
 *                     y eso es menos del 1% de su tiempo.
 *
 *   1  DMA0 canal 3   El audio. Es la unica que tiene un plazo de verdad: si
 *                     no termina antes de que llegue el bloque siguiente, se
 *                     pierden muestras y eso se OYE. Por debajo del reloj y
 *                     por encima de todo lo demas.
 *
 *   2  EXTI2          El tactil. Es un dedo: puede esperar un milisegundo sin
 *                     que nadie lo note. Va la ultima a proposito, que es lo
 *                     contrario de lo que decia el codigo de antes.
 *
 * COMO SE COMPRUEBA QUE ESTO SIRVE DE ALGO: main.c lleva un contador de tics
 * perdidos que compara el reloj de milisegundos con el contador de ciclos del
 * nucleo, que no depende de ninguna interrupcion. Sale en Ajustes -> Equipo
 * -> Informacion, en el renglon "Reloj". Tiene que poner 0.
 */

/* El reparto: cuatro bits para expropiar, ninguno para desempatar. */
#define IRQ_GRUPO        NVIC_PRIGROUP_PRE4_SUB0

#define IRQ_PRIO_SYSTICK 0U
#define IRQ_PRIO_AUDIO   1U
#define IRQ_PRIO_TACTIL  2U

/* La subprioridad no existe con este reparto, pero la funcion del fabricante
 * la pide igual. Una constante con nombre para que quede claro que no es un
 * cero elegido al azar. */
#define IRQ_SUB_NINGUNA  0U

#endif /* IRQ_PRIO_H */
