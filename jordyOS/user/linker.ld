ENTRY(_user_start)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
PHDRS {
  text PT_LOAD FLAGS(5);
  rodata PT_LOAD FLAGS(4);
  data PT_LOAD FLAGS(6);
}
SECTIONS {
  . = 0x40000000;
  .text : { KEEP(*(.text.entry)) *(.text*) } :text
  .rodata ALIGN(4096) : { *(.rodata*) } :rodata
  .data ALIGN(4096) : { *(.data*) } :data
  .bss ALIGN(4096) (NOLOAD) : { *(.bss*) *(COMMON) } :data
  ASSERT(. < 0x40800000, "User image exceeds private 8 MiB region")
  /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
