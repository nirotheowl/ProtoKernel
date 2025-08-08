/*
 * kernel/include/memory/kmalloc.h
 *
 * Kernel memory allocator interface
 * Provides variable-size memory allocation built on top of slab allocator
 * 
 */

#ifndef _KMALLOC_H_
#define _KMALLOC_H_

#include <stdint.h>
#include <stddef.h>
#include <memory/slab.h>

// Forward declaration for malloc type (Phase 3)
struct malloc_type;

// Large allocation header structure (for allocations > KMALLOC_MAX_SIZE)
// This is much smaller than before - only 16 bytes
struct kmalloc_large_header {
    size_t size;                    // Allocation size
    uint32_t magic;                 // Magic number for validation
    uint32_t flags;                 // Allocation flags
};

// Magic numbers
#define KMALLOC_LARGE_MAGIC   0xBEEFCAFE
#define KMALLOC_LARGE_FREE    0xDEADBEEF

// Header size for large allocations only
#define KMALLOC_LARGE_HEADER_SIZE sizeof(struct kmalloc_large_header)

// Debug mode settings
#ifdef KMALLOC_DEBUG
#define KMALLOC_REDZONE_SIZE  16    // Guard bytes before/after allocation
#define KMALLOC_REDZONE_MAGIC 0xDE  // Fill pattern for red zones
#else
#define KMALLOC_REDZONE_SIZE  0
#define KMALLOC_REDZONE_MAGIC 0     // Not used when debug is off
#endif

// Core allocation functions
void *kmalloc(size_t size, int flags);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size, int flags);
void *kcalloc(size_t nmemb, size_t size, int flags);

// Typed allocation functions (stubs for Phase 3)
void *kmalloc_type(size_t size, struct malloc_type *type, int flags);
void kfree_type(void *ptr, struct malloc_type *type);

// Initialization
void kmalloc_init(void);

// Statistics and debugging
struct kmalloc_stats {
    uint64_t total_allocs;          // Total allocations
    uint64_t total_frees;           // Total frees
    uint64_t active_allocs;         // Current active allocations
    uint64_t total_bytes;           // Total bytes allocated
    uint64_t active_bytes;          // Currently allocated bytes
    uint64_t large_allocs;          // Number of large allocations
    uint64_t large_bytes;           // Bytes in large allocations
    uint64_t failed_allocs;         // Failed allocation attempts
};

void kmalloc_get_stats(struct kmalloc_stats *stats);
void kmalloc_dump_stats(void);

// Size determination and validation
size_t kmalloc_size(void *ptr);
int kmalloc_validate(void *ptr);

// Internal helper - find which cache owns an address
struct kmem_cache *kmalloc_find_cache(void *ptr);

#endif /* _KMALLOC_H_ */
