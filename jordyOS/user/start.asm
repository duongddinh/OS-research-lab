BITS 64
SECTION .text.entry
GLOBAL _user_start
EXTERN user_main
_user_start:
    cld
    xor ebp, ebp
    and rsp, -16
    call user_main
    mov rdi, rax
    xor eax, eax                 ; USYS_EXIT
    int 0x80
    ud2
SECTION .note.GNU-stack noalloc noexec nowrite progbits
