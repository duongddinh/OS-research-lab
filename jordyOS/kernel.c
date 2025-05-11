#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> 
#include "util.h"    
static volatile uint16_t *const vga = (uint16_t *)0xB8000; 
static uint8_t row = 0, col = 0;                           
static uint8_t attr = 0x0F;                               

static void clear_screen_internal(void) {
    for (int r = 0; r < 25; r++) {
        for (int c = 0; c < 80; c++) {
            vga[r * 80 + c] = (attr << 8) | ' '; 
        }
    }
    row = 0; 
    col = 0;
}

static void putc(char c) {
    if (c == '\b') {
        if (col > 0) col--;
        return;
    }
    if (c == '\n' || col >= 80) { col = 0; row++; }
    if (row >= 25) { // Screen is full, scroll up
        // Move all lines up by one
        for (int i = 0; i < 24 * 80; i++) vga[i] = vga[i + 80];
        // Clear the last line
        for (int i = 24 * 80; i < 25 * 80; i++) vga[i] = (attr << 8) | ' ';
        row = 24; // Reset row to the new last line
    }
    if (c != '\n') vga[row * 80 + col++] = (attr << 8) | (uint8_t)c;
}

static void puts(const char *s) {
    while (*s) {
        if (s[0] == '\b' && s[1] == ' ' && s[2] == '\b') { 
            if (col > 0) {
                col--;
                vga[row * 80 + col] = (attr << 8) | ' ';
            }
            s += 3;
        } else {
            putc(*s++);
        }
    }
}

static inline uint8_t inb(uint16_t p) {
    uint8_t r; __asm__ volatile ("inb %1,%0" : "=a"(r) : "Nd"(p)); return r;
}

static char get_ch(void) {
    static const char map[0x3A] = { 
        0, 27,'1','2','3','4','5','6','7','8','9','0','-','=',8, '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
        'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
    };
    uint8_t sc;
    for (;;) {
        if (inb(0x64) & 0x01) {
            sc = inb(0x60);
            if (sc & 0x80) continue; 
            if (sc < 0x3A && map[sc] != 0) return map[sc];
        }
    }
}

void read_line(char *buf, int max) {
    int len = 0;
    for (;;) {
        char c = get_ch();
        if (c == '\n') { putc('\n'); break; }
        if ((c == 8 || c == 127) && len > 0) { len--; puts("\b \b"); } 
        else if (c >= 32 && c < 127 && len < max - 1) { buf[len++] = c; putc(c); } 
    }
    buf[len] = 0;
}

size_t strlen(const char *s) { size_t i = 0; while (s[i]) i++; return i; }
int strcmp(const char *a, const char *b) { while (*a && (*a == *b)) { a++; b++; } return *(const unsigned char*)a - *(const unsigned char*)b; }
char* strcpy(char *dst, const char *src) { char *orig_dst = dst; while ((*dst++ = *src++)); return orig_dst; }
char* strncpy(char *dst, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    for ( ; i < n; i++) {
        dst[i] = '\0';
    }
    return dst; 
}
void* memset(void *s, int c, size_t n) { unsigned char *p = (unsigned char *)s; while (n--) *p++ = (unsigned char)c; return s; }
void* memcpy(void *dest, const void *src, size_t n) { char *d = dest; const char *s = src; while (n--) *d++ = *s++; return dest; }
int32_t atoi(const char *s) { bool neg = false; if (*s == '-') { neg = true; s++; } int32_t v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return neg ? -v : v; }
void itoa(int32_t n, char *buf) { bool neg = n < 0; if (neg) n = -n; char t[12]; int i = 0; do { t[i++] = '0' + (n % 10); n /= 10; } while (n > 0); if (i == 0) t[i++] = '0'; if (neg) t[i++] = '-'; int j = 0; while (i > 0) buf[j++] = t[--i]; buf[j] = 0; }

#define MAX_FILE_SIZE 512   
typedef struct { char name[MAX_FILENAME_LEN]; char data[MAX_FILE_SIZE]; size_t size; bool in_use; } file_t;
static file_t fs_files[MAX_FILES];

void fs_init(void) { for(int i=0;i<MAX_FILES;i++){fs_files[i].in_use=false;fs_files[i].name[0]='\0';fs_files[i].size=0;memset(fs_files[i].data,0,MAX_FILE_SIZE);}}
static int fs_find_file(const char*fn){for(int i=0;i<MAX_FILES;i++)if(fs_files[i].in_use&&!strcmp(fs_files[i].name,fn))return i; return -1;}
static int fs_find_empty_slot(void){for(int i=0;i<MAX_FILES;i++)if(!fs_files[i].in_use)return i; return -1;}


#define MAX_TASKS 6
typedef struct { uint32_t *sp; void (*entry)(void); } task_t;
static task_t tasks[MAX_TASKS];
static int cur = -1;
extern void ctx_switch(uint32_t **old_sp_ptr_location, uint32_t *new_sp);

static void yield(void) {  if(cur==-1)return;int p=cur;int n=(cur+1)%MAX_TASKS;while(tasks[n].sp==NULL&&n!=p)n=(n+1)%MAX_TASKS;if(tasks[n].sp==NULL||n==p)return;uint32_t**o;uint32_t*d;if(tasks[p].sp==NULL)o=&d;else o=&tasks[p].sp;cur=n;ctx_switch(o,tasks[cur].sp);}
static void task_create(void (*fn)(void), uint8_t *s_bot, size_t s_sz){/* ... */ int si=-1;for(int i=0;i<MAX_TASKS;i++)if(tasks[i].sp==NULL&&(tasks[i].entry==NULL||tasks[i].entry==fn)){si=i;break;}if(si==-1)for(int i=0;i<MAX_TASKS;i++)if(tasks[i].sp==NULL){si=i;break;}if(si!=-1){uint32_t*sp=(uint32_t*)(s_bot+s_sz);*(--sp)=(uint32_t)fn;*(--sp)=0;*(--sp)=0;*(--sp)=0;*(--sp)=0;tasks[si].sp=sp;tasks[si].entry=fn;}else puts("E:MAX_TASKS\n");}


void sys_write(const char *s) { puts(s); }
char sys_getc(void) { return get_ch(); }
void sys_yield(void) { yield(); }
void sys_exit_task(void) {
    if (cur != -1) tasks[cur].sp = NULL;
    yield();
    puts("\nExited task resumed. Halting.\n"); 
    for (;;) __asm__("hlt");
}
void sys_clear_screen(void) { 
    clear_screen_internal();
}

int sys_list_files(char*ob,int bl){/* ... */ if(!ob||bl<=0)return -1;memset(ob,0,bl);int cp=0;bool ff=true;for(int i=0;i<MAX_FILES;i++){if(fs_files[i].in_use){if(!ff){if(cp<bl-1)ob[cp++]='\n';else return cp;}size_t nl=strlen(fs_files[i].name);if(cp+nl<(size_t)bl){strcpy(ob+cp,fs_files[i].name);cp+=nl;ff=false;}else return cp;}}return cp;}
int sys_read_file(const char*fn,char*ub,int us){/* ... */ if(!fn||!ub||us<=0)return -1;int fi=fs_find_file(fn);if(fi==-1)return -1;if((size_t)us<fs_files[fi].size)return -2;memcpy(ub,fs_files[fi].data,fs_files[fi].size);return(int)fs_files[fi].size;}
int sys_write_file(const char*fn,const char*d,int dl){/* ... */ if(!fn||!d||dl<0)return -1;if(strlen(fn)>=MAX_FILENAME_LEN)return -2;if(dl>MAX_FILE_SIZE)return -3;int fi=fs_find_file(fn);if(fi==-1){fi=fs_find_empty_slot();if(fi==-1)return -4;fs_files[fi].in_use=true;strncpy(fs_files[fi].name,fn,MAX_FILENAME_LEN-1);fs_files[fi].name[MAX_FILENAME_LEN-1]='\0';}memcpy(fs_files[fi].data,d,dl);fs_files[fi].size=dl;return 0;}
int sys_delete_file(const char*fn){/* ... */ if(!fn)return -1;int fi=fs_find_file(fn);if(fi==-1)return -1;fs_files[fi].in_use=false;fs_files[fi].name[0]='\0';fs_files[fi].size=0;return 0;}


void app_calc(void); void app_edit(void);

static void shell(void) {
    char shell_cmd_buffer[32]; 

    for (;;) {
        sys_write("\nsh> ");
        read_line(shell_cmd_buffer, sizeof(shell_cmd_buffer));

        if (!strcmp(shell_cmd_buffer, "calc")) {
            bool running = false; for(int i=0; i<MAX_TASKS; ++i) if(tasks[i].entry == app_calc && tasks[i].sp != NULL) running = true;
            if(running) sys_write("calc is already running.\n");
            else { static uint8_t st[1024]; task_create(app_calc, st, sizeof st); }
        } else if (!strcmp(shell_cmd_buffer, "edit")) {
            bool running = false; for(int i=0; i<MAX_TASKS; ++i) if(tasks[i].entry == app_edit && tasks[i].sp != NULL) running = true;
            if(running) sys_write("edit is already running.\n");
            else { static uint8_t st[2048]; task_create(app_edit, st, sizeof st); }
        } else if (!strcmp(shell_cmd_buffer, "help")) {
            sys_write("Available commands:\n");
            sys_write("  calc    - Run the calculator app\n");
            sys_write("  edit    - Run the text editor app\n");
            sys_write("  clear   - Clear the screen\n");
            sys_write("  help    - Show this help message\n");
        } else if (!strcmp(shell_cmd_buffer, "clear") || !strcmp(shell_cmd_buffer, "cls")) { 
            sys_clear_screen();
        } else if (shell_cmd_buffer[0] != 0) { 
            sys_write("Unknown command: '");
            sys_write(shell_cmd_buffer);
            sys_write("'. Type 'help'.\n");
        }
        sys_yield();
    }
}

void kmain(void) {
    fs_init();
    puts("\n*** jordyOS multitask w/ In-Memory FS ***\n");

    static uint8_t sh_stack[1024];
    task_create(shell, sh_stack, sizeof sh_stack);
    cur = -1;
    for(int i=0; i < MAX_TASKS; ++i) if(tasks[i].sp) { cur = i; break; }
    if (cur == -1) {
        puts("Error: No initial task. Halting.\n");
        for (;;) __asm__("hlt");
    }
    uint32_t *dummy_sp_kmain = NULL;
    ctx_switch(&dummy_sp_kmain, tasks[cur].sp);

    puts("\nkmain: ctx_switch from initial task returned. Halting.\n");
    for (;;) __asm__("hlt");
}
