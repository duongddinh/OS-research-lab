%ifndef KERNEL_SECTORS
 %error "assemble with -DKERNEL_SECTORS=<number>"
%endif

BITS 16
ORG 0x7C00

start:
    mov [Drive], dl             ; save BIOS drive number

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov bx, 0x0800              ; 0000:0800 – load address
    mov cx, KERNEL_SECTORS      ; CX = sectors remaining

    mov byte [Sec], 2           ; CHS start: cyl 0, head 0, sector 2
    mov byte [Head], 0
    mov byte [Cyl], 0

.next:
    mov dl, [Drive]
    mov ah, 0x02                ; read
    mov al, 1                   ; one sector
    mov ch, [Cyl]
    mov dh, [Head]
    mov cl, [Sec]
    int 0x13
    jc  .read_err

    mov al, '.'
    mov ah, 0x0E
    int 0x10

    add bx, 512

    inc byte [Sec]
    cmp byte [Sec], 19           ; past sector 18?
    jne .done_chs_update

    mov byte [Sec], 1
    inc byte [Head]
    cmp byte [Head], 2           ; past head 1
    jne .done_chs_update

    mov byte [Head], 0
    inc byte [Cyl]               ; next cylinder

.done_chs_update:
    dec cx
    jnz .next

    jmp 0000h:0800h              ; jump to kernel entry

.read_err:                       ; print ERR then hang
    mov si, err
.prt: lodsb
     or  al, al
     jz  $
     mov ah, 0x0E
     int 0x10
     jmp .prt

Drive db 0
Sec   db 0
Head  db 0
Cyl   db 0
err   db 'ERR',0

times 510-($-$$) db 0
dw 0xAA55
