/*
 * EL CARGADOR DE ARRANQUE NUESTRO - 01/10/2026.
 *
 * RAMA "reload", 17/10/2026: 0x08000000, 32 kB (sectores 0 y 1).
 *
 * Antes se reservaban 128 kB porque era lo que ocupaba el gestor de
 * fabrica y la aplicacion empezaba en 0x08020000 pasara lo que pasara. Al
 * dejar de depender de aquel gestor se bajo a 48 kB -sectores 0, 1 y 2-,
 * y el 17/10/2026 a 32, que es donde esta ahora.
 *
 * POR QUE SE BAJO A 32. Porque el sector 2 son 16.384 bytes y a la
 * aplicacion le quedaban 2.720 libres de 475.136. El cargador medía
 * 41.212 y el 27 % de eso era UNA FUENTE: font_ui_18b entera, 117 glifos,
 * de los que pinta 68. Recortada a los que usa y bajada a 11 px cabe en
 * dos sectores con 152 bytes de sobra (ver tools/poda_fuente.py y
 * font_boot.c, que es generado).
 *
 * 152 BYTES DE MARGEN SON POCOS, y hay que saberlo: el dia que el
 * cargador crezca mas que eso, el enlazador para aqui. Entonces hay dos
 * salidas -adelgazarlo otra vez, o volver a 48 kB y devolverle el sector
 * a la aplicacion- y las dos piden mover LOS DOS numeros a la vez: este
 * LENGTH y CARGA_APP_BASE, mas los dos .ld de la aplicacion, el VTOR y la
 * tabla de sectores de fmc_borra(). Los vigila tools/mapa.py, que no deja
 * sacar un binario si no cuadran.
 *
 * Y LA PAREJA SE GRABA JUNTA. Un cargador de 32 kB salta a 0x08008000 y
 * una aplicacion vieja esta en 0x0800C000: no arranca ninguna de las dos
 * combinaciones cruzadas. Al actualizar hay que grabar el cargador Y el
 * update.bin nuevo.
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
  FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 32K
  TCMRAM (rw)  : ORIGIN = 0x10000000, LENGTH = 32K
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

  /*
   * DONDE ACABA LO QUE SE GRABA - 02/10/2026.
   *
   * No es _etext: detras van .rodata, los arrays de constructores y las
   * copias iniciales de .data y .tcmram, y todo eso esta en el .bin. Esto
   * es el final de verdad, y es lo que necesita boot_main.c para sacar el
   * CRC de SI MISMO -ver CARGADOR_VERSION-.
   *
   * Tiene que ir despues de .tcmram, que es la ultima que carga en FLASH.
   */
  _eflash = LOADADDR(.tcmram) + SIZEOF(.tcmram);

  . = ALIGN(4);
  .bss : { _sbss = .; __bss_start__ = _sbss; *(.bss) *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = _ebss; } >RAM

  ._user_heap_stack : {
    . = ALIGN(8); PROVIDE ( end = . ); PROVIDE ( _end = . );
    . = . + _Min_Heap_Size; . = . + _Min_Stack_Size; . = ALIGN(8);
  } >RAM

  /DISCARD/ : { libc.a ( * ) libm.a ( * ) libgcc.a ( * ) }
  .ARM.attributes 0 : { *(.ARM.attributes) }
}
