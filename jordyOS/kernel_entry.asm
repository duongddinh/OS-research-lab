BITS 16
SECTION .entry
GLOBAL _start
EXTERN kmain, __bss_start, __bss_end
_start:
    cli
    cld
    mov ax, 0x1000
    mov ds, ax
    ; Enable A20 before using the runtime heap above 1 MiB.
    in al, 0x92
    or al, 2
    and al, 0xfe
    out 0x92, al
    lgdt [gdt_descriptor - _start]
    mov eax, cr0
    and eax, 0xfffffffb       ; clear EM
    or eax, 0x23             ; PE and MP
    mov cr0, eax
    jmp dword 0x08:protected_start
BITS 32
protected_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    fninit                   ; Apelang numbers use x87 doubles.
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    call kmain
.hang:
    hlt
    jmp .hang
ALIGN 8
gdt_start:
    dq 0
    dq 0x00cf9a000000ffff
    dq 0x00cf92000000ffff
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
SECTION .note.GNU-stack noalloc noexec nowrite progbits
