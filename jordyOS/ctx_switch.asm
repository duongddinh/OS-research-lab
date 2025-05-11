; ctx_switch(old_sp_ptr, new_sp)
BITS 32
GLOBAL ctx_switch
ctx_switch:
    ; save callee‑saved regs
    push ebp
    push ebx
    push esi
    push edi
    ; *old_sp_ptr = current ESP
    mov  esi, [esp+20]
    mov  [esi], esp
    ; load new ESP
    mov  esp, [esp+24]
    ; restore regs of next task
    pop  edi
    pop  esi
    pop  ebx
    pop  ebp
    ret
