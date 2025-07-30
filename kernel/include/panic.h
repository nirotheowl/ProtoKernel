#ifndef __PANIC_H__
#define __PANIC_H__

#include <stdarg.h>

/* Kernel panic - halt the system with an error message */
void panic(const char *fmt, ...) __attribute__((noreturn));

/* Simple panic with just a string message */
void panic_str(const char *str) __attribute__((noreturn));

#endif /* __PANIC_H__ */