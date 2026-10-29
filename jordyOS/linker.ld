ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { KEEP(*(.entry)) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  ASSERT(. < 0x80000, "Kernel load image overlaps stack/BIOS area")
  .bss 0x100000 (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; }
  /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
