MEMORY {
  CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}
ENTRY(_reset)
SECTIONS {
  .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > CODE
  .data : { _sdata = .; *(.data*) _edata = .; } > RAM AT > CODE
  _sidata = LOADADDR(.data);
  .bss  : { _sbss = .; *(.bss*) *(.tcmram*) *(COMMON) _ebss = .; } > RAM
  . = ALIGN(8);
  _estack = ORIGIN(RAM) + LENGTH(RAM) - 16;
}
