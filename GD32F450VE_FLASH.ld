/*
 * Linker script para GD32F450VET6 (LQFP100)
 *  - Flash: 512 KB  (variante "VE")
 *  - RAM:   192 KB en 0x20000000 (SRAM0+SRAM1+SRAM2) + 64 KB TCM en 0x10000000
 *    (192 + 64 = 256 KB, coincide con los 256 KB de SRAM listados para la
 *    variante VE en el datasheet). El TCM solo es accesible por el bus de
 *    datos del core, no lo uses para buffers que acceda DMA/EXMC.
 */

ENTRY(Reset_Handler)

_estack = 0x10010000; /* CAMBIADO: el bootloader exige que el SP inicial de la app este en el
    rango 0x10000000-0x1001FFFF (TCM RAM), verificado con precision analizando su chequeo
    de validez ((SP<<3)>>20 == 0x800). Usar la SRAM principal (0x2003xxxx) da "APP not
    Programmed" aunque el resto este bien. */
/* MONTON A CERO, 24/09/2026. Este firmware no llama a malloc ni a nada que
 * lo arrastre: comprobado con "arm-none-eabi-nm build/firmware.elf | grep
 * malloc", que no devuelve nada. Reservarle 1 kB era quitarselo a la pila,
 * que es lo unico que de verdad se usa en esa zona y que iba justa.
 *
 * Si algun dia entra malloc, el enlazador NO va a avisar de esto. Lo que
 * avisa es tools/pila.py, que lee _Min_Heap_Size de aqui mismo y descuenta
 * lo que ponga. Asi que si hace falta monton, se sube este numero y el
 * calculo de pila se ajusta solo.
 */
_Min_Heap_Size  = 0x0;

/* Minimo que el enlazador EXIGE que quede libre para pila. No es cuanta pila
 * hay -la pila usa todo el hueco entre 'end' y _estack- sino el suelo por
 * debajo del cual fallar. Subido de 0x800 a 0xC00 (3 kB) el 24/09/2026:
 * el peor caso medido es de 3.080 bytes, asi que si alguien mete otro buffer
 * gordo en el TCM y el hueco baja de 3 kB, falla AQUI, en el enlazador, con
 * el numero delante. La comprobacion fina la hace tools/pila.py; esto es la
 * red de debajo, por si algun dia se salta esa. */
_Min_Stack_Size = 0xC00;

MEMORY
{
  RAM (xrw)   : ORIGIN = 0x20000000, LENGTH = 192K
  /*
   * LA FLASH PARTIDA EN DOS, Y POR QUE. 27/09/2026.
   *
   * Hasta hoy aqui ponia 256K de una pieza, con el razonamiento de que el
   * formato update4.bin mete una firma de 8 bytes en el desplazamiento
   * 0x40000 EXACTO y por tanto el binario tiene que caber por debajo.
   *
   * Desensamblando el gestor de arranque entero resulta que eso era la
   * mitad de la historia. Lo que hace el gestor, en 0x0800A7CA, es:
   *
   *     ldr.w r0, [r8]        ; r8 = 0x08020000, la pila inicial de la app
   *     ubfx  r0, r0, #17, #12
   *     cmp   r0, #0x800      ; -> el SP tiene que estar en el TCM
   *     bne   "APP Not Programmed !"
   *     movs  r2, #8
   *     ldr   r1, =0x08060000
   *     ldr   r0, =0x0800be2c
   *     bl    memcmp          ; 8 bytes, y nada mas
   *     cbz   r0, "Running APP..."   ; iguales -> arranca
   *     ...                          ; distintos -> "Running APP---" y cuelga
   *
   * O sea: la firma NO es un remate que el gestor sepa quitar. Son,
   * literalmente, los 8 bytes que haya en la direccion 0x08060000. Se
   * comprueban en CADA arranque, no solo al actualizar. Y no hay checksum,
   * ni CRC, ni longitud: del resto de la imagen no se mira un solo byte.
   *
   * La receta de update4.bin rellenaba con ceros hasta 0x40000 y pegaba la
   * firma al final porque la imagen era mas pequena. Pero si la imagen es
   * mas grande y lleva esos 8 bytes DENTRO, en su desplazamiento 0x40000,
   * el gestor arranca igual. Y el camino de actualizacion:
   *
   *   - acepta ficheros de hasta 0x50000 (327.680 bytes: "cmp.w r4, #0x50000")
   *   - borra los sectores 5, 6 y 7, o sea 0x08020000..0x0807FFFF
   *   - copia el fichero tal cual a 0x08020000, sin mirar nada
   *
   * Asi que el techo de verdad son 320 kB, no 256, y se llega sin SWD.
   *
   * De ahi las tres regiones: la baja como siempre, ocho bytes clavados
   * para la firma, y un desvan de 64 kB por encima. Lo que sube al desvan
   * se elige a mano en la seccion .arriba, mas abajo.
   *
   * SI ALGO SALE MAL: el gestor monta el USB y busca update4.bin ANTES de
   * llegar a esta comprobacion, asi que una imagen mala deja la radio
   * colgada en "Running APP---" pero el siguiente arranque vuelve a leer el
   * pendrive. Se recupera copiando el update4.bin bueno. Sin SWD.
   */
  FLASH  (rx) : ORIGIN = 0x08020000, LENGTH = 0x40000
  FIRMA  (r)  : ORIGIN = 0x08060000, LENGTH = 8
  /*
   * 65.520 y no 65.528, que es lo que queda hasta 0x50000. Los 8 bytes que
   * faltan son para la receta de update4 del Makefile, que hace
   *
   *     firmware + b'\x00' * (0x40000 - len(firmware)) + firma
   *
   * y con una imagen de mas de 0x40000 ese multiplicador sale NEGATIVO: el
   * relleno se queda vacio -bien, la firma ya va dentro- pero los 8 bytes
   * se pegan igual al final. Son inofensivos (caen en zona ya borrada), pero
   * cuentan para el tamano del fichero. Dejandoles sitio aqui, el limite de
   * 0x50000 no se puede pasar ni en el peor caso, y quien avisa es el
   * ENLAZADOR con los bytes que sobran delante. El Makefile no se puede
   * tocar desde aqui (ver el comentario del -include de avisos.mk), asi que
   * la cuenta se ajusta por este lado. avisos.mk lo vuelve a comprobar sobre
   * el fichero ya generado, por si esta cuenta se queda vieja.
   */
  FLASH2 (rx) : ORIGIN = 0x08060008, LENGTH = 65520
  TCMRAM (rw) : ORIGIN = 0x10000000, LENGTH = 64K
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
   * LOS OCHO BYTES QUE MIRA EL GESTOR DE ARRANQUE.
   *
   * Tienen que estar en 0x08060000 y valer 8F 25 C8 65 59 9C 55 31. El
   * contenido esta en User/firma_app.c; aqui solo se clava la direccion.
   * KEEP() es obligatorio: no hay ni una referencia a esa variable en todo
   * el firmware, asi que --gc-sections se la llevaria por delante y la radio
   * se quedaria colgada en "Running APP---" sin que nadie hubiera tocado una
   * linea de codigo.
   *
   * La region FIRMA mide 8 bytes justos: si algun dia alguien mete otra cosa
   * en la seccion .firma, falla el enlazado en vez de desplazar la firma.
   */
  .firma :
  {
    KEEP(*(.firma))
  } >FIRMA

  /*
   * EL DESVAN: 64 kB por encima de la firma.
   *
   * El enlazador NO derrama solo de una region a otra. Lo que sube aqui se
   * elige a mano, por nombre de seccion de entrada, aprovechando que el
   * proyecto compila con -ffunction-sections -fdata-sections y cada simbolo
   * tiene ya su propia seccion.
   *
   * De momento suben los mapas de bits y los glifos de las fuentes y las
   * paletas de la cascada: 42.052 bytes de tablas que no cambian nunca, que
   * no referencian nada y a las que nadie referencia por direccion fija.
   * Son el candidato perfecto porque son grandes, inertes y autocontenidas.
   * Con eso la region baja pasa de 3.988 bytes libres a 45.640.
   *
   * LA LISTA ES EXPLICITA A PROPOSITO. Con un comodin ".rodata.font_*"
   * quedaria mas corto, pero entonces anadir una fuente nueva la mandaria
   * aqui arriba, y el dia que el desvan se llene el fallo aparecera en una
   * compilacion que no tiene nada que ver. Con la lista a mano, lo que se
   * anada cae en la region BAJA, que es la que se vigila. Si algun dia hace
   * falta rebalancear, basta con borrar una linea de aqui y esa tabla vuelve
   * abajo, o anadir el nombre de otra tabla gorda para subirla.
   *
   * Los comodines que no casan con nada no molestan: hoy font_ui_24b,
   * font_title_60 y font_rain_16 no los usa nadie y --gc-sections ya se los
   * ha llevado antes de llegar aqui.
   *
   * Esta seccion va ANTES que .rodata en este fichero y eso no es casualidad:
   * el enlazador asigna cada seccion de entrada a la PRIMERA de salida que
   * la casa, y el "*(.rodata*)" de .rodata se las tragaria todas.
   */
  .arriba :
  {
    . = ALIGN(4);
    _sarriba = .;
    *(.rodata.font_ui_24b_bitmap)
    *(.rodata.font_ui_18b_bitmap)
    *(.rodata.font_ui_14b_bitmap)
    *(.rodata.font_ui_14_bitmap)
    *(.rodata.font_title_60_bitmap)
    *(.rodata.font_num_44_bitmap)
    *(.rodata.font_num_20_bitmap)
    *(.rodata.font_rain_16_bitmap)
    /* Y las tablas de glifos y rangos de esas mismas fuentes. Se referencian
     * SIEMPRE por puntero desde la estructura de la fuente (que se queda
     * abajo, son 16 bytes), asi que separarlas de su mapa de bits no cambia
     * nada. Las fuentes que --gc-sections ya haya tirado -hoy font_ui_24b,
     * font_title_60 y font_rain_16, que no las usa nadie- no aparecen y el
     * comodin vacio no molesta. */
    *(.rodata.font_ui_24b_glyphs)  *(.rodata.font_ui_24b_ranges)
    *(.rodata.font_ui_18b_glyphs)  *(.rodata.font_ui_18b_ranges)
    *(.rodata.font_ui_14b_glyphs)  *(.rodata.font_ui_14b_ranges)
    *(.rodata.font_ui_14_glyphs)   *(.rodata.font_ui_14_ranges)
    *(.rodata.font_title_60_glyphs) *(.rodata.font_title_60_ranges)
    *(.rodata.font_num_44_glyphs)  *(.rodata.font_num_44_ranges)
    *(.rodata.font_num_20_glyphs)  *(.rodata.font_num_20_ranges)
    *(.rodata.font_rain_16_glyphs) *(.rodata.font_rain_16_ranges)
    /* Las paletas de la cascada: seis tablas de 512 bytes que se leen por
     * indice y no cambian nunca. */
    *(.rodata.k_lut_viridis)
    *(.rodata.k_lut_turbo)
    *(.rodata.k_lut_plasma)
    *(.rodata.k_lut_magma)
    *(.rodata.k_lut_inferno)
    *(.rodata.k_lut_gqrx)
    . = ALIGN(4);
    _earriba = .;
  } >FLASH2

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
