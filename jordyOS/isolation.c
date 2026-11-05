#include "isolation.h"
#include "user/abi.h"
#include "user/image.h"
#include "util.h"
#include "gui.h"
#include "net/network.h"
#include "ape/ape_platform.h"

#define PAGE 4096ul
#define P 1ul
#define W 2ul
#define U 4ul
#define NX (1ul << 63)
#define SLOTS 2
#define MARKER (USER_BASE + USER_IMAGE_LIMIT - PAGE)
typedef struct {
    uint64_t pml4[512], pdpt[512], pd[512], pt[5][512];
} __attribute__((aligned(4096))) PageTables;
typedef struct {
    PageTables tables;
    unsigned char memory[USER_IMAGE_LIMIT] __attribute__((aligned(4096)));
    unsigned char startup[PAGE] __attribute__((aligned(4096)));
    unsigned char stack[USER_STACK_SIZE] __attribute__((aligned(4096)));
} Process;
static Process processes[SLOTS] __attribute__((aligned(4096)));
static unsigned char entry_stack[65536] __attribute__((aligned(16)));
static unsigned char fault_stack[8192] __attribute__((aligned(16)));
static unsigned char io_buffer[MAX_FILE_SIZE];
static Process *active;
static unsigned next_slot, pid, completed, faults, rejected;
static uint64_t last_vector, last_error, last_address;
static long last_status;
static bool ready, faulted;
extern uint64_t gdt_start[], isolation_isrs[];
extern void isolation_enter(uintptr_t entry, uintptr_t stack, uintptr_t cr3, uintptr_t startup);
typedef struct {
    uint32_t reserved0;
    uint64_t rsp[3], reserved1, ist[7], reserved2;
    uint16_t reserved3, iomap;
} __attribute__((packed)) Tss;
typedef struct {
    uint16_t low, selector;
    uint8_t ist, type;
    uint16_t middle;
    uint32_t high, reserved;
} __attribute__((packed)) Gate;
static Tss tss;
static Gate idt[256] __attribute__((aligned(16)));
typedef char FrameMustMatchAssembly[(sizeof(InterruptFrame)==176) ? 1 : -1];
typedef char TssMustBe104[(sizeof(Tss)==104) ? 1 : -1];
static uintptr_t current_cr3(void) { uintptr_t value; __asm__ volatile("mov %%cr3,%0":"=r"(value)); return value; }
static void switch_cr3(uintptr_t value) { __asm__ volatile("mov %0,%%cr3"::"r"(value):"memory"); }
static void hex(uint64_t value) {
    char text[19]="0x0000000000000000";
    for (unsigned i=0;i<16;i++) { text[17-i]="0123456789abcdef"[value & 15]; value >>= 4; }
    sys_write(text);
}
void isolation_init(void) {
    tss.rsp[0]=(uintptr_t)entry_stack+sizeof entry_stack;
    tss.ist[0]=(uintptr_t)fault_stack+sizeof fault_stack;
    tss.iomap=sizeof tss; /* bitmap beyond TSS limit denies all user port I/O */
    uintptr_t base=(uintptr_t)&tss; uint64_t limit=sizeof tss-1;
    gdt_start[6]=(limit & 0xffff) | (base & 0xffff)<<16 | ((base>>16)&255)<<32 |
        (uint64_t)0x89<<40 | ((limit>>16)&15)<<48 | ((base>>24)&255)<<56;
    gdt_start[7]=base>>32;
    uint16_t selector=0x30;
    __asm__ volatile("ltr %0"::"r"(selector):"memory");
    for (unsigned i=0;i<256;i++) {
        uintptr_t address=isolation_isrs[i];
        idt[i]=(Gate){address & 0xffff,0x18,i==8 ? 1 : 0,i==128 ? 0xee : 0x8e,
                      address>>16,address>>32,0};
    }
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) descriptor={sizeof idt-1,(uintptr_t)idt};
    __asm__ volatile("lidt %0"::"m"(descriptor):"memory");
    ready=true;
}
static void map_page(Process *p, uintptr_t address, uintptr_t physical, uint64_t flags) {
    uintptr_t offset=address-USER_BASE;
    p->tables.pt[offset>>21][offset>>12 & 511]=physical | P | U | flags;
}
static void prepare(Process *p, unsigned mode, const char *command) {
    memset(p,0,sizeof *p);
    p->tables.pml4[0]=(uintptr_t)p->tables.pdpt | P | W | U;
    p->tables.pdpt[0]=0x82000 | P | W; /* shared supervisor-only kernel identity map */
    p->tables.pdpt[1]=(uintptr_t)p->tables.pd | P | W | U;
    for (unsigned i=0;i<5;i++) p->tables.pd[i]=(uintptr_t)p->tables.pt[i] | P | W | U;
    for (unsigned s=0;s<sizeof user_segments/sizeof *user_segments;s++) {
        const UserSegment *segment=&user_segments[s];
        memcpy(p->memory+segment->offset,user_image_bytes+segment->payload_offset,segment->file_size);
        uint64_t flags=(segment->flags & 2 ? W : 0) | (segment->flags & 1 ? 0 : NX);
        for (unsigned offset=0;offset<segment->memory_size;offset+=PAGE)
            map_page(p,USER_BASE+segment->offset+offset,(uintptr_t)p->memory+segment->offset+offset,flags);
    }
    map_page(p,MARKER,(uintptr_t)p->memory+USER_IMAGE_LIMIT-PAGE,W|NX);
    map_page(p,USER_STARTUP,(uintptr_t)p->startup,NX);
    for (unsigned offset=0;offset<USER_STACK_SIZE;offset+=PAGE)
        map_page(p,USER_STACK_BOTTOM+offset,(uintptr_t)p->stack+offset,W|NX);
    UserStartup *startup=(UserStartup *)p->startup;
    startup->mode=mode; startup->marker_address=MARKER;
    unsigned slot=(unsigned)(p-processes);
    if (mode==UMODE_PEER_READ || mode==UMODE_PEER_WRITE)
        startup->peer_address=(uintptr_t)processes[slot^1].memory+USER_IMAGE_LIMIT-PAGE;
    if (command) strncpy(startup->command,command,sizeof startup->command-1);
}
static bool user_range(uintptr_t address, size_t size, bool write) {
    if (!size) return true;
    if (!active || address<USER_BASE || address>=USER_STACK_TOP || size>USER_STACK_TOP-address) return false;
    uintptr_t last=address+size-1;
    for (uintptr_t page=address & ~(PAGE-1);page<=last;page+=PAGE) {
        uintptr_t offset=page-USER_BASE;
        uint64_t entry=active->tables.pt[offset>>21][offset>>12 & 511];
        if ((entry & (P|U))!=(P|U) || (write && !(entry & W))) return false;
    }
    return true;
}
static bool user_string(char *out, uintptr_t source, size_t capacity) {
    for (size_t i=0;i<capacity;i++) {
        if (source>UINTPTR_MAX-i || !user_range(source+i,1,false)) return false;
        out[i]=*(const char *)(source+i);
        if (!out[i]) return true;
    }
    return false;
}
static long syscall(InterruptFrame *f) {
    uintptr_t a=f->rdi,b=f->rsi,c=f->rdx,d=f->rcx,e=f->r8;
    char text[2048], name[MAX_FILENAME_LEN];
    switch (f->rax) {
        case USYS_WRITE:
            if (!user_string(text,a,sizeof text)) break;
            sys_write(text); return 0;
        case USYS_READLINE:
            if (!b || b>sizeof text || !user_range(a,b,true)) break;
            read_line(text,(int)b); memcpy((void *)a,text,strlen(text)+1); return 0;
        case USYS_GETC: return (unsigned char)sys_getc();
        case USYS_POLLC: return (unsigned char)sys_pollc();
        case USYS_YIELD: rtl_poll(); return 0;
        case USYS_CLEAR: sys_clear_screen(); return 0;
        case USYS_FILES: {
            if (!b || b>MAX_FILES*(MAX_FILENAME_LEN+1) || !user_range(a,b,true)) break;
            int n=sys_list_files(text,(int)b);
            if (n>=0) memcpy((void *)a,text,(size_t)n+1);
            return n;
        }
        case USYS_READ_FILE: {
            if (c>MAX_FILE_SIZE || !user_string(name,a,sizeof name) || !user_range(b,c,true)) break;
            int n=sys_read_file(name,(char *)io_buffer,(int)c);
            if (n>=0) memcpy((void *)b,io_buffer,(size_t)n);
            return n;
        }
        case USYS_WRITE_FILE:
            if (c>MAX_FILE_SIZE || !user_string(name,a,sizeof name) || !user_range(b,c,false)) break;
            memcpy(io_buffer,(const void *)b,c); return sys_write_file(name,(const char *)io_buffer,(int)c);
        case USYS_DELETE_FILE:
            if (!user_string(name,a,sizeof name)) break;
            return sys_delete_file(name);
        case USYS_PAINT:
            if ((int64_t)a < -320 || (int64_t)a>320 || (int64_t)b < -200 || (int64_t)b>200 ||
                c>640 || d>400 || e>9) break;
            gui_paint((int)a,(int)b,(int)c,(int)d,(int)e); return 0;
        case USYS_DRAWTEXT:
            if ((int64_t)a < -320 || (int64_t)a>320 || (int64_t)b < 0 || b>192 || d>9 ||
                !user_string(text,c,sizeof text)) break;
            gui_drawtext((int)a,(int)b,text,(int)d); return 0;
        case USYS_TERMINAL:
            if (!user_string(text,a,38)) break;
            gui_terminal(text); return 0;
        case USYS_WAIT:
            if (!a || a>1000) break;
            ape_wait((unsigned)a); return 0;
        case USYS_IDENTITY: return pid;
        default: break;
    }
    rejected++; return -1;
}
int isolation_interrupt(InterruptFrame *f) {
    if ((f->cs & 3)!=3 || !active || f->vector==8) {
        sys_write("KERNEL EXCEPTION vector="); net_print_number(f->vector);
        sys_write(" rip="); hex(f->rip); sys_write("\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    if (f->vector==128) {
        if (f->rax==USYS_EXIT) { last_status=(long)f->rdi; return 1; }
        f->rax=(uint64_t)syscall(f); return 0;
    }
    last_vector=f->vector; last_error=f->error;
    if (f->vector==14) __asm__ volatile("mov %%cr2,%0":"=r"(last_address));
    last_status=-1; faulted=true; faults++;
    return 1;
}
static bool run(unsigned mode, const char *command, unsigned slot, bool announce) {
    if (!ready || active || slot>=SLOTS) return false;
    Process *p=&processes[slot]; prepare(p,mode,command);
    uintptr_t kernel_cr3=current_cr3(); active=p; pid++;
    last_vector=last_error=last_address=0; last_status=0; faulted=false;
    isolation_enter(USER_ENTRY,USER_STACK_TOP,(uintptr_t)p->tables.pml4,USER_STARTUP);
    switch_cr3(kernel_cr3); active=NULL; completed++;
    if (faulted && announce) {
        gui_terminal("JordyOS / Shell");
        sys_write("Application stopped: CPU fault vector="); net_print_number(last_vector);
        sys_write(" address="); hex(last_address); sys_write("; kernel survived.\n");
    }
    return !faulted && last_status==0;
}
void app_ape(void) {
    extern char ape_command[96];
    run(UMODE_APP,ape_command,next_slot++ % SLOTS,true);
}
static void check(bool ok, const char *name, unsigned *failed) {
    sys_write(ok ? "PASS " : "FAIL "); sys_write(name); sys_write("\n");
    if (!ok) (*failed)++;
}
void isolation_command(const char *arguments) {
    if (!*arguments || !strcmp(arguments,"status")) {
        sys_write("Application isolation: ring 3; private CR3 per launch\n"
            "User pages: private RX code, read-only data/startup, RW+NX heap/stack\n"
            "Kernel/page tables/devices: supervisor-only; checked int 0x80 services\n"
            "One foreground app; kernel shell, display, RAM files and network are shared services.\n");
        sys_write("processes="); net_print_number(completed); sys_write(" faults="); net_print_number(faults);
        sys_write(" rejected_syscalls="); net_print_number(rejected); sys_write("\n"); return;
    }
    if (strcmp(arguments,"test")) { sys_write("isolation [status|test]\n"); return; }
    unsigned failed=0;
    check(run(UMODE_HEALTH,NULL,0,false),"ring3 execution and syscall return",&failed);
    check(run(UMODE_MARKER_WRITE,NULL,0,false) && run(UMODE_MARKER_READ,NULL,1,false),"same virtual address has private physical memory",&failed);
    const struct {unsigned mode,vector;const char *name;} tests[]={
        {UMODE_KERNEL_READ,14,"kernel read blocked"},{UMODE_KERNEL_WRITE,14,"kernel write blocked"},
        {UMODE_PEER_READ,14,"other process memory read blocked"},{UMODE_PEER_WRITE,14,"other process memory write blocked"},
        {UMODE_HLT,13,"privileged HLT blocked"},{UMODE_IO,13,"direct device I/O blocked"},
        {UMODE_CR3,13,"page-table register access blocked"},{UMODE_CODE_WRITE,14,"code page write blocked"},
        {UMODE_DIVIDE,0,"divide-error fault contained"},{UMODE_BAD_OPCODE,6,"invalid-opcode fault contained"},
        {UMODE_DATA_EXEC,14,"data execution blocked by NX"},{UMODE_NULL,14,"null access blocked"},
        {UMODE_STARTUP_WRITE,14,"read-only startup page write blocked"},
        {UMODE_GUARD,14,"stack guard blocked"},
    };
    for (unsigned i=0;i<sizeof tests/sizeof *tests;i++) {
        run(tests[i].mode,NULL,1,false);
        bool ok=last_vector==tests[i].vector && last_status==-1;
        if (last_vector==14) ok=ok && (last_error & 4);
        if (tests[i].mode==UMODE_DATA_EXEC) ok=ok && (last_error & 16);
        check(ok,tests[i].name,&failed);
    }
    check(*(uint64_t *)(processes[0].memory+USER_IMAGE_LIMIT-PAGE)==0xabcddcba,
        "other process memory remains unchanged",&failed);
    check(run(UMODE_DIRECTION_FLAG,NULL,1,false),"kernel entry clears user direction flag",&failed);
    uint32_t before; __asm__ volatile("stmxcsr %0":"=m"(before));
    bool fp=run(UMODE_FPU_CONTROL,NULL,1,false);
    uint32_t control; __asm__ volatile("stmxcsr %0":"=m"(control));
    check(fp && control==before,"user floating-point control cannot contaminate kernel",&failed);
    check(run(UMODE_BAD_POINTER,NULL,0,false),"kernel validates syscall pointers and sizes",&failed);
    check(run(UMODE_HEALTH,NULL,0,false),"healthy app runs after all faults",&failed);
    sys_write(failed ? "ISOLATION TESTS FAILED\n" : "ISOLATION TESTS PASS\n");
}
