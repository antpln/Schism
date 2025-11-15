/* linker.ld */
ENTRY(_start)

PHDRS
{
  text   PT_LOAD FLAGS(5); /* R-X */
  rodata PT_LOAD FLAGS(4); /* R-- */
  data   PT_LOAD FLAGS(6); /* RW- */
}

SECTIONS
{
  . = 0x40000000;              /* QEMU will jump to entry; absolute LMA here is fine */
  __text_start = .;        /* Mark the start of the .text section */
  .text : ALIGN(0x1000) { /* Section for code */
    KEEP(*(.text._start))
    *(.text .text.*)
  } :text
  __text_end = .;          /* Mark the end of the .text section */

  . = ALIGN(0x1000);       /* Guard page between .text and .rodata */
  . += 0x1000;

  __rodata_start = .;      /* Mark the start of the .rodata section */
  .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) } :rodata
  __rodata_end = .;

  . = ALIGN(0x1000);       /* Guard page between .rodata and .data */
  . += 0x1000;

  __data_start = .;
  .data : ALIGN(0x1000) { *(.data .data.*) } :data
  __data_end = .;

  . = ALIGN(0x1000);       /* Guard page between .data and .bss */
  . += 0x1000;

  __bss_start = .;
  .bss (NOLOAD) : ALIGN(0x1000) {
    *(.bss .bss.* COMMON)
  } :data
  __bss_end = .;

  . = ALIGN(0x1000);       /* Guard page below the EL2 stack */
  __stack_guard = .;
  . += 0x1000;
  __stack_bottom = .;
  . += 0x8000;             /* 32 KiB EL2 stack */
  __stack_top = .;
}
