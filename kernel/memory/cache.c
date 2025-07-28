/*
 * kernel/memory/cache.c
 *
 * ARM64 cache management functions
 */

#include <stdint.h>

/* Get data cache line size */
static inline uint64_t get_dcache_line_size(void) {
    uint64_t ctr;
    __asm__ volatile("mrs %0, CTR_EL0" : "=r"(ctr));
    return 4 << ((ctr >> 16) & 0xF);
}

/* Clean data cache by address range */
void dcache_clean_range(uint64_t start, uint64_t size) {
    uint64_t line_size = get_dcache_line_size();
    uint64_t addr;
    
    for (addr = start & ~(line_size - 1); addr < start + size; addr += line_size) {
        __asm__ volatile("dc cvac, %0" : : "r"(addr) : "memory");
    }
    
    __asm__ volatile("dsb sy" : : : "memory");
}

/* Invalidate data cache by address range */
void dcache_invalidate_range(uint64_t start, uint64_t size) {
    uint64_t line_size = get_dcache_line_size();
    uint64_t addr;
    
    for (addr = start & ~(line_size - 1); addr < start + size; addr += line_size) {
        __asm__ volatile("dc ivac, %0" : : "r"(addr) : "memory");
    }
    
    __asm__ volatile("dsb sy" : : : "memory");
}

/* Clean and invalidate data cache by address range */
void dcache_clean_invalidate_range(uint64_t start, uint64_t size) {
    uint64_t line_size = get_dcache_line_size();
    uint64_t addr;
    
    for (addr = start & ~(line_size - 1); addr < start + size; addr += line_size) {
        __asm__ volatile("dc civac, %0" : : "r"(addr) : "memory");
    }
    
    __asm__ volatile("dsb sy" : : : "memory");
}

/* Invalidate entire instruction cache */
void icache_invalidate_all(void) {
    __asm__ volatile(
        "ic iallu\n"
        "dsb sy\n"
        "isb"
        : : : "memory"
    );
}