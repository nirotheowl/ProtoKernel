/*
 * kernel/include/memory/cpu_cache.h
 *
 * ARM64 cache management functions
 */

#ifndef _CPU_CACHE_H_
#define _CPU_CACHE_H_

#include <stdint.h>

/* Data cache operations */
void dcache_clean_range(uint64_t start, uint64_t size);
void dcache_invalidate_range(uint64_t start, uint64_t size);
void dcache_clean_invalidate_range(uint64_t start, uint64_t size);

/* Instruction cache operations */
void icache_invalidate_all(void);

#endif /* _CPU_CACHE_H_ */