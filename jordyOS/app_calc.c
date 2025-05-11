#include <stdint.h>
#include <stdbool.h>
#include "util.h"               


extern void sys_write(const char*);
extern char sys_getc(void);     
extern void sys_yield(void);      
extern void sys_exit_task(void);

#define OP_BUF_SIZE 8 

static int parse(const char *ln, char *op, int32_t *a, int32_t *b) {
    int i = 0;
    while (*ln == ' ' && *ln != 0) ln++;

    while (*ln != ' ' && *ln != 0 && i < OP_BUF_SIZE - 1) {
        op[i++] = *ln++;
    }
    op[i] = 0;
    if (i == 0 && !*ln) return 0;
    if (i > 0 && !*ln) return 1; 
    while (*ln == ' ' && *ln != 0) ln++;
    if (!*ln) return 1; 
    *a = atoi(ln); 

    if (*ln == '-') ln++;
    while (*ln >= '0' && *ln <= '9') ln++;

    while (*ln == ' ' && *ln != 0) ln++;
    if (!*ln) return 2;
    *b = atoi(ln); 
    return 3;
}

void app_calc(void) {
    char ln[64];
    char op[OP_BUF_SIZE]; 
    char out[12];        

    sys_write("Calculator App. Operation: add, sub, mul, div. Type 'exit' or 'quit' to close.\n");

    while (1) {
        sys_write("calc> ");
        read_line(ln, sizeof ln);

        int32_t a = 0, b = 0, res = 0;
        bool ok = true;
        int parse_result = parse(ln, op, &a, &b);

        // Check for exit commands first
        if (parse_result == 1 && (!strcmp(op, "exit") || !strcmp(op, "quit"))) {
            sys_write("Exiting calculator...\n");
            sys_exit_task(); 
            return; 
        }

        if (parse_result != 3) {
            sys_write("Syntax: op num1 num2 (e.g. add 5 2) Operation: add, sub, mul, div or 'exit'/'quit'\n");
            continue; 
        }

        if (!strcmp(op, "add")) res = a + b;
        else if (!strcmp(op, "sub")) res = a - b;
        else if (!strcmp(op, "mul")) res = a * b;
        else if (!strcmp(op, "div")) {
            if (b != 0) res = a / b;
            else {
                sys_write("Error: Division by zero.\n");
                ok = false;
            }
        } else {
            sys_write("Unknown operation: '");
            sys_write(op);
            sys_write("'\n");
            ok = false;
        }

        if (ok) {
            itoa(res, out);
            sys_write(out);
            sys_write("\n");
        }
    }
}
