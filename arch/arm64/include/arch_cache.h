/*
 * arch/arm64/include/arch_cache.h
 *
 * ARM64 cache operations
 */

#ifndef _ARM64_ARCH_CACHE_H_
#define _ARM64_ARCH_CACHE_H_

#include <stdint.h>
#include <stddef.h>

/* Architecture must provide these functions */
void arch_cache_init(void);
void arch_cache_clean(void *addr, size_t size);
void arch_cache_invalidate(void *addr, size_t size);
void arch_cache_flush(void *addr, size_t size);

/* Get cache line size from CTR_EL0 */
static inline uint64_t arch_cache_get_line_size(void) {
    uint64_t ctr;
    __asm__ volatile("mrs %0, CTR_EL0" : "=r"(ctr));
    return 4 << ((ctr >> 16) & 0xF);
}

#endif /* _ARM64_ARCH_CACHE_H_ */