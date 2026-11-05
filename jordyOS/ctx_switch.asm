; SysV AMD64: ctx_switch(old_sp_ptr in RDI, new_sp in RSI).
; Cooperative CALL boundary: save the six callee-saved integer registers.
BITS 64
GLOBAL ctx_switch
ctx_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, rsi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
SECTION .note.GNU-stack noalloc noexec nowrite progbits
