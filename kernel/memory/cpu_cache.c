/*
 * kernel/memory/cpu_cache.c
 *
 * Architecture-independent cache management wrapper
 */

#include <memory/cpu_cache.h>
#include <arch_cache.h>
#include <stddef.h>

void cpu_cache_init(void) {
    arch_cache_init();
}

void cpu_dcache_clean_range(void *addr, size_t size) {
    arch_cache_clean(addr, size);
}

void cpu_dcache_invalidate_range(void *addr, size_t size) {
    arch_cache_invalidate(addr, size);
}

void cpu_dcache_clean_invalidate_range(void *addr, size_t size) {
    arch_cache_flush(addr, size);
}