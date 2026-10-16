/*
 * MAPA CONTIGUO - rama "reload", 02/10/2026.
 *
 * Linker script para GD32F450VET6 (LQFP100)
 *  - Flash: 512 KB  (variante "VE")
 *  - RAM:   192 KB en 0x20000000 (SRAM0+SRAM1+SRAM2) + 64 KB TCM en 0x10000000
 *    El TCM solo es accesible por el bus de datos del core, no lo uses para
 *    buffers que acceda DMA/EXMC.
 *
 * QUE CAMBIA RESPECTO A LA RAMA "deepsdr", Y POR QUE SE PUEDE.
 *
 * El mapa de siempre lo imponia el gestor de arranque DE FABRICA, que no es
 * nuestro y no se puede cambiar: exigia la aplicacion en 0x08020000, ocho
 * bytes de firma clavados en 0x08060000 y ficheros de como mucho 0x50000.
 * De ahi salian las tres regiones -la baja de 256 kB, la firma, y un
 * "desvan" de 64 kB por encima- y de ahi salia tambien que el desvan NO
 * sirviera para codigo: el enlazador no derrama de una region a otra, asi
 * que lo que subia ahi se elegia A MANO, seccion por seccion, y solo valian
 * tablas inertes.
 *
 * Con cargador propio ese mapa deja de ser una imposicion. Este fichero lo
 * sustituye por UNA SOLA REGION:
 *
 *     0x08000000  +--------------------------+
 *                 |  cargador (S0,S1,S2)     |  48 kB,  33.768 usados
 *     0x0800C000  +--------------------------+  <- la aplicacion empieza aqui
 *                 |                          |
 *                 |  aplicacion, de una      |  475.136 bytes
 *                 |  pieza: codigo, rodata,  |  (S3 16k, S4 64k, S5..S7 128k)
 *                 |  tablas, lo que sea      |
 *                 |                          |
 *     0x08080000  +--------------------------+
 *
 * Se acabaron .arriba, FLASH2, _sarriba/_earriba y la lista a mano de
 * secciones. Lo que crezca, crece, y si no cabe lo dice el ENLAZADOR con los
 * bytes que sobran delante, que es como tiene que ser.
 *
 * LO QUE SE PIERDE A CAMBIO: esta imagen NO arranca con el boot.bin de
 * fabrica ni con nuestro cargador v1.1. Hace falta el cargador del mapa
 * contiguo, y hay que grabarlo ANTES. Si se graba esta imagen con un
 * cargador viejo, la radio se queda en la pantalla de actualizacion -no es
 * un ladrillo: el cargador lee el pendrive antes de validar nada, asi que
 * se vuelve atras copiando el update4.bin de siempre-. Y por debajo sigue
 * estando el gesto del mando para entrar en DFU, que no necesita abrir la
 * radio.
 */

ENTRY(Reset_Handler)

/*
 * LA PILA EN EL TCM, Y POR QUE SIGUE AHI AUNQUE YA NO ESTEMOS OBLIGADOS.
 *
 * El gestor de fabrica EXIGIA que el SP inicial de la aplicacion cayera en
 * 0x10000000-0x1001FFFF: tres instrucciones suyas, "ubfx r0,r0,#17,#12" y
 * comparar con 0x800. Nuestro cargador ya no esta obligado a exigir eso...
 * pero lo exige igual, y a proposito: es la comprobacion mas barata que
 * existe de "aqui dentro hay algo con pinta de aplicacion", una flash recien
 * borrada (0xFFFFFFFF) no la pasa, y cambiarla no ganaria nada. Ademas la
 * pila VA de verdad en el TCM por rendimiento, asi que no es una concesion.
 */
_estack = 0x10010000;

/* MONTON A CERO, 24/09/2026. Este firmware no llama a malloc ni a nada que
 * lo arrastre. Lo vigila tools/pila.py, que lee _Min_Heap_Size de aqui
 * mismo y descuenta lo que ponga.
 */
_Min_Heap_Size  = 0x0;

/* Minimo que el enlazador EXIGE que quede libre para pila; ver el comentario
 * largo del mismo fichero en la rama "deepsdr". El peor caso medido con anidamiento de
 * interrupciones son 5.464 bytes, y este suelo se pone un poco POR ENCIMA
 * para que avise el enlazador antes que pila.py. */
_Min_Stack_Size = 0x1600;

/*
 * DONDE EMPIEZA LA APLICACION. Un solo sitio, y de aqui lo leen tambien la
 * cabecera de mas abajo y (por -D en el Makefile) el cargador.
 *
 * 0x0800C000 es el principio del sector 3. Tiene que ser frontera de sector
 * porque el cargador borra por sectores y no puede borrarse a si mismo; y
 * tiene que estar alineado a 512 como minimo por el VTOR.
 */
_app_base = 0x0800C000;

MEMORY
{
  RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 192K
  /*
   * 475.136 = 0x08080000 - 0x0800C000. Toda la flash menos el cargador.
   * Ni firma en medio, ni tope de 0x50000, ni desvan.
   */
  FLASH  (rx)  : ORIGIN = 0x0800C000, LENGTH = 475136
  TCMRAM (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  /*
   * LA CABECERA DE LA IMAGEN - 16 bytes en _app_base + 0x200.
   *
   * Sustituye a los 8 bytes de firma del gestor de fabrica, y no es el mismo
   * truco con otro nombre: aquella firma era un valor constante en una
   * direccion fija, o sea que una imagen grabada A MEDIAS con esos ocho
   * bytes puestos pasaba la prueba, arrancaba y se estrellaba. Aqui hay
   * longitud y CRC32, asi que una imagen incompleta o corrompida NO pasa.
   *
   *   +0x00  magia     0x52445344  ("DSDR" leido en un volcado)
   *   +0x04  longitud  bytes de la imagen, desde _app_base
   *   +0x08  crc32     de esos bytes, con ESTE campo contado como cero
   *   +0x0C  version   la del firmware, para enseniarla el cargador
   *
   * Los cuatro los deja a cero el compilador (User/cabecera_app.c) y los
   * rellena tools/cabecera.py sobre el .bin ya enlazado, que es el unico
   * momento en que se conocen la longitud y el CRC.
   *
   * POR QUE EN +0x200 Y NO PEGADA AL VECTOR: para que el cargador la
   * encuentre en un sitio fijo sin tener que saber cuanto mide el vector de
   * interrupciones. El vector de este chip ocupa 428 bytes; el ASSERT de
   * abajo es el que se entera si algun dia crece.
   */
  .cabecera _app_base + 0x200 :
  {
    KEEP(*(.cabecera))
    . = ALIGN(4);
  } >FLASH

  ASSERT(SIZEOF(.isr_vector) <= 0x200,
         "El vector de interrupciones ha crecido por encima de 0x200 y pisa la cabecera de imagen")
  ASSERT(SIZEOF(.cabecera) == 16,
         "La cabecera de imagen tiene que medir 16 bytes justos")

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  /*
   * Y aqui ya no hay nada que elegir a mano: las fuentes, las paletas, los
   * mapas de bits y lo que venga caen todos en el mismo sitio que el resto
   * del .rodata. Esto es lo que de verdad desaparece con el mapa contiguo.
   */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH
  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH
  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH


  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  /*
   * DONDE SE VAN 48 kB DE FLASH SIN QUE NADIE LOS USE. 23/09/2026.
   *
   * Aqui viven los buffers que el DSP quiere en la RAM rapida del nucleo:
   * los estados de los filtros, las ventanas de la FFT, las tablas de
   * giro... unos 48 kB. Todos se declaran con la macro TCMRAM_BSS, que
   * ya en su nombre dice lo que quiere ser: BSS, o sea memoria que
   * empieza a cero y que NO ocupa nada en la flash.
   *
   * Pero esta seccion se escribio con "AT> FLASH", que es lo contrario:
   * le dice al enlazador que guarde una COPIA INICIAL en la flash para
   * que alguien la vuelque al arrancar. Resultado: 49.068 bytes de flash
   * ocupados por ceros. Y ademas el arranque NUNCA los volcaba -mirar
   * startup_gd32f450_470.S: copia .data y pone .bss a cero, y de .tcmram
   * no sabia nada-, asi que esos 48 kB no solo sobraban: no los leia
   * nadie. Se comprobo volcando la seccion: 49.068 bytes, 100% ceros.
   *
   * Con (NOLOAD) la seccion sigue reservando su sitio en la RAM del
   * nucleo pero deja de ocupar flash. El arranque ahora SI la pone a
   * cero, que es lo que la macro prometia desde el principio.
   *
   * Lo que se gana: de 11 kB libres a 60 kB.
   */
  .tcmram (NOLOAD) :
  {
    . = ALIGN(4);
    _stcmram = .;
    *(.tcmram)
    *(.tcmram*)
    . = ALIGN(4);
    _etcmram = .;
  } >TCMRAM

  /* Flash realmente ocupada, para la pantalla de informacion. Antes se
   * calculaba con LOADADDR(.tcmram), que ya no existe; esto es lo mismo
   * pero sin depender de que .tcmram tenga copia. */
  _eflash = LOADADDR(.data) + SIZEOF(.data);

  /*
   * UNA VARIABLE QUE SOBREVIVE AL REINICIO - 01/10/2026.
   *
   * NOLOAD y fuera de .bss a proposito: el arranque pone .bss a cero y .data
   * a su valor inicial, asi que ninguna de las dos sirve para pasarle un
   * recado al OTRO lado de un NVIC_SystemReset(). Esta no se toca.
   *
   * Y va DESPUES de .bss, no antes. *** 01/10/2026: la primera version la
   * puso en 0x20000178 y el testigo no sobrevivia. El motivo no es nuestro
   * arranque -que la respeta- sino el GESTOR DE ARRANQUE, que corre en
   * medio: barriendo sus literales, referencia SRAM de 0x20000000 a
   * 0x200074E0, o sea que el 0x20000178 cae dentro de su propio .bss y lo
   * pone a cero antes de saltar a la aplicacion. ***
   *
   * Detras de .bss cae sobre 0x2002F110, muy por encima de donde llega el
   * gestor.
   *
   * La usa dfu_salto.c para el truco del testigo: la fila de Informacion
   * escribe la palabra magica y reinicia; al arrancar, lo PRIMERO que hace
   * main() es mirarla. Asi el salto a la ROM sale de una maquina recien
   * reseteada y no de una con el DMA del audio y la pantalla en marcha.
   */
  .noinit (NOLOAD) :
  {
    . = ALIGN(4);
    *(.noinit)
    *(.noinit*)
    . = ALIGN(4);
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >TCMRAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
