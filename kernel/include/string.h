#ifndef STRING_H
#define STRING_H

#include <stddef.h>

static inline void* memset(void* s, int c, size_t n) {
    unsigned char* p = s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

#endif