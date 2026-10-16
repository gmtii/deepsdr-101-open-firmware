/*
 * SALTAR AL CARGADOR DFU QUE EL CHIP TRAE DE FABRICA.
 *
 * Esta en un fichero propio porque lo usan DOS programas: el cargador de
 * arranque (boot/boot_main.c) y la aplicacion (la fila DFU de Informacion).
 * Es el mismo codigo compilado en los dos sitios, no dos copias.
 */
#ifndef DFU_SALTO_H
#define DFU_SALTO_H

/* No vuelve. Deja el reloj como un reset, mapea la memoria de sistema en
 * 0x00000000 y salta. Para volver, apagar y encender. */
void dfu_salta(void);

/* El camino recomendado: dejar un testigo y reiniciar. Ver dfu_salto.c. */
void dfu_pide_reinicio(void);

/*
 * *** OJO: ESTA NO LA LLAMA NADIE, Y NO HAY QUE LLAMARLA. 10/10/2026. ***
 *
 * Aqui ponia "lo PRIMERO que tiene que llamar main(), antes de tocar
 * periferico alguno", y main() NO la llama. No es un olvido: el trabajo lo
 * hace SystemInit() en CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c, que mira
 * el mismo testigo 0xDF0DF0DF y salta antes incluso de que main() exista.
 * Tiene que ser asi: para cuando main() corre, ya se han tocado perifericos.
 *
 * O sea que el comentario mandaba a main() a llamar algo que ya no le toca,
 * y hacerle caso saltaria al cargador DFU dos veces. La funcion se queda de
 * momento -la usa el cargador de arranque como camino alternativo- pero
 * desde la aplicacion NO se llama.
 */
void dfu_mira_al_arrancar(void);

/*
 * EL #endif ESTABA EN MEDIO. 10/10/2026.
 *
 * Cerraba despues de dfu_salta(), dejando las otras dos declaraciones FUERA
 * de la guarda. Con un fichero que se incluye en DOS programas distintos
 * -el cargador y la aplicacion- eso es una redeclaracion esperando a que
 * alguien lo incluya dos veces por caminos distintos. Hoy no daba sintoma
 * porque son declaraciones, no definiciones.
 */
#endif
