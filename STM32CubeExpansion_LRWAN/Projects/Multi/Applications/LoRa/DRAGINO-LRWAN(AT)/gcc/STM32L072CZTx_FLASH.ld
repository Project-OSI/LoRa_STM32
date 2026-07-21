/*
 * STM32L072CZTx_FLASH.ld
 *
 * GNU ld linker script for the Dragino LSN50v2 (STM32L072CZT6, Cortex-M0+).
 *
 * Written for this GCC build environment; not copied from a vendor project
 * (ST ships no CubeL0 GCC linker script in this tree -- only the
 * STM32L1xx templates under
 * Drivers/CMSIS/Device/ST/STM32L1xx/Source/Templates/gcc/linker/ -- which
 * this file mirrors in structure/section layout for a different device).
 *
 * Memory map (STM32L072CZTx, per ST datasheet/reference manual):
 *   FLASH : 0x08000000, 192K  (main code flash)
 *   RAM   : 0x20000000,  20K  (SRAM)
 * The 6K data-EEPROM (0x08080000) is deliberately NOT modeled as a linker
 * output region: firmware addresses it directly at runtime through
 * DATA_EEPROM_BASE (see CMSIS device header), it is never a linker input
 * section, and there is nothing in this source tree that expects the
 * linker to place data there.
 */

ENTRY(Reset_Handler)

/* Top of RAM: reserve nothing extra here, headroom is enforced by
 * _Min_Heap_Size/_Min_Stack_Size below and checked post-build with
 * arm-none-eabi-size. */
_estack = 0x20000000 + 20K;

/* Minimum heap/stack the linker must be able to fit *in addition* to
 * .data/.bss -- generates a link-time error via the ._user_heap_stack
 * placement below if RAM is oversubscribed, standard CubeL0 convention. */
_Min_Heap_Size  = 0x200;  /* 512 B */
_Min_Stack_Size = 0x400;  /* 1 KiB */

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 192K
  /* rw, not xrw: this firmware never relocates the vector table to SRAM
   * (system_stm32l0xx.c's VECT_TAB_SRAM is left undefined, matching the
   * Keil target) and has no RAM-resident code, so RAM does not need to be
   * executable. Marking it rw also avoids a modern GNU ld "LOAD segment
   * with RWX permissions" warning that an xrw RAM region (the STM32L1xx
   * vendor GCC template's convention, which has the same property) would
   * otherwise trigger for the .data load segment. */
  RAM   (rw) : ORIGIN = 0x20000000, LENGTH = 20K
}

SECTIONS
{
  /* Vector table first in flash, at 0x08000000. */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

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

  /* Symbols the startup code's data/bss init loop expects. */
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

  /* Reserve heap+stack headroom; overflows RAM at link time (not silently
   * at runtime) if .data+.bss leave less than _Min_Heap_Size+_Min_Stack_Size
   * free below _estack. */
  ._user_heap_stack :
  {
    . = ALIGN(4);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(4);
  } >RAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
