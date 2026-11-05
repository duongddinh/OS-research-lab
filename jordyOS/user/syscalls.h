#ifndef JORDY_USER_SYSCALLS_H
#define JORDY_USER_SYSCALLS_H
#include "abi.h"
long user_call(unsigned id, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e);
void user_wait(unsigned milliseconds);
void user_exit(int status) __attribute__((noreturn));
#endif
