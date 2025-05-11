
ENTRY(_start)

SECTIONS
{
  . = 0x00000800;

  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }

  .bss  : { *(.bss*) }
}
