/*
 * arch/riscv/include/arch_cache.h
 * 
 * RISC-V cache operations (stub for initial testing)
 */

#ifndef _ARCH_CACHE_H_
#define _ARCH_CACHE_H_

#include <stddef.h>
#include <stdint.h>

// RISC-V cache operations
void arch_cache_init(void);
void arch_cache_clean(void *addr, size_t size);
void arch_cache_invalidate(void *addr, size_t size);
void arch_cache_flush(void *addr, size_t size);
uint64_t arch_cache_get_line_size(void);

#endif /* _ARCH_CACHE_H_ */