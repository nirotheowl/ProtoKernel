/*
 * arch/arm64/kernel/cache.c
 *
 * ARM64 cache management functions
 */

#include "../include/arch_cache.h"
#include <stdint.h>
#include <stddef.h>

static uint64_t cache_line_size = 0;

// Initialize cache subsystem
void arch_cache_init(void) {
    cache_line_size = arch_cache_get_line_size();
}

// Clean data cache by address range
void arch_cache_clean(void *addr, size_t size) {
    uint64_t start = (uint64_t)addr & ~(cache_line_size - 1);
    uint64_t end = ((uint64_t)addr + size + cache_line_size - 1) & ~(cache_line_size - 1);
    
    for (uint64_t line = start; line < end; line += cache_line_size) {
        __asm__ volatile("dc cvac, %0" : : "r"(line) : "memory");
    }
    
    __asm__ volatile("dsb sy" : : : "memory");
}

// Invalidate data cache by address range
void arch_cache_invalidate(void *addr, size_t size) {
    uint64_t start = (uint64_t)addr & ~(cache_line_size - 1);
    uint64_t end = ((uint64_t)addr + size + cache_line_size - 1) & ~(cache_line_size - 1);
    
    for (uint64_t line = start; line < end; line += cache_line_size) {
        __asm__ volatile("dc ivac, %0" : : "r"(line) : "memory");
    }
    
    __asm__ volatile("dsb sy" : : : "memory");
}

// Clean and invalidate data cache by address range
void arch_cache_flush(void *addr, size_t size) {
    uint64_t start = (uint64_t)addr & ~(cache_line_size - 1);
    uint64_t end = ((uint64_t)addr + size + cache_line_size - 1) & ~(cache_line_size - 1);
    
    for (uint64_t line = start; line < end; line += cache_line_size) {
        __asm__ volatile("dc civac, %0" : : "r"(line) : "memory");
    }
    
    __asm__ volatile("dsb sy" : : : "memory");
}

