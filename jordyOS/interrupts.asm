BITS 64
SECTION .text
EXTERN isolation_interrupt
GLOBAL isolation_enter
; RDI=entry, RSI=user RSP, RDX=private CR3, RCX=read-only startup pointer.
isolation_enter:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rel saved_kernel_rsp], rsp
    fxsave64 [rel saved_kernel_fx]
    mov rax, rdx
    mov cr3, rax
    fninit
    times 8 fldz                ; replace old x87 data before exposing user state
    fninit
    ldmxcsr [rel default_mxcsr]
%assign reg 0
%rep 16
    pxor xmm%+reg, xmm%+reg
%assign reg reg+1
%endrep
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    push qword 0x23
    push rsi
    push qword 2                ; IOPL=0, IF=0, DF=0, reserved bit 1
    push qword 0x2b
    push rdi
    mov rdi, rcx
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq

%macro ISR 1
isr_%1:
%if %1 != 8 && %1 != 10 && %1 != 11 && %1 != 12 && %1 != 13 && %1 != 14 && %1 != 17 && %1 != 21 && %1 != 29 && %1 != 30
    push qword 0
%endif
    push qword %1
    jmp interrupt_common
%endmacro
%assign vector 0
%rep 256
    ISR vector
%assign vector vector+1
%endrep

interrupt_common:
    cld                         ; user DF must never affect kernel copies
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rbx, rsp
    sub rsp, 528
    and rsp, -16
    fxsave64 [rsp]
    fninit
    ldmxcsr [rel default_mxcsr]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov rdi, rbx
    call isolation_interrupt
    test eax, eax
    jnz leave_user
    fxrstor64 [rsp]
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov rsp, rbx
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq
leave_user:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    fxrstor64 [rel saved_kernel_fx]
    mov rsp, [rel saved_kernel_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

SECTION .rodata
GLOBAL isolation_isrs
isolation_isrs:
%assign vector 0
%rep 256
    dq isr_%+vector
%assign vector vector+1
%endrep
default_mxcsr dd 0x1f80
SECTION .bss
ALIGN 16
saved_kernel_fx resb 512
saved_kernel_rsp resq 1
SECTION .note.GNU-stack noalloc noexec nowrite progbits
