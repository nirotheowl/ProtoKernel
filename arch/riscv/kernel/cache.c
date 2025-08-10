/*
 * arch/riscv/kernel/cache.c
 *
 * RISC-V cache management functions
 * 
 * Note: RISC-V doesn't have standardized cache management instructions.
 * Cache operations are typically handled by:
 * 1. Platform-specific instructions (vendor extensions)
 * 2. SBI (Supervisor Binary Interface) calls
 * 3. Cache coherent implementations that don't require explicit management
 * 
 * This implementation provides the required interface with fence instructions
 * for memory ordering, which is sufficient for cache-coherent systems.
 */

#include <arch_cache.h>
#include <stdint.h>
#include <stddef.h>

// RISC-V typically has 64-byte cache lines, but this may vary
#define DEFAULT_CACHE_LINE_SIZE 64

static uint64_t cache_line_size = DEFAULT_CACHE_LINE_SIZE;

// Initialize cache subsystem
void arch_cache_init(void) {
    // TODO: Could query cache line size from device tree or 
    // platform-specific configuration if available
    cache_line_size = DEFAULT_CACHE_LINE_SIZE;
}

// Clean data cache by address range
void arch_cache_clean(void *addr, size_t size) {
    (void)addr;
    (void)size;
    
    // RISC-V fence ensures memory ordering.
    // On cache-coherent systems, this is sufficient.
    // Non-coherent systems would need platform-specific instructions.
    __asm__ volatile("fence rw, rw" ::: "memory");
}

// Invalidate data cache by address range
void arch_cache_invalidate(void *addr, size_t size) {
    (void)addr;
    (void)size;
    
    // RISC-V doesn't have standard cache invalidate instructions.
    // The fence.i instruction synchronizes instruction and data streams,
    // which is the closest standard equivalent.
    __asm__ volatile("fence.i" ::: "memory");
}

// Clean and invalidate data cache by address range
void arch_cache_flush(void *addr, size_t size) {
    (void)addr;
    (void)size;
    
    // Ensure all memory operations complete and instruction stream is synchronized
    __asm__ volatile("fence rw, rw" ::: "memory");
    __asm__ volatile("fence.i" ::: "memory");
}

// Get cache line size
uint64_t arch_cache_get_line_size(void) {
    return cache_line_size;
}