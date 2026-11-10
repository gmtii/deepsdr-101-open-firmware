MEMORY {
  CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}
ENTRY(_reset)
SECTIONS {
  .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > CODE
  .data : { _sdata = .; *(.data*) _edata = .; } > RAM AT > CODE
  _sidata = LOADADDR(.data);
  /*
   * (NOLOAD) EN .bss, Y HACE FALTA - 08/11/2026.
   *
   * Sin eso, cada compilacion de este banco sacaba:
   *
   *     ld: warning: section `.bss' type changed to PROGBITS
   *
   * El motivo: .tcmram la generan los ficheros del firmware como seccion
   * CON contenido (PROGBITS), aunque ese contenido sean 9.216 bytes de
   * ceros -se comprobo volcandola-. Al meterla aqui dentro de .bss, que
   * es NOBITS, el enlazador cambiaba el tipo de .bss entera y avisaba.
   *
   * En la radio no pasa porque GD32F450VE_FLASH.ld declara .tcmram con
   * (NOLOAD) justamente por esto (ver su comentario: eran 49 kB de flash
   * llenos de ceros que no volcaba nadie). Aqui se hace lo mismo con
   * .bss, que es donde cae.
   *
   * No cambia lo que corre: el contenido son ceros y start.c pone a cero
   * todo _sbss.._ebss al arrancar, igual que el arranque de la radio. Lo
   * que se va es el aviso -que salia en cada tanda y acaba siendo
   * paisaje- y 9 kB de ceros dentro del .elf.
   */
  .bss (NOLOAD) : { _sbss = .; *(.bss*) *(.tcmram*) *(COMMON) _ebss = .; } > RAM
  . = ALIGN(8);
  _estack = ORIGIN(RAM) + LENGTH(RAM) - 16;
}
