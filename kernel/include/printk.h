#ifndef PRINTK_H
#define PRINTK_H

#include <stdarg.h>
#include <stdint.h>

// Very minimal printk for your kernel
int printk(const char *fmt, ...);

#endif

