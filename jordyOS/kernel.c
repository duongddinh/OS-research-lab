#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util.h"
#include "gui.h"

static inline uint8_t inb(uint16_t p) {
  uint8_t r;
  __asm__ volatile("inb %1,%0" : "=a"(r) : "Nd"(p));
  return r;
}

static inline void outb(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0,%1" : : "a"(value), "Nd"(port));
}
static void serial_init(void) {
  outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 1); outb(0x3f9, 0);
  outb(0x3fb, 3); outb(0x3fa, 0xc7); outb(0x3fc, 0x0b);
}
static void serial_putc(char c) {
  if (c == '\n') serial_putc('\r');
  while (!(inb(0x3fd) & 0x20)) __asm__ volatile("pause");
  outb(0x3f8, c);
}

static void clear_screen_internal(void) { gui_clear(); }
static void putc(char c) { serial_putc(c); gui_putc(c); }
static void puts(const char* s) { while (*s) putc(*s++); }

static char poll_ch(void) {
  static const char map[0x3A] = {
      0,   27,  '1', '2',  '3',  '4', '5', '6',  '7', '8', '9', '0',
      '-', '=', 8,   '\t', 'q',  'w', 'e', 'r',  't', 'y', 'u', 'i',
      'o', 'p', '[', ']',  '\n', 0,   'a', 's',  'd', 'f', 'g', 'h',
      'j', 'k', 'l', ';',  '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
      'b', 'n', 'm', ',',  '.',  '/', 0,   '*',  0,   ' '};
  static const char shifted[0x3A] = {
      [2]='!', [3]='@', [4]='#', [5]='$', [6]='%', [7]='^', [8]='&',
      [9]='*', [10]='(', [11]=')', [12]='_', [13]='+', [26]='{', [27]='}',
      [39]=':', [40]='"', [41]='~', [43]='|', [51]='<', [52]='>', [53]='?'};
  static bool left_shift=false, right_shift=false;
  {
    if (inb(0x3fd) & 1) {
      char c=(char)inb(0x3f8); return c=='\r'?'\n':c;
    }
    if (inb(0x64) & 1) {
      uint8_t sc=inb(0x60);
      if (sc==42 || sc==0xaa) { left_shift=sc==42; return 0; }
      if (sc==54 || sc==0xb6) { right_shift=sc==54; return 0; }
      if (sc & 0x80) return 0;
      if (sc==0x4b) return 0x11;
      if (sc==0x4d) return 0x12;
      if (sc==0x48) return 0x13;
      if (sc==0x50) return 0x14;
      if (sc < 0x3A && map[sc]) {
        char c=map[sc];
        if (left_shift || right_shift) {
          if (c>='a' && c<='z') c-=32;
          else if (shifted[sc]) c=shifted[sc];
        }
        return c;
      }
    }
    return 0;
  }
}

static char get_ch(void) {
  for (;;) { char c=poll_ch(); if (c) return c; __asm__ volatile("pause"); }
}

void read_line(char *buf, int max) {
  if (!buf || max <= 0) return;
  int len = 0;
  for (;;) {
    char c = get_ch();
    if (c == '\n') {
      putc('\n');
      break;
    }
    if ((c == 8 || c == 127) && len > 0) {
      len--;
      puts("\b \b");
    } else if (c >= 32 && c < 127 && len < max - 1) {
      buf[len++] = c;
      putc(c);
    }
  }
  buf[len] = 0;
}

size_t strlen(const char *s) {
  size_t i = 0;
  while (s[i]) i++;
  return i;
}
int strcmp(const char *a, const char *b) {
  while (*a && (*a == *b)) {
    a++;
    b++;
  }
  return *(const unsigned char *)a - *(const unsigned char *)b;
}
char *strcpy(char *dst, const char *src) {
  char *orig_dst = dst;
  while ((*dst++ = *src++));
  return orig_dst;
}
char *strncpy(char *dst, const char *src, size_t n) {
  size_t i;
  for (i = 0; i < n && src[i] != '\0'; i++) {
    dst[i] = src[i];
  }
  for (; i < n; i++) {
    dst[i] = '\0';
  }
  return dst;
}
void *memset(void *s, int c, size_t n) {
  unsigned char *p = (unsigned char *)s;
  while (n--) *p++ = (unsigned char)c;
  return s;
}
void *memcpy(void *dest, const void *src, size_t n) {
  char *d = dest;
  const char *s = src;
  while (n--) *d++ = *s++;
  return dest;
}
int32_t atoi(const char *s) {
  bool neg = false;
  if (*s == '-') {
    neg = true;
    s++;
  }
  int32_t v = 0;
  while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
  return neg ? -v : v;
}
void itoa(int32_t n, char *buf) {
  bool neg = n < 0;
  uint32_t magnitude = neg ? 0u - (uint32_t)n : (uint32_t)n;
  char t[12];
  int i = 0;
  do {
    t[i++] = '0' + (magnitude % 10);
    magnitude /= 10;
  } while (magnitude > 0);
  if (i == 0) t[i++] = '0';
  if (neg) t[i++] = '-';
  int j = 0;
  while (i > 0) buf[j++] = t[--i];
  buf[j] = 0;
}

typedef struct {
  char name[MAX_FILENAME_LEN];
  char data[MAX_FILE_SIZE];
  size_t size;
  bool in_use;
} file_t;
static file_t fs_files[MAX_FILES];

void fs_init(void) {
  for (int i = 0; i < MAX_FILES; i++) {
    fs_files[i].in_use = false;
    fs_files[i].name[0] = '\0';
    fs_files[i].size = 0;
    memset(fs_files[i].data, 0, MAX_FILE_SIZE);
  }
}
static int fs_find_file(const char *fn) {
  for (int i = 0; i < MAX_FILES; i++)
    if (fs_files[i].in_use && !strcmp(fs_files[i].name, fn)) return i;
  return -1;
}
static int fs_find_empty_slot(void) {
  for (int i = 0; i < MAX_FILES; i++)
    if (!fs_files[i].in_use) return i;
  return -1;
}

#define MAX_TASKS 6
typedef struct {
  uint32_t *sp;
  void (*entry)(void);
} task_t;
static task_t tasks[MAX_TASKS];
static int cur = -1;
extern void ctx_switch(uint32_t **old_sp_ptr_location, uint32_t *new_sp);

static void yield(void) {
  if (cur == -1) return;
  int p = cur;
  int n = (cur + 1) % MAX_TASKS;
  while (tasks[n].sp == NULL && n != p) n = (n + 1) % MAX_TASKS;
  if (tasks[n].sp == NULL || n == p) return;
  uint32_t **o;
  uint32_t *d;
  if (tasks[p].sp == NULL)
    o = &d;
  else
    o = &tasks[p].sp;
  cur = n;
  ctx_switch(o, tasks[cur].sp);
}
static void task_bootstrap(void) {
  tasks[cur].entry();
  sys_exit_task();
}
static void task_create(void (*fn)(void), uint8_t *s_bot,
                        size_t s_sz) { 
  int si = -1;
  for (int i = 0; i < MAX_TASKS; i++)
    if (tasks[i].sp == NULL &&
        (tasks[i].entry == NULL || tasks[i].entry == fn)) {
      si = i;
      break;
    }
  if (si == -1)
    for (int i = 0; i < MAX_TASKS; i++)
      if (tasks[i].sp == NULL) {
        si = i;
        break;
      }
  if (si != -1) {
    uint32_t *sp = (uint32_t *)(s_bot + s_sz);
    *(--sp) = (uint32_t)task_bootstrap;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    tasks[si].sp = sp;
    tasks[si].entry = fn;
  } else
    puts("E:MAX_TASKS\n");
}

void sys_write(const char *s) { puts(s); }
char sys_getc(void) { return get_ch(); }
char sys_pollc(void) { return poll_ch(); }
void sys_yield(void) { yield(); }
void sys_exit_task(void) {
  if (cur != -1) tasks[cur].sp = NULL;
  yield();
  puts("\nExited task resumed. Halting.\n");
  for (;;) __asm__("hlt");
}
void sys_clear_screen(void) { clear_screen_internal(); }

int sys_list_files(char *ob, int bl) {
  if (!ob || bl <= 0) return -1;
  memset(ob, 0, bl);
  int cp = 0;
  bool ff = true;
  for (int i = 0; i < MAX_FILES; i++) {
    if (fs_files[i].in_use) {
      if (!ff) {
        if (cp < bl - 1)
          ob[cp++] = '\n';
        else
          return cp;
      }
      size_t nl = strlen(fs_files[i].name);
      if (cp + nl < (size_t)bl) {
        strcpy(ob + cp, fs_files[i].name);
        cp += nl;
        ff = false;
      } else
        return cp;
    }
  }
  return cp;
}
int sys_read_file(const char *fn, char *ub, int us) { 
  if (!fn || !ub || us <= 0) return -1;
  int fi = fs_find_file(fn);
  if (fi == -1) return -1;
  if ((size_t)us < fs_files[fi].size) return -2;
  memcpy(ub, fs_files[fi].data, fs_files[fi].size);
  return (int)fs_files[fi].size;
}
int sys_write_file(const char *fn, const char *d, int dl) { 
  if (!fn || !d || dl < 0) return -1;
  if (!fn[0] || strlen(fn) >= MAX_FILENAME_LEN) return -2;
  if (dl > MAX_FILE_SIZE) return -3;
  int fi = fs_find_file(fn);
  if (fi == -1) {
    fi = fs_find_empty_slot();
    if (fi == -1) return -4;
    fs_files[fi].in_use = true;
    strncpy(fs_files[fi].name, fn, MAX_FILENAME_LEN - 1);
    fs_files[fi].name[MAX_FILENAME_LEN - 1] = '\0';
  }
  memcpy(fs_files[fi].data, d, dl);
  fs_files[fi].size = dl;
  return 0;
}
int sys_delete_file(const char *fn) { 
  if (!fn) return -1;
  int fi = fs_find_file(fn);
  if (fi == -1) return -1;
  fs_files[fi].in_use = false;
  fs_files[fi].name[0] = '\0';
  fs_files[fi].size = 0;
  return 0;
}

void app_ape(void);
void ape_seed_files(void);
char ape_command[96];

static void shell(void) {
  char shell_cmd_buffer[96];
  bool desktop_mode=true;
  static uint8_t app_stack[65536] __attribute__((aligned(16)));

  for (;;) {
    if (desktop_mode) {
      int action=gui_desktop();
      gui_terminal(action==0 ? "Calculator / Apelang" : action==1 ? "Editor / Apelang" :
                   action==2 ? "Apelang / REPL" : "JordyOS / Shell");
      if (action==3) { desktop_mode=false; continue; }
      strcpy(shell_cmd_buffer,action==0 ? "calc" : action==1 ? "edit" : action==4 ? "snake" : action==5 ? "history" : "ape");
    } else {
      sys_write("\nsh> ");
      read_line(shell_cmd_buffer, sizeof(shell_cmd_buffer));
    }

    if (!strcmp(shell_cmd_buffer, "ape") || !memcmp(shell_cmd_buffer, "ape ", 4) ||
        !memcmp(shell_cmd_buffer, "apb ", 4) || !memcmp(shell_cmd_buffer, "compile ", 8)) {
      strcpy(ape_command, shell_cmd_buffer);
      task_create(app_ape, app_stack, sizeof app_stack);
    } else if (!strcmp(shell_cmd_buffer, "ls")) {
      char files[MAX_FILES * (MAX_FILENAME_LEN + 1)];
      sys_list_files(files, sizeof(files)); sys_write(files); sys_write("\n");
    } else if (!strcmp(shell_cmd_buffer, "calc") || !strcmp(shell_cmd_buffer, "edit") || !strcmp(shell_cmd_buffer, "snake") || !strcmp(shell_cmd_buffer, "history")) {
      strcpy(ape_command, !strcmp(shell_cmd_buffer, "calc") ? "ape calc.ape" : !strcmp(shell_cmd_buffer, "edit") ? "ape edit.ape" : !strcmp(shell_cmd_buffer, "snake") ? "ape snake.ape" : "ape history.ape");
      task_create(app_ape, app_stack, sizeof app_stack);
    } else if (!strcmp(shell_cmd_buffer, "desktop")) {
      desktop_mode=true;
    } else if (!strcmp(shell_cmd_buffer, "help")) {
      sys_write("Available commands:\n");
      sys_write("  desktop         - Open graphical launcher\n");
      sys_write("  snake           - Play Snake (Apelang graphics)\n");
      sys_write("  history         - View project development dates\n");
      sys_write("  ape             - Apelang REPL (exit to return)\n");
      sys_write("  ape <file.ape>  - Compile and run source\n");
      sys_write("  compile <file.ape> - Compile to RAM bytecode\n");
      sys_write("  apb <file.apb>  - Run Apelang bytecode\n");
      sys_write("  ls              - List RAM files\n");
      sys_write("  calc    - Run the calculator app\n");
      sys_write("  edit    - Run the text editor app\n");
      sys_write("  clear   - Clear the screen\n");
      sys_write("  help    - Show this help message\n");
    } else if (!strcmp(shell_cmd_buffer, "clear") ||
               !strcmp(shell_cmd_buffer, "cls")) {
      sys_clear_screen();
    } else if (shell_cmd_buffer[0] != 0) {
      sys_write("Unknown command: '");
      sys_write(shell_cmd_buffer);
      sys_write("'. Type 'help'.\n");
    }
    // A single console belongs to the foreground app until it exits.
    for (;;) {
      bool foreground=false;
      for (int i=0; i<MAX_TASKS; i++) if (i!=cur && tasks[i].sp) foreground=true;
      if (!foreground) break;
      sys_yield();
    }
  }
}

void kmain(void) {
  serial_init();
  gui_init();
  clear_screen_internal();
  fs_init();
  ape_seed_files();
  puts("\n*** jordyOS multitask w/ In-Memory FS ***\n");

  static uint8_t sh_stack[4096];
  task_create(shell, sh_stack, sizeof sh_stack);
  cur = -1;
  for (int i = 0; i < MAX_TASKS; ++i)
    if (tasks[i].sp) {
      cur = i;
      break;
    }
  if (cur == -1) {
    puts("Error: No initial task. Halting.\n");
    for (;;) __asm__("hlt");
  }
  uint32_t *dummy_sp_kmain = NULL;
  ctx_switch(&dummy_sp_kmain, tasks[cur].sp);

  puts("\nkmain: ctx_switch from initial task returned. Halting.\n");
  for (;;) __asm__("hlt");
}
