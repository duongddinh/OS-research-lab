#ifndef JORDY_ISOLATION_H
#define JORDY_ISOLATION_H
#include <stdint.h>
void isolation_init(void);
void isolation_command(const char *arguments);
typedef struct {
    uint64_t r15,r14,r13,r12,r11,r10,r9,r8,rsi,rdi,rbp,rdx,rcx,rbx,rax;
    uint64_t vector,error,rip,cs,rflags,rsp,ss;
} InterruptFrame;
int isolation_interrupt(InterruptFrame *frame);
#endif
