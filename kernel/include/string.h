/*
 * kernel/include/string.h
 *
 * String and memory manipulation function declarations
 */

#ifndef STRING_H
#define STRING_H

#include <stddef.h>

// Memory manipulation functions
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
void* memmove(void* dest, const void* src, size_t n);

// String manipulation functions
size_t strlen(const char* s);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
char* strstr(const char* haystack, const char* needle);
char* strchr(const char* s, int c);

// Utility functions
int num_to_str(char *buf, size_t size, unsigned long num);
int snprintf(char *buf, size_t size, const char *fmt, ...);

#endif