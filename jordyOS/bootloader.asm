; Build: nasm -f bin -DKERNEL_SECTORS=<n> bootloader.asm -o bootloader.bin
%ifndef KERNEL_SECTORS
 %error "assemble with -DKERNEL_SECTORS=<number>"
%endif

BITS 16
ORG 0x7C00

start:
    mov  [Drive], dl          ; save boot drive #
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00

    mov  bx, 0x0800           ; load addr
    mov  cx, KERNEL_SECTORS
    mov  byte [Sec], 2
    mov  byte [Head], 0
    mov  byte [Cyl],  0

.next:
    mov  dl, [Drive]
    mov  ch, [Cyl]
    mov  dh, [Head]
    mov  cl, [Sec]
    mov  ah, 2
    mov  al, 1
    int  0x13
    jc   .err

    ; dot for progress
    mov  al, '.'
    mov  ah, 0x0E
    int  0x10

    add  bx, 512
    inc  byte [Sec]
    cmp  byte [Sec], 19
    jne  .adv_ok
    mov  byte [Sec], 1
    inc  byte [Head]
    cmp  byte [Head], 2
    jne  .adv_ok
    mov  byte [Head], 0
    inc  byte [Cyl]
.adv_ok:
    dec  cx
    jnz  .next

    jmp  0000h:0800h          ; jump to kernel

.err:
    mov si, msg
.pr: lodsb
     or  al, al
     jz  $
     mov ah,0x0E
     int 0x10
     jmp .pr

Drive db 0
Sec   db 0
Head  db 0
Cyl   db 0
msg   db 'ERR',0
times 510-($-$$) db 0
dw 0xAA55
