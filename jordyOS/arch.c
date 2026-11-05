#include "arch.h"
#include "util.h"
#include "ape/ape_platform.h"

#if !defined(__x86_64__) || __SIZEOF_POINTER__ != 8 || __SIZEOF_LONG__ != 8
#error "JordyOS requires the x86-64 LP64 ABI"
#endif
static uint64_t cr0(void) { uint64_t v; __asm__ volatile("mov %%cr0,%0" : "=r"(v)); return v; }
static uint64_t cr3(void) { uint64_t v; __asm__ volatile("mov %%cr3,%0" : "=r"(v)); return v; }
static uint64_t cr4(void) { uint64_t v; __asm__ volatile("mov %%cr4,%0" : "=r"(v)); return v; }
static uint64_t efer(void) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000080u));
    return (uint64_t)high << 32 | low;
}
static unsigned selector(void) { uint16_t cs; __asm__ volatile("mov %%cs,%0" : "=r"(cs)); return cs; }
bool arch_ready(void) {
    return selector() == 0x18 && (efer() & 0x500) == 0x500 &&
        (cr0() & 0x80010001u) == 0x80010001u && (cr4() & 0x620) == 0x620 && cr3() == 0x80000;
}
__attribute__((noinline)) static double add(double left, double right) { return left + right; }
void arch_report(void) {
    volatile double input[2] = {1.25, 2.5};
    double sum = add(input[0], input[1]);
    char text[96];
    sprintf(text, "ABI probe: %g %d %s %lu", sum, 42, "ok", 4294967297ul);
    bool abi = !strcmp(text, "ABI probe: 3.75 42 ok 4294967297");
    void *allocation = malloc(17);
    bool aligned = allocation && ((uintptr_t)allocation & 15) == 0;
    free(allocation);
    sys_write("Architecture: x86-64; pointers=64 bits; long=64 bits\n");
    sys_write(arch_ready() ? "CPU: long mode active; CS=0x18; EFER.LMA=1\n" : "CPU: FAILED long-mode validation\n");
    sys_write("Paging: enabled; CR3=0x80000; shared 32 MiB identity map\n");
    sys_write(abi && aligned ? "ABI: PASS (SSE2, mixed varargs, 64-bit integers, heap alignment)\n" : "ABI: FAILED\n");
    sys_write("Isolation: shared ring 0; no per-process address spaces\n");
}
