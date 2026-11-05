#include "syscalls.h"
#include "../util.h"
extern void app_ape(void);
char ape_command[96];
static unsigned char writable_code[8] = {0xc3};

long user_main(const UserStartup *startup) {
    unsigned short cs; __asm__ volatile("mov %%cs,%0" : "=r"(cs));
    if ((cs & 3) != 3) return 90;
    volatile uint64_t *kernel = (volatile uint64_t *)0x10000;
    volatile uint64_t *peer = (volatile uint64_t *)startup->peer_address;
    switch (startup->mode) {
        case UMODE_APP:
            strncpy(ape_command,startup->command,sizeof ape_command-1);
            app_ape(); return 0;
        case UMODE_HEALTH:
            if (user_call(USYS_IDENTITY,0,0,0,0,0) <= 0) return 91;
            sys_write("USER ring3 health PASS\n"); return 0;
        case UMODE_KERNEL_READ: return (long)*kernel;
        case UMODE_KERNEL_WRITE: *kernel = 0; break;
        case UMODE_PEER_READ: return (long)*peer;
        case UMODE_PEER_WRITE: *peer = 0; break;
        case UMODE_HLT: __asm__ volatile("hlt"); break;
        case UMODE_IO: __asm__ volatile("outb %%al,$0x80" : : "a"(0)); break;
        case UMODE_CR3: { uintptr_t v; __asm__ volatile("mov %%cr3,%0" : "=r"(v)); return v; }
        case UMODE_DIVIDE: __asm__ volatile("xor %%edx,%%edx; mov $1,%%eax; xor %%ecx,%%ecx; div %%ecx":::"rax","rcx","rdx","cc"); break;
        case UMODE_BAD_OPCODE: __asm__ volatile("ud2"); break;
        case UMODE_CODE_WRITE: *(volatile unsigned char *)(uintptr_t)USER_BASE = 0; break;
        case UMODE_DATA_EXEC: ((void (*)(void))(uintptr_t)writable_code)(); break;
        case UMODE_NULL: return *(volatile unsigned char *)(uintptr_t)1;
        case UMODE_GUARD: return *(volatile unsigned char *)(USER_STACK_BOTTOM-1);
        case UMODE_MARKER_WRITE: *(volatile uint64_t *)startup->marker_address = 0xabcddcba; return 0;
        case UMODE_MARKER_READ: return *(volatile uint64_t *)startup->marker_address ? 93 : 0;
        case UMODE_STARTUP_WRITE: *(volatile uint64_t *)(uintptr_t)startup = 1; break;
        case UMODE_DIRECTION_FLAG: {
            long result=USYS_WRITE;
            __asm__ volatile("std; int $0x80; cld" : "+a"(result) : "D"("USER DF-safe syscall PASS\n") : "memory","cc");
            return result;
        }
        case UMODE_FPU_CONTROL: {
            uint32_t control=0x3f80;
            __asm__ volatile("ldmxcsr %0"::"m"(control)); return 0;
        }
        case UMODE_BAD_POINTER: {
            char name[] = "demo.ape";
            if (user_call(USYS_WRITE,0x10000,0,0,0,0) != -1 ||
                user_call(USYS_WRITE,UINT64_MAX,0,0,0,0) != -1 ||
                user_call(USYS_READLINE,USER_BASE,8,0,0,0) != -1 ||
                user_call(USYS_READ_FILE,(uintptr_t)name,0x10000,32,0,0) != -1 ||
                user_call(USYS_READ_FILE,(uintptr_t)name,USER_STACK_TOP-8,32,0,0) != -1 ||
                user_call(USYS_WRITE_FILE,(uintptr_t)name,0x10000,32,0,0) != -1 ||
                user_call(USYS_FILES,0x10000,64,0,0,0) != -1 ||
                user_call(USYS_DELETE_FILE,0x10000,0,0,0,0) != -1 ||
                user_call(USYS_TERMINAL,0x10000,0,0,0,0) != -1 ||
                user_call(USYS_DRAWTEXT,0,0,0x10000,1,0) != -1 ||
                user_call(USYS_WAIT,1001,0,0,0,0) != -1 ||
                user_call(USYS_PAINT,0,0,INT64_MAX,20,1) != -1 ||
                user_call(9999,0,0,0,0,0) != -1) return 92;
            sys_write("USER checked syscall pointers PASS\n"); return 0;
        }
        default: return 94;
    }
    return 95; /* Any attempted forbidden instruction/access must fault first. */
}
