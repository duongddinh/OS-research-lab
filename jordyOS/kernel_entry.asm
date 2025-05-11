; Build with: nasm -f elf32 kernel_entry.asm -o kernel_entry.o
BITS 16
GLOBAL _start

_start:
    cli
    lgdt [gdt_descriptor]   ; 32‑bit flat GDT below
    mov eax, cr0
    or  eax, 1              ; PE=1, enable protected mode
    mov cr0, eax
    jmp 08h:protected_start ; far jump flushes pipeline

BITS 32
protected_start:
    ; Update segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00        ; simple stack (just below 640k)

    extern kmain
    call kmain

halt: hlt
      jmp halt

gdt_start:
    dd 0,0                  ; null
gdt_code:                   ; 0x08
    dw 0xFFFF, 0x0000, 0x9A00, 0x00CF
gdt_data:                   ; 0x10
    dw 0xFFFF, 0x0000, 0x9200, 0x00CF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
