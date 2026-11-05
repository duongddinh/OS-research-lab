#include "syscalls.h"
#include "../util.h"
#include "../gui.h"

long user_call(unsigned id, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    register uintptr_t fifth __asm__("r8") = e;
    long result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"((uintptr_t)id),
        "D"(a), "S"(b), "d"(c), "c"(d), "r"(fifth) : "memory", "cc");
    return result;
}
#define CALL(n,a,b,c,d,e) user_call(n,(uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),(uintptr_t)(d),(uintptr_t)(e))
void user_exit(int status) { CALL(USYS_EXIT,status,0,0,0,0); for (;;) __asm__ volatile("ud2"); }
void sys_exit_task(void) { user_exit(0); }
void sys_write(const char *s) { CALL(USYS_WRITE,s,0,0,0,0); }
void read_line(char *p, int n) { CALL(USYS_READLINE,p,n,0,0,0); }
char sys_getc(void) { return (char)CALL(USYS_GETC,0,0,0,0,0); }
char sys_pollc(void) { return (char)CALL(USYS_POLLC,0,0,0,0,0); }
void sys_yield(void) { CALL(USYS_YIELD,0,0,0,0,0); }
void sys_clear_screen(void) { CALL(USYS_CLEAR,0,0,0,0,0); }
int sys_list_files(char *p,int n) { return CALL(USYS_FILES,p,n,0,0,0); }
int sys_read_file(const char *name,char *p,int n) { return CALL(USYS_READ_FILE,name,p,n,0,0); }
int sys_write_file(const char *name,const char *p,int n) { return CALL(USYS_WRITE_FILE,name,p,n,0,0); }
int sys_delete_file(const char *name) { return CALL(USYS_DELETE_FILE,name,0,0,0,0); }
void gui_paint(int x,int y,int w,int h,int color) { CALL(USYS_PAINT,x,y,w,h,color); }
void gui_drawtext(int x,int y,const char *s,int color) { CALL(USYS_DRAWTEXT,x,y,s,color,0); }
void gui_terminal(const char *s) { CALL(USYS_TERMINAL,s,0,0,0,0); }
void user_wait(unsigned ms) { CALL(USYS_WAIT,ms,0,0,0,0); }
