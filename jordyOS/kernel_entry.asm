; assemble:  nasm -f elf32 kernel_entry.asm -o kernel_entry.o

BITS 16
GLOBAL _start

_start:
    cli                         ; keep IRQs off during mode switch
    lgdt [gdt_descriptor]       ; load 32‑bit flat GDT

    mov  eax, cr0
    or   eax, 1                 ; set PE bit
    mov  cr0, eax
    jmp  08h:protected_start    ; far jump, 32‑bit code segment

BITS 32
protected_start:
    mov ax, 0x10                ; data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00            ; temp 32‑bit stack (below 640k)


    extern kmain
    call  kmain                 ; jump into C kernel

.hang:
    hlt
    jmp  .hang

gdt_start:
    dq 0                        ; null descriptor

gdt_code:                       ; selector 0x08
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00    ; base 0, limit 4GB, code, 32‑bit

gdt_data:                       ; selector 0x10
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00    ; base 0, limit 4GB, data, 32‑bit

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1   ; size‑1
    dd gdt_start                 ; linear address
