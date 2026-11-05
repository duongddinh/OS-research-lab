; BIOS enters at physical 0x10000 in 16-bit mode. C runs only in long mode.
BITS 16
SECTION .entry
GLOBAL _start
GLOBAL gdt_start
EXTERN kmain, __bss_start, __bss_end
PML4 equ 0x80000
PDPT equ 0x81000
PD equ 0x82000
BOOT_STACK equ 0x90000
_start:
    cli
    cld
    mov ax, 0x1000
    mov ds, ax
    in al, 0x92
    or al, 2
    and al, 0xfe
    out 0x92, al               ; A20
    lgdt [gdt_descriptor - _start]
    mov eax, cr0
    and eax, 0xfffffff3        ; clear EM/TS
    or eax, 0x23              ; PE/MP/NE
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
    mov esp, BOOT_STACK
    ; Check CPUID, MSRs, PAE, FXSR/SSE/SSE2, then architectural long mode.
    pushfd
    pop ecx
    mov eax, ecx
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    test eax, 1 << 21
    jz unsupported
    xor eax, eax
    cpuid
    cmp eax, 1
    jb unsupported
    mov eax, 1
    cpuid
    and edx, (1 << 5) | (1 << 6) | (1 << 24) | (1 << 25) | (1 << 26)
    cmp edx, (1 << 5) | (1 << 6) | (1 << 24) | (1 << 25) | (1 << 26)
    jne unsupported
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb unsupported
    mov eax, 0x80000001
    cpuid
    and edx, (1 << 29) | (1 << 20)
    cmp edx, (1 << 29) | (1 << 20)
    jne unsupported

    ; Three zeroed 4-KiB tables, sixteen supervisor RW 2-MiB leaf mappings.
    ; Identity-map physical 0..32 MiB, including VGA, DMA, kernel and RAM heap.
    mov edi, PML4
    xor eax, eax
    mov ecx, 3 * 4096 / 4
    rep stosd
    mov dword [PML4], PDPT | 3
    mov dword [PDPT], PD | 3
    mov edi, PD
    mov eax, 0x83             ; present, writable, large page; supervisor only
    mov ecx, 16
.map:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .map
    mov eax, cr4
    or eax, (1 << 5) | (1 << 9) | (1 << 10) ; PAE, OSFXSR, OSXMMEXCPT
    mov cr4, eax
    mov eax, PML4
    mov cr3, eax
    mov ecx, 0xc0000080        ; EFER
    rdmsr
    or eax, (1 << 8) | (1 << 11) ; LME + NXE
    wrmsr
    mov eax, cr0
    or eax, (1 << 31) | (1 << 16) ; paging + supervisor write-protect
    mov cr0, eax
    jmp 0x18:long_start

unsupported:
    ; No BIOS calls after protected-mode entry. Report through COM1 and halt.
    mov dx, 0x3f9
    xor al, al
    out dx, al
    mov dx, 0x3fb
    mov al, 0x80
    out dx, al
    mov dx, 0x3f8
    mov al, 1
    out dx, al
    mov dx, 0x3f9
    xor al, al
    out dx, al
    mov dx, 0x3fb
    mov al, 3
    out dx, al
    mov esi, unsupported_message
.character:
    lodsb
    test al, al
    jz .halt
    mov bl, al
.wait:
    mov dx, 0x3fd
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, 0x3f8
    mov al, bl
    out dx, al
    jmp .character
.halt:
    cli
    hlt
    jmp .halt

BITS 64
long_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    mov rsp, BOOT_STACK       ; 16-byte aligned before CALL, no red zone
    xor ebp, ebp
    fninit
    ldmxcsr [rel initial_mxcsr]
    lea rdi, [rel __bss_start]
    lea rcx, [rel __bss_end]
    sub rcx, rdi
    rep stosb
    call kmain
.halt:
    cli
    hlt
    jmp .halt

ALIGN 8
gdt_start:
    dq 0
    dq 0x00cf9a000000ffff      ; 0x08: 32-bit bootstrap code
    dq 0x00cf92000000ffff      ; 0x10: data
    dq 0x00af9a000000ffff      ; 0x18: 64-bit code (L=1, D=0)
    dq 0x00cff2000000ffff      ; 0x23: ring-3 data
    dq 0x00affa000000ffff      ; 0x2b: ring-3 64-bit code
    dq 0, 0                   ; 0x30: 64-bit TSS descriptor, initialized by C
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
ALIGN 4
initial_mxcsr dd 0x1f80
unsupported_message db 'JordyOS requires an x86-64 CPU with SSE2 and NX.', 13, 10, 0
SECTION .note.GNU-stack noalloc noexec nowrite progbits
