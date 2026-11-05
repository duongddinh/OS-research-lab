ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
SECTIONS {
  . = 0x10000;
  .text : { KEEP(*(.entry)) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  ASSERT(. < 0x80000, "Kernel load image overlaps bootstrap page tables")
  .bss 0x100000 (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; }
  ASSERT(__bss_end <= 0x02000000, "Kernel BSS exceeds 32 MiB bootstrap identity map")
  /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
