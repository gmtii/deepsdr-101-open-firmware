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

#endif

/* El camino recomendado: dejar un testigo y reiniciar. Ver dfu_salto.c. */
void dfu_pide_reinicio(void);

/* Lo PRIMERO que tiene que llamar main(), antes de tocar periferico alguno. */
void dfu_mira_al_arrancar(void);
