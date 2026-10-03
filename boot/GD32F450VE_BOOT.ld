/*
 * EL CARGADOR DE ARRANQUE NUESTRO - 01/10/2026.
 *
 * RAMA "reload", 02/10/2026: 0x08000000, 48 kB (sectores 0, 1 y 2).
 *
 * Antes se reservaban 128 kB porque era lo que ocupaba el gestor de
 * fabrica y la aplicacion empezaba en 0x08020000 pasara lo que pasara. Al
 * dejar de depender de aquel gestor, lo que se reserva es lo que hace
 * falta: el cargador ocupa 33.768 bytes, los sectores 0 a 2 son 49.152, y
 * los 79 kB que sobraban se le dan a la aplicacion.
 *
 * NO SE PUEDE SUBIR DE 48 kB SIN MOVER LA APLICACION. Los sectores 0 a 3
 * miden 16 kB cada uno, asi que el siguiente escalon es 64 kB y se come el
 * sector 3, que es donde empieza ahora la aplicacion (CARGA_APP_BASE). Si
 * algun dia el cargador no cabe, el enlazador lo dice aqui, y lo que hay
 * que mover son LOS DOS a la vez: este LENGTH y CARGA_APP_BASE, y volver a
 * grabar la pareja entera.
 *
 * La pila del cargador va en la SRAM, no en el TCM. El TCM se deja
 * intacto a proposito: ahi es donde la aplicacion pone la suya (su
 * _estack es 0x10010000) y no hay razon para ensuciarlo.
 */
ENTRY(Reset_Handler)

/*
 * LA PILA DE ESTE CARGADOR, Y POR QUE NO ESTA EN EL TOPE DE LA SRAM.
 *
 * *** Hallazgo del 01/10/2026, repasando. *** La aplicacion guarda en
 * 0x2002F108 el testigo del modo DFU, en una seccion .noinit que NO se borra
 * al reiniciar: es lo que le dice a su SystemInit() que tiene que saltar al
 * cargador de fabrica.
 *
 * Si la pila de ESTE cargador empezara en 0x20030000 y creciera hacia abajo,
 * tendria 3.832 bytes hasta pisarlo. Hoy no llega -usa menos de 1 kB- pero
 * el dia que alguien meta un buffer en la pila del modo actualizacion, el
 * boton de DFU dejaria de funcionar y el motivo seria invisible: el testigo
 * se escribe, se reinicia, y aparece borrado sin que nadie lo borre.
 *
 * Con la pila por debajo del testigo eso no puede pasar nunca.
 */
_estack = 0x2002F000;
_Min_Heap_Size  = 0;
_Min_Stack_Size = 0x2000;

MEMORY
{
  RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 192K
  FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 48K
  TCMRAM (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

SECTIONS
{
  .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } >FLASH
  .text : {
    . = ALIGN(4);
    *(.text) *(.text*) *(.glue_7) *(.glue_7t) *(.eh_frame)
    KEEP (*(.init)) KEEP (*(.fini))
    . = ALIGN(4); _etext = .;
  } >FLASH
  .rodata : { . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); } >FLASH
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >FLASH
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } >FLASH
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } >FLASH
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } >FLASH

  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data) *(.data*) . = ALIGN(4); _edata = .; } >RAM AT> FLASH

  /* El cargador no usa TCM, pero el startup copia esa seccion igual: se
   * declara vacia para que _stcmram y _etcmram existan y la copia no
   * mueva ni un byte. */
  _sitcmram = LOADADDR(.tcmram);
  .tcmram : { . = ALIGN(4); _stcmram = .; *(.tcmram) *(.tcmram*) . = ALIGN(4); _etcmram = .; } >TCMRAM AT> FLASH

  . = ALIGN(4);
  .bss : { _sbss = .; __bss_start__ = _sbss; *(.bss) *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = _ebss; } >RAM

  ._user_heap_stack : {
    . = ALIGN(8); PROVIDE ( end = . ); PROVIDE ( _end = . );
    . = . + _Min_Heap_Size; . = . + _Min_Stack_Size; . = ALIGN(8);
  } >RAM

  /DISCARD/ : { libc.a ( * ) libm.a ( * ) libgcc.a ( * ) }
  .ARM.attributes 0 : { *(.ARM.attributes) }
}
