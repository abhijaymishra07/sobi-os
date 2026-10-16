#ifndef SOBI_PANIC_H
#define SOBI_PANIC_H

#include <sobi/types.h>
#include <sobi/compiler.h>

void panic_at(const char *file, int line, const char *func, const char *fmt, ...)
    __printf(4, 5) __noreturn;

#define panic(...) panic_at(__FILE__, __LINE__, __func__, __VA_ARGS__)

#endif
