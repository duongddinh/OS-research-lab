; BIOS floppy loader. ES advances one sector at a time, independent of CHS.
BITS 16
ORG 0x7C00
%ifndef KERNEL_SECTORS
%error "KERNEL_SECTORS is required"
%endif
%if KERNEL_SECTORS > 896
%error "Kernel exceeds reserved conventional-memory load area"
%endif
start:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [drive], dl
    ; Graphics mode and ROM font are selected while BIOS services are available.
    mov ax, 0x0013
    int 0x10
    mov ax, 0x1130
    mov bh, 3
    int 0x10
    xor eax, eax
    mov ax, es
    shl eax, 4
    movzx ebx, bp
    add eax, ebx
    mov [0x500], eax
    mov word [remaining], KERNEL_SECTORS
    mov ax, 0x1000
    mov es, ax
.next:
    mov byte [retries], 3
.retry:
    xor bx, bx
    mov dl, [drive]
    mov ch, [cylinder]
    mov dh, [head]
    mov cl, [sector]
    mov ax, 0x0201
    push es
    int 0x13
    pop es
    jnc .loaded
    xor ax, ax
    mov dl, [drive]
    push es
    int 0x13
    pop es
    dec byte [retries]
    jnz .retry
    mov si, message
.error:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0e
    int 0x10
    jmp .error
.halt:
    cli
    hlt
    jmp .halt
.loaded:
    mov ax, es
    add ax, 0x20
    mov es, ax
    inc byte [sector]
    cmp byte [sector], 19
    jne .advance
    mov byte [sector], 1
    xor byte [head], 1
    jnz .advance
    inc byte [cylinder]
.advance:
    dec word [remaining]
    jnz .next
    jmp 0x1000:0
remaining dw 0
drive db 0
sector db 2
head db 0
cylinder db 0
retries db 0
message db 'Disk read failed', 0
times 510-($-$$) db 0
dw 0xAA55
