// String and memory manipulation functions

#include <stddef.h>

// Import uart functions for debugging
extern void uart_puts(const char *str);
extern void uart_puthex(unsigned long val);

// GCC may generate calls to memcpy even with -ffreestanding
void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    
    // Check for NULL pointers
    if (!dest || !src) {
        return dest;
    }
    
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

// GCC may also generate calls to memset
void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

// GCC may generate calls to memmove
void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    
    if (d < s) {
        // Copy forward
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        // Copy backward
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    
    return dest;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++) != '\0');
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    
    // Copy characters from src to dest
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    
    // Pad with zeros if necessary
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

char* strstr(const char* haystack, const char* needle) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return (char*)haystack;
    }
    
    while (*haystack) {
        if (strncmp(haystack, needle, needle_len) == 0) {
            return (char*)haystack;
        }
        haystack++;
    }
    return NULL;
}

/* Simple number to string conversion helper */
int num_to_str(char *buf, size_t size, unsigned long num) {
    char temp[32];
    int i = 0, j = 0;
    
    if (size == 0) return 0;
    
    /* Handle zero */
    if (num == 0) {
        if (size > 1) {
            buf[0] = '0';
            buf[1] = '\0';
            return 1;
        }
        return 0;
    }
    
    /* Convert to string (backwards) */
    while (num > 0 && i < 31) {
        temp[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    /* Copy to buffer in correct order */
    while (i > 0 && j < size - 1) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
    
    return j;
}

/* Basic snprintf - only supports %zu, %d, and plain strings */
int snprintf(char *buf, size_t size, const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    
    size_t i = 0;
    const char *p = fmt;
    
    while (*p && i < size - 1) {
        if (*p == '%') {
            p++;
            if (*p == 'z' && *(p + 1) == 'u') {
                /* Handle %zu */
                size_t val = __builtin_va_arg(args, size_t);
                int len = num_to_str(buf + i, size - i, val);
                i += len;
                p += 2;
            } else if (*p == 'd') {
                /* Handle %d */
                int val = __builtin_va_arg(args, int);
                if (val < 0 && i < size - 1) {
                    buf[i++] = '-';
                    val = -val;
                }
                int len = num_to_str(buf + i, size - i, (unsigned long)val);
                i += len;
                p++;
            } else if (*p == '%') {
                /* Handle %% */
                buf[i++] = '%';
                p++;
            } else {
                /* Unknown format, just copy */
                buf[i++] = '%';
                if (*p && i < size - 1) {
                    buf[i++] = *p++;
                }
            }
        } else {
            buf[i++] = *p++;
        }
    }
    
    buf[i] = '\0';
    __builtin_va_end(args);
    return i;
}