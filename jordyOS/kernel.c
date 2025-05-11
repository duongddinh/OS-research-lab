

#include <stdint.h>
#include <stdbool.h>

static volatile uint16_t *const vga = (uint16_t *)0xB8000;
static uint8_t row = 0, col = 0;
static uint8_t attr = 0x0F;               

static void putc(char c)
{
    if (c == '\n' || col >= 80) { col = 0; row++; }
    if (row >= 25) {                       
        for (int i = 0; i < 24 * 80; ++i) vga[i] = vga[i + 80];
        for (int i = 24 * 80; i < 25 * 80; ++i) vga[i] = (attr << 8) | ' ';
        row = 24;
    }
    if (c != '\n')
        vga[row * 80 + col++] = (attr << 8) | (uint8_t)c;
}

static void puts(const char *s) { while (*s) putc(*s++); }

static void itoa(int32_t n, char *buf)
{
    bool neg = false;
    if (n < 0) { neg = true; n = -n; }

    char tmp[12];
    int i = 0;
    do { tmp[i++] = '0' + (n % 10); n /= 10; } while (n);
    if (neg) tmp[i++] = '-';

    int j = 0;
    while (i--) buf[j++] = tmp[i];
    buf[j] = 0;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0,%1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1,%0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static char get_char(void)
{
    for (;;) {
        if (inb(0x64) & 1) {               
            uint8_t sc = inb(0x60);

            if (sc & 0x80) continue;

            switch (sc) {
                case 0x02: return '1';
                case 0x03: return '2';
                case 0x04: return '3';
                case 0x05: return '4';
                case 0x06: return '5';
                case 0x07: return '6';
                case 0x08: return '7';
                case 0x09: return '8';
                case 0x0A: return '9';
                case 0x0B: return '0';
                case 0x0C: return '-';
                case 0x0D: return '=';
                case 0x1C: return '\n';
                case 0x39: return ' ';
                case 0x1E: return 'a'; case 0x30: return 'b';
                case 0x2E: return 'c'; case 0x20: return 'd';
                case 0x12: return 'e'; case 0x21: return 'f';
                case 0x22: return 'g'; case 0x23: return 'h';
                case 0x17: return 'i'; case 0x24: return 'j';
                case 0x25: return 'k'; case 0x26: return 'l';
                case 0x32: return 'm'; case 0x31: return 'n';
                case 0x18: return 'o'; case 0x19: return 'p';
                case 0x10: return 'q'; case 0x13: return 'r';
                case 0x1F: return 's'; case 0x14: return 't';
                case 0x16: return 'u'; case 0x2F: return 'v';
                case 0x11: return 'w'; case 0x2D: return 'x';
                case 0x15: return 'y'; case 0x2C: return 'z';
                case 0x37: return '*'; case 0x4A: return '-';
                case 0x4E: return '+'; case 0x35: return '/';
                case 0x53: return '.';
            }
        }
    }
}

static void read_line(char *buf, int max)
{
    int len = 0;
    for (;;) {
        char c = get_char();
        if (c == '\n') { putc('\n'); break; }

        if (c == '\b' || c == 0x7F) {         
            if (len) { len--; putc('\b'); putc(' '); putc('\b'); }
        } else if (len < max - 1) {
            buf[len++] = c;
            putc(c);
        }
    }
    buf[len] = 0;
}
static int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

static int32_t atoi(const char *s)
{
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    int32_t n = 0;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
    return neg ? -n : n;
}

static bool parse_line(const char *l, char *op, int32_t *a, int32_t *b)
{
    int i = 0;
    while (*l == ' ') l++;

    /* Copy operator (max 3 chars) */
    while (*l && *l != ' ' && i < 3) op[i++] = *l++;
    op[i] = 0;
    if (!*op) return false;

    while (*l == ' ') l++;
    *a = atoi(l);
    while (*l && *l != ' ') l++;
    while (*l == ' ') l++;
    if (!*l) return false;
    *b = atoi(l);
    return true;
}

static void calc_shell(void)
{
    char line[64];
    puts("jordyOS ready.  Type e.g.  add 3 5 (operation: add|sub|mul|div)\n");

    while (1) {
        puts("> ");
        read_line(line, sizeof(line));

        char op[4]; int32_t a, b, res = 0;
        bool ok = true;

        if (!parse_line(line, op, &a, &b)) {
            puts("Err: syntax. Use  add|sub|mul|div INT INT\n");
            continue;
        }

        if (!strcmp(op, "add")) res = a + b;
        else if (!strcmp(op, "sub")) res = a - b;
        else if (!strcmp(op, "mul")) res = a * b;
        else if (!strcmp(op, "div")) { if (b) res = a / b; else ok = false; }
        else ok = false;

        if (!ok) { puts("Err.\n"); continue; }

        char buf[12];
        itoa(res, buf);
        puts("= "); puts(buf); puts("\n");
    }
}

void kmain(void)
{
    puts("\nWelcome to jordyOS\n");
    calc_shell();

    for (;;) __asm__ volatile ("hlt");
}
